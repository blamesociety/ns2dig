/* MSBT (MsgStdBn) message files: the game's text in each language, and turning a message (with its control
   tags) into plain display text for the layout viewer. Plain C, no Windows calls - text is UTF-16 (uint16_t). */
#ifndef MSG_CORE_H
#define MSG_CORE_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct { char *label; unsigned char *b; int nb; } MsgEnt;   // b = raw UTF-16LE bytes of the message (with tags)
typedef struct { MsgEnt *e; int n; } Msbt;

static unsigned MsgRd16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static uint32_t MsgRd32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static inline void MsbtFree(Msbt *m) {
    for (int i = 0; i < m->n; i++) { free(m->e[i].label); free(m->e[i].b); }
    free(m->e); m->e = NULL; m->n = 0;
}
// little-endian UTF-16 MSBT only (what the game uses); returns 1 on success
static int MsbtParse(Msbt *m, const unsigned char *b, size_t n) {
    memset(m, 0, sizeof *m);
    if (n < 0x20 || memcmp(b, "MsgStdBn", 8) || b[8] != 0xFF || b[9] != 0xFE || b[0x0C] != 1) return 0;
    int nsec = (int)MsgRd16(b + 0x0E);
    size_t pos = 0x20;
    char **labels = NULL; int nlab = 0;
    for (int s = 0; s < nsec && pos + 16 <= n; s++) {
        uint32_t size = MsgRd32(b + pos + 4); size_t d = pos + 16;
        if (d + size > n) break;
        if (!memcmp(b + pos, "LBL1", 4) && size >= 4) {
            uint32_t slots = MsgRd32(b + d);
            for (uint32_t k = 0; k < slots && d + 4 + k * 8 + 8 <= d + size; k++) {
                uint32_t cnt = MsgRd32(b + d + 4 + k * 8), off = MsgRd32(b + d + 8 + k * 8);
                size_t p = d + off;
                for (uint32_t j = 0; j < cnt && p < d + size; j++) {
                    int ln = b[p];
                    if (p + 1 + ln + 4 > d + size) break;
                    uint32_t idx = MsgRd32(b + p + 1 + ln);
                    if (idx < 100000) {
                        if ((int)idx >= nlab) {
                            char **nl = (char **)realloc(labels, (idx + 1) * sizeof(char *)); if (!nl) break;
                            memset(nl + nlab, 0, (idx + 1 - nlab) * sizeof(char *)); labels = nl; nlab = idx + 1;
                        }
                        free(labels[idx]); labels[idx] = (char *)malloc(ln + 1);
                        if (labels[idx]) { memcpy(labels[idx], b + p + 1, ln); labels[idx][ln] = 0; }
                    }
                    p += 1 + ln + 4;
                }
            }
        } else if (!memcmp(b + pos, "TXT2", 4) && size >= 4 && !m->e) {
            uint32_t cnt = MsgRd32(b + d);
            if (cnt < 100000 && 4 + cnt * 4 <= size) {
                m->e = (MsgEnt *)calloc(cnt + 1, sizeof(MsgEnt));
                if (m->e) {
                    m->n = (int)cnt;
                    for (uint32_t i = 0; i < cnt; i++) {
                        uint32_t a = MsgRd32(b + d + 4 + i * 4), z = i + 1 < cnt ? MsgRd32(b + d + 8 + i * 4) : size;
                        if (a > size || z > size || z < a) continue;
                        m->e[i].b = (unsigned char *)malloc(z - a + 2);
                        if (m->e[i].b) { memcpy(m->e[i].b, b + d + a, z - a); m->e[i].nb = (int)(z - a); }
                    }
                }
            }
        }
        pos = d + ((size + 15) & ~(size_t)15);
    }
    for (int i = 0; i < m->n; i++) {
        if (i < nlab && labels[i]) { m->e[i].label = labels[i]; labels[i] = NULL; }
        else { m->e[i].label = (char *)malloc(16); if (m->e[i].label) snprintf(m->e[i].label, 16, "#%d", i); }
    }
    for (int i = 0; i < nlab; i++) free(labels[i]);
    free(labels);
    return m->e != NULL;
}
static int MsbtFind(const Msbt *m, const char *label) {
    for (int i = 0; i < m->n; i++) if (m->e[i].label && !strcmp(m->e[i].label, label)) return i;
    return -1;
}
/* Which message belongs to a text pane. key = the pane's name, with the part panes it sits inside in front
   ("L_BtnMini_01-T_Content_00"). Tried in order: exact; key_0; the shortest label that starts with key_ or key-
   (variants such as _Separate / _Single); then the pane name without its trailing number (T_FilterName_00 ->
   T_FilterName_Oil). Returns the index, or -1. *fuzzy is set for the last kind. */
