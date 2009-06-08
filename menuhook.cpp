/* Copyright (c) 2004-2009 Wez Furlong.
 * This source is provided under terms of the GPLv2.
 * See the file named LICENSE for full details */
#define WEZDESK_SUPRESS_MANIFEST 1
#include "wezdeskapi.h"
#include "wezdesk-int.h"
#include "wezdeskres.h"
#include <strsafe.h>

/* This hook is designed to be injected into the processes on your desktop.
 * To make things fun, on win64, we need a 32-bit version of this DLL and
 * a 32-bit host process to be able to inject into 32-bit processes.
 */
#pragma section(".shared",read,write,shared)
#define SHARED	__declspec(allocate(".shared"))
SHARED HHOOK		wnd_hook = NULL;
SHARED HHOOK        gm_hook = NULL;
SHARED HHOOK        mouse_hook = NULL;
SHARED unsigned int workspace_msg;
SHARED TCHAR base_dir[MAX_PATH];
SHARED HWND mouse_sink = NULL;

#define WZC_LAST        0xf000
#define WZC_WORKSPACE   0x1000
#define WZC_STICKY      WZC_WORKSPACE+32
#define WZC_TRANSPARENT 0x100

static HINSTANCE lang1 = NULL, lang2 = NULL;

static int load_string(int code, TCHAR *buf, int size)
{
	TCHAR langdll[MAX_PATH];
	int ret;

	if (!lang1) {
		StringCbPrintf(langdll, sizeof(langdll),
#ifdef _WIN64
			TEXT("%s\\64-%04x.dll"),
#else
			TEXT("%s\\32-%04x.dll"),
#endif
			base_dir,
			GetUserDefaultUILanguage());
		lang1 = LoadLibraryEx(langdll, NULL, LOAD_LIBRARY_AS_DATAFILE);
	}
	if (!lang2) {
		StringCbPrintf(langdll, sizeof(langdll),
#ifdef _WIN64
			TEXT("%s\\64-0409.dll"),
#else
			TEXT("%s\\32-0409.dll"),
#endif
			base_dir);
		lang2 = LoadLibraryEx(langdll, NULL, LOAD_LIBRARY_AS_DATAFILE);
	}

	if (lang1) {
		ret = LoadString(lang1, code, buf, size);
		if (ret) return ret;
	}
	return LoadString(lang2, code, buf, size);
}

static void unload_lang(void)
{
	if (lang1) FreeLibrary(lang1);
	if (lang2) FreeLibrary(lang2);
	lang1 = lang2 = NULL;
}

LRESULT CALLBACK WndHook(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION) {
		PCWPSTRUCT c = (PCWPSTRUCT)lParam;
		switch (c->message) {
			case WM_MENUSELECT:
				if (HIWORD(c->wParam) == 0xffff && c->lParam == NULL) {
					HMENU menu = GetSystemMenu(c->hwnd, FALSE);

					RemoveMenu(menu, WZC_WORKSPACE, MF_BYCOMMAND);
					RemoveMenu(menu, WZC_WORKSPACE+1, MF_BYCOMMAND);
					RemoveMenu(menu, WZC_WORKSPACE+2, MF_BYCOMMAND);
					RemoveMenu(menu, WZC_WORKSPACE+3, MF_BYCOMMAND);
					RemoveMenu(menu, WZC_STICKY, MF_BYCOMMAND);
				}
				break;

			case WM_EXITMENULOOP:
				if (c->wParam == FALSE) {
					unload_lang();
				}
				break;

			case WM_INITMENUPOPUP:
				if (HIWORD(c->lParam) == TRUE) {
					HMENU menu = GetSystemMenu(c->hwnd, FALSE);
					TCHAR caption[MAX_PATH];

					/* modify here */
					load_string(IDS_MOVE_WINDOW_TO_WORKSPACE_1, caption,
						sizeof(caption)/sizeof(caption[0]));
					AppendMenu(menu, MF_BYCOMMAND|MF_STRING,
						WZC_WORKSPACE, caption);

					load_string(IDS_MOVE_WINDOW_TO_WORKSPACE_2, caption,
						sizeof(caption)/sizeof(caption[0]));
					AppendMenu(menu, MF_BYCOMMAND|MF_STRING,
						WZC_WORKSPACE+1, caption);

					load_string(IDS_MOVE_WINDOW_TO_WORKSPACE_3, caption,
						sizeof(caption)/sizeof(caption[0]));
					AppendMenu(menu, MF_BYCOMMAND|MF_STRING,
						WZC_WORKSPACE+2, caption);

					load_string(IDS_MOVE_WINDOW_TO_WORKSPACE_4, caption,
						sizeof(caption)/sizeof(caption[0]));
					AppendMenu(menu, MF_BYCOMMAND|MF_STRING,
						WZC_WORKSPACE+3, caption);

					load_string(IDS_MAKE_STICKY, caption,
						sizeof(caption)/sizeof(caption[0]));
					AppendMenu(menu, MF_BYCOMMAND|MF_STRING,
						WZC_STICKY, caption);
				}
				break;
		}
	}
	return CallNextHookEx(wnd_hook, nCode, wParam, lParam);
}

