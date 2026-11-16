/* Video player: the game's WebM movies (romfs\Movie\*.webm - VP9, no sound), decoded with libvpx.
   Play / pause, frame stepping, a position slider, looping, playback speed and saving the current frame as PNG. */
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
void UiSkinWindow(HWND top);
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdint.h>
#include <wchar.h>
#include "arc_util.h"
#include "webm_core.h"
#include "vpx/vpx_decoder.h"
#include "vpx/vp8dx.h"
#include "ui_theme.h"
static void xfree(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }
#include "deflate_lite.h"

#define VV_CLASS L"AlsarcVideoView"
#define VV_PANEL_CLASS L"AlsarcVideoPanel"
enum { VV_PANEL = 100, VV_PLAY, VV_PREV, VV_NEXT, VV_POS, VV_TIME, VV_LOOP, VV_SPEED, VV_SAVE, VV_INFO, VV_EXPORT };
#define VV_TIMER 1

typedef struct {
    HWND wnd, panel, play, prev, next, pos, time, loop, speedCb, save, info, exportBtn;
    HFONT font; int dpi;
    wchar_t title[600], name[300];
    unsigned char *file; size_t fileN;
    WmFile wm;
    vpx_codec_ctx_t cx; int cxOk;
    int dec;                       // last frame fed to the decoder (-1 none)
    int shown;                     // frame in rgb
    unsigned char *rgb; int w, h;  // BGRA of the shown frame
    int playing; double t0Frame; DWORD t0; double speed;
    int goal;                      // frame being sought (decoded a little at a time from the timer), -1 none
    int threads;
    char err[200];
} VV;

static int VvS(VV *v, int x) { return MulDiv(x, v->dpi, 96); }

/* ---------------- decoding ---------------- */
static void VvToRgb(VV *v, const vpx_image_t *im) {
    int W = (int)im->d_w, H = (int)im->d_h;
    if (!v->rgb || W != v->w || H != v->h) { free(v->rgb); v->rgb = (unsigned char *)malloc((size_t)W * H * 4); v->w = W; v->h = H; }
    if (!v->rgb) return;
    int hbd = (im->fmt & VPX_IMG_FMT_HIGHBITDEPTH) != 0;
    int sx = im->x_chroma_shift, sy = im->y_chroma_shift;
    for (int y = 0; y < H; y++) {
        const unsigned char *Y = im->planes[0] + (size_t)y * im->stride[0];
        const unsigned char *U = im->planes[1] + (size_t)(y >> sy) * im->stride[1];
        const unsigned char *V = im->planes[2] + (size_t)(y >> sy) * im->stride[2];
        unsigned char *o = v->rgb + (size_t)y * W * 4;
        for (int x = 0; x < W; x++) {
            int yy, uu, vv;
            if (hbd) { yy = ((const uint16_t *)Y)[x] >> (im->bit_depth - 8); uu = ((const uint16_t *)U)[x >> sx] >> (im->bit_depth - 8); vv = ((const uint16_t *)V)[x >> sx] >> (im->bit_depth - 8); }
            else { yy = Y[x]; uu = U[x >> sx]; vv = V[x >> sx]; }
            // BT.709, limited range
            int c = yy - 16, d = uu - 128, e = vv - 128;
            int r = (298 * c + 459 * e + 128) >> 8, g = (298 * c - 55 * d - 136 * e + 128) >> 8, b = (298 * c + 541 * d + 128) >> 8;
            o[0] = (unsigned char)(b < 0 ? 0 : b > 255 ? 255 : b); o[1] = (unsigned char)(g < 0 ? 0 : g > 255 ? 255 : g);
            o[2] = (unsigned char)(r < 0 ? 0 : r > 255 ? 255 : r); o[3] = 255; o += 4;
        }
    }
}
static int VvFeed(VV *v, int i, int convert) {
    if (vpx_codec_decode(&v->cx, v->file + v->wm.f[i].off, (unsigned)v->wm.f[i].size, NULL, 0)) {
        snprintf(v->err, sizeof v->err, "frame %d: %s", i, vpx_codec_error(&v->cx)); return 0;
    }
    v->dec = i;
    vpx_codec_iter_t it = NULL; vpx_image_t *im, *last = NULL;
    while ((im = vpx_codec_get_frame(&v->cx, &it))) last = im;
    if (last && convert) { VvToRgb(v, last); v->shown = i; }
    return 1;
}
// decode towards frame target for at most budget ms (carry on from the last decoded frame, or start again at the keyframe
// before it). Shows the last frame decoded. 1 when target is on screen.
static int VvStep(VV *v, int target, DWORD budget) {
    if (!v->cxOk || !v->wm.nf) return 1;
    if (target < 0) target = 0;
    if (target >= v->wm.nf) target = v->wm.nf - 1;
    if (target == v->shown) return 1;
    int key = 0; for (int k = target; k >= 0; k--) if (v->wm.f[k].key || k == 0) { key = k; break; }
    int from = (v->dec >= 0 && target > v->dec && key <= v->dec + 1) ? v->dec + 1 : key;
    DWORD t0 = GetTickCount(); int i;
    for (i = from; i <= target; i++) {
        int last = i == target || GetTickCount() - t0 >= budget;
        if (!VvFeed(v, i, last)) break;
        if (last) break;
    }
    if (v->panel) InvalidateRect(v->panel, NULL, FALSE);
    return v->shown == target;
}
static void VvGoto(VV *v, int target) { while (!VvStep(v, target, 1000)) if (v->err[0]) break; }
static void VvTimerOn(VV *v) { SetTimer(v->wnd, VV_TIMER, 10, NULL); }
static void VvSeek(VV *v, int target) {                // show target soon; decoding happens in the timer
    if (target < 0) target = 0;
    if (target >= v->wm.nf) target = v->wm.nf - 1;
    v->goal = target; VvTimerOn(v);
}

