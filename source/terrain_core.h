/* Mario Kart World terrain tiles (TerrainArc\<scene>\<area>.<layer>, "TARB"): unpack and turn into pictures / heights.
   TARB: magic, u32 version, u16 chunk count, u16 header size, then (size, compressed size) per chunk; chunks are zstd.
   Inside: 0x34-byte header (words: [1] channel formats, [3] width, [5] format 2 (BCn kind in the top byte),
   [7]/[8] first section offset/size, [11]/[12] second section offset (from 0x2C) / size), then texels in the NVN2
   block-linear layout with 1-GOB-tall blocks. Portable C. */
#ifndef TERRAIN_CORE_H
#define TERRAIN_CORE_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "zstd.h"

int T2D_Decode(int kind, const uint8_t *data, int w, int h, int bw, int bh, uint32_t *out);

static uint32_t TrRd32(const unsigned char *b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }

// the TARB payload (all chunks joined). NULL if not a TARB or it can't be unpacked.
static unsigned char *TarbUnpack(const unsigned char *b, size_t n, size_t *outN) {
    if (n < 0x14 || memcmp(b, "TARB", 4)) return NULL;
    unsigned nch = b[8] | (b[9] << 8), hs = b[10] | (b[11] << 8);
    if (!nch || nch > 64 || 0x0C + (size_t)nch * 8 > n) return NULL;
    size_t tot = 0, q = hs;
    for (unsigned i = 0; i < nch; i++) tot += TrRd32(b + 0x0C + i * 8);
    if (tot > (256u << 20)) return NULL;
    unsigned char *out = (unsigned char *)malloc(tot + 16); if (!out) return NULL;
    size_t at = 0;
    for (unsigned i = 0; i < nch; i++) {
        size_t ds = TrRd32(b + 0x0C + i * 8), cs = TrRd32(b + 0x10 + i * 8);
        if (q + cs > n) { free(out); return NULL; }
        if (cs >= 4 && b[q] == 0x28 && b[q + 1] == 0xB5 && b[q + 2] == 0x2F && b[q + 3] == 0xFD) {
            size_t r = ZSTD_decompress(out + at, ds, b + q, cs);
            if (ZSTD_isError(r)) { free(out); return NULL; }
        } else memcpy(out + at, b + q, cs < ds ? cs : ds);
        at += ds; q += cs;
    }
    *outN = at; return out;
}

// NVN2 block-linear -> linear (1-GOB-tall blocks)
static void TrUntile(const unsigned char *src, size_t srcn, unsigned char *dst, int w, int h, int bpe) {
    size_t rb = (size_t)w * bpe, gx = (rb + 63) / 64;
    for (int y = 0; y < h; y++) {
        unsigned yy = (unsigned)(y & 7);
        size_t by = (size_t)(y / 8) * gx * 512;
        for (size_t xb = 0; xb < rb; xb++) {
            unsigned xx = (unsigned)(xb & 63);
            size_t a = by + (xb / 64) * 512 + ((xx & 15) | ((yy & 1) << 4) | (((yy >> 1) & 1) << 5) | (((xx >> 4) & 1) << 6) |
                                              (((yy >> 2) & 1) << 7) | (((xx >> 5) & 1) << 8));
            dst[y * rb + xb] = a < srcn ? src[a] : 0;
        }
    }
}

static float TrUf(unsigned x, int mb) {                     // unsigned small float (R11G11B10)
    unsigned e = (x >> mb) & 31, m = x & ((1u << mb) - 1);
    return e == 0 ? (float)m / (1 << mb) * (1.0f / 16384) : (1 + (float)m / (1 << mb)) * powf(2.0f, (float)e - 15);
}

// 257 x 257 heights (0-65535) from an unpacked .hght payload. 1 on success.
static int TarbHeights(const unsigned char *d, size_t n, uint16_t *out) {
    if (n < 0x34 || TrRd32(d + 0x0C) != 257) return 0;
    size_t off2 = 0x2C + TrRd32(d + 0x2C), sz2 = TrRd32(d + 0x30);
    if (memcmp(d + 4, "\x09\0\0\0", 4) || off2 + sz2 > n) return 0;
    unsigned char *lin = (unsigned char *)malloc(257 * 257 * 2); if (!lin) return 0;
    TrUntile(d + off2, sz2, lin, 257, 257, 2);
    for (int i = 0; i < 257 * 257; i++) out[i] = lin[i * 2] | (lin[i * 2 + 1] << 8);
    free(lin); return 1;
}

