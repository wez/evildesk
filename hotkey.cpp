/* Copyright (c) 2004-2009 Wez Furlong.
 * This source is provided under terms of the GPLv2.
 * See the file named LICENSE for full details */
#include "wezdesk.h"
#include <map>

class hotkey {
public:
	class hotkey *next;

	UINT mod, vk;
	int id;

	hotkey_func_t func;
	void *arg;
	hotkey_func_t dtor;
};

static hotkey *hotkeys = NULL;
static int next_id = 0;

static hotkey *resolve_hotkey(UINT mod, UINT vk)
{
	hotkey *k;

	for (k = hotkeys; k; k = k->next) {
		if (k->mod == mod && k->vk == vk) {
			return k;
		}
	}

	return NULL;
}

int handle_hotkey(UINT mod, UINT vk)
{
	hotkey *hk;
	HWND wnd;
	RECT r;
	PerWindow *w;

	hk = resolve_hotkey(mod, vk);
	if (!hk) return 0;

	wnd = GetForegroundWindow();
	if (is_app_window(wnd)) {
		active_workspace_context_window = wnd;
	} else {
		active_workspace_context_window = NULL;
	}
	
	SetForegroundWindow(core_funcs.GetRootWindow());

	hk->func(hk->arg, NULL);

	// We don't reset this to NULL here because the context menu might
	// not get dispatched until after we pop back out to the message
	// pump.
	//      active_workspace_context_window = NULL;
	
	if (GetForegroundWindow() == core_funcs.GetRootWindow()) {
		SetForegroundWindow(wnd);
	}

	return 1;
}

int register_hotkey(UINT mod, UINT vk, hotkey_func_t func, void *arg, hotkey_func_t dtor)
{
	hotkey *hk;

	hk = resolve_hotkey(mod, vk);
	if (hk) {
		/* change the values */
		goto copy;
	}

	/* create a new one */
	hk = new hotkey;
	hk->mod = mod;
	hk->vk = vk;
	hk->id = next_id++;

	hk->next = hotkeys;
	hotkeys = hk;

	RegisterHotKey(core_funcs.GetRootWindow(), hk->id, hk->mod, hk->vk);

copy:
	hk->dtor = dtor;
	hk->func = func;
	hk->arg = arg;
	return hk->id;
}

void unregister_hotkeys(void)
{
	hotkey *hk;

	while (hotkeys) {
		hk = hotkeys;
		hotkeys = hk->next;

		UnregisterHotKey(core_funcs.GetRootWindow(), hk->id);
		if (hk->dtor) {
			hk->dtor(hk->arg, NULL);
		}
		delete hk;
	}
}

int enum_hotkeys(int (*cb)(hotkey_func_t func, void *hotkeyarg, UINT mod, UINT vk, void *cookie), void *cookie)
{
	hotkey *hk;
	for (hk = hotkeys; hk; hk = hk->next) {
		if ((cb)(hk->func, hk->arg, hk->mod, hk->vk, cookie)) {
			return 1;
		}
	}
	return 0;
}

