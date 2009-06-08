/* Copyright (c) 2004-2009 Wez Furlong.
 * This source is provided under terms of the GPLv2.
 * See the file named LICENSE for full details */
//#include "wezdesk.h"
#define WINVER			0x501
#define _WIN32_WINNT	0x501
#define _WIN32_IE		0x600


#include "wezdesk.h"
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <vector>
#include <gdiplus.h>
#include <algorithm>
#include <math.h>

#define SPACING 24

using namespace std;
using namespace Gdiplus;

Image *bgtile;
Image *backbuffer;
Font *captionfont = NULL;
StringFormat *format, *emptyFormat;
SolidBrush *blackBrush;
SolidBrush *whiteBrush;
SolidBrush *greybrush;
Graphics *G;
ImageAttributes *greyscale;

double scale = 1.0;
struct item {
	int zorder;
	HWND w;
	RECT r;
	POINT center;
	double center_distance;
	TCHAR caption[64];
	RECT er;
	Image *capture;
};

vector <struct item *> task_list;
HWND wnd;
int hot_item = -1;
int nwindows = 0;

int screen_width;
int screen_height;

typedef void (*layout_algo)(void);

static inline void scale_rect(RECT &r)
{
	r.left /= scale;
	r.top /= scale;
	r.bottom /= scale;
	r.right /= scale;
}

ColorMatrix myColMat = {
		0.299, 0.299, 0.299, 0, 0,
		0.587, 0.587, 0.587, 0, 0,
		0.114, 0.114, 0.114, 0, 0,
		0, 0, 0, 1, 0,
		0, 0, 0, 0, 1};

static void paint_item(int i, int hot, HWND wnd)
{
	struct item *item;

	item = task_list[i];

	wprintf(L"drawing item %d: %d,%d,%d,%d %s\n", i, 
			item->er.left, item->er.top, item->er.right, item->er.bottom, item->caption);

	RECT r = item->er;

	G->FillRectangle(greybrush, Rect(r.left, r.top, RECTWIDTH(r), RECTHEIGHT(r)));

	if (hot) {
		G->DrawImage(item->capture, r.left, r.top);
		InflateRect(&r, -2, -2);

		RectF layoutRect(r.left, r.top, RECTWIDTH(r), RECTHEIGHT(r));
		RectF bounds;

		G->MeasureString(item->caption, lstrlen(item->caption), captionfont, layoutRect, format, &bounds);

		// Draw string.
		G->DrawString(
				item->caption,
				lstrlen(item->caption),
				captionfont,
				bounds,
				emptyFormat,
				blackBrush);

		bounds.X -= 1;
		bounds.Y -= 1;

		// Draw string.
		G->DrawString(
				item->caption,
				lstrlen(item->caption),
				captionfont,
				bounds,
				emptyFormat,
				whiteBrush);


	} else {
		G->DrawImage(item->capture, 
				RectF(r.left, r.top, RECTWIDTH(r), RECTHEIGHT(r)),
				0, 0, RECTWIDTH(r), RECTHEIGHT(r),
				UnitPixel, greyscale, NULL, NULL);

	}

	if (wnd) {
		InvalidateRect(wnd, &item->er, TRUE);
	}
}

static void update_back_buffer(void)
{
	int i;
	struct item *item;

	Graphics G(backbuffer);

printf("\n\n--------- update: hot=%d ----\n\n", hot_item);

//	G.SetInterpolationMode(InterpolationModeHighQualityBicubic);
	G.DrawImage(bgtile, 0, 0, screen_width, screen_height);
	//G.Clear(Color::MakeARGB(0xff, 80,80,80));

	G.SetTextRenderingHint(TextRenderingHintAntiAlias);

	for (i = 0; i < task_list.size(); i++) {
		paint_item(i, i == hot_item, 0);
	}
	printf("paint done\n");
}

static void on_paint(HDC hdc)
{
	Graphics G(hdc, NULL);
	G.DrawImage(backbuffer, 0, 0);
	printf("blit!\n");
}

/* calculates overall bounds and normalizes to top-left of display (probably needs fixing for multi-mon */
static void calc_union(RECT &b)
{
	int i;
	struct item *item;

	memset(&b, 0, sizeof(b));

	for (i = 0; i < task_list.size(); i++) {
		item = task_list[i];

		if (item->r.left < b.left)
			b.left = item->r.left;
		if (item->r.right > b.right)
			b.right = item->r.right;
		if (item->r.bottom > b.bottom)
			b.bottom = item->r.bottom;
		if (item->r.top < b.top)
			b.top = item->r.top;
	}

	/* normalize */
	for (i = 0; i < task_list.size(); i++) {
		item = task_list[i];
		OffsetRect(&item->r, -b.left, -b.top);
		printf("normalized [%s] to %d,%d %d,%d\n", item->caption, item->r.left, item->r.top, item->r.right, item->r.bottom);
	}
	OffsetRect(&b, -b.left, -b.top);
}

