/* Event viewer: one event script (romfs\Event\EventFlow\X.bfevfl.zs) - a conversation, shop, cutscene or the
   credits - shown as readable steps with the real dialogue from the game's message archives (Mals), in any
   language the dump has. Entry points (Talk, Wait...) and timelines are listed on the left. */
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
void UiSkinWindow(HWND top);
#include <commctrl.h>
#include <richedit.h>
#include <stdio.h>
#include <stdint.h>
#include <wchar.h>
#include "arc_util.h"
#include "evfl_core.h"
#include "msg_core.h"

#define EV_CLASS L"AlsarcEventView"
enum { EVV_LIST = 100, EVV_TEXT, EVV_LANG, EVV_LANGLBL, EVV_DLG, EVV_PARAMS, EVV_INFO };

typedef struct { char code[8]; int ver; wchar_t path[1024]; } EvLang;
typedef struct { char name[96]; Msbt m; } EvMsgFile;
typedef struct { int fc, ent, tl; } EvItem;          // list entry: an entry point of flowchart fc, or timeline tl
typedef struct {
    HWND wnd, list, text, lang, langLbl, dlg, params, info;
    HFONT font; int dpi;
    wchar_t path[2048], title[600];
    EvFile f; char flowBase[128];
    EvItem items[512]; int nitems, cur;
    EvLang *langs; int nlangs, langSel;
    EvMsgFile *mf; int nmf; MsgCtx mctx; MsbpColors pal;
    unsigned char *seen; int nseen;
    int lines;
} EV;
static char g_evLang[8] = "USen";

static int EvS(EV *v, int x) { return MulDiv(x, v->dpi, 96); }

