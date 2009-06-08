/* Copyright (c) 2004-2009 Wez Furlong.
 * This source is provided under terms of the GPLv2.
 * See the file named LICENSE for full details */
#include "wezdesk.h"
#include <Wtsapi32.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <vector>
#include <algorithm>
#include <map>
#include "wezdeskres.h"

using namespace std;
static TCHAR *resolve_constant(TCHAR *name);

enum {
	/* NOTE: numbering must match the WEZDESK_MENU_XXX options in wezdeskapi.h */
	AT_NONE = 0,
	AT_SEPARATOR,
	AT_FUNC,
	AT_SCAN_DIR,
	AT_SCAN_DIR_SUB,	/* generated at run time */
	AT_SUB_MENU,
	AT_EXECUTE_FILE,
	AT_EXPLORER,
	AT_RUNAS,
	AT_SUB_MENU_PLUGIN,
	AT_SUBMENU_BY_NAME,
	AT_FUNC_BY_NAME,
	AT_PLUGIN_SUBMENU
};
enum {
	STO_SUSPEND,
	STO_HIBERNATE,
	STO_SHUTDOWN,
	STO_REBOOT,
	STO_LOGOFF,
	STO_DISCONNECT
};

class SlitAction;
class ResourcePool;

static HMENU realize_menu_by_name(TCHAR *menu_name, ResourcePool *pool);
HIMAGELIST big_icons, small_icons;
typedef TCHAR dir_component[MAX_PATH];
typedef vector<TCHAR*> dirvector;
struct menu_def {
	TCHAR *name;
	SlitAction *root;
};
typedef vector<menu_def> menu_definitions;
static TCHAR *resolve_constant(TCHAR *name);
static void cleanup_res_labels(void);
static menu_definitions menu_defs;

class ResourcePool {
private:
	struct destructor {
		void (*func)(void *);
		void *thing;
	};
	vector <destructor> tokill;
	int ref;

public:
	~ResourcePool() {
		vector <destructor>::iterator i;
		for (i = tokill.begin(); i != tokill.end(); i++) {
			i->func(i->thing);
		}
	}
	ResourcePool() {
		ref = 1;
	}
	
	void add(void (*func)(void*), void *arg) {
		struct destructor d;
		d.func = func;
		d.thing = arg;

		/* sanity check */
		vector <destructor>::iterator i;
		for (i = tokill.begin(); i != tokill.end(); i++) {
			destructor c = *i;
			if (!memcmp(&c, &d, sizeof(c))) {
				abort();
			}
		}

		tokill.push_back(d);
	}

	void AddRef(void) {
		ref++;
	}
	void Release(void) {
		if (--ref == 0) {
			delete this;
		}
	}

	TCHAR *dup(const TCHAR *src) {
		TCHAR *n = lstrdup(src);
		add(free, n);
		return n;
	}

	void *alloc(unsigned long size) {
		void *p = malloc(size);
		add(free, p);
		return p;
	}
};

void contextmenu_delete_pool(void *_pool)
{
	ResourcePool *pool = (ResourcePool*)_pool;
	delete pool;
}

class SlitAction {
public:
	UINT action_type;
	TCHAR *caption;
	void *param;
	void *param2;
	union {
		struct {
			TCHAR *a, *b;
		} merge_dirs;
		TCHAR *name;
		struct {
			TCHAR *verb;
			TCHAR *exe;
			TCHAR *args;
		} exec;
		struct {
			TCHAR *name;
			TCHAR *arg;
		} func;
	} u;
	HMENU menu;
	struct item_track {
		class SlitAction *ptr;
		const char *file;
		int line;
	};
	vector <struct item_track> sub_items;
	int cloned;
	ResourcePool *pool;

	SlitAction() {
		caption = NULL;
		memset(&u, 0, sizeof(u));
		menu = NULL;
		cloned = 0;
		pool = 0;
	}
	~SlitAction() {
		SlitAction *kid;

		if (!pool) {
			switch (action_type) {
				case AT_EXECUTE_FILE:
					if (u.exec.exe) free(u.exec.exe);
					if (u.exec.args) free(u.exec.args);
					if (u.exec.verb) free(u.exec.verb);
					break;
				case AT_FUNC_BY_NAME:
					if (u.func.arg) free(u.func.arg);
					if (u.func.name) free(u.func.name);
					break;
				case AT_SUBMENU_BY_NAME:
					if (u.name) free(u.name);
					break;
				case AT_SCAN_DIR:
					if (u.merge_dirs.a) free(u.merge_dirs.a);
					if (u.merge_dirs.b) free(u.merge_dirs.b);
					break;
			}
			memset(&u, 0, sizeof(u));

			if (caption) free(caption);

			if (menu) {
				while (GetMenuItemCount(menu)) {
					MENUITEMINFO mi;
					memset(&mi, 0, sizeof(mi));
					mi.cbSize = sizeof(mi);
					mi.fMask = MIM_MENUDATA;
					if (GetMenuItemInfo(menu, 0, TRUE, &mi)) {
						kid = (SlitAction*)mi.dwItemData;
						if (kid != this) {
							delete kid;
						}
					}
					DeleteMenu(menu, 0, MF_BYPOSITION);
				}
				DestroyMenu(menu);
			}

			if (!cloned) {
				vector <struct item_track>::iterator j;
				for (j = sub_items.begin(); j != sub_items.end(); j++) {
					kid = j->ptr;
					if (kid != this) {
						delete kid;
					}
				}
			}
		}
		sub_items.clear();
	}

	void Add(SlitAction *kid, const char *file, int line) {
		struct item_track t;
		t.ptr = kid;
		t.file = file;
		t.line = line;
		sub_items.push_back(t);
	}

	void Execute(HWND hwnd) {
		debug_printf(TEXT("--- action type is (item=%p) %d\r\n"),
			this, action_type);
		switch (action_type) {
			case AT_EXECUTE_FILE:
				core_funcs.Execute(GetDesktopWindow(),
						u.exec.verb, u.exec.exe, u.exec.args,
						resolve_constant(TEXT("$CSIDL_PERSONAL")), SW_SHOWNORMAL, 1);
				break;

			case AT_RUNAS:
				core_funcs.Execute(GetDesktopWindow(), TEXT("runas"),
						(TCHAR*)param, 
						param2 ? (TCHAR*)param2 : TEXT(""),
						resolve_constant(TEXT("$CSIDL_PERSONAL")), SW_SHOWNORMAL, 1);
				break;


			case AT_EXPLORER:
				core_funcs.Execute(GetDesktopWindow(), NULL,
						TEXT("explorer.exe"),
						(TCHAR*)param, resolve_constant(TEXT("$CSIDL_PERSONAL")),
						SW_SHOWNORMAL, 1);
				break;

			case AT_FUNC_BY_NAME:
				run_func_by_name(u.func.name, u.func.arg, hwnd);
				break;

			case AT_FUNC:
				{
					void (*func)(void*,HWND) = (void (*)(void*,HWND))param;
					func(param2, hwnd);
				}
				break;
		}
	}

