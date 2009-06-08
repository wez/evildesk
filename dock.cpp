/* Copyright (c) 2004-2009 Wez Furlong.
 * This source is provided under terms of the GPLv2.
 * See the file named LICENSE for full details */
#include "wezdeskapi.h"
#include "wezdeskres.h"
#include <shlobj.h>
#include <shellapi.h>
#include <vector>
#include <algorithm>
#include <gdiplus.h>
#define STRSAFE_LIB
#include <strsafe.h>

using namespace std;
using namespace Gdiplus;

#define TASK_SCAN_INTERVAL 1200
#define ICON_GATHER_TIMEOUT 1200

static Image *cue_arrow = NULL;
static UINT_PTR timer;
static WezDeskFuncs *f;
static Plugin my_plugin;
static HWND tooltips = NULL;
static int is_vert = 1;
static int slit_align;
static DWORD dir_change_time = 0;
static DWORD task_scan_time = 0;

static HWND dock;
static HANDLE hqldir;
static TCHAR dockdir[MAX_PATH];
static TCHAR dockdirandpat[MAX_PATH];
static int zorder_counter;
static int ICON_PAD = 4;
static int ICON_SIZE = 32;
static int QL_WIDTH = 48;
static int QL_HEIGHT = 40;
static BYTE _file_notify_information[4096];
static OVERLAPPED olap;
static int has_changed = 0;
static void update_view(void);
static void calc_item_rect(int i, RECT *r, int ticks);
static TCHAR launch_text[256];
static TCHAR properties_text[256];

struct running_item {
	HWND wnd;
	int flashing;
	int flash_ticks;
	int zorder;

	bool operator < (const struct running_item &b) {
		if (flashing && !b.flashing) {
			return true;
		}
		if (b.flashing && !flashing) {
			return false;
		}
		if (GetForegroundWindow() == wnd) {
			return true;
		}
		if (zorder < b.zorder) return true;
		return false;
	}
};

class dock_item {
public:
	enum {
		dock_is_resident,
		dock_is_task
	} type;
	HICON icon;
	TOOLINFO ti;
	int hot;
	int icon_to_kill;
	vector <struct running_item> apps;
	/* if resident, these are used to launch the app,
	 * and to tie a running window back to the icon */
	TCHAR exec[MAX_PATH];
	TCHAR module_path[MAX_PATH];
	TCHAR icon_text[MAX_PATH];
	TCHAR tip_text[MAX_PATH];
	DWORD last_tip_change;