/* ---------------- ui ---------------- */
static void VvUpdateTime(VV *v) {
    double fps = v->wm.fps > 0 ? v->wm.fps : 60;
    wchar_t t[200];
    if (v->goal >= 0 && v->goal != v->shown)
        swprintf(t, 200, L"frame %d / %d     %.2f / %.2f s     (going to frame %d...)", v->shown + 1, v->wm.nf, v->shown / fps, v->wm.nf / fps, v->goal + 1);
    else swprintf(t, 200, L"frame %d / %d     %.2f / %.2f s", v->shown + 1, v->wm.nf, v->shown / fps, v->wm.nf / fps);
    SetWindowTextW(v->time, t);
    if (v->goal < 0 && GetCapture() != v->pos) SendMessageW(v->pos, TBM_SETPOS, TRUE, v->shown);
    SetWindowTextW(v->play, v->playing ? L"Pause" : L"Play");
}
static void VvSetPlaying(VV *v, int on) {
    if (on && v->shown >= v->wm.nf - 1 && SendMessageW(v->loop, BM_GETCHECK, 0, 0) != BST_CHECKED) VvGoto(v, 0);
    v->playing = on;
    if (on) { v->goal = -1; v->t0 = GetTickCount(); v->t0Frame = v->shown; VvTimerOn(v); }
    else if (v->goal < 0) KillTimer(v->wnd, VV_TIMER);
    VvUpdateTime(v);
}
static void VvTick(VV *v) {
    if (v->goal >= 0) {                                  // seeking: a slice of decoding per tick keeps the window responsive
        if (VvStep(v, v->goal, 40) || v->err[0]) { v->goal = -1; if (!v->playing) KillTimer(v->wnd, VV_TIMER); }
        VvUpdateTime(v); return;
    }
    if (!v->playing) { KillTimer(v->wnd, VV_TIMER); return; }
    double fps = v->wm.fps > 0 ? v->wm.fps : 60;
    double f = v->t0Frame + (GetTickCount() - v->t0) / 1000.0 * fps * v->speed;
    int target = (int)f;
    if (target >= v->wm.nf) {
        if (SendMessageW(v->loop, BM_GETCHECK, 0, 0) == BST_CHECKED) { v->t0 = GetTickCount(); v->t0Frame = 0; target = 0; }
        else { VvGoto(v, v->wm.nf - 1); VvSetPlaying(v, 0); return; }
    }
    if (target != v->shown) {
        // if decoding can't keep up, slow the clock down instead of jumping ahead
        if (!VvStep(v, target, 30) && v->shown >= 0) { v->t0 = GetTickCount(); v->t0Frame = v->shown + 1; }
        VvUpdateTime(v);
    }
}
// the video exactly as the game stores it: a normal WebM (VP9) file that VLC, web browsers and most players open.
// Nothing is re-encoded, so it is the original quality.
static void VvExportVideo(VV *v) {
    if (!v->file || !v->fileN) return;
    int wasPlaying = v->playing; VvSetPlaying(v, 0);
    const wchar_t *leaf = v->name; for (const wchar_t *q = v->name; *q; q++) if (*q == L'/' || *q == L'\\') leaf = q + 1;
    wchar_t path[MAX_PATH]; swprintf(path, MAX_PATH, L"%ls", leaf);
    for (wchar_t *q = path; *q; q++) if (wcschr(L"\\/:*?\"<>|", *q)) *q = L'_';
    { size_t l = wcslen(path); if (l < 5 || _wcsicmp(path + l - 5, L".webm")) wcsncat(path, L".webm", MAX_PATH - l - 1); }
    OPENFILENAMEW of; ZeroMemory(&of, sizeof of);
    of.lStructSize = sizeof of; of.hwndOwner = v->wnd; of.lpstrFilter = L"WebM video (*.webm)\0*.webm\0";
    of.lpstrFile = path; of.nMaxFile = MAX_PATH; of.lpstrDefExt = L"webm"; of.lpstrTitle = L"Export video";
    of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&of)) {
        FILE *f = _wfopen(path, L"wb"); int ok = 0;
        if (f) { ok = fwrite(v->file, 1, v->fileN, f) == v->fileN; if (fclose(f)) ok = 0; }
        if (!ok) MessageBoxW(v->wnd, L"The video couldn't be saved there.", L"Export video", MB_ICONWARNING);
    }
    if (wasPlaying) VvSetPlaying(v, 1);
}
static void VvSavePng(VV *v) {
    if (!v->rgb) return;
    int wasPlaying = v->playing; VvSetPlaying(v, 0);
    wchar_t path[MAX_PATH]; swprintf(path, MAX_PATH, L"%ls_frame%04d.png", v->name, v->shown + 1);
    OPENFILENAMEW of; ZeroMemory(&of, sizeof of);
    of.lStructSize = sizeof of; of.hwndOwner = v->wnd; of.lpstrFilter = L"PNG image (*.png)\0*.png\0";
    of.lpstrFile = path; of.nMaxFile = MAX_PATH; of.lpstrDefExt = L"png"; of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&of)) {
        static int crc = 0; if (!crc) { CrcInit(); crc = 1; }
        int w = v->w, h = v->h; size_t rawN = (size_t)(w * 4 + 1) * h;
        unsigned char *raw = (unsigned char *)malloc(rawN); int ok = 0;
        if (raw) {
            for (int y = 0; y < h; y++) {
                unsigned char *o = raw + (size_t)y * (w * 4 + 1); *o++ = 0;
                const unsigned char *s = v->rgb + (size_t)y * w * 4;
                for (int x = 0; x < w; x++) { o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = 255; o += 4; s += 4; }
            }
            size_t dn = 0; unsigned char *def = DeflateBuf(raw, rawN, &dn);
            uint32_t a1 = 1, a2 = 0; for (size_t i = 0; i < rawN; i++) { a1 = (a1 + raw[i]) % 65521; a2 = (a2 + a1) % 65521; }
            FILE *f = def ? _wfopen(path, L"wb") : NULL;
            if (f) {
                static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 }; fwrite(sig, 1, 8, f);
                #define PUT32(x) { unsigned char q_[4] = { (unsigned char)((x) >> 24), (unsigned char)((x) >> 16), (unsigned char)((x) >> 8), (unsigned char)(x) }; fwrite(q_, 1, 4, f); }
                unsigned char ih[17] = { 'I','H','D','R', (unsigned char)(w >> 24), (unsigned char)(w >> 16), (unsigned char)(w >> 8), (unsigned char)w,
                                         (unsigned char)(h >> 24), (unsigned char)(h >> 16), (unsigned char)(h >> 8), (unsigned char)h, 8, 6, 0, 0, 0 };
                PUT32(13); fwrite(ih, 1, 17, f); PUT32(Crc32(ih, 17));
                unsigned char *z = (unsigned char *)malloc(dn + 10);
                if (z) {
                    memcpy(z, "IDAT", 4); z[4] = 0x78; z[5] = 0x01; memcpy(z + 6, def, dn);
                    z[6 + dn] = (unsigned char)(a2 >> 8); z[7 + dn] = (unsigned char)a2; z[8 + dn] = (unsigned char)(a1 >> 8); z[9 + dn] = (unsigned char)a1;
                    PUT32((uint32_t)(dn + 6)); fwrite(z, 1, dn + 10, f); PUT32(Crc32(z, dn + 10)); free(z);
                    unsigned char ie[4] = { 'I','E','N','D' }; PUT32(0); fwrite(ie, 1, 4, f); PUT32(Crc32(ie, 4));
                    ok = !ferror(f);
                }
                #undef PUT32
                fclose(f);
            }
            xfree(def); free(raw);
        }
        if (!ok) MessageBoxW(v->wnd, L"The PNG couldn't be written.", L"Video", MB_ICONWARNING);
    }
    if (wasPlaying) VvSetPlaying(v, 1);
}

