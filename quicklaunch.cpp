/* Copyright (c) 2004-2009 Wez Furlong.
 * This source is provided under terms of the GPLv2.
 * See the file named LICENSE for full details */
#include "wezdeskapi.h"
#include <shlobj.h>
#include <shellapi.h>
#include <vector>
#define STRSAFE_LIB
#include <strsafe.h>

using namespace std;

static WezDeskFuncs *f;
static Plugin my_plugin;
static HWND tooltips = NULL;
static int is_vert = 1;
HWND quicklaunch;
HANDLE hqldir; // ReadDirectoryChangesW
TCHAR quicklaunchdir[MAX_PATH];
TCHAR quicklaunchdirandpat[MAX_PATH];
int icon_pref = SHGFI_SHELLICONSIZE;
//int icon_pref = SHGFI_LARGEICON;
//int icon_pref = SHGFI_SMALLICON;
int ICON_PAD = 4;
int ICON_SIZE = 32;
int QL_WIDTH = 48;
int QL_HEIGHT = 40;
#if 0
static BYTE _file_notify_information[4096];
static OVERLAPPED olap;
#endif

struct quicklaunch {
	TCHAR exec[MAX_PATH];
	SHFILEINFO info;
	int hot;
	TOOLINFO ti;
};
vector <struct quicklaunch> items;

static void calc_item_rect(int i, RECT *r)
{
	if (is_vert) {
		r->left = (QL_WIDTH - ICON_SIZE) / 2;
		r->top = ICON_PAD + (i * (ICON_SIZE+ICON_PAD));
	} else {
		r->top = (QL_HEIGHT - ICON_SIZE) / 2;
		r->left = ICON_PAD + (i * (ICON_SIZE+ICON_PAD));
	}
	r->bottom = r->top + ICON_SIZE;
	r->right = r->left + ICON_SIZE;
}

static void scan_dir(void)
{
	HANDLE h;
	WIN32_FIND_DATA fd;
	int i;

	for (i = 0; i < items.size(); i++) {
		DestroyIcon(items[i].info.hIcon);
		SendMessage(tooltips, TTM_DELTOOL, 0, (LPARAM)&items[i].ti);
		free(items[i].ti.lpszText);
	}
	items.clear();

	h = FindFirstFile(quicklaunchdirandpat, &fd);
	i = 0;
	if (h != INVALID_HANDLE_VALUE) {
		do {
			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) == FILE_ATTRIBUTE_HIDDEN) {
				continue;
			}
			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY) {
				continue;
			}

			struct quicklaunch item;
			TCHAR *bs;
			StringCbPrintf(item.exec, sizeof(item.exec), TEXT("%s\\%s"),
				quicklaunchdir, fd.cFileName);
			item.hot = 0;

			if (!SHGetFileInfo(item.exec, 0, &item.info, sizeof(item.info),
					SHGFI_ICON|icon_pref)) {
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

//			f->ShowMessageBox(item.exec, item.info.szDisplayName, MB_OK);

//			item.info.hIcon = ExtractIcon(GetModuleHandle(NULL),
//				item.info.szDisplayName,
//				item.info.iIcon);

//			SHDefExtractIcon(item.info.szDisplayName, item.info.iIcon,
//				0, &item.info.hIcon, NULL, ICON_SIZE);

//			item.info.hIcon = (HICON)LoadImage(NULL, item.info.szDisplayName,
//				IMAGE_ICON, ICON_SIZE, ICON_SIZE,
//				LR_LOADFROMFILE);

//			SHGetFileInfo(item.exec, 0, &item.info, sizeof(item.info),
//				SHGFI_DISPLAYNAME);

			item.ti.cbSize = sizeof(item.ti);
			item.ti.uFlags = TTF_SUBCLASS;
			item.ti.hwnd = quicklaunch;
			item.ti.uId = i;
			item.ti.hinst = f->GetPluginInstance(my_plugin);
			calc_item_rect(i, &item.ti.rect);
			item.ti.lpszText = lstrdup(item.exec + lstrlen(item.exec) - lstrlen(fd.cFileName));
			bs = item.ti.lpszText + lstrlen(fd.cFileName) - 1;
			while (*bs != '.')
				--bs;
			if (!lstrcmp(bs, TEXT(".lnk")) || !lstrcmp(bs, TEXT(".url"))) {
				*bs = '\0';
			}
			items.push_back(item);
			SendMessage(tooltips, TTM_ADDTOOL, 0, (LPARAM)&item.ti);

			i++;

		} while (FindNextFile(h, &fd));
		FindClose(h);
	}
					
	QL_WIDTH = ICON_SIZE + (ICON_SIZE / 2);

	if (is_vert) {
		SetWindowPos(quicklaunch, NULL, 0, 0,
			QL_WIDTH, ICON_PAD + ((ICON_SIZE+ICON_PAD) * items.size()),
			SWP_NOZORDER|SWP_NOMOVE);
	} else {
		SetWindowPos(quicklaunch, NULL, 0, 0,
			ICON_PAD + ((ICON_SIZE+ICON_PAD) * items.size()), QL_HEIGHT,
			SWP_NOZORDER|SWP_NOMOVE);

	}

	InvalidateRect(quicklaunch, NULL, TRUE);
	UpdateWindow(quicklaunch);
}

static void CALLBACK dir_changed(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped)
{
#if WE_ARE_SMART
	FILE_NOTIFY_INFORMATION *info = (FILE_NOTIFY_INFORMATION*)_file_notify_information;

	do {

		if (!info->NextEntryOffset)
			break;
		info = (FILE_NOTIFY_INFORMATION*)(((BYTE *)info) + info->NextEntryOffset);
	} while (1);
#else
	scan_dir();
#endif
}

