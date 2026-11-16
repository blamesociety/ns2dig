/* Drawing text with the game's own fonts (Windows GDI). Load a Font*.bfarc archive with GfLoad; GfRender draws a
   string in one of its .fcpx fonts into an RGBA buffer: scalable fonts are installed privately for this process
   (AddFontMemResourceEx) and drawn with GDI, bitmap fonts (PictColor icons) are copied from their glyph sheets. */
#ifndef FONT_WIN_H
#define FONT_WIN_H
#include <windows.h>
#include <math.h>
#include "arc_util.h"
#include "tex_core.h"
#include "font_core.h"

typedef struct { char file[64]; HANDLE mem; wchar_t family[64]; unsigned char *cov; int upm, asc, desc; wchar_t tmp[MAX_PATH]; char err[200]; } GfFace;
typedef struct { char name[64]; Fcpx c; int src[16]; } GfFont;      // src: face index, 1000+bitmap index, -1 missing
typedef struct {
    int ok; wchar_t path[1024]; char err[300];
    int loose;                     // path is a folder of loose font files (Mario Kart World: romfs\Font\X.bfotf.zs, X.bfcpx)
    GfFace face[24]; int nface;
    Bffnt bmp[4]; char bmpName[4][64]; int nbmp;
    GfFont font[24]; int nfont;
} GameFonts;