static LRESULT CALLBACK VvPanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    VV *v = (VV *)GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA);
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        if (v && v->rgb) {
            // fit, keeping the shape; black bars around
            double s = (double)rc.right / v->w; if (v->h * s > rc.bottom) s = (double)rc.bottom / v->h;
            int dw = (int)(v->w * s), dh = (int)(v->h * s), dx = (rc.right - dw) / 2, dy = (rc.bottom - dh) / 2;
            HBRUSH bk = (HBRUSH)GetStockObject(BLACK_BRUSH);
            RECT r1 = { 0, 0, rc.right, dy }, r2 = { 0, dy + dh, rc.right, rc.bottom }, r3 = { 0, dy, dx, dy + dh }, r4 = { dx + dw, dy, rc.right, dy + dh };
            FillRect(dc, &r1, bk); FillRect(dc, &r2, bk); FillRect(dc, &r3, bk); FillRect(dc, &r4, bk);
            BITMAPINFO bi; ZeroMemory(&bi, sizeof bi); bi.bmiHeader.biSize = sizeof bi.bmiHeader;
            bi.bmiHeader.biWidth = v->w; bi.bmiHeader.biHeight = -v->h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
            SetStretchBltMode(dc, s < 1 ? HALFTONE : COLORONCOLOR); SetBrushOrgEx(dc, 0, 0, NULL);
            StretchDIBits(dc, dx, dy, dw, dh, 0, 0, v->w, v->h, v->rgb, &bi, DIB_RGB_COLORS, SRCCOPY);
        } else {
            FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
            if (v && v->err[0]) { SetTextColor(dc, RGB(255, 200, 200)); SetBkMode(dc, TRANSPARENT); wchar_t t[300]; swprintf(t, 300, L"%hs", v->err); DrawTextW(dc, t, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
        }
        EndPaint(hwnd, &ps); return 0;
    }
    case WM_LBUTTONDOWN: SetFocus(hwnd); if (v) VvSetPlaying(v, !v->playing); return 0;
    case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_KEYDOWN:
        if (!v) break;
        if (wp == VK_SPACE) VvSetPlaying(v, !v->playing);
        else if (wp == VK_LEFT) { VvSetPlaying(v, 0); VvSeek(v, (v->goal >= 0 ? v->goal : v->shown) - 1); VvUpdateTime(v); }
        else if (wp == VK_RIGHT) { VvSetPlaying(v, 0); VvSeek(v, (v->goal >= 0 ? v->goal : v->shown) + 1); VvUpdateTime(v); }
        else if (wp == VK_HOME) { int pl = v->playing; VvSetPlaying(v, 0); VvGoto(v, 0); if (pl) VvSetPlaying(v, 1); VvUpdateTime(v); }
        else if (wp == VK_END) { VvSetPlaying(v, 0); VvSeek(v, v->wm.nf - 1); VvUpdateTime(v); }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void VvLayout(VV *v) {
    RECT rc; GetClientRect(v->wnd, &rc);
    int m = VvS(v, 8), g = VvS(v, 6), rh = VvS(v, 26), y = m;
    MoveWindow(v->info, m, y + VvS(v, 3), rc.right - 2 * m, rh - VvS(v, 3), TRUE); y += rh;
    int ch = rh * 2 + g * 3;
    MoveWindow(v->panel, m, y, rc.right - 2 * m, rc.bottom - y - ch - m > 20 ? rc.bottom - y - ch - m : 20, TRUE);
    int cy = rc.bottom - m - ch + g;
    MoveWindow(v->pos, m, cy, rc.right - 2 * m, rh, TRUE); cy += rh + g;
    int x = m, bw = VvS(v, 70), sw = VvS(v, 34);
    MoveWindow(v->play, x, cy, bw, rh, TRUE); x += bw + g;
    MoveWindow(v->prev, x, cy, sw, rh, TRUE); x += sw + 2;
    MoveWindow(v->next, x, cy, sw, rh, TRUE); x += sw + g * 2;
    MoveWindow(v->loop, x, cy, VvS(v, 60), rh, TRUE); x += VvS(v, 60) + g;
    MoveWindow(v->speedCb, x, cy, VvS(v, 80), VvS(v, 200), TRUE); x += VvS(v, 80) + g * 2;
    int pw = VvS(v, 150);
    int ew = VvS(v, 150);
    MoveWindow(v->save, rc.right - m - pw, cy, pw, rh, TRUE);
    MoveWindow(v->exportBtn, rc.right - m - pw - g - ew, cy, ew, rh, TRUE);
    int tw = rc.right - m - pw - g - ew - g - x;
    MoveWindow(v->time, x, cy + VvS(v, 4), tw > 0 ? tw : 0, rh - VvS(v, 4), TRUE);
}
static HWND VvCtl(VV *v, const wchar_t *cls, const wchar_t *text, DWORD style, DWORD ex, int id) {
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, v->wnd, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)v->font, TRUE);
    return h;
}
static LRESULT CALLBACK VvProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    VV *v = (VV *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!v && msg != WM_CREATE) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_CREATE: {
        v = (VV *)((CREATESTRUCTW *)lp)->lpCreateParams; SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)v); v->wnd = hwnd;
        v->info = VvCtl(v, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 0, VV_INFO);
        v->panel = CreateWindowExW(0, VV_PANEL_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)VV_PANEL, GetModuleHandleW(NULL), NULL);
        v->pos = VvCtl(v, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, VV_POS);
        SendMessageW(v->pos, TBM_SETRANGE, TRUE, MAKELPARAM(0, v->wm.nf > 0 ? v->wm.nf - 1 : 0));
        SendMessageW(v->pos, TBM_SETPAGESIZE, 0, 30);
        v->play = VvCtl(v, L"BUTTON", L"Play", BS_PUSHBUTTON | WS_TABSTOP, 0, VV_PLAY);
        v->prev = VvCtl(v, L"BUTTON", L"\x25C0", BS_PUSHBUTTON | WS_TABSTOP, 0, VV_PREV);
        v->next = VvCtl(v, L"BUTTON", L"\x25B6", BS_PUSHBUTTON | WS_TABSTOP, 0, VV_NEXT);
        v->loop = VvCtl(v, L"BUTTON", L"Loop", BS_AUTOCHECKBOX | WS_TABSTOP, 0, VV_LOOP);
        SendMessageW(v->loop, BM_SETCHECK, BST_CHECKED, 0);
        v->speedCb = VvCtl(v, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, 0, VV_SPEED);
        static const wchar_t *sp[] = { L"0.1\x00D7", L"0.25\x00D7", L"0.5\x00D7", L"1\x00D7", L"2\x00D7" };
        for (int i = 0; i < 5; i++) SendMessageW(v->speedCb, CB_ADDSTRING, 0, (LPARAM)sp[i]);
        SendMessageW(v->speedCb, CB_SETCURSEL, 3, 0);
        v->time = VvCtl(v, L"STATIC", L"", SS_LEFT, 0, VV_TIME);
        v->save = VvCtl(v, L"BUTTON", L"Save frame as PNG...", BS_PUSHBUTTON | WS_TABSTOP, 0, VV_SAVE);
        v->exportBtn = VvCtl(v, L"BUTTON", L"Export video...", BS_PUSHBUTTON | WS_TABSTOP, 0, VV_EXPORT);
        wchar_t inf[400]; int nkey = 0; for (int k = 0; k < v->wm.nf; k++) nkey += v->wm.f[k].key != 0;
        swprintf(inf, 400, L"%d \x00D7 %d   \x00B7   %.0f frames a second   \x00B7   %.2f seconds (%d frames, %d keyframes)   \x00B7   %hs, no sound, %d decoding threads   \x00B7   Space = play/pause, \x2190 \x2192 = one frame",
                 v->w ? v->w : v->wm.width, v->h ? v->h : v->wm.height, v->wm.fps, v->wm.nf / (v->wm.fps > 0 ? v->wm.fps : 60), v->wm.nf, nkey,
                 !strcmp(v->wm.codec, "V_VP9") ? "VP9" : v->wm.codec, v->threads);
        SetWindowTextW(v->info, inf);
        VvLayout(v); VvUpdateTime(v);
        SetFocus(v->panel);
        VvSetPlaying(v, 1);
        return 0;
    }
    case WM_SIZE: VvLayout(v); InvalidateRect(v->panel, NULL, FALSE); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO *)lp)->ptMinTrackSize.x = VvS(v, 560); ((MINMAXINFO *)lp)->ptMinTrackSize.y = VvS(v, 340); return 0;
    case WM_TIMER: if (wp == VV_TIMER) VvTick(v); return 0;
    case WM_HSCROLL:
        // read the slider before pausing: pausing redraws the time line, which puts the slider back on the current frame
        if ((HWND)lp == v->pos) { int to = (int)SendMessageW(v->pos, TBM_GETPOS, 0, 0); v->playing = 0; VvSeek(v, to); VvUpdateTime(v); }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case VV_PLAY: VvSetPlaying(v, !v->playing); SetFocus(v->panel); break;
        case VV_PREV: VvSetPlaying(v, 0); VvSeek(v, (v->goal >= 0 ? v->goal : v->shown) - 1); VvUpdateTime(v); break;
        case VV_NEXT: VvSetPlaying(v, 0); VvSeek(v, (v->goal >= 0 ? v->goal : v->shown) + 1); VvUpdateTime(v); break;
        case VV_SAVE: VvSavePng(v); break;
        case VV_EXPORT: VvExportVideo(v); break;
        case VV_SPEED:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                static const double sp[] = { 0.1, 0.25, 0.5, 1, 2 };
                int k = (int)SendMessageW(v->speedCb, CB_GETCURSEL, 0, 0); v->speed = sp[k >= 0 && k < 5 ? k : 3];
                if (v->playing) VvSetPlaying(v, 1);
                SetFocus(v->panel);
            }
            break;
        case VV_LOOP: SetFocus(v->panel); break;
        }
        return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: KillTimer(hwnd, VV_TIMER); return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        if (v->cxOk) vpx_codec_destroy(&v->cx);
        WmFree(&v->wm); free(v->file); free(v->rgb); free(v);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// dumpFile: the .webm in the game dump
