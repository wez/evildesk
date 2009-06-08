/* Copyright (c) 2004-2009 Wez Furlong.
 * This source is provided under terms of the GPLv2.
 * See the file named LICENSE for full details */
#include "wezdesk.h"

/* I am a host for the 32-bit hook DLL.
 * I am only really supposed to run on win64, so that the menu
 * hooking can take place.
 */

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nShowCmd)
{
	void (*hook)(HINSTANCE, HWND, BOOL) = NULL;
	TCHAR me[MAX_PATH];
	DWORD len;
	HINSTANCE dll;
	TCHAR *slash;

	len = GetModuleFileName(NULL, me, sizeof(me)/sizeof(me[0]));
	me[len] = '\0';
	slash = wcsrchr(me, '\\');
	if (!slash) return 0;
	slash++;
	*slash = '\0';
	StringCbCat(me, sizeof(me), TEXT("wdmenu32.dll"));

	dll = LoadLibrary(me);
	if (!dll) return 0;

	hook = (void(*)(HINSTANCE, HWND, BOOL))GetProcAddress(dll, "hook");
	if (!hook) return 0;

	hook(dll, 0, 1);

	while (1) {
		SetProcessWorkingSetSize(GetCurrentProcess(), 0xffffffff, 0xffffffff);
		Sleep(30000);
	}

	hook(dll, 0, 0);
	return 0;
}

