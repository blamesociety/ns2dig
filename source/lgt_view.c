/* Lighting viewer: the environment settings of one level / mini-game / menu (romfs\Env\X.genvb.zs) -
   sun and sky, fog, lights, bloom, depth of field, colour correction, shadows. Lists the files in the archive,
   shows each one's settings with colour swatches, compares a file with another (e.g. a cutscene's settings with
   the level's default) and draws a rough preview of the sky, sun, ground and fog for the chosen setup. */
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include "ui_theme.h"
void UiSkinWindow(HWND top);
#include <commctrl.h>
#include <stdio.h>
#include <stdint.h>
#include <wchar.h>
#include <math.h>
#include "arc_util.h"
#include "aamp_core.h"

void TexViewOpen(HWND owner, const wchar_t *dumpFile, const char *inner, const wchar_t *title, HFONT font, int dpi);

#define LG_CLASS L"AlsarcLightView"
#define LG_PREV_CLASS L"AlsarcLightPreview"
enum { LG_FILES = 100, LG_PARAMS, LG_CMP, LG_CMPLBL, LG_DIFF, LG_UNNAMED, LG_PREVIEW, LG_INFO, LG_TEX, LG_FILESLBL, LG_TEX2 };

typedef struct { char name[96]; AmFile f; } LgFile;
typedef struct {
    HWND wnd, files, params, cmp, cmpLbl, diff, unnamed, preview, info, tex, tex2, filesLbl;
    HFONT font, bold; int dpi;
    wchar_t title[600], dump[2048];
    LgFile *fl; int nf;
    int sel, cmpSel;                 // file shown; file compared with (-1 none)
    int cmpList[256], ncmpList;      // combo entry -> file
    int *rowA, *rowB, nrows;         // list row -> setting in sel / in cmp (-1 = not there)
    char texInner[128];              // collect.genvres, if the archive has one
    char flareInner[128];            // default/default.bofx (lens flare textures + settings), if there is one
} LG;

static int LgS(LG *v, int x) { return MulDiv(x, v->dpi, 96); }

/* ---------------- file kinds ---------------- */
static const struct { const char *ptype, *what; } LgKinds[] = {
    { "projgfxbase", "Sun, moon, stars, fog, sky, wind" }, { "aglenv", "Lights (ambient, sun, sky/ground)" },
    { "aglsky", "Sky" }, { "aglblm", "Bloom (glow)" }, { "agldof", "Depth of field (blur)" },
    { "aglccr", "Colour correction" }, { "aglshpp", "Shadows" }, { "ssprojgfxbase", "Screen-space extras" },
    { "aglenvset", "Light presets" }, { "aglcube", "Reflection cubemaps (capture points)" }, { "aglfila", "Anti-aliasing (FXAA)" }, { "aglofx", "Lens flare (sun)" }, { "gsdw", "Shadow light, projected shadow, SSAO" }, { "aglvolm", "Light shafts (volumetric light)" }, { "aglatex", "Auto exposure (eye adaptation)" }, { "agllmap", "Light maps (shading ramps)" }, { "genv", "List of files" },
};
static int LgKindIdx(const char *pt) { for (int i = 0; i < (int)(sizeof LgKinds / sizeof LgKinds[0]); i++) if (!strcmp(LgKinds[i].ptype, pt)) return i; return 99; }
static const char *LgWhat(const char *pt) { int k = LgKindIdx(pt); return k < 99 ? LgKinds[k].what : pt; }
static void LgBase(const char *name, char *out, size_t n) { snprintf(out, n, "%s", name); char *d = strchr(out, '.'); if (d) *d = 0; }

static int LgFileCmp(const void *a, const void *b) {
    const LgFile *x = (const LgFile *)a, *y = (const LgFile *)b;
    int kx = LgKindIdx(x->f.ptype), ky = LgKindIdx(y->f.ptype);
    if (kx != ky) return kx - ky;
    int dx = !strncmp(x->name, "default.", 8), dy = !strncmp(y->name, "default.", 8);
    if (dx != dy) return dy - dx;
    return strcmp(x->name, y->name);
}