void VidViewOpen(HWND owner, const wchar_t *dumpFile, const wchar_t *title, HFONT font, int dpi) {
    static int reg = 0;
    HINSTANCE in = GetModuleHandleW(NULL);
    if (!reg) {
        WNDCLASSEXW wc = { 0 }; wc.cbSize = sizeof wc; wc.lpfnWndProc = VvProc; wc.hInstance = in; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = VV_CLASS;
        wc.hIcon = (HICON)SendMessageW(owner, WM_GETICON, ICON_BIG, 0); wc.hIconSm = (HICON)SendMessageW(owner, WM_GETICON, ICON_SMALL, 0);
        RegisterClassExW(&wc);
        WNDCLASSEXW pc = { 0 }; pc.cbSize = sizeof pc; pc.lpfnWndProc = VvPanelProc; pc.hInstance = in; pc.hCursor = LoadCursor(NULL, IDC_HAND);
        pc.lpszClassName = VV_PANEL_CLASS; RegisterClassExW(&pc);
        reg = 1;
    }
    VV *v = (VV *)calloc(1, sizeof(VV)); if (!v) return;
    v->font = font; v->dpi = dpi ? dpi : 96; v->dec = -1; v->shown = -1; v->speed = 1; v->goal = -1;
    swprintf(v->title, 600, L"Video - %ls", title); swprintf(v->name, 300, L"%ls", title);
    size_t n; unsigned char *b = ArcReadAll(dumpFile, &n);
    if (b) b = ArcUnzstd(b, n, &n);
    const char *err = NULL;
    if (!b) err = "The file couldn't be read from the game dump.";
    else if (!WmOpen(&v->wm, b, n)) err = v->wm.err;
    else if (strcmp(v->wm.codec, "V_VP9")) err = "Only VP9 video is supported (this one isn't).";
    else {
        SYSTEM_INFO si; GetSystemInfo(&si);
        vpx_codec_dec_cfg_t cfg = { 0 }; cfg.threads = si.dwNumberOfProcessors > 1 ? (si.dwNumberOfProcessors > 8 ? 8 : si.dwNumberOfProcessors) : 1;
        if (vpx_codec_dec_init(&v->cx, vpx_codec_vp9_dx(), &cfg, 0)) err = "The VP9 decoder couldn't be started.";
        else { v->threads = (int)cfg.threads; vpx_codec_control(&v->cx, VP9D_SET_ROW_MT, 1); vpx_codec_control(&v->cx, VP9D_SET_LOOP_FILTER_OPT, 1); }
    }
    if (err) {
        wchar_t m[2600]; swprintf(m, 2600, L"This video can't be played.\n\n%hs\n\nFile: %ls", err, dumpFile);
        MessageBoxW(owner, m, L"Video", MB_ICONINFORMATION);
        WmFree(&v->wm); free(b); free(v); return;
    }
    v->cxOk = 1; v->file = b; v->fileN = n;
    VvGoto(v, 0);
    // window: the video at its own size (or smaller on a small screen) plus the controls
    int vw = v->w ? v->w : 960, vh = v->h ? v->h : 540;
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int extra = MulDiv(26 * 3 + 6 * 3 + 16, v->dpi, 96);
    double s = (double)MulDiv(vw, v->dpi, 96) / vw;
    while ((vw * s + 40 > wa.right - wa.left || vh * s + extra + 60 > wa.bottom - wa.top) && s > 0.2) s *= 0.9;
    RECT r = { 0, 0, (LONG)(vw * s) + MulDiv(16, v->dpi, 96), (LONG)(vh * s) + extra };
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
    HWND wnd = CreateWindowExW(0, VV_CLASS, v->title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, in, v);
    if (!wnd) { vpx_codec_destroy(&v->cx); WmFree(&v->wm); free(v->file); free(v->rgb); free(v); return; }
    if (wnd) UiSkinWindow(wnd);                 // the main window's dark look
    ShowWindow(wnd, SW_SHOW);
}

