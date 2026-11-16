/* Minimal WebM / Matroska reader: finds the first video track and lists its frames (SimpleBlock / Block, no lacing
   needed for video). Portable C; the whole file is in memory. */
#ifndef WEBM_CORE_H
#define WEBM_CORE_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct { size_t off, size; double t; int key; } WmFrame;   // t in seconds
typedef struct {
    char codec[32]; int width, height; double duration, fps;
    int track;                  // video track number
    WmFrame *f; int nf, cap;
    char err[160];
} WmFile;

static int WmVint(const unsigned char *b, size_t n, size_t *o, uint64_t *v, int keepMarker) {
    if (*o >= n) return 0;
    unsigned c = b[*o]; int len = 1; unsigned mask = 0x80;
    while (len <= 8 && !(c & mask)) { mask >>= 1; len++; }
    if (len > 8 || *o + len > n) return 0;
    uint64_t x = keepMarker ? c : (c & (mask - 1));
    int allOnes = (c & (mask - 1)) == mask - 1;
    for (int i = 1; i < len; i++) { x = (x << 8) | b[*o + i]; if (b[*o + i] != 0xFF) allOnes = 0; }
    *o += len; *v = (!keepMarker && allOnes) ? UINT64_MAX : x;      // all ones = unknown size
    return 1;
}
static uint64_t WmUint(const unsigned char *b, size_t n) { uint64_t x = 0; for (size_t i = 0; i < n && i < 8; i++) x = (x << 8) | b[i]; return x; }
static double WmFloat(const unsigned char *b, size_t n) {
    if (n == 4) { uint32_t x = (uint32_t)WmUint(b, 4); float f; memcpy(&f, &x, 4); return f; }
    if (n == 8) { uint64_t x = WmUint(b, 8); double d; memcpy(&d, &x, 8); return d; }
    return 0;
}
static void WmAddFrame(WmFile *w, size_t off, size_t size, double t, int key) {
    if (w->nf == w->cap) { int c = w->cap ? w->cap * 2 : 256; WmFrame *q = (WmFrame *)realloc(w->f, c * sizeof(WmFrame)); if (!q) return; w->f = q; w->cap = c; }
    w->f[w->nf].off = off; w->f[w->nf].size = size; w->f[w->nf].t = t; w->f[w->nf].key = key; w->nf++;
}
// IDs (with their marker bits)
#define WM_SEGMENT 0x18538067
#define WM_INFO 0x1549A966
#define WM_TCSCALE 0x2AD7B1
#define WM_DURATION 0x4489
#define WM_TRACKS 0x1654AE6B
#define WM_TRACKENTRY 0xAE
#define WM_TRACKNUM 0xD7
#define WM_TRACKTYPE 0x83
#define WM_CODECID 0x86
#define WM_VIDEO 0xE0
#define WM_PIXW 0xB0
#define WM_PIXH 0xBA
#define WM_DEFDUR 0x23E383
#define WM_CLUSTER 0x1F43B675
#define WM_TIMECODE 0xE7
#define WM_SIMPLEBLOCK 0xA3
#define WM_BLOCKGROUP 0xA0
#define WM_BLOCK 0xA1

typedef struct { uint64_t tcScale; uint64_t clusterTc; uint64_t defDur; } WmCtx;
static int WmIsMaster(uint32_t id) { return id == WM_SEGMENT || id == WM_INFO || id == WM_TRACKS || id == WM_TRACKENTRY || id == WM_VIDEO || id == WM_CLUSTER || id == WM_BLOCKGROUP; }

