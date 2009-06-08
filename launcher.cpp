/* Copyright (c) 2004-2009 Wez Furlong.
 * This source is provided under terms of the GPLv2.
 * See the file named LICENSE for full details */
#include "wezdeskapi.h"
#include <commctrl.h>
#include <shlobj.h>
#include <vector>
#include <WinSafer.h>
#include <gdiplus.h>
#include <algorithm>
#define STRSAFE_LIB
#include <strsafe.h>
#include <shlwapi.h>
#include "wezdeskres.h"

using namespace std;
using namespace Gdiplus;

static int SHOW_ITEMS = 6;
static int LAUNCHER_WIDTH;
static WezDeskFuncs *f = NULL;

static TCHAR text[MAX_PATH];
static HANDLE include_pattern = NULL;
static HANDLE exclude_pattern = NULL;


typedef struct {
	WezDeskLaunchItem **items;
	int nitems;
	int nalloc;
} results_t;

static HWND switch_wnd, edit_wnd;
static Plugin me;
static HFONT edit_font = NULL;
static results_t *curr_results = NULL;
static HANDLE indexer_thread = NULL;
static Image *bgimage = NULL;
static COLORREF fgcol, shadowcol;
static int selected = 0;
static int last_rid = 0;

struct thread_start_data {
	TCHAR *text;
	int rid;
};

static IShellLink *get_link(WezDeskLaunchItem *i)
{
	HRESULT res;
	IShellLink *psl;

	res = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
			IID_IShellLink, (LPVOID*)&psl);
	if (SUCCEEDED(res)) {
		IPersistFile *ppf;

		res = psl->QueryInterface(IID_IPersistFile, (void**)&ppf);
		if (SUCCEEDED(res)) {
			res = ppf->Load(i->e.exec.command, STGM_READ);
			ppf->Release();
			if (SUCCEEDED(res)) {
				psl->Resolve(switch_wnd, 0);
				return psl;
			}
		}
		psl->Release();
	}
	return NULL;
}

static int is_same_link(WezDeskLaunchItem *a, WezDeskLaunchItem *b)
{
	IShellLink *psla = NULL, *pslb = NULL;
	TCHAR pa[MAX_PATH], pb[MAX_PATH];
	WIN32_FIND_DATA wfd;
	int ret = 0;

	if (a->e.exec.verb != b->e.exec.verb) goto out;
	if (a->e.exec.args != b->e.exec.args) goto out;

	if (!lstrcmpi(a->e.exec.command, b->e.exec.command)) {
		return 1;
	}

	psla = get_link(a);
	if (!psla) goto out;
	psla->GetPath(pa, sizeof(pa)/sizeof(pa[0]), &wfd, SLGP_UNCPRIORITY);

	pslb = get_link(b);
	if (!pslb) goto out;
	pslb->GetPath(pb, sizeof(pb)/sizeof(pb[0]), &wfd, SLGP_UNCPRIORITY);

	if (lstrcmpi(pa, pb)) goto out;

	ret = 1;
out:
	if (psla) psla->Release();
	if (pslb) pslb->Release();

	return ret;
}

void calc_item_weight(WezDeskLaunchItem *item) {
	int w = 0;
	TCHAR *dot;

	if (item->execType == WezDeskLaunchItem::WZDLI_EXECUTE
			&& !item->weighting) {
		/* if not already weighted, want shortcuts and executables
		 * to sort before other types of thing */
		dot = wcsrchr(item->e.exec.command, '.');
		if (dot) {
			if (!lstrcmpi(dot, TEXT(".lnk"))) {
				w -= 5;
			} else if (!lstrcmpi(dot, TEXT(".exe"))) {
				w -= 2;
			}
		}
	}
	
	if (item->execType == WezDeskLaunchItem::WZDLI_EXECUTE) {
		if (!lstrcmpi(item->e.exec.command, text)) {
			w -= 10;
		}
	}
	if (!lstrcmpi(item->displayText, text)) {
		w -= 10;
	}

	item->weighting += w;
}

static void free_item(WezDeskLaunchItem *i, int no_strings)
{
	if (!no_strings) {
		if (i->displayText) free(i->displayText);
		if (i->execType == WezDeskLaunchItem::WZDLI_EXECUTE) {
			if (i->e.exec.command) free(i->e.exec.command);
			if (i->e.exec.args) free(i->e.exec.args);
			if (i->e.exec.verb) free(i->e.exec.verb);
		}
	}
	if (i->imageType == WezDeskLaunchItem::WZDLI_HICON && i->i.hIcon) {
		DestroyIcon(i->i.hIcon);
	} else if (i->imageType == WezDeskLaunchItem::WZDLI_IMAGE && i->i.Image) {
		delete i->i.Image;
	}
	if (!no_strings) free(i);
}