	SlitAction *clone(void) {
		SlitAction *c = new SlitAction;
		*c = *this;
		c->cloned = 1;
		c->pool = NULL;
		return c;
	}
};

static void delete_item(void *_item)
{
	/* note: if this routine appears before the declaration of the dtor
	 * for SlitAction, delete won't call it! */
	SlitAction *item = (SlitAction*)_item;
	delete item;
}

void contextmenu_shutdown(void)
{
	{
		vector<menu_def>::iterator i;
		for (i = menu_defs.begin(); i != menu_defs.end(); i++) {
			free(i->name);
			delete i->root;
		}
		menu_defs.clear();
	}
	{
		vector<safer_match>::iterator i;
		for (i = safer_matches.begin(); i != safer_matches.end(); i++) {
			delete i->re;
		}
		safer_matches.clear();
	}
	{
		vector<window_match>::iterator i;
		for (i = window_matches.begin(); i != window_matches.end(); i++) {
			delete i->re;
			free(i->match_type);
			free(i->propname);
			free(i->funcname);
			free(i->funcarg);
		}
		window_matches.clear();
	}
	cleanup_res_labels();
}

void context_menu_register_plugin_submenu(Plugin plugin, TCHAR *name, 
		wezdesk_popup_callback_func cb)
{
	menu_def mdef;
	SlitAction *root = new SlitAction;

	mdef.name = lstrdup(name);
	root->action_type = AT_PLUGIN_SUBMENU;
	root->param = plugin;
	root->param2 = cb;
	mdef.root = root;

	menu_defs.push_back(mdef);
}

SlitAction *is_plugin_menu(TCHAR *menu_name)
{
	vector<menu_def>::iterator i;

	for (i = menu_defs.begin(); i != menu_defs.end(); i++) {
		if (!lstrcmp(menu_name, i->name)) {
			if (i->root->action_type == AT_PLUGIN_SUBMENU) {
				return i->root;
			}
			return NULL;
		}
	}
	return NULL;
}

static void shutdown_the_os(int how, const TCHAR *prompt)
{
	if (lstrlen(prompt) && IDNO == MessageBox(core_funcs.GetRootWindow(), 
			prompt, prompt,
			MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND | MB_TOPMOST)) {
		return;
	}

	if (how == STO_LOGOFF) {
		ExitWindowsEx(EWX_LOGOFF, 0);
		return;
	}

	if (how == STO_DISCONNECT) {
		WTSDisconnectSession(WTS_CURRENT_SERVER_HANDLE,
			WTS_CURRENT_SESSION,
			TRUE);
		return;
	}

	HANDLE tok;
	TOKEN_PRIVILEGES tkp;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) {
		return;
	}

	LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
	tkp.PrivilegeCount = 1;
	tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	AdjustTokenPrivileges(tok, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0);

	switch (how) {
		case STO_HIBERNATE:
			SetSystemPowerState(FALSE, FALSE);
			break;

		case STO_SUSPEND:
			SetSystemPowerState(TRUE, FALSE);
			break;

		case STO_SHUTDOWN:
			ExitWindowsEx(EWX_SHUTDOWN | EWX_POWEROFF, 0);
			break;

		case STO_REBOOT:
			ExitWindowsEx(EWX_REBOOT, 0);
			break;
	}

}

static void do_suspend(void* prompt, HWND wnd) {
	shutdown_the_os(STO_SUSPEND, (TCHAR*)prompt);
}

static void do_hibernate(void* prompt, HWND wnd) {
	shutdown_the_os(STO_HIBERNATE, (TCHAR*)prompt);
}

static void do_shutdown(void* prompt, HWND wnd)	{
	shutdown_the_os(STO_SHUTDOWN, (TCHAR*)prompt);
}

static void do_reboot(void* prompt, HWND wnd) {
	shutdown_the_os(STO_REBOOT, (TCHAR*)prompt);
}

static void do_logoff(void* prompt, HWND wnd) {
	shutdown_the_os(STO_LOGOFF, (TCHAR*)prompt);
}

static void do_disconnect(void* prompt, HWND wnd) {
	shutdown_the_os(STO_DISCONNECT, (TCHAR*)prompt);
}

static void do_quit(void* prompt, HWND wnd)		{ 
	if (IDNO == MessageBox(core_funcs.GetRootWindow(), 
			(TCHAR*)prompt, (TCHAR*)prompt,
			MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND | MB_TOPMOST)) {
		return;
	}
	
	PostQuitMessage(0);
}

static void do_restart_shell(void *prompt, HWND wnd) {
	if (IDNO == MessageBox(core_funcs.GetRootWindow(), 
			(TCHAR*)prompt, (TCHAR*)prompt,
			MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND | MB_TOPMOST)) {
		return;
	}
	need_to_respawn_shell = TRUE;
	PostQuitMessage(0);
}

static void do_set_current_workspace(void *arg, HWND wnd)
{
	TCHAR *which = (TCHAR*)arg;
	int desk = _wtoi(which);

	core_funcs.SwitchDesktop(desk);
}

static void do_set_workspace(void *arg, HWND wnd)
{
	TCHAR *which = (TCHAR*)arg;
	DWORD bits;
	int desk;

	if (!lstrcmp(which, TEXT("*"))) {
		bits = 0xffffffff;
	} else {
		desk = _wtoi(which);
		bits = 1 << (desk - 1);
	}
	
	PerWindow *w = get_PerWindow(active_workspace_context_window, core_funcs.GetActiveDesktop());
	if (w) {
		w->desktop_bits = w->not_minimized = bits;

		if (lstrcmp(which, TEXT("*"))) {
			core_funcs.SwitchDesktop(desk);
			ShowWindowAsync(active_workspace_context_window, SW_SHOW);
		}
	}

}

static void do_set_transparent(void *arg, HWND slit)
{
	TCHAR *which = (TCHAR*)arg;

	if (!lstrcmp(which, TEXT("toggle"))) {
		if ((GetWindowLong(active_workspace_context_window, GWL_EXSTYLE) & WS_EX_LAYERED) == WS_EX_LAYERED) {
			SetWindowLong(active_workspace_context_window, GWL_EXSTYLE, GetWindowLong(active_workspace_context_window, GWL_EXSTYLE) &~ WS_EX_LAYERED);
		} else {
			SetWindowLong(active_workspace_context_window, GWL_EXSTYLE, GetWindowLong(active_workspace_context_window, GWL_EXSTYLE)|WS_EX_LAYERED|WS_EX_COMPOSITED);
			SetLayeredWindowAttributes(active_workspace_context_window, 0, (255 * 80)/100, LWA_ALPHA);
		}
	} else {
		int amount = _wtoi(which);

		if (amount) {
			SetWindowLong(active_workspace_context_window, GWL_EXSTYLE, GetWindowLong(active_workspace_context_window, GWL_EXSTYLE)|WS_EX_LAYERED|WS_EX_COMPOSITED);
			SetLayeredWindowAttributes(active_workspace_context_window, 0, (255 * amount)/100, LWA_ALPHA);
		} else {
			if ((GetWindowLong(active_workspace_context_window, GWL_EXSTYLE) & WS_EX_LAYERED) == WS_EX_LAYERED) {
				SetWindowLong(active_workspace_context_window, GWL_EXSTYLE, GetWindowLong(active_workspace_context_window, GWL_EXSTYLE) &~ WS_EX_LAYERED);
			}
		}
	}
}

