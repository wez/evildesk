/* Copyright (c) 2004-2009 Wez Furlong.
 * This source is provided under terms of the GPLv2.
 * See the file named LICENSE for full details */
#define UNICODE 1
#define WINVER			0x501
#define _WIN32_WINNT	0x501
#define _WIN32_IE		0x600

#ifdef DEBUG_MEM
#define _CRTDBG_MAP_ALLOC
#define DEBUG
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <windows.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <gdiplus.h>

#ifndef WEZDESK_SUPRESS_MANIFEST
#ifdef _WIN64
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='X86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif
#endif

#define WEZDESK_API_NUMBER	0x10000005
#ifndef lstrdup
#define lstrdup(src) _wcsdup(src)
#endif

typedef struct _WezDeskPluginInstance *Plugin;
typedef struct _WezDeskFuncs WezDeskFuncs;

typedef struct _WezDeskThumb {
	LPVOID vt;
	HWND wDest, wSrc;
	RECT dest, source;
	BYTE opacity;
} WezDeskThumb;

typedef void (*wezdesk_popup_callback_func)(Plugin plugin, WezDeskFuncs *funcs, HMENU parent, HMENU menu);

typedef struct _WezDeskLaunchItem {
	TCHAR *displayText;
	enum {
		WZDLI_EXECUTE,
		WZDLI_FUNC,
	} execType;
	int add_display_text_to_run_mru;
	int weighting;
	union {
		struct {
			TCHAR *command;
			TCHAR *args;
			TCHAR *verb;
		} exec;
		struct {
			void (*execute)(void *arg);
			void *arg;
		} func;
	} e;
	enum {
		WZDLI_HICON,
		WZDLI_IMAGE
	} imageType;
	union {
		HICON hIcon;
		Gdiplus::Image *Image;
	} i;
} WezDeskLaunchItem;

/* The launcher will take its own copy of the strings in item.
 * If you pass an HICON or an Image, the launcher will take ownership;
 * it will destroy the resource when it is done using it, so you should
 * ensure that it is safe for it to do so.
 */
typedef void (*WezDeskAddToLauncherFunc)(void *cookie, WezDeskLaunchItem *item);

/* when WZN_ON_LAUNCHER_TEXT is received, you find one of these in the LPARAM */
typedef struct _WezDeskLauncherTextRequest {
	/* the text the user typed */
	TCHAR *text;
	/* function that will add matching items */
	WezDeskAddToLauncherFunc add;
	/* cookie to pass to the add function */
	void *addCookie;
} WezDeskLauncherTextRequest;


/* A pointer to this structure is passed to your plugin when it initializes.
 * You may use it to call back into the main application. */
struct _WezDeskFuncs {
	unsigned long version;
	const TCHAR *version_string;

	/* version 0x10000000 functions here */
	void (__stdcall *MSWinShutdown)(HWND window);
	/* typically called will all params set to NULL */
	void (__stdcall *RunDlg)(HWND window, HICON icon, LPCTSTR string1, LPCTSTR string2, LPCTSTR string3, int dunno);

	/* execute a file with the shell */
	HINSTANCE (*Execute)(HWND owner, LPCTSTR operation, LPCTSTR command, LPCTSTR args, LPCTSTR cwd, int nshowcmd, int dont_show_errors);

	/* switch to a different desktop. We support a maximum of 32 desktops */
	void (*SwitchDesktop)(int desktop);

	/* show a message box */
	int (*ShowMessageBox)(LPCTSTR caption, LPCTSTR body, unsigned long flags);

	/* Get the root window */
	HWND (*GetRootWindow)(void);

	/* Change the desktop flags bit field for a given window.
	 * Desktop 1 corresponds to bit 0, desktop 16 to bit 15.
	 * minimized state on each desktop for a window corresponds to bits 16 through 31; 0 means minimized, 1 means not-minimized.
	 * Set all bits to make the window visible on all desktops.
	 * If mask is set to 0, retrieves the current flags without modifying them, otherwise,
	 * mask specifies which bits to change, and newval specifies the new values for those bits */
	DWORD (*ChangeDesktopBits)(HWND hwnd, DWORD mask, DWORD newval);

	/* fetch the number of the active desktop */
	int (*GetActiveDesktop)(void);

	/* load a plugin */
	Plugin (*LoadPlugin)(LPCTSTR name, HWND slit);

