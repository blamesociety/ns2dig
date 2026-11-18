/* The game's fonts: Font\Font*.bfarc archives hold
     fcpx\*.bfcpx  - a "font" as the layouts name it (FontCaption.fcpx...): an ordered list of real fonts, each with
                     the characters it is used for, a size scale and baseline shift; or one bitmap font
     scft\*.bfotf/.bfttf - ordinary OpenType/TrueType fonts XORed with a 4-byte key behind an 8-byte header
     font\*.bffnt  - bitmap fonts (PictColor: colour icons) whose glyph sheets are embedded BNTX textures
   Plain C (no Windows calls). Include tex_core.h first for the bffnt sheets. */
#ifndef FONT_CORE_H
#define FONT_CORE_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static uint32_t FcRd32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static unsigned FcRd16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static uint32_t FcBe32(const unsigned char *p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static unsigned FcBe16(const unsigned char *p) { return (p[0] << 8) | p[1]; }
static float FcRdF(const unsigned char *p) { uint32_t u = FcRd32(p); float f; memcpy(&f, &u, 4); return f; }

/* ---- .bfotf / .bfttf (and plain .otf / .ttf): returns a malloc'd plain OTF/TTF, or NULL ---- */
static unsigned char *FontUnscramble(const unsigned char *b, size_t n, size_t *outN) {
    if (n < 16) return NULL;
    if (!memcmp(b, "OTTO", 4) || !memcmp(b, "\0\1\0\0", 4) || !memcmp(b, "true", 4)) {   // a plain font (Switch Sports' scft\X.otf): as it is
        unsigned nt = FcBe16(b + 4);
        if (nt && nt <= 64 && 12 + (size_t)nt * 16 <= n) { unsigned char *o = (unsigned char *)malloc(n); if (!o) return NULL; memcpy(o, b, n); *outN = n; return o; }
    }
    /* The key: the header's first word is the magic 7F 9A 02 18 XORed with it; or the data starts with a font
       signature ("OTTO" CFF OpenType, 00 01 00 00 / "true" TrueType, "ttcf" collection) */
    static const unsigned char sig[5][4] = { { 0x7F, 0x9A, 0x02, 0x18 }, { 'O', 'T', 'T', 'O' }, { 0, 1, 0, 0 }, { 't', 'r', 'u', 'e' }, { 't', 't', 'c', 'f' } };
    for (int s = 0; s < 5; s++) {
        unsigned char k[4]; for (int i = 0; i < 4; i++) k[i] = (s == 0 ? b[i] : b[8 + i]) ^ sig[s][i];
        unsigned char h[8]; for (int i = 0; i < 8; i++) h[i] = b[i] ^ k[i & 3];
        uint32_t size = FcBe32(h + 4);
        if (size > n - 8 || size < 12) { uint32_t le = FcRd32(h + 4); size = le <= n - 8 && le >= 12 ? le : (uint32_t)(n - 8); }
        unsigned char *o = (unsigned char *)malloc(size); if (!o) return NULL;
        memcpy(o, b + 8, size);                // XOR four bytes at a time (a per-byte k[i & 3] loop is miscompiled by clang -O2)
        for (uint32_t i = 0; i < size; i += 4) {
            o[i] ^= k[0];
            if (i + 1 < size) o[i + 1] ^= k[1];
            if (i + 2 < size) o[i + 2] ^= k[2];
            if (i + 3 < size) o[i + 3] ^= k[3];
        }
        int okSig = !memcmp(o, "OTTO", 4) || !memcmp(o, "\0\1\0\0", 4) || !memcmp(o, "true", 4) || !memcmp(o, "ttcf", 4);
        unsigned nt = FcBe16(o + 4);
        if (!okSig || (memcmp(o, "ttcf", 4) && (nt == 0 || nt > 64 || 12 + nt * 16 > size))) { free(o); continue; }
        *outN = size; return o;
    }
    return NULL;
}
// an OTF/TTF table
static const unsigned char *OtTable(const unsigned char *f, size_t n, const char *tag, uint32_t *len) {
    unsigned nt = FcBe16(f + 4);
    for (unsigned i = 0; i < nt && 12 + i * 16 + 16 <= n; i++) {
        const unsigned char *e = f + 12 + i * 16;
        if (!memcmp(e, tag, 4)) { uint32_t o = FcBe32(e + 8), l = FcBe32(e + 12); if (o + l > n) return NULL; *len = l; return f + o; }
    }
    return NULL;
}
// family name as Windows sees it (name ID 1, Windows platform), UTF-16 into out
static int OtFamily(const unsigned char *f, size_t n, uint16_t *out, int outN) {
    uint32_t l; const unsigned char *t = OtTable(f, n, "name", &l); if (!t || l < 6) return 0;
    unsigned cnt = FcBe16(t + 2), so = FcBe16(t + 4);
    for (int pass = 0; pass < 2; pass++)
        for (unsigned i = 0; i < cnt && 6 + i * 12 + 12 <= l; i++) {
            const unsigned char *r = t + 6 + i * 12;
            unsigned pid = FcBe16(r), lang = FcBe16(r + 4), id = FcBe16(r + 6), len = FcBe16(r + 8), off = FcBe16(r + 10);
            if (pid != 3 || id != 1 || (pass == 0 && lang != 0x409) || so + off + len > l) continue;
            int k = 0; for (unsigned q = 0; q + 1 < len && k < outN - 1; q += 2) out[k++] = (uint16_t)FcBe16(t + so + off + q);
            out[k] = 0; return k;
        }
    return 0;
}
// which BMP characters the font has: bit per code (8 KB); returns NULL if no usable cmap
static unsigned char *OtCoverage(const unsigned char *f, size_t n, int *count) {
    uint32_t l; const unsigned char *t = OtTable(f, n, "cmap", &l); if (!t || l < 4) return NULL;
    unsigned nt = FcBe16(t + 2); const unsigned char *best = NULL; unsigned bestFmt = 0;
    for (unsigned i = 0; i < nt && 4 + i * 8 + 8 <= l; i++) {
        const unsigned char *r = t + 4 + i * 8; uint32_t o = FcBe32(r + 4); if (o + 4 > l) continue;
        unsigned fmt = FcBe16(t + o);
        if ((fmt == 12 && bestFmt != 12) || (fmt == 4 && !best)) { best = t + o; bestFmt = fmt; }
    }
    if (!best) return NULL;
    unsigned char *bits = (unsigned char *)calloc(8192, 1); if (!bits) return NULL;
    int c = 0;
    size_t lim = (size_t)(t + l - best);
#define SETB(cp) do { uint32_t cp_ = (cp); if (cp_ < 65536 && !(bits[cp_ >> 3] & (1 << (cp_ & 7)))) { bits[cp_ >> 3] |= 1 << (cp_ & 7); c++; } } while (0)
    if (bestFmt == 4) {
        unsigned segs = FcBe16(best + 6) / 2;
        if (14 + segs * 8 + 2 > lim) { free(bits); return NULL; }
        const unsigned char *ends = best + 14, *starts = ends + segs * 2 + 2, *deltas = starts + segs * 2, *ros = deltas + segs * 2;
        for (unsigned s = 0; s < segs; s++) {
            unsigned e = FcBe16(ends + s * 2), st = FcBe16(starts + s * 2), dl = FcBe16(deltas + s * 2), ro = FcBe16(ros + s * 2);
            for (unsigned cp = st; cp <= e && cp != 0xFFFF; cp++) {
                unsigned g;
                if (!ro) g = (cp + dl) & 0xFFFF;
                else {
                    size_t gi = (size_t)(ros + s * 2 - best) + ro + (cp - st) * 2;
                    if (gi + 2 > lim) break;
                    g = FcBe16(best + gi); if (g) g = (g + dl) & 0xFFFF;
                }
                if (g) SETB(cp);
            }
        }
    } else {
        uint32_t ng = FcBe32(best + 12);
        for (uint32_t k = 0; k < ng && 16 + k * 12 + 12 <= lim; k++) {
            const unsigned char *g = best + 16 + k * 12;
            uint32_t a = FcBe32(g), z = FcBe32(g + 4);
            for (uint32_t cp = a; cp <= z && cp < 65536; cp++) SETB(cp);
        }
    }
#undef SETB
    if (count) *count = c;
    return bits;
}
static int OtHas(const unsigned char *bits, unsigned cp) { return bits && cp < 65536 && (bits[cp >> 3] >> (cp & 7)) & 1; }
// font metrics in font units: units per em, Windows ascent / descent (what GDI uses for the cell height)
static void OtMetrics(const unsigned char *f, size_t n, int *upm, int *asc, int *desc) {
    uint32_t l; const unsigned char *h = OtTable(f, n, "head", &l);
    *upm = h && l >= 20 ? (int)FcBe16(h + 18) : 1000; if (*upm <= 0) *upm = 1000;
    const unsigned char *o = OtTable(f, n, "OS/2", &l);
    if (o && l >= 78) { *asc = (int)FcBe16(o + 74); *desc = (int)FcBe16(o + 76); }
    else { *asc = *upm * 88 / 100; *desc = *upm * 12 / 100; }
    if (*asc + *desc <= 0) { *asc = *upm; *desc = 0; }
}