static void do_run_dialog(void *arg, HWND slit)
{
	core_funcs.RunDlg(NULL, NULL, NULL, NULL, NULL, 0);
}

static void do_show_menu(void *arg, HWND slit)
{
	TCHAR *menu_name = (TCHAR*)arg;
	DWORD pos = GetMessagePos();

	popup_context_menu(LOWORD(pos), HIWORD(pos), core_funcs.GetRootWindow(), menu_name);
}

//HOTKEY WIN F Func "flash-next-window"
static void do_flash_window(void *arg, HWND slit)
{
	FLASHWINFO fw;
	memset(&fw, 0, sizeof(fw));
	fw.cbSize = sizeof(fw);
	fw.hwnd = GetNextWindow(active_workspace_context_window, GW_HWNDNEXT);
	fw.dwFlags = FLASHW_ALL;
	fw.uCount = 4;
	FlashWindowEx(&fw);
	//FlashWindow(GetNextWindow(active_workspace_context_window, GW_HWNDNEXT), TRUE);
}

static void do_crash(void *arg, HWND slit)
{
	debug_printf(TEXT("crash function called\n"));
	*(char*)0 = 4;
	ExitProcess(255);
}

static const struct {
	const TCHAR *funcname;
	void (*function)(void *arg, HWND wnd);
} context_menu_funcs[] = {
	{ TEXT("suspend"),		do_suspend },
	{ TEXT("hibernate"),	do_hibernate },
	{ TEXT("shutdown"),		do_shutdown },
	{ TEXT("reboot"),		do_reboot },
	{ TEXT("logoff"),		do_logoff },
	{ TEXT("wts-disconnect"),		do_disconnect },
	{ TEXT("quit"),			do_quit },
	{ TEXT("restart-shell"),do_restart_shell },
	{ TEXT("set-window-workspace"), do_set_workspace },
	{ TEXT("set-window-transparent"), do_set_transparent },
	{ TEXT("set-current-workspace"), do_set_current_workspace },
	{ TEXT("shell-run-dialog"), do_run_dialog },
	{ TEXT("show-context-menu"), do_show_menu },
	{ TEXT("flash-next-window"), do_flash_window },
	{ TEXT("change-slit-alignment"), do_slit_align },
	{ TEXT("change-slit-gravity"), do_slit_gravity },
	{ TEXT("set-slit-autohide"), do_slit_autohide },
	{ TEXT("set-slit-floating"), do_slit_float },
	{ TEXT("debug-crash"), do_crash },
	{ NULL, NULL }
};


static HMENU CreateNotifyByPositionPopupMenu(SlitAction *action) {
	HMENU menu = CreatePopupMenu();

	MENUINFO info;

	memset(&info, 0, sizeof(info));
	info.cbSize = sizeof(info);
	info.fMask = MIM_STYLE|MIM_MENUDATA;
	info.dwStyle = MNS_NOTIFYBYPOS;
	info.dwMenuData = (ULONG_PTR)action;
	SetMenuInfo(menu, &info);
	action->menu = menu;
	
	return menu;
}

static SlitAction *menu_item_from_HMENU(HMENU menu)
{
	MENUINFO mi;
	memset(&mi, 0, sizeof(mi));
	mi.cbSize = sizeof(mi);
	mi.fMask = MIM_MENUDATA;
	if (GetMenuInfo(menu, &mi)) {
		return (SlitAction*)mi.dwMenuData;
	}
	return NULL;
}

void slit_add_per_plugin_sub_menu(HMENU menu, LPCWSTR caption, Plugin owner)
{
	MENUITEMINFO info;
	SlitAction *def;

	SlitAction *pdef = menu_item_from_HMENU(menu);

	def = new SlitAction;
	def->param = owner;
	def->pool= pdef->pool;
	def->action_type = AT_SUB_MENU_PLUGIN;
	def->pool->add(delete_item, def);

	memset(&info, 0, sizeof(info));
	info.cbSize = sizeof(info);
	info.fMask = MIIM_DATA | MIIM_ID | MIIM_STRING | MIIM_FTYPE | MIIM_SUBMENU;
	info.fType = MFT_STRING;
	info.wID = GetMenuItemCount(menu);
	info.dwItemData = (UINT_PTR)def;
	info.dwTypeData = (LPWSTR)caption;

	info.hSubMenu = CreateNotifyByPositionPopupMenu(def);
	info.fMask |= MIIM_SUBMENU;

	InsertMenuItem(menu, GetMenuItemCount(menu), TRUE, &info);
}

void slit_add_callback_menu_item(HMENU menu, LPCWSTR caption,
		void (*cbfunc)(void*), void *arg, void (*dtorfunc)(void*))
{
	MENUITEMINFO info;
	SlitAction *def, *pdef;

	pdef = menu_item_from_HMENU(menu);

	def = new SlitAction;
	def->pool = pdef->pool;
	
	def->param = cbfunc;
	def->param2 = arg;
	def->action_type = AT_FUNC;

	memset(&info, 0, sizeof(info));
	info.cbSize = sizeof(info);
	info.fMask = MIIM_DATA | MIIM_ID | MIIM_STRING | MIIM_FTYPE;
	info.fType = MFT_STRING;
	info.wID = GetMenuItemCount(menu);
	info.dwItemData = (UINT_PTR)def;
	info.dwTypeData = (LPWSTR)caption;

	InsertMenuItem(menu, GetMenuItemCount(menu), TRUE, &info);

	if (dtorfunc) {
		def->pool->add(dtorfunc, arg);
	}
	def->pool->add(delete_item, def);
}

