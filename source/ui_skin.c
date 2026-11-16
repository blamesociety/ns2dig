// ui_skin.c - gives the viewer windows (3D view, textures, sound, layout, font, lighting, event, video) the main
// window's dark look. UiSkinWindow(top) is called once a viewer has made its controls: it darkens the title bar
// and background, and restyles every child control by its class (buttons, check boxes, text boxes, drop-downs,
// lists, trees, sliders, progress bars). Windows the viewers draw themselves are left alone.
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <wchar.h>
#include <stdlib.h>
#include "ui_theme.h"
#ifndef TBM_SETPOSNOTIFY
#define TBM_SETPOSNOTIFY (WM_USER + 34)
#endif

static HBRUSH sBg, sPanel, sPanel2;
#define SK_HOVER RGB(51, 51, 51)                // the main window tree's hover grey
static int SkS(HWND h, int x) {
    typedef UINT (WINAPI *GetDpiFn)(HWND);
    static GetDpiFn f = NULL; static int tried = 0;
    if (!tried) { f = (GetDpiFn)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"); tried = 1; }
    UINT d = f ? f(h) : 96; if (!d) d = 96;
    return MulDiv(x, (int)d, 96);
}
static void SkBrushes(void) {
    if (!sBg) { sBg = CreateSolidBrush(UI_BG); sPanel = CreateSolidBrush(UI_PANEL); sPanel2 = CreateSolidBrush(UI_PANEL2); }
}
#define SK_PROP L"ns2digSkin"

/* ------------------------------------------------------------------ buttons and check boxes */
typedef struct { int hot, check, isCheck, pushLike, glyph, primary; } SkBtn;
static int SkGlyphForText(const wchar_t *t) {
    static const struct { const wchar_t *w; int g; } m[] = {
        { L"Play", UI_GL_PLAY }, { L"Restart", UI_GL_REFRESH }, { L"Stop", UI_GL_STOP }, { L"Pause", UI_GL_STOP },
        { L"Save", UI_GL_DISK }, { L"Export PNG", UI_GL_IMAGE }, { L"Export", UI_GL_DOWNLOAD }, { L"Top", UI_GL_NEXT }, { L"Controls", UI_GL_KEYS }, { L"Copy", UI_GL_COPY }, { L"Open", UI_GL_FOLDER },
        { L"Reset", UI_GL_REFRESH }, { L"Fit", UI_GL_IMAGE }, { L"Find", UI_GL_SEARCH }, { L"Search", UI_GL_SEARCH },
    };
    for (size_t i = 0; i < sizeof m / sizeof m[0]; i++) if (!wcsncmp(t, m[i].w, wcslen(m[i].w))) return m[i].g;
    return UI_GL_NONE;
}
static LRESULT CALLBACK SkBtnProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    (void)id;
    SkBtn *b = (SkBtn *)ref;
    switch (msg) {
    case WM_MOUSEMOVE:
        if (!b->hot) { b->hot = 1; InvalidateRect(h, NULL, FALSE); TRACKMOUSEEVENT tm = { sizeof tm, TME_LEAVE, h, 0 }; TrackMouseEvent(&tm); }
        break;
    case WM_MOUSELEAVE: b->hot = 0; InvalidateRect(h, NULL, FALSE); break;
    case BM_GETCHECK: if (b->isCheck) return b->check ? BST_CHECKED : BST_UNCHECKED; break;
    case BM_SETCHECK: if (b->isCheck) { b->check = wp == BST_CHECKED; InvalidateRect(h, NULL, FALSE); return 0; } break;
    case WM_LBUTTONUP:
        if (b->isCheck && GetCapture() == h) {
            POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) }; RECT rc; GetClientRect(h, &rc);
            if (PtInRect(&rc, pt)) { b->check = !b->check; InvalidateRect(h, NULL, FALSE); }
        }
        break;
    case WM_KEYUP:
        if (b->isCheck && wp == VK_SPACE && (SendMessageW(h, BM_GETSTATE, 0, 0) & BST_PUSHED)) { b->check = !b->check; InvalidateRect(h, NULL, FALSE); }
        break;
    case WM_LBUTTONDBLCLK: msg = WM_LBUTTONDOWN; break;
    case WM_ENABLE: case WM_SETTEXT: InvalidateRect(h, NULL, FALSE); break;
    case WM_ERASEBKGND: return 1;
    case WM_NCDESTROY: RemoveWindowSubclass(h, SkBtnProc, 1); RemovePropW(h, SK_PROP); free(b); break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}
