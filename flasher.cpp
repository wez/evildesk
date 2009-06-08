/* Copyright (c) 2004-2009 Wez Furlong.
 * This source is provided under terms of the GPLv2.
 * See the file named LICENSE for full details */
#include "wezdeskapi.h"
#include <commctrl.h>
#include <vector>
#define STRSAFE_LIB
#include <strsafe.h>

using namespace std;
#define ICON_SIZE 	32
#define ICON_PAD	4
#define FLASHER_WIDTH	48

static WezDeskFuncs *f;
static Plugin my_plugin;
HWND flasher;
UINT_PTR timer;
int is_vert;

struct flashee {
	HWND app;
	int ticks;
	HICON icon;
};

static vector<struct flashee> apps;
static void recalc_size(void);

static void calc_item_rect(int i, RECT *r) {
	struct flashee fl;
	fl = apps[i];

	if (is_vert) {
		r->left = (FLASHER_WIDTH - ICON_SIZE) / 2;
		r->top = ICON_PAD + ((ICON_PAD + ICON_SIZE) * i);

		if (fl.ticks < ICON_SIZE / 2)
			r->left -= fl.ticks;
		else
			r->left += fl.ticks - (ICON_SIZE/2);

	} else {
		r->left = ICON_PAD + ((ICON_PAD + ICON_SIZE) * i);
		r->top = (FLASHER_WIDTH - ICON_SIZE) / 2;

		if (fl.ticks < ICON_SIZE / 2)
			r->top -= fl.ticks;
		else
			r->top += fl.ticks - (ICON_SIZE/2);
	}

	r->bottom = r->top + ICON_SIZE;
	r->right = r->left + ICON_SIZE;
}

static void paint_flasher(HDC hdc)
{
	int i;
	RECT r;
	struct flashee fl;

	for (i = 0; i < apps.size(); i++) {
		fl = apps[i];
		calc_item_rect(i, &r);

		if (fl.icon == 0) {
			fl.icon = f->GetWindowIcon(fl.app, 100);
			if (!fl.icon) {
				fl.icon = LoadIcon(NULL, IDI_APPLICATION);
			}
		}
		apps[i].icon = fl.icon;

		if (fl.icon) {
			DrawIconEx(hdc, r.left, r.top, fl.icon, ICON_SIZE, ICON_SIZE, 0, NULL, DI_NORMAL);
		} else {
			DrawEdge(hdc, &r, EDGE_SUNKEN, BF_RECT);

			TCHAR caption[MAX_PATH];
			caption[0] = '?';
			caption[1] = '\0';
			GetWindowText(fl.app, caption, sizeof(caption) / sizeof(caption[0]));

			f->Trace(TEXT("window has no icon!? %s\r\n"), caption);
		}
	}

#if 0
	
	SYSTEMTIME lt;
	TCHAR the_time[128];
	int i;
	RECT r, o;

	i = GetTimeFormat(LOCALE_USER_DEFAULT,
		TIME_NOSECONDS,
		NULL,
		NULL,
		the_time,
		sizeof(the_time) / sizeof(the_time[0])
		);

	the_time[i-1] = '\n';
	
	i += GetDateFormat(LOCALE_USER_DEFAULT,
		DATE_LONGDATE,
		NULL,
		NULL,
		&the_time[i],
		(sizeof(the_time) / sizeof(the_time[0])) - i);

	--i;

	GetClientRect(flasher, &r);
	memcpy(&o, &r, sizeof(r));

	HFONT ft = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

	if (ft) {
		SelectObject(hdc, ft);
	} else {
		f->Trace(TEXT("failed to get system font\r\n"));
	}
	DrawText(hdc, the_time, i, &r, DT_CALCRECT|DT_WORDBREAK|DT_CENTER);

	if (o.bottom != r.bottom) {
		GetClientRect(GetParent(flasher), &o);
		f->Trace(TEXT("adjusting height to fit flasher: o.right=%d o.left=%d r.bottom=%d\r\n"), o.right, o.left, r.bottom);
		SetWindowPos(flasher, NULL, -1, -1, o.right - o.left, r.bottom, SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOZORDER);
	}
	GetClientRect(flasher, &r);
	SetBkMode(hdc, TRANSPARENT);
	DrawText(hdc, the_time, i, &r, DT_CENTER|DT_VCENTER|DT_WORDBREAK);
#endif
}

