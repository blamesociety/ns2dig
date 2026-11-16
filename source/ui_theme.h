// ui_theme.h - ns2dig's dark look: colours, Windows dark mode, and a tiny anti-aliased shape painter used
// for file-type icons, button glyphs, rounded buttons and progress bars. Plain Win32 + GDI, no extra DLLs.
#ifndef UI_THEME_H
#define UI_THEME_H
#ifndef UI_NO_WIN
#include <windows.h>
#endif
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ colours (match the website) */
#define UI_BG       RGB(0x14, 0x11, 0x13)   // window background
#define UI_PANEL    RGB(0x1D, 0x19, 0x1B)   // tree, lists, text view
#define UI_PANEL2   RGB(0x26, 0x20, 0x23)   // raised: buttons, header boxes
#define UI_HOVER    RGB(0x33, 0x2A, 0x2E)
#define UI_LINE     RGB(0x3A, 0x30, 0x34)   // borders
#define UI_LINE2    RGB(0x4A, 0x3D, 0x42)
#define UI_INK      RGB(0xF1, 0xEB, 0xE5)   // main text
#define UI_MUT      RGB(0xB0, 0xA2, 0x9C)   // secondary text
#define UI_DIM      RGB(0x6E, 0x63, 0x60)   // disabled text
#define UI_ACC      RGB(0xFF, 0x5D, 0x4F)   // coral accent (deliberately not Nintendo red)
#define UI_ACC_HOT  RGB(0xFF, 0x74, 0x67)
#define UI_ACC_INK  RGB(0x1A, 0x0B, 0x09)   // text on the accent
#define UI_ACC2     RGB(0xFF, 0xB3, 0x47)   // amber
#define UI_SEL      RGB(0x4A, 0x24, 0x20)   // selected row
#define UI_TAGBG    RGB(0x3A, 0x2A, 0x14)   // amber tag background
#define UI_STATUSBG RGB(0x10, 0x0D, 0x0F)

#ifndef UI_NO_WIN
/* ------------------------------------------------------------------ Windows dark mode */
typedef enum { UiAppModeDefault, UiAppModeAllowDark, UiAppModeForceDark } UiAppMode;
static BOOL UiInitDarkMode(void) {
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return FALSE;
    // undocumented, but stable since Windows 10 1903 (used by Notepad++, Explorer...): ordinal 135
    typedef int (WINAPI *SetPreferredAppModeFn)(int);
    typedef void (WINAPI *FlushMenuThemesFn)(void);
    SetPreferredAppModeFn sp = (SetPreferredAppModeFn)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    FlushMenuThemesFn fm = (FlushMenuThemesFn)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(136));
    OSVERSIONINFOW vi = { sizeof vi };
    typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOW *);
    RtlGetVersionFn rv = (RtlGetVersionFn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    if (rv) rv(&vi);
    if (sp && vi.dwBuildNumber >= 18362) sp(UiAppModeForceDark);      // dark context menus and scrollbars
    typedef void (WINAPI *RefreshFn)(void);
    RefreshFn rf = (RefreshFn)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(104));      // RefreshImmersiveColorPolicyState
    if (rf && vi.dwBuildNumber >= 18362) rf();
    if (fm && vi.dwBuildNumber >= 18362) fm();
    return sp && vi.dwBuildNumber >= 18362;
}
// let Windows draw this window's menus and scrollbars dark (ordinal 133, AllowDarkModeForWindow)
static void UiAllowDarkWindow(HWND w) {
    HMODULE ux = GetModuleHandleW(L"uxtheme.dll"); if (!ux) return;
    typedef BOOL (WINAPI *AllowFn)(HWND, BOOL);
    AllowFn af = (AllowFn)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(133));
    if (af) { af(w, TRUE); SendMessageW(w, WM_THEMECHANGED, 0, 0); }
}
// dark title bar (Windows 10 2004+ / 11) and, on Windows 11, a caption the same colour as the window
static void UiDarkTitle(HWND w) {
    HMODULE dwm = LoadLibraryExW(L"dwmapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dwm) return;
    typedef HRESULT (WINAPI *DwmSetFn)(HWND, DWORD, LPCVOID, DWORD);
    DwmSetFn set = (DwmSetFn)(void *)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (!set) return;
    BOOL on = TRUE;
    if (FAILED(set(w, 20, &on, sizeof on))) set(w, 19, &on, sizeof on);   // DWMWA_USE_IMMERSIVE_DARK_MODE (19 on old builds)
    COLORREF cap = UI_BG, txt = UI_INK;
    set(w, 35, &cap, sizeof cap);                                        // DWMWA_CAPTION_COLOR (Windows 11)
    set(w, 36, &txt, sizeof txt);                                        // DWMWA_TEXT_COLOR
}
static void UiDarkControl(HWND h, const wchar_t *cls) {
    typedef HRESULT (WINAPI *SetWindowThemeFn)(HWND, LPCWSTR, LPCWSTR);
    static SetWindowThemeFn swt = NULL; static int tried = 0;
    if (!tried) { HMODULE ux = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32); if (ux) swt = (SetWindowThemeFn)(void *)GetProcAddress(ux, "SetWindowTheme"); tried = 1; }
    if (swt) swt(h, cls, NULL);
}

