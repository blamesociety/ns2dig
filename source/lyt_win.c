/* Layout viewer: draws a UI screen (.bflyt) from its archive - pictures with their textures, text with a Windows
   font, parts (other layouts) - and plays its animations (.bflan) with a frame slider. OpenGL 1.1. */
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include "ui_theme.h"
void UiSkinWindow(HWND top);
#include <commctrl.h>
#include <commdlg.h>
#include <math.h>
#include <stdio.h>
#include <GL/gl.h>
#include "arc_util.h"
#include "tex_core.h"
#include "lyt_core.h"
#include "msg_core.h"
#include "font_win.h"

static void *xalloc(size_t n) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n); }
static void xfree(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }
#include "deflate_lite.h"
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif
#ifndef GL_MIRRORED_REPEAT
#define GL_MIRRORED_REPEAT 0x8370
#endif

#define LV_CLASS L"AlsarcLytView"
#define LV_GL_CLASS L"AlsarcLytGL"
enum { LV_LAYOUT = 200, LV_ANIM, LV_PLAY, LV_LOOP, LV_SLIDER, LV_FRAME, LV_HIDDEN, LV_BG, LV_FIT, LV_SAVE, LV_INFO, LV_PANES, LV_GL, LV_OUTLINE, LV_LANG, LV_LANGINFO, LV_GFONTS };
#define LV_TIMER 7
#define WM_LV_LOADED (WM_APP + 50)

typedef struct { char name[96]; GLuint id; int w, h, tried; } LvTex;
typedef struct { char name[64]; Lyt L; int ok; } LvLayout;
typedef struct { const Lyt *L; int pane; char key[256]; GLuint id; int w, h; } LvText;
typedef struct { char code[8]; int ver; wchar_t path[1024]; } LvLang;
static char g_lvLang[8] = "USen";          // language last chosen in any layout window ("-" = the layout file's own text)

typedef struct {
    HWND wnd, layout, animCb, play, loop, slider, frameLbl, hidden, bg, fit, save, info, panes, gl, outline, langCb, langInfo, gfCheck;
    HFONT font; int dpi;
    wchar_t path[2048], title[600]; char inner[600];
    // archive
    unsigned char *arc; size_t arcN;
    unsigned char *bntx; size_t bntxN; TexFile tf;
    char (*blyt)[64]; int nblyt; char (*anims)[96]; int nanims;
    // layouts (main + parts), animation
    LvLayout **lays; int nlays;              // each separately allocated: addresses stay put while drawing loads parts
    int cur;                                  // index into lays of the shown layout
    Lan anim; int haveAnim; float frame; int playing;
    // working copies the animation writes into
    LytPane *wp; LytMat *wm; char (*wtex)[96];
    unsigned char *userHide;                  // pane visibility switched off in the list
    // GL
    HDC dc; HGLRC rc;
    LvTex *tex; int ntex, captex;
    LvText *txt; int ntxt, captxt;
    double zoom, panX, panY; int fitMode, dragging; POINT dragFrom; double panFrom[2];
    int bgMode, showHidden, outlines;
    int loaded; char err[300];
    char startAnim[96];
    // language text (Mals/<lang>.Product.<ver>.sarc.zs)
    LvLang *langs; int nlangs, langSel;        // langSel: index into langs, -1 = the layout's own text
    unsigned char *msgArc; size_t msgArcN;
    Msbt mcache[64]; char mname[64][100]; int nm;
    const Msbt *lmsg;                          // LayoutMsg/<shown layout>.msbt
    MsgCtx mctx; MsgOut mo;
    int txtTotal, txtMatched, shownTotal, shownMatched;
    // the game's fonts (romfs\Font\Font[_<lang>].*.bfarc.zs)
    wchar_t roots[12][1024]; int nroots;      // romfs folders of the dump, newest first
    GameFonts gf; int gameFonts;
    int noSwap;                                // preview: leave the picture in the back buffer to read back
} LV;

static int Sx(LV *v, int x) { return MulDiv(x, v->dpi, 96); }

/* ---------------- archive ---------------- */
typedef struct { LV *v; } LvScan;
static int LvScanCb(const char *name, const unsigned char *d, size_t n, void *u) {
    LV *v = ((LvScan *)u)->v; (void)d; (void)n;
    size_t l = strlen(name);
    if (!strncmp(name, "blyt/", 5) && l > 11 && !strcmp(name + l - 6, ".bflyt")) {
        char (*nb)[64] = realloc(v->blyt, (v->nblyt + 1) * 64); if (!nb) return 1; v->blyt = nb;
        snprintf(v->blyt[v->nblyt], 64, "%.*s", (int)(l - 11), name + 5); v->nblyt++;
    } else if (!strncmp(name, "anim/", 5) && l > 11 && !strcmp(name + l - 6, ".bflan")) {
        char (*na)[96] = realloc(v->anims, (v->nanims + 1) * 96); if (!na) return 1; v->anims = na;
        snprintf(v->anims[v->nanims], 96, "%.*s", (int)(l - 11), name + 5); v->nanims++;
    } else if (!v->bntx && !strncmp(name, "timg/", 5) && l > 5 && !strcmp(name + l - 5, ".bntx")) {
        v->bntx = (unsigned char *)malloc(n + 1); if (v->bntx) { memcpy(v->bntx, d, n); v->bntxN = n; }
    }
    return 0;
}
static int CmpName64(const void *a, const void *b) { return _stricmp((const char *)a, (const char *)b); }
static int CmpName96(const void *a, const void *b) { return _stricmp((const char *)a, (const char *)b); }
static DWORD WINAPI LvLoadThread(LPVOID p) {
    LV *v = (LV *)p; size_t n;
    unsigned char *b = ArcReadAll(v->path, &n);
    if (b) b = ArcUnzstd(b, n, &n);
    if (!b) { snprintf(v->err, sizeof v->err, "The file couldn't be read from the game dump."); PostMessageW(v->wnd, WM_LV_LOADED, 0, 0); return 0; }
    v->arc = b; v->arcN = n;
    if (v->inner[0]) {
        LvScan s = { v }; ArcSarcEach(b, n, LvScanCb, &s);
        if (v->blyt) qsort(v->blyt, v->nblyt, 64, CmpName64);
        if (v->anims) qsort(v->anims, v->nanims, 96, CmpName96);
        if (v->bntx) TexOpen(&v->tf, v->bntx, v->bntxN);
    }
    PostMessageW(v->wnd, WM_LV_LOADED, 0, 1);
    return 0;
}
// a layout by name (parsed once); -1 if missing
static int LvLayoutIdx(LV *v, const char *name) {
    for (int i = 0; i < v->nlays; i++) if (!strcmp(v->lays[i]->name, name)) return v->lays[i]->ok ? i : -1;
    LvLayout **nl = (LvLayout **)realloc(v->lays, (v->nlays + 1) * sizeof(LvLayout *)); if (!nl) return -1; v->lays = nl;
    LvLayout *x = (LvLayout *)calloc(1, sizeof(LvLayout)); if (!x) return -1;
    v->lays[v->nlays++] = x; snprintf(x->name, sizeof x->name, "%s", name);
    size_t n = 0; unsigned char *b = NULL;
    if (v->inner[0]) { char in[128]; snprintf(in, sizeof in, "blyt/%s.bflyt", name); b = ArcSarcMember(v->arc, v->arcN, in, &n); }
    else if (v->arc && v->nlays == 1) { b = (unsigned char *)malloc(v->arcN); if (b) { memcpy(b, v->arc, v->arcN); n = v->arcN; } }
    if (b) { x->ok = LytParse(&x->L, b, n); free(b); }
    return x->ok ? v->nlays - 1 : -1;
}

