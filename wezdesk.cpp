/* Copyright (c) 2004-2009 Wez Furlong.
 * This source is provided under terms of the GPLv2.
 * See the file named LICENSE for full details */
#include "wezdesk.h"
#include <commctrl.h>
#include <docobj.h>
#include <ShlGuid.h>
#include <shlobj.h>
#include <softpub.h>
#include <psapi.h>
#include <gdiplus.h>
using namespace Gdiplus;

HINSTANCE i18n_dll = NULL;
HINSTANCE i18n_def = NULL;

//int MAX_IMAGE_SIZE = 1048576;
int MAX_IMAGE_SIZE = 1572864;

vector <config_value> core_config;

static HWND root_window;
static int current_desktop = 0;
static unsigned int WM_SHELLHOOKMESSAGE, WM_WORKSPACE;
static int run_startup = 0;
static int desktop_switching = 0;
TCHAR base_dir[1024] = TEXT("");
static PerDesktop desktops[32];
static HHOOK the_shell_hook = NULL;
static HFONT font_def = NULL, font_def_bold = NULL, font_def_title = NULL, font_def_smtitle = NULL, font_def_tip = NULL, font_def_msg = NULL;
static HANDLE single_instance_mtx = NULL;
BOOL need_to_respawn_shell = FALSE;
HWND active_workspace_context_window = NULL;
static CRITICAL_SECTION callback_mutex;

static void fixup_window_munchness(HWND hWnd, BOOL delay);
void schedule_plugin_callbacks(void);

#define WIN_HASH_VALUE 23
static struct per_window *per_window_hash[WIN_HASH_VALUE+1];

vector <Plugin> plugins;
vector <IOleCommandTarget*> service_objects;
vector <safer_match> safer_matches;
vector <window_match> window_matches;

struct startup_item {
	TCHAR *what;
	int pri;
};

vector <struct startup_item> startup_items;
UINT startup_items_timer;
UINT plugin_timer = 0;
static void run(LPCTSTR what);
static void (*enable_hook_dll)(HINSTANCE, HWND, BOOL) = NULL;
static HINSTANCE hook_dll_instance = NULL;

static HANDLE log_file = NULL;

void debug_printf(LPCTSTR fmt, ...)
{
	va_list ap;
	TCHAR buf[4096];

	if (log_file == NULL) {
		DWORD n = GetTempPath(sizeof(buf)/sizeof(buf[0]), buf);
		StringCbCopy(buf, sizeof(buf), TEXT("\\evildesk.log"));

		log_file = CreateFile(buf, GENERIC_WRITE, FILE_SHARE_READ,
			NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	}
	
	va_start(ap, fmt);
	wvnsprintf(buf, sizeof(buf)/sizeof(buf[0]), fmt, ap);
	OutputDebugString(buf);

	if (log_file && log_file != INVALID_HANDLE_VALUE) {
		DWORD wrote;
		WriteFile(log_file, buf, lstrlen(buf)*sizeof(buf[0]), &wrote, NULL);
	}

	va_end(ap);
}

void delete_config_value(vector <config_value> &cv)
{
	int i;
	for (i = 0; i < cv.size(); i++) {
		struct config_value cfg = cv[i];
		free(cfg.name);
		free(cfg.value);
	}
	cv.clear();
}

static void delete_PerWindow(HWND hwnd)
{
	int i, h;
	PerWindow *w;

	h = ((DWORD)hwnd) % WIN_HASH_VALUE;
	for (w = per_window_hash[h]; w; w = w->next) {
		if (w->wnd == hwnd) {
			for (i = 0; i < 32 ; i++) {
				vector <HWND>::iterator iter;
				for (iter = desktops[i].zorder.begin(); iter != desktops[i].zorder.end(); iter++) {
					HWND tmpwnd;
					tmpwnd = *iter;
					if (tmpwnd == hwnd) {
						desktops[i].zorder.erase(iter);
						break;
					}
				}
			}
			if (w->prev)
				w->prev->next = w->next;
			else
				per_window_hash[h] = w->next;

			if (w->next)
				w->next->prev = w->prev;

			if (w->capture) delete w->capture;
			free(w);

			return;
		}
	}
}

PerWindow *get_PerWindow(HWND hwnd, int this_desk)
{
	int h;
	PerWindow *w;

	if (hwnd == 0) return NULL;

	h = ((DWORD)hwnd) % WIN_HASH_VALUE;
again:
	for (w = per_window_hash[h]; w; w = w->next) {
		if (w->wnd == hwnd) {
			return w;
		}
		if (!IsWindow(w->wnd)) {
			delete_PerWindow(w->wnd);
			goto again;
		}
	}
	w = (PerWindow*)calloc(1, sizeof(*w));
	/* lives on the active desktop only at this point */
	w->desktop_bits = 1 << this_desk;
	if (!IsIconic(hwnd))
		w->not_minimized = 1 << this_desk;
#if 0
	TCHAR caption[128];
	GetWindowText(hwnd, caption, (sizeof(caption) / sizeof(caption[0])));
	core_funcs.Trace(TEXT("GPWNIH:%08x:%08x:%08x:%s\r\n"),
		hwnd, w->desktop_bits, w->not_minimized,
		caption);
#endif
	
	w->wnd = hwnd;
	w->next = per_window_hash[h];
	if (w->next)
		w->next->prev = w;
	per_window_hash[h] = w;
	
	return w;
}

static void get_window_module_path(HWND hwnd, TCHAR *buf, int nbuf)
{
	DWORD pid;
	if (GetWindowThreadProcessId(hwnd, &pid)) {
		HANDLE p = OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,
				FALSE, pid);
		if (p) {
			DWORD needed = 0;
			HMODULE mod = 0;
			EnumProcessModules(p, &mod, sizeof(mod), &needed);
			if (mod && !GetModuleFileNameEx(p, mod, buf, nbuf)) {
				*buf = 0;
			}
			CloseHandle(p);
		}
	} else {
		*buf = 0;
	}
}

static TCHAR *core_get_window_module_path(HWND hwnd)
{
	PerWindow *w = get_PerWindow(hwnd, 0);
	static TCHAR ugh[MAX_PATH];

	if (w) {
		if (!w->module_path[0]) {
			get_window_module_path(hwnd, w->module_path,
				sizeof(w->module_path)/sizeof(w->module_path[0]));
		}
		return w->module_path;
	}
	get_window_module_path(hwnd, ugh,
		sizeof(ugh)/sizeof(ugh[0]));

	return ugh;
}

static HWND core_get_root_window(void)
{
	return root_window;
}

static int core_message_box(LPCTSTR caption, LPCTSTR body, unsigned long flags)
{
	debug_printf(TEXT("MessageBox[%s]: %s\r\n"), caption, body);
	return MessageBox(root_window, body, caption, flags | MB_TOPMOST | MB_SETFOREGROUND);
}

static int core_get_active_desktop(void)
{
	return current_desktop + 1;
}

