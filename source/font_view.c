/* Font viewer window: one of the game's fonts from a Font\*.bfarc archive in the dump -
   a real font (scft\*.bfotf / .bfttf), a bitmap font (font\*.bffnt, e.g. the PictColor icons) or a font
   combination as the layouts name it (fcpx\*.bfcpx). Shows every character it has in a grid, and a line of
   sample text you can type. */
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
void UiSkinWindow(HWND top);
#include <commctrl.h>
#include <stdio.h>
#include <stdint.h>
#include <wchar.h>
#include "font_win.h"

#define FV_CLASS L"AlsarcFontView"
#define FV_GRID_CLASS L"AlsarcFontGrid"
enum { FV_SAMPLE = 100, FV_SIZE, FV_INFO, FV_GRID, FV_CELL };

typedef struct {
    HWND wnd, sample, size, info, grid, cell;
    HFONT font, small; int dpi;
    wchar_t title[600];
    GameFonts gf;
    int mode;                 // 0 real font, 1 bitmap font, 2 combination
    int face, bmp, fcpx;      // which one
    char what[96];
    unsigned *cps; int ncp;   // the characters
    int em;                   // glyph size in pixels
    int scroll, cellW, cellH, cols, sel;
    HBITMAP sheetBm[8];       // premultiplied BGRA sheets (bitmap fonts)
    HFONT faceFont[24];       // per face, at the current size (combination entries add their scale)
} FV;

static int S_(FV *v, int x) { return MulDiv(x, v->dpi, 96); }