LRESULT CALLBACK GMHook(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION) {
		PMSG msg = (PMSG)lParam;
		switch (msg->message) {
			case WM_SYSCOMMAND:
				switch (LOWORD(msg->wParam)) {
					case WZC_TRANSPARENT: {
						LONG style = GetWindowLong(msg->hwnd, GWL_EXSTYLE);
						if ((style & WS_EX_LAYERED) == WS_EX_LAYERED) {
							SetWindowLong(msg->hwnd, GWL_EXSTYLE,
								style & ~WS_EX_LAYERED);
						} else {
							SetWindowLong(msg->hwnd, GWL_EXSTYLE,
								WS_EX_LAYERED|WS_EX_COMPOSITED);
							SetLayeredWindowAttributes(msg->hwnd, 0,
								(255*80)/100, LWA_ALPHA);
						}
						break;
					}
					case WZC_WORKSPACE:
					case WZC_WORKSPACE+1:
					case WZC_WORKSPACE+2:
					case WZC_WORKSPACE+3:
					case WZC_WORKSPACE+4:
					case WZC_WORKSPACE+5:
					case WZC_WORKSPACE+6:
					case WZC_WORKSPACE+7:
					case WZC_WORKSPACE+8:
					case WZC_WORKSPACE+9:
					case WZC_WORKSPACE+10:
					case WZC_WORKSPACE+11:
					case WZC_WORKSPACE+12:
					case WZC_WORKSPACE+13:
					case WZC_WORKSPACE+14:
					case WZC_WORKSPACE+15:
					case WZC_WORKSPACE+16:
					case WZC_WORKSPACE+17:
					case WZC_WORKSPACE+18:
					case WZC_WORKSPACE+19:
					case WZC_WORKSPACE+20:
					case WZC_WORKSPACE+21:
					case WZC_WORKSPACE+22:
					case WZC_WORKSPACE+23:
					case WZC_WORKSPACE+24:
					case WZC_WORKSPACE+25:
					case WZC_WORKSPACE+26:
					case WZC_WORKSPACE+27:
					case WZC_WORKSPACE+28:
					case WZC_WORKSPACE+29:
					case WZC_WORKSPACE+30:
					case WZC_WORKSPACE+31:
					case WZC_STICKY:
					{
						PostMessage(HWND_BROADCAST,
							workspace_msg, (WPARAM)msg->hwnd,
							LOWORD(msg->wParam) - WZC_WORKSPACE);
						break;
					}
				}
				break;
		}
	}
	return CallNextHookEx(gm_hook, nCode, wParam, lParam);
}

LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION) {
		PMOUSEHOOKSTRUCT pmhStruct = (PMOUSEHOOKSTRUCT)lParam;
		switch (wParam) {
			case WM_RBUTTONDOWN:
			case WM_LBUTTONDOWN:
			case WM_MBUTTONDOWN:
			case WM_XBUTTONDOWN:
				if (pmhStruct->hwnd == GetDesktopWindow()) {
					SetForegroundWindow(mouse_sink);
				}
				break;
			case WM_RBUTTONUP:
				if (pmhStruct->hwnd == GetDesktopWindow()) {
					SetForegroundWindow(mouse_sink);
					PostMessage(mouse_sink, WZDM_DESKTOP_MOUSE, wParam,
						pmhStruct->pt.x | (pmhStruct->pt.y << 16));
				}
				break;
		}
	}
	return CallNextHookEx(mouse_hook, nCode, wParam, lParam);
}


extern "C" __declspec(dllexport) void hook(HINSTANCE my_instance, HWND root_win, BOOL enable)
{
	if (enable) {
		workspace_msg = RegisterWindowMessage(TEXT("WezDeskWorkSpaceMessage"));
		mouse_sink = root_win;
		DWORD len = GetModuleFileName(my_instance, base_dir, sizeof(base_dir)/sizeof(base_dir[0]));
		base_dir[len] = '\0';
		TCHAR *slash = wcsrchr(base_dir, '\\');
		*slash = '\0';
		wnd_hook = SetWindowsHookEx(WH_CALLWNDPROC, 
			(HOOKPROC)WndHook, my_instance, 0);
		gm_hook = SetWindowsHookEx(WH_GETMESSAGE, 
			(HOOKPROC)GMHook, my_instance, 0);

		if (mouse_sink) {
			mouse_hook = SetWindowsHookEx(WH_MOUSE,
				(HOOKPROC)MouseProc, my_instance, 0);
		}
	} else {
		if (wnd_hook) UnhookWindowsHookEx(wnd_hook);
		if (gm_hook) UnhookWindowsHookEx(gm_hook);
		if (mouse_hook) UnhookWindowsHookEx(mouse_hook);
	}
}