static int IsToolWindow(HWND w)
{
	return ((GetWindowLong(w, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) == WS_EX_TOOLWINDOW);
}

int is_app_window(HWND w)
{
	if (IsWindowVisible(w)) {
		if (GetParent(w) == 0) {
			if ((GetWindowLong(w, GWL_EXSTYLE) & WS_EX_APPWINDOW) == WS_EX_APPWINDOW) {
				return 1;
			}
			HWND hWndOwner = (HWND)GetWindowLongPtr(w, GWLP_HWNDPARENT);
			if ((hWndOwner == 0) || IsToolWindow(hWndOwner)) {
				if (!IsToolWindow(w)) {
					return 1;
				}
			}
		}
	}
	return 0;
}

void capture_window(PerWindow *win)
{
	RECT rect;

	if (MAX_IMAGE_SIZE == 0) return;

	if (!win || IsIconic(win->wnd)) return;

	if (win->capture) {
		if (GetTickCount() <= win->last_capture_time + CAPTURE_INTERVAL) return;
		delete win->capture;
		win->capture = NULL;
	}

	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

	RedrawWindow(win->wnd, NULL, NULL, RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
	InvalidateRect(win->wnd, NULL, TRUE);
//	UpdateWindow(win->wnd);
	GetWindowRect(win->wnd, &rect);

	/* how much memory will it take to hold the bitmap for this window ? */
	int w, h;
	double aspect;
	unsigned long memsize;

	w = RECTWIDTH(rect);
	h = RECTHEIGHT(rect);
	memsize = w * h * 32;

	if (memsize > MAX_IMAGE_SIZE) {
		/* we need to scale it down */
		win->capture_scale = 0.9;
		do {
			w = RECTWIDTH(rect) * win->capture_scale;
			h = RECTHEIGHT(rect) * win->capture_scale;
			memsize = w * h * 32;
			if (memsize <= MAX_IMAGE_SIZE)
				break;
			win->capture_scale -= 0.1;
		} while(win->capture_scale >= 0.01);
			
		debug_printf(TEXT("scaling back capture to %dx%d (%dx%d size=%d scale=%f)\r\n"),
			w, h, RECTWIDTH(rect), RECTHEIGHT(rect), memsize, win->capture_scale);
	} else {
		win->capture_scale = 1.0;
	}

	debug_printf(TEXT("capturing at %dx%d (scale=%f)\r\n"),
			w, h, win->capture_scale);


	win->capture_w = w;
	win->capture_h = h;

	HDC memdc, windc;
	HBITMAP bm;
	HGDIOBJ old;
	DWORD result;
	
	memdc = CreateCompatibleDC(NULL);
	windc = GetDC(win->wnd);
	bm = CreateCompatibleBitmap(windc, RECTWIDTH(rect), RECTHEIGHT(rect));
	ReleaseDC(win->wnd, windc);
	
	old = SelectObject(memdc, bm);
	BOOL grabbed = PrintWindow(win->wnd, memdc, 0);
	SelectObject(memdc, old);
	DeleteObject(memdc);
	InvalidateRect(win->wnd, NULL, FALSE);

	if (grabbed) {
		Bitmap *src = Bitmap::FromHBITMAP(bm, NULL);
		win->capture = new Bitmap(w, h);
		Graphics *graph = Graphics::FromImage(win->capture);

		graph->SetInterpolationMode(InterpolationModeHighQualityBicubic);
		graph->DrawImage(src, 0, 0, w, h);

		win->last_capture_time = GetTickCount();

		delete graph;
		delete src;
	}
	DeleteObject(bm);
	
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
}

static BOOL CALLBACK minimize_if_not_on_desk(HWND hwnd, LPARAM target_desk)
{
	if (is_app_window(hwnd)) {
		PerWindow *win = get_PerWindow(hwnd, current_desktop);
#if 0
	{
	TCHAR caption[128];
	GetWindowText(hwnd, caption, (sizeof(caption) / sizeof(caption[0])));
	core_funcs.Trace(TEXT("SWD2:%08x:%08x:%08x:%s\r\n"),
		hwnd, win->desktop_bits, win->not_minimized,
		caption);
	}
#endif

		/* if not present on this desk, minimize.  If it is present, but should be minimized, minimize */
		if ((win->desktop_bits & (1<<target_desk)) == 0 || (win->not_minimized & (1<<target_desk)) == 0) {
			if (!IsIconic(hwnd)) {
				if (!win->capture) {
					capture_window(win);
				}
				ShowWindowAsync(hwnd, SW_MINIMIZE);
			}
		}
	}
	return TRUE;
}

static void core_switch_desktop(int desktop)
{
	HWND w, wprev, wlastactive = NULL;
	unsigned long v, cur_mask, tar_mask;
	PerWindow *win;
	int old_desk = current_desktop;

	if (desktop > 32 || desktop < 1)
		return;

	if (desktop - 1 == current_desktop)
		return;

	if (desktop_switching)
		return;

	if (core_send_notify(WZN_DESKTOP_SWITCHING, 0, desktop, current_desktop) < 0) {
		return;
	}

	++desktop_switching;
	try {
		--desktop;

		cur_mask = 1 << current_desktop;
		tar_mask = 1 << desktop;


		/* zero out the zorder */
		desktops[current_desktop].zorder.clear();
					
		/* pass 1: save the state for windows on the current desktop */
		w = GetTopWindow(NULL);
		while (w) {
			if (is_app_window(w)) {
				win = get_PerWindow(w, current_desktop);
#if 0
	{
	TCHAR caption[128];
	GetWindowText(w, caption, (sizeof(caption) / sizeof(caption[0])));
	core_funcs.Trace(TEXT("SWD1:%08x:%08x:%08x:%s\r\n"),
		w, win->desktop_bits, win->not_minimized,
		caption);
	}
#endif

				if ((win->desktop_bits & cur_mask) == cur_mask) {
					/* on this desktop; save/update the current state */

					if (IsIconic(w))
						win->not_minimized &= ~cur_mask;
					else
						win->not_minimized |= cur_mask;

					/* also add it to the z-order for this desktop */
					desktops[current_desktop].zorder.push_back(w);
				}
			}
			w = GetNextWindow(w, GW_HWNDNEXT);
		}

		/* pass 2: minimize any windows that are not also present on the target desktop */
		EnumWindows(minimize_if_not_on_desk, (LPARAM)desktop);

		w = NULL;

		/* pass 3: restore any windows that were on the target desktop.  Go backwards, so that
		 * we get the correct z-order */
		vector <HWND>::reverse_iterator riter;
		for (wprev = HWND_TOP, riter = desktops[desktop].zorder.rbegin(); riter != desktops[desktop].zorder.rend(); riter++)
		{
			w = *riter;
			if (IsWindow(w) && is_app_window(w)) {
				win = get_PerWindow(w, desktop);
#if 0
	{
	TCHAR caption[128];
	GetWindowText(w, caption, (sizeof(caption) / sizeof(caption[0])));
	core_funcs.Trace(TEXT("SWD3:%08x:%08x:%08x:%s\r\n"),
		w, win->desktop_bits, win->not_minimized,
		caption);
	}
#endif

				if ((win->desktop_bits & tar_mask) == tar_mask) {
					if ((win->not_minimized & tar_mask) == 0) {
						if (!IsIconic(w)) ShowWindowAsync(w, SW_MINIMIZE);
					} else {
						if (IsIconic(w)) {
							ShowWindow(w, SW_RESTORE);
						}

						if (!is_slit(w))
							wlastactive = w;

					}
				}
			}
		}

		if (wlastactive) {
			/* activate the new top-most window */
			ShowWindowAsync(wlastactive, SW_SHOW);
		}

		desktops[desktop].zorder.clear();
	} catch (...) {
	}

	/* de-bounce the desktop switch; there's a window of opportunity for shell
	 * hook messages to arrive for windows that we've just minimized, and we
	 * risk pulling them back onto the target desktop */
	MSG msg;
	if (GetMessage(&msg, 0, 0, 0)) {
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

	current_desktop = desktop;
	--desktop_switching;
		
	/* send notifcation of the switch */
	core_send_notify(WZN_DESKTOP_SWITCHED, 0, desktop+1, old_desk+1);
}

static HANDLE wezdesk_get_safer_token(DWORD level)
{
	SAFER_LEVEL_HANDLE authz = NULL;
	HANDLE imp = NULL;

	if (!SaferCreateLevel(SAFER_SCOPEID_USER, level, 0, &authz, NULL)) {
		/* error out */
		core_message_box(TEXT("SaferCreateLevel"), TEXT("Failed to create a safer authz handle"), MB_ICONERROR|MB_OK);
		return NULL;
	}

	if (!SaferComputeTokenFromLevel(authz, NULL, &imp, 0, NULL)) {
		/* error out */
		SaferCloseLevel(authz);
		core_message_box(TEXT("SaferComputeTokenFromLevel"), TEXT("Failed to compute safer level"), MB_ICONERROR|MB_OK);
		return NULL;
	}
	SaferCloseLevel(authz);

	return imp;
}

static HINSTANCE core_execute(HWND owner, LPCTSTR operation, LPCTSTR command, LPCTSTR args, LPCTSTR cwd, int nshowcmd, int dont_show_errors)
{
	DWORD type;
	SHELLEXECUTEINFO si;
	HANDLE imp = NULL;
	int i;

	if (!lstrlen(command)) {
		core_message_box(TEXT(""), TEXT("zero length command??"), MB_ICONERROR|MB_OK);
		return (HINSTANCE)32;
	}

	type = GetFileAttributes(command);
	if ((type & FILE_ATTRIBUTE_DIRECTORY) && type != 0xFFFFFFFF) {
		return ShellExecute(owner, operation, command, args, NULL, nshowcmd ? nshowcmd : SW_SHOWNORMAL);
	}

	/* now, we want to see if the command or args match one of the safer_matches patterns */
	for (i = 0; i < safer_matches.size(); i++) {
		struct safer_match m = safer_matches[i];

		if (m.re->match(command) > 0 || m.re->match(args) > 0) {
			imp = wezdesk_get_safer_token(m.level);
			if (!imp) {
				return (HINSTANCE)32;
			}
			/* and we're now ready to run with the imp token */
			break;
		}
	}

	memset(&si, 0, sizeof(si));
	si.cbSize = sizeof(SHELLEXECUTEINFO);
	si.hwnd = owner;
	si.lpVerb = operation;
	si.lpFile = command;
	si.lpParameters = args;
	si.lpDirectory = cwd;
	si.nShow = nshowcmd ? nshowcmd : SW_SHOWNORMAL;
	si.fMask = SEE_MASK_DOENVSUBST | SEE_MASK_FLAG_DDEWAIT | SEE_MASK_FLAG_LOG_USAGE;// | SEE_MASK_FLAG_NO_UI;

	if (imp) {
		debug_printf(TEXT("Running this command with a SAFER token\r\n"));
		if (!ImpersonateLoggedOnUser(imp)) {
			core_message_box(TEXT("safer execution failed"), TEXT("Unable to use impersonation token"), MB_ICONERROR|MB_OK);
			return (HINSTANCE)32;
		}
	}
	
	SetLastError(0);
	ShellExecuteEx(&si);

	if (imp) {
		RevertToSelf();
		CloseHandle(imp);
	}
	
	if ((int)si.hInstApp <= 32 && !dont_show_errors) {
		TCHAR errormsg[2048];

		StringCbPrintf(errormsg, sizeof(errormsg),
			TEXT("execute: returned %d [lasterr=%d] for command @%s@\n"),
			si.hInstApp, GetLastError(), command);

		StringCbCat(errormsg, sizeof(errormsg), TEXT("   \n\nArguments:\n"));
		if (args != NULL)
			StringCbCat(errormsg, sizeof(errormsg), args);

		StringCbCat(errormsg, sizeof(errormsg), TEXT("   \n\nWorking directory:\n"));
		if (cwd != NULL)
			StringCbCat(errormsg, sizeof(errormsg), cwd);

		StringCbCat(errormsg, sizeof(errormsg), TEXT("   "));
		core_message_box(TEXT(""), errormsg, MB_ICONERROR|MB_OK);
	}
	return si.hInstApp;
}

static unsigned long core_change_desktop_bits(HWND hwnd, unsigned long mask, unsigned long newval)
{
	PerWindow *w = get_PerWindow(hwnd, current_desktop);
	unsigned long ret;

	if (!w)
		return 0;

	ret = w->desktop_bits;
	if (mask != 0) {
		ret &= ~mask;
		ret |= newval & mask;
		w->desktop_bits = ret;
	}

	return ret;
}

LONG create_reg_key(HKEY key, LPCTSTR keyname, HKEY *out_key)
{
	DWORD disp;
	LONG res;
	
	debug_printf(TEXT("create_reg_key: %s\r\n"), keyname);
	
	res = RegCreateKeyEx(key, keyname, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, out_key, &disp);

	if (res == ERROR_SUCCESS)
		return res;

	TCHAR parentname[1024];
	TCHAR *slash;
	StringCbCopy(parentname, sizeof(parentname), keyname);
	slash = parentname + lstrlen(parentname) - 1;
	while (*slash != '\\')
		--slash;
	*slash = 0;

	HKEY k;
	res = create_reg_key(key, parentname, &k);
	if (ERROR_SUCCESS == res) {
		RegCloseKey(k);
		return RegCreateKeyEx(key, keyname, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, out_key, &disp);
	}
	return res;	
}

static void do_respawn_shell(void) {
	TCHAR myexe[1024];
	DWORD len = GetModuleFileName(NULL, myexe, sizeof(myexe)/sizeof(myexe[0]));
	myexe[len] = 0;
	core_execute(NULL, TEXT("open"), myexe, TEXT(""), NULL, SW_SHOWNORMAL, 1);
}

static Plugin core_load_plugin(LPCTSTR name, HWND slit) {
	debug_printf(TEXT("loading plugin %s\r\n"), name);
	TCHAR dllname[1024];

	StringCbPrintf(dllname, sizeof(dllname), TEXT("%s\\%s.dll"),
		base_dir, name);

	Plugin p = new _WezDeskPluginInstance;
	p->module = LoadLibrary(dllname);
	p->name = lstrdup(name);

	if (p->module) {
		WezDeskPlugin *(*get_plugin)(void) = (WezDeskPlugin*(*)())GetProcAddress(p->module, "GetWezDeskPlugin");

		if (get_plugin) {
			TCHAR data_key[1024];
			wnsprintf(data_key, sizeof(data_key)/sizeof(data_key[0]),
				TEXT("Software\\Evil, as in Dr.\\WezDesk\\%s"), name);

			debug_printf(TEXT("plugin loaded; creating config key %s\r\n"), data_key);
			
			if (ERROR_SUCCESS == create_reg_key(HKEY_CURRENT_USER, data_key, &p->key)) {

				debug_printf(TEXT("ready to boot plugin\r\n"));
				p->plugin = (get_plugin)();

				debug_printf(TEXT("got struct %p\r\n"), p->plugin);

				if (p->plugin->plugin_version != WEZDESK_API_NUMBER) {
					core_message_box(name, TEXT("This plugin was built for a different version of the application"), MB_ICONHAND);
				} else {
					p->data = (p->plugin->initialize)(p, &core_funcs, slit);
					if (p->data) {
						plugins.push_back(p);
						return p;
					}
				}

				RegCloseKey(p->key);
			}
		}
		FreeLibrary(p->module);
	}
	delete p;
	return NULL;
}

static HKEY core_get_plugin_data_store(Plugin plugin) {
	return plugin->key;
}

int GetPluginBlobPath(Plugin plugin, TCHAR *key, TCHAR *path, int size)
{
	TCHAR sub[MAX_PATH];
	TCHAR dir[MAX_PATH];
	HANDLE f;
	DWORD dw;
	int len;

	SHGetFolderPathAndSubDir(NULL,
		CSIDL_FLAG_CREATE|CSIDL_APPDATA, NULL,
		SHGFP_TYPE_CURRENT, TEXT("Evil, as in Dr."), dir);

	StringCbPrintf(sub, sizeof(sub), 
		TEXT("%s\\Shell"),
		dir);
	CreateDirectory(sub, NULL);

	StringCbPrintf(sub, sizeof(sub), 
		TEXT("%s\\Shell\\Plugins"),
		dir);
	CreateDirectory(sub, NULL);

	StringCbPrintf(sub, sizeof(sub), 
		TEXT("%s\\Shell\\Plugins\\%s"),
		dir, plugin->name);
	CreateDirectory(sub, NULL);

	StringCbPrintf(sub, sizeof(sub), 
		TEXT("%s\\Shell\\Plugins\\%s\\blobs"),
		dir, plugin->name);
	CreateDirectory(sub, NULL);

	StringCbPrintf(path, size,
		TEXT("%s\\Shell\\Plugins\\%s\\blobs\\%s"),
		dir, plugin->name, key);

	return lstrlen(path);
}

static int GetPluginBlob(Plugin plugin, TCHAR *key, void **blob)
{
	TCHAR name[MAX_PATH];
	HANDLE f;
	DWORD dw;
	int len;

	GetPluginBlobPath(plugin, key, name, sizeof(name));
	
	f = CreateFile(name, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (f) {
		len = GetFileSize(f, &dw);
		*blob = malloc(len);
		if (*blob) {
			if (!ReadFile(f, *blob, len, &dw, NULL) || dw != len) {
				free(*blob);
				*blob = NULL;
				len = 0;
			}
		} else {
			len = 0;
		}
		CloseHandle(f);
	} else {
		*blob = NULL;
		len = 0;
	}
	return len;
}


HANDLE GetPluginBlobHandle(Plugin plugin, TCHAR *key, int forwrite)
{
	TCHAR name[MAX_PATH];
	HANDLE f;
	DWORD wrote;

	GetPluginBlobPath(plugin, key, name, sizeof(name));

	if (forwrite) {
		f = CreateFile(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
	} else {
	f = CreateFile(name, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	}
	return f;
}

static void StorePluginBlob(Plugin plugin, TCHAR *key, void *blob, int len)
{
	TCHAR name[MAX_PATH];
	HANDLE f;
	DWORD wrote;

	GetPluginBlobPath(plugin, key, name, sizeof(name));
	
	f = CreateFile(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
	if (f) {
		WriteFile(f, blob, len, &wrote, NULL);
		CloseHandle(f);
	}
}

static void core_set_gravity_from_config(Plugin plugin, HWND hwnd, LPCTSTR name, int defgrav, int defpri)
{
	DWORD val;
	DWORD len = sizeof(val);
	PerWindow *w = get_PerWindow(hwnd, current_desktop);

	if (w) {
		if (ERROR_SUCCESS == RegQueryValueEx(plugin->key, name, NULL, NULL, (LPBYTE)&val, &len)) {
			defgrav = (int)((val & 0xffff0000) >> 16);
			defpri = (int)(val & 0xffff);
		}
		w->gravity = defgrav;
		w->grav_pri = defpri;
	}
}

static const BYTE hexchars_iv[256] = {
 -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
 -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, /*0-31*/
 -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
 0,1,2,3,4,5,6,7,8,9,-1,-1,-1,-1,-1,-1, /*32-63*/
 -1,0xa,0xb,0xc,0xd,0xe,0xf,-1,-1,-1,-1,-1,-1,-1,-1,-1,
 -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, /*64-95*/
 -1,0xa,0xb,0xc,0xd,0xe,0xf,-1,-1,-1,-1,-1,-1,-1,-1,-1,
 -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, /*96-127*/
 -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
 -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, /*128-159*/
 -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
 -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, /*160-191*/
 -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
 -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, /*192-223*/
 -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
 -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1  /*224-255*/
};

static DWORD string_to_int(TCHAR *value)
{
	if (*value == '#') {
		/* it's an RGB string */
		BYTE r, g, b;
		value++;
		if (lstrlen(value) == 3) {
			r = hexchars_iv[value[0]];
			r |= r << 4;
			g = hexchars_iv[value[1]];
			g |= g << 4;
			b = hexchars_iv[value[2]];
			b |= b << 4;
		} else if (lstrlen(value) == 6) {
			r = (hexchars_iv[value[0]] << 4) | hexchars_iv[value[1]];
			g = (hexchars_iv[value[2]] << 4) | hexchars_iv[value[3]];
			b = (hexchars_iv[value[4]] << 4) | hexchars_iv[value[5]];
		} else {
			return 0;
		}
		return RGB(r, g, b);
	} else {
		return _wtoi(value);
	}
}

static DWORD core_get_plugin_int(Plugin plugin, LPCTSTR name, DWORD defval) {
	DWORD ret;
	DWORD len = sizeof(ret);
	int i;

	if (plugin) {
		for (i = 0; i < plugin->config.size(); i++) {
			struct config_value cfg = plugin->config[i];
			if (!lstrcmp(name, cfg.name)) {
				return string_to_int(cfg.value);
			}
		}
	} else {
		for (i = 0; i < core_config.size(); i++) {
			struct config_value cfg = core_config[i];
			if (!lstrcmp(name, cfg.name)) {
				return string_to_int(cfg.value);
			}
		}
		return defval;
	}

	if (ERROR_SUCCESS == RegQueryValueEx(plugin->key, name, NULL, NULL, (LPBYTE)&ret, &len)) {
		return ret;
	}
	return defval;
}

static LPTSTR core_get_plugin_string(Plugin plugin, LPCTSTR name, LPCTSTR defval) {
	LPTSTR ret = NULL;
	DWORD len = 0;
	int i;

	if (plugin) {
		for (i = 0; i < plugin->config.size(); i++) {
			struct config_value cfg = plugin->config[i];
			if (!lstrcmp(name, cfg.name)) {
				return lstrdup(cfg.value);
			}
		}
	} else {
		for (i = 0; i < core_config.size(); i++) {
			struct config_value cfg = core_config[i];
			if (!lstrcmp(name, cfg.name)) {
				return lstrdup(cfg.value);
			}
		}
	}

	if (plugin == NULL) {
		HKEY key;
		if (ERROR_SUCCESS == create_reg_key(HKEY_CURRENT_USER, TEXT("Software\\Evil, as in Dr.\\WezDesk\\.core"), &key)) {
			if (ERROR_SUCCESS == RegQueryValueEx(key, name, NULL, NULL, NULL, &len)) {
				ret = (LPTSTR)calloc(len, sizeof(BYTE));
				if (ERROR_SUCCESS == RegQueryValueEx(key, name, NULL, NULL, (LPBYTE)ret, &len)) {
					RegCloseKey(key);
					return ret;
				}
				free(ret);
			}
			RegCloseKey(key);
		}
	} else if (ERROR_SUCCESS == RegQueryValueEx(plugin->key, name, NULL, NULL, NULL, &len)) {
		ret = (LPTSTR)calloc(len, sizeof(BYTE));
		if (ERROR_SUCCESS == RegQueryValueEx(plugin->key, name, NULL, NULL, (LPBYTE)ret, &len)) {
			return ret;
		}
		free(ret);
	}
	return defval ? lstrdup(defval) : NULL;
}

static HMODULE core_get_plugin_instance(Plugin plugin) {
	return plugin->module;
}

static void *core_get_plugin_data(Plugin plugin) {
	return plugin->data;
}

static void core_set_gravity(HWND hwnd, int gravity, int priority) {
	PerWindow *w = get_PerWindow(hwnd, current_desktop);
	if (w) {
		w->gravity = gravity;
		w->grav_pri = priority;
	}
}

static int core_get_gravity(HWND hwnd, int *gravity, int *priority) {
	PerWindow *w = get_PerWindow(hwnd, current_desktop);
	if (w) {
		if (gravity)
			*gravity = w->gravity;
		if (priority)
			*priority = w->grav_pri;

		return 1;
	}
	return 0;
}

static TCHAR* lstrchr(TCHAR *str, TCHAR c)
{
	while (*str) {
		if (*str == c)
			return str;
		str++;
	}
	return NULL;
}

static HFONT core_load_font(HDC hdc, TCHAR *descriptor)
{
	TCHAR *name = lstrdup(descriptor);
	TCHAR *tmp, *tmp2;
	HFONT f;
	LOGFONT lf;

	memset(&lf, 0, sizeof(lf));
	lf.lfHeight = 12;
	lf.lfWeight = FW_NORMAL;
	lf.lfCharSet = DEFAULT_CHARSET;
	lf.lfOutPrecision = OUT_TT_PRECIS;
	lf.lfQuality = CLEARTYPE_QUALITY;
	lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
	
	tmp = lstrchr(name, ',');
	if (!tmp) {
		StringCbCopy(lf.lfFaceName, sizeof(lf.lfFaceName), name);
	} else {
		*tmp = '\0';
		StringCbCopy(lf.lfFaceName, sizeof(lf.lfFaceName), name);
		tmp++;

		while (lstrlen(tmp)) {
			tmp2 = lstrchr(tmp, ',');
			if (tmp2) {
				*tmp2 = '\0';
			}

			if (!lstrcmp(tmp, TEXT("bold"))) {
				lf.lfWeight = FW_BOLD;
			} else if (!lstrcmp(tmp, TEXT("italic"))) {
				lf.lfItalic = TRUE;
			} else if (!lstrcmp(tmp, TEXT("strikeout"))) {
				lf.lfStrikeOut = TRUE;
			}

			if (!tmp2)
				break;
			tmp = tmp2 + 1;
		}
	}

	lf.lfHeight = -MulDiv(lf.lfHeight, GetDeviceCaps(hdc, LOGPIXELSY), 72);

	f = CreateFontIndirect(&lf);

	free(name);
	return f;
}

static HFONT core_get_stock_font(int which)
{
	switch (which) {
		case WEZDESK_FONT_DEFAULT:
			if (font_def == NULL) {
				LOGFONT lf;
				if (SystemParametersInfo(SPI_GETICONTITLELOGFONT, sizeof(lf), &lf, 0)) {
					font_def = CreateFontIndirect(&lf);
				} else {
					font_def = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
				}
			}
			return font_def;
		
		case WEZDESK_FONT_DEFAULT_BOLD:
			if (font_def_bold == NULL) {
				LOGFONT lf;
				GetObject(core_get_stock_font(WEZDESK_FONT_DEFAULT), sizeof(lf), &lf);
				lf.lfWeight += 300;
				if (lf.lfWeight > FW_BLACK)
					lf.lfWeight = FW_BLACK;
				font_def_bold = CreateFontIndirect(&lf);
			}
			return font_def_bold;

		case WEZDESK_FONT_TITLE:
			if (font_def_title == NULL) {
				NONCLIENTMETRICS m;
				m.cbSize = sizeof(NONCLIENTMETRICS);
				if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, m.cbSize, &m, 0)) {
					debug_printf(TEXT("TITLE FONT:%s\r\n"), m.lfCaptionFont.lfFaceName);
					font_def_title = CreateFontIndirect(&m.lfCaptionFont);
				} else {
					font_def_title = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
				}
			}
			return font_def_title;
		case WEZDESK_FONT_SMALL_TITLE:
			if (font_def_smtitle == NULL) {
				NONCLIENTMETRICS m;
				m.cbSize = sizeof(NONCLIENTMETRICS);
				if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, m.cbSize, &m, 0)) {
					debug_printf(TEXT("SMALL TITLE FONT:%s\r\n"), m.lfSmCaptionFont.lfFaceName);
					font_def_smtitle = CreateFontIndirect(&m.lfSmCaptionFont);
				} else {
					font_def_smtitle = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
				}
			}
			return font_def_smtitle;
		case WEZDESK_FONT_TOOLTIP:
			if (font_def_tip == NULL) {
				NONCLIENTMETRICS m;
				m.cbSize = sizeof(NONCLIENTMETRICS);
				if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, m.cbSize, &m, 0)) {
					debug_printf(TEXT("TIP FONT:%s\r\n"), m.lfStatusFont.lfFaceName);
					font_def_tip = CreateFontIndirect(&m.lfStatusFont);
				} else {
					font_def_tip = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
				}
			}
			return font_def_tip;
		case WEZDESK_FONT_MESSAGE:
			if (font_def_msg == NULL) {
				NONCLIENTMETRICS m;
				m.cbSize = sizeof(NONCLIENTMETRICS);
				if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, m.cbSize, &m, 0)) {
					debug_printf(TEXT("MESSAGE FONT:%s\r\n"), m.lfMessageFont.lfFaceName);
					font_def_msg = CreateFontIndirect(&m.lfMessageFont);
				} else {
					font_def_msg = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
				}
			}
			return font_def_msg;

	}
	return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