/* ---------------- loading ---------------- */
typedef struct { LG *v; } LgLoadCtx;
static int LgEach(const char *name, const unsigned char *d, size_t n, void *u) {
    LG *v = ((LgLoadCtx *)u)->v;
    size_t ln = strlen(name);
    if (n >= 0x100 && !memcmp(d, "FRES", 4) && ln > 5 && !_stricmp(name + ln - 5, ".bofx")) {
        // lens flare: a BFRES whose embedded files are the flare textures and an AAMP (aglofx)
        snprintf(v->flareInner, sizeof v->flareInner, "%s", name);
        uint64_t arr; uint16_t cnt; memcpy(&arr, d + 0xB8, 8); memcpy(&cnt, d + 0xEC, 2);
        for (int i = 0; i < cnt && arr && arr + (uint64_t)i * 16 + 12 <= n; i++) {
            uint64_t eo; uint32_t es; memcpy(&eo, d + arr + i * 16, 8); memcpy(&es, d + arr + i * 16 + 8, 4);
            if (eo + es > n || es < 0x30 || memcmp(d + eo, "AAMP", 4)) continue;
            const char *sl = strrchr(name, '/');
            LgEach(sl ? sl + 1 : name, d + eo, es, u);      // listed as "default.bofx"
        }
        return 0;
    }
    if (n >= 4 && !memcmp(d, "FRES", 4)) { snprintf(v->texInner, sizeof v->texInner, "%s", name); return 0; }
    if (n < 4 || memcmp(d, "AAMP", 4)) return 0;
    LgFile *q = (LgFile *)realloc(v->fl, (size_t)(v->nf + 1) * sizeof(LgFile)); if (!q) return 1;
    v->fl = q; LgFile *f = &v->fl[v->nf];
    memset(f, 0, sizeof *f); snprintf(f->name, sizeof f->name, "%s", name);
    if (AmParse(d, n, &f->f)) v->nf++; else AmFree(&f->f);
    return 0;
}
static int LgLoad(LG *v, const wchar_t *dump, char *err, size_t en) {
    size_t n; unsigned char *b = ArcReadAll(dump, &n);
    if (!b) { snprintf(err, en, "The archive couldn't be read from the game dump."); return 0; }
    b = ArcUnzstd(b, n, &n);
    if (!b) { snprintf(err, en, "The archive couldn't be decompressed."); return 0; }
    LgLoadCtx c = { v };
    int k = ArcSarcEach(b, n, LgEach, &c);
    free(b);
    if (!k) { snprintf(err, en, "This isn't an environment archive (SARC)."); return 0; }
    if (!v->nf) { snprintf(err, en, "No lighting settings were found inside."); return 0; }
    qsort(v->fl, v->nf, sizeof(LgFile), LgFileCmp);
    return 1;
}
static int LgFindFile(LG *v, const char *name) { for (int i = 0; i < v->nf; i++) if (!_stricmp(v->fl[i].name, name)) return i; return -1; }
// the file of a kind for a setup: "<base>.<ext>" if there is one, else "default.<ext>"
static const AmFile *LgPick(LG *v, const char *base, const char *ptype) {
    int def = -1;
    for (int i = 0; i < v->nf; i++) {
        if (strcmp(v->fl[i].f.ptype, ptype)) continue;
        char b2[96]; LgBase(v->fl[i].name, b2, sizeof b2);
        if (!_stricmp(b2, base)) return &v->fl[i].f;
        if (!_stricmp(b2, "default")) def = i;
    }
    return def >= 0 ? &v->fl[def].f : NULL;
}
static const char *LgPickName(LG *v, const AmFile *f) { for (int i = 0; i < v->nf; i++) if (&v->fl[i].f == f) return v->fl[i].name; return "?"; }

/* ---------------- values ---------------- */
static void LgFmt(const AmParam *p, wchar_t *out, size_t n) {
    switch (p->type) {
    case AM_BOOL: swprintf(out, n, p->f[0] ? L"yes" : L"no"); return;
    case AM_INT: case AM_U32: swprintf(out, n, L"%.0f", p->f[0]); return;
    case AM_F32: swprintf(out, n, L"%g", p->f[0]); return;
    case AM_VEC2: case AM_VEC3: case AM_VEC4: case AM_QUAT: case AM_COLOR: {
        size_t at = 0; out[0] = 0;
        for (int i = 0; i < p->nf && at < n - 20; i++) at += swprintf(out + at, n - at, i ? L",  %g" : L"%g", p->f[i]);
        if (p->type == AM_COLOR) {
            float m = fmaxf(p->f[0], fmaxf(p->f[1], p->f[2]));
            if (m > 1.0f && at < n - 32) swprintf(out + at, n - at, L"   (%g\x00D7 brighter than white)", m);
        }
        return;
    }
    default: swprintf(out, n, L"%hs", p->text); return;
    }
}
static int LgSame(const AmParam *a, const AmParam *b) {
    if (!a || !b || a->type != b->type || a->nf != b->nf) return 0;
    for (int i = 0; i < a->nf; i++) if (a->f[i] != b->f[i]) return 0;
    return !strcmp(a->text, b->text);
}
// a colour to show: colours, and 3/4-number settings whose name says colour (SunLightColor is stored as 3 numbers)
static int LgIsColour(const AmParam *p) {
    if (!p) return 0;
    if (p->type == AM_COLOR) return 1;
    return (p->type == AM_VEC3 || p->type == AM_VEC4) && (strstr(p->name, "Color") || strstr(p->name, "color"));
}
static COLORREF LgRgb(const float *f, int hdrScale) {   // HDR colours: scaled down to show their hue
    float r = f[0], g = f[1], b = f[2], m = fmaxf(r, fmaxf(g, b));
    if (hdrScale && m > 1.0f) { r /= m; g /= m; b /= m; }
    #define C255(x) (BYTE)((x) <= 0 ? 0 : (x) >= 1 ? 255 : (int)((x) * 255 + 0.5f))
    return RGB(C255(r), C255(g), C255(b));
}