#endif
#ifndef UI_NO_WIN
/* ------------------------------------------------------------------ fonts */
static HFONT UiFontTry(const LOGFONTW *base, const wchar_t *face, int heightPx, int weight) {
    LOGFONTW lf = *base; wcscpy(lf.lfFaceName, face);
    if (heightPx) lf.lfHeight = heightPx;
    if (weight) lf.lfWeight = weight;
    lf.lfQuality = CLEARTYPE_QUALITY;
    HFONT f = CreateFontIndirectW(&lf);
    HDC dc = GetDC(NULL); HGDIOBJ o = SelectObject(dc, f);
    wchar_t got[LF_FACESIZE] = L""; GetTextFaceW(dc, LF_FACESIZE, got);
    SelectObject(dc, o); ReleaseDC(NULL, dc);
    if (_wcsicmp(got, face)) { DeleteObject(f); return NULL; }
    return f;
}

#endif
#ifndef UI_NO_WIN
#include <commctrl.h>

/* drop-down lists open without Windows' slide animation: its frames are drawn with a light edge the app can't
   restyle. The setting is switched off only for the moment the list opens, then put back as it was. */
static LRESULT UiDropNoSlide(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    BOOL anim = FALSE;
    SystemParametersInfoW(SPI_GETCOMBOBOXANIMATION, 0, &anim, 0);
    if (anim) SystemParametersInfoW(SPI_SETCOMBOBOXANIMATION, 0, (PVOID)FALSE, 0);
    LRESULT r = DefSubclassProc(h, msg, wp, lp);
    if (anim) SystemParametersInfoW(SPI_SETCOMBOBOXANIMATION, 0, (PVOID)TRUE, 0);
    return r;
}
#define UI_DROP_MSG(m) ((m) == WM_LBUTTONDOWN || (m) == WM_LBUTTONDBLCLK || (m) == WM_KEYDOWN || (m) == WM_SYSKEYDOWN || (m) == CB_SHOWDROPDOWN)
#endif
/* ------------------------------------------------------------------ anti-aliased shape painter
   A canvas is premultiplied BGRA (top-down). Shapes are tested at 4 x 4 points per pixel and blended over. */
typedef struct { uint32_t *px; int w, h; } UiCanvas;

typedef struct {
    int kind;                 // 0 round rect, 1 circle, 2 polygon, 3 thick line (capsule), 4 ring
    float a, b, c, d, r;      // rrect: x0 y0 x1 y1 radius; circle/ring: cx cy r (ring width in d); line: x0 y0 x1 y1, r = half width
    const float *pts; int n;  // polygon (x, y pairs)
} UiShape;