static void add_to_results(void *cookie, WezDeskLaunchItem *newitem)
{
	results_t *r = (results_t*)cookie;
	int i;
	WezDeskLaunchItem *item;

	/* avoid duplicates */
	if (newitem->execType == WezDeskLaunchItem::WZDLI_EXECUTE) {
		for (i = 0; i < r->nitems; i++) {
			item = r->items[i];

			if (item->execType == WezDeskLaunchItem::WZDLI_EXECUTE &&
					is_same_link(item, newitem)) {
				free_item(newitem, 1);
				return;
			}
		}
	}
		
	item = (WezDeskLaunchItem*)malloc(sizeof(*item));
	memcpy(item, newitem, sizeof(*item));
	calc_item_weight(item);

	if (item->displayText) item->displayText = lstrdup(item->displayText);
	if (item->execType == WezDeskLaunchItem::WZDLI_EXECUTE) {
		if (item->e.exec.command) item->e.exec.command = lstrdup(item->e.exec.command);
		if (item->e.exec.args) item->e.exec.args = lstrdup(item->e.exec.args);
		if (item->e.exec.verb) item->e.exec.verb = lstrdup(item->e.exec.verb);
	}

	if (r->nitems + 1 >= r->nalloc) {
		int x = r->nalloc;
		WezDeskLaunchItem **items;
		if (x == 0) x = 16;
		else x *= 2;
		items = (WezDeskLaunchItem**)realloc(r->items, x * sizeof(item));
		if (!items) {
			free_item(item, 0);
			return;
		}
		r->nalloc = x;
		r->items = items;
	}

	r->items[r->nitems++] = item;
}

static void free_results(results_t *r)
{
	int i;
	for (i = 0; i < r->nitems; i++) {
		free_item(r->items[i], 0);
	}
	if (r->items) free(r->items);
	free(r);
}

static void crack_args(TCHAR *buf, TCHAR **cmdptr, TCHAR **argptr)
{
	TCHAR *ptr, *rtrim, *args = NULL, *cmd = NULL;

	*cmdptr = NULL;
	*argptr = NULL;

	cmd = buf;
	while (*cmd == ' ' || *cmd == '\t')
		cmd++;

	if (*cmd == '"') {
		cmd++;
		for (ptr = cmd; *ptr && *ptr != '"'; ptr++)
			;
		if (*ptr == '"') {
			*ptr = '\0';
			ptr++;
		}
	} else {
		ptr = cmd;
		while (*ptr) {
			if (*ptr == ' ' || *ptr == '\t') {
				break;
			}
			ptr++;
		}
	}

	if (*ptr == ' ' || *ptr == '\t') {
		*ptr = '\0';
		ptr++;
		while (*ptr == ' ' || *ptr == '\t') {
			ptr++;
		}
	}

	if (*ptr) args = ptr;

	*cmdptr = cmd;
	*argptr = args;
}

static void resolve_command_name(TCHAR *out, int outsize, TCHAR *cmd)
{
	StringCbCopy(out, outsize, cmd);
	if (PathFileExists(out)) return;

	TCHAR *dot = PathFindExtension(cmd);

	TCHAR the_path[4096];
	DWORD len = GetEnvironmentVariable(TEXT("PATH"), the_path,
			sizeof(the_path)/sizeof(the_path[0]));
	TCHAR *sep, *cur;
	TCHAR path_ext[1024];
	if (!GetEnvironmentVariable(TEXT("PATHEXT"), path_ext,
			sizeof(path_ext)/sizeof(path_ext[0]))) {
		StringCbCopy(path_ext, sizeof(path_ext), TEXT(".COM;.EXE;.BAT;.CMD"));
	}

	if (!len) return;
	cur = the_path;
	do {
		sep = wcschr(cur, ';');
		if (sep) {
			*sep = '\0';
		}
		while (*cur == ' ' || *cur == '\t') {
			cur++;
		}

		StringCbPrintf(out, outsize, TEXT("%s\\%s"), cur, cmd);
		if (PathFileExists(out)) return;
		if (!dot || !*dot) {
			/* try alternatives */
			TCHAR *ext, *semic;

			ext = path_ext;
			do {
				semic = wcschr(ext, ';');
				if (semic) {
					StringCbPrintf(out, outsize, TEXT("%s\\%s%.*s"),
						cur, cmd, semic - ext, ext);
					if (PathFileExists(out)) return;
				} else {
					StringCbPrintf(out, outsize, TEXT("%s\\%s%s"),
						cur, cmd, ext);
					if (PathFileExists(out)) return;
					break;
				}
				ext = semic + 1;
			} while (*ext);
		}
		
		if (!sep) break;
		cur = sep + 1;
	} while (cur && *cur);

}