	~dock_item() {
		if (icon_to_kill) {
			DestroyIcon(this->icon);
		}
		delete_tip();
	}
	void delete_tip() {
		if (ti.cbSize != sizeof(ti)) return;
		SendMessage(tooltips, TTM_DELTOOL, 0, (LPARAM)&ti);
		memset(&ti, 0, sizeof(ti));
	}
	void populate_tip(int i) {
		calc_item_rect(i, &ti.rect, 0);
		if (apps.size()) {
			sort_apps();
			GetWindowText(apps[0].wnd, tip_text,
					sizeof(tip_text)/sizeof(tip_text[0]));
			ti.lpszText = tip_text;
		} else {
			ti.lpszText = icon_text;
		}
	}
	void update_tip(int i) {
		if (ti.cbSize == sizeof(ti) && ti.uId != i) {
			delete_tip();
		}
		if (ti.cbSize != sizeof(ti)) {
			memset(&ti, 0, sizeof(ti));
			ti.uId = i;
			ti.cbSize = sizeof(ti);
			ti.uFlags = TTF_SUBCLASS;
			ti.hwnd = dock;
			ti.hinst = f->GetPluginInstance(my_plugin);
			populate_tip(i);
			SendMessage(tooltips, TTM_ADDTOOL, 0, (LPARAM)&ti);
		} else {
			if (last_tip_change + TASK_SCAN_INTERVAL <= GetTickCount()) {
				populate_tip(i);
				SendMessage(tooltips, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
				last_tip_change = GetTickCount();
			}
		}
	}

	dock_item() {
		last_tip_change = 0;
		ti.cbSize = 0;
		hot = 0;
		icon_to_kill = 0;
		exec[0] = 0;
		module_path[0] = 0;
	}

	void sort_apps() {
		sort(apps.begin(), apps.end());
	}
};

struct dock {
	TCHAR exec[MAX_PATH];
	TCHAR icon_text[MAX_PATH];
	SHFILEINFO info;
	int hot;
};
vector <struct dock_item *> items;

static void calc_item_rect(int i, RECT *r, int ticks)
{
	if (is_vert) {
		r->left = (QL_WIDTH - ICON_SIZE) / 2;
		r->top = ICON_PAD + (i * (ICON_SIZE+ICON_PAD));

		if (ticks) {
			if (ticks < ICON_SIZE / 2) {
				r->left -= ticks;
			} else {
				r->left += ticks - (ICON_SIZE/2);
			}
		}
	} else {
		r->top = (QL_HEIGHT - ICON_SIZE) / 2;
		r->left = ICON_PAD + (i * (ICON_SIZE+ICON_PAD));
		if (ticks) {
			if (ticks < ICON_SIZE / 2) {
				r->top -= ticks;
			} else {
				r->top += ticks - (ICON_SIZE/2);
			}
		}
	}
	r->bottom = r->top + ICON_SIZE;
	r->right = r->left + ICON_SIZE;
}

static void get_module_name(dock_item *di)
{
	HRESULT res;
	IShellLink *psl;
	WIN32_FIND_DATA wfd;

	res = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
			IID_IShellLink, (LPVOID*)&psl);
	if (SUCCEEDED(res)) {
		IPersistFile *ppf;

		res = psl->QueryInterface(IID_IPersistFile, (void**)&ppf);
		if (SUCCEEDED(res)) {
			res = ppf->Load(di->exec, STGM_READ);
			if (SUCCEEDED(res)) {
				res = psl->Resolve(dock, 0);
				if (SUCCEEDED(res)) {
					res = psl->GetPath(di->module_path,
							sizeof(di->module_path)/sizeof(di->module_path[0]),
							&wfd, SLGP_UNCPRIORITY);
					if (FAILED(res)) {
						StringCbCopy(di->module_path, sizeof(di->module_path), di->exec);
					} else {
						f->Trace(TEXT("resolved module_path is %s\n"),
							di->module_path);
					}
				} else {
					res = psl->GetPath(di->module_path,
							sizeof(di->module_path)/sizeof(di->module_path[0]),
							&wfd, SLGP_UNCPRIORITY);
					if (FAILED(res)) {
						StringCbCopy(di->module_path, sizeof(di->module_path), di->exec);
					} else {
						f->Trace(TEXT("NON-resolved module_path is %s\n"),
							di->module_path);
					}
				}
			}
			ppf->Release();
		}
		psl->Release();
	}
	if (!lstrlen(di->module_path)) {
		StringCbCopy(di->module_path, sizeof(di->module_path), di->exec);
	}
}

static void scan_dir(void)
{
	HANDLE h;
	WIN32_FIND_DATA fd;
	int i, q;
	vector <struct dock> qitems;
	vector <struct dock_item*>::iterator iter;
	dock_item *di;

	dir_change_time = 0;

	h = FindFirstFile(dockdirandpat, &fd);
	i = 0;
	if (h != INVALID_HANDLE_VALUE) {
		do {
			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) 
					== FILE_ATTRIBUTE_HIDDEN) {
				continue;
			}
			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
					== FILE_ATTRIBUTE_DIRECTORY) {
				continue;
			}

			struct dock item;
			TCHAR *bs;

			memset(&item, 0, sizeof(item));
			StringCbPrintf(item.exec, sizeof(item.exec), TEXT("%s\\%s"),
					dockdir, fd.cFileName);
			item.hot = 0;

			StringCbCopy(item.icon_text, sizeof(item.icon_text),
				item.exec + lstrlen(item.exec) - lstrlen(fd.cFileName));
			bs = item.icon_text + lstrlen(fd.cFileName) - 1;
			while (*bs != '.')
				--bs;

			if (!lstrcmpi(bs, TEXT(".scf"))) {
				/* skip explorer.exe command */
				continue;
			}
				
			if (!lstrcmpi(bs, TEXT(".lnk")) || !lstrcmpi(bs, TEXT(".url"))) {
				*bs = '\0';
			}

			if (!SHGetFileInfo(item.exec, 0, &item.info, sizeof(item.info),
					SHGFI_ICON|SHGFI_LARGEICON)) {
				continue;
			}

			ICONINFO info;
			BITMAP bm;

			if (GetIconInfo(item.info.hIcon, &info) &&
					GetObject(info.hbmColor, sizeof(bm), &bm)) {
				if (bm.bmWidth) {
					ICON_SIZE = bm.bmWidth;
				}
			}

			qitems.push_back(item);

			i++;

		} while (FindNextFile(h, &fd));
		FindClose(h);
	}

	/* now find any resident icons that may have been deleted */
