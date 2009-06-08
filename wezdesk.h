/* Copyright (c) 2004-2009 Wez Furlong.
 * This source is provided under terms of the GPLv2.
 * See the file named LICENSE for full details */
#include "wezdeskapi.h"
#include "pcre.h"
#include "wezdeskres.h"
#include <vector>
#include <map>
#include <WinSafer.h>
#include <gdiplus.h>
#define STRSAFE_LIB
#include <strsafe.h>
using namespace std;
using namespace Gdiplus;
extern TCHAR base_dir[1024];

struct config_value {
	TCHAR *name;
	TCHAR *value;
};
extern vector <config_value> core_config;

extern void delete_config_value(vector <config_value> &cv);

struct plugin_func {
	TCHAR *name;
	void (*func)(TCHAR *arg, HWND wnd);
};

struct plugin_callback {
	int id;
	DWORD due;
	void *cookie;
	void (*func)(void*);
};

class _WezDeskPluginInstance {
public:
	TCHAR *name;
	HMODULE			module;
	WezDeskPlugin	*plugin;
	void     		*data;
	HKEY			key;
	vector <config_value> config;
	vector <plugin_func> funcs;
	vector <plugin_callback> callbacks;
};

extern WezDeskFuncs core_funcs;

typedef struct per_desktop PerDesktop;
typedef struct per_window PerWindow;

struct per_window {
	HWND wnd;
	struct per_window *next, *prev;
	DWORD desktop_bits;
	DWORD not_minimized;
	int gravity, grav_pri;

	/* when minimizing a window, we'll capture a thumbnail here */
	Image *capture;
	int capture_w, capture_h;
	double capture_scale; /* scaling factor from original size to current image size */
	DWORD last_capture_time;
#define CAPTURE_INTERVAL 20000

	TCHAR module_path[MAX_PATH];

	/* extend this with a FSM so that the WindowMatch code can handle the case
	 * where a window is being created but needs to end up on a different
	 * desktop */

};
void capture_window(PerWindow *win);

struct per_desktop {
	vector<HWND> zorder;
};

extern vector <Plugin> plugins;
typedef vector <Plugin>::iterator PluginIterator;

int init_tray(void);
int tray_get_tray_data(NOTIFYICONDATA *buf);
void debug_printf(LPCTSTR fmt, ...);

extern HWND slit_left, slit_right, slit_top, slit_bottom;
extern void init_slit(void);
extern void init_context_menu(void);
LRESULT CALLBACK context_menu_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

extern int is_app_window(HWND w);
extern void init_task_switcher(void);
extern PerWindow *get_PerWindow(HWND hwnd, int current_desktop);
void popup_context_menu(int x, int y, HWND wnd, TCHAR *name);

extern int MAX_IMAGE_SIZE;

#include "wezdesk-int.h"

void slit_add_callback_menu_item(HMENU menu, LPCWSTR caption, void (*cbfunc)(void*), void *arg, void (*dtorfunc)(void*));
void slit_add_per_plugin_sub_menu(HMENU menu, LPCWSTR caption, Plugin owner);
#define lstrdup(src) _wcsdup(src)

class wezdesk_re {
	pcre *re;
	pcre_extra *extra;
	int *offsets;
	int size_offsets;

public:
	~wezdesk_re();
/* expects a pattern of the form: "/pattern/optional_flags"
 * converts it to utf8 and compiles it */

	int compile(LPCTSTR pattern);
	int match(LPCTSTR subject);
};

struct safer_match {
	wezdesk_re *re;
	DWORD level;
};

struct window_match {
	wezdesk_re *re;
	TCHAR *match_type;
	TCHAR *propname;
	TCHAR *funcname;
	TCHAR *funcarg;
};

extern vector<safer_match> safer_matches;
extern vector<window_match> window_matches;
extern BOOL need_to_respawn_shell;
extern HWND active_workspace_context_window;
void unregister_hotkeys(void);

typedef void (*hotkey_func_t)(void *arg, HWND wnd);

int register_hotkey(UINT mod, UINT vk, hotkey_func_t func, void *arg, hotkey_func_t dtor);
int run_func_by_name(TCHAR *name, TCHAR *arg, HWND wnd);
int handle_hotkey(UINT mod, UINT vk);
extern int maximum_desktops_used_by_the_user;
int is_slit(HWND w);
HWND create_new_slit(WORD align, WORD grav, TCHAR *name);
void destroy_slits(void);
void context_menu_register_plugin_submenu(Plugin plugin, TCHAR *name, wezdesk_popup_callback_func cb);
void slit_hide_if_fullscreen(HWND wnd);
int core_send_notify(UINT msg, UINT secondary, WPARAM wparam, LPARAM lparam);
void switcher_move_forward(void);
void switcher_activate(BOOL shift);
void switcher_move_forward(void);
void switcher_move_backward(void);
void switcher_change_desk(void);
void switcher_cancel(void);
void switcher_select(void);


void slit_set_config(HWND wnd, TCHAR *name, TCHAR *value);
void core_set_config(TCHAR *name, TCHAR *value);
void plugin_set_config(TCHAR *plugin_name, TCHAR *name, TCHAR *value);
int get_slit_alignment(HWND slit);

/* vista bits */
#define DWM_TNP_RECTDESTINATION 1
#define DWM_TNP_RECTSOURCE      2
#define DWM_TNP_OPACITY         4
#define DWM_TNP_VISIBLE         8
#define DWM_TNP_SOURCECLIENTAREAONLY 16

typedef struct _DWM_THUMBNAIL_PROPERTIES {
	DWORD dwFlags;
	RECT rcDestination;
	RECT rcSource;
	BYTE opacity;
	BOOL fVisible;
	BOOL fSourceClientAreaOnly;
} DWM_THUMBNAIL_PROPERTIES;

typedef LPVOID HTHUMBNAIL;

void do_slit_align(void *arg, HWND slit);
void do_slit_gravity(void *arg, HWND slit);
void do_slit_autohide(void *arg, HWND slit);
void do_slit_float(void *arg, HWND slit);
UINT resolve_grav(TCHAR *grav);
LONG create_reg_key(HKEY key, LPCTSTR keyname, HKEY *out_key);
void contextmenu_shutdown(void);
void contextmenu_delete_item(void *_item, HWND wnd);
void contextmenu_delete_pool(void *_pool);
void fini_tray(void);
LONG WINAPI crash_dumper(PEXCEPTION_POINTERS info);
HWND find_a_slit(HWND hWnd);

int enum_hotkeys(int (*cb)(hotkey_func_t func, void *hotkeyarg, UINT mod, UINT vk, void *cookie), void *cookie);
int fake_root_context_hotkey(void);


