/* Copyright (c) 2004-2009 Wez Furlong.
 * This source is provided under terms of the GPLv2.
 * See the file named LICENSE for full details */
#include "wezdeskapi.h"
#include "wezdeskres.h"
#define STRSAFE_LIB
#include <strsafe.h>


static WezDeskFuncs *f;
static Plugin my_plugin;
static HKEY mykey;

static int unload(Plugin plugin, WezDeskFuncs *funcs, int force)
{
	return 1;
}

static void free_putty(void *arg)
{
	if (arg) free(arg);
}
	
static void execute_putty(void *arg)
{
	TCHAR *putty;
	TCHAR command[MAX_PATH];
	TCHAR args[MAX_PATH];

	putty = f->GetPluginString(my_plugin, TEXT("putty.exe"), 
#ifdef _WIN64
		TEXT("C:\\Program Files (x86)\\PuTTY\\putty.exe")
#else
		TEXT("C:\\Program Files\\PuTTY\\putty.exe")
#endif
		);

	if (arg)
		StringCbPrintf(args, sizeof(args), TEXT("@%s"), (TCHAR*)arg);
	else
		args[0] = '\0';

	StringCbPrintf(command, sizeof(command), TEXT("\"%s\""), putty);
	free(putty);

	f->Execute(f->GetRootWindow(), TEXT("open"), command, args, NULL, SW_SHOWDEFAULT, 0);
}

static int unhex(TCHAR c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return 0;
}

static void do_popup(Plugin plugin, WezDeskFuncs *funcs, HMENU parent, HMENU menu)
{
	HKEY key;
	DWORD i;
	WCHAR name[256];
	DWORD namelen;
	FILETIME wtime;
	int j, k;


	if (SUCCEEDED(RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\SimonTatham\\PuTTY\\Sessions"),
					0, KEY_READ, &key))) {
		
		TCHAR scratch[256];
		funcs->LoadString(IDS_PUTTY_NEW_SESSION, scratch,
				sizeof(scratch)/sizeof(scratch[0]));

		LPWSTR name_dup = f->GetPluginString(my_plugin, TEXT("New.Session"),
								scratch);
		funcs->AddCallbackMenuItem(menu, name_dup, execute_putty, NULL, free_putty);

		for (i = 0;
				namelen = sizeof(name)/sizeof(name[0]),
				ERROR_SUCCESS == RegEnumKeyEx(key, i, name, &namelen, NULL, NULL, NULL, &wtime);
				i++) {

			if (!lstrcmp(TEXT("Default%20Settings"), name)) continue;

			name_dup = (LPWSTR)malloc((namelen+1) * sizeof(name[0]));

			for (j = 0, k = 0; k < namelen; ) {
				if (name[k] == '%') {
					unsigned c;
					name_dup[j++] = (unhex(name[k+1]) << 4) | (unhex(name[k+2]));
					k += 3;
				} else {
					name_dup[j++] = name[k++];
				}
			}
			name_dup[j] = '\0';
			
			funcs->AddCallbackMenuItem(menu, name_dup, execute_putty, name_dup, free_putty);
		}

		RegCloseKey(key);
	}
}

static void *initialize(Plugin plugin, WezDeskFuncs *funcs, HWND slit) {
	f = funcs;
	my_plugin = plugin;

	mykey = f->GetPluginDataStore(plugin);
	f->RegisterPluginContextMenu(plugin, TEXT("putty"), do_popup);
	return (void*)1;
}

static WezDeskPlugin the_plugin = {
	WEZDESK_API_NUMBER,
	TEXT("Putty Launcher"),
	TEXT("Adds putty sessions to the slit menu"),
	TEXT("BSD"),
	TEXT("Wez Furlong <wez@php.net>"),
	TEXT("http://netevil.org/wiki.php?WezDesk"),
	TEXT("0.1"),
	initialize,
	NULL, /* unload ok */
	unload, /* unload */
	NULL, /* veto */
	NULL, /* on_shell_message */
	NULL, /* on_tray_change */
	NULL /* notify */
};

GET_PLUGIN(the_plugin);

