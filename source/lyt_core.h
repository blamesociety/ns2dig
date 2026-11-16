/* BFLYT (Nintendo UI layout, version 9) and BFLAN (layout animation) reading. Portable C. */
#ifndef LYT_CORE_H
#define LYT_CORE_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>

enum { LP_NULL, LP_PIC, LP_TEXT, LP_WINDOW, LP_PART, LP_BOUNDS, LP_ALIGN, LP_SCISSOR };

typedef struct {
    char name[29];
    unsigned char black[4], white[4];       // RGBA
    int tex;                                // first texture map index (-1 none)
    int wrapS, wrapT;
    float srt[5];                           // first texture SRT: tx, ty, rot, sx, sy
    char shader[33];                        // combiner user shader name (procedural shapes), "" if none
} LytMat;

typedef struct {
    int kind; char name[25];
    int parent, firstChild, next;           // tree (indices into panes, -1 none)
    int visible, influence, alpha, origin;
    float pos[3], rot[3], scale[2], size[2];
    // picture / window / text
    int mat; unsigned char vtx[4][4];       // corner colours TL, TR, BL, BR (RGBA)
    float uv[8];                            // TL u,v TR u,v BL u,v BR u,v
    // text
    wchar_t *text; int font, textPos, align; unsigned char textCol[2][4]; float fontSize[2], charSpace, lineSpace;
    // part
    char part[64]; float magnify[2];
} LytPane;

typedef struct {
    char name[64]; float w, h;
    int ntex; char (*tex)[96];
    int nfont; char (*font)[96];
    int nmat; LytMat *mat;
    int npane; LytPane *pane;
    int root;
} Lyt;

