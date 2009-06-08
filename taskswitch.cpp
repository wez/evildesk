/* Copyright (c) 2004-2009 Wez Furlong.
 * This source is provided under terms of the GPLv2.
 * See the file named LICENSE for full details */
#include "wezdeskapi.h"
#include <commctrl.h>
#include <shlobj.h>
#include <vector>
#include <WinSafer.h>
#include <gdiplus.h>
#define STRSAFE_LIB
#include <strsafe.h>
#include "wezdeskres.h"

using namespace std;
using namespace Gdiplus;
static HHOOK keyhook = NULL;

static int PREVIEW_WIDTH	= 200;
static int PREVIEW_HEIGHT	= 150;

static HWND switch_wnd;
static Plugin me;

static HWND original_active;
static vector <HWND> task_list;
static int task_index = -1;
static UINT timer;
static int rows, cols;
static int ts_desk = -1;
static HICON fallback_icon;
static HFONT my_font;
static COLORREF fgcol, shadowcol;

static TCHAR *workspace_label = NULL;
static Image *switcher_bg = NULL;
static WezDeskFuncs *f = NULL;

#define HOVER_DELAY	500
#define TIMER_MS	50

#define MAX_COLS_PER_ROW	8
#define ICON_SIZE 	32
#define ICON_PAD	8

#define CAPTION_TEXT_PAD	56

static void calc_item_rect(int i, RECT *r)
{
	int row, col;

	row = i / cols;
	col = i % cols;

	r->left = (ICON_SIZE/2) + (col * (ICON_SIZE+ICON_PAD));
	r->top = CAPTION_TEXT_PAD + (ICON_SIZE/2) + (row * (ICON_SIZE+ICON_PAD));
	r->bottom = r->top + ICON_SIZE;
	r->right = r->left + ICON_SIZE;
}

static int mouse_hit(POINT pt)
{
	int i;

	for (i = 0; i < task_list.size(); i++) {
		RECT r;
		calc_item_rect(i, &r);
		if (PtInRect(&r, pt))
			return i;
	}
	return -1;
}