static int MsbtMatch(const Msbt *m, const char *key, int *fuzzy) {
    if (fuzzy) *fuzzy = 0;
    int i = MsbtFind(m, key); if (i >= 0) return i;
    size_t kl = strlen(key); char k0[300];
    if (kl + 3 < sizeof k0) { snprintf(k0, sizeof k0, "%s_0", key); i = MsbtFind(m, k0); if (i >= 0) return i; }
    int best = -1;
    for (int j = 0; j < m->n; j++) {
        const char *l = m->e[j].label; if (!l) continue;
        if (!strncmp(l, key, kl) && (l[kl] == '_' || l[kl] == '-')) {
            if (best < 0 || strlen(l) < strlen(m->e[best].label) || (strlen(l) == strlen(m->e[best].label) && strcmp(l, m->e[best].label) < 0)) best = j;
        }
    }
    if (best >= 0) return best;
    size_t bl = kl; while (bl > 0 && key[bl - 1] >= '0' && key[bl - 1] <= '9') bl--;
    if (bl == kl || bl < 2 || key[bl - 1] != '_') return -1;
    bl--;                                                      // base without "_NN"
    for (int j = 0; j < m->n; j++) {
        const char *l = m->e[j].label; if (!l) continue;
        if (strncmp(l, key, bl) || l[bl] != '_') continue;
        const char *r = l + bl + 1; int digits = *r != 0;
        for (const char *q = r; *q; q++) if (*q < '0' || *q > '9') { digits = 0; break; }
        if (digits) continue;                                   // T_X_01 is another pane, not a variant
        if (best < 0 || strlen(l) < strlen(m->e[best].label) || (strlen(l) == strlen(m->e[best].label) && strcmp(l, m->e[best].label) < 0)) best = j;
    }
    if (best >= 0 && fuzzy) *fuzzy = 1;
    return best;
}

/* ---------------- rendering a message to display text ---------------- */
typedef const Msbt *(*MsgLoadFn)(void *user, const char *file);   // file like "Glossary/PadStyle" (no .msbt)
typedef struct {
    MsgLoadFn load; void *user;
    int (*has)(void *user, unsigned cp);      // optional: the font can draw this character itself (button icons)
    int puaReady; int npua; uint16_t puaCode[160]; char puaName[160][28];
} MsgCtx;
// wantCol: also fill col[] with the text colour of each character (0 = default, k+1 = project.msbp colour k)
typedef struct { uint16_t *s; int n, cap; int literal, numVars; int wantCol, curCol, colCap; unsigned char *col; } MsgOut;

static void MoPut(MsgOut *o, unsigned c) {
    if (o->n + 2 >= o->cap) { int nc = o->cap ? o->cap * 2 : 128; uint16_t *t = (uint16_t *)realloc(o->s, nc * sizeof(uint16_t)); if (!t) return; o->s = t; o->cap = nc; }
    if (o->wantCol && o->n + 2 >= o->colCap) { int nc = o->cap; unsigned char *t = (unsigned char *)realloc(o->col, nc); if (!t) return; o->col = t; o->colCap = nc; }
    if (o->wantCol) o->col[o->n] = (unsigned char)o->curCol;
    o->s[o->n++] = (uint16_t)c; o->s[o->n] = 0;
}