static void GfFree(GameFonts *g) {
    for (int i = 0; i < g->nface; i++) {
        GfFace *f = &g->face[i];
        if (f->tmp[0]) { RemoveFontResourceExW(f->tmp, FR_PRIVATE, 0); DeleteFileW(f->tmp); }
        else if (f->mem) RemoveFontMemResourceEx(f->mem);
        free(f->cov);
    }
    for (int i = 0; i < g->nbmp; i++) BffntFree(&g->bmp[i]);
    for (int i = 0; i < g->nfont; i++) FcpxFree(&g->font[i].c);
    memset(g, 0, sizeof *g);
}
// a member of the font archive ("scft/X.bfotf", "font/X.bffnt", "fcpx/X.bfcpx"), or, for a loose folder, the file
// <folder>\X(.zs) (fcpx: <folder>\X.bfcpx or X.<anything>.bfcpx). malloc'd, decompressed.
static unsigned char *GfMember(const GameFonts *g, const unsigned char *arc, size_t an, const char *member, size_t *n) {
    if (!g->loose) return ArcSarcMember(arc, an, member, n);
    const char *base = strchr(member, '/'); base = base ? base + 1 : member;
    wchar_t p[1200]; unsigned char *b = NULL;
    if (!strncmp(member, "fcpx/", 5)) {
        char nm[96]; snprintf(nm, sizeof nm, "%s", base); char *d = strstr(nm, ".bfcpx"); if (d) *d = 0;
        swprintf(p, 1200, L"%ls\\%hs.bfcpx", g->path, nm);
        if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) {
            wchar_t pat[1200]; swprintf(pat, 1200, L"%ls\\%hs.*.bfcpx", g->path, nm);
            WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd);
            if (h != INVALID_HANDLE_VALUE) { FindClose(h); swprintf(p, 1200, L"%ls\\%ls", g->path, fd.cFileName); }
        }
    } else {
        swprintf(p, 1200, L"%ls\\%hs.zs", g->path, base);
        if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) swprintf(p, 1200, L"%ls\\%hs", g->path, base);
    }
    size_t k; b = ArcReadAll(p, &k); if (!b) return NULL;
    b = ArcUnzstd(b, k, &k); if (!b) return NULL;
    *n = k; return b;
}
static int GfFace_(GameFonts *g, const unsigned char *arc, size_t an, const char *file) {
    for (int i = 0; i < g->nface; i++) if (!strcmp(g->face[i].file, file)) return g->face[i].mem ? i : -1;
    if (g->nface >= 24) return -1;
    GfFace *f = &g->face[g->nface++]; memset(f, 0, sizeof *f); snprintf(f->file, sizeof f->file, "%s", file);
    char in[96]; snprintf(in, sizeof in, "scft/%s", file);
    size_t n, m; unsigned char *b = GfMember(g, arc, an, in, &n);
    if (!b) { snprintf(f->err, sizeof f->err, g->loose ? "%s isn't in the Font folder." : "%s isn't in the font archive.", in); snprintf(g->err, sizeof g->err, "%s", f->err); return -1; }
    char hx[3 * 16 + 1] = ""; for (size_t i = 0; i < 16 && i < n; i++) snprintf(hx + i * 3, 4, "%02X ", b[i]);
    unsigned char *o = FontUnscramble(b, n, &m); free(b);
    if (!o) { snprintf(f->err, sizeof f->err, "%s couldn't be unscrambled (%u bytes, starts %s).", in, (unsigned)n, hx); snprintf(g->err, sizeof g->err, "%s", f->err); return -1; }
    uint16_t fam[64];
    if (!OtFamily(o, m, fam, 64)) { snprintf(f->err, sizeof f->err, "%s has no font name.", in); snprintf(g->err, sizeof g->err, "%s", f->err); }
    else {
        DWORD cnt = 0; f->mem = AddFontMemResourceEx(o, (DWORD)m, NULL, &cnt);
        if (!f->mem) {                                     // Windows wouldn't take it from memory: a private temporary file instead
            DWORD e1 = GetLastError();
            wchar_t dir[MAX_PATH]; GetTempPathW(MAX_PATH, dir);
            swprintf(f->tmp, MAX_PATH, L"%lsns2dig_%lu_%hs%ls", dir, GetCurrentProcessId(), file, m >= 4 && !memcmp(o, "OTTO", 4) ? L".otf" : L".ttf");
            HANDLE h = CreateFileW(f->tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
            DWORD wr = 0; int okw = h != INVALID_HANDLE_VALUE && WriteFile(h, o, (DWORD)m, &wr, NULL) && wr == (DWORD)m;
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
            if (okw && AddFontResourceExW(f->tmp, FR_PRIVATE, 0) > 0) f->mem = (HANDLE)1;
            else {
                snprintf(f->err, sizeof f->err, "Windows wouldn't load %s (error %lu / %lu).", file, (unsigned long)e1, (unsigned long)GetLastError()); snprintf(g->err, sizeof g->err, "%s", f->err);
                DeleteFileW(f->tmp); f->tmp[0] = 0;
            }
        }
        if (f->mem) {
            memcpy(f->family, fam, sizeof f->family); f->family[63] = 0;
            f->cov = OtCoverage(o, m, NULL); OtMetrics(o, m, &f->upm, &f->asc, &f->desc);
        }
    }
    free(o);
    return f->mem ? g->nface - 1 : -1;
}
static int GfLoad(GameFonts *g, const wchar_t *path) {
    memset(g, 0, sizeof *g); swprintf(g->path, 1024, L"%ls", path);
    DWORD at = GetFileAttributesW(path);
    if (at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_DIRECTORY)) {      // a folder of loose font files
        g->loose = 1;
        size_t ln = wcslen(g->path); if (ln && (g->path[ln - 1] == L'\\' || g->path[ln - 1] == L'/')) g->path[ln - 1] = 0;
        char list[64][64]; int nl = 0;
        wchar_t pat[1200]; swprintf(pat, 1200, L"%ls\\*.bfcpx", g->path);
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do { char nm[64]; snprintf(nm, sizeof nm, "%ls", fd.cFileName); char *d = strchr(nm, '.'); if (d) *d = 0; if (nl < 64 && nm[0]) snprintf(list[nl++], 64, "%s", nm); }
            while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        for (int i = 0; i < nl && g->nfont < 24; i++) {
            char in[96]; snprintf(in, sizeof in, "fcpx/%s.bfcpx", list[i]);
            size_t cn; unsigned char *cb = GfMember(g, NULL, 0, in, &cn); if (!cb) continue;
            GfFont *F = &g->font[g->nfont];
            if (FcpxParse(&F->c, cb, cn)) {
                snprintf(F->name, sizeof F->name, "%s", list[i]);
                if (F->c.bitmap) {
                    int bi = -1;
                    for (int k = 0; k < g->nbmp; k++) if (!strcmp(g->bmpName[k], F->c.bitmapFile)) bi = k;
                    if (bi < 0 && g->nbmp < 4) {
                        char bn[96]; snprintf(bn, sizeof bn, "font/%s", F->c.bitmapFile);
                        size_t fn; unsigned char *fb = GfMember(g, NULL, 0, bn, &fn); char err[200];
                        if (fb && BffntParse(&g->bmp[g->nbmp], fb, fn, err, sizeof err)) { snprintf(g->bmpName[g->nbmp], 64, "%s", F->c.bitmapFile); bi = g->nbmp++; }
                        else BffntFree(&g->bmp[g->nbmp]);
                        free(fb);
                    }
                    F->src[0] = bi >= 0 ? 1000 + bi : -1;
                } else for (int k = 0; k < F->c.n; k++) F->src[k] = GfFace_(g, NULL, 0, F->c.e[k].file);
                g->nfont++;
            } else FcpxFree(&F->c);
            free(cb);
        }
        g->ok = 1;                          // a folder can hold just plain fonts too
        return g->ok;
    }
    size_t n; unsigned char *arc = ArcReadAll(path, &n);
    if (!arc) { snprintf(g->err, sizeof g->err, "The font archive couldn't be read (error %lu).", (unsigned long)GetLastError()); return 0; }
    arc = ArcUnzstd(arc, n, &n);
    if (!arc) { snprintf(g->err, sizeof g->err, "The font archive couldn't be decompressed."); return 0; }
    if (n < 0x20 || memcmp(arc, "SARC", 4)) { snprintf(g->err, sizeof g->err, "The font archive isn't a SARC archive (starts %02X %02X %02X %02X).", arc[0], n > 1 ? arc[1] : 0, n > 2 ? arc[2] : 0, n > 3 ? arc[3] : 0); free(arc); return 0; }
    static const char *names[] = { "FontCaption", "FontHeadline", "FontPopUpEmerush", "Ruby", "Banana", "PictColor" };
    // every fcpx the archive has (the names above first, then any others)
    char list[64][64]; int nl = 0;
    for (int i = 0; i < 6; i++) snprintf(list[nl++], 64, "%s", names[i]);
    // scan names
    {
        size_t o; int nf;
        if (n >= 0x20 && !memcmp(arc, "SARC", 4)) {
            o = arc[4] | (arc[5] << 8);
            nf = arc[o + 6] | (arc[o + 7] << 8);
            size_t ents = o + 12, nm = ents + (size_t)nf * 16 + 8;
            for (int f = 0; f < nf && nl < 64; f++) {
                uint32_t attr = ArcRd32(arc + ents + (size_t)f * 16 + 4); size_t no = nm + (size_t)(attr & 0xFFFFFF) * 4;
                if (no + 6 >= n) continue;
                const char *s = (const char *)arc + no;
                if (strncmp(s, "fcpx/", 5)) continue;
                char base[64]; snprintf(base, sizeof base, "%s", s + 5); char *d = strstr(base, ".bfcpx"); if (!d) continue; *d = 0;
                int have = 0; for (int k = 0; k < nl; k++) if (!strcmp(list[k], base)) have = 1;
                if (!have) snprintf(list[nl++], 64, "%s", base);
            }
        }
    }
    for (int i = 0; i < nl && g->nfont < 24; i++) {
        char in[96]; snprintf(in, sizeof in, "fcpx/%s.bfcpx", list[i]);
        size_t cn; unsigned char *cb = ArcSarcMember(arc, n, in, &cn); if (!cb) continue;
        GfFont *F = &g->font[g->nfont];
        if (FcpxParse(&F->c, cb, cn)) {
            snprintf(F->name, sizeof F->name, "%s", list[i]);
            if (F->c.bitmap) {
                int bi = -1;
                for (int k = 0; k < g->nbmp; k++) if (!strcmp(g->bmpName[k], F->c.bitmapFile)) bi = k;
                if (bi < 0 && g->nbmp < 4) {
                    char bn[96]; snprintf(bn, sizeof bn, "font/%s", F->c.bitmapFile);
                    size_t fn; unsigned char *fb = ArcSarcMember(arc, n, bn, &fn); char err[200];
                    if (fb && BffntParse(&g->bmp[g->nbmp], fb, fn, err, sizeof err)) { snprintf(g->bmpName[g->nbmp], 64, "%s", F->c.bitmapFile); bi = g->nbmp++; }
                    else BffntFree(&g->bmp[g->nbmp]);
                    free(fb);
                }
                F->src[0] = bi >= 0 ? 1000 + bi : -1;
            } else for (int k = 0; k < F->c.n; k++) F->src[k] = GfFace_(g, arc, n, F->c.e[k].file);
            g->nfont++;
        } else FcpxFree(&F->c);
        free(cb);
    }
    free(arc);
    g->ok = g->nfont > 0;
    if (!g->ok && !g->err[0]) snprintf(g->err, sizeof g->err, "No fcpx fonts were found in the archive.");
    return g->ok;
}
// a layout's font name ("FontCaption.fcpx") -> index, or -1
static int GfFind(const GameFonts *g, const char *name) {
    char base[64]; snprintf(base, sizeof base, "%s", name); char *d = strrchr(base, '.'); if (d) *d = 0;
    for (int i = 0; i < g->nfont; i++) if (!_stricmp(g->font[i].name, base)) return i;
    return -1;
}
// does any loaded scalable font draw this character (e.g. the button icons U+E0E0...)?
static inline int GfAnyHas(const GameFonts *g, unsigned cp) {
    for (int i = 0; i < g->nface; i++) if (g->face[i].mem && OtHas(g->face[i].cov, cp)) return 1;
    return 0;
}

