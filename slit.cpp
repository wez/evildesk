/* Copyright (c) 2004-2009 Wez Furlong.
 * This source is provided under terms of the GPLv2.
 * See the file named LICENSE for full details */
#include "wezdesk.h"
#include <Wtsapi32.h>
#include <commctrl.h>
#include <shlobj.h>
#include <vector>
#include <map>

#define USE_REGION 0

using namespace std;

/* implements the "slit" windows.
 * There is one slit for each side of the display.
 * Each one shrinks/grows to fit its contents, adjusting
 * the desktop work area appropriately.
 */

static vector <HWND> slit_list;
struct slit_info {
	HWND wnd;
	WORD align, grav;
	APPBARDATA abd;
	UINT slit_recalc_timer;
	vector <config_value> config;
	Image *bgtile;
	int floating;
	int auto_hide;
	int auto_hide_interval;
	int hidden;
	DWORD mouse_left;
	TCHAR *name;
};
static void do_hide_slit(HWND slit);


int is_slit(HWND w)
{
	int i;
	for (i = 0; i < slit_list.size(); i++) {
		if (slit_list[i] == w)
			return 1;
	}
	return 0;
}

HWND find_a_slit(HWND hWnd)
{
	if (slit_list.size()) {
		SetForegroundWindow(slit_list[0]);
		return slit_list[0];
	}
	return hWnd;
}

static BOOL CALLBACK notify_plugins_of_layout_change(HWND wnd, LPARAM p)
{
	struct slit_info *info = (struct slit_info*)p;
	SendMessage(wnd, WM_SLIT_LAYOUT_CHANGED, info->align, info->grav);
	return TRUE;
}

void destroy_slits(void)
{
	int i;
	for (i = 0; i < slit_list.size(); i++) {
#if 0
		struct slit_info *info = (struct slit_info*)GetWindowLongPtr(slit_list[i], GWLP_USERDATA);
		SHAppBarMessage(ABM_REMOVE, &info->abd);
#endif
		CloseWindow(slit_list[i]);
		DestroyWindow(slit_list[i]);
	}
}

/* magically get out of the way of applications that are going full screen */
void slit_hide_if_fullscreen(HWND wnd)
{
	HMONITOR mon = MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi = { sizeof(mi) };
	RECT r, isect;
	BOOL fs;
	int i;
	TCHAR buf[64];
#if 0
	WINDOWINFO wi;
	MINMAXINFO mmi;
	WINDOWPLACEMENT wp;
#endif

	if (is_slit(wnd)) return;

	if (!GetMonitorInfo(mon, &mi)) return;

#if 0
	/* hoops to jump through to get the rectangle actually occupied on screen */
	wi.cbSize = sizeof(wi);
	GetWindowInfo(wnd, &wi);
	SendMessage(wnd, WM_GETMINMAXINFO, NULL, (LPARAM) &mmi);
	wp.length = sizeof(wp);
	GetWindowPlacement(wnd, &wp);

	GetWindowText(wnd, buf, sizeof(buf) / sizeof(buf[0]));
#endif
	GetWindowRect(wnd, &r);

#if 0
	debug_printf(TEXT("fs: window name %s\r\n"), buf);
	debug_printf(TEXT("fs: window info rcWindow is [%d,%d,%d,%d]\r\n"), wi.rcWindow.left, wi.rcWindow.top, wi.rcWindow.right, wi.rcWindow.bottom);
	debug_printf(TEXT("fs: window info rcClient is [%d,%d,%d,%d]\r\n"), wi.rcClient.left, wi.rcClient.top, wi.rcClient.right, wi.rcClient.bottom);
	debug_printf(TEXT("fs: window plcmnt is [%d,%d]\r\n"), wp.ptMaxPosition.x, wp.ptMaxPosition.y);
	debug_printf(TEXT("fs: window max is [%d,%d,%d,%d]\r\n"), mmi.ptMaxPosition.x, mmi.ptMaxPosition.y, mmi.ptMaxSize.x, mmi.ptMaxSize.y);
	debug_printf(TEXT("fs: window is [%d,%d,%d,%d]\r\n"), r.left, r.top, r.right, r.bottom);
	debug_printf(TEXT("fs: monitor is [%d,%d,%d,%d]\r\n"), mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom);
#endif
	
	fs = IntersectRect(&isect, &r, &mi.rcMonitor);
//	debug_printf(TEXT("fs: intersect is [%d,%d,%d,%d]\r\n"), isect.left, isect.top, isect.right, isect.bottom);
	if (fs) {
		fs = EqualRect(&isect, &mi.rcMonitor);
//		debug_printf(TEXT("fs: equal: %d\r\n"), fs);
	}

	for (i = 0; i < slit_list.size(); i++) {
		struct slit_info *info = (struct slit_info*)
			GetWindowLongPtr(slit_list[i], GWLP_USERDATA);
		if (fs && !info->auto_hide) {
			SetWindowPos(slit_list[i], wnd, 0, 0, 0, 0,
				SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_FRAMECHANGED);
		} else {
			SetWindowPos(slit_list[i], HWND_TOPMOST, 0, 0, 0, 0,
				SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_FRAMECHANGED);
		}
	}
}