static void paint(HDC hdc)
{
	TCHAR info[MAX_PATH];
	RECT r;
	int i;
	WezDeskLaunchItem *item;
	Graphics G(hdc);
	
	G.SetInterpolationMode(InterpolationModeHighQualityBicubic);
	GetClientRect(switch_wnd, &r);

	if (!bgimage) {
		TCHAR *name = f->GetPluginString(me, TEXT("Background.Image"), NULL);
		if (!name) {
			name = f->GetFilePath(TEXT("default.png"));
		}
		if (name) {
			bgimage = new Image(name);
			free(name);
		}

		name = f->GetPluginString(me, TEXT("Font"), NULL);
		if (name) {
			edit_font = f->LoadFont(hdc, name);
			free(name);
		} else {
			edit_font = f->GetStockFont(WEZDESK_FONT_TITLE);
		}

		fgcol = f->GetPluginInt(me, TEXT("Font.fg"), RGB(0xff,0xff,0xff));
		shadowcol = f->GetPluginInt(me, TEXT("Font.fg"), RGB(0x44,0x44,0x44));
	}

	G.DrawImage(bgimage, 0, 0, RECTWIDTH(r), RECTHEIGHT(r)+4);

	SetBkMode(hdc, TRANSPARENT);
	SelectObject(hdc, edit_font);

	r.left = 10;
	r.top = 10;

	f->LoadString(IDS_LAUNCHER_INSTRUCTION, info, sizeof(info)/sizeof(info[0]));

	DrawText(hdc, info, lstrlen(info),
		&r, DT_WORDBREAK|DT_CALCRECT);
	
	RECT v;
	GetWindowRect(edit_wnd, &v);
	if (v.left != 10 || v.top != r.bottom + 10) {
		SetWindowPos(edit_wnd, NULL, 10, r.bottom + 10, -1, -1,
			SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOZORDER);
	}

	f->DrawShadowText(hdc, info, lstrlen(info),
		&r, DT_WORDBREAK, fgcol, shadowcol, 4, 4);
	
	RECT er;
	SendMessage(edit_wnd, EM_GETRECT, 0, (LPARAM)&er);

	int top = r.bottom + er.bottom + 20;
	GetClientRect(switch_wnd, &r);

	if (curr_results) {
		i = curr_results->nitems;
	} else {
		i = 0;
	}
	if (i > SHOW_ITEMS) {
		i = SHOW_ITEMS;
	}

	GetWindowRect(switch_wnd, &v);
	if (RECTHEIGHT(v) != top + (i * 38) + 4) {
		SetWindowPos(switch_wnd, NULL, -1, -1, RECTWIDTH(r),
				top + (i * 38) + 4, SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOZORDER);
	}

	if (curr_results && curr_results->nitems) {
		int first_offset, last_offset, vpos;

		if (selected < SHOW_ITEMS/2) {
			/* falls into the top half */
			first_offset = 0;
			last_offset = first_offset + SHOW_ITEMS;
		} else if (selected > curr_results->nitems - (SHOW_ITEMS/2)) {
			/* falls into the bottom half */
			last_offset = curr_results->nitems;
			first_offset = last_offset - SHOW_ITEMS;
		} else {
			/* it's in the middle */
			first_offset = selected - SHOW_ITEMS/2;
			last_offset = first_offset + SHOW_ITEMS;
		}

		if (first_offset < 0) {
			first_offset = 0;
		}

		for (i = first_offset; i < last_offset && i < curr_results->nitems; i++) {
			item = curr_results->items[i];

			vpos = i - first_offset;

			r.top = top + (vpos * 38);
			r.bottom = r.top + 32;
			r.left = 32 + 16;
			r.right -= 16;
			
			StringCbPrintf(info, sizeof(info),
				TEXT("%s"),
					item->displayText,
					selected, first_offset, last_offset, vpos);

			f->DrawShadowText(hdc, info, lstrlen(info),
					&r, DT_WORDBREAK|DT_PATH_ELLIPSIS|
					DT_VCENTER|DT_SINGLELINE, fgcol, shadowcol, 4, 4);

			r.right += 16;

			if (item->imageType == WezDeskLaunchItem::WZDLI_HICON) {
				if (item->i.hIcon == 0) {
					SHFILEINFO info;
					TCHAR name[MAX_PATH];
					memset(&info, 0, sizeof(info));

					resolve_command_name(name, sizeof(name), item->e.exec.command);
					if (SHGetFileInfo(name, 0, &info, sizeof(info),
							SHGFI_ICON|SHGFI_LARGEICON)) {
						item->i.hIcon = info.hIcon;
					}
				}
				DrawIconEx(hdc, 10, r.top, item->i.hIcon, 32, 32, 0, NULL, DI_NORMAL);
			}

			if (i == selected) {
				r.top -= 2;
				r.left = 8;
				r.right -= 8;
				r.bottom += 2;
				FrameRect(hdc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
				r.bottom += 2;
			}
		}
	}
}

void store_mrulist(HKEY key, TCHAR *keyname, TCHAR *mrulist)
{
	DWORD result;

	RegSetValueEx(key, keyname, 0, REG_SZ,
			(BYTE*)mrulist, sizeof(TCHAR) * (lstrlen(mrulist)+1));
}

void launch(void)
{
	TCHAR info[MAX_PATH];
	RECT r;
	int i;
	WezDeskLaunchItem *item;
	TCHAR mrutext[2048];
	int add_mru = 0;

	if (curr_results && curr_results->nitems && selected < curr_results->nitems) {
		item = curr_results->items[selected];
		if (item->execType == WezDeskLaunchItem::WZDLI_EXECUTE) {
			f->Execute(GetDesktopWindow(), item->e.exec.verb,
					item->e.exec.command, item->e.exec.args,
					NULL, SW_SHOWNORMAL, 0);
		} else if (item->execType == WezDeskLaunchItem::WZDLI_FUNC) {
			item->e.func.execute(item->e.func.arg);
		}
		if (item->add_display_text_to_run_mru) {
			StringCbPrintf(mrutext, sizeof(mrutext), TEXT("%s\\1"),
					item->displayText);
			add_mru = 1;
		}
	} else if (lstrlen(text)) {
		TCHAR command_line_buffer[MAX_PATH];
		TCHAR *args = NULL, *cmd = NULL;
	
		StringCbCopy(command_line_buffer, sizeof(command_line_buffer), text);

		crack_args(command_line_buffer, &cmd, &args);
		
		f->Execute(GetDesktopWindow(), NULL, cmd, args, NULL, SW_SHOWNORMAL, 0);

		StringCbPrintf(mrutext, sizeof(mrutext), TEXT("%s\\1"), text);
		add_mru = 1;
	}
	DestroyWindow(switch_wnd);

	if (add_mru) {
		HKEY key;
		LONG res;

		res = RegOpenKeyEx(HKEY_CURRENT_USER,
				TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\RunMRU"),
				0, KEY_ALL_ACCESS, &key);
		if (res == ERROR_SUCCESS) {
			int i, m, n;
			TCHAR name[64];
			TCHAR value[2048];
			TCHAR mvalue[2048];
			DWORD namelen, valuelen;
			TCHAR mrulist[128];
			TCHAR newlist[128];
			int mlen;

			valuelen = sizeof(mrulist);
			res = RegQueryValueEx(key, TEXT("MRUList"), NULL, NULL,
					(LPBYTE)mrulist, &valuelen);

			if (res == ERROR_SUCCESS) {
				mlen = valuelen/sizeof(TCHAR);
				i = 0;

				for (i = 0; i < mlen; i++) {
					name[0] = mrulist[i];
					name[1] = '\0';

					valuelen = sizeof(value);	/* BYTES */
					res = RegQueryValueEx(key, name,
							NULL, NULL,
							(LPBYTE)value, &valuelen);

					if (res != ERROR_SUCCESS) {
						break;
					}
// bdeca
					/* if it matches, we need to bump this guy to the start
					 * of the list */
					if (!lstrcmp(value, mrutext)) {
						if (i == 0) {
							/* already the first */
							add_mru = 0;
							break;
						}

						/* abcdef
						 *   i
						 * to become
						 * cabdef
						 */

						for (n = i; n > 0; n--) {
							mrulist[n] = mrulist[n-1];
						}
						mrulist[0] = name[0];

						/* TODO: and now store it */
						store_mrulist(key, TEXT("MRUList"), mrulist);
						add_mru = 0;
						break;
					}
				}
				if (add_mru) {
					TCHAR newname[2];

					if (lstrlen(mrulist) == 26) {
						/* kick out the last guy */
						newname[0] = mrulist[25];
					} else {
						/* add a new guy */
						newname[0] = 'a' + lstrlen(mrulist);
						StringCbCat(mrulist, sizeof(mrulist), TEXT("0"));
					}
					newname[1] = '\0';
					/* and store this command in there */
					for (n = lstrlen(mrulist)-1; n > 0; n--) {
						mrulist[n] = mrulist[n-1];
					}
					mrulist[0] = newname[0];

					store_mrulist(key, TEXT("MRUList"), mrulist);
					store_mrulist(key, newname, mrutext);
				}
			}
			RegCloseKey(key);
		}
	}
}
static int compare_items(const void *A, const void *B)
{
	WezDeskLaunchItem *a = *(WezDeskLaunchItem**)A;
	WezDeskLaunchItem *b = *(WezDeskLaunchItem**)B;
	int w1 = a->weighting;
	int w2 = b->weighting;
	int r = w1 - w2;
	if (r == 0) {
		r = lstrcmp(a->displayText, b->displayText);
		if (r == 0) r = a->displayText < b->displayText;
	}
	return r;
}

void recalc_size(void)
{
	InvalidateRect(switch_wnd, NULL, TRUE);
	UpdateWindow(switch_wnd);
}

static DWORD WINAPI find_match(void *_data)
{
	struct thread_start_data *data = (struct thread_start_data*)_data;
	results_t *r = (results_t*)calloc(1, sizeof(*r));
	WezDeskLauncherTextRequest req;
	req.text = data->text;
	req.add = add_to_results;
	req.addCookie = (void*)r;
	f->SendNotify(WZN_ON_LAUNCHER_TEXT, 0, 0, (LPARAM)&req);
	qsort(r->items, r->nitems, sizeof(WezDeskLaunchItem*), compare_items);
	PostMessage(switch_wnd, WM_USER, data->rid, (LPARAM)r);
	free(data->text);
	free(data);
	return 0;
}

void kick_off_update(void)
{
	DWORD id;
	static int rid = 0;
	struct thread_start_data *data = (struct thread_start_data*)calloc(1, sizeof(*data));
	data->text = lstrdup(text);
	data->rid = rid++;
	HANDLE t = CreateThread(NULL, 0, find_match, data, 0, &id);
	CloseHandle(t);
}

void auto_complete(void)
{
#if 0
	if (curr_results && curr_results->nitems == 1 && lstrlen(text)) {
		if (!_wcsnicmp(text, curr_results->items[0]->displayText, lstrlen(text))) {
			SetWindowText(edit_wnd, curr_results->items[0]->displayText);
		}
	}
#endif
}

static LRESULT CALLBACK launcher_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);
			paint(hdc);
			EndPaint(hWnd, &ps);
			break;
		}

		case WM_HOTKEY:
			switch (wParam) {
				case 0:
					ShowWindow(hWnd, SW_SHOW);
			}
			return 1;

		case WM_KEYUP:
			switch (wParam) {
				case VK_ESCAPE:
					DestroyWindow(hWnd);
					return 0;
			}
			return DefWindowProc(hWnd, uMsg, wParam, lParam);

		case WM_ACTIVATE:
			SetFocus(edit_wnd);
			return 0;

		case WM_DESTROY:
			if (curr_results) free_results(curr_results);
			curr_results = NULL;
			return 0;

		case WM_USER: {
			/* ensure that we're looking at the latest results */
			if (wParam >= last_rid) {
				if (curr_results) free_results(curr_results);
				curr_results = (results_t*)lParam;
				last_rid = wParam;
				selected = 0;
				recalc_size();
				auto_complete();
			} else {
				/* discard out of sequence results */
				free_results((results_t*)lParam);
			}
			break;
		}

		case WM_COMMAND:
			if ((HWND)lParam == edit_wnd && HIWORD(wParam) == EN_CHANGE) {
				TCHAR new_text[MAX_PATH];
				GetWindowText(edit_wnd, new_text, sizeof(new_text)/sizeof(new_text[0]));
				if (lstrcmp(new_text, text)) {
					StringCbCopy(text, sizeof(text), new_text);
					kick_off_update();
				}
				return 0;
			}

		default:
			return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}

