/* Copyright (c) 2004-2009 Wez Furlong.
 * This source is provided under terms of the GPLv2.
 * See the file named LICENSE for full details */
#include "wezdesk.h"
#include <docobj.h>
#include <ShlGuid.h>
#include <vector>

using namespace std;
static void send_update(int action, NOTIFYICONDATA *nid, DWORD changemask);

/* keeps track of the notification area,
 * exposing the contents to other plugins */

static vector <class tray_item *> tray_items;
static HWND tray_window;


struct undocumented_tray_data {
	DWORD cookie;
	DWORD cmd;
	DWORD cbSize;
	DWORD hWnd; /* deliberately not an HWND; win64 passes 32-bits here */
	UINT uID;
	UINT uFlags;
	UINT uCallbackMessage;
	DWORD hIcon; /* deliberately not an HICON; win64 passes 32-bits here */
	WCHAR  szTip[128];
	DWORD dwState;
	DWORD dwStateMask;
	WCHAR  szInfo[256];
	union {
		UINT  uTimeout;
		UINT  uVersion;
	} DUMMYUNIONNAME;
	WCHAR  szInfoTitle[64];
	DWORD dwInfoFlags;
	GUID guidItem;
};

class tray_item {
public:
	~tray_item() {
		this->remove_from_list();
		this->destroy_icon();
		send_update(NIM_DELETE, &this->ni, 0);
	}

	void remove_from_list() {
		vector <class tray_item*>::iterator iter;

		for (iter = tray_items.begin(); iter != tray_items.end(); iter++) {
			if (*iter == this) {
				tray_items.erase(iter);
				break;
			}
		}
	}

	void destroy_icon() {
		if (this->ni.uFlags & NIF_ICON) {
			DestroyIcon(this->ni.hIcon);
			this->ni.uFlags &= ~NIF_ICON;
		}
	}

	NOTIFYICONDATA ni;
};

static void drop_zombies(void)
{
	PluginIterator piter;
	vector <class tray_item *>::iterator iter;
	class tray_item *item;

again:
	for (iter = tray_items.begin(); iter < tray_items.end(); iter++) {
		item = *iter;

		if (item->ni.cbSize && !IsWindow(item->ni.hWnd)) {
			delete item;
			tray_items.erase(iter);
			goto again;
		}
	}
}

static void send_update(int action, NOTIFYICONDATA *nid, DWORD changemask)
{
	PluginIterator piter;
#if 0
	vector <class tray_item *>::iterator iter;
	class tray_item *item;

again:
	for (iter = tray_items.begin(); iter < tray_items.end(); iter++) {
		item = *iter;

		if (item->ni.cbSize && !IsWindow(item->ni.hWnd)) {
			delete item;
			tray_items.erase(iter);
			goto again;
		}
		if (item->ni.cbSize && item->ni.hWnd == nid->hWnd) {
			MSG msg;
			memset(&msg, 0, sizeof(msg));
			msg.hwnd = item->ni.hWnd;
			msg.message = WM_MOUSEMOVE;
			msg.time = GetTickCount();
			PostMessage(item->ni.hWnd, item->ni.uCallbackMessage,
				item->ni.uID, (LPARAM)&msg);
		}
	}
#endif

	for (piter = plugins.begin(); piter != plugins.end(); piter++) {
		Plugin p = *piter;
		if (p->plugin && p->plugin->on_tray_change) {
			(p->plugin->on_tray_change)(p, &core_funcs, action, nid, changemask);
		}
	}
}



static class tray_item *find_icon(NOTIFYICONDATA *data, vector <class tray_item *>::iterator *p_iter) {
	vector <class tray_item *>::iterator iter;
	class tray_item *item;

	for (iter = tray_items.begin(); iter < tray_items.end(); iter++) {
		item = *iter;

		if (item->ni.hWnd == data->hWnd && item->ni.uID == data->uID) {
			if (p_iter)
				*p_iter = iter;
			return item;
		}
	}
	return NULL;
}