/* ---------------- settings list ---------------- */
static int LgFindParam(const AmFile *f, const AmParam *p) {
    for (int i = 0; i < f->np; i++) if (!strcmp(f->p[i].obj, p->obj) && !strcmp(f->p[i].name, p->name)) return i;
    return -1;
}
static void LgFillParams(LG *v) {
    SendMessageW(v->params, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(v->params);
    free(v->rowA); free(v->rowB); v->rowA = v->rowB = NULL; v->nrows = 0;
    if (v->sel < 0) { SendMessageW(v->params, WM_SETREDRAW, TRUE, 0); return; }
    const AmFile *A = &v->fl[v->sel].f, *B = v->cmpSel >= 0 ? &v->fl[v->cmpSel].f : NULL;
    int cap = A->np + (B ? B->np : 0) + 1;
    v->rowA = (int *)malloc(cap * sizeof(int)); v->rowB = (int *)malloc(cap * sizeof(int));
    if (!v->rowA || !v->rowB) return;
    int diffOnly = B && SendMessageW(v->diff, BM_GETCHECK, 0, 0) == BST_CHECKED;
    int showUnnamed = SendMessageW(v->unnamed, BM_GETCHECK, 0, 0) == BST_CHECKED;
    for (int i = 0; i < A->np; i++) {
        int j = B ? LgFindParam(B, &A->p[i]) : -1;
        if (!showUnnamed && !A->p[i].named) continue;
        if (diffOnly && j >= 0 && LgSame(&A->p[i], &B->p[j])) continue;
        v->rowA[v->nrows] = i; v->rowB[v->nrows] = j; v->nrows++;
    }
    if (B) for (int j = 0; j < B->np; j++) {             // settings only the other file has
        if (LgFindParam(A, &B->p[j]) >= 0 || (!showUnnamed && !B->p[j].named)) continue;
        v->rowA[v->nrows] = -1; v->rowB[v->nrows] = j; v->nrows++;
    }
    wchar_t t[400];
    for (int r = 0; r < v->nrows; r++) {
        const AmParam *p = v->rowA[r] >= 0 ? &A->p[v->rowA[r]] : &B->p[v->rowB[r]];
        LVITEMW it; ZeroMemory(&it, sizeof it); it.mask = LVIF_TEXT | LVIF_PARAM; it.iItem = r; it.lParam = r;
        swprintf(t, 400, L"%hs", p->obj); it.pszText = t;
        int row = (int)SendMessageW(v->params, LVM_INSERTITEMW, 0, (LPARAM)&it);
        LVITEMW s; ZeroMemory(&s, sizeof s);
        swprintf(t, 400, L"%hs", p->name); s.iSubItem = 1; s.pszText = t; SendMessageW(v->params, LVM_SETITEMTEXTW, row, (LPARAM)&s);
        if (v->rowA[r] >= 0) LgFmt(&A->p[v->rowA[r]], t, 400); else swprintf(t, 400, L"(not in this file)");
        s.iSubItem = 2; s.pszText = t; SendMessageW(v->params, LVM_SETITEMTEXTW, row, (LPARAM)&s);
        if (B) {
            if (v->rowB[r] >= 0) LgFmt(&B->p[v->rowB[r]], t, 400); else swprintf(t, 400, L"(not in that file)");
            s.iSubItem = 3; s.pszText = t; SendMessageW(v->params, LVM_SETITEMTEXTW, row, (LPARAM)&s);
        }
    }
    LVCOLUMNW c; ZeroMemory(&c, sizeof c); c.mask = LVCF_TEXT | LVCF_WIDTH;
    if (B) swprintf(t, 400, L"%hs", v->fl[v->cmpSel].name); else swprintf(t, 400, L"(pick a file to compare with above)");
    c.pszText = t; c.cx = LgS(v, B ? 230 : 260); ListView_SetColumn(v->params, 3, &c);
    SendMessageW(v->params, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(v->params, NULL, TRUE);
    // info line
    int ndiff = 0; if (B) for (int r = 0; r < v->nrows; r++) if (v->rowA[r] < 0 || v->rowB[r] < 0 || !LgSame(&A->p[v->rowA[r]], &B->p[v->rowB[r]])) ndiff++;
    wchar_t inf[600];
    int at = swprintf(inf, 600, L"%hs  -  %hs.   %d settings", v->fl[v->sel].name, LgWhat(A->ptype), A->np);
    if (A->unnamed) at += swprintf(inf + at, 600 - at, L" (%d with unrecovered names, shown as 0x........)", A->unnamed);
    if (B) swprintf(inf + at, 600 - at, L".   Compared with %hs: %d %ls (highlighted).", v->fl[v->cmpSel].name, ndiff, ndiff == 1 ? L"difference" : L"differences");
    SetWindowTextW(v->info, inf);
}

static void LgFillCompare(LG *v) {
    SendMessageW(v->cmp, CB_RESETCONTENT, 0, 0); v->ncmpList = 0;
    SendMessageW(v->cmp, CB_ADDSTRING, 0, (LPARAM)L"(no comparison)");
    int pick = 0;
    if (v->sel >= 0) {
        const char *pt = v->fl[v->sel].f.ptype;
        for (int i = 0; i < v->nf && v->ncmpList < 255; i++) {
            if (i == v->sel || strcmp(v->fl[i].f.ptype, pt)) continue;
            wchar_t t[200]; swprintf(t, 200, L"%hs", v->fl[i].name);
            SendMessageW(v->cmp, CB_ADDSTRING, 0, (LPARAM)t);
            v->cmpList[v->ncmpList++] = i;
            if (!strncmp(v->fl[i].name, "default.", 8) && strncmp(v->fl[v->sel].name, "default.", 8)) pick = v->ncmpList;   // compare with the level's default
        }
    }
    if (!v->ncmpList) {                    // only files of the same kind can be compared: say so when there are none
        SendMessageW(v->cmp, CB_RESETCONTENT, 0, 0);
        SendMessageW(v->cmp, CB_ADDSTRING, 0, (LPARAM)L"(no other file of this kind here)");
    }
    EnableWindow(v->cmp, v->ncmpList > 0);
    SendMessageW(v->cmp, CB_SETCURSEL, pick, 0);
    v->cmpSel = pick ? v->cmpList[pick - 1] : -1;
    EnableWindow(v->diff, v->cmpSel >= 0);
}
static void LgSelect(LG *v, int i) {
    if (i < 0 || i >= v->nf) return;
    v->sel = i;
    LgFillCompare(v); LgFillParams(v);
    InvalidateRect(v->preview, NULL, FALSE);
}

/* ---------------- preview ---------------- */
static void LgGrad(HDC dc, int x0, int y0, int x1, int y1, COLORREF a, COLORREF b) {
    TRIVERTEX tv[2] = { { x0, y0, (COLOR16)(GetRValue(a) << 8), (COLOR16)(GetGValue(a) << 8), (COLOR16)(GetBValue(a) << 8), 0xFF00 },
                        { x1, y1, (COLOR16)(GetRValue(b) << 8), (COLOR16)(GetGValue(b) << 8), (COLOR16)(GetBValue(b) << 8), 0xFF00 } };
    GRADIENT_RECT gr = { 0, 1 };
    GradientFill(dc, tv, 2, &gr, 1, GRADIENT_FILL_RECT_V);
}
static COLORREF LgMix(COLORREF a, COLORREF b, float t) {
    return RGB((int)(GetRValue(a) * (1 - t) + GetRValue(b) * t), (int)(GetGValue(a) * (1 - t) + GetGValue(b) * t), (int)(GetBValue(a) * (1 - t) + GetBValue(b) * t));
}
static void LgSwatch(HDC dc, int x, int y, int s, COLORREF c, const wchar_t *label, COLORREF tc) {
    HBRUSH br = CreateSolidBrush(c); RECT r = { x, y, x + s, y + s }; FillRect(dc, &r, br); DeleteObject(br);
    FrameRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SetTextColor(dc, tc); TextOutW(dc, x + s + 4, y + (s - 14) / 2, label, (int)wcslen(label));
}
static void LgPaintPreview(LG *v, HDC dc, RECT rc) {
    int W = rc.right, H = rc.bottom;
    HBRUSH bg = CreateSolidBrush(RGB(40, 40, 48)); FillRect(dc, &rc, bg); DeleteObject(bg);
    if (v->sel < 0) return;
    char base[96]; LgBase(v->fl[v->sel].name, base, sizeof base);
    const AmFile *env = LgPick(v, base, "aglenv"), *sky = LgPick(v, base, "aglsky"), *proj = LgPick(v, base, "projgfxbase");
    const AmParam *skyC = AmFind(env, "HemisphereLight", "SkyColor"), *grdC = AmFind(env, "HemisphereLight", "GroundColor");
    const AmParam *amb = AmFind(env, "AmbientLight", "Color"), *dif = AmFind(env, "DirectionalLight", "DiffuseColor");
    const AmParam *dir = AmFind(env, "DirectionalLight", "Direction"), *dirI = AmFind(env, "DirectionalLight", "Intensity");
    const AmParam *sunL = AmFind(proj, "", "SunLightColor"), *yfog = AmFind(proj, "", "YFogColor");
    const AmParam *fogN = AmFind(proj, "", "FogNear"), *fogF = AmFind(proj, "", "FogFar");
    const AmParam *skyDyn = AmFind(sky, "", "dynamic_color"), *skyGrd = AmFind(sky, "", "ground_color"), *adhoc = AmFind(sky, "", "adhoc_fog_color");
    const AmParam *sunSize = AmFind(sky, "", "render_sun_size");
    COLORREF cTop = skyC ? LgRgb(skyC->f, 1) : RGB(120, 150, 210);
    COLORREF cHor = skyDyn ? LgMix(cTop, LgRgb(skyDyn->f, 1), 0.5f) : LgMix(cTop, RGB(255, 255, 255), 0.4f);
    COLORREF cFog = LgIsColour(yfog) ? LgRgb(yfog->f, 1) : adhoc ? LgRgb(adhoc->f, 1) : cHor;
    COLORREF cG1 = skyGrd ? LgRgb(skyGrd->f, 1) : RGB(120, 100, 80), cG2 = grdC ? LgRgb(grdC->f, 1) : LgMix(cG1, 0, 0.4f);
    COLORREF cSun = sunL && sunL->nf >= 3 ? LgRgb(sunL->f, 1) : dif ? LgRgb(dif->f, 1) : RGB(255, 245, 220);
    int horizon = H * 62 / 100;
    LgGrad(dc, 0, 0, W, horizon - LgS(v, 14), LgMix(cTop, 0, 0.15f), cHor);
    LgGrad(dc, 0, horizon - LgS(v, 14), W, horizon, cHor, LgMix(cHor, cFog, 0.7f));
    LgGrad(dc, 0, horizon, W, H, LgMix(cG1, cFog, 0.45f), cG2);
    // the sun: the main light shines along Direction, so it sits opposite
    double elev = 45, az = 0;
    if (dir && dir->nf >= 3) {
        float dx = -dir->f[0], dy = -dir->f[1], dz = -dir->f[2], l = sqrtf(dx * dx + dy * dy + dz * dz);
        if (l > 0) { elev = asin(dy / l) * 180 / 3.14159265; az = atan2(dx, dz) * 180 / 3.14159265; }
    }
    int sx = W / 2 + (int)(az / 180.0 * (W / 2 - LgS(v, 40))), sy = horizon - (int)(elev / 90.0 * (horizon - LgS(v, 26)));
    int sr = LgS(v, 12) * (sunSize && sunSize->f[0] > 0 && sunSize->f[0] < 4 ? sunSize->f[0] : 1);
    if (elev > -5) {
        for (int g = 3; g >= 1; g--) {      // glow
            HBRUSH br = CreateSolidBrush(LgMix(LgMix(cHor, cTop, 0.5f), cSun, 0.25f * (4 - g)));
            HGDIOBJ o = SelectObject(dc, br); SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, sx - sr - g * sr / 2, sy - sr - g * sr / 2, sx + sr + g * sr / 2, sy + sr + g * sr / 2);
            SelectObject(dc, o); DeleteObject(br);
        }
        HBRUSH br = CreateSolidBrush(cSun); HGDIOBJ o = SelectObject(dc, br);
        Ellipse(dc, sx - sr, sy - sr, sx + sr, sy + sr); SelectObject(dc, o); DeleteObject(br);
    }
    // labels
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ of = SelectObject(dc, v->font);
    wchar_t t[600];
    swprintf(t, 600, L"Rough preview of \"%hs\"  (from %hs, %hs, %hs) - the game mixes in many more settings",
             base, env ? LgPickName(v, env) : "-", sky ? LgPickName(v, sky) : "-", proj ? LgPickName(v, proj) : "-");
    RECT tr = { LgS(v, 8), LgS(v, 6), W - LgS(v, 8), LgS(v, 26) };
    SetTextColor(dc, RGB(0, 0, 0)); OffsetRect(&tr, 1, 1); DrawTextW(dc, t, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    SetTextColor(dc, RGB(255, 255, 255)); OffsetRect(&tr, -1, -1); DrawTextW(dc, t, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    // swatches along the bottom
    int s = LgS(v, 16), y = H - s - LgS(v, 8), x = LgS(v, 8);
    RECT band = { 0, y - LgS(v, 5), W, H }; HBRUSH dk = CreateSolidBrush(RGB(30, 30, 36)); FillRect(dc, &band, dk); DeleteObject(dk);
    struct { const AmParam *p; const wchar_t *n; } sw[] = { { sunL, L"Sun light" }, { dif, L"Main light" }, { amb, L"Ambient" },
        { skyC, L"Sky light" }, { grdC, L"Ground light" }, { skyDyn, L"Sky colour" }, { LgIsColour(yfog) ? yfog : adhoc, L"Fog" } };
    for (int i = 0; i < (int)(sizeof sw / sizeof sw[0]); i++) {
        if (!sw[i].p || sw[i].p->nf < 3) continue;
        LgSwatch(dc, x, y, s, LgRgb(sw[i].p->f, 1), sw[i].n, RGB(230, 230, 230));
        SIZE sz; GetTextExtentPoint32W(dc, sw[i].n, (int)wcslen(sw[i].n), &sz); x += s + sz.cx + LgS(v, 18);
    }
    int at = 0; t[0] = 0;
    if (dir) at += swprintf(t + at, 600 - at, L"Sun %.0f\x00B0 above the horizon", elev);
    if (dirI) at += swprintf(t + at, 600 - at, L"%lslight \x00D7%g", at ? L"   \x00B7   " : L"", dirI->f[0]);
    if (fogN && fogF) at += swprintf(t + at, 600 - at, L"%lsfog %g \x2192 %g", at ? L"   \x00B7   " : L"", fogN->f[0], fogF->f[0]);
    if (t[0]) { SetTextColor(dc, RGB(230, 230, 230)); RECT r2 = { x + LgS(v, 10), y - 1, W - LgS(v, 8), y + s + 2 }; DrawTextW(dc, t, -1, &r2, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS); }
    SelectObject(dc, of);
}
static LRESULT CALLBACK LgPrevProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    LG *v = (LG *)GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA);
    if (msg == WM_PAINT && v) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HDC m = CreateCompatibleDC(dc); HBITMAP bm = CreateCompatibleBitmap(dc, rc.right, rc.bottom); HGDIOBJ o = SelectObject(m, bm);
        LgPaintPreview(v, m, rc);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, m, 0, 0, SRCCOPY);
        SelectObject(m, o); DeleteObject(bm); DeleteDC(m);
        EndPaint(hwnd, &ps); return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------------- drawing colour swatches in the settings list ---------------- */
static LRESULT LgCustomDraw(LG *v, NMLVCUSTOMDRAW *cd) {
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: return CDRF_NOTIFYSUBITEMDRAW;
    case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
        int r = (int)cd->nmcd.lItemlParam; if (r < 0 || r >= v->nrows || v->sel < 0) return CDRF_DODEFAULT;
        const AmFile *A = &v->fl[v->sel].f, *B = v->cmpSel >= 0 ? &v->fl[v->cmpSel].f : NULL;
        const AmParam *pa = v->rowA[r] >= 0 ? &A->p[v->rowA[r]] : NULL, *pb = B && v->rowB[r] >= 0 ? &B->p[v->rowB[r]] : NULL;
        int differs = B && !LgSame(pa, pb);
        cd->clrTextBk = differs ? RGB(0x3A, 0x2A, 0x14) : RGB(0x1D, 0x19, 0x1B);   // dark theme: amber tint where the files differ
        cd->clrText = (pa ? pa->named : pb && pb->named) ? RGB(0xF1, 0xEB, 0xE5) : RGB(0x8A, 0x7D, 0x76);
        int col = cd->iSubItem;
        const AmParam *p = col == 2 ? pa : col == 3 ? pb : NULL;
        if (!LgIsColour(p)) return CDRF_NEWFONT;
        // a swatch, then the numbers
        HDC dc = cd->nmcd.hdc; RECT rc; ListView_GetSubItemRect(v->params, (int)cd->nmcd.dwItemSpec, col, LVIR_BOUNDS, &rc);
        int selected = ListView_GetItemState(v->params, (int)cd->nmcd.dwItemSpec, LVIS_SELECTED) & LVIS_SELECTED;
        HBRUSH bb = CreateSolidBrush(selected ? RGB(0x4A, 0x24, 0x20) : cd->clrTextBk); FillRect(dc, &rc, bb); DeleteObject(bb);
        int s = rc.bottom - rc.top - 4; RECT sw = { rc.left + 4, rc.top + 2, rc.left + 4 + s * 2, rc.top + 2 + s };
        HBRUSH cb = CreateSolidBrush(LgRgb(p->f, 1)); FillRect(dc, &sw, cb); DeleteObject(cb);
        FrameRect(dc, &sw, (HBRUSH)GetStockObject(GRAY_BRUSH));
        wchar_t t[400]; LgFmt(p, t, 400);
        RECT tr = rc; tr.left = sw.right + 6;
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, selected ? RGB(0xF1, 0xEB, 0xE5) : cd->clrText);
        DrawTextW(dc, t, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        return CDRF_SKIPDEFAULT;
    }
    }
    return CDRF_DODEFAULT;
}