static void SkDrawButton(DRAWITEMSTRUCT *di, SkBtn *b) {
    HWND h = di->hwndItem;
    RECT rc = di->rcItem; int w = rc.right - rc.left, hh = rc.bottom - rc.top;
    if (w <= 0 || hh <= 0) return;
    BOOL dis = (di->itemState & ODS_DISABLED) != 0, down = (di->itemState & ODS_SELECTED) != 0, focus = (di->itemState & ODS_FOCUS) && !(di->itemState & ODS_NOFOCUSRECT);
    wchar_t text[200]; GetWindowTextW(h, text, 200);
    UiDib d = { 0 };
    if (!UiDibMake(&d, w, hh, di->hDC)) return;
    UiDibClear(&d, UI_BG, 1);
    HFONT of = (HFONT)SelectObject(d.dc, (HFONT)SendMessageW(h, WM_GETFONT, 0, 0));
    SetBkMode(d.dc, TRANSPARENT);
    float r = (float)SkS(h, 6);
    if (b->isCheck && !b->pushLike) {
        int bs = SkS(h, 16), by = (hh - bs) / 2;
        COLORREF box = b->check ? (b->hot ? UI_ACC_HOT : UI_ACC) : (b->hot ? UI_HOVER : UI_PANEL2);
        UiRRect(&d.cv, 0.5f, by + 0.5f, bs - 0.5f, by + bs - 0.5f, (float)SkS(h, 4), b->check ? box : UI_LINE2, dis ? 120 : 255);
        if (!b->check) UiRRect(&d.cv, 1.5f, by + 1.5f, bs - 1.5f, by + bs - 1.5f, (float)SkS(h, 3), box, 255);
        else {
            float k = bs / 16.0f;
            UiLine(&d.cv, 3.8f * k, by + 8.3f * k, 6.8f * k, by + 11.2f * k, 1.9f * k, UI_ACC_INK, 255);
            UiLine(&d.cv, 6.8f * k, by + 11.2f * k, 12.3f * k, by + 5.0f * k, 1.9f * k, UI_ACC_INK, 255);
        }
        RECT tr = { bs + SkS(h, 8), 0, w, hh };
        SetTextColor(d.dc, dis ? UI_DIM : UI_INK);
        DrawTextW(d.dc, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (focus) { RECT fr = { bs + SkS(h, 5), SkS(h, 2), w - 1, hh - SkS(h, 2) }; UiDibOpaque(&d); DrawFocusRect(d.dc, &fr); }
    } else {
        BOOL on = b->isCheck && b->check;                  // a push-like toggle that is on looks like a primary button
        BOOL prim = (b->primary || on) && !dis;
        COLORREF bg = prim ? (b->hot ? UI_ACC_HOT : UI_ACC) : (down ? UI_PANEL2 : b->hot ? UI_HOVER : UI_PANEL2);
        COLORREF border = prim ? bg : (b->hot ? UI_LINE2 : UI_LINE);
        COLORREF fg = dis ? UI_DIM : prim ? UI_ACC_INK : UI_INK;
        UiRRect(&d.cv, 0.5f, 0.5f, w - 0.5f, hh - 0.5f, r, border, 255);
        UiRRect(&d.cv, 1.5f, 1.5f, w - 1.5f, hh - 1.5f, r - 1, bg, 255);
        if (focus && !prim) UiRRect(&d.cv, 0.5f, 0.5f, w - 0.5f, hh - 0.5f, r, UI_ACC, 90);
        SIZE ts = { 0 }; GetTextExtentPoint32W(d.dc, text, (int)wcslen(text), &ts);
        int gs = b->glyph ? SkS(h, 16) : 0, gap = gs ? SkS(h, 7) : 0;
        if (gs && ts.cx + gs + gap + SkS(h, 12) > w) gs = gap = 0;      // not enough room: text only
        int total = gs + gap + ts.cx, x = (w - total) / 2; if (x < SkS(h, 6)) x = SkS(h, 6);
        if (gs) UiDrawGlyph(&d.cv, b->glyph, (float)x, (hh - gs) / 2.0f, (float)gs, fg);
        UiDibOpaque(&d);
        RECT tr = { x + gs + gap, 0, w - SkS(h, 4), hh };
        SetTextColor(d.dc, fg);
        DrawTextW(d.dc, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    SelectObject(d.dc, of);
    BitBlt(di->hDC, rc.left, rc.top, w, hh, d.dc, 0, 0, SRCCOPY);
    UiDibFree(&d);
}
static void SkButton(HWND h) {
    LONG_PTR st = GetWindowLongPtrW(h, GWL_STYLE);
    DWORD type = (DWORD)st & BS_TYPEMASK;
    int isCheck = (type == BS_AUTOCHECKBOX || type == BS_CHECKBOX);
    if (!(type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON || isCheck)) return;   // group boxes, radio buttons etc. stay as they are
    SkBtn *b = (SkBtn *)calloc(1, sizeof *b); if (!b) return;
    b->isCheck = isCheck; b->pushLike = (st & BS_PUSHLIKE) != 0;
    if (isCheck) b->check = SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
    wchar_t t[200]; GetWindowTextW(h, t, 200);
    b->glyph = (isCheck && !b->pushLike) ? UI_GL_NONE : SkGlyphForText(t);
    b->primary = !isCheck && (!wcsncmp(t, L"Play", 4) || type == BS_DEFPUSHBUTTON);
    SetWindowLongPtrW(h, GWL_STYLE, (st & ~(LONG_PTR)BS_TYPEMASK) | BS_OWNERDRAW);
    SetPropW(h, SK_PROP, (HANDLE)b);
    SetWindowSubclass(h, SkBtnProc, 1, (DWORD_PTR)b);
    InvalidateRect(h, NULL, TRUE);
}

/* ------------------------------------------------------------------ text boxes and drop-downs */
static BOOL SkFocusIn(HWND h) { HWND f = GetFocus(); return f && (f == h || IsChild(h, f)); }
static void SkPaintBox(HWND h, HDC dc, int w, int hh, BOOL chevron, const RECT *skip) {
    UiDib d = { 0 };
    if (!UiDibMake(&d, w, hh, dc)) return;
    UiDibClear(&d, UI_BG, 1);
    float r = (float)SkS(h, 7);
    UiRRect(&d.cv, 0.5f, 0.5f, w - 0.5f, hh - 0.5f, r, SkFocusIn(h) ? UI_ACC : UI_LINE2, IsWindowEnabled(h) ? 255 : 140);
    UiRRect(&d.cv, 1.5f, 1.5f, w - 1.5f, hh - 1.5f, r - 1, UI_PANEL, 255);
    if (chevron) {
        float cx = w - (float)SkS(h, 14), cy = hh / 2.0f, a = (float)SkS(h, 4), lw = SkS(h, 1) * 1.5f;
        UiLine(&d.cv, cx - a, cy - a / 2, cx, cy + a / 2, lw, UI_MUT, 255);
        UiLine(&d.cv, cx, cy + a / 2, cx + a, cy - a / 2, lw, UI_MUT, 255);
    }
    UiDibOpaque(&d);
    if (skip) ExcludeClipRect(dc, skip->left, skip->top, skip->right, skip->bottom);
    BitBlt(dc, 0, 0, w, hh, d.dc, 0, 0, SRCCOPY);
    UiDibFree(&d);
}
static LRESULT CALLBACK SkEditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    switch (msg) {
    case WM_NCCALCSIZE: {
        RECT *r = (RECT *)lp;
        HFONT f = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
        HDC dc = GetDC(h); HGDIOBJ o = SelectObject(dc, f ? f : GetStockObject(DEFAULT_GUI_FONT));
        TEXTMETRICW tm; GetTextMetricsW(dc, &tm); SelectObject(dc, o); ReleaseDC(h, dc);
        int hh = r->bottom - r->top, padY = (hh - tm.tmHeight) / 2; if (padY < 1) padY = 1;
        r->left += SkS(h, 10); r->right -= SkS(h, 8); r->top += padY; r->bottom -= padY;
        if (r->bottom < r->top) r->bottom = r->top;
        return 0;
    }
    case WM_NCPAINT: {
        RECT wr, cr; GetWindowRect(h, &wr); GetClientRect(h, &cr);
        POINT o = { 0, 0 }; ClientToScreen(h, &o);
        RECT cl = { o.x - wr.left, o.y - wr.top, o.x - wr.left + cr.right, o.y - wr.top + cr.bottom };
        HDC dc = GetWindowDC(h);
        SkPaintBox(h, dc, wr.right - wr.left, wr.bottom - wr.top, FALSE, &cl);
        ReleaseDC(h, dc);
        return 0;
    }
    case WM_SETFOCUS: case WM_KILLFOCUS: case WM_ENABLE: {
        LRESULT r = DefSubclassProc(h, msg, wp, lp);
        SetWindowPos(h, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_DRAWFRAME);
        return r;
    }
    case WM_NCDESTROY: RemoveWindowSubclass(h, SkEditProc, 2); break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}
/* ------------------------------------------------------------------ drop-down text too long to show: it scrolls
   While a drop-down's list is open, the hovered row whose text doesn't fit slides slowly left so it can be read
   (a pause, the slide, a pause at the end, then again); the same for the closed box while the mouse is over it.
   One at a time: item -2 = the closed box. */
static struct { HWND combo; int item; DWORD t0; UINT_PTR timer; } sMq;
static void SkMqStop(void) { if (sMq.timer) KillTimer(NULL, sMq.timer); memset(&sMq, 0, sizeof sMq); }
static int SkMqOffset(HWND h, int over) {
    int speed = SkS(h, 45), pause = 900, endPause = 1300;               // pixels a second, milliseconds
    int run = over * 1000 / (speed > 0 ? speed : 45);
    DWORD el = (GetTickCount() - sMq.t0) % (DWORD)(pause + run + endPause);
    if ((int)el < pause) return 0;
    el -= pause; if ((int)el < run) return (int)el * speed / 1000;
    return over;
}
static void CALLBACK SkMqTick(HWND hw, UINT m, UINT_PTR id, DWORD t) {
    (void)hw; (void)m; (void)id; (void)t;
    if (!sMq.combo || !IsWindow(sMq.combo)) { SkMqStop(); return; }
    int dropped = (int)SendMessageW(sMq.combo, CB_GETDROPPEDSTATE, 0, 0);
    if (sMq.item == -2) {
        POINT pt; GetCursorPos(&pt); RECT r; GetWindowRect(sMq.combo, &r);
        if (dropped || !PtInRect(&r, pt)) { HWND c = sMq.combo; SkMqStop(); InvalidateRect(c, NULL, FALSE); return; }
        InvalidateRect(sMq.combo, NULL, FALSE);
    } else {
        if (!dropped) { SkMqStop(); return; }
        COMBOBOXINFO ci = { sizeof ci }; GetComboBoxInfo(sMq.combo, &ci);
        RECT r; if (ci.hwndList && SendMessageW(ci.hwndList, LB_GETITEMRECT, sMq.item, (LPARAM)&r) != LB_ERR) InvalidateRect(ci.hwndList, &r, FALSE);
    }
}
static void SkMqStart(HWND c, int item) {
    if (sMq.combo == c && sMq.item == item) return;
    sMq.combo = c; sMq.item = item; sMq.t0 = GetTickCount();
    if (!sMq.timer) sMq.timer = SetTimer(NULL, 0, 30, SkMqTick);
}

static LRESULT CALLBACK SkComboProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    if (UI_DROP_MSG(msg)) { LRESULT r = UiDropNoSlide(h, msg, wp, lp); InvalidateRect(h, NULL, FALSE); return r; }
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        COMBOBOXINFO ci = { sizeof ci }; GetComboBoxInfo(h, &ci);
        // a drop-down with a typing box has an edit inside that draws the text; a pick-only one reports itself
        if (ci.hwndItem && ci.hwndItem != h) SkPaintBox(h, dc, rc.right, rc.bottom, TRUE, &ci.rcItem);
        else {
            SkPaintBox(h, dc, rc.right, rc.bottom, TRUE, NULL);
            int sel = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
            if (sel >= 0) {
                int n = (int)SendMessageW(h, CB_GETLBTEXTLEN, sel, 0);
                wchar_t *t = (wchar_t *)calloc((size_t)(n > 0 ? n : 0) + 2, sizeof(wchar_t));
                if (t) {
                    SendMessageW(h, CB_GETLBTEXT, sel, (LPARAM)t);
                    HFONT of = (HFONT)SelectObject(dc, (HFONT)SendMessageW(h, WM_GETFONT, 0, 0));
                    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, IsWindowEnabled(h) ? UI_INK : UI_DIM);
                    RECT tr = { SkS(h, 10), 0, rc.right - SkS(h, 26), rc.bottom };
                    SIZE ts; GetTextExtentPoint32W(dc, t, (int)wcslen(t), &ts);
                    int over = ts.cx - (tr.right - tr.left);
                    if (over > 0 && sMq.combo == h && sMq.item == -2) {       // the mouse is over it: scroll instead of "..."
                        int sv = SaveDC(dc); IntersectClipRect(dc, tr.left, 0, tr.right, rc.bottom);
                        tr.left -= SkMqOffset(h, over); tr.right = tr.left + ts.cx + 10;
                        DrawTextW(dc, t, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                        RestoreDC(dc, sv);
                    } else DrawTextW(dc, t, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
                    SelectObject(dc, of); free(t);
                }
            }
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE:                                                    // hovering the closed box: a long choice scrolls
        if (!SendMessageW(h, CB_GETDROPPEDSTATE, 0, 0) && !(sMq.combo == h && sMq.item == -2)) { SkMqStart(h, -2); InvalidateRect(h, NULL, FALSE); }
        break;
    case WM_SETFOCUS: case WM_KILLFOCUS: case WM_ENABLE: case CB_SETCURSEL: case CB_SELECTSTRING: case WM_SETTEXT:
    case WM_KEYDOWN: case WM_CHAR: case WM_MOUSEWHEEL: case CB_RESETCONTENT:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_CAPTURECHANGED: {
        LRESULT r = DefSubclassProc(h, msg, wp, lp); InvalidateRect(h, NULL, FALSE); return r;
    }
    case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: { HDC dc = (HDC)wp; SkBrushes(); SetTextColor(dc, UI_INK); SetBkColor(dc, UI_PANEL); return (LRESULT)sPanel; }
    case WM_NCDESTROY: RemoveWindowSubclass(h, SkComboProc, 3); break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}
// size a drop-down so its closed height matches a button's (rh = the viewer's row height)
static void SkComboHeight(HWND cb, int rh) {
    SendMessageW(cb, CB_SETITEMHEIGHT, (WPARAM)-1, rh - 6);
    RECT r; GetWindowRect(cb, &r);
    int got = r.bottom - r.top;
    if (got != rh) SendMessageW(cb, CB_SETITEMHEIGHT, (WPARAM)-1, rh - 6 + (rh - got));
}

/* ------------------------------------------------------------------ owner-drawn list rows (lists and drop-down lists) */
static void SkDrawListRow(DRAWITEMSTRUCT *di, BOOL combo) {
    if (di->itemID == (UINT)-1) return;
    HDC dc = di->hDC; RECT rc = di->rcItem;
    if (combo && (di->itemState & ODS_COMBOBOXEDIT)) { InvalidateRect(di->hwndItem, NULL, FALSE); return; }   // the closed box: drawn in its WM_PAINT
    BOOL sel = (di->itemState & ODS_SELECTED) != 0;
    BOOL hot = !combo && !sel && (int)(INT_PTR)GetPropW(di->hwndItem, L"ns2digHot") - 1 == (int)di->itemID;
    SkBrushes();
    HBRUSH br = CreateSolidBrush(sel ? UI_SEL : hot ? SK_HOVER : UI_PANEL); FillRect(dc, &rc, br); DeleteObject(br);
    if (sel) { RECT bar = { rc.left, rc.top, rc.left + SkS(di->hwndItem, 3), rc.bottom }; HBRUSH ab = CreateSolidBrush(UI_ACC); FillRect(dc, &bar, ab); DeleteObject(ab); }
    int n = (int)SendMessageW(di->hwndItem, combo ? CB_GETLBTEXTLEN : LB_GETTEXTLEN, di->itemID, 0);
    if (n < 0) return;
    wchar_t *t = (wchar_t *)calloc((size_t)n + 2, sizeof(wchar_t)); if (!t) return;
    SendMessageW(di->hwndItem, combo ? CB_GETLBTEXT : LB_GETTEXT, di->itemID, (LPARAM)t);
    HFONT of = (HFONT)SelectObject(dc, (HFONT)SendMessageW(di->hwndItem, WM_GETFONT, 0, 0));
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, UI_INK);
    RECT tr = { rc.left + SkS(di->hwndItem, 10), rc.top, rc.right + 100000, rc.bottom };
    int saved = 0;
    if (combo) {                                                          // too long for the row: the hovered one scrolls
        SIZE ts; GetTextExtentPoint32W(dc, t, (int)wcslen(t), &ts);
        int avail = rc.right - tr.left - SkS(di->hwndItem, 8), over = ts.cx - avail;
        if (sel && over > 0) {
            SkMqStart(di->hwndItem, (int)di->itemID);
            saved = SaveDC(dc); IntersectClipRect(dc, tr.left, rc.top, rc.right - SkS(di->hwndItem, 4), rc.bottom);
            tr.left -= SkMqOffset(di->hwndItem, over);
        } else if (sel && sMq.combo == di->hwndItem && sMq.item >= 0) SkMqStop();
    }
    DrawTextW(dc, t, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_EXPANDTABS);
    if (saved) RestoreDC(dc, saved);
    SelectObject(dc, of); free(t);
}

// the open list of a drop-down: Windows' own border shows light while the list slides open, so the list has none
// and a coral 1-pixel outline is drawn along the edges of its contents instead (also into the animation frames)
// The open list of a drop-down keeps Windows' 1-pixel border (outside the rows and the scroll bar, so scrolling
// never moves it) and that border is painted coral. On Windows 11 the system also draws its own outline round
// pop-up lists (it showed as a light line while the list slid open): that one is set to coral too.
static void SkDropBorderWin(HWND h, HDC dc) {
    RECT wr; GetWindowRect(h, &wr); OffsetRect(&wr, -wr.left, -wr.top);
    HBRUSH b = CreateSolidBrush(UI_ACC); FrameRect(dc, &wr, b); DeleteObject(b);
}
static LRESULT CALLBACK SkDropListProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    switch (msg) {
    case WM_NCPAINT: { LRESULT r = DefSubclassProc(h, msg, wp, lp); HDC dc = GetWindowDC(h); if (dc) { SkDropBorderWin(h, dc); ReleaseDC(h, dc); } return r; }
    case WM_PRINT: { LRESULT r = DefSubclassProc(h, msg, wp, lp); if (lp & PRF_NONCLIENT) SkDropBorderWin(h, (HDC)wp); return r; }
    case WM_ERASEBKGND: { RECT rc; GetClientRect(h, &rc); SkBrushes(); FillRect((HDC)wp, &rc, sPanel); return 1; }
    case WM_NCDESTROY: RemoveWindowSubclass(h, SkDropListProc, 6); break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}
// used by the main window too: style a drop-down's list
void UiStyleDropList(HWND combo) {
    COMBOBOXINFO ci = { sizeof ci };
    if (!GetComboBoxInfo(combo, &ci) || !ci.hwndList) return;
    UiDarkControl(ci.hwndList, L"DarkMode_Explorer");
    typedef BOOL (WINAPI *AllowFn)(HWND, BOOL);
    HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
    AllowFn af = ux ? (AllowFn)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(133)) : NULL;
    if (af) { af(ci.hwndList, TRUE); SendMessageW(ci.hwndList, WM_THEMECHANGED, 0, 0); }
    LONG_PTR ls = GetWindowLongPtrW(ci.hwndList, GWL_STYLE);
    SetWindowLongPtrW(ci.hwndList, GWL_STYLE, ls | WS_BORDER);
    SetWindowPos(ci.hwndList, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    SetWindowSubclass(ci.hwndList, SkDropListProc, 6, 0);
    {
        HMODULE dwm = LoadLibraryExW(L"dwmapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        typedef HRESULT (WINAPI *DwmSetFn)(HWND, DWORD, LPCVOID, DWORD);
        DwmSetFn set = dwm ? (DwmSetFn)(void *)GetProcAddress(dwm, "DwmSetWindowAttribute") : NULL;
        if (set) { COLORREF c = UI_ACC; set(ci.hwndList, 34 /* DWMWA_BORDER_COLOR */, &c, sizeof c); }
    }
    if (GetWindowLongPtrW(combo, GWL_STYLE) & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) SendMessageW(combo, CB_SETITEMHEIGHT, 0, SkS(combo, 24));
}
// rows of an owner-drawn drop-down list (main window and viewers)
BOOL UiSkinComboRow(DRAWITEMSTRUCT *di) { if (di->CtlType != ODT_COMBOBOX) return FALSE; SkDrawListRow(di, TRUE); return TRUE; }

// owner-drawn lists: remember the row under the mouse so it can be drawn in the hover grey
static void SkLbHot(HWND h, int idx) {
    int old = (int)(INT_PTR)GetPropW(h, L"ns2digHot") - 1;
    if (old == idx) return;
    SetPropW(h, L"ns2digHot", (HANDLE)(INT_PTR)(idx + 1));
    RECT r;
    if (old >= 0 && SendMessageW(h, LB_GETITEMRECT, old, (LPARAM)&r) != LB_ERR) InvalidateRect(h, &r, FALSE);
    if (idx >= 0 && SendMessageW(h, LB_GETITEMRECT, idx, (LPARAM)&r) != LB_ERR) InvalidateRect(h, &r, FALSE);
}
static LRESULT CALLBACK SkListHoverProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    if (msg == WM_MOUSEMOVE) {
        LRESULT hit = SendMessageW(h, LB_ITEMFROMPOINT, 0, lp);
        SkLbHot(h, HIWORD(hit) ? -1 : (int)LOWORD(hit));
        TRACKMOUSEEVENT tm = { sizeof tm, TME_LEAVE, h, 0 }; TrackMouseEvent(&tm);
    } else if (msg == WM_MOUSELEAVE) SkLbHot(h, -1);
    else if (msg == WM_MOUSEWHEEL || msg == WM_VSCROLL) { LRESULT r = DefSubclassProc(h, msg, wp, lp); SkLbHot(h, -1); return r; }
    else if (msg == WM_NCDESTROY) { RemovePropW(h, L"ns2digHot"); RemoveWindowSubclass(h, SkListHoverProc, 8); }
    return DefSubclassProc(h, msg, wp, lp);
}

/* ------------------------------------------------------------------ sliders */
// the slider only repaints the bits around its handle when it moves: repaint all of it so the coral fill keeps up
static LRESULT CALLBACK SkTrackProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    switch (msg) {
    case TBM_SETPOS: case TBM_SETPOSNOTIFY: case TBM_SETRANGE: case TBM_SETRANGEMIN: case TBM_SETRANGEMAX:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_MOUSEMOVE: case WM_KEYDOWN: case WM_MOUSEWHEEL: case WM_ENABLE: {
        if (msg == WM_MOUSEMOVE && !(wp & MK_LBUTTON)) break;
        LRESULT r = DefSubclassProc(h, msg, wp, lp);
        InvalidateRect(h, NULL, FALSE);
        return r;
    }
    case WM_ERASEBKGND: return 1;
    case WM_NCDESTROY: RemoveWindowSubclass(h, SkTrackProc, 7); break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}
static LRESULT SkTrackbarDraw(NMCUSTOMDRAW *cd) {
    HWND h = cd->hdr.hwndFrom;
    switch (cd->dwDrawStage) {
    case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
        RECT r = cd->rc; int w = r.right - r.left, hh = r.bottom - r.top;
        if (cd->dwItemSpec == TBCD_TICS) return CDRF_SKIPDEFAULT;
        if (w <= 0 || hh <= 0) return CDRF_SKIPDEFAULT;
        if (cd->dwItemSpec == TBCD_CHANNEL) {
            // the whole slider background, then a rounded track filled in coral up to the thumb
            RECT cr; GetClientRect(h, &cr);
            UiDib d = { 0 };
            if (UiDibMake(&d, cr.right, cr.bottom, cd->hdc)) {
                UiDibClear(&d, UI_BG, 1);
                float th = (float)SkS(h, 4), cy = (r.top + r.bottom) / 2.0f;
                UiRRect(&d.cv, (float)r.left, cy - th / 2, (float)r.right, cy + th / 2, th / 2, UI_PANEL2, 255);
                RECT tb; SendMessageW(h, TBM_GETTHUMBRECT, 0, (LPARAM)&tb);
                float tx = (tb.left + tb.right) / 2.0f;
                if (tx > r.left) UiRRect(&d.cv, (float)r.left, cy - th / 2, tx, cy + th / 2, th / 2, IsWindowEnabled(h) ? UI_ACC : UI_DIM, 255);
                BitBlt(cd->hdc, 0, 0, cr.right, cr.bottom, d.dc, 0, 0, SRCCOPY);
                UiDibFree(&d);
            }
            return CDRF_SKIPDEFAULT;
        }
        if (cd->dwItemSpec == TBCD_THUMB) {
            UiDib d = { 0 };
            if (UiDibMake(&d, w, hh, cd->hdc)) {
                BitBlt(d.dc, 0, 0, w, hh, cd->hdc, r.left, r.top, SRCCOPY);       // what's behind it
                for (int i = 0; i < w * hh; i++) d.cv.px[i] |= 0xFF000000u;
                float rr = (w < hh ? w : hh) / 2.0f - 1, cx = w / 2.0f, cy = hh / 2.0f;
                BOOL hot = (cd->uItemState & (CDIS_HOT | CDIS_SELECTED)) != 0;
                UiCircle(&d.cv, cx, cy, rr, IsWindowEnabled(h) ? (hot ? UI_ACC_HOT : UI_ACC) : UI_DIM, 255);
                UiCircle(&d.cv, cx, cy, rr * 0.38f, UI_ACC_INK, 255);
                BitBlt(cd->hdc, r.left, r.top, w, hh, d.dc, 0, 0, SRCCOPY);
                UiDibFree(&d);
            }
            return CDRF_SKIPDEFAULT;
        }
        return CDRF_DODEFAULT;
    }
    }
    return CDRF_DODEFAULT;
}

/* ------------------------------------------------------------------ progress bars */
static LRESULT CALLBACK SkProgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        UiDib d = { 0 };
        if (rc.right > 0 && rc.bottom > 0 && UiDibMake(&d, rc.right, rc.bottom, dc)) {
            UiDibClear(&d, UI_BG, 1);
            int lo = (int)SendMessageW(h, PBM_GETRANGE, TRUE, 0), hi = (int)SendMessageW(h, PBM_GETRANGE, FALSE, 0), pos = (int)SendMessageW(h, PBM_GETPOS, 0, 0);
            float th = (float)SkS(h, 6); if (th > rc.bottom) th = (float)rc.bottom; float y0 = (rc.bottom - th) / 2;
            UiRRect(&d.cv, 0, y0, (float)rc.right, y0 + th, th / 2, UI_PANEL2, 255);
            if (hi > lo && pos > lo) { float fw = (float)rc.right * (pos - lo) / (hi - lo); if (fw < th) fw = th; UiRRect(&d.cv, 0, y0, fw, y0 + th, th / 2, UI_ACC, 255); }
            BitBlt(dc, 0, 0, rc.right, rc.bottom, d.dc, 0, 0, SRCCOPY);
            UiDibFree(&d);
        }
        EndPaint(h, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == PBM_SETPOS || msg == PBM_DELTAPOS || msg == PBM_STEPIT || msg == PBM_SETRANGE32) { LRESULT r = DefSubclassProc(h, msg, wp, lp); InvalidateRect(h, NULL, FALSE); return r; }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, SkProgProc, 5);
    return DefSubclassProc(h, msg, wp, lp);
}