/* ---------------- languages (same discovery as the layout viewer) ---------------- */
static const char *EvLangName(const char *c) {
    static const char *t[][2] = { { "USen", "English (US)" }, { "EUen", "English (UK)" }, { "JPja", "Japanese" }, { "EUfr", "French" },
        { "USfr", "French (Canada)" }, { "EUde", "German" }, { "EUes", "Spanish (Spain)" }, { "USes", "Spanish (Latin America)" },
        { "EUit", "Italian" }, { "EUnl", "Dutch" }, { "EUru", "Russian" }, { "KRko", "Korean" }, { "CNzh", "Chinese (Simplified)" },
        { "TWzh", "Chinese (Traditional)" }, { "EUpt", "Portuguese" }, { "USpt", "Portuguese (Brazil)" }, { "EUpl", "Polish" }, { "THth", "Thai" } };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) if (!strcmp(t[i][0], c)) return t[i][1];
    return c;
}
static void EvAddLangDir(EV *v, const wchar_t *dir) {
    wchar_t pat[1100]; swprintf(pat, 1100, L"%ls\\*.Product.*.sarc.zs", dir);
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd); if (h == INVALID_HANDLE_VALUE) return;
    do {
        const wchar_t *n = fd.cFileName, *d = wcschr(n, L'.'), *pv = wcsstr(n, L".Product.");
        if (!d || !pv || d != pv || d - n < 2 || d - n > 7 || (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        char code[8]; int cl = WideCharToMultiByte(CP_UTF8, 0, n, (int)(d - n), code, 7, NULL, NULL); code[cl > 0 ? cl : 0] = 0;
        int ver = _wtoi(pv + 9), k = -1;
        for (int i = 0; i < v->nlangs; i++) if (!strcmp(v->langs[i].code, code)) k = i;
        if (k >= 0 && v->langs[k].ver >= ver) continue;
        if (k < 0) { EvLang *nl = (EvLang *)realloc(v->langs, (v->nlangs + 1) * sizeof(EvLang)); if (!nl) break; v->langs = nl; k = v->nlangs++; }
        snprintf(v->langs[k].code, 8, "%s", code); v->langs[k].ver = ver; swprintf(v->langs[k].path, 1024, L"%ls\\%ls", dir, n);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static int EvCmpLang(const void *a, const void *b) { return strcmp(EvLangName(((const EvLang *)a)->code), EvLangName(((const EvLang *)b)->code)); }
static void EvFindLangs(EV *v) {
    size_t n = wcslen(v->path), at = (size_t)-1;
    for (size_t i = 0; i + 7 <= n; i++) if (!_wcsnicmp(v->path + i, L"\\romfs\\", 7)) { at = i; break; }
    if (at == (size_t)-1) return;
    wchar_t d[2200], own[2048];
    swprintf(d, 2200, L"%.*ls\\Mals", (int)(at + 6), v->path); EvAddLangDir(v, d);
    swprintf(own, 2048, L"%.*ls", (int)at, v->path);
    wchar_t *s = wcsrchr(own, L'\\');
    if (s) {
        int ok = 0;
        if (!_wcsicmp(s + 1, L"base")) { *s = 0; ok = 1; }
        else { *s = 0; wchar_t *s2 = wcsrchr(own, L'\\'); if (s2 && !_wcsicmp(s2 + 1, L"updates")) { *s2 = 0; ok = 1; } }
        if (ok) {
            swprintf(d, 2200, L"%ls\\base\\romfs\\Mals", own); EvAddLangDir(v, d);
            wchar_t pat[2200]; swprintf(pat, 2200, L"%ls\\updates\\*", own);
            WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.') {
                    swprintf(d, 2200, L"%ls\\updates\\%ls\\romfs\\Mals", own, fd.cFileName); EvAddLangDir(v, d);
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        }
    }
    if (v->nlangs > 1) qsort(v->langs, v->nlangs, sizeof(EvLang), EvCmpLang);
}

/* ---------------- messages ---------------- */
static const Msbt *EvMsgLoad(void *user, const char *file) {
    EV *v = (EV *)user;
    for (int i = 0; i < v->nmf; i++) if (!strcmp(v->mf[i].name, file)) return &v->mf[i].m;
    return NULL;
}
static int EvMsgEach(const char *name, const unsigned char *d, size_t n, void *u) {
    EV *v = (EV *)u; size_t l = strlen(name);
    if (l >= 5 && !strcmp(name + l - 5, ".msbp")) { MsbpParse(&v->pal, d, n); return 0; }   // text colours
    if (l < 6 || strcmp(name + l - 5, ".msbt")) return 0;
    EvMsgFile *q = (EvMsgFile *)realloc(v->mf, (v->nmf + 1) * sizeof(EvMsgFile)); if (!q) return 1;
    v->mf = q; EvMsgFile *f = &v->mf[v->nmf]; memset(f, 0, sizeof *f);
    snprintf(f->name, sizeof f->name, "%.*s", (int)(l - 5), name);
    if (MsbtParse(&f->m, d, n)) v->nmf++; else MsbtFree(&f->m);
    return 0;
}
static void EvFreeMsgs(EV *v) { for (int i = 0; i < v->nmf; i++) MsbtFree(&v->mf[i].m); free(v->mf); v->mf = NULL; v->nmf = 0; }
static void EvSetLang(EV *v, int sel) {
    EvFreeMsgs(v); memset(&v->pal, 0, sizeof v->pal);
    memset(&v->mctx, 0, sizeof v->mctx); v->mctx.load = EvMsgLoad; v->mctx.user = v;
    v->langSel = sel >= 0 && sel < v->nlangs ? sel : -1;
    if (v->langSel < 0) return;
    HCURSOR oc = SetCursor(LoadCursor(NULL, IDC_WAIT));
    size_t n; unsigned char *b = ArcReadAll(v->langs[sel].path, &n);
    if (b) b = ArcUnzstd(b, n, &n);
    if (b) { ArcSarcEach(b, n, EvMsgEach, v); free(b); }
    SetCursor(oc);
    snprintf(g_evLang, sizeof g_evLang, "%s", v->langs[sel].code);
}
// is this message file a likely home for the event's text? (the speaking actor's file, or the flow's own)
static int EvPreferred(EV *v, const char *file, const char *actor) {
    const char *s = strrchr(file, '/'); s = s ? s + 1 : file;
    if (actor && actor[0] && !strcmp(s, actor)) return 1;
    if (v->flowBase[0] && !strcmp(s, v->flowBase)) return 1;
    return 0;
}
// a label: in the preferred files first, then anywhere; variants for balloons and placement-made labels
static int EvFindLabel(EV *v, const char *label, const char *actor, int *fi) {
    char t[300];
    const char *forms[2] = { label, NULL }; snprintf(t, sizeof t, "TalkBalloon_%s", label); forms[1] = t;
    for (int pass = 0; pass < 2; pass++)
        for (int k = 0; k < 2; k++)
            for (int i = 0; i < v->nmf; i++) {
                if (!pass && !EvPreferred(v, v->mf[i].name, actor)) continue;
                int j = MsbtFind(&v->mf[i].m, forms[k]); if (j >= 0) { *fi = i; return j; }
            }
    size_t ll = strlen(label);                       // IsCreateLabelByPlacement: "<placement>_<suffix>"
    for (int i = 0; i < v->nmf; i++) {
        if (!EvPreferred(v, v->mf[i].name, actor)) continue;
        const Msbt *m = &v->mf[i].m;
        for (int j = 0; j < m->n; j++) { const char *l = m->e[j].label; size_t n = l ? strlen(l) : 0;
            if (n > ll + 1 && l[n - ll - 1] == '_' && !strcmp(l + n - ll, label)) { *fi = i; return j; } }
    }
    return -1;
}
// col (optional, n bytes): per output character, 0 = normal text, k+1 = project.msbp colour k
static int EvRenderMsg(EV *v, int fi, int j, wchar_t *out, size_t n, unsigned char *col) {
    MsgOut o; memset(&o, 0, sizeof o); o.wantCol = col != NULL;
    MsgRender(&v->mctx, &v->mf[fi].m, j, &o);
    size_t at = 0;
    for (int i = 0; i < o.n && at < n - 8; i++) {
        wchar_t c = (wchar_t)o.s[i]; unsigned char k = o.col ? o.col[i] : 0;
        if (c == L'\n') { if (col) col[at] = col[at + 1] = 0; out[at++] = L'\r'; out[at++] = L'\n'; }
        else if (c >= 0x20 || c == L'\t') { if (col) col[at] = k; out[at++] = c; }
    }
    out[at] = 0; free(o.s); free(o.col);
    return at > 0;
}
// a project.msbp colour made readable on the dark background (dark colours lifted towards white, keeping the hue)
static COLORREF EvPalColour(EV *v, int k) {
    if (k <= 0 || k > v->pal.n) return RGB(0xF1, 0xEB, 0xE5);
    uint32_t c = v->pal.rgba[k - 1]; double r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255, a = ((c >> 24) & 255) / 255.0;
    r = 0x1D + (r - 0x1D) * a; g = 0x19 + (g - 0x19) * a; b = 0x1B + (b - 0x1B) * a;      // over the dark panel
    double lum = 0.299 * r + 0.587 * g + 0.114 * b;
    if (lum < 140) { double t = (140 - lum) / (255 - lum + 1e-6); r += (255 - r) * t; g += (255 - g) * t; b += (255 - b) * t; }
    return RGB((int)r, (int)g, (int)b);
}

/* ---------------- rich text ---------------- */
enum { ST_STEP, ST_FLOW, ST_TEXT, ST_CHOICE, ST_NOTE, ST_HEAD };
static void EvAppC(EV *v, const wchar_t *t, int style, COLORREF over, int useOver);
static void EvApp(EV *v, const wchar_t *t, int style) { EvAppC(v, t, style, 0, 0); }
static void EvAppC(EV *v, const wchar_t *t, int style, COLORREF over, int useOver) {
    CHARRANGE cr = { -1, -1 }; SendMessageW(v->text, EM_EXSETSEL, 0, (LPARAM)&cr);
    CHARFORMAT2W cf; ZeroMemory(&cf, sizeof cf); cf.cbSize = sizeof cf;
    cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_SIZE | CFM_FACE;
    wcscpy(cf.szFaceName, L"Segoe UI"); cf.yHeight = 190;
    switch (style) {
    case ST_STEP: cf.crTextColor = RGB(0xB0, 0xA2, 0x9C); break;
    case ST_FLOW: cf.crTextColor = RGB(0x7F, 0xB8, 0xFF); cf.dwEffects = CFE_BOLD; break;
    case ST_TEXT: cf.crTextColor = RGB(0xF1, 0xEB, 0xE5); cf.yHeight = 220; break;
    case ST_CHOICE: cf.crTextColor = RGB(0x9F, 0xD0, 0xA4); cf.yHeight = 210; break;
    case ST_NOTE: cf.crTextColor = RGB(0x8A, 0x7D, 0x76); cf.dwEffects = CFE_ITALIC; cf.yHeight = 170; break;
    case ST_HEAD: cf.crTextColor = RGB(0xFF, 0xB3, 0x47); cf.dwEffects = CFE_BOLD; cf.yHeight = 240; break;
    }
    if (useOver) { cf.crTextColor = over; cf.dwEffects |= CFE_BOLD; }
    SendMessageW(v->text, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageW(v->text, EM_REPLACESEL, FALSE, (LPARAM)t);
}
static void EvIndent(EV *v, int ind) { wchar_t t[200]; int n = ind * 4 < 190 ? ind * 4 : 190; for (int i = 0; i < n; i++) t[i] = L' '; t[n] = 0; if (n) EvApp(v, t, ST_STEP); }
static void EvParamsText(const EvParams *ps, wchar_t *out, size_t n) {
    size_t at = 0; out[0] = 0;
    for (int i = 0; i < ps->n && at < n - 64; i++) {
        const EvParam *p = &ps->p[i];
        if (p->type == 5) at += swprintf(out + at, n - at, L"%ls%hs=\"%hs\"", i ? L", " : L"", p->key, p->str ? p->str : "");
        else if (p->type == 3) at += swprintf(out + at, n - at, L"%ls%hs=%ls", i ? L", " : L"", p->key, p->num ? L"yes" : L"no");
        else at += swprintf(out + at, n - at, L"%ls%hs=%g", i ? L", " : L"", p->key, p->num);
    }
}
// the dialogue of a step: every label-like parameter that names a message
static int EvStepText(EV *v, const EvParams *ps, const char *what, int ind, int *choicesFile, char *choiceBase) {
    int any = 0; const char *actor = EvParamStr(ps, "ActorName");
    wchar_t buf[8192];
    for (int i = 0; i < ps->n; i++) {
        const EvParam *p = &ps->p[i];
        if (p->type != 5 || !p->str || !p->str[0] || (!strstr(p->key, "Label") && !strstr(p->key, "Message"))) continue;
        int fi = -1, j = EvFindLabel(v, p->str, actor, &fi);
        int isSelect = what && strstr(what, "Select");
        if (isSelect) {                                   // a question with choices: X_Query, X_Case1..n
            char q[300]; snprintf(q, sizeof q, "%s_Query", p->str);
            int qf = -1, qj = EvFindLabel(v, q, actor, &qf);
            if (qj >= 0) { fi = qf; j = qj; }
            if (choicesFile && qf >= 0) { *choicesFile = qf; snprintf(choiceBase, 256, "%s", v->mf[qf].m.e[qj].label); char *u = strstr(choiceBase, "_Query"); if (u) *u = 0; }
        }
        if (j < 0) continue;
        static unsigned char bcol[8192], ocol[9000];
        if (EvRenderMsg(v, fi, j, buf, 8192, bcol)) {
            // indent continuation lines like the first
            wchar_t out[9000]; size_t at = 0; int ii = (ind + 1) * 4;
            for (int k = 0; k < ii && at < 100; k++) { ocol[at] = 0; out[at++] = L' '; }
            ocol[at] = 0; out[at++] = 0x201C;
            for (size_t q = 0; buf[q] && at < 8800; q++) {
                ocol[at] = bcol[q]; out[at++] = buf[q];
                if (buf[q] == L'\n') for (int k = 0; k < ii + 1 && at < 8800; k++) { ocol[at] = 0; out[at++] = L' '; }
            }
            ocol[at] = 0; out[at++] = 0x201D; out[at] = 0;
            for (size_t r0 = 0; r0 < at;) {                // one run per text colour ([0:3] colour tags)
                size_t r1 = r0; while (r1 < at && ocol[r1] == ocol[r0]) r1++;
                wchar_t keep = out[r1]; out[r1] = 0;
                EvAppC(v, out + r0, ST_TEXT, EvPalColour(v, ocol[r0]), ocol[r0] != 0);
                out[r1] = keep; r0 = r1;
            }
            wchar_t note[400]; swprintf(note, 400, L"   (%hs: %hs)\r\n", v->mf[fi].name, v->mf[fi].m.e[j].label ? v->mf[fi].m.e[j].label : "");
            EvApp(v, note, ST_NOTE);
            any = 1; v->lines++;
        }
    }
    return any;
}
static void EvChoiceText(EV *v, int fi, int j, wchar_t *out, size_t n) {
    wchar_t b[2048]; out[0] = 0;
    if (fi < 0 || j < 0 || !EvRenderMsg(v, fi, j, b, 2048, NULL)) return;
    for (wchar_t *c = b; *c; c++) if (*c == L'\r' || *c == L'\n') *c = L' ';
    swprintf(out, n, L"  \x201C%ls\x201D", b);
}

/* ---------------- the script ---------------- */
static void EvWalk(EV *v, const EvFlow *F, int i, int ind, int stop, int branch, int depth) {
    int dlgOnly = SendMessageW(v->dlg, BM_GETCHECK, 0, 0) == BST_CHECKED, showP = SendMessageW(v->params, BM_GETCHECK, 0, 0) == BST_CHECKED;
    wchar_t t[4096], pt[3000];
    while (i >= 0 && i != stop && depth < 200) {
        if (i >= F->nev) { EvIndent(v, ind); EvApp(v, L"(bad step)\r\n", ST_NOTE); return; }
        const EvEvent *e = &F->ev[i];
        if (v->seen[i]) { EvIndent(v, ind); swprintf(t, 4096, L"\x2192 continues at %hs (above)\r\n", e->name); EvApp(v, t, ST_FLOW); return; }
        v->seen[i] = 1;
        const EvActor *a = e->actor >= 0 && e->actor < F->nactors ? &F->actors[e->actor] : NULL;
        if (showP) { EvParamsText(&e->prm, pt, 3000); } else pt[0] = 0;
        if (e->type == EV_ACTION) {
            const char *act = a && e->item < a->nact ? a->acts[e->item] : "?";
            // dialogue first decides whether a "dialogue only" view shows this step
            CHARRANGE before; SendMessageW(v->text, EM_EXGETSEL, 0, (LPARAM)&before);
            EvIndent(v, ind);
            swprintf(t, 4096, L"%hs   %hs.%hs%ls%ls%ls\r\n", e->name, a ? a->name : "?", act, pt[0] ? L"(" : L"", pt, pt[0] ? L")" : L"");
            EvApp(v, t, ST_STEP);
            int had = EvStepText(v, &e->prm, act, ind, NULL, NULL);
            if (dlgOnly && !had) {                       // take the step line back out
                CHARRANGE cr = { before.cpMin, -1 }; SendMessageW(v->text, EM_EXSETSEL, 0, (LPARAM)&cr); SendMessageW(v->text, EM_REPLACESEL, FALSE, (LPARAM)L"");
            }
            i = e->next;
        } else if (e->type == EV_SWITCH) {
            const char *q = a && e->item < a->nq ? a->qs[e->item] : "?";
            EvIndent(v, ind);
            swprintf(t, 4096, L"%hs   %hs.%hs%ls%ls%ls  \x2192 depends on the answer\r\n", e->name, a ? a->name : "?", q, pt[0] ? L"(" : L"", pt, pt[0] ? L")" : L"");
            EvApp(v, t, ST_FLOW);
            int cf = -1; char cbase[256] = "";
            EvStepText(v, &e->prm, q, ind, &cf, cbase);
            for (int k = 0; k < e->ncase; k++) {
                EvIndent(v, ind + 1);
                wchar_t ch[2200] = L"";
                if (cf >= 0 && cbase[0]) { char cl[300]; snprintf(cl, sizeof cl, "%s_Case%d", cbase, e->caseVal[k] + 1); EvChoiceText(v, cf, MsbtFind(&v->mf[cf].m, cl), ch, 2200); }
                swprintf(t, 4096, L"answer %d:", e->caseVal[k]); EvApp(v, t, ST_FLOW);
                if (ch[0]) EvApp(v, ch, ST_CHOICE);
                EvApp(v, L"\r\n", ST_FLOW);
                EvWalk(v, F, e->caseEv[k], ind + 2, -1, 0, depth + 1);
            }
            return;
        } else if (e->type == EV_FORK) {
            EvIndent(v, ind); swprintf(t, 4096, L"%hs   at the same time:\r\n", e->name); EvApp(v, t, ST_FLOW);
            for (int k = 0; k < e->nbr; k++) {
                EvIndent(v, ind + 1); swprintf(t, 4096, L"branch %d:\r\n", k + 1); EvApp(v, t, ST_FLOW);
                EvWalk(v, F, e->br[k], ind + 2, e->join, 1, depth + 1);
            }
            if (e->join >= 0 && e->join < F->nev) {
                EvIndent(v, ind); swprintf(t, 4096, L"(all branches done)\r\n"); EvApp(v, t, ST_NOTE);
                v->seen[e->join] = 1; i = F->ev[e->join].next;
            } else return;
        } else if (e->type == EV_JOIN) {
            i = e->next;
        } else if (e->type == EV_SUBFLOW) {
            EvIndent(v, ind);
            if (!e->sfFlow[0]) swprintf(t, 4096, L"%hs   run \"%hs\" (in this file)\r\n", e->name, e->sfEntry);
            else swprintf(t, 4096, L"%hs   run \"%hs\" from %hs.bfevfl\r\n", e->name, e->sfEntry, e->sfFlow);
            EvApp(v, t, ST_FLOW);
            i = e->next;
        } else if (e->type == EV_SUBSWITCH) {
            EvIndent(v, ind);
            if (!e->sfFlow[0]) swprintf(t, 4096, L"%hs   run \"%hs\" (in this file)%ls%ls%ls  \x2192 depends on its result\r\n", e->name, e->sfEntry, pt[0] ? L" (" : L"", pt, pt[0] ? L")" : L"");
            else swprintf(t, 4096, L"%hs   run \"%hs\" from %hs.bfevfl%ls%ls%ls  \x2192 depends on its result\r\n", e->name, e->sfEntry, e->sfFlow, pt[0] ? L" (" : L"", pt, pt[0] ? L")" : L"");
            EvApp(v, t, ST_FLOW);
            for (int k = 0; k < e->ncase; k++) {
                char vn[24]; EvValName((uint32_t)e->caseVal[k], vn, sizeof vn);
                EvIndent(v, ind + 1); swprintf(t, 4096, L"result %hs:\r\n", vn); EvApp(v, t, ST_FLOW);
                EvWalk(v, F, e->caseEv[k], ind + 2, -1, 0, depth + 1);
            }
            return;
        } else if (e->type == EV_RETURN) {
            char vn[24]; EvValName(e->ret, vn, sizeof vn);
            EvIndent(v, ind); swprintf(t, 4096, L"%hs   return %hs\r\n", e->name, vn); EvApp(v, t, ST_FLOW);
            return;
        } else { EvIndent(v, ind); swprintf(t, 4096, L"%hs   (unknown step type %d)\r\n", e->name, e->type); EvApp(v, t, ST_NOTE); return; }
    }
    if (i < 0 && !branch) { EvIndent(v, ind); EvApp(v, L"end\r\n", ST_NOTE); }
}
static void EvShow(EV *v) {
    SendMessageW(v->text, WM_SETREDRAW, FALSE, 0);
    SetWindowTextW(v->text, L"");
    v->lines = 0;
    if (v->cur >= 0 && v->cur < v->nitems) {
        EvItem it = v->items[v->cur];
        wchar_t t[2000];
        if (it.tl < 0) {
            const EvFlow *F = &v->f.fc[it.fc]; const EvEntry *en = &F->ent[it.ent];
            swprintf(t, 2000, L"%hs  \x2192  %hs\r\n", F->name, en->name); EvApp(v, t, ST_HEAD);
            free(v->seen); v->seen = (unsigned char *)calloc(F->nev + 1, 1);
            if (v->seen) EvWalk(v, F, en->main, 0, -1, 0, 0);
        } else {
            const EvTl *T = &v->f.tl[it.tl];
            int showP = SendMessageW(v->params, BM_GETCHECK, 0, 0) == BST_CHECKED;
            swprintf(t, 2000, L"Timeline %hs  (%.2f seconds)\r\n", T->name, T->dur / 60.0); EvApp(v, t, ST_HEAD);
            for (int pass = 0; pass < 2; pass++) {
                int n = pass ? T->nos : T->nclips; const EvClip *cl = pass ? T->os : T->clips;
                for (int k = 0; k < n; k++) {
                    const EvClip *c = &cl[k]; const EvActor *a = c->actor < T->nactors ? &T->actors[c->actor] : NULL;
                    const char *act = a && c->action < a->nact ? a->acts[c->action] : "?";
                    wchar_t pt[3000]; if (showP) EvParamsText(&c->prm, pt, 3000); else pt[0] = 0;
                    if (pass) swprintf(t, 2000, L"at %.2f s   %hs.%hs%ls%ls%ls\r\n", c->start / 60.0, a ? a->name : "?", act, pt[0] ? L"(" : L"", pt, pt[0] ? L")" : L"");
                    else swprintf(t, 2000, L"%.2f s \x2013 %.2f s   %hs.%hs%ls%ls%ls\r\n", c->start / 60.0, (c->start + c->dur) / 60.0, a ? a->name : "?", act, pt[0] ? L"(" : L"", pt, pt[0] ? L")" : L"");
                    EvApp(v, t, ST_STEP);
                    EvStepText(v, &c->prm, act, 0, NULL, NULL);
                }
            }
        }
    }
    CHARRANGE cr = { 0, 0 }; SendMessageW(v->text, EM_EXSETSEL, 0, (LPARAM)&cr);
    SendMessageW(v->text, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(v->text, NULL, TRUE);
    wchar_t inf[600];
    if (v->langSel < 0) swprintf(inf, 600, L"No message archives (romfs\\Mals) found in the dump: dialogue can't be shown.");
    else swprintf(inf, 600, L"%d line%ls of dialogue found in %hs messages.%ls", v->lines, v->lines == 1 ? L"" : L"s", EvLangName(v->langs[v->langSel].code),
                  v->lines ? L"" : L"  (Some events have no text, or name it in ways not matched yet.)");
    SetWindowTextW(v->info, inf);
}

/* ---------------- window ---------------- */
static void EvLayout(EV *v) {
    RECT rc; GetClientRect(v->wnd, &rc);
    int m = EvS(v, 8), g = EvS(v, 6), rh = EvS(v, 24), lw = EvS(v, 230), y = m;
    int ll = EvS(v, 70), cw = EvS(v, 220), x = m;
    MoveWindow(v->langLbl, x, y + EvS(v, 4), ll, rh - EvS(v, 4), TRUE); x += ll;
    MoveWindow(v->lang, x, y, cw, EvS(v, 400), TRUE); x += cw + g * 2;
    int dw = EvS(v, 120), pw = EvS(v, 150);
    MoveWindow(v->dlg, x, y, dw, rh, TRUE); x += dw + g;
    MoveWindow(v->params, x, y, pw, rh, TRUE); x += pw + g * 2;
    MoveWindow(v->info, x, y + EvS(v, 4), rc.right - x - m > 0 ? rc.right - x - m : 0, rh - EvS(v, 4), TRUE);
    y += rh + g;
    MoveWindow(v->list, m, y, lw, rc.bottom - y - m, TRUE);
    MoveWindow(v->text, m + lw + g, y, rc.right - (m + lw + g) - m, rc.bottom - y - m, TRUE);
}
static HWND EvCtl(EV *v, const wchar_t *cls, const wchar_t *text, DWORD style, DWORD ex, int id) {
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, v->wnd, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)v->font, TRUE);
    return h;
}
static LRESULT CALLBACK EvProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    EV *v = (EV *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!v && msg != WM_CREATE) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_CREATE: {
        v = (EV *)((CREATESTRUCTW *)lp)->lpCreateParams; SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)v); v->wnd = hwnd;
        v->langLbl = EvCtl(v, L"STATIC", L"Language:", SS_LEFT, 0, EVV_LANGLBL);
        v->lang = EvCtl(v, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, 0, EVV_LANG);
        v->dlg = EvCtl(v, L"BUTTON", L"Dialogue only", BS_AUTOCHECKBOX | WS_TABSTOP, 0, EVV_DLG);
        v->params = EvCtl(v, L"BUTTON", L"Show step settings", BS_AUTOCHECKBOX | WS_TABSTOP, 0, EVV_PARAMS);
        SendMessageW(v->params, BM_SETCHECK, BST_CHECKED, 0);
        v->info = EvCtl(v, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 0, EVV_INFO);
        v->list = EvCtl(v, L"LISTBOX", L"", LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, EVV_LIST);
        LoadLibraryW(L"Msftedit.dll");
        v->text = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_NOHIDESEL | WS_TABSTOP,
                                  0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)EVV_TEXT, GetModuleHandleW(NULL), NULL);
        if (!v->text) v->text = EvCtl(v, L"EDIT", L"", WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_NOHIDESEL, WS_EX_CLIENTEDGE, EVV_TEXT);
        SendMessageW(v->text, EM_SETBKGNDCOLOR, 0, RGB(0x1D, 0x19, 0x1B));
        SendMessageW(v->text, EM_EXLIMITTEXT, 0, 64 * 1024 * 1024);
        // the list: entry points of each flowchart, then timelines
        for (int c = 0; c < v->f.nfc; c++) for (int e = 0; e < v->f.fc[c].nent && v->nitems < 512; e++) {
            wchar_t t[300]; swprintf(t, 300, v->f.nfc > 1 ? L"\x25B6 %hs : %hs" : L"\x25B6 %hs%.0hs", v->f.nfc > 1 ? v->f.fc[c].name : v->f.fc[c].ent[e].name, v->f.nfc > 1 ? v->f.fc[c].ent[e].name : "");
            SendMessageW(v->list, LB_ADDSTRING, 0, (LPARAM)t);
            v->items[v->nitems].fc = c; v->items[v->nitems].ent = e; v->items[v->nitems].tl = -1; v->nitems++;
        }
        for (int k = 0; k < v->f.ntl && v->nitems < 512; k++) {
            wchar_t t[300]; swprintf(t, 300, L"\x23F1 timeline %hs", v->f.tl[k].name);
            SendMessageW(v->list, LB_ADDSTRING, 0, (LPARAM)t);
            v->items[v->nitems].fc = -1; v->items[v->nitems].ent = -1; v->items[v->nitems].tl = k; v->nitems++;
        }
        // languages
        EvFindLangs(v);
        int pick = -1;
        for (int i = 0; i < v->nlangs; i++) {
            wchar_t t[120]; swprintf(t, 120, L"%hs  (%hs)", EvLangName(v->langs[i].code), v->langs[i].code);
            SendMessageW(v->lang, CB_ADDSTRING, 0, (LPARAM)t);
            if (!strcmp(v->langs[i].code, g_evLang)) pick = i;
        }
        if (pick < 0) for (int i = 0; i < v->nlangs; i++) if (!strcmp(v->langs[i].code, "USen")) pick = i;
        if (pick < 0 && v->nlangs) pick = 0;
        if (!v->nlangs) { SendMessageW(v->lang, CB_ADDSTRING, 0, (LPARAM)L"(no message archives found)"); EnableWindow(v->lang, FALSE); }
        SendMessageW(v->lang, CB_SETCURSEL, pick >= 0 ? pick : 0, 0);
        EvSetLang(v, pick);
        EvLayout(v);
        // start on the entry point with the story: "Talk", else "Event" (cutscenes), "Start", else the first that
        // isn't an idle "Wait" loop (DemoClearSkyDivingBoss opened on "Wait", which has no dialogue)
        int first = -1;
        static const char *pref[] = { "Talk", "Event", "Start" };
        for (int pr = 0; pr < 3 && first < 0; pr++)
            for (int i = 0; i < v->nitems; i++) if (v->items[i].tl < 0 && !strcmp(v->f.fc[v->items[i].fc].ent[v->items[i].ent].name, pref[pr])) { first = i; break; }
        for (int i = 0; i < v->nitems && first < 0; i++) if (v->items[i].tl >= 0 || strcmp(v->f.fc[v->items[i].fc].ent[v->items[i].ent].name, "Wait")) first = i;
        if (first < 0) first = 0;
        v->cur = v->nitems ? first : -1;
        SendMessageW(v->list, LB_SETCURSEL, first, 0);
        EvShow(v);
        return 0;
    }
    case WM_SIZE: EvLayout(v); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO *)lp)->ptMinTrackSize.x = EvS(v, 700); ((MINMAXINFO *)lp)->ptMinTrackSize.y = EvS(v, 400); return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case EVV_LIST: if (HIWORD(wp) == LBN_SELCHANGE) { v->cur = (int)SendMessageW(v->list, LB_GETCURSEL, 0, 0); EvShow(v); } break;
        case EVV_LANG: if (HIWORD(wp) == CBN_SELCHANGE) { EvSetLang(v, (int)SendMessageW(v->lang, CB_GETCURSEL, 0, 0)); EvShow(v); } break;
        case EVV_DLG: case EVV_PARAMS: EvShow(v); break;
        }
        return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        EvFreeMsgs(v); EvFree(&v->f); free(v->langs); free(v->seen); free(v);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// dumpFile: the X.bfevfl(.zs) file in the game dump