static int dofit(int n) 
{
	RECT overlap;
	int moved = 0;
	int i;
	struct item *iitem, *nitem;

	nitem = task_list[n];

	for (i = n+1; i < task_list.size(); i++) {
		iitem = task_list[i];
		
		if (IntersectRect(&overlap, &nitem->r, &iitem->r)) {
			printf("n[%d] overlaps with i[%d]:  %d,%d %d,%d\n", n, i, overlap.left, overlap.top, overlap.right, overlap.bottom);
			printf("i at %d,%d %d,%d\n", iitem->r.left, iitem->r.top, iitem->r.right, iitem->r.bottom);
			if (RECTWIDTH(overlap) < RECTHEIGHT(overlap)) {
				/* move horizontally */
				if (overlap.right >= nitem->r.left + (RECTWIDTH(nitem->r)/2)) {
					/* move down */
					OffsetRect(&iitem->r, RECTWIDTH(overlap), 0);
				} else {
					/* move up */
					OffsetRect(&iitem->r, -RECTWIDTH(overlap), 0);
				}
			} else {
				/* move vertically */
				if (overlap.top >= nitem->r.top + (RECTHEIGHT(nitem->r)/2)) {
					/* move down */
					OffsetRect(&iitem->r, 0, RECTHEIGHT(overlap));
				} else {
					/* move up */
					OffsetRect(&iitem->r, 0, -RECTHEIGHT(overlap));
				}
			}
			printf("i moved to %d,%d %d,%d\n", iitem->r.left, iitem->r.top, iitem->r.right, iitem->r.bottom);
			return 1;//moved = 1;
		}
	}
	
	return moved;
}

bool order_by_centerness(struct item *a, struct item *b)
{
	double diff = a->center_distance - b->center_distance;

	if (diff > 0) {
		return true;
	} else if (diff < 0) {
		return false;
	}

	/* same distance, higher z-order get priority */
	if (a->zorder < b->zorder) {
		return true;
	} else if (a->zorder > b->zorder) {
		return false;
	}

	return false;
}

static void fit(void)
{
	int moved;
	int i;
	int panic = 20;
	struct item *item;
	
	
	/* sort the rectangles so that the most central rectangle is first */
	sort(task_list.begin(), task_list.end(), order_by_centerness);

	/* now, place the first rectangle at the center */
	item = *task_list.begin();
	item->er.left = (screen_width + RECTWIDTH(item->r)) / 2;
	item->er.top = (screen_height + RECTHEIGHT(item->r)) / 2;

	/* for the next item */
	

	do {
		moved = 0;
		for (i = 0; i < task_list.size(); i++) {
			if (dofit(i))
				moved = 1;
		}
	} while (moved && --panic);

	/* now find max bounds for this lot */
	RECT b;
	calc_union(b);
	printf("normalized overall to %d,%d %d,%d\n", b.left, b.top, b.right, b.bottom);

	/* scale to fit display */

	int w = screen_width;
	int h = screen_height;

	int orig_w = b.right;
	
	double aspect = (double)b.right / (double)b.bottom;
	printf("aspect is %f\n", aspect);

	if (b.right > w) {
		b.right = w;
		b.bottom /= aspect;
	
		printf("adjusted to %d,%d %d,%d\n", b.left, b.top, b.right, b.bottom);
	}
	
	aspect = (double)b.right / (double)b.bottom;
	printf("aspect is %f\n", aspect);
	if (b.top > h) {
		b.bottom = h;
		b.right /= aspect;
		printf("adjusted to %d,%d %d,%d\n", b.left, b.top, b.right, b.bottom);
	}

	scale = (double)orig_w / (double)b.right;
	printf("scale is %f\n", scale);
}

