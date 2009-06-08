/* Copyright (c) 2004-2009 Wez Furlong.
 * This source is provided under terms of the GPLv2.
 * See the file named LICENSE for full details */
#include "wezdeskapi.h"
#include <docobj.h>
#include <ShlGuid.h>
#include <vector>
#include <commctrl.h>
#define STRSAFE_LIB
#include <strsafe.h>


#define ICON_SIZE	16
#define ICON_PAD	4

using namespace std;
static HWND tray_window = NULL;
static HWND tooltips = NULL;
static HWND balloon = NULL;
static int rows = 1, cols = 1;
static WezDeskFuncs *f;
static Plugin my_plugin;
static UINT_PTR timer;
static int is_vert;
static UINT next_tool_id = 0;

struct tool {
	unsigned use:1;
	unsigned tipvalid:1;
	DWORD deadline;
	TOOLINFO ti;
	TCHAR tip[256];
	NOTIFYICONDATA icon;
};

static vector <struct tool *>tools;
static vector <struct tool *>balloons;

static void calc_item_rect(int i, RECT *r);
static void show_balloon(struct tool *tool);

static struct tool *find_icon(NOTIFYICONDATA *key, vector <struct tool*>::iterator &iter)
{
	struct tool *tool;
	for (iter = tools.begin(); iter < tools.end(); iter++) {
		tool = *iter;
		if (tool->icon.uID == key->uID && tool->icon.hWnd == key->hWnd) {
			return tool;
		}
	}
	return NULL;
}

static struct tool *first_balloon(void)
{
	struct tool *tool;
	vector <struct tool*>::iterator iter;

	iter = balloons.begin();
	if (iter < balloons.end()) {
		tool = *iter;
		return tool;
	}
	return NULL;
}

static struct tool *find_tool(RECT *r, vector <struct tool*> &where = tools)
{
	vector <struct tool*>::iterator iter;
	struct tool *tool;

	for (iter = where.begin(); iter < where.end(); iter++) {
		tool = *iter;

		if (!memcmp(&tool->ti.rect, r, sizeof(RECT))) {
			return tool;
		}
	}
	return NULL;
}

