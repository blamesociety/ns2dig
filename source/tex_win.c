/* Texture viewer window: the textures in a BNTX file (loose, or inside an archive), from the game dump.
   List with a name filter, image with zoom/pan and a choice of background, PNG export (one or all). */
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
void UiSkinWindow(HWND top);
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdint.h>
#include <wchar.h>
#include "tex_core.h"
#define COBJMACROS
#include <objbase.h>
#include <wincodec.h>

// JPEG / PNG (e.g. Mario Kart World control\icon_<language>.dat) -> "NSDIGIMG" + width + height + RGBA, using
// Windows' own picture decoder (WIC). NULL if it can't be decoded.
static const GUID kClsWicFactory = { 0xcacaf262, 0x9370, 0x4615, { 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a } };
static const GUID kIidWicFactory = { 0xec5ec8a9, 0xc395, 0x4314, { 0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70 } };
static const GUID kWicRGBA32 = { 0xf5c7ad2d, 0x6a8d, 0x43dd, { 0xa7, 0xa8, 0xa2, 0x99, 0x35, 0x26, 0x1a, 0xe9 } };
static unsigned char *TvPictureToImg(const unsigned char *b, size_t n, size_t *outN, char *err, size_t errn) {
    HRESULT ci = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    IWICImagingFactory *f = NULL; IWICStream *st = NULL; IWICBitmapDecoder *dec = NULL; IWICBitmapFrameDecode *fr = NULL;
    IWICFormatConverter *cv = NULL; unsigned char *out = NULL; UINT w = 0, h = 0;
    if (FAILED(CoCreateInstance(&kClsWicFactory, NULL, CLSCTX_INPROC_SERVER, &kIidWicFactory, (void **)&f))) goto done;
    if (FAILED(IWICImagingFactory_CreateStream(f, &st))) goto done;
    if (FAILED(IWICStream_InitializeFromMemory(st, (BYTE *)b, (DWORD)n))) goto done;
    if (FAILED(IWICImagingFactory_CreateDecoderFromStream(f, (IStream *)st, NULL, WICDecodeMetadataCacheOnDemand, &dec))) goto done;
    if (FAILED(IWICBitmapDecoder_GetFrame(dec, 0, &fr))) goto done;
    if (FAILED(IWICImagingFactory_CreateFormatConverter(f, &cv))) goto done;
    if (FAILED(IWICFormatConverter_Initialize(cv, (IWICBitmapSource *)fr, &kWicRGBA32, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom))) goto done;
    if (FAILED(IWICFormatConverter_GetSize(cv, &w, &h)) || !w || !h || w > 16384 || h > 16384) goto done;
    out = (unsigned char *)malloc(16 + (size_t)w * h * 4);
    if (!out) goto done;
    memcpy(out, "NSDIGIMG", 8);
    for (int i = 0; i < 4; i++) { out[8 + i] = (unsigned char)(w >> (8 * i)); out[12 + i] = (unsigned char)(h >> (8 * i)); }
    if (FAILED(IWICFormatConverter_CopyPixels(cv, NULL, w * 4, w * h * 4, out + 16))) { free(out); out = NULL; goto done; }
    *outN = 16 + (size_t)w * h * 4;
done:
    if (cv) IWICFormatConverter_Release(cv);
    if (fr) IWICBitmapFrameDecode_Release(fr);
    if (dec) IWICBitmapDecoder_Release(dec);
    if (st) IWICStream_Release(st);
    if (f) IWICImagingFactory_Release(f);
    if (SUCCEEDED(ci)) CoUninitialize();
    if (!out) snprintf(err, errn, "This picture couldn't be decoded.");
    return out;
}

static void *xalloc(size_t n) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n); }
static void xfree(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }
#include "deflate_lite.h"

#define TV_CLASS L"AlsarcTexView"
#define TV_IMG_CLASS L"AlsarcTexImg"
enum { TV_LIST = 100, TV_FILTER, TV_SAVE, TV_SAVEALL, TV_BG, TV_FIT, TV_INFO, TV_COUNT, TV_IMG };
#define WM_TV_LOADED  (WM_APP + 40)
#define WM_TV_SAVEPROG (WM_APP + 41)
#define WM_TV_SAVEDONE (WM_APP + 42)

typedef struct {
    HWND wnd, list, filter, save, saveall, bg, fit, info, count, img;
    HFONT font; int dpi;
    wchar_t path[2048], title[600]; char inner[600];
    unsigned char *file; size_t fileN;          // the BNTX bytes
    TexFile tf; int loaded;
    int *shown; int nshown;                     // list rows -> texture index
    int cur;                                    // texture shown (-1 none)
    unsigned char *rgba;                        // decoded current texture
    unsigned char *disp;                        // BGRA composited onto the background, for drawing
    int bgMode;                                 // 0 checker, 1 black, 2 white, 3 grey
    double zoom; double panX, panY; int fitMode; // fitMode: zoom follows the window
    int dragging; POINT dragFrom; double panFrom[2];
    char err[300];
    volatile LONG cancelSave; HANDLE saveThread; int saving;
} TV;