static int UiInside(const UiShape *s, float x, float y) {
    switch (s->kind) {
    case 0: {
        if (x < s->a || x > s->c || y < s->b || y > s->d) return 0;
        float r = s->r, cx = x < s->a + r ? s->a + r : x > s->c - r ? s->c - r : x, cy = y < s->b + r ? s->b + r : y > s->d - r ? s->d - r : y;
        float dx = x - cx, dy = y - cy; return dx * dx + dy * dy <= r * r;
    }
    case 1: { float dx = x - s->a, dy = y - s->b; return dx * dx + dy * dy <= s->r * s->r; }
    case 4: { float dx = x - s->a, dy = y - s->b, d2 = dx * dx + dy * dy, ro = s->r, ri = s->r - s->d; return d2 <= ro * ro && d2 >= ri * ri; }
    case 2: {
        int in = 0;
        for (int i = 0, j = s->n - 1; i < s->n; j = i++) {
            float xi = s->pts[i * 2], yi = s->pts[i * 2 + 1], xj = s->pts[j * 2], yj = s->pts[j * 2 + 1];
            if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
        }
        return in;
    }
    case 3: {
        float vx = s->c - s->a, vy = s->d - s->b, wx = x - s->a, wy = y - s->b, l2 = vx * vx + vy * vy;
        float t = l2 > 0 ? (wx * vx + wy * vy) / l2 : 0; if (t < 0) t = 0; if (t > 1) t = 1;
        float dx = wx - t * vx, dy = wy - t * vy; return dx * dx + dy * dy <= s->r * s->r;
    }
    }
    return 0;
}
static void UiBounds(const UiShape *s, float *x0, float *y0, float *x1, float *y1) {
    switch (s->kind) {
    case 0: *x0 = s->a; *y0 = s->b; *x1 = s->c; *y1 = s->d; return;
    case 1: case 4: *x0 = s->a - s->r; *y0 = s->b - s->r; *x1 = s->a + s->r; *y1 = s->b + s->r; return;
    case 3: *x0 = fminf(s->a, s->c) - s->r; *x1 = fmaxf(s->a, s->c) + s->r; *y0 = fminf(s->b, s->d) - s->r; *y1 = fmaxf(s->b, s->d) + s->r; return;
    case 2: *x0 = *x1 = s->pts[0]; *y0 = *y1 = s->pts[1];
        for (int i = 1; i < s->n; i++) { float x = s->pts[i * 2], y = s->pts[i * 2 + 1]; if (x < *x0) *x0 = x; if (x > *x1) *x1 = x; if (y < *y0) *y0 = y; if (y > *y1) *y1 = y; }
        return;
    }
}
// paint a shape (coordinates in pixels) in colour col with opacity alpha (0..255)
static void UiFill(UiCanvas *cv, const UiShape *s, COLORREF col, int alpha) {
    float fx0, fy0, fx1, fy1; UiBounds(s, &fx0, &fy0, &fx1, &fy1);
    int x0 = (int)floorf(fx0), y0 = (int)floorf(fy0), x1 = (int)ceilf(fx1), y1 = (int)ceilf(fy1);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > cv->w) x1 = cv->w; if (y1 > cv->h) y1 = cv->h;
    unsigned cr = GetRValue(col), cg = GetGValue(col), cb = GetBValue(col);
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        int cov = 0;
        for (int sy = 0; sy < 4; sy++) for (int sx = 0; sx < 4; sx++) cov += UiInside(s, x + (sx + 0.5f) / 4, y + (sy + 0.5f) / 4);
        if (!cov) continue;
        unsigned a = (unsigned)(alpha * cov / 16);
        uint32_t *p = &cv->px[(size_t)y * cv->w + x], d = *p;
        unsigned ia = 255 - a;
        unsigned nb = (cb * a + (d & 255) * ia) / 255, ng = (cg * a + ((d >> 8) & 255) * ia) / 255,
                 nr = (cr * a + ((d >> 16) & 255) * ia) / 255, na = a + ((d >> 24) * ia) / 255;
        *p = nb | (ng << 8) | (nr << 16) | (na << 24);
    }
}
static void UiRRect(UiCanvas *cv, float x0, float y0, float x1, float y1, float r, COLORREF c, int a) { UiShape s = { 0, x0, y0, x1, y1, r }; UiFill(cv, &s, c, a); }
static void UiCircle(UiCanvas *cv, float cx, float cy, float r, COLORREF c, int a) { UiShape s = { 1, cx, cy, 0, 0, r }; UiFill(cv, &s, c, a); }
static void UiRing(UiCanvas *cv, float cx, float cy, float r, float w, COLORREF c, int a) { UiShape s = { 4, cx, cy, 0, w, r }; UiFill(cv, &s, c, a); }
static void UiLine(UiCanvas *cv, float x0, float y0, float x1, float y1, float w, COLORREF c, int a) { UiShape s = { 3, x0, y0, x1, y1, w / 2 }; UiFill(cv, &s, c, a); }
static void UiPoly(UiCanvas *cv, const float *pts, int n, COLORREF c, int a) { UiShape s = { 2, 0, 0, 0, 0, 0, pts, n }; UiFill(cv, &s, c, a); }

#ifndef UI_NO_WIN
// a 32-bit top-down DIB to paint into, then AlphaBlend / BitBlt to a DC
typedef struct { HBITMAP bmp; HDC dc; HGDIOBJ old; UiCanvas cv; } UiDib;
static int UiDibMake(UiDib *d, int w, int h, HDC like) {
    BITMAPINFO bi; ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    d->bmp = CreateDIBSection(like, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!d->bmp) return 0;
    d->dc = CreateCompatibleDC(like); d->old = SelectObject(d->dc, d->bmp);
    d->cv.px = (uint32_t *)bits; d->cv.w = w; d->cv.h = h;
    return 1;
}
static void UiDibFree(UiDib *d) { if (d->dc) { SelectObject(d->dc, d->old); DeleteDC(d->dc); } if (d->bmp) DeleteObject(d->bmp); ZeroMemory(d, sizeof *d); }
static void UiDibClear(UiDib *d, COLORREF c, int opaque) {
    uint32_t v = opaque ? (GetBValue(c) | (GetGValue(c) << 8) | (GetRValue(c) << 16) | 0xFF000000u) : 0;
    for (int i = 0; i < d->cv.w * d->cv.h; i++) d->cv.px[i] = v;
}
static void UiDibBlend(HDC dst, int x, int y, UiDib *d) {
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(dst, x, y, d->cv.w, d->cv.h, d->dc, 0, 0, d->cv.w, d->cv.h, bf);
}
// GDI text drawn onto an opaque DIB leaves alpha at 0: make every pixel opaque again
static void UiDibOpaque(UiDib *d) { for (int i = 0; i < d->cv.w * d->cv.h; i++) d->cv.px[i] |= 0xFF000000u; }