// where a character comes from: entry k of the font (scalable or bitmap), or -1 = a Windows font
static int GfPick(const GameFonts *g, const GfFont *F, unsigned cp) {
    if (F->c.bitmap) { const Bffnt *b = F->src[0] >= 1000 ? &g->bmp[F->src[0] - 1000] : NULL; return b && b->map && b->map[cp & 0xFFFF] != 0xFFFF && cp < 65536 ? 0 : -1; }
    for (int k = 0; k < F->c.n; k++) {
        int s = F->src[k]; if (s < 0) continue;
        if (F->c.e[k].nr && !FcpxEntHas(&F->c.e[k], cp)) continue;
        if (OtHas(g->face[s].cov, cp)) return k;
    }
    return -1;
}
static const wchar_t *GfSystemFace(unsigned cp) {
    if ((cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x3130 && cp <= 0x318F)) return L"Malgun Gothic";
    if (cp >= 0x0E00 && cp <= 0x0E7F) return L"Leelawadee UI";
    return L"Yu Gothic UI";
}

typedef struct { int src; float x, w; int line; unsigned cp; } GfGlyph;     // src: entry, or -1 Windows font
/* Draws text into rgba (W x H, straight alpha, white text / coloured icons). The text block is placed in the
   box (bx,by,bw,bh) of the buffer: hpos/vpos 0 centre, 1 left/top, 2 right/bottom; lineAlign 0 = as hpos,
   1 left, 2 centre, 3 right. fsx/fsy = the text pane's font size, a scale of the font's own size (the .fcpx size
   is the em; the line height is the main font's ascent + descent at that em). Lines wider than the box are squeezed. */