static int S_(TV *v, int x) { return MulDiv(x, v->dpi, 96); }

#include "arc_util.h"
#include "terrain_core.h"
#define TvReadAll ArcReadAll
#define TvUnzstd ArcUnzstd
#define TvSarcMember ArcSarcMember

// read the texture container from the dump (decompressed, taken out of its archive, terrain/XTX turned into
// RGBA, BFRES -> embedded BNTX). Returns malloc'd bytes or NULL with err set.
static unsigned char *TvLoadBytes(const wchar_t *path, const char *inner, size_t *outN, char *tdesc, size_t tdn, char *err, size_t errn) {
    size_t n; tdesc[0] = 0;
    unsigned char *b = TvReadAll(path, &n);
    if (!b) { snprintf(err, errn, "The file couldn't be read from the game dump."); return NULL; }
    b = TvUnzstd(b, n, &n);
    if (!b) { snprintf(err, errn, "The file couldn't be decompressed."); return NULL; }
    if (inner && inner[0]) {
        size_t mn; unsigned char *m = TvSarcMember(b, n, inner, &mn);
        free(b);
        if (!m) { snprintf(err, errn, "%s wasn't found inside the archive.", inner); return NULL; }
        b = m; n = mn;
    }
    if (n > 0x14 && (!memcmp(b, "TARB", 4) || !memcmp(b, "DFvN", 4))) {   // terrain tile layer / plain NVN texture -> square RGBA
        int side = 0; char e2[200] = "";
        unsigned char *img = !memcmp(b, "TARB", 4) ? TarbToRGBA(b, n, &side, tdesc, tdn, e2, sizeof e2)
                                                   : XtxToRGBA(b, n, &side, e2, sizeof e2);
        free(b);
        if (!img) { snprintf(err, errn, "%s", e2); return NULL; }
        if (!tdesc[0]) snprintf(tdesc, tdn, "debug font");
        b = img; n = (size_t)side * side * 4;
    }
    if (n > 8 && (!memcmp(b, "\xff\xd8\xff", 3) || !memcmp(b, "\x89PNG\r\n\x1a\n", 8))) {   // JPEG / PNG picture
        size_t pn = 0; char e2[200] = "";
        unsigned char *img = TvPictureToImg(b, n, &pn, e2, sizeof e2);
        snprintf(tdesc, tdn, "%s picture", b[0] == 0xff ? "JPEG" : "PNG");
        free(b);
        if (!img) { snprintf(err, errn, "%s", e2); return NULL; }
        b = img; n = pn;
    }
    if (n > 0x20 && !memcmp(b, "FRES", 4)) {       // a BFRES with its textures inside (Env\*.genvres): use the embedded BNTX
        size_t o = 0x10; while (o + 8 <= n && memcmp(b + o, "BNTX\0\0\0\0", 8)) o += 4;
        if (o + 8 > n) { free(b); snprintf(err, errn, "This file has no textures inside."); return NULL; }
        memmove(b, b + o, n - o); n -= o;
    }
    *outN = n;
    return b;
}

static DWORD WINAPI TvLoadThread(LPVOID p) {
    TV *v = (TV *)p; size_t n = 0;
    char tdesc[200] = "";
    unsigned char *b = TvLoadBytes(v->path, v->inner, &n, tdesc, sizeof tdesc, v->err, sizeof v->err);
    if (!b) { PostMessageW(v->wnd, WM_TV_LOADED, 0, 0); return 0; }
    v->file = b; v->fileN = n;
    int ok = TexOpen(&v->tf, b, n);
    if (!ok) snprintf(v->err, sizeof v->err, "%s", v->tf.err);
    else if (tdesc[0] && v->tf.n) snprintf(v->tf.t[0].name, sizeof v->tf.t[0].name, "%d x %d - %s", v->tf.t[0].w, v->tf.t[0].h, tdesc);
    PostMessageW(v->wnd, WM_TV_LOADED, 0, ok);
    return 0;
}