/* ---------------- textures ---------------- */
static GLuint LvUpload(const unsigned char *rgba, int w, int h, int wrapS, int wrapT) {
    GLuint id = 0; glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    static const GLint wr[3] = { GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT };
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wr[wrapS > 2 ? 0 : wrapS]); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wr[wrapT > 2 ? 0 : wrapT]);
    return id;
}
static LvTex *LvTexture(LV *v, const char *name) {
    for (int i = 0; i < v->ntex; i++) if (!strcmp(v->tex[i].name, name)) return v->tex[i].id ? &v->tex[i] : NULL;
    if (v->ntex == v->captex) { int nc = v->captex ? v->captex * 2 : 64; LvTex *t = (LvTex *)realloc(v->tex, nc * sizeof(LvTex)); if (!t) return NULL; v->tex = t; v->captex = nc; }
    LvTex *t = &v->tex[v->ntex++]; memset(t, 0, sizeof *t); snprintf(t->name, sizeof t->name, "%s", name);
    for (int i = 0; i < v->tf.n; i++) if (!strcmp(v->tf.t[i].name, name)) {
        char e[200]; unsigned char *px = TexDecode(v->bntx, v->bntxN, &v->tf.t[i], v->tf.ounce, e, sizeof e);
        if (px) { t->id = LvUpload(px, v->tf.t[i].w, v->tf.t[i].h, 1, 1); t->w = v->tf.t[i].w; t->h = v->tf.t[i].h; free(px); }
        break;
    }
    return t->id ? t : NULL;
}
// text drawn with a Windows font into a texture the size of the text box (white; tinted when drawn)
static LvText *LvTextTex(LV *v, const Lyt *L, int pi, const LytPane *p, const char *path) {
    char key[256]; snprintf(key, sizeof key, "%s%s", path, p->name);
    for (int i = 0; i < v->ntxt; i++) if (v->txt[i].L == L && v->txt[i].pane == pi && !strcmp(v->txt[i].key, key)) return v->txt[i].id ? &v->txt[i] : NULL;
    if (v->ntxt == v->captxt) { int nc = v->captxt ? v->captxt * 2 : 64; LvText *t = (LvText *)realloc(v->txt, nc * sizeof(LvText)); if (!t) return NULL; v->txt = t; v->captxt = nc; }
    LvText *t = &v->txt[v->ntxt++]; memset(t, 0, sizeof *t); t->L = L; t->pane = pi; snprintf(t->key, sizeof t->key, "%s", key);
    // the text: the chosen language's message for this pane (by its name, with the part panes it sits in), else the layout's own
    const wchar_t *text = p->text; int fromMsg = 0;
    if (v->langSel >= 0) {
        int k = v->lmsg ? MsbtMatch(v->lmsg, key, NULL) : -1;
        if (k >= 0 && MsgRender(&v->mctx, v->lmsg, k, &v->mo)) { text = (const wchar_t *)v->mo.s; fromMsg = 1; }
        else if (p->text) { MsgRenderPlain(&v->mctx, (const uint16_t *)p->text, (int)wcslen(p->text), &v->mo); text = (const wchar_t *)v->mo.s; }
    }
    if (fromMsg || (p->text && p->text[0])) { v->txtTotal++; if (fromMsg) v->txtMatched++; }
    if (!text || !text[0]) return NULL;
    int gfi = v->gameFonts && v->gf.ok && p->font >= 0 && p->font < L->nfont ? GfFind(&v->gf, L->font[p->font]) : -1;
    if (gfi >= 0) {                                        // the game's own font
        int bw = (int)(fabsf(p->size[0]) + 0.5f), bh = (int)(fabsf(p->size[1]) + 0.5f);
        int pad = bh > 48 ? bh : 48;
        int w = bw + 2 * pad, h = bh + 2 * pad; if (w > 4096) w = 4096; if (h > 2048) h = 2048;
        unsigned char *px = (unsigned char *)calloc((size_t)w * h, 4);
        if (px && GfRender(&v->gf, gfi, text, p->fontSize[0], p->fontSize[1], p->charSpace, p->lineSpace,
                           p->textPos & 3, (p->textPos >> 2) & 3, p->align, (float)pad, (float)pad, (float)bw, (float)bh, px, w, h)) {
            t->id = LvUpload(px, w, h, 0, 0); t->w = w; t->h = h;
        }
        free(px);
        return t->id ? t : NULL;
    }
    int w = (int)(fabsf(p->size[0]) + 0.5f), h = (int)(fabsf(p->size[1]) + 0.5f);
    float fs = p->fontSize[1] > 0 ? p->fontSize[1] * 100.0f : h * 0.8f;
    int pad = (int)fs;                                     // room for text running past the box
    w += 2 * pad; h += 2 * pad; if (w < 4 || h < 4) return NULL; if (w > 4096) w = 4096; if (h > 2048) h = 2048;
    BITMAPINFO bi; ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void *bits = NULL; HDC dc = CreateCompatibleDC(NULL); HBITMAP bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bm) { DeleteDC(dc); return NULL; }
    HGDIOBJ ob = SelectObject(dc, bm);
    memset(bits, 0, (size_t)w * h * 4);
    HFONT f = CreateFontW(-(int)(fs + 0.5f), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Yu Gothic UI");
    HGDIOBJ of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
    RECT rc = { pad, pad, w - pad, h - pad };
    int hx = p->textPos & 3, vy = (p->textPos >> 2) & 3;   // 0 centre, 1 left/top, 2 right/bottom
    UINT fl = DT_NOPREFIX | (hx == 1 ? DT_LEFT : hx == 2 ? DT_RIGHT : DT_CENTER);
    RECT calc = rc; DrawTextW(dc, text, -1, &calc, fl | DT_CALCRECT | DT_WORDBREAK);
    int th = calc.bottom - calc.top, bh = rc.bottom - rc.top;
    if (vy == 0) rc.top += (bh - th) / 2; else if (vy == 2) rc.top = rc.bottom - th;
    rc.bottom = rc.top + th + 2;
    rc.left -= pad / 2; rc.right += pad / 2;
    DrawTextW(dc, text, -1, &rc, fl | DT_WORDBREAK);
    GdiFlush();
    unsigned char *px = (unsigned char *)bits;
    for (size_t i = 0; i < (size_t)w * h; i++) { unsigned char a = px[i * 4 + 1]; px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = 255; px[i * 4 + 3] = a; }
    t->id = LvUpload(px, w, h, 0, 0); t->w = w; t->h = h;
    SelectObject(dc, of); DeleteObject(f); SelectObject(dc, ob); DeleteObject(bm); DeleteDC(dc);
    return t;
}

/* ---------------- animation ---------------- */
static void LvApplyAnim(LV *v) {
    if (v->cur < 0) return;
    Lyt *L = &v->lays[v->cur]->L;
    memcpy(v->wp, L->pane, sizeof(LytPane) * L->npane);
    memcpy(v->wm, L->mat, sizeof(LytMat) * L->nmat);
    for (int i = 0; i < L->nmat; i++) v->wtex[i][0] = 0;
    if (!v->haveAnim) return;
    for (int c = 0; c < v->anim.ncurve; c++) {
        const LanCurve *cv = &v->anim.curve[c];
        float val = LanEval(cv, v->frame);
        int ch = cv->channel;
        if (!cv->isMat) {
            int pi = LytFindPane(L, cv->target); if (pi < 0) continue;
            LytPane *p = &v->wp[pi];
            if (!strcmp(cv->tag, "FLPA")) {
                if (ch <= 2) p->pos[ch] = val; else if (ch <= 5) p->rot[ch - 3] = val; else if (ch <= 7) p->scale[ch - 6] = val; else if (ch <= 9) p->size[ch - 8] = val;
            } else if (!strcmp(cv->tag, "FLVI")) p->visible = val != 0;
            else if (!strcmp(cv->tag, "FLVC")) {
                int x = (int)(val + 0.5f); if (x < 0) x = 0; if (x > 255) x = 255;
                if (ch < 16) p->vtx[ch / 4][ch % 4] = (unsigned char)x; else if (ch == 16) p->alpha = x;
            }
        } else {
            int mi = LytFindMat(L, cv->target); if (mi < 0) continue;
            LytMat *m = &v->wm[mi];
            if (!strcmp(cv->tag, "FLMC")) {
                int x = (int)(val + 0.5f); if (x < 0) x = 0; if (x > 255) x = 255;
                if (ch < 4) m->black[ch] = (unsigned char)x; else if (ch < 8) m->white[ch - 4] = (unsigned char)x;
            } else if (!strcmp(cv->tag, "FLTS") && cv->index == 0 && ch < 5) m->srt[ch] = val;
            else if (!strcmp(cv->tag, "FLTP") && cv->index == 0) {
                int ti = (int)(val + 0.5f);
                if (ti >= 0 && ti < v->anim.ntex) snprintf(v->wtex[mi], 96, "%s", v->anim.tex[ti]);
            }
        }
    }
}

/* ---------------- drawing ---------------- */
typedef struct { double a, b, c, d, x, y; } M2;      // [a c x; b d y]
static M2 MMul(M2 p, M2 q) { M2 r = { p.a * q.a + p.c * q.b, p.b * q.a + p.d * q.b, p.a * q.c + p.c * q.d, p.b * q.c + p.d * q.d,
                                      p.a * q.x + p.c * q.y + p.x, p.b * q.x + p.d * q.y + p.y }; return r; }