static void fixup_window_munchness(HWND hWnd, BOOL delay)
{
	HMONITOR mon;
	RECT work, rect;
	
	/* sanity check positioning for multi-monitor-ness */
	mon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONULL);

	if (delay && mon == NULL) {
		Sleep(200);
		mon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONULL);
	}

	if (mon == NULL) {
		/* not on any monitor - pull it on */
		SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
		GetWindowRect(hWnd, &rect);

		rect.right -= rect.left;
		rect.bottom -= rect.top;
		rect.left = rect.top = 0;

		rect.left = (work.right - work.left) / 2;
		rect.top = (work.bottom - work.top) / 2;
	
		if (rect.right < GetSystemMetrics(SM_CXMIN)) {
			/* munched! */
			rect.right = 4 * GetSystemMetrics(SM_CXMIN);
		}
		if (rect.bottom < GetSystemMetrics(SM_CYMIN)) {
			/* munched! */
			rect.bottom = 4 * GetSystemMetrics(SM_CYMIN);
		}
			
		SetWindowPos(hWnd, HWND_TOP, rect.left, rect.top, rect.right, rect.bottom,
			SWP_ASYNCWINDOWPOS|SWP_SHOWWINDOW);
	}
}

static void core_switch_to_window(HWND hWnd, BOOL altTab)
{
	PerWindow *win;
	HWND fg = GetForegroundWindow();

#if 0
	if (fg != hWnd && is_app_window(hWnd)) {
		win = get_PerWindow(fg, current_desktop);
		if (win) {
			PostMessage(root_window, WZDM_DEFER_CAPTURE, (WPARAM)win->wnd, 0);
		}
		win = NULL;
	}
#endif

	SwitchToThisWindow(hWnd, altTab);
	fixup_window_munchness(hWnd, FALSE);
	
#if 1
	if (is_app_window(hWnd)) {
		win = get_PerWindow(hWnd, current_desktop);
		if (win) {
			PostMessage(root_window, WZDM_DEFER_CAPTURE, (WPARAM)win->wnd, 0);
		}
	}
#endif
}