/* ------------------------------------------------------------------ the viewer window itself */
static int SkRowHeight(HWND top) {
    // the height of the viewer's buttons, so text boxes and drop-downs match them
    HWND c = GetWindow(top, GW_CHILD); int best = 0;
    while (c) {
        wchar_t cls[32]; GetClassNameW(c, cls, 32);
        DWORD type = (DWORD)GetWindowLongPtrW(c, GWL_STYLE) & BS_TYPEMASK;
        if (!_wcsicmp(cls, L"Button") && (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON || type == BS_OWNERDRAW)) { RECT r; GetWindowRect(c, &r); if (r.bottom - r.top > best) best = r.bottom - r.top; }
        c = GetWindow(c, GW_HWNDNEXT);
    }
    return best;
}
// list views: Windows' theme paints selected rows blue whatever colours are asked for, so selected rows are
// painted over afterwards (coral tint, the row's text again). Cells the viewer draws itself are left alone.
// the list view's own text margin: twice the system edge width (scaled for the window's DPI)
static int SkEdge(HWND h) {
    typedef int (WINAPI *MetricsForDpiFn)(int, UINT);
    static MetricsForDpiFn f = NULL; static int tried = 0;
    if (!tried) { f = (MetricsForDpiFn)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi"); tried = 1; }
    typedef UINT (WINAPI *GetDpiFn)(HWND);
    GetDpiFn gd = (GetDpiFn)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    UINT dpi = gd ? gd(h) : 96; if (!dpi) dpi = 96;
    return f ? f(SM_CXEDGE, dpi) : GetSystemMetrics(SM_CXEDGE);
}
// text of one cell, placed and aligned the way the list view itself places it
static void SkLvCell(HWND lv, HDC dc, int item, int col, const RECT *cell) {
    wchar_t t[600]; t[0] = 0; ListView_GetItemText(lv, item, col, t, 600);
    LVCOLUMNW lc = { 0 }; lc.mask = LVCF_FMT; ListView_GetColumn(lv, col, &lc);
    UINT al = (lc.fmt & LVCFMT_JUSTIFYMASK) == LVCFMT_RIGHT ? DT_RIGHT : (lc.fmt & LVCFMT_JUSTIFYMASK) == LVCFMT_CENTER ? DT_CENTER : DT_LEFT;
    if (col == 0) al = DT_LEFT;
    int e = SkEdge(lv);
    HFONT of = (HFONT)SelectObject(dc, (HFONT)SendMessageW(lv, WM_GETFONT, 0, 0));
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, UI_INK);
    RECT tr = *cell;
    if (col == 0) { tr.left += e; tr.right -= e; }
    else { tr.left += 2 * e + 2; tr.right -= (al == DT_RIGHT) ? 2 * e + 4 : 2 * e + 2; }   // measured against Windows' own
    DrawTextW(dc, t, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS | al);
    SelectObject(dc, of);
}
static LRESULT SkListViewDraw(HWND top, UINT msg, WPARAM wp, LPARAM lp) {
    NMLVCUSTOMDRAW *cd = (NMLVCUSTOMDRAW *)lp;
    HWND lv = cd->nmcd.hdr.hwndFrom;
    LRESULT r = DefSubclassProc(top, msg, wp, lp);                 // the viewer's own drawing first
    DWORD st = cd->nmcd.dwDrawStage;
    int item = (int)cd->nmcd.dwItemSpec;
    BOOL plain = GetPropW(lv, SK_PROP) == (HANDLE)2;                 // tick lists: no highlight at all
    BOOL sel = (st & CDDS_ITEM) && (ListView_GetItemState(lv, item, LVIS_SELECTED) & LVIS_SELECTED);
    BOOL hot = (st & CDDS_ITEM) && ((cd->nmcd.uItemState & CDIS_HOT) || ListView_GetHotItem(lv) == item);   // per-cell drawing only marks the cell under the mouse
    if (plain) sel = FALSE;                                          // tick lists: clicking ticks, so no highlight
    BOOL mark = sel || hot;
    if (st == CDDS_PREPAINT) return r | CDRF_NOTIFYITEMDRAW;
    if (st == CDDS_ITEMPREPAINT) {
        if (r & CDRF_SKIPDEFAULT) return r;
        cd->nmcd.uItemState &= ~CDIS_FOCUS;
        if (mark) return r | ((r & CDRF_NOTIFYSUBITEMDRAW) ? 0 : CDRF_NOTIFYPOSTPAINT);
        return r;
    }
    if (st == (CDDS_SUBITEM | CDDS_ITEMPREPAINT)) {
        if (mark && !(r & CDRF_SKIPDEFAULT)) r |= CDRF_NOTIFYPOSTPAINT;
        return r;
    }
    if (!mark) return r;
    HDC dc = cd->nmcd.hdc;
    COLORREF bgc = sel ? UI_SEL : SK_HOVER;
    if (st == CDDS_ITEMPOSTPAINT) {                                 // the whole row
        RECT row; ListView_GetItemRect(lv, item, &row, LVIR_BOUNDS);
        HWND hd = ListView_GetHeader(lv); int nc = hd ? Header_GetItemCount(hd) : 1; if (nc < 1) nc = 1;
        RECT lab0; ListView_GetItemRect(lv, item, &lab0, LVIR_LABEL);
        RECT fill = row; if (plain) fill.left = lab0.left;           // tick lists keep their tick box
        HBRUSH b = CreateSolidBrush(bgc); FillRect(dc, &fill, b); DeleteObject(b);
        if (sel) { RECT bar = { row.left, row.top, row.left + SkS(lv, 3), row.bottom }; HBRUSH ab = CreateSolidBrush(UI_ACC); FillRect(dc, &bar, ab); DeleteObject(ab); }
        HIMAGELIST il = ListView_GetImageList(lv, LVSIL_SMALL);
        if (il && !plain) {
            LVITEMW it = { 0 }; it.mask = LVIF_IMAGE; it.iItem = item; ListView_GetItem(lv, &it);
            RECT ir; ListView_GetItemRect(lv, item, &ir, LVIR_ICON);
            int iw, ih; ImageList_GetIconSize(il, &iw, &ih);
            if (it.iImage >= 0) ImageList_Draw(il, it.iImage, dc, ir.left + (ir.right - ir.left - iw) / 2, ir.top + (ir.bottom - ir.top - ih) / 2, ILD_TRANSPARENT);
        }
        for (int c = 0; c < nc; c++) {
            RECT cell; if (c == 0) cell = lab0; else ListView_GetSubItemRect(lv, item, c, LVIR_BOUNDS, &cell);
            if (c == 0 && nc > 1) { RECT c1; ListView_GetSubItemRect(lv, item, 1, LVIR_BOUNDS, &c1); if (c1.left > cell.left) cell.right = c1.left; }
            SkLvCell(lv, dc, item, c, &cell);
        }
        return r;
    }
    if (st == (CDDS_SUBITEM | CDDS_ITEMPOSTPAINT)) {                // one cell the viewer left to Windows
        int c = cd->iSubItem; RECT cell;
        RECT fill;
        if (c == 0) {                                               // fill from the row's left edge, text where Windows puts it
            ListView_GetItemRect(lv, item, &cell, LVIR_LABEL); RECT row; ListView_GetItemRect(lv, item, &row, LVIR_BOUNDS);
            fill = cell; fill.left = row.left;
            HWND hd = ListView_GetHeader(lv);
            if (hd && Header_GetItemCount(hd) > 1) { RECT c1; ListView_GetSubItemRect(lv, item, 1, LVIR_BOUNDS, &c1); if (c1.left > cell.left) { cell.right = c1.left; fill.right = c1.left; } }
        } else { ListView_GetSubItemRect(lv, item, c, LVIR_BOUNDS, &cell); fill = cell; }
        HBRUSH b = CreateSolidBrush(bgc); FillRect(dc, &fill, b); DeleteObject(b);
        if (c == 0 && sel) { RECT bar = { fill.left, fill.top, fill.left + SkS(lv, 3), fill.bottom }; HBRUSH ab = CreateSolidBrush(UI_ACC); FillRect(dc, &bar, ab); DeleteObject(ab); }
        SkLvCell(lv, dc, item, c, &cell);
        return r;
    }
    return r;
}

