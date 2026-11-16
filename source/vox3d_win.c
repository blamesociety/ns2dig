// vox3d_win.c - 3D view window for voxel terrain parts (OpenGL, built into Windows).
// Opened with Vox3DOpen(); each window loads one part folder from the game dump, builds a smooth
// surface in a background thread and shows it. Left-drag rotates, right-drag moves, wheel zooms.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
void UiSkinWindow(HWND top);
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <GL/gl.h>
#include <wchar.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

#define V3_MAXTEX 4096
typedef wchar_t VOXCHAR;
#define VOXSTR(x) L##x
#define VOXFMT L"ls"
#define VOXSEP L"\\"
#define VOXSPRINTF swprintf

static unsigned char *VoxReadFile(const wchar_t *path, size_t *size) {
    wchar_t lp[4200];
    if (wcsncmp(path, L"\\\\?\\", 4) == 0) swprintf(lp, 4200, L"%ls", path);
    else if (wcsncmp(path, L"\\\\", 2) == 0) swprintf(lp, 4200, L"\\\\?\\UNC\\%ls", path + 2);
    else swprintf(lp, 4200, L"\\\\?\\%ls", path);
    HANDLE h = CreateFileW(lp, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    if (sz.QuadPart < 0 || sz.QuadPart > 0x7FFFFFFFLL) { CloseHandle(h); return NULL; }
    unsigned char *b = (unsigned char *)malloc((size_t)sz.QuadPart + 1);
    DWORD got = 0;
    if (!b || !ReadFile(h, b, (DWORD)sz.QuadPart, &got, NULL)) { free(b); CloseHandle(h); return NULL; }
    CloseHandle(h);
    *size = got;
    return b;
}
#include "vox3d_core.h"
#include "ui_theme.h"
#include "arc_util.h"
#include "byml_mini.h"
#include "msg_core.h"
#include "helper_core.h"
#include "tex_core.h"
static void xfree(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }
#include "deflate_lite.h"

/* ------------------------------------------------------------------ GL 1.5 buffer functions (optional) */
#ifndef APIENTRY
#define APIENTRY __stdcall
#endif
typedef ptrdiff_t GLsizeiptr_;
typedef void (APIENTRY *PFNGenBuffers)(GLsizei, GLuint *);
typedef void (APIENTRY *PFNBindBuffer)(GLenum, GLuint);
typedef void (APIENTRY *PFNBufferData)(GLenum, GLsizeiptr_, const void *, GLenum);
typedef void (APIENTRY *PFNDeleteBuffers)(GLsizei, const GLuint *);
#define GL_ARRAY_BUFFER_ 0x8892
#define GL_ELEMENT_ARRAY_BUFFER_ 0x8893
#define GL_STATIC_DRAW_ 0x88E4
#ifndef GL_LIGHT_MODEL_TWO_SIDE
#define GL_LIGHT_MODEL_TWO_SIDE 0x0B52
#endif

#define WM_V3_LOADED  (WM_APP + 60)   // lp = 1 ok / 0 failed
#define WM_V3_MESHED  (WM_APP + 61)   // wp = generation, lp = VoxMesh* (or NULL)
#define WM_V3_EXPORTED (WM_APP + 62)  // lp = V3Export* (finished)
#define V3_TIMER_REBUILD 1
#define V3_TIMER_PROGRESS 2
#define V3_TIMER_ANIM 3
#define V3_TIMER_FX 4
enum { V3_PROPS = 1986, V3_LANDMARKS = 1987, V3_HELPER = 1988, V3_CLOTHSIM = 1989, V3_CLOTH = 1990, V3_LOD = 2001, V3_CUT, V3_RESET, V3_EXPORT, V3_LIST, V3_ALL, V3_NONE, V3_INFO, V3_HELP, V3_CUTLBL, V3_MATLBL, V3_ANIM, V3_PLAY, V3_FRAME, V3_ANIMFILE, V3_ATTACH, V3_SPEED, V3_KEEP, V3_SORT, V3_FIND, V3_COLOUR, V3_OVERLAY, V3_TOPVIEW, V3_PNG, V3_PROBES, V3_CTLBTN, V3_COORD, V3_SOUNDS, V3_SAVEVIEW };

/* placed objects of the MKW world (Banc\World_Game03 + World_Game03-Free), read once per dump and kept: */
typedef struct { char nm[96]; float T[3], R[3], S[3]; unsigned char gridify, resident, sphere; } V3Act;   // sphere: the PrimitiveType - 0 none / other (Cone, Camera...), 1 Sphere, 2 Cylinder(HighRes), 3 Cube
// one record per object name: how many are placed in the whole world, its model's size, and what the last load did
// with it (ent: the list entry it's drawn as; why: why it isn't) - the "objects near" list reports from this
typedef struct { char nm[96]; int count; float rad; signed char model; int ci; char ent[64]; char why[72]; } V3Name;   // (ci: this load's model)
typedef struct {
    HWND wnd, gl, info, lod, cut, cutlbl, reset, topb, pngb, exportb, matlbl, list, all, none, help, animCb, play, frame, animFileCb, attachCb, clothCb;
    HANDLE exportThread; volatile long exporting;
    HFONT font; int dpi;
    HDC dc; HGLRC rc;
    // data
    wchar_t dir[2048], title[512], geom[2048];   // geom: VoxelGeometry JSON for voxel objects (dir = archive), else empty
    VoxPart part; int partOk;
    unsigned char hidden[VOX_MAXMAT];
    int lodSel;                      // 1 or 2
    HANDLE thread; volatile long cancel, progress; long gen; int rebuildPending; int loading; int reloadPending; HWND owner; POINT rdown; int rmoved;
    // GL mesh
    int haveMesh, useVbo; GLuint vbo[2]; GLsizei nidx;
    float *cpuVert; uint32_t *cpuIdx; // fallback (no VBOs)
    int stride;                       // bytes per vertex: 20, or 28 with texture coordinates
    struct { int mat; GLsizei first, count; } *runs; int nruns;   // triangles grouped by part (models)
    // model textures: decoded in the loader thread, uploaded on WM_V3_LOADED
    struct { char key[200]; unsigned char *rgba; int w, h, alpha; GLuint id; } tex[V3_MAXTEX]; int ntex;
    char (*mtexName)[128]; int *mtexTex; int nmtex, capmtex;   // MKW terrain: textures of placed scenery (slot -> name, -> tex index)
    int partTex[VOX_MAXMAT];                 // part -> tex index, -1 none
    int partMix[VOX_MAXMAT];                 // part -> its eye-shine (EmmMix) texture, -1 none
    int texWanted, texFound, showTex;
    unsigned char waterBlend[VOX_MAXMAT];   // MKW terrain: materials drawn see-through (water), after everything else
    int terrTex;                     // Mario Kart World terrain: ground detail textures loaded (drawn over the colour map)
    // skeletal animation (models): X.anim.bfres next to the model
    BfAnimFile anims; BfSkelAnim anim; int animSel, playing; float animFrame; DWORD animT0; float animF0; BfShapeAnim shAnim; BfVisAnim visAnim; BfMatAnim matAnim; unsigned char visCtl[VOX_MAXMAT], visOn[VOX_MAXMAT], uvCtl[VOX_MAXMAT], uvCtl1[VOX_MAXMAT]; float uvM[VOX_MAXMAT][6], uvM1[VOX_MAXMAT][6]; float *morphP; float keepOff[3]; HWND speedCb, keepCb, sortCb, findEd; int sortMode, sortDesc; float fps; float *worldM;
    // animation files to choose from: found for this model by name (X.anim, PlayerAnimation for DK...) or picked by hand
    wchar_t animFiles[16][1024]; int animFileCount[16], nAnimFiles, animFileSel; int hasSkeleton;
    // models attached to this one (DK's head and ties): bones lo..hi-1 and parts partLo..partHi-1 of the merged model
    struct { int lo, hi, partLo, partHi, defShown; char name[64]; int fi[4], nfi; BfSkelAnim anim; BfShapeAnim sh; BfVisAnim vis; BfMatAnim mat; float Tb[12]; } att[16]; int tieSel, tieWasShown[16];
    int nAtt, bodyN, attOn;
    int nCloth; char clothInfo[300]; float drawCentre[3];
    int hasLandmarks, landmarksOff; HWND lmCb; HWND propCb; int propsOn;
    char (*keepName)[VOX_MATN]; unsigned char *keepHidden; int nkeep, ovlFresh, dropKeep;   // terrain: which list entries were ticked, kept over a reload (by name)   // propsOn: "Show props" (full-detail areas; loads them)   // MKW terrain: "Landmarks" - the other objects in WorldResident (mountains, cranes...)
    int hasCliffs, cliffsOff;        // MKW terrain: cliffs / ice models placed in the world (the helper-bones box becomes "Show cliffs and ice")
    HWND prbCb; int prbSel; float *prb; int nprb; float prbLo[3], prbHi[3]; char prbFile[96]; float *prbLoad; int nprbLoad; char prbFileLoad[96]; int nprbFine, nprbFineLoad;   // (fine: the .dlpg probes at the end of the list, full-detail areas)
    HWND sndCb; int sndSel, sndHot; float *snd; int nsnd; float sndLo, sndHi; float *sndLoad; int nsndLoad, sndOrient, sndOrientLoad;   // sound (acoustic) probes, full-detail areas
    HWND ctlBtn, coord, ctlWnd, prbTip; int prbHot; wchar_t prbTipText[400]; int arrowHot; POINT hoverPt; int tracking;          // Controls button + window, cursor position read-out, area arrows   // (loaded by the worker, taken over when it's done)   // MKW terrain: baked light probes (Env\World_<scene>.dlpr) as dots
    HWND ovlCb; int ovlSel, ovlFromParent, viewPending;          // MKW terrain: "Ground: ..." - colour the ground by another layer (VOV_...)
    int hasWater, waterOff;          // MKW terrain with water / lava surfaces: the cloth check box becomes "Show water and lava"
    ClothSim sim[16]; int nsim; HWND simCb; float simLast; int simReset;   // cloth simulation (tie, hair)
    HelperBones hb; HWND hbCb; char hbFile[64];                             // helper bones (Phive\HelperBone.alsarc.zs)   // cloth physics overlays added; where the drawn mesh was centred
    BfAnimFile attFile[8]; wchar_t attFilePath[8][1024]; int nAttFile;   // their animation files (shared)
    // colour clips (fur / pants colours, damage flashes, adrenaline glow): the files holding them, the list, and
    // what is applied: a look per part (albedo swap + tint, layered as clips are picked) and one animated effect
    HWND colCb; BfAnimFile colF[4]; int ncolF; char colNameErr[160];
    struct V3Col { char name[64]; short f, i; char animated; wchar_t title[64]; } col[400]; int ncol;
    float lookTint[256][3], lookEmis[256][3]; int origTex[256]; char origAlb[256][128]; unsigned char colChanged[256]; int origSaved;
    BfColAnim fx; int fxOn; DWORD fxT0;
    unsigned char *patOn, *patRGB, partPat[VOX_MAXMAT]; int patTex[VOX_MAXMAT];   // gradient furs: per model vertex the pattern colour + strength (RGBA), and per part a grey copy of its fur texture
    uint32_t *meshSrc; size_t meshNv; float meshCentre[3]; unsigned char *animBuf; float *skinM, *posedP, *posedN;
    float bmin[3], bmax[3], radius;
    size_t tris, verts;
    // camera
    float yaw, pitch, dist, panx, pany;
    int dragging; POINT last;
    int cutPos;                      // 0..1000 (1000 = no cut)
    int fillingList;
    int listMat[VOX_MAXMAT * 2], nlist;   // list row -> entry (>= 0) or group (-1 - group)
    // the list's groups (V3FillList): name, its entries (grpIds from grpStart, grpN of them), its row (-1 none);
    // per entry its group and row (-1: in a closed group / not listed); the groups left open (by name)
    char grpName[VOX_MAXMAT][VOX_MATN]; int grpStart[VOX_MAXMAT], grpN[VOX_MAXMAT], grpIds[VOX_MAXMAT], grpRow[VOX_MAXMAT], ngrp;
    short grpOfMat[VOX_MAXMAT]; int matRow[VOX_MAXMAT]; char (*expName)[VOX_MATN]; int nexp, capexp; unsigned char grpSome[VOX_MAXMAT];   // grpSome: only some shown (drawn as a square in the box)
    int panelW96, col0W96, col2W96, splitDrag, colsSet;    // left panel and list column widths (at 96 dpi; saved with the view); dragging the divider
    PFNGenBuffers glGenBuffers_; PFNBindBuffer glBindBuffer_; PFNBufferData glBufferData_; PFNDeleteBuffers glDeleteBuffers_;
    wchar_t status[256];
    V3Act *allAct; int nAllAct; wchar_t allRoot[1024];       // MKW: every placed object (see V3GatherActors)
    V3Name *names; int capNames, nNames;                     // MKW: per object name (see V3Name)
    // loading status over the bottom of the view: the step (set by the loading thread) and how far it is (0-1000;
    // -1: unknown, the bar just moves); a counted step: base + span x done / n
    char *restoreTxt; int restoreLeft; HWND saveb;              // saved view being put back (V3RestorePass); the Save button
    unsigned char *actArc, *miArc, *genArc; size_t actArcN, miArcN, genArcN; int gyArcTried, genArcTried;
    int ncoll; struct { int mat, list; char txt[600]; } coll[8];
    // P-switch missions / stickers: placed objects that name a mission (V3GatherActors), the collectible lists' points
    // (all of them), the sticker names by id (murmur3 of the name) and the mission texts (Mals\USen\MsgMain.msarc)
    struct V3Wta { float T[3]; char ms[56]; } *wta; int nwta;
    struct V3Isl { uint64_t hash; float T[3], LT[3]; int noPos; char nm[64], island[48], ms[56]; } *isl; int nisl;   // actors inside the islands (drawn at the entrance)
    struct V3CPt { float T[3], R[3], S[3]; int list, placed, noPos; char stk[40]; char ms[56]; char island[48], inm[64], an[96]; } *cpt; int ncpt;
    char clist[8][40], clfile[8][120], clact[8][4][96]; int nclist, clnact[8], clall[8], cltot[8]; unsigned char *actColl;
    struct V3CInst { size_t v0, v1; float A[3], h; int cpt; } *cinst; int ncinst, cHot, glowAll; HWND stkWnd; unsigned char *stkPx; int stkW, stkH; char stkName[48]; size_t cv0; float *corig, collScale;   // the collectibles drawn (V3AddCollectibles)
    struct V3Stk { uint32_t id; char nm[40]; } *stk; int nstk, stkTried; Msbt missMsg; int missMsgTried; char missMsgFrom[200];      // the game's collectible lists drawn this load (V3AddCollectibles)   // the game's actor settings / model info archives (V3ActorModelFile)
    char *pieceTxt[VOX_MAXMAT];                              // terrain: per entry, the ground-piece part names drawn as it (\n-separated)
    unsigned char fromPiece[VOX_MAXMAT];                     // terrain: entries with triangles from the merged ground pieces
    size_t capV, capT;                                       // terrain load: room in the part's vertex / triangle arrays (V3Reserve)
    HWND ldText, ldBar; volatile long ldProg, ldDone, ldN, ldBase, ldSpan; wchar_t ldStage[160]; DWORD busyT0; int busyOn, busyMarq;
} V3;

static int V3S(V3 *v, int x) { return MulDiv(x, v->dpi, 96); }

/* ------------------------------------------------------------------ model textures (romfs\\Tex\\<name>.bntx.zs) */
// romfs folders to look in for a file's companions: its own romfs, then the dump's updates and base
static int V3RomfsRoots(const wchar_t *modelPath, wchar_t roots[12][1024]) {
    int nr = 0;
    size_t n = wcslen(modelPath), at = (size_t)-1;
    for (size_t i = 0; i + 7 <= n; i++) if (!_wcsnicmp(modelPath + i, L"\\romfs\\", 7)) { at = i; break; }
    if (at == (size_t)-1) return 0;
    swprintf(roots[nr++], 1024, L"%.*ls", (int)(at + 6), modelPath);
    wchar_t own[1024]; swprintf(own, 1024, L"%.*ls", (int)at, modelPath);
    wchar_t *s = wcsrchr(own, L'\\');
    if (s) {
        int ok = 0;
        if (!_wcsicmp(s + 1, L"base")) { *s = 0; ok = 1; }
        else { *s = 0; wchar_t *s2 = wcsrchr(own, L'\\'); if (s2 && !_wcsicmp(s2 + 1, L"updates")) { *s2 = 0; ok = 1; } }
        if (ok) {
            wchar_t pat[1100]; swprintf(pat, 1100, L"%ls\\updates\\*", own);
            WIN32_FIND_DATAW fd; HANDLE fh = FindFirstFileW(pat, &fd);
            if (fh != INVALID_HANDLE_VALUE) {
                do if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.' && nr < 11)
                    swprintf(roots[nr++], 1024, L"%ls\\updates\\%ls\\romfs", own, fd.cFileName);
                while (FindNextFileW(fh, &fd));
                FindClose(fh);
            }
            if (nr < 12) swprintf(roots[nr++], 1024, L"%ls\\base\\romfs", own);
        }
    }
    return nr;
}
// one layer of a texture (array textures: e.g. a ? panel's faces); *nl: how many layers it has
static unsigned char *V3ReadTexL(const wchar_t *modelPath, const char *name, int layer, int *w, int *h, int *nl) {
    wchar_t roots[12][1024]; int nr = V3RomfsRoots(modelPath, roots);
    if (nl) *nl = 0;
    for (int r = 0; r < nr; r++) {
        wchar_t f[1200]; swprintf(f, 1200, L"%ls\\Tex\\%hs.bntx.zs", roots[r], name);
        size_t zn; unsigned char *z = VoxReadFile(f, &zn); if (!z) continue;
        size_t bn; unsigned char *b = VoxUnzstd(z, zn, &bn); free(z); if (!b) continue;
        TexFile tf; unsigned char *px = NULL;
        if (TexOpen(&tf, b, bn) && tf.n > 0) {
            TexInfo t = tf.t[0]; int L = t.layers > 1 ? t.layers : 1;
            if (nl) *nl = L;
            if (layer > 0 && layer < L) { uint32_t st = t.size / (uint32_t)L; t.data += (uint64_t)st * layer; t.size = st; }   // (layers: equal slices of the data)
            char err[200]; px = TexDecode(b, bn, &t, tf.ounce, err, sizeof err);
            if (px) { *w = t.w; *h = t.h; }
            TexClose(&tf);
        }
        free(b);
        if (px) return px;
    }
    return NULL;
}
static unsigned char *V3ReadTex(const wchar_t *modelPath, const char *name, int *w, int *h) {
    wchar_t roots[12][1024]; int nr = V3RomfsRoots(modelPath, roots);
    for (int r = 0; r < nr; r++) {
        wchar_t f[1200]; swprintf(f, 1200, L"%ls\\Tex\\%hs.bntx.zs", roots[r], name);
        size_t zn; unsigned char *z = VoxReadFile(f, &zn); if (!z) continue;
        size_t bn; unsigned char *b = VoxUnzstd(z, zn, &bn); free(z); if (!b) continue;
        TexFile tf; unsigned char *px = NULL;
        if (TexOpen(&tf, b, bn) && tf.n > 0) {
            char err[200]; px = TexDecode(b, bn, &tf.t[0], tf.ounce, err, sizeof err);
            if (px) { *w = tf.t[0].w; *h = tf.t[0].h; }
            TexClose(&tf);
        }
        free(b);
        if (px) return px;
    }
    return NULL;
}
// a material's colour texture as RGBA (the albedo, with the opacity map in alpha for fur cards); NULL if not found
static unsigned char *V3ReadMatTex(const wchar_t *dir, const VoxPart *p, int id, int *w, int *h, int *alpha) {
    *alpha = 0;
    unsigned char *px = V3ReadTex(dir, p->texAlb[id], w, h);
    if (px && p->texOpc[id][0]) {                  // opacity map -> alpha (fur cards)
        int ow = 0, oh = 0; unsigned char *op = V3ReadTex(dir, p->texOpc[id], &ow, &oh);
        if (op) {
            for (int y = 0; y < *h; y++) for (int x = 0; x < *w; x++)
                px[((size_t)y * *w + x) * 4 + 3] = op[((size_t)(y * oh / *h) * ow + (x * ow / *w)) * 4];
            free(op); *alpha = 1;
        }
    }
    return px;
}
static void V3LoadTextures(V3 *v) {
    VoxPart *p = &v->part;
    for (int i = 0; i < VOX_MAXMAT; i++) v->partTex[i] = -1;
    for (int id = 0; id < p->nmat && id < 256; id++) {
        if (!p->texAlb[id][0]) continue;
        char key[200]; snprintf(key, sizeof key, "%s|%s", p->texAlb[id], p->texOpc[id]);
        int k = -1;
        for (int t = 0; t < v->ntex; t++) if (!strcmp(v->tex[t].key, key)) k = t;
        if (k < 0 && v->ntex < V3_MAXTEX) {
            k = v->ntex++; snprintf(v->tex[k].key, sizeof v->tex[k].key, "%s", key);
            v->texWanted++;
            int w = 0, h = 0, al = 0; unsigned char *px = V3ReadMatTex(v->dir, p, id, &w, &h, &al);
            if (px) { v->tex[k].rgba = px; v->tex[k].w = w; v->tex[k].h = h; v->tex[k].alpha = al; v->texFound++; }
        }
        if (k >= 0 && (v->tex[k].rgba || v->tex[k].id)) v->partTex[id] = k;
    }
    // eye shine textures (added on top of the eye)
    for (int id = 0; id < VOX_MAXMAT; id++) v->partMix[id] = -1;
    for (int id = 0; id < p->nmat && id < 256; id++) {
        if (!p->texMix[id][0]) continue;
        char key[200]; snprintf(key, sizeof key, "%s|mix", p->texMix[id]);
        int k = -1; for (int t = 0; t < v->ntex; t++) if (!strcmp(v->tex[t].key, key)) k = t;
        if (k < 0 && v->ntex < V3_MAXTEX) {
            int w = 0, h = 0; unsigned char *px = V3ReadTex(v->dir, p->texMix[id], &w, &h);
            k = v->ntex++; snprintf(v->tex[k].key, sizeof v->tex[k].key, "%s", key);
            if (px) { v->tex[k].rgba = px; v->tex[k].w = w; v->tex[k].h = h; v->tex[k].alpha = 0; }
        }
        if (k >= 0 && (v->tex[k].rgba || v->tex[k].id)) v->partMix[id] = k;
    }
}
static void V3UploadTextures(V3 *v) {
    if (!v->rc) return;
    wglMakeCurrent(v->dc, v->rc);
    for (int t = 0; t < v->ntex; t++) {
        if (!v->tex[t].rgba || v->tex[t].id) continue;
        glGenTextures(1, &v->tex[t].id); glBindTexture(GL_TEXTURE_2D, v->tex[t].id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        int mip = !strncmp(v->tex[t].key, "tera|", 5) || !strncmp(v->tex[t].key, "mdl|", 4) || !strncmp(v->tex[t].key, "water|", 6);            // ground textures repeat thousands of times: mipmaps
        if (mip) { glTexParameteri(GL_TEXTURE_2D, 0x8191 /*GL_GENERATE_MIPMAP*/, GL_TRUE); glTexParameterf(GL_TEXTURE_2D, 0x84FE /*max anisotropy*/, 8.0f); }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, v->tex[t].w, v->tex[t].h, 0, GL_RGBA, GL_UNSIGNED_BYTE, v->tex[t].rgba);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mip ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        while (glGetError() != GL_NO_ERROR) {}                     // (anisotropy isn't on every card)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        free(v->tex[t].rgba); v->tex[t].rgba = NULL;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

/* ------------------------------------------------------------------ animation files */
// names of .anim.bfres files that may animate model "base": its own, then shared ones the game uses under another
// name (DK's body moves are in PlayerAnimation, Pauline's in PaulineAnimation...)
static int V3AnimNames(const wchar_t *base, wchar_t out[8][300]) {
    int n = 0;
    #define V3ADD(...) do { if (n < 8) { swprintf(out[n], 300, __VA_ARGS__); int dup = 0; for (int q = 0; q < n; q++) if (!_wcsicmp(out[q], out[n])) dup = 1; if (!dup && out[n][0]) n++; } } while (0)
    V3ADD(L"%ls", base);
    if (!wcsncmp(base, L"DonkeyKong", 10) || !wcsncmp(base, L"FigureDonkeyKong", 16)) {
        if (wcsstr(base, L"Head")) V3ADD(L"PlayerHeadAnimation");
        else if (wcsstr(base, L"Tie")) V3ADD(L"PlayerTieAnimation");
        else if (!wcsstr(base, L"Helmet") && !wcsstr(base, L"Rock")) {
            V3ADD(L"PlayerAnimation");
            V3ADD(L"DonkeyKongAnimation"); V3ADD(L"DonkeyKong");   // DK's colour clips (fur, pants), also for DonkeyKongNaked
        }
    }
    V3ADD(L"%lsAnimation", base);
    { wchar_t w[300]; int i = 1; swprintf(w, 300, L"%ls", base); while (w[i] && !(w[i] >= L'A' && w[i] <= L'Z')) i++;   // first word: MonkeyMiner -> Monkey
      if (w[i]) { w[i] = 0; V3ADD(L"%lsAnimation", w); V3ADD(L"%ls", w); } }
    #undef V3ADD
    return n;
}

// a model file X.X.bfres.zs beside this one or in another romfs folder's Model
static int V3FindModel(V3 *v, const wchar_t *name, wchar_t *out) {
    swprintf(out, 1024, L"%ls", v->dir);
    wchar_t *slash = wcsrchr(out, L'\\'); if (slash) swprintf(slash + 1, 1024 - (slash + 1 - out), L"%ls.%ls.bfres.zs", name, name);
    if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return 1;
    wchar_t roots[12][1024]; int nr = V3RomfsRoots(v->dir, roots);
    for (int r = 0; r < nr; r++) { swprintf(out, 1024, L"%ls\\Model\\%ls.%ls.bfres.zs", roots[r], name, name); if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return 1; }
    return 0;
}
// an animation file for an attached model, shared between attachments (all 13 ties use PlayerTieAnimation)
static int V3AttAnimFile(V3 *v, const wchar_t *path) {
    for (int i = 0; i < v->nAttFile; i++) if (!_wcsicmp(v->attFilePath[i], path)) return v->attFile[i].count ? i : -1;
    if (v->nAttFile >= 8) return -1;
    int i = v->nAttFile;
    if (!BfAnimFileOpen(&v->attFile[i], path)) memset(&v->attFile[i], 0, sizeof v->attFile[i]);
    swprintf(v->attFilePath[i], 1024, L"%ls", path); v->nAttFile++;
    return v->attFile[i].count ? i : -1;
}
// colour clips in an animation file: listed, and the file kept open (a copy) for when one is picked
static void V3ColScan(V3 *v, const BfAnimFile *f, const wchar_t *path) {
    if (v->ncolF >= 4) return;
    int fi = -1;
    for (int i = 0; i < f->count && v->ncol < 400; i++) {
        BfColAnim a; if (!BfColAnimLoad(f, i, &v->part, &a)) continue;
        if (fi < 0) { fi = v->ncolF; if (!BfAnimFileOpen(&v->colF[fi], path)) { BfColAnimFree(&a); return; } v->ncolF++; }
        struct V3Col *c = &v->col[v->ncol++]; snprintf(c->name, sizeof c->name, "%s", a.name); c->f = (short)fi; c->i = (short)i; c->animated = (char)a.animated;
        BfColAnimFree(&a);
    }
}
static void V3RequestBuild(V3 *v);
static void V3ColReset(V3 *v) {
    for (int m = 0; m < 256; m++) { v->lookTint[m][0] = v->lookTint[m][1] = v->lookTint[m][2] = 1; v->lookEmis[m][0] = v->lookEmis[m][1] = v->lookEmis[m][2] = 0; }
    for (int m = 0; m < v->part.nmat && m < 256; m++) if (v->colChanged[m]) { v->partTex[m] = v->origTex[m]; snprintf(v->part.texAlb[m], sizeof v->part.texAlb[m], "%s", v->origAlb[m]); v->colChanged[m] = 0; }
    v->origSaved = 0;
    if (v->fxOn) { BfColAnimFree(&v->fx); v->fxOn = 0; KillTimer(v->wnd, V3_TIMER_FX); }
    if (v->patOn) { free(v->patOn); free(v->patRGB); v->patOn = v->patRGB = NULL; memset(v->partPat, 0, sizeof v->partPat); V3RequestBuild(v); }
    for (int m = 0; m < VOX_MAXMAT; m++) v->patTex[m] = -1;
}
static int V3TexFor(V3 *v, const char *name);
static int V3TexForPart(V3 *v, int m, const char *albedo);
static int V3LumTexFor(V3 *v, int m, const char *albedo);
static unsigned char *V3ReadMatTex(const wchar_t *dir, const VoxPart *p, int id, int *w, int *h, int *alpha);
static unsigned char *V3ReadTex(const wchar_t *modelPath, const char *name, int *w, int *h);
// a grey copy of a fur texture (brightness only, average about 0.8), keeping its opacity: the pattern colour is
// drawn through it so the strands keep their light and dark
static int V3LumTexFor(V3 *v, int m, const char *albedo) {
    const char *opc = m < v->part.nmat ? v->part.texOpc[m] : "";
    char key[200]; snprintf(key, sizeof key, "lum|%s|%s", albedo, opc);
    for (int t = 0; t < v->ntex; t++) if (!strcmp(v->tex[t].key, key)) return v->tex[t].rgba || v->tex[t].id ? t : -1;
    if (v->ntex >= V3_MAXTEX) return -1;
    VoxPart *tmp = (VoxPart *)calloc(1, sizeof(VoxPart)); if (!tmp) return -1;
    snprintf(tmp->texAlb[0], sizeof tmp->texAlb[0], "%s", albedo); snprintf(tmp->texOpc[0], sizeof tmp->texOpc[0], "%s", opc);
    int w = 0, h = 0, alpha = 0; unsigned char *px = V3ReadMatTex(v->dir, tmp, 0, &w, &h, &alpha); free(tmp);
    int k = v->ntex++; snprintf(v->tex[k].key, sizeof v->tex[k].key, "%s", key);
    if (!px) return -1;
    size_t cnt = (size_t)w * h; double sum = 0;
    for (size_t i = 0; i < cnt; i++) sum += 0.299 * px[i * 4] + 0.587 * px[i * 4 + 1] + 0.114 * px[i * 4 + 2];
    double mean = cnt ? sum / cnt : 128; if (mean < 4) mean = 4;
    for (size_t i = 0; i < cnt; i++) {
        double l = (0.299 * px[i * 4] + 0.587 * px[i * 4 + 1] + 0.114 * px[i * 4 + 2]) / mean * 0.8 * 255; if (l > 255) l = 255;
        px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = (unsigned char)l; if (!alpha) px[i * 4 + 3] = 255;
    }
    v->tex[k].rgba = px; v->tex[k].w = w; v->tex[k].h = h; v->tex[k].alpha = 1;
    V3UploadTextures(v);
    return k;
}
/* the pattern of a gradient fur on part m: the mask projected from the front onto each vertex (bind pose), giving
   each vertex a colour multiplier. The game blends per pixel in its shader; here the pattern colour is matched to
   the fur texture's brightness and given per vertex, which suits the dense fur cards. */
static int V3ApplyPattern(V3 *v, const BfColTrack *T) {
    int m = T->part; VoxPart *p = &v->part; if (m >= 256 || !p->tv || !p->ti) return 0;
    int mw = 0, mh = 0; unsigned char *mask = V3ReadTex(v->dir, T->mask, &mw, &mh); if (!mask) return 0;
    int lt = V3LumTexFor(v, m, T->albedo[0] ? T->albedo : p->texAlb[m]); if (lt < 0) { free(mask); return 0; }
    if (!v->patOn) { v->patOn = (unsigned char *)calloc(p->ntv, 1); v->patRGB = (unsigned char *)calloc(p->ntv, 4); if (!v->patOn || !v->patRGB) { free(mask); return 0; } }
    // the pattern colour, on the same scale as the fur colours (0.25 = the neutral strength these clips use)
    float pc[3]; for (int c = 0; c < 3; c++) { float x = T->pCol[c] / 0.25f; pc[c] = powf(x > 0 ? (x > 1 ? 1 : x) : 0, 1 / 2.2f); }
    float a = T->pCol[3] > 1 ? 1 : T->pCol[3];
    for (size_t t = 0; t < p->nti; t++) {
        if (p->tmat[t] != m) continue;
        for (int k = 0; k < 3; k++) {
            uint32_t i = p->ti[t * 3 + k]; if (i >= p->ntv) continue;
            float x = p->tv[i * 3], y = p->tv[i * 3 + 1];
            float mu = (x - T->pOff[0]) / T->pScale[0] + 0.5f, mv = 1 - ((y - T->pOff[1]) / T->pScale[1] + 0.5f);
            mu = mu < 0 ? 0 : mu > 1 ? 1 : mu; mv = mv < 0 ? 0 : mv > 1 ? 1 : mv;
            float fx = mu * (mw - 1), fy = mv * (mh - 1); int x0 = (int)fx, y0 = (int)fy, x1 = x0 + 1 < mw ? x0 + 1 : x0, y1 = y0 + 1 < mh ? y0 + 1 : y0;
            float tx = fx - x0, ty = fy - y0;
            float w = ((mask[((size_t)y0 * mw + x0) * 4] * (1 - tx) + mask[((size_t)y0 * mw + x1) * 4] * tx) * (1 - ty) +
                       (mask[((size_t)y1 * mw + x0) * 4] * (1 - tx) + mask[((size_t)y1 * mw + x1) * 4] * tx) * ty) / 255.f * a;
            for (int c = 0; c < 3; c++) v->patRGB[i * 4 + c] = (unsigned char)(pc[c] * 255 + 0.5f);
            v->patRGB[i * 4 + 3] = (unsigned char)(w < 0 ? 0 : w > 1 ? 255 : w * 255 + 0.5f);
            v->patOn[i] = 1;
        }
    }
    free(mask); v->partPat[m] = 1; v->patTex[m] = lt;
    return 1;
}
static void V3ClearPattern(V3 *v, int m) {
    if (!v->patOn || m >= VOX_MAXMAT || !v->partPat[m]) return;
    for (size_t t = 0; t < v->part.nti; t++) if (v->part.tmat[t] == m) for (int k = 0; k < 3; k++) { uint32_t i = v->part.ti[t * 3 + k]; if (i < v->part.ntv) v->patOn[i] = 0; }
    v->partPat[m] = 0;
}
// pick colour clip k (0 = as modelled): a still one is layered onto the parts it names; an animated one plays on a loop
static void V3PickColour(V3 *v, int k) {
    if (k <= 0 || k > v->ncol) { V3ColReset(v); InvalidateRect(v->gl, NULL, FALSE); return; }
    struct V3Col *c = &v->col[k - 1];
    BfColAnim a; if (!BfColAnimLoad(&v->colF[c->f], c->i, &v->part, &a)) return;
    v->origSaved = 1;                                    // (colours are now drawn per part)
    if (a.animated) {
        if (v->fxOn) BfColAnimFree(&v->fx);
        v->fx = a; v->fxOn = 1; v->fxT0 = GetTickCount(); SetTimer(v->wnd, V3_TIMER_FX, 16, NULL);
    } else {
        int rebuild = 0;
        for (int t = 0; t < a.n; t++) {
            const BfColTrack *T = &a.t[t]; int m = T->part; if (m >= 256) continue;
            if (v->partPat[m]) { V3ClearPattern(v, m); rebuild = 1; }
            if (T->hasPat && V3ApplyPattern(v, T)) rebuild = 1;
            if (T->albedo[0]) {
                int tx = V3TexForPart(v, m, T->albedo);
                if (tx >= 0) {
                    if (!v->colChanged[m]) { v->origTex[m] = v->partTex[m]; snprintf(v->origAlb[m], 128, "%s", v->part.texAlb[m]); v->colChanged[m] = 1; }
                    v->partTex[m] = tx; snprintf(v->part.texAlb[m], sizeof v->part.texAlb[m], "%s", T->albedo);
                }
            }
            // basecolor_const: these clips use 0.25 as normal strength (Leaf-Green, 0.25, is as bright in the game as the
            // natural fur, 1.0); Cloud-White 0.4 is brighter, Sesame 0.1 darker
            int allOne = T->has[0] && T->v[0][0] == 1 && T->v[0][1] == 1 && T->v[0][2] == 1;
            for (int ch = 0; ch < 3; ch++) { v->lookTint[m][ch] = T->has[0] && !allOne ? T->v[0][ch] / 0.25f : 1; v->lookEmis[m][ch] = (T->has[1] ? T->v[1][ch] : 0) + (T->has[2] ? T->v[2][ch] : 0); }
        }
        BfColAnimFree(&a);
        if (rebuild) V3RequestBuild(v);                  // the pattern goes into the vertex colours
    }
    InvalidateRect(v->gl, NULL, FALSE);
}
// a part's colour multiplier and glow for drawing (the game's values are linear and can exceed 1: brighter than
// white goes into the glow)
static void V3PartColour(V3 *v, int m, float fxFrame, float tint[3], float em[3]) {
    float base[3] = { 1, 1, 1 }, glow[3] = { 0, 0, 0 };
    if (m < 256) for (int ch = 0; ch < 3; ch++) { base[ch] = v->lookTint[m][ch]; glow[ch] = v->lookEmis[m][ch]; }
    if (v->fxOn) for (int t = 0; t < v->fx.n; t++) if (v->fx.t[t].part == m) {
        float o[3][3]; BfColAt(&v->fx, &v->fx.t[t], fxFrame, o);
        for (int ch = 0; ch < 3; ch++) { if (v->fx.t[t].has[0]) base[ch] *= o[0][ch]; glow[ch] += o[1][ch] + o[2][ch]; }
    }
    for (int ch = 0; ch < 3; ch++) {
        float d = powf(base[ch] > 0 ? base[ch] : 0, 1 / 2.2f), g = powf(glow[ch] > 0 ? glow[ch] : 0, 1 / 2.2f);
        tint[ch] = d > 1 ? 1 : d; g += d > 1 ? (d - 1) * 0.3f : 0; em[ch] = g > 1 ? 1 : g;
    }
}
static void V3FillColours(V3 *v) {
    SendMessageW(v->colCb, CB_RESETCONTENT, 0, 0);
    {   wchar_t t0[300]; swprintf(t0, 300, L"Colours: as modelled%ls%hs%ls", v->colNameErr[0] ? L"   (no fur names: " : L"", v->colNameErr, v->colNameErr[0] ? L")" : L"");
        SendMessageW(v->colCb, CB_ADDSTRING, 0, (LPARAM)t0); }
    for (int i = 0; i < v->ncol; i++) {
        wchar_t t[200]; swprintf(t, 200, L"%hs%ls%ls%ls", v->col[i].name, v->col[i].title[0] ? L" \x2013 " : L"", v->col[i].title, v->col[i].animated ? L"  (animated)" : L"");
        if (!strncmp(v->col[i].name, "FurMaxColor", 11) && v->col[i].title[0]) wcscat(t, L" (full adrenaline)");
        SendMessageW(v->colCb, CB_ADDSTRING, 0, (LPARAM)t);
    }
    SendMessageW(v->colCb, CB_SETCURSEL, 0, 0);
}

static int V3AnimNames(const wchar_t *base, wchar_t out[8][300]);
// load model 'name' and join it to the bone 'bone'; returns the new attachment's index or -1
static int V3AttachOne(V3 *v, const wchar_t *name, int bone, const char *label, int shown, const char extraAnim[2][64], int nExtra, const float *fix) {
    wchar_t path[1024];
    if (bone < 0 || v->nAtt >= 16 || !V3FindModel(v, name, path)) return -1;
    VoxPart *sub = (VoxPart *)calloc(1, sizeof(VoxPart)); if (!sub) return -1;     // (too big for the stack)
    if (!VoxLoadBfres(sub, path)) { VoxFreePart(sub); free(sub); return -1; }
    float Tb[12]; int partLo = 0, lo = VoxAttachModel(&v->part, sub, bone, label, &partLo, Tb, fix);
    VoxFreePart(sub); free(sub);
    if (lo < 0) return -1;
    int a = v->nAtt++;
    v->att[a].lo = lo; v->att[a].hi = v->part.skN; v->att[a].partLo = partLo; v->att[a].partHi = v->part.nmat;
    v->att[a].defShown = shown; snprintf(v->att[a].name, 64, "%ls", name); memcpy(v->att[a].Tb, Tb, 48);
    if (!shown) for (int m = partLo; m < v->part.nmat; m++) v->part.startHidden[m] = 1;
    // its animations: the ones its actor names (ModelParam AnimArc3), then the same discovery as for a model on its
    // own (DonkeyKongHead -> PlayerHeadAnimation...)
    wchar_t names[10][300]; int nn = 0;
    for (int e = 0; e < nExtra && nn < 2; e++) swprintf(names[nn++], 300, L"%hs", extraAnim[e]);
    // parts from an actor pack: only their own banks (<name>.anim, <name>Animation). The first-word guess would
    // give PaulineHair the body's PaulineAnimation, whose Root / Skl_Root moves flung the hair off her head.
    if (fix) { swprintf(names[nn++], 300, L"%ls", name); swprintf(names[nn++], 300, L"%lsAnimation", name); }
    else nn += V3AnimNames(name, (wchar_t (*)[300])names[nn]);
    wchar_t roots[12][1024]; int nr = V3RomfsRoots(v->dir, roots);
    for (int k = 0; k < nn && v->att[a].nfi < 4; k++) {
        wchar_t ap[1024]; swprintf(ap, 1024, L"%ls", v->dir);
        wchar_t *slash = wcsrchr(ap, L'\\'); if (slash) swprintf(slash + 1, 1024 - (slash + 1 - ap), L"%ls.anim.bfres.zs", names[k]);
        int found = GetFileAttributesW(ap) != INVALID_FILE_ATTRIBUTES;
        for (int r = 0; r < nr && !found; r++) { swprintf(ap, 1024, L"%ls\\Model\\%ls.anim.bfres.zs", roots[r], names[k]); found = GetFileAttributesW(ap) != INVALID_FILE_ATTRIBUTES; }
        if (!found) continue;
        int fi = V3AttAnimFile(v, ap), dup = 0;
        for (int q = 0; q < v->att[a].nfi; q++) if (v->att[a].fi[q] == fi) dup = 1;
        if (fi >= 0 && !dup) v->att[a].fi[v->att[a].nfi++] = fi;
    }
    return a;
}

/* Actor packs (Pack\Actor\X.pack.zs) say how the game puts a character together: SubActorParam's CreatorList names
   each sub-actor (ArchiveName) and the bone it is fixed to (FixInfo.JointName); the sub-actor's own ModelParam gives
   its model (ModelArcName) and animation banks (AnimArc3). Pauline: body -> PaulineHead on NeckPoint -> PaulineHair
   on the head's Head bone. */
typedef struct { unsigned char *sarc; size_t n; } V3Pack;
static int V3OpenPack(V3 *v, const char *actor, V3Pack *pk) {
    wchar_t roots[12][1024]; int nr = V3RomfsRoots(v->dir, roots);
    pk->sarc = NULL; pk->n = 0;
    for (int r = nr - 1; r >= 0 && !pk->sarc; r--) {      // later roots are updates: prefer them
        wchar_t p[1200]; swprintf(p, 1200, L"%ls\\Pack\\Actor\\%hs.pack.zs", roots[r], actor);
        size_t n; unsigned char *b = ArcReadAll(p, &n); if (!b) continue;
        pk->sarc = ArcUnzstd(b, n, &pk->n);
        if (pk->sarc && (pk->n < 4 || memcmp(pk->sarc, "SARC", 4))) { free(pk->sarc); pk->sarc = NULL; }
    }
    return pk->sarc != NULL;
}
typedef struct { const char *suffix; unsigned char *out; size_t n; } V3PackFind_;
static int V3PackFindCb_(const char *name, const unsigned char *d, size_t n, void *u) {
    V3PackFind_ *f = (V3PackFind_ *)u; size_t ln = strlen(name), ls = strlen(f->suffix);
    if (ln < ls || strcmp(name + ln - ls, f->suffix)) return 0;
    f->out = (unsigned char *)malloc(n + 1); if (f->out) { memcpy(f->out, d, n); f->n = n; }
    return 1;
}
static unsigned char *V3PackMember(V3Pack *pk, const char *suffix, size_t *n) {
    V3PackFind_ f = { suffix, NULL, 0 }; ArcSarcEach(pk->sarc, pk->n, V3PackFindCb_, &f); *n = f.n; return f.out;
}
// an actor's model name and up to 2 animation bank names (from "Work/ActorData/Model/Npc/PaulineAnimation/workspace...")
static void V3ActorModel(V3 *v, const char *actor, char model[64], char anim[2][64], int *nAnim) {
    snprintf(model, 64, "%s", actor); *nAnim = 0;
    V3Pack pk; if (!V3OpenPack(v, actor, &pk)) return;
    size_t n; unsigned char *b = V3PackMember(&pk, "__Actor__ModelParam.bgyml", &n);
    if (b) {
        Byml y; BymlNode root = BymlOpen(&y, b, n);
        const char *m = BymlStr(&y, BymlGet(&y, root, "ModelArcName")); if (m && *m) snprintf(model, 64, "%s", m);
        BymlNode arr = BymlGet(&y, root, "AnimArc3");
        for (int i = 0; i < BymlCount(&y, arr) && *nAnim < 2; i++) {
            const char *ap = BymlStr(&y, BymlGet(&y, BymlAt(&y, arr, i), "ArchivePath")); if (!ap) continue;
            const char *e = strstr(ap, "/workspace"); if (!e) continue;
            const char *s0 = e; while (s0 > ap && s0[-1] != '/') s0--;
            if (e - s0 > 0 && e - s0 < 64) { memcpy(anim[*nAnim], s0, e - s0); anim[*nAnim][e - s0] = 0; (*nAnim)++; }
        }
        free(b);
    }
    free(pk.sarc);
}
// returns -1 if the model has no actor pack (then the old name rules are used), else the number of models joined
static int V3AttachSubActors(V3 *v, const char *actor) {
    struct { char actor[64]; int parent; } q[16]; int nq = 0, done = 0;
    V3Pack pk0; if (!V3OpenPack(v, actor, &pk0)) return -1; free(pk0.sarc);
    snprintf(q[nq].actor, 64, "%s", actor); q[nq].parent = -1; nq++;
    for (int qi = 0; qi < nq; qi++) {
        V3Pack pk; if (!V3OpenPack(v, q[qi].actor, &pk)) continue;
        size_t n; unsigned char *b = V3PackMember(&pk, "SubActorParam.bgyml", &n);
        free(pk.sarc);
        if (!b) continue;
        Byml y; BymlNode root = BymlOpen(&y, b, n), list = BymlGet(&y, root, "CreatorList");
        for (int i = 0; i < BymlCount(&y, list) && v->nAtt < 16; i++) {
            BymlNode c = BymlAt(&y, list, i), fix = BymlGet(&y, c, "FixInfo");
            const char *arc = BymlStr(&y, BymlGet(&y, c, "ArchiveName")), *joint = BymlStr(&y, BymlGet(&y, fix, "JointName"));
            if (!arc || !*arc || !joint || !*joint) continue;
            // the joint is looked up on the parent's own model (the head's Head bone, not the body's)
            int lo = 0, hi = v->bodyN, bone = -1;
            if (q[qi].parent >= 0) { lo = v->att[q[qi].parent].lo; hi = v->att[q[qi].parent].hi; }
            for (int k = lo; k < hi && k < v->part.skN; k++) if (!strcmp(v->part.skName[k], joint)) { bone = k; break; }
            if (bone < 0) continue;
            char model[64], anim[2][64]; int na; V3ActorModel(v, arc, model, anim, &na);
            const char *lab = arc; size_t al = strlen(actor); if (!strncmp(arc, actor, al) && arc[al]) lab = arc + al;   // "Head", "Hair"
            char label[40]; snprintf(label, sizeof label, "%s", lab);
            wchar_t wm[300]; swprintf(wm, 300, L"%hs", model);
            // placed as the game does: the joint times LocalRotate (degrees) and LocalTrans
            BymlNode lr = BymlGet(&y, fix, "LocalRotate"), lt = BymlGet(&y, fix, "LocalTrans");
            float fx[6] = { BymlFloat(BymlGet(&y, lr, "X"), 0), BymlFloat(BymlGet(&y, lr, "Y"), 0), BymlFloat(BymlGet(&y, lr, "Z"), 0),
                            BymlFloat(BymlGet(&y, lt, "X"), 0), BymlFloat(BymlGet(&y, lt, "Y"), 0), BymlFloat(BymlGet(&y, lt, "Z"), 0) };
            int a = V3AttachOne(v, wm, bone, label, BymlBool(BymlGet(&y, c, "IsAlive"), 1), (const char (*)[64])anim, na, fx);
            if (a < 0) continue;
            done++;
            if (nq < 16) { snprintf(q[nq].actor, 64, "%s", arc); q[nq].parent = a; nq++; }
        }
        free(b);
    }
    return done;
}

// the fur names players see ("Fractone Gradient Fur"): Glossary/Cloth.msbt in the newest English message archive.
// FurColorNN / FurMaxColorNN = Player_Color_NN_A
// every member of a message archive whose name ends in Cloth.msbt (Glossary/Cloth.msbt holds the names; an NPC's
// NpcMsg/.../Cloth.msbt is something else): copies, Glossary first
typedef struct { char name[128]; unsigned char *d; size_t n; } V3MsgPick;
typedef struct { V3MsgPick p[8]; int n; } V3MsgPicks;
static int V3ClothCb_(const char *name, const unsigned char *d, size_t n, void *u) {
    V3MsgPicks *q = (V3MsgPicks *)u; size_t ln = strlen(name);
    if (ln < 10 || strcmp(name + ln - 10, "Cloth.msbt") || q->n >= 8) return 0;
    V3MsgPick *k = &q->p[q->n]; k->d = (unsigned char *)malloc(n + 1); if (!k->d) return 0;
    memcpy(k->d, d, n); k->n = n; snprintf(k->name, sizeof k->name, "%s", name);
    if (strstr(name, "Glossary") && q->n) { V3MsgPick t = q->p[0]; q->p[0] = *k; *k = t; }
    q->n++; return 0;
}
// does message label lab name fur colour num? "Player_Color_32_A", or another spelling with "Color" and the number
static int V3FurLabel(const char *lab, int num) {
    const char *c = strstr(lab, "Color"); if (!c) return 0;
    for (const char *q = c + 5; *q; q++) if (*q >= '0' && *q <= '9') { return atoi(q) == num && (q == lab || q[-1] < '0' || q[-1] > '9'); }
    return 0;
}
static void V3ColourNames(V3 *v) {
    // every English archive (USen, EUen, AUen) in every romfs root, newest first: the base game's Cloth.msbt has
    // older labels, the update's has Player_Color_NN_A
    wchar_t roots[12][1024]; int nr = V3RomfsRoots(v->dir, roots);
    static const wchar_t *langs[3] = { L"USen", L"EUen", L"AUen" };
    struct { wchar_t path[1200]; int ver, lang; } arc[24]; int na = 0;
    for (int r = 0; r < nr; r++) for (int l = 0; l < 3; l++) {
        wchar_t pat[1200]; swprintf(pat, 1200, L"%ls\\Mals\\%ls.Product.*.sarc.zs", roots[r], langs[l]);
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd); if (h == INVALID_HANDLE_VALUE) continue;
        do { if (na < 24) { const wchar_t *pv = wcsstr(fd.cFileName, L".Product."); arc[na].ver = pv ? _wtoi(pv + 9) : 0; arc[na].lang = l;
                            swprintf(arc[na].path, 1200, L"%ls\\Mals\\%ls", roots[r], fd.cFileName); na++; } } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (!na) { snprintf(v->colNameErr, sizeof v->colNameErr, "no English .Product.*.sarc.zs in romfs\\Mals"); return; }
    for (int a = 0; a < na; a++) for (int b2 = a + 1; b2 < na; b2++)               // best first: language, then newest
        if (arc[b2].lang < arc[a].lang || (arc[b2].lang == arc[a].lang && arc[b2].ver > arc[a].ver)) { char t[sizeof arc[0]]; memcpy(t, &arc[a], sizeof t); memcpy(&arc[a], &arc[b2], sizeof t); memcpy(&arc[b2], t, sizeof t); }
    int found = 0; char seen[160] = "";
    for (int a = 0; a < na && !found; a++) {
        size_t n; unsigned char *b = ArcReadAll(arc[a].path, &n); if (!b) continue;
        size_t sn; unsigned char *sarc = ArcUnzstd(b, n, &sn); if (!sarc) continue;
        V3MsgPicks q; memset(&q, 0, sizeof q); ArcSarcEach(sarc, sn, V3ClothCb_, &q); free(sarc);
        for (int c = 0; c < q.n; c++) {
            Msbt m; if (!MsbtParse(&m, q.p[c].d, q.p[c].n)) continue;
            int here = 0;
            for (int i = 0; i < v->ncol; i++) {
                int num; const char *nm = v->col[i].name;
                if (sscanf(nm, "FurColor%d", &num) != 1 && sscanf(nm, "FurMaxColor%d", &num) != 1) continue;
                int pick = -1;
                for (int e = 0; e < m.n; e++) if (m.e[e].label && m.e[e].b && V3FurLabel(m.e[e].label, num)) {
                    if (pick < 0) pick = e;
                    const char *L = m.e[e].label; size_t ll = strlen(L); if (ll > 2 && !strcmp(L + ll - 2, "_A")) { pick = e; break; }   // the name (_B / _C are descriptions)
                }
                if (pick < 0) continue;
                int at = 0; const unsigned char *u = m.e[pick].b;
                for (int k = 0; k + 1 < m.e[pick].nb && at < 63; ) {       // the text, without tags
                    unsigned ch = u[k] | (u[k + 1] << 8);
                    if (ch == 0x0E && k + 8 <= m.e[pick].nb) { k += 8 + (u[k + 6] | (u[k + 7] << 8)); continue; }
                    if (ch == 0x0F && k + 6 <= m.e[pick].nb) { k += 6; continue; }
                    if (ch == 0 || ch == '\n') break;
                    v->col[i].title[at++] = (wchar_t)ch; k += 2;
                }
                v->col[i].title[at] = 0; here++;
            }
            if (!here && strlen(seen) < 100) { size_t l = strlen(seen); snprintf(seen + l, sizeof seen - l, "%s%s (%d labels)", l ? ", " : "", q.p[c].name, m.n); }
            found += here;
            MsbtFree(&m);
            if (here) break;
        }
        for (int c = 0; c < q.n; c++) free(q.p[c].d);
    }
    if (found) v->colNameErr[0] = 0;
    else snprintf(v->colNameErr, sizeof v->colNameErr, "%s", seen[0] ? seen : "no Cloth.msbt in the English archives");
}
// the model's helper bones: Phive\HelperBone.alsarc.zs\<name>.bphhb (DonkeyKongNaked uses DonkeyKong's)
static void V3LoadHelperBones(V3 *v, const char *base) {
    wchar_t roots[12][1024]; int nr = V3RomfsRoots(v->dir, roots);
    char names[2][64]; int nn = 0; snprintf(names[nn++], 64, "%s.bphhb", base);
    if (!strncmp(base, "DonkeyKong", 10) && strcmp(base, "DonkeyKong")) snprintf(names[nn++], 64, "DonkeyKong.bphhb");
    for (int r = nr - 1; r >= 0; r--) {
        wchar_t pth[1200]; swprintf(pth, 1200, L"%ls\\Phive\\HelperBone.alsarc.zs", roots[r]);
        size_t n; unsigned char *b = ArcReadAll(pth, &n); if (!b) continue;
        size_t sn; unsigned char *sarc = ArcUnzstd(b, n, &sn); if (!sarc) continue;
        for (int k = 0; k < nn; k++) {
            size_t mn; unsigned char *m = ArcSarcMember(sarc, sn, names[k], &mn);
            if (!m) continue;
            HelperBones h; if (HbParse(&h, m, mn) && HbBind(&h, &v->part) > 0) { v->hb = h; snprintf(v->hbFile, sizeof v->hbFile, "%s", names[k]); free(m); free(sarc); return; }
            HbFree(&h); free(m);
        }
        free(sarc);
    }
}

// DK (and the Bananza forms) are put together from several models: the body, a head on the body's NeckPoint bone and
// a tie on TiePoint. Load them and join them to the body; all ties are added, only the first is shown.
static void V3AttachParts(V3 *v) {
    const wchar_t *fn = wcsrchr(v->dir, L'\\'); fn = fn ? fn + 1 : v->dir;
    wchar_t base[300]; swprintf(base, 300, L"%ls", fn); wchar_t *dot = wcschr(base, L'.'); if (dot) *dot = 0;
    if (wcsstr(base, L"Head") || wcsstr(base, L"Tie") || wcsstr(base, L"Helmet")) return;
    if (wcsncmp(base, L"DonkeyKong", 10)) {            // other characters: as their actor packs say, if found
        char ab[300]; snprintf(ab, sizeof ab, "%ls", base);
        if (V3AttachSubActors(v, ab) >= 0) { v->attOn = v->nAtt > 0; return; }
    }
    struct { wchar_t name[300]; const char *bone, *label; int shown; } want[20]; int nw = 0;
    wchar_t path[1024];
    swprintf(want[nw].name, 300, L"%lsHead", base);
    if (!V3FindModel(v, want[nw].name, path) && !wcsncmp(base, L"DonkeyKong", 10)) swprintf(want[nw].name, 300, L"DonkeyKongHead");
    want[nw].bone = "NeckPoint"; want[nw].label = "Head"; want[nw].shown = 1; nw++;
    if (!wcscmp(base, L"DonkeyKong"))
        for (int t = 0; t <= 15 && nw < 20; t++) { swprintf(want[nw].name, 300, L"DonkeyKongTie%02d", t); want[nw].bone = "TiePoint"; want[nw].label = NULL; want[nw].shown = t == 0; nw++; }
    for (int w = 0; w < nw && v->nAtt < 16; w++) {
        char label[40]; if (want[w].label) snprintf(label, sizeof label, "%s", want[w].label); else snprintf(label, sizeof label, "%ls", want[w].name + 10);  // "Tie03"
        V3AttachOne(v, want[w].name, BfBoneIndex(&v->part, want[w].bone), label, want[w].shown, NULL, 0, NULL);
    }
    v->attOn = v->nAtt > 0;
    // the body has two neck bands under the tie knot: Tie (coloured to match the tie) and Tie_09 (grey, for tie 09
    // only). With Tie00 shown, start with Tie_09's band hidden.
    if (v->nAtt) for (int m = 0; m < v->part.nmat; m++) if (!strncmp(v->part.matName[m], "Tie_09__", 8)) v->part.startHidden[m] = 1;
}


/* DK's neck band (body part Tie__Mt_Tie) is coloured to match the tie: the game swaps its colour texture to
   DonkeyKongTie_Alb.<tie number>. Tie 09 uses the body's own Tie_09 band instead; ties whose texture is a 1x1
   placeholder (10, 11) bring their own neck piece, so the band is hidden. */
static int V3TexFor(V3 *v, const char *name) {          // tex[] index for a colour texture by name, loaded on demand
    for (int t = 0; t < v->ntex; t++) if (!strncmp(v->tex[t].key, name, strlen(name)) && v->tex[t].key[strlen(name)] == '|') return v->tex[t].rgba || v->tex[t].id ? t : -1;
    if (v->ntex >= V3_MAXTEX) return -1;
    int k = v->ntex++; snprintf(v->tex[k].key, sizeof v->tex[k].key, "%s|", name);
    int w = 0, h = 0; unsigned char *px = V3ReadTex(v->dir, name, &w, &h);
    if (!px) return -1;
    v->tex[k].rgba = px; v->tex[k].w = w; v->tex[k].h = h; v->tex[k].alpha = 0;
    V3UploadTextures(v);
    return k;
}
// part m's colour texture with another albedo, keeping its opacity map (fur cards) - loaded on demand
static int V3TexForPart(V3 *v, int m, const char *albedo) {
    const char *opc = m < v->part.nmat ? v->part.texOpc[m] : "";
    if (!opc[0]) return V3TexFor(v, albedo);
    char key[200]; snprintf(key, sizeof key, "%s|%s", albedo, opc);
    for (int t = 0; t < v->ntex; t++) if (!strcmp(v->tex[t].key, key)) return v->tex[t].rgba || v->tex[t].id ? t : -1;
    if (v->ntex >= V3_MAXTEX) return -1;
    VoxPart *tmp = (VoxPart *)calloc(1, sizeof(VoxPart)); if (!tmp) return -1;   // V3ReadMatTex reads names from a part
    snprintf(tmp->texAlb[0], sizeof tmp->texAlb[0], "%s", albedo); snprintf(tmp->texOpc[0], sizeof tmp->texOpc[0], "%s", opc);
    int w = 0, h = 0, alpha = 0; unsigned char *px = V3ReadMatTex(v->dir, tmp, 0, &w, &h, &alpha); free(tmp);
    int k = v->ntex++; snprintf(v->tex[k].key, sizeof v->tex[k].key, "%s", key);
    if (!px) return -1;
    v->tex[k].rgba = px; v->tex[k].w = w; v->tex[k].h = h; v->tex[k].alpha = alpha;
    V3UploadTextures(v);
    return k;
}
static int V3UpdateTieNeck(V3 *v) {                       // returns 1 if any part's visibility changed
    VoxPart *p = &v->part; int band = -1, band9 = -1;
    for (int m = 0; m < p->nmat; m++) { if (!strncmp(p->matName[m], "Tie__", 5)) band = m; else if (!strncmp(p->matName[m], "Tie_09__", 8)) band9 = m; }
    if (band < 0 && band9 < 0) return 0;
    // which tie is showing: a newly ticked one wins, else the one already chosen, else the first visible
    int shown[16] = { 0 }, pick = -1;
    for (int a = 0; a < v->nAtt; a++) {
        if (strncmp(v->att[a].name, "DonkeyKongTie", 13)) continue;
        for (int m = v->att[a].partLo; m < v->att[a].partHi; m++) if (!v->hidden[m]) { shown[a] = 1; break; }
        if (shown[a] && !v->tieWasShown[a]) pick = a;
    }
    if (pick < 0 && v->tieSel >= 0 && v->tieSel < v->nAtt && shown[v->tieSel]) pick = v->tieSel;
    for (int a = 0; a < v->nAtt && pick < 0; a++) if (shown[a]) pick = a;
    for (int a = 0; a < v->nAtt; a++) v->tieWasShown[a] = shown[a];
    v->tieSel = pick;
    int num = pick >= 0 ? atoi(v->att[pick].name + 13) : -1;
    int showBand = 0, showBand9 = 0;
    if (pick >= 0 && v->attOn) {
        if (num == 9) showBand9 = 1;
        else {
            char tn[64]; snprintf(tn, sizeof tn, "DonkeyKongTie_Alb.%d", num);
            int k = V3TexFor(v, tn);
            if (k < 0) { k = V3TexFor(v, "DonkeyKongTie_Alb.0"); snprintf(tn, sizeof tn, "DonkeyKongTie_Alb.0"); }
            if (k >= 0 && v->tex[k].w <= 1 && v->tex[k].h <= 1) showBand = 0;      // placeholder: the tie has its own neck piece
            else {
                showBand = 1;
                if (band >= 0 && k >= 0) { v->partTex[band] = k; snprintf(p->texAlb[band], sizeof p->texAlb[band], "%s", tn); p->texOpc[band][0] = 0; }
            }
        }
    }
    int changed = 0;
    if (band >= 0 && v->hidden[band] != !showBand) { v->hidden[band] = (unsigned char)!showBand; changed = 1; }
    if (band9 >= 0 && v->hidden[band9] != !showBand9) { v->hidden[band9] = (unsigned char)!showBand9; changed = 1; }
    return changed;
}
/* ------------------------------------------------------------------ cloth physics overlay */
// Phive\Cloth\<name>.bphcl in this or another romfs folder (newest first)
static int V3FindCloth(V3 *v, const char *name, wchar_t *out) {
    wchar_t roots[12][1024]; int nr = V3RomfsRoots(v->dir, roots);
    for (int r = 0; r < nr; r++) for (int z = 0; z < 2; z++) {
        swprintf(out, 1024, L"%ls\\Phive\\Cloth\\%hs.bphcl%ls", roots[r], name, z ? L".zs" : L"");
        if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return 1;
    }
    return 0;
}
// overlay: draw it (particles, links, shapes); lo..hi: the bones of the model it belongs to, for the simulation
static int V3AddClothFile2(V3 *v, const wchar_t *path, const float *X, const char *prefix, int overlay, int lo, int hi) {
    size_t zn; unsigned char *z = VoxReadFile(path, &zn); if (!z) return 0;
    size_t n; unsigned char *b = VoxUnzstd(z, zn, &n); free(z); if (!b) return 0;
    ClothVis c; char err[200] = ""; int k = 0;
    if (ClothRead(&c, b, n, err, sizeof err)) {
        if (v->nsim < 16 && v->part.skN && ClothSimInit(&v->sim[v->nsim], &c, X, &v->part, lo, hi)) v->nsim++;
        if (!overlay) { ClothFree(&c); free(b); return 0; }
        k = VoxAddCloth(&v->part, &c, X, prefix);
        if (k > 0) {
            v->nCloth++;
            size_t l = strlen(v->clothInfo);
            snprintf(v->clothInfo + l, sizeof v->clothInfo - l, "%s%s (%d particles, %d shapes)", l ? ", " : "", c.name, c.np, c.nsh);
        }
    }
    ClothFree(&c); free(b);
    return k;
}
static int V3AddClothFile(V3 *v, const wchar_t *path, const float *X, const char *prefix) { return V3AddClothFile2(v, path, X, prefix, 1, 0, v->part.skN); }
// the model's own cloth, and its attached head's / default tie's (moved to where they are attached)
static void V3AddCloths(V3 *v, const char *base) {
    wchar_t p[1024];
    if (V3FindCloth(v, base, p)) V3AddClothFile2(v, p, NULL, "", 1, 0, v->bodyN ? v->bodyN : v->part.skN);
    for (int a = 0; a < v->nAtt; a++) {
        if (!V3FindCloth(v, v->att[a].name, p)) continue;
        char pre[40]; const char *nm = v->att[a].name; if (!strncmp(nm, "DonkeyKong", 10) && nm[10]) nm += 10;
        snprintf(pre, sizeof pre, "%s: ", nm);
        // the overlay for the attachments shown at the start; the simulation for all (every tie swings)
        V3AddClothFile2(v, p, v->att[a].Tb, pre, v->att[a].defShown, v->att[a].lo, v->att[a].hi);
    }
}
// a .bphcl opened on its own: the model of the same name with the cloth on it, or just the cloth
static int V3LoadClothView(V3 *v) {
    const wchar_t *fn = wcsrchr(v->dir, L'\\'); fn = fn ? fn + 1 : v->dir;
    wchar_t base[300]; swprintf(base, 300, L"%ls", fn); wchar_t *dot = wcschr(base, L'.'); if (dot) *dot = 0;
    wchar_t mp[1024];
    if (V3FindModel(v, base, mp) && VoxLoadBfres(&v->part, mp)) return 1;
    VoxFreePart(&v->part);
    v->part.isTriMesh = 1; v->part.isModel = 1; v->part.scale[0] = v->part.scale[1] = v->part.scale[2] = 1;
    snprintf(v->part.modelName, sizeof v->part.modelName, "%ls (no model of that name found - cloth only)", base);
    return 1;
}

/* ------------------------------------------------------------------ worker */
typedef struct { V3 *v; long gen; int lod; unsigned char hidden[VOX_MAXMAT]; int doLoad; } V3Job;

// the whole Mario Kart World terrain ("terrainworld") or one area of it ("terrainarea:x,z,size" in .tscb units),
// stitched from the tiles beside the .tscb. Low resolution: 1025 x 1025 points; full: 2049 x 2049.
static int V3IsWaterMat(const char *n) {
    static const char *k[] = { "Water", "Muddy water", "Lava", "Ghost swamp water", "Chocolate", "Aurora" };
    for (int i = 0; i < 6; i++) { size_t l = strlen(k[i]); if (!strncmp(n, k[i], l) && (!n[l] || n[l] == ':')) return 1; }
    return 0;
}
/* the romfs folder a terrain scene belongs to: ...\TerrainArc\<scene>.tscb (Mario Kart World) -> ...; any other
   <romfs>\<folder>\<scene>.tscb -> <romfs> (the folder above the scene's folder; a tile <folder>\<scene>\<t>.hght: one more).
   In place; 0 if it can't tell. */
static int V3SceneRoot(wchar_t *root) {
    wchar_t *ta = wcsstr(root, L"\\TerrainArc\\"); if (ta) { *ta = 0; return 1; }
    size_t L = wcslen(root); int up = L > 5 && !_wcsicmp(root + L - 5, L".tscb") ? 2 : 3;
    for (int k = 0; k < up; k++) { wchar_t *s = wcsrchr(root, L'\\'); if (!s) return 0; *s = 0; }
    return root[0] != 0;
}
// the scene's name (the .tscb's file name without ".tscb": "Game03_Proc", "DemoIsland")
static void V3SceneName(const wchar_t *dir, char *out, size_t n) {
    const wchar_t *f = wcsrchr(dir, L'\\'); f = f ? f + 1 : dir;
    snprintf(out, n, "%ls", f); char *d = strrchr(out, '.'); if (d && !_stricmp(d, ".tscb")) *d = 0;
}
static int V3IsTerrainGrid(const V3 *v) { return !wcscmp(v->geom, L"terrainworld") || !wcsncmp(v->geom, L"terrainarea:", 12); }
static void V3DrawArrows(V3 *v);
static void V3SetInfo(V3 *v, const wchar_t *t);
static float V3GridH(const VoxPart *p, float x, float z, int *in);
// the ground detail textures: Tex\CmnTex_Tera_Product_Proc_Alb.bntx.zs beside TerrainArc in the dump, one layer per
// material (kTeraMatLayer). Mostly grey patterns: scaled so their average is mid-grey, then drawn x2 over the colour map.
static void V3ParallelFor(int n, void (*fn)(void *, int), void *ctx);
typedef struct { int want, layer; const unsigned char *b; size_t n; const TexFile *tf; unsigned char *px; int w, h; const wchar_t *dir; } V3LayerJob;
static int V3SourceStamp(const wchar_t *modelPath, const char *name, uint64_t *sz, uint64_t *tm);
static void V3CachePath(const wchar_t *modelPath, const char *name, int size, const char *kind, wchar_t *out, size_t n);
static unsigned char *V3CacheLoad(const wchar_t *path, uint64_t sz, uint64_t tm, int *w, int *h, int *alpha);
static void V3CacheSave(const wchar_t *path, uint64_t sz, uint64_t tm, const unsigned char *px, int w, int h, int alpha);
// one ground detail layer: from the cache, else decoded, halved (512) and scaled so its average is mid-grey
static void V3LayerJobFn(void *ctx, int i) {
    V3LayerJob *j = ((V3LayerJob **)ctx)[i];
    char nm[64]; snprintf(nm, sizeof nm, "CmnTex_Tera_Product_Proc_Alb#%d", j->layer);
    uint64_t sz = 0, tm = 0; wchar_t cp[1200];
    int stamp = V3SourceStamp(j->dir, "CmnTex_Tera_Product_Proc_Alb", &sz, &tm);
    if (stamp) { V3CachePath(j->dir, nm, 512, "tera", cp, 1200); int a; j->px = V3CacheLoad(cp, sz, tm, &j->w, &j->h, &a); if (j->px) return; }
    if (!j->b) return;
    const TexInfo *t = &j->tf->t[0];
    int nl = t->layers > 0 ? t->layers : 1, L = j->layer < nl ? j->layer : nl - 1;
    size_t stride = t->size / (size_t)nl;
    TexInfo u = *t; u.size = (uint32_t)stride; u.data = t->data + (uint64_t)stride * L;
    char err[200]; unsigned char *px = TexDecode(j->b, j->n, &u, j->tf->ounce, err, sizeof err);
    if (!px) return;
    int W = t->w / 2, H = t->h / 2;
    unsigned char *h = (unsigned char *)malloc((size_t)W * H * 4);
    if (h) {
        double sum = 0;
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) for (int c = 0; c < 3; c++) {
            const unsigned char *q = px + ((size_t)(y * 2) * t->w + x * 2) * 4 + c;
            unsigned vv = (q[0] + q[4] + q[t->w * 4] + q[t->w * 4 + 4] + 2) / 4;
            h[((size_t)y * W + x) * 4 + c] = (unsigned char)vv; sum += vv;
        }
        double mean = sum / ((double)W * H * 3); float sc = mean > 1 ? (float)(128.0 / mean) : 1;
        for (size_t k = 0; k < (size_t)W * H; k++) {
            for (int c = 0; c < 3; c++) { float f = h[k * 4 + c] * sc; h[k * 4 + c] = (unsigned char)(f > 255 ? 255 : f); }
            h[k * 4 + 3] = 255;
        }
        if (stamp) V3CacheSave(cp, sz, tm, h, W, H, 0);
    }
    free(px);
    j->px = h; j->w = W; j->h = H;
}
static int V3IsWaterMat(const char *n);
static unsigned char *V3ReadTex(const wchar_t *modelPath, const char *name, int *w, int *h);
// water, muddy water, ghost swamp: the game's water texture (CmnTex_Tera_Water_Emm, first layer) as a grey ripple pattern over
// their colour, a little see-through (drawn last, blended). Lava keeps its plain colour.
static void V3LoadWaterTexture(V3 *v) {
    VoxPart *p = &v->part;
    int any = 0; for (int k = 0; k < p->nmat && k < VOX_MAXMAT; k++) { v->waterBlend[k] = 0; if (V3IsWaterMat(p->matName[k]) && strncmp(p->matName[k], "Lava", 4) && !strchr(p->matName[k], ':')) any = 1; }
    if (!any || !p->tuv) return;
    // CmnTex_Tera_Water_Wat is only a 7 x 1 colour table; the ripple / foam patterns are in _Emm (128 x 128, BC1, 3 layers)
    char sname[200]; const char *name = "CmnTex_Tera_Water_Emm";
    if (p->terrGeneric) { char sc[160]; V3SceneName(v->dir, sc, sizeof sc); snprintf(sname, sizeof sname, "%s_Water", sc); name = sname; }   // (another scene: Tex\<scene>_Water)
    char key[200]; snprintf(key, sizeof key, "water|%s", name);
    int k = -1; for (int t = 0; t < v->ntex; t++) if (!strcmp(v->tex[t].key, key)) k = t;
    if (k < 0 && v->ntex < V3_MAXTEX) {
        uint64_t sz = 0, tm = 0; wchar_t cp[1200]; int stamp = V3SourceStamp(v->dir, name, &sz, &tm), w = 0, h = 0, a = 0;
        unsigned char *px = NULL;
        if (stamp) { V3CachePath(v->dir, name, 256, "water", cp, 1200); px = V3CacheLoad(cp, sz, tm, &w, &h, &a); }
        if (!px) {
            int sw = 0, shh = 0; unsigned char *src = V3ReadTex(v->dir, name, &sw, &shh);
            if (src && sw > 0 && shh > 0) {
                w = sw; h = shh; while (w > 256 || h > 256) { w = w > 1 ? w / 2 : 1; h = h > 1 ? h / 2 : 1; }
                px = (unsigned char *)malloc((size_t)w * h * 4);
                if (px) {
                    int fx = sw / w, fy = shh / h; double sum = 0;
                    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {      // brightness only (it may be a normal map)
                        unsigned acc = 0;
                        for (int yy = 0; yy < fy; yy++) for (int xx = 0; xx < fx; xx++) { const unsigned char *q = src + ((size_t)(y * fy + yy) * sw + x * fx + xx) * 4; acc += (q[0] * 3 + q[1] * 6 + q[2]) / 10; }
                        unsigned g = acc / (fx * fy); px[((size_t)y * w + x) * 4] = (unsigned char)g; sum += g;
                    }
                    {   // soften the fine speckle: two 3 x 3 blurs (wrapping, the texture repeats)
                        unsigned char *t2 = (unsigned char *)malloc((size_t)w * h);
                        for (int pass = 0; pass < 2 && t2; pass++) {
                            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
                                unsigned a = 0; for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) a += px[((size_t)((y + dy + h) % h) * w + (x + dx + w) % w) * 4];
                                t2[(size_t)y * w + x] = (unsigned char)(a / 9);
                            }
                            for (size_t i = 0; i < (size_t)w * h; i++) px[i * 4] = t2[i];
                        }
                        free(t2);
                    }
                    double mean = sum / ((double)w * h); float sc = mean > 1 ? (float)(128.0 / mean) : 1;
                    for (size_t i = 0; i < (size_t)w * h; i++) {
                        float f = 128 + (px[i * 4] * sc - 128) * 0.22f;           // a faint ripple (stronger looked like tiles, not water)
                        unsigned char g = (unsigned char)(f < 0 ? 0 : f > 255 ? 255 : f);
                        px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = g; px[i * 4 + 3] = 200;   // 200/255: slightly see-through
                    }
                    if (stamp) V3CacheSave(cp, sz, tm, px, w, h, 1);
                }
            }
            free(src);
        }
        if (!px && (px = (unsigned char *)malloc(16)) != NULL) {     // texture not found: plain, still see-through
            w = h = 2; for (int i = 0; i < 4; i++) { px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = 128; px[i * 4 + 3] = 200; }
        }
        if (px) { k = v->ntex++; snprintf(v->tex[k].key, sizeof v->tex[k].key, "%s", key); v->tex[k].rgba = px; v->tex[k].w = w; v->tex[k].h = h; v->tex[k].alpha = 1; v->tex[k].id = 0; }
    }
    if (k < 0 || (!v->tex[k].rgba && !v->tex[k].id)) return;
    for (int m = 0; m < p->nmat && m < VOX_MAXMAT; m++)
        if (V3IsWaterMat(p->matName[m]) && strncmp(p->matName[m], "Lava", 4) && !strchr(p->matName[m], ':')) { v->partTex[m] = k; v->waterBlend[m] = 1; v->terrTex++; }
}
static void V3LoadTerrainTextures(V3 *v) {
    VoxPart *p = &v->part;
    for (int i = 0; i < VOX_MAXMAT; i++) { v->partTex[i] = -1; v->waterBlend[i] = 0; }
    v->terrTex = 0;
    if (p->overlay) return;                          // ground coloured by a layer: no ground / water textures over it
    int need = 0; for (int k = 0; k < p->nmat && k < VOX_MAXMAT; k++) if (p->terrMat[k] >= 0) need = 1;
    if (!need) return;
    // the layers still needed: from the cache if they're there; the 13 MB texture file is only read for the rest
    V3LayerJob lj[46]; memset(lj, 0, sizeof lj);
    V3LayerJob *todo[46]; int nt = 0;
    for (int mi = 0; mi < p->nmat && mi < VOX_MAXMAT; mi++) {
        int gm = p->terrMat[mi]; if (gm < 0 || gm >= 62) continue;
        int layer = p->terrGeneric ? (gm < 46 ? gm : 45) : kTeraMatLayer[gm];   // (another scene: material n = layer n) if (layer < 0 || layer >= 46 || lj[layer].want) continue;
        char key[200]; snprintf(key, sizeof key, "tera|%d", layer);
        int have = 0; for (int t = 0; t < v->ntex; t++) if (!strcmp(v->tex[t].key, key)) have = 1;
        if (!have) { lj[layer].want = 1; lj[layer].layer = layer; lj[layer].dir = v->dir; todo[nt++] = &lj[layer]; }
    }
    V3ParallelFor(nt, V3LayerJobFn, todo);                    // pass 1: cache only (no file given yet)
    int missing = 0; for (int k = 0; k < nt; k++) if (!todo[k]->px) missing = 1;
    unsigned char *b = NULL; size_t n = 0; TexFile tf; memset(&tf, 0, sizeof tf);
    if (missing) {
        wchar_t path[2100]; swprintf(path, 2100, L"%ls", v->dir);
        if (V3SceneRoot(path)) {
            // the ground texture array: Mario Kart World's, else another scene's own Tex\<scene>_Ground.bntx.zs (one layer per material)
            wchar_t rt[2100]; swprintf(rt, 2100, L"%ls", path); char sc[200]; V3SceneName(v->dir, sc, sizeof sc);
            if (p->terrGeneric) swprintf(path, 2100, L"%ls\\Tex\\%hs_Ground.bntx.zs", rt, sc);
            else swprintf(path, 2100, L"%ls\\Tex\\CmnTex_Tera_Product_Proc_Alb.bntx.zs", rt);
            size_t zn; unsigned char *z = VoxReadFile(path, &zn);
            if (z) {
                unsigned long long dn = ZSTD_getFrameContentSize(z, zn);
                if (dn != ZSTD_CONTENTSIZE_ERROR && dn != ZSTD_CONTENTSIZE_UNKNOWN && dn <= (256ull << 20)) {
                    b = (unsigned char *)malloc((size_t)dn + 16);
                    if (b && ZSTD_isError(ZSTD_decompress(b, (size_t)dn, z, zn))) { free(b); b = NULL; }
                    n = (size_t)dn;
                }
                free(z);
            }
            if (b && (!TexOpen(&tf, b, n) || tf.n < 1 || tf.t[0].layers < (p->terrGeneric ? 1 : 46))) { TexClose(&tf); free(b); b = NULL; }
        }
        if (b) {
            V3LayerJob *todo2[46]; int n2 = 0;
            for (int k = 0; k < nt; k++) if (!todo[k]->px) { todo[k]->b = b; todo[k]->n = n; todo[k]->tf = &tf; todo2[n2++] = todo[k]; }
            V3ParallelFor(n2, V3LayerJobFn, todo2);           // pass 2: decode the rest
        }
    }
    for (int mi = 0; mi < p->nmat && mi < VOX_MAXMAT; mi++) {
        int gm = p->terrMat[mi]; if (gm < 0 || gm >= 62) continue;
        int layer = p->terrGeneric ? (gm < 46 ? gm : 45) : kTeraMatLayer[gm];   // (another scene: material n = layer n)
        char key[200]; snprintf(key, sizeof key, "tera|%d", layer);
        int k = -1; for (int t = 0; t < v->ntex; t++) if (!strcmp(v->tex[t].key, key)) k = t;
        if (k < 0 && lj[layer].want && v->ntex < V3_MAXTEX) {
            k = v->ntex++; snprintf(v->tex[k].key, sizeof v->tex[k].key, "%s", key);
            v->tex[k].rgba = lj[layer].px; v->tex[k].w = lj[layer].w; v->tex[k].h = lj[layer].h; v->tex[k].alpha = 0; v->tex[k].id = 0;
            lj[layer].px = NULL;
        }
        if (k >= 0 && (v->tex[k].rgba || v->tex[k].id)) { v->partTex[mi] = k; v->terrTex++; }
    }
    for (int l = 0; l < 46; l++) free(lj[l].px);
    if (b) { TexClose(&tf); free(b); }
    V3LoadWaterTexture(v);
}
// Lava, chocolate, ghost-swamp... surfaces that aren't in the terrain's water layer: "special surface" objects placed
// in the world (Banc\World_Game03\WorldResident.bcett.byml.zs, actors CmnObj_SpecialSurface<Kind>000_<X>_00 with
// Translate / Rotate / Scale in world units). Those with a model (Model\<base>.<base>_<X>.bfres.zs) are added to the
// terrain as one material per kind. World x / z are the terrain's (centre 0); the terrain grid starts at -size / 2.
static const char *V3SurfaceKind(const char *n) {
    if (strstr(n, "Lava") || strstr(n, "Volcano") || strstr(n, "AirshipFortress") || strstr(n, "DinoBones")) return "Lava";
    if (strstr(n, "Ghost")) return "Ghost swamp water";
    if (strstr(n, "Choco")) return "Chocolate";
    if (strstr(n, "Aurora")) return "Aurora";
    return "Water";
}
/* ------------------------------------------------------------------ parallel loading helpers */
typedef struct { volatile LONG next; int n; void (*fn)(void *, int); void *ctx; } V3Par;
static DWORD WINAPI V3ParWorker(LPVOID a) {
    V3Par *P = (V3Par *)a;
    for (;;) { int i = (int)InterlockedIncrement(&P->next) - 1; if (i >= P->n) break; P->fn(P->ctx, i); }
    return 0;
}
// fn(ctx, i) for i = 0 .. n-1 on all the processor's cores
static void V3ParallelFor(int n, void (*fn)(void *, int), void *ctx) {
    if (n <= 0) return;
    SYSTEM_INFO si; GetSystemInfo(&si);
    int nt = (int)si.dwNumberOfProcessors; if (nt < 1) nt = 1; if (nt > 16) nt = 16; if (nt > n) nt = n;
    V3Par P = { -0 + 0, n, fn, ctx }; P.next = 0;
    HANDLE th[16]; int k = 0;
    for (int i = 1; i < nt; i++) { th[k] = CreateThread(NULL, 1 << 20, V3ParWorker, &P, 0, NULL); if (th[k]) k++; }
    V3ParWorker(&P);                                         // this thread works too
    if (k) WaitForMultipleObjects(k, th, TRUE, INFINITE);
    for (int i = 0; i < k; i++) CloseHandle(th[i]);
}
typedef struct { wchar_t *path; VoxPart *part; volatile long *done; } V3LoadJob;
static void V3LoadJobFn(void *ctx, int i) {
    V3LoadJob *j = (V3LoadJob *)ctx + i;
    VoxPart *m = (VoxPart *)calloc(1, sizeof(VoxPart));
    if (m && (!VoxLoadBfres(m, j->path) || !m->nti)) { VoxFreePart(m); free(m); m = NULL; }
    j->part = m;
    if (j->done) InterlockedIncrement(j->done);
}
// the loading status (from the loading thread): a step with a fixed position, or a counted one
static void V3Stage(V3 *v, const wchar_t *t, long prog) { swprintf(v->ldStage, 160, L"%ls", t); v->ldN = 0; v->ldProg = prog; }
static void V3StageCount(V3 *v, const wchar_t *t, long base, long span, long n) {
    swprintf(v->ldStage, 160, L"%ls", t); v->ldDone = 0; v->ldBase = base; v->ldSpan = span; v->ldProg = base; v->ldN = n > 0 ? n : 0;
}
// the terrain part grows: keep its per-vertex texture coordinates and per-triangle texture slots the same length
static int V3GrowExtras(VoxPart *p, size_t nv, size_t nt) {
    float *u = (float *)realloc(p->tuv, (nv + 1) * 2 * sizeof(float)); if (!u) return 0;
    if (!p->tuv) memset(u, 0, (nv + 1) * 2 * sizeof(float));
    p->tuv = u;
    uint16_t *t = (uint16_t *)realloc(p->ttex, (nt + 1) * sizeof(uint16_t)); if (!t) return 0;
    if (!p->ttex) for (size_t k = 0; k < nt + 1; k++) t[k] = 0xFFFF;
    p->ttex = t;
    return 1;
}
// room for nv vertices / nt triangles in the terrain part's arrays, grown by half again each time (growing them to the
// exact size for every placed object copied the whole map's arrays over and over)
static int V3Reserve(V3 *v, VoxPart *p, size_t nv, size_t nt) {
    if (nv > v->capV || !p->tv || !p->tuv) {
        size_t c = v->capV + v->capV / 2; if (c < nv) c = nv; c += 4096;
        float *a = (float *)realloc(p->tv, c * 3 * sizeof(float)); if (!a) return 0; p->tv = a;
        float *u = (float *)realloc(p->tuv, (c + 1) * 2 * sizeof(float)); if (!u) return 0;
        if (!p->tuv) memset(u, 0, (c + 1) * 2 * sizeof(float));
        p->tuv = u; v->capV = c;
    }
    if (nt > v->capT || !p->ti || !p->tmat || !p->ttex) {
        size_t c = v->capT + v->capT / 2; if (c < nt) c = nt; c += 4096;
        uint32_t *a = (uint32_t *)realloc(p->ti, c * 3 * sizeof(uint32_t)); if (!a) return 0; p->ti = a;
        uint16_t *m = (uint16_t *)realloc(p->tmat, (c + 1) * sizeof(uint16_t)); if (!m) return 0; p->tmat = m;
        uint16_t *t = (uint16_t *)realloc(p->ttex, (c + 1) * sizeof(uint16_t)); if (!t) return 0;
        if (!p->ttex) for (size_t k = 0; k < c + 1; k++) t[k] = 0xFFFF;
        p->ttex = t; v->capT = c;
    }
    return 1;
}
// a placed model's albedo texture -> slot (0xFFFF if none)
static uint16_t V3TexSlot(V3 *v, const VoxPart *m, unsigned part) {
    if (part >= 256 || !m->texAlb[part][0] || !m->tuv) return 0xFFFF;
    // a state panel (array albedo + emission, the ? panels: its glowing "?" is in the emission): "albedo|emission"
    char nm[128]; snprintf(nm, sizeof nm, "%s", m->texAlb[part]);
    // (only the ? panels: the P-switch's array faces looked wrong with their glow - plain albedo, first layer, for them)
    if (strstr(m->texAlb[part], "HatenaPanel_Array_") && m->texEmm[part][0] && strlen(m->texAlb[part]) + strlen(m->texEmm[part]) + 2 <= sizeof nm)
        snprintf(nm, sizeof nm, "%s|%s", m->texAlb[part], m->texEmm[part]);
    for (int i = 0; i < v->nmtex; i++) if (!strcmp(v->mtexName[i], nm)) return (uint16_t)i;
    if (v->nmtex >= 0xFFF0) return 0xFFFF;
    if (v->nmtex == v->capmtex) {
        int c = v->capmtex ? v->capmtex * 2 : 256;
        char (*nn)[128] = (char (*)[128])realloc(v->mtexName, (size_t)c * 128); if (!nn) return 0xFFFF; v->mtexName = nn;
        int *nt = (int *)realloc(v->mtexTex, (size_t)c * sizeof(int)); if (!nt) return 0xFFFF; v->mtexTex = nt;
        v->capmtex = c;
    }
    snprintf(v->mtexName[v->nmtex], 128, "%s", nm); v->mtexTex[v->nmtex] = -1;
    return (uint16_t)v->nmtex++;
}
// cliffs and ice placed as models (rock faces, overhangs, ice shelves and icebergs aren't in the height map):
// CmnObj_Cliff<area>... (Proc = one generated model per 1000 x 1000 world square; Simple / Deformed / Unique / Bump /
// ExtraGuide = hand-placed pieces) and CmnObj_Iceberg... The list entry is by area; NULL = not one of these.
static const char *V3CliffKind(const char *n) {
    if (!strncmp(n, "Stage_", 6)) return "Stage";                   // the race courses' own models (Stage_SnowMountain...)
    if (strncmp(n, "CmnObj_", 7)) return NULL;
    if (!strncmp(n + 7, "Iceberg", 7)) return "Iceberg";
    if (strstr(n, "SpecialSurface")) return NULL;                 // lava, aurora, chocolate...: drawn as special surfaces (their own entries)
    if (strncmp(n + 7, "Cliff", 5)) return "Landmark";             // other always-present objects: mountains (RoundMountain), cranes, gates...
    return "Cliff";                                               // (CmnObj_Cliff<area code>...: VL, DT, IC...)
}
// an object's name for the list: without "CmnObj_" / "Stage_" in front or "_00" at the end
static void V3ObjShort(const char *nm, char *out, size_t n) {
    const char *s = !strncmp(nm, "CmnObj_", 7) ? nm + 7 : !strncmp(nm, "Stage_", 6) ? nm + 6 : nm;
    snprintf(out, n, "%s", s);
    size_t L = strlen(out); if (L > 3 && !strcmp(out + L - 3, "_00")) out[L - 3] = 0;
}
// WorldResident's objects, one entry each, grouped by kind: "Stage: DonkeyKong_A", "Landmark: RoundMountain000_A",
// "Cliff: CliffVL012_A", "Iceberg: Iceberg001_A"
static void V3ResidentEntry(const char *nm, char *out, size_t n) {
    const char *k = V3CliffKind(nm); if (!k) { out[0] = 0; return; }
    char g[24]; snprintf(g, sizeof g, "%s", k); char *c = strchr(g, ':'); if (c) *c = 0;   // (just the group)
    char s[VOX_MATN]; V3ObjShort(nm, s, sizeof s);
    snprintf(out, n, "%s: %s", g, s);
}
static int V3IsCliffMat(const char *n) { return !strncmp(n, "Iceberg:", 8) || !strncmp(n, "Cliff:", 6) || !strncmp(n, "Scenery", 7) || !strncmp(n, "Stage:", 6); }
/* ---- the per-name records (V3Name), an open-addressing table keyed by the object name */
static unsigned V3StrHash(const char *s) { unsigned h = 2166136261u; while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; } return h; }
static V3Name *V3NameGet(V3 *v, const char *name, int add) {
    char nm[96]; snprintf(nm, sizeof nm, "%s", name);
    if (!v->names) { if (!add) return NULL; v->capNames = 16384; v->names = (V3Name *)calloc((size_t)v->capNames, sizeof(V3Name)); v->nNames = 0; if (!v->names) return NULL; }
    if (add && v->nNames * 2 >= v->capNames) {                       // grow: twice the size, everything placed again
        int nc = v->capNames * 2; V3Name *nn = (V3Name *)calloc((size_t)nc, sizeof(V3Name));
        if (nn) {
            for (int i = 0; i < v->capNames; i++) if (v->names[i].nm[0]) { unsigned q = V3StrHash(v->names[i].nm) & (nc - 1); while (nn[q].nm[0]) q = (q + 1) & (nc - 1); nn[q] = v->names[i]; }
            free(v->names); v->names = nn; v->capNames = nc;
        }
    }
    unsigned i = V3StrHash(nm) & (v->capNames - 1);
    while (v->names[i].nm[0]) { if (!strcmp(v->names[i].nm, nm)) return &v->names[i]; i = (i + 1) & (v->capNames - 1); }
    if (!add || v->nNames * 4 >= v->capNames * 3) return NULL;
    memset(&v->names[i], 0, sizeof(V3Name)); snprintf(v->names[i].nm, sizeof v->names[i].nm, "%s", nm); v->names[i].rad = -1; v->names[i].model = -1; v->nNames++;
    return &v->names[i];
}
// what the load did with an object: drawn as list entry 'ent', or not drawn because 'why'
static void V3MarkDrawn(V3 *v, const char *nm, const char *ent) {
    V3Name *e = V3NameGet(v, nm, 1); if (!e || !ent || !ent[0]) return;
    e->why[0] = 0;
    if (!e->ent[0]) snprintf(e->ent, sizeof e->ent, "%s", ent);
    else if (strcmp(e->ent, ent) && !strstr(e->ent, " (+ more)") && strlen(e->ent) < sizeof e->ent - 10) strcat(e->ent, " (+ more)");
}
static void V3MarkWhy(V3 *v, const char *nm, const char *why) {
    V3Name *e = V3NameGet(v, nm, 1); if (!e || e->ent[0]) return;
    snprintf(e->why, sizeof e->why, "%s", why);
}
/* Every placed object of the world: Banc\World_Game03\*.bcett (WorldResident = always-present scenery, the rest by
   area) and Banc\World_Game03-Free\*.bcett (free roam's objects: coins, item boxes...). Read once per dump and kept.
   The same object at the same spot in both folders is kept once. Counts per name go to the name records. */
static void V3GenArc(V3 *v);
static const unsigned char *V3SarcGet(const unsigned char *s, size_t sn, const char *name, size_t *outN);
static void V3GymlMember(const char *ref, char *out, size_t n);
// the scene's object folders in Banc: Mario Kart World's World_Game03 + World_Game03-Free; another scene: World_<scene>
static int V3BancFolders(V3 *v, wchar_t f[2][200]) {
    if (v->part.terrGeneric) { char sc[160]; V3SceneName(v->dir, sc, sizeof sc); swprintf(f[0], 200, L"World_%hs", sc); f[1][0] = 0; return 1; }
    swprintf(f[0], 200, L"World_Game03"); swprintf(f[1], 200, L"World_Game03-Free"); return 2;
}
static void V3GatherActors(V3 *v, const wchar_t *root) {
    wchar_t fold[2][200]; int nfold = V3BancFolders(v, fold);
    wchar_t key[1100]; swprintf(key, 1100, L"%ls|%ls", root, fold[0]);   // (cached per dump and scene)
    if (v->allAct && !_wcsicmp(v->allRoot, key)) return;
    free(v->allAct); v->allAct = NULL; v->nAllAct = 0; free(v->names); v->names = NULL; v->capNames = v->nNames = 0;
    free(v->wta); v->wta = NULL; v->nwta = 0; int capW = 0;
    free(v->isl); v->isl = NULL; v->nisl = 0; int capI = 0;
    struct { float T[3]; char info[160]; } sens[32]; int nsens = 0;      // the island tunnels' sensors (WorldResident)
    swprintf(v->allRoot, 1024, L"%ls", key);
    int cap = 8192; V3Act *act = (V3Act *)malloc((size_t)cap * sizeof(V3Act)); if (!act) return;
    const int HS = 1 << 21; uint64_t *seen = (uint64_t *)calloc(HS, sizeof(uint64_t)); int nseen = 0;   // (name, spot) already taken
    for (int fo = 0; fo < nfold; fo++) {
        wchar_t pat[2300]; swprintf(pat, 2300, L"%ls\\Banc\\%ls\\*.bcett.byml.zs", root, fold[fo]);
        WIN32_FIND_DATAW fd; HANDLE fh = FindFirstFileW(pat, &fd);
        if (fh == INVALID_HANDLE_VALUE) continue;
        do {
            int resident = fo == 0 && !wcsncmp(fd.cFileName, L"WorldResident.", 14);
            wchar_t path[2400]; swprintf(path, 2400, L"%ls\\Banc\\%ls\\%ls", root, fold[fo], fd.cFileName);
            size_t zn; unsigned char *z = VoxReadFile(path, &zn); if (!z) continue;
            size_t n; unsigned char *b = VoxUnzstd(z, zn, &n); free(z); if (!b) continue;
            Byml y; BymlNode r = BymlOpen(&y, b, n), acts = BymlGet(&y, r, "Actors");
            for (int i = 0; i < BymlCount(&y, acts); i++) {
                BymlNode a = BymlAt(&y, acts, i);
                const char *nm = BymlStr(&y, BymlGet(&y, a, "Name"));
                char gclean[96];                                   // (no Name: the Gyaml path's file name)
                if (!nm) { const char *g = BymlStr(&y, BymlGet(&y, a, "Gyaml")); if (!g) continue; const char *sl = strrchr(g, '/');
                           snprintf(gclean, sizeof gclean, "%s", sl ? sl + 1 : g); char *e = strstr(gclean, ".gear__"); if (e) *e = 0; nm = gclean; }
                if (!nm[0]) continue;
                V3Act A; memset(&A, 0, sizeof A); snprintf(A.nm, sizeof A.nm, "%s", nm); A.resident = (unsigned char)resident;
                BymlNode t = BymlGet(&y, a, "Translate"), ro = BymlGet(&y, a, "Rotate"), sc = BymlGet(&y, a, "Scale");
                for (int k = 0; k < 3; k++) { A.T[k] = BymlFloat(BymlAt(&y, t, k), 0); A.R[k] = BymlFloat(BymlAt(&y, ro, k), 0); A.S[k] = 1; }
                if (sc.type == 0xC0) for (int k = 0; k < 3; k++) A.S[k] = BymlFloat(BymlAt(&y, sc, k), 1);
                else if (sc.type) A.S[0] = A.S[1] = A.S[2] = BymlFloat(sc, 1);
                A.gridify = (unsigned char)BymlBool(BymlGet(&y, a, "IsGridify"), 0);
                const char *prim = BymlStr(&y, BymlGet(&y, a, "PrimitiveType")); A.sphere = (unsigned char)(!prim ? 0 : !strcmp(prim, "Sphere") ? 1 : !strncmp(prim, "Cylinder", 8) ? 2 : !strcmp(prim, "Cube") ? 3 : 0);
                if (seen && nseen < HS / 2) {                      // the same thing at the same spot (to 10 cm): once
                    char key[160]; snprintf(key, sizeof key, "%s|%d|%d|%d", A.nm, (int)lroundf(A.T[0] * 10), (int)lroundf(A.T[1] * 10), (int)lroundf(A.T[2] * 10));
                    uint64_t h = 1469598103934665603ULL; for (const char *q = key; *q; q++) { h ^= (unsigned char)*q; h *= 1099511628211ULL; } if (!h) h = 1;
                    unsigned s = (unsigned)(h & (HS - 1)); int dup = 0;
                    while (seen[s]) { if (seen[s] == h) { dup = 1; break; } s = (s + 1) & (HS - 1); }
                    if (dup) continue;
                    seen[s] = h; nseen++;
                }
                if (!strcmp(nm, "SystemObj_IslandTunnelSensor_00") && nsens < 32) {   // an island's way in: Dynamic.IslandInfo
                    const char *ii = BymlStr(&y, BymlGet(&y, BymlGet(&y, a, "Dynamic"), "IslandInfo"));
                    if (ii && *ii) { memcpy(sens[nsens].T, A.T, sizeof A.T); snprintf(sens[nsens].info, sizeof sens[nsens].info, "%s", ii); nsens++; }
                }
                {   const char *wt = BymlStr(&y, BymlGet(&y, BymlGet(&y, a, "Dynamic"), "WTAType"));   // a P-switch's mission
                    if (wt && *wt) {
                        if (v->nwta == capW) { int nc = capW ? capW * 2 : 512; struct V3Wta *nw = (struct V3Wta *)realloc(v->wta, (size_t)nc * sizeof *nw); if (nw) { v->wta = nw; capW = nc; } }
                        if (v->nwta < capW) { struct V3Wta *w = &v->wta[v->nwta++]; memcpy(w->T, A.T, sizeof w->T);
                            const char *sl = strrchr(wt, '/'); snprintf(w->ms, sizeof w->ms, "%s", sl ? sl + 1 : wt); char *d = strchr(w->ms, '.'); if (d) *d = 0; }
                    }
                }
                if (v->nAllAct == cap) { V3Act *na = (V3Act *)realloc(act, (size_t)cap * 2 * sizeof(V3Act)); if (!na) break; act = na; cap *= 2; }
                act[v->nAllAct++] = A;
                V3Name *e = V3NameGet(v, A.nm, 1); if (e) e->count++;
            }
            free(b);
        } while (FindNextFileW(fh, &fd));
        FindClose(fh);
    }
    free(seen);
    v->allAct = act;
    /* the islands: small areas of their own (Banc\Island_<name>.bcett.byml.zs and its _Fix), entered by a tunnel.
       The sensor's IslandInfo (general_format: Gyml/IslandInfo/<x>.game__IslandInfo.bgyml) names the island's Banc
       ("Work/Banc/Scene/Island_ChocoMountain_00.bcett.json"). Their actors have the island's own coordinates, so
       they're kept apart (by the game's Hash, which the collectible lists use as BancId) and shown at the way in. */
    // each sensor's island: its IslandInfo's Banc ("Work/Banc/Scene/Island_ChocoMountain_00.bcett.json")
    char sisl[32][64]; for (int k = 0; k < nsens; k++) sisl[k][0] = 0;
    if (nsens) V3GenArc(v);
    for (int k = 0; k < nsens; k++) {
        char mem[200]; V3GymlMember(sens[k].info, mem, sizeof mem);
        size_t gn; const unsigned char *g = V3SarcGet(v->genArc, v->genArcN, mem, &gn);
        const char *bn = NULL; Byml gy;
        if (g) { BymlNode gr = BymlOpen(&gy, g, gn); bn = BymlStr(&gy, BymlGet(&gy, gr, "Banc")); }
        if (!bn) bn = sens[k].info;                                   // (else the IslandInfo's own name: "Chocomountain_00")
        const char *sl = strrchr(bn, '/'); snprintf(sisl[k], sizeof sisl[k], "%s", sl ? sl + 1 : bn); char *d = strchr(sisl[k], '.'); if (d) *d = 0;
    }
    // every island file (Banc\Island_<name>.bcett.byml.zs and its _Fix), matched to its sensor by name (case and the
    // "Island_" prefix ignored); an island whose way in isn't found keeps no position
    wchar_t ipat[2300]; swprintf(ipat, 2300, L"%ls\\Banc\\Island_*.bcett.byml.zs", root);
    WIN32_FIND_DATAW ifd; HANDLE ifh = FindFirstFileW(ipat, &ifd);
    if (ifh != INVALID_HANDLE_VALUE) do {
        char isl[64]; snprintf(isl, sizeof isl, "%ls", ifd.cFileName); { char *d = strstr(isl, ".bcett"); if (d) *d = 0; }
        size_t il = strlen(isl); if (il > 4 && !_stricmp(isl + il - 4, "_Fix")) isl[il - 4] = 0;
        const char *ib = !_strnicmp(isl, "Island_", 7) ? isl + 7 : isl;
        int ks = -1;
        for (int k = 0; k < nsens && ks < 0; k++) { const char *sb = !_strnicmp(sisl[k], "Island_", 7) ? sisl[k] + 7 : sisl[k]; if (!_stricmp(sb, ib)) ks = k; }
        wchar_t path[2400]; swprintf(path, 2400, L"%ls\\Banc\\%ls", root, ifd.cFileName);
        size_t zn; unsigned char *z = VoxReadFile(path, &zn); if (!z) continue;
        size_t n; unsigned char *b = VoxUnzstd(z, zn, &n); free(z); if (!b) continue;
        Byml y; BymlNode r = BymlOpen(&y, b, n), acts = BymlGet(&y, r, "Actors");
        for (int i = 0; i < BymlCount(&y, acts); i++) {
            BymlNode a = BymlAt(&y, acts, i);
            uint64_t hh = BymlU64(&y, BymlGet(&y, a, "Hash"), 0); if (!hh) continue;
            const char *nm = BymlStr(&y, BymlGet(&y, a, "Name")); char gclean[96];
            if (!nm) { const char *g = BymlStr(&y, BymlGet(&y, a, "Gyaml")); if (!g) continue; const char *sl = strrchr(g, '/');   // (no Name: the Gyaml's file name)
                       snprintf(gclean, sizeof gclean, "%s", sl ? sl + 1 : g); char *e2 = strchr(gclean, '.'); if (e2) *e2 = 0; nm = gclean; }
            if (v->nisl == capI) { int nc = capI ? capI * 2 : 256; struct V3Isl *ni = (struct V3Isl *)realloc(v->isl, (size_t)nc * sizeof *ni); if (!ni) break; v->isl = ni; capI = nc; }
            struct V3Isl *e = &v->isl[v->nisl++]; e->hash = hh; e->noPos = ks < 0;
            {   BymlNode tr = BymlGet(&y, a, "Translate"); for (int q = 0; q < 3; q++) e->LT[q] = BymlFloat(BymlAt(&y, tr, q), 0); }   // (its own coordinates)
            if (ks >= 0) memcpy(e->T, sens[ks].T, sizeof e->T); else e->T[0] = e->T[1] = e->T[2] = 0;
            snprintf(e->nm, sizeof e->nm, "%s", nm); snprintf(e->island, sizeof e->island, "%ls", ifd.cFileName); { char *d = strstr(e->island, ".bcett"); if (d) *d = 0; }
            e->ms[0] = 0;
            const char *wt = BymlStr(&y, BymlGet(&y, BymlGet(&y, a, "Dynamic"), "WTAType"));
            if (wt && *wt) { const char *sl = strrchr(wt, '/'); snprintf(e->ms, sizeof e->ms, "%s", sl ? sl + 1 : wt); char *d = strchr(e->ms, '.'); if (d) *d = 0; }
        }
        free(b);
    } while (FindNextFileW(ifh, &ifd));
    if (ifh != INVALID_HANDLE_VALUE) FindClose(ifh);
}
// the invisible areas: list entry for an area object (NULL: not an area); *shown: ticked at first
static int V3AreaKnown(const char *nm) {                 // the area kinds that come first (and get their own colours)
    static const char *const k[] = { "SystemObj_WorldEndArea_00", "SystemObj_RescueArea_00", "SystemObj_ValleyArea_00", "SystemObj_RideKeepOutArea_00",
                                     "Area_CourseAreaCube", "Area_CourseAreaSphere", "SystemObj_GlideArea_00", "SystemObj_WaterShallowArea_00" };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) if (!strcmp(nm, k[i])) return 1;
    return 0;
}
// an invisible area's list entry, by its game name without the prefix and "_00" ("Area: WorldEndArea"); NULL: not an
// area. *shown: ticked at first (only the free-roam out of bounds)
static const char *V3AreaLabel(const char *nm, int *shown, char buf[40]) {
    if (!((!strncmp(nm, "SystemObj_", 10) || !strncmp(nm, "Area_", 5)) && strstr(nm, "Area"))) return NULL;
    const char *s = strchr(nm, '_') + 1; char t[40]; snprintf(t, sizeof t, "%s", s);
    size_t L = strlen(t); if (L > 3 && !strcmp(t + L - 3, "_00")) t[L - 3] = 0;
    snprintf(buf, 40, "Area: %s", t);
    if (shown) *shown = !strcmp(nm, "SystemObj_WorldEndArea_00");
    return buf;
}
// list entry for an object drawn as one piece: "Props: Coin" (CmnObj_), "Other: EffectObj Steam" (any other kind);
// once 'full' the rest share "Props: other" / "Other: other"
// an object drawn as itself: "Props: Coin000_A" (CmnObj_), "Other: EffectObj_WarioStadiumEngineSteam" (any other
// kind); once 'full' the rest share "Props: other" / "Other: other"
static void V3GenericKind(const char *nm, int full, char *out, size_t n) {
    int cmn = !strncmp(nm, "CmnObj_", 7);
    if (full) { snprintf(out, n, cmn ? "Object: other" : "Other: other"); return; }
    char s[VOX_MATN]; V3ObjShort(nm, s, sizeof s);
    snprintf(out, n, !strncmp(nm, "CmnObj_SFC", 10) ? "Surface: %s" : cmn ? "Object: %s" : "Other: %s", s);   // (SFC...: surfaces)
}
/* ---- which model the game gives a placed object: its settings (System\GymlArchive\actor_general_format...sarc.zs,
   Gyml/ActorGParam/<name>.gear__ActorGParam.bgyml; follow "$parent" until one has Components.ModelInfoRef) ->
   that model info (modelinfo_general_format...sarc.zs, Gyml/Component/ModelInfoGParam/...; follow "$parent" until one
   has ModelResource) -> its first Fmdb "Work/Model/CmnObj/CmnObj_MissionGate000/output/CmnObj_MissionGate000_S.fmdb"
   = Model\CmnObj_MissionGate000.CmnObj_MissionGate000_S.bfres.zs. (CmnObj_MissionGate001_S_00 has no model of its
   own: its parent is MissionGate000_S.) The two archives are read once and kept. */
// a SARC member by name: binary search of the archive's name-hash table (no copy; NULL if not there)
static const unsigned char *V3SarcGet(const unsigned char *s, size_t sn, const char *name, size_t *outN) {
    if (!s || sn < 0x20 || memcmp(s, "SARC", 4)) return NULL;
    uint32_t dataOff = ArcRd32(s + 0x0C); size_t o = s[4] | (s[5] << 8);
    if (o + 12 > sn || memcmp(s + o, "SFAT", 4)) return NULL;
    int nf = s[o + 6] | (s[o + 7] << 8); uint32_t mult = ArcRd32(s + o + 8);
    size_t ents = o + 12, names = ents + (size_t)nf * 16 + 8;
    uint32_t h = 0; for (const unsigned char *c = (const unsigned char *)name; *c; c++) h = h * mult + (uint32_t)(signed char)*c;
    int lo = 0, hi = nf - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2; const unsigned char *e = s + ents + (size_t)mid * 16; uint32_t eh = ArcRd32(e);
        if (eh < h) lo = mid + 1; else if (eh > h) hi = mid - 1;
        else {
            for (int f = mid; f >= 0 && ArcRd32(s + ents + (size_t)f * 16) == h; f--)        // (equal hashes: check the names)
                {
                    const unsigned char *e2 = s + ents + (size_t)f * 16; uint32_t attr = ArcRd32(e2 + 4), st = ArcRd32(e2 + 8), en = ArcRd32(e2 + 12);
                    size_t no = names + (size_t)(attr & 0xFFFFFF) * 4;
                    if ((attr >> 24) && no < sn && !strcmp((const char *)s + no, name) && (size_t)dataOff + en <= sn && en >= st) { *outN = en - st; return s + dataOff + st; }
                }
            for (int f = mid + 1; f < nf && ArcRd32(s + ents + (size_t)f * 16) == h; f++) {
                const unsigned char *e2 = s + ents + (size_t)f * 16; uint32_t attr = ArcRd32(e2 + 4), st = ArcRd32(e2 + 8), en = ArcRd32(e2 + 12);
                size_t no = names + (size_t)(attr & 0xFFFFFF) * 4;
                if ((attr >> 24) && no < sn && !strcmp((const char *)s + no, name) && (size_t)dataOff + en <= sn && en >= st) { *outN = en - st; return s + dataOff + st; }
            }
            return NULL;
        }
    }
    return NULL;
}
// the newest copy of System\GymlArchive\<kind>_general_format.Product_*.sarc.zs (updates first), unpacked
static unsigned char *V3ReadGymlArc(V3 *v, const char *kind, size_t *n) {
    wchar_t roots[12][1024]; int nr = V3RomfsRoots(v->dir, roots);
    *n = 0;
    for (int r = nr - 1; r >= 0; r--) {                      // (later roots are updates: prefer them)
        wchar_t pat[1200]; swprintf(pat, 1200, L"%ls\\System\\GymlArchive\\%hs%hsgeneral_format.Product_*.sarc.zs", roots[r], kind, *kind ? "_" : "");   // ("": general_format...)
        WIN32_FIND_DATAW fd; HANDLE fh = FindFirstFileW(pat, &fd); if (fh == INVALID_HANDLE_VALUE) continue;
        wchar_t best[300] = L""; do { if (wcscmp(fd.cFileName, best) > 0) swprintf(best, 300, L"%ls", fd.cFileName); } while (FindNextFileW(fh, &fd));
        FindClose(fh);
        wchar_t p[1600]; swprintf(p, 1600, L"%ls\\System\\GymlArchive\\%ls", roots[r], best);
        size_t zn; unsigned char *z = ArcReadAll(p, &zn); if (!z) continue;
        unsigned char *s = ArcUnzstd(z, zn, n);
        if (s && *n >= 4 && !memcmp(s, "SARC", 4)) return s;
        free(s); *n = 0;
    }
    return NULL;
}
// "Work/Gyml/X/Y.gear__Z.gyml" -> the archive member "Gyml/X/Y.gear__Z.bgyml"
static void V3GymlMember(const char *ref, char *out, size_t n) {
    snprintf(out, n, "%s", !strncmp(ref, "Work/", 5) ? ref + 5 : ref);
    size_t L = strlen(out); if (L > 5 && !strcmp(out + L - 5, ".gyml") && L + 2 < n) { memmove(out + L - 4, out + L - 5, 6); out[L - 4] = 'b'; }
}
// the model the game gives object 'actor': proj + fmdb ("CmnObj_MissionGate000", "CmnObj_MissionGate000_S"); 0 if unknown
static int V3ActorModelFile(V3 *v, const char *actor, char *proj, size_t pn, char *fmdb, size_t fn) {
    if (!v->gyArcTried) { v->gyArcTried = 1; v->actArc = V3ReadGymlArc(v, "actor", &v->actArcN); v->miArc = V3ReadGymlArc(v, "modelinfo", &v->miArcN); }
    if (!v->actArc || !v->miArc) return 0;
    char mem[512]; snprintf(mem, sizeof mem, "Gyml/ActorGParam/%s.gear__ActorGParam.bgyml", actor);
    char mi[512] = "";
    for (int depth = 0; depth < 10 && !mi[0]; depth++) {      // the actor, then its parents, until one names its model info
        size_t n; const unsigned char *b = V3SarcGet(v->actArc, v->actArcN, mem, &n); if (!b) return 0;
        Byml y; BymlNode r = BymlOpen(&y, b, n);
        const char *ref = BymlStr(&y, BymlGet(&y, BymlGet(&y, r, "Components"), "ModelInfoRef"));
        if (ref && *ref) { V3GymlMember(ref, mi, sizeof mi); break; }
        const char *par = BymlStr(&y, BymlGet(&y, r, "$parent")); if (!par || !*par) return 0;
        V3GymlMember(par, mem, sizeof mem);
    }
    if (!mi[0]) return 0;
    for (int depth = 0; depth < 10; depth++) {                // the model info, then its parents, until one has a model
        size_t n; const unsigned char *b = V3SarcGet(v->miArc, v->miArcN, mi, &n); if (!b) return 0;
        Byml y; BymlNode r = BymlOpen(&y, b, n);
        BymlNode res = BymlGet(&y, r, "ModelResource");
        if (res.type && BymlCount(&y, res) > 0) {
            const char *f = BymlStr(&y, BymlGet(&y, BymlAt(&y, res, 0), "Fmdb")); if (!f) return 0;
            const char *out = strstr(f, "/output/"); const char *base = strrchr(f, '/');
            if (!out || !base) return 0;
            const char *ps = out; while (ps > f && ps[-1] != '/') ps--;               // the folder before /output/
            snprintf(proj, pn, "%.*s", (int)(out - ps), ps);
            snprintf(fmdb, fn, "%s", base + 1); char *dot = strrchr(fmdb, '.'); if (dot) *dot = 0;
            return proj[0] && fmdb[0];
        }
        const char *par = BymlStr(&y, BymlGet(&y, r, "$parent")); if (!par || !*par) return 0;
        V3GymlMember(par, mi, sizeof mi);
    }
    return 0;
}
// model file of an object name: Model\<base>.<base>_<variant>.bfres.zs (name <base>_<variant>[_00]), else
// Model\<name>.<name>.bfres.zs; far: the lighter _Far version when there is one. 0 if there's none.
static int V3ModelPath(V3 *v, const wchar_t *root, const char *nm, int useFar, wchar_t *mp, size_t mpn) {
    char pj[160], fm[160];                                     // first: the model the game's own settings give it
    if (v && V3ActorModelFile(v, nm, pj, sizeof pj, fm, sizeof fm)) {
        if (useFar) { swprintf(mp, mpn, L"%ls\\Model\\%hs.%hs_Far.bfres.zs", root, pj, fm); if (GetFileAttributesW(mp) != INVALID_FILE_ATTRIBUTES) return 1; }
        swprintf(mp, mpn, L"%ls\\Model\\%hs.%hs.bfres.zs", root, pj, fm); if (GetFileAttributesW(mp) != INVALID_FILE_ATTRIBUTES) return 1;
    }
    char base[200]; snprintf(base, sizeof base, "%s", nm);
    size_t L = strlen(base); if (L > 5 && !strcmp(base + L - 3, "_00")) base[L -= 3] = 0;
    char *us = strrchr(base, '_');
    if (us) {
        char variant[40]; snprintf(variant, sizeof variant, "%s", us + 1); *us = 0;
        if (useFar) { swprintf(mp, mpn, L"%ls\\Model\\%hs.%hs_%hs_Far.bfres.zs", root, base, base, variant); if (GetFileAttributesW(mp) != INVALID_FILE_ATTRIBUTES) return 1; }
        swprintf(mp, mpn, L"%ls\\Model\\%hs.%hs_%hs.bfres.zs", root, base, base, variant); if (GetFileAttributesW(mp) != INVALID_FILE_ATTRIBUTES) return 1;
    }
    swprintf(mp, mpn, L"%ls\\Model\\%hs.%hs.bfres.zs", root, nm, nm); if (GetFileAttributesW(mp) != INVALID_FILE_ATTRIBUTES) return 1;
    mp[0] = 0; return 0;
}
static void V3AddMarker(V3 *v, VoxPart *p, const float *T, float half, const char *ent);
static int V3AddShape(V3 *v, const V3Act *A, const char *label, int shown);
static const char *V3PieceKind(const char *shape);
static void V3PieceEntry(const char *shape, char *out, size_t n);
static void V3CollectPoints(V3 *v);
static void V3AddSurfaceObjects(V3 *v) {
    VoxPart *p = &v->part;
    if (!(p->terrWorld[2] > 0) || p->gridW < 2) return;

    wchar_t root[2100]; swprintf(root, 2100, L"%ls", v->dir);
    if (!V3SceneRoot(root)) return;                                                    // ...\romfs
    float ws = p->terrWorld[2], half = p->terrWorld[3] * ws / 2;               // world -> terrain part coordinates: + half
    int whole = !(p->terrArea[2] > 0);
    float ax0 = -1e30f, az0 = -1e30f, ax1 = 1e30f, az1 = 1e30f;                 // an area view: only what's inside it
    if (!whole) { ax0 = (p->terrArea[0] - p->terrWorld[0]) * ws; az0 = (p->terrArea[1] - p->terrWorld[1]) * ws; ax1 = ax0 + p->terrArea[2] * ws; az1 = az0 + p->terrArea[2] * ws; }
    V3Stage(v, L"Reading the placed objects...", 150);
    V3GatherActors(v, root);
    if (!v->allAct || !v->names) return;
    V3CollectPoints(v);                                  // (the collectibles: drawn by V3AddCollectibles)
    for (int i = 0; i < v->capNames; i++) { v->names[i].ent[0] = v->names[i].why[0] = 0; v->names[i].ci = -1; }   // this load's record
    // the list holds VOX_MAXMAT entries: keep room for the areas (added after this) and the ground pieces' kinds; past
    // 'budget' objects share "Props: other" / "Other: other" / "<kind>: other parts"
    int nAreaKinds = 0;
    {   char seenA[64][40]; int ns = 0;
        for (int i = 0; i < v->capNames; i++) {
            if (!v->names[i].nm[0]) continue;
            int sh; char ab[40]; const char *l = V3AreaLabel(v->names[i].nm, &sh, ab); if (!l) continue;
            int q = 0; while (q < ns && strcmp(seenA[q], l)) q++;
            if (q == ns && ns < 64) snprintf(seenA[ns++], 40, "%s", l);
        }
        nAreaKinds = ns;
    }
    int budget = VOX_MAXMAT - 2 - (nAreaKinds < 60 ? nAreaKinds : 60) - 24;
    const V3Act *act = v->allAct; int nact = v->nAllAct;
    /* What's drawn (everything placed in the two folders gets a list entry or a reason in the name record):
       - areas: by V3AddAreas;  - WorldResident's special surfaces (lava, water...), cliffs / ice / courses / landmarks;
       - everything else with a model: "Object: <name>" (CmnObj_) / "Other: <kind>" - in full-detail areas all of them,
         on the whole map the large ones (25 m across or more) and the rare ones (30 or fewer in the world);
       - no model: a small marker, "Marker: <name>" (full-detail areas, hidden at first). */
    V3LoadJob *cache = (V3LoadJob *)calloc(4096, sizeof *cache); int ncache = 0;
    float *crad = (float *)calloc(4096, sizeof(float));
    if (!cache || !crad) { free(cache); free(crad); return; }
    #define V3_RELEASE(mm) do { (void)(mm); } while (0)      /* models stay in the cache until the end */
    for (int phase = 0; phase < 2; phase++) {           // 0: list the models needed, then load them all at once; 1: place them
    if (phase == 1) {
        V3StageCount(v, L"Loading the objects' models", 200, 400, ncache);
        for (int c = 0; c < ncache; c++) cache[c].done = &v->ldDone;
        V3ParallelFor(ncache, V3LoadJobFn, cache);
        V3StageCount(v, L"Placing the objects", 600, 50, nact);
        for (int c = 0; c < ncache; c++) {                 // each model's size (half its largest extent)
            VoxPart *m = cache[c].part; crad[c] = -1; if (!m || !m->ntv) continue;
            float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
            for (size_t k = 0; k < m->ntv; k++) for (int q = 0; q < 3; q++) { float t = m->tv[k * 3 + q]; if (t < lo[q]) lo[q] = t; if (t > hi[q]) hi[q] = t; }
            crad[c] = fmaxf(hi[0] - lo[0], fmaxf(hi[1] - lo[1], hi[2] - lo[2])) / 2;
        }
    }
    for (int i = 0; i < nact; i++) {
        if (phase == 1 && !(i & 255)) v->ldDone = i;
        const char *nm = act[i].nm;
        V3Name *e = V3NameGet(v, nm, 1); if (!e) continue;
        int shownA; char abuf[40];
        if (V3AreaLabel(nm, &shownA, abuf)) continue;                       // drawn by V3AddAreas
        if (v->actColl && v->actColl[i]) {                                  // a collectible: drawn by V3AddCollectibles
            if (phase) { char ce[VOX_MATN]; snprintf(ce, sizeof ce, "Marker: %s", v->clist[v->actColl[i] - 1]); V3MarkDrawn(v, nm, ce); }
            continue;
        }
        const float *T = act[i].T, *Rr = act[i].R, *S = act[i].S;
        float ms = fmaxf(fabsf(S[0]), fmaxf(fabsf(S[1]), fabsf(S[2])));
        int special = act[i].resident && strstr(nm, "SpecialSurface") != NULL;
        const char *ck = NULL; char kind[VOX_MATN]; int generic = 0;
        if (special) ck = NULL;
        else if (act[i].resident && V3CliffKind(nm)) { V3ResidentEntry(nm, kind, sizeof kind); ck = kind; }
        else if (!act[i].resident && !strncmp(nm, "CmnObj_", 7) && (!strncmp(nm + 7, "Cliff", 5) || !strncmp(nm + 7, "Iceberg", 7))) {
            if (phase) V3MarkWhy(v, nm, "cliff / iceberg outside WorldResident: skipped (cliffs come from WorldResident and the ground pieces)");
            continue;
        } else { generic = 1; V3GenericKind(nm, p->nmat >= budget, kind, sizeof kind); ck = kind; }
        if (ck && strncmp(nm, "Stage_", 6) && act[i].gridify) {               // merged into the gridify pieces (loaded there)
            if (phase) {
                if (whole) V3MarkWhy(v, nm, "merged into the ground pieces (Gridify): shown in full-detail areas");
                else { char t[VOX_MATN]; V3PieceEntry(nm, t, sizeof t); if (t[0]) V3MarkDrawn(v, nm, t); }
            }
            continue;
        }
        float mg = !strncmp(nm, "Stage_", 6) ? 4000.0f : 1500.0f;                                     // courses reach far from their origin
        if (T[0] + half < ax0 - mg || T[0] + half > ax1 + mg || T[2] + half < az0 - mg || T[2] + half > az1 + mg) continue;
        float gm = 300 + (e->rad > 0 ? e->rad * ms : 0);                 // (big objects reach in from further off)
        if (generic && !whole && (T[0] + half < ax0 - gm || T[0] + half > ax1 + gm || T[2] + half < az0 - gm || T[2] + half > az1 + gm)) continue;
        if (generic && whole && e->count > 30 && e->rad >= 0 && e->rad * ms < 25) {
            if (phase) { char t[72]; snprintf(t, sizeof t, "%d placed, small: shown in full-detail areas", e->count); V3MarkWhy(v, nm, t); }
            continue;
        }
        // the model (found once per name this load)
        if (e->ci == -1) {
            wchar_t mp[2300];
            if (!V3ModelPath(v, root, nm, whole && ck != NULL, mp, 2300)) e->ci = -2;
            else {
                int c = 0; while (c < ncache && wcscmp(cache[c].path, mp)) c++;
                if (c == ncache) { if (ncache >= 4096) { e->ci = -3; } else { cache[ncache].path = _wcsdup(mp); cache[ncache].part = NULL; ncache++; } }
                if (e->ci == -1) e->ci = c;
            }
        }
        if (phase == 0) continue;
        if (e->ci == -2) {                                          // no model: a marker (full-detail areas)
            if (whole) { V3MarkWhy(v, nm, "no model file: a marker in full-detail areas"); continue; }
            char ent[VOX_MATN], sh[VOX_MATN]; V3ObjShort(nm, sh, sizeof sh);
            if (strstr(nm, "SpecialSurface") || !strncmp(nm, "CmnObj_SFC", 10))     // surfaces without a model: with the other surfaces
                snprintf(ent, sizeof ent, "Surface: %s (no model)", !strncmp(sh, "SpecialSurface", 14) ? sh + 14 : sh);
            else snprintf(ent, sizeof ent, "Marker: %s", sh);
            if (act[i].sphere) { if (V3AddShape(v, &act[i], ent, 0)) V3MarkDrawn(v, nm, ent); continue; }   // a box / ball / cylinder of its own (warp zones, triggers...)
            V3AddMarker(v, p, T, half, ent); V3MarkDrawn(v, nm, ent);
            continue;
        }
        if (e->ci == -3) { V3MarkWhy(v, nm, "too many different models in view (4096)"); continue; }
        VoxPart *m = cache[e->ci].part;
        if (!m) { V3MarkWhy(v, nm, "model can't be read"); continue; }
        if (e->rad < 0) e->rad = crad[e->ci];
        if (generic && whole && e->count > 30 && e->rad * ms < 25) {
            char t[72]; snprintf(t, sizeof t, "%d placed, small: shown in full-detail areas", e->count); V3MarkWhy(v, nm, t); continue;
        }
        char base[200]; snprintf(base, sizeof base, "%s", nm);       // (special surfaces: their kind comes from the base name)
        { size_t L = strlen(base); if (L > 5 && !strcmp(base + L - 3, "_00")) base[L -= 3] = 0; char *us = strrchr(base, '_'); if (us) *us = 0; }
        // rotation: X, then Y, then Z (radians)
        float cx = cosf(Rr[0]), sx = sinf(Rr[0]), cy = cosf(Rr[1]), sy = sinf(Rr[1]), cz = cosf(Rr[2]), sz = sinf(Rr[2]);
        size_t nv0 = p->ntv, nt0 = p->nti;
        if (!V3Reserve(v, p, nv0 + m->ntv, nt0 + m->nti)) { V3_RELEASE(m); break; }
        for (size_t k = 0; k < m->ntv; k++) {
            float x = m->tv[k * 3] * S[0], yy = m->tv[k * 3 + 1] * S[1], zz = m->tv[k * 3 + 2] * S[2];
            float y1 = yy * cx - zz * sx, z1 = yy * sx + zz * cx;                    // about X
            float x2 = x * cy + z1 * sy, z2 = -x * sy + z1 * cy;                     // about Y
            float x3 = x2 * cz - y1 * sz, y3 = x2 * sz + y1 * cz;                    // about Z
            float *o = p->tv + (nv0 + k) * 3; o[0] = x3 + T[0] + half; o[1] = y3 + T[1]; o[2] = z2 + T[2] + half;
        }
        int hadMats = p->nmat, matOf[256]; for (int k = 0; k < 256; k++) matOf[k] = -1;   // [part * 2 + steep]
        int32_t *remap = (int32_t *)malloc(m->ntv * sizeof(int32_t));     // only the vertices of kept triangles (keeps the view's bounds right)
        if (!remap) { V3_RELEASE(m); break; }
        for (size_t k = 0; k < m->ntv; k++) remap[k] = -1;
        size_t added = 0, used = 0;
        float *tmp = (float *)malloc(m->ntv * 3 * sizeof(float));
        if (!tmp) { free(remap); V3_RELEASE(m); break; }
        memcpy(tmp, p->tv + nv0 * 3, m->ntv * 3 * sizeof(float));
        for (size_t k = 0; k < m->nti; k++) {
            if ((!ck || !strncmp(nm, "Stage_", 6) || !strncmp(ck, "Landmark:", 9)) && m->tmat && m->startHidden[m->tmat[k]]) continue;                // far / spare versions (cliffs: keep every part -
                                                                                       // their walls are flagged hidden too)
            const uint32_t *q = m->ti + k * 3;
            float mx = 0, mz = 0; for (int c = 0; c < 3; c++) { mx += tmp[q[c] * 3] / 3; mz += tmp[q[c] * 3 + 2] / 3; }
            if (mx < ax0 || mx > ax1 || mz < az0 || mz > az1) continue;
            uint32_t *o = p->ti + (nt0 + added) * 3;
            for (int c = 0; c < 3; c++) {
                if (remap[q[c]] < 0) {
                    remap[q[c]] = (int32_t)used; memcpy(p->tv + (nv0 + used) * 3, tmp + q[c] * 3, 12);
                    if (m->tuv) memcpy(p->tuv + (nv0 + used) * 2, m->tuv + (size_t)q[c] * 2, 8);
                    used++;
                }
                o[c] = (uint32_t)(nv0 + remap[q[c]]);
            }
            // one list entry per part of the model ("Lava: <part>"): edge strips / glow cards can be told apart and
            // hidden; parts with an opacity texture are see-through in the game, so they start hidden
            // steep triangles (the upright strips round a pool) get their own entry "... (walls)", hidden at first:
            // in the game they line castle walls / rock that isn't drawn here, so on their own they stand in mid-air
            unsigned mp2 = m->tmat ? m->tmat[k] : 0;
            int steep = 0;
            {   const float *A = tmp + q[0] * 3, *B = tmp + q[1] * 3, *C = tmp + q[2] * 3;
                float e1[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] }, e2[3] = { C[0] - A[0], C[1] - A[1], C[2] - A[2] };
                float nx = e1[1] * e2[2] - e1[2] * e2[1], ny = e1[2] * e2[0] - e1[0] * e2[2], nz = e1[0] * e2[1] - e1[1] * e2[0];
                float l = sqrtf(nx * nx + ny * ny + nz * nz); steep = l > 0 && fabsf(ny) / l < 0.5f; }
            if (ck) steep = 0;
            int slot = ck ? 0 : (int)mp2 * 2 + steep; if (slot > 255) slot = 255;
            if (matOf[slot] < 0 && ck) matOf[slot] = PhMat(p, ck);
            if (matOf[slot] < 0) {
                char nm3[VOX_MATN + 16]; const char *sk = V3SurfaceKind(base);
                (void)sk;
                if (p->nmat >= budget) snprintf(nm3, sizeof nm3, "Surface: other parts");
                else {                                       // one entry per object and part: "Surface: Lava000_A - <part> (walls)"
                    char so[VOX_MATN]; V3ObjShort(nm, so, sizeof so);
                    const char *s2 = !strncmp(so, "SpecialSurface", 14) ? so + 14 : so;
                    snprintf(nm3, sizeof nm3, "Surface: %s - %s", s2, mp2 < (unsigned)m->nmat ? m->matName[mp2] : "?");
                    nm3[VOX_MATN - 9] = 0; if (steep) strcat(nm3, " (walls)");
                }
                matOf[slot] = PhMat(p, nm3);
                if (matOf[slot] >= 0 && matOf[slot] < VOX_MAXMAT && (steep || m->texOpc[mp2][0])) p->startHidden[matOf[slot]] = 1;
            }
            p->ttex[nt0 + added] = V3TexSlot(v, m, mp2);     // their own textures (lava: CmnTex_Special_Lava_Alb...)
            p->tmat[nt0 + added] = (uint16_t)matOf[slot]; p->matCount[matOf[slot]]++; added++;
        }
        free(tmp); free(remap);
        p->ntv = nv0 + used; p->nti = nt0 + added; p->solid = p->nti;
        if (!added && p->nmat > hadMats) p->nmat = hadMats;          // nothing of it here: no empty list entry
        if (added) { for (int k = 0; k < 256; k++) if (matOf[k] >= 0 && matOf[k] < p->nmat) { V3MarkDrawn(v, nm, p->matName[matOf[k]]); break; } }
        else V3MarkWhy(v, nm, "nothing of it drawn here (outside this area, or only spare / far parts)");
        V3_RELEASE(m);
    }
    }                                                  // (phase loop)
    for (int c = 0; c < ncache; c++) { free(cache[c].path); if (cache[c].part) { VoxFreePart(cache[c].part); free(cache[c].part); } }
    free(cache); free(crad);
}
// a small marker (a 4 m diamond) for an object without a model, hidden at first
static int V3AddMarkerSz(V3 *v, VoxPart *p, const float *T, float half, const char *ent, float r, int hidden);
static void V3AddMarker(V3 *v, VoxPart *p, const float *T, float half, const char *ent) { V3AddMarkerSz(v, p, T, half, ent, 2, 1); }
// a diamond 'r' each way from its centre, its lowest point at T (world); returns its list entry (-1: none)
static int V3AddMarkerSz(V3 *v, VoxPart *p, const float *T, float half, const char *ent, float r, int hidden) {
    const float pv[6][3] = { { r, 0, 0 }, { -r, 0, 0 }, { 0, r, 0 }, { 0, -r, 0 }, { 0, 0, r }, { 0, 0, -r } };
    static const int ft[8][3] = { { 0, 2, 4 }, { 4, 2, 1 }, { 1, 2, 5 }, { 5, 2, 0 }, { 4, 3, 0 }, { 1, 3, 4 }, { 5, 3, 1 }, { 0, 3, 5 } };
    size_t nv0 = p->ntv, nt0 = p->nti;
    if (!V3Reserve(v, p, nv0 + 6, nt0 + 8)) return -1;
    int had = p->nmat, mi = PhMat(p, ent); if (mi < 0 || mi >= VOX_MAXMAT) return -1;
    if (mi >= had && hidden) p->startHidden[mi] = 1;
    for (int k = 0; k < 6; k++) { float *o = p->tv + (nv0 + k) * 3; o[0] = T[0] + half + pv[k][0]; o[1] = T[1] + r + pv[k][1]; o[2] = T[2] + half + pv[k][2]; p->tuv[(nv0 + k) * 2] = p->tuv[(nv0 + k) * 2 + 1] = 0; }
    for (int k = 0; k < 8; k++) { uint32_t *o = p->ti + (nt0 + k) * 3; for (int c = 0; c < 3; c++) o[c] = (uint32_t)(nv0 + ft[k][c]); p->tmat[nt0 + k] = (uint16_t)mi; p->ttex[nt0 + k] = 0xFFFF; p->matCount[mi]++; }
    p->ntv = nv0 + 6; p->nti = nt0 + 8; p->solid = p->nti;
    return mi;
}
// MurmurHash3 (32-bit, seed 0): the game's sticker ids are this of the sticker's name (537258079 = "Sticker_1120000")
static uint32_t V3Murmur3(const char *str) {
    const unsigned char *b = (const unsigned char *)str; size_t n = strlen(str); uint32_t h = 0, k;
    for (size_t i = 0; i + 4 <= n; i += 4) {
        k = b[i] | (b[i + 1] << 8) | (b[i + 2] << 16) | ((uint32_t)b[i + 3] << 24);
        k *= 0xcc9e2d51u; k = (k << 15) | (k >> 17); k *= 0x1b873593u; h ^= k; h = (h << 13) | (h >> 19); h = h * 5 + 0xe6546b64u;
    }
    k = 0; size_t t = n & 3;
    if (t) { for (size_t i = 0; i < t; i++) k |= (uint32_t)b[n - t + i] << (8 * i); k *= 0xcc9e2d51u; k = (k << 15) | (k >> 17); k *= 0x1b873593u; h ^= k; }
    h ^= (uint32_t)n; h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16; return h;
}
static void V3GenArc(V3 *v) { if (!v->genArcTried) { v->genArcTried = 1; v->genArc = V3ReadGymlArc(v, "", &v->genArcN); } }
// a sticker's name from its id: the general_format archive's Gyml/StickerInfoGParam/<name>.game__StickerInfoGParam.bgyml
static const char *V3StickerName(V3 *v, uint32_t id) {
    if (!v->stkTried) {
        v->stkTried = 1; V3GenArc(v);
        const unsigned char *s = v->genArc; size_t sn = v->genArcN;
        if (s && sn >= 0x20 && !memcmp(s, "SARC", 4)) {
            size_t o = s[4] | (s[5] << 8);
            if (o + 12 <= sn && !memcmp(s + o, "SFAT", 4)) {
                int nf = s[o + 6] | (s[o + 7] << 8); size_t ents = o + 12, names = ents + (size_t)nf * 16 + 8;
                static const char pre[] = "Gyml/StickerInfoGParam/";
                v->stk = (struct V3Stk *)malloc((size_t)(nf ? nf : 1) * sizeof *v->stk);
                for (int f = 0; v->stk && f < nf; f++) {
                    uint32_t attr = ArcRd32(s + ents + (size_t)f * 16 + 4); size_t no = names + (size_t)(attr & 0xFFFFFF) * 4;
                    if (!(attr >> 24) || no >= sn || strncmp((const char *)s + no, pre, sizeof pre - 1)) continue;
                    struct V3Stk *k = &v->stk[v->nstk]; snprintf(k->nm, sizeof k->nm, "%s", (const char *)s + no + sizeof pre - 1);
                    char *d = strchr(k->nm, '.'); if (d) *d = 0;
                    k->id = V3Murmur3(k->nm); v->nstk++;
                }
            }
        }
    }
    for (int i = 0; i < v->nstk; i++) if (v->stk[i].id == id) return v->stk[i].nm;
    return NULL;
}
// a message's text without its tags (icons and other private-use characters dropped)
static void V3MsgText(const MsgEnt *e, wchar_t *out, int n) {
    int at = 0; const unsigned char *u = e->b;
    for (int k = 0; u && k + 1 < e->nb && at < n - 1; ) {
        unsigned ch = u[k] | (u[k + 1] << 8);
        if (ch == 0x0E && k + 8 <= e->nb) { k += 8 + (u[k + 6] | (u[k + 7] << 8)); continue; }
        if (ch == 0x0F && k + 6 <= e->nb) { k += 6; continue; }
        if (ch == 0) break;
        if (ch == '\n') ch = ' ';
        if (!(ch >= 0xE000 && ch < 0xF900)) out[at++] = (wchar_t)ch;
        k += 2;
    }
    out[at] = 0;
}
// the mission texts: Mission.msbt in romfs\Mals\<USen / EUen / AUen>\MsgMain.msarc (any romfs of the dump)
static const wchar_t *V3MissionText(V3 *v, const char *msgId, wchar_t *buf, int n) {
    if (!v->missMsgTried) {
        v->missMsgTried = 1;
        wchar_t roots[12][1024]; int nr = V3RomfsRoots(v->dir, roots);
        static const wchar_t *const langs[3] = { L"USen", L"EUen", L"AUen" };
        for (int l = 0; l < 3 && !v->missMsg.n; l++) for (int r = 0; r < nr && !v->missMsg.n; r++) for (int z = 0; z < 2 && !v->missMsg.n; z++) {
            wchar_t p[1200]; swprintf(p, 1200, L"%ls\\Mals\\%ls\\MsgMain.msarc%ls", roots[r], langs[l], z ? L".zs" : L"");
            size_t bn; unsigned char *b = ArcReadAll(p, &bn); if (!b) continue;
            b = ArcUnzstd(b, bn, &bn); if (!b) continue;
            size_t mn; const unsigned char *m = V3SarcGet(b, bn, "Mission.msbt", &mn);
            if (m && MsbtParse(&v->missMsg, m, mn)) snprintf(v->missMsgFrom, sizeof v->missMsgFrom, "%ls", p);
            free(b);
        }
    }
    for (int i = 0; i < v->missMsg.n; i++) if (v->missMsg.e[i].label && !strcmp(v->missMsg.e[i].label, msgId)) { V3MsgText(&v->missMsg.e[i], buf, n); return buf; }
    return NULL;
}
// a P-switch mission in one line: its text, time limit and reward sticker (Gyml/WTAMission/<ms>.game__WTAMission.bgyml)
// a mission's reward sticker ("Sticker_1010002"; NULL if none)
static const char *V3MissionSticker(V3 *v, const char *ms) {
    V3GenArc(v);
    char mem[160]; snprintf(mem, sizeof mem, "Gyml/WTAMission/%s.game__WTAMission.bgyml", ms);
    size_t bn; const unsigned char *b = V3SarcGet(v->genArc, v->genArcN, mem, &bn); if (!b) return NULL;
    Byml y; BymlNode r = BymlOpen(&y, b, bn), sid = BymlGet(&y, r, "RewardStickerId");
    uint32_t id = sid.type == 0xD1 || sid.type == 0xD3 ? sid.v : 0;
    return id ? V3StickerName(v, id) : NULL;
}
/* a sticker's picture: its StickerInfoGParam gives RefAlb / RefMsk ("Work/Model/CmnTex/.../CmnTex_Sticker_Sticker_1010002_Alb.ftxb"),
   which are romfs\Tex\<name>.bntx.zs; the mask cuts its shape. RGBA (alpha = the mask), NULL if not found. */
static unsigned char *V3StickerImage(V3 *v, const char *stk, int *w, int *h) {
    V3GenArc(v);
    char mem[160], an[128] = "", mn[128] = ""; snprintf(mem, sizeof mem, "Gyml/StickerInfoGParam/%s.game__StickerInfoGParam.bgyml", stk);
    size_t bn; const unsigned char *b = V3SarcGet(v->genArc, v->genArcN, mem, &bn);
    if (b) {
        Byml y; BymlNode r = BymlOpen(&y, b, bn);
        const char *ra = BymlStr(&y, BymlGet(&y, r, "RefAlb")), *rm = BymlStr(&y, BymlGet(&y, r, "RefMsk"));
        if (ra) { const char *sl = strrchr(ra, '/'); snprintf(an, sizeof an, "%s", sl ? sl + 1 : ra); char *d = strrchr(an, '.'); if (d) *d = 0; }
        if (rm) { const char *sl = strrchr(rm, '/'); snprintf(mn, sizeof mn, "%s", sl ? sl + 1 : rm); char *d = strrchr(mn, '.'); if (d) *d = 0; }
    }
    if (!an[0]) { snprintf(an, sizeof an, "CmnTex_Sticker_%s_Alb", stk); snprintf(mn, sizeof mn, "CmnTex_Sticker_%s_Msk", stk); }
    unsigned char *px = V3ReadTex(v->dir, an, w, h); if (!px) return NULL;
    int mw = 0, mh = 0; unsigned char *mk = mn[0] ? V3ReadTex(v->dir, mn, &mw, &mh) : NULL;
    if (mk && mw > 0 && mh > 0) {
        int useA = 0; for (size_t k = 0; k < (size_t)mw * mh && !useA; k++) if (mk[k * 4 + 3] < 250) useA = 1;   // (the mask in alpha, else in red)
        for (int yy = 0; yy < *h; yy++) for (int xx = 0; xx < *w; xx++) {
            const unsigned char *q = mk + ((size_t)(yy * mh / *h) * mw + xx * mw / *w) * 4;
            px[((size_t)yy * *w + xx) * 4 + 3] = useA ? q[3] : q[0];
        }
    }
    free(mk);
    return px;
}
static LRESULT CALLBACK V3StkProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        V3 *v = (V3 *)GetWindowLongPtrW(h, GWLP_USERDATA);
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
        HBRUSH bg = CreateSolidBrush(UI_BG); FillRect(dc, &rc, bg); DeleteObject(bg);
        if (v && v->stkPx && v->stkW > 0 && v->stkH > 0) {               // the sticker over the background, by its mask
            int W = v->stkW, H = v->stkH; unsigned char *o = (unsigned char *)malloc((size_t)W * H * 4);
            if (o) {
                int br = GetRValue(UI_BG), bgc = GetGValue(UI_BG), bb = GetBValue(UI_BG);
                for (size_t k = 0; k < (size_t)W * H; k++) { const unsigned char *q = v->stkPx + k * 4; int a = q[3];
                    o[k * 4 + 0] = (unsigned char)((q[2] * a + bb * (255 - a)) / 255); o[k * 4 + 1] = (unsigned char)((q[1] * a + bgc * (255 - a)) / 255);
                    o[k * 4 + 2] = (unsigned char)((q[0] * a + br * (255 - a)) / 255); o[k * 4 + 3] = 0; }
                BITMAPINFO bi; ZeroMemory(&bi, sizeof bi); bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H;
                bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
                int m = 4, side = rc.right - 2 * m; SetStretchBltMode(dc, HALFTONE); SetBrushOrgEx(dc, 0, 0, NULL);
                StretchDIBits(dc, m, m, side, rc.bottom - 2 * m, 0, 0, W, H, o, &bi, DIB_RGB_COLORS, SRCCOPY);
                free(o);
            }
        }
        EndPaint(h, &ps); return 0;
    }
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(h, msg, wp, lp);
}
// show (stk set) or hide the sticker picture just below the hover tip
static void V3StickerShow(V3 *v, const char *stk) {
    if (!stk || !stk[0]) { if (v->stkWnd) ShowWindow(v->stkWnd, SW_HIDE); return; }
    if (strcmp(stk, v->stkName)) {
        free(v->stkPx); v->stkPx = NULL; v->stkW = v->stkH = 0; snprintf(v->stkName, sizeof v->stkName, "%s", stk);
        v->stkPx = V3StickerImage(v, stk, &v->stkW, &v->stkH);
        if (v->stkWnd) InvalidateRect(v->stkWnd, NULL, FALSE);
    }
    if (!v->stkPx) { if (v->stkWnd) ShowWindow(v->stkWnd, SW_HIDE); return; }
    if (!v->stkWnd) {
        static int reg = 0;
        if (!reg) { WNDCLASSEXW wc; ZeroMemory(&wc, sizeof wc); wc.cbSize = sizeof wc; wc.lpfnWndProc = V3StkProc; wc.hInstance = GetModuleHandleW(NULL);
                    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.lpszClassName = L"AlsarcVox3DSticker"; RegisterClassExW(&wc); reg = 1; }
        v->stkWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"AlsarcVox3DSticker", L"", WS_POPUP | WS_BORDER, 0, 0, 10, 10, v->wnd, NULL, GetModuleHandleW(NULL), NULL);
        if (!v->stkWnd) return;
        SetWindowLongPtrW(v->stkWnd, GWLP_USERDATA, (LONG_PTR)v);
    }
    RECT tr = { 0 }; if (v->prbTip) GetWindowRect(v->prbTip, &tr);
    int side = V3S(v, 136);
    SetWindowPos(v->stkWnd, HWND_TOPMOST, tr.left, tr.bottom + V3S(v, 4), side, side, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(v->stkWnd, NULL, FALSE);
}
static void V3MissionLine(V3 *v, const char *ms, wchar_t *out, int n) {
    V3GenArc(v);
    char mem[160]; snprintf(mem, sizeof mem, "Gyml/WTAMission/%s.game__WTAMission.bgyml", ms);
    size_t bn; const unsigned char *b = V3SarcGet(v->genArc, v->genArcN, mem, &bn);
    if (!b) { swprintf(out, n, L"mission %hs (its file isn't in the general_format archive)", ms); return; }
    Byml y; BymlNode r = BymlOpen(&y, b, bn);
    const char *mid = BymlStr(&y, BymlGet(&y, r, "MessageId")); float lim = BymlFloat(BymlGet(&y, r, "LimitSec"), 0);
    BymlNode sid = BymlGet(&y, r, "RewardStickerId"); uint32_t id = sid.type == 0xD1 || sid.type == 0xD3 ? sid.v : 0;
    const char *sn = id ? V3StickerName(v, id) : NULL;
    wchar_t tb[300]; const wchar_t *tx = mid ? V3MissionText(v, mid, tb, 300) : NULL;
    int o = swprintf(out, n, L"mission %hs", ms);
    if (tx) o += swprintf(out + o, n - o, L": \"%ls\"", tx);
    if (lim > 0) o += swprintf(out + o, n - o, L"  (%g s)", lim);
    if (sn) o += swprintf(out + o, n - o, L"  sticker %hs", sn);
    else if (id) o += swprintf(out + o, n - o, L"  sticker id %u", id);
}
/* The game's collectibles, from its own lists (System\GymlArchive\general_format.Product_*.sarc.zs,
   Gyml/FreerunCollectionListGParam/<Name>.game__FreerunCollectionListGParam.bgyml: ActorNames, TotalNum and
   Collection[].BancData[]: BancId (= the placed object's Hash), Pos (world), StickerName): PSwitch (the P-switches,
   CmnObj_MissionGate000-003_S), SpecialCoin (Peach medallions, CmnObj_Coin001_A), HatenaPanel (? panels,
   CmnObj_HatenaBlockPanel000_A). V3CollectPoints reads every point (and finds its placed object: rotation, scale,
   mission) before the objects are placed - those placed objects are then left to V3AddCollectibles, which draws each
   as the game's model at its real size, one entry per list ("Marker: PSwitch"), on the whole map too. The size can
   be enlarged (right-click the view > Collectible size), about each one's lowest point so it stays on its ground.
   Inside an island (its own coordinates): drawn at the island's way in. */
static void V3CollectPoints(V3 *v) {
    v->ncpt = 0; v->nclist = 0;
    free(v->actColl); v->actColl = v->nAllAct ? (unsigned char *)calloc((size_t)v->nAllAct, 1) : NULL;
    V3GenArc(v);
    const unsigned char *s = v->genArc; size_t sn = v->genArcN;
    if (!s || sn < 0x20 || memcmp(s, "SARC", 4)) return;
    uint32_t dataOff = ArcRd32(s + 0x0C); size_t o = s[4] | (s[5] << 8);
    if (o + 12 > sn || memcmp(s + o, "SFAT", 4)) return;
    int nf = s[o + 6] | (s[o + 7] << 8);
    size_t ents = o + 12, names = ents + (size_t)nf * 16 + 8;
    static const char pre[] = "Gyml/FreerunCollectionListGParam/";
    int capP = 0;
    for (int f = 0; f < nf && v->nclist < 8; f++) {
        const unsigned char *e = s + ents + (size_t)f * 16; uint32_t attr = ArcRd32(e + 4), st = ArcRd32(e + 8), en = ArcRd32(e + 12);
        size_t no = names + (size_t)(attr & 0xFFFFFF) * 4;
        if (!(attr >> 24) || no >= sn || strncmp((const char *)s + no, pre, sizeof pre - 1)) continue;
        if ((size_t)dataOff + en > sn || en < st) continue;
        const char *fn = (const char *)s + no + sizeof pre - 1;
        int li = v->nclist++;
        snprintf(v->clist[li], sizeof v->clist[li], "%s", fn); { char *dt = strchr(v->clist[li], '.'); if (dt) *dt = 0; }
        snprintf(v->clfile[li], sizeof v->clfile[li], "%s%s", pre, fn);
        Byml y; BymlNode root = BymlOpen(&y, s + dataOff + st, en - st);
        BymlNode tot = BymlGet(&y, root, "TotalNum"), an = BymlGet(&y, root, "ActorNames"), col = BymlGet(&y, root, "Collection");
        v->cltot[li] = tot.type == 0xD1 || tot.type == 0xD3 ? (int)tot.v : -1; v->clall[li] = 0; v->clnact[li] = 0;
        for (int a = 0; a < BymlCount(&y, an) && v->clnact[li] < 4; a++) {      // "Work/Gyml/ActorGParam/CmnObj_Coin001_A_00.gear__..." -> CmnObj_Coin001_A_00
            const char *nmA = BymlStr(&y, BymlAt(&y, an, a)); if (!nmA) continue;
            const char *b0 = strrchr(nmA, '/'); b0 = b0 ? b0 + 1 : nmA;
            char *d = v->clact[li][v->clnact[li]++]; snprintf(d, 96, "%s", b0); char *g = strchr(d, '.'); if (g) *g = 0;
        }
        for (int c = 0; c < BymlCount(&y, col); c++) {
            BymlNode bd = BymlGet(&y, BymlAt(&y, col, c), "BancData");
            for (int b = 0; b < BymlCount(&y, bd); b++) {
                BymlNode it = BymlAt(&y, bd, b), pos = BymlGet(&y, it, "Pos");
                float T[3] = { BymlFloat(BymlGet(&y, pos, "X"), 0), BymlFloat(BymlGet(&y, pos, "Y"), 0), BymlFloat(BymlGet(&y, pos, "Z"), 0) };
                v->clall[li]++;
                if (v->ncpt == capP) { int nc = capP ? capP * 2 : 1024; struct V3CPt *np = (struct V3CPt *)realloc(v->cpt, (size_t)nc * sizeof *np); if (!np) continue; v->cpt = np; capP = nc; }
                struct V3CPt *cp = &v->cpt[v->ncpt++]; memset(cp, 0, sizeof *cp);
                memcpy(cp->T, T, sizeof T); cp->list = li; cp->S[0] = cp->S[1] = cp->S[2] = 1;
                snprintf(cp->an, sizeof cp->an, "%s", v->clnact[li] ? v->clact[li][0] : "");
                uint64_t bid = BymlU64(&y, BymlGet(&y, it, "BancId"), 0);
                int qi = -1;
                for (int q = 0; bid && q < v->nisl; q++) if (v->isl[q].hash == bid) { qi = q; break; }   // inside an island: shown at the way in
                // (the list can name the island's original placement, whose Hash the _Fix file doesn't keep: then the same
                // object at the same spot in the island's own coordinates - Island_ShyGuyBazaar_00_Fix's P-switch)
                if (qi < 0 && !(T[0] == 0 && T[1] == 0 && T[2] == 0))
                    for (int q = 0; q < v->nisl && qi < 0; q++) {
                        const struct V3Isl *I = &v->isl[q];
                        if (fabsf(I->LT[0] - T[0]) > 0.05f || fabsf(I->LT[1] - T[1]) > 3.0f || fabsf(I->LT[2] - T[2]) > 0.05f) continue;
                        for (int a = 0; a < v->clnact[li]; a++) if (!strcmp(I->nm, v->clact[li][a])) { qi = q; break; }
                    }
                if (qi >= 0) {
                    const struct V3Isl *I = &v->isl[qi]; memcpy(cp->T, I->T, sizeof cp->T);
                    snprintf(cp->inm, sizeof cp->inm, "%s", I->nm); snprintf(cp->an, sizeof cp->an, "%s", I->nm);
                    snprintf(cp->island, sizeof cp->island, "%s", I->island); snprintf(cp->ms, sizeof cp->ms, "%s", I->ms); cp->noPos = I->noPos;
                }
                const char *sk = BymlStr(&y, BymlGet(&y, it, "StickerName"));
                if (sk && *sk) { const char *sl = strrchr(sk, '/'); snprintf(cp->stk, sizeof cp->stk, "%s", sl ? sl + 1 : sk); char *d2 = strchr(cp->stk, '.'); if (d2) *d2 = 0; }
                for (int w = 0; !cp->island[0] && w < v->nwta; w++)  // the placed object here that names a mission
                    if (fabsf(v->wta[w].T[0] - T[0]) < 0.05f && fabsf(v->wta[w].T[2] - T[2]) < 0.05f && fabsf(v->wta[w].T[1] - T[1]) < 0.05f) { snprintf(cp->ms, sizeof cp->ms, "%s", v->wta[w].ms); break; }
            }
        }
    }
    // the placed objects that are these points: their name (which model), rotation and scale; they're drawn here,
    // not by V3AddSurfaceObjects
    for (int i = 0; v->actColl && i < v->nAllAct; i++) {
        const V3Act *A = &v->allAct[i]; int lst = -1;
        for (int l = 0; l < v->nclist && lst < 0; l++) for (int a = 0; a < v->clnact[l]; a++) if (!strcmp(A->nm, v->clact[l][a])) { lst = l; break; }
        if (lst < 0) continue;
        // the nearest list point within 2 m across and 3 m up: a medallion's list position is its object's position less
        // its 1.8 m "up" - turned with the object, so a tilted one is off across too (0209: list -5804.83, 332.0, 1987.65,
        // object -5804.6, 333.8, 1987.6). Drawn where the object is.
        int best = -1; float bd = 1e30f;
        for (int c = 0; c < v->ncpt; c++) {
            const struct V3CPt *cp = &v->cpt[c];
            if (cp->list != lst || cp->island[0] || cp->placed) continue;
            float dx = cp->T[0] - A->T[0], dz = cp->T[2] - A->T[2], dy = cp->T[1] - A->T[1], d2 = dx * dx + dz * dz;
            if (d2 > 4.0f || fabsf(dy) > 3.0f || d2 + dy * dy * 0.01f >= bd) continue;
            bd = d2 + dy * dy * 0.01f; best = c;
        }
        if (best >= 0) {
            struct V3CPt *cp = &v->cpt[best];
            cp->placed = 1; memcpy(cp->T, A->T, sizeof cp->T);
            snprintf(cp->an, sizeof cp->an, "%s", A->nm); memcpy(cp->R, A->R, sizeof cp->R); memcpy(cp->S, A->S, sizeof cp->S);
            v->actColl[i] = (unsigned char)(lst + 1);
        }
    }
}
// the collectibles' size: each one scaled by v->collScale about its lowest point (no reload)
static float V3CollS(const V3 *v) { return v->collScale > 0 ? v->collScale : 1; }
static void V3CollApplyScale(V3 *v) {
    VoxPart *p = &v->part; float s = V3CollS(v);
    if (!v->corig) return;
    for (int i = 0; i < v->ncinst; i++) {
        const struct V3CInst *c = &v->cinst[i];
        for (size_t k = c->v0; k < c->v1 && k < p->ntv; k++) {
            const float *o = v->corig + (k - v->cv0) * 3; float *t = p->tv + k * 3;
            for (int q = 0; q < 3; q++) t[q] = c->A[q] + (o[q] - c->A[q]) * s;
        }
    }
}
static void V3AddCollectibles(V3 *v) {
    VoxPart *p = &v->part; v->ncoll = 0; v->ncinst = 0; free(v->cinst); v->cinst = NULL; free(v->corig); v->corig = NULL;
    if (!(p->terrWorld[2] > 0) || p->gridW < 2 || !v->ncpt) return;
    wchar_t root[2100]; swprintf(root, 2100, L"%ls", v->dir);
    if (!V3SceneRoot(root)) return;
    float ws = p->terrWorld[2], half = p->terrWorld[3] * ws / 2;
    int whole = !(p->terrArea[2] > 0);
    float ax0 = -1e30f, az0 = -1e30f, ax1 = 1e30f, az1 = 1e30f;
    if (!whole) { ax0 = (p->terrArea[0] - p->terrWorld[0]) * ws; az0 = (p->terrArea[1] - p->terrWorld[1]) * ws; ax1 = ax0 + p->terrArea[2] * ws; az1 = az0 + p->terrArea[2] * ws; }
    struct { char an[96]; VoxPart *m; float lowY; } mc[16]; int nmc = 0;     // the models, loaded once each
    v->cinst = (struct V3CInst *)calloc((size_t)v->ncpt, sizeof *v->cinst);
    if (!v->cinst) return;
    v->cv0 = p->ntv; v->glowAll = 1;                                // (their glow textures added: the "not yet done" look)
    int here[8] = { 0 }, mat[8]; for (int l = 0; l < 8; l++) mat[l] = -1;
    for (int c = 0; c < v->ncpt; c++) {
        const struct V3CPt *cp = &v->cpt[c];
        // not drawn: an island with no known way in, or a point with no placed object that looks like an island's own
        // coordinates (near 0, 0, 0). Others with no placed object found (the medallions) are drawn at the list's position.
        if (cp->noPos || (!cp->placed && !cp->island[0] && fabsf(cp->T[0]) < 300 && fabsf(cp->T[2]) < 300 && fabsf(cp->T[1]) < 50)) continue;
        if (cp->T[0] + half < ax0 || cp->T[0] + half > ax1 || cp->T[2] + half < az0 || cp->T[2] + half > az1) continue;
        char ent[VOX_MATN]; snprintf(ent, sizeof ent, "Marker: %s", v->clist[cp->list]);
        int k = 0; while (k < nmc && strcmp(mc[k].an, cp->an)) k++;
        if (k == nmc && nmc < 16) {
            snprintf(mc[k].an, sizeof mc[k].an, "%s", cp->an); mc[k].m = NULL; mc[k].lowY = 0; nmc++;
            wchar_t mp[2300];
            if (cp->an[0] && V3ModelPath(v, root, cp->an, 0, mp, 2300)) {
                VoxPart *m = (VoxPart *)calloc(1, sizeof(VoxPart));
                if (m && (!VoxLoadBfres(m, mp) || !m->nti)) { VoxFreePart(m); free(m); m = NULL; }
                mc[k].m = m;
            }
        }
        VoxPart *m = k < nmc ? mc[k].m : NULL;
        struct V3CInst *in = &v->cinst[v->ncinst]; in->cpt = c; in->v0 = p->ntv;
        if (!m) {                                                   // no model: a small diamond
            int mi = V3AddMarkerSz(v, p, cp->T, half, ent, 1.5f, 0); if (mi < 0) continue;
            mat[cp->list] = mi;
        } else {
            const float *T = cp->T, *Rr = cp->R, *S = cp->S;
            float cx = cosf(Rr[0]), sx = sinf(Rr[0]), cy = cosf(Rr[1]), sy = sinf(Rr[1]), cz = cosf(Rr[2]), sz = sinf(Rr[2]);
            size_t nv0 = p->ntv, nt0 = p->nti;
            if (!V3Reserve(v, p, nv0 + m->ntv, nt0 + m->nti)) break;
            for (size_t q = 0; q < m->ntv; q++) {                   // (as the placed objects: scale, then about X, Y, Z)
                float x = m->tv[q * 3] * S[0], yy = m->tv[q * 3 + 1] * S[1], zz = m->tv[q * 3 + 2] * S[2];
                float y1 = yy * cx - zz * sx, z1 = yy * sx + zz * cx, x2 = x * cy + z1 * sy, z2 = -x * sy + z1 * cy, x3 = x2 * cz - y1 * sz, y3 = x2 * sz + y1 * cz;
                float *o = p->tv + (nv0 + q) * 3; o[0] = x3 + T[0] + half; o[1] = y3 + T[1]; o[2] = z2 + T[2] + half;
                if (p->tuv) { if (m->tuv) memcpy(p->tuv + (nv0 + q) * 2, m->tuv + q * 2, 8); else p->tuv[(nv0 + q) * 2] = p->tuv[(nv0 + q) * 2 + 1] = 0; }
            }
            int had = p->nmat, mi = PhMat(p, ent); if (mi < 0 || mi >= VOX_MAXMAT) break;
            if (mi >= had) p->startHidden[mi] = 0;
            size_t added = 0;
            for (size_t q = 0; q < m->nti; q++) {
                if (m->tmat && m->startHidden[m->tmat[q]]) continue;      // (far / spare versions)
                for (int e = 0; e < 3; e++) p->ti[(nt0 + added) * 3 + e] = (uint32_t)(nv0 + m->ti[q * 3 + e]);
                p->ttex[nt0 + added] = V3TexSlot(v, m, m->tmat ? m->tmat[q] : 0);
                p->tmat[nt0 + added] = (uint16_t)mi; p->matCount[mi]++; added++;
            }
            p->ntv = nv0 + m->ntv; p->nti = nt0 + added; p->solid = p->nti;
            mat[cp->list] = mi;
        }
        in->v1 = p->ntv;
        float lo = 1e30f, hi = -1e30f; for (size_t q = in->v0; q < in->v1; q++) { float yv = p->tv[q * 3 + 1]; if (yv < lo) lo = yv; if (yv > hi) hi = yv; }
        in->A[0] = cp->T[0] + half; in->A[1] = lo < 1e29f ? lo : cp->T[1]; in->A[2] = cp->T[2] + half; in->h = hi > lo ? (hi - lo) / 2 : 1;
        here[cp->list]++; v->ncinst++;
    }
    v->glowAll = 0;
    for (int k = 0; k < nmc; k++) if (mc[k].m) { VoxFreePart(mc[k].m); free(mc[k].m); }
    size_t nv = p->ntv - v->cv0;                                     // (the real-size copy, for enlarging)
    v->corig = nv ? (float *)malloc(nv * 3 * sizeof(float)) : NULL;
    if (v->corig) memcpy(v->corig, p->tv + v->cv0 * 3, nv * 3 * sizeof(float));
    if (V3CollS(v) != 1) V3CollApplyScale(v);
    for (int l = 0; l < v->nclist && v->ncoll < 8; l++) {
        if (mat[l] < 0) continue;
        int k = v->ncoll++; v->coll[k].mat = mat[l]; v->coll[k].list = l;
        int q = snprintf(v->coll[k].txt, sizeof v->coll[k].txt, "the game's collectible list %s\n%d here, of %d in the list", v->clfile[l], here[l], v->clall[l]);
        if (v->cltot[l] >= 0) q += snprintf(v->coll[k].txt + q, sizeof v->coll[k].txt - q, " (TotalNum %d)", v->cltot[l]);
        for (int a = 0; a < v->clnact[l] && q < (int)sizeof v->coll[k].txt - 100; a++) q += snprintf(v->coll[k].txt + q, sizeof v->coll[k].txt - q, "%s%s", a ? ", " : "\nobjects: ", v->clact[l][a]);
        if (V3CollS(v) != 1 && q < (int)sizeof v->coll[k].txt - 40) snprintf(v->coll[k].txt + q, sizeof v->coll[k].txt - q, "\nshown %g x their size", V3CollS(v));
    }
}
// Most cliffs, rocks and other fixed scenery are merged by the game into "gridify" pieces, one set per 1000 x 1000
// world square (X 0-15, Z 0-15; square X's centre is at (X - 7.5) x 1000): Model\Piece_Gridify_Game03_X12_Z04.
// Gridify_Game03_X12_Z04_<row><col>.bfres.zs (4 x 4 per square; Piece_Gridify_..._<row><col> are the coarser 2 x 2).
// Each part's name says what it came from (..._Mt_CmnObj_CliffICProc..._Mt_CliffWall_Blend): cliffs and ice are
// listed by area, everything else as "Scenery (other)", hidden at first. Area views only (the whole map would need
// about a thousand of these files).
static const char *V3PieceKind(const char *shape) {
    if (strstr(shape, "SpecialSurface")) return NULL;                 // drawn from its own placement already
    if (strstr(shape, "Iceberg")) return "Iceberg";
    const char *c = strstr(shape, "CmnObj_Cliff"); if (c) { const char *k = V3CliffKind(c); if (k) return k; }
    return "Scenery (other)";
}
// a ground piece's part (or a merged object) -> its entry: the object it came from, grouped by kind -
// "Cliffs: CliffVL012_A", "Ice cliffs: ...", "Icebergs: ...", "Scenery: Rock003_A" ("Scenery: other pieces" if the
// part doesn't name its object); "" for special surfaces (drawn from their own placements)
static void V3PieceEntry(const char *shape, char *out, size_t n) {
    out[0] = 0;
    const char *k = V3PieceKind(shape); if (!k) return;
    char g[24]; snprintf(g, sizeof g, "%s", k); char *cc = strchr(g, ':'); if (cc) *cc = 0;
    if (!strcmp(g, "Scenery (other)")) snprintf(g, sizeof g, "Scenery");
    // the object the part came from: the first of the game's object prefixes in its name (CmnObj_ dropped, like
    // everywhere in the list; the others kept: "EditObj_RoadsideMesh_Game03_X04_Z12")
    static const char *const pre[] = { "CmnObj_", "EditObj_", "Stage_", "EffectObj_", "SystemObj_", "MapObj_", "Npc_", "Enemy_" };
    const char *o = NULL; int cmn = 0;
    for (size_t i = 0; i < sizeof pre / sizeof pre[0]; i++) { const char *f = strstr(shape, pre[i]); if (f && (!o || f < o)) { o = f; cmn = i == 0; } }
    if (!o) {
        // no object name: per-square meshes say what and where in their material - "Mt_Road_Game03_X04_Z12_13_..."
        // (the road of map square X04 Z12, ground-piece cell 13) -> group "Road", entry "Game03_X04_Z12"
        for (const char *q = strstr(shape, "Mt_"); q; q = strstr(q + 3, "Mt_")) {
            const char *wd = q + 3, *us = strchr(wd, '_');
            if (!us || us == wd || us - wd > 20 || strncmp(us + 1, "Game03_X", 8)) continue;
            const char *sq = us + 1; size_t L2 = 0;                      // "Game03_X04_Z12"
            while (sq[L2] && L2 < 14) L2++;
            if (L2 < 14 || sq[10] != '_' || sq[11] != 'Z') continue;
            snprintf(out, n, "%.*s: %.*s", (int)(us - wd), wd, 14, sq);
            return;
        }
        snprintf(out, n, "%s: other pieces", g); return;
    }
    char nm[VOX_MATN]; size_t L = 0; if (cmn) o += 7;
    while (o[L] && o[L] != '|' && L + 1 < sizeof nm && strncmp(o + L, "_Mt_", 4) && strncmp(o + L, "__", 2)) L++;
    memcpy(nm, o, L); nm[L] = 0;
    if (L > 3 && !strcmp(nm + L - 3, "_00")) nm[L - 3] = 0;
    if (!strncmp(nm, "SFC", 3)) snprintf(g, sizeof g, "Surface");          // the game's surface objects (SFCGhostValley...)
    snprintf(out, n, "%s: %s", g, nm);
}
static void V3AddGridifyPieces(V3 *v) {
    VoxPart *p = &v->part;
    if (!(p->terrWorld[2] > 0) || p->gridW < 2) return;
    int whole = p->terrArea[2] <= 0;
    wchar_t root[2100]; swprintf(root, 2100, L"%ls", v->dir);
    if (!V3SceneRoot(root)) return;
    float ws = p->terrWorld[2], half = p->terrWorld[3] * ws / 2;
    float ax0 = -1e30f, az0 = -1e30f, ax1 = 1e30f, az1 = 1e30f;
    if (!whole) { ax0 = (p->terrArea[0] - p->terrWorld[0]) * ws; az0 = (p->terrArea[1] - p->terrWorld[1]) * ws; ax1 = ax0 + p->terrArea[2] * ws; az1 = az0 + p->terrArea[2] * ws; }
    int cx0 = 0, cx1 = 15, cz0 = 0, cz1 = 15;
    if (!whole) { cx0 = (int)floorf((ax0 - half) / 1000 + 8); cx1 = (int)floorf((ax1 - half) / 1000 + 8); cz0 = (int)floorf((az0 - half) / 1000 + 8); cz1 = (int)floorf((az1 - half) / 1000 + 8); }
    float cell = whole ? 500.0f : 250.0f;                                  // fine pieces (4 x 4) in areas, coarse (2 x 2) on the whole map
    // every piece file of the squares in view, with where its cell's centre is
    typedef struct { wchar_t *path; float offx, offz; } V3Piece;
    V3Piece *list = NULL; int nlist = 0, caplist = 0;
    for (int cz = cz0; cz <= cz1; cz++) for (int cx = cx0; cx <= cx1; cx++) {
        if (cx < 0 || cz < 0 || cx > 15 || cz > 15) continue;
        wchar_t pat[2300]; swprintf(pat, 2300, whole ? L"%ls\\Model\\Piece_Gridify_Game03_X%02d_Z%02d.Piece_Gridify_Game03_X%02d_Z%02d_*.bfres.zs"
                                                    : L"%ls\\Model\\Piece_Gridify_Game03_X%02d_Z%02d.Gridify_Game03_X%02d_Z%02d_*.bfres.zs", root, cx, cz, cx, cz);
        WIN32_FIND_DATAW fd; HANDLE fh = FindFirstFileW(pat, &fd);
        if (fh == INVALID_HANDLE_VALUE) continue;
        do {
            // which cell: the two digits before ".bfres.zs" = row (z) then column (x); a piece's vertices are relative to
            // its cell's centre (checked: the same cliff in a fine and a coarse piece lines up exactly that way)
            const wchar_t *dot = wcsstr(fd.cFileName, L".bfres.zs"); if (!dot || dot - fd.cFileName < 3) continue;
            int row = dot[-2] - L'0', col = dot[-1] - L'0'; if (row < 0 || row > 3 || col < 0 || col > 3) continue;
            if (nlist == caplist) { int c2 = caplist ? caplist * 2 : 256; V3Piece *nl = (V3Piece *)realloc(list, (size_t)c2 * sizeof *nl); if (!nl) break; list = nl; caplist = c2; }
            wchar_t mp[2300]; swprintf(mp, 2300, L"%ls\\Model\\%ls", root, fd.cFileName);
            list[nlist].path = _wcsdup(mp);
            list[nlist].offx = (cx - 8) * 1000 + (col + 0.5f) * cell; list[nlist].offz = (cz - 8) * 1000 + (row + 0.5f) * cell;
            nlist++;
        } while (FindNextFileW(fh, &fd));
        FindClose(fh);
    }
    // load them on all cores, a batch at a time (each loaded piece is held until it's merged), then merge in order
    V3StageCount(v, L"Loading the ground pieces (cliffs, rocks...)", 650, 150, nlist);
    for (int b0 = 0; b0 < nlist; b0 += 48) {
        int bn = nlist - b0 < 48 ? nlist - b0 : 48;
        V3LoadJob jobs[48]; memset(jobs, 0, sizeof jobs);
        for (int j = 0; j < bn; j++) { jobs[j].path = list[b0 + j].path; jobs[j].done = &v->ldDone; }
        V3ParallelFor(bn, V3LoadJobFn, jobs);
        for (int j = 0; j < bn; j++) {
            VoxPart *m = jobs[j].part; if (!m) continue;
            float offx = list[b0 + j].offx, offz = list[b0 + j].offz;
            size_t nv0 = p->ntv, nt0 = p->nti;
            int32_t *remap = V3Reserve(v, p, nv0 + m->ntv, nt0 + m->nti) ? (int32_t *)malloc(m->ntv * sizeof(int32_t)) : NULL;
            if (!remap) { VoxFreePart(m); free(m); continue; }
            for (size_t k = 0; k < m->ntv; k++) remap[k] = -1;
            int matOf[256]; for (int k = 0; k < 256; k++) matOf[k] = -2;
            size_t added = 0, used = 0;
            for (size_t k = 0; k < m->nti; k++) {
                unsigned pt = m->tmat ? m->tmat[k] : 0;          // (every part, even ones whose bone starts hidden)
                if (matOf[pt] == -2) {
                    char both[200]; snprintf(both, sizeof both, "%s|%s", m->shapeFull[pt][0] ? m->shapeFull[pt] : m->matName[pt], m->matFull[pt]);
                    char ent[VOX_MATN]; V3PieceEntry(both, ent, sizeof ent);
                    int had = p->nmat;
                    matOf[pt] = ent[0] ? PhMat(p, ent) : -1;
                    if (ent[0] && !strncmp(ent, "Scenery:", 8) && matOf[pt] >= had && matOf[pt] < VOX_MAXMAT) p->startHidden[matOf[pt]] = 1;
                    if (matOf[pt] >= 0 && matOf[pt] < VOX_MAXMAT) {
                        int mi = matOf[pt]; v->fromPiece[mi] = 1;
                        char line[260]; snprintf(line, sizeof line, "%s\n", both);           // (each part name once per entry, up to 64 KB)
                        size_t ol = v->pieceTxt[mi] ? strlen(v->pieceTxt[mi]) : 0;
                        if (ol < 65536 && (!v->pieceTxt[mi] || !strstr(v->pieceTxt[mi], line))) {
                            char *nt = (char *)realloc(v->pieceTxt[mi], ol + strlen(line) + 1);
                            if (nt) { strcpy(nt + ol, line); v->pieceTxt[mi] = nt; }
                        }
                    }
                }
                if (matOf[pt] < 0) continue;
                const uint32_t *q = m->ti + k * 3;
                float tx = 0, tz = 0; for (int c = 0; c < 3; c++) { tx += (m->tv[q[c] * 3] + offx + half) / 3; tz += (m->tv[q[c] * 3 + 2] + offz + half) / 3; }
                if (tx < ax0 || tx > ax1 || tz < az0 || tz > az1) continue;
                uint32_t *o = p->ti + (nt0 + added) * 3;
                for (int c = 0; c < 3; c++) {
                    if (remap[q[c]] < 0) {
                        remap[q[c]] = (int32_t)used; float *d = p->tv + (nv0 + used) * 3; const float *sv = m->tv + q[c] * 3;
                        d[0] = sv[0] + offx + half; d[1] = sv[1]; d[2] = sv[2] + offz + half;
                        if (m->tuv) memcpy(p->tuv + (nv0 + used) * 2, m->tuv + (size_t)q[c] * 2, 8);
                        used++;
                    }
                    o[c] = (uint32_t)(nv0 + remap[q[c]]);
                }
                p->ttex[nt0 + added] = V3TexSlot(v, m, pt);
                p->tmat[nt0 + added] = (uint16_t)matOf[pt]; p->matCount[matOf[pt]]++; added++;
            }
            free(remap);
            p->ntv = nv0 + used; p->nti = nt0 + added; p->solid = p->nti;
            VoxFreePart(m); free(m);
        }
    }
    for (int i = 0; i < nlist; i++) free(list[i].path);
    free(list);
}
/* ------------------------------------------------------------------ texture cache
   Shrunk textures are kept in <exe folder>\tex_cache\<hash>.bin (header: "NSTC", width, height, see-through flag,
   the source file's size and time; then RGBA), so the next opening doesn't decode them again. A changed source file
   (another game version / a new dump) is decoded again. */
typedef struct { char magic[4]; int w, h, alpha; uint64_t srcSize, srcTime; } V3CacheHead;
static int V3SourceStamp(const wchar_t *modelPath, const char *name, uint64_t *sz, uint64_t *tm) {
    wchar_t roots[12][1024]; int nr = V3RomfsRoots(modelPath, roots);
    for (int r = 0; r < nr; r++) {
        wchar_t f[1200]; swprintf(f, 1200, L"%ls\\Tex\\%hs.bntx.zs", roots[r], name);
        WIN32_FILE_ATTRIBUTE_DATA a;
        if (GetFileAttributesExW(f, GetFileExInfoStandard, &a)) {
            *sz = ((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow; *tm = ((uint64_t)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
            return 1;
        }
    }
    return 0;
}
static void V3CachePath(const wchar_t *modelPath, const char *name, int size, const char *kind, wchar_t *out, size_t n) {
    wchar_t exe[1024]; GetModuleFileNameW(NULL, exe, 1024); wchar_t *sl = wcsrchr(exe, L'\\'); if (sl) *sl = 0;
    char key[1400]; snprintf(key, sizeof key, "%ls|%s|%d|%s", modelPath, name, size, kind);
    const char *k2 = key;
    unsigned long long h = 1469598103934665603ULL; for (const char *q = k2; *q; q++) { h ^= (unsigned char)*q; h *= 1099511628211ULL; }
    swprintf(out, n, L"%ls\\tex_cache\\%016llx.bin", exe, h);
}
#define V3_DISK_CACHE 0     // off: it saved only a few seconds per opening for ~50 MB of disk
static unsigned char *V3CacheLoad(const wchar_t *path, uint64_t sz, uint64_t tm, int *w, int *h, int *alpha) {
    if (!V3_DISK_CACHE) return NULL;
    size_t n; unsigned char *b = VoxReadFile(path, &n); if (!b) return NULL;
    V3CacheHead hd; if (n < sizeof hd) { free(b); return NULL; }
    memcpy(&hd, b, sizeof hd);
    if (memcmp(hd.magic, "NSTC", 4) || hd.srcSize != sz || hd.srcTime != tm || hd.w < 1 || hd.h < 1 || hd.w > 8192 || hd.h > 8192 ||
        n < sizeof hd + (size_t)hd.w * hd.h * 4) { free(b); return NULL; }
    unsigned char *px = (unsigned char *)malloc((size_t)hd.w * hd.h * 4);
    if (px) memcpy(px, b + sizeof hd, (size_t)hd.w * hd.h * 4);
    free(b); *w = hd.w; *h = hd.h; *alpha = hd.alpha; return px;
}
static void V3CacheSave(const wchar_t *path, uint64_t sz, uint64_t tm, const unsigned char *px, int w, int h, int alpha) {
    if (!V3_DISK_CACHE) return;
    wchar_t dir[1100]; swprintf(dir, 1100, L"%ls", path); wchar_t *sl = wcsrchr(dir, L'\\'); if (sl) { *sl = 0; CreateDirectoryW(dir, NULL); }
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL); if (f == INVALID_HANDLE_VALUE) return;
    V3CacheHead hd; memcpy(hd.magic, "NSTC", 4); hd.w = w; hd.h = h; hd.alpha = alpha; hd.srcSize = sz; hd.srcTime = tm;
    DWORD wr; WriteFile(f, &hd, sizeof hd, &wr, NULL); WriteFile(f, px, (DWORD)((size_t)w * h * 4), &wr, NULL); CloseHandle(f);
}
// the placed scenery's textures: each read from Tex\<name>.bntx.zs, made small (they're seen from far off) and decoded
// on all cores. Kept between loads.
typedef struct { V3 *v; int slot, size; unsigned char *px; int w, h, alpha; } V3TexJob;
static void V3TexJobFn(void *ctx, int i) {
    V3TexJob *j = (V3TexJob *)ctx + i;
    const char *name = j->v->mtexName[j->slot];
    uint64_t sz = 0, tm = 0; wchar_t cp[1200]; int haveStamp = V3SourceStamp(j->v->dir, name, &sz, &tm);
    if (haveStamp) {
        V3CachePath(j->v->dir, name, j->size, "mdl", cp, 1200);
        int cw, ch, ca; unsigned char *c = V3CacheLoad(cp, sz, tm, &cw, &ch, &ca);
        if (c) { j->px = c; j->w = cw; j->h = ch; j->alpha = ca; return; }
    }
    int w = 0, h = 0; unsigned char *px = NULL;
    const char *bar = strchr(name, '|');
    if (bar) {
        // "albedo|emission", both array textures (one layer per state): the state whose glow is brightest (a ? panel
        // not yet driven through: its yellow "?"), albedo + glow
        char an[128], en[128]; snprintf(an, sizeof an, "%.*s", (int)(bar - name), name); snprintf(en, sizeof en, "%s", bar + 1);
        int nl = 0, best = 0; double bestL = -1; unsigned char *em = NULL; int ew = 0, eh = 0;
        for (int L = 0; L < 8; L++) {
            int w2 = 0, h2 = 0, n2 = 0; unsigned char *q = V3ReadTexL(j->v->dir, en, L, &w2, &h2, &n2);
            if (!L) nl = n2;
            // (the most COLOURFUL glow, not the brightest: a P-switch's brightest layer is a plain white flash, which
            // turned it grey with a white P; the ? panel's is its yellow "?")
            if (q) { double sum = 0; for (size_t k = 0; k < (size_t)w2 * h2; k++) { int a = q[k * 4], b2 = q[k * 4 + 1], c2 = q[k * 4 + 2];
                     int mx = a > b2 ? (a > c2 ? a : c2) : (b2 > c2 ? b2 : c2), mn = a < b2 ? (a < c2 ? a : c2) : (b2 < c2 ? b2 : c2); sum += mx - mn; }
                     if (sum > bestL) { bestL = sum; best = L; free(em); em = q; ew = w2; eh = h2; } else free(q); }
            if (L + 1 >= nl) break;
        }
        px = V3ReadTexL(j->v->dir, an, best, &w, &h, NULL);
        if (px && em && ew > 0 && eh > 0)
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
                unsigned char *d = px + ((size_t)y * w + x) * 4; const unsigned char *q = em + ((size_t)(y * eh / h) * ew + x * ew / w) * 4;
                for (int c = 0; c < 3; c++) { int v2 = d[c] + q[c] * 5 / 2; d[c] = (unsigned char)(v2 > 255 ? 255 : v2); }   // (glow x2.5: lit as in the game, not shaded)
            }
        free(em);
    } else px = V3ReadTex(j->v->dir, name, &w, &h);
    if (!px || w < 1 || h < 1) { free(px); return; }
    if (strstr(name, "Lava") && strlen(name) > 4 && !strcmp(name + strlen(name) - 4, "_Alb")) {
        // lava glows in the game (its _Emm texture); added on top so it isn't a dull brown
        char en[160]; snprintf(en, sizeof en, "%.*s_Emm", (int)strlen(name) - 4, name);
        int ew = 0, eh = 0; unsigned char *em = V3ReadTex(j->v->dir, en, &ew, &eh);
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            unsigned char *d = px + ((size_t)y * w + x) * 4;
            int e[3];
            if (em && ew > 0 && eh > 0) {
                const unsigned char *q = em + ((size_t)(y * eh / h) * ew + x * ew / w) * 4;
                int g = (q[0] * 3 + q[1] * 6 + q[2]) / 10, grey = abs(q[0] - q[1]) < 12 && abs(q[1] - q[2]) < 12;
                if (grey) { e[0] = g; e[1] = g * 45 / 100; e[2] = g / 10; } else { e[0] = q[0]; e[1] = q[1]; e[2] = q[2]; }
            } else {                                     // no glow map: brighten the bright parts toward orange
                int g = (d[0] * 3 + d[1] * 6 + d[2]) / 10; e[0] = g; e[1] = g * 40 / 100; e[2] = 0;
            }
            for (int c = 0; c < 3; c++) { int v2 = d[c] + e[c] * 2; d[c] = (unsigned char)(v2 > 255 ? 255 : v2); }
        }
        free(em);
    }
    int W = w, H = h; while (W > j->size || H > j->size) { W = W > 1 ? W / 2 : 1; H = H > 1 ? H / 2 : 1; }
    unsigned char *o = (unsigned char *)malloc((size_t)W * H * 4); if (!o) { free(px); return; }
    int sx = w / W, sy = h / H, al = 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {               // box filter
        unsigned acc[4] = { 0, 0, 0, 0 };
        for (int yy = 0; yy < sy; yy++) for (int xx = 0; xx < sx; xx++) { const unsigned char *q = px + ((size_t)(y * sy + yy) * w + x * sx + xx) * 4; for (int c = 0; c < 4; c++) acc[c] += q[c]; }
        unsigned char *d = o + ((size_t)y * W + x) * 4; for (int c = 0; c < 4; c++) d[c] = (unsigned char)(acc[c] / (sx * sy));
        if (d[3] < 128) al = 1;
    }
    free(px);
    j->px = o; j->w = W; j->h = H; j->alpha = al;
    if (haveStamp) V3CacheSave(cp, sz, tm, o, W, H, al);
}
static void V3TexJobCounted(void *ctx, int i) { V3TexJobFn(ctx, i); InterlockedIncrement(&((V3TexJob *)ctx)[i].v->ldDone); }
static void V3LoadModelTextures(V3 *v) {
    int size = v->part.terrArea[2] > 0 ? 256 : 64;
    V3TexJob *jobs = (V3TexJob *)calloc(v->nmtex + 1, sizeof *jobs); int nj = 0;
    if (!jobs) return;
    for (int i = 0; i < v->nmtex; i++) {
        char key[200]; snprintf(key, sizeof key, "mdl|%d|%s", size, v->mtexName[i]);
        int k = -1; for (int t = 0; t < v->ntex; t++) if (!strcmp(v->tex[t].key, key)) { k = t; break; }
        v->mtexTex[i] = k;
        if (k < 0) { jobs[nj].v = v; jobs[nj].slot = i; jobs[nj].size = size; nj++; }
    }
    V3StageCount(v, L"Loading the objects' textures", 840, 90, nj);
    V3ParallelFor(nj, V3TexJobCounted, jobs);
    for (int j = 0; j < nj; j++) {
        if (v->ntex >= V3_MAXTEX) { free(jobs[j].px); continue; }
        int k = v->ntex++; snprintf(v->tex[k].key, sizeof v->tex[k].key, "mdl|%d|%s", size, v->mtexName[jobs[j].slot]);
        v->tex[k].rgba = jobs[j].px; v->tex[k].w = jobs[j].w; v->tex[k].h = jobs[j].h; v->tex[k].alpha = jobs[j].alpha; v->tex[k].id = 0;
        v->mtexTex[jobs[j].slot] = jobs[j].px ? k : -1;
    }
    free(jobs);
}
/* Baked light probes for the whole course: Env\World_<scene>.dlpr.zs. After "DLPR", version: a 128 x 128 map of
   the world (one byte per 125-unit square: how many probes are in use there, 0-4; row = z, column = x), then one
   block of 164 floats per square in the same order: 4 probes of 40 floats (32 light values, then position slots
   that are empty here) and the stack: lowest height, then the spacing up to the 2nd, 3rd and 4th probe. The
   square's probes stand at its corner: x = column x 125 - 8000, z = row x 125 - 8000 (checked against the .dlpg
   files, which give the positions). Kept per probe: x, y, z in this view's coordinates, then the 32 values. */
#define V3_PRB_STRIDE 35
static void V3LoadProbes(V3 *v) {
    free(v->prbLoad); v->prbLoad = NULL; v->nprbLoad = 0; v->nprbFineLoad = 0; v->prbFileLoad[0] = 0;
    VoxPart *p = &v->part;
    if (!(p->terrWorld[2] > 0)) return;
    wchar_t roots[12][1024]; int nr = V3RomfsRoots(v->dir, roots);
    wchar_t path[1200] = L""; int found = 0;
    for (int r = 0; r < nr && !found; r++) {
        wchar_t pat[1200]; swprintf(pat, 1200, L"%ls\\Env\\World_*.dlpr.zs", roots[r]);
        WIN32_FIND_DATAW fd; HANDLE fh = FindFirstFileW(pat, &fd);
        if (fh == INVALID_HANDLE_VALUE) continue;
        do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) { swprintf(path, 1200, L"%ls\\Env\\%ls", roots[r], fd.cFileName); found = 1; } } while (!found && FindNextFileW(fh, &fd));
        FindClose(fh);
    }
    if (!found) return;
    size_t n = 0; unsigned char *f = VoxReadFile(path, &n); if (!f) return;
    size_t dn = 0; unsigned char *d = VoxUnzstd(f, n, &dn); free(f);
    if (!d || dn < 8 + 16384 + (size_t)16384 * 656 || memcmp(d, "DLPR", 4)) { free(d); return; }
    const unsigned char *map = d + 8; const float *blk = (const float *)(d + 8 + 16384);
    float ws = p->terrWorld[2], half = p->terrWorld[3] * ws / 2;
    float ax0 = -1e30f, az0 = -1e30f, ax1 = 1e30f, az1 = 1e30f;                    // an area view: its probes only
    if (p->terrArea[2] > 0) { ax0 = (p->terrArea[0] - p->terrWorld[0]) * ws; az0 = (p->terrArea[1] - p->terrWorld[1]) * ws; ax1 = ax0 + p->terrArea[2] * ws; az1 = az0 + p->terrArea[2] * ws; }
    int total = 0; for (int i = 0; i < 16384; i++) total += map[i] > 4 ? 4 : map[i];
    v->prbLoad = (float *)malloc((size_t)(total ? total : 1) * V3_PRB_STRIDE * sizeof(float));
    if (!v->prbLoad) { free(d); return; }
    for (int i = 0; i < 16384; i++) {
        int L = map[i] > 4 ? 4 : map[i]; if (!L) continue;
        const float *b = blk + (size_t)i * 164;
        float x = (i % 128) * 125.0f - 8000 + half, z = (i / 128) * 125.0f - 8000 + half;
        if (x < ax0 || x > ax1 || z < az0 || z > az1) continue;
        float y = b[160];
        for (int k = 0; k < L; k++) {
            if (k) y += b[160 + k];
            float *o = v->prbLoad + (size_t)v->nprbLoad * V3_PRB_STRIDE;
            o[0] = x; o[1] = y; o[2] = z; memcpy(o + 3, b + k * 40, 32 * sizeof(float));
            v->nprbLoad++;
        }
    }
    free(d);
    const wchar_t *fn = wcsrchr(path, L'\\'); snprintf(v->prbFileLoad, sizeof v->prbFileLoad, "%ls", fn ? fn + 1 : path);
    v->nprbFineLoad = 0;
    if (!(p->terrArea[2] > 0)) return;
    /* a full-detail area: the finer grids too - Env\World_<scene>_X###_Z###_<n>.dlpg.zs, one per 125-unit square that
       needs more detail (towns, tunnels, bridges...): 40-float probes with their own position (floats 33-35, world
       coordinates), each vertical stack followed by 4 floats (lowest height, spacing x 3) */
    wchar_t edir[1200]; swprintf(edir, 1200, L"%ls", path); { wchar_t *sl = wcsrchr(edir, L'\\'); if (sl) *sl = 0; }
    int X0 = (int)floorf((ax0 - half + 8000) / 125), X1 = (int)ceilf((ax1 - half + 8000) / 125) - 1;
    int Z0 = (int)floorf((az0 - half + 8000) / 125), Z1 = (int)ceilf((az1 - half + 8000) / 125) - 1;
    if (X0 < 0) X0 = 0; if (Z0 < 0) Z0 = 0; if (X1 > 127) X1 = 127; if (Z1 > 127) Z1 = 127;
    int cap = total ? total : 1;                         // (what the list was made for)
    for (int Z = Z0; Z <= Z1; Z++) for (int X = X0; X <= X1; X++) {
        wchar_t pat[1300]; swprintf(pat, 1300, L"%ls\\World_*_X%03d_Z%03d_*.dlpg.zs", edir, X, Z);
        WIN32_FIND_DATAW fd; HANDLE fh = FindFirstFileW(pat, &fd);
        if (fh == INVALID_HANDLE_VALUE) continue;
        do {
            wchar_t gp[1400]; swprintf(gp, 1400, L"%ls\\%ls", edir, fd.cFileName);
            size_t gn = 0; unsigned char *gf = VoxReadFile(gp, &gn); if (!gf) continue;
            size_t gdn = 0; unsigned char *g = VoxUnzstd(gf, gn, &gdn); free(gf);
            if (!g || gdn < 8 + 160 || memcmp(g, "DLPG", 4)) { free(g); continue; }
            size_t n = (gdn - 8) / 4; float *f = (float *)malloc(n * sizeof(float)); if (!f) { free(g); continue; }
            memcpy(f, g + 8, n * sizeof(float)); free(g);
            for (size_t o = 0; o + 40 <= n; ) {
                int stack = f[o + 1] > 0.01f && fabsf(f[o + 1] - f[o + 2]) < 1e-3f && fabsf(f[o + 2] - f[o + 3]) < 1e-3f;
                if (!stack && f[o + 35] == 0.0f && fabsf(f[o + 39]) < 1e-30f && fabsf(f[o + 32]) < 1e6f && fabsf(f[o + 33]) < 1e6f && fabsf(f[o + 34]) < 1e6f) {
                    if (v->nprbLoad >= cap) {
                        cap = cap * 2; float *np = (float *)realloc(v->prbLoad, (size_t)cap * V3_PRB_STRIDE * sizeof(float));
                        if (!np) break; v->prbLoad = np;
                    }
                    float *q = v->prbLoad + (size_t)v->nprbLoad * V3_PRB_STRIDE;
                    q[0] = f[o + 32] + half; q[1] = f[o + 33]; q[2] = f[o + 34] + half; memcpy(q + 3, f + o, 32 * sizeof(float));
                    v->nprbLoad++; v->nprbFineLoad++;
                    o += 40;
                } else o += 4;
            }
            free(f);
        } while (FindNextFileW(fh, &fd));
        FindClose(fh);
    }
}
/* Sound (acoustic) probes, full-detail areas only: Sound\\Acoustic\\Probe\\<scene>\\A_B.dapg.zs, 32 x 32 files of
   500 x 500 m covering the 16 km world, each a 64 x 64 grid of 7.8 m cells: u32 64, then 64 x 64 u16 (running total
   of records up to that cell), then 124-byte records: lowest and highest height (2 floats), 2 u16, 4 floats, and 12
   pairs of (distance, u32 code) - probably the distance to the nearest surface in 12 directions and what it is made
   of. Which file number is x and which z, and the cell order, aren't known: all 4 ways are tried against the ground
   heights (a cell's lowest height should sit on the ground) and the best kept.
   Kept per record: x, y (lowest height), z, then lo, hi, n1, n2, 4 values, 12 distances, 12 codes (low byte). */
#define V3_SND_STRIDE 35
static void V3LoadSounds(V3 *v) {
    free(v->sndLoad); v->sndLoad = NULL; v->nsndLoad = 0; v->sndOrientLoad = -1;
    VoxPart *p = &v->part;
    if (!(p->terrArea[2] > 0) || !(p->terrWorld[2] > 0) || p->gridW < 2) return;
    wchar_t roots[12][1024]; int nr = V3RomfsRoots(v->dir, roots);
    wchar_t dir[1200] = L""; int found = 0;
    for (int r = 0; r < nr && !found; r++) {
        wchar_t pat[1200]; swprintf(pat, 1200, L"%ls\\Sound\\Acoustic\\Probe\\*", roots[r]);
        WIN32_FIND_DATAW fd; HANDLE fh = FindFirstFileW(pat, &fd); if (fh == INVALID_HANDLE_VALUE) continue;
        do { if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.') { swprintf(dir, 1200, L"%ls\\Sound\\Acoustic\\Probe\\%ls", roots[r], fd.cFileName); found = 1; } } while (!found && FindNextFileW(fh, &fd));
        FindClose(fh);
    }
    if (!found) return;
    float ws = p->terrWorld[2], half = p->terrWorld[3] * ws / 2;
    float ax0 = (p->terrArea[0] - p->terrWorld[0]) * ws, az0 = (p->terrArea[1] - p->terrWorld[1]) * ws, ax1 = ax0 + p->terrArea[2] * ws, az1 = az0 + p->terrArea[2] * ws;
    int F0x = (int)floorf((ax0 - half + 8000) / 500), F1x = (int)ceilf((ax1 - half + 8000) / 500) - 1;
    int F0z = (int)floorf((az0 - half + 8000) / 500), F1z = (int)ceilf((az1 - half + 8000) / 500) - 1;
    if (F0x < 0) F0x = 0; if (F0z < 0) F0z = 0; if (F1x > 31) F1x = 31; if (F1z > 31) F1z = 31;
    // read the files of the area's 500 m squares under both namings (A = x or A = z); keep them to place after choosing
    typedef struct { int fx, fz, swap; unsigned char *d; size_t n; } SndFile;
    SndFile fl[64]; int nf = 0;
    for (int fz = F0z; fz <= F1z; fz++) for (int fx = F0x; fx <= F1x; fx++) for (int sw = 0; sw < 2 && nf < 64; sw++) {
        wchar_t fp[1300]; swprintf(fp, 1300, L"%ls\\%d_%d.dapg.zs", dir, sw ? fz : fx, sw ? fx : fz);
        size_t n = 0; unsigned char *f = VoxReadFile(fp, &n); if (!f) continue;
        size_t dn = 0; unsigned char *d = VoxUnzstd(f, n, &dn); free(f);
        if (!d || dn < 4 + 8192 || *(uint32_t *)d != 64) { free(d); continue; }
        fl[nf].fx = fx; fl[nf].fz = fz; fl[nf].swap = sw; fl[nf].d = d; fl[nf].n = dn; nf++;
    }
    // choose: file naming (swap) x cell order (z-major or x-major) by how well the lowest heights match the ground
    double err[4] = { 0, 0, 0, 0 }; int cnt[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < nf; i++) {
        const unsigned char *d = fl[i].d; const uint16_t *ix = (const uint16_t *)(d + 4); const unsigned char *rec = d + 4 + 8192;
        size_t nrec = (fl[i].n - 4 - 8192) / 124;
        for (int c = 0; c < 4096; c += 7) {
            unsigned r0 = c ? ix[c - 1] : 0; if (r0 >= nrec || ix[c] <= r0) continue;
            float lo = *(const float *)(rec + (size_t)r0 * 124);
            for (int order = 0; order < 2; order++) {
                int cx = order ? c / 64 : c % 64, cz = order ? c % 64 : c / 64;
                float x = fl[i].fx * 500.0f - 8000 + (cx + 0.5f) * 7.8125f + half, z = fl[i].fz * 500.0f - 8000 + (cz + 0.5f) * 7.8125f + half;
                int in; float gh = V3GridH(p, x, z, &in); if (!in) continue;
                int k = fl[i].swap * 2 + order; err[k] += fabs(lo - gh); cnt[k]++;
            }
        }
    }
    int best = -1; double be = 1e30;
    for (int k = 0; k < 4; k++) if (cnt[k] > 20) { double e = err[k] / cnt[k]; if (e < be) { be = e; best = k; } }
    if (best < 0) best = 0;                                        // (all sea: any way looks the same)
    v->sndOrientLoad = best;
    int cap = 4096; v->sndLoad = (float *)malloc((size_t)cap * V3_SND_STRIDE * sizeof(float));
    for (int i = 0; i < nf && v->sndLoad; i++) {
        if (fl[i].swap != best / 2) continue;
        const unsigned char *d = fl[i].d; const uint16_t *ix = (const uint16_t *)(d + 4); const unsigned char *rec = d + 4 + 8192;
        size_t nrec = (fl[i].n - 4 - 8192) / 124;
        for (int c = 0; c < 4096; c++) {
            int cx = (best & 1) ? c / 64 : c % 64, cz = (best & 1) ? c % 64 : c / 64;
            float x = fl[i].fx * 500.0f - 8000 + (cx + 0.5f) * 7.8125f + half, z = fl[i].fz * 500.0f - 8000 + (cz + 0.5f) * 7.8125f + half;
            if (x < ax0 || x > ax1 || z < az0 || z > az1) continue;
            unsigned r0 = c ? ix[c - 1] : 0, r1 = ix[c]; if (r1 > nrec) r1 = (unsigned)nrec;
            for (unsigned r = r0; r < r1; r++) {
                if (v->nsndLoad >= cap) { cap *= 2; float *np = (float *)realloc(v->sndLoad, (size_t)cap * V3_SND_STRIDE * sizeof(float)); if (!np) break; v->sndLoad = np; }
                const unsigned char *R = rec + (size_t)r * 124; float *o = v->sndLoad + (size_t)v->nsndLoad * V3_SND_STRIDE;
                float lo = *(const float *)R, hi = *(const float *)(R + 4);
                o[0] = x; o[1] = lo; o[2] = z; o[3] = lo; o[4] = hi; o[5] = *(const uint16_t *)(R + 8); o[6] = *(const uint16_t *)(R + 10);
                for (int k = 0; k < 4; k++) o[7 + k] = *(const float *)(R + 12 + 4 * k);
                for (int k = 0; k < 12; k++) { o[11 + k] = *(const float *)(R + 28 + 8 * k); o[23 + k] = (float)R[32 + 8 * k]; }
                v->nsndLoad++;
            }
        }
    }
    for (int i = 0; i < nf; i++) free(fl[i].d);
}
static int V3PrbCmp(const void *a, const void *b);
static void V3SoundRange(V3 *v) {
    if (!v->nsnd || v->sndSel <= 0) return;
    float *t = (float *)malloc((size_t)v->nsnd * sizeof(float)); if (!t) return;
    int n = 0, c = 2 + v->sndSel;
    for (int i = 0; i < v->nsnd; i++) { float q = v->snd[(size_t)i * V3_SND_STRIDE + c]; if (q == q && fabsf(q) < 1e20f) t[n++] = q; }
    qsort(t, n, sizeof(float), V3PrbCmp);
    v->sndLo = n ? t[n / 100] : 0; v->sndHi = n ? t[n * 99 / 100] : 1; if (!(v->sndHi > v->sndLo)) v->sndHi = v->sndLo + 1e-6f;
    free(t);
}
static const char *V3SoundName(int sel) {                         // the drop-down's fields (sel 1..32)
    static char b[64];
    if (sel == 1) return "lowest height (the ground)"; if (sel == 2) return "highest height (ground + 90 m)";
    if (sel <= 4) { snprintf(b, sizeof b, "surface type %d (road, grass, rail, water...)", sel - 2); return b; }
    if (sel == 5) return "value 1 (distance from the sea: - at sea, + inland)";
    if (sel <= 8) { snprintf(b, sizeof b, "value %d (%s)", sel - 4, sel == 6 ? "5 at sea, 0 inland" : sel == 7 ? "about 0.2 at sea" : "1 at sea"); return b; }
    static const char *ring[3] = { "low", "up", "steeply up" }, *dir[4] = { "north-east", "north-west", "south-west", "south-east" };
    if (sel <= 20) { int k = sel - 9; snprintf(b, sizeof b, "distance %d (%s, %s)", k + 1, ring[k / 4], dir[k % 4]); return b; }
    { int k = sel - 21; snprintf(b, sizeof b, "enclosed %d (%s, %s; 0-100)", k + 1, ring[k / 4], dir[k % 4]); return b; }
}

// the colour range for the chosen entry: 1st-99th percentile (single values), or the 99th percentile of the brightest channel (colours)
static int V3PrbCmp(const void *a, const void *b) { float x = *(const float *)a, y = *(const float *)b; return x < y ? -1 : x > y; }
static void V3ProbeRange(V3 *v) {
    if (!v->nprb || v->prbSel <= 0) return;
    float *t = (float *)malloc((size_t)v->nprb * sizeof(float)); if (!t) return;
    int n = 0;
    if (v->prbSel <= 8) {                                                    // a set of 3 as a colour
        int b = 3 + (v->prbSel - 1) * 3;
        for (int i = 0; i < v->nprb; i++) { const float *q = v->prb + (size_t)i * V3_PRB_STRIDE + b; float m = fabsf(q[0]); if (fabsf(q[1]) > m) m = fabsf(q[1]); if (fabsf(q[2]) > m) m = fabsf(q[2]); if (m == m) t[n++] = m; }
        qsort(t, n, sizeof(float), V3PrbCmp);
        v->prbLo[0] = 0; v->prbHi[0] = n ? t[n * 95 / 100] : 1; if (!(v->prbHi[0] > 1e-9f)) v->prbHi[0] = 1;
    } else {
        int c = 3 + v->prbSel - 9;
        for (int i = 0; i < v->nprb; i++) { float q = v->prb[(size_t)i * V3_PRB_STRIDE + c]; if (q == q) t[n++] = q; }
        qsort(t, n, sizeof(float), V3PrbCmp);
        v->prbLo[0] = n ? t[n / 100] : 0; v->prbHi[0] = n ? t[n * 99 / 100] : 1; if (!(v->prbHi[0] > v->prbLo[0])) v->prbHi[0] = v->prbLo[0] + 1e-6f;
    }
    free(t);
}
static void V3ProbeColour(V3 *v, const float *q, unsigned char *o) {
    if (v->prbSel <= 8) {
        const float *c = q + 3 + (v->prbSel - 1) * 3;
        for (int k = 0; k < 3; k++) { float x = fabsf(c[k]) / v->prbHi[0]; if (!(x > 0)) x = 0; if (x > 1) x = 1; o[k] = (unsigned char)(sqrtf(x) * 255); }   // (square root: dim light still shows; direction sets: size only)
        o[3] = 255;
    } else {
        float x = (q[3 + v->prbSel - 9] - v->prbLo[0]) / (v->prbHi[0] - v->prbLo[0]); if (!(x > 0)) x = 0; if (x > 1) x = 1;
        VoxHeat((unsigned)(x * 255), o);
    }
}
/* Invisible areas from WorldResident, drawn as see-through boxes / balls (Translate, Rotate X-Y-Z, Scale = half
   the size; PrimitiveType Cube or Sphere): the free-roam out-of-bounds zones (SystemObj_WorldEndArea - drive in
   and you're picked up and put back), rescue, valley (fall) and keep-out areas, and each course's area. Each kind is
   its own list entry; only the out-of-bounds zones start ticked. */
// one object's shape (the game's PrimitiveType: box, ball or cylinder) as list entry 'label', see-through; in a
// full-detail area cut off at its edges. 0 if nothing of it was added
static int V3AddShape(V3 *v, const V3Act *A, const char *label, int shown) {
    VoxPart *p = &v->part;
    float ws = p->terrWorld[2], half = p->terrWorld[3] * ws / 2;
    float ax0 = -1e30f, az0 = -1e30f, ax1 = 1e30f, az1 = 1e30f;
    if (p->terrArea[2] > 0) { ax0 = (p->terrArea[0] - p->terrWorld[0]) * ws; az0 = (p->terrArea[1] - p->terrWorld[1]) * ws; ax1 = ax0 + p->terrArea[2] * ws; az1 = az0 + p->terrArea[2] * ws; }
    int sphere = A->sphere;
    float T[3], R[3], S[3];
    for (int k = 0; k < 3; k++) { T[k] = A->T[k]; R[k] = A->R[k]; S[k] = A->S[k]; }
    float reach = fmaxf(S[0], fmaxf(S[1], S[2])) * 1.8f;
    if (T[0] + half + reach < ax0 || T[0] + half - reach > ax1 || T[2] + half + reach < az0 || T[2] + half - reach > az1) return 0;
    // the shape around 0 (the game's PrimitiveType): a box of +-1, a ball (12 x 8 bands), or a cylinder round in x / z
    // and 2 high in y (24 sides; the game's CylinderHighRes - the warp pipes' zones, IslandTunnelSensor...)
    float pv[200][3]; int np = 0; uint32_t pt[400][3]; int nt = 0;
    if (sphere == 2) {
        const int K = 24;
        for (int k = 0; k < K; k++) for (int e = 0; e < 2; e++) { float a = 6.2831853f * k / K; pv[np][0] = cosf(a); pv[np][1] = e ? 1.f : -1.f; pv[np][2] = sinf(a); np++; }
        int cb = np; pv[np][0] = 0; pv[np][1] = -1; pv[np][2] = 0; np++; pv[np][0] = 0; pv[np][1] = 1; pv[np][2] = 0; np++;
        for (int k = 0; k < K; k++) {
            uint32_t b0 = k * 2, t0 = k * 2 + 1, b1 = ((k + 1) % K) * 2, t1 = b1 + 1;
            pt[nt][0] = b0; pt[nt][1] = t0; pt[nt][2] = b1; nt++; pt[nt][0] = b1; pt[nt][1] = t0; pt[nt][2] = t1; nt++;   // side
            pt[nt][0] = cb; pt[nt][1] = b0; pt[nt][2] = b1; nt++; pt[nt][0] = cb + 1; pt[nt][1] = t1; pt[nt][2] = t0; nt++; // ends
        }
    } else if (sphere != 1) {
        for (int c = 0; c < 8; c++) { pv[np][0] = c & 1 ? 1 : -1; pv[np][1] = c & 2 ? 1 : -1; pv[np][2] = c & 4 ? 1 : -1; np++; }
        static const int f[6][4] = { { 0, 1, 3, 2 }, { 4, 6, 7, 5 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 5, 7, 3 } };
        for (int q = 0; q < 6; q++) { pt[nt][0] = f[q][0]; pt[nt][1] = f[q][1]; pt[nt][2] = f[q][2]; nt++; pt[nt][0] = f[q][0]; pt[nt][1] = f[q][2]; pt[nt][2] = f[q][3]; nt++; }
    } else {
        for (int la = 0; la <= 8; la++) for (int lo = 0; lo < 12; lo++) {
            float th = 3.14159265f * la / 8, ph = 6.2831853f * lo / 12;
            pv[np][0] = sinf(th) * cosf(ph); pv[np][1] = cosf(th); pv[np][2] = sinf(th) * sinf(ph); np++;
        }
        for (int la = 0; la < 8; la++) for (int lo = 0; lo < 12; lo++) {
            uint32_t a0 = la * 12 + lo, a1 = la * 12 + (lo + 1) % 12, b0 = a0 + 12, b1 = a1 + 12;
            pt[nt][0] = a0; pt[nt][1] = b0; pt[nt][2] = a1; nt++; pt[nt][0] = a1; pt[nt][1] = b0; pt[nt][2] = b1; nt++;
        }
    }
    float cx = cosf(R[0]), sx = sinf(R[0]), cy = cosf(R[1]), sy = sinf(R[1]), cz = cosf(R[2]), sz = sinf(R[2]);
    float wv[200][3];
    for (int k = 0; k < np; k++) {
        float x = pv[k][0] * S[0], yy = pv[k][1] * S[1], zz = pv[k][2] * S[2];
        float y1 = yy * cx - zz * sx, z1 = yy * sx + zz * cx, x2 = x * cy + z1 * sy, z2 = -x * sy + z1 * cy, x3 = x2 * cz - y1 * sz, y3 = x2 * sz + y1 * cz;
        wv[k][0] = x3 + T[0] + half; wv[k][1] = y3 + T[1]; wv[k][2] = z2 + T[2] + half;
    }
    size_t nv0 = p->ntv, nt0 = p->nti, maxv = (size_t)nt * 9, maxt = (size_t)nt * 7;
    if (!V3Reserve(v, p, nv0 + maxv, nt0 + maxt)) return 0;
    int mi = PhMat(p, label);
    if (mi >= 0 && mi < VOX_MAXMAT && p->matCount[mi] == 0) p->startHidden[mi] = (unsigned char)!shown;
    V3MarkDrawn(v, A->nm, p->matName[mi]);
    size_t av = 0, at = 0;
    for (int k = 0; k < nt; k++) {
        // cut to the area shown (a full-detail area): the triangle clipped to x0..x1, z0..z1, then a fan
        float poly[12][3], tmpp[12][3]; int n2 = 3;
        for (int c = 0; c < 3; c++) memcpy(poly[c], wv[pt[k][c]], 12);
        if (p->terrArea[2] > 0) {
            const float lim[4] = { ax0, ax1, az0, az1 }; const int axis[4] = { 0, 0, 2, 2 }, side[4] = { 1, -1, 1, -1 };
            for (int e = 0; e < 4 && n2 >= 3; e++) {
                int m = 0;
                for (int c = 0; c < n2 && m < 11; c++) {
                    const float *A = poly[c], *B = poly[(c + 1) % n2];
                    float da = (A[axis[e]] - lim[e]) * side[e], db = (B[axis[e]] - lim[e]) * side[e];
                    if (da >= 0) memcpy(tmpp[m++], A, 12);
                    if ((da >= 0) != (db >= 0) && m < 11) { float t2 = da / (da - db); for (int q = 0; q < 3; q++) tmpp[m][q] = A[q] + (B[q] - A[q]) * t2; m++; }
                }
                n2 = m; memcpy(poly, tmpp, sizeof(float) * 3 * (size_t)m);
            }
        }
        if (n2 < 3) return 0;
        for (int c = 0; c < n2; c++) { memcpy(p->tv + (nv0 + av + c) * 3, poly[c], 12); p->tuv[(nv0 + av + c) * 2] = p->tuv[(nv0 + av + c) * 2 + 1] = 0; }
        for (int c = 1; c + 1 < n2; c++) {
            uint32_t *o = p->ti + (nt0 + at) * 3; o[0] = (uint32_t)(nv0 + av); o[1] = (uint32_t)(nv0 + av + c); o[2] = (uint32_t)(nv0 + av + c + 1);
            p->tmat[nt0 + at] = (uint16_t)mi; p->ttex[nt0 + at] = 0xFFFF; p->matCount[mi]++; at++;
        }
        av += (size_t)n2;
    }
    p->ntv = nv0 + av; p->nti = nt0 + at; p->solid = p->nti;
    return 1;
}
static void V3AddAreas(V3 *v) {
    VoxPart *p = &v->part;
    if (!(p->terrWorld[2] > 0) || p->gridW < 2 || !v->allAct) return;
    V3StageCount(v, L"Adding the areas", 800, 10, v->nAllAct * 2);
    for (int pass = 0; pass < 2; pass++)                 // the known kinds (out of bounds...) first, then any others
    for (int i = 0; i < v->nAllAct; i++) {
        if (!(i & 255)) v->ldDone = pass * v->nAllAct + i;
        const V3Act *A = &v->allAct[i];
        int shown = 0; char abuf[40]; const char *label = V3AreaLabel(A->nm, &shown, abuf);
        if (!label || V3AreaKnown(A->nm) == pass) continue;
        if (pass && p->nmat >= VOX_MAXMAT - 6) label = "Area: other";
        V3AddShape(v, A, label, shown);
    }
}
static int V3LoadTerrainGrid(V3 *v) {
    int grid = v->lodSel == 1 ? 2048 : 1024;
    float x = 0, z = 0, sz = 0; int ovl = -1;
    if (!wcsncmp(v->geom, L"terrainarea:", 12)) swscanf(v->geom + 12, L"%f,%f,%f,%d", &x, &z, &sz, &ovl);
    if (ovl > 0 && ovl < VOV_COUNT && !v->ovlFromParent) { v->ovlSel = ovl; v->ovlFromParent = 1; v->ovlFresh = 1; }   // opened from a view showing a layer
    g_voxOverlay = v->ovlSel;                        // (this loading thread only)
    V3Stage(v, L"Loading the ground tiles...", 20);
    int ok = VoxLoadTerrainRegion(&v->part, v->dir, grid, x, z, sz);
    if (ok) {
        v->capV = v->part.ntv; v->capT = v->part.nti;          // (the ground's arrays are exactly that big)
        memset(v->fromPiece, 0, sizeof v->fromPiece);
        for (int k = 0; k < VOX_MAXMAT; k++) { free(v->pieceTxt[k]); v->pieceTxt[k] = NULL; }
        v->nmtex = 0; V3AddSurfaceObjects(v); V3AddGridifyPieces(v);
        V3AddAreas(v); V3AddCollectibles(v);
        V3Stage(v, L"Loading the ground textures...", 810); V3LoadTerrainTextures(v);
        V3LoadModelTextures(v);
        V3Stage(v, L"Loading the light probes...", 930); V3LoadProbes(v);
        V3Stage(v, L"Loading the sound probes...", 960); V3LoadSounds(v);
        V3Stage(v, L"Finishing...", 990);
        for (int k = 0; k < v->part.nmat && k < VOX_MAXMAT; k++)                     // see-through, drawn last: areas, markers / shapes
            if (!strncmp(v->part.matName[k], "Area:", 5) || !strncmp(v->part.matName[k], "Marker:", 7) || strstr(v->part.matName[k], "(no model)")) {
                int solid = 0; for (int c = 0; c < v->ncoll; c++) if (v->coll[c].mat == k) solid = 1;   // (collectibles: solid)
                if (!solid) v->waterBlend[k] = 2;
            }
        VoxPart *p = &v->part;                          // a texture that couldn't be read: that scenery keeps its plain colour
        if (p->ttex) for (size_t t = 0; t < p->nti; t++) if (p->ttex[t] != 0xFFFF && (p->ttex[t] >= v->nmtex || v->mtexTex[p->ttex[t]] < 0)) p->ttex[t] = 0xFFFF;
    }
    return ok;
}

static DWORD WINAPI V3Worker(LPVOID p) {
    V3Job *j = (V3Job *)p;
    V3 *v = j->v;
    if (j->doLoad) {
        int ok = !wcscmp(v->geom, L"phive") ? VoxLoadPhive(&v->part, v->dir) : !wcscmp(v->geom, L"terrain") ? VoxLoadTerrain(&v->part, v->dir) : V3IsTerrainGrid(v) ? V3LoadTerrainGrid(v) : !wcscmp(v->geom, L"bfres") ? VoxLoadBfres(&v->part, v->dir)
               : !wcscmp(v->geom, L"cloth") ? V3LoadClothView(v)
               : v->geom[0] ? VoxLoadObject(&v->part, v->dir, v->geom) : VoxLoad(&v->part, v->dir);
        if (ok && v->part.isModel) {
            v->bodyN = v->part.skN;
            if (v->part.skN && v->part.vBone) {
                V3AttachParts(v);
                const wchar_t *fh = wcsrchr(v->dir, L'\\'); fh = fh ? fh + 1 : v->dir;
                char hbBase[300]; snprintf(hbBase, sizeof hbBase, "%ls", fh); char *dt = strchr(hbBase, '.'); if (dt) *dt = 0;
                V3LoadHelperBones(v, hbBase);
            }
            {   // cloth physics: the opened .bphcl, or the model's own and its attachments'
                const wchar_t *fn = wcsrchr(v->dir, L'\\'); fn = fn ? fn + 1 : v->dir;
                char base[300]; snprintf(base, sizeof base, "%ls", fn); char *dot = strchr(base, '.'); if (dot) *dot = 0;
                if (!wcscmp(v->geom, L"cloth")) { V3AddClothFile(v, v->dir, NULL, ""); for (int a = 0; a < v->nAtt; a++) { wchar_t p[1024]; if (v->att[a].defShown && V3FindCloth(v, v->att[a].name, p)) { char pre[40]; snprintf(pre, sizeof pre, "%s: ", v->att[a].name); V3AddClothFile(v, p, v->att[a].Tb, pre); } } }
                else V3AddCloths(v, base);
            }
            V3LoadTextures(v);
            // animation files: X.anim.bfres.zs beside X.Y.bfres.zs, plus files shared under another name
            if (v->part.skN && v->part.vBone) {
                v->hasSkeleton = 1;
                const wchar_t *fn = wcsrchr(v->dir, L'\\'); fn = fn ? fn + 1 : v->dir;
                wchar_t base[300]; swprintf(base, 300, L"%ls", fn); wchar_t *dot = wcschr(base, L'.'); if (dot) *dot = 0;
                wchar_t names[8][300]; int nn = V3AnimNames(base, names);
                wchar_t roots[12][1024]; int nr = V3RomfsRoots(v->dir, roots);
                for (int k = 0; k < nn && v->nAnimFiles < 15; k++) {
                    wchar_t ap[1024]; swprintf(ap, 1024, L"%ls", v->dir);
                    wchar_t *slash = wcsrchr(ap, L'\\'); if (slash) swprintf(slash + 1, 1024 - (slash + 1 - ap), L"%ls.anim.bfres.zs", names[k]);
                    int found = GetFileAttributesW(ap) != INVALID_FILE_ATTRIBUTES;
                    for (int r = 0; r < nr && !found; r++) { swprintf(ap, 1024, L"%ls\\Model\\%ls.anim.bfres.zs", roots[r], names[k]); found = GetFileAttributesW(ap) != INVALID_FILE_ATTRIBUTES; }
                    if (!found) continue;
                    BfAnimFile f;
                    if (!BfAnimFileOpen(&f, ap)) continue;
                    V3ColScan(v, &f, ap);                                       // colour clips, for the Colours list
                    if (!f.nplay) { BfAnimFileFree(&f); continue; }            // material colour clips only
                    int i = v->nAnimFiles++;
                    swprintf(v->animFiles[i], 1024, L"%ls", ap); v->animFileCount[i] = f.count;
                    // start with the file that moves bones and has the most clips (PlayerAnimation for DK,
                    // PaulineAnimation rather than Pauline's own 1-clip file), else the first one
                    if (!v->anims.count || (!v->anims.nsk && f.nsk) || (!v->anims.nsk == !f.nsk && f.count > v->anims.count)) { BfAnimFileFree(&v->anims); v->anims = f; v->animFileSel = i; } else BfAnimFileFree(&f);
                }
            }
        }
        PostMessageW(v->wnd, WM_V3_LOADED, 0, ok);
        if (!ok) { free(j); return 0; }
        // window picks LOD and starts the mesh build after LOADED
        free(j); return 0;
    }
    VoxMesh *m = (VoxMesh *)calloc(1, sizeof(VoxMesh));
    if (m && !VoxBuildMesh(&v->part, m, j->lod, j->hidden, &v->cancel, &v->progress)) { free(m); m = NULL; }
    PostMessageW(v->wnd, WM_V3_MESHED, (WPARAM)j->gen, (LPARAM)m);
    free(j);
    return 0;
}

static void V3StartJob(V3 *v, int doLoad) {
    V3Job *j = (V3Job *)calloc(1, sizeof(V3Job));
    if (!j) return;
    j->v = v; j->doLoad = doLoad; j->lod = v->lodSel; j->gen = ++v->gen;
    memcpy(j->hidden, v->hidden, sizeof j->hidden);
    for (int m = 0; m < VOX_MAXMAT; m++) if (v->visCtl[m]) j->hidden[m] = 0;      // shown / hidden while drawing instead
    v->cancel = 0; v->progress = 0;
    if (doLoad) V3Stage(v, L"Loading...", -1);
    if (!v->busyOn) v->busyT0 = GetTickCount();
    v->thread = CreateThread(NULL, 0, V3Worker, j, 0, NULL);
    if (!v->thread) { free(j); return; }
    SetTimer(v->wnd, V3_TIMER_PROGRESS, 200, NULL);
}

// the whole terrain: load it again from the tiles for the chosen resolution (low: the 21 biggest tiles; full: 85)
static void V3ReloadWorld(V3 *v) {
    if (v->thread) { v->reloadPending = 1; v->cancel = 1; return; }
    if (v->partOk && v->part.nmat) {                      // remember the list's ticks by name (the next area / level has its own entries)
        int n = v->part.nmat < VOX_MAXMAT ? v->part.nmat : VOX_MAXMAT;
        char (*nn)[VOX_MATN] = (char (*)[VOX_MATN])realloc(v->keepName, (size_t)(v->nkeep + n) * VOX_MATN); unsigned char *nh = (unsigned char *)realloc(v->keepHidden, (size_t)(v->nkeep + n));
        if (nn) v->keepName = nn; if (nh) v->keepHidden = nh;
        if (nn && nh) for (int k = 0; k < n; k++) {
            int q = 0; while (q < v->nkeep && strcmp(v->keepName[q], v->part.matName[k])) q++;
            if (q == v->nkeep) { snprintf(v->keepName[q], VOX_MATN, "%s", v->part.matName[k]); v->nkeep++; }
            v->keepHidden[q] = v->hidden[k];
        }
    }
    if (v->dropKeep) { v->nkeep = 0; v->dropKeep = 0; }   // back to game colours from a layer: the usual ticks again
    v->partOk = 0; v->rebuildPending = 0;
    VoxFreePart(&v->part);
    memset(v->hidden, 0, sizeof v->hidden);
    swprintf(v->status, 256, v->lodSel == 1 ? L"Loading the terrain in full detail..." : L"Loading the terrain at low resolution...");
    V3SetInfo(v, v->status);
    v->loading = 1;
    V3StartJob(v, 1);
}
static void V3RequestBuild(V3 *v) {
    if (!v->partOk) return;
    if (v->thread) { v->rebuildPending = 1; v->cancel = 1; return; }
    swprintf(v->status, 256, L"Building the surface...");
    V3StartJob(v, 0);
    InvalidateRect(v->info, NULL, TRUE);
}

/* ------------------------------------------------------------------ info text */
static void V3Num(wchar_t *out, size_t n, double x) {
    if (x >= 1e6) swprintf(out, n, L"%.2f million", x / 1e6);
    else if (x >= 1e4) swprintf(out, n, L"%.0f,%03.0f", floor(x / 1000), fmod(x, 1000));
    else swprintf(out, n, L"%.0f", x);
}
static void V3SetInfo(V3 *v, const wchar_t *t) {                  // the info box: new text, same scroll position
    wchar_t old[1700]; GetWindowTextW(v->info, old, 1700); if (!wcscmp(old, t)) return;
    int first = (int)SendMessageW(v->info, EM_GETFIRSTVISIBLELINE, 0, 0);
    SetWindowTextW(v->info, t);
    if (first > 0) SendMessageW(v->info, EM_LINESCROLL, 0, first);
}
static void V3UpdateInfo(V3 *v) {
    wchar_t t[1600], a[64], b[64], c[64];
    if (!v->partOk) { V3SetInfo(v, v->status); return; }
    VoxPart *p = &v->part;
    V3Num(a, 64, (double)p->solid); V3Num(b, 64, (double)p->surface); V3Num(c, 64, (double)v->tris);
    if (p->isModel) {
        float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
        for (size_t i = 0; i < p->ntv; i++) for (int k = 0; k < 3; k++) { float q = p->tv[i * 3 + k]; if (q < lo[k]) lo[k] = q; if (q > hi[k]) hi[k] = q; }
        wchar_t tv[64], tt[64]; V3Num(tv, 64, (double)p->ntv); V3Num(tt, 64, (double)p->nti);
        wchar_t attTxt[600] = L"";
        if (v->nAtt) {
            int ties = 0; for (int a = 0; a < v->nAtt; a++) if (strstr(v->att[a].name, "Tie")) ties++;
            swprintf(attTxt, 600, L"Attached: %ls%ls\r\n", v->nAtt > ties ? L"head (on NeckPoint)" : L"",
                     ties ? (v->nAtt > ties ? L", ties (on TiePoint; Tie00 shown, tick others below)" : L"ties (on TiePoint)") : L"");
        } else if (!v->anims.count && v->hasSkeleton) swprintf(attTxt, 600, L"No animation file found for it: pick one from the list\r\n");
        if (v->nCloth) { size_t l = wcslen(attTxt); swprintf(attTxt + l, 600 - l, L"Cloth physics: %hs\r\n", v->clothInfo); }
        swprintf(t, 1600, L"Model (BFRES): %hs%ls\r\nSize: %.2f \x00D7 %.2f \x00D7 %.2f   Bones: %d\r\n"
                          L"Parts (shapes): %d   Materials: %d\r\nVertices: %ls   Triangles: %ls\r\n"
                          L"Textures (colour maps) found: %d of %d%ls\r\n%ls%ls",
                 p->modelName, p->nmodels > 1 ? L" (several models)" : L"", p->ntv ? hi[0] - lo[0] : 0, p->ntv ? hi[1] - lo[1] : 0, p->ntv ? hi[2] - lo[2] : 0,
                 p->nbones, p->sections, p->nmaterials, tv, tt, v->texFound, v->texWanted,
                 v->texFound < v->texWanted ? L" (looked in romfs\\Tex; parts without one use a plain colour)" : L"",
                 attTxt, v->status);
        V3SetInfo(v, t); return;
    }
    if (p->isTriMesh) {
        float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
        for (size_t i = 0; i < p->ntv; i++) for (int k = 0; k < 3; k++) { float q = p->tv[i * 3 + k]; if (q < lo[k]) lo[k] = q; if (q > hi[k]) hi[k] = q; }
        wchar_t tv[64], tt[64]; V3Num(tv, 64, (double)p->ntv); V3Num(tt, 64, (double)p->nti);
        if (!strncmp(p->shapeType, "terrain", 7)) {
            swprintf(t, 1600, L"Terrain height map: %hs\r\nSize: %.1f \x00D7 %.1f \x00D7 %.1f\r\nVertices: %ls   Triangles: %ls\r\n%ls: %d\r\nTriangles shown: %ls\r\n%ls",
                     p->shapeType, p->ntv ? hi[0] - lo[0] : 0, p->ntv ? hi[1] - lo[1] : 0, p->ntv ? hi[2] - lo[2] : 0, tv, tt,
                     p->gcol && p->overlay ? L"Ground coloured by the chosen layer; ground materials" : p->gcol ? L"Game colours (color_mul2); ground materials" : !strncmp(p->matName[0], "height band", 11) ? L"Height bands (no .mate file beside it)" : L"Ground materials (from the .mate file)", p->nmat,
                     v->haveMesh ? c : L"-", v->status);
            if (v->nprb && v->prbSel > 0) {
                size_t l = wcslen(t); wchar_t pn[64]; V3Num(pn, 64, (double)v->nprb);
                if (v->prbSel <= 8) swprintf(t + l, 1600 - l, L"\r\nLight probes (%hs%hs): %ls dots, values %d-%d as red, green, blue (square root; full at %.3g)", v->prbFile, v->nprbFine ? " + finer .dlpg grids" : "", pn, (v->prbSel - 1) * 3 + 1, (v->prbSel - 1) * 3 + 3, v->prbHi[0]);
                else swprintf(t + l, 1600 - l, L"\r\nLight probes (%hs%hs): %ls dots, value %d: blue %.3g ... red %.3g", v->prbFile, v->nprbFine ? " + finer .dlpg grids" : "", pn, v->prbSel - 8, v->prbLo[0], v->prbHi[0]);
            }
            if (v->nsnd && v->sndSel > 0) {
                size_t l = wcslen(t); wchar_t pn[64]; V3Num(pn, 64, (double)v->nsnd);
                static const char *how[4] = { "files A_B = x_z, cells row by row", "files A_B = x_z, cells column by column", "files A_B = z_x, cells row by row", "files A_B = z_x, cells column by column" };
                swprintf(t + l, 1600 - l, L"\r\nSound probes: %ls, %hs: blue %.3g ... red %.3g (placed: %hs)", pn, V3SoundName(v->sndSel), v->sndLo, v->sndHi, v->sndOrient >= 0 && v->sndOrient < 4 ? how[v->sndOrient] : "?");
            }
            V3SetInfo(v, t); return;
        }
        swprintf(t, 1600, L"Collision mesh (Phive / Havok %hs)\r\nSize: %.1f \x00D7 %.1f \x00D7 %.1f   %ls: %d\r\n"
                          L"Vertices: %ls   Triangles: %ls\r\nCollision materials: %d\r\nTriangles shown: %ls\r\n%ls",
                 p->shapeType, p->ntv ? hi[0] - lo[0] : 0, p->ntv ? hi[1] - lo[1] : 0, p->ntv ? hi[2] - lo[2] : 0, strstr(p->shapeType, "Compound") ? L"Shapes placed" : L"Sections", p->sections,
                 tv, tt, p->nmat, v->haveMesh ? c : L"-", v->status);
        V3SetInfo(v, t); return;
    }
    if (p->isObject) {
        swprintf(t, 1600, L"Voxel object (VGEOB signed distance)\r\nGrid: %d \x00D7 %d \x00D7 %d cells\r\nCell size: %g   Layers: %d\r\n"
                          L"Cells inside: %ls\r\nCells near the surface: %ls\r\nTriangles shown: %ls\r\n%ls",
                 p->nx, p->ny, p->nz, p->cell, p->nmat, a, b, v->haveMesh ? c : L"-", v->status);
        V3SetInfo(v, t); return;
    }
    swprintf(t, 1600, L"Size: %d \x00D7 %d \x00D7 %d voxels\r\nVoxel size: %g   Blocks: %d   (%d bytes per voxel)\r\n"
                      L"Solid voxels: %ls\r\nSurface voxels: %ls\r\nTriangles shown: %ls\r\n%ls",
             p->nx, p->ny, p->nz, p->scale[0], p->nblocks, p->bytesPerVoxel, a, b, v->haveMesh ? c : L"-", v->status);
    V3SetInfo(v, t);
}

/* ------------------------------------------------------------------ GL */
static void V3InitGL(V3 *v) {
    v->dc = GetDC(v->gl);
    PIXELFORMATDESCRIPTOR pfd; ZeroMemory(&pfd, sizeof(pfd));
    pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 24; pfd.cDepthBits = 24; pfd.iLayerType = PFD_MAIN_PLANE;
    int pf = ChoosePixelFormat(v->dc, &pfd);
    if (!pf || !SetPixelFormat(v->dc, pf, &pfd)) return;
    v->rc = wglCreateContext(v->dc);
    if (!v->rc) return;
    wglMakeCurrent(v->dc, v->rc);
    v->glGenBuffers_ = (PFNGenBuffers)(void *)wglGetProcAddress("glGenBuffers");
    v->glBindBuffer_ = (PFNBindBuffer)(void *)wglGetProcAddress("glBindBuffer");
    v->glBufferData_ = (PFNBufferData)(void *)wglGetProcAddress("glBufferData");
    v->glDeleteBuffers_ = (PFNDeleteBuffers)(void *)wglGetProcAddress("glDeleteBuffers");
    v->useVbo = v->glGenBuffers_ && v->glBindBuffer_ && v->glBufferData_ && v->glDeleteBuffers_;
}

static void V3FreeGLMesh(V3 *v) {
    if (v->rc) wglMakeCurrent(v->dc, v->rc);
    if (v->useVbo && v->vbo[0]) { v->glDeleteBuffers_(2, v->vbo); v->vbo[0] = v->vbo[1] = 0; }
    free(v->cpuVert); free(v->cpuIdx); v->cpuVert = NULL; v->cpuIdx = NULL;
    v->haveMesh = 0;
}

/* Top view (T): straight down, world x to the right and world z down the screen. Terrain maps: the world's
   square fills the height of the view in the top-left corner (fitted at the lowest ground level), the same
   view for every ground layer so they can be compared. Anything else: the whole thing centred. */
static void V3TopView(V3 *v) {
    RECT rc; GetClientRect(v->gl, &rc);
    float a = rc.bottom > 1 ? (float)rc.right / rc.bottom : 1.5f, k = 0.45f;   // k: the projection's tan(half height)
    float bmin[3], bmax[3]; memcpy(bmin, v->bmin, sizeof bmin); memcpy(bmax, v->bmax, sizeof bmax);
    int terr = V3IsTerrainGrid(v);
    VoxPart *p = &v->part;
    if (terr && p->terrWorld[2] > 0 && p->gridW > 1 && p->tv) {
        // the map's (or area's) own square, not the box round everything drawn: that also holds the big invisible
        // areas and far objects, which pushed the square to the right (as in V3ResetView)
        float ws = p->terrWorld[2], x0 = 0, z0 = 0, side = p->terrWorld[3] * ws;
        if (p->terrArea[2] > 0) { x0 = (p->terrArea[0] - p->terrWorld[0]) * ws; z0 = (p->terrArea[1] - p->terrWorld[1]) * ws; side = p->terrArea[2] * ws; }
        size_t ng = (size_t)p->gridW * p->gridW; if (ng > p->ntv) ng = p->ntv;
        float lo = 1e30f; for (size_t i = 0; i < ng; i++) if (p->tv[i * 3 + 1] < lo) lo = p->tv[i * 3 + 1];
        if (lo > 1e29f) lo = v->bmin[1] + v->drawCentre[1];
        bmin[0] = x0 - v->drawCentre[0]; bmax[0] = bmin[0] + side;
        bmin[2] = z0 - v->drawCentre[2]; bmax[2] = bmin[2] + side;
        bmin[1] = lo - v->drawCentre[1];
    }
    float sx = bmax[0] - bmin[0], sz = bmax[2] - bmin[2];
    if (sx <= 0 && sz <= 0) return;
    float fit = terr ? 0.98f : 0.9f;
    float d = sz / (2 * k * fit), dw = sx / (2 * k * a * fit); if (dw > d) d = dw;
    if (d < 1e-3f) d = 1;
    v->yaw = 0; v->pitch = 90.f;
    if (terr) {
        v->panx = -fit * k * d * a - bmin[0];         // left edge of the square at the left of the view
        v->pany = fit * k * d + bmin[2];              // top edge at the top
        v->dist = d + bmin[1];
    } else {
        v->panx = -(bmin[0] + bmax[0]) / 2;
        v->pany = (bmin[2] + bmax[2]) / 2;
        v->dist = d + bmax[1];                        // fitted at the top of the object (nearest the camera)
    }
}
static void V3ResetView(V3 *v) {
    v->yaw = 35.f; v->pitch = 30.f; v->panx = v->pany = 0;
    v->dist = v->radius * 2.4f; if (v->dist < 1) v->dist = 100;
    // terrain maps: aim at the middle of the map's (or area's) square and fit it - the box round everything drawn
    // also holds the big invisible areas and far objects, whose middle is off to one side
    VoxPart *p = &v->part;
    if (V3IsTerrainGrid(v) && p->terrWorld[2] > 0 && p->gridW > 1 && p->tv) {
        float ws = p->terrWorld[2], x0 = 0, z0 = 0, side = p->terrWorld[3] * ws;
        if (p->terrArea[2] > 0) { x0 = (p->terrArea[0] - p->terrWorld[0]) * ws; z0 = (p->terrArea[1] - p->terrWorld[1]) * ws; side = p->terrArea[2] * ws; }
        size_t ng = (size_t)p->gridW * p->gridW; if (ng > p->ntv) ng = p->ntv;
        double hy = 0; for (size_t i = 0; i < ng; i++) hy += p->tv[i * 3 + 1];
        float P[3] = { x0 + side / 2 - v->drawCentre[0], (float)(ng ? hy / ng : 0) - v->drawCentre[1], z0 + side / 2 - v->drawCentre[2] };
        // where that point is after the view's turn (yaw about y, then pitch about x - as drawn)
        float cy = cosf(v->yaw * 3.14159265f / 180), sy = sinf(v->yaw * 3.14159265f / 180), cp = cosf(v->pitch * 3.14159265f / 180), sp = sinf(v->pitch * 3.14159265f / 180);
        float x1 = P[0] * cy + P[2] * sy, z1 = -P[0] * sy + P[2] * cy, y2 = P[1] * cp - z1 * sp, z2 = P[1] * sp + z1 * cp;
        v->panx = -x1; v->pany = -y2;
        v->dist = side * 0.7071f * 2.4f + z2;
    }
}

static void V3Upload(V3 *v, VoxMesh *m) {
    V3FreeGLMesh(v);
    if (!v->rc) return;
    wglMakeCurrent(v->dc, v->rc);
    // interleave: pos 12 bytes, normal 4, colour 4 (+ texture coordinates 8)
    int st = m->uv ? 28 : 20; v->stride = st;
    float *iv = (float *)malloc(m->nv * st + st);
    if (!iv) return;
    for (size_t i = 0; i < m->nv; i++) {
        unsigned char *o = (unsigned char *)iv + i * st;
        memcpy(o, m->pos + i * 3, 12); memcpy(o + 12, m->nrm + i * 4, 4); memcpy(o + 16, m->col + i * 4, 4);
        if (v->patOn && m->src && m->src[i] < v->part.ntv && v->patOn[m->src[i]]) memcpy(o + 16, v->patRGB + (size_t)m->src[i] * 4, 4);   // (byte 3 was the part number, only needed above)
        if (m->uv) memcpy(o + 20, m->uv + i * 2, 8);
    }
    free(v->animBuf); v->animBuf = NULL;
    if (m->src) { v->animBuf = (unsigned char *)malloc(m->nv * st + st); if (v->animBuf) memcpy(v->animBuf, iv, m->nv * st); }
    // runs of triangles by part (texture per part)
    free(v->runs); v->runs = NULL; v->nruns = 0;
    if (m->uv) {
        int cap = 0;
        for (size_t t = 0; t < m->ni / 3; t++) {
            int mat = m->tkeyOut ? (int)m->tkeyOut[t] : m->tmatOut ? m->tmatOut[t] : m->col[m->idx[t * 3] * 4 + 3];
            if (v->nruns && v->runs[v->nruns - 1].mat == mat) { v->runs[v->nruns - 1].count += 3; continue; }
            if (v->nruns == cap) { cap = cap ? cap * 2 : 64; void *nr = realloc(v->runs, cap * sizeof(*v->runs)); if (!nr) break; v->runs = nr; }
            v->runs[v->nruns].mat = mat; v->runs[v->nruns].first = (GLsizei)(t * 3); v->runs[v->nruns].count = 3; v->nruns++;
        }
    }
    v->nidx = (GLsizei)m->ni;
    if (v->useVbo) {
        v->glGenBuffers_(2, v->vbo);
        v->glBindBuffer_(GL_ARRAY_BUFFER_, v->vbo[0]);
        v->glBufferData_(GL_ARRAY_BUFFER_, (GLsizeiptr_)(m->nv * st), iv, GL_STATIC_DRAW_);
        v->glBindBuffer_(GL_ELEMENT_ARRAY_BUFFER_, v->vbo[1]);
        v->glBufferData_(GL_ELEMENT_ARRAY_BUFFER_, (GLsizeiptr_)(m->ni * 4), m->idx, GL_STATIC_DRAW_);
        v->glBindBuffer_(GL_ARRAY_BUFFER_, 0); v->glBindBuffer_(GL_ELEMENT_ARRAY_BUFFER_, 0);
        free(iv);
        if (glGetError() != GL_NO_ERROR) {                    // e.g. out of video memory: fall back
            v->glDeleteBuffers_(2, v->vbo); v->vbo[0] = v->vbo[1] = 0; v->useVbo = 0;
            V3Upload(v, m); return;
        }
    } else {
        v->cpuVert = iv;
        v->cpuIdx = m->idx; m->idx = NULL;                     // keep the index array
    }
    free(v->meshSrc); v->meshSrc = NULL; v->meshNv = 0;
    if (m->src) {                                         // keep what's needed to re-pose the mesh per frame
        v->meshSrc = m->src; m->src = NULL; v->meshNv = m->nv; memcpy(v->meshCentre, m->centre, 12);
    }
    int first = v->radius == 0;
    memcpy(v->drawCentre, m->centre, 12);
    for (int k = 0; k < 3; k++) { v->bmin[k] = m->bmin[k]; v->bmax[k] = m->bmax[k]; }
    float r = 0; for (int k = 0; k < 3; k++) { float e = m->bmax[k] - m->bmin[k]; r += e * e; }
    v->radius = sqrtf(r) / 2;
    if (first || v->viewPending) { v->viewPending = 0; V3ResetView(v); }
    v->tris = m->ni / 3; v->verts = m->nv;
    v->haveMesh = 1;
}

// poses the model at the current animation frame and re-sends the vertices (positions and normals change)
static void V3ApplyPose(V3 *v) {
    VoxPart *p = &v->part;
    if (!v->haveMesh || !v->meshSrc || !v->animBuf || !p->skN || !v->rc) return;
    size_t nv = p->ntv;
    if (!v->skinM) v->skinM = (float *)malloc((size_t)p->skN * 48);
    if (!v->posedP) v->posedP = (float *)malloc(nv * 12 + 12);
    if (!v->posedN) v->posedN = (float *)malloc(nv * 12 + 12);
    if (!v->skinM || !v->posedP || !v->posedN) return;
    {
        const BfSkelAnim *as[17]; int na = 0;
        if (v->animSel > 0) as[na++] = &v->anim;
        for (int a = 0; a < v->nAtt; a++) if (v->att[a].anim.nb) as[na++] = &v->att[a].anim;
        if (!v->worldM) v->worldM = (float *)malloc((size_t)p->skN * 48);
        BfPoseMulti(p, as, na, v->animFrame, v->skinM, v->worldM);
    }
    // helper bones (Sub_...): moved from the other bones' rotation, as the game does
    if (v->hb.ok && v->hbCb && v->animSel > 0 && SendMessageW(v->hbCb, BM_GETCHECK, 0, 0) == BST_CHECKED) HbApply(&v->hb, p, v->skinM, v->worldM);
    // cloth simulation while an animation runs: step by the frames moved (a jump or going backwards starts it again)
    if (v->nsim && v->simCb && v->animSel > 0 && SendMessageW(v->simCb, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        float df = v->animFrame - v->simLast;
        if (df < 0 && v->playing && v->anim.loop && v->anim.frames > 1 && -df > (v->anim.frames - 1) / 2.0f) df += (float)(v->anim.frames - 1);   // looped round: carry on
        if (v->simReset || df < 0 || df > 8) { for (int c = 0; c < v->nsim; c++) ClothSimReset(&v->sim[c], v->skinM); v->simReset = 0; df = 0; }
        if (df > 0) for (int c = 0; c < v->nsim; c++) ClothSimStep(&v->sim[c], v->skinM, df / 60.0f, 9.8f);
        for (int c = 0; c < v->nsim; c++) ClothSimApply(&v->sim[c], p, v->skinM, v->worldM);
        v->simLast = v->animFrame;
    } else v->simReset = 1;
    // cutscene clips move the whole character far from where it stands: optionally take the Root bone's movement out
    float keep[3] = { 0, 0, 0 };
    if (v->worldM && v->keepCb && SendMessageW(v->keepCb, BM_GETCHECK, 0, 0) == BST_CHECKED && v->animSel > 0) {
        int r = BfBoneIndex(p, "Root"); if (r < 0) r = 0;
        const float *Wp = v->worldM + (size_t)r * 12, *Wb = p->skBindW + (size_t)r * 12;
        keep[0] = Wp[3] - Wb[3]; keep[1] = Wp[7] - Wb[7]; keep[2] = Wp[11] - Wb[11];
        if (!isfinite(keep[0]) || !isfinite(keep[1]) || !isfinite(keep[2])) keep[0] = keep[1] = keep[2] = 0;
    }
    memcpy(v->keepOff, keep, 12);
    // eyelids (visibility clips) and eyes (texture moves) at this frame
    memset(v->uvCtl, 0, sizeof v->uvCtl); memset(v->uvCtl1, 0, sizeof v->uvCtl1);
    if (v->animSel > 0) {
        const BfVisAnim *va[17]; int nva = 0; if (v->visAnim.n) va[nva++] = &v->visAnim;
        const BfMatAnim *mv[17]; int nma = 0; if (v->matAnim.n) mv[nma++] = &v->matAnim;
        for (int a = 0; a < v->nAtt; a++) { if (v->att[a].vis.n) va[nva++] = &v->att[a].vis; if (v->att[a].mat.n) mv[nma++] = &v->att[a].mat; }
        for (int i = 0; i < nva; i++) {
            float f = v->animFrame, last = va[i]->frames > 1 ? (float)(va[i]->frames - 1) : 0; if (f > last) f = last > 0 ? fmodf(f, last) : 0;
            for (int t = 0; t < va[i]->n; t++) { int on = BfVisAt(&va[i]->t[t], f);
                for (int m = 0; m < p->nmat; m++) if (p->partBone[m] == va[i]->t[t].bone) v->visOn[m] = (unsigned char)on; }
        }
        for (int i = 0; i < nma; i++) {
            float f = v->animFrame, last = mv[i]->frames > 1 ? (float)(mv[i]->frames - 1) : 0; if (f > last) f = last > 0 ? fmodf(f, last) : 0;
            for (int t = 0; t < mv[i]->n; t++) { int m = mv[i]->t[t].part; if (m < 0 || m >= 256) continue;
                if (mv[i]->t[t].which == 1) { BfUvAt(mv[i], &mv[i]->t[t], f, v->uvM1[m]); v->uvCtl1[m] = 1; }
                else { BfUvAt(mv[i], &mv[i]->t[t], f, v->uvM[m]); v->uvCtl[m] = 1; } }
        }
    }
    // face: key shapes blended by the shape clips (the model's own and the attached head's)
    const float *basePos = NULL;
    {   const BfShapeAnim *sa[17]; int ns = 0;
        if (v->animSel > 0 && v->shAnim.nk) sa[ns++] = &v->shAnim;
        if (v->animSel > 0) for (int a = 0; a < v->nAtt; a++) if (v->att[a].sh.nk) sa[ns++] = &v->att[a].sh;
        if (ns) {
            if (!v->morphP) v->morphP = (float *)malloc(nv * 12 + 12);
            if (v->morphP) { BfShapeApply(p, sa, ns, v->animFrame, v->morphP); basePos = v->morphP; }
        }
    }
    unsigned char *seen = (unsigned char *)calloc(nv + 1, 1); if (!seen) return;
    int st = v->stride ? v->stride : 20;
    for (size_t i = 0; i < v->meshNv; i++) {
        uint32_t sv = v->meshSrc[i]; if (sv >= nv) continue;
        if (!seen[sv]) { BfSkinVertexAt(p, v->skinM, sv, basePos ? basePos + (size_t)sv * 3 : p->tv + (size_t)sv * 3, v->posedP + (size_t)sv * 3, v->posedN + (size_t)sv * 3); seen[sv] = 1; }
        unsigned char *o = v->animBuf + i * st;
        float q[3] = { v->posedP[sv * 3] - keep[0] - v->meshCentre[0], v->posedP[sv * 3 + 1] - keep[1] - v->meshCentre[1], v->posedP[sv * 3 + 2] - keep[2] - v->meshCentre[2] };
        memcpy(o, q, 12);
        signed char nn[4] = { (signed char)(v->posedN[sv * 3] * 127), (signed char)(v->posedN[sv * 3 + 1] * 127), (signed char)(v->posedN[sv * 3 + 2] * 127), 0 };
        memcpy(o + 12, nn, 4);
    }
    free(seen);
    wglMakeCurrent(v->dc, v->rc);
    if (v->useVbo && v->vbo[0]) {
        v->glBindBuffer_(GL_ARRAY_BUFFER_, v->vbo[0]);
        v->glBufferData_(GL_ARRAY_BUFFER_, (GLsizeiptr_)(v->meshNv * st), v->animBuf, GL_STATIC_DRAW_);
        v->glBindBuffer_(GL_ARRAY_BUFFER_, 0);
    } else if (v->cpuVert) memcpy(v->cpuVert, v->animBuf, v->meshNv * st);
}
static void V3UpdateFrameLabel(V3 *v) {
    if (!v->play) return;
    SetWindowTextW(v->play, v->playing ? L"Pause" : L"Play");
    if (v->animSel > 0) SendMessageW(v->frame, TBM_SETPOS, TRUE, (LPARAM)(int)v->animFrame);
}
static void V3SelectAnim(V3 *v, int sel) {
    BfSkelAnimFree(&v->anim); BfShapeAnimFree(&v->shAnim); BfVisAnimFree(&v->visAnim); BfMatAnimFree(&v->matAnim);
    v->animSel = sel; v->animFrame = 0; v->playing = 0;
    for (int a = 0; a < v->nAtt; a++) { BfSkelAnimFree(&v->att[a].anim); BfShapeAnimFree(&v->att[a].sh); BfVisAnimFree(&v->att[a].vis); BfMatAnimFree(&v->att[a].mat); }
    unsigned char oldCtl[VOX_MAXMAT]; memcpy(oldCtl, v->visCtl, sizeof oldCtl);
    if (sel > 0 && BfSkelAnimLoadRange(&v->anims, sel - 1, &v->part, &v->anim, 0, v->bodyN ? v->bodyN : v->part.skN)) {
        // the model's own face (key shape) clip of this name, if the file has one
        {   int pHi = v->nAtt ? v->att[0].partLo : v->part.nmat, bHi = v->bodyN ? v->bodyN : v->part.skN;
            BfShapeAnimLoad(&v->anims, sel - 1, &v->part, 0, pHi, &v->shAnim);
            BfVisAnimLoad(&v->anims, sel - 1, &v->part, 0, bHi, &v->visAnim);        // eyelids etc. shown / hidden
            BfMatAnimLoad(&v->anims, sel - 1, &v->part, 0, pHi, &v->matAnim); }    // eyes looking around (texture moves)
        // the head and tie play the clip of the same name from their own animation files: bones from the first
        // file that has them, the face from the first that has a face clip (DonkeyKongHead.anim)
        for (int a = 0; a < v->nAtt; a++) {
            int gotSk = 0, gotSh = 0, gotVi = 0, gotMa = 0;
            for (int k = 0; k < v->att[a].nfi; k++) {
                BfAnimFile *f = &v->attFile[v->att[a].fi[k]]; int idx = BfSkelAnimFind(f, v->anim.name); if (idx < 0) continue;
                if (!gotSk && f->sk[idx] >= 0 && BfSkelAnimLoadRange(f, idx, &v->part, &v->att[a].anim, v->att[a].lo, v->att[a].hi)) gotSk = 1;
                if (!gotSh && f->sh[idx] >= 0 && BfShapeAnimLoad(f, idx, &v->part, v->att[a].partLo, v->att[a].partHi, &v->att[a].sh)) gotSh = 1;
                if (!gotVi && f->vi[idx] >= 0 && BfVisAnimLoad(f, idx, &v->part, v->att[a].lo, v->att[a].hi, &v->att[a].vis)) gotVi = 1;
                if (!gotMa && f->ma[idx] >= 0 && BfMatAnimLoad(f, idx, &v->part, v->att[a].partLo, v->att[a].partHi, &v->att[a].mat)) gotMa = 1;
            }
        }
        SendMessageW(v->frame, TBM_SETRANGE, TRUE, MAKELPARAM(0, v->anim.frames > 0 ? v->anim.frames - 1 : 0));
        EnableWindow(v->frame, TRUE); EnableWindow(v->play, TRUE);
        v->playing = 1; v->animT0 = GetTickCount(); v->animF0 = 0;
        SetTimer(v->wnd, V3_TIMER_ANIM, 16, NULL);
    } else {
        v->animSel = 0; EnableWindow(v->frame, FALSE); EnableWindow(v->play, FALSE); KillTimer(v->wnd, V3_TIMER_ANIM);
    }
    // parts on bones a visibility clip controls are put in the mesh and shown / hidden while drawing
    memset(v->visCtl, 0, sizeof v->visCtl);
    if (v->animSel > 0) {
        const BfVisAnim *va[17]; int nva = 0; if (v->visAnim.n) va[nva++] = &v->visAnim;
        for (int a = 0; a < v->nAtt; a++) if (v->att[a].vis.n) va[nva++] = &v->att[a].vis;
        for (int m = 0; m < v->part.nmat; m++) { int bo = v->part.partBone[m]; if (bo < 0) continue;
            for (int i = 0; i < nva; i++) for (int t = 0; t < va[i]->n; t++) if (va[i]->t[t].bone == bo) v->visCtl[m] = 1; }
    }
    if (memcmp(oldCtl, v->visCtl, sizeof oldCtl)) V3RequestBuild(v);
    V3ApplyPose(v); V3UpdateFrameLabel(v);
    InvalidateRect(v->gl, NULL, FALSE);
}

// the animation-file list: files found for this model, then "Other animation file..."
static void V3FillAnimFiles(V3 *v) {
    SendMessageW(v->animFileCb, CB_RESETCONTENT, 0, 0);
    wchar_t t[400];
    for (int i = 0; i < v->nAnimFiles; i++) {
        const wchar_t *fn = wcsrchr(v->animFiles[i], L'\\'); fn = fn ? fn + 1 : v->animFiles[i];
        swprintf(t, 400, L"%ls  (%d animations)", fn, v->animFileCount[i]); SendMessageW(v->animFileCb, CB_ADDSTRING, 0, (LPARAM)t);
    }
    if (!v->nAnimFiles) SendMessageW(v->animFileCb, CB_ADDSTRING, 0, (LPARAM)L"(no animation file found for this model)");
    SendMessageW(v->animFileCb, CB_ADDSTRING, 0, (LPARAM)L"Other animation file...");
    SendMessageW(v->animFileCb, CB_SETCURSEL, v->nAnimFiles ? v->animFileSel : 0, 0);
}
static void V3FillAnims(V3 *v) {
    SendMessageW(v->animCb, CB_RESETCONTENT, 0, 0);
    wchar_t t[160];
    if (v->anims.count) swprintf(t, 160, L"Bind pose (no animation) - %d animations", v->anims.count);
    else swprintf(t, 160, L"Bind pose (no animations loaded)");
    SendMessageW(v->animCb, CB_ADDSTRING, 0, (LPARAM)t);
    for (int i = 0; i < v->anims.count; i++) { swprintf(t, 160, L"%hs  (%d frames)", v->anims.names[i], v->anims.frames[i]); SendMessageW(v->animCb, CB_ADDSTRING, 0, (LPARAM)t); }
    SendMessageW(v->animCb, CB_SETCURSEL, 0, 0);
    EnableWindow(v->animCb, v->anims.count > 0);
    EnableWindow(v->play, FALSE); EnableWindow(v->frame, FALSE);
}
static void V3SelectAnim(V3 *v, int sel);
static void V3UpdateInfo(V3 *v);
// switch to animation file i (or, past the end of the list, ask for one)
static void V3PickAnimFile(V3 *v, int i) {
    if (!v->nAnimFiles && i == 0) return;                      // the "(none found)" line
    if (i >= v->nAnimFiles) {
        wchar_t path[1024] = L"", dir[1024]; swprintf(dir, 1024, L"%ls", v->dir);
        wchar_t *slash = wcsrchr(dir, L'\\'); if (slash) *slash = 0;
        OPENFILENAMEW of; ZeroMemory(&of, sizeof of); of.lStructSize = sizeof of; of.hwndOwner = v->wnd;
        of.lpstrFilter = L"Animation files (*.anim.bfres.zs)\0*.anim.bfres.zs;*.anim.bfres\0All files\0*.*\0";
        of.lpstrFile = path; of.nMaxFile = 1024; of.lpstrInitialDir = dir; of.lpstrTitle = L"Pick an animation file for this model";
        of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&of)) { V3FillAnimFiles(v); return; }
        for (i = 0; i < v->nAnimFiles; i++) if (!_wcsicmp(v->animFiles[i], path)) break;
        if (i == v->nAnimFiles) {
            BfAnimFile f;
            HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
            int ok = BfAnimFileOpen(&f, path);
            SetCursor(old);
            if (!ok || !f.nplay) {
                if (ok) BfAnimFileFree(&f);
                MessageBoxW(v->wnd, ok ? L"That file has no animations this viewer can play."
                                       : L"That file couldn't be read as a BFRES animation file.", L"3D view", MB_ICONINFORMATION);
                V3FillAnimFiles(v); return;
            }
            if (v->nAnimFiles == 16) v->nAnimFiles = 15;
            i = v->nAnimFiles++;
            swprintf(v->animFiles[i], 1024, L"%ls", path); v->animFileCount[i] = f.count;
            V3SelectAnim(v, 0); BfAnimFileFree(&v->anims); v->anims = f; v->animFileSel = i;
            V3FillAnimFiles(v); V3FillAnims(v); V3UpdateInfo(v); return;
        }
    }
    if (i == v->animFileSel && v->anims.count) return;
    V3SelectAnim(v, 0); BfAnimFileFree(&v->anims);
    HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
    BfAnimFileOpen(&v->anims, v->animFiles[i]);
    SetCursor(old);
    v->animFileSel = i;
    V3FillAnimFiles(v); V3FillAnims(v); V3UpdateInfo(v);
}

static void V3Render(V3 *v) {
    if (!v->rc) return;
    wglMakeCurrent(v->dc, v->rc);
    RECT rc; GetClientRect(v->gl, &rc);
    int w = rc.right > 0 ? rc.right : 1, h = rc.bottom > 0 ? rc.bottom : 1;
    glViewport(0, 0, w, h);
    glClearColor(0.13f, 0.14f, 0.16f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (v->haveMesh) {
        float nearp = v->dist * 0.02f, farp = v->dist + v->radius * 4 + 10;
        if (nearp < 0.05f) nearp = 0.05f;
        float fy = nearp * 0.45f, fx = fy * w / h;          // ~48 degree vertical field of view
        glMatrixMode(GL_PROJECTION); glLoadIdentity(); glFrustum(-fx, fx, -fy, fy, nearp, farp);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        GLfloat lp[4] = { 0.35f, 0.7f, 0.6f, 0 }, amb[4] = { 0.38f, 0.38f, 0.40f, 1 }, dif[4] = { 0.75f, 0.75f, 0.72f, 1 };
        glLightfv(GL_LIGHT0, GL_POSITION, lp);                 // fixed to the camera
        glLightfv(GL_LIGHT0, GL_AMBIENT, amb); glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
        glTranslatef(v->panx, v->pany, -v->dist);
        glRotatef(v->pitch, 1, 0, 0);
        glRotatef(v->yaw, 0, 1, 0);
        glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING); glEnable(GL_LIGHT0);
        glEnable(GL_COLOR_MATERIAL); glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
        glEnable(GL_NORMALIZE); glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 1);
        glShadeModel(GL_SMOOTH); glDisable(GL_CULL_FACE);
        if (v->cutPos < 1000) {
            double y = v->bmin[1] + (v->bmax[1] - v->bmin[1]) * v->cutPos / 1000.0;
            GLdouble eq[4] = { 0, -1, 0, y };
            glClipPlane(GL_CLIP_PLANE0, eq); glEnable(GL_CLIP_PLANE0);
        } else glDisable(GL_CLIP_PLANE0);
        glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_NORMAL_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
        const char *base = NULL;
        if (v->useVbo) { v->glBindBuffer_(GL_ARRAY_BUFFER_, v->vbo[0]); v->glBindBuffer_(GL_ELEMENT_ARRAY_BUFFER_, v->vbo[1]); }
        else base = (const char *)v->cpuVert;
        int st = v->stride ? v->stride : 20;
        glVertexPointer(3, GL_FLOAT, st, base);
        glNormalPointer(GL_BYTE, st, base + 12);
        glColorPointer(3, GL_UNSIGNED_BYTE, st, base + 16);
        int anyCtl = 0; if (v->animSel > 0) for (int m = 0; m < VOX_MAXMAT; m++) if (v->visCtl[m]) { anyCtl = 1; break; }
        float fxFrame = 0;
        if (v->fxOn) { int len = v->fx.frames > 1 ? v->fx.frames : 1; fxFrame = fmodf((GetTickCount() - v->fxT0) * 60 / 1000.0f, (float)len); }
        int colours = v->origSaved || v->fxOn;
        if (st == 28 && v->nruns && (v->terrTex || v->nmtex)) {           // MKW terrain: colour map x ground detail texture (x2, so the average stays)
            glTexCoordPointer(2, GL_FLOAT, st, base + 20);
            glEnableClientState(GL_COLOR_ARRAY);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, 0x8570 /*GL_COMBINE*/);
            glTexEnvi(GL_TEXTURE_ENV, 0x8571 /*COMBINE_RGB*/, GL_MODULATE);
            glTexEnvi(GL_TEXTURE_ENV, 0x8580 /*SOURCE0_RGB*/, GL_TEXTURE); glTexEnvi(GL_TEXTURE_ENV, 0x8581 /*SOURCE1_RGB*/, 0x8577 /*PRIMARY_COLOR*/);
            glTexEnvf(GL_TEXTURE_ENV, 0x8573 /*RGB_SCALE*/, 2.0f);
            for (int r = 0; r < v->nruns; r++) {
                int rm = v->runs[r].mat;
                if (rm >= VOX_MAXMAT) {                         // placed scenery: its own texture, plain (lit colour x texture)
                    int sl = rm - VOX_MAXMAT, k = sl < v->nmtex ? v->mtexTex[sl] : -1;
                    GLuint id = k >= 0 ? v->tex[k].id : 0;
                    glMatrixMode(GL_TEXTURE); glLoadIdentity(); glMatrixMode(GL_MODELVIEW);
                    if (id) {
                        glEnableClientState(GL_TEXTURE_COORD_ARRAY); glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, id);
                        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                        if (v->tex[k].alpha) { glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.4f); } else glDisable(GL_ALPHA_TEST);
                    } else { glDisableClientState(GL_TEXTURE_COORD_ARRAY); glDisable(GL_TEXTURE_2D); glDisable(GL_ALPHA_TEST); }
                    const uint32_t *ip2 = v->useVbo ? NULL : v->cpuIdx;
                    int glow = id && sl < v->nmtex && strstr(v->mtexName[sl], "Lava");    // lava glows: not shaded
                    if (glow) glDisable(GL_LIGHTING);
                    glDrawElements(GL_TRIANGLES, v->runs[r].count, GL_UNSIGNED_INT, v->useVbo ? (const void *)((size_t)v->runs[r].first * 4) : (const void *)(ip2 + v->runs[r].first));
                    if (glow) glEnable(GL_LIGHTING);
                    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, 0x8570);
                    glDisable(GL_ALPHA_TEST);
                    continue;
                }
                if (rm < VOX_MAXMAT && v->waterBlend[rm]) continue;           // water: see-through, drawn after everything else
                int k = rm < VOX_MAXMAT ? v->partTex[rm] : -1;
                GLuint id = k >= 0 ? v->tex[k].id : 0;
                if (id) {
                    glEnableClientState(GL_TEXTURE_COORD_ARRAY); glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, id);
                    int gm = rm < VOX_MAXMAT ? v->part.terrMat[rm] : -1; float sc = gm >= 0 ? VoxTeraUVScale((unsigned)gm) : 1;
                    glMatrixMode(GL_TEXTURE); glLoadIdentity(); glScalef(sc, sc, 1); glMatrixMode(GL_MODELVIEW);
                } else { glDisableClientState(GL_TEXTURE_COORD_ARRAY); glDisable(GL_TEXTURE_2D); }
                const uint32_t *ip = v->useVbo ? NULL : v->cpuIdx;
                glDrawElements(GL_TRIANGLES, v->runs[r].count, GL_UNSIGNED_INT, v->useVbo ? (const void *)((size_t)v->runs[r].first * 4) : (const void *)(ip + v->runs[r].first));
            }
            glMatrixMode(GL_TEXTURE); glLoadIdentity(); glMatrixMode(GL_MODELVIEW);
            // water last: colour x ripple texture (x2), blended by the texture's alpha, not hiding what's under it
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
            for (int r = 0; r < v->nruns; r++) {
                int rm = v->runs[r].mat; if (rm >= VOX_MAXMAT || !v->waterBlend[rm]) continue;
                if (v->waterBlend[rm] == 2) {                  // invisible areas: their list colour, mostly see-through
                    unsigned char c[3]; VoxMatColour(&v->part, rm, c);
                    glDisableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_TEXTURE_COORD_ARRAY); glDisable(GL_TEXTURE_2D);
                    const uint32_t *ip = v->useVbo ? NULL : v->cpuIdx;
                    const void *at = v->useVbo ? (const void *)((size_t)v->runs[r].first * 4) : (const void *)(ip + v->runs[r].first);
                    glDisable(GL_LIGHTING);
                    glColor4ub(c[0], c[1], c[2], 95); glDrawElements(GL_TRIANGLES, v->runs[r].count, GL_UNSIGNED_INT, at);       // see-through sides
                    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); glLineWidth(1.5f);                                                     // and their edges, stronger
                    glColor4ub(c[0], c[1], c[2], 200); glDrawElements(GL_TRIANGLES, v->runs[r].count, GL_UNSIGNED_INT, at);
                    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glLineWidth(1); glEnable(GL_LIGHTING);
                    glEnableClientState(GL_COLOR_ARRAY);
                    continue;
                }
                int k = v->partTex[rm]; GLuint id = k >= 0 ? v->tex[k].id : 0; if (!id) continue;
                glEnableClientState(GL_TEXTURE_COORD_ARRAY); glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, id);
                const uint32_t *ip = v->useVbo ? NULL : v->cpuIdx;
                glDrawElements(GL_TRIANGLES, v->runs[r].count, GL_UNSIGNED_INT, v->useVbo ? (const void *)((size_t)v->runs[r].first * 4) : (const void *)(ip + v->runs[r].first));
            }
            glDisable(GL_BLEND); glDepthMask(GL_TRUE);
            glTexEnvf(GL_TEXTURE_ENV, 0x8573, 1.0f); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            glDisable(GL_TEXTURE_2D); glDisableClientState(GL_TEXTURE_COORD_ARRAY); glBindTexture(GL_TEXTURE_2D, 0);
        } else
        if (st == 28 && v->nruns && v->showTex && v->texFound) {
            glTexCoordPointer(2, GL_FLOAT, st, base + 20);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            glAlphaFunc(GL_GREATER, 0.4f);
            for (int r = 0; r < v->nruns; r++) {
                int rm = v->runs[r].mat;
                if (rm < VOX_MAXMAT && v->animSel > 0 && v->visCtl[rm] && !v->visOn[rm]) continue;   // hidden by a visibility clip
                int k = rm < VOX_MAXMAT ? v->partTex[rm] : -1;
                GLuint id = k >= 0 ? v->tex[k].id : 0;
                float tint[3] = { 1, 1, 1 }, em[4] = { 0, 0, 0, 1 };
                if (colours) V3PartColour(v, rm, fxFrame, tint, em);
                glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, em);
                if (id) {
                    glDisableClientState(GL_COLOR_ARRAY); glColor3f(tint[0], tint[1], tint[2]);
                    glEnableClientState(GL_TEXTURE_COORD_ARRAY); glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, id);
                    if (v->tex[k].alpha) glEnable(GL_ALPHA_TEST); else glDisable(GL_ALPHA_TEST);
                } else {
                    glEnableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_TEXTURE_COORD_ARRAY);
                    glDisable(GL_TEXTURE_2D); glDisable(GL_ALPHA_TEST);
                }
                int uv = id && rm < VOX_MAXMAT && v->animSel > 0 && v->uvCtl[rm];
                if (uv) {                                   // the eye's texture moved by a material clip
                    const float *M = v->uvM[rm];
                    GLfloat tm[16] = { M[0], M[3], 0, 0,  M[1], M[4], 0, 0,  0, 0, 1, 0,  M[2], M[5], 0, 1 };
                    glMatrixMode(GL_TEXTURE); glLoadMatrixf(tm); glMatrixMode(GL_MODELVIEW);
                }
                const uint32_t *ip = v->useVbo ? NULL : v->cpuIdx;
                glDrawElements(GL_TRIANGLES, v->runs[r].count, GL_UNSIGNED_INT, v->useVbo ? (const void *)((size_t)v->runs[r].first * 4) : (const void *)(ip + v->runs[r].first));
                if (uv) { glMatrixMode(GL_TEXTURE); glLoadIdentity(); glMatrixMode(GL_MODELVIEW); }
            }
            { GLfloat none[4] = { 0, 0, 0, 1 }; glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, none); }
            // gradient furs: the pattern colour blended over the fur, as strong as the mask says (vertex alpha),
            // through a grey copy of the fur texture
            if (v->patOn) {
                glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE);
                glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.02f);
                glColorPointer(4, GL_UNSIGNED_BYTE, st, base + 16); glEnableClientState(GL_COLOR_ARRAY);
                glEnableClientState(GL_TEXTURE_COORD_ARRAY); glEnable(GL_TEXTURE_2D);
                for (int r = 0; r < v->nruns; r++) {
                    int rm = v->runs[r].mat; if (rm >= VOX_MAXMAT || !v->partPat[rm] || v->patTex[rm] < 0) continue;
                    if (v->animSel > 0 && v->visCtl[rm] && !v->visOn[rm]) continue;
                    GLuint id = v->tex[v->patTex[rm]].id; if (!id) continue;
                    glBindTexture(GL_TEXTURE_2D, id);
                    const uint32_t *ip = v->useVbo ? NULL : v->cpuIdx;
                    glDrawElements(GL_TRIANGLES, v->runs[r].count, GL_UNSIGNED_INT, v->useVbo ? (const void *)((size_t)v->runs[r].first * 4) : (const void *)(ip + v->runs[r].first));
                }
                glColorPointer(3, GL_UNSIGNED_BYTE, st, base + 16);
                glDisable(GL_BLEND); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glAlphaFunc(GL_GREATER, 0.4f);
            }
            // eye shine: the EmmMix texture added on top (moved by tex_mtx1 clips such as EyeHighlight)
            glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE); glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE); glDisable(GL_LIGHTING); glDisable(GL_ALPHA_TEST);
            for (int r = 0; r < v->nruns; r++) {
                int rm = v->runs[r].mat; if (rm >= VOX_MAXMAT || v->partMix[rm] < 0) continue;
                if (v->animSel > 0 && v->visCtl[rm] && !v->visOn[rm]) continue;
                GLuint id = v->tex[v->partMix[rm]].id; if (!id) continue;
                glDisableClientState(GL_COLOR_ARRAY); glColor3f(1, 1, 1);
                glEnableClientState(GL_TEXTURE_COORD_ARRAY); glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, id);
                int uv = v->animSel > 0 && v->uvCtl1[rm];
                if (uv) { const float *M = v->uvM1[rm]; GLfloat tm[16] = { M[0], M[3], 0, 0,  M[1], M[4], 0, 0,  0, 0, 1, 0,  M[2], M[5], 0, 1 };
                          glMatrixMode(GL_TEXTURE); glLoadMatrixf(tm); glMatrixMode(GL_MODELVIEW); }
                const uint32_t *ip = v->useVbo ? NULL : v->cpuIdx;
                glDrawElements(GL_TRIANGLES, v->runs[r].count, GL_UNSIGNED_INT, v->useVbo ? (const void *)((size_t)v->runs[r].first * 4) : (const void *)(ip + v->runs[r].first));
                if (uv) { glMatrixMode(GL_TEXTURE); glLoadIdentity(); glMatrixMode(GL_MODELVIEW); }
            }
            glDisable(GL_BLEND); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glEnable(GL_LIGHTING);
            glDisable(GL_TEXTURE_2D); glDisable(GL_ALPHA_TEST); glDisableClientState(GL_TEXTURE_COORD_ARRAY); glBindTexture(GL_TEXTURE_2D, 0);
        } else if (anyCtl && v->nruns) {
            for (int r = 0; r < v->nruns; r++) {
                int rm = v->runs[r].mat; if (rm < VOX_MAXMAT && v->visCtl[rm] && !v->visOn[rm]) continue;
                const uint32_t *ip = v->useVbo ? NULL : v->cpuIdx;
                glDrawElements(GL_TRIANGLES, v->runs[r].count, GL_UNSIGNED_INT, v->useVbo ? (const void *)((size_t)v->runs[r].first * 4) : (const void *)(ip + v->runs[r].first));
            }
        } else
            glDrawElements(GL_TRIANGLES, v->nidx, GL_UNSIGNED_INT, v->useVbo ? NULL : v->cpuIdx);
        if (v->useVbo) { v->glBindBuffer_(GL_ARRAY_BUFFER_, 0); v->glBindBuffer_(GL_ELEMENT_ARRAY_BUFFER_, 0); }
        glDisableClientState(GL_VERTEX_ARRAY); glDisableClientState(GL_NORMAL_ARRAY); glDisableClientState(GL_COLOR_ARRAY);
        glDisable(GL_CLIP_PLANE0);
        if (v->nprb && v->prbSel > 0 && V3IsTerrainGrid(v)) {   // baked light probes: dots, coloured by the chosen value
            glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND);
            RECT gr; GetClientRect(v->gl, &gr);
            for (int pass = 0; pass < 2; pass++) {                    // the whole-world probes, then (areas) the finer .dlpg ones, smaller
                int i0 = pass ? v->nprb - v->nprbFine : 0, i1 = pass ? v->nprb : v->nprb - v->nprbFine;
                if (i1 <= i0) continue;
                // the dots grow as you zoom in: about 6 m (whole-world probes) / 2 m (finer grids) across at the view's centre
                float px = (pass ? 2.0f : 6.0f) * gr.bottom / (0.9f * (v->dist > 1 ? v->dist : 1));
                float mn = pass ? 3.0f : 4.0f; if (px < mn) px = mn; if (px > 48) px = 48;
                glPointSize(px);
                glBegin(GL_POINTS);
                for (int i = i0; i < i1; i++) {
                    const float *q = v->prb + (size_t)i * V3_PRB_STRIDE; unsigned char c[4]; V3ProbeColour(v, q, c);
                    glColor3ub(c[0], c[1], c[2]); glVertex3f(q[0] - v->drawCentre[0], q[1] - v->drawCentre[1], q[2] - v->drawCentre[2]);
                }
                glEnd();
            }
            if (v->prbHot >= 0 && v->prbHot < v->nprb) {             // the hovered dot: bigger, with a white then dark ring
                const float *q = v->prb + (size_t)v->prbHot * V3_PRB_STRIDE; unsigned char c[4]; V3ProbeColour(v, q, c);
                float base = (v->prbHot >= v->nprb - v->nprbFine ? 2.0f : 6.0f) * gr.bottom / (0.9f * (v->dist > 1 ? v->dist : 1)); if (base < 4) base = 4; if (base > 48) base = 48;
                glDisable(GL_DEPTH_TEST);
                float P[3] = { q[0] - v->drawCentre[0], q[1] - v->drawCentre[1], q[2] - v->drawCentre[2] };
                glPointSize(base + 10); glColor3ub(20, 20, 20); glBegin(GL_POINTS); glVertex3fv(P); glEnd();
                glPointSize(base + 6); glColor3ub(255, 255, 255); glBegin(GL_POINTS); glVertex3fv(P); glEnd();
                glPointSize(base + 2); glColor3ub(c[0], c[1], c[2]); glBegin(GL_POINTS); glVertex3fv(P); glEnd();
                glEnable(GL_DEPTH_TEST);
            }
            glPointSize(1); glEnable(GL_LIGHTING);
        }
        if (v->nsnd && v->sndSel > 0 && V3IsTerrainGrid(v)) {   // sound probes: one dot per record at its lowest height
            glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND);
            RECT gr; GetClientRect(v->gl, &gr);
            float px = 2.5f * gr.bottom / (0.9f * (v->dist > 1 ? v->dist : 1)); if (px < 3) px = 3; if (px > 40) px = 40;
            glPointSize(px); glBegin(GL_POINTS);
            for (int i = 0; i < v->nsnd; i++) {
                const float *q = v->snd + (size_t)i * V3_SND_STRIDE; float x = (q[2 + v->sndSel] - v->sndLo) / (v->sndHi - v->sndLo); if (!(x > 0)) x = 0; if (x > 1) x = 1;
                unsigned char c[4]; VoxHeat((unsigned)(x * 255), c); glColor3ub(c[0], c[1], c[2]);
                glVertex3f(q[0] - v->drawCentre[0], q[1] + 0.5f - v->drawCentre[1], q[2] - v->drawCentre[2]);
            }
            glEnd();
            if (v->sndHot >= 0 && v->sndHot < v->nsnd) {
                const float *q = v->snd + (size_t)v->sndHot * V3_SND_STRIDE; float P[3] = { q[0] - v->drawCentre[0], q[1] + 0.5f - v->drawCentre[1], q[2] - v->drawCentre[2] };
                glDisable(GL_DEPTH_TEST);
                glPointSize(px + 10); glColor3ub(20, 20, 20); glBegin(GL_POINTS); glVertex3fv(P); glEnd();
                glPointSize(px + 6); glColor3ub(255, 255, 255); glBegin(GL_POINTS); glVertex3fv(P); glEnd();
                glEnable(GL_DEPTH_TEST);
            }
            glPointSize(1); glEnable(GL_LIGHTING);
        }
        if (v->part.terrArea[2] > 0 && V3IsTerrainGrid(v) && v->partOk) V3DrawArrows(v);   // full-detail area: arrows to the next areas
        if (v->part.nl) {                     // cloth links and collision shapes: lines, drawn over the model so they stay visible
            glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST); glDisable(GL_TEXTURE_2D);
            glLineWidth(1.5f);
            int anim = v->animSel > 0 && v->skinM && v->part.skN;
            glBegin(GL_LINES);
            int lastM = -1;
            for (size_t i = 0; i < v->part.nl; i++) {
                int m = v->part.lmat[i]; if (v->hidden[m]) continue;
                if (m != lastM) { unsigned char rgb[3]; VoxMatColour(&v->part, m, rgb); glColor3ub(rgb[0], rgb[1], rgb[2]); lastM = m; }
                const float *q = v->part.lv + i * 6;
                float e[6]; memcpy(e, q, 24);
                if (anim && v->part.lbone) for (int k = 0; k < 2; k++) {        // follow the bone while animating
                    unsigned bi = v->part.lbone[i * 2 + k]; if (bi >= (unsigned)v->part.skN) continue;
                    const float *M = v->skinM + (size_t)bi * 12, *P = q + k * 3;
                    for (int r = 0; r < 3; r++) e[k * 3 + r] = M[r * 4] * P[0] + M[r * 4 + 1] * P[1] + M[r * 4 + 2] * P[2] + M[r * 4 + 3] - v->keepOff[r];
                }
                glVertex3f(e[0] - v->drawCentre[0], e[1] - v->drawCentre[1], e[2] - v->drawCentre[2]);
                glVertex3f(e[3] - v->drawCentre[0], e[4] - v->drawCentre[1], e[5] - v->drawCentre[2]);
            }
            glEnd();
            glEnable(GL_DEPTH_TEST); glLineWidth(1.0f);
        }
    }
    SwapBuffers(v->dc);
}


/* ------------------------------------------------------------------ export (OBJ + MTL, PLY, STL) */
typedef struct {
    V3 *v; wchar_t path[2048]; int fmt;              // 1 OBJ, 2 PLY, 3 STL, 4 GLB
    int lod; unsigned char hidden[VOX_MAXMAT];
    int ok; size_t tris; wchar_t err[300];
    unsigned char **tpx; int ntpx;                    // MKW terrain: the shown textures, read back from the graphics card (per v->tex index)
    size_t catTris[6];                                // MKW terrain: triangles per kind (ground, water/lava, cliffs/ice, courses, scenery, other)
} V3Export;

static FILE *V3Open(const wchar_t *p) {
    wchar_t lp[2200];
    if (wcsncmp(p, L"\\\\", 2) == 0) swprintf(lp, 2200, L"%ls", p); else swprintf(lp, 2200, L"\\\\?\\%ls", p);
    FILE *f = _wfopen(lp, L"wb");
    if (!f) f = _wfopen(p, L"wb");
    if (f) setvbuf(f, NULL, _IOFBF, 1 << 20);
    return f;
}

static int V3CmpTriMat(const void *x, const void *y) { return (int)((const uint32_t *)x)[3] - (int)((const uint32_t *)y)[3]; }

/* PNG in memory (for textures inside GLB files and beside OBJ files) */
static unsigned char *V3Png(const unsigned char *rgba, int w, int h, int alpha, size_t *outN) {
    static int crcReady = 0; if (!crcReady) { CrcInit(); crcReady = 1; }
    int ch = alpha ? 4 : 3; size_t row = (size_t)w * ch + 1, rawN = row * h;
    unsigned char *raw = (unsigned char *)malloc(rawN); if (!raw) return NULL;
    for (int y = 0; y < h; y++) {
        unsigned char *o = raw + (size_t)y * row; o[0] = 0;
        const unsigned char *src = rgba + (size_t)y * w * 4;
        if (alpha) memcpy(o + 1, src, (size_t)w * 4);
        else for (int x = 0; x < w; x++) { o[1 + x * 3] = src[x * 4]; o[2 + x * 3] = src[x * 4 + 1]; o[3 + x * 3] = src[x * 4 + 2]; }
    }
    size_t dn = 0; unsigned char *def = DeflateBuf(raw, rawN, &dn);
    uint32_t a1 = 1, a2 = 0;
    for (size_t i = 0; i < rawN; i++) { a1 = (a1 + raw[i]) % 65521; a2 = (a2 + a1) % 65521; }
    free(raw);
    if (!def) return NULL;
    size_t n = 8 + 25 + 12 + dn + 6 + 12;
    unsigned char *f = (unsigned char *)malloc(n); if (!f) { xfree(def); return NULL; }
    size_t at = 0;
    #define P8(x) (f[at++] = (unsigned char)(x))
    #define P32(x) do { uint32_t q_ = (uint32_t)(x); P8(q_ >> 24); P8(q_ >> 16); P8(q_ >> 8); P8(q_); } while (0)
    static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 }; memcpy(f, sig, 8); at = 8;
    P32(13); size_t c0 = at; memcpy(f + at, "IHDR", 4); at += 4; P32(w); P32(h); P8(8); P8(alpha ? 6 : 2); P8(0); P8(0); P8(0); P32(Crc32(f + c0, at - c0));
    P32(dn + 6); c0 = at; memcpy(f + at, "IDAT", 4); at += 4; P8(0x78); P8(0x01); memcpy(f + at, def, dn); at += dn;
    P8(a2 >> 8); P8(a2); P8(a1 >> 8); P8(a1); P32(Crc32(f + c0, at - c0));
    P32(0); c0 = at; memcpy(f + at, "IEND", 4); at += 4; P32(Crc32(f + c0, 4));
    #undef P8
    #undef P32
    xfree(def); *outN = at;
    return f;
}
// the textures the exported materials use, read again from the game dump (the window's copies live on the graphics card)
typedef struct { int n; char key[128][200]; unsigned char *png[128]; size_t pngN[128]; int alpha[128]; int ofMat[256]; } V3ExpTex;
static void V3ExpTexLoad(V3Export *e, VoxMesh *m, V3ExpTex *t) {
    memset(t, 0, sizeof *t); for (int i = 0; i < 256; i++) t->ofMat[i] = -1;
    VoxPart *p = &e->v->part;
    if (!p->isModel || !m->uv) return;
    unsigned char used[256] = { 0 };
    for (size_t v = 0; v < m->nv; v++) used[m->col[v * 4 + 3]] = 1;
    for (int id = 0; id < 256 && id < p->nmat; id++) {
        if (!used[id] || !p->texAlb[id][0] || e->v->cancel) continue;
        char key[200]; snprintf(key, sizeof key, "%s|%s", p->texAlb[id], p->texOpc[id]);
        int k = -1; for (int i = 0; i < t->n; i++) if (!strcmp(t->key[i], key)) k = i;
        if (k < 0) {
            if (t->n >= 128) continue;
            int w = 0, h = 0, al = 0; unsigned char *px = V3ReadMatTex(e->v->dir, p, id, &w, &h, &al);
            if (!px) continue;
            k = t->n++; snprintf(t->key[k], 200, "%s", key); t->alpha[k] = al;
            t->png[k] = V3Png(px, w, h, al, &t->pngN[k]); free(px);
        }
        if (t->png[k]) t->ofMat[id] = k;
    }
}
static void V3ExpTexFree(V3ExpTex *t) { for (int i = 0; i < t->n; i++) free(t->png[i]); }
static void V3SafeFile(const char *in, char *out, size_t n) {
    snprintf(out, n, "%s", in); for (char *q = out; *q; q++) if (strchr("\\/:*?\"<>| |", *q)) *q = '_';
}

/* the export's triangle groups: one per material, or for MKW terrain one per draw group (ground material, or placed
   scenery texture), each with its texture as PNG - the same split the window draws with */
typedef struct {
    int n; uint32_t *key; size_t *start, *count; int *mat, *tex; float *uvs; unsigned char *blend;
    int npng; unsigned char **png; size_t *pngN; int *palpha; char (*pname)[64];
    uint32_t *t4;                                     // triangles (3 indices + key), grouped
    int haveUv;
} V3Groups;
static void V3GroupsFree(V3Groups *g) {
    for (int i = 0; i < g->npng; i++) free(g->png[i]);
    free(g->png); free(g->pngN); free(g->palpha); free(g->pname);
    free(g->key); free(g->start); free(g->count); free(g->mat); free(g->tex); free(g->uvs); free(g->blend); free(g->t4);
    memset(g, 0, sizeof *g);
}
static int V3BuildGroups(V3Export *e, VoxMesh *m, V3Groups *g) {
    memset(g, 0, sizeof *g);
    V3 *v = e->v; VoxPart *p = &v->part;
    size_t nt = m->ni / 3;
    int terr = m->tkeyOut != NULL;
    g->t4 = (uint32_t *)malloc(nt * 16 + 16); if (!g->t4) return 0;
    for (size_t t = 0; t < nt; t++) { memcpy(g->t4 + t * 4, m->idx + t * 3, 12); g->t4[t * 4 + 3] = terr ? m->tkeyOut[t] : m->col[m->idx[t * 3] * 4 + 3]; }
    if (!terr) qsort(g->t4, nt, 16, V3CmpTriMat);        // (terrain triangles are already grouped)
    size_t ng = 0; for (size_t t = 0; t < nt; t++) if (!t || g->t4[t * 4 + 3] != g->t4[t * 4 - 1]) ng++;
    g->key = (uint32_t *)calloc(ng + 1, 4); g->start = (size_t *)calloc(ng + 1, sizeof(size_t)); g->count = (size_t *)calloc(ng + 1, sizeof(size_t));
    g->mat = (int *)calloc(ng + 1, sizeof(int)); g->tex = (int *)calloc(ng + 1, sizeof(int)); g->uvs = (float *)calloc(ng + 1, sizeof(float)); g->blend = (unsigned char *)calloc(ng + 1, 1);
    if (!g->key || !g->start || !g->count || !g->mat || !g->tex || !g->uvs || !g->blend) return 0;
    for (size_t t = 0; t < nt; t++) {
        if (!t || g->t4[t * 4 + 3] != g->t4[t * 4 - 1]) {
            int i = g->n++; g->key[i] = g->t4[t * 4 + 3]; g->start[i] = t;
            g->mat[i] = terr ? (int)m->tmatOut[t] : (int)g->key[i]; g->tex[i] = -1; g->uvs[i] = 1;
        }
        g->count[g->n - 1]++;
    }
    int cap = 0;
    #define V3ADDPNG(pp, nn, al, nm) do { if (g->npng >= cap) { cap = cap ? cap * 2 : 64; \
            g->png = (unsigned char **)realloc(g->png, cap * sizeof(void *)); g->pngN = (size_t *)realloc(g->pngN, cap * sizeof(size_t)); \
            g->palpha = (int *)realloc(g->palpha, cap * sizeof(int)); g->pname = (char (*)[64])realloc(g->pname, (size_t)cap * 64); } \
            g->png[g->npng] = (pp); g->pngN[g->npng] = (nn); g->palpha[g->npng] = (al); snprintf(g->pname[g->npng], sizeof g->pname[g->npng], "%s", (nm)); g->npng++; } while (0)
    if (!terr) {
        V3ExpTex tx; V3ExpTexLoad(e, m, &tx);
        for (int i = 0; i < tx.n; i++) { char nm[200]; snprintf(nm, sizeof nm, "%s", tx.key[i]); char *bar = strchr(nm, '|'); if (bar) *bar = 0; V3ADDPNG(tx.png[i], tx.pngN[i], tx.alpha[i], nm); }
        for (int i = 0; i < g->n; i++) g->tex[i] = g->key[i] < 256 ? tx.ofMat[g->key[i]] : -1;
        g->haveUv = m->uv && tx.n > 0;
        return 1;                                       // (the PNGs now belong to the groups)
    }
    // terrain: the textures the window shows (ground detail, water, scenery), as PNGs
    int *pngOf = (int *)malloc(sizeof(int) * (v->ntex + 1)); if (!pngOf) return 0;
    for (int t = 0; t <= v->ntex; t++) pngOf[t] = -1;
    unsigned char *bright = (unsigned char *)calloc(m->nv + 1, 1);
    for (int i = 0; i < g->n && !v->cancel; i++) {
        uint32_t k = g->key[i]; int ti = -1;
        if (k < VOX_MAXMAT) ti = v->partTex[k];
        else if ((int)(k - VOX_MAXMAT) < v->nmtex) ti = v->mtexTex[k - VOX_MAXMAT];
        if (ti < 0 || ti >= e->ntpx || !e->tpx[ti]) continue;
        int ground = k < VOX_MAXMAT;                            // ground / water: drawn as colour x texture x 2 in the window
        if (pngOf[ti] < 0) {
            int w = v->tex[ti].w, h = v->tex[ti].h, al = v->tex[ti].alpha;
            unsigned char *px = e->tpx[ti];
            if (ground) for (size_t q = 0; q < (size_t)w * h; q++) for (int c = 0; c < 3; c++) { int x = px[q * 4 + c] * 16 / 10; px[q * 4 + c] = (unsigned char)(x > 255 ? 255 : x); }
            size_t pn = 0; unsigned char *png = V3Png(px, w, h, al, &pn);
            if (png) {
                const char *nm = v->tex[ti].key; const char *bar = strrchr(nm, '|'); nm = bar ? bar + 1 : nm;
                char nm2[64];
                if (!strncmp(v->tex[ti].key, "tera|", 5)) { int gm = p->terrMat[k]; snprintf(nm2, sizeof nm2, "Ground_%s", gm >= 0 && gm < 62 ? kTeraMatNames[gm] : nm); }
                else if (!strncmp(v->tex[ti].key, "water|", 6)) snprintf(nm2, sizeof nm2, "Water");
                else snprintf(nm2, sizeof nm2, "%s", nm);
                pngOf[ti] = g->npng; V3ADDPNG(png, pn, al, nm2);
            }
        }
        g->tex[i] = pngOf[ti];
        if (ground) {
            int gm = p->terrMat[k]; g->uvs[i] = gm >= 0 ? VoxTeraUVScale((unsigned)gm) : 1;
            g->blend[i] = v->waterBlend[k];
            if (bright) for (size_t t = g->start[i]; t < g->start[i] + g->count[i]; t++) for (int c = 0; c < 3; c++) bright[g->t4[t * 4 + c]] = 1;
        }
    }
    #undef V3ADDPNG
    // the other half of the window's "x 2": the colour map a little brighter (texture x 1.6, colour x 1.25)
    if (bright) for (size_t q = 0; q < m->nv; q++) if (bright[q]) for (int c = 0; c < 3; c++) { int x = m->col[q * 4 + c] * 5 / 4; m->col[q * 4 + c] = (unsigned char)(x > 255 ? 255 : x); }
    free(bright); free(pngOf);
    g->haveUv = m->uv != NULL;
    return 1;
}
// a group's name: material, plus the texture for placed scenery
static void V3GroupName(V3Export *e, V3Groups *g, int i, char *out, size_t n) {
    VoxPart *p = &e->v->part; int id = g->mat[i];
    const char *mn = id >= 0 && id < p->nmat && p->matName[id][0] ? p->matName[id] : "unknown";
    if (g->key[i] >= VOX_MAXMAT && g->tex[i] >= 0) snprintf(out, n, "%03d %s - %s", id, mn, g->pname[g->tex[i]]);
    else if (g->key[i] >= VOX_MAXMAT) snprintf(out, n, "%03d %s - %u", id, mn, g->key[i] - VOX_MAXMAT);
    else snprintf(out, n, "%03d %s", id, mn);
}
static int V3WriteObj(V3Export *e, VoxMesh *m) {
    // file.obj + file.mtl; faces grouped by material (terrain: by material and texture) so other programs can recolour them
    wchar_t mtl[2100]; swprintf(mtl, 2100, L"%ls", e->path);
    wchar_t *dot = wcsrchr(mtl, L'.'); wchar_t *bs = wcsrchr(mtl, L'\\');
    if (dot && (!bs || dot > bs)) wcscpy(dot, L".mtl"); else wcscat(mtl, L".mtl");
    const wchar_t *mtlName = wcsrchr(mtl, L'\\'); mtlName = mtlName ? mtlName + 1 : mtl;
    VoxPart *p = &e->v->part;
    V3Groups g; if (!V3BuildGroups(e, m, &g)) { V3GroupsFree(&g); swprintf(e->err, 300, L"Out of memory."); return 0; }
    FILE *fm = V3Open(mtl);
    if (!fm) { V3GroupsFree(&g); swprintf(e->err, 300, L"Couldn't create %ls", mtl); return 0; }
    int haveUv = g.haveUv;
    // texture PNGs beside the .obj: <file>_<texture>.png
    char (*texFile)[300] = (char (*)[300])calloc(g.npng + 1, 300);
    if (texFile) {
        wchar_t base[2100]; swprintf(base, 2100, L"%ls", e->path); wchar_t *d2 = wcsrchr(base, L'.'); wchar_t *b2 = wcsrchr(base, L'\\'); if (d2 && (!b2 || d2 > b2)) *d2 = 0;
        const wchar_t *bn = wcsrchr(base, L'\\'); bn = bn ? bn + 1 : base;
        char bnA[600]; WideCharToMultiByte(CP_UTF8, 0, bn, -1, bnA, sizeof bnA, NULL, NULL);
        for (int i = 0; i < g.npng; i++) {
            char sf[140]; V3SafeFile(g.pname[i], sf, sizeof sf);
            snprintf(texFile[i], 300, "%s_%s.png", bnA, sf);
            for (int j = 0; j < i; j++) if (!strcmp(texFile[j], texFile[i])) { snprintf(texFile[i], 300, "%s_%s_%d.png", bnA, sf, i); break; }
            wchar_t full[2400], tw[300]; MultiByteToWideChar(CP_UTF8, 0, texFile[i], -1, tw, 300);
            wchar_t dir[2100]; swprintf(dir, 2100, L"%ls", e->path); wchar_t *b3 = wcsrchr(dir, L'\\'); if (b3) b3[1] = 0; else dir[0] = 0;
            swprintf(full, 2400, L"%ls%ls", dir, tw);
            FILE *pf = g.png[i] ? V3Open(full) : NULL;
            if (pf) { fwrite(g.png[i], 1, g.pngN[i], pf); fclose(pf); } else texFile[i][0] = 0;
        }
    }
    char (*safe)[100] = (char (*)[100])calloc(g.n + 1, 100);
    if (!safe) { fclose(fm); free(texFile); V3GroupsFree(&g); swprintf(e->err, 300, L"Out of memory."); return 0; }
    fprintf(fm, "# %s materials%s\n", p->isModel ? "model" : p->isTriMesh ? "collision / terrain" : "voxel", haveUv ? "" : " (colours are only a guide)");
    for (int i = 0; i < g.n; i++) {
        V3GroupName(e, &g, i, safe[i], 100);
        for (char *q = safe[i]; *q; q++) if (strchr(" /\\():|", *q)) *q = '_';
        unsigned char rgb[3]; VoxMatColour(p, g.mat[i] >= 0 && g.mat[i] < VOX_MAXMAT ? g.mat[i] : 0, rgb);
        int t = haveUv ? g.tex[i] : -1;
        const char *d = g.blend[i] ? "0.8" : "1";
        if (t >= 0 && texFile && texFile[t][0]) {
            char sc[64] = ""; if (g.uvs[i] != 1) snprintf(sc, sizeof sc, "-s %g %g 1 ", g.uvs[i], g.uvs[i]);
            fprintf(fm, "newmtl %s\nKd 1 1 1\nKa 0 0 0\nKs 0 0 0\nd %s\nillum 1\nmap_Kd %s%s\n%s%s%s\n", safe[i], d, sc, texFile[t],
                    g.palpha[t] ? "map_d " : "", g.palpha[t] ? texFile[t] : "", g.palpha[t] ? "\n" : "");
        } else
            fprintf(fm, "newmtl %s\nKd %.3f %.3f %.3f\nKa 0 0 0\nKs 0 0 0\nd %s\nillum 1\n\n", safe[i], rgb[0] / 255.0, rgb[1] / 255.0, rgb[2] / 255.0, d);
    }
    fclose(fm); free(texFile);
    FILE *f = V3Open(e->path);
    if (!f) { free(safe); V3GroupsFree(&g); swprintf(e->err, 300, L"Couldn't create the file."); return 0; }
    char mtlA[600]; WideCharToMultiByte(CP_UTF8, 0, mtlName, -1, mtlA, sizeof mtlA, NULL, NULL);
    if (p->isModel) fprintf(f, "# model (BFRES) %s, bind pose, exported by ns2dig\n# units: game units, Y up\nmtllib %s\n", p->modelName, mtlA);
    else if (p->isTriMesh) fprintf(f, "# collision shape (Phive) or terrain, exported by ns2dig\n# units: game units, Y up\nmtllib %s\n", mtlA);
    else fprintf(f, "# voxel terrain, exported by ns2dig\n# units: game units (voxel size %g), Y up\nmtllib %s\n", p->scale[0], mtlA);
    for (size_t v = 0; v < m->nv; v++) {
        unsigned char *c = m->col + v * 4;
        fprintf(f, "v %.3f %.3f %.3f %.3f %.3f %.3f\n", m->pos[v * 3], m->pos[v * 3 + 1], m->pos[v * 3 + 2], c[0] / 255.0, c[1] / 255.0, c[2] / 255.0);
    }
    for (size_t v = 0; v < m->nv; v++)
        fprintf(f, "vn %.4f %.4f %.4f\n", m->nrm[v * 4] / 127.0, m->nrm[v * 4 + 1] / 127.0, m->nrm[v * 4 + 2] / 127.0);
    if (haveUv) for (size_t v = 0; v < m->nv; v++) fprintf(f, "vt %.5f %.5f\n", m->uv[v * 2], 1.0 - m->uv[v * 2 + 1]);   // OBJ: v up
    for (int i = 0; i < g.n; i++) {
        fprintf(f, "g %s\nusemtl %s\n", safe[i], safe[i]);
        for (size_t t = g.start[i]; t < g.start[i] + g.count[i]; t++) {
            uint32_t *q = g.t4 + t * 4;
            if (haveUv) fprintf(f, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", q[0] + 1, q[0] + 1, q[0] + 1, q[1] + 1, q[1] + 1, q[1] + 1, q[2] + 1, q[2] + 1, q[2] + 1);
            else fprintf(f, "f %u//%u %u//%u %u//%u\n", q[0] + 1, q[0] + 1, q[1] + 1, q[1] + 1, q[2] + 1, q[2] + 1);
        }
    }
    free(safe); V3GroupsFree(&g);
    int bad = ferror(f); fclose(f);
    if (bad) { swprintf(e->err, 300, L"Writing the file failed (disk full?)."); return 0; }
    return 1;
}
static int V3WritePly(V3Export *e, VoxMesh *m) {
    FILE *f = V3Open(e->path);
    if (!f) { swprintf(e->err, 300, L"Couldn't create the file."); return 0; }
    size_t nt = m->ni / 3;
    fprintf(f, "ply\nformat binary_little_endian 1.0\ncomment voxel terrain, exported by ns2dig (Y up)\n"
               "element vertex %zu\nproperty float x\nproperty float y\nproperty float z\n"
               "property float nx\nproperty float ny\nproperty float nz\n"
               "property uchar red\nproperty uchar green\nproperty uchar blue\nproperty uchar material\n"
               "element face %zu\nproperty list uchar int vertex_indices\nend_header\n", m->nv, nt);
    for (size_t v = 0; v < m->nv; v++) {
        float n[3] = { m->nrm[v * 4] / 127.f, m->nrm[v * 4 + 1] / 127.f, m->nrm[v * 4 + 2] / 127.f };
        fwrite(m->pos + v * 3, 4, 3, f); fwrite(n, 4, 3, f); fwrite(m->col + v * 4, 1, 4, f);
    }
    for (size_t t = 0; t < nt; t++) { unsigned char three = 3; fwrite(&three, 1, 1, f); fwrite(m->idx + t * 3, 4, 3, f); }
    int bad = ferror(f); fclose(f);
    if (bad) { swprintf(e->err, 300, L"Writing the file failed (disk full?)."); return 0; }
    return 1;
}

static int V3WriteStl(V3Export *e, VoxMesh *m) {
    FILE *f = V3Open(e->path);
    if (!f) { swprintf(e->err, 300, L"Couldn't create the file."); return 0; }
    // Z up for printers: (x, y, z) -> (x, -z, y), then rest the lowest point on z = 0
    float zmin = m->bmin[1];
    char head[80]; memset(head, 0, 80);
    snprintf(head, 80, "voxel terrain - ns2dig - game units, Z up");
    fwrite(head, 1, 80, f);
    uint32_t nt = (uint32_t)(m->ni / 3); fwrite(&nt, 4, 1, f);
    for (size_t t = 0; t < nt; t++) {
        float rec[12]; unsigned short attr = 0;
        float P[3][3];
        for (int k = 0; k < 3; k++) {
            float *s = m->pos + m->idx[t * 3 + k] * 3;
            P[k][0] = s[0]; P[k][1] = -s[2]; P[k][2] = s[1] - zmin;
        }
        float ux = P[1][0] - P[0][0], uy = P[1][1] - P[0][1], uz = P[1][2] - P[0][2];
        float wx = P[2][0] - P[0][0], wy = P[2][1] - P[0][1], wz = P[2][2] - P[0][2];
        float nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx, l = sqrtf(nx * nx + ny * ny + nz * nz);
        if (l > 0) { nx /= l; ny /= l; nz /= l; }
        rec[0] = nx; rec[1] = ny; rec[2] = nz;
        for (int k = 0; k < 3; k++) { rec[3 + k * 3] = P[k][0]; rec[4 + k * 3] = P[k][1]; rec[5 + k * 3] = P[k][2]; }
        fwrite(rec, 4, 12, f); fwrite(&attr, 2, 1, f);
    }
    int bad = ferror(f); fclose(f);
    if (bad) { swprintf(e->err, 300, L"Writing the file failed (disk full?)."); return 0; }
    return 1;
}


/* GLB (binary glTF 2.0): one file; one primitive per material so each can be hidden or recoloured */
typedef struct { unsigned char *p; size_t n, cap; int fail; } V3Buf;
static void V3Put(V3Buf *b, const void *d, size_t n) {
    if (b->fail) return;
    if (b->n + n > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 1 << 16; while (nc < b->n + n) nc *= 2;
        unsigned char *np = (unsigned char *)realloc(b->p, nc); if (!np) { b->fail = 1; return; }
        b->p = np; b->cap = nc;
    }
    memcpy(b->p + b->n, d, n); b->n += n;
}
static void V3PutS(V3Buf *b, const char *s) { V3Put(b, s, strlen(s)); }
static void V3PutF(V3Buf *b, const char *fmt, ...) {
    char t[1024]; va_list ap; va_start(ap, fmt); vsnprintf(t, sizeof t, fmt, ap); va_end(ap); V3PutS(b, t);
}
static void V3JsonStr(V3Buf *b, const char *s) {        // material names are plain ASCII, but be safe
    V3PutS(b, "\"");
    for (; *s; s++) { char c = *s; if (c == '"' || c == '\\') { char e[2] = { '\\', c }; V3Put(b, e, 2); } else if ((unsigned char)c >= 32) V3Put(b, &c, 1); }
    V3PutS(b, "\"");
}
static double V3Lin(int c) { double x = c / 255.0; return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4); }

static int V3WriteGlb(V3Export *e, VoxMesh *m) {
    VoxPart *p = &e->v->part;
    size_t nt = m->ni / 3;
    V3Groups g; if (!V3BuildGroups(e, m, &g)) { V3GroupsFree(&g); swprintf(e->err, 300, L"Out of memory."); return 0; }
    int haveUv = g.haveUv && g.npng > 0, terr = m->tkeyOut != NULL;   // terrain: vertex colours (the colour map) too
    // binary chunk: positions | normals | (uvs) | (colours) | indices (by group) | (PNG images)
    V3Buf bin = { 0 };
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (size_t v = 0; v < m->nv; v++) for (int k = 0; k < 3; k++) { float x = m->pos[v * 3 + k]; if (x < lo[k]) lo[k] = x; if (x > hi[k]) hi[k] = x; }
    V3Put(&bin, m->pos, m->nv * 12);
    size_t nrmOff = bin.n;
    for (size_t v = 0; v < m->nv; v++) {
        float n[3] = { m->nrm[v * 4] / 127.f, m->nrm[v * 4 + 1] / 127.f, m->nrm[v * 4 + 2] / 127.f };
        float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (l < 1e-6f) { n[0] = 0; n[1] = 1; n[2] = 0; } else { n[0] /= l; n[1] /= l; n[2] /= l; }
        V3Put(&bin, n, 12);
    }
    size_t uvOff = bin.n;
    if (haveUv) V3Put(&bin, m->uv, m->nv * 8);
    size_t colOff = bin.n;
    if (terr) for (size_t v = 0; v < m->nv; v++) { unsigned char c[4] = { m->col[v * 4], m->col[v * 4 + 1], m->col[v * 4 + 2], 255 }; V3Put(&bin, c, 4); }
    size_t idxOff = bin.n;
    for (size_t t = 0; t < nt; t++) V3Put(&bin, g.t4 + t * 4, 12);
    size_t *imgOff = (size_t *)calloc(g.npng + 1, sizeof(size_t));
    for (int i = 0; i < g.npng && imgOff; i++) {
        while (bin.n % 4) { unsigned char z = 0; V3Put(&bin, &z, 1); }
        imgOff[i] = bin.n; if (g.png[i]) V3Put(&bin, g.png[i], g.pngN[i]);
    }
    while (bin.n % 4) { unsigned char z = 0; V3Put(&bin, &z, 1); }
    if (bin.fail || !imgOff) { free(bin.p); free(imgOff); V3GroupsFree(&g); swprintf(e->err, 300, L"Out of memory."); return 0; }

    // bufferViews: 0 positions, 1 normals, [uv], [colours], indices, images...
    int view = 2, uvView = haveUv ? view++ : -1, colView = terr ? view++ : -1, idxView = view++, imgView0 = view;
    // accessors: 0 positions, 1 normals, [uv], [colours], then one per group
    int acc = 2, uvAcc = haveUv ? acc++ : -1, colAcc = terr ? acc++ : -1, idxAcc0 = acc;
    int anyScale = 0; for (int i = 0; i < g.n; i++) if (g.tex[i] >= 0 && g.uvs[i] != 1) anyScale = 1;
    V3Buf js = { 0 };
    V3PutF(&js, "{\"asset\":{\"version\":\"2.0\",\"generator\":\"ns2dig - %s\"},", p->isModel ? "model" : p->isTriMesh ? "collision / terrain" : "voxel terrain");
    if (anyScale && haveUv) V3PutS(&js, "\"extensionsUsed\":[\"KHR_texture_transform\"],");
    V3PutS(&js, "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],");
    char title[600]; WideCharToMultiByte(CP_UTF8, 0, e->v->title, -1, title, sizeof title, NULL, NULL);
    V3PutS(&js, "\"nodes\":[{\"mesh\":0,\"name\":"); V3JsonStr(&js, title); V3PutS(&js, "}],");
    V3PutF(&js, "\"meshes\":[{\"name\":\"%s\",\"primitives\":[", p->isModel ? "model" : "terrain");
    for (int i = 0; i < g.n; i++) {
        int tex = haveUv ? g.tex[i] : -1;
        V3PutF(&js, "%s{\"attributes\":{\"POSITION\":0,\"NORMAL\":1", i ? "," : "");
        if (tex >= 0) V3PutF(&js, ",\"TEXCOORD_0\":%d", uvAcc);
        if (terr) V3PutF(&js, ",\"COLOR_0\":%d", colAcc);
        V3PutF(&js, "},\"indices\":%d,\"material\":%d,\"mode\":4}", idxAcc0 + i, i);
    }
    V3PutS(&js, "]}],\"materials\":[");
    for (int i = 0; i < g.n; i++) {
        int id = g.mat[i] >= 0 && g.mat[i] < VOX_MAXMAT ? g.mat[i] : 0;
        int tex = haveUv ? g.tex[i] : -1;
        unsigned char rgb[3]; VoxMatColour(p, id, rgb);
        const char *nmn = id < p->nmat && p->matName[id][0] ? p->matName[id] : "unknown";
        int metal = !terr && (VoxStartsWith(nmn, "Money") || VoxStartsWith(nmn, "DarkGold") || VoxStartsWith(nmn, "Iron") || VoxStartsWith(nmn, "ElectricIron"));
        char nm2[160]; V3GroupName(e, &g, i, nm2, sizeof nm2);
        // terrain: drawn from both sides, as in the window; water a little see-through
        const char *ds = terr ? "true" : (tex >= 0 && g.palpha[tex]) ? "true" : "false";
        char alpha[80] = "";
        if (g.blend[i]) snprintf(alpha, sizeof alpha, ",\"alphaMode\":\"BLEND\"");
        else if (tex >= 0 && g.palpha[tex]) snprintf(alpha, sizeof alpha, ",\"alphaMode\":\"MASK\",\"alphaCutoff\":0.5");
        V3PutF(&js, "%s{\"name\":", i ? "," : ""); V3JsonStr(&js, nm2);
        if (tex >= 0) {
            V3PutF(&js, ",\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":%d", tex);
            if (g.uvs[i] != 1) V3PutF(&js, ",\"extensions\":{\"KHR_texture_transform\":{\"scale\":[%g,%g]}}", g.uvs[i], g.uvs[i]);
            V3PutF(&js, "},\"metallicFactor\":0,\"roughnessFactor\":0.8}%s,\"doubleSided\":%s}", alpha, ds);
        } else if (terr)                                 // the vertex colours carry the colour
            V3PutF(&js, ",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,1,1,%s],\"metallicFactor\":0,\"roughnessFactor\":0.9}%s,\"doubleSided\":true}",
                   g.blend[i] ? "0.8" : "1", g.blend[i] ? ",\"alphaMode\":\"BLEND\"" : "");
        else
            V3PutF(&js, ",\"pbrMetallicRoughness\":{\"baseColorFactor\":[%.4f,%.4f,%.4f,1],\"metallicFactor\":%s,\"roughnessFactor\":%s},\"doubleSided\":false}",
                   V3Lin(rgb[0]), V3Lin(rgb[1]), V3Lin(rgb[2]), metal ? "0.8" : "0", metal ? "0.35" : "0.9");
    }
    V3PutS(&js, "],");
    if (haveUv) {
        V3PutS(&js, "\"samplers\":[{\"magFilter\":9729,\"minFilter\":9987,\"wrapS\":10497,\"wrapT\":10497}],\"images\":[");
        for (int i = 0; i < g.npng; i++) {
            V3PutF(&js, "%s{\"bufferView\":%d,\"mimeType\":\"image/png\",\"name\":", i ? "," : "", imgView0 + i); V3JsonStr(&js, g.pname[i]); V3PutS(&js, "}");
        }
        V3PutS(&js, "],\"textures\":[");
        for (int i = 0; i < g.npng; i++) V3PutF(&js, "%s{\"sampler\":0,\"source\":%d}", i ? "," : "", i);
        V3PutS(&js, "],");
    }
    V3PutS(&js, "\"accessors\":[");
    V3PutF(&js, "{\"bufferView\":0,\"componentType\":5126,\"count\":%zu,\"type\":\"VEC3\",\"min\":[%.9g,%.9g,%.9g],\"max\":[%.9g,%.9g,%.9g]},",
           m->nv, (double)lo[0], (double)lo[1], (double)lo[2], (double)hi[0], (double)hi[1], (double)hi[2]);   // exact float values
    V3PutF(&js, "{\"bufferView\":1,\"componentType\":5126,\"count\":%zu,\"type\":\"VEC3\"}", m->nv);
    if (haveUv) V3PutF(&js, ",{\"bufferView\":%d,\"componentType\":5126,\"count\":%zu,\"type\":\"VEC2\"}", uvView, m->nv);
    if (terr) V3PutF(&js, ",{\"bufferView\":%d,\"componentType\":5121,\"normalized\":true,\"count\":%zu,\"type\":\"VEC4\"}", colView, m->nv);
    for (int i = 0; i < g.n; i++)
        V3PutF(&js, ",{\"bufferView\":%d,\"byteOffset\":%zu,\"componentType\":5125,\"count\":%zu,\"type\":\"SCALAR\"}", idxView, g.start[i] * 12, g.count[i] * 3);
    V3PutF(&js, "],\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":%zu,\"target\":34962},"
                "{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu,\"target\":34962}",
           m->nv * 12, nrmOff, m->nv * 12);
    if (haveUv) V3PutF(&js, ",{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu,\"target\":34962}", uvOff, m->nv * 8);
    if (terr) V3PutF(&js, ",{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu,\"target\":34962}", colOff, m->nv * 4);
    V3PutF(&js, ",{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu,\"target\":34963}", idxOff, nt * 12);
    if (haveUv) for (int i = 0; i < g.npng; i++) V3PutF(&js, ",{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu}", imgOff[i], g.png[i] ? g.pngN[i] : (size_t)0);
    V3PutF(&js, "],\"buffers\":[{\"byteLength\":%zu}]}", bin.n);
    free(imgOff); V3GroupsFree(&g);
    while (js.n % 4) V3PutS(&js, " ");
    if (js.fail) { free(js.p); free(bin.p); swprintf(e->err, 300, L"Out of memory."); return 0; }

    unsigned long long total = 12 + 8 + js.n + 8 + bin.n;
    if (total >= 0xFFFFFFFFULL) { free(js.p); free(bin.p); swprintf(e->err, 300, L"Too big for a GLB file (over 4 GB) - try Low resolution."); return 0; }
    FILE *f = V3Open(e->path);
    if (!f) { free(js.p); free(bin.p); swprintf(e->err, 300, L"Couldn't create the file."); return 0; }
    uint32_t h[3] = { 0x46546C67u, 2, (uint32_t)total };                 // "glTF", version 2, length
    uint32_t cj[2] = { (uint32_t)js.n, 0x4E4F534Au }, cb[2] = { (uint32_t)bin.n, 0x004E4942u };   // "JSON", "BIN\0"
    fwrite(h, 4, 3, f); fwrite(cj, 4, 2, f); fwrite(js.p, 1, js.n, f); fwrite(cb, 4, 2, f); fwrite(bin.p, 1, bin.n, f);
    free(js.p); free(bin.p);
    int bad = ferror(f); fclose(f);
    if (bad) { swprintf(e->err, 300, L"Writing the file failed (disk full?)."); return 0; }
    return 1;
}

static DWORD V3ExportRun(V3Export *e) {
    VoxMesh m;
    if (!VoxBuildMesh(&e->v->part, &m, e->lod, e->hidden, &e->v->cancel, NULL)) {
        swprintf(e->err, 300, L"Building the surface failed (out of memory?)."); return 0;
    }
    if (!m.ni) { swprintf(e->err, 300, L"Nothing to export - every material is hidden."); VoxFreeMesh(&m); return 0; }
    // (the surface's own winding is already consistent and points outwards - no fixing needed)
    e->tris = m.ni / 3;
    if (m.tmatOut) for (size_t t = 0; t < m.ni / 3; t++) {         // terrain: what went in, by kind (shown when done)
        const char *n = e->v->part.matName[m.tmatOut[t]];
        int k = V3IsWaterMat(n) || !strncmp(n, "Surface:", 8) ? 1 : (!strncmp(n, "Iceberg:", 8) || !strncmp(n, "Cliff:", 6)) ? 2 :
                !strncmp(n, "Stage:", 6) ? 3 : !strncmp(n, "Scenery", 7) || !strncmp(n, "Landmark:", 9) || !strncmp(n, "Object:", 7) ? 4 : e->v->part.terrMat[m.tmatOut[t]] >= 0 ? 0 : 5;
        e->catTris[k]++;
    }
    e->ok = e->fmt == 1 ? V3WriteObj(e, &m) : e->fmt == 2 ? V3WritePly(e, &m) : e->fmt == 4 ? V3WriteGlb(e, &m) : V3WriteStl(e, &m);
    VoxFreeMesh(&m);
    return 0;
}
static DWORD WINAPI V3ExportWorker(LPVOID p) {
    V3Export *e = (V3Export *)p;
    DWORD r = V3ExportRun(e);
    if (e->tpx) { for (int i = 0; i < e->ntpx; i++) free(e->tpx[i]); free(e->tpx); e->tpx = NULL; }
    PostMessageW(e->v->wnd, WM_V3_EXPORTED, 0, (LPARAM)e);
    return r;
}

int UiWritePngFile(const wchar_t *path, const unsigned char *rgba, int w, int h);   // tex_win.c
/* Export PNG: the 3D view exactly as shown, at the window's size */
static void V3SavePng(V3 *v) {
    if (!v->rc || !v->haveMesh) return;
    RECT rc; GetClientRect(v->gl, &rc); int w = rc.right, h = rc.bottom;
    if (w < 1 || h < 1) return;
    unsigned char *px = (unsigned char *)malloc((size_t)w * h * 4), *flip = (unsigned char *)malloc((size_t)w * h * 4);
    if (!px || !flip) { free(px); free(flip); return; }
    V3Render(v); wglMakeCurrent(v->dc, v->rc);
    glFinish(); glReadBuffer(GL_FRONT); glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    for (int y = 0; y < h; y++) memcpy(flip + (size_t)y * w * 4, px + (size_t)(h - 1 - y) * w * 4, (size_t)w * 4);
    for (size_t i = 0; i < (size_t)w * h; i++) flip[i * 4 + 3] = 255;
    free(px);
    wchar_t path[2048]; const wchar_t *t = wcsstr(v->title, L" - "); swprintf(path, 2048, L"%ls", t ? t + 3 : L"view");
    for (wchar_t *q = path; *q; q++) if (wcschr(L"\\/:*?\"<>| ", *q)) *q = L'_';
    { size_t l = wcslen(path); if (l > 120) memmove(path, path + l - 120, 121 * sizeof(wchar_t)); }
    if (V3IsTerrainGrid(v) && v->ovlSel > 0 && v->ovlSel < VOV_COUNT) {          // name the ground layer
        wchar_t g[80]; swprintf(g, 80, L"_%hs", kVovNames[v->ovlSel]);
        for (wchar_t *q = g; *q; q++) if (wcschr(L"\\/:*?\"<>| +", *q)) *q = L'_';
        if (wcslen(path) + wcslen(g) < 2000) wcscat(path, g);
    }
    OPENFILENAMEW of; ZeroMemory(&of, sizeof of);
    of.lStructSize = sizeof of; of.hwndOwner = v->wnd; of.lpstrFilter = L"PNG image (*.png)\0*.png\0"; of.lpstrFile = path; of.nMaxFile = 2048;
    of.lpstrDefExt = L"png"; of.lpstrTitle = L"Export PNG (the view as shown)"; of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&of)) {
        if (UiWritePngFile(path, flip, w, h)) { swprintf(v->status, 256, L"Saved the view (%d \x00D7 %d) as %ls", w, h, wcsrchr(path, L'\\') ? wcsrchr(path, L'\\') + 1 : path); V3UpdateInfo(v); }
        else MessageBoxW(v->wnd, L"Saving the PNG failed.", L"Export PNG", MB_ICONWARNING);
    }
    free(flip);
}
static void V3StartExport(V3 *v) {
    if (!v->partOk || v->exporting) return;
    if (v->thread) { MessageBoxW(v->wnd, L"Wait for the surface to finish building first.", L"Export", MB_ICONINFORMATION); return; }
    V3Export *e = (V3Export *)calloc(1, sizeof(V3Export));
    if (!e) return;
    // default name from the window title (folder path within the scan)
    wchar_t name[600]; const wchar_t *t = wcsstr(v->title, L" - "); swprintf(name, 600, L"%ls", t ? t + 3 : L"terrain");
    for (wchar_t *q = name; *q; q++) if (wcschr(L"\\/:*?\"<>| ", *q)) *q = L'_';
    { size_t l = wcslen(name); if (l > 120) memmove(name, name + l - 120, 121 * sizeof(wchar_t)); }   // keep the end (part name)
    swprintf(e->path, 2048, L"%ls", name);
    OPENFILENAMEW of; ZeroMemory(&of, sizeof(of));
    of.lStructSize = sizeof(of); of.hwndOwner = v->wnd;
    of.lpstrFilter = L"GLB - one file with textures/colours: 3D Viewer, Blender, game engines, web (*.glb)\0*.glb\0"
                     L"OBJ - most 3D programs; textures saved as PNG files beside it (*.obj)\0*.obj\0"
                     L"PLY - colours per vertex, compact (*.ply)\0*.ply\0"
                     L"STL - for 3D printing, no colour (*.stl)\0*.stl\0";
    of.nFilterIndex = 1;
    of.lpstrFile = e->path; of.nMaxFile = 2048;
    of.lpstrDefExt = L"glb"; of.lpstrTitle = L"Export 3D file";
    of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&of)) { free(e); return; }
    { static const int map[5] = { 4, 4, 1, 2, 3 };    // list order: GLB, OBJ, PLY, STL
      e->fmt = of.nFilterIndex <= 4 ? map[of.nFilterIndex] : 4; }
    // the typed extension wins over the list choice
    const wchar_t *dot = wcsrchr(e->path, L'.');
    if (dot) { if (!_wcsicmp(dot, L".obj")) e->fmt = 1; else if (!_wcsicmp(dot, L".ply")) e->fmt = 2; else if (!_wcsicmp(dot, L".stl")) e->fmt = 3; else if (!_wcsicmp(dot, L".glb")) e->fmt = 4; }
    if (e->fmt < 1 || e->fmt > 4) e->fmt = 4;
    e->v = v; e->lod = v->lodSel; memcpy(e->hidden, v->hidden, sizeof e->hidden);
    if (V3IsTerrainGrid(v) && v->rc && v->ntex) {      // the textures as shown (the window's copies live on the graphics card)
        e->tpx = (unsigned char **)calloc(v->ntex, sizeof(void *)); e->ntpx = e->tpx ? v->ntex : 0;
        unsigned char *want = (unsigned char *)calloc(v->ntex + 1, 1);
        if (e->tpx && want) {
            for (int k = 0; k < v->part.nmat && k < VOX_MAXMAT; k++) if (!v->hidden[k] && v->partTex[k] >= 0 && v->partTex[k] < v->ntex) want[v->partTex[k]] = 1;
            for (int r = 0; r < v->nruns; r++) { int k = v->runs[r].mat - VOX_MAXMAT; if (k >= 0 && k < v->nmtex && v->mtexTex[k] >= 0 && v->mtexTex[k] < v->ntex) want[v->mtexTex[k]] = 1; }
            wglMakeCurrent(v->dc, v->rc);
            for (int t = 0; t < v->ntex; t++) {
                if (!want[t] || !v->tex[t].id || v->tex[t].w < 1 || v->tex[t].h < 1) continue;
                e->tpx[t] = (unsigned char *)malloc((size_t)v->tex[t].w * v->tex[t].h * 4);
                if (!e->tpx[t]) continue;
                glBindTexture(GL_TEXTURE_2D, v->tex[t].id); glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, e->tpx[t]);
            }
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        free(want);
    }
    v->cancel = 0;
    v->exporting = 1;
    EnableWindow(v->exportb, FALSE); EnableWindow(v->lod, FALSE); EnableWindow(v->list, FALSE);
    EnableWindow(v->all, FALSE); EnableWindow(v->none, FALSE);
    swprintf(v->status, 256, L"Exporting...");
    V3UpdateInfo(v);
    v->exportThread = CreateThread(NULL, 0, V3ExportWorker, e, 0, NULL);
    if (!v->exportThread) { v->exporting = 0; if (e->tpx) { for (int i = 0; i < e->ntpx; i++) free(e->tpx[i]); free(e->tpx); } free(e); }
}

void Vox3DOpen(HWND owner, const wchar_t *partDir, const wchar_t *geomJson, const wchar_t *title, HFONT font, int dpi);
static WINDOWPLACEMENT g_v3Place; static int g_v3PlaceSet;      // the next window's size (an area opened from a view)
/* ------------------------------------------------------------------ terrain: right-click a spot -> that area in full detail */
// the ground point under the mouse (part coordinates), by following the view ray over the height grid. 0 if none.
static float V3GridH(const VoxPart *p, float x, float z, int *in) {
    int G1 = p->gridW; float ox = p->tv[0], oz = p->tv[2], st = p->tv[3] - p->tv[0];
    float fx = (x - ox) / st, fz = (z - oz) / st;
    if (fx < 0 || fz < 0 || fx > G1 - 1 || fz > G1 - 1) { *in = 0; return 0; }
    *in = 1;
    int ix = (int)fx, iz = (int)fz; if (ix > G1 - 2) ix = G1 - 2; if (iz > G1 - 2) iz = G1 - 2;
    float ax = fx - ix, az = fz - iz;
    const float *a = p->tv + ((size_t)iz * G1 + ix) * 3, *b = a + 3, *c = a + (size_t)G1 * 3, *d = c + 3;
    return (a[1] * (1 - ax) + b[1] * ax) * (1 - az) + (c[1] * (1 - ax) + d[1] * ax) * az;
}
static int V3PickGround(V3 *v, int mx, int my, float out[3]) {
    RECT rc; GetClientRect(v->gl, &rc);
    int w = rc.right > 0 ? rc.right : 1, h = rc.bottom > 0 ? rc.bottom : 1;
    float nearp = v->dist * 0.02f; if (nearp < 0.05f) nearp = 0.05f;
    float fy = nearp * 0.45f, fx = fy * w / h;
    // ray in eye space (camera at 0, looking down -z), then undo the view: translate (pan, -dist), rotate pitch about x, yaw about y
    float d[3] = { (2.0f * (mx + 0.5f) / w - 1) * fx, (1 - 2.0f * (my + 0.5f) / h) * fy, -nearp };
    float o[3] = { -v->panx, -v->pany, v->dist };
    float cp = cosf(-v->pitch * 3.14159265f / 180), sp = sinf(-v->pitch * 3.14159265f / 180);
    float cy = cosf(-v->yaw * 3.14159265f / 180), sy = sinf(-v->yaw * 3.14159265f / 180);
    float *vv[2] = { o, d };
    for (int k = 0; k < 2; k++) {
        float *q = vv[k];
        float y1 = q[1] * cp - q[2] * sp, z1 = q[1] * sp + q[2] * cp; q[1] = y1; q[2] = z1;      // about x
        float x2 = q[0] * cy + q[2] * sy, z2 = -q[0] * sy + q[2] * cy; q[0] = x2; q[2] = z2;     // about y
        if (k == 0) for (int i = 0; i < 3; i++) q[i] += v->drawCentre[i];                        // back to part coordinates
    }
    float L = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); for (int i = 0; i < 3; i++) d[i] /= L;
    const VoxPart *p = &v->part;
    float st = (p->tv[3] - p->tv[0]) * 0.5f, tMax = v->dist + v->radius * 4;
    float prevT = 0; int was = -1;
    for (float t = 0; t < tMax; t += st) {
        float P[3] = { o[0] + d[0] * t, o[1] + d[1] * t, o[2] + d[2] * t };
        int in; float gh = V3GridH(p, P[0], P[2], &in);
        if (!in) { was = -1; prevT = t; continue; }
        int below = P[1] <= gh;
        if (below && was == 0) {                       // crossed the ground between prevT and t: narrow it down
            float a = prevT, b = t;
            for (int k = 0; k < 30; k++) { float m = (a + b) / 2; float Q[3] = { o[0] + d[0] * m, o[1] + d[1] * m, o[2] + d[2] * m };
                int i2; float g2 = V3GridH(p, Q[0], Q[2], &i2); if (Q[1] <= g2) b = m; else a = m; }
            for (int i = 0; i < 3; i++) out[i] = o[i] + d[i] * b;
            return 1;
        }
        was = below; prevT = t;
    }
    return 0;
}
#define V3_AREA 4.0f                                       // area size in .tscb units (x250 = 1,000 game units)
// a point in part coordinates -> window pixels (as V3Render draws it); 0 if behind the camera
static int V3Project(V3 *v, const float P[3], float *sx, float *sy) {
    RECT rc; GetClientRect(v->gl, &rc);
    int w = rc.right > 0 ? rc.right : 1, h = rc.bottom > 0 ? rc.bottom : 1;
    float nearp = v->dist * 0.02f; if (nearp < 0.05f) nearp = 0.05f;
    float fy = nearp * 0.45f, fx = fy * w / h;
    float q[3] = { P[0] - v->drawCentre[0], P[1] - v->drawCentre[1], P[2] - v->drawCentre[2] };
    float cy = cosf(v->yaw * 3.14159265f / 180), sy_ = sinf(v->yaw * 3.14159265f / 180);
    float x1 = q[0] * cy + q[2] * sy_, z1 = -q[0] * sy_ + q[2] * cy; q[0] = x1; q[2] = z1;             // about y
    float cp = cosf(v->pitch * 3.14159265f / 180), sp = sinf(v->pitch * 3.14159265f / 180);
    float y2 = q[1] * cp - q[2] * sp, z2 = q[1] * sp + q[2] * cp; q[1] = y2; q[2] = z2;             // about x
    q[0] += v->panx; q[1] += v->pany; q[2] -= v->dist;
    if (q[2] > -nearp * 0.5f) return 0;
    float xn = q[0] * nearp / -q[2] / fx, yn = q[1] * nearp / -q[2] / fy;
    *sx = (xn + 1) * 0.5f * w; *sy = (1 - yn) * 0.5f * h;
    return 1;
}
/* Full-detail area views: an arrow on each edge (0 north = -z, 1 south, 2 west = -x, 3 east) to load the next area.
   tri: the arrow's 3 corners (part coordinates), lying flat a little above the ground at the middle of that edge.
   Returns 0 where there's no area beyond (the edge of the world). */
static int V3ArrowTri(V3 *v, int k, float tri[3][3], float *nx, float *nz) {
    const VoxPart *p = &v->part;
    if (!v->partOk || !p->tv || !(p->terrArea[2] > 0) || !(p->terrWorld[2] > 0) || p->gridW < 2) return 0;   // (not while the next area loads)
    const float *W = p->terrWorld, *A = p->terrArea;
    static const int dx[4] = { 0, 0, -1, 1 }, dz[4] = { -1, 1, 0, 0 };
    float ax = A[0] + dx[k] * A[2], az = A[1] + dz[k] * A[2];
    if (ax < W[0] - 1e-3f) { if (A[0] <= W[0] + 1e-3f) return 0; ax = W[0]; }
    if (az < W[1] - 1e-3f) { if (A[1] <= W[1] + 1e-3f) return 0; az = W[1]; }
    if (ax + A[2] > W[0] + W[3] + 1e-3f) { if (A[0] + A[2] >= W[0] + W[3] - 1e-3f) return 0; ax = W[0] + W[3] - A[2]; }
    if (az + A[2] > W[1] + W[3] + 1e-3f) { if (A[1] + A[2] >= W[1] + W[3] - 1e-3f) return 0; az = W[1] + W[3] - A[2]; }
    if (nx) *nx = ax; if (nz) *nz = az;
    float ws = W[2], x0 = (A[0] - W[0]) * ws, z0 = (A[1] - W[1]) * ws, S = A[2] * ws;
    float mx = x0 + S / 2 + dx[k] * S / 2, mz = z0 + S / 2 + dz[k] * S / 2;
    float in = S * 0.03f, L = S * 0.08f, gap = S * 0.06f;           // clear of the area's edge
    int ok; float gh = V3GridH(p, mx - dx[k] * in, mz - dz[k] * in, &ok); if (!ok) gh = (v->bmin[1] + v->bmax[1]) / 2 + v->drawCentre[1];
    float y = gh + S * 0.02f;
    mx += dx[k] * gap; mz += dz[k] * gap;
    float px = -dz[k], pz = dx[k];                                   // across the arrow
    tri[0][0] = mx + dx[k] * L; tri[0][1] = y; tri[0][2] = mz + dz[k] * L;                     // the tip, pointing away
    tri[1][0] = mx - dx[k] * L * 0.3f + px * L * 0.75f; tri[1][1] = y; tri[1][2] = mz - dz[k] * L * 0.3f + pz * L * 0.75f;
    tri[2][0] = mx - dx[k] * L * 0.3f - px * L * 0.75f; tri[2][1] = y; tri[2][2] = mz - dz[k] * L * 0.3f - pz * L * 0.75f;
    return 1;
}
static int V3ArrowAt(V3 *v, int mx, int my) {
    for (int k = 0; k < 4; k++) {
        float t[3][3], s[3][2];
        if (!V3ArrowTri(v, k, t, NULL, NULL)) continue;
        int ok = 1; for (int i = 0; i < 3 && ok; i++) ok = V3Project(v, t[i], &s[i][0], &s[i][1]);
        if (!ok) continue;
        float cx = (s[0][0] + s[1][0] + s[2][0]) / 3, cy = (s[0][1] + s[1][1] + s[2][1]) / 3;
        for (int i = 0; i < 3; i++) { s[i][0] = cx + (s[i][0] - cx) * 1.4f; s[i][1] = cy + (s[i][1] - cy) * 1.4f; }   // a little easier to hit
        float d1 = (mx - s[1][0]) * (s[0][1] - s[1][1]) - (s[0][0] - s[1][0]) * (my - s[1][1]);
        float d2 = (mx - s[2][0]) * (s[1][1] - s[2][1]) - (s[1][0] - s[2][0]) * (my - s[2][1]);
        float d3 = (mx - s[0][0]) * (s[2][1] - s[0][1]) - (s[2][0] - s[0][0]) * (my - s[0][1]);
        int neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
        if (!(neg && pos)) return k;
        float r = fmaxf(fabsf(s[0][0] - cx), fabsf(s[0][1] - cy)); if (r < 14) { if ((mx - cx) * (mx - cx) + (my - cy) * (my - cy) < 14 * 14) return k; }
    }
    return -1;
}
static void V3DrawArrows(V3 *v) {
    glDisable(GL_LIGHTING); glDisable(GL_TEXTURE_2D); glDisable(GL_DEPTH_TEST); glDisable(GL_CLIP_PLANE0);
    for (int k = 0; k < 4; k++) {
        float t[3][3]; if (!V3ArrowTri(v, k, t, NULL, NULL)) continue;
        if (v->arrowHot == k) glColor3ub(255, 214, 90); else glColor3ub(255, 112, 84);
        glBegin(GL_TRIANGLES); for (int i = 0; i < 3; i++) glVertex3f(t[i][0] - v->drawCentre[0], t[i][1] - v->drawCentre[1], t[i][2] - v->drawCentre[2]); glEnd();
        glColor3ub(30, 20, 18); glLineWidth(2);
        glBegin(GL_LINE_LOOP); for (int i = 0; i < 3; i++) glVertex3f(t[i][0] - v->drawCentre[0], t[i][1] - v->drawCentre[1], t[i][2] - v->drawCentre[2]); glEnd();
        // its compass letter, flat on the arrow (top of the letter = north, as in the top view)
        static const float ltr[4][12][2] = {                       // strokes as pairs of points in -1..1 (y up = north)
            { { -.6f, -1 }, { -.6f, 1 }, { -.6f, 1 }, { .6f, -1 }, { .6f, -1 }, { .6f, 1 } },                                              // N
            { { .6f, .8f }, { -.6f, .8f }, { -.6f, .8f }, { -.6f, 0 }, { -.6f, 0 }, { .6f, 0 }, { .6f, 0 }, { .6f, -.8f }, { .6f, -.8f }, { -.6f, -.8f } },   // S
            { { -.7f, 1 }, { -.35f, -1 }, { -.35f, -1 }, { 0, .3f }, { 0, .3f }, { .35f, -1 }, { .35f, -1 }, { .7f, 1 } },                   // W
            { { .6f, 1 }, { -.6f, 1 }, { -.6f, 1 }, { -.6f, -1 }, { -.6f, -1 }, { .6f, -1 }, { -.6f, 0 }, { .4f, 0 } } };                  // E
        static const int nl[4] = { 6, 10, 8, 8 };
        float cxp = (t[0][0] + t[1][0] + t[2][0]) / 3, czp = (t[0][2] + t[1][2] + t[2][2]) / 3, sz = sqrtf((t[1][0] - t[2][0]) * (t[1][0] - t[2][0]) + (t[1][2] - t[2][2]) * (t[1][2] - t[2][2])) * 0.16f;
        glLineWidth(3); glColor3ub(25, 25, 25);
        glBegin(GL_LINES);
        for (int i = 0; i < nl[k]; i++) glVertex3f(cxp + ltr[k][i][0] * sz - v->drawCentre[0], t[0][1] - v->drawCentre[1], czp - ltr[k][i][1] * sz - v->drawCentre[2]);
        glEnd();
        glLineWidth(1);
    }
    glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING);
}
static void V3ReloadWorld(V3 *v);
// load the area beyond edge k in this window (same view, same choices)
static void V3GoArea(V3 *v, int k) {
    float nx, nz, t[3][3];
    if (v->thread || !V3ArrowTri(v, k, t, &nx, &nz)) return;
    float x = 0, z = 0, sz = 0; int ovl = 0;
    swscanf(v->geom + 12, L"%f,%f,%f,%d", &x, &z, &sz, &ovl);
    swprintf(v->geom, 2048, L"terrainarea:%g,%g,%g,%d", nx, nz, sz, v->ovlSel);
    const float *W = v->part.terrWorld;
    wchar_t *at = wcsstr(v->title, L" - area at "); if (at) *at = 0;
    size_t l = wcslen(v->title); swprintf(v->title + l, 512 - l, L" - area at x %.0f, z %.0f", (nx + sz / 2) * W[2], (nz + sz / 2) * W[2]);
    SetWindowTextW(v->wnd, v->title);
    v->arrowHot = -1;
    V3ReloadWorld(v);
}
// a small tip beside the cursor with the hovered probe's numbers (-1: hide); that dot is drawn larger and outlined
static void V3TipShow(V3 *v, int show, int mx, int my) {     // the tip by the cursor, with v->prbTipText
    if (!v->prbTip) {
        if (!show) return;
        v->prbTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0, v->wnd, NULL, GetModuleHandleW(NULL), NULL);
        if (!v->prbTip) return;
        TOOLINFOW ti; ZeroMemory(&ti, sizeof ti); ti.cbSize = sizeof ti; ti.uFlags = TTF_TRACK | TTF_ABSOLUTE; ti.hwnd = v->gl; ti.uId = 1; ti.lpszText = v->prbTipText;
        SendMessageW(v->prbTip, TTM_ADDTOOLW, 0, (LPARAM)&ti); SendMessageW(v->prbTip, TTM_SETMAXTIPWIDTH, 0, V3S(v, 600));
        SendMessageW(v->prbTip, WM_SETFONT, (WPARAM)v->font, FALSE);
    }
    TOOLINFOW ti; ZeroMemory(&ti, sizeof ti); ti.cbSize = sizeof ti; ti.hwnd = v->gl; ti.uId = 1;
    if (!show) { SendMessageW(v->prbTip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti); return; }
    ti.lpszText = v->prbTipText; SendMessageW(v->prbTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
    POINT pt = { mx + V3S(v, 18), my + V3S(v, 18) }; ClientToScreen(v->gl, &pt);
    SendMessageW(v->prbTip, TTM_TRACKPOSITION, 0, MAKELPARAM(pt.x, pt.y));
    SendMessageW(v->prbTip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
}
static void V3ProbeTip(V3 *v, int idx, int mx, int my) {
    if (idx != v->prbHot) { v->prbHot = idx; InvalidateRect(v->gl, NULL, FALSE); }
    if (idx < 0) { if (v->sndHot < 0) V3TipShow(v, 0, 0, 0); return; }
    const float *q = v->prb + (size_t)idx * V3_PRB_STRIDE;
    float half = v->part.terrWorld[3] * v->part.terrWorld[2] / 2;
    const wchar_t *kind = idx >= v->nprb - v->nprbFine ? L"finer grid probe" : L"probe";
    if (v->prbSel <= 8) { const float *c = q + 3 + (v->prbSel - 1) * 3;
        swprintf(v->prbTipText, 400, L"%ls at x %.1f, z %.1f, height %.1f\r\nvalues %d-%d: %.4g   %.4g   %.4g", kind, q[0] - half, q[2] - half, q[1], (v->prbSel - 1) * 3 + 1, (v->prbSel - 1) * 3 + 3, c[0], c[1], c[2]); }
    else swprintf(v->prbTipText, 400, L"%ls at x %.1f, z %.1f, height %.1f\r\nvalue %d = %.4g", kind, q[0] - half, q[2] - half, q[1], v->prbSel - 8, q[3 + v->prbSel - 9]);
    V3TipShow(v, 1, mx, my);
}
// the sound probe record under the cursor: all its numbers
static void V3SoundTip(V3 *v, int idx, int mx, int my) {
    if (idx != v->sndHot) { v->sndHot = idx; InvalidateRect(v->gl, NULL, FALSE); }
    if (idx < 0) { if (v->prbHot < 0) V3TipShow(v, 0, 0, 0); return; }
    const float *q = v->snd + (size_t)idx * V3_SND_STRIDE;
    float half = v->part.terrWorld[3] * v->part.terrWorld[2] / 2;
    wchar_t *t = v->prbTipText; size_t L = 400;
    int n = swprintf(t, L, L"sound probe at x %.1f, z %.1f\r\nheights %.1f - %.1f, surface type %.0f %.0f\r\nvalues %.4g  %.4g  %.4g  %.4g\r\ndistances",
                     q[0] - half, q[2] - half, q[3], q[4], q[5], q[6], q[7], q[8], q[9], q[10]);
    for (int k = 0; k < 12 && n > 0 && (size_t)n < L - 12; k++) n += swprintf(t + n, L - n, L" %.1f", q[11 + k]);
    if (n > 0 && (size_t)n < L - 20) { n += swprintf(t + n, L - n, L"\r\nenclosed"); for (int k = 0; k < 12 && (size_t)n < L - 6; k++) n += swprintf(t + n, L - n, L" %.0f", q[23 + k]); }
    V3TipShow(v, 1, mx, my);
}
// the cursor's place: terrain views - the ground under it, in the game's world coordinates (metres, 0 = the middle of
// the map) and its 125 m square (the .dlpg file names' X / Z); anything else - the surface under it (from the depth buffer)
static void V3UpdateCoord(V3 *v, int mx, int my, int inside) {
    wchar_t t[200] = L"";
    if (inside && v->haveMesh && v->partOk) {
        if (v->part.gridW > 1 && v->part.terrWorld[2] > 0) {
            float P[3];
            if (V3PickGround(v, mx, my, P)) {
                float half = v->part.terrWorld[3] * v->part.terrWorld[2] / 2, X = P[0] - half, Z = P[2] - half;
                swprintf(t, 200, L"x %.1f   z %.1f\r\nheight %.1f   square X%03d Z%03d", X, Z, P[1], (int)floorf((X + 8000) / 125), (int)floorf((Z + 8000) / 125));
            }
        } else if (v->rc) {
            RECT rc; GetClientRect(v->gl, &rc); int w = rc.right > 0 ? rc.right : 1, h = rc.bottom > 0 ? rc.bottom : 1;
            wglMakeCurrent(v->dc, v->rc); float d = 1; glReadPixels(mx, h - 1 - my, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &d);
            if (d < 1.0f && d > 0) {
                float nearp = v->dist * 0.02f; if (nearp < 0.05f) nearp = 0.05f;
                float farp = v->dist + v->radius * 4 + 10, fy = nearp * 0.45f, fx = fy * w / h;
                float zn = 2 * d - 1, ze = -(2 * farp * nearp) / ((farp + nearp) - zn * (farp - nearp));
                float q[3] = { (2.0f * (mx + 0.5f) / w - 1) * fx * -ze / nearp, (1 - 2.0f * (my + 0.5f) / h) * fy * -ze / nearp, ze };
                q[0] -= v->panx; q[1] -= v->pany; q[2] += v->dist;
                float cp = cosf(-v->pitch * 3.14159265f / 180), sp = sinf(-v->pitch * 3.14159265f / 180);
                float y1 = q[1] * cp - q[2] * sp, z1 = q[1] * sp + q[2] * cp; q[1] = y1; q[2] = z1;
                float cy = cosf(-v->yaw * 3.14159265f / 180), sy_ = sinf(-v->yaw * 3.14159265f / 180);
                float x2 = q[0] * cy + q[2] * sy_, z2 = -q[0] * sy_ + q[2] * cy; q[0] = x2; q[2] = z2;
                for (int i = 0; i < 3; i++) q[i] += v->drawCentre[i];
                swprintf(t, 200, L"x %.3f   y %.3f\r\nz %.3f", q[0], q[1], q[2]);
            }
        }
    }
    if (inside && v->nprb && v->prbSel > 0 && v->partOk && V3IsTerrainGrid(v)) {   // the light probe dot under the cursor: its actual numbers (a tip by the cursor)
        RECT gr; GetClientRect(v->gl, &gr);
        float dot = 6.0f * gr.bottom / (0.9f * (v->dist > 1 ? v->dist : 1)); if (dot > 48) dot = 48;
        int best = -1; float bd = fmaxf(10.0f * v->dpi / 96, dot / 2 + 4); bd *= bd;   // the nearest dot on screen, within 10 pixels (or the dot's size)
        for (int i = 0; i < v->nprb; i++) {
            const float *q = v->prb + (size_t)i * V3_PRB_STRIDE; float sx, sy;
            if (!V3Project(v, q, &sx, &sy)) continue;
            float d2 = (sx - mx) * (sx - mx) + (sy - my) * (sy - my);
            if (d2 < bd) { bd = d2; best = i; }
        }
        V3ProbeTip(v, best, mx, my);
    } else V3ProbeTip(v, -1, 0, 0);
    if (inside && v->nsnd && v->sndSel > 0 && v->partOk && V3IsTerrainGrid(v) && v->prbHot < 0) {
        RECT gr; GetClientRect(v->gl, &gr);
        float dot = 2.5f * gr.bottom / (0.9f * (v->dist > 1 ? v->dist : 1)); if (dot > 40) dot = 40;
        int best = -1; float bd = fmaxf(10.0f * v->dpi / 96, dot / 2 + 4); bd *= bd;
        for (int i = 0; i < v->nsnd; i++) {
            const float *q = v->snd + (size_t)i * V3_SND_STRIDE; float P[3] = { q[0], q[1] + 0.5f, q[2] }, sx, sy;
            if (!V3Project(v, P, &sx, &sy)) continue;
            float d2 = (sx - mx) * (sx - mx) + (sy - my) * (sy - my); if (d2 < bd) { bd = d2; best = i; }
        }
        V3SoundTip(v, best, mx, my);
    } else if (v->sndHot >= 0) V3SoundTip(v, -1, 0, 0);
    {   // a collectible under the cursor (P-switch, ? panel, medallion): what it is, its mission / sticker
        int best = -1;
        if (inside && v->ncinst && v->partOk && v->prbHot < 0 && v->sndHot < 0) {
            float bd = 14.0f * v->dpi / 96; bd *= bd;
            for (int i = 0; i < v->ncinst; i++) {
                const struct V3CInst *c = &v->cinst[i]; const struct V3CPt *q = &v->cpt[c->cpt];
                int mi = -1; for (int k = 0; k < v->ncoll; k++) if (v->coll[k].list == q->list) mi = v->coll[k].mat;
                if (mi < 0 || v->hidden[mi]) continue;
                float P[3] = { c->A[0], c->A[1] + c->h * V3CollS(v), c->A[2] }, sx, sy;
                if (!V3Project(v, P, &sx, &sy)) continue;
                float d2 = (sx - mx) * (sx - mx) + (sy - my) * (sy - my); if (d2 < bd) { bd = d2; best = i; }
            }
        }
        if (best >= 0) {
            const struct V3CPt *q = &v->cpt[v->cinst[best].cpt]; wchar_t *t2 = v->prbTipText; int n = 0;
            n += swprintf(t2 + n, 400 - n, L"%hs (%hs)  at x %.1f, z %.1f, height %.1f", v->clist[q->list], q->an, q->T[0], q->T[2], q->T[1]);
            if (q->island[0]) n += swprintf(t2 + n, 400 - n, L"\r\ninside %hs (this is its way in)", q->island);
            if (q->ms[0]) { wchar_t ml[600]; V3MissionLine(v, q->ms, ml, 600); n += swprintf(t2 + n, 400 - n, L"\r\n%.300ls", ml); }
            if (q->stk[0]) n += swprintf(t2 + n, 400 - n, L"\r\nsticker %hs", q->stk);
            v->cHot = best + 1; V3TipShow(v, 1, mx, my);
            const char *stk = q->stk[0] ? q->stk : q->ms[0] ? V3MissionSticker(v, q->ms) : NULL;   // (and its picture below the tip)
            V3StickerShow(v, stk);
        } else if (v->cHot) { v->cHot = 0; V3StickerShow(v, NULL); if (v->prbHot < 0 && v->sndHot < 0) V3TipShow(v, 0, 0, 0); }
    }
    wchar_t old[200]; GetWindowTextW(v->coord, old, 200);
    if (wcscmp(old, t)) SetWindowTextW(v->coord, t);
}

/* Right-click > "List placed objects near here": every actor in Banc\\World_<scene>\\*.bcett.byml.zs within 200 m
   (across the ground), nearest first, with its height and whether a model file for it exists - to find what a
   shape the viewer doesn't draw (but the probes outline) is. */
static void V3ShowText(V3 *v, const wchar_t *title, const wchar_t *text);
static void V3ListObjectsNear(V3 *v, float wx, float wz) {
    wchar_t root[2100]; swprintf(root, 2100, L"%ls", v->dir);
    if (!V3SceneRoot(root)) return;
    typedef struct { float d, x, y, z; char name[96]; wchar_t file[96]; int model; char kind[160]; wchar_t mpath[300]; char prim[24]; wchar_t extra[400]; } Hit;
    VoxPart *P = &v->part; float ws = P->terrWorld[2], half = P->terrWorld[3] * ws / 2;   // (the area shown, world coordinates)
    float bx0 = -1e30f, bz0 = -1e30f, bx1 = 1e30f, bz1 = 1e30f;
    if (P->terrArea[2] > 0) { bx0 = (P->terrArea[0] - P->terrWorld[0]) * ws - half; bz0 = (P->terrArea[1] - P->terrWorld[1]) * ws - half; bx1 = bx0 + P->terrArea[2] * ws; bz1 = bz0 + P->terrArea[2] * ws; }
    int cap = 4096, n = 0; Hit *h = (Hit *)malloc(cap * sizeof(Hit)); if (!h) return;
    wchar_t pat[2300]; swprintf(pat, 2300, L"%ls\\Banc\\World_*", root);
    WIN32_FIND_DATAW dd; HANDLE dh = FindFirstFileW(pat, &dd);
    HCURSOR oc = SetCursor(LoadCursor(NULL, IDC_WAIT));
    int files = 0;
    if (dh != INVALID_HANDLE_VALUE) do {
        if (!(dd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        wchar_t fp[2300]; swprintf(fp, 2300, L"%ls\\Banc\\%ls\\*.bcett.byml.zs", root, dd.cFileName);
        WIN32_FIND_DATAW fd; HANDLE fh = FindFirstFileW(fp, &fd); if (fh == INVALID_HANDLE_VALUE) continue;
        do {
            wchar_t path[2400]; swprintf(path, 2400, L"%ls\\Banc\\%ls\\%ls", root, dd.cFileName, fd.cFileName);
            size_t zn; unsigned char *z = VoxReadFile(path, &zn); if (!z) continue;
            size_t bn; unsigned char *b = VoxUnzstd(z, zn, &bn); free(z); if (!b) continue;
            files++;
            Byml y; BymlNode r = BymlOpen(&y, b, bn), acts = BymlGet(&y, r, "Actors");
            for (int i = 0; i < BymlCount(&y, acts); i++) {
                BymlNode a = BymlAt(&y, acts, i);
                const char *nm = BymlStr(&y, BymlGet(&y, a, "Name")); if (!nm) nm = BymlStr(&y, BymlGet(&y, a, "Gyaml")); if (!nm) continue;
                char clean[96]; { const char *sl = strrchr(nm, '/'); snprintf(clean, sizeof clean, "%s", sl ? sl + 1 : nm); char *g = strstr(clean, ".gear__"); if (g) *g = 0; } nm = clean;
                BymlNode t = BymlGet(&y, a, "Translate");
                float x = BymlFloat(BymlAt(&y, t, 0), 0), yy = BymlFloat(BymlAt(&y, t, 1), 0), zz = BymlFloat(BymlAt(&y, t, 2), 0);
                float d = sqrtf((x - wx) * (x - wx) + (zz - wz) * (zz - wz)); if (d > 200) continue;
                if (n == cap) { Hit *nh = (Hit *)realloc(h, (size_t)cap * 2 * sizeof(Hit)); if (!nh) break; h = nh; cap *= 2; }
                Hit *o = h + n++; o->d = d; o->x = x; o->y = yy; o->z = zz; snprintf(o->name, sizeof o->name, "%s", nm);
                {   const char *pr = BymlStr(&y, BymlGet(&y, a, "PrimitiveType")); snprintf(o->prim, sizeof o->prim, "%s", pr ? pr : ""); }   // (an area's / point's shape)
                o->extra[0] = 0;
                {   int eo = 0;                                     // in one of the game's collectible lists? its mission / sticker
                    for (int c = 0; c < v->ncpt; c++) if (fabsf(v->cpt[c].T[0] - x) < 0.05f && fabsf(v->cpt[c].T[2] - zz) < 0.05f && fabsf(v->cpt[c].T[1] - yy) < 3.0f) {
                        eo += swprintf(o->extra + eo, 400 - eo, L"   [list: %hs]", v->clist[v->cpt[c].list]);
                        if (v->cpt[c].stk[0]) eo += swprintf(o->extra + eo, 400 - eo, L" [sticker %hs]", v->cpt[c].stk);
                        break;
                    }
                    const char *wt = BymlStr(&y, BymlGet(&y, BymlGet(&y, a, "Dynamic"), "WTAType"));
                    if (wt && *wt && eo < 300) {
                        char ms[64]; const char *sl = strrchr(wt, '/'); snprintf(ms, sizeof ms, "%s", sl ? sl + 1 : wt); char *d = strchr(ms, '.'); if (d) *d = 0;
                        wchar_t ml[600]; V3MissionLine(v, ms, ml, 600); swprintf(o->extra + eo, 400 - eo, L"   [%ls]", ml);
                    }
                }
                swprintf(o->file, 96, L"%ls\\%ls", dd.cFileName, fd.cFileName); { wchar_t *e = wcsstr(o->file, L".bcett"); if (e) *e = 0; }
                // what this view did with it (the loader's record per name; see V3Name)
                {   wchar_t bf[2][200]; int nbf = V3BancFolders(v, bf), ours = 0; for (int q = 0; q < nbf; q++) if (!_wcsicmp(dd.cFileName, bf[q])) ours = 1;
                    V3Name *e = ours ? V3NameGet(v, nm, 0) : NULL;
                    int shA; char ab[40];
                    if (!ours) snprintf(o->kind, sizeof o->kind, "[not drawn: the view doesn't read the %ls folder]", dd.cFileName);
                    else if ((x < bx0 || x > bx1 || zz < bz0 || zz > bz1) && !V3AreaLabel(nm, &shA, ab)) snprintf(o->kind, sizeof o->kind, "[outside this area]");
                    else if (e && e->ent[0]) snprintf(o->kind, sizeof o->kind, "[drawn: %s]", e->ent);
                    else if (e && e->why[0]) snprintf(o->kind, sizeof o->kind, "[not drawn: %s]", e->why);
                    else snprintf(o->kind, sizeof o->kind, "[not drawn: not handled by the view]");
                }
                wchar_t mp[2400]; o->model = V3ModelPath(v, root, nm, 0, mp, 2400);   // (the game's settings first, then by name)
                swprintf(o->mpath, 300, L"%ls", o->model ? mp : L"");
            }
            free(b);
        } while (FindNextFileW(fh, &fd));
        FindClose(fh);
    } while (FindNextFileW(dh, &dd));
    if (dh != INVALID_HANDLE_VALUE) FindClose(dh);
    for (int c = 0; c < v->ncpt; c++) {                     // collectibles inside an island whose way in is near here
        const struct V3CPt *q = &v->cpt[c]; if (!q->island[0]) continue;
        float d = sqrtf((q->T[0] - wx) * (q->T[0] - wx) + (q->T[2] - wz) * (q->T[2] - wz)); if (d > 200) continue;
        if (n == cap) { Hit *nh = (Hit *)realloc(h, (size_t)cap * 2 * sizeof(Hit)); if (!nh) break; h = nh; cap *= 2; }
        Hit *o = h + n++; memset(o, 0, sizeof *o); o->d = d; o->x = q->T[0]; o->y = q->T[1]; o->z = q->T[2];
        snprintf(o->name, sizeof o->name, "%s", q->inm); swprintf(o->file, 96, L"%hs", q->island);
        snprintf(o->kind, sizeof o->kind, "[drawn: Marker: %s, at the island's way in]", v->clist[q->list]);
        int eo = swprintf(o->extra, 400, L"   [list: %hs] [inside %hs]", v->clist[q->list], q->island);
        if (q->ms[0]) { wchar_t ml[600]; V3MissionLine(v, q->ms, ml, 600); swprintf(o->extra + eo, 400 - eo, L"   [%ls]", ml); }
        o->model = 1;
    }
    SetCursor(oc);
    for (int i = 1; i < n; i++) { Hit t = h[i]; int j = i - 1; while (j >= 0 && h[j].d > t.d) { h[j + 1] = h[j]; j--; } h[j + 1] = t; }   // nearest first
    // can the view read each model? (tried once per model file, for the first 1500 shown)
    wchar_t (*seen)[300] = (wchar_t (*)[300])calloc(400, sizeof *seen); int *seenOk = (int *)calloc(400, sizeof(int)), nseen = 0;
    int *rd = (int *)calloc((size_t)(n ? n : 1), sizeof(int));     // 0 unknown, 1 reads, -1 can't be read
    for (int i = 0; i < n && i < 1500 && seen && seenOk && rd; i++) {
        if (!h[i].model || !h[i].mpath[0]) continue;
        int q = 0; while (q < nseen && wcscmp(seen[q], h[i].mpath)) q++;
        if (q == nseen) {
            if (nseen >= 400) continue;
            VoxPart *m = (VoxPart *)calloc(1, sizeof(VoxPart)); int ok = m && VoxLoadBfres(m, h[i].mpath) && m->nti;
            if (m) { VoxFreePart(m); free(m); }
            wcscpy(seen[q], h[i].mpath); seenOk[q] = ok ? 1 : -1; nseen++;
        }
        rd[i] = seenOk[q];
    }
    size_t cap2 = 600 + (size_t)n * 960; wchar_t *txt = (wchar_t *)malloc(cap2 * sizeof(wchar_t)); if (!txt) { free(h); return; }
    size_t o = (size_t)swprintf(txt, cap2, L"Placed objects within 200 m of x %.1f, z %.1f (%d placement files read). Nearest first:\r\n"
                                L"distance   position x, y (height), z   name   (folder\\file)   [model file found / can't be read / no model]   [drawn: list entry] or [not drawn: why]\r\n\r\n", wx, wz, files);
    for (int i = 0; i < n && i < 1500; i++)
        o += (size_t)swprintf(txt + o, cap2 - o, L"%6.0f m   %.1f, %.1f, %.1f   %hs%hs%hs%hs   (%ls)%ls   %hs%ls\r\n", h[i].d, h[i].x, h[i].y, h[i].z, h[i].name,
                              h[i].prim[0] ? "   [shape: " : "", h[i].prim, h[i].prim[0] ? "]" : "", h[i].file,
                              h[i].model ? (rd && rd[i] < 0 ? L"   [model, can't be read]" : L"   [model]") : L"   [no model]", h[i].kind, h[i].extra);
    if (!n) swprintf(txt + o, cap2 - o, L"(none)");
    wchar_t tt[120]; swprintf(tt, 120, L"Objects near x %.0f, z %.0f", wx, wz);
    V3ShowText(v, tt, txt);
    free(txt); free(h); free(seen); free(seenOk); free(rd);
}

static LRESULT CALLBACK V3TextProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_SIZE) { HWND e = GetDlgItem(h, 1); if (e) MoveWindow(e, 0, 0, LOWORD(lp), HIWORD(lp), TRUE); return 0; }
    return DefWindowProcW(h, msg, wp, lp);
}
/* the Controls window (not modal: it can stay open beside the view) */
static LRESULT CALLBACK V3CtlProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND: if (LOWORD(wp) == IDCANCEL || LOWORD(wp) == IDOK) { DestroyWindow(h); return 0; } break;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: { V3 *v = (V3 *)GetWindowLongPtrW(h, GWLP_USERDATA); if (v && v->ctlWnd == h) v->ctlWnd = NULL; return 0; }
    }
    return DefWindowProcW(h, msg, wp, lp);
}
void UiSkinWindow(HWND top);
// the text window's box: Ctrl+A selects everything (a multi-line edit box doesn't by itself)
static LRESULT CALLBACK V3TextEditSub(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    if (msg == WM_KEYDOWN && wp == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) { SendMessageW(h, EM_SETSEL, 0, -1); return 0; }
    if (msg == WM_CHAR && wp == 1) return 0;                 // (Ctrl+A's character: no beep)
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, V3TextEditSub, 32);
    return DefSubclassProc(h, msg, wp, lp);
}
static void V3ShowText(V3 *v, const wchar_t *title, const wchar_t *text) {   // a resizable, non-modal window with read-only text
    static int reg = 0;
    if (!reg) {
        WNDCLASSEXW wc; ZeroMemory(&wc, sizeof wc); wc.cbSize = sizeof wc; wc.lpfnWndProc = V3TextProc; wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = CreateSolidBrush(UI_BG); wc.lpszClassName = L"AlsarcVox3DText";
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1)); RegisterClassExW(&wc); reg = 1;
    }
    RECT r; GetWindowRect(v->wnd, &r);
    HWND h = CreateWindowExW(0, L"AlsarcVox3DText", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, r.left + V3S(v, 60), r.top + V3S(v, 60), V3S(v, 760), V3S(v, 560), v->wnd, NULL, GetModuleHandleW(NULL), NULL);
    if (!h) return;
    HWND e = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_HSCROLL | ES_AUTOVSCROLL | ES_AUTOHSCROLL, 0, 0, 10, 10, h, (HMENU)1, NULL, NULL);
    if (e) SetWindowSubclass(e, V3TextEditSub, 32, 0);
    SendMessageW(e, WM_SETFONT, (WPARAM)v->font, TRUE); SendMessageW(e, EM_SETLIMITTEXT, 0, 0);
    SetWindowTextW(e, text);
    UiSkinWindow(h);
    RECT cr; GetClientRect(h, &cr); MoveWindow(e, 0, 0, cr.right, cr.bottom, TRUE);
}
static void V3ShowControls(V3 *v) {
    if (v->ctlWnd) { SetForegroundWindow(v->ctlWnd); return; }
    static int reg = 0;
    if (!reg) {
        WNDCLASSEXW wc; ZeroMemory(&wc, sizeof wc); wc.cbSize = sizeof wc; wc.lpfnWndProc = V3CtlProc; wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = CreateSolidBrush(UI_BG); wc.lpszClassName = L"AlsarcVox3DControls";
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1)); RegisterClassExW(&wc); reg = 1;
    }
    const wchar_t *txt =
        L"Mouse\r\n"
        L"   Left-drag\tturn the view around\r\n"
        L"   Right-drag or middle-drag\tmove the view\r\n"
        L"   Wheel\tzoom in and out\r\n"
        L"   Double-click\treset the view\r\n"
        L"   Right-click the ground\t(terrain maps) open that area in full\r\n\tdetail, copy the coordinates, or list the\r\n\tplaced objects near there\r\n"
        L"   Click an edge arrow\t(full-detail areas) move to the next area\r\n"
        L"\r\nKeys (click the view first)\r\n"
        L"   R or Home\treset the view\r\n"
        L"   T\tlook straight down (top view)\r\n"
        L"\r\nBelow the Controls button: the place under the cursor. On terrain maps: the\r\n"
        L"game's world coordinates in metres (0 = the middle of the map), the height,\r\n"
        L"and the 125 m square (the X / Z in the light probe file names).";
    int W = V3S(v, 520), H = V3S(v, 380);
    RECT r; GetWindowRect(v->wnd, &r);
    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW, L"AlsarcVox3DControls", L"3D view controls", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                             r.left + V3S(v, 40), r.bottom - H - V3S(v, 60), W, H, v->wnd, NULL, GetModuleHandleW(NULL), NULL);
    if (!h) return;
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)v);
    RECT cr; GetClientRect(h, &cr); int m = V3S(v, 14);
    HWND st = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, m, m, cr.right - 2 * m, cr.bottom - 3 * m - V3S(v, 30), h, NULL, NULL, NULL);
    SendMessageW(st, WM_SETFONT, (WPARAM)v->font, TRUE);
    {   // tab stops for the two columns: a STATIC doesn't expand tabs to a set position, so lay the text out with spaces
        HDC dc = GetDC(st); HGDIOBJ of = SelectObject(dc, v->font);
        wchar_t out[3000]; size_t o = 0; const wchar_t *s2 = txt; int col = V3S(v, 200);
        SIZE sp; GetTextExtentPoint32W(dc, L" ", 1, &sp); if (sp.cx < 1) sp.cx = 4;
        while (*s2 && o < 2900) {
            const wchar_t *e = wcsstr(s2, L"\r\n"); size_t ln = e ? (size_t)(e - s2) : wcslen(s2);
            const wchar_t *tab = wmemchr(s2, L'\t', ln);
            if (tab) {
                size_t a = (size_t)(tab - s2); wmemcpy(out + o, s2, a); o += a;
                SIZE ts; GetTextExtentPoint32W(dc, s2, (int)a, &ts);
                int pad = (col - ts.cx) / sp.cx; if (pad < 2) pad = 2; for (int i = 0; i < pad && o < 2900; i++) out[o++] = L' ';
                wmemcpy(out + o, tab + 1, ln - a - 1); o += ln - a - 1;
            } else { wmemcpy(out + o, s2, ln); o += ln; }
            if (e) { out[o++] = L'\r'; out[o++] = L'\n'; s2 = e + 2; } else break;
        }
        out[o] = 0;
        RECT need = { 0, 0, 2000, 0 }; DrawTextW(dc, out, -1, &need, DT_CALCRECT | DT_NOPREFIX | DT_LEFT);      // size the window to the text
        SelectObject(dc, of); ReleaseDC(st, dc); SetWindowTextW(st, out);
        int cw = need.right + 2 * m + V3S(v, 8), ch = need.bottom + 3 * m + V3S(v, 30) + V3S(v, 6);
        RECT wr = { 0, 0, cw, ch }; AdjustWindowRectEx(&wr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_TOOLWINDOW);
        int ww = wr.right - wr.left, wh = wr.bottom - wr.top, top = r.bottom - wh - V3S(v, 60); if (top < r.top) top = r.top;
        SetWindowPos(h, NULL, r.left + V3S(v, 40), top, ww, wh, SWP_NOZORDER | SWP_NOACTIVATE);
        GetClientRect(h, &cr);
        MoveWindow(st, m, m, cr.right - 2 * m, need.bottom + V3S(v, 6), FALSE);
    }
    HWND b = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, cr.right - m - V3S(v, 100), cr.bottom - m - V3S(v, 30), V3S(v, 100), V3S(v, 30), h, (HMENU)IDCANCEL, NULL, NULL);
    SendMessageW(b, WM_SETFONT, (WPARAM)v->font, TRUE);
    UiSkinWindow(h);
    v->ctlWnd = h;
}

static void V3TerrainMenu(V3 *v, int mx, int my) {
    float P[3];
    int hit = V3PickGround(v, mx, my, P);
    const float *W = v->part.terrWorld;
    HMENU m = CreatePopupMenu();
    wchar_t t[200];
    if (hit) swprintf(t, 200, L"Open this area in full detail (%.0f \x00D7 %.0f units around here)", V3_AREA * W[2], V3_AREA * W[2]);
    AppendMenuW(m, MF_STRING | (hit ? 0 : MF_GRAYED), 1, hit ? t : L"Right-click the ground to open that area in full detail");
    wchar_t ct[160] = L"";
    if (hit) {
        float half = v->part.terrWorld[3] * v->part.terrWorld[2] / 2;
        swprintf(ct, 160, L"x %.1f, z %.1f, height %.1f", P[0] - half, P[2] - half, P[1]);
        wchar_t mt[200]; swprintf(mt, 200, L"Copy coordinates (%ls)", ct); AppendMenuW(m, MF_STRING, 2, mt);
        AppendMenuW(m, MF_STRING, 3, L"List placed objects near here (200 m)...");
    }
    if (v->ncinst) {                                     // the collectibles' size (from their lowest point, so they stay on the ground)
        static const float sizes[] = { 1, 2, 5, 10, 20, 50, 100 };
        HMENU sm = CreatePopupMenu();
        for (int i = 0; i < 7; i++) { wchar_t st[40]; swprintf(st, 40, i ? L"%g \x00D7" : L"Real size", sizes[i]);
            AppendMenuW(sm, MF_STRING | (V3CollS(v) == sizes[i] ? MF_CHECKED : 0) | (v->thread ? MF_GRAYED : 0), 20 + i, st); }
        AppendMenuW(m, MF_POPUP, (UINT_PTR)sm, L"Collectible size (P-switches, ? panels, medallions)");
    }
    POINT pt = { mx, my }; ClientToScreen(v->gl, &pt);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, v->wnd, NULL);
    DestroyMenu(m);
    if (cmd >= 20 && cmd < 27 && !v->thread) {
        static const float sizes[] = { 1, 2, 5, 10, 20, 50, 100 };
        v->collScale = sizes[cmd - 20]; V3CollApplyScale(v);
        for (int k = 0; k < v->ncoll; k++) { char *t3 = strstr(v->coll[k].txt, "\nshown "); if (t3) *t3 = 0;
            if (V3CollS(v) != 1) { size_t L = strlen(v->coll[k].txt); snprintf(v->coll[k].txt + L, sizeof v->coll[k].txt - L, "\nshown %g x their size", V3CollS(v)); } }
        V3RequestBuild(v); return;
    }
    if (cmd == 2 && OpenClipboard(v->wnd)) {
        EmptyClipboard(); size_t n = (wcslen(ct) + 1) * sizeof(wchar_t);
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, n); if (g) { memcpy(GlobalLock(g), ct, n); GlobalUnlock(g); SetClipboardData(CF_UNICODETEXT, g); }
        CloseClipboard(); return;
    }
    if (cmd == 3 && hit && v->loading) { MessageBoxW(v->wnd, L"The view is still loading - try again when it has finished.", L"Objects near here", MB_OK | MB_ICONINFORMATION); return; }
    if (cmd == 3 && hit) { float half = v->part.terrWorld[3] * v->part.terrWorld[2] / 2; V3ListObjectsNear(v, P[0] - half, P[2] - half); return; }
    if (cmd != 1 || !hit || !(W[2] > 0)) return;
    // the spot in .tscb units; the area snapped to the smallest tiles' grid (0.5) so their samples line up exactly
    float cx = W[0] + P[0] / W[2], cz = W[1] + P[2] / W[2];
    float ax = W[0] + roundf((cx - V3_AREA / 2 - W[0]) * 2) / 2, az = W[1] + roundf((cz - V3_AREA / 2 - W[1]) * 2) / 2;
    if (ax < W[0]) ax = W[0]; if (az < W[1]) az = W[1];
    if (ax + V3_AREA > W[0] + W[3]) ax = W[0] + W[3] - V3_AREA; if (az + V3_AREA > W[1] + W[3]) az = W[1] + W[3] - V3_AREA;
    wchar_t geom[120]; swprintf(geom, 120, L"terrainarea:%g,%g,%g,%d", ax, az, V3_AREA, v->ovlSel);   // (same ground colouring)
    wchar_t title[600]; const wchar_t *base = wcsncmp(v->title, L"3D view - ", 10) ? v->title : v->title + 10;
    wchar_t b2[512]; swprintf(b2, 512, L"%ls", base); wchar_t *at = wcsstr(b2, L" - area at "); if (at) *at = 0;
    swprintf(title, 600, L"%ls - area at x %.0f, z %.0f", b2, (ax + V3_AREA / 2) * W[2], (az + V3_AREA / 2) * W[2]);
    g_v3Place.length = sizeof g_v3Place;                 // the area's window: the same size as this one (maximised if it is)
    g_v3PlaceSet = GetWindowPlacement(v->wnd, &g_v3Place);
    Vox3DOpen(v->owner, v->dir, geom, title, v->font, v->dpi);
    g_v3PlaceSet = 0;
}

/* ------------------------------------------------------------------ GL child window: mouse and paint */
static LRESULT CALLBACK V3GLProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    V3 *v = (V3 *)GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA);
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hwnd, &ps); if (v && v->rc) V3Render(v); EndPaint(hwnd, &ps); return 0; }
    case WM_SIZE: InvalidateRect(hwnd, NULL, FALSE); return 0;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
        if (!v) break;
        v->rdown.x = (short)LOWORD(lp); v->rdown.y = (short)HIWORD(lp); v->rmoved = 0; V3ProbeTip(v, -1, 0, 0); V3SoundTip(v, -1, 0, 0);          // (a click: no movement till the button is let go)
        SetFocus(hwnd); SetCapture(hwnd);
        v->dragging = msg == WM_LBUTTONDOWN ? 1 : 2;
        v->last.x = (short)LOWORD(lp); v->last.y = (short)HIWORD(lp);
        return 0;
    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP:
        if (v) v->dragging = 0;
        ReleaseCapture();
        if (v && msg == WM_RBUTTONUP && !v->rmoved && v->partOk && v->haveMesh && v->part.gridW > 1) V3TerrainMenu(v, (short)LOWORD(lp), (short)HIWORD(lp));
        if (v && msg == WM_LBUTTONUP && !v->rmoved && v->haveMesh) { int k = V3ArrowAt(v, (short)LOWORD(lp), (short)HIWORD(lp)); if (k >= 0) V3GoArea(v, k); }
        return 0;
    case WM_SETCURSOR:
        if (v && LOWORD(lp) == HTCLIENT && v->arrowHot >= 0) { SetCursor(LoadCursor(NULL, IDC_HAND)); return TRUE; }
        break;
    case WM_MOUSELEAVE: if (v) { v->tracking = 0; V3UpdateCoord(v, 0, 0, 0); if (v->arrowHot >= 0) { v->arrowHot = -1; InvalidateRect(hwnd, NULL, FALSE); } } return 0;
    case WM_LBUTTONDBLCLK: if (v) { V3ResetView(v); InvalidateRect(hwnd, NULL, FALSE); } return 0;
    case WM_MOUSEMOVE:
        if (v && v->dragging) {
            int x = (short)LOWORD(lp), y = (short)HIWORD(lp), dx = x - v->last.x, dy = y - v->last.y;
            if (abs(x - v->rdown.x) > 3 || abs(y - v->rdown.y) > 3) v->rmoved = 1;
            v->last.x = x; v->last.y = y;
            if (v->dragging == 1) {
                v->yaw += dx * 0.4f; v->pitch += dy * 0.4f;
                if (v->pitch > 89) v->pitch = 89; if (v->pitch < -89) v->pitch = -89;
            } else {
                RECT rc; GetClientRect(hwnd, &rc);
                float per = v->dist * 0.9f / (rc.bottom > 1 ? rc.bottom : 1);
                v->panx += dx * per; v->pany -= dy * per;
            }
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (v) {
            int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
            if (!v->tracking) { TRACKMOUSEEVENT te = { sizeof te, TME_LEAVE, hwnd, 0 }; TrackMouseEvent(&te); v->tracking = 1; }
            if (x != v->hoverPt.x || y != v->hoverPt.y) {
                v->hoverPt.x = x; v->hoverPt.y = y;
                V3UpdateCoord(v, x, y, 1);
                int k = v->haveMesh ? V3ArrowAt(v, x, y) : -1;
                if (k != v->arrowHot) { v->arrowHot = k; InvalidateRect(hwnd, NULL, FALSE); }
            }
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (v) {
            int d = (short)HIWORD(wp);
            v->dist *= powf(0.88f, d / 120.f);
            if (v->dist < v->radius * 0.05f) v->dist = v->radius * 0.05f;
            if (v->dist > v->radius * 20) v->dist = v->radius * 20;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if (v && (wp == 'R' || wp == VK_HOME)) { V3ResetView(v); InvalidateRect(hwnd, NULL, FALSE); }
        if (v && wp == 'T' && v->haveMesh) { V3TopView(v); InvalidateRect(hwnd, NULL, FALSE); }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ material list */
static HBITMAP V3Swatch(int w, int h, const unsigned char rgb[3]) {
    BITMAPINFO bi; ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP bm = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bm) return NULL;
    DWORD *px = (DWORD *)bits;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int edge = x == 0 || y == 0 || x == w - 1 || y == h - 1;
        px[y * w + x] = 0xFF000000u | (edge ? 0x404040u : ((DWORD)rgb[0] << 16) | ((DWORD)rgb[1] << 8) | rgb[2]);
    }
    return bm;
}

// which model a part comes from: 0 = the opened model, k + 1 = attachment k (head, ties)
static int V3PartGroup(V3 *v, int id) { for (int a = 0; a < v->nAtt; a++) if (id >= v->att[a].partLo && id < v->att[a].partHi) return a + 1; return 0; }
static V3 *g_sortV;
static int V3CmpParts(const void *x, const void *y) {
    V3 *v = g_sortV; int a = *(const int *)x, b = *(const int *)y, r = 0;
    const char *na = a < v->part.nmat ? v->part.matName[a] : "", *nb = b < v->part.nmat ? v->part.matName[b] : "";
    switch (v->sortMode) {
    case 0: r = v->part.matCount[a] < v->part.matCount[b] ? 1 : v->part.matCount[a] > v->part.matCount[b] ? -1 : 0; break;   // most first
    case 1: r = _stricmp(na, nb); break;
    case 2: r = a - b; break;
    case 3: r = V3PartGroup(v, a) - V3PartGroup(v, b); if (!r) r = _stricmp(na, nb); break;
    case 4: r = (int)v->hidden[a] - (int)v->hidden[b]; if (!r) r = _stricmp(na, nb); break;
    }
    if (!r) r = a - b;
    return v->sortDesc ? -r : r;
}
// a count in at most 4 characters: 9999, then 10K ... 999K, then 1.0M ... 9.9M, 10M ...
static void V3Short(wchar_t *out, size_t n, double x) {
    if (x < 10000) swprintf(out, n, L"%.0f", x);
    else if (x < 999500) swprintf(out, n, L"%.0fK", floor(x / 1000));
    else if (x < 9.95e6) swprintf(out, n, L"%.1fM", floor(x / 1e5) / 10);
    else if (x < 9.995e8) swprintf(out, n, L"%.0fM", floor(x / 1e6));
    else swprintf(out, n, L"%.1fB", floor(x / 1e8) / 10);
}
// the group a list entry belongs to: the text before ": " ("Props: Coin" -> "Props"); terrain ground materials ->
// "Ground"; otherwise its own name (so "Lava" joins "Lava: walls")
static void V3MatCat(V3 *v, int id, char out[VOX_MATN]) {
    const char *n = id < v->part.nmat ? v->part.matName[id] : "";
    const char *c = strstr(n, ": ");
    if (c && c > n) { snprintf(out, VOX_MATN, "%.*s", (int)(c - n), n); return; }
    if (V3IsTerrainGrid(v) && v->part.terrMat[id] >= 0) { snprintf(out, VOX_MATN, "Ground"); return; }
    snprintf(out, VOX_MATN, "%s", n);
}
static int V3IsExpanded(V3 *v, const char *g) { for (int i = 0; i < v->nexp; i++) if (!strcmp(v->expName[i], g)) return 1; return 0; }
static void V3SetExpanded(V3 *v, const char *g, int on) {
    for (int i = 0; i < v->nexp; i++) if (!strcmp(v->expName[i], g)) { if (!on) { v->expName[i][0] = 0; memmove(v->expName[i], v->expName[i + 1], (size_t)(v->nexp - i - 1) * VOX_MATN); v->nexp--; } return; }
    if (!on) return;
    if (v->nexp == v->capexp) { int c = v->capexp ? v->capexp * 2 : 32; char (*nn)[VOX_MATN] = (char (*)[VOX_MATN])realloc(v->expName, (size_t)c * VOX_MATN); if (!nn) return; v->expName = nn; v->capexp = c; }
    snprintf(v->expName[v->nexp++], VOX_MATN, "%s", g);
}
// a group's tick: 2 all shown, 1 none, 3 some (the faded tick)
static int V3GroupState(V3 *v, int g) {
    int on = 0; for (int k = 0; k < v->grpN[g]; k++) if (!v->hidden[v->grpIds[v->grpStart[g] + k]]) on++;
    return on == v->grpN[g] ? 2 : on ? 3 : 1;
}
static void V3SetRowState(V3 *v, int row, int s) { if (row >= 0) ListView_SetItemState(v->list, row, INDEXTOSTATEIMAGEMASK(s == 2 ? 2 : 1), LVIS_STATEIMAGEMASK); }
// a group's row: its tick from its entries (some shown: an unticked box with a square drawn in it, see V3ListDraw)
static void V3SetGroupRow(V3 *v, int g) {
    if (g < 0 || v->grpRow[g] < 0) return;
    int s = V3GroupState(v, g), was = v->grpSome[g];
    v->grpSome[g] = (unsigned char)(s == 3);
    V3SetRowState(v, v->grpRow[g], s);
    if (was != v->grpSome[g]) ListView_RedrawItems(v->list, v->grpRow[g], v->grpRow[g]);
}
// the list's own drawing: the "some shown" square in a group's box
static LRESULT V3ListDraw(V3 *v, LPARAM lp) {
    NMLVCUSTOMDRAW *cd = (NMLVCUSTOMDRAW *)lp;
    if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    int row = (int)cd->nmcd.dwItemSpec;
    if (row < 0 || row >= v->nlist || v->listMat[row] >= 0 || !v->grpSome[-1 - v->listMat[row]]) return CDRF_DODEFAULT;
    if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) return CDRF_NOTIFYPOSTPAINT;
    if (cd->nmcd.dwDrawStage == CDDS_ITEMPOSTPAINT) {
        RECT ic; if (!ListView_GetItemRect(v->list, row, &ic, LVIR_ICON)) return CDRF_DODEFAULT;
        int sw = 16, sh = 16; HIMAGELIST st = ListView_GetImageList(v->list, LVSIL_STATE); if (st) ImageList_GetIconSize(st, &sw, &sh);
        int bx = ic.left - sw, by = ic.top + (ic.bottom - ic.top - sh) / 2;       // the tick box sits just left of the swatch
        int q = sw * 5 / 16, x0 = bx + (sw - q * 2 + 1) / 2, y0 = by + (sh - q * 2 + 1) / 2;
        // like a ticked box (coral) with a dark square in place of the tick
        HDC dc = cd->nmcd.hdc; HPEN pn = (HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
        HBRUSH b = CreateSolidBrush(UI_ACC); HBRUSH ob = (HBRUSH)SelectObject(dc, b);
        RoundRect(dc, bx, by, bx + sw + 1, by + sh + 1, V3S(v, 8), V3S(v, 8));
        HBRUSH b2 = CreateSolidBrush(UI_ACC_INK); SelectObject(dc, b2);
        RoundRect(dc, x0, y0, x0 + q * 2 + 1, y0 + q * 2 + 1, V3S(v, 3), V3S(v, 3));
        SelectObject(dc, ob); SelectObject(dc, pn); DeleteObject(b); DeleteObject(b2);
    }
    return CDRF_DODEFAULT;
}
/* The list as a tree: entries sharing a group ("Object: Coin000_A", "Object: StreetLight000_A"...) sit under one row for the
   group - its tick shows / hides them all (faded when only some are shown), its arrow column opens / closes it.
   An entry alone in its group is a row of its own. The chosen order decides where each group sits (its first entry). */
static void V3FillList(V3 *v) {
    v->fillingList = 1;
    int top = ListView_GetTopIndex(v->list);
    ListView_DeleteAllItems(v->list);
    int sw = V3S(v, 16);
    HIMAGELIST il = ImageList_Create(sw, sw, ILC_COLOR32, 32, 32);
    { HIMAGELIST old0 = ListView_SetImageList(v->list, il, LVSIL_SMALL); if (old0 && old0 != il) ImageList_Destroy(old0); }   // (before the rows: indents line up)
    // materials present, filtered by the Find box, in the chosen order
    wchar_t fw[100] = L""; char fa[100] = ""; if (v->findEd) { GetWindowTextW(v->findEd, fw, 100); WideCharToMultiByte(CP_UTF8, 0, fw, -1, fa, sizeof fa, NULL, NULL); }
    for (char *q = fa; *q; q++) *q = (char)tolower((unsigned char)*q);
    static int ids[VOX_MAXMAT]; int n = 0;
    for (int i = 0; i < v->part.nmat && i < VOX_MAXMAT; i++) if (v->part.matCount[i]) {
        if (fa[0]) {
            char nm[64]; snprintf(nm, sizeof nm, "%d %s", i, v->part.matName[i]);
            for (char *q = nm; *q; q++) *q = (char)tolower((unsigned char)*q);
            if (!strstr(nm, fa)) continue;
        }
        ids[n++] = i;
    }
    g_sortV = v; qsort(ids, n, sizeof(int), V3CmpParts);
    // groups, in order of their first entry
    static int gOf[VOX_MAXMAT];
    v->ngrp = 0;
    for (int i = 0; i < VOX_MAXMAT; i++) { v->grpOfMat[i] = -1; v->matRow[i] = -1; }
    for (int i = 0; i < n; i++) {
        char c[VOX_MATN]; V3MatCat(v, ids[i], c);
        int g = 0; while (g < v->ngrp && strcmp(v->grpName[g], c)) g++;
        if (g == v->ngrp) { snprintf(v->grpName[g], VOX_MATN, "%s", c); v->grpN[g] = 0; v->ngrp++; }
        gOf[i] = g; v->grpN[g]++;
    }
    for (int g = 0, s = 0; g < v->ngrp; g++) { v->grpStart[g] = s; s += v->grpN[g]; v->grpN[g] = 0; v->grpRow[g] = -1; }
    for (int i = 0; i < n; i++) { int g = gOf[i]; v->grpIds[v->grpStart[g] + v->grpN[g]++] = ids[i]; v->grpOfMat[ids[i]] = (short)g; }
    // rows: a group of 2 or more -> its row (+ its entries when open, or while finding); otherwise the entry itself
    int nrow = 0, img = 0;
    // order of the groups: where their first entry falls; sorting by name: by the name shown (a group's own name -
    // its entries' names would put Ground, whose first is "Beach sand", before Cliffs)
    static int gord[VOX_MAXMAT]; for (int g = 0; g < v->ngrp; g++) gord[g] = g;
    if (v->sortMode == 1)
        for (int a = 1; a < v->ngrp; a++) {
            int t = gord[a], b = a - 1;
            const char *kt = v->grpN[t] > 1 ? v->grpName[t] : v->part.matName[v->grpIds[v->grpStart[t]]];
            while (b >= 0) {
                int u = gord[b]; const char *ku = v->grpN[u] > 1 ? v->grpName[u] : v->part.matName[v->grpIds[v->grpStart[u]]];
                int r = _stricmp(ku, kt); if (v->sortDesc) r = -r;
                if (r <= 0) break;
                gord[b + 1] = u; b--;
            }
            gord[b + 1] = t;
        }
    for (int gi = 0; gi < v->ngrp; gi++) {
        int g = gord[gi];
        int multi = v->grpN[g] > 1, open = multi && (fa[0] || V3IsExpanded(v, v->grpName[g]));
        for (int k = -1; k < v->grpN[g]; k++) {
            if (k < 0 && !multi) continue;                  // (single entry: no group row)
            if (k >= 0 && multi && !open) break;
            int id = k < 0 ? v->grpIds[v->grpStart[g]] : v->grpIds[v->grpStart[g] + k];
            unsigned char rgb[3]; VoxMatColour(&v->part, id, rgb);
            HBITMAP bm = V3Swatch(sw, sw, rgb); ImageList_Add(il, bm, NULL); DeleteObject(bm);
            wchar_t num[16], name[96], cnt[16];
            if (k < 0) {                                    // the group's row
                uint64_t tot = 0; for (int q = 0; q < v->grpN[g]; q++) tot += v->part.matCount[v->grpIds[v->grpStart[g] + q]];
                swprintf(num, 16, L"%lc", open ? 0x25BE : 0x25B8);   // (down / right-pointing triangle)
                swprintf(name, 96, L"%hs (%d)", v->grpName[g], v->grpN[g]);
                V3Short(cnt, 16, (double)tot);
            } else {
                swprintf(num, 16, L"%d", id);
                const char *nm = v->part.matName[id], *c = strstr(nm, ": ");
                if (!nm[0]) swprintf(name, 96, L"(unnamed)");
                else swprintf(name, 96, L"%hs", multi && c ? c + 2 : nm);
                V3Short(cnt, 16, (double)v->part.matCount[id]);
            }
            LVITEMW it; ZeroMemory(&it, sizeof(it));
            it.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM | LVIF_INDENT; it.iItem = nrow; it.pszText = num; it.iImage = img++;
            it.lParam = k < 0 ? -1 - g : id; it.iIndent = k >= 0 && multi ? 1 : 0;
            int row = (int)SendMessageW(v->list, LVM_INSERTITEMW, 0, (LPARAM)&it);
            LVITEMW st; ZeroMemory(&st, sizeof(st)); st.iSubItem = 1; st.pszText = name;
            SendMessageW(v->list, LVM_SETITEMTEXTW, row, (LPARAM)&st);
            st.iSubItem = 2; st.pszText = cnt; SendMessageW(v->list, LVM_SETITEMTEXTW, row, (LPARAM)&st);
            if (k < 0) { v->grpRow[g] = row; v->listMat[nrow] = -1 - g; v->grpSome[g] = 0; V3SetGroupRow(v, g); }
            else { ListView_SetCheckState(v->list, row, !v->hidden[id]); v->matRow[id] = row; v->listMat[nrow] = id; }
            nrow++;
        }
    }
    v->nlist = nrow;
    if (top > 0 && top < nrow) {                            // keep roughly the same scroll position
        int per = ListView_GetCountPerPage(v->list);
        (void)ListView_EnsureVisible(v->list, top + per - 1 < nrow ? top + per - 1 : nrow - 1, FALSE); (void)ListView_EnsureVisible(v->list, top, FALSE);
    }
    v->fillingList = 0;
}

// a list row's tooltip: the game's own name(s) for what the entry draws
static void V3EntryTipEx(V3 *v, int row, wchar_t *out, int n, int maxNames) {
    out[0] = 0;
    if (row < 0 || row >= v->nlist) return;
    int lm = v->listMat[row]; VoxPart *p = &v->part;
    if (lm < 0) { int g = -1 - lm; swprintf(out, n, L"%hs: %d entries - open the group to see each one's game name", v->grpName[g], v->grpN[g]); return; }
    const char *nm = lm < p->nmat ? p->matName[lm] : "";
    if (V3IsTerrainGrid(v) && p->terrMat[lm] >= 0 && p->terrGeneric) { swprintf(out, n, L"ground material %d (the number in the scene's .mate tiles; its texture: layer %d of Tex\\<scene>_Ground)", p->terrMat[lm], p->terrMat[lm] < 46 ? p->terrMat[lm] : 45); return; }
    if (V3IsTerrainGrid(v) && p->terrMat[lm] >= 0) {                     // ground: the game's terrain material
        int k = p->terrMat[lm];
        if (k < 62) swprintf(out, n, L"%hs\n(terrain material %d: TerrainMaterialGParam CmnTex_Tera_Product_Proc_%hs)", kTeraMatNames[k], k, kTeraMatNames[k]);
        else swprintf(out, n, L"terrain material %d", k);
        return;
    }
    if (p->isModel && lm < 256) {                                         // model part: its shape and material
        swprintf(out, n, L"Shape: %hs\nMaterial: %hs", p->shapeFull[lm][0] ? p->shapeFull[lm] : nm, p->matFull[lm][0] ? p->matFull[lm] : "?");
        return;
    }
    int o = 0;
    static const char *const wk[4] = { "Water", "Muddy water", "Lava", "Ghost swamp water" };
    int isW = !strncmp(nm, "Water kind ", 11);
    for (int k = 0; k < 4; k++) if (!strcmp(nm, wk[k])) { isW = 1; o += swprintf(out + o, n - o, L"the terrain's water layer (<tile>.water.extm), kind %d\n", k); }
    if (isW) {                                                          // what the loaded tiles' water cells hold
        o += swprintf(out + o, n - o, L"water cells in the tiles loaded, by kind byte:");
        for (int k = 0; k < 256 && o < n - 40; k++) if (p->waterKindN[k]) o += swprintf(out + o, n - o, L"  %d: %u", k, p->waterKindN[k]);
        o += swprintf(out + o, n - o, L"\n");
    }
    if (V3IsTerrainGrid(v) && v->fromPiece[lm])               // (only entries that really have triangles from them)
        o += swprintf(out + o, n - o, L"parts of the merged ground pieces (Model\\Piece_Gridify_Game03_X.._Z..)\n");
    for (int c = 0; c < v->ncoll; c++) if (v->coll[c].mat == lm && o < n - 700) o += swprintf(out + o, n - o, L"%hs\n", v->coll[c].txt);
    // the placed objects the loader drew as this entry (by the game's object name)
    int shown = 0, more = 0; size_t L = strlen(nm);
    for (int i = 0; v->names && i < v->capNames; i++) {
        const V3Name *e = &v->names[i]; if (!e->nm[0] || !e->ent[0]) continue;
        if (strncmp(e->ent, nm, L) || (e->ent[L] && e->ent[L] != ' ')) continue;
        if (shown < maxNames && o < n - 120) { o += swprintf(out + o, n - o, L"%hs  (%d placed)\n", e->nm, e->count); shown++; } else more++;
    }
    if (more) o += swprintf(out + o, n - o, L"... and %d more\n", more);
    if (!o) swprintf(out, n, L"%hs", nm);
    else if (o > 0 && out[o - 1] == L'\n') out[o - 1] = 0;
}
static void V3EntryTip(V3 *v, int row, wchar_t *out, int n) { V3EntryTipEx(v, row, out, n, 16); }
// right-click an entry: everything it draws, in a window whose text can be copied - the placed objects (all of them)
// and the ground-piece parts' own names
static void V3EntryList(V3 *v, int row) {
    if (row < 0 || row >= v->nlist) return;
    int lm = v->listMat[row]; if (lm < 0) return;
    size_t cap = 2000000; wchar_t *t = (wchar_t *)malloc(cap * sizeof(wchar_t)); if (!t) return;
    int o = swprintf(t, cap, L"%hs\r\n\r\n", v->part.matName[lm]);
    V3EntryTipEx(v, row, t + o, (int)(cap - o - 200000), 1000000);
    for (wchar_t *q = t + o; *q; q++) if (*q == L'\n' && q[-1] != L'\r') { memmove(q + 1, q, (wcslen(q) + 1) * sizeof(wchar_t)); *q = L'\r'; q++; }
    o = (int)wcslen(t);
    if (v->pieceTxt[lm]) {
        o += swprintf(t + o, cap - o, L"\r\n\r\nGround-piece parts drawn as this entry (shape | material):\r\n");
        for (const char *q = v->pieceTxt[lm]; *q && (size_t)o < cap - 4; q++) { if (*q == '\n') t[o++] = L'\r'; t[o++] = (wchar_t)(unsigned char)*q; }
        t[o] = 0;
    }
    for (int c = 0; c < v->ncoll; c++) if (v->coll[c].mat == lm) {      // a collectible list: every one of them
        HCURSOR oc = SetCursor(LoadCursor(NULL, IDC_WAIT));
        o += swprintf(t + o, cap - o, L"\r\n\r\nEvery one in the list (x, y (height), z, then its mission or sticker):\r\n");
        for (int i = 0; i < v->ncpt && (size_t)o < cap - 1000; i++) {
            const struct V3CPt *q = &v->cpt[i]; if (q->list != v->coll[c].list) continue;
            o += swprintf(t + o, cap - o, L"%.1f, %.1f, %.1f", q->T[0], q->T[1], q->T[2]);
            if (q->island[0] && !q->noPos) o += swprintf(t + o, cap - o, L"   (inside %hs: shown at its way in)", q->island);
            else if (q->island[0]) o += swprintf(t + o, cap - o, L"   (inside %hs: its way in isn't known - not drawn)", q->island);
            else if (!q->placed && fabsf(q->T[0]) < 300 && fabsf(q->T[2]) < 300 && fabsf(q->T[1]) < 50) o += swprintf(t + o, cap - o, L"   (not found in the placement files, and looks like an island's own coordinates - not drawn)");
            else if (!q->placed) o += swprintf(t + o, cap - o, L"   (no placed object found at this spot - drawn at the list's position, unturned)");
            if (q->ms[0]) { wchar_t ml[600]; V3MissionLine(v, q->ms, ml, 600); o += swprintf(t + o, cap - o, L"   %ls", ml); }
            if (q->stk[0]) o += swprintf(t + o, cap - o, L"   sticker %hs", q->stk);
            o += swprintf(t + o, cap - o, L"\r\n");
        }
        if (v->missMsgFrom[0]) o += swprintf(t + o, cap - o, L"\r\n(mission texts: %hs)\r\n", v->missMsgFrom);
        else if (v->missMsgTried) o += swprintf(t + o, cap - o, L"\r\n(no mission texts: romfs\\Mals\\USen\\MsgMain.msarc not found)\r\n");
        SetCursor(oc);
    }
    wchar_t tt[120]; swprintf(tt, 120, L"What \"%hs\" draws", v->part.matName[lm]);
    V3ShowText(v, tt, t);
    free(t);
}
// the list's tooltips: shown only once the mouse has rested a while, gone as soon as it moves
static LRESULT CALLBACK V3ListSub(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    if (msg == WM_MOUSEMOVE) {
        static LPARAM last = -1;
        if (lp != last) { last = lp; HWND tt = ListView_GetToolTips(h); if (tt && IsWindowVisible(tt)) SendMessageW(tt, TTM_POP, 0, 0); }
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, V3ListSub, 31);
    return DefSubclassProc(h, msg, wp, lp);
}
// Show all / Hide all: every entry (also those in closed groups)
static void V3SetAll(V3 *v, int show) {
    for (int k = 0; k < v->part.nmat && k < VOX_MAXMAT; k++) if (v->part.matCount[k]) v->hidden[k] = (unsigned char)!show;
    V3FillList(v);
    V3RequestBuild(v);
}
// a tick changed in the list (by click or keyboard): an entry, or a whole group
static void V3ListTicked(V3 *v, int row, int oldState, int newState) {
    if (row < 0 || row >= v->nlist) return;
    int lm = v->listMat[row];
    v->fillingList = 1;
    if (lm < 0) {                                           // a group: from all shown -> hide all; from none / some -> show all
        int g = -1 - lm, show = oldState != 2 || v->grpSome[g]; (void)newState;
        for (int k = 0; k < v->grpN[g]; k++) { int id = v->grpIds[v->grpStart[g] + k]; v->hidden[id] = (unsigned char)!show; if (v->matRow[id] >= 0) ListView_SetCheckState(v->list, v->matRow[id], show); }
        V3SetGroupRow(v, g);
    } else {
        v->hidden[lm] = (unsigned char)(newState != 2);
        V3SetGroupRow(v, v->grpOfMat[lm]);
    }
    v->fillingList = 0;
}

/* ------------------------------------------------------------------ layout */
static void V3Layout(V3 *v) {
    RECT rc; GetClientRect(v->wnd, &rc);
    if (!v->panelW96) v->panelW96 = 340;
    int m = V3S(v, 8), pw = V3S(v, v->panelW96), rh = V3S(v, 24), y = m, gap = V3S(v, 6);
    if (pw > rc.right - V3S(v, 200)) pw = rc.right - V3S(v, 200);       // (the view keeps some room)
    if (pw < V3S(v, 240)) pw = V3S(v, 240);
    int infoH = V3S(v, v->nCloth ? 150 : 128);
    MoveWindow(v->info, m, y, pw, infoH, TRUE); y += infoH + gap;
    if (IsWindowVisible(v->lod)) { MoveWindow(v->lod, m, y, pw, rh, TRUE); y += rh + gap; }
    if (IsWindowVisible(v->ovlCb)) { MoveWindow(v->ovlCb, m, y, pw, V3S(v, 360), TRUE); y += rh + gap; }
    if (IsWindowVisible(v->prbCb)) { MoveWindow(v->prbCb, m, y, pw, V3S(v, 480), TRUE); y += rh + gap; }
    if (IsWindowVisible(v->sndCb)) { MoveWindow(v->sndCb, m, y, pw, V3S(v, 480), TRUE); y += rh + gap; }
    if (v->nCloth || v->hasWater) { MoveWindow(v->clothCb, m, y, pw, rh, TRUE); y += rh + gap; }
    if (v->nsim && v->part.isModel && v->hasSkeleton) { MoveWindow(v->simCb, m, y, pw, rh, TRUE); y += rh + gap; }
    if ((v->hb.ok && v->part.isModel && v->hasSkeleton) || v->hasCliffs) { MoveWindow(v->hbCb, m, y, pw, rh, TRUE); y += rh + gap; }
    if (v->hasLandmarks) { MoveWindow(v->lmCb, m, y, pw, rh, TRUE); y += rh + gap; }
    if (IsWindowVisible(v->propCb)) { MoveWindow(v->propCb, m, y, pw, rh, TRUE); y += rh + gap; }
    if (v->part.isModel && v->hasSkeleton) {                 // models: animation file and list, Play and the frame slider instead of the cut
        if (v->nAtt) { MoveWindow(v->attachCb, m, y, pw, rh, TRUE); y += rh + gap; }
        MoveWindow(v->keepCb, m, y, pw, rh, TRUE); y += rh + gap;

        MoveWindow(v->animFileCb, m, y, pw, V3S(v, 300), TRUE); y += rh + gap;
        MoveWindow(v->animCb, m, y, pw, V3S(v, 400), TRUE); y += rh + gap;
        int pb = V3S(v, 60), sb = V3S(v, 92);
        MoveWindow(v->play, m, y, pb, rh, TRUE); MoveWindow(v->speedCb, m + pb + gap, y, sb, V3S(v, 200), TRUE);
        MoveWindow(v->frame, m + pb + sb + 2 * gap, y, pw - pb - sb - 2 * gap, rh + V3S(v, 4), TRUE); y += rh + V3S(v, 4) + gap;
        if (v->ncol) { MoveWindow(v->colCb, m, y, pw, V3S(v, 400), TRUE); y += rh + gap; }
    } else {
        MoveWindow(v->cutlbl, m, y + V3S(v, 3), pw, rh - V3S(v, 3), TRUE); y += rh;
        MoveWindow(v->cut, m, y, pw, rh + V3S(v, 4), TRUE); y += rh + V3S(v, 4) + gap;
    }
    {   int bw4 = (pw - 3 * gap) / 4;                     // Controls (a little wider) | Reset | Top | Save
        MoveWindow(v->ctlBtn, m, y, bw4 + V3S(v, 8), rh + V3S(v, 2), TRUE);
        int x2 = m + bw4 + V3S(v, 8) + gap, bw3 = (pw - (x2 - m) - 2 * gap) / 3;
        MoveWindow(v->reset, x2, y, bw3, rh + V3S(v, 2), TRUE); MoveWindow(v->topb, x2 + bw3 + gap, y, bw3, rh + V3S(v, 2), TRUE);
        MoveWindow(v->saveb, x2 + 2 * (bw3 + gap), y, m + pw - (x2 + 2 * (bw3 + gap)), rh + V3S(v, 2), TRUE); }
    y += rh + V3S(v, 2) + gap;
    MoveWindow(v->exportb, m, y, pw, rh + V3S(v, 2), TRUE); y += rh + V3S(v, 2) + gap;
    MoveWindow(v->pngb, m, y, pw, rh + V3S(v, 2), TRUE); y += rh + V3S(v, 2) + gap * 2;
    MoveWindow(v->matlbl, m, y + V3S(v, 3), pw, rh - V3S(v, 3), TRUE); y += rh;
    {   int sb2 = V3S(v, 150);                          // sort order + find box
        MoveWindow(v->sortCb, m, y, sb2, V3S(v, 200), TRUE); MoveWindow(v->findEd, m + sb2 + gap, y, pw - sb2 - gap, rh, TRUE); y += rh + gap; }
    int helpH = V3S(v, 40);
    int bw = (pw - gap) / 2;
    int listH = rc.bottom - y - m - rh - gap - helpH - gap; if (listH < V3S(v, 60)) listH = V3S(v, 60);
    MoveWindow(v->list, m, y, pw, listH, TRUE); y += listH + gap;
    MoveWindow(v->all, m, y, bw, rh, TRUE); MoveWindow(v->none, m + bw + gap, y, bw, rh, TRUE); y += rh + gap;
    MoveWindow(v->coord, m, y, pw, helpH, TRUE);          // the place under the cursor
    ShowWindow(v->help, SW_HIDE);
    // list columns: # (an indented tick, swatch and 3 digits) and the count keep their width (dragged ones too),
    // the name takes the rest
    if (v->colsSet) { int a = ListView_GetColumnWidth(v->list, 0), c = ListView_GetColumnWidth(v->list, 2); if (a > 0) v->col0W96 = MulDiv(a, 96, v->dpi); if (c > 0) v->col2W96 = MulDiv(c, 96, v->dpi); }
    if (!v->col0W96) v->col0W96 = 92; if (!v->col2W96) v->col2W96 = 48;
    int c0 = V3S(v, v->col0W96), c2 = V3S(v, v->col2W96), c1 = pw - c0 - c2 - V3S(v, 24); if (c1 < V3S(v, 60)) c1 = V3S(v, 60);
    ListView_SetColumnWidth(v->list, 0, c0); ListView_SetColumnWidth(v->list, 1, c1); ListView_SetColumnWidth(v->list, 2, c2);
    v->colsSet = 1;
    int gx = m + pw + m;
    MoveWindow(v->gl, gx, m, rc.right - gx - m > 10 ? rc.right - gx - m : 10, rc.bottom - 2 * m > 10 ? rc.bottom - 2 * m : 10, TRUE);
    if (v->ldText) {                                     // the loading status: over the bottom of the view
        int sw = rc.right - gx - m - V3S(v, 4); if (sw < 10) sw = 10;
        int bh = V3S(v, 8), th = rh, sy = rc.bottom - m - V3S(v, 2) - bh - th;
        SetWindowPos(v->ldText, HWND_TOP, gx + V3S(v, 2), sy, sw, th, SWP_NOACTIVATE);
        SetWindowPos(v->ldBar, HWND_TOP, gx + V3S(v, 2), sy + th, sw, bh, SWP_NOACTIVATE);
    }
}
// the loading status line and bar: shown once a load / build has taken more than 0.4 s, until it's finished
/* ------------------------------------------------------------------ saved views
   "Save" keeps how a view looks - the camera, the options (resolution / textures, ground layer, probes, sounds,
   animation, colours, check boxes, list order), the cut, and which list entries are ticked - in
   <exe folder>\saved_views\<hash>.txt, one file per object / area (the key: the part's folder and what's loaded).
   Opening that object / area again puts it all back once it has loaded. */
static void V3UpdateCutLabel(V3 *v);
int UiChoiceDialog(HWND owner, const wchar_t *title, const wchar_t *heading, const wchar_t *text,
                   const wchar_t *const *titles, const wchar_t *const *descs, int n, int def);
static const int kV3SavedCtl[] = { V3_LOD, V3_OVERLAY, V3_PROBES, V3_SOUNDS, V3_ATTACH, V3_KEEP, V3_CLOTH, V3_HELPER, V3_LANDMARKS, V3_PROPS,
                                   V3_CLOTHSIM, V3_ANIMFILE, V3_ANIM, V3_SPEED, V3_COLOUR, V3_SORT };
static void V3ViewKey(const V3 *v, wchar_t *key, size_t n) {
    wchar_t g[2048]; swprintf(g, 2048, L"%ls", v->geom);
    if (!wcsncmp(g, L"terrainarea:", 12)) { wchar_t *c = g; for (int k = 0; k < 3 && c; k++) c = wcschr(c + 1, L','); if (c) *c = 0; }   // (not the ground layer)
    swprintf(key, n, L"%ls|%ls", v->dir, g);
}
static void V3ViewPath(const V3 *v, wchar_t *out, size_t n, int makeDir) {
    wchar_t key[4200]; V3ViewKey(v, key, 4200);
    unsigned long long h = 1469598103934665603ULL; for (const wchar_t *q = key; *q; q++) { h ^= (unsigned)*q; h *= 1099511628211ULL; }
    wchar_t exe[1024]; GetModuleFileNameW(NULL, exe, 1024); wchar_t *sl = wcsrchr(exe, L'\\'); if (sl) *sl = 0;
    wchar_t d[1100]; swprintf(d, 1100, L"%ls\\saved_views", exe); if (makeDir) CreateDirectoryW(d, NULL);
    swprintf(out, n, L"%ls\\%016llx.txt", d, h);
}
static int V3IsCombo(HWND h) { wchar_t c[32]; GetClassNameW(h, c, 32); return !_wcsicmp(c, L"ComboBox"); }
// the view's state as text (UTF-8)
static char *V3ViewText(V3 *v) {
    size_t cap = 4096 + (size_t)VOX_MAXMAT * (VOX_MATN + 16), o = 0; char *t = (char *)malloc(cap); if (!t) return NULL;
    wchar_t key[4200]; V3ViewKey(v, key, 4200);
    char k8[8400]; WideCharToMultiByte(CP_UTF8, 0, key, -1, k8, sizeof k8, NULL, NULL);
    o += snprintf(t + o, cap - o, "ns2dig view 1\nkey %s\nlod %d\novl %d\ncut %d\ncam %.6g %.6g %.6g %.6g %.6g\n", k8, v->lodSel, v->ovlSel, v->cutPos, v->yaw, v->pitch, v->dist, v->panx, v->pany);
    if (v->ncinst && V3CollS(v) != 1) o += snprintf(t + o, cap - o, "csize %g\n", V3CollS(v));      // (the collectibles' size)
    {   int a = ListView_GetColumnWidth(v->list, 0), c = ListView_GetColumnWidth(v->list, 2);
        o += snprintf(t + o, cap - o, "panel %d\ncol0 %d\ncol2 %d\n", v->panelW96, a > 0 ? MulDiv(a, 96, v->dpi) : v->col0W96, c > 0 ? MulDiv(c, 96, v->dpi) : v->col2W96); }
    for (size_t i = 0; i < sizeof kV3SavedCtl / sizeof kV3SavedCtl[0]; i++) {
        HWND h = GetDlgItem(v->wnd, kV3SavedCtl[i]); if (!h || !IsWindowVisible(h)) continue;
        long val = V3IsCombo(h) ? (long)SendMessageW(h, CB_GETCURSEL, 0, 0) : (long)(SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED);
        o += snprintf(t + o, cap - o, "ctl %d %ld\n", kV3SavedCtl[i], val);
    }
    for (int k = 0; k < v->part.nmat && k < VOX_MAXMAT && o + VOX_MATN + 16 < cap; k++) o += snprintf(t + o, cap - o, "hide %d %s\n", v->hidden[k] ? 1 : 0, v->part.matName[k]);
    return t;
}
static char *V3ReadSavedView(V3 *v) {
    wchar_t path[1300]; V3ViewPath(v, path, 1300, 0);
    size_t n; unsigned char *b = VoxReadFile(path, &n); if (!b) return NULL;
    char *t = (char *)realloc(b, n + 1); if (!t) { free(b); return NULL; }
    t[n] = 0;
    if (strncmp(t, "ns2dig view 1\n", 14)) { free(t); return NULL; }
    return t;
}
static int V3SaveView(V3 *v) {
    char *t = V3ViewText(v); if (!t) return 0;
    wchar_t path[1300]; V3ViewPath(v, path, 1300, 1);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    int ok = 0;
    if (f != INVALID_HANDLE_VALUE) { DWORD wr; ok = WriteFile(f, t, (DWORD)strlen(t), &wr, NULL) && wr == strlen(t); CloseHandle(f); }
    free(t);
    return ok;
}
// a saved number from the text ("lod 2")
static int V3SavedInt(const char *t, const char *what, int *out) {
    char k[40]; snprintf(k, sizeof k, "\n%s ", what); const char *q = strstr(t, k); if (!q) return 0;
    *out = atoi(q + strlen(k)); return 1;
}
// put a saved view back, a step at a time: options that load again (resolution, ground layer...) wait for that
// load to finish (called again when the view has been built); then the ticks, the cut and the camera
static void V3RestorePass(V3 *v) {
    if (!v->restoreTxt || v->thread) return;
    if (--v->restoreLeft < 0) { free(v->restoreTxt); v->restoreTxt = NULL; return; }
    const char *t = v->restoreTxt; int changed = 0;
    for (const char *q = strstr(t, "\nctl "); q; q = strstr(q + 1, "\nctl ")) {
        int id = 0; long val = 0; if (sscanf(q + 5, "%d %ld", &id, &val) != 2) continue;
        HWND h = GetDlgItem(v->wnd, id); if (!h || !IsWindowVisible(h) || !IsWindowEnabled(h)) continue;
        if (V3IsCombo(h)) {
            if (val < 0 || val >= (long)SendMessageW(h, CB_GETCOUNT, 0, 0) || (long)SendMessageW(h, CB_GETCURSEL, 0, 0) == val) continue;
            SendMessageW(h, CB_SETCURSEL, (WPARAM)val, 0); SendMessageW(v->wnd, WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), (LPARAM)h);
        } else {
            if ((SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED) == (val != 0)) continue;
            SendMessageW(h, BM_SETCHECK, val ? BST_CHECKED : BST_UNCHECKED, 0); SendMessageW(v->wnd, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), (LPARAM)h);
        }
        changed = 1;
        if (v->thread) return;                                // loading again: carry on when it's done
    }
    int cut; if (V3SavedInt(t, "cut", &cut) && cut != v->cutPos && cut >= 0 && cut <= 1000) { v->cutPos = cut; SendMessageW(v->cut, TBM_SETPOS, TRUE, cut); V3UpdateCutLabel(v); }
    int hc = 0;
    {   const char *q = strstr(t, "\ncsize "); float cs;            // the collectibles' size
        if (q && sscanf(q + 7, "%f", &cs) == 1 && cs >= 1 && cs <= 100 && cs != V3CollS(v) && v->ncinst) { v->collScale = cs; V3CollApplyScale(v); hc = 1; } }
    for (const char *q = strstr(t, "\nhide "); q; q = strstr(q + 1, "\nhide ")) {
        int hid = q[6] == '1'; const char *nm = q + 8; const char *e = strchr(nm, '\n'); size_t L = e ? (size_t)(e - nm) : strlen(nm);
        for (int k = 0; k < v->part.nmat && k < VOX_MAXMAT; k++)
            if (strlen(v->part.matName[k]) == L && !strncmp(v->part.matName[k], nm, L) && v->hidden[k] != hid) { v->hidden[k] = (unsigned char)hid; hc = 1; }
    }
    float c[5];
    { const char *q = strstr(t, "\ncam "); if (q && sscanf(q + 5, "%f %f %f %f %f", &c[0], &c[1], &c[2], &c[3], &c[4]) == 5) { v->yaw = c[0]; v->pitch = c[1]; v->dist = c[2]; v->panx = c[3]; v->pany = c[4]; } }
    if (hc) { V3FillList(v); V3RequestBuild(v); }
    InvalidateRect(v->gl, NULL, FALSE);
    if (!changed && !hc) { free(v->restoreTxt); v->restoreTxt = NULL; }
}
// closing with the window's X: offer to save over the saved view when it has changed (nothing saved: just close)
static int V3AskSaveOnClose(V3 *v) {
    if (!v->partOk || !v->haveMesh || v->restoreTxt) return 1;
    char *old = V3ReadSavedView(v); if (!old) return 1;
    char *now = V3ViewText(v);
    int same = now && !strcmp(old, now);
    free(old); free(now);
    if (same) return 1;
    static const wchar_t *const ct[2] = { L"Save and close", L"Close without saving" };
    static const wchar_t *const cd[2] = { L"Save the view as it is now over the saved one - it opens like this next time.",
                                          L"Keep the saved view as it was." };
    int r = UiChoiceDialog(v->wnd, L"Save the view?", L"This view has changed since it was saved.",
                           L"Save it over the saved view? (Cancel keeps the window open.)", ct, cd, 2, 1);   // 1 save, 2 don't, 0 cancel
    if (r == 0) return 0;
    if (r == 1 && !V3SaveView(v)) MessageBoxW(v->wnd, L"The view couldn't be saved (the saved_views folder beside ns2dig.exe couldn't be written).", L"Save the view", MB_OK | MB_ICONWARNING);
    return 1;
}
static void V3BusyOff(V3 *v) {
    v->busyOn = 0;
    if (v->ldText) { ShowWindow(v->ldText, SW_HIDE); ShowWindow(v->ldBar, SW_HIDE); }
}
static void V3BusyUpdate(V3 *v) {
    if (!v->ldText || !v->thread) return;
    wchar_t t[220]; long prog;
    if (v->loading) {
        long n = v->ldN, d = v->ldDone; if (d > n) d = n;
        if (n > 0) { prog = v->ldBase + (long)((long long)v->ldSpan * d / n); swprintf(t, 220, L"%ls... %ld of %ld", v->ldStage, d, n); }
        else { prog = v->ldProg; swprintf(t, 220, L"%ls", v->ldStage); }
    } else { prog = v->progress; swprintf(t, 220, L"Building the surface... %ld%%", v->progress / 10); }
    if (!v->busyOn) {
        if (GetTickCount() - v->busyT0 < 400) return;
        v->busyOn = 1; V3Layout(v);
        ShowWindow(v->ldText, SW_SHOWNOACTIVATE); ShowWindow(v->ldBar, SW_SHOWNOACTIVATE);
    }
    wchar_t old[220]; GetWindowTextW(v->ldText, old, 220); if (wcscmp(old, t)) SetWindowTextW(v->ldText, t);
    int marq = prog < 0;
    if (marq != v->busyMarq) {                          // step of unknown length: the bar just moves
        LONG_PTR st = GetWindowLongPtrW(v->ldBar, GWL_STYLE);
        SetWindowLongPtrW(v->ldBar, GWL_STYLE, marq ? (st | PBS_MARQUEE) : (st & ~(LONG_PTR)PBS_MARQUEE));
        SendMessageW(v->ldBar, PBM_SETMARQUEE, marq, 30); v->busyMarq = marq;
    }
    if (!marq) SendMessageW(v->ldBar, PBM_SETPOS, prog > 1000 ? 1000 : prog, 0);
}

static HWND V3Ctl(V3 *v, const wchar_t *cls, const wchar_t *text, DWORD style, DWORD ex, int id) {
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, v->wnd, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)v->font, TRUE);
    return h;
}

static void V3UpdateCutLabel(V3 *v) {
    wchar_t t[128];
    if (v->cutPos >= 1000) swprintf(t, 128, L"Cut off the top: none (drag to look inside)");
    else swprintf(t, 128, L"Cut off the top: showing the lowest %d%%", v->cutPos / 10);
    SetWindowTextW(v->cutlbl, t);
}

static LRESULT CALLBACK V3Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    V3 *v = (V3 *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    // before WM_CREATE and after WM_NCDESTROY there is no state: controls being destroyed can still
    // send notifications here
    if (!v && msg != WM_CREATE) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_CREATE: {
        v = (V3 *)((CREATESTRUCTW *)lp)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)v);
        v->wnd = hwnd;
        v->info  = V3Ctl(v, L"EDIT", L"Loading...", ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, 0, V3_INFO);   // scrolls when the text is longer than the box
        v->lod   = V3Ctl(v, L"BUTTON", L"Low resolution (faster - untick for full detail)", BS_AUTOCHECKBOX | WS_TABSTOP, 0, V3_LOD);
        v->cutlbl = V3Ctl(v, L"STATIC", L"", SS_LEFT, 0, V3_CUTLBL);
        v->cut   = V3Ctl(v, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, V3_CUT);
        SendMessageW(v->cut, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));
        SendMessageW(v->cut, TBM_SETPAGESIZE, 0, 50);
        SendMessageW(v->cut, TBM_SETPOS, TRUE, 1000);
        v->cutPos = 1000;
        v->attachCb = V3Ctl(v, L"BUTTON", L"Head and tie attached", BS_AUTOCHECKBOX | WS_TABSTOP, 0, V3_ATTACH);
        ShowWindow(v->attachCb, SW_HIDE);
        v->keepCb = V3Ctl(v, L"BUTTON", L"Keep in place (ignore the Root bone's travel)", BS_AUTOCHECKBOX | WS_TABSTOP, 0, V3_KEEP);
        ShowWindow(v->keepCb, SW_HIDE); SendMessageW(v->keepCb, BM_SETCHECK, BST_CHECKED, 0);
        v->clothCb = V3Ctl(v, L"BUTTON", L"Cloth physics (particles, links, collision)", BS_AUTOCHECKBOX | WS_TABSTOP, 0, V3_CLOTH);
        ShowWindow(v->clothCb, SW_HIDE);
        v->hbCb = V3Ctl(v, L"BUTTON", L"Helper bones (shoulders, elbows, knees keep their shape)", BS_AUTOCHECKBOX | WS_TABSTOP, 0, V3_HELPER);
        ShowWindow(v->hbCb, SW_HIDE); SendMessageW(v->hbCb, BM_SETCHECK, BST_CHECKED, 0);
        v->lmCb = V3Ctl(v, L"BUTTON", L"Show landmarks (mountains, cranes, gates...)", BS_AUTOCHECKBOX | WS_TABSTOP, 0, V3_LANDMARKS);
        ShowWindow(v->lmCb, SW_HIDE); SendMessageW(v->lmCb, BM_SETCHECK, BST_CHECKED, 0);
        v->propCb = V3Ctl(v, L"BUTTON", L"Show props (coins, item boxes, lights, cars, signs...)", BS_AUTOCHECKBOX | WS_TABSTOP, 0, V3_PROPS);
        ShowWindow(v->propCb, SW_HIDE);
        v->simCb = V3Ctl(v, L"BUTTON", L"Simulate cloth while playing (tie and hair swing)", BS_AUTOCHECKBOX | WS_TABSTOP, 0, V3_CLOTHSIM);
        ShowWindow(v->simCb, SW_HIDE); SendMessageW(v->simCb, BM_SETCHECK, BST_CHECKED, 0); v->simReset = 1;
        v->animFileCb = V3Ctl(v, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, 0, V3_ANIMFILE);
        v->animCb = V3Ctl(v, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, 0, V3_ANIM);
        SendMessageW(v->animCb, CB_SETMINVISIBLE, 25, 0);
        v->play = V3Ctl(v, L"BUTTON", L"Play", BS_PUSHBUTTON | WS_TABSTOP, 0, V3_PLAY);
        v->frame = V3Ctl(v, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, V3_FRAME);
        v->speedCb = V3Ctl(v, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, 0, V3_SPEED);
        v->colCb = V3Ctl(v, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, 0, V3_COLOUR);
        v->ovlCb = V3Ctl(v, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, 0, V3_OVERLAY);
        for (int k = 0; k < VOV_COUNT; k++) { wchar_t t[96]; swprintf(t, 96, L"Ground: %hs", kVovNames[k]); SendMessageW(v->ovlCb, CB_ADDSTRING, 0, (LPARAM)t); }
        SendMessageW(v->ovlCb, CB_SETCURSEL, 0, 0);
        ShowWindow(v->ovlCb, SW_HIDE);
        v->prbCb = V3Ctl(v, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, 0, V3_PROBES);
        SendMessageW(v->prbCb, CB_ADDSTRING, 0, (LPARAM)L"Light probes: off");
        for (int k = 0; k < 8; k++) {
            static const wchar_t *what[8] = { L" (light A: bounced daylight)", L" (light A: direction)", L" (light A: direction)", L" (light A: direction)",
                                              L" (light B: nearby lights)", L" (light B: direction)", L" (light B: direction)", L" (light B: direction)" };
            wchar_t t[96]; swprintf(t, 96, L"Light probes: values %d-%d as colour%ls", k * 3 + 1, k * 3 + 3, what[k]); SendMessageW(v->prbCb, CB_ADDSTRING, 0, (LPARAM)t);
        }
        for (int k = 1; k <= 32; k++) {
            wchar_t t[96]; static const wchar_t *rgb[3] = { L"R", L"G", L"B" };
            if (k <= 24) swprintf(t, 96, L"Light probes: value %d (light %hs %ls %ls)", k, k <= 12 ? "A" : "B", ((k - 1) % 12) < 3 ? L"average" : L"direction", rgb[(k - 1) % 3]);
            else if (k == 29) swprintf(t, 96, L"Light probes: value 29 (fade sun / moon: shade or lamps, high = fade)");
            else if (k == 30) swprintf(t, 96, L"Light probes: value 30 (fade changes: up / down)");
            else if (k >= 31) swprintf(t, 96, L"Light probes: value %d (fade changes: sideways %d)", k, k - 30);
            else if (k == 25) swprintf(t, 96, L"Light probes: value 25 (open sky: how much)");
            else if (k == 26) swprintf(t, 96, L"Light probes: value 26 (open sky: how much overhead)");
            else if (k == 27) swprintf(t, 96, L"Light probes: value 27 (open sky: to the south + / north -)");
            else swprintf(t, 96, L"Light probes: value 28 (open sky: to the east + / west -)");
            SendMessageW(v->prbCb, CB_ADDSTRING, 0, (LPARAM)t);
        }
        SendMessageW(v->prbCb, CB_SETCURSEL, 0, 0); SendMessageW(v->prbCb, CB_SETMINVISIBLE, 25, 0);
        ShowWindow(v->prbCb, SW_HIDE);
        v->sndCb = V3Ctl(v, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, 0, V3_SOUNDS);
        SendMessageW(v->sndCb, CB_ADDSTRING, 0, (LPARAM)L"Sound probes: off");
        for (int k = 1; k <= 32; k++) { wchar_t t[96]; swprintf(t, 96, L"Sound probes: %hs", V3SoundName(k)); SendMessageW(v->sndCb, CB_ADDSTRING, 0, (LPARAM)t); }
        SendMessageW(v->sndCb, CB_SETCURSEL, 0, 0); SendMessageW(v->sndCb, CB_SETMINVISIBLE, 25, 0);
        ShowWindow(v->sndCb, SW_HIDE);
        SendMessageW(v->colCb, CB_SETMINVISIBLE, 25, 0); SendMessageW(v->colCb, CB_SETDROPPEDWIDTH, V3S(v, 700), 0); ShowWindow(v->colCb, SW_HIDE);
        for (int m = 0; m < 256; m++) { v->lookTint[m][0] = v->lookTint[m][1] = v->lookTint[m][2] = 1; }
        for (int m = 0; m < VOX_MAXMAT; m++) v->patTex[m] = -1;
        {   static const wchar_t *sp[] = { L"0.25\x00D7 (15 fps)", L"0.5\x00D7 (30 fps)", L"1\x00D7 (60 fps)", L"2\x00D7 (120 fps)" };
            for (int k = 0; k < 4; k++) SendMessageW(v->speedCb, CB_ADDSTRING, 0, (LPARAM)sp[k]);
            SendMessageW(v->speedCb, CB_SETCURSEL, 2, 0); v->fps = 60; }
        ShowWindow(v->speedCb, SW_HIDE);
        ShowWindow(v->animCb, SW_HIDE); ShowWindow(v->play, SW_HIDE); ShowWindow(v->frame, SW_HIDE); ShowWindow(v->animFileCb, SW_HIDE);
        v->reset = V3Ctl(v, L"BUTTON", L"Reset", BS_PUSHBUTTON | WS_TABSTOP, 0, V3_RESET);
        v->topb = V3Ctl(v, L"BUTTON", L"Top", BS_PUSHBUTTON | WS_TABSTOP, 0, V3_TOPVIEW);
        v->saveb = V3Ctl(v, L"BUTTON", L"Save", BS_PUSHBUTTON | WS_TABSTOP, 0, V3_SAVEVIEW);
        v->exportb = V3Ctl(v, L"BUTTON", L"Export 3D file (GLB, OBJ, PLY or STL)...", BS_PUSHBUTTON | WS_TABSTOP, 0, V3_EXPORT);
        v->pngb = V3Ctl(v, L"BUTTON", L"Export PNG (the view as shown)...", BS_PUSHBUTTON | WS_TABSTOP, 0, V3_PNG);
        v->matlbl = V3Ctl(v, L"STATIC", L"Materials (untick to hide):", SS_LEFT, 0, V3_MATLBL);
        v->sortCb = V3Ctl(v, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, 0, V3_SORT);
        {   static const wchar_t *so[] = { L"Biggest first", L"Name A\x2013Z", L"Number", L"By model (body, head, ties)", L"Shown first" };
            for (int k = 0; k < 5; k++) SendMessageW(v->sortCb, CB_ADDSTRING, 0, (LPARAM)so[k]);
            SendMessageW(v->sortCb, CB_SETCURSEL, 0, 0); }
        v->findEd = V3Ctl(v, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, V3_FIND);
        SendMessageW(v->findEd, EM_SETCUEBANNER, TRUE, (LPARAM)L"Find a part...");
        v->list  = V3Ctl(v, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP, WS_EX_CLIENTEDGE, V3_LIST);
        ListView_SetExtendedListViewStyle(v->list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP);   // (tooltips: the game's names)
        SetPropW(v->list, L"ns2digOwnCol0", (HANDLE)1);
        {   HWND tt = ListView_GetToolTips(v->list);           // tooltips: after 1.2 s of the mouse resting on an entry
            if (tt) { SendMessageW(tt, TTM_SETDELAYTIME, TTDT_INITIAL, 1200); SendMessageW(tt, TTM_SETDELAYTIME, TTDT_RESHOW, 1200); SendMessageW(tt, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000); }
            SetWindowSubclass(v->list, V3ListSub, 31, 0); }   // (the first column opens groups: the skin's row click leaves it alone)
        { LVCOLUMNW c; ZeroMemory(&c, sizeof(c)); c.mask = LVCF_WIDTH | LVCF_TEXT; c.cx = 60; c.pszText = (LPWSTR)L"#";
          SendMessageW(v->list, LVM_INSERTCOLUMNW, 0, (LPARAM)&c);
          c.cx = 150; c.pszText = (LPWSTR)L"Material"; SendMessageW(v->list, LVM_INSERTCOLUMNW, 1, (LPARAM)&c);
          c.mask |= LVCF_FMT; c.fmt = LVCFMT_RIGHT; c.cx = 80; c.pszText = (LPWSTR)L"Voxels"; SendMessageW(v->list, LVM_INSERTCOLUMNW, 2, (LPARAM)&c); }
        v->all  = V3Ctl(v, L"BUTTON", L"Show all", BS_PUSHBUTTON | WS_TABSTOP, 0, V3_ALL);
        v->none = V3Ctl(v, L"BUTTON", L"Hide all", BS_PUSHBUTTON | WS_TABSTOP, 0, V3_NONE);
        v->help = V3Ctl(v, L"STATIC", L"", SS_LEFT, 0, V3_HELP);   // (kept hidden: the Controls window has the hints)
        v->ctlBtn = V3Ctl(v, L"BUTTON", L"Controls", BS_PUSHBUTTON | WS_TABSTOP, 0, V3_CTLBTN);
        v->coord = V3Ctl(v, L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0, V3_COORD);
        v->arrowHot = -1; v->prbHot = -1; v->sndHot = -1;
        v->gl = CreateWindowExW(WS_EX_CLIENTEDGE, L"AlsarcVox3DGL", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                                0, 0, 10, 10, hwnd, NULL, GetModuleHandleW(NULL), NULL);
        V3InitGL(v);
        v->ldText = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS, 0, 0, 10, 10, hwnd, NULL, GetModuleHandleW(NULL), NULL);
        SendMessageW(v->ldText, WM_SETFONT, (WPARAM)v->font, TRUE);
        v->ldBar = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | PBS_SMOOTH, 0, 0, 10, 10, hwnd, NULL, GetModuleHandleW(NULL), NULL);
        SendMessageW(v->ldBar, PBM_SETRANGE32, 0, 1000);
        V3UpdateCutLabel(v);
        V3Layout(v);
        swprintf(v->status, 256, L"Loading the part...");
        V3SetInfo(v, v->status);
        v->loading = 1;
        V3StartJob(v, 1);
        return 0;
    }
    case WM_SIZE: if (v) { V3Layout(v); InvalidateRect(v->gl, NULL, FALSE); } return 0;
    case WM_SETCURSOR:
        if (v && (HWND)wp == hwnd && LOWORD(lp) == HTCLIENT) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
            RECT g; GetWindowRect(v->gl, &g); MapWindowPoints(NULL, hwnd, (POINT *)&g, 2);
            if (pt.x >= g.left - V3S(v, 10) && pt.x < g.left) { SetCursor(LoadCursor(NULL, IDC_SIZEWE)); return TRUE; }
        }
        break;
    case WM_LBUTTONDOWN:
        if (v) {
            RECT g; GetWindowRect(v->gl, &g); MapWindowPoints(NULL, hwnd, (POINT *)&g, 2);
            int x = (short)LOWORD(lp); if (x >= g.left - V3S(v, 10) && x < g.left) { v->splitDrag = 1; SetCapture(hwnd); return 0; }
        }
        break;
    case WM_MOUSEMOVE:
        if (v && v->splitDrag) {
            RECT rc; GetClientRect(hwnd, &rc);
            int x = (short)LOWORD(lp) - V3S(v, 8) - V3S(v, 4);         // (the panel starts at the margin; grabbed mid-gap)
            if (x < V3S(v, 240)) x = V3S(v, 240); if (x > rc.right - V3S(v, 200)) x = rc.right - V3S(v, 200);
            int w96 = MulDiv(x, 96, v->dpi); if (w96 != v->panelW96) { v->panelW96 = w96; V3Layout(v); UpdateWindow(hwnd); }
            return 0;
        }
        break;
    case WM_LBUTTONUP: if (v && v->splitDrag) { v->splitDrag = 0; ReleaseCapture(); InvalidateRect(v->gl, NULL, FALSE); return 0; } break;
    case WM_CAPTURECHANGED: if (v) v->splitDrag = 0; break;
    case WM_GETMINMAXINFO: if (v) { ((MINMAXINFO *)lp)->ptMinTrackSize.x = V3S(v, 700); ((MINMAXINFO *)lp)->ptMinTrackSize.y = V3S(v, 520); } return 0;
    case WM_CTLCOLORSTATIC: break;
    case WM_TIMER:
        if (wp == V3_TIMER_PROGRESS && v && v->thread) {
            V3BusyUpdate(v);
            if (!v->loading) {
                swprintf(v->status, 256, L"Building the surface... %ld%%", v->progress / 10);
                V3UpdateInfo(v);
            }
        } else if (wp == V3_TIMER_REBUILD) { KillTimer(hwnd, V3_TIMER_REBUILD); if (v->nAtt && V3UpdateTieNeck(v)) V3FillList(v); V3RequestBuild(v); }
        else if (wp == V3_TIMER_FX && v->fxOn) { if (!(v->animSel > 0 && v->playing)) InvalidateRect(v->gl, NULL, FALSE); }
        else if (wp == V3_TIMER_ANIM && v->animSel > 0 && v->playing) {
            float f = v->animF0 + (GetTickCount() - v->animT0) * v->fps / 1000.0f;   // 1x = 60 frames a second (the game's rate)
            int len = v->anim.frames > 1 ? v->anim.frames - 1 : 1;
            if (f >= len) { if (v->anim.loop) f = fmodf(f, (float)len); else { f = (float)len; v->playing = 0; } }
            v->animFrame = f; V3ApplyPose(v); V3UpdateFrameLabel(v); InvalidateRect(v->gl, NULL, FALSE);
        }
        return 0;
    case WM_V3_LOADED:
        if (v->thread) { WaitForSingleObject(v->thread, INFINITE); CloseHandle(v->thread); v->thread = NULL; }
        v->loading = 0;
        if (!lp) {
            KillTimer(hwnd, V3_TIMER_PROGRESS); V3BusyOff(v);
            swprintf(v->status, 256, L"Couldn't load this part:\r\n%hs", v->part.err);
            V3SetInfo(v, v->status);
            return 0;
        }
        v->partOk = 1;
        if (v->terrTex || v->nmtex) V3UploadTextures(v);
        ShowWindow(v->ovlCb, V3IsTerrainGrid(v) && !v->part.terrGeneric ? SW_SHOW : SW_HIDE); SendMessageW(v->ovlCb, CB_SETCURSEL, v->ovlSel, 0);   // (Mario Kart World's layers only)
        v->prbHot = -1; v->sndHot = -1; if (v->prbTip) { TOOLINFOW ti0; ZeroMemory(&ti0, sizeof ti0); ti0.cbSize = sizeof ti0; ti0.hwnd = v->gl; ti0.uId = 1; SendMessageW(v->prbTip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti0); }
        free(v->snd); v->snd = v->sndLoad; v->nsnd = v->nsndLoad; v->sndOrient = v->sndOrientLoad; v->sndLoad = NULL; v->nsndLoad = 0;
        ShowWindow(v->sndCb, V3IsTerrainGrid(v) && v->nsnd ? SW_SHOW : SW_HIDE); SendMessageW(v->sndCb, CB_SETCURSEL, v->sndSel, 0); V3SoundRange(v);
        free(v->prb); v->prb = v->prbLoad; v->nprb = v->nprbLoad; v->nprbFine = v->nprbFineLoad; v->prbLoad = NULL; v->nprbLoad = 0; v->nprbFineLoad = 0; memcpy(v->prbFile, v->prbFileLoad, sizeof v->prbFile);
        ShowWindow(v->prbCb, V3IsTerrainGrid(v) && v->nprb ? SW_SHOW : SW_HIDE); SendMessageW(v->prbCb, CB_SETCURSEL, v->prbSel, 0); V3ProbeRange(v); V3Layout(v);
        v->hasWater = v->hasCliffs = v->hasLandmarks = 0;           // terrain: every kind of thing is its own entry in the list (no tick boxes)
        if (V3IsTerrainGrid(v)) {
            ShowWindow(v->clothCb, SW_HIDE); ShowWindow(v->hbCb, SW_HIDE); ShowWindow(v->lmCb, SW_HIDE); ShowWindow(v->propCb, SW_HIDE);
            for (int k = 0; k < v->part.nmat && k < VOX_MAXMAT; k++) {
                v->hidden[k] = v->part.startHidden[k];
                for (int q = 0; q < v->nkeep; q++) if (!strcmp(v->keepName[q], v->part.matName[k])) { v->hidden[k] = v->keepHidden[q]; break; }   // as it was before reloading
            }
            V3Layout(v);
        }
        if (V3IsTerrainGrid(v) && v->ovlSel > 0 && v->ovlFresh) {    // a ground layer just chosen: just the ground and the sea / lakes
            for (int k = 0; k < v->part.nmat && k < VOX_MAXMAT; k++) {
                const char *n = v->part.matName[k];
                int lq = V3IsWaterMat(n) && !strchr(n, ':');          // (not the "Aurora: ..." pieces)
                v->hidden[k] = (unsigned char)!(v->part.terrMat[k] >= 0 || lq);
            }
        }
        v->ovlFresh = 0;
        if (v->part.isModel) { memcpy(v->hidden, v->part.startHidden, sizeof v->hidden); V3UploadTextures(v); v->showTex = 1; v->tieSel = -1; memset(v->tieWasShown, 0, sizeof v->tieWasShown); if (v->nAtt) V3UpdateTieNeck(v); }
        // the voxel material 0 ("Invalid") and the editor-only "Collision..." materials start hidden? keep all visible
        SendMessageW(v->lod, BM_SETCHECK, v->lodSel == 2 ? BST_CHECKED : BST_UNCHECKED, 0);
        { int has = !v->part.isTriMesh || V3IsTerrainGrid(v) || v->part.isModel;     // a ready-made mesh: nothing to reduce, so no box
          ShowWindow(v->lod, has ? SW_SHOW : SW_HIDE); EnableWindow(v->lod, has); V3Layout(v); }    // (terrain grids reload with smaller tiles)
        V3FillList(v);
        if (v->part.isTriMesh) {                                    // columns: name / triangles
            LVCOLUMNW c; ZeroMemory(&c, sizeof(c)); c.mask = LVCF_TEXT;
            c.pszText = (LPWSTR)(v->part.isModel ? L"Part" : L"Material"); ListView_SetColumn(v->list, 1, &c);
            c.pszText = (LPWSTR)L"Triangles"; ListView_SetColumn(v->list, 2, &c);
        }
        if (v->part.isModel) {
            if (v->nCloth) { ShowWindow(v->clothCb, SW_SHOW); SendMessageW(v->clothCb, BM_SETCHECK, BST_CHECKED, 0); V3Layout(v); }
            if (v->nsim && v->hasSkeleton) { ShowWindow(v->simCb, SW_SHOW); V3Layout(v); }
            if (v->hb.ok && v->hasSkeleton) { ShowWindow(v->hbCb, SW_SHOW); V3Layout(v); }
            SetWindowTextW(v->matlbl, L"Parts (untick to hide):");
            SetWindowTextW(v->lod, L"Textures");                  // the half-resolution box becomes a textures switch
            EnableWindow(v->lod, v->texFound > 0); SendMessageW(v->lod, BM_SETCHECK, v->texFound ? BST_CHECKED : BST_UNCHECKED, 0);
            if (v->hasSkeleton) {
                ShowWindow(v->cut, SW_HIDE); ShowWindow(v->cutlbl, SW_HIDE);
                ShowWindow(v->animFileCb, SW_SHOW); ShowWindow(v->animCb, SW_SHOW); ShowWindow(v->play, SW_SHOW); ShowWindow(v->frame, SW_SHOW); ShowWindow(v->speedCb, SW_SHOW); ShowWindow(v->keepCb, SW_SHOW);
                V3FillAnimFiles(v); V3FillAnims(v);
                if (v->ncol) { V3ColourNames(v); V3FillColours(v); ShowWindow(v->colCb, SW_SHOW); }
                if (v->nAtt) {
                    int head = 0, ties = 0; for (int a = 0; a < v->nAtt; a++) { if (strstr(v->att[a].name, "Tie")) ties++; else head++; }
                    SetWindowTextW(v->attachCb, head && ties ? L"Head and tie attached" : head ? L"Head attached" : L"Tie attached");
                    ShowWindow(v->attachCb, SW_SHOW); SendMessageW(v->attachCb, BM_SETCHECK, BST_CHECKED, 0);
                }
                V3Layout(v);
            }
        }
        if (v->part.err[0]) swprintf(v->status, 256, L"Warning: %hs", v->part.err);
        V3RequestBuild(v);
        V3UpdateInfo(v);
        return 0;
    case WM_V3_MESHED: {
        VoxMesh *m = (VoxMesh *)lp;
        if (v->thread) { WaitForSingleObject(v->thread, INFINITE); CloseHandle(v->thread); v->thread = NULL; }
        KillTimer(hwnd, V3_TIMER_PROGRESS);
        if (!v->reloadPending && !v->rebuildPending) V3BusyOff(v);
        if (v->reloadPending) {
            v->reloadPending = 0;
            if (m) { VoxFreeMesh(m); free(m); }
            V3ReloadWorld(v);
            return 0;
        }
        if (v->rebuildPending) {
            v->rebuildPending = 0;
            if (m) { VoxFreeMesh(m); free(m); }
            V3RequestBuild(v);
            return 0;
        }
        if (m) {
            V3Upload(v, m);
            VoxFreeMesh(m); free(m);
            if (v->animSel > 0) V3ApplyPose(v);
            swprintf(v->status, 256, v->useVbo ? L"" : L"(Using basic OpenGL - rotating may be slow.)");
            if (v->part.gridW > 1 && v->part.terrWorld[2] > 0 && v->useVbo) swprintf(v->status, 256, L"Right-click the ground to open that area in full detail.");
        } else swprintf(v->status, 256, L"Building the surface failed (out of memory?) - try Low resolution.");
        V3UpdateInfo(v);
        InvalidateRect(v->gl, NULL, FALSE);
        if (m && v->restoreTxt) V3RestorePass(v);            // a saved view being put back
        return 0;
    }
    case WM_V3_EXPORTED: {
        V3Export *e = (V3Export *)lp;
        if (v->exportThread) { WaitForSingleObject(v->exportThread, INFINITE); CloseHandle(v->exportThread); v->exportThread = NULL; }
        v->exporting = 0;
        EnableWindow(v->exportb, TRUE); EnableWindow(v->lod, TRUE); EnableWindow(v->list, TRUE);
        EnableWindow(v->all, TRUE); EnableWindow(v->none, TRUE);
        if (e->ok) {
            wchar_t n[64]; V3Num(n, 64, (double)e->tris);
            swprintf(v->status, 256, L"Exported %ls triangles.", n);
            V3UpdateInfo(v);
            wchar_t m[2600], kinds[600] = L"";
            if (e->catTris[0] + e->catTris[1] + e->catTris[2] + e->catTris[3] + e->catTris[4] + e->catTris[5]) {
                static const wchar_t *kn[6] = { L"ground", L"water and lava", L"cliffs and ice", L"courses", L"scenery", L"other" };
                wcscpy(kinds, L"\n\nIncluded:");
                for (int k = 0; k < 6; k++) if (e->catTris[k]) { wchar_t q[64], line[120]; V3Num(q, 64, (double)e->catTris[k]); swprintf(line, 120, L"\n  %ls: %ls triangles", kn[k], q); wcscat(kinds, line); }
            }
            swprintf(m, 2600, L"Saved %ls triangles to:\n%ls%ls%ls\n\nShow it in Explorer?", n, e->path, kinds,
                     e->fmt == 1 ? L"\n(plus a .mtl file with the material colours)" :
                     e->fmt == 3 ? L"\n\nSizes are in game units (1 voxel = about 1 unit), so scale it down in your slicer." : L"");
            (void)0;
            if (MessageBoxW(hwnd, m, L"Export 3D file", MB_YESNO | MB_ICONINFORMATION) == IDYES) {
                wchar_t arg[2200]; swprintf(arg, 2200, L"/select,\"%ls\"", e->path);
                ShellExecuteW(hwnd, L"open", L"explorer.exe", arg, NULL, SW_SHOWNORMAL);
            }
        } else if (!v->cancel) {
            swprintf(v->status, 256, L"Export failed.");
            V3UpdateInfo(v);
            MessageBoxW(hwnd, e->err[0] ? e->err : L"Export failed.", L"Export 3D file", MB_ICONWARNING);
        }
        free(e);
        return 0;
    }
    case WM_HSCROLL:
        if ((HWND)lp == v->frame && v->animSel > 0) {
            v->playing = 0; v->animFrame = (float)SendMessageW(v->frame, TBM_GETPOS, 0, 0);
            V3ApplyPose(v); V3UpdateFrameLabel(v); InvalidateRect(v->gl, NULL, FALSE);
            return 0;
        }
        if ((HWND)lp == v->cut) {
            v->cutPos = (int)SendMessageW(v->cut, TBM_GETPOS, 0, 0);
            V3UpdateCutLabel(v);
            InvalidateRect(v->gl, NULL, FALSE);
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case V3_LOD:
            if (v->part.isModel) { v->showTex = SendMessageW(v->lod, BM_GETCHECK, 0, 0) == BST_CHECKED; InvalidateRect(v->gl, NULL, FALSE); break; }
            v->lodSel = SendMessageW(v->lod, BM_GETCHECK, 0, 0) == BST_CHECKED ? 2 : 1;
            if (V3IsTerrainGrid(v)) { V3ReloadWorld(v); break; }    // other tiles: load again
            V3RequestBuild(v); V3UpdateInfo(v);
            break;
        case V3_RESET: V3ResetView(v); InvalidateRect(v->gl, NULL, FALSE); SetFocus(v->gl); break;
        case V3_CTLBTN: V3ShowControls(v); break;
        case V3_SAVEVIEW:
            if (!v->partOk || !v->haveMesh) break;
            if (V3SaveView(v)) { swprintf(v->status, 256, L"View saved: it opens like this next time."); free(v->restoreTxt); v->restoreTxt = NULL; }
            else swprintf(v->status, 256, L"The view couldn't be saved (the saved_views folder beside ns2dig.exe couldn't be written).");
            V3UpdateInfo(v); SetFocus(v->gl); break;
        case V3_TOPVIEW: if (v->haveMesh) { V3TopView(v); InvalidateRect(v->gl, NULL, FALSE); } SetFocus(v->gl); break;
        case V3_PNG: V3SavePng(v); break;
        case V3_PROPS: {                                 // loaded only when wanted (thousands of small objects): reload the area
            int on = SendMessageW(v->propCb, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (on != v->propsOn) { v->propsOn = on; V3ReloadWorld(v); }
            break;
        }
        case V3_LANDMARKS:
            if (v->hasLandmarks) {
                int on = SendMessageW(v->lmCb, BM_GETCHECK, 0, 0) == BST_CHECKED; v->landmarksOff = !on;
                for (int m = 0; m < v->part.nmat && m < VOX_MAXMAT; m++) if (!strncmp(v->part.matName[m], "Landmark:", 9)) v->hidden[m] = (unsigned char)!on;
                V3FillList(v); V3RequestBuild(v);
            }
            break;
        case V3_HELPER:
            if (v->hasCliffs) {                     // terrain: cliffs and ice on / off
                int on = SendMessageW(v->hbCb, BM_GETCHECK, 0, 0) == BST_CHECKED; v->cliffsOff = !on;
                for (int m = 0; m < v->part.nmat && m < VOX_MAXMAT; m++) if (V3IsCliffMat(v->part.matName[m])) v->hidden[m] = (unsigned char)(!on || v->part.startHidden[m]);
                V3FillList(v); V3RequestBuild(v); break;
            }
            V3ApplyPose(v); InvalidateRect(v->gl, NULL, FALSE); break;
        case V3_CLOTHSIM: v->simReset = 1; V3ApplyPose(v); InvalidateRect(v->gl, NULL, FALSE); break;
        case V3_CLOTH: if (v->hasWater) {               // terrain: water and lava on / off
            int on = SendMessageW(v->clothCb, BM_GETCHECK, 0, 0) == BST_CHECKED; v->waterOff = !on;
            for (int m = 0; m < v->part.nmat && m < VOX_MAXMAT; m++) if (V3IsWaterMat(v->part.matName[m])) v->hidden[m] = (unsigned char)(!on || v->part.startHidden[m]);
            V3FillList(v); V3RequestBuild(v);
            break;
        } else {
            // all the cloth overlay parts on or off (on: as they started - the Bananza forms' shapes stay off)
            int on = SendMessageW(v->clothCb, BM_GETCHECK, 0, 0) == BST_CHECKED;
            for (int m = 0; m < v->part.nmat; m++)
                if (strstr(v->part.matName[m], "Cloth ") || strstr(v->part.matName[m], "Collision ")) v->hidden[m] = (unsigned char)(on ? v->part.startHidden[m] : 1);
            V3FillList(v); V3RequestBuild(v);
            break;
        }
        case V3_ATTACH: {
            // show/hide the attached models: on = the default head and tie (other ties stay as ticked in the list)
            int on = SendMessageW(v->attachCb, BM_GETCHECK, 0, 0) == BST_CHECKED; v->attOn = on;
            for (int a = 0; a < v->nAtt; a++)
                for (int m = v->att[a].partLo; m < v->att[a].partHi; m++)
                    v->hidden[m] = (unsigned char)(!on ? 1 : v->att[a].defShown ? v->part.startHidden[m] : v->hidden[m]);
            V3UpdateTieNeck(v); V3FillList(v); V3RequestBuild(v);
            break;
        }
        case V3_ANIMFILE: if (HIWORD(wp) == CBN_SELCHANGE) V3PickAnimFile(v, (int)SendMessageW(v->animFileCb, CB_GETCURSEL, 0, 0)); break;
        case V3_SORT: if (HIWORD(wp) == CBN_SELCHANGE) { v->sortMode = (int)SendMessageW(v->sortCb, CB_GETCURSEL, 0, 0); v->sortDesc = 0; V3FillList(v); } break;
        case V3_FIND: if (HIWORD(wp) == EN_CHANGE) V3FillList(v); break;
        case V3_KEEP: V3ApplyPose(v); InvalidateRect(v->gl, NULL, FALSE); break;
        case V3_SPEED: if (HIWORD(wp) == CBN_SELCHANGE) {
            static const float fp[] = { 15, 30, 60, 120 };
            int k = (int)SendMessageW(v->speedCb, CB_GETCURSEL, 0, 0);
            v->animF0 = v->animFrame; v->animT0 = GetTickCount();     // keep the current frame
            v->fps = k >= 0 && k < 4 ? fp[k] : 60;
        } break;
        case V3_COLOUR: if (HIWORD(wp) == CBN_SELCHANGE) V3PickColour(v, (int)SendMessageW(v->colCb, CB_GETCURSEL, 0, 0)); break;
        case V3_OVERLAY:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                int sel = (int)SendMessageW(v->ovlCb, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < VOV_COUNT && sel != v->ovlSel) { v->dropKeep = sel == 0 && v->ovlSel > 0; v->ovlSel = sel; v->ovlFresh = sel > 0; V3ReloadWorld(v); }   // (same view: the camera isn't moved)
            }
            break;
        case V3_SOUNDS:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                int sel = (int)SendMessageW(v->sndCb, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel <= 32) { v->sndSel = sel; V3SoundRange(v); V3UpdateInfo(v); InvalidateRect(v->gl, NULL, FALSE); }
                SetFocus(v->gl);
            }
            break;
        case V3_PROBES:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                int sel = (int)SendMessageW(v->prbCb, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel <= 40) { v->prbSel = sel; V3ProbeRange(v); V3UpdateInfo(v); InvalidateRect(v->gl, NULL, FALSE); }
                SetFocus(v->gl);
            }
            break;
        case V3_ANIM: if (HIWORD(wp) == CBN_SELCHANGE) V3SelectAnim(v, (int)SendMessageW(v->animCb, CB_GETCURSEL, 0, 0)); break;
        case V3_PLAY:
            if (v->animSel > 0) {
                v->playing = !v->playing;
                if (v->playing) {
                    if (!v->anim.loop && v->animFrame >= v->anim.frames - 1) v->animFrame = 0;
                    v->animT0 = GetTickCount(); v->animF0 = v->animFrame; SetTimer(hwnd, V3_TIMER_ANIM, 16, NULL);
                }
                V3UpdateFrameLabel(v);
            }
            break;
        case V3_EXPORT: V3StartExport(v); break;
        case V3_ALL: V3SetAll(v, 1); break;
        case V3_NONE: V3SetAll(v, 0); break;
        }
        return 0;
    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)lp;
        if (nh->hwndFrom == v->list && nh->code == NM_CUSTOMDRAW) return V3ListDraw(v, lp);
        if (nh->hwndFrom == v->list && nh->code == LVN_GETINFOTIPW) {
            NMLVGETINFOTIPW *gt = (NMLVGETINFOTIPW *)lp;
            if (gt->pszText && gt->cchTextMax > 1) V3EntryTip(v, gt->iItem, gt->pszText, gt->cchTextMax);
            SendMessageW(ListView_GetToolTips(v->list), TTM_SETMAXTIPWIDTH, 0, V3S(v, 520));   // (several lines)
            return 0;
        }
        if (nh->hwndFrom == v->list && nh->code == LVN_COLUMNCLICK) {      // # / name / triangles headers sort; again = reverse
            int col = ((NMLISTVIEW *)lp)->iSubItem, mode = col == 0 ? 2 : col == 1 ? 1 : 0;
            if (v->sortMode == mode) v->sortDesc = !v->sortDesc; else { v->sortMode = mode; v->sortDesc = 0; }
            SendMessageW(v->sortCb, CB_SETCURSEL, mode, 0); V3FillList(v);
            return 0;
        }
        if (nh->hwndFrom == v->list && nh->code == LVN_ITEMCHANGED && !v->fillingList) {
            NMLISTVIEW *nl = (NMLISTVIEW *)lp;
            if ((nl->uChanged & LVIF_STATE) && ((nl->uNewState ^ nl->uOldState) & LVIS_STATEIMAGEMASK) && nl->iItem >= 0 && nl->iItem < v->nlist) {
                V3ListTicked(v, nl->iItem, (int)((nl->uOldState & LVIS_STATEIMAGEMASK) >> 12), (int)((nl->uNewState & LVIS_STATEIMAGEMASK) >> 12));
                SetTimer(hwnd, V3_TIMER_REBUILD, 350, NULL);   // wait a moment in case more boxes are clicked
            }
        }
        if (nh->hwndFrom == v->list && nh->code == NM_RCLICK && !v->loading) {   // right-click an entry: what it draws, as a list
            LVHITTESTINFO ht = { 0 }; GetCursorPos(&ht.pt); POINT sp = ht.pt; ScreenToClient(v->list, &ht.pt);
            int row = ListView_SubItemHitTest(v->list, &ht);
            if (row >= 0 && row < v->nlist && v->listMat[row] >= 0) {
                HMENU m = CreatePopupMenu(); AppendMenuW(m, MF_STRING, 1, L"List what this entry draws (game names, can be copied)...");
                int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, sp.x, sp.y, 0, hwnd, NULL); DestroyMenu(m);
                if (cmd == 1) V3EntryList(v, row);
            }
            return 0;
        }
        if (nh->hwndFrom == v->list && nh->code == NM_CLICK) {             // the first column: a group opens / closes, an entry ticks
            LVHITTESTINFO ht = { 0 }; ht.pt = ((NMITEMACTIVATE *)lp)->ptAction;
            int row = ListView_SubItemHitTest(v->list, &ht);
            if (row >= 0 && row < v->nlist && ht.iSubItem == 0 && !(ht.flags & LVHT_ONITEMSTATEICON)) {
                int lm = v->listMat[row];
                if (lm < 0) { int g = -1 - lm; V3SetExpanded(v, v->grpName[g], !V3IsExpanded(v, v->grpName[g])); V3FillList(v); }
                else ListView_SetCheckState(v->list, row, !ListView_GetCheckState(v->list, row));
            }
        }
        return 0;
    }
    case WM_CLOSE: if (v && !V3AskSaveOnClose(v)) return 0; DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        // stop the worker and timers now; free everything in WM_NCDESTROY, after the child controls are gone
        KillTimer(hwnd, V3_TIMER_PROGRESS); KillTimer(hwnd, V3_TIMER_REBUILD); KillTimer(hwnd, V3_TIMER_ANIM);
        if (v->thread) { v->cancel = 1; WaitForSingleObject(v->thread, 15000); CloseHandle(v->thread); v->thread = NULL; }
        if (v->exportThread) { v->cancel = 1; WaitForSingleObject(v->exportThread, 60000); CloseHandle(v->exportThread); v->exportThread = NULL; }
        // GL: the drawing area still exists at this point (children are destroyed after this message)
        if (v->rc) {
            wglMakeCurrent(v->dc, v->rc);
            if (v->useVbo && v->vbo[0]) { v->glDeleteBuffers_(2, v->vbo); v->vbo[0] = v->vbo[1] = 0; }
            for (int t = 0; t < v->ntex; t++) { if (v->tex[t].id) glDeleteTextures(1, &v->tex[t].id); free(v->tex[t].rgba); v->tex[t].rgba = NULL; v->tex[t].id = 0; }
            wglMakeCurrent(NULL, NULL); wglDeleteContext(v->rc); v->rc = NULL;
        }
        v->haveMesh = 0;
        return 0;
    case WM_NCDESTROY: {
        MSG mm;   // meshes the worker posted but nobody picked up
        while (PeekMessageW(&mm, hwnd, WM_V3_MESHED, WM_V3_MESHED, PM_REMOVE)) { VoxMesh *m = (VoxMesh *)mm.lParam; if (m) { VoxFreeMesh(m); free(m); } }
        while (PeekMessageW(&mm, hwnd, WM_V3_EXPORTED, WM_V3_EXPORTED, PM_REMOVE)) free((void *)mm.lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        free(v->cpuVert); free(v->cpuIdx); free(v->runs);
        free(v->keepName); free(v->keepHidden); free(v->allAct); free(v->names); free(v->restoreTxt); free(v->expName); free(v->actArc); free(v->miArc); free(v->genArc); free(v->wta); free(v->isl); free(v->cpt); free(v->actColl); free(v->cinst); free(v->corig); free(v->stkPx); if (v->stkWnd) DestroyWindow(v->stkWnd); free(v->stk); MsbtFree(&v->missMsg); for (int k = 0; k < VOX_MAXMAT; k++) free(v->pieceTxt[k]); free(v->prb); free(v->prbLoad); free(v->snd); free(v->sndLoad); free(v->meshSrc); free(v->animBuf); free(v->skinM); free(v->worldM); free(v->morphP); BfShapeAnimFree(&v->shAnim); BfVisAnimFree(&v->visAnim); BfMatAnimFree(&v->matAnim); for (int a = 0; a < v->nAtt; a++) { BfShapeAnimFree(&v->att[a].sh); BfVisAnimFree(&v->att[a].vis); BfMatAnimFree(&v->att[a].mat); } free(v->posedP); free(v->posedN);
        BfSkelAnimFree(&v->anim); BfAnimFileFree(&v->anims); for (int c = 0; c < v->nsim; c++) ClothSimFree(&v->sim[c]); HbFree(&v->hb); free(v->patOn); free(v->patRGB); if (v->fxOn) BfColAnimFree(&v->fx); for (int k = 0; k < v->ncolF; k++) BfAnimFileFree(&v->colF[k]);
        for (int a = 0; a < v->nAtt; a++) BfSkelAnimFree(&v->att[a].anim);
        for (int i = 0; i < v->nAttFile; i++) BfAnimFileFree(&v->attFile[i]);
        // (the GL child's DC and the list's image list go away with those windows)
        VoxFreePart(&v->part);
        free(v);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// partDir: the part folder in the game dump (with Info.bin, vhead.bin, vbody.bin)
// geomJson: NULL for a terrain part; for a voxel object partDir is its .alsarc.zs in the dump and geomJson the
// extracted <name>.al__gen__VoxelGeometry.bgyml.json (grid size)
void Vox3DOpen(HWND owner, const wchar_t *partDir, const wchar_t *geomJson, const wchar_t *title, HFONT font, int dpi) {
    static int registered = 0;
    HINSTANCE inst = GetModuleHandleW(NULL);
    if (!registered) {
        WNDCLASSEXW wc; ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc); wc.lpfnWndProc = V3Proc; wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1)); wc.lpszClassName = L"AlsarcVox3D";
        RegisterClassExW(&wc);
        WNDCLASSEXW gc; ZeroMemory(&gc, sizeof(gc));
        gc.cbSize = sizeof(gc); gc.style = CS_OWNDC | CS_DBLCLKS; gc.lpfnWndProc = V3GLProc; gc.hInstance = inst;
        gc.hCursor = LoadCursor(NULL, IDC_SIZEALL); gc.lpszClassName = L"AlsarcVox3DGL";
        RegisterClassExW(&gc);
        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES };
        InitCommonControlsEx(&icc);
        registered = 1;
    }
    V3 *v = (V3 *)calloc(1, sizeof(V3));
    if (!v) return;
    v->font = font; v->dpi = dpi > 0 ? dpi : 96;
    swprintf(v->dir, 2048, L"%ls", partDir);
    swprintf(v->geom, 2048, L"%ls", geomJson ? geomJson : L"");
    v->lodSel = 2;                                  // "Low resolution" starts ticked: quicker to open
    v->restoreTxt = V3ReadSavedView(v);             // a saved view: put back once loaded (resolution and ground layer now)
    if (v->restoreTxt) {
        int lod, ovl; v->restoreLeft = 8;
        if (V3SavedInt(v->restoreTxt, "lod", &lod) && (lod == 1 || lod == 2)) v->lodSel = lod;
        int pw, c0, c2;                                  // the panel and list column widths, before the window is laid out
        if (V3SavedInt(v->restoreTxt, "panel", &pw) && pw >= 200 && pw <= 2000) v->panelW96 = pw;
        if (V3SavedInt(v->restoreTxt, "col0", &c0) && c0 >= 20 && c0 <= 600) v->col0W96 = c0;
        if (V3SavedInt(v->restoreTxt, "col2", &c2) && c2 >= 20 && c2 <= 600) v->col2W96 = c2;
        if (V3IsTerrainGrid(v) && V3SavedInt(v->restoreTxt, "ovl", &ovl) && ovl >= 0 && ovl < VOV_COUNT) { v->ovlSel = ovl; v->ovlFromParent = 1; }
    }
    v->owner = owner;
    swprintf(v->title, 512, L"3D view - %ls", title);
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int w = MulDiv(1280, v->dpi, 96), h = MulDiv(820, v->dpi, 96);
    if (w > wa.right - wa.left) w = wa.right - wa.left;
    if (h > wa.bottom - wa.top) h = wa.bottom - wa.top;
    int x0 = wa.left + (wa.right - wa.left - w) / 2, y0 = wa.top + (wa.bottom - wa.top - h) / 2, maxed = 0;
    if (g_v3PlaceSet) {                             // opened from another view: its size, a little down and right
        RECT r = g_v3Place.rcNormalPosition; int off = MulDiv(30, v->dpi, 96);
        w = r.right - r.left; h = r.bottom - r.top; x0 = r.left + off; y0 = r.top + off;
        if (x0 + w > wa.right) x0 = wa.right - w; if (y0 + h > wa.bottom) y0 = wa.bottom - h;
        if (x0 < wa.left) x0 = wa.left; if (y0 < wa.top) y0 = wa.top;
        maxed = g_v3Place.showCmd == SW_SHOWMAXIMIZED || (g_v3Place.showCmd == SW_SHOWMINIMIZED && (g_v3Place.flags & WPF_RESTORETOMAXIMIZED));
    }
    HWND wnd = CreateWindowExW(0, L"AlsarcVox3D", v->title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                               x0, y0, w, h, NULL, NULL, inst, v);
    (void)owner;
    if (wnd) UiSkinWindow(wnd);                 // the main window's dark look
    if (wnd) SendMessageW(v->info, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, V3S(v, 2)));   // (text lined up with the controls below)
    if (wnd) { ShowWindow(wnd, maxed ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL); UpdateWindow(wnd); }
    else free(v);
}

/* ------------------------------------------------------------------ preview for the main window */
// a small shaded picture of a model / collision shape / cloth's model / terrain tile, drawn by a tiny software
// renderer (no OpenGL needed): seen from the front-left and a little above, lit from the top left, material
// colours, background transparent. kind: L"bfres", L"phive", L"terrain", L"cloth".
unsigned char *Vox3DThumbnail(const wchar_t *dumpFile, const wchar_t *kind, int boxW, int boxH, int *ow, int *oh, wchar_t *note, int noteN) {
    note[0] = 0;
    if (boxW < 8 || boxH < 8) return NULL;
    V3 *v = (V3 *)calloc(1, sizeof(V3)); if (!v) return NULL;
    swprintf(v->dir, 2048, L"%ls", dumpFile); swprintf(v->geom, 2048, L"%ls", kind);
    int ok = !wcscmp(kind, L"phive") ? VoxLoadPhive(&v->part, v->dir) : !wcscmp(kind, L"terrain") ? VoxLoadTerrain(&v->part, v->dir) : !wcscmp(kind, L"terrainworld") ? VoxLoadTerrainWorld(&v->part, v->dir, 256)
           : !wcscmp(kind, L"bfres") ? VoxLoadBfres(&v->part, v->dir) : !wcscmp(kind, L"cloth") ? V3LoadClothView(v) : 0;
    unsigned char *out = NULL;
    VoxMesh m; memset(&m, 0, sizeof m);
    if (ok && v->part.isTriMesh && v->part.nti && VoxBuildTriMesh(&v->part, &m, v->part.isModel ? v->part.startHidden : NULL) && m.ni) {
        int SS = 2, W = boxW * SS, H = boxH * SS;                 // drawn at twice the size, then shrunk (smooth edges)
        float *zb = (float *)malloc((size_t)W * H * sizeof(float));
        unsigned char *img = (unsigned char *)calloc((size_t)W * H, 4);
        float *sp = (float *)malloc(m.nv * 3 * sizeof(float));    // screen x, y, depth
        float *li = (float *)malloc(m.nv * sizeof(float));          // light per vertex
        if (zb && img && sp && li) {
            for (size_t i = 0; i < (size_t)W * H; i++) zb[i] = 1e30f;
            int terrain = !wcscmp(kind, L"terrain") || !wcscmp(kind, L"terrainworld");
            float yaw = terrain ? 0.6f : 0.65f, pitch = terrain ? 0.75f : 0.32f;     // terrain: looked at more from above
            float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), spp = sinf(pitch);
            // fit: the rotated bounding box's corners
            float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
            for (int c = 0; c < 8; c++) {
                float x = c & 1 ? m.bmax[0] : m.bmin[0], y = c & 2 ? m.bmax[1] : m.bmin[1], z = c & 4 ? m.bmax[2] : m.bmin[2];
                float rx = x * cy + z * sy, rz = -x * sy + z * cy, ry = y * cp - rz * spp;
                if (rx < lo[0]) lo[0] = rx; if (rx > hi[0]) hi[0] = rx; if (ry < lo[1]) lo[1] = ry; if (ry > hi[1]) hi[1] = ry;
            }
            float ext = (hi[0] - lo[0]) / W > (hi[1] - lo[1]) / H ? (hi[0] - lo[0]) / W : (hi[1] - lo[1]) / H;
            float sc = ext > 0 ? 0.92f / ext : 1, ox = W / 2.0f - (lo[0] + hi[0]) / 2 * sc, oy = H / 2.0f + (lo[1] + hi[1]) / 2 * sc;
            float L[3] = { -0.45f, 0.75f, 0.5f }; { float l = sqrtf(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]); L[0] /= l; L[1] /= l; L[2] /= l; }
            for (size_t i = 0; i < m.nv; i++) {
                float x = m.pos[i * 3], y = m.pos[i * 3 + 1], z = m.pos[i * 3 + 2];
                float rx = x * cy + z * sy, rz = -x * sy + z * cy, ry = y * cp - rz * spp, rd = y * spp + rz * cp;
                sp[i * 3] = ox + rx * sc; sp[i * 3 + 1] = oy - ry * sc; sp[i * 3 + 2] = -rd;
                float nx = m.nrm[i * 4] / 127.0f, ny = m.nrm[i * 4 + 1] / 127.0f, nz = m.nrm[i * 4 + 2] / 127.0f;
                float d = nx * L[0] + ny * L[1] + nz * L[2]; if (d < 0) d = -d * 0.35f;        // two-sided, back faces dimmer
                li[i] = 0.35f + 0.65f * d;
            }
            DWORD t0 = GetTickCount();
            for (size_t t = 0; t + 2 < m.ni; t += 3) {
                if ((t & 0xFFFF) == 0 && GetTickCount() - t0 > 8000) break;          // give up on enormous meshes
                uint32_t a = m.idx[t], b = m.idx[t + 1], c = m.idx[t + 2];
                float x0 = sp[a * 3], y0 = sp[a * 3 + 1], z0 = sp[a * 3 + 2], x1 = sp[b * 3], y1 = sp[b * 3 + 1], z1 = sp[b * 3 + 2], x2 = sp[c * 3], y2 = sp[c * 3 + 1], z2 = sp[c * 3 + 2];
                float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
                if (area == 0) continue;
                int minx = (int)floorf(fminf(x0, fminf(x1, x2))), maxx = (int)ceilf(fmaxf(x0, fmaxf(x1, x2)));
                int miny = (int)floorf(fminf(y0, fminf(y1, y2))), maxy = (int)ceilf(fmaxf(y0, fmaxf(y1, y2)));
                if (minx < 0) minx = 0; if (miny < 0) miny = 0; if (maxx > W - 1) maxx = W - 1; if (maxy > H - 1) maxy = H - 1;
                if (minx > maxx || miny > maxy) continue;
                float ia = 1.0f / area;
                for (int py = miny; py <= maxy; py++) for (int px = minx; px <= maxx; px++) {
                    float fx = px + 0.5f, fy = py + 0.5f;
                    float w0 = ((x1 - fx) * (y2 - fy) - (x2 - fx) * (y1 - fy)) * ia, w1 = ((x2 - fx) * (y0 - fy) - (x0 - fx) * (y2 - fy)) * ia, w2 = 1 - w0 - w1;
                    if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                    float z = w0 * z0 + w1 * z1 + w2 * z2;
                    size_t k = (size_t)py * W + px;
                    if (z >= zb[k]) continue;
                    zb[k] = z;
                    float l = w0 * li[a] + w1 * li[b] + w2 * li[c];
                    const unsigned char *col = m.col + a * 4;
                    int r = (int)(col[0] * l), g = (int)(col[1] * l), bl = (int)(col[2] * l);
                    unsigned char *o = img + k * 4;
                    o[0] = (unsigned char)(bl > 255 ? 255 : bl); o[1] = (unsigned char)(g > 255 ? 255 : g); o[2] = (unsigned char)(r > 255 ? 255 : r); o[3] = 255;
                }
            }
            out = UiScaleToBox(img, W, H, boxW, boxH, ow, oh);
            size_t tris = m.ni / 3;
            if (tris >= 1000000) swprintf(note, noteN, L"%.1f million triangles", tris / 1e6);
            else swprintf(note, noteN, L"%zu triangles", tris);
        }
        free(zb); free(img); free(sp); free(li);
    }
    VoxFreeMesh(&m); VoxFreePart(&v->part); free(v);
    return out;
}
