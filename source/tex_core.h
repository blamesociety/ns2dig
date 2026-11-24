/* BNTX textures (Switch / Switch 2): list, untile and decode to RGBA. Portable C (needs t2d_c.cpp for BC/ASTC). */
#ifndef TEX_CORE_H
#define TEX_CORE_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

int T2D_Decode(int kind, const uint8_t *data, int w, int h, int bw, int bh, uint32_t *out);

typedef struct {
    char name[128];
    uint32_t fmt;                 // high byte = format, low byte = type (1 unorm, 6 srgb...)
    int w, h, depth, layers, mips, bhl, tile;
    uint32_t size;                // bytes of image data (all mips)
    uint64_t data;                // offset of mip 0 in the file
    unsigned char chan[4];        // channel mapping: 0 zero, 1 one, 2 R, 3 G, 4 B, 5 A
} TexInfo;

typedef struct { int ounce; int n; TexInfo *t; char err[200]; } TexFile;

static uint64_t TexRd64(const unsigned char *b) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = (v << 8) | b[i]; return v; }
static uint32_t TexRd32(const unsigned char *b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }

typedef struct { const char *name; int bw, bh, bpe, kind; } TexFmt;   // kind: 0 plain, 1..7 BCn, 100 ASTC
static int TexFormat(uint32_t fmt, TexFmt *f) {
    static const int astc[14][2] = { {4,4},{5,4},{5,5},{6,5},{6,6},{8,5},{8,6},{8,8},{10,5},{10,6},{10,8},{10,10},{12,10},{12,12} };
    static char nm[16][16];
    int t = fmt >> 8;
    memset(f, 0, sizeof *f); f->bw = f->bh = 1;
    switch (t) {
    case 0x02: f->name = "R8"; f->bpe = 1; return 1;
    case 0x09: f->name = "RG8"; f->bpe = 2; return 1;
    case 0x0b: f->name = "RGBA8"; f->bpe = 4; return 1;
    case 0x0c: f->name = "BGRA8"; f->bpe = 4; return 1;
    case 0x15: f->name = (fmt & 0xff) == 5 ? "RGBA16F" : "RGBA16"; f->bpe = 8; return 1;   // (0x1505: the water colours CmnTex_Tera_Water_Wat)
    case 0x1a: f->name = "BC1"; f->bw = f->bh = 4; f->bpe = 8; f->kind = 1; return 1;
    case 0x1c: f->name = "BC3"; f->bw = f->bh = 4; f->bpe = 16; f->kind = 3; return 1;
    case 0x1d: f->name = "BC4"; f->bw = f->bh = 4; f->bpe = 8; f->kind = 4; return 1;
    case 0x1e: f->name = "BC5"; f->bw = f->bh = 4; f->bpe = 16; f->kind = 5; return 1;
    case 0x1f: f->name = "BC6H"; f->bw = f->bh = 4; f->bpe = 16; f->kind = 6; return 1;
    case 0x20: f->name = "BC7"; f->bw = f->bh = 4; f->bpe = 16; f->kind = 7; return 1;
    }
    if (t >= 0x2d && t < 0x2d + 14) {
        int i = t - 0x2d; snprintf(nm[i], 16, "ASTC %dx%d", astc[i][0], astc[i][1]);
        f->name = nm[i]; f->bw = astc[i][0]; f->bh = astc[i][1]; f->bpe = 16; f->kind = 100; return 1;
    }
    f->name = "unknown"; return 0;
}