static Image *core_get_window_thumbnail(HWND hWnd, double *scale)
{
	PerWindow *win;

	if (is_app_window(hWnd)) {
		win = get_PerWindow(hWnd, current_desktop);
		if (win) {
			if (!win->capture && !IsIconic(hWnd)) {
				capture_window(win);
			}
			if (scale) *scale = win->capture_scale;
			return win->capture;
		}
	}
	return NULL;
}

static void core_define_function(Plugin plugin, TCHAR *name, void (*func)(TCHAR *arg, HWND wnd))
{
	struct plugin_func fn;

	fn.name = lstrdup(name);
	fn.func = func;

	plugin->funcs.push_back(fn);
}

void core_set_config(TCHAR *name, TCHAR *value)
{
	struct config_value config;
	config.name = lstrdup(name);
	config.value = lstrdup(value);
	core_config.push_back(config);
}


void plugin_set_config(TCHAR *plugin_name, TCHAR *name, TCHAR *value)
{
	PluginIterator iter;
	for (iter = plugins.begin(); iter != plugins.end(); iter++) {
		Plugin p = *iter;
		if (!lstrcmp(p->name, plugin_name)) {
			struct config_value config;
			config.name = lstrdup(name);
			config.value = lstrdup(value);
			p->config.push_back(config);
			break;
		}
	}
}

int core_send_notify(UINT msg, UINT secondary, WPARAM wparam, LPARAM lparam)
{
	PluginIterator iter;
	int ret;

	ret = 0;

	for (iter = plugins.begin(); iter != plugins.end(); iter++) {
		Plugin p = *iter;
		if (p->plugin->on_notify) {
			int r;
			r = (p->plugin->on_notify)(p, &core_funcs, msg, secondary, wparam, lparam);
			if (r == 0) {
				continue;
			}
			ret = r;
			if (ret < 0)
				break;
		}
	}

	return ret;
}

static HICON core_get_window_icon(HWND w, DWORD timeoutms)
{
	HICON icon = 0;

	SendMessageTimeout(w, WM_GETICON, ICON_BIG, 0,
				SMTO_ABORTIFHUNG, timeoutms, (PDWORD_PTR)&icon);
	if (!icon) {
		icon = (HICON)GetClassLongPtr(w, GCLP_HICON);
	}
	return icon;
}

int core_load_string(UINT uID, TCHAR *buf, int nbuf)
{
	int ret;
	if (i18n_dll) {
		ret = LoadString(i18n_dll, uID, buf, nbuf);
		if (ret) return ret;
	}
	return LoadString(i18n_def, uID, buf, nbuf);
}

static HRESULT (__stdcall *DwmRegisterThumbnail)(HWND,HWND,HTHUMBNAIL*) = NULL;
static HRESULT (__stdcall *DwmUnregisterThumbnail)(HTHUMBNAIL) = NULL;
static HRESULT (__stdcall *DwmUpdateThumbnailProperties)(HTHUMBNAIL, const DWM_THUMBNAIL_PROPERTIES *) = NULL;
static HRESULT (__stdcall *DwmQueryThumbnailSourceSize)(HTHUMBNAIL, PSIZE) = NULL;


static WezDeskThumb *core_register_thumb(HWND renderDest, HWND src)
{
	WezDeskThumb *t = new WezDeskThumb;
	memset(t, 0, sizeof(*t));
	t->wDest = renderDest;
	t->wSrc = src;
	if (DwmRegisterThumbnail) {
		DwmRegisterThumbnail(renderDest, src, &t->vt);
	}
	return t;
}