static TCHAR *slit_get_config(struct slit_info *info, TCHAR *name)
{
	int i;
	for (i = 0; i < info->config.size(); i++) {
		struct config_value cfg = info->config[i];
		if (!lstrcmp(cfg.name, name)) {
			return cfg.value;
		}
	}
	return NULL;
}

static void paint_slit(HDC hdc, HWND slit)
{
	struct slit_info *info = (struct slit_info*)GetWindowLongPtr(slit, GWLP_USERDATA);
	Graphics G(hdc);
	RECT r;
	GetClientRect(slit, &r);

	G.SetInterpolationMode(InterpolationModeHighQualityBicubic);

	if (!info->bgtile) {
		TCHAR *name = slit_get_config(info, TEXT("Background.Image"));
		if (!name) {
			name = core_funcs.GetFilePath(TEXT("default.png"));
		}
		if (name) {
			info->bgtile = new Image(name);
			free(name);
		}
	} 

	if (info->bgtile) {
		G.DrawImage(info->bgtile, r.left, r.top, RECTWIDTH(r), RECTHEIGHT(r));
	}
}

struct slit_metric {
	struct slit_info *info;
	RECT r;
	int num_kids;
	vector <HWND> kids;
};

static void add_client_to_metric(HWND hwnd, LPARAM lparam, BOOL force)
{
	struct slit_metric *metric = (struct slit_metric*)lparam;
	RECT r;

	if (!IsWindowVisible(hwnd) && !force)
		return;

	metric->kids.push_back(hwnd);
	metric->num_kids++;
	GetClientRect(hwnd, &r);

	switch (metric->info->align) {
		case WEZDESK_GRAVITY_LEFT:
		case WEZDESK_GRAVITY_RIGHT:
			/* vertical orientation */

			if (RECTWIDTH(r) > RECTWIDTH(metric->r))
				metric->r.right = RECTWIDTH(r);

			metric->r.bottom += RECTHEIGHT(r);

			break;

		case WEZDESK_GRAVITY_TOP:
		case WEZDESK_GRAVITY_BOTTOM:
			/* horizontal orientation */

			if (RECTHEIGHT(r) > RECTHEIGHT(metric->r))
				metric->r.bottom = RECTHEIGHT(r);

			metric->r.right += RECTWIDTH(r);
			break;
	}
}

static BOOL CALLBACK for_each_slit_client(HWND hwnd, LPARAM lparam)
{
	add_client_to_metric(hwnd, lparam, 0);
	return TRUE;
}

struct layout_info {
	HWND w;
	int grav, pri;
	int lo, hi;
};

static int compare_layout_info(const void *_a, const void *_b) {
	struct layout_info *a = (struct layout_info*)_a;
	struct layout_info *b = (struct layout_info*)_b;

	if (a->grav == b->grav) {
		if (a->pri > b->pri)
			return -1;
		if (b->pri > a->pri)
			return 1;

		return a - b;
	}
	
	if (a->grav == a->hi) {
		/* a has high gravity; it should sort before b */
		return -1;
	}

	if (b->grav == a->hi) {
		/* b has high gravity, and since a and b have different
		 * gravities, b must sort before a */
		return 1;
	}

	if (a->grav == a->lo)
		return 1;

	if (b->grav == a->lo)
		return -1;

	/* shouldn't happen */
	return a - b;
}

int get_slit_alignment(HWND slit)
{
	if (is_slit(slit)) {
		struct slit_info *info = 
			(struct slit_info*)GetWindowLongPtr(slit, GWLP_USERDATA);
		if (info) {
			return info->align;
		}
	}
	return WEZDESK_GRAVITY_LEFT;
}