HMENU build_menu(SlitAction *root) {
	HMENU menu;
	vector <SlitAction::item_track>::iterator i;
	SlitAction *def;

	menu = CreateNotifyByPositionPopupMenu(root);

	if (root->action_type == AT_PLUGIN_SUBMENU) {
		return menu;
	}

	for (i = root->sub_items.begin(); i != root->sub_items.end(); i++) {
		MENUITEMINFO info;

		def = i->ptr->clone();
		def->pool = root->pool;
		def->pool->add(delete_item, def);

		memset(&info, 0, sizeof(info));
		info.cbSize = sizeof(info);
		info.fMask = MIIM_DATA | MIIM_ID | MIIM_STRING | MIIM_FTYPE;
		info.fType = MFT_STRING;
		info.wID = GetMenuItemCount(menu);
		info.dwItemData = (UINT_PTR)def;
		info.dwTypeData = def->caption;

		switch (def->action_type) {
			case AT_SUB_MENU:
			{
				SlitAction *sm = ((SlitAction*)def->param)->clone();
				sm->pool = def->pool;
				sm->pool->add(delete_item, sm);
				info.hSubMenu = build_menu(sm);
				info.fMask |= MIIM_SUBMENU;
				break;
			}

			case AT_SUBMENU_BY_NAME:
			{
				SlitAction *kid;
				info.hSubMenu = realize_menu_by_name(def->u.name, def->pool);
				info.fMask |= MIIM_SUBMENU;

				break;
			}

			case AT_SEPARATOR:
				info.fType = MFT_SEPARATOR;
				break;

			case AT_SCAN_DIR:
				info.hSubMenu = CreateNotifyByPositionPopupMenu(def);
				info.fMask |= MIIM_SUBMENU;
				break;

			case AT_FUNC:
			case AT_FUNC_BY_NAME:
				break;
		}

		if (!InsertMenuItem(menu, GetMenuItemCount(menu), TRUE, &info))
			break;
	}

	return menu;
}

static void inner_fill(dir_component root, dirvector *dirs, dirvector *files)
{
	HANDLE h;
	WIN32_FIND_DATA fd;
	TCHAR *item = NULL;
	dir_component root_with_pat;

	ExpandEnvironmentStrings(root, root_with_pat,
		sizeof(root_with_pat)/sizeof(root_with_pat[0]));
	StringCbCat(root_with_pat, sizeof(root_with_pat), TEXT("\\*.*"));

	h = FindFirstFile(root_with_pat, &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) 
					== FILE_ATTRIBUTE_HIDDEN) {
				continue;
			}
				
			dirvector::iterator i;
			bool can_add = true;

			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
					== FILE_ATTRIBUTE_DIRECTORY) {

				if (!lstrcmp(TEXT("."), fd.cFileName) ||
						!lstrcmp(TEXT(".."), fd.cFileName))
					continue;

				for (i = dirs->begin(); i != dirs->end(); i++) {
					item = *i;
					if (!lstrcmp(item, fd.cFileName)) {
						can_add = false;
						break;
					}
				}

				if (can_add) {
					item = lstrdup(fd.cFileName);
					dirs->push_back(item);
				}
			} else {
				/* we want the full path in there */
				TCHAR *bs;

				for (i = files->begin(); i != files->end(); i++) {
					item = *i;

					bs = item + lstrlen(item) - 1;
					while (*bs != '\\')
						--bs;
					bs++;
					
					if (!lstrcmp(bs, fd.cFileName)) {
						can_add = false;
						break;
					}
				}

				int item_size = sizeof(TCHAR) * (lstrlen(fd.cFileName)
						+ 2 + lstrlen(root));
				item = (TCHAR*)malloc(item_size);
				StringCbPrintf(item, item_size, TEXT("%s\\%s"),
					root, fd.cFileName);
				files->push_back(item);
			}

		} while (FindNextFile(h, &fd));
		FindClose(h);
	}
}

static void empty_menu(void *_menu)
{
	HMENU menu = (HMENU)_menu;

	while (GetMenuItemCount(menu)) {
		DeleteMenu(menu, 0, MF_BYPOSITION);
	}
}

static bool order_by_name(TCHAR *a, TCHAR *b)
{
	return lstrcmp(a, b) < 0;
}

static void fill_menu_from_dir(SlitAction *item, HMENU menu)
{
	SlitAction *pdef;
	
	/* only need to fill if the item is empty */
	if (GetMenuItemCount(menu)) {
		debug_printf(TEXT("fill_menu_from_dir: already populated\r\n"));
		return;
	}

	/* directory names are only inserted once */
	/* but files are fair game */
	dirvector dirs, files;
	
	dir_component path;

	inner_fill(item->u.merge_dirs.a, &dirs, &files);
	inner_fill(item->u.merge_dirs.b, &dirs, &files);
	sort(dirs.begin(), dirs.end(), order_by_name);
	sort(files.begin(), files.end(), order_by_name);

	dirvector::iterator i;
	int id = 0;
	TCHAR *dir_item;
	MENUITEMINFO info;
	SlitAction *kid_item;

	for (i = dirs.begin(); i < dirs.end(); i++, id++) {
		dir_item = *i;

		TCHAR *sub;

		debug_printf(TEXT("dir: %s\r\n"), dir_item);
		
		kid_item = new SlitAction;
		kid_item->pool = item->pool;
		item->pool->add(delete_item, kid_item);
		kid_item->action_type = AT_SCAN_DIR_SUB;
		
		int sub_size = sizeof(TCHAR) * (2 + lstrlen(item->u.merge_dirs.a) + lstrlen(dir_item));
		sub = (TCHAR*)kid_item->pool->alloc(sub_size);
		StringCbPrintf(sub, sub_size, TEXT("%s\\%s"),
			item->u.merge_dirs.a, dir_item);
		kid_item->u.merge_dirs.a = sub;

		sub_size = sizeof(TCHAR) * (2 + lstrlen(item->u.merge_dirs.b) + lstrlen(dir_item));
		sub = (TCHAR*)kid_item->pool->alloc(sub_size);
		StringCbCopy(sub, sub_size, item->u.merge_dirs.b);
		StringCbCat(sub, sub_size, TEXT("\\"));
		StringCbCat(sub, sub_size, dir_item);
		kid_item->u.merge_dirs.b = sub;

		memset(&info, 0, sizeof(info));
		info.cbSize = sizeof(info);
		
		info.fMask = MIIM_DATA | MIIM_ID | MIIM_STRING | MIIM_FTYPE | MIIM_SUBMENU;
		info.fType = MFT_STRING;
		info.hSubMenu = CreateNotifyByPositionPopupMenu(kid_item);
		info.wID = id;
		info.dwItemData = (UINT_PTR)kid_item;
		info.dwTypeData = dir_item;

		InsertMenuItem(menu, id, TRUE, &info);
	
		free(dir_item);
	}

	for (i = files.begin(); i < files.end(); i++, id++) {
		dir_item = *i;

		dir_component caption;
		TCHAR *bs;


		kid_item = new SlitAction;
		kid_item->pool = item->pool;
		item->pool->add(delete_item, kid_item);
		
		debug_printf(TEXT("file: %s [kid_item=%p]\r\n"), dir_item, kid_item);

		kid_item->action_type = AT_EXECUTE_FILE;
		kid_item->u.exec.verb = NULL;
		kid_item->u.exec.exe = dir_item;
		kid_item->u.exec.args = item->pool->dup(TEXT(""));
		item->pool->add(free, dir_item);

		SHFILEINFO sinfo;
		memset(&sinfo, 0, sizeof(sinfo));
		SHGetFileInfo(dir_item, 0, &sinfo, sizeof(sinfo), SHGFI_DISPLAYNAME);
		
		memset(&info, 0, sizeof(info));
		info.cbSize = sizeof(info);
		info.fMask = MIIM_DATA | MIIM_ID | MIIM_STRING | MIIM_FTYPE;
		info.fType = MFT_STRING;// | MFT_OWNERDRAW;
		info.wID = id;
		info.dwItemData = (UINT_PTR)kid_item;
		
		kid_item->caption = NULL;//lstrdup(caption);
		
		info.dwTypeData = sinfo.szDisplayName;

		InsertMenuItem(menu, id, TRUE, &info);
	}
}