// Small preview for the main window: the first texture, scaled to fit boxW x boxH (never enlarged beyond 4x),
// as premultiplied BGRA. *count gets the number of textures in the file. NULL if it can't be made.
unsigned char *TexThumbnail(const wchar_t *dumpFile, const char *inner, int boxW, int boxH, int *ow, int *oh, int *count, int *srcW, int *srcH) {
    size_t n = 0; char tdesc[200], err[300];
    unsigned char *b = TvLoadBytes(dumpFile, inner, &n, tdesc, sizeof tdesc, err, sizeof err);
    if (!b) return NULL;
    TexFile tf; memset(&tf, 0, sizeof tf);
    unsigned char *out = NULL;
    if (TexOpen(&tf, b, n) && tf.n > 0) {
        const TexInfo *t = &tf.t[0];
        unsigned char *rgba = TexDecode(b, n, t, tf.ounce, err, sizeof err);
        if (rgba && t->w > 0 && t->h > 0) {
            double sc = (double)boxW / t->w; if ((double)boxH / t->h < sc) sc = (double)boxH / t->h;
            if (sc > 4) sc = 4;
            int w = (int)(t->w * sc + 0.5), h = (int)(t->h * sc + 0.5); if (w < 1) w = 1; if (h < 1) h = 1;
            out = (unsigned char *)malloc((size_t)w * h * 4);
            if (out) for (int y = 0; y < h; y++) {        // box filter over the source pixels each output pixel covers
                int y0 = (int)((double)y * t->h / h), y1 = (int)((double)(y + 1) * t->h / h); if (y1 <= y0) y1 = y0 + 1;
                for (int x = 0; x < w; x++) {
                    int x0 = (int)((double)x * t->w / w), x1 = (int)((double)(x + 1) * t->w / w); if (x1 <= x0) x1 = x0 + 1;
                    unsigned long long r = 0, g = 0, bl = 0, a = 0, c = 0;
                    int stepy = (y1 - y0 + 7) / 8, stepx = (x1 - x0 + 7) / 8;   // at most 8 x 8 samples per pixel
                    for (int sy = y0; sy < y1 && sy < t->h; sy += stepy) for (int sx = x0; sx < x1 && sx < t->w; sx += stepx) {
                        const unsigned char *s = rgba + ((size_t)sy * t->w + sx) * 4;
                        r += s[0] * s[3]; g += s[1] * s[3]; bl += s[2] * s[3]; a += s[3]; c++;
                    }
                    unsigned char *d = out + ((size_t)y * w + x) * 4;
                    d[0] = (unsigned char)(bl / (c * 255)); d[1] = (unsigned char)(g / (c * 255)); d[2] = (unsigned char)(r / (c * 255)); d[3] = (unsigned char)(a / c);
                }
            }
            *ow = w; *oh = h; *srcW = t->w; *srcH = t->h; *count = tf.n;
        }
        free(rgba);
    }
    TexClose(&tf); free(b);
    return out;
}

/* ---------------- PNG ---------------- */
static int TvWritePng(const wchar_t *path, const unsigned char *rgba, int w, int h) {
    static int crcReady = 0; if (!crcReady) { CrcInit(); crcReady = 1; }
    size_t rawN = (size_t)(w * 4 + 1) * h;
    unsigned char *raw = (unsigned char *)malloc(rawN);
    if (!raw) return 0;
    for (int y = 0; y < h; y++) { raw[(size_t)y * (w * 4 + 1)] = 0; memcpy(raw + (size_t)y * (w * 4 + 1) + 1, rgba + (size_t)y * w * 4, (size_t)w * 4); }
    size_t dn = 0; unsigned char *def = DeflateBuf(raw, rawN, &dn);
    uint32_t a1 = 1, a2 = 0;
    for (size_t i = 0; i < rawN; i++) { a1 = (a1 + raw[i]) % 65521; a2 = (a2 + a1) % 65521; }
    free(raw);
    if (!def) return 0;
    FILE *f = _wfopen(path, L"wb");
    if (!f) { xfree(def); return 0; }
    static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);
    #define PUT32(v) { unsigned char q_[4] = { (unsigned char)((v) >> 24), (unsigned char)((v) >> 16), (unsigned char)((v) >> 8), (unsigned char)(v) }; fwrite(q_, 1, 4, f); }
    unsigned char ih[17] = { 'I','H','D','R', (unsigned char)(w >> 24), (unsigned char)(w >> 16), (unsigned char)(w >> 8), (unsigned char)w,
                             (unsigned char)(h >> 24), (unsigned char)(h >> 16), (unsigned char)(h >> 8), (unsigned char)h, 8, 6, 0, 0, 0 };
    PUT32(13); fwrite(ih, 1, 17, f); PUT32(Crc32(ih, 17));
    unsigned char *z = (unsigned char *)malloc(dn + 10);      // "IDAT" + zlib (header, deflate, adler-32)
    if (z) {
        memcpy(z, "IDAT", 4); z[4] = 0x78; z[5] = 0x01; memcpy(z + 6, def, dn);
        z[6 + dn] = (unsigned char)(a2 >> 8); z[7 + dn] = (unsigned char)a2; z[8 + dn] = (unsigned char)(a1 >> 8); z[9 + dn] = (unsigned char)a1;
        PUT32((uint32_t)(dn + 6)); fwrite(z, 1, dn + 10, f); PUT32(Crc32(z, dn + 10)); free(z);
    }
    unsigned char ie[4] = { 'I','E','N','D' }; PUT32(0); fwrite(ie, 1, 4, f); PUT32(Crc32(ie, 4));
    #undef PUT32
    xfree(def);
    int bad = ferror(f); fclose(f);
    return !bad && z;
}
int UiWritePngFile(const wchar_t *path, const unsigned char *rgba, int w, int h) { return TvWritePng(path, rgba, w, h); }   // (3D view's Export PNG)
static void TvSafeName(const char *in, wchar_t *out, size_t n) {
    wchar_t w[300]; MultiByteToWideChar(CP_UTF8, 0, in, -1, w, 300);
    for (wchar_t *q = w; *q; q++) if (wcschr(L"\\/:*?\"<>|", *q) || *q < 32) *q = L'_';
    swprintf(out, n, L"%ls", w);
}