static LRESULT CALLBACK SkTopProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    switch (msg) {
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN: {
        HDC dc = (HDC)wp; HWND ctl = (HWND)lp; SkBrushes();
        wchar_t cls[32]; GetClassNameW(ctl, cls, 32);
        BOOL panel = msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX || (!_wcsicmp(cls, L"Edit"));   // read-only edits send CTLCOLORSTATIC
        SetTextColor(dc, panel ? UI_INK : (IsWindowEnabled(ctl) ? UI_INK : UI_DIM));
        SetBkColor(dc, panel ? UI_PANEL : UI_BG);
        return (LRESULT)(panel ? sPanel : sBg);
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)lp;
        if (di->CtlType == ODT_BUTTON) { SkBtn *b = (SkBtn *)GetPropW(di->hwndItem, SK_PROP); if (b) { SkDrawButton(di, b); return TRUE; } }
        if (di->CtlType == ODT_LISTBOX && GetPropW(di->hwndItem, SK_PROP)) { SkDrawListRow(di, FALSE); return TRUE; }
        if (di->CtlType == ODT_COMBOBOX && GetPropW(di->hwndItem, SK_PROP)) { SkDrawListRow(di, TRUE); return TRUE; }
        break;
    }
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mi = (MEASUREITEMSTRUCT *)lp;
        if (mi->CtlType == ODT_LISTBOX || mi->CtlType == ODT_COMBOBOX) { mi->itemHeight = SkS(h, 24); return TRUE; }
        break;
    }
    case WM_COMMAND:
        if (lp && (HIWORD(wp) == CBN_SELCHANGE || HIWORD(wp) == CBN_CLOSEUP || HIWORD(wp) == CBN_SELENDOK || HIWORD(wp) == CBN_SETFOCUS || HIWORD(wp) == CBN_KILLFOCUS)) {
            wchar_t cls[32]; GetClassNameW((HWND)lp, cls, 32);
            if (!_wcsicmp(cls, L"ComboBox")) InvalidateRect((HWND)lp, NULL, FALSE);
        }
        break;
    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)lp;
        if (nh->code == NM_CLICK && GetPropW(nh->hwndFrom, SK_PROP) == (HANDLE)2) {
            NMITEMACTIVATE *ia = (NMITEMACTIVATE *)lp;
            LVHITTESTINFO ht = { 0 }; ht.pt = ia->ptAction;
            int it = ListView_SubItemHitTest(nh->hwndFrom, &ht);
            if (it >= 0 && !(ht.flags & LVHT_ONITEMSTATEICON) && !(ht.iSubItem == 0 && GetPropW(nh->hwndFrom, L"ns2digOwnCol0")))   // the tick box itself toggles on its own; (lists with groups handle their first column)
                ListView_SetCheckState(nh->hwndFrom, it, !ListView_GetCheckState(nh->hwndFrom, it));
        }
        if (nh->code == NM_CUSTOMDRAW) {
            wchar_t cls[40]; GetClassNameW(nh->hwndFrom, cls, 40);
            if (!_wcsicmp(cls, TRACKBAR_CLASSW)) return SkTrackbarDraw((NMCUSTOMDRAW *)lp);
            BOOL lv = !_wcsicmp(cls, WC_LISTVIEWW), tv = !_wcsicmp(cls, WC_TREEVIEWW);
            if (lv) return SkListViewDraw(h, msg, wp, lp);
            if (tv) {
                NMTVCUSTOMDRAW *t = (NMTVCUSTOMDRAW *)lp;
                LRESULT r = DefSubclassProc(h, msg, wp, lp);
                if (t->nmcd.dwDrawStage == CDDS_PREPAINT) return r | CDRF_NOTIFYITEMDRAW;
                if (t->nmcd.dwDrawStage == CDDS_ITEMPREPAINT && (t->nmcd.uItemState & CDIS_SELECTED)) {
                    t->nmcd.uItemState &= ~(CDIS_SELECTED | CDIS_FOCUS); t->clrTextBk = UI_SEL; t->clrText = UI_INK; r |= CDRF_NEWFONT;
                }
                return r;
            }
        }
        break;
    }
    case WM_NCDESTROY: RemoveWindowSubclass(h, SkTopProc, 77); break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