int run_func_by_name(TCHAR *name, TCHAR *arg, HWND wnd)
{;
	int i;
	for (i = 0; context_menu_funcs[i].function; i++) {
		if (!lstrcmp(context_menu_funcs[i].funcname, name)) {
			context_menu_funcs[i].function(arg, wnd);
			return 1;
		}
	}

	/* try the plugin defined functions then */
	PluginIterator iter;

	for (iter = plugins.begin(); iter != plugins.end(); iter++) {
		Plugin p = *iter;
		for (i = 0; i < p->funcs.size(); i++) {
			if (!lstrcmp(name, p->funcs[i].name)) {
				p->funcs[i].func(arg, wnd);
				return 1;
			}
		}
	}

	return 0;
}

static void execute_item(void *_item, HWND wnd)
{
	SlitAction *item = (SlitAction*)_item;
	item->Execute(wnd);
}

/* This crazy piece of engineering is designed to force the input to switch to us when
 * our desktop hook catches a right mouse click.  A simple SetForegroundWindow() in the
 * context of the hook is not sufficient to give our app the focus, and without it, the
 * context menu is not very easily dismissed.
 *
 * Rather than hard-code the hotkey into the menu DLL, we look for it in our list of
 * registered hotkeys and then synthesize that input, so the right click is as though
 * the user pressed the hotkey.
 */
static int each_hotkey(hotkey_func_t func, void *hotkeyarg, UINT mod, UINT vk, void *cookie)
{
	if (func == execute_item) {
		SlitAction *item = (SlitAction*)hotkeyarg;
		if (item && item->action_type == AT_FUNC_BY_NAME &&
				!lstrcmp(TEXT("show-context-menu"), item->u.func.name) &&
				item->u.func.arg &&
				!lstrcmp(TEXT("root"), item->u.func.arg)) {

			INPUT hotkey[4];

//			core_funcs.Trace(TEXT("Going to synthesize hotkey %d,%d for root menu\n"), mod, vk);

			memset(hotkey, 0, sizeof(hotkey));
			hotkey[0].type = INPUT_KEYBOARD;
			switch (mod) {
				case MOD_WIN:
					hotkey[0].ki.wVk = VK_LWIN;
					hotkey[3].ki.wVk = VK_LWIN;
					break;
				case MOD_ALT:
				case MOD_CONTROL:
				case MOD_SHIFT:
					return 0;
			}

			hotkey[1].type = INPUT_KEYBOARD;
			hotkey[1].ki.wVk = vk;

			hotkey[2].type = INPUT_KEYBOARD;
			hotkey[2].ki.wVk = vk;
			hotkey[2].ki.dwFlags = KEYEVENTF_KEYUP;

			hotkey[3].type = INPUT_KEYBOARD;
			hotkey[3].ki.dwFlags = KEYEVENTF_KEYUP;

			SendInput(sizeof(hotkey)/sizeof(hotkey[0]),
				hotkey, sizeof(hotkey[0]));

			return 1;	
		}
	}
	return 0;
}

int fake_root_context_hotkey(void)
{
	return enum_hotkeys(each_hotkey, NULL);
}

void contextmenu_delete_item(void *_item, HWND wnd)
{
	SlitAction *item = (SlitAction*)_item;
	delete item;
}

LRESULT CALLBACK context_menu_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {
		case WM_MENUCOMMAND:
			{
				MENUITEMINFO info;

				debug_printf(TEXT("MENUCOMMAND!\r\n"));

				info.cbSize = sizeof(info);
				info.fMask = MIIM_DATA;

				if (GetMenuItemInfo((HMENU)lParam, wParam, TRUE, &info)) {
					SlitAction *item = (SlitAction*)info.dwItemData;
					item->Execute(hWnd);
				}
			}
			break;

		case WM_MEASUREITEM:
			{
				LPMEASUREITEMSTRUCT measure = (LPMEASUREITEMSTRUCT)lParam;
				SlitAction *item = (SlitAction*)measure->itemData;
				SIZE size;
				
				HDC dc = GetDC(hWnd);
				
				GetTextExtentPoint32(dc, item->caption, lstrlen(item->caption), &size);

				measure->itemWidth = size.cx;
				measure->itemHeight = size.cy;

				ReleaseDC(hWnd, dc);
	
				return TRUE;
				
			}

		case WM_DRAWITEM:
			{
				LPDRAWITEMSTRUCT di = (LPDRAWITEMSTRUCT)lParam;
				SlitAction *item = (SlitAction*)di->itemData;
				COLORREF txtcol, bkcol;
				int idx;
			
				if (di->itemState & ODS_SELECTED) {
					txtcol = SetTextColor(di->hDC, GetSysColor(COLOR_HIGHLIGHTTEXT));
					bkcol = SetBkColor(di->hDC, GetSysColor(COLOR_HIGHLIGHT));
				} else {
					txtcol = SetTextColor(di->hDC, GetSysColor(COLOR_MENUTEXT));
					bkcol = SetBkColor(di->hDC, GetSysColor(COLOR_MENU));
				}

				idx = Shell_GetCachedImageIndex((TCHAR*)item->param, 0, 0);
				idx = 0;
				if (idx >= 0) {
					ImageList_DrawEx(small_icons, idx, di->hDC, di->rcItem.left, di->rcItem.top, 0, 0,
						CLR_NONE, CLR_NONE,
						ILD_TRANSPARENT | ((di->itemState & ODS_SELECTED) ? ILD_SELECTED : 0));
				}
				ExtTextOut(di->hDC,
					GetSystemMetrics(SM_CXMENUCHECK) + di->rcItem.left,
					di->rcItem.top,
					ETO_OPAQUE,
					&di->rcItem, item->caption, lstrlen(item->caption), NULL);
				

				SetTextColor(di->hDC, txtcol);
				SetBkColor(di->hDC, bkcol);
				
				return TRUE;
			}

		case WM_MENUSELECT:
			if ((HIWORD(wParam) & MF_POPUP) == MF_POPUP) {
				MENUITEMINFO info;
				dir_component caption;

				debug_printf(TEXT("MENUSELECT on a popup\r\n"));

				info.cbSize = sizeof(info);
				info.cch = sizeof(caption) / sizeof(caption[0]);
				info.dwTypeData = caption;
				info.fMask = MIIM_DATA | MIIM_STRING | MIIM_SUBMENU;

				if (GetMenuItemInfo((HMENU)lParam, LOWORD(wParam), TRUE, &info)) {
					SlitAction *item = (SlitAction*)info.dwItemData;

					debug_printf(TEXT("--- action type is %d %s\r\n"), item->action_type, caption);

					switch (item->action_type) {
						case AT_SCAN_DIR:
						case AT_SCAN_DIR_SUB:
							fill_menu_from_dir(item, info.hSubMenu);
							break;

						case AT_SUBMENU_BY_NAME:
							if (GetMenuItemCount(info.hSubMenu) == 0) {
								SlitAction *pitem = is_plugin_menu(item->u.name);
								if (pitem) {
									Plugin p = (Plugin)pitem->param;
									wezdesk_popup_callback_func cb = (wezdesk_popup_callback_func)pitem->param2;
									cb(p, &core_funcs, (HMENU)lParam, info.hSubMenu);
								}
							}
							break;
					}
				}
				
			}

		default:
			return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}