scan_again:
	int found = 0;
	for (iter = items.begin(); iter != items.end(); iter++) {
		di = *iter;
		if (di->type != dock_item::dock_is_resident) continue;
		found = 0;
		for (q = 0; q < qitems.size(); q++) {
			if (!lstrcmpi(qitems[q].exec, di->exec)) {
				found = 1;
				break;
			}
		}

		if (!found) {
			/* this guy is no longer in the quick launch.
			 * If we have running tasks associated with us,
			 * we need to demote this entry to a task, otherwise
			 * we should delete it */
			if (di->apps.size()) {
				di->type = dock_item::dock_is_task;
			} else {
				items.erase(iter);
				delete di;
				has_changed = 1;
				goto scan_again;
			}
		}
	}

	/* now find any dock items that don't have a corresponding
	 * resident entry.  We may need to promote a task entry up to resident */
	for (q = 0; q < qitems.size(); q++) {
		found = 0;
		for (iter = items.begin(); iter != items.end(); iter++) {
			di = *iter;
			if (di->type == dock_item::dock_is_resident &&
					!lstrcmpi(qitems[q].exec, di->exec)) {
				found = 1;
				break;
			}
			if (di->type == dock_item::dock_is_task) {
				/* TODO: compare the link target against
				 * the window module to see if it matches
				 */
			}
		}
		if (!found) {
			di = new dock_item;
			di->type = dock_item::dock_is_resident;
			di->icon = qitems[q].info.hIcon;
			StringCbCopy(di->icon_text, sizeof(di->icon_text), qitems[q].icon_text);
			StringCbCopy(di->exec, sizeof(di->exec), qitems[q].exec);
			get_module_name(di);
			items.push_back(di);
			has_changed = 1;
		}
	}
	if (has_changed) update_view();
}

static void get_window_module(dock_item *di, HWND wnd)
{
	StringCbCopy(di->module_path, sizeof(di->module_path), f->GetWindowModulePath(wnd));
}

dock_item *find_matching_item(HWND wnd, 
	vector <struct dock_item*>::iterator &iter,
	vector <struct running_item>::iterator &r_iter)
{
	TCHAR wmf[MAX_PATH];
	DWORD pid;
	int got_wmf = 0;
	dock_item *di;

	for (iter = items.begin(); iter != items.end(); iter++) {
		di = *iter;

		for (r_iter = di->apps.begin(); r_iter != di->apps.end(); r_iter++) {
			struct running_item ri = *r_iter;
			if (ri.wnd == wnd) {
				return di;
			}
		}
		
		if (di->type == dock_item::dock_is_resident && got_wmf == 0) {
			StringCbCopy(wmf, sizeof(wmf), f->GetWindowModulePath(wnd));
			got_wmf = wmf[0] ? 1 : -1;
		}

		if (lstrlen(di->module_path) && got_wmf == 1) {
			if (!lstrcmpi(wmf, di->module_path)) {
				r_iter = di->apps.end();
				return di;
			}
		}
	}
	return NULL;
}

static void update_view(void)
{
	int i;
	
	has_changed = 0;

	QL_WIDTH = ICON_SIZE + (ICON_SIZE / 2);

	if (is_vert) {
		SetWindowPos(dock, NULL, 0, 0,
			QL_WIDTH, ICON_PAD + ((ICON_SIZE+ICON_PAD) * items.size()),
			SWP_NOZORDER|SWP_NOMOVE);
	} else {
		SetWindowPos(dock, NULL, 0, 0,
			ICON_PAD + ((ICON_SIZE+ICON_PAD) * items.size()), QL_HEIGHT,
			SWP_NOZORDER|SWP_NOMOVE);
	}

	InvalidateRect(dock, NULL, TRUE);
	UpdateWindow(dock);
}