static HRESULT core_render_thumb(WezDeskThumb *thumb)
{
	if (DwmUpdateThumbnailProperties) {
		DWM_THUMBNAIL_PROPERTIES tp;
		tp.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_RECTSOURCE |
			DWM_TNP_OPACITY | DWM_TNP_VISIBLE | 
			DWM_TNP_SOURCECLIENTAREAONLY;
		tp.fSourceClientAreaOnly = TRUE;
		tp.fVisible = TRUE;
		tp.opacity = thumb->opacity;
		tp.rcDestination = thumb->dest;

		return DwmUpdateThumbnailProperties(thumb->vt, &tp);
	}

	double scale = 1;
	Image *ti = core_funcs.GetWindowThumbnail(thumb->wSrc, &scale);
	if (ti) {
		HDC hdc = GetDC(thumb->wDest);

		Graphics *G = new Graphics(hdc, NULL);
		G->SetInterpolationMode(InterpolationModeHighQualityBicubic);
		G->DrawImage(ti, thumb->dest.left, thumb->dest.top,
				RECTWIDTH(thumb->dest), RECTHEIGHT(thumb->dest));
		delete G;
		ReleaseDC(thumb->wDest, hdc);
		return S_OK;
	}
	return S_OK;
}

static void core_unregister_thumb(WezDeskThumb *thumb)
{
	if (DwmUnregisterThumbnail && thumb->vt) {
		DwmUnregisterThumbnail(thumb->vt);
		thumb->vt = NULL;
	}
	delete thumb;
}
	
static HRESULT core_query_thumb_size(WezDeskThumb *thumb, PSIZE psize)
{
	if (DwmQueryThumbnailSourceSize) {
		return DwmQueryThumbnailSourceSize(thumb->vt, psize);
	}
	RECT r;
	GetClientRect(thumb->wSrc, &r);
	psize->cx = RECTWIDTH(r);
	psize->cy = RECTHEIGHT(r);
	return S_OK;
}

static TCHAR *core_get_file_path(TCHAR *filename)
{
	TCHAR p[MAX_PATH];
	StringCbPrintf(p, sizeof(p), TEXT("%s\\%s"),
		base_dir, filename);
	return lstrdup(p);
}

void schedule_plugin_callbacks(void)
{
	vector <plugin_callback>::iterator iter;
	PluginIterator p;
	DWORD now = GetTickCount();
	DWORD due = 0;
	Plugin plugin;

//	EnterCriticalSection(&callback_mutex);

	for (p = plugins.begin(); p != plugins.end(); p++) {
		plugin = *p;
again:
		for (iter = plugin->callbacks.begin(); iter != plugin->callbacks.end();
				iter++) {
			if (iter->due == 0) {
				plugin->callbacks.erase(iter);
				goto again;
			}
			if (iter->due < due) {
				due = iter->due;
				debug_printf(TEXT("plugin %s has an event at %d\n"),
					plugin->name, iter->due);
			}
		}
	}


	if (due) {
		debug_printf(TEXT("scheduled a callback for %d ticks, thats in %d\n"),
				due, due - now);
	}
	
//	LeaveCriticalSection(&callback_mutex);
}
static int core_schedule_callback(Plugin plugin, DWORD msFromNow,
		void *cookie, void (*func)(void*))
{
	plugin_callback cb;
	static int id = 1;
	DWORD now, due = 0;
	now = GetTickCount();

	cb.id = id++;
	cb.due = now + msFromNow;
	cb.cookie = cookie;
	cb.func = func;

	plugin->callbacks.push_back(cb);

	schedule_plugin_callbacks();

	return cb.id;
}

static HANDLE core_compile_regexp(LPCTSTR pattern)
{
	wezdesk_re *re = new wezdesk_re;
	if (!re->compile(pattern)) {
		delete re;
		return NULL;
	}
	return (HANDLE)re;
}

static int core_match_regexp(HANDLE hre, LPCTSTR subject)
{
	wezdesk_re *re = (wezdesk_re*)hre;
	if (!re) return 0;
	return re->match(subject);
}

WezDeskFuncs core_funcs = {
	WEZDESK_API_NUMBER,
	TEXT("Wez's Windows Shell Replacement"),
	NULL,
	NULL,
	core_execute,
	core_switch_desktop,
	core_message_box,
	core_get_root_window,
	core_change_desktop_bits,
	core_get_active_desktop,
	core_load_plugin,
	core_get_plugin_data_store,
	core_get_plugin_data,
	core_get_plugin_instance,
	core_get_plugin_int,
	core_get_plugin_string,
	debug_printf,
	tray_get_tray_data,
	core_set_gravity,
	core_set_gravity_from_config,
	core_get_gravity,
	DrawShadowText,
	core_get_stock_font,
	core_switch_to_window,
	slit_add_callback_menu_item,
	slit_add_per_plugin_sub_menu,
	context_menu_register_plugin_submenu,
	core_get_window_thumbnail,
	core_define_function,
	core_send_notify,
	core_load_font,
	get_slit_alignment,
	is_app_window,
	core_get_window_module_path,
	core_get_window_icon,
	core_load_string,
	core_register_thumb,
	core_render_thumb,
	core_unregister_thumb,
	core_query_thumb_size,
	core_get_file_path,
	StorePluginBlob,
	GetPluginBlobHandle,
	GetPluginBlobPath,
	GetPluginBlob,
	core_schedule_callback,
	core_compile_regexp,
	core_match_regexp
};

static int import_funcs(void)
{
	HMODULE mod;

	mod = GetModuleHandle(TEXT("SHELL32.DLL"));
	core_funcs.MSWinShutdown = (void (__stdcall*)(HWND))GetProcAddress(mod, (LPCSTR)MAKELPARAM(0x3c, 0));
	core_funcs.RunDlg = (void (__stdcall*)(HWND,HICON,LPCTSTR,LPCTSTR,LPCTSTR,int))GetProcAddress(mod, (LPCSTR)MAKELPARAM(61,0));

	/* Vista Desktop Window Manager */
	mod = LoadLibrary(TEXT("dwmapi.dll"));
	if (mod) {
		DwmRegisterThumbnail = 
			(HRESULT (__stdcall*)(HWND,HWND,HTHUMBNAIL*))GetProcAddress(mod,
			"DwmRegisterThumbnail");
		DwmUnregisterThumbnail = 
			(HRESULT (__stdcall*)(HTHUMBNAIL))GetProcAddress(mod,
			"DwmUnregisterThumbnail");
		DwmUpdateThumbnailProperties = 
			(HRESULT (__stdcall*)(HTHUMBNAIL, const DWM_THUMBNAIL_PROPERTIES*))GetProcAddress(mod,
			"DwmUpdateThumbnailProperties");
		DwmQueryThumbnailSourceSize = 
			(HRESULT (__stdcall*)(HTHUMBNAIL, PSIZE))GetProcAddress(mod,
			"DwmQueryThumbnailSourceSize");
	}

	return 1;
}

static int single_instance_only(void) {
	
	SetLastError(NO_ERROR);
	single_instance_mtx = CreateMutex(NULL, FALSE, TEXT("WezDeskSingleInstanceMutex"));
	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		return 0;
	}
	return 1;
}

static BOOL CALLBACK fixup_windows_callback(HWND hwnd, LPARAM lparam)
{
	if (is_app_window(hwnd)) {
		get_PerWindow(hwnd, 0);
	}
	return TRUE;
}

static void fixup_windows(void)
{
	EnumWindows(fixup_windows_callback, 0);
}

static BOOL CALLBACK cleanup_windows_callback(HWND hwnd, LPARAM lparam)
{
	delete_PerWindow(hwnd);
	return TRUE;
}

static BOOL apply_window_match(TCHAR *match_type, PerWindow *win)
{
	int i, len;
	TCHAR buffer[1024];
	int matches = 0;
	
	for (i = 0; i < window_matches.size(); i++) {
		struct window_match wm = window_matches[i];
		TCHAR *what = NULL;

		if (lstrcmp(match_type, wm.match_type)) continue;

		if (!lstrcmp(wm.propname, TEXT("CAPTION"))) {
			len = GetWindowText(win->wnd, buffer, (sizeof(buffer) / sizeof(buffer[0])));
			buffer[len] = '\0';
		} else if (!lstrcmp(wm.propname, TEXT("MODULE"))) {
			len = GetWindowModuleFileName(win->wnd, buffer, (sizeof(buffer) / sizeof(buffer[0])));
			buffer[len] = '\0';
		} else if (!lstrcmp(wm.propname, TEXT("CLASS"))) {
			len = GetClassName(win->wnd, buffer, (sizeof(buffer) / sizeof(buffer[0])));
			buffer[len] = '\0';
		} else {
			continue;
		}
		what = buffer;

		if (wm.re->match(what) > 0) {
			active_workspace_context_window = win->wnd;
			run_func_by_name(wm.funcname, wm.funcarg, win->wnd);
			active_workspace_context_window = NULL;
			matches++;
		}
	}
	return matches ? TRUE : FALSE;
}