static void recalc(void)
{
	int i, v;
	RECT r, n;
	vector <struct tool*>::iterator iter;
	struct tool *tool;
	static RECT last_rect;
	int visible_icons;
	
	GetWindowRect(tray_window, &r);

	for (visible_icons = 0, i = 0; i < tools.size(); i++) {
		if ((tools[i]->icon.dwState & NIS_HIDDEN) == NIS_HIDDEN)
			continue;
		visible_icons++;
	}

	if (is_vert) {
		cols = 48 / (ICON_SIZE + (2*ICON_PAD));
		rows = visible_icons / cols;
		if (rows * cols < visible_icons) {
			rows++;
		}
	} else {
		rows = 48 / (ICON_SIZE + (2*ICON_PAD));
		cols = visible_icons / rows;
		if (rows * cols < visible_icons) {
			cols++;
		}
	}

//	f->Trace(TEXT("icons=%d, vis=%d rows=%d cols=%d\r\n"), tools.size(), visible_icons, rows, cols);

	n.left = r.left;
	n.top = r.top;

	n.right = n.left + (cols * (ICON_SIZE + (2 * ICON_PAD)));
	n.bottom = n.top + (rows * (ICON_SIZE + (2 * ICON_PAD)));

//	f->Trace(TEXT("icons don't fit, resizing %d,%d %dx%d (%d rows, %d cols)\r\n"), n.left, n.top, n.right - n.left, n.bottom - n.top, rows, cols);
	POINT topleft;
	topleft.x = r.left;
	topleft.y = r.top;
	ScreenToClient(GetParent(tray_window), &topleft);
	n.right = topleft.x + n.right - n.left;
	n.left = topleft.x;
	n.bottom = topleft.y + n.bottom - n.top;
	n.top = topleft.y;
	if (memcmp(&last_rect, &n, sizeof(n))) {
//		f->Trace(TEXT("tray: MoveWindow(%d, %d, %d, %d)\r\n"), n.left, n.top, n.right - n.left, n.bottom - n.top);
		MoveWindow(tray_window, n.left, n.top, n.right - n.left, n.bottom - n.top, TRUE);
		memcpy(&last_rect, &n, sizeof(n));
	}
	
	/* zero the use flag for each tool */
	for (iter = tools.begin(); iter < tools.end(); iter++) {
		tool = *iter;
		tool->use = 0;
	}

	tool = NULL;
		
	for (v = -1, i = 0; i < tools.size(); i++) {
		int visible = (tools[i]->icon.dwState & NIS_HIDDEN) != NIS_HIDDEN;
		int add = 0;

		add = 0;
		if (visible)
			++v;

		if (tools[i]->tipvalid) {
			calc_item_rect(v, &r);

			if (visible && !memcmp(&r, &tools[i]->ti.rect, sizeof(r))) {
				if (tools[i]->icon.uFlags & NIF_TIP) {
					if (lstrcmp(tools[i]->tip, tools[i]->icon.szTip)) {
						StringCbCopy(tools[i]->tip, sizeof(tools[i]->tip), tools[i]->icon.szTip);
						SendMessage(tooltips, TTM_UPDATETIPTEXT, 0, (LPARAM)&tools[i]->ti);
					}
					tools[i]->use = 1;
				}
			} else {
				/* the tool moved or was hidden */
				SendMessage(tooltips, TTM_DELTOOL, 0, (LPARAM)&tools[i]->ti);
				tools[i]->tipvalid = 0;
				add = visible;
			}
		} else if ((tools[i]->icon.uFlags & NIF_TIP) && visible) {
			add = 1;
		}

		if (add) {
			tools[i]->ti.cbSize = TTTOOLINFOW_V2_SIZE; /* problems when using any other size :-/ */
			tools[i]->ti.uFlags = TTF_SUBCLASS;
			tools[i]->ti.hwnd = tray_window;
			tools[i]->ti.uId = next_tool_id++;
			tools[i]->ti.hinst = f->GetPluginInstance(my_plugin);

			calc_item_rect(v, &tools[i]->ti.rect);
			tools[i]->ti.lpszText = tools[i]->tip;
			StringCbCopy(tools[i]->tip, sizeof(tools[i]->tip), tools[i]->icon.szTip);

			SendMessage(tooltips, TTM_ADDTOOL, 0, (LPARAM)&tools[i]->ti);
			tools[i]->use = 1;
			tools[i]->tipvalid = 1;
		}
	}

	/* any tool with 0 use gets killed */
	for (iter = tools.begin(); iter < tools.end(); iter++) {
		tool = *iter;
		if (!tool->use && tool->tipvalid) {
			SendMessage(tooltips, TTM_DELTOOL, 0, (LPARAM)&tool->ti);
			tool->tipvalid = 0;
		}
	}
}

/* calculate coords for the nth icon */
static void calc_item_rect(int i, RECT *r)
{
	int row, col;

	row = i / cols;
	col = i % cols;

	r->left = ICON_PAD + (col * (ICON_SIZE+ICON_PAD));
	r->top = ICON_PAD + (row * (ICON_SIZE+ICON_PAD));
	r->bottom = r->top + ICON_SIZE;
	r->right = r->left + ICON_SIZE;
}

static void paint_tray(HDC hdc)
{
	int i;
	int n;

	for (n = 0, i = 0; i < tools.size(); i++) {
		RECT r;

		if ((tools[i]->icon.dwState & NIS_HIDDEN) == NIS_HIDDEN)
			continue;
			
		calc_item_rect(n, &r);
		DrawIconEx(hdc, r.left, r.top, tools[i]->icon.hIcon, ICON_SIZE, ICON_SIZE, 0, NULL, DI_NORMAL);

		n++;
	}
}

static TCHAR *message_name(UINT uMsg) {
	switch (uMsg) {
#define nameify(x)	case x: return TEXT(#x)
		nameify(WM_LBUTTONDOWN);
		nameify(WM_LBUTTONUP);
		nameify(WM_LBUTTONDBLCLK);
		nameify(WM_RBUTTONDOWN);
		nameify(WM_RBUTTONUP);
		nameify(WM_RBUTTONDBLCLK);
		nameify(WM_MBUTTONDOWN);
		nameify(WM_MBUTTONUP);
		nameify(WM_MBUTTONDBLCLK);
		nameify(WM_XBUTTONDOWN);
		nameify(WM_XBUTTONUP);
		nameify(WM_XBUTTONDBLCLK);
		nameify(WM_MOUSEMOVE);
		default: return TEXT("unknown");
	}
}