static LRESULT CALLBACK edit_proc(
	HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	switch (uMsg) {
		case WM_KEYUP:
			switch (wParam) {
				case VK_ESCAPE:
					DestroyWindow(switch_wnd);
					return 0;
				case VK_RETURN:
					launch();
					return 0;
				case VK_RIGHT:
					/* if we are at the end of the user text,
					 * and the selected item is a filesystem path,
					 * enter that path into the control.
					 * Otherwise, use the default right cursor
					 * behavior. */
					 if (curr_results && curr_results->nitems) {
						DWORD spos = 0, epos = 0;
						SendMessage(edit_wnd, EM_GETSEL, (WPARAM)&spos, (LPARAM)&epos);
						if (spos == epos && epos == GetWindowTextLength(edit_wnd)
								&& PathFileExists(
									curr_results->items[selected]->displayText)) {
							TCHAR path[MAX_PATH];
							StringCbCopy(path, sizeof(path),
								curr_results->items[selected]->displayText);
							if (PathIsDirectory(path)) {
								StringCbCat(path, sizeof(path), TEXT("\\"));
							}
							SetWindowText(edit_wnd, path);
							epos = lstrlen(path);
							SendMessage(edit_wnd, EM_SETSEL, epos, epos);
							return 0;
						}
					 }
					 break;
			}
			break;
		case WM_KEYDOWN:
			switch (wParam) {
				case VK_LEFT:
					/* if we are at the end of the user text,
					 * and the selected item is a filesystem path,
					 * popup up a level and enter that path into the control.
					 * Otherwise, use the default left cursor
					 * behavior. */
					 {
						DWORD spos = 0, epos = 0;
						TCHAR path[MAX_PATH];
						GetWindowText(edit_wnd, path, sizeof(path)/sizeof(path[0]));
						SendMessage(edit_wnd, EM_GETSEL, (WPARAM)&spos, (LPARAM)&epos);
						if (spos == epos && epos == lstrlen(path)
								&& PathFileExists(path)) {
							TCHAR *slash;
							slash = wcsrchr(path, '\\');
							if (slash) {
								if (slash[1] == '\0') {
									*slash = '\0';
									slash = wcsrchr(path, '\\');
									if (slash) {
										slash[1] = '\0';
									}
								} else {
									slash[1] = '\0';
								}
							}
							SetWindowText(edit_wnd, path);
							epos = lstrlen(path);
							SendMessage(edit_wnd, EM_SETSEL, epos, epos);
							return 0;
						}
					 }
					 break;

				case VK_UP:
				case VK_DOWN:
					if (curr_results) {
						if (wParam == VK_UP && selected) {
							selected--;
						} else if (wParam == VK_DOWN && 
								selected < curr_results->nitems - 1) {
							selected++;
						} else {
							return 0;
						}
						InvalidateRect(switch_wnd, NULL, TRUE);
					}
					return 0;
			}
			break;
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static void show_launcher(TCHAR *arg, HWND slit)
{
	LAUNCHER_WIDTH = GetSystemMetrics(SM_CXFULLSCREEN)/2;
	switch_wnd = CreateWindowEx(
		WS_EX_TOOLWINDOW|WS_EX_COMPOSITED|WS_EX_LAYERED,
		TEXT("WezDeskLauncher"),
		NULL,
		WS_POPUP,
		0, 0,
		LAUNCHER_WIDTH, 64,
		NULL, NULL,
		f->GetPluginInstance(me),
		NULL);

	edit_wnd = CreateWindow(TEXT("EDIT"), NULL,
		WS_CHILD|WS_VISIBLE, 10, 10, LAUNCHER_WIDTH-20, 32,
		switch_wnd, 0, f->GetPluginInstance(me),
		NULL);

	SendMessage(edit_wnd, WM_SETFONT, 
		(WPARAM)f->GetStockFont(WEZDESK_FONT_DEFAULT) , FALSE);
	RECT er;
	SendMessage(edit_wnd, EM_GETRECT, 0, (LPARAM)&er);
	MoveWindow(edit_wnd, 10, 10, LAUNCHER_WIDTH-20, RECTHEIGHT(er), TRUE);

	SetWindowSubclass(edit_wnd, edit_proc, 0, NULL);

	SetLayeredWindowAttributes(switch_wnd, 0, (90 * 255) / 100, LWA_ALPHA);

	SetWindowPos(switch_wnd, HWND_TOPMOST,
		0, 0,
		LAUNCHER_WIDTH, 64,
		SWP_SHOWWINDOW);
	text[0] = '\0';
	recalc_size();
	kick_off_update();
}

static DWORD WINAPI indexer(void *unused)
{
	f->SendNotify(WZN_ON_LAUNCHER_INDEX, 0, 0, 0);
	CloseHandle(indexer_thread);
	indexer_thread = NULL;
	return 0;
}

static void indexing_run(void *cookie)
{
	if (!indexer_thread) {
		DWORD id;
		indexer_thread = CreateThread(NULL, 0, indexer, NULL, 0, &id);
	}
	f->ScheduleCallBack(me, 10 * 60 * 1000, NULL, indexing_run);
}

static void *initialize(Plugin plugin, WezDeskFuncs *funcs, HWND slit) {
	WNDCLASS wc;

	f = funcs;
	me = plugin;

	memset(&wc, 0, sizeof(wc));

	wc.lpfnWndProc = launcher_proc;
	wc.hInstance = funcs->GetPluginInstance(plugin);
	wc.lpszClassName = TEXT("WezDeskLauncher");
	wc.hbrBackground = (HBRUSH)(1 + COLOR_APPWORKSPACE);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);

	if (!RegisterClass(&wc)) {
		return NULL;
	}

	funcs->DefineFunction(plugin, TEXT("show-launcher"), show_launcher);
	
	TCHAR path[MAX_PATH];
	f->GetPluginBlobPath(plugin, TEXT("execs"), path, sizeof(path));
	if (!PathFileExists(path)) {
		f->ScheduleCallBack(me, 60 * 1000, NULL, indexing_run);
	} else {
		f->ScheduleCallBack(me, 20 * 60 * 1000, NULL, indexing_run);
	}
	return plugin;
}

static int unload(Plugin plugin, WezDeskFuncs *funcs, int force)
{
	if (indexer_thread) {
		WaitForSingleObject(indexer_thread, INFINITE);
	}
	DestroyWindow(switch_wnd);
	return 1;
}

static void scan_dir(HANDLE h, TCHAR *dir, int exe_only)
{
	WIN32_FIND_DATA fd;
	HANDLE find;
	TCHAR pat[MAX_PATH];
	TCHAR full[MAX_PATH];

	if (!lstrlen(dir)) return;

	StringCbPrintf(pat, sizeof(pat), TEXT("%s\\*.*"), dir);

	find = FindFirstFile(pat, &fd);
	if (find != INVALID_HANDLE_VALUE) {
		do {
			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
					== FILE_ATTRIBUTE_HIDDEN) {
				continue;
			}
			StringCbPrintf(full, sizeof(full), TEXT("%s\\%s"),
				dir, fd.cFileName);

			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
					== FILE_ATTRIBUTE_DIRECTORY) {
				if (fd.cFileName[0] != '.') {
					scan_dir(h, full, exe_only);
				}
				continue;
			}

			TCHAR *dot = wcsrchr(fd.cFileName, '.');
			BOOL ok = 1;

			if (exe_only) {
				if (!lstrcmpi(dot, TEXT(".lnk"))
						|| !lstrcmpi(dot, TEXT(".exe"))
						|| !lstrcmpi(dot, TEXT(".cpl"))
						|| !lstrcmpi(dot, TEXT(".msc"))
						|| !lstrcmpi(dot, TEXT(".pif"))
						|| !lstrcmpi(dot, TEXT(".bat"))
						|| !lstrcmpi(dot, TEXT(".cmd"))
						|| !lstrcmpi(dot, TEXT(".url"))
				   ) {
					ok = 1;
				} else {
					ok = 0;
				}

			} else if (include_pattern) {
				ok = f->MatchRegexp(include_pattern, fd.cFileName);
			}

			if (ok && exclude_pattern && !f->MatchRegexp(exclude_pattern, fd.cFileName)) {
				ok = 0;
			}
			
			if (!ok) continue;

			SHFILEINFO info;
			memset(&info, 0, sizeof(info));
			if (SHGetFileInfo(full, 0, &info, sizeof(info),
					SHGFI_DISPLAYNAME)) {
				WORD plen, dlen;
				DWORD wrote;

				plen = lstrlen(full);
				dlen = lstrlen(info.szDisplayName);
				WriteFile(h, &plen, sizeof(plen), &wrote, NULL);
				WriteFile(h, &dlen, sizeof(dlen), &wrote, NULL);
				WriteFile(h, full, plen*sizeof(TCHAR), &wrote, NULL);
				WriteFile(h, info.szDisplayName, dlen*sizeof(TCHAR), &wrote, NULL);
			}
		} while (FindNextFile(find, &fd));
		FindClose(find);
	}
}