	HKEY (*GetPluginDataStore)(Plugin plugin);
	void *(*GetPluginData)(Plugin plugin);
	HMODULE (*GetPluginInstance)(Plugin plugin);
	DWORD (*GetPluginInt)(Plugin plugin, LPCTSTR name, DWORD defval);
	/* caller must free() the return value of this function */
	LPTSTR (*GetPluginString)(Plugin plugin, LPCTSTR name, LPCTSTR defval);

	void (*Trace)(LPCTSTR fmt, ...);
	
	/* copies the data for a given icon into the provided buffer */
	int (*GetTrayData)(NOTIFYICONDATA *buf);

#define WEZDESK_GRAVITY_NONE	0
#define WEZDESK_GRAVITY_LEFT	1
#define WEZDESK_GRAVITY_RIGHT	2
#define WEZDESK_GRAVITY_TOP		4
#define WEZDESK_GRAVITY_BOTTOM	8
#define WEZDESK_GRAVITY_MIDDLE	16

	/* set the alignment gravity and priority for a given window */
	void (*SetGravity)(HWND hwnd, int gravity, int priority);
	void (*SetGravityFromConfig)(Plugin plugin, HWND hwnd, LPCTSTR name, int defgrav, int defpri);

	int (*GetGravity)(HWND hwnd, int *gravity, int *priority);
	
	int (__stdcall * DrawShadowText)(HDC hdc, LPCWSTR pszText, UINT cch, RECT *pRect,
		DWORD dwFlags, COLORREF crText, COLORREF crShadow, int ixOffset, int iyOffset);

#define WEZDESK_FONT_DEFAULT		0
#define WEZDESK_FONT_DEFAULT_BOLD	1
#define WEZDESK_FONT_TITLE 			2
#define WEZDESK_FONT_SMALL_TITLE    3
#define WEZDESK_FONT_TOOLTIP        4
#define WEZDESK_FONT_MESSAGE        5

	HFONT (*GetStockFont)(int which);

	void (*SwitchToWindow)(HWND hWnd, BOOL altTab);

	/* context menu plugin utilities */
	void (*AddCallbackMenuItem)(HMENU menu, LPCWSTR caption, void (*cbfunc)(void*), void *arg, void (*dtorfun)(void*));
	void (*AddSubMenu)(HMENU menu, LPCWSTR caption, Plugin owner);
	void (*RegisterPluginContextMenu)(Plugin plugin, TCHAR *name, wezdesk_popup_callback_func cb);

	/* You might want to look at RegisterThumb instead */
	Gdiplus::Image *(*GetWindowThumbnail)(HWND hWnd, double *scale);

	void (*DefineFunction)(Plugin plugin, TCHAR *name, void (*func)(TCHAR *arg, HWND wnd));

	/* send a notification.  The protocol is that negative numbers will stop enumerating
	 * over the plugin.  0 means that it was not handled, a positive number indicates
	 * a success return, but to continue enumeration */
	int (*SendNotify)(UINT msg, UINT secondary, WPARAM wparam, LPARAM lparam);

	HFONT (*LoadFont)(HDC hdc, TCHAR *descriptor);
	
	/* version 0x10000005 functions here */
	int (*GetSlitAlignment)(HWND slit);
	int (*IsAppWindow)(HWND w);
	TCHAR *(*GetWindowModulePath)(HWND w);
	HICON (*GetWindowIcon)(HWND w, DWORD timeoutms);
	int (*LoadString)(UINT uID, TCHAR *buf, int nbuf);

	/* vista compatible thumbnail API */
	WezDeskThumb *(*RegisterThumb)(HWND renderDest, HWND src);
	HRESULT (*RenderThumb)(WezDeskThumb *thumb);
	void (*UnregisterThumb)(WezDeskThumb *thumb);
	HRESULT (*QueryThumbSourceSize)(WezDeskThumb *thumb, PSIZE pSze);
	TCHAR *(*GetFilePath)(TCHAR *filename);

	void (*StorePluginBlob)(Plugin plugin, TCHAR *key, void *blob, int len);
	HANDLE (*GetPluginBlobHandle)(Plugin plugin, TCHAR *key, int forwrite);
	int (*GetPluginBlobPath)(Plugin plugin, TCHAR *key, TCHAR *path, int size);

