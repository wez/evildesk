/* Copyright (c) 2004-2009 Wez Furlong.
 * This source is provided under terms of the GPLv2.
 * See the file named LICENSE for full details */
#define WEZDESK_SUPRESS_MANIFEST 1
#include "wezdeskapi.h"
#include "wezdesk-int.h"

#pragma section(".shared",read,write,shared)
#define SHARED	__declspec(allocate(".shared"))
SHARED HHOOK		mouse_hook;
SHARED HINSTANCE	my_instance;
SHARED HWND			hook_sink;

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
					SetForegroundWindow(hook_sink);
				}
				break;
			case WM_RBUTTONUP:
				if (pmhStruct->hwnd == GetDesktopWindow()) {
					PostMessage(hook_sink, WZDM_DESKTOP_MOUSE, wParam,
						pmhStruct->pt.x | (pmhStruct->pt.y << 16));
				}
				break;
		}
	}
	return CallNextHookEx(mouse_hook, nCode, wParam, lParam);
}

static void *initialize(Plugin plugin, WezDeskFuncs *funcs, HWND slit) {
	hook_sink = funcs->GetRootWindow();
	mouse_hook = SetWindowsHookEx(WH_MOUSE, (HOOKPROC)MouseProc, my_instance, 0);
	return (void*)mouse_hook;
}

static int unload(Plugin plugin, WezDeskFuncs *funcs, int force)
{
	UnhookWindowsHookEx(mouse_hook);
	return 1;
}

static WezDeskPlugin the_plugin = {
	WEZDESK_API_NUMBER,
	TEXT("Hooks"),
	TEXT("hooks to make things nicer for you to use"),
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

BOOL WINAPI DllMain(HINSTANCE hInstDll, DWORD reason, LPVOID res)
{
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(hInstDll);
		my_instance = hInstDll;
	}
	return TRUE;
}