static LRESULT CALLBACK tray_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);
			paint_tray(hdc);
			EndPaint(hWnd, &ps);
			break;
		}

//		case WM_ERASEBKGND:
//			return 1;

		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_LBUTTONDBLCLK:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_RBUTTONDBLCLK:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_MBUTTONDBLCLK:
		case WM_XBUTTONDOWN:
		case WM_XBUTTONUP:
		case WM_XBUTTONDBLCLK:
//			f->Trace(TEXT("tray: got mouse button message %08x %s\r\n"), uMsg, message_name(uMsg));
		case WM_MOUSEMOVE:
		{
			POINT pt;
			RECT r;
			int i, v;

			pt.x = LOWORD(lParam);
			pt.y = HIWORD(lParam);

			for (v = 0, i = 0; i < tools.size(); i++) {
				if ((tools[i]->icon.dwState & NIS_HIDDEN) == NIS_HIDDEN)
					continue;
				calc_item_rect(v++, &r);
				if (PtInRect(&r, pt)) {
					/* we hit an icon */
					switch (uMsg) {
						case WM_RBUTTONDOWN:
						case WM_LBUTTONDOWN:
						case WM_MBUTTONDOWN:
						case WM_XBUTTONDOWN:
							/* set the focus to the window associated with the icon */
							SetForegroundWindow(tools[i]->icon.hWnd);
							break;
					}
					/* forward message to the icon window */
//					f->Trace(TEXT("tray: forwarding %s to tool %d %s\r\n"), message_name(uMsg), i, tools[i]->icon.szTip);
					PostMessage(tools[i]->icon.hWnd, tools[i]->icon.uCallbackMessage,
						(WPARAM)tools[i]->icon.uID, (LPARAM)uMsg);
					return 0;
				}
			}
			return DefWindowProc(hWnd, uMsg, wParam, lParam);
		}

		default:
			return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}

static void paint_balloon(HDC hdc)
{
	int i;
	struct tool *tool;
	RECT rcap, rbody, r, cr;

	tool = first_balloon();

	if (tool) {
		memset(&rcap, 0, sizeof(rcap));
		rcap.right = GetSystemMetrics(SM_CXFULLSCREEN) / 3;
		rbody = rcap;

#define TEXT_FLAGS	DT_NOPREFIX|DT_WORDBREAK
#define TEXT_CENTER	0 //DT_CENTER|DT_VCENTER

	//	f->Trace(TEXT("first: dimensions %d,%d,%d,%d\r\n"), rcap.left, rcap.top, rcap.right, rcap.bottom);

		SelectObject(hdc, f->GetStockFont(WEZDESK_FONT_SMALL_TITLE));
		DrawText(hdc, tool->icon.szInfoTitle, lstrlen(tool->icon.szInfoTitle), &rcap, DT_CALCRECT|TEXT_FLAGS);
		
		SelectObject(hdc, f->GetStockFont(WEZDESK_FONT_TOOLTIP));
		DrawText(hdc, tool->icon.szInfo, lstrlen(tool->icon.szInfo), &rbody, DT_CALCRECT|TEXT_FLAGS);

		r.left = 0;
		r.top = 0;
		r.bottom = rcap.bottom + rbody.bottom + 16;
		r.right = (rcap.right > rbody.right ? rcap.right : rbody.right) + 16;

		rcap.left += 4;
		rcap.right += 4;
		rcap.top += 4;
		rcap.bottom += 4;

		rbody.left += 4;
		rbody.right += 4;
		rbody.top = rcap.bottom + 4;
		rbody.bottom += rbody.top;

		RECT workarea;
		SystemParametersInfo(SPI_GETWORKAREA, 0, &workarea, 0);

	//	f->Trace(TEXT("balloon dimensions: %d,%d,%d,%d\r\n"), r.left, r.top, r.right, r.bottom);
		SetWindowPos(balloon, NULL,
			workarea.right - (r.right - r.left),
			workarea.bottom - (r.bottom - r.top),
			r.right - r.left, r.bottom - r.top,
			SWP_ASYNCWINDOWPOS|SWP_NOZORDER|SWP_NOACTIVATE);
		
		GetClientRect(balloon, &cr);
		FillRect(hdc, &cr, GetSysColorBrush(COLOR_INFOBK));
		FrameRect(hdc, &cr, GetSysColorBrush(COLOR_INFOTEXT));

		SetBkMode(hdc, TRANSPARENT);
		SelectObject(hdc, GetSysColorBrush(COLOR_INFOTEXT));
		SetBkColor(hdc, COLOR_INFOBK);

	//	f->Trace(TEXT("dimensions for title: %d,%d,%d,%d\r\n\t%s\r\n"), rcap.left, rcap.top, rcap.right, rcap.bottom, tool->icon.szInfoTitle);
	//	f->Trace(TEXT("dimensions for body: %d,%d,%d,%d\r\n\t%s\r\n"), rbody.left, rbody.top, rbody.right, rbody.bottom, tool->icon.szInfo);

		SelectObject(hdc, f->GetStockFont(WEZDESK_FONT_DEFAULT_BOLD));
		DrawText(hdc, tool->icon.szInfoTitle, lstrlen(tool->icon.szInfoTitle), &rcap, TEXT_FLAGS|TEXT_CENTER);
		SelectObject(hdc, f->GetStockFont(WEZDESK_FONT_DEFAULT));
#if 0
		TCHAR buf[128];
		wsprintf(buf, TEXT("%d %d"), tool->deadline, GetTickCount());
		DrawText(hdc, buf, lstrlen(buf), &rbody, TEXT_FLAGS|TEXT_CENTER);
#else
		DrawText(hdc, tool->icon.szInfo, lstrlen(tool->icon.szInfo), &rbody, TEXT_FLAGS|TEXT_CENTER);
#endif
	} else {
		ShowWindow(balloon, SW_HIDE);
	}
}