static void LgCopy(LG *v) {
    int n = ListView_GetSelectedCount(v->params); if (!n) return;
    size_t cap = (size_t)n * 1200 + 16; wchar_t *buf = (wchar_t *)malloc(cap * sizeof(wchar_t)); if (!buf) return;
    size_t at = 0; int i = -1;
    while ((i = ListView_GetNextItem(v->params, i, LVNI_SELECTED)) >= 0 && at < cap - 1200) {
        for (int c = 0; c < (v->cmpSel >= 0 ? 4 : 3); c++) {
            wchar_t t[400]; t[0] = 0; ListView_GetItemText(v->params, i, c, t, 400);
            at += swprintf(buf + at, cap - at, c ? L"\t%ls" : L"%ls", t);
        }
        at += swprintf(buf + at, cap - at, L"\r\n");
    }
    if (OpenClipboard(v->wnd)) {
        EmptyClipboard();
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (at + 1) * sizeof(wchar_t));
        if (g) { memcpy(GlobalLock(g), buf, (at + 1) * sizeof(wchar_t)); GlobalUnlock(g); SetClipboardData(CF_UNICODETEXT, g); }
        CloseClipboard();
    }
    free(buf);
}

/* ---------------- window ---------------- */
static void LgLayout(LG *v) {
    RECT rc; GetClientRect(v->wnd, &rc);
    int m = LgS(v, 8), g = LgS(v, 6), rh = LgS(v, 24), lw = LgS(v, 290), y = m;
    MoveWindow(v->filesLbl, m, y + LgS(v, 4), lw, rh - LgS(v, 4), TRUE);
    int fy = y + rh;
    int texH = (v->texInner[0] ? rh + g : 0) + (v->flareInner[0] ? rh + g : 0), by = rc.bottom - m - rh;
    MoveWindow(v->files, m, fy, lw, rc.bottom - fy - m - texH, TRUE);
    if (v->flareInner[0]) { MoveWindow(v->tex2, m, by, lw, rh, TRUE); by -= rh + g; }
    if (v->texInner[0]) MoveWindow(v->tex, m, by, lw, rh, TRUE);
    int x = m + lw + m, w = rc.right - x - m; if (w < LgS(v, 200)) w = LgS(v, 200);
    int cl = LgS(v, 95), cw = LgS(v, 240);
    MoveWindow(v->cmpLbl, x, y + LgS(v, 4), cl, rh - LgS(v, 4), TRUE);
    MoveWindow(v->cmp, x + cl, y, cw, LgS(v, 300), TRUE);
    int bx = x + cl + cw + g * 2, dw = LgS(v, 140), uw = LgS(v, 190);
    MoveWindow(v->diff, bx, y, dw, rh, TRUE);
    MoveWindow(v->unnamed, bx + dw + g, y, uw, rh, TRUE);
    y += rh + g;
    int ph = (rc.bottom - y) * 32 / 100; if (ph < LgS(v, 150)) ph = LgS(v, 150); if (ph > LgS(v, 260)) ph = LgS(v, 260);
    MoveWindow(v->preview, x, y, w, ph, TRUE); y += ph + g;
    MoveWindow(v->info, x, y, w, rh, TRUE); y += rh;
    MoveWindow(v->params, x, y, w, rc.bottom - y - m, TRUE);
}
static HWND LgCtl(LG *v, const wchar_t *cls, const wchar_t *text, DWORD style, DWORD ex, int id) {
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, v->wnd, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)v->font, TRUE);
    return h;
}