static BOOL add_or_update(NOTIFYICONDATA *nid, BOOL adding)
{
	class tray_item *item;
	DWORD changemask;
	
	changemask = nid->uFlags;

	item = find_icon(nid, NULL);

//debug_printf(TEXT("add_or_update item=%08x,adding=%d,wnd=%08x,id=%d,tip=%s\r\n\tinfo=%s\r\n"),
//		item, adding, nid->hWnd, nid->uID, nid->szTip, nid->szInfo);

	if (!item && !adding) {
		drop_zombies();
		return FALSE;
	}

	if (item && adding) {
		adding = FALSE;
		drop_zombies();
		return FALSE;
	}
	
	if (!item) {
		item = new class tray_item;
		memset(item, 0, sizeof(*item));
		tray_items.push_back(item);

		item->ni.uID = nid->uID;
		item->ni.hWnd = nid->hWnd;
	}
	
	int had_tip = item->ni.szTip[0] != '\0';
	
	if (nid->uFlags & NIF_ICON) {	
		item->destroy_icon();
		item->ni.hIcon = CopyIcon(nid->hIcon);
		item->ni.uFlags |= NIF_ICON;
	}
	if (nid->uFlags & NIF_MESSAGE) {
		item->ni.uCallbackMessage = nid->uCallbackMessage;
		item->ni.uFlags |= NIF_MESSAGE;
	}
	if (nid->uFlags & NIF_TIP) {
		StringCbCopy(item->ni.szTip, sizeof(item->ni.szTip), nid->szTip);
		item->ni.uFlags |= NIF_TIP;
	}

	if (nid->uFlags & NIF_STATE) {
		item->ni.dwState &= ~nid->dwStateMask;
		item->ni.dwState |= nid->dwState & nid->dwStateMask;
		item->ni.uFlags |= NIF_STATE;
	}
	if (nid->uFlags & NIF_INFO) {
		StringCbCopy(item->ni.szInfo, sizeof(item->ni.szInfo), nid->szInfo);
		item->ni.uTimeout = nid->uTimeout;
		StringCbCopy(item->ni.szInfoTitle, sizeof(item->ni.szInfoTitle), nid->szInfoTitle);
		item->ni.dwInfoFlags = nid->dwInfoFlags;
		item->ni.uFlags |= NIF_INFO;
	} else if (((nid->uFlags & NIF_TIP) == NIF_TIP) && (item->ni.szTip[0] == '\0') && had_tip) {
		PostMessage(item->ni.hWnd, item->ni.uCallbackMessage, item->ni.uID, WM_MOUSEMOVE);
	}

	drop_zombies();
	send_update(adding ? NIM_ADD : NIM_MODIFY, &item->ni, changemask);
#if 0

		PluginIterator piter;
		vector <class tray_item *>::iterator iter;
		class tray_item *nitem;
		int i;

		for (i = 0, iter = tray_items.begin(); iter < tray_items.end(); iter++) {
			nitem = *iter;

			if ((nitem->ni.uFlags & NIF_STATE) == NIF_STATE) {
				if ((nitem->ni.dwState & NIS_HIDDEN) == NIS_HIDDEN)
					continue;
			}

			if (item == nitem)
				break;

			i++;
		}

		for (piter = plugins.begin(); piter != plugins.end(); piter++) {
			Plugin p = *piter;
			if (p->plugin->on_tray_bubble) {
				(p->plugin->on_tray_bubble)(p, &core_funcs, i, &item->ni);
			}
		}
	}
#endif
	return TRUE;
}

static BOOL remove_icon(NOTIFYICONDATA *data, BOOL update_after)
{
	class tray_item *item;
	vector <class tray_item *>::iterator iter;

	item = find_icon(data, &iter);

	if (!item) {
		return FALSE;
	}

	if (update_after) {
		send_update(NIM_DELETE, &item->ni, data->uFlags);
	}

	delete item;
	drop_zombies();

	return TRUE;
}

static void hex_dump(void *data, int len)
{
	BYTE *b, *e;
	int i, j;

	b = (BYTE*)data;
	e = b + len;
	i = 0;
	while (b < e) {
		char c;

		debug_printf(TEXT(" %04x "), i);

		for (j = 0; j < 8; j++) {
			if (b + j >= e) break;
			debug_printf(TEXT(" %02x"), b[j]);
		}
		debug_printf(TEXT("   "));
		for (j = 0; j < 8; j++) {
			if (b + j >= e) break;
			i++;

			if (
					(*b >= 'a' && *b <= 'z') ||
					(*b >= 'A' && *b <= 'Z') ||
					(*b >= '0' && *b <= '9')
			   ) {
				c = *b;
			} else {
				c = '?';
			}

			debug_printf(TEXT("%c"), c);
			b++;
		}
		debug_printf(TEXT("\r\n"));
	}
	debug_printf(TEXT("\r\n"));

}

static void dump_nid(NOTIFYICONDATA *nid)
{
	debug_printf(TEXT("\r\ncbSize=%x\r\nhWnd=%p\r\nuID=%x\r\nuFlags=%x\r\nuCallbackMessage=%x\r\nhIcon=%p\r\nszTip=%s\r\ndwState=%08x\r\ndwStateMask=%08x\r\nszInfo=%s\r\nuVersion=%x\r\nszInfoTitle=%s\r\ndwInfoFlags=%08x\r\n\r\n"),
		nid->cbSize,
		nid->hWnd,
		nid->uID,
		nid->uFlags,
		nid->uCallbackMessage,
		nid->hIcon,
		nid->szTip,
		nid->dwState,
		nid->dwStateMask,
		nid->szInfo,
		nid->uVersion,
		nid->szInfoTitle,
		nid->dwInfoFlags);

}