static uint32_t LytRd32(const unsigned char *b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }
static uint16_t LytRd16(const unsigned char *b) { return (uint16_t)(b[0] | (b[1] << 8)); }
static float LytRdF(const unsigned char *b) { float f; memcpy(&f, b, 4); return f; }
static void LytStr(char *dst, size_t dn, const unsigned char *b, size_t o, size_t max, size_t n) {
    size_t i = 0; for (; i + 1 < dn && o + i < n && (max == 0 || i < max) && b[o + i]; i++) dst[i] = (char)b[o + i];
    dst[i] = 0;
}
static void LytFree(Lyt *L) {
    if (!L) return;
    for (int i = 0; i < L->npane; i++) free(L->pane[i].text);
    free(L->pane); free(L->mat); free(L->tex); free(L->font);
    memset(L, 0, sizeof *L);
}
static void LytMaterial(const unsigned char *b, size_t n, size_t o, LytMat *m) {
    memset(m, 0, sizeof *m); m->tex = -1; m->srt[3] = m->srt[4] = 1;
    if (o + 0x2C > n) return;
    LytStr(m->name, sizeof m->name, b, o, 0x1C, n);
    uint32_t f = LytRd32(b + o + 0x1C);
    memcpy(m->black, b + o + 0x24, 4); memcpy(m->white, b + o + 0x28, 4);
    size_t p = o + 0x2C;
    int nt = f & 3, ns = (f >> 2) & 3;
    for (int i = 0; i < nt; i++) { if (p + 4 > n) return; if (!i) { m->tex = LytRd16(b + p); m->wrapS = b[p + 2] & 3; m->wrapT = b[p + 3] & 3; } p += 4; }
    for (int i = 0; i < ns; i++) { if (p + 20 > n) return; if (!i) for (int k = 0; k < 5; k++) m->srt[k] = LytRdF(b + p + k * 4); p += 20; }
    p += ((f >> 4) & 3) * 16 + ((f >> 6) & 7) * 4;
    if (f >> 9 & 1) p += 8;
    if (f >> 10 & 1) p += 4;
    if (f >> 12 & 1) p += 4;
    if (f >> 14 & 1) p += 12;
    p += ((f >> 15) & 3) * 20;
    if (f >> 17 & 1) p += 8;
    if (f >> 20 & 1) LytStr(m->shader, sizeof m->shader, b, p, 0x60, n);
}
static int LytKind(const unsigned char *t) {
    if (!memcmp(t, "pan1", 4)) return LP_NULL; if (!memcmp(t, "pic1", 4)) return LP_PIC; if (!memcmp(t, "txt1", 4)) return LP_TEXT;
    if (!memcmp(t, "wnd1", 4)) return LP_WINDOW; if (!memcmp(t, "prt1", 4)) return LP_PART; if (!memcmp(t, "bnd1", 4)) return LP_BOUNDS;
    if (!memcmp(t, "ali1", 4)) return LP_ALIGN; if (!memcmp(t, "scr1", 4)) return LP_SCISSOR;
    return -1;
}
static int LytParse(Lyt *L, const unsigned char *b, size_t n) {
    memset(L, 0, sizeof *L); L->root = -1;
    if (n < 0x14 || memcmp(b, "FLYT", 4)) return 0;
    size_t o = LytRd16(b + 6); int ns = LytRd16(b + 0x10);
    // count panes first
    int cap = 0; { size_t q = o; for (int i = 0; i < ns && q + 8 <= n; i++) { if (LytKind(b + q) >= 0) cap++; uint32_t sz = LytRd32(b + q + 4); if (sz < 8) break; q += sz; } }
    L->pane = (LytPane *)calloc(cap + 1, sizeof(LytPane)); if (!L->pane) return 0;
    int stack[64], sp = 0, last = -1, lastChild[4096];
    for (int i = 0; i < 4096; i++) lastChild[i] = -1;
    for (int i = 0; i < ns && o + 8 <= n; i++) {
        uint32_t sz = LytRd32(b + o + 4); if (sz < 8 || o + sz > n) break;
        const unsigned char *s = b + o; int k = LytKind(s);
        if (!memcmp(s, "lyt1", 4) && sz >= 0x1C) { L->w = LytRdF(s + 0xC); L->h = LytRdF(s + 0x10); LytStr(L->name, sizeof L->name, b, o + 0x1C, 0, o + sz); }
        else if (!memcmp(s, "txl1", 4)) {
            int c = LytRd16(s + 8); L->tex = (char (*)[96])calloc(c + 1, 96); L->ntex = c;
            for (int t = 0; t < c && 0xC + t * 4 + 4 <= sz; t++) LytStr(L->tex[t], 96, b, o + 0xC + LytRd32(s + 0xC + t * 4), 0, o + sz);
        } else if (!memcmp(s, "fnl1", 4)) {
            int c = LytRd16(s + 8); L->font = (char (*)[96])calloc(c + 1, 96); L->nfont = c;
            for (int t = 0; t < c && 0xC + t * 4 + 4 <= sz; t++) LytStr(L->font[t], 96, b, o + 0xC + LytRd32(s + 0xC + t * 4), 0, o + sz);
        } else if (!memcmp(s, "mat1", 4)) {
            int c = LytRd16(s + 8); L->mat = (LytMat *)calloc(c + 1, sizeof(LytMat)); L->nmat = c;
            for (int t = 0; t < c && 0xC + t * 4 + 4 <= sz; t++) LytMaterial(b, o + sz, o + LytRd32(s + 0xC + t * 4), &L->mat[t]);
        } else if (k >= 0 && sz >= 0x54 && L->npane < cap) {
            int id = L->npane++; LytPane *p = &L->pane[id];
            p->kind = k; p->firstChild = p->next = -1; p->mat = -1;
            p->visible = s[8] & 1; p->influence = (s[8] >> 1) & 1; p->origin = s[9]; p->alpha = s[10];
            LytStr(p->name, sizeof p->name, b, o + 0x0C, 0x18, o + sz);
            for (int q = 0; q < 3; q++) { p->pos[q] = LytRdF(s + 0x2C + q * 4); p->rot[q] = LytRdF(s + 0x38 + q * 4); }
            for (int q = 0; q < 2; q++) { p->scale[q] = LytRdF(s + 0x44 + q * 4); p->size[q] = LytRdF(s + 0x4C + q * 4); }
            memset(p->vtx, 255, sizeof p->vtx);
            float defuv[8] = { 0, 0, 1, 0, 0, 1, 1, 1 }; memcpy(p->uv, defuv, sizeof defuv);
            if (k == LP_PIC && sz >= 0x68) {
                for (int c = 0; c < 4; c++) memcpy(p->vtx[c], s + 0x54 + c * 4, 4);
                p->mat = LytRd16(s + 0x64);
                if (s[0x66] && sz >= 0x88) for (int q = 0; q < 8; q++) p->uv[q] = LytRdF(s + 0x68 + q * 4);
            } else if (k == LP_TEXT && sz >= 0x78) {
                int slen = LytRd16(s + 0x56); p->mat = LytRd16(s + 0x58); p->font = LytRd16(s + 0x5A);
                p->textPos = s[0x5C]; p->align = s[0x5D];
                uint32_t toff = LytRd32(s + 0x64);
                memcpy(p->textCol[0], s + 0x68, 4); memcpy(p->textCol[1], s + 0x6C, 4);
                p->fontSize[0] = LytRdF(s + 0x70); p->fontSize[1] = LytRdF(s + 0x74);
                p->charSpace = LytRdF(s + 0x78); p->lineSpace = LytRdF(s + 0x7C);
                if (toff && slen && toff + slen <= sz) {
                    p->text = (wchar_t *)calloc(slen / 2 + 1, sizeof(wchar_t));
                    if (p->text) for (int q = 0; q < slen / 2; q++) p->text[q] = (wchar_t)LytRd16(s + toff + q * 2);
                }
            } else if (k == LP_WINDOW && sz >= 0x84) {
                p->mat = LytRd16(s + 0x80);
                for (int c = 0; c < 4; c++) memcpy(p->vtx[c], s + 0x70 + c * 4, 4);
            } else if (k == LP_PART && sz >= 0x60) {
                uint32_t np = LytRd32(s + 0x54); p->magnify[0] = LytRdF(s + 0x58); p->magnify[1] = LytRdF(s + 0x5C);
                if (0x60 + (size_t)np * 0x28 < sz) LytStr(p->part, sizeof p->part, b, o + 0x60 + np * 0x28, 0, o + sz);
            }
            // tree
            p->parent = sp ? stack[sp - 1] : -1;
            if (p->parent < 0) { if (L->root < 0) L->root = id; }
            else if (p->parent < 4096) {
                if (lastChild[p->parent] < 0) L->pane[p->parent].firstChild = id; else L->pane[lastChild[p->parent]].next = id;
                lastChild[p->parent] = id;
            }
            last = id;
        } else if (!memcmp(s, "pas1", 4)) { if (last >= 0 && sp < 64) stack[sp++] = last; }
        else if (!memcmp(s, "pae1", 4)) { if (sp) sp--; }
        o += sz;
    }
    return L->root >= 0;
}
static int LytFindPane(const Lyt *L, const char *name) { for (int i = 0; i < L->npane; i++) if (!strcmp(L->pane[i].name, name)) return i; return -1; }
static int LytFindMat(const Lyt *L, const char *name) { for (int i = 0; i < L->nmat; i++) if (!strcmp(L->mat[i].name, name)) return i; return -1; }