static int IsToolWindow(HWND w)
{
	return ((GetWindowLong(w, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) == WS_EX_TOOLWINDOW);
}

int is_app_window(HWND w)
{
	TCHAR caption[128];
	if (IsWindowVisible(w)) {
	GetWindowText(w, caption, sizeof(caption)/sizeof(caption[0]));
	wprintf(TEXT("Considering [%08x] %s\n"), w, caption);

		HWND parent = GetParent(w);

		if (parent == 0) {
			HWND hWndOwner = (HWND)GetWindowLong(w, GWL_HWNDPARENT);
			if ((hWndOwner == 0) || IsToolWindow(hWndOwner)) {
				if (!IsToolWindow(w)) {
					return 1;
				}
			}
		} else if (!IsWindowVisible(parent)) {
			wprintf(TEXT("parent is %08x.  Desktop is %08x, owner is %08x\n"),
				GetParent(w), GetDesktopWindow(), GetWindowLong(w, GWL_HWNDPARENT));
			return 1;
		}
	}
	return 0;
}


static BOOL CALLBACK add_to_task_list(HWND hwnd, LPARAM lparam)
{
	if (is_app_window(hwnd)) {
		WINDOWPLACEMENT wp;
		struct item *item = new struct item;
		RECT r;

		item->zorder = nwindows++;

		if (IsIconic(hwnd)) {
			wp.length = sizeof(wp);
			GetWindowPlacement(hwnd, &wp);
			memcpy(&item->r, &wp.rcNormalPosition, sizeof(item->r));
		} else {
			GetWindowRect(hwnd, &item->r);
		}

		GetWindowText(hwnd, item->caption, sizeof(item->caption)/sizeof(item->caption[0]));

//		printf("%s\n normal %d,%d %d,%d\n rect   %d,%d %d,%d\n",
//			item->caption, item->r.left, item->r.top, item->r.right, item->r.bottom,
//			r.left, r.top, r.right, r.bottom);

		item->w = hwnd;

		/* determine the center point */
		item->center.x = item->r.left + (RECTWIDTH(item->r) / 2);
		item->center.y = item->r.top + (RECTHEIGHT(item->r) / 2);

		/* how far is that from the center of the screen? */
		double x = abs(item->center.x - (screen_width / 2));
		double y = abs(item->center.y - (screen_height / 2));
		item->center_distance = sqrt((x*x) + (y*y));

		/* default position mirrors that on the screen */
		item->er = item->r;
		
		task_list.push_back(item);
	}
	return TRUE;
}


static LRESULT CALLBACK wnd_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hWnd, &ps);
			on_paint(hdc);
			EndPaint(hWnd, &ps);
			break;
		}

		case WM_MOUSEMOVE:
		{
			int i;
			int was_hot = hot_item;
			POINT pt;
			RECT r;

			pt.x = LOWORD(lParam);
			pt.y = HIWORD(lParam);

			hot_item = -1;
			for (i = 0; i < task_list.size(); i++) {
				r = task_list[i]->er;

				if (PtInRect(&r, pt)) {
					hot_item = i;
					if (was_hot != i) {
						paint_item(i, 1, hWnd);
						if (was_hot >= 0) {
							paint_item(was_hot, 0, hWnd);
						}
					}
					UpdateWindow(hWnd);	
					return 0;
				}
			}
			if (was_hot >= 0 && hot_item == -1) {
				paint_item(was_hot, 0, hWnd);
			}
			UpdateWindow(hWnd);	
		}
		break;

	case WM_ERASEBKGND:
		return 1;

		case WM_LBUTTONUP:
		{
			int i;
			POINT pt;
			RECT r;

			pt.x = LOWORD(lParam);
			pt.y = HIWORD(lParam);

			for (i = 0; i < task_list.size(); i++) {
				r = task_list[i]->er;
				if (PtInRect(&r, pt)) {
					CloseWindow(hWnd);
					SetForegroundWindow(task_list[i]->w);
					break;
				}
			}

			PostQuitMessage(0);
			break;
		}

		default:
			return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	return 0;
}

bool order_by_zorder(struct item *a, struct item *b)
{
	if (a->zorder < b->zorder)
		return true;
	return false;
}

static void layout_by_zorder(void)
{
	double SW = screen_width - 16;
	double SH = screen_height - 16;
	int i;
	int x, y, h;
	struct item *item;

	sort(task_list.begin(), task_list.end(), order_by_zorder);
re_fit:
	x = SPACING;
	y = SPACING;
	h = 0;
	for (i = 0; i < task_list.size(); i++) {
		item = task_list[i];

		if (RECTWIDTH(item->r) + x >= SW) {
			/* too big to fit on this row, move to the next */
			x = SPACING;
			y += h + SPACING;
			h = 0;
		}
		if (RECTWIDTH(item->r) >= SW || RECTHEIGHT(item->r) + y >= SH) {
			/* still too big, then we need to increase the display area */
			SW *= 1.1;
			SH *= 1.1;
			goto re_fit;
		}

		item->er.left = x;
		item->er.top = y;
		item->er.right = x + RECTWIDTH(item->r);
		item->er.bottom = y + RECTHEIGHT(item->r);

		if (RECTHEIGHT(item->r) > h)
			h = RECTHEIGHT(item->r);

		x += RECTWIDTH(item->r) + SPACING;
	}

	scale = SW / screen_width;

	printf("Scale is %f (SWxSH=%f,%f)\n", scale, SW, SH);

}