static LRESULT CALLBACK shell_wnd_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_ENDSESSION:
		case WM_QUERYENDSESSION:
			return TRUE;

		case WM_TIMER:
			switch (wParam) {
				case 1:
					/* run a startup item */
					if (startup_items.size()) {
						vector <struct startup_item>::iterator iter;
						iter = startup_items.begin();
						struct startup_item item = *iter;
						startup_items.erase(iter);
						run(item.what);
						free(item.what);
					}
					if (!startup_items.size()) {
						KillTimer(hWnd, startup_items_timer);
					}
					break;
				case 2:
					/* dispatch a plugin callback */
					{
						vector <plugin_callback>::iterator iter;
						PluginIterator p;
						Plugin plugin;
						DWORD now;

						now = GetTickCount();
						for (p = plugins.begin(); p != plugins.end(); p++) {
							plugin = *p;
							for (iter = plugin->callbacks.begin(); 
									iter != plugin->callbacks.end();
									iter++) {
								if (iter->due == 0) {
									continue;
								}
								if (iter->due <= now) {
									iter->due = 0;
									iter->func(iter->cookie);
									break;
								}
							}
						}
						schedule_plugin_callbacks();
					}
					break;
			}
			return 0;


		case WM_HOTKEY:
			if (handle_hotkey(LOWORD(lParam), HIWORD(lParam))) {
				return 1;
			}
			break;
			
		case WM_CREATE:
			break;
	
		case WM_CONTEXTMENU:
		{
			SetForegroundWindow(hWnd);
			popup_context_menu(LOWORD(lParam), HIWORD(lParam), hWnd, TEXT("root"));
			return 1;
		}
	
		case WZDM_DESKTOP_MOUSE:
			switch (wParam) {
				case WM_RBUTTONUP:
					{
						if (!fake_root_context_hotkey()) {
							PostMessage(hWnd, WM_CONTEXTMENU, (WPARAM)find_a_slit(hWnd), lParam);
						}
						return 1;
//						popup_context_menu(LOWORD(lParam),
//							HIWORD(lParam), find_a_slit(hWnd),
//							TEXT("root"));
					}
					break;
			}
			return 1;
		case WZDM_DEFER_CAPTURE:
			{
				HWND wnd = (HWND)wParam;
				if (is_app_window(wnd)) {
					PerWindow *win = get_PerWindow(wnd, current_desktop);
					if (win) {
						capture_window(win);
					}
				}
			}
			return 1;
		case WZDM_ALT_TAB:
			if (wParam || lParam)
				DebugBreak();
			core_send_notify(WZN_ALT_TAB_SELECT, 0, 0, 0);
			return 1;

		case WZDM_FREE_MENU:
			contextmenu_delete_pool((void*)wParam);
			return 1;

		default:
			if (uMsg == WM_SHELLHOOKMESSAGE) {

				switch (wParam) {
					case HSHELL_GETMINRECT: 
					{
						RECT *r = (RECT*)lParam;
						debug_printf(TEXT("HSHELL_GETMINRECT (%d,%d,%d,%d)\r\n"), r->left,r->top, r->right, r->bottom);
						//capture_window(get_PerWindow((HWND)wParam, current_desktop));
						
						break;
					}
					case HSHELL_WINDOWACTIVATED: debug_printf(TEXT("HSHELL_WINDOWACTIVATED\r\n")); break;
					case HSHELL_RUDEAPPACTIVATED: debug_printf(TEXT("HSHELL_RUDEAPPACTIVATED\r\n")); break;
					case HSHELL_WINDOWREPLACING: debug_printf(TEXT("HSHELL_WINDOWREPLACING\r\n")); break;
					case HSHELL_WINDOWREPLACED: debug_printf(TEXT("HSHELL_WINDOWREPLACED\r\n")); break;
					case HSHELL_WINDOWCREATED: debug_printf(TEXT("HSHELL_WINDOWCREATED\r\n")); break;
					case HSHELL_WINDOWDESTROYED: debug_printf(TEXT("HSHELL_WINDOWDESTROYED\r\n")); break;
					case HSHELL_ACTIVATESHELLWINDOW: debug_printf(TEXT("HSHELL_ACTIVATESHELLWINDOW\r\n")); break;
					case HSHELL_TASKMAN: debug_printf(TEXT("HSHELL_TASKMAN\r\n")); break;
					case HSHELL_REDRAW: debug_printf(TEXT("HSHELL_REDRAW\r\n")); break;
					case HSHELL_FLASH: debug_printf(TEXT("HSHELL_FLASH\r\n")); break;
					case HSHELL_ENDTASK: debug_printf(TEXT("HSHELL_ENDTASK\r\n")); break;
					case HSHELL_APPCOMMAND: debug_printf(TEXT("HSHELL_APPCOMMAND \r\n")); break;
					default:
						debug_printf(TEXT("shell hook: %x\r\n"), wParam);
				}

				/* workspace management */
				switch (wParam) {
					case HSHELL_WINDOWCREATED:
					{
						if (lParam && !desktop_switching) {
							PerWindow *win = get_PerWindow((HWND)lParam, current_desktop);
							apply_window_match(TEXT("CREATE"), win);
							slit_hide_if_fullscreen((HWND)lParam);
						}

						break;
					}
					case HSHELL_WINDOWACTIVATED:
					case HSHELL_RUDEAPPACTIVATED:
					{
						/* ensure that the window is marked as being present on this desktop */
						if (lParam && !desktop_switching) {
							PerWindow *win = get_PerWindow((HWND)lParam, current_desktop);
							slit_hide_if_fullscreen((HWND)lParam);
						//	capture_window(win);
						}
						break;
					}

					case HSHELL_REDRAW:
						if (lParam && !desktop_switching && !IsIconic((HWND)lParam)) {
							/* ensure that the window is marked as being present on this desktop */
							PerWindow *win = get_PerWindow((HWND)lParam, current_desktop);
							win->desktop_bits |= 1 << current_desktop;
							win->not_minimized |= 1 << current_desktop;
							slit_hide_if_fullscreen((HWND)lParam);
//							capture_window(win);
						}
						break;
				}

				
				PluginIterator iter;

				for (iter = plugins.begin(); iter != plugins.end(); iter++) {
					Plugin p = *iter;
					if (p->plugin && p->plugin->on_shell_message) {
						(p->plugin->on_shell_message)(p, &core_funcs, wParam, lParam);
					}
				}

				switch (wParam) {
					case HSHELL_WINDOWDESTROYED:
						/* remove our per-window structure */
						delete_PerWindow((HWND)lParam);
						break;
				}


			} else if (uMsg == WM_WORKSPACE) {
				HWND h = (HWND)wParam;
				DWORD bits;
				
				if (lParam == 32) {
					bits = 0xffffffff;
				} else {
					bits = 1 << (lParam);
				}
				PerWindow *w = get_PerWindow(h, core_funcs.GetActiveDesktop());
				if (w) {
					w->desktop_bits = w->not_minimized = bits;
					if (bits != 0xffffffff) {
						core_funcs.SwitchDesktop(lParam+1);
						ShowWindowAsync(h, SW_SHOW);
					}
				}
				return 0;
			}

			return context_menu_proc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}

static LRESULT CALLBACK ShellProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	/* just having this here helps persuade RegisterShellHookWindow to work */
	debug_printf(TEXT("ShellProc: nCode = %d\r\n"), nCode);
	if (nCode == HSHELL_GETMINRECT) {
		debug_printf(TEXT("A Window is going to be minimized\r\n"));
	}
	return CallNextHookEx(the_shell_hook, nCode, wParam, lParam);
}


static int init_shell(HINSTANCE instance)
{
	MINIMIZEDMETRICS mm;
	WNDCLASS wc;
	INITCOMMONCONTROLSEX ic;
	ANIMATIONINFO ai;

	/* hide minimized windows */
	memset(&mm, 0, sizeof(mm));
	mm.cbSize = sizeof(MINIMIZEDMETRICS);
	SystemParametersInfo(SPI_GETMINIMIZEDMETRICS, sizeof(MINIMIZEDMETRICS), &mm, 0);
	if (mm.iArrange & ARW_HIDE) {
		debug_printf(TEXT("already set to hide\r\n"));
	} else {
		mm.iArrange = ARW_TOPLEFT|ARW_RIGHT|ARW_HIDE;
		SetLastError(0);
		if (!SystemParametersInfo(SPI_SETMINIMIZEDMETRICS, sizeof(MINIMIZEDMETRICS), &mm, SPIF_SENDCHANGE)) {
			debug_printf(TEXT("failed to set minimize metrics?? %d\r\n"), GetLastError());
		} else {
			debug_printf(TEXT("metrics were set, last error is 0x%08x\r\n"), GetLastError());
		}
	}
	debug_printf(TEXT("GetSystemMetrics(SM_ARRANGE) = 0x%08x\r\n"), GetSystemMetrics(SM_ARRANGE));

	memset(&ai, 0, sizeof(ai));
	ai.cbSize = sizeof(ai);
	ai.iMinAnimate = 0;
	SystemParametersInfo(SPI_SETANIMATION, sizeof(ai), &ai, 0);

	memset(&wc, 0, sizeof(wc));
	wc.hInstance = instance;
	wc.lpfnWndProc = shell_wnd_proc;
	wc.lpszClassName = TEXT("WezDesk Shell Window");
	
	if (!RegisterClass(&wc)) {
		return 0;
	}

	root_window = CreateWindowEx(WS_EX_TOOLWINDOW,
			wc.lpszClassName,
			wc.lpszClassName,
			WS_POPUP,
			0, 0, 0, 0,
			NULL, NULL,
			instance,
			NULL);

	if (!root_window) {
		return 0;
	}

	SetForegroundWindow(root_window);
	
	WM_SHELLHOOKMESSAGE = RegisterWindowMessage(TEXT("SHELLHOOK"));
	WM_WORKSPACE = RegisterWindowMessage(TEXT("WezDeskWorkSpaceMessage"));
	debug_printf(TEXT("SHELLHOOKMESSAGE is %d\r\n"), WM_SHELLHOOKMESSAGE);

#if 1
	the_shell_hook = SetWindowsHookEx(WH_SHELL, ShellProc, NULL, GetCurrentThreadId());
	debug_printf(TEXT("the_shell_hook = %x\r\n"), the_shell_hook);
#endif

#if 1
	BOOL (WINAPI *reg_shell_window)(HWND, DWORD) = (BOOL(WINAPI*)(HWND,DWORD))GetProcAddress(LoadLibrary(TEXT("SHELL32.DLL")), (LPSTR)((long)0xB5));
	if (reg_shell_window) {
		if (!reg_shell_window(root_window, 3)) {
			debug_printf(TEXT("failed to register shell hook window??\r\n"));
		} else {
			debug_printf(TEXT("registered shell hook window\r\n"));
		}
	} else {
		debug_printf(TEXT("unable to find api for registering hook\r\n"));
	}

#else

#if 0
	BOOL (__stdcall *set_task_man_window)(HWND) = (BOOL(__stdcall*)(HWND))GetProcAddress(LoadLibrary(TEXT("SHELL32.DLL")), "SetTaskmanWindow");
	if (set_task_man_window) {
		set_task_man_window(root_window);
	}
#endif

	if (!RegisterShellHookWindow(root_window)) {
		debug_printf(TEXT("failed to register shell hook window??\r\n"));
		return 0;
	} else {
		debug_printf(TEXT("registered shell hook window\r\n"));
	}
#endif

#if 0
	/* SetShellWindow is for acting as a file manager */
	BOOL (__stdcall *set_shell_window)(HWND) = (BOOL(__stdcall*)(HWND))GetProcAddress(LoadLibrary(TEXT("SHELL32.DLL")), "SetShellWindow");
	if (set_shell_window) {
		set_shell_window(root_window);
	}
#endif


	ic.dwSize = sizeof(ic);
	ic.dwICC = ICC_BAR_CLASSES|ICC_WIN95_CLASSES;
	if (!InitCommonControlsEx(&ic)) {
		debug_printf(TEXT("Failed to init common controls!!!!!\r\n"));
		return 0;
	}
	
	/* magic for DDE */
	void (__stdcall *ddefunc)(BOOL) = (void(__stdcall*)(BOOL))GetProcAddress(LoadLibrary(TEXT("SHDOCVW.DLL")), (LPSTR)118);
	if (ddefunc) {
		debug_printf(TEXT("Hooking up magic DDE function\r\n"));
		(ddefunc)(TRUE);
	}

	/* tell the logon screen to show the desktop */
	HANDLE hEvent = OpenEvent(EVENT_MODIFY_STATE, 1, TEXT("msgina: ShellReadyEvent"));
	if (hEvent) {
		SetEvent(hEvent);
		CloseHandle(hEvent);
	}

	
	return 1;
}