static LRESULT CALLBACK flasher_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);
			paint_flasher(hdc);
			EndPaint(hWnd, &ps);
			return 0;
		}

		case WM_LBUTTONUP:
		{
			POINT pt;
			pt.x = LOWORD(lParam);
			pt.y = HIWORD(lParam);
			for (int i = 0; i < apps.size(); i++) {
				RECT r;
				calc_item_rect(i, &r);
				pt.x = LOWORD(lParam);
				if (PtInRect(&r, pt)) {
					f->SwitchToWindow(apps[i].app, TRUE);
					break;
				}
			}
			return 1;
		}

		case WM_TIMER:
		{
			int i;
			vector <struct flashee>::iterator iter;
run_again:
			for (iter = apps.begin(); iter != apps.end(); iter++) {
				if (!IsWindow((*iter).app) || (*iter).app == GetForegroundWindow()) {
					FLASHWINFO fw;
					fw.cbSize = sizeof(fw);
					fw.hwnd = (*iter).app;
					fw.dwFlags = FLASHW_STOP;
					fw.uCount = 0;
					fw.dwTimeout = 0;
					FlashWindowEx(&fw);


					apps.erase(iter);
					recalc_size();
					goto run_again;
				}
			}

			for (int i = 0; i < apps.size(); i++) {
				if (++apps[i].ticks > ICON_SIZE)
					apps[i].ticks = 0;
			}
			InvalidateRect(hWnd, NULL, TRUE);
			UpdateWindow(hWnd);

			return 0;
		}

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
	wc.lpfnWndProc = flasher_proc;
	wc.hInstance = funcs->GetPluginInstance(plugin);
	wc.lpszClassName = TEXT("WezDesk Flasher Window");
	wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	//wc.hbrBackground = (HBRUSH)(1 + COLOR_BTNFACE);
	wc.hbrBackground = (HBRUSH)(1 + COLOR_APPWORKSPACE);

	if (!RegisterClass(&wc)) {
		return NULL;
	}

	flasher = CreateWindowEx(
			WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT,
			wc.lpszClassName,
			NULL,
			WS_CHILD|WS_VISIBLE,
			0, 0,
			1, 1,
			slit,
			NULL,
			wc.hInstance,
			NULL);

	/* prefer to gravitate lower */
	funcs->SetGravityFromConfig(plugin, flasher, TEXT("gravity"),
		is_vert ? WEZDESK_GRAVITY_BOTTOM : WEZDESK_GRAVITY_RIGHT, 0);
	timer = SetTimer(flasher, 1, 50, NULL);

	recalc_size();

	return (void*)1;
}

static void recalc_size(void)
{
	if (apps.size()) {
		int x = FLASHER_WIDTH;
		int y = (ICON_SIZE + (2 * ICON_PAD)) * apps.size();
		SetWindowPos(flasher, NULL, -1, -1, 
			is_vert ? x : y,
			is_vert ? y : x,
			SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOZORDER|SWP_SHOWWINDOW);
		InvalidateRect(flasher, NULL, TRUE);
		UpdateWindow(flasher);
	} else {
		InvalidateRect(flasher, NULL, TRUE);
		SetWindowPos(flasher, NULL, -1, -1, 0, 0,
			SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOZORDER);

		ShowWindow(flasher, SW_HIDE);
	}
}
	
static void on_shell_message(Plugin plugin, WezDeskFuncs *funcs, WPARAM msg, LPARAM lparam)
{
	int i;
	struct flashee fl;

	switch (msg) {
		case HSHELL_FLASH:

			/* yes, 'doze still sends flash messages if the window is active */
			if ((HWND)lparam == GetForegroundWindow())
				return;
			
			/* do we have this app already listed ? */
			for (i = 0; i < apps.size(); i++) {
				fl = apps[i];
				if (fl.app == (HWND)lparam) {
					InvalidateRect(flasher, NULL, TRUE);
				//	UpdateWindow(flasher);
					return;	
				}
			}

			/* nope, so let's add it */
			fl.app = (HWND)lparam;
			fl.ticks = 0;
			fl.icon = 0;
			
			apps.push_back(fl);
			f->Trace(TEXT("flash: there are now %d flashing entries\r\n"), apps.size());
			recalc_size();
			break;

		case HSHELL_REDRAW:
			if (GetForegroundWindow() == (HWND)lparam) {
				/* if the user is switching to a flashing app, remove it from the list */
				for (vector<struct flashee>::iterator iter = apps.begin();
						iter != apps.end(); iter++) {
					fl = *iter;
					if (fl.app == (HWND)lparam && fl.app == GetForegroundWindow()) {
						apps.erase(iter);
						recalc_size();
						return;	
					}
				}
			}
			break;
	}
}

static int unload(Plugin plugin, WezDeskFuncs *funcs, int force)
{
	KillTimer(flasher, timer);
	DestroyWindow(flasher);
	flasher = NULL;
	return 1;
}

static WezDeskPlugin the_plugin = {
	WEZDESK_API_NUMBER,
	TEXT("Flasher"),
	TEXT("Flashes application icons when they need your attention"),
	TEXT("BSD"),
	TEXT("Wez Furlong <wez@php.net>"),
	TEXT("http://netevil.org/wiki.php?WezDesk"),
	TEXT("0.1"),
	initialize,
	NULL, /* unload ok */
	unload, /* unload */
	NULL, /* veto */
	on_shell_message, /* shell msg */
	NULL, /* on_tray_change */
};

GET_PLUGIN(the_plugin);

