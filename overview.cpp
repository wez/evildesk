/* Copyright (c) 2004-2009 Wez Furlong.
 * This source is provided under terms of the GPLv2.
 * See the file named LICENSE for full details */
#include "wezdesk.h"
#include <docobj.h>
#include <ShlGuid.h>
#include <vector>
#include <commctrl.h>
#include <algorithm>
#define STRSAFE_LIB
#include <strsafe.h>


static WezDeskFuncs *f;
static Plugin my_plugin;
static WNDCLASS wc;

struct item {
	int zorder;
	struct per_monitor *mon;
	HWND w;
	RECT r;
	POINT center;
	double center_distance;
	TCHAR caption[128];
	RECT er;
	Image *capture;
	double scale;
	Image *highres;
};

struct per_monitor {
	MONITORINFOEX info;
	HWND wnd;
	HMONITOR hmon;
	UINT timer;
	HWND tooltips;
	double scale;
	int hot; /* which item in the list is "hot" */
	vector <struct item *> task_list;
};

static HANDLE update_thread = NULL;
vector <struct per_monitor *> monitors;

Font *captionfont = NULL;
StringFormat *format = NULL, *emptyFormat = NULL;
SolidBrush *blackBrush = NULL;
SolidBrush *whiteBrush = NULL;
SolidBrush *greybrush = NULL;
ImageAttributes *greyscale = NULL;

ColorMatrix greyscaleMatrix = {
		0.299, 0.299, 0.299, 0, 0,
		0.587, 0.587, 0.587, 0, 0,
		0.114, 0.114, 0.114, 0, 0,
		0, 0, 0, 1, 0,
		0, 0, 0, 0, 1};


static void start_update_thread(void);

static void pump(int block)
{
	MSG msg;
	BOOL got;

	if (block) 
		got = GetMessage(&msg, 0, 0, 0);
	else
		got = PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);

	if (got) {
		do {
#ifdef RELEASE_BUILD
			try {
#endif
				TranslateMessage(&msg);
				DispatchMessage(&msg);
#ifdef RELEASE_BUILD
			} catch (...) {
				/* nada */
				debug_printf(TEXT("hmmmmm, an exception was caught by the message pump\r\n"));
			}
#endif
		} while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE));
	}
}

static void close_overview(HWND switch_to)
{
	int i, j;
	for (i = 0; i < monitors.size(); i++) {
		struct per_monitor *mon = monitors[i];
		DestroyWindow(mon->wnd);
		mon->wnd = NULL;
	}

	pump(0);

	if (switch_to) {
		f->SwitchToWindow(switch_to, TRUE);
	}

	pump(0);

	if (update_thread) {
		WaitForSingleObject(update_thread, INFINITE);
		CloseHandle(update_thread);
		update_thread = NULL;
	}

	for (i = 0; i < monitors.size(); i++) {
		struct per_monitor *mon = monitors[i];

		for (j = 0; j < mon->task_list.size(); j++) {
			struct item *item = mon->task_list[j];

			if (item->highres)
				delete item->highres;

			delete item;
		}
		delete mon;
	}
	monitors.clear();


	if (captionfont) {
		delete captionfont;
		captionfont = NULL;

		delete format;
		format = NULL;

		delete emptyFormat;
		emptyFormat = NULL;

		delete blackBrush;
		blackBrush = NULL;

		delete whiteBrush;
		whiteBrush = NULL;

		delete greybrush;
		greybrush = NULL;

		delete greyscale;
		greyscale = NULL;
	}
}