static void recalc_slit_size(HWND slit, HWND forced_child = 0)
{
	struct slit_metric metric;
	struct slit_info *info;
	RECT d, r, mon;
	int grav_lo, grav_hi;
	int i, x, y;
#if USE_REGION
	HRGN rgn = NULL, krgn = NULL;
#endif

	info = (struct slit_info*)GetWindowLongPtr(slit, GWLP_USERDATA);

	memset(&metric, 0, sizeof(metric));
	metric.info = info;
	
	/* determine the size of the primary monitor, and work from there.
	 * This is needed for the case when the user switches primary monitors in
	 * a multi-mon setup; the new primary may have had bogus dimensions hanging
	 * around, which then cause the workarea to be set too small.  The end
	 * result of this situation is that you can only move windows around within
	 * a small area on the screen.
	 * */
	HMONITOR pri_mon;
	MONITORINFO mon_info;

	SystemParametersInfo(SPI_GETWORKAREA, 0, &d, 0);
	pri_mon = MonitorFromRect(&d, MONITOR_DEFAULTTOPRIMARY);

	mon_info.cbSize = sizeof(mon_info);
	GetMonitorInfo(pri_mon, &mon_info);

	mon = mon_info.rcMonitor;

	EnumChildWindows(slit, for_each_slit_client, (LPARAM)&metric);
	if (forced_child && metric.num_kids == 0) {
		add_client_to_metric(forced_child, (LPARAM)&metric, TRUE);
	}
//	debug_printf(TEXT("%d children, using max area: %d x %d\r\n"), metric.num_kids, metric.r.right, metric.r.bottom);

	d = mon;

	int slit_x;
	int slit_y;

	switch (info->align) {
		case WEZDESK_GRAVITY_RIGHT:
			slit_x = d.right - RECTWIDTH(metric.r);
			slit_y = info->grav == WEZDESK_GRAVITY_TOP ? 
						0 : d.bottom - RECTHEIGHT(metric.r);
			if (info->grav == WEZDESK_GRAVITY_MIDDLE) {
				slit_y = (RECTHEIGHT(d) - RECTHEIGHT(metric.r)) / 2;
			}

			grav_hi = WEZDESK_GRAVITY_TOP;
			grav_lo = WEZDESK_GRAVITY_BOTTOM;
			info->abd.uEdge = ABE_RIGHT;
			break;

		case WEZDESK_GRAVITY_LEFT:
			slit_x = 0;
			slit_y = info->grav == WEZDESK_GRAVITY_TOP ?
						0 : d.bottom - RECTHEIGHT(metric.r);
			if (info->grav == WEZDESK_GRAVITY_MIDDLE) {
				slit_y = (RECTHEIGHT(d) - RECTHEIGHT(metric.r)) / 2;
			}
			grav_hi = WEZDESK_GRAVITY_TOP;
			grav_lo = WEZDESK_GRAVITY_BOTTOM;
			info->abd.uEdge = ABE_LEFT;
			break;

		case WEZDESK_GRAVITY_TOP:
			slit_x = info->grav == WEZDESK_GRAVITY_RIGHT ? 
						(RECTWIDTH(d) - RECTWIDTH(metric.r)) : 0;
			if (info->grav == WEZDESK_GRAVITY_MIDDLE) {
				slit_x = (RECTWIDTH(d) - RECTWIDTH(metric.r)) / 2;
			}
			slit_y = 0;
			grav_hi = WEZDESK_GRAVITY_LEFT;
			grav_lo = WEZDESK_GRAVITY_RIGHT;
			info->abd.uEdge = ABE_TOP;
			break;

		case WEZDESK_GRAVITY_BOTTOM:
			slit_x = info->grav == WEZDESK_GRAVITY_RIGHT ?
						(RECTWIDTH(d) - RECTWIDTH(metric.r)) : 0;
			if (info->grav == WEZDESK_GRAVITY_MIDDLE) {
				slit_x = (RECTWIDTH(d) - RECTWIDTH(metric.r)) / 2;
			}
			slit_y = d.bottom - RECTHEIGHT(metric.r);

			grav_hi = WEZDESK_GRAVITY_LEFT;
			grav_lo = WEZDESK_GRAVITY_RIGHT;
			info->abd.uEdge = ABE_BOTTOM;
			break;
		default:
			debug_printf(TEXT("Bad:%s:%d\r\n"), __FILE__, __LINE__);
	}

	if (info->auto_hide_interval && info->hidden) {
		SetWindowPos(slit, NULL, -1, -1,
			RECTWIDTH(metric.r), RECTHEIGHT(metric.r),
			SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOZORDER);
		//do_hide_slit(slit);
		info->hidden = 1;
	} else {
		MoveWindow(slit, slit_x, slit_y,
			RECTWIDTH(metric.r), RECTHEIGHT(metric.r), TRUE);
	}

	/* now, we want to lay out the clients for this slit.
	 * We sort them according to gravity, priority descending */

	struct layout_info *li = new struct layout_info[metric.num_kids];
	for (i = 0; i < metric.num_kids; i++) {
		li[i].w = metric.kids[i];
		core_funcs.GetGravity(li[i].w, &li[i].grav, &li[i].pri);
		li[i].lo = grav_lo;
		li[i].hi = grav_hi;

		/* fixup bogus alignment */
		switch (info->align) {
			case WEZDESK_GRAVITY_RIGHT:
			case WEZDESK_GRAVITY_LEFT:
				if (li[i].grav == WEZDESK_GRAVITY_LEFT) {
					li[i].grav = WEZDESK_GRAVITY_TOP;
				} else if (li[i].grav == WEZDESK_GRAVITY_RIGHT) {
					li[i].grav = WEZDESK_GRAVITY_BOTTOM;
				}
				break;
			case WEZDESK_GRAVITY_TOP:
			case WEZDESK_GRAVITY_BOTTOM:
				if (li[i].grav == WEZDESK_GRAVITY_TOP) {
					li[i].grav = WEZDESK_GRAVITY_LEFT;
				} else if (li[i].grav == WEZDESK_GRAVITY_BOTTOM) {
					li[i].grav = WEZDESK_GRAVITY_RIGHT;
				}
				break;
		}
	}
	qsort(li, metric.num_kids, sizeof(li[0]), compare_layout_info);
	int last_grav = -1;

	switch (info->align) {
		case WEZDESK_GRAVITY_RIGHT:
		case WEZDESK_GRAVITY_LEFT:
#if SLIT_DRAW_EDGE
			x = 1;
#else
			x = 0;
#endif
			y = 0;

			for (i = 0; i < metric.num_kids; i++) {
				GetClientRect(li[i].w, &r);

				if (li[i].grav != last_grav && li[i].grav == WEZDESK_GRAVITY_BOTTOM) {
					y = RECTHEIGHT(metric.r);
				}

				if (li[i].grav == WEZDESK_GRAVITY_BOTTOM) {
					y -= RECTHEIGHT(r);
				}

				/* center horizontally */
				x = (RECTWIDTH(metric.r) - RECTWIDTH(r)) / 2;
				
//				debug_printf(TEXT("moving slit item with gravity %d to %d,%d\r\n"), li[i].grav, x, y);
				if (!SetWindowPos(li[i].w, NULL, x, y, -1, -1, SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOZORDER)) {
					debug_printf(TEXT("--- failed to move %d\r\n"), GetLastError());
				}
				
				if (li[i].grav == WEZDESK_GRAVITY_TOP) {
					y += RECTHEIGHT(r);
				}
				last_grav = li[i].grav;

#if USE_REGION
				x = 0;
				krgn = CreateRectRgn(x, y, x + RECTWIDTH(r), y + RECTHEIGHT(r));
				if (rgn) {
					CombineRgn(rgn, rgn, krgn, RGN_OR);
					DeleteObject(krgn);
				} else {
					rgn = krgn;
				}
#endif
			}

			break;

		case WEZDESK_GRAVITY_TOP:
		case WEZDESK_GRAVITY_BOTTOM:
			x = 0;
			y = 0;

			for (i = 0; i < metric.num_kids; i++) {
				GetClientRect(li[i].w, &r);

				if (li[i].grav != last_grav && li[i].grav == WEZDESK_GRAVITY_RIGHT) {
					x = RECTWIDTH(metric.r);
				}

				if (li[i].grav == WEZDESK_GRAVITY_RIGHT) {
					x -= RECTWIDTH(r);
				}
				/* center vertically */
				y = (RECTHEIGHT(metric.r) - RECTHEIGHT(r)) / 2;
				
				debug_printf(TEXT("moving slit item with gravity %d to %d,%d\r\n"), li[i].grav, x, y);
				if (!SetWindowPos(li[i].w, NULL, x, y, -1, -1, SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOZORDER)) {
					debug_printf(TEXT("--- failed to move %d\r\n"), GetLastError());
				}
				
				if (li[i].grav == WEZDESK_GRAVITY_LEFT) {
					x += RECTWIDTH(r);
				}
				last_grav = li[i].grav;

#if USE_REGION
				y = 0;
				krgn = CreateRectRgn(x, y, x + (r.right - r.left), y + (r.bottom - r.top));
				if (rgn) {
					CombineRgn(rgn, rgn, krgn, RGN_OR);
					DeleteObject(krgn);
				} else {
					rgn = krgn;
				}
#endif
			}

			break;
	}

#if USE_REGION
	if (rgn) {
		if (!SetWindowRgn(slit, rgn, TRUE)) {
			debug_printf(TEXT("---- failed to set region %d\r\n"), GetLastError());
			DeleteObject(rgn);
		}
	}
#endif

	delete [] li;

	/* determine the new work area based on the sizes of all slits */
	d = mon;
	info = NULL;

	WORD left = 0, right = 0, top = 0, bottom = 0;

	for (i = 0; i < slit_list.size(); i++) {
		HWND aslit = slit_list[i];

		info = (struct slit_info*)GetWindowLongPtr(aslit, GWLP_USERDATA);

		if (info->floating) continue;
		
		GetWindowRect(aslit, &r);

		info->abd.rc = r;
		//SHAppBarMessage(ABM_SETPOS, &info->abd);
		
		switch (info->align) {
			case WEZDESK_GRAVITY_LEFT:
				if (info->auto_hide_interval) {
					if (info->auto_hide > left) {
						left = info->auto_hide;
					}
				} else if (r.right - r.left > left) {
					left = r.right - r.left;
				}
				break;
			case WEZDESK_GRAVITY_RIGHT:
				if (info->auto_hide_interval) {
					if (info->auto_hide > right) {
						right = info->auto_hide;
					}
				} else if (r.right - r.left > right) {
					right = r.right - r.left;
				}
				break;
			case WEZDESK_GRAVITY_TOP:
				if (info->auto_hide_interval) {
					if (info->auto_hide > top) {
						top = info->auto_hide;
					}
				} else if (r.bottom - r.top > top) {
					top = r.bottom - r.top;
				}
				break;
			case WEZDESK_GRAVITY_BOTTOM:
				if (info->auto_hide_interval) {
					if (info->auto_hide > bottom) {
						bottom = info->auto_hide;
					}
				} else if (r.bottom - r.top > bottom) {
					bottom = r.bottom - r.top;
				}
				break;
		}
	}
	
	d.left += left;
	d.right -= right;
	d.top += top;
	d.bottom -= bottom;

//	debug_printf(TEXT("setting work area to %d,%d %d,%d\n"), d.left, d.top, d.right, d.bottom);

	SystemParametersInfo(SPI_SETWORKAREA, 0, &d, SPIF_SENDCHANGE);
	//RedrawWindow(slit, NULL, NULL, RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
	InvalidateRect(slit, NULL, TRUE);
	//UpdateWindow(slit);
}