static void paint_switch(HDC hdc)
{
	int i;
	HWND item;
	HICON icon = 0;
	RECT r, wndrect, sr;
	TCHAR caption[128];
	int kill_icon;
//	PerWindow *win;
	Graphics G(hdc);

	if (workspace_label == NULL) {
		TCHAR *label;
		TCHAR scratch[256];

		f->LoadString(IDS_TASKSWITCH_WORKSPACE_LABEL, scratch,
				sizeof(scratch)/sizeof(scratch[0]));

		label = f->GetPluginString(NULL,
			TEXT("WorkspaceLabel"),
			scratch);
		int l_size = (lstrlen(label) + 2) * sizeof(TCHAR);
		workspace_label = (TCHAR*)malloc(l_size);
		StringCbPrintf(workspace_label, l_size, TEXT("%s\n"), label);
		free(label);
	}

	GetClientRect(switch_wnd, &wndrect);

	//DrawEdge(hdc, &wndrect, EDGE_RAISED, BF_RECT);
	G.SetInterpolationMode(InterpolationModeHighQualityBicubic);

	if (!switcher_bg) {
		TCHAR *name = f->GetPluginString(me, TEXT("Background.Image"), NULL);
		if (!name) {
			name = f->GetFilePath(TEXT("default.png"));
		}
		if (name) {
			switcher_bg = new Image(name);
			free(name);
		}

		name = f->GetPluginString(me, TEXT("Font"), NULL);
		if (name) {
			my_font = f->LoadFont(hdc, name);
			free(name);
		} else {
			my_font = f->GetStockFont(WEZDESK_FONT_TITLE);
		}

		fgcol = f->GetPluginInt(me, TEXT("Font.fg"), RGB(0xff,0xff,0xff));
		shadowcol = f->GetPluginInt(me, TEXT("Font.fg"), RGB(0x44,0x44,0x44));
	}
	
	G.DrawImage(switcher_bg, 0, 0, RECTWIDTH(wndrect), RECTHEIGHT(wndrect)+4);

	SetBkMode(hdc, TRANSPARENT);
	SelectObject(hdc, my_font);

	for (i = 0; i < task_list.size(); i++) {
		item = task_list[i];

		calc_item_rect(i, &r);

		//		win = get_PerWindow(item, f->GetActiveDesktop()-1);

		icon = f->GetWindowIcon(item, 100);
		if (!icon) {
			icon = fallback_icon;
		}
		kill_icon = 0;

		DrawIconEx(hdc, r.left, r.top, icon, ICON_SIZE, ICON_SIZE, 0, NULL, DI_NORMAL);

		if (kill_icon && icon) {
			DestroyIcon(icon);
		}

		if (task_index == i) {
			StringCbPrintf(caption, sizeof(caption), workspace_label, ts_desk+1);
			TCHAR window_text[128];
			GetWindowText(item, window_text, sizeof(window_text)/sizeof(window_text[0]));
			StringCbCat(caption, sizeof(caption), window_text);

			InflateRect(&r, 3, 3);
			FrameRect(hdc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH)); 
			InflateRect(&r, 1, 1);
			FrameRect(hdc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH)); 

			SIZE cap_size;
			WezDeskThumb *thumb = f->RegisterThumb(switch_wnd, item);
			f->QueryThumbSourceSize(thumb, &cap_size);
			/* the image has already been scaled to fit within the desired memory
			 * constraints.  We need to scale it to fit the preview area now */
			int w, h;
			double aspect;

			w = cap_size.cx;
			h = cap_size.cy;

			if (h == 0) {
				aspect = 1;
			} else {
				aspect = (double)w / (double)h;
			}
			if (aspect > 1.0) {
				/* landscape */
				w = PREVIEW_WIDTH;
				h = w / aspect;

				if (h > PREVIEW_HEIGHT) {
					double factor = (double)h / (double)PREVIEW_HEIGHT;
					w /= factor;
					h = PREVIEW_HEIGHT;
				}
			} else {
				h = PREVIEW_HEIGHT;
				w = h * aspect;

				if (w > PREVIEW_WIDTH) {
					double factor = (double)w / (double)PREVIEW_WIDTH;
					h /= factor;
					w = PREVIEW_WIDTH;
				}
			}

			thumb->dest.left = (RECTWIDTH(wndrect) - w) / 2;
			thumb->dest.top = wndrect.bottom - (ICON_PAD * 2) - PREVIEW_HEIGHT;
			thumb->dest.right = thumb->dest.left + w;
			thumb->dest.bottom = thumb->dest.top + h;

			f->RenderThumb(thumb);
			f->UnregisterThumb(thumb);
		}
	}

	if (task_list.size() == 0) {
		StringCbPrintf(caption, sizeof(caption), workspace_label, ts_desk+1);
	} /* else caption was set in the loop above */
	
	sr = wndrect;
	InflateRect(&sr, -ICON_PAD, -ICON_PAD);

	f->DrawShadowText(hdc, caption, lstrlen(caption), 
		&sr, DT_CENTER|DT_VCENTER|DT_WORDBREAK,
		fgcol, shadowcol,
		4, 4);
}

static BOOL CALLBACK add_to_task_list(HWND hwnd, LPARAM lparam)
{
	if (f->IsAppWindow(hwnd)) {
		DWORD bits;

		bits = f->ChangeDesktopBits(hwnd, 0, 0);
		if ((bits & (1 << ts_desk)) == (1 << ts_desk)) {
			task_list.push_back(hwnd);
		}
	}
	return TRUE;

}