	/* sets *blob to a memory blob holding your serialized data.
	 * you are responsible for free()ing it.
	 * Returns the length of the blob.
	 */
	int (*GetPluginBlob)(Plugin plugin, TCHAR *key, void **blob);

	/* schedules a callback after a certain time period expires */
	int (*ScheduleCallBack)(Plugin plugin, DWORD msFromNow,
		void *cookie, void (*func)(void *));

	/* compiles a regular expression and returns a handle to it */
	HANDLE (*CompileRegexp)(LPCTSTR pattern);
	int (*MatchRegexp)(HANDLE re, LPCTSTR subject);
};
/* sent by a plugin to the slit when it resizes.  This happens
 * automatically because the slit subclasses your plugin window.
 * You do not need to manually send this message */
#define WM_SLIT_CHILD_RESIZED  WM_USER+1
/* sent by the slit to its children when its layout properties are
 * changed.  The plugin windows should adjust their size accordingly
 * in response to this message, which may include a change in orientation.
 */
#define WM_SLIT_LAYOUT_CHANGED WM_USER+2

/* Your plugin should export a function named "GetWezDeskPlugin"
 * that returns this statically allocated structure.  
 * It should take no other action than to allocate the structure.
 * */
typedef struct _WezDeskPlugin {
	unsigned long plugin_version;	/* WezDeskPlugin structure version */

	/* descriptive data about the plugin */
	const TCHAR *name;
	const TCHAR *description;
	const TCHAR *license;
	const TCHAR *authors;
	const TCHAR *support_url;
	const TCHAR *version;
	
	/* initialize a plugin instance.  Return NULL to indicate failure,
	 * any other value otherwise.  Value will be passed into subsequent
	 * calls from the core */
	void *(*initialize)(Plugin plugin, WezDeskFuncs *funcs, HWND slit);
	
	/* ask if it is okay to shutdown the plugin */
	int (*is_unload_ok)(Plugin plugin, WezDeskFuncs *funcs);

	/* shut down the plugin */
	int (*unload)(Plugin plugin, WezDeskFuncs *funcs, int force);
	
	/* veto startup items; return a positive value to allow the item to run, 0 to veto it.
	 * If veoted, set the BSTR parameter to the reason why (use SysAllocString),
	 * or leave it NULL to silently suppress it.
	 * The return value is the priority with which to run the item when compared to other
	 * items that will run.  The higher the number, the earlier in the startup it will run.
	 * The startup list is generated by scanning all sources first, and then executing once
	 * that process is complete.
	 * command_line is the item to launch, source identifies where it was registered:
	 * HKLM/Run, HKLM/RunOnce, HKCU/Run, HKCU/RunOnce, StartMenu/StartUp, WezDesk/StartUp */
	int (*veto_startup_item)(Plugin plugin, WezDeskFuncs *funcs, TCHAR *command_line, TCHAR *source, BSTR **reason);

	/* handle a shell level message */
	void (*on_shell_message)(Plugin plugin, WezDeskFuncs *funcs, WPARAM msg, LPARAM lparam);

	/* get notification about tray events */
	void (*on_tray_change)(Plugin plugin, WezDeskFuncs *funcs, int action, NOTIFYICONDATA *nid, DWORD changemask);

	/* generic notification */
	int (*on_notify)(Plugin plugin, WezDeskFuncs *funcs, UINT code, UINT secondary, WPARAM wparam, LPARAM lparam);
#define WZN_ALT_TAB_ACTIVATE   0
#define WZN_ALT_TAB_FORWARD    1
#define WZN_ALT_TAB_BACKWARD   2
#define WZN_ALT_TAB_SELECT     3
#define WZN_ALT_TAB_CANCEL     4
#define WZN_ALT_TAB_DESK       5

#define WZN_DESKTOP_SWITCHING  6
#define WZN_DESKTOP_SWITCHED   7

#define WZN_ON_LAUNCHER_TEXT 8
#define WZN_ON_LAUNCHER_INDEX 9

} WezDeskPlugin;



#define GET_PLUGIN(name)	extern "C" __declspec(dllexport) WezDeskPlugin *GetWezDeskPlugin(void) { return &name; }

#ifndef RECTWIDTH
# define RECTWIDTH(r)	((r).right - (r).left)
#endif
#ifndef RECTHEIGHT
# define RECTHEIGHT(r)	((r).bottom - (r).top)
#endif