/* ---------------- display ---------------- */
static void TvComposite(TV *v) {
    xfree(v->disp); v->disp = NULL;
    if (v->cur < 0 || !v->rgba) return;
    const TexInfo *t = &v->tf.t[v->cur];
    size_t np = (size_t)t->w * t->h;
    v->disp = (unsigned char *)xalloc(np * 4);
    if (!v->disp) return;
    for (int y = 0; y < t->h; y++) for (int x = 0; x < t->w; x++) {
        size_t i = (size_t)y * t->w + x; const unsigned char *s = v->rgba + i * 4; unsigned char *d = v->disp + i * 4;
        int bgc = v->bgMode == 1 ? 0 : v->bgMode == 2 ? 255 : v->bgMode == 3 ? 128 : ((((x >> 3) ^ (y >> 3)) & 1) ? 200 : 150);
        int a = s[3];
        d[0] = (unsigned char)((s[2] * a + bgc * (255 - a)) / 255);
        d[1] = (unsigned char)((s[1] * a + bgc * (255 - a)) / 255);
        d[2] = (unsigned char)((s[0] * a + bgc * (255 - a)) / 255);
        d[3] = 255;
    }
}
static void TvFit(TV *v) {
    if (v->cur < 0) return;
    RECT rc; GetClientRect(v->img, &rc);
    const TexInfo *t = &v->tf.t[v->cur];
    double zx = (rc.right - 20) / (double)t->w, zy = (rc.bottom - 20) / (double)t->h;
    v->zoom = zx < zy ? zx : zy; if (v->zoom > 8) v->zoom = 8; if (v->zoom <= 0) v->zoom = 1;
    v->panX = v->panY = 0;
}
static void TvShowInfo(TV *v) {
    wchar_t t[700];
    if (v->cur < 0) { SetWindowTextW(v->info, v->loaded ? L"Pick a texture on the left." : L""); return; }
    const TexInfo *x = &v->tf.t[v->cur]; TexFmt f; TexFormat(x->fmt, &f);
    const char *cn[6] = { "0", "1", "R", "G", "B", "A" };
    wchar_t nm[200]; MultiByteToWideChar(CP_UTF8, 0, x->name, -1, nm, 200);
    swprintf(t, 700, L"%ls   %d \x00D7 %d   %hs%ls   mips %d%ls   channels %hs%hs%hs%hs   zoom %.0f%%%ls",
             nm, x->w, x->h, f.name, (x->fmt & 0xFF) == 6 ? L" (sRGB)" : L"", x->mips,
             x->layers > 1 ? L"   (first layer shown)" : L"",
             cn[x->chan[0] % 6], cn[x->chan[1] % 6], cn[x->chan[2] % 6], cn[x->chan[3] % 6], v->zoom * 100,
             v->err[0] ? L"   - " : L"");
    if (v->err[0]) { size_t l = wcslen(t); MultiByteToWideChar(CP_UTF8, 0, v->err, -1, t + l, (int)(700 - l)); }
    SetWindowTextW(v->info, t);
}
static void TvSelect(TV *v, int idx) {
    free(v->rgba); v->rgba = NULL; v->err[0] = 0;
    v->cur = idx;
    if (idx >= 0) {
        HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
        v->rgba = TexDecode(v->file, v->fileN, &v->tf.t[idx], v->tf.ounce, v->err, sizeof v->err);
        SetCursor(old);
    }
    TvComposite(v);
    if (v->fitMode || v->zoom <= 0) TvFit(v);
    TvShowInfo(v);
    EnableWindow(v->save, v->rgba != NULL);
    InvalidateRect(v->img, NULL, FALSE);
}