static void save_slit_state(struct slit_info *info)
{
	HKEY key;
	TCHAR regpath[MAX_PATH];

	if (!info->name) return;

	StringCbPrintf(regpath, sizeof(regpath),
			TEXT("Software\\Evil, as in Dr.\\WezDesk\\slits\\%s"),
			info->name);

	if (ERROR_SUCCESS == create_reg_key(HKEY_CURRENT_USER,
				regpath, &key)) {
		DWORD val;

		val = info->align;
		RegSetValueEx(key, TEXT("align"), 0, REG_DWORD,
				(LPBYTE)&val, sizeof(val));

		val = info->grav;
		RegSetValueEx(key, TEXT("gravity"), 0, REG_DWORD,
				(LPBYTE)&val, sizeof(val));

		val = info->floating;
		RegSetValueEx(key, TEXT("floating"), 0, REG_DWORD,
				(LPBYTE)&val, sizeof(val));

		val = info->auto_hide;
		RegSetValueEx(key, TEXT("autohide"), 0, REG_DWORD,
				(LPBYTE)&val, sizeof(val));

		val = info->auto_hide_interval;
		RegSetValueEx(key, TEXT("autohide.interval"), 0, REG_DWORD,
				(LPBYTE)&val, sizeof(val));

		RegCloseKey(key);

		if (info->auto_hide_interval) {
			if (!info->auto_hide) info->auto_hide = 2;
			info->floating = 0;
			info->slit_recalc_timer = SetTimer(info->wnd, 2,
					info->auto_hide_interval, NULL);
			info->mouse_left = GetTickCount();
			info->hidden = 0;
		}
	}
}