static int TexOpen(TexFile *tf, const unsigned char *b, size_t n) {
    memset(tf, 0, sizeof *tf);
    if (n >= 16 && !memcmp(b, "NSDIGIMG", 8)) {          // a picture already decoded to RGBA (JPEG / PNG): "NSDIGIMG", w, h, pixels
        int w = (int)TexRd32(b + 8), h = (int)TexRd32(b + 12);
        if (w > 0 && h > 0 && w <= 16384 && h <= 16384 && (size_t)w * h * 4 + 16 <= n) {
            tf->t = (TexInfo *)calloc(1, sizeof(TexInfo)); if (!tf->t) return 0;
            TexInfo *t = &tf->t[0];
            snprintf(t->name, sizeof t->name, "picture (%dx%d)", w, h);
            t->fmt = 0x0b01; t->w = w; t->h = h; t->depth = t->layers = t->mips = 1; t->tile = 1;
            t->size = (uint32_t)((size_t)w * h * 4); t->data = 16; t->chan[0] = 2; t->chan[1] = 3; t->chan[2] = 4; t->chan[3] = 5;
            tf->n = 1; return 1;
        }
    }
    if (n >= 64 && memcmp(b, "BNTX", 4)) {               // raw square RGBA8 image (agl's arrow.raw: the 32x32 mouse pointer)
        int s = 1; while ((size_t)s * s * 4 < n && s < 4096) s++;
        if ((size_t)s * s * 4 == n) {
            tf->t = (TexInfo *)calloc(1, sizeof(TexInfo)); if (!tf->t) return 0;
            TexInfo *t = &tf->t[0];
            snprintf(t->name, sizeof t->name, "image (%dx%d RGBA)", s, s);
            t->fmt = 0x0b01; t->w = t->h = s; t->depth = t->layers = t->mips = 1; t->tile = 1;
            t->size = (uint32_t)n; t->data = 0; t->chan[0] = 2; t->chan[1] = 3; t->chan[2] = 4; t->chan[3] = 5;
            tf->n = 1; return 1;
        }
    }
    if (n < 0x40 || memcmp(b, "BNTX", 4)) { snprintf(tf->err, sizeof tf->err, "Not a BNTX file."); return 0; }
    tf->ounce = !memcmp(b + 0x20, "Ounc", 4);
    uint32_t cnt = TexRd32(b + 0x24); uint64_t tab = TexRd64(b + 0x28);
    if (cnt > 100000 || tab + (uint64_t)cnt * 8 > n) { snprintf(tf->err, sizeof tf->err, "Bad texture table."); return 0; }
    tf->t = (TexInfo *)calloc(cnt ? cnt : 1, sizeof(TexInfo));
    if (!tf->t) return 0;
    for (uint32_t i = 0; i < cnt; i++) {
        uint64_t o = TexRd64(b + tab + i * 8);
        if (o + 0x80 > n || memcmp(b + o, "BRTI", 4)) continue;
        TexInfo *t = &tf->t[tf->n];
        t->tile = b[o + 0x12] | (b[o + 0x13] << 8); t->mips = b[o + 0x16] | (b[o + 0x17] << 8);
        t->fmt = TexRd32(b + o + 0x1C); t->w = (int)TexRd32(b + o + 0x24); t->h = (int)TexRd32(b + o + 0x28);
        t->depth = (int)TexRd32(b + o + 0x2C); t->layers = (int)TexRd32(b + o + 0x30); t->bhl = (int)TexRd32(b + o + 0x34);
        t->size = TexRd32(b + o + 0x50); memcpy(t->chan, b + o + 0x58, 4);
        uint64_t no = TexRd64(b + o + 0x60), mt = TexRd64(b + o + 0x70);
        if (no + 2 <= n) { unsigned l = b[no] | (b[no + 1] << 8); if (l > 127) l = 127; if (no + 2 + l <= n) { memcpy(t->name, b + no + 2, l); t->name[l] = 0; } }
        if (mt + 8 <= n) t->data = TexRd64(b + mt);
        if (t->w <= 0 || t->h <= 0 || t->w > 16384 || t->h > 16384 || t->data + t->size > n) continue;
        tf->n++;
    }
    return 1;
}
static void TexClose(TexFile *tf) { free(tf->t); memset(tf, 0, sizeof *tf); }

// block linear (GOB = 64 bytes x 8 rows). Switch 2 ("Ounc") swaps two address bits inside each GOB vs the Switch 1.
static void TexUntile(const unsigned char *src, size_t srcn, unsigned char *dst, int wb, int hb, int bpe, int bhl, int ounce) {
    size_t gobh = (size_t)1 << bhl, wg = ((size_t)wb * bpe + 63) / 64;
    for (int y = 0; y < hb; y++) {
        size_t rowbase = (y / (8 * gobh)) * (512 * gobh * wg) + ((y % (8 * gobh)) / 8) * 512;
        unsigned yy = (unsigned)(y & 7);
        for (int x = 0; x < wb; x++) {
            size_t xb = (size_t)x * bpe;
            size_t a = rowbase + (xb / 64) * (512 * gobh);
            unsigned xx = (unsigned)(xb & 63);
            unsigned in = ounce ? ((xx & 15) | ((yy & 1) << 4) | (((yy >> 1) & 1) << 5) | (((xx >> 4) & 1) << 6) | (((yy >> 2) & 1) << 7) | (((xx >> 5) & 1) << 8))
                                : ((xx & 15) | ((yy & 1) << 4) | (((xx >> 4) & 1) << 5) | (((yy >> 1) & 3) << 6) | (((xx >> 5) & 1) << 8));
            a += in;
            unsigned char *d = dst + ((size_t)y * wb + x) * bpe;
            if (a + bpe <= srcn) memcpy(d, src + a, bpe); else memset(d, 0, bpe);
        }
    }
}