#endif

/* scale a BGRA picture (straight alpha) to fit inside boxW x boxH (never more than 4x up); returns malloc'd
   premultiplied BGRA for AlphaBlend, size in *ow, *oh */
static inline unsigned char *UiScaleToBox(const unsigned char *src, int w, int h, int boxW, int boxH, int *ow, int *oh) {
    if (!src || w <= 0 || h <= 0 || boxW <= 0 || boxH <= 0) return NULL;
    double sc = (double)boxW / w; if ((double)boxH / h < sc) sc = (double)boxH / h; if (sc > 4) sc = 4;
    int W = (int)(w * sc + 0.5), H = (int)(h * sc + 0.5); if (W < 1) W = 1; if (H < 1) H = 1;
    unsigned char *out = (unsigned char *)malloc((size_t)W * H * 4);
    if (!out) return NULL;
    for (int y = 0; y < H; y++) {
        int y0 = (int)((double)y * h / H), y1 = (int)((double)(y + 1) * h / H); if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < W; x++) {
            int x0 = (int)((double)x * w / W), x1 = (int)((double)(x + 1) * w / W); if (x1 <= x0) x1 = x0 + 1;
            unsigned long long r = 0, g = 0, b = 0, a = 0, c = 0;
            int sy = (y1 - y0 + 7) / 8, sx = (x1 - x0 + 7) / 8;
            for (int yy = y0; yy < y1 && yy < h; yy += sy) for (int xx = x0; xx < x1 && xx < w; xx += sx) {
                const unsigned char *p = src + ((size_t)yy * w + xx) * 4;
                b += p[0] * p[3]; g += p[1] * p[3]; r += p[2] * p[3]; a += p[3]; c++;
            }
            unsigned char *d = out + ((size_t)y * W + x) * 4;
            if (!c) { d[0] = d[1] = d[2] = d[3] = 0; continue; }
            d[0] = (unsigned char)(b / (c * 255)); d[1] = (unsigned char)(g / (c * 255)); d[2] = (unsigned char)(r / (c * 255)); d[3] = (unsigned char)(a / c);
        }
    }
    *ow = W; *oh = H;
    return out;
}

/* ------------------------------------------------------------------ file-type icons (16-unit design grid) */
enum { UI_IC_FILE, UI_IC_FOLDER, UI_IC_ARCHIVE, UI_IC_MODEL, UI_IC_TEXTURE, UI_IC_SOUND, UI_IC_VIDEO, UI_IC_TEXT,
       UI_IC_FONT, UI_IC_TERRAIN, UI_IC_LAYOUT, UI_IC_LIGHT, UI_IC_EVENT, UI_IC_COLLISION, UI_IC_DATA, UI_IC_SHADER, UI_IC_COUNT };