/* ---- animation (BFLAN): curves to evaluate per frame ---- */
typedef struct { float f, v, s; } LanKey;
typedef struct {
    char target[29];          // pane or material name
    int isMat;                // content type 1 = material
    char tag[5];              // FLPA, FLVI, FLVC, FLMC, FLTS, FLTP, ...
    int index, channel, step;
    int nkey; LanKey *key;
} LanCurve;
typedef struct { char name[64]; int frames, loop; int ntex; char (*tex)[96]; int ncurve; LanCurve *curve; } Lan;
static void LanFree(Lan *A) { if (!A) return; for (int i = 0; i < A->ncurve; i++) free(A->curve[i].key); free(A->curve); free(A->tex); memset(A, 0, sizeof *A); }
static int LanParse(Lan *A, const unsigned char *b, size_t n) {
    memset(A, 0, sizeof *A);
    if (n < 0x14 || memcmp(b, "FLAN", 4)) return 0;
    size_t o = LytRd16(b + 6); int ns = LytRd16(b + 0x10);
    for (int i = 0; i < ns && o + 8 <= n; i++) {
        uint32_t sz = LytRd32(b + o + 4); if (sz < 8 || o + sz > n) break;
        const unsigned char *s = b + o;
        if (!memcmp(s, "pat1", 4) && sz > 0x18) LytStr(A->name, sizeof A->name, b, o + LytRd32(s + 0xC), 0, o + sz);
        else if (!memcmp(s, "pai1", 4) && sz >= 0x14) {
            A->frames = LytRd16(s + 8); A->loop = s[0xA];
            int ntex = LytRd16(s + 0xC), nc = LytRd16(s + 0xE); uint32_t coff = LytRd32(s + 0x10);
            if (ntex) {
                A->tex = (char (*)[96])calloc(ntex, 96); A->ntex = ntex;
                for (int t = 0; t < ntex; t++) LytStr(A->tex[t], 96, b, o + 0x14 + LytRd32(s + 0x14 + t * 4), 0, o + sz);
            }
            // count curves
            int cap = 0;
            for (int c = 0; c < nc; c++) {
                size_t co = o + LytRd32(s + coff + c * 4); if (co + 0x24 > o + sz) continue;
                int ninfo = b[co + 0x1C], ctype = b[co + 0x1D]; size_t p = co + 0x20;
                if (ctype == 2) p = co + LytRd32(b + co + 0x20);
                for (int k = 0; k < ninfo; k++) { size_t io = co + LytRd32(b + p + k * 4); if (io + 8 <= o + sz) cap += b[io + 4]; }
            }
            A->curve = (LanCurve *)calloc(cap + 1, sizeof(LanCurve));
            for (int c = 0; c < nc && A->curve; c++) {
                size_t co = o + LytRd32(s + coff + c * 4); if (co + 0x24 > o + sz) continue;
                int ninfo = b[co + 0x1C], ctype = b[co + 0x1D]; size_t p = co + 0x20;
                if (ctype == 2) p = co + LytRd32(b + co + 0x20);
                for (int k = 0; k < ninfo; k++) {
                    size_t io = co + LytRd32(b + p + k * 4); if (io + 8 > o + sz) continue;
                    int nt = b[io + 4];
                    for (int t = 0; t < nt && A->ncurve < cap; t++) {
                        size_t to = io + LytRd32(b + io + 8 + t * 4); if (to + 12 > o + sz) continue;
                        LanCurve *cv = &A->curve[A->ncurve];
                        LytStr(cv->target, sizeof cv->target, b, co, 0x1C, o + sz); cv->isMat = ctype == 1;
                        memcpy(cv->tag, b + io, 4); cv->tag[4] = 0;
                        cv->index = b[to]; cv->channel = b[to + 1]; cv->step = b[to + 2] == 1;
                        int nk = LytRd16(b + to + 4); size_t ko = to + LytRd32(b + to + 8);
                        cv->key = (LanKey *)calloc(nk + 1, sizeof(LanKey));
                        for (int q = 0; q < nk && cv->key; q++) {
                            if (cv->step) { if (ko + q * 8 + 6 > o + sz) break; cv->key[q].f = LytRdF(b + ko + q * 8); cv->key[q].v = LytRd16(b + ko + q * 8 + 4); }
                            else { if (ko + q * 12 + 12 > o + sz) break; cv->key[q].f = LytRdF(b + ko + q * 12); cv->key[q].v = LytRdF(b + ko + q * 12 + 4); cv->key[q].s = LytRdF(b + ko + q * 12 + 8); }
                            cv->nkey = q + 1;
                        }
                        A->ncurve++;
                    }
                }
            }
        }
        o += sz;
    }
    return 1;
}
// value of a curve at a frame (hermite between keys, held outside)
static float LanEval(const LanCurve *c, float fr) {
    if (!c->nkey) return 0;
    if (fr <= c->key[0].f) return c->key[0].v;
    if (fr >= c->key[c->nkey - 1].f) return c->key[c->nkey - 1].v;
    for (int i = 0; i + 1 < c->nkey; i++) {
        const LanKey *a = &c->key[i], *b = &c->key[i + 1];
        if (fr < a->f || fr > b->f) continue;
        if (c->step) return a->v;
        float d = b->f - a->f; if (d <= 0) return b->v;
        float t = (fr - a->f) / d, t2 = t * t, t3 = t2 * t;
        return (2 * t3 - 3 * t2 + 1) * a->v + (t3 - 2 * t2 + t) * a->s * d + (-2 * t3 + 3 * t2) * b->v + (t3 - t2) * b->s * d;
    }
    return c->key[c->nkey - 1].v;
}
#endif