static LRESULT CALLBACK TvImgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    TV *v = (TV *)GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA);
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(dc); HBITMAP bm = CreateCompatibleBitmap(dc, rc.right, rc.bottom); HGDIOBJ ob = SelectObject(mem, bm);
        FillRect(mem, &rc, (HBRUSH)GetStockObject(DKGRAY_BRUSH));
        if (v && v->disp && v->cur >= 0) {
            const TexInfo *t = &v->tf.t[v->cur];
            int dw = (int)(t->w * v->zoom + 0.5), dh = (int)(t->h * v->zoom + 0.5);
            int x0 = (int)((rc.right - dw) / 2 + v->panX), y0 = (int)((rc.bottom - dh) / 2 + v->panY);
            BITMAPINFO bi; ZeroMemory(&bi, sizeof bi);
            bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = t->w; bi.bmiHeader.biHeight = -t->h;
            bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(mem, v->zoom < 1 ? HALFTONE : COLORONCOLOR); SetBrushOrgEx(mem, 0, 0, NULL);
            StretchDIBits(mem, x0, y0, dw, dh, 0, 0, t->w, t->h, v->disp, &bi, DIB_RGB_COLORS, SRCCOPY);
        } else if (v) {
            SetBkMode(mem, TRANSPARENT); SetTextColor(mem, RGB(220, 220, 220)); SelectObject(mem, v->font);
            const wchar_t *m = !v->loaded ? (v->err[0] ? L"Couldn't load the textures." : L"Loading...") : v->err[0] ? L"This texture can't be shown." : L"";
            DrawTextW(mem, m, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob); DeleteObject(bm); DeleteDC(mem);
        EndPaint(hwnd, &ps); return 0;
    }
    case WM_MOUSEWHEEL: if (v && v->cur >= 0) {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) }; ScreenToClient(hwnd, &pt);
        RECT rc; GetClientRect(hwnd, &rc);
        double f = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1.25 : 0.8, nz = v->zoom * f;
        if (nz < 0.02) nz = 0.02; if (nz > 32) nz = 32; f = nz / v->zoom;
        double cx = pt.x - rc.right / 2.0, cy = pt.y - rc.bottom / 2.0;      // keep the point under the mouse still
        v->panX = cx - (cx - v->panX) * f; v->panY = cy - (cy - v->panY) * f;
        v->zoom = nz; v->fitMode = 0; SendMessageW(v->fit, BM_SETCHECK, BST_UNCHECKED, 0);
        TvShowInfo(v); InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    } break;
    case WM_LBUTTONDOWN: if (v) { SetFocus(hwnd); SetCapture(hwnd); v->dragging = 1; v->dragFrom.x = (short)LOWORD(lp); v->dragFrom.y = (short)HIWORD(lp); v->panFrom[0] = v->panX; v->panFrom[1] = v->panY; } return 0;
    case WM_MOUSEMOVE: if (v && v->dragging) {
        v->panX = v->panFrom[0] + ((short)LOWORD(lp) - v->dragFrom.x); v->panY = v->panFrom[1] + ((short)HIWORD(lp) - v->dragFrom.y);
        InvalidateRect(hwnd, NULL, FALSE);
    } return 0;
    case WM_LBUTTONUP: if (v && v->dragging) { v->dragging = 0; ReleaseCapture(); } return 0;
    case WM_LBUTTONDBLCLK: if (v) { v->fitMode = 1; SendMessageW(v->fit, BM_SETCHECK, BST_CHECKED, 0); TvFit(v); TvShowInfo(v); InvalidateRect(hwnd, NULL, FALSE); } return 0;
    case WM_SIZE: if (v && v->fitMode) { TvFit(v); TvShowInfo(v); } InvalidateRect(hwnd, NULL, FALSE); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void TvFillList(TV *v) {
    wchar_t flt[200]; GetWindowTextW(v->filter, flt, 200); _wcslwr(flt);
    char f8[600]; WideCharToMultiByte(CP_UTF8, 0, flt, -1, f8, 600, NULL, NULL);
    SendMessageW(v->list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(v->list);
    free(v->shown); v->shown = (int *)malloc(sizeof(int) * (v->tf.n + 1)); v->nshown = 0;
    int sel = -1;
    for (int i = 0; i < v->tf.n; i++) {
        char low[128]; snprintf(low, 128, "%s", v->tf.t[i].name); for (char *q = low; *q; q++) if (*q >= 'A' && *q <= 'Z') *q += 32;
        if (f8[0] && !strstr(low, f8)) continue;
        int row = v->nshown; v->shown[v->nshown++] = i;
        wchar_t nm[200]; MultiByteToWideChar(CP_UTF8, 0, v->tf.t[i].name, -1, nm, 200);
        LVITEMW it = { 0 }; it.mask = LVIF_TEXT | LVIF_PARAM; it.iItem = row; it.pszText = nm; it.lParam = i;
        SendMessageW(v->list, LVM_INSERTITEMW, 0, (LPARAM)&it);
        TexFmt f; TexFormat(v->tf.t[i].fmt, &f);
        wchar_t c1[40], c2[40]; swprintf(c1, 40, L"%hs", f.name); swprintf(c2, 40, L"%d \x00D7 %d", v->tf.t[i].w, v->tf.t[i].h);
        LVITEMW s1 = { 0 }; s1.iSubItem = 1; s1.pszText = c1; SendMessageW(v->list, LVM_SETITEMTEXTW, row, (LPARAM)&s1);
        LVITEMW s2 = { 0 }; s2.iSubItem = 2; s2.pszText = c2; SendMessageW(v->list, LVM_SETITEMTEXTW, row, (LPARAM)&s2);
        if (i == v->cur) sel = row;
    }
    SendMessageW(v->list, WM_SETREDRAW, TRUE, 0);
    if (sel >= 0) { ListView_SetItemState(v->list, sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); ListView_EnsureVisible(v->list, sel, FALSE); }
    wchar_t c[120]; swprintf(c, 120, L"%d of %d textures", v->nshown, v->tf.n); SetWindowTextW(v->count, c);
}

/* ---------------- saving ---------------- */
typedef struct { TV *v; wchar_t dir[MAX_PATH]; int done, failed, total; } TvSaveJob;
static DWORD WINAPI TvSaveAllThread(LPVOID p) {
    TvSaveJob *j = (TvSaveJob *)p; TV *v = j->v;
    for (int k = 0; k < j->total && !v->cancelSave; k++) {
        int i = v->shown[k];
        char err[200]; unsigned char *rgba = TexDecode(v->file, v->fileN, &v->tf.t[i], v->tf.ounce, err, sizeof err);
        wchar_t nm[300], path[MAX_PATH + 320]; TvSafeName(v->tf.t[i].name, nm, 300);
        swprintf(path, MAX_PATH + 320, L"%ls\\%ls.png", j->dir, nm);
        if (!rgba || !TvWritePng(path, rgba, v->tf.t[i].w, v->tf.t[i].h)) j->failed++; else j->done++;
        free(rgba);
        PostMessageW(v->wnd, WM_TV_SAVEPROG, k + 1, j->total);
    }
    PostMessageW(v->wnd, WM_TV_SAVEDONE, 0, (LPARAM)j);
    return 0;
}
static void TvSaveOne(TV *v) {
    if (v->cur < 0 || !v->rgba) return;
    const TexInfo *t = &v->tf.t[v->cur];
    wchar_t path[MAX_PATH]; TvSafeName(t->name, path, MAX_PATH); wcsncat(path, L".png", MAX_PATH - wcslen(path) - 1);
    OPENFILENAMEW of; ZeroMemory(&of, sizeof of);
    of.lStructSize = sizeof of; of.hwndOwner = v->wnd; of.lpstrFilter = L"PNG image (*.png)\0*.png\0";
    of.lpstrFile = path; of.nMaxFile = MAX_PATH; of.lpstrDefExt = L"png"; of.lpstrTitle = L"Save texture as PNG";
    of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&of)) return;
    if (!TvWritePng(path, v->rgba, t->w, t->h)) MessageBoxW(v->wnd, L"Saving the PNG failed.", L"Textures", MB_ICONWARNING);
}
BOOL UiPickFolder(HWND owner, const wchar_t *start, const wchar_t *title, wchar_t *out, size_t n);   // (alsarc_gui.c)
static void TvSaveAll(TV *v) {
    if (v->saving) { v->cancelSave = 1; return; }
    if (!v->nshown) return;
    wchar_t t[200]; swprintf(t, 200, L"Choose a folder for the %d textures listed (as PNG files)", v->nshown);
    wchar_t dir[1024];
    if (!UiPickFolder(v->wnd, L"", t, dir, 1024)) return;
    TvSaveJob *j = (TvSaveJob *)calloc(1, sizeof *j);
    if (!j) return;
    swprintf(j->dir, sizeof j->dir / sizeof j->dir[0], L"%ls", dir);
    j->v = v; j->total = v->nshown; v->cancelSave = 0; v->saving = 1;
    SetWindowTextW(v->saveall, L"Stop saving");
    v->saveThread = CreateThread(NULL, 0, TvSaveAllThread, j, 0, NULL);
    if (!v->saveThread) { v->saving = 0; free(j); SetWindowTextW(v->saveall, L"Save all listed as PNG..."); }
}