static int sys_install(BOOL install) {
	HKEY key, fkey;
	DWORD result;
	int retval = 0;
	TCHAR *shell = install ?
		TEXT("USR:Software\\Microsoft\\Windows NT\\CurrentVersion\\WinLogon") :
		TEXT("SYS:Microsoft\\Windows NT\\CurrentVersion\\WinLogon");

	if (ERROR_SUCCESS == RegCreateKeyEx(HKEY_LOCAL_MACHINE,
			TEXT("Software\\Microsoft\\Windows NT\\CurrentVersion\\IniFileMapping\\system.ini\\boot"), 0, NULL,
			REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &key, &result)) {
		if (ERROR_SUCCESS == RegSetValueEx(key, TEXT("Shell"), 0, REG_SZ,
				(BYTE*)shell, sizeof(TCHAR) * (lstrlen(shell)+1))) {
			retval = 1;
		}
		RegCloseKey(key);
	}
	return retval;
}

static int install_shell(LPTSTR shell, DWORD desktop_process) {
	HKEY key, fkey;
	DWORD result;
	int retval = 0;

	if (ERROR_SUCCESS == RegCreateKeyEx(HKEY_CURRENT_USER,
			TEXT("Software\\Microsoft\\Windows NT\\CurrentVersion\\WinLogon"), 0, NULL,
			REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &key, &result)) {
		if (ERROR_SUCCESS == RegSetValueEx(key, TEXT("Shell"), 0, REG_SZ, (BYTE*)shell, sizeof(TCHAR) * (lstrlen(shell)+1))) {
			if (ERROR_SUCCESS == RegCreateKeyEx(HKEY_CURRENT_USER,
					TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer"), 0, NULL,
					REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &fkey, &result)) {
				if (ERROR_SUCCESS == RegSetValueEx(fkey, TEXT("DesktopProcess"), 0, REG_DWORD, (BYTE*)&desktop_process, sizeof(desktop_process))) {
					retval = 1;
				}
				RegCloseKey(fkey);
			}
		}
		RegCloseKey(key);
	}

	return retval;
}

static int handle_command_line(HINSTANCE instance)
{
	LPTSTR cmdline, rtrim;
	TCHAR command_line_buffer[2048];

	StringCbCopy(command_line_buffer, sizeof(command_line_buffer), GetCommandLine());
	cmdline = command_line_buffer;

	debug_printf(TEXT("GetCommandLine: %s\r\n"), cmdline);
	
	if (*cmdline == '"') {
		for (cmdline++; *cmdline && *cmdline != '"'; cmdline++)
			;
	}

	while (*cmdline && *cmdline != ' ')
		cmdline++;
	while (*cmdline && *cmdline == ' ')
		cmdline++;
	
	rtrim = cmdline + lstrlen(cmdline) - 1;
	while (*rtrim == ' ') {
		*rtrim = '\0';
		--rtrim;
	}

	debug_printf(TEXT("munged to %s\r\n"), cmdline);
#if 0
	{
		TCHAR message[2048];
		wsprintf(message, TEXT("munged to '%s' (%d chars)"), cmdline, lstrlen(cmdline));
		core_message_box(TEXT("WezDesk"), message, MB_ICONERROR|MB_OK);
	}
#endif
	
	if (lstrcmp(cmdline, TEXT("/runstartup")) == 0) {
		run_startup = 1;
		return 0;
	}

	if (lstrcmp(cmdline, TEXT("/sysinstall")) == 0) {
		sys_install(1);
		return 1;
	}

	if (lstrcmp(cmdline, TEXT("/sysremove")) == 0) {
		sys_install(0);
		return 1;
	}

	if (lstrcmp(cmdline, TEXT("/install")) == 0) {
		/* install the shell for the current user.
		 * Seems that winlogon borks when the install path has spaces in it */
		TCHAR command_line[1024];
		DWORD len;

		len = GetModuleFileName(instance, command_line, sizeof(command_line)/sizeof(TCHAR));
		command_line[len] = '\0';
		StringCbCat(command_line, sizeof(command_line), TEXT(" /runstartup"));
		
		install_shell(command_line, 1);
		core_message_box(TEXT("WezDesk"), TEXT("Your shell has now been changed to my Evil Shell.\r\nIf this is the first time you have installed the shell, you should reboot your computer now.  Otherwise, you will need to log out and then log back in before you can use the shell."), MB_ICONINFORMATION|MB_OK);
		return 1;
	}

	if (lstrcmp(cmdline, TEXT("/uninstall")) == 0) {
		/* reset to the default shell for the current user */
		install_shell(TEXT("explorer.exe"), 0);
		core_message_box(TEXT("WezDesk"), TEXT("Your shell has now been reset to explorer.\r\nSorry to see you go :-("), MB_ICONINFORMATION|MB_OK);
		return 1;
	}

	if (lstrlen(cmdline)) {
		TCHAR message[2048];
		StringCbPrintf(message, sizeof(message), TEXT("Didn't understand command line args: '%s' (%d chars)"), cmdline, lstrlen(cmdline));
		core_message_box(TEXT("WezDesk"), message, MB_ICONERROR|MB_OK);
		return 1;
	}
	
	return 0;
}

static void load_shell_service_objects(void) {
	HKEY key;
	LONG res;
	int i;
			
	debug_printf(TEXT("loading shell objects\r\n"));

	res = RegOpenKeyEx(HKEY_LOCAL_MACHINE, 
			TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\ShellServiceObjectDelayLoad"),
			0, KEY_READ, &key);

	if (res != ERROR_SUCCESS) {
		debug_printf(TEXT("borked shell services\r\n"));
		return;
	}
	
	i = 0;
	while (1) {
		TCHAR name[64];
		TCHAR value[64];
		DWORD namelen, valuelen;
		CLSID clsid;
		HRESULT hr;
		IOleCommandTarget *cmd;
		
		namelen = sizeof(name) / sizeof(name[0]); /* TCHARS */
		valuelen = sizeof(value);	/* BYTES */

		res = RegEnumValue(key, i++, name, &namelen, 0, NULL, (LPBYTE)value, &valuelen);

		if (res != ERROR_SUCCESS) {
			break;
		}

		debug_printf(TEXT("shell service: clsid %s\r\n"), value);
		
#ifdef UNICODE
		CLSIDFromString(value, &clsid);
#else
		OLECHAR wvalue[64];
		MultiByteToWideChar(CP_ACP, 0, value, valuelen, wvalue, sizeof(wvalue) / sizeof(wvalue[0]));
		CLSIDFromString(wvalue, &clsid);
#endif
		
		hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER,
				IID_IOleCommandTarget, (void **) &cmd);

		if (SUCCEEDED(hr)) {
			cmd->Exec(&CGID_ShellServiceObject,
				2, // start
				0,
				NULL, NULL);
			service_objects.push_back(cmd);	
		}
	}
	RegCloseKey(key);
}

static void run(LPCTSTR what)
{
	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	memset(&si, 0, sizeof(si));

	si.cb = sizeof(si);

	int l = lstrlen(what);
	if (lstrcmp(what + l - 4, TEXT(".lnk")) == 0) {
		core_execute(NULL, NULL, what, NULL, TEXT("\\Windows\\System32"), SW_SHOWNORMAL, 0);
	} else {
#if USE_WINVERIFY_TRUST_ON_STARTUP_ITEMS
		WINTRUST_FILE_INFO fi;
		WINTRUST_DATA wtd;
		HRESULT res;
		GUID prov = WINTRUST_ACTION_GENERIC_VERIFY_V2;
		
		memset(&fi, 0, sizeof(fi));
		fi.cbStruct = sizeof(fi);
		fi.pcwszFilePath = what;

		memset(&wtd, 0, sizeof(wtd));
		wtd.cbStruct = sizeof(wtd);
		wtd.dwUIChoice = WTD_UI_NONE;
		wtd.dwUnionChoice = WTD_CHOICE_FILE;
		wtd.pFile = &fi;

		res = WinVerifyTrust(root_window, &prov, &wtd);

		if (res == TRUST_E_SUBJECT_NOT_TRUSTED) {
			return;
		}
#endif
		HANDLE imp = NULL;
		int i;
		BOOL ok = FALSE;

		for (i = 0; i < safer_matches.size(); i++) {
			struct safer_match m = safer_matches[i];

			if (m.re->match(what) > 0) {
				imp = wezdesk_get_safer_token(m.level);
				if (!imp) return;

				ok = CreateProcessAsUser(imp, NULL,
					(TCHAR*)what, NULL, NULL,
					FALSE, 0, NULL, TEXT("\\Windows\\System32"), &si, &pi);

				CloseHandle(imp);
			}
			break;
		}

		if (!imp) {
			ok = CreateProcess(NULL, (TCHAR*)what, NULL, NULL, FALSE, 0,
				NULL, TEXT("\\Windows\\System32"), &si, &pi);
		}
		
		if (ok) {
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
		} else {
			debug_printf(TEXT("failed to run startup item: %s"), what);
		}
	}
}

static void queue_run(LPTSTR what, LPTSTR source)
{
	struct startup_item item;
	int pri;

	debug_printf(TEXT("q: %s"), what);

	if (!lstrlen(what))
		return;
	
	item.pri = 1;

	PluginIterator iter;

	for (iter = plugins.begin(); iter != plugins.end(); iter++) {
		Plugin p = *iter;
		if (p->plugin->veto_startup_item) {
			pri = (p->plugin->veto_startup_item)(p, &core_funcs, what, source, NULL);
			if (pri == 0) {
				/* vetoed */
				debug_printf(TEXT("vetoed by plugin %s"), p->plugin->name);
				return;
			}
			if (pri > item.pri)
				item.pri = pri;
		}
	}

	int l = lstrlen(what);
	item.what = lstrdup(what);

	startup_items.push_back(item);
	debug_printf(TEXT("added to q\r\n"));
}