static void build_task_list(int forward, int next_desk = 0)
{
	task_list.clear();

	PREVIEW_WIDTH = f->GetPluginInt(me, TEXT("Preview.Width"), 200);
	PREVIEW_HEIGHT = f->GetPluginInt(me, TEXT("Preview.Height"), 150);

	if (next_desk) {
		ts_desk++;
		if (ts_desk == f->GetPluginInt(me, TEXT("MaximumWorkspaces"), 4))
			ts_desk = 0;
	} else {
		ts_desk = f->GetActiveDesktop()-1;
	}
	
	EnumWindows(add_to_task_list, NULL);

	/* calculate rows and columns */
	cols = task_list.size();

	if (cols > MAX_COLS_PER_ROW) {
		cols = MAX_COLS_PER_ROW;
		rows = task_list.size() / MAX_COLS_PER_ROW;
		if (rows * MAX_COLS_PER_ROW < task_list.size()) {
			rows++;
		}
	} else {
		rows = 1;
	}
	
	int y = (rows + 1) * ICON_SIZE;
	
	task_index = 0;
	if (!next_desk && task_list.size() > 1) {
		if (forward)
			task_index = 1;
		else
			task_index = task_list.size() - 1;
	}

	int height = 
		CAPTION_TEXT_PAD + y + PREVIEW_HEIGHT + (4 * ICON_PAD);
	int width = 
		((MAX_COLS_PER_ROW + 0) * (ICON_SIZE + ICON_PAD)) + (3 * ICON_PAD);

	SetWindowPos(switch_wnd, HWND_TOPMOST,
		(GetSystemMetrics(SM_CXFULLSCREEN) - width) / 2,
		(GetSystemMetrics(SM_CYFULLSCREEN) - height) / 2,
		width,
		height,
		SWP_SHOWWINDOW);

	InvalidateRect(switch_wnd, NULL, TRUE);
	UpdateWindow(switch_wnd);
}

void switcher_activate(BOOL shift)
{
	PostMessage(switch_wnd, WM_USER + WZN_ALT_TAB_ACTIVATE, shift, 0);
}

void switcher_move_forward(void)
{
	PostMessage(switch_wnd, WM_USER + WZN_ALT_TAB_FORWARD, 0, 0);
}

void switcher_move_backward(void)
{
	PostMessage(switch_wnd, WM_USER + WZN_ALT_TAB_BACKWARD, 0, 0);
}

void switcher_change_desk(void)
{
	PostMessage(switch_wnd, WM_USER + WZN_ALT_TAB_DESK, 0, 0);
}

void switcher_cancel(void)
{
	PostMessage(switch_wnd, WM_USER + WZN_ALT_TAB_CANCEL, 0, 0);
}

static void do_select(int task_index)
{
	ShowWindow(switch_wnd, SW_HIDE);

	if (ts_desk != f->GetActiveDesktop()-1) {
		f->SwitchDesktop(ts_desk+1);
	}

	if (task_list.size()) {
		DWORD pid;
		DWORD_PTR res;
		GetWindowThreadProcessId(task_list[task_index], &pid);
		AllowSetForegroundWindow(pid);
		SendMessageTimeout(task_list[task_index], WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 1000, &res);
		f->SwitchToWindow(task_list[task_index], TRUE);
		SetForegroundWindow(task_list[task_index]);
	}
	task_list.clear();
	ts_desk = -1;
}

void switcher_select(void)
{
	PostMessage(switch_wnd, WM_USER + WZN_ALT_TAB_SELECT, 0, 0);
}