/* ---------------- project.msbp: the message project's colour table ---------------- */
typedef struct { int n; uint32_t rgba[64]; char name[64][40]; } MsbpColors;   // rgba: R in the low byte
static int MsbpParse(MsbpColors *c, const unsigned char *b, size_t n) {
    memset(c, 0, sizeof *c);
    if (n < 0x20 || memcmp(b, "MsgPrjBn", 8) || b[8] != 0xFF || b[9] != 0xFE) return 0;
    size_t pos = 0x20; int nsec = (int)MsgRd16(b + 0x0E);
    for (int k = 0; k < nsec && pos + 16 <= n; k++) {
        uint32_t sz = MsgRd32(b + pos + 4); size_t d = pos + 16;
        if (d + sz > n) break;
        if (!memcmp(b + pos, "CLR1", 4) && sz >= 4) {
            uint32_t cnt = MsgRd32(b + d); if (cnt > 64) cnt = 64; if (4 + cnt * 4 > sz) cnt = (sz - 4) / 4;
            for (uint32_t i = 0; i < cnt; i++) c->rgba[i] = MsgRd32(b + d + 4 + i * 4);
            if ((int)cnt > c->n) c->n = (int)cnt;
        } else if (!memcmp(b + pos, "CLB1", 4) && sz >= 4) {
            uint32_t slots = MsgRd32(b + d);
            for (uint32_t s = 0; s < slots && 4 + s * 8 + 8 <= sz; s++) {
                uint32_t cnt = MsgRd32(b + d + 4 + s * 8), off = MsgRd32(b + d + 8 + s * 8); size_t q = d + off;
                for (uint32_t j = 0; j < cnt && q < d + sz; j++) {
                    unsigned L = b[q]; if (q + 1 + L + 4 > d + sz) break;
                    uint32_t idx = MsgRd32(b + q + 1 + L);
                    if (idx < 64) { snprintf(c->name[idx], 40, "%.*s", (int)L, (const char *)b + q + 1); }
                    q += 1 + L + 4;
                }
            }
        }
        pos = d + ((sz + 15) & ~(size_t)15);
    }
    return c->n > 0;
}
static void MoPutA(MsgOut *o, const char *a) { while (*a) MoPut(o, (unsigned char)*a++); }