static void UiPolyU(UiCanvas *cv, float ox, float oy, float k, const float *p, int n, COLORREF c) {
    float t[64]; for (int i = 0; i < n; i++) { t[i * 2] = ox + p[i * 2] * k; t[i * 2 + 1] = oy + p[i * 2 + 1] * k; }
    UiPoly(cv, t, n, c, 255);
}
// draw icon `ic` with its top-left at (ox, oy), size px pixels
static void UiDrawIcon(UiCanvas *cv, int ic, float ox, float oy, float px) {
    float k = px / 16.0f;
    #define X(v) (ox + (v) * k)
    #define Y(v) (oy + (v) * k)
    switch (ic) {
    case UI_IC_FOLDER: {
        const float a[] = { 1.5f,3.5f, 6.1f,3.5f, 7.5f,5, 14.5f,5, 14.5f,13, 1.5f,13 };
        UiPolyU(cv, ox, oy, k, a, 6, RGB(0xE8, 0xB0, 0x4A));
        UiRRect(cv, X(1.5f), Y(6), X(14.5f), Y(13.2f), 1.2f * k, RGB(0xF5, 0xC7, 0x6A), 255);
        break; }
    case UI_IC_ARCHIVE:
        UiRRect(cv, X(2), Y(2.5f), X(14), Y(13.5f), 1.5f * k, RGB(0xB0, 0x7A, 0x4A), 255);
        UiRRect(cv, X(7), Y(2.5f), X(9), Y(13.5f), 0, RGB(0xE3, 0xB5, 0x83), 255);
        UiRRect(cv, X(6.4f), Y(7), X(9.6f), Y(9.6f), 0.6f * k, RGB(0x5A, 0x3A, 0x20), 255);
        break;
    case UI_IC_MODEL: {
        const float top[] = { 8,1.5f, 14,5, 8,8.5f, 2,5 }, rt[] = { 8,8.5f, 14,5, 14,11, 8,14.5f }, lf[] = { 2,5, 8,8.5f, 8,14.5f, 2,11 };
        UiPolyU(cv, ox, oy, k, lf, 4, RGB(0xFF, 0x7A, 0x6B)); UiPolyU(cv, ox, oy, k, rt, 4, RGB(0xD9, 0x4D, 0x40)); UiPolyU(cv, ox, oy, k, top, 4, RGB(0xFF, 0xAB, 0x9F));
        break; }
    case UI_IC_COLLISION: {
        const float top[] = { 8,1.5f, 14,5, 8,8.5f, 2,5 }, rt[] = { 8,8.5f, 14,5, 14,11, 8,14.5f }, lf[] = { 2,5, 8,8.5f, 8,14.5f, 2,11 };
        UiPolyU(cv, ox, oy, k, lf, 4, RGB(0x6E, 0xC8, 0xE0)); UiPolyU(cv, ox, oy, k, rt, 4, RGB(0x3A, 0x8E, 0xAD)); UiPolyU(cv, ox, oy, k, top, 4, RGB(0xA8, 0xE4, 0xF2));
        UiLine(cv, X(2), Y(5), X(14), Y(11), 0.9f * k, RGB(0x1D, 0x4A, 0x5C), 255);
        break; }
    case UI_IC_TEXTURE: {
        UiRRect(cv, X(1.5f), Y(2.5f), X(14.5f), Y(13.5f), 1.5f * k, RGB(0x4A, 0xA3, 0xD9), 255);
        UiCircle(cv, X(5.5f), Y(6), 1.6f * k, RGB(0xFF, 0xF6, 0xC9), 255);
        const float m[] = { 1.5f,12, 6,8, 9,10.5f, 11.2f,8.7f, 14.5f,11.5f, 14.5f,13.5f, 1.5f,13.5f };
        UiPolyU(cv, ox, oy, k, m, 7, RGB(0x2C, 0x6F, 0x9C));
        break; }
    case UI_IC_TERRAIN: {
        UiRRect(cv, X(1.5f), Y(2.5f), X(14.5f), Y(13.5f), 1.5f * k, RGB(0x2F, 0x5D, 0x3A), 255);
        const float m[] = { 1.5f,12, 5.5f,6, 8.5f,10, 10.5f,7.5f, 14.5f,12, 14.5f,13.5f, 1.5f,13.5f };
        UiPolyU(cv, ox, oy, k, m, 7, RGB(0x8F, 0xD1, 0x8A));
        break; }
    case UI_IC_SOUND:
        UiRRect(cv, X(1.5f), Y(1.5f), X(14.5f), Y(14.5f), 3 * k, RGB(0x9A, 0x6B, 0xD9), 255);
        { const float h[5][2] = { { 4, 2 }, { 6, 6 }, { 8, 8 }, { 10, 4 }, { 12, 2 } };
          for (int i = 0; i < 5; i++) UiLine(cv, X(h[i][0]), Y(8 - h[i][1] / 2), X(h[i][0]), Y(8 + h[i][1] / 2), 1.3f * k, RGB(255, 255, 255), 255); }
        break;
    case UI_IC_VIDEO: {
        UiRRect(cv, X(1.5f), Y(3), X(14.5f), Y(13), 2 * k, RGB(0xD9, 0x48, 0x6B), 255);
        const float t[] = { 6.5f,5.6f, 10.6f,8, 6.5f,10.4f };
        UiPolyU(cv, ox, oy, k, t, 3, RGB(255, 255, 255));
        break; }
    case UI_IC_FONT:
        UiRRect(cv, X(1.5f), Y(1.5f), X(14.5f), Y(14.5f), 3 * k, RGB(0x3F, 0xB3, 0x8F), 255);
        UiLine(cv, X(4.2f), Y(11.5f), X(6.6f), Y(4.5f), 1.4f * k, RGB(255, 255, 255), 255);
        UiLine(cv, X(6.6f), Y(4.5f), X(9), Y(11.5f), 1.4f * k, RGB(255, 255, 255), 255);
        UiLine(cv, X(5.2f), Y(9), X(8), Y(9), 1.2f * k, RGB(255, 255, 255), 255);
        UiRing(cv, X(11), Y(9.7f), 1.9f * k, 1.2f * k, RGB(255, 255, 255), 255);
        UiLine(cv, X(12.6f), Y(7.8f), X(12.6f), Y(11.6f), 1.2f * k, RGB(255, 255, 255), 255);
        break;
    case UI_IC_LAYOUT:
        UiRRect(cv, X(1.5f), Y(2.5f), X(14.5f), Y(13.5f), 1.5f * k, RGB(0xE0, 0x8A, 0x3A), 255);
        UiRRect(cv, X(3), Y(4), X(13), Y(6), 0.6f * k, RGB(0xFF, 0xE1, 0xB8), 255);
        UiRRect(cv, X(3), Y(7.3f), X(7.5f), Y(12), 0.6f * k, RGB(0xFF, 0xE1, 0xB8), 255);
        UiRRect(cv, X(8.5f), Y(7.3f), X(13), Y(12), 0.6f * k, RGB(0xFF, 0xC8, 0x85), 255);
        break;
    case UI_IC_LIGHT:
        UiCircle(cv, X(8), Y(8), 3.2f * k, RGB(0xFF, 0xC9, 0x4A), 255);
        for (int i = 0; i < 8; i++) { float an = i * 3.14159265f / 4, c = cosf(an), s = sinf(an);
            UiLine(cv, X(8 + c * 5), Y(8 + s * 5), X(8 + c * 6.7f), Y(8 + s * 6.7f), 1.3f * k, RGB(0xFF, 0xC9, 0x4A), 255); }
        break;
    case UI_IC_EVENT:
        UiRRect(cv, X(1.5f), Y(2), X(7.5f), Y(6.5f), 1.2f * k, RGB(0x5A, 0xA9, 0xFF), 255);
        UiRRect(cv, X(8.5f), Y(9.5f), X(14.5f), Y(14), 1.2f * k, RGB(0x5A, 0xA9, 0xFF), 255);
        UiLine(cv, X(4.5f), Y(6.5f), X(4.5f), Y(11.7f), 1.3f * k, RGB(0xA9, 0xCF, 0xFF), 255);
        UiLine(cv, X(4.5f), Y(11.7f), X(8.5f), Y(11.7f), 1.3f * k, RGB(0xA9, 0xCF, 0xFF), 255);
        break;
    case UI_IC_SHADER:
        UiRRect(cv, X(1.5f), Y(1.5f), X(14.5f), Y(14.5f), 3 * k, RGB(0x6A, 0x5A, 0xCD), 255);
        UiLine(cv, X(6), Y(5), X(3.5f), Y(8), 1.4f * k, RGB(255, 255, 255), 255); UiLine(cv, X(3.5f), Y(8), X(6), Y(11), 1.4f * k, RGB(255, 255, 255), 255);
        UiLine(cv, X(10), Y(5), X(12.5f), Y(8), 1.4f * k, RGB(255, 255, 255), 255); UiLine(cv, X(12.5f), Y(8), X(10), Y(11), 1.4f * k, RGB(255, 255, 255), 255);
        break;
    case UI_IC_DATA: case UI_IC_TEXT: case UI_IC_FILE: default: {
        COLORREF paper = ic == UI_IC_DATA ? RGB(0xD9, 0xD2, 0xF0) : RGB(0xE9, 0xE2, 0xDA), ink = ic == UI_IC_DATA ? RGB(0x6A, 0x5F, 0x95) : RGB(0x8A, 0x7D, 0x76);
        const float pg[] = { 3,1.5f, 9.8f,1.5f, 13,4.7f, 13,14.5f, 3,14.5f };
        UiPolyU(cv, ox, oy, k, pg, 5, paper);
        const float fold[] = { 9.8f,1.5f, 13,4.7f, 9.8f,4.7f };
        UiPolyU(cv, ox, oy, k, fold, 3, RGB(0xB8, 0xAE, 0xA6));
        if (ic == UI_IC_DATA) {
            UiLine(cv, X(6.6f), Y(7), X(5.2f), Y(9.5f), 1.1f * k, ink, 255); UiLine(cv, X(5.2f), Y(9.5f), X(6.6f), Y(12), 1.1f * k, ink, 255);
            UiLine(cv, X(9.4f), Y(7), X(10.8f), Y(9.5f), 1.1f * k, ink, 255); UiLine(cv, X(10.8f), Y(9.5f), X(9.4f), Y(12), 1.1f * k, ink, 255);
        } else if (ic == UI_IC_TEXT) {
            for (int i = 0; i < 3; i++) UiLine(cv, X(5), Y(7.3f + i * 2.2f), X(i == 2 ? 9 : 11), Y(7.3f + i * 2.2f), 1.1f * k, ink, 255);
        }
        break; }
    }
    #undef X
    #undef Y
}