/* ---------- drawing one character ---------- */
static HBITMAP FvSheet(FV *v, const Bffnt *B, int s) {
    if (s < 0 || s >= 8 || s >= B->nsheet || !B->sheet[s]) return NULL;
    if (v->sheetBm[s]) return v->sheetBm[s];
    BITMAPINFO bi; ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = B->sheetW; bi.bmiHeader.biHeight = -B->sheetH; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void *bits = NULL; HBITMAP bm = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bm) return NULL;
    const unsigned char *s4 = B->sheet[s]; unsigned char *d = (unsigned char *)bits;
    for (size_t i = 0; i < (size_t)B->sheetW * B->sheetH; i++) {
        unsigned a = s4[i * 4 + 3];
        d[i * 4] = (unsigned char)(s4[i * 4 + 2] * a / 255); d[i * 4 + 1] = (unsigned char)(s4[i * 4 + 1] * a / 255);
        d[i * 4 + 2] = (unsigned char)(s4[i * 4] * a / 255); d[i * 4 + 3] = (unsigned char)a;
    }
    v->sheetBm[s] = bm;
    return bm;
}
static HFONT FvFaceFont(FV *v, int face, float scale) {
    if (face < 0 || face >= 24) return NULL;
    if (scale == 1.0f) {
        if (!v->faceFont[face]) v->faceFont[face] = CreateFontW(-v->em, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, v->gf.face[face].family);
        return v->faceFont[face];
    }
    return CreateFontW(-(int)(v->em * scale + 0.5f), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, v->gf.face[face].family);
}
// where character cp comes from: *face >= 0 (and scale), or *bmp >= 0
static void FvSource(FV *v, unsigned cp, int *face, int *bmp, float *scale) {
    *face = *bmp = -1; *scale = 1;
    if (v->mode == 0) { if (OtHas(v->gf.face[v->face].cov, cp)) *face = v->face; return; }
    if (v->mode == 1) { if (cp < 65536 && v->gf.bmp[v->bmp].map[cp] != 0xFFFF) *bmp = v->bmp; return; }
    const GfFont *F = &v->gf.font[v->fcpx];
    if (F->c.bitmap) { if (F->src[0] >= 1000 && cp < 65536 && v->gf.bmp[F->src[0] - 1000].map[cp] != 0xFFFF) *bmp = F->src[0] - 1000; return; }
    int k = GfPick(&v->gf, F, cp);
    if (k >= 0) { *face = F->src[k]; *scale = F->c.e[k].scale; }
}
// draws cp with its left edge at x and baseline at y (or, when centre is set, centred on x); returns the advance
static int FvDrawChar(FV *v, HDC dc, unsigned cp, int x, int y, int centre, int draw) {
    int face, bmp; float scale; FvSource(v, cp, &face, &bmp, &scale);
    if (bmp >= 0) {
        const Bffnt *B = &v->gf.bmp[bmp]; int gi = B->map[cp], s, cx, cy;
        float k = (float)v->em / (B->ascent ? B->ascent : B->height);
        FfWidth w = BffntWidth(B, gi);
        int adv = (int)(w.charW * k + 0.5f);
        if (draw && BffntCell(B, gi, &s, &cx, &cy)) {
            HBITMAP sb = FvSheet(v, B, s);
            if (sb) {
                HDC md = CreateCompatibleDC(dc); HGDIOBJ o = SelectObject(md, sb);
                int gw = (int)(w.glyphW * k + 0.5f), gh = (int)(B->cellH * k + 0.5f);
                int gx = centre ? x - gw / 2 : x + (int)(w.left * k), gy = y - (int)(B->baseline * k + 0.5f);
                BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
                SetStretchBltMode(dc, HALFTONE);
                AlphaBlend(dc, gx, gy, gw, gh, md, cx, cy, w.glyphW, B->cellH, bf);
                SelectObject(md, o); DeleteDC(md);
            }
        }
        return adv;
    }
    HFONT f = face >= 0 ? FvFaceFont(v, face, scale) : NULL; int own = face >= 0 && scale != 1.0f;
    if (!f) { f = CreateFontW(-v->em, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Yu Gothic UI"); own = 1; }
    HGDIOBJ of = SelectObject(dc, f);
    wchar_t c1 = (wchar_t)cp; SIZE sz; GetTextExtentPoint32W(dc, &c1, 1, &sz);
    if (draw) {
        SetTextAlign(dc, TA_BASELINE | TA_LEFT); SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, face >= 0 ? RGB(255, 255, 255) : RGB(150, 150, 150));
        TextOutW(dc, centre ? x - sz.cx / 2 : x, y, &c1, 1);
    }
    SelectObject(dc, of); if (own) DeleteObject(f);
    return sz.cx;
}

/* ---------- grid ---------- */
static void FvLayout(FV *v) {
    RECT rc; GetClientRect(v->grid, &rc);
    v->cellW = v->em * 2 > S_(v, 56) ? v->em * 2 : S_(v, 56); v->cellH = v->cellW + S_(v, 14);
    v->cols = rc.right / v->cellW; if (v->cols < 1) v->cols = 1;
    int rows = (v->ncp + v->cols - 1) / v->cols, total = rows * v->cellH;
    if (v->scroll > total - rc.bottom) v->scroll = total - rc.bottom; if (v->scroll < 0) v->scroll = 0;
    SCROLLINFO si = { sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, 0, total > 0 ? total - 1 : 0, (UINT)rc.bottom, v->scroll };
    SetScrollInfo(v->grid, SB_VERT, &si, TRUE);
}
static void FvShowCell(FV *v) {
    wchar_t t[400];
    if (v->sel < 0 || v->sel >= v->ncp) { swprintf(t, 400, L"%d characters. Click one to see where it comes from.", v->ncp); SetWindowTextW(v->cell, t); return; }
    unsigned cp = v->cps[v->sel]; int face, bmp; float scale; FvSource(v, cp, &face, &bmp, &scale);
    wchar_t ch[3] = { (wchar_t)cp, 0, 0 };
    if (bmp >= 0) {
        const Bffnt *B = &v->gf.bmp[bmp]; int gi = B->map[cp]; FfWidth w = BffntWidth(B, gi); int per = B->cols * B->rows;
        swprintf(t, 400, L"U+%04X  \"%ls\"   %hs: glyph %d (sheet %d, column %d, row %d), width %d, advance %d",
                 cp, ch, v->gf.bmpName[bmp], gi, gi / per, (gi % per) % B->cols, (gi % per) / B->cols, w.glyphW, w.charW);
    } else if (face >= 0) {
        swprintf(t, 400, L"U+%04X  \"%ls\"   from %hs (%ls)%ls", cp, ch, v->gf.face[face].file, v->gf.face[face].family, scale != 1.0f ? L", drawn larger" : L"");
    } else swprintf(t, 400, L"U+%04X", cp);
    SetWindowTextW(v->cell, t);
}
static LRESULT CALLBACK FvGridProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    FV *v = (FV *)GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA);
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: if (v) { FvLayout(v); InvalidateRect(hwnd, NULL, FALSE); } return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC pdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HDC dc = CreateCompatibleDC(pdc); HBITMAP bm = CreateCompatibleBitmap(pdc, rc.right, rc.bottom); HGDIOBJ ob = SelectObject(dc, bm);
        HBRUSH bg = CreateSolidBrush(RGB(0x1D, 0x19, 0x1B)); FillRect(dc, &rc, bg); DeleteObject(bg);
        if (v && v->ncp) {
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(0x3A, 0x30, 0x34)); HGDIOBJ op = SelectObject(dc, pen);
            HBRUSH selb = CreateSolidBrush(RGB(0x4A, 0x24, 0x20));
            int r0 = v->scroll / v->cellH, r1 = (v->scroll + rc.bottom) / v->cellH + 1;
            for (int r = r0; r <= r1; r++)
                for (int c = 0; c < v->cols; c++) {
                    int i = r * v->cols + c; if (i >= v->ncp) break;
                    int x = c * v->cellW, y = r * v->cellH - v->scroll;
                    RECT cr = { x, y, x + v->cellW, y + v->cellH };
                    if (i == v->sel) { FillRect(dc, &cr, selb); RECT bar = { x, y, x + S_(v, 3), y + v->cellH }; HBRUSH ab = CreateSolidBrush(RGB(0xFF, 0x5D, 0x4F)); FillRect(dc, &bar, ab); DeleteObject(ab); }
                    MoveToEx(dc, x + v->cellW - 1, y, NULL); LineTo(dc, x + v->cellW - 1, y + v->cellH - 1); LineTo(dc, x - 1, y + v->cellH - 1);
                    FvDrawChar(v, dc, v->cps[i], x + v->cellW / 2, y + v->cellW / 2 + v->em / 3, 1, 1);
                    wchar_t lab[12]; swprintf(lab, 12, L"%04X", v->cps[i]);
                    HGDIOBJ of = SelectObject(dc, v->small); SetTextColor(dc, RGB(0xB0, 0xA2, 0x9C)); SetBkMode(dc, TRANSPARENT); SetTextAlign(dc, TA_TOP | TA_CENTER);
                    TextOutW(dc, x + v->cellW / 2, y + v->cellH - S_(v, 15), lab, (int)wcslen(lab));
                    SelectObject(dc, of);
                }
            SelectObject(dc, op); DeleteObject(pen); DeleteObject(selb);
        }
        BitBlt(pdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, ob); DeleteObject(bm); DeleteDC(dc);
        EndPaint(hwnd, &ps); return 0;
    }
    case WM_VSCROLL: {
        if (!v) return 0;
        RECT rc; GetClientRect(hwnd, &rc);
        SCROLLINFO si = { sizeof si, SIF_ALL }; GetScrollInfo(hwnd, SB_VERT, &si);
        int p = v->scroll;
        switch (LOWORD(wp)) {
        case SB_LINEUP: p -= v->cellH; break; case SB_LINEDOWN: p += v->cellH; break;
        case SB_PAGEUP: p -= rc.bottom; break; case SB_PAGEDOWN: p += rc.bottom; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: p = si.nTrackPos; break;
        case SB_TOP: p = 0; break; case SB_BOTTOM: p = si.nMax; break;
        }
        v->scroll = p; FvLayout(v); InvalidateRect(hwnd, NULL, FALSE); return 0;
    }
    case WM_MOUSEWHEEL: if (v) { v->scroll -= GET_WHEEL_DELTA_WPARAM(wp) * v->cellH / 120; FvLayout(v); InvalidateRect(hwnd, NULL, FALSE); } return 0;
    case WM_LBUTTONDOWN: {
        if (!v) return 0;
        SetFocus(hwnd);
        int x = (short)LOWORD(lp), y = (short)HIWORD(lp) + v->scroll, c = x / v->cellW, r = y / v->cellH;
        int i = c < v->cols ? r * v->cols + c : -1; v->sel = i < v->ncp ? i : -1;
        FvShowCell(v); InvalidateRect(hwnd, NULL, FALSE); return 0;
    }
    case WM_LBUTTONDBLCLK: {                   // add the character to the sample text
        if (!v || v->sel < 0) return 0;
        int n = GetWindowTextLengthW(v->sample); wchar_t c1[2] = { (wchar_t)v->cps[v->sel], 0 };
        SendMessageW(v->sample, EM_SETSEL, n, n); SendMessageW(v->sample, EM_REPLACESEL, FALSE, (LPARAM)c1);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------- sample line ---------- */
static void FvPaintSample(FV *v, HDC dc, RECT *area) {
    HBRUSH bg = CreateSolidBrush(RGB(0x14, 0x11, 0x13)); FillRect(dc, area, bg); DeleteObject(bg);
    wchar_t t[1024]; GetWindowTextW(v->sample, t, 1024);
    int x = area->left + S_(v, 10), y = area->top + (area->bottom - area->top) / 2 + v->em / 3;
    HRGN clip = CreateRectRgnIndirect(area); SelectClipRgn(dc, clip);
    for (const wchar_t *p = t; *p && x < area->right; p++) x += FvDrawChar(v, dc, *p, x, y, 0, 1);
    SelectClipRgn(dc, NULL); DeleteObject(clip);
}
static void FvRelayout(FV *v) {
    RECT rc; GetClientRect(v->wnd, &rc);
    int m = S_(v, 8), rh = S_(v, 24), g = S_(v, 6), sw = S_(v, 130);
    MoveWindow(v->info, m, m, rc.right - 2 * m, S_(v, 20), TRUE);
    int y = m + S_(v, 22);
    MoveWindow(v->sample, m, y, rc.right - 3 * m - sw, rh, TRUE);
    MoveWindow(v->size, rc.right - m - sw, y, sw, S_(v, 300), TRUE);
    y += rh + g;
    int sh = v->em * 2 + S_(v, 10);                                  // the sample is painted in this strip
    y += sh + g;
    MoveWindow(v->cell, m, y, rc.right - 2 * m, S_(v, 20), TRUE); y += S_(v, 22);
    MoveWindow(v->grid, m, y, rc.right - 2 * m, rc.bottom - y - m, TRUE);
    InvalidateRect(v->wnd, NULL, TRUE);
}
static void FvSampleRect(FV *v, RECT *r) {
    RECT rc; GetClientRect(v->wnd, &rc);
    int m = S_(v, 8), y = m + S_(v, 22) + S_(v, 24) + S_(v, 6);
    r->left = m; r->right = rc.right - m; r->top = y; r->bottom = y + v->em * 2 + S_(v, 10);
}
static void FvSetSize(FV *v, int px) {
    v->em = S_(v, px);
    for (int i = 0; i < 24; i++) if (v->faceFont[i]) { DeleteObject(v->faceFont[i]); v->faceFont[i] = NULL; }
    FvRelayout(v); FvLayout(v); InvalidateRect(v->grid, NULL, FALSE);
}

static int FvLoad(FV *v, const wchar_t *dump, const char *inner) {
    HCURSOR oc = SetCursor(LoadCursor(NULL, IDC_WAIT));
    int ok = GfLoad(&v->gf, dump);
    const char *base = strrchr(inner, '/'); base = base ? base + 1 : inner;
    snprintf(v->what, sizeof v->what, "%s", base);
    v->face = v->bmp = v->fcpx = -1;
    if (!strncmp(inner, "scft/", 5)) {
        v->mode = 0;
        for (int i = 0; i < v->gf.nface; i++) if (!strcmp(v->gf.face[i].file, base) && v->gf.face[i].mem) v->face = i;
        if (v->face < 0) {                  // not used by any combination: load it now
            if (v->gf.loose) v->face = GfFace_(&v->gf, NULL, 0, base);
            else {
                size_t n; unsigned char *a = ArcReadAll(dump, &n); if (a) a = ArcUnzstd(a, n, &n);
                if (a) { v->face = GfFace_(&v->gf, a, n, base); free(a); }
            }
        }
        if (v->face < 0) for (int i = 0; i < v->gf.nface; i++) if (!strcmp(v->gf.face[i].file, base) && v->gf.face[i].err[0]) snprintf(v->gf.err, sizeof v->gf.err, "%s", v->gf.face[i].err);
    } else if (!strncmp(inner, "font/", 5)) {
        v->mode = 1;
        for (int i = 0; i < v->gf.nbmp; i++) if (!strcmp(v->gf.bmpName[i], base)) v->bmp = i;
        if (v->bmp < 0 && v->gf.nbmp < 4) {
            size_t n; unsigned char *a = v->gf.loose ? NULL : ArcReadAll(dump, &n); if (a) a = ArcUnzstd(a, n, &n);
            if (a || v->gf.loose) {
                size_t fn; unsigned char *fb = GfMember(&v->gf, a, a ? n : 0, inner, &fn); char err[200];
                if (fb && BffntParse(&v->gf.bmp[v->gf.nbmp], fb, fn, err, sizeof err)) { snprintf(v->gf.bmpName[v->gf.nbmp], 64, "%s", base); v->bmp = v->gf.nbmp++; }
                else BffntFree(&v->gf.bmp[v->gf.nbmp]);
                free(fb); free(a);
            }
        }
    } else {
        v->mode = 2; v->fcpx = GfFind(&v->gf, base);
    }
    SetCursor(oc);
    int have = v->mode == 0 ? v->face >= 0 : v->mode == 1 ? v->bmp >= 0 : v->fcpx >= 0;
    if (!have) {
        if (!v->gf.err[0]) snprintf(v->gf.err, sizeof v->gf.err, "%s wasn't found in the archive (%d fonts, %d real fonts, %d bitmap fonts read).", inner, v->gf.nfont, v->gf.nface, v->gf.nbmp);
        return 0;
    }
    (void)ok;
    // the characters
    v->cps = (unsigned *)malloc(65536 * sizeof(unsigned)); v->ncp = 0;
    for (unsigned cp = 32; cp < 65536 && v->cps; cp++) {
        if (cp >= 0xD800 && cp < 0xE000) continue;
        int f, b; float s; FvSource(v, cp, &f, &b, &s);
        if (f >= 0 || b >= 0) v->cps[v->ncp++] = cp;
    }
    return 1;
}
static void FvSetInfo(FV *v) {
    wchar_t t[600];
    if (v->mode == 0) {
        const GfFace *f = &v->gf.face[v->face];
        swprintf(t, 600, L"%hs - real font \"%ls\" (unscrambled from the game's .%hs): %d characters", v->what, f->family, strrchr(v->what, '.') ? strrchr(v->what, '.') + 1 : "", v->ncp);
    } else if (v->mode == 1) {
        const Bffnt *B = &v->gf.bmp[v->bmp];
        swprintf(t, 600, L"%hs - bitmap font, %d glyphs on %d sheets of %dx%d (cells %dx%d)%ls", v->what, B->nglyph, B->nsheet, B->sheetW, B->sheetH, B->cellW, B->cellH,
                 B->sheet && B->sheet[0] ? L"" : L" - the sheets couldn't be decoded");
    } else {
        const GfFont *F = &v->gf.font[v->fcpx];
        if (F->c.bitmap) swprintf(t, 600, L"%hs - uses the bitmap font %hs: %d characters", v->what, F->c.bitmapFile, v->ncp);
        else {
            int o = swprintf(t, 600, L"%hs - combination of: ", v->what);
            for (int k = 0; k < F->c.n && o < 560; k++) o += swprintf(t + o, 600 - o, L"%hs%hs", k ? ", " : "", F->c.e[k].file);
            if (F->c.variants > 1) o += swprintf(t + o, 600 - o, L"  [first of %d variants]", F->c.variants);
            swprintf(t + o, 600 - o, L"  (%d characters)", v->ncp);
        }
    }
    SetWindowTextW(v->info, t);
}

static LRESULT CALLBACK FvProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    FV *v = (FV *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp; v = (FV *)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)v); v->wnd = hwnd;
        HINSTANCE in = cs->hInstance;