static M2 MLocal(double tx, double ty, double rotDeg, double sx, double sy) {
    double r = rotDeg * 3.14159265358979 / 180, c = cos(r), s = sin(r);
    M2 m = { c * sx, s * sx, -s * sy, c * sy, tx, ty }; return m;
}
static void PaneRect(const LytPane *p, double *l, double *b, double *r, double *t) {
    double w = p->size[0], h = p->size[1]; int xo = p->origin & 3, yo = (p->origin >> 2) & 3;
    *l = xo == 0 ? -w / 2 : xo == 1 ? 0 : -w; *r = *l + w;
    *t = yo == 0 ? h / 2 : yo == 1 ? 0 : h; *b = *t - h;
}
static void Vtx(M2 m, double x, double y) { glVertex2d(m.a * x + m.c * y + m.x, m.b * x + m.d * y + m.y); }
static void DrawQuad(M2 m, double l, double b, double r, double t, const float *uv, const unsigned char vc[4][4], const float tint[4], int textured) {
    // corners TL, TR, BL, BR
    double X[4] = { l, r, l, r }, Y[4] = { t, t, b, b }; int order[4] = { 0, 1, 3, 2 };
    glBegin(GL_QUADS);
    for (int k = 0; k < 4; k++) {
        int i = order[k];
        glColor4f(vc[i][0] / 255.0f * tint[0], vc[i][1] / 255.0f * tint[1], vc[i][2] / 255.0f * tint[2], vc[i][3] / 255.0f * tint[3]);
        if (textured) glTexCoord2f(uv[i * 2], uv[i * 2 + 1]);
        Vtx(m, X[i], Y[i]);
    }
    glEnd();
}
static void DrawOutline(M2 m, double l, double b, double r, double t) {
    glDisable(GL_TEXTURE_2D); glColor4f(1, 0.3f, 0.9f, 0.8f);
    glBegin(GL_LINE_LOOP); Vtx(m, l, t); Vtx(m, r, t); Vtx(m, r, b); Vtx(m, l, b); glEnd();
}
static void LvDrawPane(LV *v, const Lyt *L, LytPane *panes, LytMat *mats, char (*mtex)[96], int pi, M2 parent, double palpha, int depth, const char *path) {
    const LytPane *p = &panes[pi];
    int isMain = L == &v->lays[v->cur]->L;
    if (!v->showHidden && !p->visible) return;
    if (isMain && v->userHide && v->userHide[pi]) return;
    double alpha = palpha * p->alpha / 255.0;
    M2 m = MMul(parent, MLocal(p->pos[0], p->pos[1], p->rot[2], p->scale[0], p->scale[1]));
    double l, b, r, t; PaneRect(p, &l, &b, &r, &t);
    if ((p->kind == LP_PIC || p->kind == LP_WINDOW) && p->mat >= 0 && p->mat < L->nmat) {
        const LytMat *mt = &mats[p->mat];
        const char *tn = mtex && mtex[p->mat][0] ? mtex[p->mat] : (mt->tex >= 0 && mt->tex < L->ntex ? L->tex[mt->tex] : NULL);
        LvTex *tx = tn ? LvTexture(v, tn) : NULL;
        float tint[4] = { mt->white[0] / 255.0f, mt->white[1] / 255.0f, mt->white[2] / 255.0f, (float)(mt->white[3] / 255.0 * alpha) };
        if (mt->shader[0]) tint[3] *= 0.5f;     // procedural shape (rounded box, neon...): drawn by a GPU shader in the game - shown as a half-transparent stand-in
        if (tx) {
            glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tx->id);
            static const GLint wr[3] = { GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT };
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wr[mt->wrapS > 2 ? 0 : mt->wrapS]); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wr[mt->wrapT > 2 ? 0 : mt->wrapT]);
            glMatrixMode(GL_TEXTURE); glLoadIdentity();             // texture scroll/rotate/scale (about the centre)
            glTranslatef(0.5f + mt->srt[0], 0.5f - mt->srt[1], 0); glRotatef(mt->srt[2], 0, 0, 1); glScalef(mt->srt[3], mt->srt[4], 1); glTranslatef(-0.5f, -0.5f, 0);
            glMatrixMode(GL_MODELVIEW);
            DrawQuad(m, l, b, r, t, p->uv, p->vtx, tint, 1);
            glMatrixMode(GL_TEXTURE); glLoadIdentity(); glMatrixMode(GL_MODELVIEW);
            glDisable(GL_TEXTURE_2D);
        } else if (!(mt->tex >= 0)) {
            DrawQuad(m, l, b, r, t, p->uv, p->vtx, tint, 0);    // untextured: plain colour
        }
    } else if (p->kind == LP_TEXT) {
        LvText *tt = LvTextTex(v, L, pi, p, path);
        if (tt) {
            const LytMat *mt = p->mat >= 0 && p->mat < L->nmat ? &mats[p->mat] : NULL;
            float tint[4] = { mt ? mt->white[0] / 255.0f : 1, mt ? mt->white[1] / 255.0f : 1, mt ? mt->white[2] / 255.0f : 1, (float)alpha };
            unsigned char vc[4][4]; for (int i = 0; i < 4; i++) memcpy(vc[i], p->textCol[i < 2 ? 0 : 1], 4);
            double padX = (tt->w - fabs(p->size[0])) / 2, padY = (tt->h - fabs(p->size[1])) / 2;
            float uv[8] = { 0, 0, 1, 0, 0, 1, 1, 1 };
            glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tt->id);
            DrawQuad(m, l - padX, b - padY, r + padX, t + padY, uv, vc, tint, 1);
            glDisable(GL_TEXTURE_2D);
        }
    } else if (p->kind == LP_PART && depth < 8) {
        int li = LvLayoutIdx(v, p->part);
        if (li >= 0) {
            const Lyt *PL = &v->lays[li]->L;
            M2 q = MMul(m, MLocal(0, 0, 0, p->magnify[0] ? p->magnify[0] : 1, p->magnify[1] ? p->magnify[1] : 1));
            char sub[256]; snprintf(sub, sizeof sub, "%s%s-", path, p->name);     // message labels name panes inside parts as Part-Pane
            for (int c = PL->pane[PL->root].firstChild; c >= 0; c = PL->pane[c].next) LvDrawPane(v, PL, PL->pane, PL->mat, NULL, c, q, alpha, depth + 1, sub);
        }
    }
    if (v->outlines && p->kind != LP_NULL) DrawOutline(m, l, b, r, t);
    double calpha = p->influence ? alpha : palpha;
    for (int c = p->firstChild; c >= 0; c = panes[c].next) LvDrawPane(v, L, panes, mats, mtex, c, m, calpha, depth, path);
}
static void LvFit(LV *v) {
    if (v->cur < 0) return;
    RECT rc; GetClientRect(v->gl, &rc);
    const Lyt *L = &v->lays[v->cur]->L;
    double zx = (rc.right - 20) / (L->w > 0 ? L->w : 100), zy = (rc.bottom - 20) / (L->h > 0 ? L->h : 100);
    v->zoom = zx < zy ? zx : zy; if (v->zoom <= 0) v->zoom = 1; v->panX = v->panY = 0;
}
static void LvPaint(LV *v) {
    if (!v->rc) return;
    wglMakeCurrent(v->dc, v->rc);
    RECT rc; GetClientRect(v->gl, &rc);
    glViewport(0, 0, rc.right, rc.bottom);
    static const float bg[4][3] = { { 0.18f, 0.2f, 0.24f }, { 0, 0, 0 }, { 1, 1, 1 }, { 0.2f, 0.45f, 0.25f } };
    glClearColor(bg[v->bgMode][0], bg[v->bgMode][1], bg[v->bgMode][2], 1); glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, rc.right, 0, rc.bottom, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    if (v->cur >= 0) {
        const Lyt *L = &v->lays[v->cur]->L;
        M2 view = { v->zoom, 0, 0, v->zoom, rc.right / 2.0 + v->panX, rc.bottom / 2.0 - v->panY };
        // the screen area
        glColor4f(0, 0, 0, 0.25f);
        glBegin(GL_QUADS); Vtx(view, -L->w / 2, -L->h / 2); Vtx(view, L->w / 2, -L->h / 2); Vtx(view, L->w / 2, L->h / 2); Vtx(view, -L->w / 2, L->h / 2); glEnd();
        LvDrawPane(v, L, v->wp, v->wm, v->wtex, L->root, view, 1.0, 0, "");
        glColor4f(1, 1, 1, 0.35f);
        glBegin(GL_LINE_LOOP); Vtx(view, -L->w / 2, -L->h / 2); Vtx(view, L->w / 2, -L->h / 2); Vtx(view, L->w / 2, L->h / 2); Vtx(view, -L->w / 2, L->h / 2); glEnd();
    }
    if (!v->noSwap) SwapBuffers(v->dc);
}