/* water layer (<area>.water.extm): first section = 68 x 68 cells of 8 bytes, linear: a 2-cell border round 64 x 64
   real cells whose centres sit at (j + 0.5) / 64 across the tile (the same layout as the colour maps' 260 = 256 + 4).
   Per cell: u16 surface height (0-65535 = 0-max_height), two u16 around 0x7FFF (flow?), byte 6 = 3 for a real cell
   (0 in the border), byte 7 = kind (0 water, 1 mud, 2 lava, 3 ghost swamp - the order of the game's water material
   labels 00-03). (The second section is the same grid again, tiled and cut to 65 x 65.) 1 on success. */
static int TarbWater(const unsigned char *d, size_t n, unsigned char *out) {
    if (n < 0x34 || TrRd32(d) != 0x00024400 || memcmp(d + 4, "\x09\x09\x09\x09", 4)) return 0;
    size_t off1 = 0x1C + TrRd32(d + 0x1C), sz1 = TrRd32(d + 0x20);
    if (sz1 < 68 * 68 * 8 || off1 + 68 * 68 * 8 > n) return 0;
    memcpy(out, d + off1, 68 * 68 * 8);
    return 1;
}
static void TrMatColour(unsigned m, unsigned char c[3]) {      // a steady, distinct colour per material number
    static const unsigned char pal[12][3] = { {120,170,80},{190,170,120},{140,140,140},{90,120,60},{210,200,170},{160,110,70},
                                              {100,150,190},{230,230,235},{170,90,60},{80,100,80},{200,180,90},{130,110,150} };
    unsigned k = (m * 7u + 3u) % 12u; c[0] = pal[k][0]; c[1] = pal[k][1]; c[2] = pal[k][2];
}