static struct {
	const TCHAR *label;
	DWORD value;
	dir_component resolved;
} csidl_labels[] = {
#define MAKE_LABEL(foo)	{ TEXT("$") TEXT(#foo), foo, TEXT("")}
	MAKE_LABEL(CSIDL_STARTMENU),
	MAKE_LABEL(CSIDL_COMMON_STARTMENU),
	MAKE_LABEL(CSIDL_PERSONAL),
	MAKE_LABEL(CSIDL_COMMON_DOCUMENTS),
	MAKE_LABEL(CSIDL_APPDATA),
	MAKE_LABEL(CSIDL_ADMINTOOLS),
	{ NULL, 0 }
};

static struct {
	const TCHAR *symbol;
	UINT uID;
	TCHAR *resolved;
} res_labels[] = {
#define MAKE_RES_SYMBOL(x)	{ TEXT("$") TEXT(#x), x, NULL },
#include "wezdeskres-symbols.h"
	{ NULL, 0 }
};

static void cleanup_res_labels(void)
{
	int j;
	for (j = 0; res_labels[j].symbol; j++) {
		if (res_labels[j].resolved) free(res_labels[j].resolved);
	}
}

static TCHAR *resolve_constant(TCHAR *name)
{
	int j;
	for (j = 0; csidl_labels[j].label; j++) {
		if (!lstrcmp(csidl_labels[j].label, name)) {
			if (csidl_labels[j].resolved[0] == '\0') {
				SHGetFolderPath(NULL, CSIDL_FLAG_CREATE|csidl_labels[j].value,
						NULL, SHGFP_TYPE_CURRENT,
						csidl_labels[j].resolved);
				debug_printf(TEXT("resolving %s to %s\r\n"), csidl_labels[j].label, csidl_labels[j].resolved);
			}
			return csidl_labels[j].resolved;
		}
	}
	for (j = 0; res_labels[j].symbol; j++) {
		if (!lstrcmp(res_labels[j].symbol, name)) {
			if (!res_labels[j].resolved) {
				TCHAR buf[2048];
				if (core_funcs.LoadString(res_labels[j].uID, buf,
						sizeof(buf)/sizeof(buf[0]))) {
					res_labels[j].resolved = lstrdup(buf);
				}
			}
			return res_labels[j].resolved;
		}
	}
			
	debug_printf(TEXT("failed to resolve %s\r\n"), name);
	return TEXT("");
}

static struct {
	const TCHAR *symbol;
	DWORD value;
} hotkey_modifier_aliases[] = {
	{ TEXT("ALT"), MOD_ALT },
	{ TEXT("WIN"), MOD_WIN },
	{ TEXT("CTRL"), MOD_CONTROL },
	{ TEXT("SHIFT"), MOD_SHIFT },
	{ TEXT(""), 0 },
	{ NULL }
};

static UINT resolve_hotkey_modifier(TCHAR *name)
{
	UINT mod = 0;
	int i, l;
	TCHAR *next;

	next = name;
	do {
		/* find each modifier token; they can be separated by | */
		while (*next && *next != '|')
			next++;

		l = next - name;
		
		/* now find the modifier label */
		for (i = 0; hotkey_modifier_aliases[i].symbol; i++) {
			if (!wcsncmp(name, hotkey_modifier_aliases[i].symbol, l)) {
				mod |= hotkey_modifier_aliases[i].value;
				break;
			}
		}

		if (*next) {
			name = next+1;
			next = name;
		} else {
			break;
		}
	} while (1);

	return mod;
}

static struct {
	const TCHAR *symbol;
	DWORD value;
} hotkey_key_names[] = {
	{ TEXT("F1"), VK_F1 },
	{ TEXT("F2"), VK_F2 },
	{ TEXT("F3"), VK_F3 },
	{ TEXT("F4"), VK_F4 },
	{ TEXT("F5"), VK_F5 },
	{ TEXT("F6"), VK_F6 },
	{ TEXT("F7"), VK_F7 },
	{ TEXT("F8"), VK_F8 },
	{ TEXT("F9"), VK_F9 },
	{ TEXT("F10"), VK_F10 },
	{ TEXT("F11"), VK_F11 },
	{ TEXT("MUTE"), VK_VOLUME_MUTE },
	{ TEXT("VOLUP"), VK_VOLUME_UP },
	{ TEXT("VOLDOWN"), VK_VOLUME_DOWN },
	{ TEXT("APP1"), VK_LAUNCH_APP1 },
	{ TEXT("APP2"), VK_LAUNCH_APP2 },
	{ TEXT("SLEEP"), VK_SLEEP },
	{ TEXT("SPACE"), VK_SPACE },
	{ TEXT("ESCAPE"), VK_ESCAPE },
	{ NULL }
};

static UINT resolve_hotkey_name(TCHAR *key)
{
	int i;

	for (i = 0; hotkey_key_names[i].symbol; i++) {
		if (!lstrcmp(key, hotkey_key_names[i].symbol)) {
			return hotkey_key_names[i].value;
		}
	}

	/* if it is a single character, then use that as the 'ascii' code */
	if (lstrlen(key) == 1) {
		return *key;
	}

	/* otherwise, if it is #decimal then it is specified numerically */
	if (*key == '#') {
		return _wtoi(key+1);
	}

	return 0;
}

static struct {
	const TCHAR *symbol;
	DWORD value;
} grav_names[] = {
	{ TEXT("LEFT"),		WEZDESK_GRAVITY_LEFT },
	{ TEXT("RIGHT"),	WEZDESK_GRAVITY_RIGHT },
	{ TEXT("TOP"), 		WEZDESK_GRAVITY_TOP },
	{ TEXT("BOTTOM"),	WEZDESK_GRAVITY_BOTTOM },
	{ TEXT("MIDDLE"),	WEZDESK_GRAVITY_MIDDLE },
	{ NULL }
};