static void paint_quicklaunch(HDC hdc)
{
	int i;

	for (i = 0; i < items.size(); i++) {
		RECT r;

		calc_item_rect(i, &r);
		DrawIconEx(hdc, r.left, r.top, items[i].info.hIcon, ICON_SIZE, ICON_SIZE, 0, NULL, DI_NORMAL);

		if (items[i].hot) {
			InflateRect(&r, ICON_PAD/2, ICON_PAD/2);
			FrameRect(hdc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH)); 
			InflateRect(&r, 1, 1);
			FrameRect(hdc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH)); 
		}
	}
}

static LRESULT CALLBACK quicklaunch_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);
			paint_quicklaunch(hdc);
			EndPaint(hWnd, &ps);
			break;
		}

//		case WM_ERASEBKGND:
//			return 1;

		case WM_LBUTTONUP:
		{
			int i;
			POINT pt;
			
			pt.x = LOWORD(lParam);
			pt.y = HIWORD(lParam);

			for (i = 0; i < items.size(); i++) {
				RECT r;

				calc_item_rect(i, &r);
				if (PtInRect(&r, pt)) {
					f->Execute(GetDesktopWindow(), NULL, items[i].exec, TEXT(""), NULL, SW_SHOWNORMAL, 0);
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
				items[i].hot = 0;
			}
			InvalidateRect(hWnd, NULL, TRUE);
			UpdateWindow(hWnd);
			return 1;
		}

		case WM_MOUSEMOVE:
		{
			int i;
			POINT pt;
			
			pt.x = LOWORD(lParam);
			pt.y = HIWORD(lParam);

			for (i = 0; i < items.size(); i++) {
				RECT r;

				calc_item_rect(i, &r);
				if (PtInRect(&r, pt)) {
					items[i].hot = 1;
					InvalidateRect(hWnd, NULL, TRUE);
				} else if (items[i].hot) {
					items[i].hot = 0;
					InvalidateRect(hWnd, NULL, TRUE);
				}
			}

			UpdateWindow(hWnd);
			return 1;
		}

		case WM_KEYUP:
			scan_dir();	
			return 1;

		default:
			return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}

static void *initialize(Plugin plugin, WezDeskFuncs *funcs, HWND slit) {
	WNDCLASS wc;
	int slit_align;

	f = funcs;
	my_plugin = plugin;

	slit_align = f->GetSlitAlignment(slit);
	if (slit_align == WEZDESK_GRAVITY_BOTTOM || slit_align == WEZDESK_GRAVITY_TOP) {
		is_vert = 0;
	} else {
		is_vert = 1;
	}
	
	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = quicklaunch_proc;
	wc.hInstance = funcs->GetPluginInstance(plugin);
	wc.lpszClassName = TEXT("WezDesk Quicklaunch Window");
	wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	//wc.hbrBackground = (HBRUSH)(1 + COLOR_BTNFACE);
	wc.hbrBackground = (HBRUSH)(1 + COLOR_APPWORKSPACE);

	if (!RegisterClass(&wc)) {
		return NULL;
	}

	SHGetFolderPathAndSubDir(NULL, CSIDL_FLAG_CREATE|CSIDL_APPDATA, NULL,
		SHGFP_TYPE_CURRENT, TEXT("Microsoft\\Internet Explorer\\Quick Launch"),
		quicklaunchdir);
	StringCbPrintf(quicklaunchdirandpat, sizeof(quicklaunchdirandpat),
		TEXT("%s\\*.*"), quicklaunchdir);

	quicklaunch = CreateWindowEx(
			WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT,
			wc.lpszClassName,
			NULL,
			WS_CHILD|WS_VISIBLE,
			0, 0,
			is_vert ? QL_WIDTH : QL_HEIGHT,
			is_vert ? QL_HEIGHT : QL_WIDTH,
			slit,
			NULL,
			wc.hInstance,
			NULL);

	funcs->SetGravityFromConfig(plugin, quicklaunch, TEXT("gravity"), 
		is_vert ? WEZDESK_GRAVITY_BOTTOM : WEZDESK_GRAVITY_LEFT,
		-1);

	hqldir = CreateFile(quicklaunchdir, FILE_LIST_DIRECTORY, 
		FILE_SHARE_READ|FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

#if 0
	memset(&olap, 0, sizeof(olap));
	ReadDirectoryChangesW(hqldir, _file_notify_information, sizeof(_file_notify_information),
		FALSE, FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_ATTRIBUTES|FILE_NOTIFY_CHANGE_SIZE|
		FILE_NOTIFY_CHANGE_LAST_WRITE, NULL, &olap, dir_changed);
#endif

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
	

	scan_dir();

	return (void*)1;
}

static int unload(Plugin plugin, WezDeskFuncs *funcs, int force)
{
	int i;

	DestroyWindow(quicklaunch);

	for (i = 0; i < items.size(); i++) {
		DestroyIcon(items[i].info.hIcon);
	}
	items.clear();

	return 1;
}

static WezDeskPlugin the_plugin = {
	WEZDESK_API_NUMBER,
	TEXT("Quicklaunch"),
	TEXT("Implements the quicklaunch"),
	TEXT("BSD"),
	TEXT("Wez Furlong <wez@php.net>"),
	TEXT("http://netevil.org/wiki.php?WezDesk"),
	TEXT("0.1"),
	initialize,
	NULL, /* unload ok */
	unload, /* unload */
	NULL, /* veto */
	NULL, /* shell msg */
	NULL, /* on_tray_change */
	NULL /* notify */
};

GET_PLUGIN(the_plugin);