static LRESULT CALLBACK LgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    LG *v = (LG *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!v && msg != WM_CREATE) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_CREATE: {
        v = (LG *)((CREATESTRUCTW *)lp)->lpCreateParams; SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)v); v->wnd = hwnd;
        v->filesLbl = LgCtl(v, L"STATIC", L"Files in this environment archive:", SS_LEFT, 0, LG_FILESLBL);
        v->files = LgCtl(v, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP, WS_EX_CLIENTEDGE, LG_FILES);
        ListView_SetExtendedListViewStyle(v->files, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        v->tex = LgCtl(v, L"BUTTON", L"Environment textures (collect.genvres)", BS_PUSHBUTTON | WS_TABSTOP, 0, LG_TEX);
        if (!v->texInner[0]) ShowWindow(v->tex, SW_HIDE);
        { wchar_t bt[200]; const char *sl = strrchr(v->flareInner, '/'); swprintf(bt, 200, L"Lens flare textures (%hs)", sl ? sl + 1 : v->flareInner);
          v->tex2 = LgCtl(v, L"BUTTON", bt, BS_PUSHBUTTON | WS_TABSTOP, 0, LG_TEX2); }
        if (!v->flareInner[0]) ShowWindow(v->tex2, SW_HIDE);
        v->cmpLbl = LgCtl(v, L"STATIC", L"Compare with:", SS_LEFT, 0, LG_CMPLBL);
        v->cmp = LgCtl(v, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, 0, LG_CMP);
        v->diff = LgCtl(v, L"BUTTON", L"Only differences", BS_AUTOCHECKBOX | WS_TABSTOP, 0, LG_DIFF);
        v->unnamed = LgCtl(v, L"BUTTON", L"Show unnamed (0x...) settings", BS_AUTOCHECKBOX | WS_TABSTOP, 0, LG_UNNAMED);
        SendMessageW(v->unnamed, BM_SETCHECK, BST_CHECKED, 0);
        v->preview = CreateWindowExW(WS_EX_CLIENTEDGE, LG_PREV_CLASS, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)LG_PREVIEW, GetModuleHandleW(NULL), NULL);
        v->info = LgCtl(v, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 0, LG_INFO);
        v->params = LgCtl(v, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SHOWSELALWAYS | WS_TABSTOP, WS_EX_CLIENTEDGE, LG_PARAMS);
        ListView_SetExtendedListViewStyle(v->params, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
        {
            LVCOLUMNW c; ZeroMemory(&c, sizeof c); c.mask = LVCF_TEXT | LVCF_WIDTH;
            c.pszText = (LPWSTR)L"File"; c.cx = LgS(v, 150); ListView_InsertColumn(v->files, 0, &c);
            c.pszText = (LPWSTR)L"Settings for"; c.cx = LgS(v, 116); ListView_InsertColumn(v->files, 1, &c);
            c.pszText = (LPWSTR)L"Group"; c.cx = LgS(v, 170); ListView_InsertColumn(v->params, 0, &c);
            c.pszText = (LPWSTR)L"Setting"; c.cx = LgS(v, 190); ListView_InsertColumn(v->params, 1, &c);
            c.pszText = (LPWSTR)L"Value"; c.cx = LgS(v, 230); ListView_InsertColumn(v->params, 2, &c);
            c.pszText = (LPWSTR)L""; c.cx = LgS(v, 230); ListView_InsertColumn(v->params, 3, &c);
        }
        for (int i = 0; i < v->nf; i++) {
            wchar_t t[200]; swprintf(t, 200, L"%hs", v->fl[i].name);
            LVITEMW it; ZeroMemory(&it, sizeof it); it.mask = LVIF_TEXT | LVIF_PARAM; it.iItem = i; it.lParam = i; it.pszText = t;
            int row = (int)SendMessageW(v->files, LVM_INSERTITEMW, 0, (LPARAM)&it);
            swprintf(t, 200, L"%hs", LgWhat(v->fl[i].f.ptype));
            LVITEMW s; ZeroMemory(&s, sizeof s); s.iSubItem = 1; s.pszText = t; SendMessageW(v->files, LVM_SETITEMTEXTW, row, (LPARAM)&s);
        }
        LgLayout(v);
        int first = v->sel >= 0 ? v->sel : 0;
        ListView_SetItemState(v->files, first, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(v->files, first, FALSE);
        LgSelect(v, first);
        return 0;
    }
    case WM_SIZE: LgLayout(v); InvalidateRect(v->preview, NULL, FALSE); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO *)lp)->ptMinTrackSize.x = LgS(v, 760); ((MINMAXINFO *)lp)->ptMinTrackSize.y = LgS(v, 480); return 0;
    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)lp;
        if (nh->hwndFrom == v->files && nh->code == LVN_ITEMCHANGED) {
            NMLISTVIEW *nl = (NMLISTVIEW *)lp;
            if ((nl->uNewState & LVIS_SELECTED) && !(nl->uOldState & LVIS_SELECTED)) LgSelect(v, (int)nl->lParam);
        } else if (nh->hwndFrom == v->params && nh->code == NM_CUSTOMDRAW) {
            return LgCustomDraw(v, (NMLVCUSTOMDRAW *)lp);
        } else if (nh->hwndFrom == v->params && nh->code == LVN_KEYDOWN) {
            NMLVKEYDOWN *k = (NMLVKEYDOWN *)lp;
            if (k->wVKey == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) LgCopy(v);
            if (k->wVKey == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) ListView_SetItemState(v->params, -1, LVIS_SELECTED, LVIS_SELECTED);
        }
        return 0;
    }
    case WM_CONTEXTMENU:
        if ((HWND)wp == v->params) {
            HMENU m = CreatePopupMenu(); AppendMenuW(m, MF_STRING, 1, L"Copy selected rows\tCtrl+C");
            POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) }; if (pt.x == -1) GetCursorPos(&pt);
            if (TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL) == 1) LgCopy(v);
            DestroyMenu(m);
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case LG_CMP:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                int k = (int)SendMessageW(v->cmp, CB_GETCURSEL, 0, 0);
                v->cmpSel = k > 0 && k <= v->ncmpList ? v->cmpList[k - 1] : -1;
                EnableWindow(v->diff, v->cmpSel >= 0);
                LgFillParams(v);
            }
            break;
        case LG_DIFF: case LG_UNNAMED: LgFillParams(v); break;
        case LG_TEX: {
            wchar_t t[700]; swprintf(t, 700, L"%ls - %hs", v->title, v->texInner);
            TexViewOpen(hwnd, v->dump, v->texInner, t, v->font, v->dpi);
            break;
        }
        case LG_TEX2: {
            wchar_t t[700]; swprintf(t, 700, L"%ls - %hs", v->title, v->flareInner);
            TexViewOpen(hwnd, v->dump, v->flareInner, t, v->font, v->dpi);
            break;
        }
        }
        return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        for (int i = 0; i < v->nf; i++) AmFree(&v->fl[i].f);
        free(v->fl); free(v->rowA); free(v->rowB); free(v);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// dumpFile: the X.genvb.zs archive in the game dump; inner: a file in it to show first (or NULL)