static void scan_path(HANDLE h)
{
	TCHAR the_path[4096];
	DWORD len = GetEnvironmentVariable(TEXT("PATH"), the_path,
			sizeof(the_path)/sizeof(the_path[0]));
	TCHAR *sep, *cur;

	if (!len) return;
	
	cur = the_path;
	do {
		sep = wcschr(cur, ';');
		if (sep) {
			*sep = '\0';
		}
		while (*cur == ' ' || *cur == '\t') {
			cur++;
		}
		scan_dir(h, cur, 1);
		if (!sep) break;
		cur = sep + 1;
	} while (cur && *cur);
}

static void scan_special(HANDLE h, DWORD csidl)
{
	TCHAR path[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPath(NULL, csidl, NULL, SHGFP_TYPE_CURRENT, path))) {
		scan_dir(h, path, 0);
	}
}

static void filesystem_suggest(WezDeskLauncherTextRequest *req)
{
	WezDeskLaunchItem item;
	TCHAR path[MAX_PATH], full[MAX_PATH], pat[MAX_PATH];
	TCHAR *ptr;
	TCHAR *basename;

	StringCbCopy(path, sizeof(path), req->text);
	ptr = wcsrchr(path, '\\');
	if (!ptr) return;
	*ptr = '\0';
	if (!PathFileExists(path)) return;

	/* we have a valid dir on disk, and some component of a file
	 * name.  Let's change it to a pattern and find matching files */
	*ptr = '\\';
	StringCbPrintf(pat, sizeof(pat), TEXT("%s*.*"), path);

	WIN32_FIND_DATA fd;
	HANDLE find;
	find = FindFirstFile(pat, &fd);

	if (find != INVALID_HANDLE_VALUE) {
		do {
			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
					== FILE_ATTRIBUTE_HIDDEN) {
				continue;
			}

			StringCbPrintf(full, sizeof(full), TEXT("%.*s%s"),
					1 + (ptr - path), path,
					fd.cFileName);

			memset(&item, 0, sizeof(item));
			item.displayText = full;
			item.execType = WezDeskLaunchItem::WZDLI_EXECUTE;
		
			SHFILEINFO info;
			memset(&info, 0, sizeof(info));

			if (SHGetFileInfo(full, 0, &info, sizeof(info),
							SHGFI_ICON|SHGFI_LARGEICON)) {
				item.i.hIcon = info.hIcon;
			}
	
			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
					== FILE_ATTRIBUTE_DIRECTORY) {
				if (!lstrcmp(fd.cFileName, TEXT("."))
						|| !lstrcmpi(fd.cFileName, TEXT(".."))) {
					continue;
				}
				item.e.exec.command = TEXT("explorer.exe");
				item.e.exec.args = full;
			} else {
				item.e.exec.command = full;
			}
			req->add(req->addCookie, &item);

		} while (FindNextFile(find, &fd));
		FindClose(find);
	}
}