static void TvLayout(TV *v) {
    RECT rc; GetClientRect(v->wnd, &rc);
    int m = S_(v, 8), g = S_(v, 6), lw = S_(v, 330), rh = S_(v, 26);
    MoveWindow(v->filter, m, m, lw, rh, TRUE);
    MoveWindow(v->count, m, m + rh + g, lw, S_(v, 20), TRUE);
    MoveWindow(v->list, m, m + rh + g + S_(v, 22), lw, rc.bottom - (m + rh + g + S_(v, 22)) - m, TRUE);
    int x = m + lw + m, y = m;
    int b1 = S_(v, 120), b2 = S_(v, 170), b3 = S_(v, 150), b4 = S_(v, 110);
    MoveWindow(v->save, x, y, b1, rh, TRUE);
    MoveWindow(v->saveall, x + b1 + g, y, b2, rh, TRUE);
    MoveWindow(v->bg, x + b1 + g + b2 + g, y, b3, rh, TRUE);
    MoveWindow(v->fit, x + b1 + g + b2 + g + b3 + g * 2, y, b4, rh, TRUE);
    MoveWindow(v->info, x, y + rh + g, rc.right - x - m, S_(v, 20), TRUE);
    MoveWindow(v->img, x, y + rh + g + S_(v, 24), rc.right - x - m, rc.bottom - (y + rh + g + S_(v, 24)) - m, TRUE);
}