/* ------------------------------------------------------------------ one-colour glyphs for buttons */
enum { UI_GL_NONE, UI_GL_DOWNLOAD, UI_GL_REFRESH, UI_GL_FOLDER, UI_GL_SEARCH, UI_GL_CUBE, UI_GL_IMAGE, UI_GL_PLAY,
       UI_GL_FONT, UI_GL_LAYOUT, UI_GL_SUN, UI_GL_FLOW, UI_GL_LOG, UI_GL_STOP, UI_GL_ARROW, UI_GL_FILTER, UI_GL_COPY, UI_GL_NEXT, UI_GL_BROWSE, UI_GL_STAR, UI_GL_STARF, UI_GL_DISK, UI_GL_KEYS };
static void UiDrawGlyph(UiCanvas *cv, int g, float ox, float oy, float px, COLORREF c) {
    float k = px / 16.0f, w = 1.6f * k;
    #define X(v) (ox + (v) * k)
    #define Y(v) (oy + (v) * k)
    #define L(a, b, cc, d) UiLine(cv, X(a), Y(b), X(cc), Y(d), w, c, 255)
    switch (g) {
    case UI_GL_DOWNLOAD: L(8, 2, 8, 10); L(4.5f, 7, 8, 10.5f); L(11.5f, 7, 8, 10.5f); L(3, 13.5f, 13, 13.5f); break;
    case UI_GL_REFRESH:
        for (int i = 0; i < 14; i++) { float a0 = -0.6f + i * 0.36f, a1 = a0 + 0.36f; L(8 + 5 * cosf(a0), 8 + 5 * sinf(a0), 8 + 5 * cosf(a1), 8 + 5 * sinf(a1)); }
        L(13.2f, 2.4f, 13, 5.2f); L(13, 5.2f, 10.2f, 4.9f); break;
    case UI_GL_FOLDER: case UI_GL_BROWSE:
        L(2, 4.5f, 6, 4.5f); L(6, 4.5f, 7.3f, 5.8f); L(7.3f, 5.8f, 14, 5.8f); L(14, 5.8f, 14, 13); L(14, 13, 2, 13); L(2, 13, 2, 4.5f);
        if (g == UI_GL_BROWSE) { L(5, 9.5f, 5.2f, 9.5f); L(8, 9.5f, 8.2f, 9.5f); L(11, 9.5f, 11.2f, 9.5f); }
        break;
    case UI_GL_SEARCH: UiRing(cv, X(7), Y(7), 4.6f * k, w, c, 255); L(10.4f, 10.4f, 13.8f, 13.8f); break;
    case UI_GL_CUBE: L(8, 1.8f, 13.5f, 5); L(13.5f, 5, 13.5f, 11); L(13.5f, 11, 8, 14.2f); L(8, 14.2f, 2.5f, 11); L(2.5f, 11, 2.5f, 5); L(2.5f, 5, 8, 1.8f);
        L(8, 8.2f, 13.5f, 5); L(8, 8.2f, 2.5f, 5); L(8, 8.2f, 8, 14.2f); break;
    case UI_GL_IMAGE: L(2, 3, 14, 3); L(14, 3, 14, 13); L(14, 13, 2, 13); L(2, 13, 2, 3); L(2.5f, 12, 6.5f, 8.5f); L(6.5f, 8.5f, 9.5f, 11); L(9.5f, 11, 11, 9.8f); L(11, 9.8f, 13.5f, 12);
        UiCircle(cv, X(10.5f), Y(6), 1.3f * k, c, 255); break;
    case UI_GL_PLAY: { const float t[] = { 4.5f,2.5f, 13,8, 4.5f,13.5f }; UiPolyU(cv, ox, oy, k, t, 3, c); break; }
    case UI_GL_FONT: L(2.5f, 13, 6.5f, 3); L(6.5f, 3, 10.5f, 13); L(4, 9.5f, 9, 9.5f); UiRing(cv, X(12), Y(10.7f), 2.2f * k, w, c, 255); L(14.2f, 8.4f, 14.2f, 13); break;
    case UI_GL_LAYOUT: L(2, 3, 14, 3); L(14, 3, 14, 13); L(14, 13, 2, 13); L(2, 13, 2, 3); L(2, 6.5f, 14, 6.5f); L(7.5f, 6.5f, 7.5f, 13); break;
    case UI_GL_SUN: UiRing(cv, X(8), Y(8), 3 * k, w, c, 255);
        for (int i = 0; i < 8; i++) { float an = i * 3.14159265f / 4; L(8 + cosf(an) * 5, 8 + sinf(an) * 5, 8 + cosf(an) * 6.6f, 8 + sinf(an) * 6.6f); } break;
    case UI_GL_FLOW: L(2, 2.5f, 7, 2.5f); L(7, 2.5f, 7, 6.5f); L(7, 6.5f, 2, 6.5f); L(2, 6.5f, 2, 2.5f);
        L(9, 9.5f, 14, 9.5f); L(14, 9.5f, 14, 13.5f); L(14, 13.5f, 9, 13.5f); L(9, 13.5f, 9, 9.5f); L(4.5f, 6.5f, 4.5f, 11.5f); L(4.5f, 11.5f, 9, 11.5f); break;
    case UI_GL_LOG: L(3.5f, 2, 12.5f, 2); L(12.5f, 2, 12.5f, 14); L(12.5f, 14, 3.5f, 14); L(3.5f, 14, 3.5f, 2); L(6, 5.5f, 10, 5.5f); L(6, 8.5f, 10, 8.5f); L(6, 11.5f, 8.5f, 11.5f); break;
    case UI_GL_STOP: UiRRect(cv, X(3.5f), Y(3.5f), X(12.5f), Y(12.5f), 1.5f * k, c, 255); break;
    case UI_GL_ARROW: L(2.5f, 8, 13, 8); L(9, 4, 13, 8); L(9, 12, 13, 8); break;
    case UI_GL_FILTER: { const float f[] = { 2,3, 14,3, 9.5f,8.5f, 9.5f,13.5f, 6.5f,12, 6.5f,8.5f }; UiPolyU(cv, ox, oy, k, f, 6, c); break; }
    case UI_GL_COPY: L(5.5f, 5.5f, 13.5f, 5.5f); L(13.5f, 5.5f, 13.5f, 14); L(13.5f, 14, 5.5f, 14); L(5.5f, 14, 5.5f, 5.5f); L(2.5f, 11, 2.5f, 2.5f); L(2.5f, 2.5f, 10.5f, 2.5f); break;
    case UI_GL_NEXT: L(8, 2.5f, 8, 13); L(4, 9, 8, 13); L(12, 9, 8, 13); break;
    case UI_GL_KEYS:                                        // a keyboard: outline, two rows of keys, the space bar
        L(1.5f, 4, 14.5f, 4); L(14.5f, 4, 14.5f, 12.5f); L(14.5f, 12.5f, 1.5f, 12.5f); L(1.5f, 12.5f, 1.5f, 4);
        L(4, 6.8f, 4.3f, 6.8f); L(6.6f, 6.8f, 6.9f, 6.8f); L(9.2f, 6.8f, 9.5f, 6.8f); L(11.8f, 6.8f, 12.1f, 6.8f);
        L(5.3f, 8.6f, 5.6f, 8.6f); L(7.9f, 8.6f, 8.2f, 8.6f); L(10.5f, 8.6f, 10.8f, 8.6f); L(5, 10.6f, 11, 10.6f); break;
    case UI_GL_DISK:                                        // a floppy disk: outline with a cut corner, shutter, label
        L(2.5f, 2.5f, 11.5f, 2.5f); L(11.5f, 2.5f, 13.5f, 4.5f); L(13.5f, 4.5f, 13.5f, 13.5f); L(13.5f, 13.5f, 2.5f, 13.5f); L(2.5f, 13.5f, 2.5f, 2.5f);
        L(5, 2.5f, 5, 6); L(5, 6, 10.5f, 6); L(10.5f, 6, 10.5f, 2.5f); L(5, 13.5f, 5, 9.5f); L(5, 9.5f, 11, 9.5f); L(11, 9.5f, 11, 13.5f); break;
    case UI_GL_STAR: case UI_GL_STARF: {                    // five-pointed star: outline, or filled
        float pt[20];
        for (int i = 0; i < 10; i++) {
            float an = -3.14159265f / 2 + i * 3.14159265f / 5, rr = (i & 1) ? 2.9f : 6.6f;
            pt[i * 2] = 8 + cosf(an) * rr; pt[i * 2 + 1] = 8.6f + sinf(an) * rr;
        }
        if (g == UI_GL_STARF) UiPolyU(cv, ox, oy, k, pt, 10, c);
        else for (int i = 0; i < 10; i++) { int j = (i + 1) % 10; L(pt[i * 2], pt[i * 2 + 1], pt[j * 2], pt[j * 2 + 1]); }
        break; }
    }
    #undef L
    #undef X
    #undef Y
}
#endif