UINT resolve_grav(TCHAR *grav)
{
	int i;

	for (i = 0; grav_names[i].symbol; i++) {
		if (!lstrcmp(grav, grav_names[i].symbol)) {
			return grav_names[i].value;
		}
	}
	return WEZDESK_GRAVITY_LEFT;
}


#define MAX_TOKENS 10

static int tokenize_line(TCHAR **line_ptr, int *ntokens, TCHAR **tokens)
{
	TCHAR *eol, *next_line, *cur;
	int n = 0, i;
	TCHAR scratch[1024];

	memset(tokens, 0, MAX_TOKENS * sizeof(TCHAR*));

	cur = *line_ptr;
	for (eol = *line_ptr; *eol && *eol != '\r' && *eol != '\n'; eol++)
		;
	
	next_line = eol;
	while (*next_line == '\r' || *next_line == '\n')
		*next_line++;
	*line_ptr = next_line;

	if (next_line == cur) return 0;

	while (cur < eol && n < MAX_TOKENS) {

		/* trim leading whitespace */
		while (cur < eol && (*cur == ' ' || *cur == '\t'))
			cur++;

		if (cur >= eol) break;

		/* ignore rest of line, if commented */
		if (*cur == '#')
			break;

		/* build a quoted string */
		if (*cur == '"') {
			for (++cur, i = 0; cur < eol && *cur != '"' && i < ((sizeof(scratch)/sizeof(scratch[0]))-1); cur++) {
				if (*cur == '\\') {
					cur++;
					switch (*cur) {
						case 'n':	scratch[i++] = '\n'; break;
						case 'r':	scratch[i++] = '\r'; break;
						case 't':	scratch[i++] = '\t'; break;
						default:	scratch[i++] = *cur; break;
					}
				} else {
					scratch[i++] = *cur;
				}
			}
			cur++;
		} else {
			/* a bare string */
			for (i = 0; cur < eol && *cur != ' ' && *cur != '\t' &&  i < ((sizeof(scratch)/sizeof(scratch[0]))-1); cur++) {
				scratch[i++] = *cur;
			}
			
			if (scratch[0] == '$') {
				/* a constant */
				TCHAR *constant;
				scratch[i] = '\0';
				constant = resolve_constant(scratch);
				StringCbCopy(scratch, sizeof(scratch), constant);
				i = lstrlen(scratch);
			}
		}
			
		scratch[i] = '\0';
		tokens[n++] = lstrdup(scratch);
		debug_printf(TEXT("    %s\r\n"), tokens[n-1]);
	}

	*ntokens = n;
	debug_printf(TEXT("%d tokens\r\n"), n);

	return 1;
}

/* parses data as though it were a context menu file,
 * generating appropriate context menu definitions */
int parse_context_menu(char *utf8_data)
{
	TCHAR *data, *line_ptr;
	int i;
	int ntokens;
	int lineno = 0;
	TCHAR *tokens[MAX_TOKENS];
	menu_def mdef;
	SlitAction *item;
	TCHAR msg[1024];
	HWND slit = NULL;

	memset(&mdef, 0, sizeof(mdef));

#define config_error()	{ int ith; \
	StringCbPrintf(msg, sizeof(msg), TEXT("Error in menu definitions on line %d (parsed %d tokens)\n"), lineno, ntokens); \
	for (ith = 0; ith < ntokens; ith++) { \
		StringCbCat(msg, sizeof(msg), tokens[ith]); \
		StringCbCat(msg, sizeof(msg), TEXT("\n")); \
	} \
	core_funcs.ShowMessageBox(TEXT("config error"), msg, MB_ICONERROR|MB_OK); \
	continue; \
}

	i = MultiByteToWideChar(CP_UTF8, 0, utf8_data, strlen(utf8_data), NULL, 0);
	data = (TCHAR*)malloc((1 + i) * sizeof(TCHAR));
	MultiByteToWideChar(CP_UTF8, 0, utf8_data, strlen(utf8_data), data, i);
	data[i] = '\0';