/* ---------------- preview for the main window ---------------- */
// the middle frame of the video (many fade in from black), scaled to fit boxW x boxH, premultiplied BGRA
unsigned char *VidThumbnail(const wchar_t *dumpFile, int boxW, int boxH, int *ow, int *oh, wchar_t *note, int noteN) {
    note[0] = 0;
    size_t n; unsigned char *b = ArcReadAll(dumpFile, &n);
    if (b) b = ArcUnzstd(b, n, &n);
    if (!b) return NULL;
    VV *v = (VV *)calloc(1, sizeof(VV));
    if (!v) { free(b); return NULL; }
    v->dec = -1; v->shown = -1; v->file = b; v->fileN = n;
    unsigned char *out = NULL;
    if (WmOpen(&v->wm, b, n) && !strcmp(v->wm.codec, "V_VP9") && v->wm.nf > 0) {
        SYSTEM_INFO si; GetSystemInfo(&si);
        vpx_codec_dec_cfg_t cfg = { 0 }; cfg.threads = si.dwNumberOfProcessors > 1 ? (si.dwNumberOfProcessors > 8 ? 8 : si.dwNumberOfProcessors) : 1;
        if (!vpx_codec_dec_init(&v->cx, vpx_codec_vp9_dx(), &cfg, 0)) {
            v->cxOk = 1;
            vpx_codec_control(&v->cx, VP9D_SET_ROW_MT, 1);
            VvGoto(v, v->wm.nf / 2);
            if (v->rgb) out = UiScaleToBox(v->rgb, v->w, v->h, boxW, boxH, ow, oh);
            double secs = v->wm.nf / (v->wm.fps > 0 ? v->wm.fps : 60);
            swprintf(note, noteN, L"%d x %d - %d:%02d", v->w ? v->w : v->wm.width, v->h ? v->h : v->wm.height, (int)secs / 60, (int)secs % 60);
            vpx_codec_destroy(&v->cx);
        }
    }
    WmFree(&v->wm); free(v->rgb); free(b); free(v);
    return out;
}