static LRESULT CALLBACK switch_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);
			if (IsWindowVisible(hWnd)) {
				paint_switch(hdc);
			}
			EndPaint(hWnd, &ps);
			break;
		}

		case WM_LBUTTONUP:
		{
			POINT pt;
			pt.x = LOWORD(lParam);
			pt.y = HIWORD(lParam);

			int item = mouse_hit(pt);
			if (item >= 0) {
				do_select(item);
			}
			return 0;
		}
		
		case WM_USER + WZN_ALT_TAB_ACTIVATE:
		{
			original_active = GetForegroundWindow();
			build_task_list(wParam ? 0 : 1);
			InvalidateRect(switch_wnd, NULL, TRUE);
			SwitchToThisWindow(switch_wnd, TRUE);
			return 1;
		}
		case WM_USER + WZN_ALT_TAB_FORWARD:
		{
			task_index++;
			if (task_index >= task_list.size())
				task_index = 0;
			InvalidateRect(switch_wnd, NULL, TRUE);
			return 1;
		}
		case WM_USER + WZN_ALT_TAB_BACKWARD:
		{
			task_index--;
			if (task_index < 0) {
				task_index = task_list.size() - 1;
			}
			InvalidateRect(switch_wnd, NULL, TRUE);
			return 1;
		}
		case WM_USER + WZN_ALT_TAB_DESK:
		{
			build_task_list(1, 1);
			return 1;
		}

		case WM_USER + WZN_ALT_TAB_SELECT:
		{
			do_select(task_index);
			return 1;
		}

		case WM_USER + WZN_ALT_TAB_CANCEL:
		{
			ShowWindow(switch_wnd, SW_HIDE);
			return 1;
		}
		case WM_HOTKEY:
			switch (wParam) {
				case 0:
					if (!IsWindowVisible(hWnd)) f->SendNotify(WZN_ALT_TAB_ACTIVATE, 0, 0, 0);
					break;
				case 1:
					if (!IsWindowVisible(hWnd)) f->SendNotify(WZN_ALT_TAB_ACTIVATE, 1, 0, 0);
					break;
			}
			return 1;
				
		case WM_ACTIVATEAPP:
			if (!wParam) {
				return 0;
			}
			break;

		case WM_ACTIVATE:
//			if (LOWORD(wParam) == WA_INACTIVE && !expected) {
//				/* some tray window or tool tip is stealing focus */
//				SetForegroundWindow(hWnd);
//			}
			return 0;

		default:
			return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}

LRESULT CALLBACK KeyProc(INT nCode, WPARAM wParam, LPARAM lParam)
{
	static BOOL shift = FALSE;
	static BOOL alttab = FALSE;
	BOOL handled = FALSE;

	if (nCode == HC_ACTION) {
		KBDLLHOOKSTRUCT *hook = (KBDLLHOOKSTRUCT *)lParam;
		switch (wParam) {
			case WM_SYSKEYDOWN:
				switch (hook->vkCode) {
					case VK_LSHIFT:
					case VK_RSHIFT:
						shift = TRUE;
						break;
					case VK_TAB:
						if (hook->flags & LLKHF_ALTDOWN) {
							if (alttab) {
								/* switcher is active, move focus */
								f->SendNotify(shift ? WZN_ALT_TAB_BACKWARD : WZN_ALT_TAB_FORWARD, 0, 0, 0);
								handled = TRUE;
							} else {
								/* alt-tab pressed. We need to allow the alt-tab hotkey to
								 * find its way to our switcher window, so that we become
								 * the application with input focus.  If we don't do that,
								 * then we wind up with flashing apps instead of actually
								 * switching to them; thereforce, we don't set handled to true
								 * here. */
								alttab = TRUE;
							}
						}
						break;
					case VK_ESCAPE:
						if (alttab) {// && hook->flags & LLKHF_ALTDOWN) {
							/* end the switcher here */
							f->SendNotify(WZN_ALT_TAB_CANCEL, 0, 0, 0);
							alttab = FALSE;
							handled = TRUE;
						}
						break;
				}
				break;
			case WM_KEYUP:
			case WM_SYSKEYUP:
				switch (hook->vkCode) {
					case VK_LMENU:
					case VK_RMENU:
						if (alttab) {
							/* user released alt key; activate the selected item
							 * and end the switcher */
							//selected = TRUE;
							alttab = FALSE;
							f->SendNotify(WZN_ALT_TAB_SELECT, 0, 0, 0);
						}
						break;
					case VK_LSHIFT:
					case VK_RSHIFT:
						shift = FALSE;
						break;
					case VK_RETURN:
						if (alttab) {
							/* alt-enter */
							f->SendNotify(WZN_ALT_TAB_DESK, 0, 0, 0);
						}
						break;
				}
				/* try to ensure that we don't stick the switcher on the display */
				if (alttab && !(hook->flags & LLKHF_ALTDOWN)) {
					/* if we released a key and ALT is not held down and we think it is,
					 * it's not really.  Try to ensure that it isn't */
					alttab = FALSE;
					f->SendNotify(WZN_ALT_TAB_CANCEL, 0, 0, 0);
				}
				break;
		}
	}
	
	return handled ? TRUE : CallNextHookEx(keyhook, nCode, wParam, lParam);
}