static LRESULT CALLBACK SkLvProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    if (msg == WM_NOTIFY) {
        NMHDR *nh = (NMHDR *)lp;
        if (nh->hwndFrom == ListView_GetHeader(h) && nh->code == NM_CUSTOMDRAW) {
            NMCUSTOMDRAW *cd = (NMCUSTOMDRAW *)lp;
            if (cd->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (cd->dwDrawStage == CDDS_ITEMPREPAINT) { SetTextColor(cd->hdc, UI_MUT); return CDRF_DODEFAULT; }
        }
    } else if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, SkLvProc, 10);
    return DefSubclassProc(h, msg, wp, lp);
}

// tick box images: unticked, ticked (trees want an empty first image)
static HIMAGELIST SkTickImages(HWND c, BOOL tree) {
    int sz = SkS(c, 16);
    HIMAGELIST il = ImageList_Create(sz, sz, ILC_COLOR32, 3, 0);
    if (!il) return NULL;
    HDC dc = GetDC(c);
    for (int k = tree ? -1 : 0; k < 2; k++) {
        UiDib d = { 0 };
        if (!UiDibMake(&d, sz, sz, dc)) continue;
        UiDibClear(&d, 0, 0);
        if (k == 1) {
            UiRRect(&d.cv, 0.5f, 0.5f, sz - 0.5f, sz - 0.5f, (float)SkS(c, 4), UI_ACC, 255);
            float q = sz / 16.0f;
            UiLine(&d.cv, 3.8f * q, 8.3f * q, 6.8f * q, 11.2f * q, 1.9f * q, UI_ACC_INK, 255);
            UiLine(&d.cv, 6.8f * q, 11.2f * q, 12.3f * q, 5.0f * q, 1.9f * q, UI_ACC_INK, 255);
        } else if (k == 0) {
            UiRRect(&d.cv, 0.5f, 0.5f, sz - 0.5f, sz - 0.5f, (float)SkS(c, 4), UI_LINE2, 255);
            UiRRect(&d.cv, 1.5f, 1.5f, sz - 1.5f, sz - 1.5f, (float)SkS(c, 3), UI_PANEL2, 255);
        }
        SelectObject(d.dc, d.old); ImageList_Add(il, d.bmp, NULL); d.old = SelectObject(d.dc, d.bmp);
        UiDibFree(&d);
    }
    ReleaseDC(c, dc);
    return il;
}