static void paint_item(struct per_monitor *mon, Graphics *G, int iitem, int hot, HWND wnd)
{
	struct item *item;

	if (mon->task_list.size() == 0) return;
	
	item = mon->task_list[iitem];
	RECT r = item->er;
	Image *capture = item->highres ? item->highres : item->capture;
	BOOL draw_caption = FALSE;

	if (!capture) {
		G->FillRectangle(greybrush, Rect(r.left, r.top, RECTWIDTH(r), RECTHEIGHT(r)));
		if (!lstrlen(item->caption))
			GetWindowText(item->w, item->caption, sizeof(item->caption)/sizeof(item->caption[0]));

		draw_caption = TRUE;

		HICON icon = 0;
		BOOL kill_icon;
		
		icon = f->GetWindowIcon(item->w, 100);
		if (!icon) {
			icon = LoadIcon(NULL, IDI_APPLICATION);
		}

		Bitmap *iconimage = Bitmap::FromHICON(icon);
		G->DrawImage(iconimage, r.left, r.top);//, RECTWIDTH(r), RECTHEIGHT(r));
		delete iconimage;
	
	} else if (hot) {
		G->DrawImage(capture, r.left, r.top, RECTWIDTH(r), RECTHEIGHT(r));
		draw_caption = TRUE;
	} else {
		G->DrawImage(capture, 
				RectF(r.left, r.top, RECTWIDTH(r), RECTHEIGHT(r)),
				0, 0, capture->GetWidth(), capture->GetHeight(),
				UnitPixel, greyscale, NULL, NULL);
	}

#if 0
	if (draw_caption) {
		InflateRect(&r, -2, -2);

		RectF layoutRect(r.left, r.top, RECTWIDTH(r), RECTHEIGHT(r));
		RectF bounds;

		G->MeasureString(item->caption, lstrlen(item->caption), captionfont, layoutRect, format, &bounds);

		G->FillRectangle(greybrush, bounds);

		// Draw string.
		G->DrawString(
				item->caption,
				lstrlen(item->caption),
				captionfont,
				bounds,
				emptyFormat,
				blackBrush);

		bounds.X -= 1;
		bounds.Y -= 1;

		// Draw string.
		G->DrawString(
				item->caption,
				lstrlen(item->caption),
				captionfont,
				bounds,
				emptyFormat,
				whiteBrush);
	}
#endif

	if (wnd) {
	//	InvalidateRect(wnd, &item->er, FALSE);
	}
}

static void on_paint(struct per_monitor *mon, HDC hdc)
{
	Graphics G(hdc, NULL);
	int i;

	G.SetInterpolationMode(InterpolationModeHighQualityBicubic);

	for (i = 0; i < mon->task_list.size(); i++) {
		struct item *item = mon->task_list[i];

		paint_item(mon, &G, i, mon->hot == i, NULL);
	}
}

static int hit_test(struct per_monitor *mon, POINT pt)
{
	int i;

	for (i = 0; i < mon->task_list.size(); i++) {
		struct item *item = mon->task_list[i];

		if (PtInRect(&item->er, pt)) {
			return i;
		}
	}
	return -1;
}

static LRESULT CALLBACK wnd_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	struct per_monitor *mon = (struct per_monitor*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

	switch (uMsg) {
		case WM_CREATE:
		{
			mon = (struct per_monitor*)((LPCREATESTRUCT)lParam)->lpCreateParams;
			mon->wnd = hWnd;
			SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)mon);
			return 0;
		}

		case WM_DESTROY:
		{
			KillTimer(hWnd, mon->timer);
			DestroyWindow(mon->tooltips);
			return 0;
		}

		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);
			on_paint(mon, hdc);
			EndPaint(hWnd, &ps);
			break;
		}

		case WM_MOUSEMOVE:
		{
			POINT pt;
			int new_hot;

			pt.x = LOWORD(lParam);
			pt.y = HIWORD(lParam);
			new_hot = hit_test(mon, pt);

			if (new_hot != mon->hot) {
				Graphics G(hWnd);
				G.SetInterpolationMode(InterpolationModeHighQualityBicubic);
				if (mon->hot != -1) {
					paint_item(mon, &G, mon->hot, 0, hWnd);
				}
				if (new_hot != -1) {
					paint_item(mon, &G, new_hot, 1, hWnd);
				}
				mon->hot = new_hot;
//				UpdateWindow(hWnd);	
			}
			return 0;

		}
		break;

		case WM_ERASEBKGND:
			PaintDesktop((HDC)wParam);
			return 1;

		case WM_LBUTTONUP:
		{
			POINT pt;
			int new_hot;

			pt.x = LOWORD(lParam);
			pt.y = HIWORD(lParam);
			new_hot = hit_test(mon, pt);

			if (new_hot >= 0) {
				struct item *item = mon->task_list[new_hot];
				close_overview(item->w);
			} else {
				close_overview(NULL);
			}
			break;
		}

		case WM_TIMER:
			start_update_thread();
			break;

		default:
			return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}