void do_slit_autohide(void *arg, HWND slit)
{
	if (!is_slit(slit)) {
		slit = GetParent(slit);
		if (!is_slit(slit)) return;
	}
	struct slit_info *info = 
		(struct slit_info*)GetWindowLongPtr(slit, GWLP_USERDATA);
	if (!info) return;
	TCHAR *num = (TCHAR*)arg;

	if (num[0] == '~') {
		info->auto_hide = info->auto_hide ? 0 : 2;
	} else {
		info->auto_hide = _wtoi((TCHAR*)arg);
	}
	if (info->auto_hide) {
		if (!info->auto_hide_interval) {
			info->auto_hide_interval = 1400;
		}
		info->floating = 0;

		info->slit_recalc_timer = SetTimer(slit, 2, info->auto_hide_interval, NULL);
		info->mouse_left = GetTickCount();
	} else {
		KillTimer(slit, info->slit_recalc_timer);
		info->slit_recalc_timer = 0;
		info->auto_hide_interval = 0;
	}
	info->hidden = 0;
	save_slit_state(info);
	recalc_slit_size(slit, 0);
}

void do_slit_float(void *arg, HWND slit)
{
	if (!is_slit(slit)) {
		slit = GetParent(slit);
		if (!is_slit(slit)) return;
	}
	struct slit_info *info = 
		(struct slit_info*)GetWindowLongPtr(slit, GWLP_USERDATA);
	if (!info) return;
	TCHAR *num = (TCHAR*)arg;

	if (num[0] == '~') {
		info->floating = !info->floating;
	} else {
		info->floating = _wtoi((TCHAR*)arg);
	}
	if (info->floating) {
		info->auto_hide = 0;
	}
	info->hidden = 0;
	save_slit_state(info);
	recalc_slit_size(slit, 0);
}