static void *initialize(Plugin plugin, WezDeskFuncs *funcs, HWND slit) {
	WNDCLASS wc;

	f = funcs;
	me = plugin;

	fallback_icon = LoadIcon(NULL, IDI_APPLICATION);

	memset(&wc, 0, sizeof(wc));

	wc.lpfnWndProc = switch_proc;
	wc.hInstance = funcs->GetPluginInstance(plugin);
	wc.lpszClassName = TEXT("WezDeskSwitch");
	wc.hbrBackground = (HBRUSH)(1 + COLOR_APPWORKSPACE);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);

	if (!RegisterClass(&wc)) {
		return NULL;
	}
	
	switch_wnd = CreateWindowEx(
		WS_EX_TOOLWINDOW|WS_EX_COMPOSITED|WS_EX_LAYERED,
		wc.lpszClassName,
		NULL,
		WS_POPUP,
		0, 0,
		320, 64,
		NULL, NULL,
		wc.hInstance,
		NULL);

	SetLayeredWindowAttributes(switch_wnd, 0, (90 * 255) / 100, LWA_ALPHA);
	
	/* Despite using a keyboard hook, we need the hotkeys to be able to gain
	 * the ability to change the foreground window */
	RegisterHotKey(switch_wnd, 0, MOD_ALT, VK_TAB);
	RegisterHotKey(switch_wnd, 1, MOD_SHIFT|MOD_ALT, VK_TAB);
	keyhook = SetWindowsHookEx(WH_KEYBOARD_LL, (HOOKPROC)KeyProc, f->GetPluginInstance(plugin), 0);

	return switch_wnd;
}

static int on_notify(Plugin plugin, WezDeskFuncs *funcs, UINT code, UINT secondary, WPARAM wparam, LPARAM lparam)
{
	switch (code) {
		case WZN_ALT_TAB_ACTIVATE:
			switcher_activate(secondary);
			return 1;

		case WZN_ALT_TAB_FORWARD:
			switcher_move_forward();
			return 1;

		case WZN_ALT_TAB_BACKWARD:
			switcher_move_backward();
			return 1;

		case WZN_ALT_TAB_SELECT:
			switcher_select();
			return 1;

		case WZN_ALT_TAB_CANCEL:
			switcher_cancel();
			return 1;

		case WZN_ALT_TAB_DESK:
			switcher_change_desk();
			return 1;
	}
	return 0;
}

static int unload(Plugin plugin, WezDeskFuncs *funcs, int force)
{
	if (keyhook) UnhookWindowsHookEx(keyhook);
	DestroyWindow(switch_wnd);
	if (workspace_label) {
		free(workspace_label);
		workspace_label = NULL;
	}
	if (switcher_bg) {
		delete switcher_bg;
		switcher_bg = NULL;
	}
	return 1;
}


static WezDeskPlugin the_plugin = {
	WEZDESK_API_NUMBER,
	TEXT("Task-switcher"),
	TEXT("Workspace aware alt-tab"),
	TEXT("BSD"),
	TEXT("Wez Furlong <wez@php.net>"),
	TEXT("http://netevil.org/wiki.php?WezDesk"),
	TEXT("0.1"),
	initialize,
	NULL, /* unload ok */
	unload, /* unload */
	NULL, /* veto */
	NULL, /* shell msg */
	NULL, /* tray */
	on_notify
};

GET_PLUGIN(the_plugin);