static int marshall_nid(NOTIFYICONDATA *out, WPARAM wparam,
		const struct undocumented_tray_data *in)
{
	memset(out, 0, sizeof(*out));
	out->cbSize = sizeof(*out);

	out->hWnd = (HWND)in->hWnd;
	out->uID = in->uID;
	out->uFlags = in->uFlags;
	out->uCallbackMessage = in->uCallbackMessage;
	out->hIcon = (HICON)in->hIcon;
	StringCbCopy(out->szTip, sizeof(out->szTip), in->szTip);
	out->dwState = in->dwState;
	out->dwStateMask = in->dwStateMask;
	StringCbCopy(out->szInfo, sizeof(out->szInfo), in->szInfo);
	out->uVersion = in->uVersion;
	StringCbCopy(out->szInfoTitle, sizeof(out->szInfoTitle), in->szInfoTitle);
	out->dwInfoFlags = in->dwInfoFlags;

	//dump_nid(out);
	return 1;
}

static LRESULT CALLBACK tray_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (uMsg == WM_COPYDATA) {
		COPYDATASTRUCT *cpdata = (COPYDATASTRUCT*)lParam;

#if 0
		debug_printf(TEXT("WM_COPYDATA: dwData: %p, hwnd: %p cbData: %d (nid=%d)\r\n"),
			cpdata->dwData, wParam, 
			cpdata->cbData, sizeof(undocumented_tray_data));
#endif
		
		if (cpdata->dwData == 1) {
			struct undocumented_tray_data *data = (struct undocumented_tray_data*)cpdata->lpData;
			class tray_item *item;
			LRESULT ret = FALSE;
			NOTIFYICONDATA nid;

#if 0
			hex_dump(cpdata->lpData, cpdata->cbData);
			TCHAR *size_type = TEXT("?");
			if (data->nid.cbSize == NOTIFYICONDATAA_V1_SIZE)
				debug_printf(TEXT("A_V1\r\n"));

			if (data->nid.cbSize == NOTIFYICONDATAW_V1_SIZE)
				debug_printf(TEXT("W_V1\r\n"));

			if (data->nid.cbSize == NOTIFYICONDATAA_V2_SIZE)
				debug_printf(TEXT("A_V2\r\n"));

			if (data->nid.cbSize == NOTIFYICONDATAW_V2_SIZE)
				debug_printf(TEXT("W_V2\r\n"));

			if (data->nid.cbSize == sizeof(NOTIFYICONDATAW))
				debug_printf(TEXT("W_CURRENT\r\n"));

			if (data->nid.cbSize == sizeof(NOTIFYICONDATAA))
				debug_printf(TEXT("A_CURRENT\r\n"));

			debug_printf(TEXT("NIM size type %s, %d bytes [%d]\r\n"), size_type, data->nid.cbSize, sizeof(NOTIFYICONDATA));
			debug_printf(TEXT("copydata cookie is %d\r\n"), data->cookie);
#endif
			if (!marshall_nid(&nid, wParam, data)) return FALSE;

			switch (data->cmd) {
				case NIM_ADD:
					remove_icon(&nid, FALSE);
					return add_or_update(&nid, TRUE);

				case NIM_MODIFY:
					return add_or_update(&nid, FALSE);

				case NIM_DELETE:
					return remove_icon(&nid, TRUE);

				case NIM_SETFOCUS:
				case NIM_SETVERSION:
				default:
					return FALSE;
			}
		}
		return FALSE;			
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

int init_tray(void) {
	WNDCLASS wc;
	
	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = tray_proc;
	wc.hInstance = GetModuleHandle(NULL);
	wc.lpszClassName = TEXT("Shell_TrayWnd");

	if (!RegisterClass(&wc)) {
		return 0;
	}

	tray_window = CreateWindowEx(
			WS_EX_TOOLWINDOW,
			wc.lpszClassName,
			NULL,
			WS_POPUP,
			0, 0, 0, 0,
			NULL, NULL,
			wc.hInstance,
			NULL);

	if (!tray_window)
		return 0;
	
	/* let running apps know that a new tray is around */
	PostMessage(HWND_BROADCAST, RegisterWindowMessage(TEXT("TaskbarCreated")), 0, 0);

	return 1;	
}

void fini_tray(void) {
	while (tray_items.size()) {
		delete *(tray_items.begin());
	}
}

int tray_get_tray_data(NOTIFYICONDATA *buf)
{
	vector <class tray_item *>::iterator iter;
	class tray_item *item;

	item = find_icon(buf, &iter);
	if (item) {
		memcpy(buf, &item->ni, sizeof(item->ni));
		return 1;
	}
	return 0;
}