static BOOL CALLBACK do_task_scan(HWND hwnd, LPARAM lparam)
{
	if (f->IsAppWindow(hwnd) && IsWindowVisible(hwnd)) {
		unsigned long bits = f->ChangeDesktopBits(hwnd, 0, 0);
		int desk = 1 << (f->GetActiveDesktop() - 1);
		vector <dock_item *>::iterator iter;
		vector <struct running_item>::iterator riter;
		struct running_item ritem;
		int found;
		int on_desk;
		dock_item *di;

		on_desk = (bits & desk) == desk;
		found = 0;

		di = find_matching_item(hwnd, iter, riter);

		if (di) {
			if (riter != di->apps.end()) {
				if (!on_desk && !riter->flashing) {
					/* remove this entry */
					di->apps.erase(riter);
					if (di->apps.size() == 0 && di->type == dock_item::dock_is_task) {
						/* remove the icon completely */
						items.erase(iter);
						delete di;
						has_changed = 1;
					}
					return TRUE;
				}
				riter->zorder = zorder_counter++;
				return TRUE;
			}
			/* didn't find an exact match, but found which icon to collect it
			 * with */
			if (!on_desk) return TRUE;
			memset(&ritem, 0, sizeof(ritem));
			ritem.wnd = hwnd;
			ritem.zorder = zorder_counter++;
			get_window_module(di, hwnd);
			di->apps.push_back(ritem);
			has_changed = 1;
			return TRUE;
		}

		if (!on_desk) return TRUE;

		/* create an entry */
		di = new dock_item;
		di->type = dock_item::dock_is_task;
		di->icon = f->GetWindowIcon(hwnd, ICON_GATHER_TIMEOUT);
		memset(&ritem, 0, sizeof(ritem));
		ritem.wnd = hwnd;
			ritem.zorder = zorder_counter++;
		di->apps.push_back(ritem);
		items.push_back(di);
		get_window_module(di, hwnd);
		has_changed = 1;
	}
	return TRUE;
}

static void scan_tasks(void)
{
	vector <dock_item *>::iterator iter;
	vector <struct running_item>::iterator riter;
	struct running_item ritem;
	dock_item *di;

	zorder_counter = 0;
	EnumWindows(do_task_scan, NULL);
	/* find dead apps */
find_again:
	for (iter = items.begin(); iter != items.end(); iter++) {
		di = *iter;
		for (riter = di->apps.begin(); riter != di->apps.end(); riter++) {
			ritem = *riter;
			if (!IsWindow(ritem.wnd) || !IsWindowVisible(ritem.wnd)) {
				di->apps.erase(riter);
				has_changed = 1;
				if (di->apps.size() == 0 && di->type == dock_item::dock_is_task) {
					/* remove the icon completely */
					items.erase(iter);
					delete di;
					goto find_again;
				}
				break;
			}
		}
		if (!di->icon && di->apps.size()) {
			for (riter = di->apps.begin();
					di->icon == 0 && riter != di->apps.end();
					riter++) {
				di->icon = f->GetWindowIcon(riter->wnd, ICON_GATHER_TIMEOUT);
			}
		}
		di->sort_apps();
	}

	task_scan_time = GetTickCount();
	if (has_changed) update_view();
}

static void monitor_dir(void);
static void CALLBACK dir_changed(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped)
{
	dir_change_time = GetTickCount() + 1200;
	monitor_dir();
}

static void monitor_dir(void)
{
	memset(&olap, 0, sizeof(olap));
	ReadDirectoryChangesW(hqldir,
			_file_notify_information, sizeof(_file_notify_information),
			FALSE,
			FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_ATTRIBUTES|
			FILE_NOTIFY_CHANGE_SIZE|FILE_NOTIFY_CHANGE_LAST_WRITE,
			NULL, &olap, dir_changed);
}