static void WmBlock(WmFile *w, WmCtx *c, const unsigned char *b, size_t o, size_t end, int simple) {
    size_t p = o; uint64_t tn;
    if (!WmVint(b, end, &p, &tn, 0) || p + 3 > end) return;
    if (w->track && (int)tn != w->track) return;
    int16_t rel = (int16_t)((b[p] << 8) | b[p + 1]); unsigned fl = b[p + 2]; p += 3;
    if (fl & 0x06) return;                          // laced: not used for video
    double t = (double)(c->clusterTc + rel) * (double)c->tcScale / 1e9;
    WmAddFrame(w, p, end - p, t, simple ? (fl & 0x80) != 0 : 0);
}
static void WmParse(WmFile *w, WmCtx *c, const unsigned char *b, size_t o, size_t end, int depth, int *curTrackNum, int *curTrackType) {
    while (o < end && depth < 16) {
        uint64_t id, sz; size_t start = o;
        if (!WmVint(b, end, &o, &id, 1) || !WmVint(b, end, &o, &sz, 0)) return;
        size_t dEnd = sz == UINT64_MAX || o + sz > end ? end : o + (size_t)sz;
        uint32_t i = (uint32_t)id;
        if (WmIsMaster(i)) {
            if (i == WM_TRACKENTRY) { *curTrackNum = 0; *curTrackType = 0; }
            if (i == WM_CLUSTER && sz == UINT64_MAX) {      // unknown-size cluster: runs until the next cluster
                WmParse(w, c, b, o, end, depth + 1, curTrackNum, curTrackType); return;
            }
            WmParse(w, c, b, o, dEnd, depth + 1, curTrackNum, curTrackType);
            if (i == WM_TRACKENTRY && *curTrackType == 1 && !w->track) w->track = *curTrackNum;
        } else if (i == WM_CLUSTER) {
        } else switch (i) {
            case WM_TCSCALE: c->tcScale = WmUint(b + o, dEnd - o); break;
            case WM_DURATION: w->duration = WmFloat(b + o, dEnd - o); break;
            case WM_TRACKNUM: *curTrackNum = (int)WmUint(b + o, dEnd - o); break;
            case WM_TRACKTYPE: *curTrackType = (int)WmUint(b + o, dEnd - o);
                break;
            case WM_CODECID: if (!w->codec[0] || *curTrackType == 1) { size_t l = dEnd - o; if (l > 31) l = 31; memcpy(w->codec, b + o, l); w->codec[l] = 0; } break;
            case WM_PIXW: w->width = (int)WmUint(b + o, dEnd - o); break;
            case WM_PIXH: w->height = (int)WmUint(b + o, dEnd - o); break;
            case WM_DEFDUR: c->defDur = WmUint(b + o, dEnd - o); break;
            case WM_TIMECODE: c->clusterTc = WmUint(b + o, dEnd - o); break;
            case WM_SIMPLEBLOCK: WmBlock(w, c, b, o, dEnd, 1); break;
            case WM_BLOCK: WmBlock(w, c, b, o, dEnd, 0); break;
        }
        if (dEnd <= start) return;
        o = dEnd;
        if (i == WM_CLUSTER && sz == UINT64_MAX) return;
    }
}
static void WmFree(WmFile *w) { free(w->f); memset(w, 0, sizeof *w); }
static int WmOpen(WmFile *w, const unsigned char *b, size_t n) {
    memset(w, 0, sizeof *w);
    if (n < 4 || b[0] != 0x1A || b[1] != 0x45 || b[2] != 0xDF || b[3] != 0xA3) { snprintf(w->err, sizeof w->err, "Not a WebM / Matroska file."); return 0; }
    WmCtx c = { 1000000, 0, 0 }; int tn = 0, tt = 0;
    WmParse(w, &c, b, 0, n, 0, &tn, &tt);
    if (!w->nf) { snprintf(w->err, sizeof w->err, "No video frames were found."); return 0; }
    w->duration = w->duration * (double)c.tcScale / 1e9;
    if (c.defDur) w->fps = 1e9 / (double)c.defDur;
    else if (w->nf > 1 && w->f[w->nf - 1].t > w->f[0].t) w->fps = (w->nf - 1) / (w->f[w->nf - 1].t - w->f[0].t);
    if (w->duration <= 0 && w->fps > 0) w->duration = w->nf / w->fps;
    return 1;
}
#endif
