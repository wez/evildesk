/* Copyright (c) 2004-2009 Wez Furlong.
 * This source is provided under terms of the GPLv2.
 * See the file named LICENSE for full details */
#include "wezdeskapi.h"
#include "wezdeskres.h"
#include <commctrl.h>
#define STRSAFE_LIB
#include <strsafe.h>


#define CL_WIDTH	48
#define CL_HEIGHT	24

struct TheClock {
	WezDeskFuncs *funcs;
	Plugin my_plugin;
	HWND clock;
	UINT_PTR timer;
	FILETIME filetime;
	int is_vert;
	TCHAR *date_format;
	TCHAR *mail_format;
	HFONT my_font;
	COLORREF fgcol, shadowcol;

	TheClock() {
		my_font = NULL;
		date_format = NULL;
		mail_format = NULL;
	}

	~TheClock() {
		KillTimer(clock, timer);
		DestroyWindow(clock);
		if (date_format) free(date_format);
		if (mail_format) free(mail_format);
	}
};

static void paint_clock(HDC hdc, TheClock *P)
{
	SYSTEMTIME lt;
	TCHAR the_time[128];
	int i;
	RECT r, o;
	DWORD mail_count;

	if (!P->my_font) {
		TCHAR *name = P->funcs->GetPluginString(P->my_plugin, TEXT("Font"), NULL);
		if (name) {
			P->my_font = P->funcs->LoadFont(hdc, name);
		} else {
			P->my_font = P->funcs->GetStockFont(WEZDESK_FONT_DEFAULT);
		}

		P->fgcol = P->funcs->GetPluginInt(P->my_plugin, TEXT("Font.fg"), RGB(0xff,0xff,0xff));
		P->shadowcol = P->funcs->GetPluginInt(P->my_plugin, TEXT("Font.fg"), RGB(0x44,0x44,0x44));
	}

	if (SUCCEEDED(SHGetUnreadMailCount(NULL, NULL,
			&mail_count, &P->filetime, NULL, NULL)) && mail_count > 0) {
		StringCbPrintf(the_time, sizeof(the_time), P->mail_format, mail_count);
		StringCbCat(the_time, sizeof(the_time), TEXT("\n"));
		i = lstrlen(the_time);
	} else {
		i = 0;
	}
	i = GetDateFormat(LOCALE_USER_DEFAULT,
		0,
		NULL,
		P->date_format,
		&the_time[i],
		(sizeof(the_time) / sizeof(the_time[0])) - i
		);

	if (i) {
		StringCbCat(the_time, sizeof(the_time), TEXT("\n"));
		i = lstrlen(the_time);
	}

	i = GetTimeFormat(LOCALE_USER_DEFAULT,
		TIME_NOSECONDS,
		NULL,
		NULL,
		&the_time[i],
		(sizeof(the_time) / sizeof(the_time[0])) - i
		);
	
	i = lstrlen(the_time);

	GetClientRect(P->clock, &r);
	memcpy(&o, &r, sizeof(r));

	SelectObject(hdc, P->my_font);
	DrawText(hdc, the_time, i, &r, DT_CALCRECT|DT_WORDBREAK|DT_CENTER);
	
	if (RECTHEIGHT(o) != RECTHEIGHT(r) || RECTWIDTH(o) != RECTWIDTH(r)) {
		SetWindowPos(P->clock, NULL, -1, -1, 
			RECTWIDTH(r), RECTHEIGHT(r), 
			SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOZORDER);
	}
	GetClientRect(P->clock, &r);
	SetBkMode(hdc, TRANSPARENT);

	P->funcs->DrawShadowText(hdc, the_time, i, &r, DT_CENTER|DT_VCENTER|DT_WORDBREAK,
		P->fgcol, P->shadowcol,
		2, 2);
}

static void handle_slit_layout_change(HWND w)
{
	struct TheClock *P = (struct TheClock*)GetWindowLongPtr(w, GWLP_USERDATA);

	/* prefer to gravitate lower */
	switch (P->funcs->GetSlitAlignment(GetParent(w))) {
		case WEZDESK_GRAVITY_LEFT:
		case WEZDESK_GRAVITY_RIGHT:
			P->is_vert = 1;
			break;
		default:
			P->is_vert = 0;
	}

	P->funcs->SetGravityFromConfig(P->my_plugin, P->clock, TEXT("gravity"),
		P->is_vert ? WEZDESK_GRAVITY_BOTTOM : WEZDESK_GRAVITY_RIGHT,
		100);

	InvalidateRect(w, NULL, TRUE);
	UpdateWindow(w);
}

static LRESULT CALLBACK clock_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);
			paint_clock(hdc, (TheClock*)GetWindowLongPtr(hWnd, GWLP_USERDATA));
			EndPaint(hWnd, &ps);
			break;
		}

		case WM_SLIT_LAYOUT_CHANGED:
			handle_slit_layout_change(hWnd);
			return 1;

		case WM_TIMER:
			InvalidateRect(hWnd, NULL, FALSE);
			UpdateWindow(hWnd);
			break;

		default:
			return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}
static void *initialize(Plugin plugin, WezDeskFuncs *funcs, HWND slit) {
	WNDCLASS wc;
	TCHAR scratch[256];

	TheClock *P = new TheClock;
	P->funcs = funcs;
	P->my_plugin = plugin;

	funcs->LoadString(IDS_CLOCK_DATEFORMAT, scratch,
				sizeof(scratch)/sizeof(scratch[0]));
	P->date_format = P->funcs->GetPluginString(P->my_plugin,
		TEXT("DateFormat"), scratch);

	funcs->LoadString(IDS_CLOCK_MAILFORMAT, scratch,
				sizeof(scratch)/sizeof(scratch[0]));
	P->mail_format = P->funcs->GetPluginString(P->my_plugin, TEXT("MailFormat"),
		scratch);

	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = clock_proc;
	wc.hInstance = funcs->GetPluginInstance(plugin);
	wc.lpszClassName = TEXT("WezDesk Clock Window");
	wc.style = CS_DBLCLKS;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	//wc.hbrBackground = (HBRUSH)(1 + COLOR_BTNFACE);
	wc.hbrBackground = (HBRUSH)(1 + COLOR_APPWORKSPACE);

	if (!RegisterClass(&wc)) {
		return NULL;
	}

	P->clock = CreateWindowEx(
			WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT,
			wc.lpszClassName,
			NULL,
			WS_CHILD|WS_VISIBLE,
			0, 0,
			CL_WIDTH,
			P->is_vert ? CL_HEIGHT : CL_WIDTH,
			slit, NULL,
			wc.hInstance,
			NULL);

	SetWindowLongPtr(P->clock, GWLP_USERDATA, (ULONG_PTR)P);
	handle_slit_layout_change(P->clock);

	P->timer = SetTimer(P->clock, 1, 1000, NULL);

	return P;
}

static int unload(Plugin plugin, WezDeskFuncs *funcs, int force)
{
	TheClock *P = (TheClock*)funcs->GetPluginData(plugin);
	delete P;

	return 1;
}

static WezDeskPlugin the_plugin = {
	WEZDESK_API_NUMBER,
	TEXT("Clock"),
	TEXT("Implements a simple desktop clock"),
	TEXT("GPL"),
	TEXT("Wez Furlong <wez@php.net>"),
	TEXT("http://netevil.org/wiki.php?WezDesk"),
	TEXT("0.1"),
	initialize,
	NULL,	/* unload ok */
	unload, /* unload */
	NULL, /* veto */
	NULL, /* shell msg */
	NULL, /* on_tray_change */
};

GET_PLUGIN(the_plugin);