/* ---- .bfcpx ---- */
typedef struct { uint32_t lo, hi; } FcRange;
typedef struct { char file[64]; float scale; int shift; int nr; FcRange *r; } FcpxEnt;   // nr == 0: every character the font has
typedef struct { float size; int bitmap; char bitmapFile[64]; int n; FcpxEnt e[16]; int variants; } Fcpx;   // variants: >0 = several lists, first shown

static void FcpxFree(Fcpx *c) { for (int i = 0; i < c->n; i++) free(c->e[i].r); memset(c, 0, sizeof *c); }
static void FcStr(char *out, int outN, const unsigned char *b, size_t n, size_t o) {
    int k = 0; while (o < n && b[o] && k < outN - 1) out[k++] = (char)b[o++]; out[k] = 0;
}
// one font block at X: kind 0 = bitmap font, 1 = list of fonts, 2 = several lists ("variants", Mario Kart World) - the first is used
static int FcpxBlock(Fcpx *c, const unsigned char *b, size_t n, size_t X, int depth) {
    if (X + 0x24 > n || depth > 2) return 0;
    uint32_t kind = FcRd32(b + X);
    if (kind == 0) {
        c->bitmap = 1; FcStr(c->bitmapFile, sizeof c->bitmapFile, b, n, X + FcRd32(b + X + 4));
        return c->bitmapFile[0] != 0;
    }
    if (kind == 2) {
        uint32_t first = FcRd32(b + X + 4); int nv = first >= 4 ? (int)(first - 4) / 4 : 0;
        if (nv > 64) nv = 64;
        c->variants = nv;
        for (int k = 0; k < nv; k++) { uint32_t off = FcRd32(b + X + 4 + 4 * k); if (off && FcpxBlock(c, b, n, X + off, depth + 1)) return 1; }
        return 0;
    }
    if (kind != 1) return 0;
    c->size = FcRdF(b + X + 4);
    int cnt = (int)FcRd32(b + X + 0x0C); if (cnt > 16) cnt = 16;
    for (int k = 0; k < cnt; k++) {
        size_t E = X + 0x24 + (size_t)k * 0x28; if (E + 0x28 > n) break;
        FcpxEnt *e = &c->e[c->n];
        FcStr(e->file, sizeof e->file, b, n, X + FcRd32(b + E + 8));
        e->shift = (int16_t)FcRd16(b + E + 14);
        e->scale = FcRdF(b + E + 16); if (!(e->scale > 0.05f && e->scale < 20)) e->scale = 1;
        uint32_t nr = FcRd32(b + E + 28); size_t ro = X + FcRd32(b + E + 32);
        if (nr && nr < 4096 && ro + (size_t)nr * 8 <= n) {
            e->r = (FcRange *)malloc(nr * sizeof(FcRange));
            if (e->r) { for (uint32_t i = 0; i < nr; i++) { e->r[i].lo = FcRd32(b + ro + i * 8); e->r[i].hi = FcRd32(b + ro + i * 8 + 4); } e->nr = (int)nr; }
        }
        if (e->file[0]) c->n++;
    }
    return c->n > 0;
}
static int FcpxParse(Fcpx *c, const unsigned char *b, size_t n) {
    memset(c, 0, sizeof *c);
    if (n < 0x24 || memcmp(b, "FCPX", 4)) return 0;
    return FcpxBlock(c, b, n, 0x18, 0);
}
static int FcpxEntHas(const FcpxEnt *e, unsigned cp) {
    for (int i = 0; i < e->nr; i++) if (cp >= e->r[i].lo && cp <= e->r[i].hi) return 1;
    return 0;
}