static void do_slit_align_grav_change(TCHAR *align, TCHAR *grav, HWND slit)
{
	if (!is_slit(slit)) {
		slit = GetParent(slit);
		if (!is_slit(slit)) return;
	}
	struct slit_info *info = 
		(struct slit_info*)GetWindowLongPtr(slit, GWLP_USERDATA);
	if (!info) return;

	if (align) {
		info->align = resolve_grav(align);
	}
	if (grav) {
		info->grav = resolve_grav(grav);
	}

	EnumChildWindows(slit, notify_plugins_of_layout_change, (LPARAM)info);

	save_slit_state(info);
	recalc_slit_size(slit, 0);
}

void do_slit_align(void *arg, HWND slit)
{
	do_slit_align_grav_change((TCHAR*)arg, NULL, slit);
}

void do_slit_gravity(void *arg, HWND slit)
{
	do_slit_align_grav_change(NULL, (TCHAR*)arg, slit);
}

void do_hide_slit(HWND slit)
{
	RECT r;
	struct slit_info *info = (struct slit_info*)GetWindowLongPtr(slit, GWLP_USERDATA);
	GetWindowRect(slit, &r);
	switch (info->align) {
		case WEZDESK_GRAVITY_RIGHT:
			SetWindowPos(slit, NULL, r.right - info->auto_hide,
					r.top, -1, -1, SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOZORDER);
			break;
		case WEZDESK_GRAVITY_LEFT:
			SetWindowPos(slit, NULL,
					(r.left + info->auto_hide) - RECTWIDTH(r),
					r.top, -1, -1, SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOZORDER);
			break;
		case WEZDESK_GRAVITY_TOP:
			SetWindowPos(slit, NULL,
					r.left, (r.top + info->auto_hide) - RECTHEIGHT(r),
					-1, -1, SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOZORDER);
			break;
		case WEZDESK_GRAVITY_BOTTOM:
			SetWindowPos(slit, NULL,
					r.left, r.bottom - info->auto_hide,
					-1, -1, SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOZORDER);
			break;
	}
	info->hidden = 1;
}

void do_show_slit(HWND slit)
{
	struct slit_info *info = (struct slit_info*)GetWindowLongPtr(slit, GWLP_USERDATA);
	if (info->hidden) {
		info->hidden = 0;
		recalc_slit_size(slit, 0);
	}
}