/* ---------------- language text ---------------- */
static const char *LvLangName(const char *c) {
    static const char *t[][2] = { { "USen", "English (US)" }, { "EUen", "English (UK)" }, { "JPja", "Japanese" }, { "EUfr", "French" },
        { "USfr", "French (Canada)" }, { "EUde", "German" }, { "EUes", "Spanish (Spain)" }, { "USes", "Spanish (Latin America)" },
        { "EUit", "Italian" }, { "EUnl", "Dutch" }, { "EUru", "Russian" }, { "KRko", "Korean" }, { "CNzh", "Chinese (Simplified)" },
        { "TWzh", "Chinese (Traditional)" }, { "EUpt", "Portuguese" }, { "USpt", "Portuguese (Brazil)" }, { "EUpl", "Polish" }, { "THth", "Thai" } };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) if (!strcmp(t[i][0], c)) return t[i][1];
    return c;
}
static const Msbt *LvMsgLoad(void *user, const char *file) {
    LV *v = (LV *)user;
    for (int i = 0; i < v->nm; i++) if (!strcmp(v->mname[i], file)) return v->mcache[i].e ? &v->mcache[i] : NULL;
    if (v->nm >= 64 || !v->msgArc) return NULL;
    int i = v->nm++; snprintf(v->mname[i], sizeof v->mname[i], "%s", file); memset(&v->mcache[i], 0, sizeof(Msbt));
    char in[128]; snprintf(in, sizeof in, "%s.msbt", file);
    size_t n; unsigned char *b = ArcSarcMember(v->msgArc, v->msgArcN, in, &n);
    if (b) { MsbtParse(&v->mcache[i], b, n); free(b); }
    return v->mcache[i].e ? &v->mcache[i] : NULL;
}
static void LvClearText(LV *v) {
    if (v->rc) wglMakeCurrent(v->dc, v->rc);
    for (int i = 0; i < v->ntxt; i++) if (v->txt[i].id) glDeleteTextures(1, &v->txt[i].id);
    v->ntxt = 0; v->txtTotal = v->txtMatched = 0; v->shownTotal = v->shownMatched = -1;
}
static void LvAddLangDir(LV *v, const wchar_t *dir) {
    wchar_t pat[1100]; swprintf(pat, 1100, L"%ls\\*.Product.*.sarc.zs", dir);
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd); if (h == INVALID_HANDLE_VALUE) return;
    do {
        const wchar_t *n = fd.cFileName, *d = wcschr(n, L'.'), *pv = wcsstr(n, L".Product.");
        if (!d || !pv || d != pv || d - n < 2 || d - n > 7 || (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        char code[8]; int cl = WideCharToMultiByte(CP_UTF8, 0, n, (int)(d - n), code, 7, NULL, NULL); code[cl > 0 ? cl : 0] = 0;
        int ver = _wtoi(pv + 9), k = -1;
        for (int i = 0; i < v->nlangs; i++) if (!strcmp(v->langs[i].code, code)) k = i;
        if (k >= 0 && v->langs[k].ver >= ver) continue;         // keep the newest (an update's .300 over the base .100)
        if (k < 0) { LvLang *nl = (LvLang *)realloc(v->langs, (v->nlangs + 1) * sizeof(LvLang)); if (!nl) break; v->langs = nl; k = v->nlangs++; }
        snprintf(v->langs[k].code, 8, "%s", code); v->langs[k].ver = ver; swprintf(v->langs[k].path, 1024, L"%ls\\%ls", dir, n);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static int CmpLang(const void *a, const void *b) { return strcmp(LvLangName(((const LvLang *)a)->code), LvLangName(((const LvLang *)b)->code)); }
// the message archives: romfs\Mals next to this file, and those of the base game and every update in the same dump
static void LvFindLangs(LV *v) {
    size_t n = wcslen(v->path), at = (size_t)-1;
    for (size_t i = 0; i + 7 <= n; i++) if (!_wcsnicmp(v->path + i, L"\\romfs\\", 7)) { at = i; break; }
    if (at == (size_t)-1) return;
    wchar_t d[2200], own[2048];
    swprintf(d, 2200, L"%.*ls\\Mals", (int)(at + 6), v->path); LvAddLangDir(v, d);
    wchar_t ownRomfs[1024]; swprintf(ownRomfs, 1024, L"%.*ls", (int)(at + 6), v->path);
    swprintf(own, 2048, L"%.*ls", (int)at, v->path);           // ...\base or ...\updates\<n>
    wchar_t *s = wcsrchr(own, L'\\');
    if (s) {
        int ok = 0;
        if (!_wcsicmp(s + 1, L"base")) { *s = 0; ok = 1; }
        else { *s = 0; wchar_t *s2 = wcsrchr(own, L'\\'); if (s2 && !_wcsicmp(s2 + 1, L"updates")) { *s2 = 0; ok = 1; } }
        if (ok) {
            swprintf(d, 2200, L"%ls\\base\\romfs\\Mals", own); LvAddLangDir(v, d);
            if (v->nroots < 12) swprintf(v->roots[v->nroots++], 1024, L"%ls\\base\\romfs", own);
            wchar_t pat[2200]; swprintf(pat, 2200, L"%ls\\updates\\*", own);
            WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.') {
                    swprintf(d, 2200, L"%ls\\updates\\%ls\\romfs\\Mals", own, fd.cFileName); LvAddLangDir(v, d);
                    if (v->nroots < 12) swprintf(v->roots[v->nroots++], 1024, L"%ls\\updates\\%ls\\romfs", own, fd.cFileName);
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        }
    }
    if (v->nlangs > 1) qsort(v->langs, v->nlangs, sizeof(LvLang), CmpLang);
    // romfs folders, newest first: updates (highest number first), then base; just this one if the dump looks different
    if (!v->nroots) swprintf(v->roots[v->nroots++], 1024, L"%ls", ownRomfs);
    for (int i = 0; i < v->nroots; i++) for (int j = i + 1; j < v->nroots; j++) {
        int bi = wcsstr(v->roots[i], L"\\base\\romfs") != NULL, bj = wcsstr(v->roots[j], L"\\base\\romfs") != NULL;
        const wchar_t *ui = wcsstr(v->roots[i], L"\\updates\\"), *uj = wcsstr(v->roots[j], L"\\updates\\");
        long ni = ui ? wcstol(ui + 9, NULL, 10) : -1, nj = uj ? wcstol(uj + 9, NULL, 10) : -1;
        if ((bi && !bj) || (!bi && !bj && nj > ni)) { wchar_t t[1024]; wcscpy(t, v->roots[i]); wcscpy(v->roots[i], v->roots[j]); wcscpy(v->roots[j], t); }
    }
}
// the font archive for a language: Font_<lang>.*.bfarc.zs if the game has one for it, else Font.*.bfarc.zs
static int LvFontArchive(LV *v, const char *lang, wchar_t *out) {
    for (int pass = 0; pass < 2; pass++)
        for (int r = 0; r < v->nroots; r++) {
            wchar_t pat[1200];
            if (pass == 0) { if (!lang || !lang[0] || !strcmp(lang, "-")) break; swprintf(pat, 1200, L"%ls\\Font\\Font_%hs.*.bfarc.zs", v->roots[r], lang); }
            else swprintf(pat, 1200, L"%ls\\Font\\Font.*.bfarc.zs", v->roots[r]);
            WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd);
            if (h != INVALID_HANDLE_VALUE) { FindClose(h); swprintf(out, 1024, L"%ls\\Font\\%ls", v->roots[r], fd.cFileName); return 1; }
        }
    for (int r = 0; r < v->nroots; r++) {      // Mario Kart World: loose fonts in romfs\Font (X.bfcpx, X.bfotf.zs)
        wchar_t pat[1200]; swprintf(pat, 1200, L"%ls\\Font\\*.bfcpx", v->roots[r]);
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) { FindClose(h); swprintf(out, 1024, L"%ls\\Font", v->roots[r]); return 1; }
    }
    return 0;
}
static int LvHasGlyph(void *user, unsigned cp) { LV *v = (LV *)user; return v->gameFonts && v->gf.ok && GfAnyHas(&v->gf, cp); }
static void LvLoadFonts(LV *v) {
    wchar_t p[1024];
    const char *lang = v->langSel >= 0 ? v->langs[v->langSel].code : NULL;
    if (!LvFontArchive(v, lang, p)) { GfFree(&v->gf); snprintf(v->gf.err, sizeof v->gf.err, "no Font\\Font*.bfarc.zs or Font\\*.bfcpx in the dump's romfs folders"); return; }
    if (v->gf.ok && !_wcsicmp(v->gf.path, p)) return;
    HCURSOR oc = SetCursor(LoadCursor(NULL, IDC_WAIT));
    GfFree(&v->gf); GfLoad(&v->gf, p);
    SetCursor(oc);
}
static void LvBindLayoutMsg(LV *v) {
    v->lmsg = NULL;
    if (v->langSel >= 0 && v->cur >= 0) { char f[120]; snprintf(f, sizeof f, "LayoutMsg/%s", v->lays[v->cur]->name); v->lmsg = LvMsgLoad(v, f); }
}
static void LvSetLang(LV *v, int sel) {
    for (int i = 0; i < v->nm; i++) MsbtFree(&v->mcache[i]);
    v->nm = 0; free(v->msgArc); v->msgArc = NULL; v->msgArcN = 0; v->lmsg = NULL;
    memset(&v->mctx, 0, sizeof v->mctx); v->mctx.load = LvMsgLoad; v->mctx.user = v; v->mctx.has = LvHasGlyph;
    v->langSel = sel >= 0 && sel < v->nlangs ? sel : -1;
    if (v->langSel >= 0) {
        HCURSOR oc = SetCursor(LoadCursor(NULL, IDC_WAIT));
        size_t n; unsigned char *b = ArcReadAll(v->langs[sel].path, &n);
        if (b) b = ArcUnzstd(b, n, &n);
        if (b) { v->msgArc = b; v->msgArcN = n; }
        SetCursor(oc);
        snprintf(g_lvLang, sizeof g_lvLang, "%s", v->langs[sel].code);
    } else snprintf(g_lvLang, sizeof g_lvLang, "-");
    if (v->gameFonts) LvLoadFonts(v);
    LvBindLayoutMsg(v);
    LvClearText(v);
    if (v->gl) InvalidateRect(v->gl, NULL, FALSE);
}
static void LvUpdateLangInfo(LV *v) {
    if (v->shownTotal == v->txtTotal && v->shownMatched == v->txtMatched) return;
    v->shownTotal = v->txtTotal; v->shownMatched = v->txtMatched;
    wchar_t t[400];
    if (!v->nlangs) swprintf(t, 400, L"No message archives (romfs\\Mals) found in this dump - showing the text stored in the layout.");
    else if (v->langSel < 0) swprintf(t, 400, L"Showing the text stored in the layout file (the developers' placeholders, mostly Japanese).");
    else if (!v->msgArc) swprintf(t, 400, L"That language's message archive couldn't be read.");
    else if (!v->lmsg) swprintf(t, 400, L"This screen has no messages of its own (the game fills its text in) - showing the layout's text.");
    else swprintf(t, 400, L"%d of %d text boxes shown in %hs; the rest are filled in by the game, so the layout's sample text is shown.",
                  v->txtMatched, v->txtTotal, LvLangName(v->langs[v->langSel].code));
    if (v->gameFonts && !v->gf.ok) {
        size_t l = wcslen(t);
        if (v->gf.err[0]) swprintf(t + l, 400 - l, L"  Game fonts not loaded: %hs", v->gf.err);
        else if (!v->nroots) swprintf(t + l, 400 - l, L"  Game fonts not found (no romfs\\Font folder).");
    }
    SetWindowTextW(v->langInfo, t);
}

/* ---------------- UI ---------------- */
static void LvSetFrameText(LV *v) {
    wchar_t t[80];
    if (v->haveAnim) swprintf(t, 80, L"frame %d / %d", (int)(v->frame + 0.5f), v->anim.frames); else swprintf(t, 80, L"(no animation)");
    SetWindowTextW(v->frameLbl, t);
}
static void LvFillPanes(LV *v) {
    TreeView_DeleteAllItems(v->panes);
    if (v->cur < 0) return;
    const Lyt *L = &v->lays[v->cur]->L;
    HTREEITEM *items = (HTREEITEM *)calloc(L->npane + 1, sizeof(HTREEITEM));
    static const wchar_t *kn[] = { L"", L"pic", L"text", L"window", L"part", L"bounds", L"align", L"scissor" };
    for (int i = 0; i < L->npane && items; i++) {
        const LytPane *p = &L->pane[i];
        wchar_t t[160]; swprintf(t, 160, L"%hs%ls%ls%ls%hs", p->name, p->kind ? L"  (" : L"", p->kind ? kn[p->kind] : L"", p->kind ? L")" : L"",
                                 p->kind == LP_PART ? p->part : "");
        TVINSERTSTRUCTW is = { 0 }; is.hParent = p->parent >= 0 && p->parent < i ? items[p->parent] : TVI_ROOT; is.hInsertAfter = TVI_LAST;
        is.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_STATE; is.item.pszText = t; is.item.lParam = i;
        is.item.stateMask = TVIS_STATEIMAGEMASK | TVIS_EXPANDED;
        is.item.state = INDEXTOSTATEIMAGEMASK(v->userHide && v->userHide[i] ? 1 : 2) | (p->parent < 0 || L->pane[p->parent].parent < 0 ? TVIS_EXPANDED : 0);
        items[i] = (HTREEITEM)SendMessageW(v->panes, TVM_INSERTITEMW, 0, (LPARAM)&is);
    }
    free(items);
}
static void LvSelectAnim(LV *v, int idx) {
    KillTimer(v->wnd, LV_TIMER); v->playing = 0; SetWindowTextW(v->play, L"Play");
    LanFree(&v->anim); v->haveAnim = 0; v->frame = 0;
    if (idx > 0) {
        wchar_t w[120]; SendMessageW(v->animCb, CB_GETLBTEXT, idx, (LPARAM)w);
        char nm[120], in[160]; WideCharToMultiByte(CP_UTF8, 0, w, -1, nm, sizeof nm, NULL, NULL);
        snprintf(in, sizeof in, "anim/%s.bflan", nm);
        size_t n; unsigned char *b = ArcSarcMember(v->arc, v->arcN, in, &n);
        if (b) { v->haveAnim = LanParse(&v->anim, b, n); free(b); }
    }
    SendMessageW(v->slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, v->haveAnim ? v->anim.frames : 0));
    SendMessageW(v->slider, TBM_SETPOS, TRUE, 0);
    EnableWindow(v->slider, v->haveAnim); EnableWindow(v->play, v->haveAnim);
    if (v->haveAnim) SendMessageW(v->loop, BM_SETCHECK, v->anim.loop ? BST_CHECKED : BST_UNCHECKED, 0);
    LvApplyAnim(v); LvSetFrameText(v); InvalidateRect(v->gl, NULL, FALSE);
}
static void LvSelectLayout(LV *v, const char *name) {
    KillTimer(v->wnd, LV_TIMER); v->playing = 0;
    int li = LvLayoutIdx(v, name);
    v->cur = li;
    LvClearText(v); LvBindLayoutMsg(v);
    free(v->wp); free(v->wm); free(v->wtex); free(v->userHide); v->wp = NULL; v->wm = NULL; v->wtex = NULL; v->userHide = NULL;
    wchar_t info[400];
    if (li < 0) { swprintf(info, 400, L"%hs couldn't be read.", name); SetWindowTextW(v->info, info); TreeView_DeleteAllItems(v->panes); InvalidateRect(v->gl, NULL, FALSE); return; }
    Lyt *L = &v->lays[li]->L;
    v->wp = (LytPane *)calloc(L->npane + 1, sizeof(LytPane)); v->wm = (LytMat *)calloc(L->nmat + 1, sizeof(LytMat));
    v->wtex = (char (*)[96])calloc(L->nmat + 1, 96); v->userHide = (unsigned char *)calloc(L->npane + 1, 1);
    swprintf(info, 400, L"%hs   %g \x00D7 %g   %d panes, %d materials, %d textures", L->name, L->w, L->h, L->npane, L->nmat, L->ntex);
    SetWindowTextW(v->info, info);
    // animations of this layout: <layout>_<name>
    SendMessageW(v->animCb, CB_RESETCONTENT, 0, 0);
    SendMessageW(v->animCb, CB_ADDSTRING, 0, (LPARAM)L"(none - default state)");
    size_t ln = strlen(name); int sel = 0;
    for (int i = 0; i < v->nanims; i++)
        if (!strncmp(v->anims[i], name, ln) && v->anims[i][ln] == '_') {
            wchar_t w[120]; MultiByteToWideChar(CP_UTF8, 0, v->anims[i], -1, w, 120);
            int k = (int)SendMessageW(v->animCb, CB_ADDSTRING, 0, (LPARAM)w);
            if (v->startAnim[0] && !strcmp(v->startAnim, v->anims[i])) sel = k;
        }
    v->startAnim[0] = 0;
    SendMessageW(v->animCb, CB_SETCURSEL, sel, 0);
    LvFillPanes(v);
    LvSelectAnim(v, sel);
    if (v->fitMode) LvFit(v);
    InvalidateRect(v->gl, NULL, FALSE);
}
static void LvLayoutCtl(LV *v) {
    RECT rc; GetClientRect(v->wnd, &rc);
    int m = Sx(v, 8), g = Sx(v, 6), lw = Sx(v, 330), rh = Sx(v, 26), x = m, y = m;
    MoveWindow(v->layout, x, y, lw, Sx(v, 400), TRUE); y += rh + g;
    MoveWindow(v->animCb, x, y, lw, Sx(v, 400), TRUE); y += rh + g;
    MoveWindow(v->langCb, x, y, lw, Sx(v, 400), TRUE); y += rh + g;
    MoveWindow(v->gfCheck, x, y, lw, rh, TRUE); y += rh + g;
    int pb = Sx(v, 70); MoveWindow(v->play, x, y, pb, rh, TRUE);
    MoveWindow(v->loop, x + pb + g, y, Sx(v, 60), rh, TRUE);
    MoveWindow(v->frameLbl, x + pb + g + Sx(v, 60) + g, y + Sx(v, 5), lw - pb - Sx(v, 60) - 2 * g, rh - Sx(v, 5), TRUE); y += rh + g / 2;
    MoveWindow(v->slider, x, y, lw, rh, TRUE); y += rh + g;
    MoveWindow(v->hidden, x, y, lw / 2, rh, TRUE); MoveWindow(v->outline, x + lw / 2, y, lw / 2, rh, TRUE); y += rh + g;
    MoveWindow(v->panes, x, y, lw, rc.bottom - y - m, TRUE);
    int rx = m + lw + m, b1 = Sx(v, 150), b2 = Sx(v, 110), b3 = Sx(v, 130);
    MoveWindow(v->bg, rx, m, b1, rh, TRUE); MoveWindow(v->fit, rx + b1 + g, m, b2, rh, TRUE); MoveWindow(v->save, rx + b1 + g + b2 + g, m, b3, rh, TRUE);
    MoveWindow(v->info, rx, m + rh + g, rc.right - rx - m, Sx(v, 20), TRUE);
    MoveWindow(v->langInfo, rx, m + rh + g + Sx(v, 20), rc.right - rx - m, Sx(v, 20), TRUE);
    MoveWindow(v->gl, rx, m + rh + g + Sx(v, 44), rc.right - rx - m, rc.bottom - (m + rh + g + Sx(v, 44)) - m, TRUE);
}
static void LvSavePng(LV *v) {
    if (!v->rc) return;
    RECT rc; GetClientRect(v->gl, &rc); int w = rc.right, h = rc.bottom;
    unsigned char *px = (unsigned char *)malloc((size_t)w * h * 4), *flip = (unsigned char *)malloc((size_t)w * h * 4);
    if (!px || !flip) { free(px); free(flip); return; }
    LvPaint(v); wglMakeCurrent(v->dc, v->rc);
    glReadBuffer(GL_FRONT); glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    for (int y = 0; y < h; y++) memcpy(flip + (size_t)y * w * 4, px + (size_t)(h - 1 - y) * w * 4, (size_t)w * 4);
    for (size_t i = 0; i < (size_t)w * h; i++) flip[i * 4 + 3] = 255;
    wchar_t path[MAX_PATH]; swprintf(path, MAX_PATH, L"%hs.png", v->cur >= 0 ? v->lays[v->cur]->name : "layout");
    OPENFILENAMEW of; ZeroMemory(&of, sizeof of);
    of.lStructSize = sizeof of; of.hwndOwner = v->wnd; of.lpstrFilter = L"PNG image (*.png)\0*.png\0"; of.lpstrFile = path; of.nMaxFile = MAX_PATH;
    of.lpstrDefExt = L"png"; of.lpstrTitle = L"Save the view as PNG"; of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&of)) {
        static int crc = 0; if (!crc) { CrcInit(); crc = 1; }
        size_t rawN = (size_t)(w * 4 + 1) * h; unsigned char *raw = (unsigned char *)malloc(rawN);
        int ok = 0;
        if (raw) {
            for (int y = 0; y < h; y++) { raw[(size_t)y * (w * 4 + 1)] = 0; memcpy(raw + (size_t)y * (w * 4 + 1) + 1, flip + (size_t)y * w * 4, (size_t)w * 4); }
            size_t dn = 0; unsigned char *def = DeflateBuf(raw, rawN, &dn);
            uint32_t a1 = 1, a2 = 0; for (size_t i = 0; i < rawN; i++) { a1 = (a1 + raw[i]) % 65521; a2 = (a2 + a1) % 65521; }
            FILE *f = def ? _wfopen(path, L"wb") : NULL;
            if (f) {
                #define P32(x) { unsigned char q_[4] = { (unsigned char)((x) >> 24), (unsigned char)((x) >> 16), (unsigned char)((x) >> 8), (unsigned char)(x) }; fwrite(q_, 1, 4, f); }
                static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 }; fwrite(sig, 1, 8, f);
                unsigned char ih[17] = { 'I','H','D','R', (unsigned char)(w >> 24), (unsigned char)(w >> 16), (unsigned char)(w >> 8), (unsigned char)w,
                                         (unsigned char)(h >> 24), (unsigned char)(h >> 16), (unsigned char)(h >> 8), (unsigned char)h, 8, 6, 0, 0, 0 };
                P32(13); fwrite(ih, 1, 17, f); P32(Crc32(ih, 17));
                unsigned char *z = (unsigned char *)malloc(dn + 10);
                if (z) {
                    memcpy(z, "IDAT", 4); z[4] = 0x78; z[5] = 0x01; memcpy(z + 6, def, dn);
                    z[6 + dn] = (unsigned char)(a2 >> 8); z[7 + dn] = (unsigned char)a2; z[8 + dn] = (unsigned char)(a1 >> 8); z[9 + dn] = (unsigned char)a1;
                    P32((uint32_t)(dn + 6)); fwrite(z, 1, dn + 10, f); P32(Crc32(z, dn + 10)); free(z);
                    unsigned char ie[4] = { 'I','E','N','D' }; P32(0); fwrite(ie, 1, 4, f); P32(Crc32(ie, 4));
                    ok = !ferror(f);
                }
                #undef P32
                fclose(f);
            }
            xfree(def); free(raw);
        }
        if (!ok) MessageBoxW(v->wnd, L"Saving the PNG failed.", L"Layout", MB_ICONWARNING);
    }
    free(px); free(flip);
}