// i-th string parameter of a tag (u16 byte length + UTF-16), as ASCII; 0 if absent
static int MsgParam(const unsigned char *p, int pn, int idx, char *out, int outN) {
    int o = 0;
    for (int i = 0; ; i++) {
        if (o + 2 > pn) return 0;
        int l = (int)MsgRd16(p + o);
        if (l & 1 || o + 2 + l > pn) return 0;
        if (i == idx) {
            int k = 0;
            for (int q = 0; q < l / 2 && k < outN - 1; q++) { unsigned c = MsgRd16(p + o + 2 + q * 2); out[k++] = c < 128 ? (char)c : '?'; }
            out[k] = 0; return 1;
        }
        o += 2 + l;
    }
}
static int StartsWith(const char *s, const char *p) { return !strncmp(s, p, strlen(p)); }
// "DualButtonA" -> "A", "HandheldKeyUp" -> arrow, "DualStickL" -> "L stick", "DualGuideOpenMap" -> "OpenMap"
static void MsgButtonName(const char *nm, char *out, int outN) {
    static const char *pre[] = { "Dual", "Handheld", "FullKey", "Left", "Right" };
    for (int i = 0; i < 5; i++) if (StartsWith(nm, pre[i]) && (nm[strlen(pre[i])] == 'B' || nm[strlen(pre[i])] == 'K' || nm[strlen(pre[i])] == 'S' || nm[strlen(pre[i])] == 'G')) { nm += strlen(pre[i]); break; }
    if (StartsWith(nm, "Button")) {
        nm += 6;
        if (!strcmp(nm, "Plus")) nm = "+"; else if (!strcmp(nm, "Minus")) nm = "-";
        snprintf(out, outN, "%s", nm);
    } else if (StartsWith(nm, "Key")) {
        nm += 3;
        snprintf(out, outN, "%s", !strcmp(nm, "Up") ? "\x01" : !strcmp(nm, "Down") ? "\x02" : !strcmp(nm, "Left") ? "\x03" : !strcmp(nm, "Right") ? "\x04" : nm);
    } else if (StartsWith(nm, "StickPush")) snprintf(out, outN, "%s stick press", nm + 9);
    else if (StartsWith(nm, "Stick")) snprintf(out, outN, "%s stick", nm + 5);
    else if (StartsWith(nm, "Guide")) snprintf(out, outN, "%s", nm + 5);
    else snprintf(out, outN, "%s", nm);
}
static void MoPutButton(MsgOut *o, const char *name) {
    MoPut(o, '(');
    for (const char *a = name; *a; a++) {
        if (*a >= 1 && *a <= 4) MoPut(o, 0x2191 + (*a == 1 ? 0 : *a == 2 ? 2 : *a == 3 ? -1 : 1));   // arrows
        else MoPut(o, (unsigned char)*a);
    }
    MoPut(o, ')');
}
static void MsgRenderBytes(MsgCtx *c, const unsigned char *b, int nb, MsgOut *o, int depth);
// the game's picture-font characters (U+E000...) named from Glossary/PadStyle (DualButtonA = [12:0 E0E0] ...)
static void MsgPuaInit(MsgCtx *c) {
    if (c->puaReady) return;
    c->puaReady = 1;
    const Msbt *ps = c->load ? c->load(c->user, "Glossary/PadStyle") : NULL; if (!ps) return;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < ps->n; i++) {
            const MsgEnt *e = &ps->e[i];
            if (!e->label || e->nb < 8 || MsgRd16(e->b) != 0x0E || MsgRd16(e->b + 2) != 12) continue;
            if ((pass == 0) != StartsWith(e->label, "Dual")) continue;
            int pn = (int)MsgRd16(e->b + 6); if (pn < 2 || 8 + pn > e->nb) continue;
            unsigned code = MsgRd16(e->b + 8);
            int have = 0; for (int k = 0; k < c->npua; k++) if (c->puaCode[k] == code) have = 1;
            if (have || c->npua >= 160) continue;
            c->puaCode[c->npua] = (uint16_t)code; MsgButtonName(e->label, c->puaName[c->npua], 28); c->npua++;
        }
}
static void MoPutChar(MsgCtx *c, MsgOut *o, unsigned ch) {
    if (ch >= 0xE000 && ch <= 0xF8FF && !(c->has && c->has(c->user, ch))) {
        MsgPuaInit(c);
        for (int k = 0; k < c->npua; k++) if (c->puaCode[k] == ch) { MoPutButton(o, c->puaName[k]); return; }
    }
    MoPut(o, ch);
}
static int MsgLookup(MsgCtx *c, const char *file, const char *label, MsgOut *o, int depth) {
    const Msbt *g = c->load ? c->load(c->user, file) : NULL; if (!g) return 0;
    int i = MsbtFind(g, label); if (i < 0) return 0;
    MsgRenderBytes(c, g->e[i].b, g->e[i].nb, o, depth + 1);
    return 1;
}
static void MsgRenderBytes(MsgCtx *c, const unsigned char *b, int nb, MsgOut *o, int depth) {
    if (depth > 4) return;
    int p = 0;
    while (p + 2 <= nb) {
        unsigned ch = MsgRd16(b + p);
        if (!ch) break;
        if (ch == 0x0E && p + 8 <= nb) {
            unsigned g = MsgRd16(b + p + 2), pn = MsgRd16(b + p + 6);
            const unsigned char *pr = b + p + 8; if (p + 8 + (int)pn > nb) pn = nb - p - 8;
            char a[96], f[96], l[96];
            unsigned ty = MsgRd16(b + p + 4);
            if (g == 0 && ty == 3 && pn >= 2) { unsigned k = MsgRd16(pr); o->curCol = k == 0xFFFF || k > 62 ? 0 : (int)k + 1; }   // text colour (project.msbp)
            else if (g == 12 && pn >= 2) MoPutChar(c, o, MsgRd16(pr));                  // picture-font character
            else if (g == 13 && MsgParam(pr, pn, 0, a, sizeof a)) {                      // controller button (per pad style)
                if (!MsgLookup(c, "Glossary/PadStyle", a, o, depth)) { MsgButtonName(a, f, sizeof f); MoPutButton(o, f); }
            } else if (g == 15 && MsgParam(pr, pn, 0, a, sizeof a)) {                    // button chosen by a control setting
                char *s = a; if (StartsWith(s, "Button")) s += 6;
                char *q = strstr(s, "Help"); if (!q) q = strstr(s, "Type"); if (q && q > s) *q = 0;
                MoPutButton(o, s);
            } else if (g == 10 || g == 16) {                                             // number / date filled in by the game
                if (g == 10) MoPut(o, '0');
                else if (MsgParam(pr, pn, 0, a, sizeof a)) { MoPut(o, '{'); MoPutA(o, a); MoPut(o, '}'); }
                o->numVars++;
            } else if (g == 11 && pn) {                                                  // a name or glossary word
                int ok = 0;
                if (MsgParam(pr, pn, 1, f, sizeof f) && MsgParam(pr, pn, 2, l, sizeof l) && StartsWith(f, "Glossary/") && strcmp(l, "0"))
                    ok = MsgLookup(c, f, l, o, depth);
                if (!ok && MsgParam(pr, pn, 0, a, sizeof a)) {
                    ok = 1;
                    if (a[0] >= '0' && a[0] <= '9') o->numVars++;                       // an icon picked by number
                    else if (strstr(a, "Icon")) { MoPut(o, '{'); MoPutA(o, a); MoPut(o, '}'); o->numVars++; }
                    else { MoPut(o, '{'); MoPutA(o, a); MoPut(o, '}'); }
                }
                if (!ok && MsgParam(pr, pn, 1, f, sizeof f) && StartsWith(f, "Glossary/")) { MoPut(o, '{'); MoPutA(o, f + 9); MoPut(o, '}'); }
            } else if (g == 201 && MsgParam(pr, pn, 0, a, sizeof a)) MoPutA(o, a);   // singular/plural word
            p += 8 + pn; if (pn & 1) p++;
            continue;
        }
        if (ch == 0x0F) { p += 6; continue; }                                            // tag close
        MoPutChar(c, o, ch); o->literal++;
        p += 2;
    }
}
// renders message i of m; returns 1 if it is worth showing instead of the layout's own text
static int MsgRender(MsgCtx *c, const Msbt *m, int i, MsgOut *o) {
    o->n = 0; o->literal = o->numVars = 0; o->curCol = 0; if (o->s) o->s[0] = 0;
    if (i < 0 || i >= m->n || !m->e[i].b) return 0;
    MsgRenderBytes(c, m->e[i].b, m->e[i].nb, o, 0);
    if (!o->literal && o->numVars) return 0;        // only a number the game fills in: the layout's sample digits look better
    return o->n > 0;
}
// the layout's own text with picture-font characters named
static inline void MsgRenderPlain(MsgCtx *c, const uint16_t *s, int n, MsgOut *o) {
    o->n = 0; if (o->s) o->s[0] = 0;
    for (int i = 0; i < n && s[i]; i++) MoPutChar(c, o, s[i]);
}
#endif