static void run_from_csidl(DWORD csidl, LPTSTR src)
{
	TCHAR dir[MAX_PATH], what[MAX_PATH];
	HANDLE h;
	int n;
	WIN32_FIND_DATA fd;

	SHGetFolderPath(NULL, CSIDL_FLAG_CREATE | csidl, NULL, SHGFP_TYPE_CURRENT, dir);
	n = lstrlen(dir);
	StringCbCat(dir, sizeof(dir), TEXT("\\*.*"));

	h = FindFirstFile(dir, &fd);
	if (h != INVALID_HANDLE_VALUE) {
		dir[n+1] = '\0';
		do {
			if ((fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_DIRECTORY))) {
				continue;
			}

			StringCbPrintf(what, sizeof(what), TEXT("%s%s"), dir, fd.cFileName);

			queue_run(what, src);
		} while (FindNextFile(h, &fd));
		FindClose(h);
	}
}

static void run_from_reg(HKEY key, LPCTSTR path, BOOL once, LPTSTR src)
{
	HKEY sub;
	LONG res;

	res = RegOpenKeyEx(key, path, 0, KEY_READ, &sub);

	if (res != ERROR_SUCCESS) {
		/* it's ok if the key doesn't exist; this can and does happen for newly
		 * created accounts */
		debug_printf(TEXT("run_from_reg: fail to open %s [%d]"), path, GetLastError());
		return;
	}

	TCHAR name[MAX_PATH], value[MAX_PATH];
	DWORD n, namesize, valuesize;

	for (n = 0; ; n++) {
		namesize = sizeof(name) / sizeof(name[0]);
		valuesize = sizeof(value);

		res = RegEnumValue(sub, n, name, &namesize, NULL, NULL, (LPBYTE)value, &valuesize);

		if (res != ERROR_SUCCESS)
			break;

		queue_run(value, src);
	}
	RegCloseKey(sub);
}

static DWORD WINAPI run_startup_items(LPVOID param)
{
	run_from_reg(HKEY_LOCAL_MACHINE, TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Run"), FALSE, TEXT("HKLM/Run"));
	run_from_reg(HKEY_LOCAL_MACHINE, TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce"), TRUE, TEXT("HKLM/RunOnce"));
	run_from_reg(HKEY_CURRENT_USER, TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Run"), FALSE, TEXT("HKCU/Run"));
	run_from_csidl(CSIDL_COMMON_STARTUP, TEXT("Common/Startup"));
	run_from_csidl(CSIDL_STARTUP, TEXT("My/Startup"));
	run_from_reg(HKEY_CURRENT_USER, TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce"), TRUE, TEXT("HKCU/RunOnce"));

	debug_printf(TEXT("%d startup items q'd"), startup_items.size());
	if (startup_items.size()) {
		startup_items_timer = SetTimer(root_window, 1, 200, NULL);
	}

	return 0;
}

static void destroy_per_window(void)
{
	int i;
	struct per_window *w;

	for (i = 0; i < sizeof(per_window_hash)/sizeof(per_window_hash[0]); i++) {
		while (per_window_hash[i]) {
			delete_PerWindow(per_window_hash[i]->wnd);
		}
	}
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	MSG msg;
	GdiplusStartupInput	gdiinput;
	ULONG_PTR			gditok;
	DWORD ret;

	__try {

	if (handle_command_line(hInstance)) {
		return 0;
	}

	if (Ok != GdiplusStartup(&gditok, &gdiinput, NULL)) {
		core_message_box(TEXT("WezDesk"), TEXT("GDI+ failed"), MB_ICONERROR|MB_OK);
		return 1;
	}
	
	if (!single_instance_only()) {
		core_message_box(TEXT("WezDesk"), TEXT("Can only run one instance at a time"), MB_ICONERROR|MB_OK);
		return 1;
	}

#ifdef DEBUG_MEM
	_CrtSetDbgFlag (
		_CRTDBG_ALLOC_MEM_DF
		| _CRTDBG_LEAK_CHECK_DF
		| _CRTDBG_CHECK_ALWAYS_DF
// Note: it's a good idea to do a few runs with and without this next
// Flag enabled, as the DELAY_FREE can mask some heap corruption.
//		| _CRTDBG_DELAY_FREE_MEM_DF
		);
#endif

	CoInitialize(NULL);
	
	/* hook up some API functions */
	import_funcs();
	memset(per_window_hash, 0, sizeof(per_window_hash));
	memset(desktops, 0, sizeof(desktops));
	if (!init_shell(hInstance)) {
		goto out;
	}

	{
		DWORD len = GetModuleFileName(NULL, base_dir, sizeof(base_dir));
		base_dir[len] = 0;

		--len;
		while (base_dir[len] != '\\')
			--len;
		base_dir[len] = 0;

		debug_printf(TEXT("plugin dir is %s\r\n"), base_dir);
	}

	/* load language dll */
	{
		TCHAR langdll[MAX_PATH];

		StringCbPrintf(langdll, sizeof(langdll),
#ifdef _WIN64
					TEXT("%s\\64-0409.dll"),
#else
					TEXT("%s\\32-0409.dll"),
#endif
					base_dir);
		i18n_def = LoadLibraryEx(langdll, NULL,
				LOAD_LIBRARY_AS_DATAFILE);

		StringCbPrintf(langdll, sizeof(langdll),
#ifdef _WIN64
			TEXT("%s\\64-%04x.dll"),
#else
			TEXT("%s\\32-%04x.dll"),
#endif
			base_dir, 
			GetUserDefaultUILanguage()
			);
//MessageBox(root_window, langdll, TEXT("uilang"), MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
		i18n_dll = LoadLibraryEx(langdll, NULL,
			LOAD_LIBRARY_AS_DATAFILE);
	}


	init_slit();
	init_context_menu();
	init_tray();

	InitializeCriticalSection(&callback_mutex);
	plugin_timer = SetTimer(root_window, 2, 2000, NULL);

	/* load native hooks */
	{
		TCHAR me[MAX_PATH];

		StringCbPrintf(me, sizeof(me),
#ifdef _WIN64
			TEXT("%s\\wdmenu64.dll"),
#else
			TEXT("%s\\wdmenu32.dll"),
#endif
			base_dir);

		hook_dll_instance = LoadLibrary(me);
		if (hook_dll_instance) {
			enable_hook_dll = (void(*)(HINSTANCE, HWND, BOOL))
				GetProcAddress(hook_dll_instance, "hook");

			if (enable_hook_dll) {
				enable_hook_dll(hook_dll_instance, root_window, 1);
			}
		}
	}

#ifdef _WIN64
	 {
	/* We want our menu hooks to show up in 32-bit processes
	 * too, so start our 32-bit helper process that will inject
	 * them.
	 * That process is at base_dir/32/wezdesk-hh32.exe
	 */
	 TCHAR proc[MAX_PATH];
	 StringCbPrintf(proc, sizeof(proc), TEXT("%s\\32bit\\wezdesk-hh32.exe"),
	 	base_dir);
	 STARTUPINFO si;
	 memset(&si, 0, sizeof(si));
	 si.cb = sizeof(si);
	 PROCESS_INFORMATION pi;
	 memset(&pi, 0, sizeof(pi));
	 if (CreateProcess(proc, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	 }
	 }
#endif

	load_shell_service_objects();
	
	if (run_startup) {
		run_startup_items(NULL);
	}
	
	PostMessage(HWND_BROADCAST, RegisterWindowMessage(TEXT("TaskbarCreated")), 0, 0);
	schedule_plugin_callbacks();

	/* an alertable message pump */
	do {
		msg.message = WM_TIMER;
		ret = MsgWaitForMultipleObjectsEx(0, NULL,
			INFINITE,
			QS_ALLEVENTS|QS_ALLINPUT|QS_ALLPOSTMESSAGE|
			QS_MOUSE|QS_SENDMESSAGE|QS_TIMER,
			MWMO_INPUTAVAILABLE|MWMO_ALERTABLE);
		if (ret == WAIT_OBJECT_0) {
			while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
				if (msg.message == WM_QUIT) break;
			}
		}
	} while (msg.message != WM_QUIT);

out:
	if (enable_hook_dll) {
		enable_hook_dll(hook_dll_instance, 0, 0);
	}

#ifdef DEBUG_MEM
	_CrtSetDbgFlag (
		_CRTDBG_ALLOC_MEM_DF
		| _CRTDBG_LEAK_CHECK_DF
//		| _CRTDBG_DELAY_FREE_MEM_DF
		);
#endif

	/* drain */
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	
	for (int i = 0; i < service_objects.size(); i++) {
		service_objects[i]->Exec(&CGID_ShellServiceObject,
				3, // stop
				0,
				NULL, NULL);

		service_objects[i]->Release();
	}

	/* drain */
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	
	/* unload in reverse order */
	PluginIterator iter;
	for (int i = plugins.size() - 1; i >= 0; i--) {
		Plugin p = plugins[i];
		if (p->plugin->unload) {
			(p->plugin->unload)(p, &core_funcs, 1);
		}
		p->plugin = NULL;
		FreeLibrary(p->module);
		p->module = NULL;
	}

	/* drain */
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	
	DeregisterShellHookWindow(root_window);
	if (the_shell_hook) UnhookWindowsHookEx(the_shell_hook);
	EnumWindows(cleanup_windows_callback, 0);
	fini_tray();
	
	/* drain */
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	
	unregister_hotkeys();
	destroy_slits();
	/* drain */
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	DestroyWindow(root_window);
	contextmenu_shutdown();
	destroy_per_window();
	/* drain */
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	if (single_instance_mtx) {
		CloseHandle(single_instance_mtx);
	}

	if (need_to_respawn_shell) {
		/* launch ourselves anew */
		Sleep(2000);
		do_respawn_shell();
	}

	/* unload in reverse order */
	for (int i = plugins.size() - 1; i >= 0; i--) {
		Plugin p = plugins[i];
		delete_config_value(p->config);
		vector <plugin_func>::iterator pf;
		for (pf = p->funcs.begin(); pf != p->funcs.end(); pf++) {
			free(pf->name);
		}
		free(p->name);
		delete p;
	}

	CoUninitialize();
	GdiplusShutdown(gditok);

	delete_config_value(core_config);

#ifdef DEBUG_MEM
	DebugBreak();
#endif
	return msg.wParam;

	}
	__except (crash_dumper(GetExceptionInformation()))
	{
	}
	return 1;
}

