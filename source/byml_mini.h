/* Minimal little-endian BYML reader (v2-7): look up dictionary keys, array items and string values in place. */
#ifndef BYML_MINI_H
#define BYML_MINI_H
#include <stdint.h>
#include <string.h>
#include <stddef.h>

typedef struct { const unsigned char *b; size_t n; uint32_t keys, strs; } Byml;
typedef struct { int type; uint32_t v; } BymlNode;   // type 0 = missing

static uint32_t Byml24_(const Byml *y, size_t p) { return p + 3 <= y->n ? (uint32_t)(y->b[p] | (y->b[p + 1] << 8) | (y->b[p + 2] << 16)) : 0; }
static uint32_t Byml32_(const Byml *y, size_t p) { return p + 4 <= y->n ? (uint32_t)(y->b[p] | (y->b[p + 1] << 8) | (y->b[p + 2] << 16) | ((uint32_t)y->b[p + 3] << 24)) : 0; }
static const char *BymlTabStr_(const Byml *y, uint32_t tab, uint32_t i) {
    if (!tab || tab + 4 > y->n || y->b[tab] != 0xC2 || i >= Byml24_(y, tab + 1)) return NULL;
    uint32_t o = tab + Byml32_(y, tab + 4 + (size_t)i * 4);
    if (o >= y->n || !memchr(y->b + o, 0, y->n - o)) return NULL;
    return (const char *)y->b + o;
}
// root node; type 0 if not BYML
static BymlNode BymlOpen(Byml *y, const unsigned char *b, size_t n) {
    BymlNode r = { 0, 0 };
    y->b = b; y->n = n;
    if (n < 16 || b[0] != 'Y' || b[1] != 'B') return r;
    y->keys = Byml32_(y, 4); y->strs = Byml32_(y, 8);
    uint32_t root = Byml32_(y, 12);
    if (!root || root >= n) return r;
    r.type = b[root]; r.v = root; return r;
}
static BymlNode BymlGet(const Byml *y, BymlNode d, const char *key) {
    BymlNode r = { 0, 0 };
    if (d.type != 0xC1 || d.v >= y->n) return r;
    uint32_t cnt = Byml24_(y, d.v + 1);
    for (uint32_t i = 0; i < cnt; i++) {
        size_t p = d.v + 4 + (size_t)i * 8;
        if (p + 8 > y->n) break;
        const char *k = BymlTabStr_(y, y->keys, Byml24_(y, p));
        if (k && !strcmp(k, key)) { r.type = y->b[p + 3]; r.v = Byml32_(y, p + 4); return r; }
    }
    return r;
}
static int BymlCount(const Byml *y, BymlNode a) { return (a.type == 0xC0 || a.type == 0xC1) && a.v < y->n ? (int)Byml24_(y, a.v + 1) : 0; }
static BymlNode BymlAt(const Byml *y, BymlNode a, int i) {
    BymlNode r = { 0, 0 };
    if (a.type != 0xC0 || i < 0 || i >= BymlCount(y, a)) return r;
    int cnt = BymlCount(y, a);
    size_t base = (a.v + 4 + (size_t)cnt + 3) & ~(size_t)3;
    r.type = y->b[a.v + 4 + i]; r.v = Byml32_(y, base + (size_t)i * 4);
    return r;
}
static const char *BymlStr(const Byml *y, BymlNode s) { return s.type == 0xA0 ? BymlTabStr_(y, y->strs, s.v) : NULL; }
static float BymlFloat(BymlNode s, float def) {
    if (s.type == 0xD2) { float f; memcpy(&f, &s.v, 4); return f; }
    if (s.type == 0xD1) return (float)(int32_t)s.v;
    if (s.type == 0xD3) return (float)s.v;
    return def;
}
// a 64-bit value (u64 / s64 stored elsewhere; 32-bit ones widened); def if not a number
static uint64_t BymlU64(const Byml *y, BymlNode s, uint64_t def) {
    if ((s.type == 0xD4 || s.type == 0xD5) && (size_t)s.v + 8 <= y->n) { uint64_t r; memcpy(&r, y->b + s.v, 8); return r; }
    if (s.type == 0xD1 || s.type == 0xD3) return s.v;
    return def;
}
static int BymlBool(BymlNode s, int def) { return s.type == 0xD0 ? s.v != 0 : def; }
#endif
