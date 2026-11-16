/* Reading game files from the dump: whole file -> zstd (if compressed) -> SARC member. Windows (wide paths). */
#ifndef ARC_UTIL_H
#define ARC_UTIL_H
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "zstd.h"

static uint32_t ArcRd32(const unsigned char *b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }
static unsigned char *ArcReadAll(const wchar_t *path, size_t *n) {
    wchar_t lp[2100];
    if (wcslen(path) > 250 && wcsncmp(path, L"\\\\?\\", 4)) swprintf(lp, 2100, L"\\\\?\\%ls", path); else swprintf(lp, 2100, L"%ls", path);
    HANDLE h = CreateFileW(lp, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    unsigned char *b = sz.QuadPart < (1LL << 31) ? (unsigned char *)malloc((size_t)sz.QuadPart + 1) : NULL;
    DWORD got = 0;
    if (!b || !ReadFile(h, b, (DWORD)sz.QuadPart, &got, NULL) || got != (DWORD)sz.QuadPart) { free(b); CloseHandle(h); return NULL; }
    CloseHandle(h); *n = got; return b;
}
// takes ownership of src; returns the decompressed data (or src itself if it wasn't compressed)
static unsigned char *ArcUnzstd(unsigned char *src, size_t n, size_t *outN) {
    if (n < 4 || !(src[0] == 0x28 && src[1] == 0xB5 && src[2] == 0x2F && src[3] == 0xFD)) { *outN = n; return src; }
    ZSTD_DStream *ds = ZSTD_createDStream(); if (!ds) { free(src); return NULL; }
    ZSTD_initDStream(ds);
    unsigned long long fs = ZSTD_getFrameContentSize(src, n);
    size_t cap = (fs != ZSTD_CONTENTSIZE_UNKNOWN && fs != ZSTD_CONTENTSIZE_ERROR && fs < (1ULL << 31)) ? (size_t)fs + 16 : n * 4 + 65536, len = 0;
    unsigned char *out = (unsigned char *)malloc(cap);
    ZSTD_inBuffer in = { src, n, 0 };
    while (out) {
        if (len == cap) { unsigned char *t = (unsigned char *)realloc(out, cap * 2); if (!t) { free(out); out = NULL; break; } out = t; cap *= 2; }
        ZSTD_outBuffer ob = { out + len, cap - len, 0 };
        size_t r = ZSTD_decompressStream(ds, &ob, &in);
        len += ob.pos;
        if (ZSTD_isError(r)) { free(out); out = NULL; break; }
        if (in.pos == in.size && ob.pos < ob.size) break;
    }
    ZSTD_freeDStream(ds); free(src);
    *outN = len; return out;
}
// a SARC's files: calls fn(name, data, size, user) for each; returns count (0 if not a SARC)
typedef int (*ArcEachFn)(const char *name, const unsigned char *data, size_t n, void *user);
static int ArcSarcEach(const unsigned char *s, size_t sn, ArcEachFn fn, void *user) {
    if (sn < 0x20 || memcmp(s, "SARC", 4) || s[6] != 0xFF || s[7] != 0xFE) return 0;
    uint32_t dataOff = ArcRd32(s + 0x0C); size_t o = s[4] | (s[5] << 8);
    if (o + 12 > sn || memcmp(s + o, "SFAT", 4)) return 0;
    int nf = s[o + 6] | (s[o + 7] << 8), k = 0;
    size_t ents = o + 12, names = ents + (size_t)nf * 16 + 8;
    for (int f = 0; f < nf; f++) {
        const unsigned char *e = s + ents + (size_t)f * 16;
        uint32_t attr = ArcRd32(e + 4), st = ArcRd32(e + 8), en = ArcRd32(e + 12);
        if (!(attr >> 24)) continue;
        size_t no = names + (size_t)(attr & 0xFFFFFF) * 4;
        if (no >= sn || (size_t)dataOff + en > sn || en < st) continue;
        k++;
        if (fn && fn((const char *)s + no, s + dataOff + st, en - st, user)) break;
    }
    return k;
}
typedef struct { const char *want; unsigned char *out; size_t n; } ArcFind_;
static int ArcFindCb_(const char *name, const unsigned char *d, size_t n, void *u) {
    ArcFind_ *f = (ArcFind_ *)u;
    if (strcmp(name, f->want)) return 0;
    f->out = (unsigned char *)malloc(n + 1); if (f->out) { memcpy(f->out, d, n); f->n = n; }
    return 1;
}
__attribute__((unused)) static unsigned char *ArcSarcMember(const unsigned char *s, size_t sn, const char *name, size_t *outN) {
    ArcFind_ f = { name, NULL, 0 }; ArcSarcEach(s, sn, ArcFindCb_, &f); *outN = f.n; return f.out;
}
#endif