static int on_notify(Plugin plugin, WezDeskFuncs *funcs, UINT code,
	UINT secondary, WPARAM wparam, LPARAM lparam)
{
	if (code == WZN_ON_LAUNCHER_TEXT) {
		WezDeskLauncherTextRequest *req = (WezDeskLauncherTextRequest*)lparam;
		WezDeskLaunchItem item;
		TCHAR text[MAX_PATH];

		StringCbCopy(text, sizeof(text), req->text);
		_wcslwr_s(text);

		if (lstrlen(text)) {
			HANDLE h = f->GetPluginBlobHandle(plugin, TEXT("execs"), 0);
			if (h) {
				WORD plen, dlen;
				DWORD r;
				TCHAR name[MAX_PATH];
				TCHAR disp[MAX_PATH];
				TCHAR lname[MAX_PATH];
				TCHAR ldisp[MAX_PATH];
				TCHAR *base;

				do {
					plen = dlen = 0;
					if (ReadFile(h, &plen, sizeof(plen), &r, NULL) && 
							r == sizeof(plen) &&
							ReadFile(h, &dlen, sizeof(dlen), &r, NULL) &&
							r == sizeof(dlen)) {
						if (dlen * sizeof(TCHAR) >= sizeof(name) ||
								plen * sizeof(TCHAR) >= sizeof(disp)) {
							break;
						}
						if (!ReadFile(h, name, plen * sizeof(TCHAR), &r, NULL) ||
								r != plen * sizeof(TCHAR) ||
								!ReadFile(h, disp, dlen * sizeof(TCHAR), &r, NULL) ||
								r != dlen * sizeof(TCHAR)
						   ) {
							break;
						}
						name[plen] = '\0';
						disp[dlen] = '\0';
						StringCbCopy(lname, sizeof(lname), name);
						StringCbCopy(ldisp, sizeof(ldisp), disp);
						_wcslwr_s(lname);
						base = wcsrchr(lname, '\\');
						if (base) base++;
						else base = lname;
						_wcslwr_s(ldisp);
						if (wcsstr(base, text) || wcsstr(ldisp, text)) {
							/* got a match! */
							memset(&item, 0, sizeof(item));
							item.execType = WezDeskLaunchItem::WZDLI_EXECUTE;
							item.e.exec.command = name;
							item.displayText = disp;
							req->add(req->addCookie, &item);
						}
					} else {
						break;
					}
				}
				while (1);
				CloseHandle(h);
			}
		}

		/* RunMRU registry entry too ? */
		HKEY key;
		LONG res;
		res = RegOpenKeyEx(HKEY_CURRENT_USER,
			TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\RunMRU"),
			0, KEY_READ, &key);
		if (res == ERROR_SUCCESS) {
			int i, m;
			TCHAR name[64];
			TCHAR value[2048];
			TCHAR mvalue[2048];
			TCHAR mrulist[128];
			DWORD namelen, valuelen;

			valuelen = sizeof(mrulist);
			res = RegQueryValueEx(key, TEXT("MRUList"),
					NULL, NULL, (LPBYTE)mrulist, &valuelen);
			if (res != ERROR_SUCCESS) {
				mrulist[0] = '\0';
			}

			i = 0;
			while (1) {
				namelen = sizeof(name) / sizeof(name[0]); /* TCHARS */
				valuelen = sizeof(value);	/* BYTES */

				res = RegEnumValue(key, i++, name, &namelen, 0, NULL,
						(LPBYTE)value, &valuelen);
				if (res != ERROR_SUCCESS) {
					break;
				}

				if (lstrlen(name) == 1 && name[0] >= 'a' && name[0] <= 'z') {
					TCHAR cmdbuf[2048];

					/* entries have \1 on the end, remove it */
					value[(valuelen/sizeof(TCHAR))-3] = '\0';

					StringCbCopy(mvalue, sizeof(mvalue), value);
					_wcslwr_s(mvalue);
					if (!lstrlen(text) || wcsstr(mvalue, text)) {
						StringCbCopy(cmdbuf, sizeof(cmdbuf), value);
						memset(&item, 0, sizeof(item));
						item.execType = WezDeskLaunchItem::WZDLI_EXECUTE;
						item.displayText = value;
						item.add_display_text_to_run_mru = 1;
						for (m = 0; mrulist[m] != '\0'; m++) {
							if (mrulist[m] == name[0]) {
								item.weighting = -(50 - m);
								break;
							}
						}
						crack_args(cmdbuf, &item.e.exec.command, &item.e.exec.args);
						req->add(req->addCookie, &item);
					}
				}
			}
			RegCloseKey(key);
		}

		/* suggest possibilities from the filesystem */
		filesystem_suggest(req);


	} else if (code == WZN_ON_LAUNCHER_INDEX) {
		TCHAR tmp[MAX_PATH];
		TCHAR path[MAX_PATH];
		HANDLE h;
		TCHAR *pat;
		
		if (!include_pattern) {
			pat = f->GetPluginString(me, TEXT("include_files"), NULL);
			if (!pat) {
				pat = lstrdup(TEXT("/\\.(pdf|doc|xls|txt|lnk|exe|msc|rdp|htm|html|url)$/i"));
			}
			if (pat) {
				include_pattern = f->CompileRegexp(pat);
				free(pat);
			}
		}
		if (!exclude_pattern) {
			pat = f->GetPluginString(me, TEXT("exclude_files"), NULL);
			if (pat) {
				exclude_pattern = f->CompileRegexp(pat);
				free(pat);
			}
		}

		f->GetPluginBlobPath(plugin, TEXT("execs"), path, sizeof(path));
		StringCbPrintf(tmp, sizeof(tmp), TEXT("%s.tmp"), path);
		h = CreateFile(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, NULL);
		scan_path(h);
		scan_special(h, CSIDL_STARTMENU);
		scan_special(h, CSIDL_COMMON_STARTMENU);
		scan_special(h, CSIDL_PERSONAL);
		scan_special(h, CSIDL_COMMON_DOCUMENTS);
		scan_special(h, CSIDL_ADMINTOOLS);
		scan_special(h, CSIDL_COMMON_ADMINTOOLS);
		scan_special(h, CSIDL_DESKTOPDIRECTORY);
		scan_special(h, CSIDL_CONTROLS);
		scan_special(h, CSIDL_FAVORITES);
		CloseHandle(h);
		DeleteFile(path);
		MoveFile(tmp, path);
	}
	return 0;
}

static WezDeskPlugin the_plugin = {
	WEZDESK_API_NUMBER,
	TEXT("Launcher"),
	TEXT("Smart app launcher"),
	TEXT("BSD"),
	TEXT("Wez Furlong <wez@php.net>"),
	TEXT("http://netevil.org/wiki.php?WezDesk"),
	TEXT("0.1"),
	initialize,
	NULL, /* unload ok */
	unload, /* unload */
	NULL, /* veto */
	NULL, /* shell msg */
	NULL, /* tray */
	on_notify
};

GET_PLUGIN(the_plugin);