static LRESULT CALLBACK LvGlProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    LV *v = (LV *)GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA);
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hwnd, &ps); if (v) { LvPaint(v); LvUpdateLangInfo(v); } EndPaint(hwnd, &ps); return 0; }
    case WM_SIZE: if (v && v->fitMode) LvFit(v); InvalidateRect(hwnd, NULL, FALSE); return 0;
    case WM_MOUSEWHEEL: if (v) {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) }; ScreenToClient(hwnd, &pt);
        RECT rc; GetClientRect(hwnd, &rc);
        double f = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1.2 : 1 / 1.2, nz = v->zoom * f;
        if (nz < 0.02) nz = 0.02; if (nz > 40) nz = 40; f = nz / v->zoom;
        double cx = pt.x - rc.right / 2.0, cy = pt.y - rc.bottom / 2.0;
        v->panX = cx - (cx - v->panX) * f; v->panY = cy - (cy - v->panY) * f; v->zoom = nz;
        v->fitMode = 0; SendMessageW(v->fit, BM_SETCHECK, BST_UNCHECKED, 0);
        InvalidateRect(hwnd, NULL, FALSE); return 0;
    } break;
    case WM_LBUTTONDOWN: if (v) { SetFocus(hwnd); SetCapture(hwnd); v->dragging = 1; v->dragFrom.x = (short)LOWORD(lp); v->dragFrom.y = (short)HIWORD(lp); v->panFrom[0] = v->panX; v->panFrom[1] = v->panY; } return 0;
    case WM_MOUSEMOVE: if (v && v->dragging) { v->panX = v->panFrom[0] + ((short)LOWORD(lp) - v->dragFrom.x); v->panY = v->panFrom[1] + ((short)HIWORD(lp) - v->dragFrom.y); InvalidateRect(hwnd, NULL, FALSE); } return 0;
    case WM_LBUTTONUP: if (v && v->dragging) { v->dragging = 0; ReleaseCapture(); } return 0;
    case WM_LBUTTONDBLCLK: if (v) { v->fitMode = 1; SendMessageW(v->fit, BM_SETCHECK, BST_CHECKED, 0); LvFit(v); InvalidateRect(hwnd, NULL, FALSE); } return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK LvProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    LV *v = (LV *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        v = (LV *)((CREATESTRUCTW *)lp)->lpCreateParams; SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)v); v->wnd = hwnd;
        HINSTANCE in = GetModuleHandleW(NULL);
        #define MK(cls, txt, st, ex, id) CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | (st), 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)(id), in, NULL)
        v->layout = MK(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, 0, LV_LAYOUT);
        v->animCb = MK(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, 0, LV_ANIM);
        v->play = MK(L"BUTTON", L"Play", BS_PUSHBUTTON | WS_TABSTOP, 0, LV_PLAY);
        v->loop = MK(L"BUTTON", L"Loop", BS_AUTOCHECKBOX | WS_TABSTOP, 0, LV_LOOP);
        v->frameLbl = MK(L"STATIC", L"", 0, 0, LV_FRAME);
        v->slider = MK(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, LV_SLIDER);
        v->hidden = MK(L"BUTTON", L"Show hidden panes", BS_AUTOCHECKBOX | WS_TABSTOP, 0, LV_HIDDEN);
        v->outline = MK(L"BUTTON", L"Outlines", BS_AUTOCHECKBOX | WS_TABSTOP, 0, LV_OUTLINE);
        v->panes = MK(WC_TREEVIEWW, L"", TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_CHECKBOXES | TVS_SHOWSELALWAYS | WS_TABSTOP, WS_EX_CLIENTEDGE, LV_PANES);
        v->bg = MK(L"BUTTON", L"Background: slate", BS_PUSHBUTTON | WS_TABSTOP, 0, LV_BG);
        v->fit = MK(L"BUTTON", L"Fit to window", BS_AUTOCHECKBOX | WS_TABSTOP, 0, LV_FIT);
        v->save = MK(L"BUTTON", L"Save view as PNG...", BS_PUSHBUTTON | WS_TABSTOP, 0, LV_SAVE);
        v->info = MK(L"STATIC", L"Loading...", SS_ENDELLIPSIS, 0, LV_INFO);
        v->langCb = MK(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, 0, LV_LANG);
        v->langInfo = MK(L"STATIC", L"", SS_ENDELLIPSIS, 0, LV_LANGINFO);
        v->gfCheck = MK(L"BUTTON", L"Game fonts", BS_AUTOCHECKBOX | WS_TABSTOP, 0, LV_GFONTS);
        v->gl = MK(LV_GL_CLASS, L"", WS_CLIPSIBLINGS | WS_CLIPCHILDREN, WS_EX_CLIENTEDGE, LV_GL);
        #undef MK
        HWND kids[] = { v->layout, v->animCb, v->play, v->loop, v->frameLbl, v->hidden, v->outline, v->panes, v->bg, v->fit, v->save, v->info, v->langCb, v->langInfo, v->gfCheck };
        for (size_t i = 0; i < sizeof kids / sizeof kids[0]; i++) SendMessageW(kids[i], WM_SETFONT, (WPARAM)v->font, TRUE);
        SendMessageW(v->fit, BM_SETCHECK, BST_CHECKED, 0); v->fitMode = 1;
        EnableWindow(v->play, FALSE); EnableWindow(v->slider, FALSE);
        // GL context on the drawing child
        v->dc = GetDC(v->gl);
        PIXELFORMATDESCRIPTOR pfd; ZeroMemory(&pfd, sizeof pfd);
        pfd.nSize = sizeof pfd; pfd.nVersion = 1; pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cAlphaBits = 8;
        int pf = ChoosePixelFormat(v->dc, &pfd);
        if (pf && SetPixelFormat(v->dc, pf, &pfd)) { v->rc = wglCreateContext(v->dc); if (v->rc) wglMakeCurrent(v->dc, v->rc); }
        // languages: the text of every text box from the game's message archives
        v->langSel = -1; v->gameFonts = 1; LvFindLangs(v);
        SendMessageW(v->gfCheck, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(v->langCb, CB_ADDSTRING, 0, (LPARAM)L"Text: as stored in the layout (placeholders)");
        int want = -1;
        for (int i = 0; i < v->nlangs; i++) {
            wchar_t w[120]; swprintf(w, 120, L"Text: %hs  (%hs)", LvLangName(v->langs[i].code), v->langs[i].code);
            SendMessageW(v->langCb, CB_ADDSTRING, 0, (LPARAM)w);
            if (!strcmp(v->langs[i].code, g_lvLang)) want = i;
        }
        if (want < 0 && strcmp(g_lvLang, "-")) for (int i = 0; i < v->nlangs; i++) if (!strcmp(v->langs[i].code, "EUen") || !strcmp(v->langs[i].code, "USen")) want = i;
        SendMessageW(v->langCb, CB_SETCURSEL, want + 1, 0);
        EnableWindow(v->langCb, v->nlangs > 0);
        LvSetLang(v, want);
        LvLayoutCtl(v);
        HANDLE t = CreateThread(NULL, 0, LvLoadThread, v, 0, NULL); if (t) CloseHandle(t);
        return 0;
    }
    case WM_SIZE: if (v) LvLayoutCtl(v); return 0;
    case WM_GETMINMAXINFO: if (v) { ((MINMAXINFO *)lp)->ptMinTrackSize.x = Sx(v, 800); ((MINMAXINFO *)lp)->ptMinTrackSize.y = Sx(v, 480); } return 0;
    case WM_LV_LOADED: {
        if (!lp) { wchar_t e[400]; MultiByteToWideChar(CP_UTF8, 0, v->err, -1, e, 400); SetWindowTextW(v->info, e); return 0; }
        v->loaded = 1;
        // the layout to show first: the one named in inner (blyt/X.bflyt), or the layout an animation belongs to
        char want[96] = "";
        if (!strncmp(v->inner, "blyt/", 5)) { snprintf(want, sizeof want, "%s", v->inner + 5); char *d = strstr(want, ".bflyt"); if (d) *d = 0; }
        else if (!strncmp(v->inner, "anim/", 5)) {
            char an[96]; snprintf(an, sizeof an, "%s", v->inner + 5); char *d = strstr(an, ".bflan"); if (d) *d = 0;
            snprintf(v->startAnim, sizeof v->startAnim, "%s", an);
            size_t best = 0;
            for (int i = 0; i < v->nblyt; i++) { size_t l = strlen(v->blyt[i]); if (l > best && !strncmp(an, v->blyt[i], l) && an[l] == '_') { best = l; snprintf(want, sizeof want, "%s", v->blyt[i]); } }
        }
        SendMessageW(v->layout, CB_RESETCONTENT, 0, 0);
        int sel = 0;
        for (int i = 0; i < v->nblyt; i++) {
            wchar_t w[80]; MultiByteToWideChar(CP_UTF8, 0, v->blyt[i], -1, w, 80);
            SendMessageW(v->layout, CB_ADDSTRING, 0, (LPARAM)w);
            if (!strcmp(v->blyt[i], want)) sel = i;
        }
        if (!v->inner[0]) { snprintf(want, sizeof want, "layout"); SendMessageW(v->layout, CB_ADDSTRING, 0, (LPARAM)L"layout"); }
        SendMessageW(v->layout, CB_SETCURSEL, sel, 0);
        if (v->nblyt) LvSelectLayout(v, v->blyt[sel]); else LvSelectLayout(v, want);
        return 0;
    }
    case WM_TIMER:
        if (wp == LV_TIMER && v->haveAnim) {
            v->frame += 1;
            if (v->frame > v->anim.frames) {
                if (SendMessageW(v->loop, BM_GETCHECK, 0, 0) == BST_CHECKED) v->frame = 0;
                else { v->frame = (float)v->anim.frames; KillTimer(hwnd, LV_TIMER); v->playing = 0; SetWindowTextW(v->play, L"Play"); }
            }
            SendMessageW(v->slider, TBM_SETPOS, TRUE, (LPARAM)(int)v->frame);
            LvApplyAnim(v); LvSetFrameText(v); InvalidateRect(v->gl, NULL, FALSE);
        }
        return 0;
    case WM_HSCROLL:
        if ((HWND)lp == v->slider) { v->frame = (float)SendMessageW(v->slider, TBM_GETPOS, 0, 0); LvApplyAnim(v); LvSetFrameText(v); InvalidateRect(v->gl, NULL, FALSE); }
        return 0;
    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)lp;
        if (nh->idFrom == LV_PANES && (nh->code == NM_CLICK || nh->code == TVN_KEYDOWN)) {
            // the check state changes after the click; read it a moment later
            PostMessageW(hwnd, WM_APP + 51, 0, 0);
        }
        break;
    }
    case WM_APP + 51: {
        if (v->cur < 0 || !v->userHide) return 0;
        const Lyt *L = &v->lays[v->cur]->L;
        // walk the tree reading the checkboxes
        HTREEITEM it = TreeView_GetRoot(v->panes);
        while (it) {
            TVITEMW ti = { 0 }; ti.mask = TVIF_PARAM | TVIF_STATE; ti.hItem = it; ti.stateMask = TVIS_STATEIMAGEMASK;
            SendMessageW(v->panes, TVM_GETITEMW, 0, (LPARAM)&ti);
            if (ti.lParam >= 0 && ti.lParam < L->npane) v->userHide[ti.lParam] = ((ti.state >> 12) & 0xF) == 1;
            HTREEITEM nx = TreeView_GetChild(v->panes, it);
            if (!nx) { while (it && !(nx = TreeView_GetNextSibling(v->panes, it))) it = TreeView_GetParent(v->panes, it); }
            it = nx;
        }
        InvalidateRect(v->gl, NULL, FALSE);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case LV_LAYOUT: if (HIWORD(wp) == CBN_SELCHANGE) { int i = (int)SendMessageW(v->layout, CB_GETCURSEL, 0, 0); if (i >= 0 && i < v->nblyt) LvSelectLayout(v, v->blyt[i]); } return 0;
        case LV_ANIM: if (HIWORD(wp) == CBN_SELCHANGE) LvSelectAnim(v, (int)SendMessageW(v->animCb, CB_GETCURSEL, 0, 0)); return 0;
        case LV_PLAY:
            if (!v->haveAnim) return 0;
            if (v->playing) { KillTimer(hwnd, LV_TIMER); v->playing = 0; SetWindowTextW(v->play, L"Play"); }
            else { if (v->frame >= v->anim.frames) v->frame = 0; SetTimer(hwnd, LV_TIMER, 16, NULL); v->playing = 1; SetWindowTextW(v->play, L"Pause"); }
            return 0;
        case LV_HIDDEN: v->showHidden = SendMessageW(v->hidden, BM_GETCHECK, 0, 0) == BST_CHECKED; InvalidateRect(v->gl, NULL, FALSE); return 0;
        case LV_OUTLINE: v->outlines = SendMessageW(v->outline, BM_GETCHECK, 0, 0) == BST_CHECKED; InvalidateRect(v->gl, NULL, FALSE); return 0;
        case LV_BG: {
            static const wchar_t *bn[4] = { L"Background: slate", L"Background: black", L"Background: white", L"Background: green" };
            v->bgMode = (v->bgMode + 1) % 4; SetWindowTextW(v->bg, bn[v->bgMode]); InvalidateRect(v->gl, NULL, FALSE); return 0;
        }
        case LV_FIT: v->fitMode = SendMessageW(v->fit, BM_GETCHECK, 0, 0) == BST_CHECKED; if (v->fitMode) LvFit(v); InvalidateRect(v->gl, NULL, FALSE); return 0;
        case LV_SAVE: LvSavePng(v); return 0;
        case LV_GFONTS: {
            v->gameFonts = SendMessageW(v->gfCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (v->gameFonts) LvLoadFonts(v);
            v->mctx.puaReady = 0; v->mctx.npua = 0;
            LvClearText(v); InvalidateRect(v->gl, NULL, FALSE); return 0;
        }
        case LV_LANG: if (HIWORD(wp) == CBN_SELCHANGE) LvSetLang(v, (int)SendMessageW(v->langCb, CB_GETCURSEL, 0, 0) - 1); return 0;
        }
        break;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        KillTimer(hwnd, LV_TIMER);
        if (v && v->rc) {
            wglMakeCurrent(v->dc, v->rc);
            for (int i = 0; i < v->ntex; i++) if (v->tex[i].id) glDeleteTextures(1, &v->tex[i].id);
            for (int i = 0; i < v->ntxt; i++) if (v->txt[i].id) glDeleteTextures(1, &v->txt[i].id);
            wglMakeCurrent(NULL, NULL); wglDeleteContext(v->rc); v->rc = NULL;
        }
        if (v && v->dc) { ReleaseDC(v->gl, v->dc); v->dc = NULL; }
        return 0;
    case WM_NCDESTROY:
        if (v) {
            for (int i = 0; i < v->nlays; i++) { LytFree(&v->lays[i]->L); free(v->lays[i]); }
            free(v->lays); LanFree(&v->anim); TexClose(&v->tf);
            free(v->arc); free(v->bntx); free(v->blyt); free(v->anims); free(v->tex); free(v->txt);
            free(v->wp); free(v->wm); free(v->wtex); free(v->userHide);
            for (int i = 0; i < v->nm; i++) MsbtFree(&v->mcache[i]);
            free(v->msgArc); free(v->langs); free(v->mo.s); GfFree(&v->gf); free(v);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// dumpFile: the archive (or a loose .bflyt) in the game dump; inner: "blyt/X.bflyt" or "anim/X.bflan" inside it (UTF-8), or NULL
void LytViewOpen(HWND owner, const wchar_t *dumpFile, const char *inner, const wchar_t *title, HFONT font, int dpi) {
    static int reg = 0;
    HINSTANCE in = GetModuleHandleW(NULL);
    if (!reg) {
        WNDCLASSEXW wc = { 0 }; wc.cbSize = sizeof wc; wc.lpfnWndProc = LvProc; wc.hInstance = in; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = LV_CLASS;
        wc.hIcon = (HICON)SendMessageW(owner, WM_GETICON, ICON_BIG, 0); wc.hIconSm = (HICON)SendMessageW(owner, WM_GETICON, ICON_SMALL, 0);
        RegisterClassExW(&wc);
        WNDCLASSEXW gc = { 0 }; gc.cbSize = sizeof gc; gc.lpfnWndProc = LvGlProc; gc.hInstance = in; gc.hCursor = LoadCursor(NULL, IDC_HAND);
        gc.style = CS_OWNDC | CS_DBLCLKS; gc.lpszClassName = LV_GL_CLASS; RegisterClassExW(&gc);
        reg = 1;
    }
    LV *v = (LV *)calloc(1, sizeof(LV));
    if (!v) return;
    v->font = font; v->dpi = dpi ? dpi : 96; v->cur = -1; v->zoom = 1;
    swprintf(v->path, 2048, L"%ls", dumpFile); swprintf(v->title, 600, L"Layout - %ls", title);
    if (inner) snprintf(v->inner, sizeof v->inner, "%s", inner);
    int w = MulDiv(1400, v->dpi, 96), h = MulDiv(860, v->dpi, 96);
    HWND wnd = CreateWindowExW(0, LV_CLASS, v->title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, w, h, NULL, NULL, in, v);
    if (!wnd) { free(v); return; }
    if (wnd) UiSkinWindow(wnd);                 // the main window's dark look
    ShowWindow(wnd, SW_SHOW);
}

/* ---------------- preview for the main window ---------------- */
// the layout drawn (default state, the layout's own text) by the viewer's renderer into a hidden window, read
// back and scaled to fit boxW x boxH
static LRESULT CALLBACK LvThumbWndProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
unsigned char *LytThumbnail(const wchar_t *dumpFile, const char *inner, int boxW, int boxH, int *ow, int *oh, wchar_t *note, int noteN) {
    note[0] = 0;
    static LONG reg = 0;
    HINSTANCE in = GetModuleHandleW(NULL);
    if (!InterlockedExchange(&reg, 1)) {
        WNDCLASSEXW wc = { 0 }; wc.cbSize = sizeof wc; wc.style = CS_OWNDC; wc.lpfnWndProc = LvThumbWndProc; wc.hInstance = in; wc.lpszClassName = L"ns2digLytThumb";
        RegisterClassExW(&wc);
    }
    LV *v = (LV *)calloc(1, sizeof(LV)); if (!v) return NULL;
    v->dpi = 96; v->cur = -1; v->fitMode = 1; v->zoom = 1; v->langSel = -1;
    swprintf(v->path, 2048, L"%ls", dumpFile);
    if (inner) snprintf(v->inner, sizeof v->inner, "%s", inner);
    unsigned char *out = NULL;
    size_t n; unsigned char *b = ArcReadAll(v->path, &n);
    if (b) b = ArcUnzstd(b, n, &n);
    if (b) {
        v->arc = b; v->arcN = n;
        if (v->inner[0]) {
            LvScan s = { v }; ArcSarcEach(b, n, LvScanCb, &s);
            if (v->blyt) qsort(v->blyt, v->nblyt, 64, CmpName64);
            if (v->anims) qsort(v->anims, v->nanims, 96, CmpName96);
            if (v->bntx) TexOpen(&v->tf, v->bntx, v->bntxN);
        }
        char want[96] = "layout";
        if (!strncmp(v->inner, "blyt/", 5)) { snprintf(want, sizeof want, "%s", v->inner + 5); char *d = strstr(want, ".bflyt"); if (d) *d = 0; }
        else if (!strncmp(v->inner, "anim/", 5)) {
            char an[96]; snprintf(an, sizeof an, "%s", v->inner + 5); char *d = strstr(an, ".bflan"); if (d) *d = 0;
            size_t best = 0;
            for (int i = 0; i < v->nblyt; i++) { size_t l = strlen(v->blyt[i]); if (l > best && !strncmp(an, v->blyt[i], l) && an[l] == '_') { best = l; snprintf(want, sizeof want, "%s", v->blyt[i]); } }
        } else if (v->nblyt) snprintf(want, sizeof want, "%s", v->blyt[0]);
        int li = LvLayoutIdx(v, want);
        if (li >= 0) {
            const Lyt *L = &v->lays[li]->L;
            int W = 512, H = L->w > 0 && L->h > 0 ? (int)(512 * L->h / L->w) : 288; if (H < 64) H = 64; if (H > 512) H = 512;
            v->gl = CreateWindowExW(WS_EX_TOOLWINDOW, L"ns2digLytThumb", L"", WS_POPUP, -32000, -32000, W, H, NULL, NULL, in, NULL);
            if (v->gl) {
                v->dc = GetDC(v->gl);
                PIXELFORMATDESCRIPTOR pfd; ZeroMemory(&pfd, sizeof pfd);
                pfd.nSize = sizeof pfd; pfd.nVersion = 1; pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
                pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cAlphaBits = 8;
                int pf = ChoosePixelFormat(v->dc, &pfd);
                if (pf && SetPixelFormat(v->dc, pf, &pfd)) v->rc = wglCreateContext(v->dc);
                if (v->rc && wglMakeCurrent(v->dc, v->rc)) {
                    v->cur = li;
                    v->wp = (LytPane *)calloc(L->npane + 1, sizeof(LytPane)); v->wm = (LytMat *)calloc(L->nmat + 1, sizeof(LytMat));
                    v->wtex = (char (*)[96])calloc(L->nmat + 1, 96); v->userHide = (unsigned char *)calloc(L->npane + 1, 1);
                    LvSelectAnim(v, 0);                     // the default state into the working copies
                    LvFit(v);
                    v->noSwap = 1; LvPaint(v);               // draws into the back buffer
                    unsigned char *px = (unsigned char *)malloc((size_t)W * H * 4);
                    if (px) {
                        glReadBuffer(GL_BACK); glFinish();
                        glReadPixels(0, 0, W, H, GL_BGRA_EXT, GL_UNSIGNED_BYTE, px);
                        // GL rows are bottom-up; make it top-down and opaque
                        unsigned char *row = (unsigned char *)malloc((size_t)W * 4);
                        if (row) for (int y = 0; y < H / 2; y++) {
                            memcpy(row, px + (size_t)y * W * 4, (size_t)W * 4);
                            memcpy(px + (size_t)y * W * 4, px + (size_t)(H - 1 - y) * W * 4, (size_t)W * 4);
                            memcpy(px + (size_t)(H - 1 - y) * W * 4, row, (size_t)W * 4);
                        }
                        free(row);
                        int any = 0; for (int i = 0; i < W * H && !any; i++) any = px[i * 4] | px[i * 4 + 1] | px[i * 4 + 2];
                        for (int i = 0; i < W * H; i++) px[i * 4 + 3] = 255;
                        if (any) out = UiScaleToBox(px, W, H, boxW, boxH, ow, oh);      // nothing read back: keep the icon
                        free(px);
                    }
                    swprintf(note, noteN, L"%hs - %g x %g", L->name, L->w, L->h);
                    for (int i = 0; i < v->ntex; i++) if (v->tex[i].id) glDeleteTextures(1, &v->tex[i].id);
                    for (int i = 0; i < v->ntxt; i++) if (v->txt[i].id) glDeleteTextures(1, &v->txt[i].id);
                    wglMakeCurrent(NULL, NULL);
                }
                if (v->rc) wglDeleteContext(v->rc);
                ReleaseDC(v->gl, v->dc);
                DestroyWindow(v->gl);
            }
        }
    }
    for (int i = 0; i < v->nlays; i++) { LytFree(&v->lays[i]->L); free(v->lays[i]); }
    free(v->lays); LanFree(&v->anim); TexClose(&v->tf);
    free(v->arc); free(v->bntx); free(v->blyt); free(v->anims); free(v->tex); free(v->txt);
    free(v->wp); free(v->wm); free(v->wtex); free(v->userHide);
    for (int i = 0; i < v->nm; i++) MsbtFree(&v->mcache[i]);
    free(v->msgArc); free(v->langs); free(v->mo.s); GfFree(&v->gf); free(v);
    return out;
}