#define wrap_up_vec()	\
	if (mdef.name) { \
		menu_defs.push_back(mdef); \
	} \
	mdef.name = 0; \
	mdef.root = NULL;

	line_ptr = data;
	while (tokenize_line(&line_ptr, &ntokens, tokens)) {
		lineno++;
		if (!ntokens) continue;

		if (!lstrcmp(tokens[0], TEXT("CONTEXTMENU"))) {
			wrap_up_vec();
			
			if (ntokens != 2) {
				config_error();
			}
			mdef.name = tokens[1];
			mdef.root = new SlitAction;
			mdef.root->action_type = AT_SUB_MENU;
			free(tokens[0]);

			continue;
		}

		if (!lstrcmp(tokens[0], TEXT("MergeDirs"))) {
			if (ntokens != 4) {
				config_error();
			}

			item = new SlitAction;
			item->action_type = AT_SCAN_DIR;
			item->caption = tokens[1];

			item->u.merge_dirs.a = tokens[2];
			item->u.merge_dirs.b = tokens[3];
			mdef.root->Add(item, __FILE__, __LINE__);

			free(tokens[0]);
			continue;
		}

		if (!lstrcmp(tokens[0], TEXT("SubMenu"))) {
			if (ntokens != 3) {
				config_error();
			}

			item = new SlitAction;
			item->action_type = AT_SUBMENU_BY_NAME;
			item->caption = tokens[1];
			item->u.name = tokens[2];
			mdef.root->Add(item, __FILE__, __LINE__);

			free(tokens[0]);
			continue;
		}

		if (!lstrcmp(tokens[0], TEXT("Execute"))) {
			if (ntokens < 4 || ntokens > 5) {
				config_error();
			}

			item = new SlitAction;
			item->action_type = AT_EXECUTE_FILE;
			item->caption = tokens[1];
			item->u.exec.verb = tokens[2];
			item->u.exec.exe = tokens[3];
			item->u.exec.args = ntokens == 5 ? tokens[4] : lstrdup(TEXT(""));
			mdef.root->Add(item, __FILE__, __LINE__);

			free(tokens[0]);
			continue;
		}

		if (!lstrcmp(tokens[0], TEXT("Func"))) {
			if (ntokens != 4) {
				config_error();
			}

			item = new SlitAction;
			item->action_type = AT_FUNC_BY_NAME;
			item->caption = tokens[1];
			item->u.func.name = tokens[2];
			item->u.func.arg = tokens[3];
			mdef.root->Add(item, __FILE__, __LINE__);

			free(tokens[0]);
			continue;
		}

		if (!lstrcmp(tokens[0], TEXT("Separator"))) {
			if (ntokens != 1) {
				config_error();
			}

			item = new SlitAction;
			item->action_type = AT_SEPARATOR;
			mdef.root->Add(item, __FILE__, __LINE__);
			free(tokens[0]);
			continue;
		}

		if (!lstrcmp(tokens[0], TEXT("SaferExec"))) {
			if (ntokens != 3) {
				config_error();
			}

			struct safer_match s;
			const char *errstr = NULL;

			switch (tokens[1][0]) {
				case 'U': case 'u': s.level = SAFER_LEVELID_UNTRUSTED; break;
				case 'C': case 'c': s.level = SAFER_LEVELID_CONSTRAINED; break;
				default: s.level = SAFER_LEVELID_NORMALUSER; break;
			}

			s.re = new wezdesk_re();
			
			if (!s.re->compile(tokens[2])) {
				config_error();
			}

			safer_matches.push_back(s);
			free(tokens[0]);
			free(tokens[1]);
			free(tokens[2]);
			continue;
		}

		if (!lstrcmp(tokens[0], TEXT("HOTKEY"))) {
			UINT mod, vk;
			
			if (ntokens < 5) {
				config_error();
			}

			mod = resolve_hotkey_modifier(tokens[1]);
			vk = resolve_hotkey_name(tokens[2]);

			/* accept either Func or Execute */
			if (!lstrcmp(tokens[3], TEXT("Func"))) {
				item = new SlitAction;
				item->action_type = AT_FUNC_BY_NAME;
				item->caption = tokens[0];
				item->u.func.name = tokens[4];
				item->u.func.arg = ntokens < 6 ? NULL : tokens[5];

				register_hotkey(mod, vk, execute_item, item, contextmenu_delete_item);
				free(tokens[1]);
				free(tokens[2]);
				free(tokens[3]);
				continue;

			} else if (!lstrcmp(tokens[3], TEXT("Execute"))) {
				item = new SlitAction;
				item->action_type = AT_EXECUTE_FILE;
				item->caption = tokens[0];
				item->u.exec.verb = tokens[4];
				item->u.exec.exe = tokens[5];
				item->u.exec.args = ntokens == 7 ? tokens[6] : lstrdup(TEXT(""));

				register_hotkey(mod, vk, execute_item, item, contextmenu_delete_item);
				free(tokens[1]);
				free(tokens[2]);
				free(tokens[3]);
				continue;

			}
			config_error();
		}

		if (!lstrcmp(tokens[0], TEXT("SLIT"))) {
			if (ntokens == 3) {
				UINT align, grav;
				align = resolve_grav(tokens[1]);
				grav = resolve_grav(tokens[2]);
				slit = create_new_slit(align, grav, NULL);

				free(tokens[2]);
			} else if (ntokens == 2) {
				slit = create_new_slit(WEZDESK_GRAVITY_RIGHT,
						WEZDESK_GRAVITY_BOTTOM, tokens[1]);
			} else {
				config_error();
			}
			free(tokens[0]);
			free(tokens[1]);
			
			continue;
		}

		if (!lstrcmp(tokens[0], TEXT("LOAD"))) {
			if (ntokens != 2) {
				config_error();
			}
			core_funcs.LoadPlugin(tokens[1], slit);
			free(tokens[0]);
			free(tokens[1]);
			continue;
		}

		if (!lstrcmp(tokens[0], TEXT("MATCH"))) {
			if (ntokens < 5) {
				config_error();
			}

			struct window_match w;

			w.re = new wezdesk_re();
			if (!w.re->compile(tokens[3])) {
				config_error();
			}

			w.match_type = tokens[1];
			w.propname = tokens[2];
			w.funcname = tokens[4];
			w.funcarg = tokens[5];

			window_matches.push_back(w);
			free(tokens[0]);
			free(tokens[3]);
			continue;	
		}

		if (!lstrcmp(tokens[0], TEXT("SET"))) {
			/* SET context name value */
			if (ntokens != 4) {
				config_error();
			}

			if (!lstrcmp(tokens[1], TEXT("slit"))) {
				slit_set_config(slit, tokens[2], tokens[3]);
			} else if (!lstrcmp(tokens[1], TEXT("core"))) {
				core_set_config(tokens[2], tokens[3]);
				if (!lstrcmp(tokens[2], TEXT("Thumbnail.Max.Size"))) {
					MAX_IMAGE_SIZE = _wtoi(tokens[3]);
				}
			} else {
				plugin_set_config(tokens[1], tokens[2], tokens[3]);
			}
			free(tokens[0]);
			free(tokens[1]);
			free(tokens[2]);
			free(tokens[3]);
			continue;
		}

		config_error();
	}

	wrap_up_vec();

	free(data);

	return 1;
}

void init_context_menu(void)
{
	HANDLE f;
	char *data;
	DWORD size, nread;
	dir_component path;
	TCHAR *dir;

	/* find the users menu file */
	dir = resolve_constant(TEXT("$CSIDL_APPDATA"));
	StringCbPrintf(path, sizeof(path),
		TEXT("%s\\Evil, as in Dr."), dir);
	CreateDirectory(path, NULL);

	StringCbPrintf(path, sizeof(path),
		TEXT("%s\\Evil, as in Dr.\\Shell"), dir);
	CreateDirectory(path, NULL);

	StringCbPrintf(path, sizeof(path),
		TEXT("%s\\Evil, as in Dr.\\Shell\\evildesk.evdm"), dir);

	if (!PathFileExists(path)) {
		DWORD len = GetModuleFileName(NULL, path, sizeof(path));
		path[len] = 0;
		--len;
		while (path[len] != '\\')
			--len;
		path[++len] = 0;
		StringCbCat(path, sizeof(path), TEXT("default.evdm"));
	}

	f = CreateFile(path, GENERIC_READ,
		FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN,
		NULL);

	size = GetFileSize(f, NULL);
	data = (char*)malloc(size + 1);

	ReadFile(f, data, size, &nread, NULL);
	data[size] = '\0';

	CloseHandle(f);

	parse_context_menu(data);
	free(data);

//	Shell_GetImageLists(&big_icons, &small_icons);
}

static HMENU realize_menu_by_name(TCHAR *menu_name, ResourcePool *pool)
{
	vector<menu_def>::iterator i;

	for (i = menu_defs.begin(); i != menu_defs.end(); i++) {
		struct menu_def def = *i;

		if (!lstrcmp(menu_name, def.name)) {
			SlitAction *act;
			act = def.root->clone();
			act->pool = pool;
			pool->add(delete_item, act);
			return build_menu(act);
		}
	}
	return NULL;
}

void popup_context_menu(int x, int y, HWND wnd, TCHAR *name)
{
	HMENU m;
	ResourcePool *pool = new ResourcePool;

	m = realize_menu_by_name(name, pool);
	if (!m) {
		delete pool;
		return;
	}
	debug_printf(TEXT("popup_context_menu(%s) called, realized to %x\r\n"), name, m);

	TrackPopupMenuEx(
				m,
				TPM_NOANIMATION,
				x, y,
				wnd,
				NULL);

	PostMessage(core_funcs.GetRootWindow(), WZDM_FREE_MENU, (WPARAM)pool, NULL);
}