void LgtViewOpen(HWND owner, const wchar_t *dumpFile, const char *inner, const wchar_t *title, HFONT font, int dpi) {
    static int reg = 0;
    HINSTANCE in = GetModuleHandleW(NULL);
    if (!reg) {
        WNDCLASSEXW wc = { 0 }; wc.cbSize = sizeof wc; wc.lpfnWndProc = LgProc; wc.hInstance = in; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = LG_CLASS;
        wc.hIcon = (HICON)SendMessageW(owner, WM_GETICON, ICON_BIG, 0); wc.hIconSm = (HICON)SendMessageW(owner, WM_GETICON, ICON_SMALL, 0);
        RegisterClassExW(&wc);
        WNDCLASSEXW pc = { 0 }; pc.cbSize = sizeof pc; pc.lpfnWndProc = LgPrevProc; pc.hInstance = in; pc.hCursor = LoadCursor(NULL, IDC_ARROW);
        pc.lpszClassName = LG_PREV_CLASS; RegisterClassExW(&pc);
        reg = 1;
    }
    LG *v = (LG *)calloc(1, sizeof(LG)); if (!v) return;
    v->font = font; v->dpi = dpi ? dpi : 96; v->sel = -1; v->cmpSel = -1;
    swprintf(v->title, 600, L"Lighting - %ls", title); swprintf(v->dump, 2048, L"%ls", dumpFile);
    char err[300] = "";
    if (!LgLoad(v, dumpFile, err, sizeof err)) {
        wchar_t m[2600]; swprintf(m, 2600, L"The lighting settings couldn't be read.\n\n%hs\n\nArchive: %ls", err, dumpFile);
        MessageBoxW(owner, m, L"Lighting viewer", MB_ICONINFORMATION);
        for (int i = 0; i < v->nf; i++) AmFree(&v->fl[i].f);
        free(v->fl); free(v); return;
    }
    if (inner && inner[0]) { const char *s = strrchr(inner, '/'); v->sel = LgFindFile(v, s ? s + 1 : inner); }
    if (v->sel < 0) for (int i = 0; i < v->nf; i++) if (!strcmp(v->fl[i].name, "default.bprojgfxbase")) { v->sel = i; break; }
    int w = MulDiv(1200, v->dpi, 96), h = MulDiv(820, v->dpi, 96);
    HWND wnd = CreateWindowExW(0, LG_CLASS, v->title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, w, h, NULL, NULL, in, v);
    if (!wnd) { for (int i = 0; i < v->nf; i++) AmFree(&v->fl[i].f); free(v->fl); free(v); return; }
    if (wnd) UiSkinWindow(wnd);                 // the main window's dark look
    ShowWindow(wnd, SW_SHOW);
}