static inline int GfRender(const GameFonts *g, int fi, const wchar_t *text, float fsx, float fsy, float charSpace, float lineSpace,
                    int hpos, int vpos, int lineAlign, float bx, float by, float bw, float bh, unsigned char *rgba, int W, int H) {
    if (fi < 0 || fi >= g->nfont || fsy <= 0.001f) return 0;
    const GfFont *F = &g->font[fi];
    int n = (int)wcslen(text); if (!n) return 1;
    GfGlyph *gl = (GfGlyph *)calloc(n + 1, sizeof(GfGlyph)); if (!gl) return 0;
    HDC dc = CreateCompatibleDC(NULL);
    BITMAPINFO bi; ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void *bits = NULL; HBITMAP bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bm) { DeleteDC(dc); free(gl); return 0; }
    HGDIOBJ ob = SelectObject(dc, bm); memset(bits, 0, (size_t)W * H * 4);
    // sizes: the line height is the main font's cell (ascent + descent); every part font is drawn at that em, times its scale
    const Bffnt *B = F->c.bitmap && F->src[0] >= 1000 ? &g->bmp[F->src[0] - 1000] : NULL;
    float em = fsy * (F->c.size > 1 ? F->c.size : 48), lineH = em * 1.25f, ascent = em, bsc = fsy;
    float hs = fsx > 0.001f ? fsx / fsy : 1;               // horizontal stretch
    if (B) { lineH = B->height * bsc; ascent = B->ascent * bsc; }
    else {
        int main = -1;
        for (int k = 0; k < F->c.n; k++) if (F->src[k] >= 0 && !F->c.e[k].nr) { main = F->src[k]; break; }
        if (main < 0) for (int k = 0; k < F->c.n; k++) if (F->src[k] >= 0) { main = F->src[k]; break; }
        if (main >= 0) { const GfFace *m = &g->face[main]; lineH = em * (m->asc + m->desc) / m->upm; ascent = em * m->asc / m->upm; }
    }
    HFONT hf[17] = { 0 }; HFONT sys[3] = { 0 }; const wchar_t *sysName[3] = { L"Yu Gothic UI", L"Malgun Gothic", L"Leelawadee UI" };
    // measure
    int line = 0; float x = 0, lineW[256] = { 0 }; int nlines = 1;
    for (int i = 0; i < n; i++) {
        unsigned cp = text[i]; GfGlyph *q = &gl[i]; q->cp = cp; q->line = line; q->x = x;
        if (cp == '\n') { lineW[line] = x > 0 ? x - charSpace : 0; if (line < 255) line++; nlines = line + 1; x = 0; q->src = -2; continue; }
        if (cp == '\r') { q->src = -2; continue; }
        float adv;
        if (B) {
            int gi = B->map ? B->map[cp & 0xFFFF] : 0xFFFF;
            if (gi != 0xFFFF) { FfWidth w = BffntWidth(B, gi); adv = w.charW * bsc; q->src = 0; }
            else { q->src = -1; adv = 0; }
        } else q->src = GfPick(g, F, cp);
        if (!B || q->src < 0) {
            HFONT f;
            if (q->src >= 0) {
                if (!hf[q->src]) hf[q->src] = CreateFontW(-(int)(em * F->c.e[q->src].scale + 0.5f), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, g->face[F->src[q->src]].family);
                f = hf[q->src];
            } else {
                const wchar_t *sn = GfSystemFace(cp); int s = sn == sysName[1] ? 1 : sn == sysName[2] ? 2 : 0;
                if (!sys[s]) sys[s] = CreateFontW(-(int)(em + 0.5f), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, sysName[s]);
                f = sys[s];
            }
            SelectObject(dc, f); SIZE sz; wchar_t c1 = (wchar_t)cp; GetTextExtentPoint32W(dc, &c1, 1, &sz); adv = (float)sz.cx;
        }
        q->w = adv; x += adv + charSpace;
    }
    lineW[line] = x > 0 ? x - charSpace : 0;
    float maxW = 0; for (int l = 0; l < nlines; l++) if (lineW[l] > maxW) maxW = lineW[l];
    float sx = bw > 1 && maxW * hs > bw ? bw / (maxW * hs) : 1;     // squeeze lines that don't fit
    float k = hs * sx;                                              // x scale of everything
    float lineStep = lineH + lineSpace, blockH = nlines * lineH + (nlines - 1) * lineSpace, blockW = maxW * k;
    float top = vpos == 1 ? by : vpos == 2 ? by + bh - blockH : by + (bh - blockH) / 2;
    float left = hpos == 1 ? bx : hpos == 2 ? bx + bw - blockW : bx + (bw - blockW) / 2;
    int la = lineAlign ? lineAlign : (hpos == 1 ? 1 : hpos == 2 ? 3 : 2);
    // draw the scalable glyphs with GDI (squeezed with a world transform), white on black
    SetGraphicsMode(dc, GM_ADVANCED);
    XFORM xf = { k, 0, 0, 1, 0, 0 }; SetWorldTransform(dc, &xf);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255)); SetTextAlign(dc, TA_BASELINE | TA_LEFT | TA_NOUPDATECP);
    for (int i = 0; i < n; i++) {
        GfGlyph *q = &gl[i]; if (q->src == -2 || (B && q->src == 0)) continue;
        float lx = la == 1 ? 0 : la == 3 ? maxW - lineW[q->line] : (maxW - lineW[q->line]) / 2;
        float gx = left / k + lx + q->x, base = top + q->line * lineStep + ascent;
        HFONT f;
        if (q->src >= 0) { f = hf[q->src]; base -= F->c.e[q->src].shift * em / (F->c.size > 1 ? F->c.size : 48); }
        else { const wchar_t *sn = GfSystemFace(q->cp); f = sys[sn == sysName[1] ? 1 : sn == sysName[2] ? 2 : 0]; }
        if (!f) continue;
        SelectObject(dc, f); wchar_t c1 = (wchar_t)q->cp;
        ExtTextOutW(dc, (int)lroundf(gx), (int)lroundf(base), 0, NULL, &c1, 1, NULL);
    }
    GdiFlush();
    const unsigned char *px = (const unsigned char *)bits;
    for (size_t i = 0; i < (size_t)W * H; i++) { rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255; rgba[i * 4 + 3] = px[i * 4 + 1]; }
    // bitmap glyphs: scaled copies from the sheets, laid over
    if (B) for (int i = 0; i < n; i++) {
        GfGlyph *q = &gl[i]; if (q->src != 0) continue;
        int gi = B->map[q->cp & 0xFFFF], s, cx, cy; if (!BffntCell(B, gi, &s, &cx, &cy)) continue;
        FfWidth w = BffntWidth(B, gi);
        float lx = la == 1 ? 0 : la == 3 ? maxW - lineW[q->line] : (maxW - lineW[q->line]) / 2;
        float gx = left + (lx + q->x + w.left * bsc) * k, gy = top + q->line * lineStep + ascent - B->baseline * bsc;
        float gw = w.glyphW * bsc * k, gh = B->cellH * bsc;
        int x0 = (int)floorf(gx), y0 = (int)floorf(gy), x1 = (int)ceilf(gx + gw), y1 = (int)ceilf(gy + gh);
        const unsigned char *sh = B->sheet[s];
        for (int yy = y0 < 0 ? 0 : y0; yy < y1 && yy < H; yy++)
            for (int xx = x0 < 0 ? 0 : x0; xx < x1 && xx < W; xx++) {
                float u = (xx + 0.5f - gx) / gw * w.glyphW, vv = (yy + 0.5f - gy) / gh * B->cellH;
                if (u < 0 || vv < 0 || u >= w.glyphW || vv >= B->cellH) continue;
                // bilinear
                float fu = u - 0.5f, fv = vv - 0.5f; int iu = (int)floorf(fu), iv = (int)floorf(fv); float au = fu - iu, av = fv - iv;
                float acc[4] = { 0 };
                for (int c4 = 0; c4 < 4; c4++) {
                    int uu = iu + (c4 & 1), vq = iv + (c4 >> 1);
                    if (uu < 0) uu = 0; if (vq < 0) vq = 0; if (uu >= w.glyphW) uu = w.glyphW - 1; if (vq >= B->cellH) vq = B->cellH - 1;
                    float wt = ((c4 & 1) ? au : 1 - au) * ((c4 >> 1) ? av : 1 - av);
                    const unsigned char *sp = sh + ((size_t)(cy + vq) * B->sheetW + (cx + uu)) * 4;
                    float a = sp[3] / 255.0f;
                    acc[0] += sp[0] * a * wt; acc[1] += sp[1] * a * wt; acc[2] += sp[2] * a * wt; acc[3] += a * wt;
                }
                if (acc[3] <= 0.001f) continue;
                unsigned char *d = rgba + ((size_t)yy * W + xx) * 4;
                float da = d[3] / 255.0f, oa = acc[3] + da * (1 - acc[3]);
                for (int c = 0; c < 3; c++) d[c] = (unsigned char)((acc[c] + d[c] * da * (1 - acc[3])) / oa + 0.5f);
                d[3] = (unsigned char)(oa * 255 + 0.5f);
            }
    }
    for (int i = 0; i < 17; i++) if (hf[i]) DeleteObject(hf[i]);
    for (int i = 0; i < 3; i++) if (sys[i]) DeleteObject(sys[i]);
    SelectObject(dc, ob); DeleteObject(bm); DeleteDC(dc); free(gl);
    return 1;
}
#endif
