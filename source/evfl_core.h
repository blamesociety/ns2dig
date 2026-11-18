/* BFEVFL event flows (versions 0.3 and 4.0): flowcharts (actors, events, entry points) and timelines. Portable C.
   Layout as in alsarc_dump.py's read_bfevfl. */
#ifndef EVFL_CORE_H
#define EVFL_CORE_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

enum { EV_ACTION = 0, EV_SWITCH, EV_FORK, EV_JOIN, EV_SUBFLOW, EV_SUBSWITCH, EV_RETURN };   // 5, 6: version 4 only
typedef struct { char key[64]; int type; char *str; double num; } EvParam;   // flattened: nested keys joined with '.'
typedef struct { EvParam *p; int n, cap; } EvParams;
typedef struct { char name[64]; char **acts; int nact; char **qs; int nq; } EvActor;
typedef struct {
    char name[64]; int type;
    int next, actor, item;            // action / query index into the actor's lists
    int ncase; int *caseVal, *caseEv;
    int nbr; int *br; int join;
    char sfFlow[128], sfEntry[128];
    uint32_t ret;                     // EV_RETURN: the value returned (a hash, see EvValName)
    EvParams prm;
} EvEvent;
typedef struct { char name[96]; int main; } EvEntry;
typedef struct { char name[128]; EvActor *actors; int nactors; EvEvent *ev; int nev; EvEntry *ent; int nent; } EvFlow;
typedef struct { float start, dur; int actor, action; EvParams prm; } EvClip;
typedef struct { char name[128]; float dur; EvActor *actors; int nactors; EvClip *clips; int nclips; EvClip *os; int nos; } EvTl;
typedef struct { char name[128]; EvFlow *fc; int nfc; EvTl *tl; int ntl; char err[200]; } EvFile;