static BOOL CALLBACK SkChild(HWND c, LPARAM lp) {
    HWND top = (HWND)lp;
    wchar_t cls[40]; GetClassNameW(c, cls, 40);
    LONG_PTR st = GetWindowLongPtrW(c, GWL_STYLE), ex = GetWindowLongPtrW(c, GWL_EXSTYLE);
    if (!_wcsicmp(cls, L"Button")) SkButton(c);
    else if (!_wcsicmp(cls, L"Edit")) {
        if (GetParent(c) != top) { wchar_t pc[32]; GetClassNameW(GetParent(c), pc, 32); if (!_wcsicmp(pc, L"ComboBox")) return TRUE; }
        if (st & ES_MULTILINE) {                                       // big text areas: flat, dark scroll bars
            SetWindowLongPtrW(c, GWL_EXSTYLE, ex & ~WS_EX_CLIENTEDGE);
            UiDarkControl(c, L"DarkMode_Explorer");
            SendMessageW(c, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(SkS(c, 8), SkS(c, 6)));
        } else {                                                        // one-line boxes: rounded
            SetWindowLongPtrW(c, GWL_EXSTYLE, ex & ~WS_EX_CLIENTEDGE);
            SetWindowLongPtrW(c, GWL_STYLE, st & ~WS_BORDER);
            SetWindowSubclass(c, SkEditProc, 2, 0);
        }
        SetWindowPos(c, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    else if (!_wcsicmp(cls, L"ComboBox")) {
        UiDarkControl(c, L"DarkMode_CFD");
        SetWindowSubclass(c, SkComboProc, 3, 0);
        int rh = SkRowHeight(top); if (rh > 0) { RECT r; GetWindowRect(c, &r); POINT p = { r.left, r.top }; ScreenToClient(GetParent(c), &p); SkComboHeight(c, rh); }
        if ((st & CBS_OWNERDRAWFIXED) && (st & CBS_HASSTRINGS)) SetPropW(c, SK_PROP, (HANDLE)1);
        UiStyleDropList(c);
    }
    else if (!_wcsicmp(cls, L"ListBox")) {
        SetWindowLongPtrW(c, GWL_EXSTYLE, ex & ~WS_EX_CLIENTEDGE);
        UiDarkControl(c, L"DarkMode_Explorer");
        if ((st & LBS_OWNERDRAWFIXED) && (st & LBS_HASSTRINGS)) { SetPropW(c, SK_PROP, (HANDLE)1); SendMessageW(c, LB_SETITEMHEIGHT, 0, SkS(c, 24)); SetWindowSubclass(c, SkListHoverProc, 8, 0); }
        SetWindowPos(c, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    else if (!_wcsicmp(cls, WC_LISTVIEWW)) {
        SetWindowLongPtrW(c, GWL_EXSTYLE, ex & ~WS_EX_CLIENTEDGE);
        UiDarkControl(c, L"DarkMode_Explorer");
        ListView_SetBkColor(c, UI_PANEL); ListView_SetTextBkColor(c, UI_PANEL); ListView_SetTextColor(c, UI_INK);
        HWND hd = ListView_GetHeader(c); if (hd) UiDarkControl(hd, L"DarkMode_ItemsView");
        SetWindowSubclass(c, SkLvProc, 10, 0);
        if (ListView_GetExtendedListViewStyle(c) & LVS_EX_CHECKBOXES) {
            HIMAGELIST il = SkTickImages(c, FALSE);
            HIMAGELIST old = ListView_SetImageList(c, il, LVSIL_STATE);
            if (old) ImageList_Destroy(old);
            SetPropW(c, SK_PROP, (HANDLE)2);           // rows toggle when clicked anywhere
        }
        SetWindowPos(c, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    else if (!_wcsicmp(cls, WC_TREEVIEWW)) {
        if (st & TVS_CHECKBOXES) {
            HIMAGELIST il = SkTickImages(c, TRUE);
            if (il) { HIMAGELIST old = TreeView_SetImageList(c, il, TVSIL_STATE); if (old) ImageList_Destroy(old); }
        }
        SetWindowLongPtrW(c, GWL_EXSTYLE, ex & ~WS_EX_CLIENTEDGE);
        UiDarkControl(c, L"DarkMode_Explorer");
        TreeView_SetBkColor(c, UI_PANEL); TreeView_SetTextColor(c, UI_INK); TreeView_SetLineColor(c, UI_LINE2);
        SetWindowPos(c, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    else if (!_wcsicmp(cls, PROGRESS_CLASSW)) SetWindowSubclass(c, SkProgProc, 5, 0);
    else if (!_wcsicmp(cls, TRACKBAR_CLASSW)) SetWindowSubclass(c, SkTrackProc, 7, 0);
    else if (!_wcsicmp(cls, MSFTEDIT_CLASS) || !_wcsnicmp(cls, L"RichEdit", 8)) {
        SetWindowLongPtrW(c, GWL_EXSTYLE, ex & ~WS_EX_CLIENTEDGE);
        UiDarkControl(c, L"DarkMode_Explorer");
        SendMessageW(c, EM_SETBKGNDCOLOR, 0, UI_PANEL);
        SetWindowPos(c, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    return TRUE;
}

void UiSkinWindow(HWND top) {
    if (!top) return;
    SkBrushes();
    UiDarkTitle(top);
    typedef BOOL (WINAPI *AllowFn)(HWND, BOOL);
    HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
    AllowFn af = ux ? (AllowFn)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(133)) : NULL;
    if (af) af(top, TRUE);
    SetClassLongPtrW(top, GCLP_HBRBACKGROUND, (LONG_PTR)sBg);
    HINSTANCE inst = GetModuleHandleW(NULL);
    HICON big = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    HICON sm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    if (big) SendMessageW(top, WM_SETICON, ICON_BIG, (LPARAM)big);
    if (sm) SendMessageW(top, WM_SETICON, ICON_SMALL, (LPARAM)sm);
    if (!(GetWindowLongPtrW(top, GWL_STYLE) & WS_VISIBLE)) {
        HWND mainw = FindWindowW(L"ns2dig", NULL);
        if (mainw && mainw != top) {
            RECT mr, wr; GetWindowRect(mainw, &mr); GetWindowRect(top, &wr);
            int w = wr.right - wr.left, hh = wr.bottom - wr.top;
            int x = mr.left + (mr.right - mr.left - w) / 2, y = mr.top + (mr.bottom - mr.top - hh) / 2;
            MONITORINFO mi = { sizeof mi }; GetMonitorInfoW(MonitorFromWindow(mainw, MONITOR_DEFAULTTONEAREST), &mi);
            RECT wa = mi.rcWork;
            if (w > wa.right - wa.left) w = wa.right - wa.left;
            if (hh > wa.bottom - wa.top) hh = wa.bottom - wa.top;
            if (x + w > wa.right) x = wa.right - w; if (y + hh > wa.bottom) y = wa.bottom - hh;
            if (x < wa.left) x = wa.left; if (y < wa.top) y = wa.top;
            SetWindowPos(top, NULL, x, y, w, hh, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    SetWindowSubclass(top, SkTopProc, 77, 0);
    EnumChildWindows(top, SkChild, (LPARAM)top);
    RedrawWindow(top, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}
// colours for viewers that draw parts themselves
COLORREF UiSkinColour(int which) {
    switch (which) {
    case 0: return UI_PANEL; case 1: return UI_INK; case 2: return UI_MUT; case 3: return UI_SEL; case 4: return UI_TAGBG;
    case 5: return UI_BG; case 6: return UI_ACC; case 7: return UI_ACC2;
    }
    return UI_INK;
}

/* ------------------------------------------------------------------ dark choice dialog (replaces Windows' task dialog)
   A heading, a line of text, one large button per choice (title + explanation), and Cancel. Returns the chosen
   choice's number (1, 2, ...) or 0 for Cancel / Esc / closing it. */
typedef struct { const wchar_t *const *t, *const *d; int n, def, result, done; HFONT fTitle, fText, fCard; int hot; HWND cards[8]; } SkChoice;
#define SK_CHOICE_CANCEL 900
static LRESULT CALLBACK SkCardProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    (void)id; SkChoice *c = (SkChoice *)ref;
    if (msg == WM_MOUSEMOVE) {
        int me = GetDlgCtrlID(h) - 1;
        if (c->hot != me) { c->hot = me; for (int i = 0; i < c->n; i++) InvalidateRect(c->cards[i], NULL, FALSE); TRACKMOUSEEVENT tm = { sizeof tm, TME_LEAVE, h, 0 }; TrackMouseEvent(&tm); }
    } else if (msg == WM_MOUSELEAVE) { c->hot = -1; InvalidateRect(h, NULL, FALSE); }
    else if (msg == WM_ERASEBKGND) return 1;
    else if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, SkCardProc, 11);
    return DefSubclassProc(h, msg, wp, lp);
}
static void SkDrawCard(SkChoice *c, DRAWITEMSTRUCT *di) {
    int i = (int)di->CtlID - 1; HWND h = di->hwndItem;
    RECT rc = di->rcItem; int w = rc.right - rc.left, hh = rc.bottom - rc.top;
    UiDib d = { 0 }; if (!UiDibMake(&d, w, hh, di->hDC)) return;
    UiDibClear(&d, UI_BG, 1);
    BOOL hot = c->hot == i, focus = (di->itemState & ODS_FOCUS) != 0, down = (di->itemState & ODS_SELECTED) != 0;
    float r = (float)SkS(h, 8);
    COLORREF border = focus || hot ? UI_ACC : UI_LINE;
    UiRRect(&d.cv, 0.5f, 0.5f, w - 0.5f, hh - 0.5f, r, border, 255);
    UiRRect(&d.cv, 1.5f, 1.5f, w - 1.5f, hh - 1.5f, r - 1, down ? UI_SEL : hot ? UI_HOVER : UI_PANEL2, 255);
    int gx = SkS(h, 16), gs = SkS(h, 18);
    UiDrawGlyph(&d.cv, UI_GL_ARROW, (float)gx, (float)SkS(h, 14), (float)gs, UI_ACC);
    UiDibOpaque(&d);
    SetBkMode(d.dc, TRANSPARENT);
    int tx = gx + gs + SkS(h, 12);
    HFONT of = (HFONT)SelectObject(d.dc, c->fCard);
    SetTextColor(d.dc, UI_INK);
    RECT tr = { tx, SkS(h, 10), w - SkS(h, 14), hh };
    DrawTextW(d.dc, c->t[i], -1, &tr, DT_SINGLELINE | DT_NOPREFIX);
    SIZE ts; GetTextExtentPoint32W(d.dc, L"Ag", 2, &ts);
    SelectObject(d.dc, c->fText);
    SetTextColor(d.dc, UI_MUT);
    RECT dr = { tx, SkS(h, 10) + ts.cy + SkS(h, 4), w - SkS(h, 14), hh - SkS(h, 8) };
    DrawTextW(d.dc, c->d[i], -1, &dr, DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(d.dc, of);
    BitBlt(di->hDC, rc.left, rc.top, w, hh, d.dc, 0, 0, SRCCOPY);
    UiDibFree(&d);
}
static LRESULT CALLBACK SkChoiceProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    SkChoice *c = (SkChoice *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE: SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)lp)->lpCreateParams); break;
    case WM_DRAWITEM: { DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)lp; if (c && di->CtlID >= 1 && (int)di->CtlID <= c->n) { SkDrawCard(c, di); return TRUE; } break; }
    case WM_COMMAND:
        if (!c) break;
        if (LOWORD(wp) >= 1 && LOWORD(wp) <= c->n) { c->result = LOWORD(wp); c->done = 1; return 0; }
        if (LOWORD(wp) == SK_CHOICE_CANCEL || LOWORD(wp) == IDCANCEL) { c->result = 0; c->done = 1; return 0; }
        if (LOWORD(wp) == IDOK) { HWND f = GetFocus(); int id = f ? GetDlgCtrlID(f) : 0; c->result = (id >= 1 && id <= c->n) ? id : (id == SK_CHOICE_CANCEL ? 0 : c->def); c->done = 1; return 0; }
        break;
    case WM_CLOSE: if (c) { c->result = 0; c->done = 1; } return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
int UiChoiceDialog(HWND owner, const wchar_t *title, const wchar_t *heading, const wchar_t *text,
                   const wchar_t *const *titles, const wchar_t *const *descs, int n, int def) {
    if (n < 1 || n > 8) return 0;
    static int reg = 0; HINSTANCE in = GetModuleHandleW(NULL);
    SkBrushes();
    if (!reg) {
        WNDCLASSEXW wc = { 0 }; wc.cbSize = sizeof wc; wc.lpfnWndProc = SkChoiceProc; wc.hInstance = in; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = sBg; wc.lpszClassName = L"ns2digChoice"; RegisterClassExW(&wc); reg = 1;
    }
    SkChoice c = { 0 }; c.t = titles; c.d = descs; c.n = n; c.def = def; c.hot = -1;
    // sizes, scaled for the owner's screen
    HWND ref = owner ? owner : GetDesktopWindow();
    int W = SkS(ref, 560), m = SkS(ref, 22), gap = SkS(ref, 10);
    NONCLIENTMETRICSW ncm = { sizeof ncm }; SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    LOGFONTW lf = ncm.lfMessageFont; lf.lfQuality = CLEARTYPE_QUALITY;
    HFONT f;
    lf.lfHeight = -MulDiv(10, SkS(ref, 96), 72); wcscpy(lf.lfFaceName, L"Segoe UI Variable Text");
    f = CreateFontIndirectW(&lf); c.fText = f;
    lf.lfHeight = -MulDiv(15, SkS(ref, 96), 72); lf.lfWeight = FW_SEMIBOLD; wcscpy(lf.lfFaceName, L"Segoe UI Variable Display");
    c.fTitle = CreateFontIndirectW(&lf);
    lf.lfHeight = -MulDiv(12, SkS(ref, 96), 72); lf.lfWeight = FW_SEMIBOLD;
    c.fCard = CreateFontIndirectW(&lf);
    // measure the text so every part fits
    HDC dc = GetDC(ref);
    HGDIOBJ o = SelectObject(dc, c.fTitle); RECT r1 = { 0, 0, W - 2 * m, 0 }; DrawTextW(dc, heading, -1, &r1, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, c.fText); RECT r2 = { 0, 0, W - 2 * m, 0 }; DrawTextW(dc, text ? text : L"", -1, &r2, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    int cardH[8];
    for (int i = 0; i < n; i++) {
        SelectObject(dc, c.fCard); SIZE ts; GetTextExtentPoint32W(dc, L"Ag", 2, &ts);
        SelectObject(dc, c.fText); RECT rd = { 0, 0, W - 2 * m - SkS(ref, 60), 0 }; DrawTextW(dc, descs[i], -1, &rd, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        cardH[i] = SkS(ref, 10) + ts.cy + SkS(ref, 4) + rd.bottom + SkS(ref, 12);
    }
    SelectObject(dc, o); ReleaseDC(ref, dc);
    int rh = SkS(ref, 32), y = m;
    int hHead = r1.bottom, hText = r2.bottom;
    int total = m + hHead + gap + hText + gap * 2; for (int i = 0; i < n; i++) total += cardH[i] + gap; total += gap + rh + m;
    RECT wr = { 0, 0, W, total }; AdjustWindowRectEx(&wr, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    RECT orc; if (owner) GetWindowRect(owner, &orc); else SystemParametersInfoW(SPI_GETWORKAREA, 0, &orc, 0);
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ns2digChoice", title, WS_POPUP | WS_CAPTION | WS_SYSMENU,
                               orc.left + (orc.right - orc.left - ww) / 2, orc.top + (orc.bottom - orc.top - wh) / 2, ww, wh, owner, NULL, in, &c);
    if (!dlg) { DeleteObject(c.fText); DeleteObject(c.fTitle); DeleteObject(c.fCard); return 0; }
    HWND hd = CreateWindowExW(0, L"STATIC", heading, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, m, y, W - 2 * m, hHead, dlg, NULL, in, NULL);
    SendMessageW(hd, WM_SETFONT, (WPARAM)c.fTitle, FALSE); y += hHead + gap;
    HWND tx = CreateWindowExW(0, L"STATIC", text ? text : L"", WS_CHILD | WS_VISIBLE | SS_NOPREFIX, m, y, W - 2 * m, hText, dlg, NULL, in, NULL);
    SendMessageW(tx, WM_SETFONT, (WPARAM)c.fText, FALSE); y += hText + gap * 2;
    for (int i = 0; i < n; i++) {
        c.cards[i] = CreateWindowExW(0, L"BUTTON", titles[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, m, y, W - 2 * m, cardH[i], dlg, (HMENU)(INT_PTR)(i + 1), in, NULL);
        SetWindowSubclass(c.cards[i], SkCardProc, 11, (DWORD_PTR)&c);
        y += cardH[i] + gap;
    }
    y += gap;
    int bw = SkS(ref, 110);
    HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, W - m - bw, y, bw, rh, dlg, (HMENU)(INT_PTR)SK_CHOICE_CANCEL, in, NULL);
    SendMessageW(cancel, WM_SETFONT, (WPARAM)c.fText, FALSE);
    UiSkinWindow(dlg);
    SetWindowPos(dlg, NULL, orc.left + (orc.right - orc.left - ww) / 2, orc.top + (orc.bottom - orc.top - wh) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetFocus(c.cards[def >= 1 && def <= n ? def - 1 : 0]);
    MSG msg; int quit = 0;
    while (!c.done && IsWindow(dlg)) {
        int g = GetMessageW(&msg, NULL, 0, 0);
        if (g <= 0) { quit = 1; break; }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) { c.result = 0; c.done = 1; continue; }
        if (!IsDialogMessageW(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    if (owner) { EnableWindow(owner, TRUE); SetActiveWindow(owner); }
    DestroyWindow(dlg);
    DeleteObject(c.fText); DeleteObject(c.fTitle); DeleteObject(c.fCard);
    if (quit) PostQuitMessage((int)msg.wParam);
    return c.result;
}