static void capture_screens(HWND wnd)
{
	int i;
	struct item *item;
	RECT rect;

	for (i = 0; i < task_list.size(); i++) {
		item = task_list[i];
		scale_rect(item->er);

		RedrawWindow(item->w, NULL, NULL, RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
		InvalidateRect(item->w, NULL, TRUE);
		GetWindowRect(item->w, &rect);

		int w, h;
		double aspect;
		RECT pr = item->er;
		int prw = RECTWIDTH(pr);
		int prh = RECTHEIGHT(pr);

		if (RECTHEIGHT(rect) == 0) {
			aspect = 1;
		} else {
			aspect = (double)RECTWIDTH(rect) / (double)RECTHEIGHT(rect);
		}

		if (aspect > 1.0) {
			/* landscape */
			w = prw;
			h = w / aspect;

			if (h > prh) {
				double factor = (double)h / (double)prh;
				w /= factor;
				h = prh;
			}
		} else {
			h = prh;
			w = h * aspect;

			if (w > prw) {
				double factor = (double)w / (double)prw;
				h /= factor;
				w = prw;
			}
		}

		HDC memdc, windc;
		HBITMAP bm;
		HGDIOBJ old;
		DWORD result;

		memdc = CreateCompatibleDC(NULL);
		windc = GetDC(item->w);
		bm = CreateCompatibleBitmap(windc, RECTWIDTH(rect), RECTHEIGHT(rect));
		ReleaseDC(item->w, windc);

		old = SelectObject(memdc, bm);
		BOOL grabbed = PrintWindow(item->w, memdc, 0);
		SelectObject(memdc, old);
		DeleteObject(memdc);

		if (grabbed) {
			Bitmap *src = Bitmap::FromHBITMAP(bm, NULL);
			item->capture = new Bitmap(w, h);
			Graphics *graph = Graphics::FromImage(item->capture);

			graph->SetInterpolationMode(InterpolationModeHighQualityBicubic);
			graph->DrawImage(src, 0, 0, w, h);

			delete graph;
			delete src;
		}
		DeleteObject(bm);

		paint_item(i, 0, wnd);
		UpdateWindow(wnd);
	}
}

//int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
int main(int argc, char*argv[])
{
	MSG msg;
	HINSTANCE hInstance = GetModuleHandle(NULL);
	layout_algo algo = layout_by_zorder;
	GdiplusStartupInput	gdiinput;
	ULONG_PTR			gditok;
	TCHAR wallpaper[MAX_PATH];

	if (Ok != GdiplusStartup(&gditok, &gdiinput, NULL)) {
		return 1;
	}

	SystemParametersInfo(SPI_GETDESKWALLPAPER, sizeof(wallpaper)/sizeof(wallpaper[0]), wallpaper, 0);
	bgtile = new Image(wallpaper);

	screen_width = GetSystemMetrics(SM_CXFULLSCREEN);
	screen_height = GetSystemMetrics(SM_CYFULLSCREEN);

	backbuffer = new Bitmap(screen_width, screen_height);

	printf("WxH %dx%d\n", screen_width, screen_height);
	captionfont = new Font(L"Trebuchet MS", 16);
	format = new StringFormat;
	emptyFormat = new StringFormat;
	format->SetAlignment(StringAlignmentCenter);
	format->SetLineAlignment(StringAlignmentCenter);
	blackBrush = new SolidBrush(Color(255, 0, 0, 0));
	whiteBrush = new SolidBrush(Color(255, 255,255,255));
	greybrush = new SolidBrush(Color(255, 80, 80, 80));
	G = new Graphics(backbuffer);
	greyscale = new ImageAttributes;
	greyscale->SetColorMatrix(&myColMat);

	WNDCLASS wc;

	memset(&wc, 0, sizeof(wc));

	wc.lpfnWndProc = wnd_proc;
	wc.hInstance = hInstance;
	wc.lpszClassName = TEXT("WezDeskExpose");
	wc.hbrBackground = (HBRUSH)(1 + COLOR_APPWORKSPACE);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);

	if (!RegisterClass(&wc)) {
		return 1;
	}
	
	wnd = CreateWindowEx(
		WS_EX_TOOLWINDOW,
		wc.lpszClassName,
		NULL,
		WS_POPUP|WS_VISIBLE,
		0, 0,
		screen_width, screen_height,
		NULL, NULL,
		hInstance,
		NULL);
	
	update_back_buffer();
	UpdateWindow(wnd);	
	EnumWindows(add_to_task_list, NULL);
	
	algo();
	capture_screens(wnd);
//	update_back_buffer();
//	InvalidateRect(wnd, NULL, TRUE);
//	UpdateWindow(wnd);	

	while (GetMessage(&msg, 0, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	GdiplusShutdown(gditok);

	return msg.wParam;
}