static void next_balloon(int why)
{
	vector <struct tool*>::iterator iter;
	struct tool *tool;
//	f->Trace(TEXT("next_balloon [why = %d]\r\n"), why);
		ShowWindow(balloon, SW_HIDE);
		return;

	iter = balloons.begin();
	if (iter < balloons.end()) {
		tool = *iter;
	
		f->Trace(TEXT("next_balloon: current is %p\r\n"), tool);

		PostMessage(tool->icon.hWnd, tool->icon.uCallbackMessage,
			tool->icon.uID, why);
		tool->deadline = 0;

		balloons.erase(iter);
	}

	tool = first_balloon();
	if (tool) {
		f->Trace(TEXT("next_balloon is %p\r\n"), tool);
		show_balloon(tool);
	} else {
		f->Trace(TEXT("next_balloon: NONE\r\n"));
		ShowWindow(balloon, SW_HIDE);
	}
}

static LRESULT CALLBACK balloon_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);
			paint_balloon(hdc);
			EndPaint(hWnd, &ps);
			break;
		}

		case WM_LBUTTONUP:
		{
			struct tool *tool = first_balloon();
			if (tool) {
				f->Trace(TEXT("left clicking on balloon\r\n"));
				next_balloon(NIN_BALLOONHIDE);
			} else {
				ShowWindow(hWnd, SW_HIDE);
			}
			return 1;

		}
		case WM_RBUTTONUP:
		{
			struct tool *tool = first_balloon();
			if (tool) {
				f->Trace(TEXT("right clicking on balloon\r\n"));
				next_balloon(NIN_BALLOONUSERCLICK);
			} else {
				ShowWindow(hWnd, SW_HIDE);
			}
			return 1;
		}

		case WM_TIMER:
		{
			struct tool *tool = first_balloon();
			if (tool) {
				if (GetTickCount() >= tool->deadline) {
					next_balloon(NIN_BALLOONTIMEOUT);
				}
			} else {
				ShowWindow(hWnd, SW_HIDE);
			}
		}
		break;

		default:
			return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}