static LRESULT CALLBACK slit_client_subclass_proc(
	HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	switch (uMsg) {
		case WM_SIZE:
//			recalc_slit_size(GetParent(hWnd));
			PostMessage(GetParent(hWnd), WM_SLIT_CHILD_RESIZED, 0, 0);
			break;

		case WM_DESTROY:
			RemoveWindowSubclass(hWnd, slit_client_subclass_proc, 0);
			break;

		/* override the default background erase so that the slit fill
		 * is used instead. */
		case WM_ERASEBKGND:
			return 1;

		case WM_MOUSEMOVE:
		{
			struct slit_info *info = (struct slit_info*)
				GetWindowLongPtr(GetParent(hWnd), GWLP_USERDATA);
			
			TRACKMOUSEEVENT evt;
			memset(&evt, 0, sizeof(evt));
			evt.cbSize = sizeof(evt);
			evt.hwndTrack = hWnd;
			evt.dwFlags = TME_LEAVE;
			TrackMouseEvent(&evt);

			if (info->hidden) {
				do_show_slit(GetParent(hWnd));
			} else if (info->auto_hide_interval) {
				info->mouse_left = 0;//GetTickCount();
			}
			break;
		}
		case WM_ACTIVATE: 
			if (wParam == WA_INACTIVE) {
				struct slit_info *info = (struct slit_info*)
					GetWindowLongPtr(GetParent(hWnd), GWLP_USERDATA);
				info->mouse_left = GetTickCount();
			} else {
				do_show_slit(GetParent(hWnd));
			}
			break;

		case WM_ACTIVATEAPP:
			if (wParam == FALSE) {
				struct slit_info *info = (struct slit_info*)
					GetWindowLongPtr(GetParent(hWnd), GWLP_USERDATA);
				info->mouse_left = GetTickCount();
			} else {
				do_show_slit(GetParent(hWnd));
			}
			break;

		case WM_MOUSELEAVE:
		{
			struct slit_info *info = (struct slit_info*)
				GetWindowLongPtr(GetParent(hWnd), GWLP_USERDATA);
			if (info->auto_hide_interval) {
				info->mouse_left = GetTickCount();
			}
			break;
		}
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK slit_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);
			paint_slit(hdc, hWnd);
			EndPaint(hWnd, &ps);
			break;
		}

		case WM_ERASEBKGND:
			//paint_slit((HDC)wParam, hWnd);
			return 1;

		case WM_CREATE:
		{
			struct slit_info *info = (struct slit_info*)((LPCREATESTRUCT)lParam)->lpCreateParams;
			info->wnd = hWnd;
			SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)info);
			SetLayeredWindowAttributes(hWnd, 0, (255 * 70) / 100, LWA_ALPHA);
			WTSRegisterSessionNotification(hWnd, NOTIFY_FOR_THIS_SESSION);
			return 0;
		}

		case WM_DESTROY:
		{
			struct slit_info *info = (struct slit_info*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
			if (info->bgtile) {
				delete info->bgtile;
			}
			if (info->name) {
				free(info->name);
			}
			int i;
			for (i = 0; i < info->config.size(); i++) {
				struct config_value cfg = info->config[i];
				free(cfg.name);
				free(cfg.value);
			}
			delete info;
			return DefWindowProc(hWnd, uMsg, wParam, lParam);
		}

		case WM_DISPLAYCHANGE:
		case WM_DEVMODECHANGE:
//		case WM_SETTINGCHANGE:
		case WM_SYSCOLORCHANGE:
		case WM_THEMECHANGED:
		case WM_WTSSESSION_CHANGE:
			do_show_slit(hWnd);
			recalc_slit_size(hWnd);
			return 0;

		case WM_SLIT_CHILD_RESIZED:
			recalc_slit_size(hWnd);
			return 0;	

		case WM_PARENTNOTIFY:
			debug_printf(TEXT("got PARENTNOTIFY\r\n"));
			switch (LOWORD(wParam)) {
				case WM_CREATE:
					/* hook the window so we can notice it resizing */
					if (!SetWindowSubclass((HWND)lParam, slit_client_subclass_proc, 0, NULL)) {
						debug_printf(TEXT("failed to set subclass on client window; %d\r\n"), GetLastError());
					}
					recalc_slit_size(hWnd, (HWND)lParam);
					break;

				case WM_DESTROY:
					recalc_slit_size(hWnd);
					break;
			}
			return DefWindowProc(hWnd, uMsg, wParam, lParam);

		case WM_CONTEXTMENU:
		{
			popup_context_menu(LOWORD(lParam), HIWORD(lParam), hWnd, TEXT("root"));
			return 1;
		}

#if 0
		case WM_USER+100:
		{
			/* An AppBar Message */
			switch (wParam) {
				case ABN_WINDOWARRANGE:
					debug_printf(TEXT("APPBAR: WINDOARRANGE\n"));
					break;
				case ABN_STATECHANGE:
					debug_printf(TEXT("APPBAR: STATECHANGE\n"));
					break;
				case ABN_FULLSCREENAPP:
					debug_printf(TEXT("APPBAR: FULLSCREENAPP\n"));
					break;
				case ABN_POSCHANGED:
					debug_printf(TEXT("APPBAR: POSCHANGED\n"));
					break;
				default:
					debug_printf(TEXT("APPBAR: %d\n"), wParam);
			}
		}
#endif

		case WM_ACTIVATEAPP:
			if (wParam == FALSE) {
				struct slit_info *info = (struct slit_info*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
				info->mouse_left = GetTickCount();
			} else {
				do_show_slit(hWnd);
			}
			return context_menu_proc(hWnd, uMsg, wParam, lParam);

		case WM_ACTIVATE: 
			if (wParam == WA_INACTIVE) {
				struct slit_info *info = (struct slit_info*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
				info->mouse_left = GetTickCount();
			} else {
				do_show_slit(hWnd);
			}
			return context_menu_proc(hWnd, uMsg, wParam, lParam);

		case WM_TIMER: {
			struct slit_info *info = (struct slit_info*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
			if (!info->hidden && info->mouse_left &&
					info->mouse_left + info->auto_hide_interval <= GetTickCount()) {
				/* we need to hide */
				do_hide_slit(hWnd);
			}
			return 1;
		}
		
		case WM_MOUSELEAVE: {
			struct slit_info *info = (struct slit_info*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
			if (info->auto_hide_interval) {
				info->mouse_left = GetTickCount();
			}
			return 1;
		}
		
		case WM_MOUSEMOVE: {
			struct slit_info *info = (struct slit_info*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
			if (info->auto_hide_interval) {
				do_show_slit(hWnd);
			}
			TRACKMOUSEEVENT evt;
			memset(&evt, 0, sizeof(evt));
			evt.cbSize = sizeof(evt);
			evt.hwndTrack = hWnd;
			evt.dwFlags = TME_LEAVE;
			TrackMouseEvent(&evt);
			info->mouse_left = 0;//GetTickCount();
			return 1;
		}

		default:
			return context_menu_proc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}

#define SLIT_EX		WS_EX_COMPOSITED|WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_LAYERED
#define SLIT_REG	WS_POPUP|WS_CLIPCHILDREN|WS_VISIBLE
static WNDCLASS wc;

HWND create_new_slit(WORD align, WORD grav, TCHAR *name)
{
	HWND w;
	struct slit_info *info = new slit_info;

	memset(info, 0, sizeof(*info));
	info->align = align;
	info->grav = grav;
	info->name = lstrdup(name);

	w = CreateWindowEx(SLIT_EX,
		wc.lpszClassName,
		NULL,
		SLIT_REG,
		0, 0,
		0, 0,
		NULL, NULL,
		wc.hInstance,
		info);

	/* load state from registry */
	if (name) {
		HKEY key;
		TCHAR regpath[MAX_PATH];
		StringCbPrintf(regpath, sizeof(regpath),
				TEXT("Software\\Evil, as in Dr.\\WezDesk\\slits\\%s"),
				info->name);
		if (ERROR_SUCCESS == create_reg_key(HKEY_CURRENT_USER,
					regpath, &key)) {
			DWORD val;
			DWORD len;

			len = sizeof(val);
			if (ERROR_SUCCESS == RegQueryValueEx(key, TEXT("align"), NULL, NULL,
						(LPBYTE)&val, &len)) {
				info->align = val;
			}
			len = sizeof(val);
			if (ERROR_SUCCESS == RegQueryValueEx(key, TEXT("gravity"), NULL, NULL,
						(LPBYTE)&val, &len)) {
				info->grav = val;
			}
			len = sizeof(val);
			if (ERROR_SUCCESS == RegQueryValueEx(key, TEXT("floating"), NULL, NULL,
						(LPBYTE)&val, &len)) {
				info->floating = val;
			}
			len = sizeof(val);
			if (ERROR_SUCCESS == RegQueryValueEx(key, TEXT("autohide"), NULL, NULL,
						(LPBYTE)&val, &len)) {
				info->auto_hide = val;
			}
			len = sizeof(val);
			if (ERROR_SUCCESS == RegQueryValueEx(key, TEXT("autohide.interval"), NULL, NULL,
						(LPBYTE)&val, &len)) {
				info->auto_hide_interval = val;
			}

			RegCloseKey(key);

			if (info->auto_hide_interval) {
				if (!info->auto_hide) info->auto_hide = 2;
				info->floating = 0;
				info->slit_recalc_timer = 
					SetTimer(info->wnd, 2, info->auto_hide_interval, NULL);
				info->mouse_left = GetTickCount();
				info->hidden = 0;
			}
		}
	}


#if 0
	if (w) {
		info->abd.cbSize = sizeof(info->abd);
		info->abd.hWnd = w;
		info->abd.uCallbackMessage = WM_USER + 100;
		if (!SHAppBarMessage(ABM_NEW, &info->abd)) {
			debug_printf(TEXT("ShAppBarMessage(ABM_NEW) returned false\n"));
		}
	}
#endif
	slit_list.push_back(w);

	return w;
}

void init_slit(void)
{

	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = slit_proc;
	wc.hInstance = GetModuleHandle(NULL);
	wc.lpszClassName = TEXT("WezDeskSlit");
	wc.hbrBackground = (HBRUSH)(1 + COLOR_APPWORKSPACE);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;

	if (!RegisterClass(&wc)) {
		debug_printf(TEXT("failed to register slit!\r\n"));
		return;
	}
}

void slit_set_config(HWND wnd, TCHAR *name, TCHAR *value)
{
	struct config_value config;
	struct slit_info *info = (struct slit_info*)GetWindowLongPtr(wnd, GWLP_USERDATA);

	config.name = lstrdup(name);
	config.value = lstrdup(value);

	info->config.push_back(config);
	if (!lstrcmpi(name, TEXT("Floating"))) {
		info->floating = _wtoi(value);
	}
	if (!lstrcmpi(name, TEXT("AutoHide.Interval"))) {
		info->auto_hide_interval = _wtoi(value);
	}
	if (!lstrcmpi(name, TEXT("AutoHide.Size"))) {
		info->auto_hide = _wtoi(value);
	}
	if (info->auto_hide_interval) {
		if (!info->auto_hide) info->auto_hide = 2;
		info->floating = 0;
		info->slit_recalc_timer = SetTimer(wnd, 2, info->auto_hide_interval, NULL);
		info->mouse_left = GetTickCount();
		info->hidden = 0;
	}
}