/* ---------------- preview for the main window ---------------- */
// the viewer's sky preview for the archive (or for the file inner within it), scaled to fit boxW x boxH
unsigned char *LgtThumbnail(const wchar_t *dumpFile, const char *inner, int boxW, int boxH, int *ow, int *oh, wchar_t *note, int noteN) {
    note[0] = 0;
    LG *v = (LG *)calloc(1, sizeof(LG)); if (!v) return NULL;
    v->dpi = 96; v->sel = -1; v->cmpSel = -1;
    unsigned char *out = NULL; char err[300];
    if (LgLoad(v, dumpFile, err, sizeof err)) {
        if (inner && inner[0]) { const char *s = strrchr(inner, '/'); v->sel = LgFindFile(v, s ? s + 1 : inner); }
        if (v->sel < 0) for (int i = 0; i < v->nf; i++) if (!strcmp(v->fl[i].name, "default.bprojgfxbase")) { v->sel = i; break; }
        if (v->sel < 0) v->sel = 0;
        int W = 480, H = 270;
        v->font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        BITMAPINFO bi; ZeroMemory(&bi, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        void *bits = NULL; HDC dc = CreateCompatibleDC(NULL); HBITMAP bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (bm && bits) {
            HGDIOBJ ob = SelectObject(dc, bm);
            RECT rc = { 0, 0, W, H };
            LgPaintPreview(v, dc, rc);
            GdiFlush();
            unsigned char *p = (unsigned char *)bits; for (int i = 0; i < W * H; i++) p[i * 4 + 3] = 255;
            out = UiScaleToBox(p, W, H, boxW, boxH, ow, oh);
            SelectObject(dc, ob);
        }
        if (bm) DeleteObject(bm);
        DeleteDC(dc); if (v->font) DeleteObject(v->font);
        swprintf(note, noteN, L"%d lighting files", v->nf);
    }
    for (int i = 0; i < v->nf; i++) AmFree(&v->fl[i].f);
    free(v->fl); free(v->rowA); free(v->rowB); free(v);
    return out;
}