void EvtViewOpen(HWND owner, const wchar_t *dumpFile, const wchar_t *title, HFONT font, int dpi) {
    static int reg = 0;
    HINSTANCE in = GetModuleHandleW(NULL);
    if (!reg) {
        WNDCLASSEXW wc = { 0 }; wc.cbSize = sizeof wc; wc.lpfnWndProc = EvProc; wc.hInstance = in; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = EV_CLASS;
        wc.hIcon = (HICON)SendMessageW(owner, WM_GETICON, ICON_BIG, 0); wc.hIconSm = (HICON)SendMessageW(owner, WM_GETICON, ICON_SMALL, 0);
        RegisterClassExW(&wc); reg = 1;
    }
    EV *v = (EV *)calloc(1, sizeof(EV)); if (!v) return;
    v->font = font; v->dpi = dpi ? dpi : 96; v->cur = -1; v->langSel = -1;
    swprintf(v->path, 2048, L"%ls", dumpFile); swprintf(v->title, 600, L"Event - %ls", title);
    size_t n; unsigned char *b = ArcReadAll(dumpFile, &n);
    if (b) b = ArcUnzstd(b, n, &n);
    if (!b || !EvParse(b, n, &v->f)) {
        wchar_t m[2600]; swprintf(m, 2600, L"This event script couldn't be read.\n\n%hs\n\nFile: %ls", b ? v->f.err : "The file couldn't be read from the game dump.", dumpFile);
        MessageBoxW(owner, m, L"Event viewer", MB_ICONINFORMATION);
        free(b); EvFree(&v->f); free(v); return;
    }
    free(b);
    // the flow's own message file is DemoMsg/<name>.msbt (a timeline file X_TL001 belongs to X)
    snprintf(v->flowBase, sizeof v->flowBase, "%s", v->f.name);
    char *tl = strstr(v->flowBase, "_TL"); if (tl) *tl = 0;
    int w = MulDiv(1150, v->dpi, 96), h = MulDiv(800, v->dpi, 96);
    HWND wnd = CreateWindowExW(0, EV_CLASS, v->title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, w, h, NULL, NULL, in, v);
    if (!wnd) { EvFree(&v->f); free(v); return; }
    if (wnd) UiSkinWindow(wnd);                 // the main window's dark look
    ShowWindow(wnd, SW_SHOW);
}