static void *initialize(Plugin plugin, WezDeskFuncs *funcs, HWND slit) {
	WNDCLASS wc;

	f = funcs;
	my_plugin = plugin;

	switch (f->GetSlitAlignment(slit)) {
		case WEZDESK_GRAVITY_LEFT:
		case WEZDESK_GRAVITY_RIGHT:
			is_vert = 1;
			break;
		default:
			is_vert = 0;
	}

	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = tray_proc;
	wc.hInstance = funcs->GetPluginInstance(plugin);
	wc.lpszClassName = TEXT("WezDesk Tray Window");
	wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	//wc.hbrBackground = (HBRUSH)(1 + COLOR_BTNFACE);
	wc.hbrBackground = (HBRUSH)(1 + COLOR_APPWORKSPACE);

	if (!RegisterClass(&wc)) {
		return NULL;
	}

	funcs->Trace(TEXT("create tray window\r\n"));

	tray_window = CreateWindowEx(
			WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT,
			wc.lpszClassName,
			NULL,
			WS_CHILD|WS_VISIBLE,
			0, 0,
			is_vert ? 48 : 24,
			is_vert ? 24 : 48,
			slit,
			NULL,
			wc.hInstance,
			NULL);

	/* prefer to gravitate lower */
	funcs->SetGravityFromConfig(plugin, tray_window, TEXT("gravity"),
		is_vert ? WEZDESK_GRAVITY_BOTTOM : WEZDESK_GRAVITY_RIGHT, 50);

	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = balloon_proc;
	wc.hInstance = funcs->GetPluginInstance(plugin);
	wc.lpszClassName = TEXT("WezDesk Tray Balloon Window");
	wc.style = CS_DBLCLKS;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(1 + COLOR_INFOBK);

	if (!RegisterClass(&wc)) {
		return NULL;
	}

#if !USE_NATIVE_BALLOON
	balloon = CreateWindowEx(
			WS_EX_TOOLWINDOW|WS_EX_TOPMOST,
			wc.lpszClassName,
			NULL,
			WS_POPUP,
			0, 0,
			GetSystemMetrics(SM_CXFULLSCREEN)/3, 24,
			NULL, NULL,
			wc.hInstance,
			NULL);
	SetWindowPos(balloon, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
#else

	balloon = CreateWindowEx(
			WS_EX_TOPMOST|WS_EX_TOOLWINDOW,
			TOOLTIPS_CLASS,
			TEXT(""),
			TTS_ALWAYSTIP | WS_POPUP | TTS_NOPREFIX | TTS_BALLOON,
			CW_USEDEFAULT, CW_USEDEFAULT,
			CW_USEDEFAULT, CW_USEDEFAULT,
			NULL, NULL,
			wc.hInstance,
			NULL);
	SetWindowPos(balloon, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);

#endif

	timer = SetTimer(balloon, 1, 1000, NULL);
	
//	SetLayeredWindowAttributes(balloon, 0, 192, LWA_ALPHA);

	tooltips = CreateWindowEx(
			WS_EX_TOPMOST,
			TOOLTIPS_CLASS,
			TEXT(""),
			TTS_ALWAYSTIP | WS_POPUP | TTS_NOPREFIX,
			CW_USEDEFAULT, CW_USEDEFAULT,
			CW_USEDEFAULT, CW_USEDEFAULT,
			NULL, NULL,
			wc.hInstance,
			NULL);
	SetWindowPos(tooltips, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
	funcs->ChangeDesktopBits(tooltips, 0xffffffff, 0xffffffff);	
	
	if (!SendMessage(tooltips, TTM_SETMAXTIPWIDTH, 0, GetSystemMetrics(SM_CXFULLSCREEN)/3)) {
		funcs->Trace(TEXT("failed to set tip width\r\n"));
	}
	
	return (void*)1;	
}

static void on_tray_change(Plugin plugin, WezDeskFuncs *funcs, int action, NOTIFYICONDATA *nid, DWORD changemask)
{
	int i;
	struct tool *tool;
	vector <struct tool*>::iterator iter;

	tool = find_icon(nid, iter);

	if (tool) {
		if (action == NIM_DELETE) {
			SendMessage(tooltips, TTM_DELTOOL, 0, (LPARAM)&tool->ti);
			tools.erase(iter);

			for (iter = balloons.begin(); iter < balloons.end(); iter++) {
				struct tool *tmp = *iter;
				if (tmp == tool) {
					balloons.erase(iter);
					break;
				}
			}
			
			delete tool;
			recalc();
			return;
		}
	} else if (action == NIM_DELETE) {
		return;
	} else {
		/* create a new tool */
		tool = new struct tool;
		memset(tool, 0, sizeof(*tool));
		tools.push_back(tool);
	}
	/* update */

//f->Trace(TEXT("NIM_%s id=%d hwnd=%x\r\n"), action == NIM_ADD ? TEXT("ADD") : TEXT("UPDATE"), nid->uID, nid->hWnd);

	if (!memcmp(&tool->icon, nid, sizeof(tool->icon))) {
		/* no change */
		return;
	}

	if (((changemask & NIF_INFO) == NIF_INFO) && (tool->icon.uFlags & NIF_INFO) == NIF_INFO) {
#if 1
		if (!lstrlen(nid->szInfo)) {
			struct tool *old_first = first_balloon();
			for (iter = balloons.begin(); iter < balloons.end(); iter++) {
				if (*iter == tool) {
					balloons.erase(iter);
					break;
				}
			}
			if (!first_balloon()) {
				ShowWindow(balloon, SW_HIDE);
			} else if (old_first != first_balloon()) {
				show_balloon(first_balloon());
			}
			return;
		}
#endif
		if (!tool->deadline) {
			int found = 0;
			for (iter = balloons.begin(); iter < balloons.end(); iter++) {
				if (*iter == tool) {
					found = 1;
					break;
				}
			}
			if (!found) balloons.push_back(tool);
//			f->Trace(TEXT("modified icon is a balloon, tool=%p\r\n"), tool);
		}
	
		memcpy(&tool->icon, nid, sizeof(tool->icon));

		if (tool == first_balloon()) {
			show_balloon(tool);
		} else if (!first_balloon()) {
			ShowWindow(balloon, SW_HIDE);
		}
	} else {
	memcpy(&tool->icon, nid, sizeof(tool->icon));


	}

	/* re-create any tool tips */
	recalc();

	InvalidateRect(tray_window, NULL, TRUE);
	UpdateWindow(tray_window);
}

static void show_balloon(struct tool *tool)
{
#if USE_NATIVE_BALLOON
	TOOLINFO ti;

    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_TRANSPARENT | TTF_CENTERTIP | TTF_PARSELINKS;
    ti.hwnd = tray_window;
    ti.uId = 0;
    ti.hinst = NULL;
    ti.lpszText = tool->tip;
	GetClientRect(tray_window, &ti.rect);

//	SendMessage(balloon, TTM_DELTOOL, 0, (LPARAM)&ti);
	SendMessage(balloon, TTM_ADDTOOL, 0, (LPARAM)&ti);
	
	SendMessage(balloon, TTM_SETTITLE, 1, (LPARAM)tool->icon.szInfoTitle);

	SendMessage(balloon, WM_MOUSEMOVE, 0, 0x00100010);
	SendMessage(balloon, TTM_POPUP, 0, 0);


#else
	DWORD timeout = tool->icon.uTimeout;

	if (timeout < 7000) {
		timeout = 7000;
	} else if (timeout > 30000) {
		timeout = 30000;
	}
	f->Trace(TEXT("ShowBalloon [timeout = %d; requested %d]\r\n"), timeout, tool->icon.uTimeout);
	if (tool->deadline < GetTickCount()) {// || ((GetTickCount() - tool->deadline) < 3000)) {
		tool->deadline = GetTickCount() + timeout;
	}

	ShowWindow(balloon, SW_SHOW);
	InvalidateRect(balloon, NULL, FALSE);
	UpdateWindow(balloon);
	BringWindowToTop(balloon);
	SetWindowPos(balloon, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED|SWP_NOACTIVATE);
#endif
	PostMessage(tool->icon.hWnd, tool->icon.uCallbackMessage,
			tool->icon.uID, NIN_BALLOONSHOW); 
}

static int unload(Plugin plugin, WezDeskFuncs *funcs, int force)
{
	KillTimer(balloon, timer);
	DestroyWindow(balloon);
	DestroyWindow(tooltips);
	DestroyWindow(tray_window);

	vector <struct tool *>::iterator i;
	for (i = tools.begin(); i != tools.end(); i++) {
		delete *i;
	}
	
	tools.clear();
	balloons.clear();

	return 1;
}


static WezDeskPlugin the_plugin = {
	WEZDESK_API_NUMBER,
	TEXT("Notification Area (aka Tray)"),
	TEXT("Implements the System Notification Area, commonly misnamed the \"Tray\""),
	TEXT("BSD"),
	TEXT("Wez Furlong <wez@php.net>"),
	TEXT("http://netevil.org/wiki.php?WezDesk"),
	TEXT("0.1"),
	initialize,
	NULL, /* unload ok */
	unload, /* unload */
	NULL, /* veto */
	NULL, /* shell msg */
	on_tray_change,
	NULL /* notify */
};

GET_PLUGIN(the_plugin);