static void paint_dock(HDC hdc)
{
	int i;
	dock_item *di;
	RECT client;

	Graphics G(hdc);
	GetClientRect(dock, &client);

	G.SetInterpolationMode(InterpolationModeHighQualityBicubic);

	for (i = 0; i < items.size(); i++) {
		RECT r;
		HICON icon;
		int kill;
		int ticks;
		int appno;

		di = items[i];
		ticks = 0;
		for (appno = 0; appno < di->apps.size(); appno++) {
			if (di->apps[appno].flashing) {
				ticks = di->apps[appno].flash_ticks;
				break;
			}
		}

		calc_item_rect(i, &r, ticks);

		icon = di->icon;
		kill = 0;
		if (di->apps.size()) {
			icon = f->GetWindowIcon(di->apps[0].wnd, ICON_GATHER_TIMEOUT);
			if (!icon) icon = di->icon;
		}
		if (!icon) {
			icon = LoadIcon(NULL, IDI_APPLICATION);
		}

		DrawIconEx(hdc, r.left, r.top, icon,
			ICON_SIZE, ICON_SIZE, 0, NULL, DI_NORMAL);
		if (kill) DestroyIcon(icon);

		if (items[i]->hot) {
			InflateRect(&r, ICON_PAD/2, ICON_PAD/2);
			FrameRect(hdc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH)); 
			InflateRect(&r, 1, 1);
			FrameRect(hdc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH)); 
		}

		if (di->apps.size() && cue_arrow) {
			/* provide visual cue that there are running apps here */
			int cue_size = (QL_WIDTH - ICON_SIZE) / 2;

			if (is_vert) {
				r.left = 0;
				r.top = ICON_PAD + (i * (ICON_SIZE+ICON_PAD)) + (ICON_SIZE/2) - (cue_size/2);
			} else {
				r.top = 0;
				r.left = ICON_PAD + (i * (ICON_SIZE+ICON_PAD)) + (ICON_SIZE/2) - (cue_size/2);
			}
			switch (slit_align) {
				case WEZDESK_GRAVITY_RIGHT:
					G.DrawImage(cue_arrow,
						client.right - cue_size,
						r.top, 
						cue_size, cue_size);
					break;
				case WEZDESK_GRAVITY_BOTTOM:
					G.DrawImage(cue_arrow,
						r.left, client.bottom - cue_size,
						cue_size, cue_size);
					break;
				case WEZDESK_GRAVITY_TOP:
					G.DrawImage(cue_arrow,
						r.left, 0,
						cue_size, cue_size);
					break;
				case WEZDESK_GRAVITY_LEFT:
					G.DrawImage(cue_arrow,
						client.left,
						r.top, 
						cue_size, cue_size);
					break;
			}
		}
	}
}

static void handle_slit_layout_change(void)
{
	HWND slit = GetParent(dock);
	slit_align = f->GetSlitAlignment(slit);
	if (slit_align == WEZDESK_GRAVITY_BOTTOM || slit_align == WEZDESK_GRAVITY_TOP) {
		is_vert = 0;
	} else {
		is_vert = 1;
	}

	TCHAR path[MAX_PATH];
	DWORD len = GetModuleFileName(NULL, path, sizeof(path)/sizeof(path[0]));
	path[len] = 0;
	--len;
	while (path[len] != '\\') --len;
	path[len+1] = 0;
	StringCbCat(path, sizeof(path), TEXT("arrow.png"));
	TCHAR *name = f->GetPluginString(my_plugin, TEXT("Running.Image"), path);
	cue_arrow = new Image(name);
	free(name);

	/* we assume that the arrow points right by default */
	switch (slit_align) {
		case WEZDESK_GRAVITY_RIGHT:
			cue_arrow->RotateFlip(Rotate180FlipNone);
			break;
		case WEZDESK_GRAVITY_TOP:
			cue_arrow->RotateFlip(Rotate90FlipNone);
			break;
		case WEZDESK_GRAVITY_BOTTOM:
			cue_arrow->RotateFlip(Rotate270FlipNone);
			break;
	}
	f->SetGravityFromConfig(my_plugin, dock, TEXT("gravity"), 
		is_vert ? WEZDESK_GRAVITY_BOTTOM : WEZDESK_GRAVITY_LEFT,
		-1);
	has_changed = 1;
	update_view();
}

static LRESULT CALLBACK dock_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);
			paint_dock(hdc);
			EndPaint(hWnd, &ps);
			break;
		}

		case WM_SLIT_LAYOUT_CHANGED:
			handle_slit_layout_change();
			return 1;