/* ---- .bffnt (bitmap font; sheets are BNTX) ---- */
typedef struct { signed char left; unsigned char glyphW, charW; } FfWidth;
typedef struct {
    int height, ascent, lineFeed, baseline, cellW, cellH, cols, rows, nsheet, sheetW, sheetH;
    FfWidth defW;
    unsigned char **sheet;                // RGBA, sheetW x sheetH each
    int nw; FfWidth *w; int wStart;       // widths by glyph index
    uint16_t *map;                        // 65536 entries: glyph index, 0xFFFF = none
    int nglyph;
} Bffnt;
static void BffntFree(Bffnt *f) {
    if (f->sheet) for (int i = 0; i < f->nsheet; i++) free(f->sheet[i]);
    free(f->sheet); free(f->w); free(f->map); memset(f, 0, sizeof *f);
}
#ifdef TEX_CORE_H
static int BffntParse(Bffnt *f, const unsigned char *b, size_t n, char *err, size_t errn) {
    memset(f, 0, sizeof *f);
    if (n < 0x40 || memcmp(b, "FFNT", 4)) { snprintf(err, errn, "not a bffnt"); return 0; }
    size_t o = FcRd16(b + 6);
    if (o + 0x20 > n || memcmp(b + o, "FINF", 4)) { snprintf(err, errn, "no FINF"); return 0; }
    const unsigned char *fi = b + o + 8;
    f->height = fi[1]; f->ascent = fi[3]; f->lineFeed = (int)FcRd16(fi + 4);
    f->defW.left = (signed char)fi[8]; f->defW.glyphW = fi[9]; f->defW.charW = fi[10];
    uint32_t pG = FcRd32(fi + 12), pW = FcRd32(fi + 16), pM = FcRd32(fi + 20);
    if (pG < 8 || pG + 0x1C > n) { snprintf(err, errn, "bad TGLP"); return 0; }
    const unsigned char *tg = b + pG;
    f->cellW = tg[0]; f->cellH = tg[1]; f->nsheet = tg[2];
    uint32_t sheetSize = FcRd32(tg + 4);
    f->baseline = (int)FcRd16(tg + 8); f->cols = (int)FcRd16(tg + 12); f->rows = (int)FcRd16(tg + 14);
    f->sheetW = (int)FcRd16(tg + 16); f->sheetH = (int)FcRd16(tg + 18);
    uint32_t sd = FcRd32(tg + 20);
    if (!f->nsheet || !f->cols || !f->rows) { snprintf(err, errn, "empty TGLP"); return 0; }
    f->sheet = (unsigned char **)calloc(f->nsheet, sizeof(unsigned char *));
    if (!f->sheet) return 0;
    (void)sheetSize;
    TexFile tf;                                          // one BNTX texture, a layer per sheet
    if (sd >= n || !TexOpen(&tf, b + sd, n - sd) || tf.n < 1) { snprintf(err, errn, "sheets: %s", sd < n ? tf.err : "outside the file"); return 0; }
    for (int s = 0; s < f->nsheet && s < tf.t[0].layers; s++) {
        TexInfo t = tf.t[0]; uint32_t ls = t.size / (t.layers ? t.layers : 1);
        t.data += (uint64_t)s * ls; t.size = ls; t.layers = 1;
        if (t.w == f->sheetW && t.h == f->sheetH) f->sheet[s] = TexDecode(b + sd, n - sd, &t, tf.ounce, err, errn);
        unsigned char *px = f->sheet[s]; size_t row = (size_t)t.w * 4;     // stored bottom row first
        if (px) for (int y = 0; y < t.h / 2; y++) for (size_t x = 0; x < row; x++) {
            unsigned char c = px[y * row + x]; px[y * row + x] = px[(t.h - 1 - y) * row + x]; px[(t.h - 1 - y) * row + x] = c;
        }
    }
    TexClose(&tf);
    // widths
    for (uint32_t p = pW; p >= 8 && p + 4 <= n; ) {
        unsigned a = FcRd16(b + p), z = FcRd16(b + p + 2); uint32_t nx = FcRd32(b + p + 4);
        if (z >= a && p + 8 + (z - a + 1) * 3 <= n) {
            if ((int)z + 1 > f->nw) {
                FfWidth *t = (FfWidth *)realloc(f->w, (z + 1) * sizeof(FfWidth)); if (!t) break;
                for (int i = f->nw; i <= (int)z; i++) t[i] = f->defW;
                f->w = t; f->nw = z + 1;
            }
            for (unsigned i = a; i <= z; i++) { const unsigned char *q = b + p + 8 + (i - a) * 3; f->w[i].left = (signed char)q[0]; f->w[i].glyphW = q[1]; f->w[i].charW = q[2]; }
        }
        if (!nx || nx <= p) break; p = nx;
    }
    // character map
    f->map = (uint16_t *)malloc(65536 * sizeof(uint16_t)); if (!f->map) return 0;
    memset(f->map, 0xFF, 65536 * sizeof(uint16_t));
    for (uint32_t p = pM; p >= 8 && p + 12 <= n; ) {
        uint32_t a = FcRd32(b + p), z = FcRd32(b + p + 4); unsigned meth = FcRd16(b + p + 8); uint32_t nx = FcRd32(b + p + 12);
        const unsigned char *d = b + p + 16;
        if (meth == 0 && d + 2 <= b + n) { unsigned off = FcRd16(d); for (uint32_t c = a; c <= z && c < 65536; c++) f->map[c] = (uint16_t)(c - a + off); }
        else if (meth == 1) { for (uint32_t c = a; c <= z && c < 65536 && d + (c - a) * 2 + 2 <= b + n; c++) f->map[c] = (uint16_t)FcRd16(d + (c - a) * 2); }
        else if (meth == 2 && d + 4 <= b + n) {
            uint32_t k = FcRd32(d);
            for (uint32_t i = 0; i < k && d + 4 + i * 8 + 8 <= b + n; i++) { uint32_t c = FcRd32(d + 4 + i * 8); if (c < 65536) f->map[c] = (uint16_t)FcRd32(d + 8 + i * 8); }
        }
        if (!nx || nx <= p) break; p = nx;
    }
    for (int c = 0; c < 65536; c++) if (f->map[c] != 0xFFFF) f->nglyph++;
    return 1;
}
#endif
// where glyph gi is: sheet, and the cell's top-left pixel; 0 if not available
static int BffntCell(const Bffnt *f, int gi, int *sheet, int *x, int *y) {
    int per = f->cols * f->rows; if (gi < 0 || !per) return 0;
    int s = gi / per, c = gi % per; if (s >= f->nsheet || !f->sheet || !f->sheet[s]) return 0;
    *sheet = s; *x = (c % f->cols) * (f->cellW + 1) + 1; *y = (c / f->cols) * (f->cellH + 1) + 1;
    return 1;
}
static FfWidth BffntWidth(const Bffnt *f, int gi) { return gi >= 0 && gi < f->nw ? f->w[gi] : f->defW; }
#endif