/* Any terrain layer -> a square RGBA picture. *side = its width/height. desc: what it shows. NULL + err if unsupported. */
static unsigned char *TarbToRGBA(const unsigned char *file, size_t fn, int *side, char *desc, size_t dn, char *err, size_t en) {
    size_t n; unsigned char *d = TarbUnpack(file, fn, &n);
    if (!d) { snprintf(err, en, "This terrain tile couldn't be unpacked."); return NULL; }
    unsigned char *rgba = NULL; int s = 0;
    if (n < 0x34) { free(d); snprintf(err, en, "This terrain tile is too short."); return NULL; }
    uint32_t w0 = TrRd32(d), w3 = TrRd32(d + 0x0C), w5 = TrRd32(d + 0x14), w7 = TrRd32(d + 0x1C), w8 = TrRd32(d + 0x20);
    uint32_t w11 = TrRd32(d + 0x2C), w12 = TrRd32(d + 0x30);
    const unsigned char *fmt = d + 4, *body = d + 0x34; size_t bn = n - 0x34;
    if (!memcmp(fmt, "\x09\0\0\0", 4) && w3 == 257) {                                   // heights
        uint16_t *h = (uint16_t *)malloc(257 * 257 * 2);
        if (h && TarbHeights(d, n, h)) {
            unsigned lo = 65535, hi = 0; for (int i = 0; i < 257 * 257; i++) { if (h[i] < lo) lo = h[i]; if (h[i] > hi) hi = h[i]; }
            s = 257; rgba = (unsigned char *)malloc((size_t)s * s * 4);
            if (rgba) for (int i = 0; i < s * s; i++) { unsigned char g = (unsigned char)(hi > lo ? (h[i] - lo) * 255u / (hi - lo) : 0);
                rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = g; rgba[i * 4 + 3] = 255; }
            snprintf(desc, dn, "heights %u-%u (world %.1f-%.1f), lighter = higher", lo, hi, lo / 65535.0 * 2000, hi / 65535.0 * 2000);
        }
        free(h);
    } else if (!memcmp(fmt, "\x06\x06\0\0", 4)) {                                       // normals (X, Z)
        s = 257; unsigned char *lin = (unsigned char *)malloc((size_t)s * s * 2); rgba = (unsigned char *)malloc((size_t)s * s * 4);
        if (lin && rgba) {
            TrUntile(body, bn, lin, s, s, 2);
            for (int i = 0; i < s * s; i++) {
                float x = (lin[i * 2] - 128) / 127.0f, z = (lin[i * 2 + 1] - 128) / 127.0f, y2 = 1 - x * x - z * z, y = y2 > 0 ? sqrtf(y2) : 0;
                rgba[i * 4] = lin[i * 2]; rgba[i * 4 + 1] = (unsigned char)(y * 127 + 128); rgba[i * 4 + 2] = lin[i * 2 + 1]; rgba[i * 4 + 3] = 255;
            }
            snprintf(desc, dn, "surface normals (red = X, green = up, blue = Z)");
        }
        free(lin);
    } else if (!memcmp(fmt, "\x06\x06\x06\x06", 4)) {                                   // materials
        s = 257; unsigned char *lin = (unsigned char *)malloc((size_t)s * s * 4); rgba = (unsigned char *)malloc((size_t)s * s * 4);
        if (lin && rgba) {
            TrUntile(body, bn, lin, s, s, 4);
            for (int i = 0; i < s * s; i++) {
                unsigned char a[3], b2[3]; TrMatColour(lin[i * 4], a); TrMatColour(lin[i * 4 + 1], b2);
                unsigned t = lin[i * 4 + 2];
                for (int k = 0; k < 3; k++) rgba[i * 4 + k] = (unsigned char)((a[k] * (255 - t) + b2[k] * t) / 255);
                rgba[i * 4 + 3] = 255;
            }
            snprintf(desc, dn, "ground materials (one colour per material, blended)");
        }
        free(lin);
    } else if (!memcmp(fmt, "\x08\x08\x07\0", 4)) {                                     // baked light, R11G11B10 floats
        s = 257; unsigned char *lin = (unsigned char *)malloc((size_t)s * s * 4); rgba = (unsigned char *)malloc((size_t)s * s * 4);
        float *f = (float *)malloc((size_t)s * s * 3 * sizeof(float));
        if (lin && rgba && f) {
            TrUntile(body, bn, lin, s, s, 4); float mx = 0;
            for (int i = 0; i < s * s; i++) { uint32_t v = TrRd32(lin + i * 4); f[i * 3] = TrUf(v & 0x7FF, 6); f[i * 3 + 1] = TrUf((v >> 11) & 0x7FF, 6); f[i * 3 + 2] = TrUf(v >> 22, 5);
                for (int k = 0; k < 3; k++) if (f[i * 3 + k] > mx) mx = f[i * 3 + k]; }
            for (int i = 0; i < s * s; i++) { for (int k = 0; k < 3; k++) { float c = f[i * 3 + k] / (f[i * 3 + k] + 0.05f); rgba[i * 4 + k] = (unsigned char)(c * 255); } rgba[i * 4 + 3] = 255; }
            snprintf(desc, dn, "baked light (packed floats, brightest %.2f; brightened to show dim light)", mx);
        }
        free(lin); free(f);
    } else if (w0 == 0x00024400 && !memcmp(fmt, "\x09\x09\x09\x09", 4)) {              // water cells (65 x 65 x 8 bytes)
        s = 65; size_t off2 = 0x2C + w11;
        unsigned char *lin = (unsigned char *)malloc((size_t)s * s * 8); rgba = (unsigned char *)malloc((size_t)s * s * 4);
        if (lin && rgba && off2 + w12 <= n) {
            TrUntile(d + off2, w12, lin, s, s, 8);
            unsigned lo = 65535, hi = 0;
            for (int i = 0; i < s * s; i++) if (lin[i * 8 + 6]) { unsigned v = lin[i * 8] | (lin[i * 8 + 1] << 8); if (v < lo) lo = v; if (v > hi) hi = v; }
            for (int i = 0; i < s * s; i++) {
                unsigned v = lin[i * 8] | (lin[i * 8 + 1] << 8), fl = lin[i * 8 + 6];
                unsigned char g = (unsigned char)(hi > lo ? 60 + (v - lo) * 195u / (hi - lo) : 160);
                rgba[i * 4] = fl ? g / 4 : 0; rgba[i * 4 + 1] = fl ? g / 2 : 0; rgba[i * 4 + 2] = fl ? g : 0; rgba[i * 4 + 3] = 255;
            }
            snprintf(desc, dn, "water surface height (brighter blue = higher; black = edge padding)");
        } else { free(rgba); rgba = NULL; }
        free(lin);
    } else if (w7 == 0x18 && !memcmp(fmt, "\x06\0\0\0", 4) && w8 == 260 * 260 && 0x34 + w8 <= n) {   // slow-down, linear bytes
        s = 260; rgba = (unsigned char *)malloc((size_t)s * s * 4);
        if (rgba) for (int i = 0; i < s * s; i++) { unsigned char g = body[i]; rgba[i * 4] = g; rgba[i * 4 + 1] = g; rgba[i * 4 + 2] = g; rgba[i * 4 + 3] = 255; }
        snprintf(desc, dn, "off-road slow-down (white = most, black = none: roads)");
    } else if (fmt[1] == 0x16 && ((w5 >> 24) == 0x30 || (w5 >> 24) == 0x34)) {          // BC1 / BC3 colour
        int sc = (w0 & 0xFF) == 2 ? 2 : 1;                   // sky_bounce: 520 x 520 (twice the usual detail)
        int bpe = (w5 >> 24) == 0x30 ? 8 : 16, bw = 65 * sc;
        size_t need = (((size_t)bw * bpe + 63) / 64) * 64 * (size_t)((bw + 7) / 8 * 8);
        if (w12 != need || bn < need) { free(d); snprintf(err, en, "This layer's picture is stored in a way that isn't understood yet (%u bytes).", w12); return NULL; }
        s = 260 * sc; unsigned char *lin = (unsigned char *)malloc((size_t)bw * bw * bpe); rgba = (unsigned char *)malloc((size_t)s * s * 4);
        if (lin && rgba) {
            TrUntile(body, bn, lin, bw, bw, bpe);
            T2D_Decode(bpe == 8 ? 1 : 3, lin, s, s, 4, 4, (uint32_t *)rgba);
            for (int i = 0; i < s * s; i++) { unsigned char c = rgba[i * 4]; rgba[i * 4] = rgba[i * 4 + 2]; rgba[i * 4 + 2] = c; rgba[i * 4 + 3] = 255; }
            snprintf(desc, dn, "colour (BC%d%s)", bpe == 8 ? 1 : 3, sc > 1 ? ", 520 x 520" : "");
        }
        free(lin);
    } else { free(d); snprintf(err, en, "This terrain layer isn't drawn yet."); return NULL; }
    free(d);
    if (!rgba) { snprintf(err, en, "Out of memory."); return NULL; }
    *side = s; return rgba;
}