static const unsigned char *EvB; static size_t EvN; static size_t EvEvSize = 0x28;   // 0x30 in version 4
static unsigned EvU8(size_t o) { return o < EvN ? EvB[o] : 0; }
static unsigned EvU16(size_t o) { return o + 2 <= EvN ? (unsigned)(EvB[o] | (EvB[o + 1] << 8)) : 0; }
static uint32_t EvU32(size_t o) { return o + 4 <= EvN ? (uint32_t)(EvB[o] | (EvB[o + 1] << 8) | (EvB[o + 2] << 16) | ((uint32_t)EvB[o + 3] << 24)) : 0; }
static uint64_t EvU64(size_t o) { return o + 8 <= EvN ? (uint64_t)EvU32(o) | ((uint64_t)EvU32(o + 4) << 32) : 0; }
static float EvF32(size_t o) { uint32_t x = EvU32(o); float f; memcpy(&f, &x, 4); return f; }
// version 4 sub-flows return a value as the FNV-1a hash of its text: "1" / "0" for yes / no, other numbers, true / false
static uint32_t EvFnv(const char *t) { uint32_t h = 0x811C9DC5u; while (*t) { h ^= (unsigned char)*t++; h *= 0x01000193u; } return h; }
static void EvValName(uint32_t h, char *out, size_t n) {
    static const char *w[] = { "true", "false", "True", "False", "None" };
    char t[16];
    for (int i = -16; i < 256; i++) { snprintf(t, sizeof t, "%d", i); if (EvFnv(t) == h) { snprintf(out, n, "%s", t); return; } }
    for (int i = 0; i < 5; i++) if (EvFnv(w[i]) == h) { snprintf(out, n, "%s", w[i]); return; }
    snprintf(out, n, "0x%08X", (unsigned)h);
}
// a length-prefixed string
static void EvStr(uint64_t o, char *out, size_t n) {
    out[0] = 0; if (!o || o + 2 > EvN) return;
    size_t l = EvU16((size_t)o); if (o + 2 + l > EvN) return; if (l >= n) l = n - 1;
    memcpy(out, EvB + o + 2, l); out[l] = 0;
}
static char *EvStrDup(uint64_t o) { char t[512]; EvStr(o, t, sizeof t); char *r = (char *)malloc(strlen(t) + 1); if (r) strcpy(r, t); return r; }
// the names of a DIC radix tree, in entry order
static int EvDic(uint64_t o, char names[][96], int max) {
    if (!o || o + 8 > EvN || memcmp(EvB + o, "DIC ", 4)) return 0;
    int n = (int)EvU32((size_t)o + 4); if (n > max) n = max;
    for (int i = 1; i <= n; i++) EvStr(EvU64((size_t)o + 8 + 16 * (size_t)i + 8), names[i - 1], 96);
    return n;
}
static void EvAddP(EvParams *ps, const char *key, int type, const char *str, double num) {
    if (ps->n == ps->cap) { int c = ps->cap ? ps->cap * 2 : 8; EvParam *q = (EvParam *)realloc(ps->p, c * sizeof(EvParam)); if (!q) return; ps->p = q; ps->cap = c; }
    EvParam *p = &ps->p[ps->n++]; memset(p, 0, sizeof *p);
    snprintf(p->key, sizeof p->key, "%s", key); p->type = type; p->num = num;
    if (str) { p->str = (char *)malloc(strlen(str) + 1); if (p->str) strcpy(p->str, str); }
}
// parameters: type 1 = container (named children); 2 int, 3 bool, 4 float, 5/6 string (+ arrays 7..11), 0 argument, 12 actor
static void EvContainer(uint64_t o, const char *prefix, EvParams *ps, int depth) {
    if (!o || o + 16 > EvN || depth > 8) return;
    int t = (int)EvU8((size_t)o), n = (int)EvU16((size_t)o + 2); uint64_t d = EvU64((size_t)o + 8); size_t q = (size_t)o + 16;
    if (n > 256) n = 256;
    if (t == 1) {
        static char names[256][96]; char nm[96];
        int nn = EvDic(d, names, 256);
        for (int i = 0; i < n; i++) {
            char key[160];
            snprintf(nm, 96, "%s", i < nn ? names[i] : ""); if (!nm[0]) snprintf(nm, 96, "%d", i);
            if (prefix[0]) snprintf(key, sizeof key, "%s.%s", prefix, nm); else snprintf(key, sizeof key, "%s", nm);
            EvContainer(EvU64(q + 8 * (size_t)i), key, ps, depth + 1);
            nn = EvDic(d, names, 256);     // (names is reused by nested containers)
        }
        return;
    }
    char tmp[512];
    if (t == 0) { EvStr(EvU64(q), tmp, sizeof tmp); EvAddP(ps, prefix, 5, tmp, 0); return; }
    if (t == 12) { char a[256], b2[256]; EvStr(EvU64(q), a, sizeof a); EvStr(EvU64(q + 8), b2, sizeof b2); snprintf(tmp, sizeof tmp, "%s%s%s", a, b2[0] ? "/" : "", b2); EvAddP(ps, prefix, 5, tmp, 0); return; }
    for (int i = 0; i < n; i++) {
        char key[160];
        if (n > 1 || t >= 7) snprintf(key, sizeof key, "%s[%d]", prefix, i); else snprintf(key, sizeof key, "%s", prefix);
        if (t == 2 || t == 7) EvAddP(ps, key, 2, NULL, (double)(int32_t)EvU32(q + 4 * (size_t)i));
        else if (t == 3 || t == 8) EvAddP(ps, key, 3, NULL, EvU32(q + 4 * (size_t)i) ? 1 : 0);
        else if (t == 4 || t == 9) EvAddP(ps, key, 4, NULL, EvF32(q + 4 * (size_t)i));
        else if (t == 5 || t == 6 || t == 10 || t == 11) { EvStr(EvU64(q + 8 * (size_t)i), tmp, sizeof tmp); EvAddP(ps, key, 5, tmp, 0); }
    }
}
static void EvFreeParams(EvParams *ps) { for (int i = 0; i < ps->n; i++) free(ps->p[i].str); free(ps->p); memset(ps, 0, sizeof *ps); }
static const char *EvParamStr(const EvParams *ps, const char *key) {
    for (int i = 0; i < ps->n; i++) if (ps->p[i].type == 5 && !strcmp(ps->p[i].key, key)) return ps->p[i].str;
    return NULL;
}
static void EvActorRead(size_t o, EvActor *a) {
    memset(a, 0, sizeof *a);
    EvStr(EvU64(o), a->name, sizeof a->name);
    int na = (int)EvU16(o + 0x30), nq = (int)EvU16(o + 0x32);
    a->acts = (char **)calloc(na + 1, sizeof(char *)); a->qs = (char **)calloc(nq + 1, sizeof(char *));
    for (int k = 0; k < na && a->acts; k++) a->acts[a->nact++] = EvStrDup(EvU64((size_t)EvU64(o + 24) + 8 * (size_t)k));
    for (int k = 0; k < nq && a->qs; k++) a->qs[a->nq++] = EvStrDup(EvU64((size_t)EvU64(o + 32) + 8 * (size_t)k));
}
static void EvFreeActors(EvActor *a, int n) {
    for (int i = 0; i < n; i++) { for (int k = 0; k < a[i].nact; k++) free(a[i].acts[k]); for (int k = 0; k < a[i].nq; k++) free(a[i].qs[k]); free(a[i].acts); free(a[i].qs); }
    free(a);
}
static int EvFlowRead(size_t o, EvFlow *f) {
    memset(f, 0, sizeof *f);
    if (o + 0x48 > EvN || memcmp(EvB + o, "EVFL", 4)) return 0;
    int nact = (int)EvU16(o + 0x10), nev = (int)EvU16(o + 0x16), nent = (int)EvU16(o + 0x18);
    EvStr(EvU64(o + 0x20), f->name, sizeof f->name);
    f->actors = (EvActor *)calloc(nact + 1, sizeof(EvActor)); f->ev = (EvEvent *)calloc(nev + 1, sizeof(EvEvent)); f->ent = (EvEntry *)calloc(nent + 1, sizeof(EvEntry));
    if (!f->actors || !f->ev || !f->ent) return 0;
    for (int i = 0; i < nact; i++) EvActorRead((size_t)EvU64(o + 0x28) + 0x38 * (size_t)i, &f->actors[f->nactors++]);
    for (int i = 0; i < nev; i++) {
        size_t e = (size_t)EvU64(o + 0x30) + EvEvSize * (size_t)i; EvEvent *v = &f->ev[f->nev++];
        EvStr(EvU64(e), v->name, sizeof v->name);
        v->type = (int)EvU8(e + 8); v->next = -1; v->join = -1;
        unsigned a = EvU16(e + 0xA), b = EvU16(e + 0xC), c = EvU16(e + 0xE);
        if (v->type == EV_ACTION) { v->next = a == 0xFFFF ? -1 : (int)a; v->actor = (int)b; v->item = (int)c; EvContainer(EvU64(e + 0x10), "", &v->prm, 0); }
        else if (v->type == EV_SWITCH) {
            v->actor = (int)b; v->item = (int)c; EvContainer(EvU64(e + 0x10), "", &v->prm, 0);
            v->ncase = (int)a; if (v->ncase > 64) v->ncase = 64;
            v->caseVal = (int *)calloc(v->ncase + 1, sizeof(int)); v->caseEv = (int *)calloc(v->ncase + 1, sizeof(int));
            size_t cs = (size_t)EvU64(e + 0x18);
            for (int j = 0; j < v->ncase && v->caseVal && v->caseEv; j++) { v->caseVal[j] = (int)EvU32(cs + 8 * (size_t)j); v->caseEv[j] = (int)EvU16(cs + 8 * (size_t)j + 4); }
        } else if (v->type == EV_FORK) {
            v->nbr = (int)a; if (v->nbr > 32) v->nbr = 32; v->join = (int)b;
            v->br = (int *)calloc(v->nbr + 1, sizeof(int)); size_t fp = (size_t)EvU64(e + 0x10);
            for (int j = 0; j < v->nbr && v->br; j++) v->br[j] = (int)EvU16(fp + 2 * (size_t)j);
        } else if (v->type == EV_JOIN) v->next = a == 0xFFFF ? -1 : (int)a;
        else if (v->type == EV_SUBFLOW) {
            v->next = a == 0xFFFF ? -1 : (int)a; EvContainer(EvU64(e + 0x10), "", &v->prm, 0);
            EvStr(EvU64(e + 0x18), v->sfFlow, sizeof v->sfFlow); EvStr(EvU64(e + 0x20), v->sfEntry, sizeof v->sfEntry);
        } else if (v->type == EV_SUBSWITCH) {     // run a sub-flow, then branch on the value it returns
            EvContainer(EvU64(e + 0x10), "", &v->prm, 0);
            EvStr(EvU64(e + 0x20), v->sfFlow, sizeof v->sfFlow); EvStr(EvU64(e + 0x28), v->sfEntry, sizeof v->sfEntry);
            v->ncase = (int)a; if (v->ncase > 64) v->ncase = 64;
            v->caseVal = (int *)calloc(v->ncase + 1, sizeof(int)); v->caseEv = (int *)calloc(v->ncase + 1, sizeof(int));
            size_t cs = (size_t)EvU64(e + 0x18);
            for (int j = 0; j < v->ncase && v->caseVal && v->caseEv; j++) { v->caseVal[j] = (int)EvU32(cs + 8 * (size_t)j); v->caseEv[j] = (int)EvU16(cs + 8 * (size_t)j + 4); }
        } else if (v->type == EV_RETURN) v->ret = EvU32(e + 0xC);
    }
    static char names[512][96]; int nn = EvDic(EvU64(o + 0x38), names, 512);
    for (int i = 0; i < nent; i++) {
        size_t p = (size_t)EvU64(o + 0x40) + 0x20 * (size_t)i; EvEntry *en = &f->ent[f->nent++];
        if (i < nn) snprintf(en->name, sizeof en->name, "%s", names[i]); else snprintf(en->name, sizeof en->name, "%d", i);
        unsigned m = EvU16(p + 0x1C); en->main = m == 0xFFFF ? -1 : (int)m;
    }
    return 1;
}
static void EvClipRead(size_t c, EvClip *cl, int oneshot) {
    memset(cl, 0, sizeof *cl);
    if (oneshot) { cl->start = EvF32(c); cl->actor = (int)EvU16(c + 4); cl->action = (int)EvU16(c + 6); }
    else { cl->start = EvF32(c); cl->dur = EvF32(c + 4); cl->actor = (int)EvU16(c + 8); cl->action = (int)EvU16(c + 10); }
    EvContainer(EvU64(c + 0x10), "", &cl->prm, 0);
}
static int EvTlRead(size_t o, EvTl *t) {
    memset(t, 0, sizeof *t);
    if (o + 0x60 > EvN || memcmp(EvB + o, "TLIN", 4)) return 0;
    t->dur = EvF32(o + 0x10);
    int nact = (int)EvU16(o + 0x14), ncl = (int)EvU16(o + 0x18), nos = (int)EvU16(o + 0x1A);
    EvStr(EvU64(o + 0x20), t->name, sizeof t->name);
    t->actors = (EvActor *)calloc(nact + 1, sizeof(EvActor)); t->clips = (EvClip *)calloc(ncl + 1, sizeof(EvClip)); t->os = (EvClip *)calloc(nos + 1, sizeof(EvClip));
    if (!t->actors || !t->clips || !t->os) return 0;
    for (int i = 0; i < nact; i++) EvActorRead((size_t)EvU64(o + 0x28) + 0x38 * (size_t)i, &t->actors[t->nactors++]);
    for (int i = 0; i < ncl; i++) EvClipRead((size_t)EvU64(o + 0x30) + 0x18 * (size_t)i, &t->clips[t->nclips++], 0);
    for (int i = 0; i < nos; i++) EvClipRead((size_t)EvU64(o + 0x38) + 0x18 * (size_t)i, &t->os[t->nos++], 1);
    return 1;
}
static void EvFree(EvFile *f) {
    for (int i = 0; i < f->nfc; i++) {
        EvFlow *c = &f->fc[i];
        for (int k = 0; k < c->nev; k++) { EvFreeParams(&c->ev[k].prm); free(c->ev[k].caseVal); free(c->ev[k].caseEv); free(c->ev[k].br); }
        EvFreeActors(c->actors, c->nactors); free(c->ev); free(c->ent);
    }
    for (int i = 0; i < f->ntl; i++) {
        EvTl *t = &f->tl[i];
        for (int k = 0; k < t->nclips; k++) EvFreeParams(&t->clips[k].prm);
        for (int k = 0; k < t->nos; k++) EvFreeParams(&t->os[k].prm);
        EvFreeActors(t->actors, t->nactors); free(t->clips); free(t->os);
    }
    free(f->fc); free(f->tl); memset(f, 0, sizeof *f);
}
static int EvParse(const unsigned char *b, size_t n, EvFile *f) {
    memset(f, 0, sizeof *f); EvB = b; EvN = n;
    if (n < 0x48 || memcmp(b, "BFEVFL\0\0", 8)) { snprintf(f->err, sizeof f->err, "Not a BFEVFL event flow."); return 0; }
    size_t no = EvU32(0x10); size_t i = 0; while (no + i < n && b[no + i] && i < sizeof f->name - 1) { f->name[i] = (char)b[no + i]; i++; }
    int nfc = (int)EvU16(0x20), ntl = (int)EvU16(0x22);
    EvEvSize = b[9] >= 4 ? 0x30 : 0x28;
    f->fc = (EvFlow *)calloc(nfc + 1, sizeof(EvFlow)); f->tl = (EvTl *)calloc(ntl + 1, sizeof(EvTl));
    if (!f->fc || !f->tl) return 0;
    for (int k = 0; k < nfc; k++) if (EvFlowRead((size_t)EvU64((size_t)EvU64(0x28) + 8 * (size_t)k), &f->fc[f->nfc])) f->nfc++;
    for (int k = 0; k < ntl; k++) if (EvTlRead((size_t)EvU64((size_t)EvU64(0x38) + 8 * (size_t)k), &f->tl[f->ntl])) f->ntl++;
    if (!f->nfc && !f->ntl) { snprintf(f->err, sizeof f->err, "No flowcharts or timelines could be read."); return 0; }
    return 1;
}
#endif