static LRESULT CALLBACK TvProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    TV *v = (TV *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        v = (TV *)((CREATESTRUCTW *)lp)->lpCreateParams; SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)v); v->wnd = hwnd;
        HINSTANCE in = GetModuleHandleW(NULL);
        #define MK(cls, txt, st, ex, id) CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | (st), 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)(id), in, NULL)
        v->filter = MK(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, TV_FILTER);
        SendMessageW(v->filter, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter by name...");
        v->count = MK(L"STATIC", L"", 0, 0, TV_COUNT);
        v->list = MK(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP, WS_EX_CLIENTEDGE, TV_LIST);
        ListView_SetExtendedListViewStyle(v->list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        const wchar_t *hd[3] = { L"Name", L"Format", L"Size" }; int wd[3] = { 180, 70, 80 };
        for (int i = 0; i < 3; i++) { LVCOLUMNW c = { 0 }; c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM; c.pszText = (LPWSTR)hd[i]; c.cx = S_(v, wd[i]); c.iSubItem = i; SendMessageW(v->list, LVM_INSERTCOLUMNW, i, (LPARAM)&c); }
        v->save = MK(L"BUTTON", L"Save PNG...", BS_PUSHBUTTON | WS_TABSTOP, 0, TV_SAVE);
        v->saveall = MK(L"BUTTON", L"Save all listed as PNG...", BS_PUSHBUTTON | WS_TABSTOP, 0, TV_SAVEALL);
        v->bg = MK(L"BUTTON", L"Background: checks", BS_PUSHBUTTON | WS_TABSTOP, 0, TV_BG);
        v->fit = MK(L"BUTTON", L"Fit to window", BS_AUTOCHECKBOX | WS_TABSTOP, 0, TV_FIT);
        v->info = MK(L"STATIC", L"Loading...", SS_ENDELLIPSIS, 0, TV_INFO);
        v->img = MK(TV_IMG_CLASS, L"", WS_CLIPSIBLINGS, WS_EX_CLIENTEDGE, TV_IMG);
        #undef MK
        HWND kids[] = { v->filter, v->count, v->list, v->save, v->saveall, v->bg, v->fit, v->info };
        for (size_t i = 0; i < sizeof kids / sizeof kids[0]; i++) SendMessageW(kids[i], WM_SETFONT, (WPARAM)v->font, TRUE);
        SendMessageW(v->fit, BM_SETCHECK, BST_CHECKED, 0); v->fitMode = 1;
        EnableWindow(v->save, FALSE); EnableWindow(v->saveall, FALSE);
        TvLayout(v);
        HANDLE t = CreateThread(NULL, 0, TvLoadThread, v, 0, NULL); if (t) CloseHandle(t);
        return 0;
    }
    case WM_SIZE: if (v) TvLayout(v); return 0;
    case WM_GETMINMAXINFO: if (v) { ((MINMAXINFO *)lp)->ptMinTrackSize.x = S_(v, 760); ((MINMAXINFO *)lp)->ptMinTrackSize.y = S_(v, 420); } return 0;
    case WM_TV_LOADED:
        v->loaded = (int)lp;
        if (!lp) { SetWindowTextW(v->info, L""); { wchar_t e[400]; MultiByteToWideChar(CP_UTF8, 0, v->err, -1, e, 400); SetWindowTextW(v->info, e); } InvalidateRect(v->img, NULL, FALSE); return 0; }
        TvFillList(v);
        EnableWindow(v->saveall, v->tf.n > 0);
        if (v->nshown) { ListView_SetItemState(v->list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); }
        else TvShowInfo(v);
        return 0;
    case WM_TV_SAVEPROG: { wchar_t t[120]; swprintf(t, 120, L"Saving PNG files... %d of %d", (int)wp, (int)lp); SetWindowTextW(v->info, t); return 0; }
    case WM_TV_SAVEDONE: {
        TvSaveJob *j = (TvSaveJob *)lp;
        if (v->saveThread) { WaitForSingleObject(v->saveThread, INFINITE); CloseHandle(v->saveThread); v->saveThread = NULL; }
        v->saving = 0; SetWindowTextW(v->saveall, L"Save all listed as PNG...");
        wchar_t m[MAX_PATH + 200];
        swprintf(m, MAX_PATH + 200, L"%ls %d PNG files to:\n%ls%ls\n\nOpen the folder?", v->cancelSave ? L"Stopped - saved" : L"Saved", j->done, j->dir,
                 j->failed ? L"\n(some textures couldn't be decoded and were skipped)" : L"");
        if (MessageBoxW(hwnd, m, L"Textures", MB_YESNO | MB_ICONINFORMATION) == IDYES) ShellExecuteW(hwnd, L"open", j->dir, NULL, NULL, SW_SHOWNORMAL);
        free(j); TvShowInfo(v);
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)lp;
        if (nh->idFrom == TV_LIST && nh->code == LVN_ITEMCHANGED) {
            NMLISTVIEW *lv = (NMLISTVIEW *)lp;
            if ((lv->uNewState & LVIS_SELECTED) && !(lv->uOldState & LVIS_SELECTED) && lv->iItem >= 0 && lv->iItem < v->nshown && v->shown[lv->iItem] != v->cur)
                TvSelect(v, v->shown[lv->iItem]);
        }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case TV_FILTER: if (HIWORD(wp) == EN_CHANGE && v->loaded) TvFillList(v); return 0;
        case TV_SAVE: TvSaveOne(v); return 0;
        case TV_SAVEALL: TvSaveAll(v); return 0;
        case TV_BG: {
            static const wchar_t *bn[4] = { L"Background: checks", L"Background: black", L"Background: white", L"Background: grey" };
            v->bgMode = (v->bgMode + 1) % 4; SetWindowTextW(v->bg, bn[v->bgMode]); TvComposite(v); InvalidateRect(v->img, NULL, FALSE); return 0;
        }
        case TV_FIT:
            v->fitMode = SendMessageW(v->fit, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (v->fitMode) TvFit(v); else { v->zoom = 1; v->panX = v->panY = 0; }
            TvShowInfo(v); InvalidateRect(v->img, NULL, FALSE); return 0;
        }
        break;
    case WM_CLOSE:
        if (v->saving) { v->cancelSave = 1; if (v->saveThread) WaitForSingleObject(v->saveThread, 30000); }
        DestroyWindow(hwnd); return 0;
    case WM_NCDESTROY:
        if (v) {
            if (v->saveThread) { CloseHandle(v->saveThread); }
            TexClose(&v->tf); free(v->file); free(v->rgba); xfree(v->disp); free(v->shown); free(v);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// dumpFile: the file in the game dump (a .bntx, or the archive it is inside); inner: its path inside the archive (UTF-8) or NULL
void TexViewOpen(HWND owner, const wchar_t *dumpFile, const char *inner, const wchar_t *title, HFONT font, int dpi) {
    static int reg = 0;
    HINSTANCE in = GetModuleHandleW(NULL);
    if (!reg) {
        WNDCLASSEXW wc = { 0 }; wc.cbSize = sizeof wc; wc.lpfnWndProc = TvProc; wc.hInstance = in; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = TV_CLASS;
        wc.hIcon = (HICON)SendMessageW(owner, WM_GETICON, ICON_BIG, 0); wc.hIconSm = (HICON)SendMessageW(owner, WM_GETICON, ICON_SMALL, 0);
        RegisterClassExW(&wc);
        WNDCLASSEXW ic = { 0 }; ic.cbSize = sizeof ic; ic.lpfnWndProc = TvImgProc; ic.hInstance = in; ic.hCursor = LoadCursor(NULL, IDC_HAND);
        ic.style = CS_DBLCLKS; ic.lpszClassName = TV_IMG_CLASS; RegisterClassExW(&ic);
        reg = 1;
    }
    TV *v = (TV *)calloc(1, sizeof(TV));
    if (!v) return;
    v->font = font; v->dpi = dpi ? dpi : 96; v->cur = -1; v->zoom = 1;
    swprintf(v->path, 2048, L"%ls", dumpFile); swprintf(v->title, 600, L"Textures - %ls", title);
    if (inner) snprintf(v->inner, sizeof v->inner, "%s", inner);
    int w = MulDiv(1200, v->dpi, 96), h = MulDiv(780, v->dpi, 96);
    HWND wnd = CreateWindowExW(0, TV_CLASS, v->title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, w, h, NULL, NULL, in, v);
    if (!wnd) { free(v); return; }
    if (wnd) UiSkinWindow(wnd);                 // the main window's dark look
    ShowWindow(wnd, SW_SHOW);
}