// mip 0, first layer -> RGBA bytes (w*h*4), with the texture's channel mapping applied. NULL if unsupported.
static unsigned char *TexDecode(const unsigned char *b, size_t n, const TexInfo *t, int ounce, char *err, size_t errn) {
    TexFmt f;
    if (!TexFormat(t->fmt, &f)) { snprintf(err, errn, "Texture format 0x%04X isn't supported yet.", t->fmt); return NULL; }
    if (t->data + t->size > n) { snprintf(err, errn, "The texture data is outside the file."); return NULL; }
    int wb = (t->w + f.bw - 1) / f.bw, hb = (t->h + f.bh - 1) / f.bh;
    size_t lin = (size_t)wb * hb * f.bpe, np = (size_t)t->w * t->h;
    unsigned char *l = (unsigned char *)malloc(lin + 16);
    uint32_t *px = (uint32_t *)calloc(np + 4, 4);
    if (!l || !px) { free(l); free(px); snprintf(err, errn, "Out of memory."); return NULL; }
    if (t->tile == 0) TexUntile(b + t->data, (size_t)t->size, l, wb, hb, f.bpe, t->bhl, ounce);
    else memcpy(l, b + t->data, lin <= t->size ? lin : t->size);            // already linear
    unsigned char *rgba = (unsigned char *)px;
    if (f.kind) {                                                           // BGRA out
        T2D_Decode(f.kind, l, t->w, t->h, f.bw, f.bh, px);
        for (size_t i = 0; i < np; i++) { unsigned char *q = rgba + i * 4, c = q[0]; q[0] = q[2]; q[2] = c; }
    } else {
        for (size_t i = 0; i < np; i++) {
            const unsigned char *s = l + i * f.bpe; unsigned char *q = rgba + i * 4;
            if (f.bpe == 8) {                                             // 4 x 16 bits: half floats (shown 0-1) or unsigned
                for (int c = 0; c < 4; c++) {
                    unsigned hv = s[c * 2] | (s[c * 2 + 1] << 8); float fv;
                    if ((t->fmt & 0xff) == 5) {
                        int e = (hv >> 10) & 31, m = hv & 1023; fv = e == 0 ? m / 16777216.0f : e == 31 ? 1e9f : ldexpf(1.0f + m / 1024.0f, e - 15);
                        if (hv & 0x8000) fv = -fv;
                    } else fv = hv / 65535.0f;
                    q[c] = (unsigned char)(fv <= 0 ? 0 : fv >= 1 ? 255 : (int)(fv * 255 + 0.5f));
                }
            }
            else if (f.bpe == 1) { q[0] = s[0]; q[1] = q[2] = 0; q[3] = 255; }
            else if (f.bpe == 2) { q[0] = s[0]; q[1] = s[1]; q[2] = 0; q[3] = 255; }
            else if (!strcmp(f.name, "BGRA8")) { q[0] = s[2]; q[1] = s[1]; q[2] = s[0]; q[3] = s[3]; }
            else memcpy(q, s, 4);
        }
    }
    free(l);
    // channel mapping (BC4 masks are usually "white with the mask as alpha")
    int ident = t->chan[0] == 2 && t->chan[1] == 3 && t->chan[2] == 4 && t->chan[3] == 5;
    if (!ident) {
        for (size_t i = 0; i < np; i++) {
            unsigned char *q = rgba + i * 4, src[6] = { 0, 255, q[0], q[1], q[2], q[3] }, o[4];
            for (int k = 0; k < 4; k++) o[k] = t->chan[k] < 6 ? src[t->chan[k]] : 0;
            memcpy(q, o, 4);
        }
    }
    return rgba;
}
#endif