/* XTX (DFvN) single texture, 8-bit grey -> RGBA. */
static unsigned char *XtxToRGBA(const unsigned char *b, size_t n, int *side, char *err, size_t en) {
    if (n < 0x10 || memcmp(b, "DFvN", 4)) { snprintf(err, en, "Not an XTX file."); return NULL; }
    size_t p = TrRd32(b + 4); int w = 0, h = 0, fmt = -1; const unsigned char *data = NULL; size_t dsz = 0;
    while (p + 0x24 <= n && !memcmp(b + p, "HBvN", 4)) {
        uint64_t ds = TrRd32(b + p + 8) | ((uint64_t)TrRd32(b + p + 12) << 32), doff = TrRd32(b + p + 16);
        uint32_t typ = TrRd32(b + p + 24);
        if (p + doff + ds > n) break;
        if (typ == 2 && ds >= 0x20) { w = (int)TrRd32(b + p + doff + 12); h = (int)TrRd32(b + p + doff + 16); fmt = (int)TrRd32(b + p + doff + 28); }
        else if (typ == 3) { data = b + p + doff; dsz = (size_t)ds; }
        p += doff + ds;
    }
    if (!data || fmt != 1 || w <= 0 || w != h || w > 4096) { snprintf(err, en, "This XTX texture's format isn't drawn yet."); return NULL; }
    unsigned char *lin = (unsigned char *)malloc((size_t)w * h), *rgba = (unsigned char *)malloc((size_t)w * h * 4);
    if (!lin || !rgba) { free(lin); free(rgba); snprintf(err, en, "Out of memory."); return NULL; }
    {   // XTX: blocks as tall as the texture needs (up to 16 GOBs)
        int bh = 1; while (bh < 16 && bh * 8 < h) bh *= 2;
        size_t gx = ((size_t)w + 63) / 64;
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            unsigned yy = y & 7, xx = x & 63;
            size_t a = (size_t)(y / (8 * bh)) * gx * bh * 512 + (size_t)((y % (8 * bh)) / 8) * 512 + (size_t)(x / 64) * bh * 512 +
                       ((xx & 15) | ((yy & 1) << 4) | (((yy >> 1) & 1) << 5) | (((xx >> 4) & 1) << 6) | (((yy >> 2) & 1) << 7) | (((xx >> 5) & 1) << 8));
            lin[(size_t)y * w + x] = a < dsz ? data[a] : 0;
        }
    }
    for (size_t i = 0; i < (size_t)w * h; i++) { rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = lin[i]; rgba[i * 4 + 3] = 255; }
    free(lin); *side = w; return rgba;
}
#endif