//		case WM_ERASEBKGND:
//			return 1;

		case WM_MENUCOMMAND:
		{
			MENUITEMINFO info;
			info.cbSize = sizeof(info);
			info.fMask = MIIM_DATA | MIIM_ID;
			if (GetMenuItemInfo((HMENU)lParam, wParam, TRUE, &info)) {
				if (info.wID == 0) {
					dock_item *di = (dock_item *)info.dwItemData;
					f->Execute(GetDesktopWindow(), NULL, di->exec,
						TEXT(""), NULL, SW_SHOWNORMAL, 0);
				} else if (info.wID == -1) {
					dock_item *di = (dock_item *)info.dwItemData;
					f->Execute(GetDesktopWindow(), TEXT("properties"),
						di->exec, TEXT(""), NULL, SW_SHOWNORMAL, 0);
				} else {
					HWND app = (HWND)info.dwItemData;
					f->SwitchToWindow(app, TRUE);
				}
			}
			return 1;
		}

		case WM_CONTEXTMENU:
		{
			int i, j;
			POINT pt;
			dock_item *di;
			
			pt.x = LOWORD(lParam);
			pt.y = HIWORD(lParam);
			ScreenToClient(dock, &pt);

			for (i = 0; i < items.size(); i++) {
				RECT r;

				calc_item_rect(i, &r, 0);
				if (PtInRect(&r, pt)) {
					di = items[i];
					di->sort_apps();
					/* give the user a choice of the windows */
					HMENU m = CreatePopupMenu();
					MENUINFO info;
					info.cbSize = sizeof(info);
					info.fMask = MIM_STYLE;
					info.dwStyle = MNS_NOTIFYBYPOS;
					SetMenuInfo(m, &info);

					MENUITEMINFO mi;
					vector <TCHAR *> names;
					TCHAR caption[256];
					
					for (j = 0; j < di->apps.size(); j++) {
						memset(&mi, 0, sizeof(mi));
						mi.cbSize = sizeof(mi);
						mi.fMask = MIIM_DATA | MIIM_ID | MIIM_STRING | MIIM_FTYPE;
						mi.fType = MFT_STRING;
						mi.wID = j+1;
						mi.dwItemData = (UINT_PTR)di->apps[j].wnd;
						GetWindowText(di->apps[j].wnd, caption,
							sizeof(caption)/sizeof(caption[0]));
						mi.dwTypeData = lstrdup(caption);
						names.push_back(mi.dwTypeData);
						InsertMenuItem(m, GetMenuItemCount(m), TRUE, &mi);
					}

					if (di->type == dock_item::dock_is_resident) {
						memset(&mi, 0, sizeof(mi));
						mi.cbSize = sizeof(mi);
						mi.fMask = MIIM_DATA | MIIM_ID | MIIM_STRING | MIIM_FTYPE;
						mi.fType = MFT_STRING;
						mi.wID = 0;
						mi.dwItemData = (UINT_PTR)di;
						mi.dwTypeData = launch_text;
						InsertMenuItem(m, GetMenuItemCount(m), TRUE, &mi);

						memset(&mi, 0, sizeof(mi));
						mi.cbSize = sizeof(mi);
						mi.fMask = MIIM_DATA | MIIM_ID | MIIM_STRING | MIIM_FTYPE;
						mi.fType = MFT_STRING;
						mi.wID = -1;
						mi.dwItemData = (UINT_PTR)di;
						mi.dwTypeData = properties_text;
						InsertMenuItem(m, GetMenuItemCount(m), TRUE, &mi);
					}

					if (TrackPopupMenuEx(m, TPM_NOANIMATION,
								LOWORD(lParam), HIWORD(lParam), dock, NULL)) {
						/* pump the MENUCOMMAND msg through */
						MSG msg;
						if (PeekMessage(&msg, dock, WM_MENUCOMMAND,
									WM_MENUCOMMAND, PM_REMOVE)) {
							DispatchMessage(&msg);
						}
					}
					for (j = 0; j < names.size(); j++) {
						free(names[j]);
					}
					DestroyMenu(m);

					return 1;
				}
			}
			return DefWindowProc(hWnd, uMsg, wParam, lParam);
		}

		case WM_LBUTTONUP:
		{
			int i;
			POINT pt;
			dock_item *di;
			
			pt.x = LOWORD(lParam);
			pt.y = HIWORD(lParam);

			for (i = 0; i < items.size(); i++) {
				RECT r;

				calc_item_rect(i, &r, 0);
				if (PtInRect(&r, pt)) {
					HWND wnd_to_switch = 0;

					di = items[i];
					if (di->apps.size()) {
						int fl;
						di->sort_apps();
						for (fl = 0; fl < di->apps.size(); fl++) {
							if (di->apps[fl].flashing) {
								wnd_to_switch = di->apps[fl].wnd;
								di->apps[fl].flashing = 0;
								di->apps[fl].flash_ticks = 0;
								break;
							}
						}
						if (!wnd_to_switch) wnd_to_switch = di->apps[0].wnd;
					}

					if (wnd_to_switch) {
						f->SwitchToWindow(wnd_to_switch, TRUE);
					} else if (di->type == dock_item::dock_is_resident) {
						f->Execute(GetDesktopWindow(), NULL, items[i]->exec,
								TEXT(""), NULL, SW_SHOWNORMAL, 0);
					}
					return 1;
				}
			}
			scan_dir();
			break;
		}

		case WM_MOUSELEAVE:
		{
			int i;
			
			for (i = 0; i < items.size(); i++) {
				items[i]->hot = 0;
			}
			InvalidateRect(hWnd, NULL, TRUE);
			UpdateWindow(hWnd);
			return 1;
		}

		case WM_MOUSEMOVE:
		{
			int i;
			POINT pt;
			int invalidated = 0;
			
			pt.x = LOWORD(lParam);
			pt.y = HIWORD(lParam);

			for (i = 0; i < items.size(); i++) {
				RECT r;

				calc_item_rect(i, &r, 0);
				if (PtInRect(&r, pt)) {
					items[i]->hot = 1;
					InvalidateRect(hWnd, NULL, TRUE);
					items[i]->update_tip(i);
					invalidated = 1;
				} else if (items[i]->hot) {
					items[i]->hot = 0;
					InvalidateRect(hWnd, NULL, TRUE);
					invalidated = 1;
				}
			}
			if (invalidated) {
				UpdateWindow(hWnd);
			}
			return 1;
		}

		case WM_TIMER: {
			DWORD now;

			now = GetTickCount();
			if (dir_change_time && now >= dir_change_time) {
				scan_dir();
			}
			if (task_scan_time + TASK_SCAN_INTERVAL <= now) {
				scan_tasks();
			}
			dock_item *di;
			vector <struct dock_item*>::iterator iter;
			vector <struct running_item>::iterator r_iter;
			struct running_item ri;
			int flashing = 0;

			for (iter = items.begin(); iter != items.end(); iter++) {
				di = *iter;
				for (r_iter = di->apps.begin(); r_iter != di->apps.end(); r_iter++) {
					if (r_iter->flashing) {
						if (++r_iter->flash_ticks > ICON_SIZE) {
							r_iter->flash_ticks = 0;
						}
						flashing = 1;
					}
				}
			}

			if (has_changed) {
				update_view();
			} else if (flashing) {
				InvalidateRect(dock, NULL, TRUE);
				UpdateWindow(dock);
			}

	
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

	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = dock_proc;
	wc.hInstance = funcs->GetPluginInstance(plugin);
	wc.lpszClassName = TEXT("WezDesk Dock Window");
	wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	//wc.hbrBackground = (HBRUSH)(1 + COLOR_BTNFACE);
	wc.hbrBackground = (HBRUSH)(1 + COLOR_APPWORKSPACE);

	if (!RegisterClass(&wc)) {
		return NULL;
	}

	f->LoadString(IDS_LAUNCH_APP, launch_text,
		sizeof(launch_text)/sizeof(launch_text[0]));
	f->LoadString(IDS_SHOW_ICON_PROPERTIES, properties_text,
		sizeof(properties_text)/sizeof(properties_text[0]));

	SHGetFolderPathAndSubDir(NULL, CSIDL_FLAG_CREATE|CSIDL_APPDATA, NULL,
		SHGFP_TYPE_CURRENT, TEXT("Microsoft\\Internet Explorer\\Quick Launch"),
		dockdir);
	StringCbPrintf(dockdirandpat, sizeof(dockdirandpat), TEXT("%s\\*.*"),
		dockdir);

	dock = CreateWindowEx(
			WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT,
			wc.lpszClassName,
			NULL,
			WS_CHILD|WS_VISIBLE,
			0, 0,
			QL_WIDTH,
			QL_HEIGHT,
			slit,
			NULL,
			wc.hInstance,
			NULL);

	handle_slit_layout_change();
	hqldir = CreateFile(dockdir, FILE_LIST_DIRECTORY, 
		FILE_SHARE_READ|FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OVERLAPPED,
		NULL);

	tooltips = CreateWindowEx(
			WS_EX_TOPMOST|WS_EX_TRANSPARENT,
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

	timer = SetTimer(dock, 1, 50, NULL);
	scan_dir();
	monitor_dir();

	return (void*)1;
}

static int unload(Plugin plugin, WezDeskFuncs *funcs, int force)
{
	int i;

	DestroyWindow(dock);

	for (i = 0; i < items.size(); i++) {
		delete items[i];
	}
	items.clear();
	if (cue_arrow) {
		delete cue_arrow;
		cue_arrow = NULL;
	}

	return 1;
}

static void on_shell_message(Plugin plugin, WezDeskFuncs *funcs, WPARAM msg, LPARAM lparam)
{
	int i;
	dock_item *di;
	vector <struct dock_item*>::iterator iter;
	vector <struct running_item>::iterator r_iter;
	struct running_item ri;

	switch (msg) {
		case HSHELL_FLASH:
			/* yes, 'doze still sends flash messages if the window is active */
			if ((HWND)lparam == GetForegroundWindow())
				return;
			/* do we have this app already listed ? */
			di = find_matching_item((HWND)lparam, iter, r_iter);
			if (di && r_iter != di->apps.end()) {
				r_iter->flashing = 1;
				has_changed = 1;
				update_view();
				return;	
			}

			memset(&ri, 0, sizeof(ri));
			ri.wnd = (HWND)lparam;
			ri.flashing = 1;
			
			if (!di) {
				di = new dock_item;
				di->type = dock_item::dock_is_task;
				di->icon = f->GetWindowIcon((HWND)lparam, ICON_GATHER_TIMEOUT);
				get_window_module(di, (HWND)lparam);
				items.push_back(di);	
			}

			di->apps.push_back(ri);

			has_changed = 1;
			update_view();
			break;

		case HSHELL_REDRAW:
			if (GetForegroundWindow() == (HWND)lparam) {
				/* if the user is switching to a flashing app,
				 * remove it from the list */
				di = find_matching_item((HWND)lparam, iter, r_iter);
				if (di && r_iter != di->apps.end()) {
					r_iter->flashing = 0;
					r_iter->flash_ticks = 0;
					has_changed = 1;
					di->sort_apps();
				}
			} else {
				di = find_matching_item((HWND)lparam, iter, r_iter);
				if (di && r_iter != di->apps.end()) {
					di->sort_apps();
					has_changed = 1;
				}
			}
			update_view();
			break;

		case HSHELL_WINDOWCREATED:
			scan_tasks();
			/* fall through */
		case HSHELL_RUDEAPPACTIVATED:
		case HSHELL_WINDOWACTIVATED:
			di = find_matching_item((HWND)lparam, iter, r_iter);
			if (di && r_iter != di->apps.end()) {
				di->sort_apps();
				has_changed = 1;
			}
			update_view();
			break;

	}
}

static int on_notify(Plugin plugin, WezDeskFuncs *funcs, UINT code,
	UINT secondary, WPARAM wparam, LPARAM lparam)
{
	if (code == WZN_DESKTOP_SWITCHED) {
		scan_tasks();
		if (has_changed) update_view();
	}
	return 0;
}

static WezDeskPlugin the_plugin = {
	WEZDESK_API_NUMBER,
	TEXT("Dock"),
	TEXT("OSXish Dock"),
	TEXT("BSD"),
	TEXT("Wez Furlong <wez@php.net>"),
	TEXT("http://evildesk.netevil.org"),
	TEXT("0.1"),
	initialize,
	NULL, /* unload ok */
	unload, /* unload */
	NULL, /* veto */
	on_shell_message, /* shell msg */
	NULL, /* on_tray_change */
	on_notify /* notify */
};

GET_PLUGIN(the_plugin);