static BOOL CALLBACK for_each_monitor(HMONITOR hmon, HDC hdcmon, LPRECT rcMon, LPARAM data)
{
	TCHAR *arg = (TCHAR*)data;
	struct per_monitor *mon = new per_monitor;

	memset(mon, 0, sizeof(*mon));

	mon->hmon = hmon;
	mon->info.cbSize = sizeof(mon->info);
	GetMonitorInfo(hmon, &mon->info);

	CreateWindowEx(
		WS_EX_TOOLWINDOW|WS_EX_TOPMOST,
		wc.lpszClassName,
		NULL,
		WS_POPUP|WS_VISIBLE,
#if 0
		mon->info.rcWork.left, mon->info.rcWork.top,
		RECTWIDTH(mon->info.rcWork), RECTHEIGHT(mon->info.rcWork),
#else
		rcMon->left, rcMon->top,
		RECTWIDTH(*rcMon), RECTHEIGHT(*rcMon),
#endif
		NULL, NULL,
		wc.hInstance,
		mon);

	monitors.push_back(mon);

	return TRUE;
}

static int IsToolWindow(HWND w)
{
	return ((GetWindowLong(w, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) == WS_EX_TOOLWINDOW);
}

int is_app_window(HWND w)
{
	if (IsWindowVisible(w)) {
		HWND parent = GetParent(w);
		RECT r;

		GetWindowRect(w, &r);
		if (RECTWIDTH(r) < 8 || RECTHEIGHT(r) < 8) return 0;

		if (parent == 0) {
			HWND hWndOwner = (HWND)GetWindowLongPtr(w, GWLP_HWNDPARENT);
			if ((hWndOwner == 0) || IsToolWindow(hWndOwner)) {
				if (!IsToolWindow(w)) {
					return 1;
				}
			}
		} else if (!IsWindowVisible(parent)) {
			/* one of those elusive dialog boxes that are really hard
			 * to find on a busy desktop */
			return 1;
		}
	}
	return 0;
}

static BOOL CALLBACK add_to_task_list(HWND hwnd, LPARAM lparam)
{
	TCHAR *arg = (TCHAR*)lparam;

	if (is_app_window(hwnd) && IsWindow(hwnd)) {
		struct per_monitor *mon;
		HMONITOR hmon;
		int i;

		/* which monitor is this? */
		hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
		for (i = 0; i < monitors.size(); i++) {
			mon = monitors[i];
			if (mon->hmon == hmon)
				break;
		}

		/* are we including this window? */
		int this_desk = f->GetActiveDesktop() - 1;
		unsigned long bits = f->ChangeDesktopBits(hwnd, 0, 0);
		if ((bits & (1<<this_desk)) == 0) {
			/* it's not on this desk; skip it */
			return TRUE;
		}

		struct item *item = new struct item;
		memset(item, 0, sizeof(*item));
		item->zorder = mon->task_list.size();
		item->mon = mon;
		mon->task_list.push_back(item);
		GetWindowText(hwnd, item->caption, sizeof(item->caption)/sizeof(item->caption[0]));
		item->w = hwnd;

		item->capture = f->GetWindowThumbnail(hwnd, &item->scale);
		if (IsIconic(hwnd)) {
			WINDOWPLACEMENT wp;
			wp.length = sizeof(wp);
			GetWindowPlacement(hwnd, &wp);
			memcpy(&item->r, &wp.rcNormalPosition, sizeof(item->r));
		} else {
			GetWindowRect(hwnd, &item->r);
		}
	}
	return TRUE;
}

bool order_by_zorder(struct item *a, struct item *b)
{
	if (a->zorder < b->zorder)
		return true;
	return false;
}

bool order_by_size_desc(struct item *a, struct item *b)
{
	unsigned long A = RECTWIDTH(a->er) * RECTHEIGHT(a->er);
	unsigned long B = RECTWIDTH(b->er) * RECTHEIGHT(b->er);

	if (!a->capture && b->capture)
		return true;
	
	if (!b->capture && a->capture)
		return false;

	if (A > B)
		return true;

	return false;
}

DWORD WINAPI update_captures(LPVOID unused)
{
	/* now that we've got something on the screen, update the graphics for
	 * non-iconic windows */
	HDC memdc, windc;
	HBITMAP bm;
	HGDIOBJ old;
	DWORD result;
	struct item *item;
	struct per_monitor *mon;
	int i, j;

	/* we want to start with the largest first */
	vector <struct item*> all_windows;

	f->Trace(TEXT("update_captures!\r\n"));

	for (i = 0; i < monitors.size(); i++) {
		mon = monitors[i];
		if (!mon->wnd) {
			f->Trace(TEXT("monitor not valid; breaking out of update_captures\r\n"));
			return 0;
		}

		for (j = 0; j < mon->task_list.size(); j++) {
			item = mon->task_list[j];
			if (IsIconic(item->w)) continue;
			all_windows.push_back(item);
		}
	}
	sort(all_windows.begin(), all_windows.end(), order_by_size_desc);
	
	memdc = CreateCompatibleDC(NULL);
	
	for (i = 0; i < all_windows.size(); i++) {
		item = all_windows[i];

		RECT rect;

		if (item->highres)
			Sleep(500);

//		RedrawWindow(item->w, NULL, NULL, RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
//		InvalidateRect(item->w, NULL, TRUE);
		GetWindowRect(item->w, &rect);

		windc = GetDC(item->w);
		bm = CreateCompatibleBitmap(windc, RECTWIDTH(rect), RECTHEIGHT(rect));
		ReleaseDC(item->w, windc);

		old = SelectObject(memdc, bm);
		BOOL grabbed;
		int tries = 0;
		do {
			grabbed = PrintWindow(item->w, memdc, 0);
			if (!grabbed) {
				f->Trace(TEXT("failed to capture a window\r\n"));
				Sleep(tries);
			}
		} while (!grabbed && tries++ < 5);
		SelectObject(memdc, old);

		if (grabbed) {
			Bitmap *src = Bitmap::FromHBITMAP(bm, NULL);
			Image *hr;
			hr = new Bitmap(RECTWIDTH(item->er), RECTHEIGHT(item->er));
			Graphics *graph = Graphics::FromImage(hr);
			graph->SetInterpolationMode(InterpolationModeHighQualityBicubic);
			graph->DrawImage(src, 0, 0, RECTWIDTH(item->er), RECTHEIGHT(item->er));

			f->Trace(TEXT("Captured a window; invalidating overview window\r\n"));

			delete graph;
			delete src;
			
			Image *oldim = item->highres;
			item->highres = hr;
			if (oldim) delete oldim;
			InvalidateRect(item->mon->wnd, &item->er, FALSE);
		}
		DeleteObject(bm);
		UpdateWindow(item->mon->wnd);
		InvalidateRect(item->w, NULL, FALSE);
		Sleep(1);
	}
	DeleteObject(memdc);
	return 0;
}

static void show_overview(TCHAR *arg, HWND slit)
{
	int i, j;
	double SW, SH;
	int x, y, h;
	int SPACING = 16;
	struct item *item;
	struct per_monitor *mon;

	if (monitors.size()) {
		close_overview(NULL);
		return;
	}

	if (update_thread) {
		WaitForSingleObject(update_thread, INFINITE);
		CloseHandle(update_thread);
	}

	captionfont = new Font(L"Trebuchet MS", 12);
	format = new StringFormat;
	emptyFormat = new StringFormat;
	format->SetAlignment(StringAlignmentCenter);
	format->SetLineAlignment(StringAlignmentCenter);
	blackBrush = new SolidBrush(Color(255, 0, 0, 0));
	whiteBrush = new SolidBrush(Color(255, 255,255,255));
	greybrush = new SolidBrush(Color(255, 80, 80, 80));
	greyscale = new ImageAttributes;
	greyscale->SetColorMatrix(&greyscaleMatrix);

	/* build up the list of monitors and display the overview windows */
	EnumDisplayMonitors(NULL, NULL, for_each_monitor, (LPARAM)arg);

	/* build up the task list, putting the windows into the correct monitors */
	EnumWindows(add_to_task_list, (LPARAM)arg);
	
	for (i = 0; i < monitors.size(); i++) {
		mon = monitors[i];

		sort(mon->task_list.begin(), mon->task_list.end(), order_by_zorder);
		SW = RECTWIDTH(mon->info.rcWork);
		SH = RECTHEIGHT(mon->info.rcWork);
re_fit:
		x = SPACING;
		y = SPACING;
		h = 0;
		for (j = 0; j < mon->task_list.size(); j++) {
			item = mon->task_list[j];

			if (RECTWIDTH(item->r) + x + SPACING>= SW) {
				/* too big to fit on this row, move to the next */
				x = SPACING;
				y += h + SPACING;
				h = 0;
			}
			if (RECTWIDTH(item->r) >= SW || RECTHEIGHT(item->r) + y + SPACING >= SH) {
				/* still too big, then we need to increase the display area */
				SW *= 1.1;
				SH *= 1.1;
				goto re_fit;
			}

			item->er.left = x;
			item->er.top = y;
			item->er.right = x + RECTWIDTH(item->r);
			item->er.bottom = y + RECTHEIGHT(item->r);

			if (RECTHEIGHT(item->r) > h)
				h = RECTHEIGHT(item->r);

			x += RECTWIDTH(item->r) + SPACING;
		}
		mon->scale = SW / RECTWIDTH(mon->info.rcWork);

		mon->tooltips = CreateWindowEx(
			WS_EX_TOPMOST, TOOLTIPS_CLASS, TEXT(""),
			TTS_ALWAYSTIP|WS_POPUP|TTS_NOPREFIX,
			CW_USEDEFAULT, CW_USEDEFAULT,
			CW_USEDEFAULT, CW_USEDEFAULT,
			NULL, NULL,
			wc.hInstance,
			NULL);
		if (!SendMessage(mon->tooltips, TTM_SETMAXTIPWIDTH, 0, GetSystemMetrics(SM_CXFULLSCREEN)/3)) {
			f->Trace(TEXT("failed to set tip width\r\n"));
		}

		for (j = 0; j < mon->task_list.size(); j++) {
			TOOLINFO ti;

			item = mon->task_list[j];
			item->er.left /= mon->scale;
			item->er.top /= mon->scale;
			item->er.bottom /= mon->scale;
			item->er.right /= mon->scale;

			memset(&ti, 0, sizeof(ti));
			ti.cbSize = TTTOOLINFOW_V2_SIZE;
			ti.uFlags = TTF_SUBCLASS;
			ti.hwnd = mon->wnd;
			ti.uId = j;
			ti.hinst = wc.hInstance;
			ti.lpszText = item->caption;
			ti.rect = item->er;
			SendMessage(mon->tooltips, TTM_ADDTOOL, 0, (LPARAM)&ti);
		}
		UpdateWindow(mon->wnd);

	}
	update_thread = CreateThread(NULL, 0, update_captures, NULL, 0, NULL);
	for (i = 0; i < monitors.size(); i++) {
		mon = monitors[i];
		mon->timer = SetTimer(mon->wnd, 1, 12000, NULL);
	}
}

void start_update_thread(void)
{
	if (update_thread) {
		if (WaitForSingleObject(update_thread, 0) == WAIT_OBJECT_0) {
			CloseHandle(update_thread);
			update_thread = NULL;
		}
	}
	if (!update_thread) {
		update_thread = CreateThread(NULL, 0, update_captures, NULL, 0, NULL);
	}
}


static void *initialize(Plugin plugin, WezDeskFuncs *funcs, HWND slit) {
	f = funcs;
	my_plugin = plugin;

	memset(&wc, 0, sizeof(wc));

	wc.lpfnWndProc = wnd_proc;
	wc.hInstance = funcs->GetPluginInstance(plugin);
	wc.lpszClassName = TEXT("WezDeskOverview");
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);

	if (!RegisterClass(&wc)) {
		return 0;
	}

	funcs->DefineFunction(plugin, TEXT("show-overview"), show_overview);

	return (void*)1;
};

static int unload(Plugin plugin, WezDeskFuncs *funcs, int force)
{
	return 1;
}

static int on_notify(Plugin plugin, WezDeskFuncs *funcs, UINT code, UINT secondary, WPARAM wparam, LPARAM lparam)
{
	switch (code) {
		case WZN_DESKTOP_SWITCHED:
			close_overview(NULL);
			return 1;

		case WZN_ALT_TAB_ACTIVATE:
			/* TODO: if the overview is active, make it work like an alt-tab replacement */
			close_overview(NULL);
			return 0;
	}
	return 0;
}

static WezDeskPlugin the_plugin = {
	WEZDESK_API_NUMBER,
	TEXT("Task-switcher overview"),
	TEXT("Implements task switching via a visual overview of the windows on your desktop"),
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