#define MK(cls, txt, st, ex, id) CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | (st), 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)(id), in, NULL)
        v->info = MK(L"STATIC", L"", SS_ENDELLIPSIS, 0, FV_INFO);
        v->sample = MK(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, FV_SAMPLE);
        v->size = MK(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, 0, FV_SIZE);
        v->cell = MK(L"STATIC", L"", SS_ENDELLIPSIS, 0, FV_CELL);
        v->grid = MK(FV_GRID_CLASS, L"", WS_VSCROLL | WS_TABSTOP, 0, FV_GRID);
#undef MK
        HWND all[] = { v->info, v->sample, v->size, v->cell };
        for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) SendMessageW(all[i], WM_SETFONT, (WPARAM)v->font, TRUE);
        const wchar_t *sz[] = { L"Size: 24", L"Size: 36", L"Size: 48", L"Size: 72", L"Size: 96" };
        for (int i = 0; i < 5; i++) SendMessageW(v->size, CB_ADDSTRING, 0, (LPARAM)sz[i]);
        SendMessageW(v->size, CB_SETCURSEL, 2, 0);
        v->small = CreateFontW(-S_(v, 10), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        v->em = S_(v, 48); v->sel = -1;
        // a sample that shows what the font is for
        wchar_t s[300] = L"";
        if (v->mode == 1 || (v->mode == 2 && v->gf.font[v->fcpx].c.bitmap)) { int k = 0; for (int i = 0; i < v->ncp && k < 24; i++) s[k++] = (wchar_t)v->cps[i * (v->ncp / 24 ? v->ncp / 24 : 1) % v->ncp]; s[k] = 0; }
        else {
            int face, bmp; float sc; FvSource(v, 'D', &face, &bmp, &sc);
            if (face < 0) { int k = 0; for (int i = 0; i < v->ncp && k < 30; i++) s[k++] = (wchar_t)v->cps[i]; s[k] = 0; }   // a symbol font
            else {
                swprintf(s, 300, L"The quick brown fox jumps over the lazy dog 0123456789!?");
                FvSource(v, 0xE0E0, &face, &bmp, &sc); if (face >= 0) wcscat(s, L" PRESS \xE0E0");
                FvSource(v, 0x3069, &face, &bmp, &sc); if (face >= 0) wcscat(s, L" \x3042\x3044\x3046\x3048\x304A \x30A2\x30A4\x30A6");
            }
        }
        SetWindowTextW(v->sample, s);
        FvSetInfo(v); FvShowCell(v);
        FvRelayout(v); FvLayout(v);
        return 0;
    }
    case WM_SIZE: if (v) FvRelayout(v); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps);
        if (v) {
            RECT r; FvSampleRect(v, &r);
            HDC md = CreateCompatibleDC(dc); HBITMAP bm = CreateCompatibleBitmap(dc, r.right - r.left, r.bottom - r.top); HGDIOBJ ob = SelectObject(md, bm);
            RECT lr = { 0, 0, r.right - r.left, r.bottom - r.top }; FvPaintSample(v, md, &lr);
            BitBlt(dc, r.left, r.top, lr.right, lr.bottom, md, 0, 0, SRCCOPY);
            SelectObject(md, ob); DeleteObject(bm); DeleteDC(md);
        }
        EndPaint(hwnd, &ps); return 0;
    }
    case WM_COMMAND:
        if (!v) break;
        if (LOWORD(wp) == FV_SAMPLE && HIWORD(wp) == EN_CHANGE) { RECT r; FvSampleRect(v, &r); InvalidateRect(hwnd, &r, FALSE); return 0; }
        if (LOWORD(wp) == FV_SIZE && HIWORD(wp) == CBN_SELCHANGE) {
            static const int px[] = { 24, 36, 48, 72, 96 }; int i = (int)SendMessageW(v->size, CB_GETCURSEL, 0, 0);
            if (i >= 0 && i < 5) FvSetSize(v, px[i]);
            return 0;
        }
        break;
    case WM_GETMINMAXINFO: { MINMAXINFO *mm = (MINMAXINFO *)lp; mm->ptMinTrackSize.x = 500; mm->ptMinTrackSize.y = 400; return 0; }
    case WM_DESTROY:
        if (v) {
            for (int i = 0; i < 8; i++) if (v->sheetBm[i]) DeleteObject(v->sheetBm[i]);
            for (int i = 0; i < 24; i++) if (v->faceFont[i]) DeleteObject(v->faceFont[i]);
            if (v->small) DeleteObject(v->small);
            GfFree(&v->gf); free(v->cps); free(v);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// dumpFile: the font archive in the game dump; inner: the font's path inside it ("scft/X.bfotf", "font/X.bffnt", "fcpx/X.bfcpx")
void FontViewOpen(HWND owner, const wchar_t *dumpFile, const char *inner, const wchar_t *title, HFONT font, int dpi) {
    static int reg = 0;
    HINSTANCE in = GetModuleHandleW(NULL);
    if (!reg) {
        WNDCLASSEXW wc = { 0 }; wc.cbSize = sizeof wc; wc.lpfnWndProc = FvProc; wc.hInstance = in; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = FV_CLASS;
        wc.hIcon = (HICON)SendMessageW(owner, WM_GETICON, ICON_BIG, 0); wc.hIconSm = (HICON)SendMessageW(owner, WM_GETICON, ICON_SMALL, 0);
        RegisterClassExW(&wc);
        WNDCLASSEXW gc = { 0 }; gc.cbSize = sizeof gc; gc.lpfnWndProc = FvGridProc; gc.hInstance = in; gc.hCursor = LoadCursor(NULL, IDC_ARROW);
        gc.style = CS_DBLCLKS; gc.lpszClassName = FV_GRID_CLASS; RegisterClassExW(&gc);
        reg = 1;
    }
    if (!inner || !inner[0]) { MessageBoxW(owner, L"This font's location isn't known (fonts are read from the game's Font archives or Font folder).", L"Font viewer", MB_ICONINFORMATION); return; }
    FV *v = (FV *)calloc(1, sizeof(FV)); if (!v) return;
    v->font = font; v->dpi = dpi ? dpi : 96; v->em = MulDiv(48, v->dpi, 96);
    swprintf(v->title, 600, L"Font - %ls", title);
    if (!FvLoad(v, dumpFile, inner)) {
        wchar_t m[1600];
        swprintf(m, 1600, L"This font couldn't be read from the game dump.\n\n%hs\n\nArchive: %ls\nFont: %hs", v->gf.err[0] ? v->gf.err : "(no details)", dumpFile, inner);
        MessageBoxW(owner, m, L"Font viewer", MB_ICONINFORMATION);
        GfFree(&v->gf); free(v->cps); free(v); return;
    }
    int w = MulDiv(1000, v->dpi, 96), h = MulDiv(760, v->dpi, 96);
    HWND wnd = CreateWindowExW(0, FV_CLASS, v->title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, w, h, NULL, NULL, in, v);
    if (!wnd) { GfFree(&v->gf); free(v->cps); free(v); return; }
    if (wnd) UiSkinWindow(wnd);                 // the main window's dark look
    ShowWindow(wnd, SW_SHOW);
}

/* ---------- preview for the main window ---------- */
// a short sample ("Aa" and a Japanese character when the font has one) drawn in the font, premultiplied BGRA
unsigned char *FontThumbnail(const wchar_t *dumpFile, const char *inner, int boxW, int boxH, int *ow, int *oh, wchar_t *note, int noteN) {
    note[0] = 0;
    if (!inner || !inner[0] || boxW < 8 || boxH < 8) return NULL;
    FV *v = (FV *)calloc(1, sizeof(FV)); if (!v) return NULL;
    v->dpi = 96; v->sel = -1; v->em = boxH * 45 / 100;
    unsigned char *out = NULL;
    if (FvLoad(v, dumpFile, inner)) {
        unsigned sample[4]; int ns = 0;
        unsigned want[] = { 'A', 'a', 0x3042 /* hiragana a */, 0x5B57 /* kanji */ };
        for (int i = 0; i < 4; i++) { int f, b; float s; FvSource(v, want[i], &f, &b, &s); if (f >= 0 || b >= 0) sample[ns++] = want[i]; }
        if (!ns && v->ncp) for (int i = 0; i < v->ncp && ns < 3; i++) if (v->cps[i] > 32) sample[ns++] = v->cps[i];   // icon fonts: their first glyphs
        BITMAPINFO bi; ZeroMemory(&bi, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = boxW; bi.bmiHeader.biHeight = -boxH; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        void *bits = NULL; HDC dc = CreateCompatibleDC(NULL); HBITMAP bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (bm && bits) {
            HGDIOBJ ob = SelectObject(dc, bm);
            memset(bits, 0, (size_t)boxW * boxH * 4);
            int total = 0; for (int i = 0; i < ns; i++) total += FvDrawChar(v, dc, sample[i], 0, 0, 0, 0) + v->em / 8;
            if (total > boxW) { v->em = v->em * boxW / (total + 1); for (int i = 0; i < 24; i++) if (v->faceFont[i]) { DeleteObject(v->faceFont[i]); v->faceFont[i] = NULL; } total = 0; for (int i = 0; i < ns; i++) total += FvDrawChar(v, dc, sample[i], 0, 0, 0, 0) + v->em / 8; }
            int x = (boxW - total) / 2, y = boxH / 2 + v->em / 3;
            for (int i = 0; i < ns; i++) x += FvDrawChar(v, dc, sample[i], x, y, 0, 1) + v->em / 8;
            GdiFlush();
            unsigned char *p = (unsigned char *)bits;                 // drawn on black: brightness becomes coverage
            for (size_t i = 0; i < (size_t)boxW * boxH; i++) {
                unsigned a = p[i * 4]; if (p[i * 4 + 1] > a) a = p[i * 4 + 1]; if (p[i * 4 + 2] > a) a = p[i * 4 + 2];
                p[i * 4 + 3] = (unsigned char)a;
            }
            out = (unsigned char *)malloc((size_t)boxW * boxH * 4);
            if (out) { memcpy(out, bits, (size_t)boxW * boxH * 4); *ow = boxW; *oh = boxH; }
            SelectObject(dc, ob);
        }
        if (bm) DeleteObject(bm);
        DeleteDC(dc);
        if (v->mode == 0) swprintf(note, noteN, L"%ls - %d chars", v->gf.face[v->face].family, v->ncp);
        else swprintf(note, noteN, L"%d characters", v->ncp);
    }
    for (int i = 0; i < 8; i++) if (v->sheetBm[i]) DeleteObject(v->sheetBm[i]);
    for (int i = 0; i < 24; i++) if (v->faceFont[i]) DeleteObject(v->faceFont[i]);
    GfFree(&v->gf); free(v->cps); free(v);
    return out;
}
