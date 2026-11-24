// vox3d_core.h - load a DK Bananza voxel terrain part and build a smooth surface mesh.
// Portable C (no Windows headers): the including file must provide
//     unsigned char *VoxReadFile(const VOXCHAR *path, size_t *size);   // malloc'd, NULL if missing
// and define VOXCHAR / VOXSTR / VOXFMT / VOXSEP / VOXSPRINTF (wchar_t, L"", "ls", L"\\", swprintf on Windows).
//
// Part folder:  Info.bin  "VX", size in voxels at byte 12 (3 x u32), voxel scale at byte 24 (3 x f32)
//               vhead.bin bitmap, 1 bit per 8x8x8 block (MSB first; x fastest, then y, then z)
//               vbody.bin per set bit: u32 size + zstd frame of 512 voxels (x fastest) of 5 or 8 bytes:
//                         byte 0 fill (0 empty .. 255 solid), byte 1 material, byte 2 second material
//               ../MaterialTable.bin  u16 version, u16 count, count x 40-byte names
// Missing blocks are empty. The surface is built with "surface nets" at fill = 127.5.
#ifndef VOX3D_CORE_H
#define VOX3D_CORE_H
#define VOX_ISO_INT 127
#define TERA_TEX_REPEAT 4.0f              // MKW ground textures repeat every 4 game units (a guess)
#define TERA_WATER_REPEAT 160.0f          // ... and the water ripple texture every 160 (48 looked like small tiles)
#include "terrain_core.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include "zstd.h"

#define VOX_MAXMAT 4096              // list entries (materials / parts) a part can have; triangles store them in 2 bytes
#define VOX_MATN 64                  // longest list entry name + 1
typedef struct {
    int nx, ny, nz;              // size in voxels
    int bx, by, bz;              // size in 8x8x8 blocks
    float scale[3];
    int nblocks;                 // blocks with data
    int bytesPerVoxel;
    unsigned char **fill;        // [bx*by*bz] -> 512 fill bytes or NULL
    unsigned char **mat;         // [bx*by*bz] -> 512 material ids or NULL
    int nmat;
    char matName[VOX_MAXMAT][VOX_MATN];
    char shapeFull[256][96];     // model: each part's full shape name (matName is cut to 39 characters)
    char matFull[256][96];       // model: each part's material name in full
    uint64_t matCount[VOX_MAXMAT];      // solid-or-surface voxels per material
    uint64_t solid, surface;
    int isObject;                // loaded from VGEOB layers (voxel object) instead of a terrain part
    float cell;                  // object: CellSize
    int isTriMesh;               // a ready-made triangle mesh (Phive collision shape) instead of voxels
    float *tv; size_t ntv;       // tri mesh: vertices (x,y,z)
    uint32_t *ti; size_t nti;    // tri mesh: 3 vertex indices per triangle
    uint16_t *tmat;              // tri mesh: material per triangle
    signed char *tn;             // tri mesh: optional normals (4 per vertex) - models
    int sections;                // tri mesh: geometry sections
    int isModel, nbones, nmodels, nmaterials; char modelName[128];   // BFRES model
    float *tuv;                  // model: texture coordinates (2 per vertex), or NULL
    char texAlb[256][128], texOpc[256][128], texEmm[256][128];   // (texEmm: the emission (_e0), for state panels)   // model: per part, the albedo and opacity textures' names
    char texMix[256][128];                    // model: an "EmmMix" texture (eye shine), drawn added on top
    unsigned char startHidden[VOX_MAXMAT];          // model: parts the game hides until needed (their bone is hidden)
    short partBone[256];                     // model: the bone a part hangs on (visibility animations), -1 none
    // model skinning (single-model files): every vertex is stored in the bind pose and follows up to 8 bones
    int skN;                                 // bones in the skeleton (0 = can't animate)
    char (*skName)[64]; short *skParent; float (*skBind)[10]; unsigned char *skEuler;   // bind pose: scale 3, rotation 4, translation 3
    float *skBindW;                          // 12 per bone: bind-pose world matrix (3x4, row-major)
    uint16_t *vBone; float *vW;              // 8 per vertex
    float *lv; unsigned char *lmat; size_t nl, lcap;   // extra line geometry (cloth links, collision shapes): 6 floats + a part per line
    uint16_t *lbone; uint16_t lbCur[2];      // per line end: the bone it follows when animated (0xFFFF none); lbCur = for new lines
    // key shapes (morph targets, e.g. DK's face): per part, the offset of each vertex from the base shape
    struct VoxKey { int part; char name[32]; size_t v0; uint32_t nv; float *d; } *keys; int nkeys;
    char shapeType[128];
    float terrWorld[4];          // terrain world/area: world min x, min z (.tscb units), world_scale, world size
    int terrGeneric;             // terrain: not Mario Kart World's scene (Game03_Proc): plain material names / colours, its own textures
    float terrArea[3];           // terrain area: its min x, min z, size (.tscb units); size 0 = the whole world
    int overlay;                 // MKW terrain: the ground is coloured by another layer (VOV_..., 0 = game colours)
    unsigned char *gcol;         // height grid: the game's own colour per point (color_mul2 / albedo_far, RGB), or NULL
    int terrMat[VOX_MAXMAT];            // height grid: ground material number (0-61) of each material entry, -1 = none
    uint16_t *ttex;              // height grid: per triangle, the model texture slot of placed scenery (0xFFFF = none), or NULL
    uint32_t waterKindN[256];    // terrain: water cells per kind byte (the .water.extm's byte 7) in the tiles loaded
    int gridW;                   // tri mesh that is a square height grid (gridW x gridW points, rows of x): drawn smooth, vertices shared
    char err[300];
} VoxPart;

typedef struct {
    uint32_t *src;               // models: the part's vertex each mesh vertex came from (for animation), or NULL
    float *uv;                   // 2 per vertex (models with texture coordinates), or NULL
    float *pos;                  // 3 per vertex (world units, centred)
    signed char *nrm;            // 4 per vertex
    unsigned char *col;          // 4 per vertex
    uint32_t *idx;               // 3 per triangle
    size_t nv, ni, capv, capi;
    float bmin[3], bmax[3];      // bounds after centring
    float centre[3];             // what was subtracted
    uint16_t *tmatOut;           // height grid with ground textures: material per triangle as emitted (grouped), or NULL
    uint32_t *tkeyOut;           // height grid: draw group per emitted triangle (material, or VOX_MAXMAT + model texture slot), or NULL
} VoxMesh;

static void VoxFreePart(VoxPart *p) {
    free(p->tv); free(p->ti); free(p->tmat); free(p->tn); free(p->tuv);
    free(p->skName); free(p->skParent); free(p->skBind); free(p->skEuler); free(p->skBindW); free(p->vBone); free(p->vW);
    free(p->lv); free(p->lmat); free(p->lbone); free(p->gcol); free(p->ttex);
    for (int k = 0; k < p->nkeys; k++) free(p->keys[k].d);
    free(p->keys);
    int n = p->bx * p->by * p->bz;
    if (p->fill) for (int i = 0; i < n; i++) free(p->fill[i]);
    if (p->mat) for (int i = 0; i < n; i++) free(p->mat[i]);
    free(p->fill); free(p->mat);
    memset(p, 0, sizeof(*p));
}
static void VoxFreeMesh(VoxMesh *m) { free(m->tkeyOut); free(m->tmatOut); free(m->src); free(m->uv); free(m->pos); free(m->nrm); free(m->col); free(m->idx); memset(m, 0, sizeof(*m)); }

static uint32_t VoxRd32(const unsigned char *b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }

// dir = part folder (no trailing separator)
static int VoxLoad(VoxPart *p, const VOXCHAR *dir) {
    VOXCHAR path[4096];
    size_t n;
    memset(p, 0, sizeof(*p));
    VOXSPRINTF(path, 4096, VOXSTR("%") VOXFMT VOXSEP VOXSTR("Info.bin"), dir);
    unsigned char *info = VoxReadFile(path, &n);
    if (!info || n < 36 || info[0] != 'V' || info[1] != 'X') { free(info); snprintf(p->err, sizeof p->err, "Info.bin is missing or not a voxel part (VX)."); return 0; }
    p->nx = (int)VoxRd32(info + 12); p->ny = (int)VoxRd32(info + 16); p->nz = (int)VoxRd32(info + 20);
    memcpy(p->scale, info + 24, 12);
    free(info);
    if (p->nx <= 0 || p->ny <= 0 || p->nz <= 0 || p->nx > 100000 || p->ny > 100000 || p->nz > 100000) { snprintf(p->err, sizeof p->err, "Info.bin has an odd size (%d x %d x %d).", p->nx, p->ny, p->nz); return 0; }
    for (int k = 0; k < 3; k++) if (!(p->scale[k] > 0.0001f && p->scale[k] < 1000.f)) p->scale[k] = 1.f;
    p->bx = (p->nx + 7) / 8; p->by = (p->ny + 7) / 8; p->bz = (p->nz + 7) / 8;
    size_t nb = (size_t)p->bx * p->by * p->bz;
    if (nb > 50000000) { snprintf(p->err, sizeof p->err, "Part is too big (%zu blocks).", nb); return 0; }
    p->fill = (unsigned char **)calloc(nb, sizeof(char *));
    p->mat  = (unsigned char **)calloc(nb, sizeof(char *));
    if (!p->fill || !p->mat) { snprintf(p->err, sizeof p->err, "Out of memory."); return 0; }

    VOXSPRINTF(path, 4096, VOXSTR("%") VOXFMT VOXSEP VOXSTR("vhead.bin"), dir);
    size_t hn; unsigned char *head = VoxReadFile(path, &hn);
    VOXSPRINTF(path, 4096, VOXSTR("%") VOXFMT VOXSEP VOXSTR("vbody.bin"), dir);
    size_t bn; unsigned char *body = VoxReadFile(path, &bn);
    if (!head || !body) { free(head); free(body); snprintf(p->err, sizeof p->err, "vhead.bin or vbody.bin is missing."); return 0; }

    ZSTD_DCtx *dc = ZSTD_createDCtx();
    unsigned char *tmp = (unsigned char *)malloc(512 * 16);
    size_t pos = 0;
    for (size_t i = 0; i < nb && i < hn * 8; i++) {
        if (!((head[i >> 3] >> (7 - (i & 7))) & 1)) continue;
        if (pos + 4 > bn) { snprintf(p->err, sizeof p->err, "vbody.bin ends early (block %d).", p->nblocks); break; }
        uint32_t csz = VoxRd32(body + pos);
        if (pos + 4 + csz > bn) { snprintf(p->err, sizeof p->err, "vbody.bin ends early (block %d).", p->nblocks); break; }
        size_t got = ZSTD_decompressDCtx(dc, tmp, 512 * 16, body + pos + 4, csz);
        pos += 4 + csz;
        if (ZSTD_isError(got) || got % 512 || got / 512 < 3) { snprintf(p->err, sizeof p->err, "Block %d didn't decompress (%s).", p->nblocks, ZSTD_isError(got) ? ZSTD_getErrorName(got) : "odd size"); break; }
        int s = (int)(got / 512);
        p->bytesPerVoxel = s;
        unsigned char *f = (unsigned char *)malloc(512), *m = (unsigned char *)malloc(512);
        if (!f || !m) { free(f); free(m); snprintf(p->err, sizeof p->err, "Out of memory."); break; }
        for (int v = 0; v < 512; v++) {
            f[v] = tmp[v * s];
            unsigned char a = tmp[v * s + 1], b = tmp[v * s + 2];
            m[v] = a ? a : b;
            if (f[v]) { p->matCount[m[v]]++; if (f[v] == 255) p->solid++; else p->surface++; }
        }
        p->fill[i] = f; p->mat[i] = m; p->nblocks++;
    }
    ZSTD_freeDCtx(dc);
    free(tmp); free(head); free(body);
    if (p->err[0] && !p->nblocks) return 0;

    // material names from the stage folder above
    VOXSPRINTF(path, 4096, VOXSTR("%") VOXFMT, dir);
    {   // parent folder (no ".." - it doesn't work in \\?\ paths)
        size_t l = 0; while (path[l]) l++;
        while (l && (path[l - 1] == '/' || path[l - 1] == '\\')) path[--l] = 0;
        while (l && path[l - 1] != '/' && path[l - 1] != '\\') l--;
        path[l] = 0;
    }
    {   VOXCHAR tmpp[4096]; VOXSPRINTF(tmpp, 4096, VOXSTR("%") VOXFMT VOXSTR("MaterialTable.bin"), path);
        memcpy(path, tmpp, sizeof(tmpp)); }
    size_t mn; unsigned char *mt = VoxReadFile(path, &mn);
    if (mt && mn >= 4) {
        int cnt = mt[2] | (mt[3] << 8);
        for (int i = 0; i < cnt && i < 256 && 4 + (size_t)(i + 1) * 40 <= mn; i++) {
            memcpy(p->matName[i], mt + 4 + i * 40, 39); p->matName[i][39] = 0;
            p->nmat = i + 1;
        }
    }
    free(mt);
    return 1;
}

/* ---------------- voxel objects: ObjectData/Voxel/<name>.alsarc.zs (zstd + SARC of .vgeob layers) ----------------
   VGEOB: "VGEOB", u32 record count, records of 9 bytes walking the grid (x fastest, then y, then z):
          s8 signed distance (negative inside), u32 empty cells skipped first, u32 extra cells with the same value.
   Grid size and cell size: Voxel/Geometry/<name>.al__gen__VoxelGeometry (GridNum, CellSize) - read here from the
   extracted JSON, whose first "GridNum" and "CellSize" are the top-level ones. */
static unsigned char *VoxUnzstd(const unsigned char *src, size_t n, size_t *outN) {
    if (n < 4 || !(src[0] == 0x28 && src[1] == 0xB5 && src[2] == 0x2F && src[3] == 0xFD)) {   // not compressed
        unsigned char *c = (unsigned char *)malloc(n ? n : 1); if (c) memcpy(c, src, n); *outN = n; return c;
    }
    ZSTD_DStream *ds = ZSTD_createDStream(); if (!ds) return NULL;
    ZSTD_initDStream(ds);
    size_t cap = n * 4 + 65536, len = 0;
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
    ZSTD_freeDStream(ds);
    *outN = len; return out;
}
static uint16_t VoxRd16(const unsigned char *b) { return (uint16_t)(b[0] | (b[1] << 8)); }
static const char *VoxJsonNum(const char *js, const char *key, const char *sub, double *val) {
    const char *k = strstr(js, key); if (!k) return NULL;
    if (sub) { k = strstr(k, sub); if (!k) return NULL; }
    k = strchr(k, ':'); if (!k) return NULL;
    *val = strtod(k + 1, NULL); return k;
}
static int VoxLoadObject(VoxPart *p, const VOXCHAR *archive, const VOXCHAR *geomJson) {
    memset(p, 0, sizeof(*p));
    p->isObject = 1;
    size_t jn; unsigned char *js = VoxReadFile(geomJson, &jn);
    if (!js) { snprintf(p->err, sizeof p->err, "The object's VoxelGeometry file (grid size) wasn't found in this scan."); return 0; }
    js[jn] = 0;
    double gx = 0, gy = 0, gz = 0, cs = 1;
    const char *g = strstr((char *)js, "\"GridNum\"");
    if (g) { VoxJsonNum(g, "\"X\"", NULL, &gx); VoxJsonNum(g, "\"Y\"", NULL, &gy); VoxJsonNum(g, "\"Z\"", NULL, &gz); }
    VoxJsonNum((char *)js, "\"CellSize\"", NULL, &cs);
    free(js);
    if (gx < 1 || gy < 1 || gz < 1 || gx * gy * gz > 4e8) { snprintf(p->err, sizeof p->err, "The VoxelGeometry file has no usable GridNum."); return 0; }
    if (!(cs > 0.0001 && cs < 1000)) cs = 1;
    p->nx = (int)gx; p->ny = (int)gy; p->nz = (int)gz; p->cell = (float)cs;
    p->scale[0] = p->scale[1] = p->scale[2] = (float)cs;
    p->bytesPerVoxel = 9;
    p->bx = (p->nx + 7) / 8; p->by = (p->ny + 7) / 8; p->bz = (p->nz + 7) / 8;
    size_t nb = (size_t)p->bx * p->by * p->bz;
    p->fill = (unsigned char **)calloc(nb, sizeof(char *));
    p->mat  = (unsigned char **)calloc(nb, sizeof(char *));
    if (!p->fill || !p->mat) { snprintf(p->err, sizeof p->err, "Out of memory."); return 0; }

    size_t zn; unsigned char *z = VoxReadFile(archive, &zn);
    if (!z) { snprintf(p->err, sizeof p->err, "The object archive couldn't be read from the dump."); return 0; }
    size_t sn; unsigned char *s = VoxUnzstd(z, zn, &sn); free(z);
    if (!s || sn < 0x20 || memcmp(s, "SARC", 4) || s[6] != 0xFF || s[7] != 0xFE) {   // little-endian SARC only
        free(s); snprintf(p->err, sizeof p->err, "The object archive isn't a SARC (or didn't decompress)."); return 0; }
    uint32_t dataOff = VoxRd32(s + 0x0C);
    size_t o = VoxRd16(s + 4);
    if (o + 12 > sn || memcmp(s + o, "SFAT", 4)) { free(s); snprintf(p->err, sizeof p->err, "Bad SARC (no SFAT)."); return 0; }
    int nf = VoxRd16(s + o + 6);
    size_t ents = o + 12, names = ents + (size_t)nf * 16 + 8;
    if (names > sn) { free(s); snprintf(p->err, sizeof p->err, "Bad SARC (file table)."); return 0; }
    for (int f = 0; f < nf; f++) {
        const unsigned char *e = s + ents + (size_t)f * 16;
        uint32_t attr = VoxRd32(e + 4), st = VoxRd32(e + 8), en = VoxRd32(e + 12);
        if (!(attr >> 24) || (size_t)dataOff + en > sn || en < st) continue;
        const char *nm = (const char *)s + names + (size_t)(attr & 0xFFFFFF) * 4;
        if ((const unsigned char *)nm >= s + sn) continue;
        const unsigned char *b = s + dataOff + st; size_t bl = en - st;
        if (bl < 9 || memcmp(b, "VGEOB", 5)) continue;
        if (p->nmat >= 255) break;
        int id = p->nmat++;
        {   const char *sl = strrchr(nm, '/'); const char *base = sl ? sl + 1 : nm;
            snprintf(p->matName[id], VOX_MATN, "%s", base);
            char *dot = strstr(p->matName[id], ".vgeob"); if (dot) *dot = 0; }
        uint32_t nrec = VoxRd32(b + 5);
        if (9 + (size_t)nrec * 9 > bl) { snprintf(p->err, sizeof p->err, "%s is shorter than its record count.", p->matName[id]); nrec = (uint32_t)((bl - 9) / 9); }
        uint64_t pos = 0, total = (uint64_t)p->nx * p->ny * p->nz;
        for (uint32_t r = 0; r < nrec; r++) {
            const unsigned char *q = b + 9 + (size_t)r * 9;
            int v = (signed char)q[0];
            pos += VoxRd32(q + 1);
            uint64_t cnt = (uint64_t)VoxRd32(q + 5) + 1;
            int fv = 127 - v; if (fv < 0) fv = 0; if (fv > 255) fv = 255;
            for (uint64_t c = 0; c < cnt && pos < total; c++, pos++) {
                if (!fv) continue;
                int x = (int)(pos % p->nx), y = (int)((pos / p->nx) % p->ny), zz = (int)(pos / ((uint64_t)p->nx * p->ny));
                size_t bi = ((size_t)(zz >> 3) * p->by + (y >> 3)) * p->bx + (x >> 3);
                if (!p->fill[bi]) {
                    p->fill[bi] = (unsigned char *)calloc(512, 1); p->mat[bi] = (unsigned char *)calloc(512, 1);
                    if (!p->fill[bi] || !p->mat[bi]) { free(s); snprintf(p->err, sizeof p->err, "Out of memory."); return 0; }
                    p->nblocks++;
                }
                int i = ((zz & 7) * 8 + (y & 7)) * 8 + (x & 7);
                if (fv > p->fill[bi][i]) { p->fill[bi][i] = (unsigned char)fv; p->mat[bi][i] = (unsigned char)id; }
            }
        }
    }
    free(s);
    if (!p->nmat) { snprintf(p->err, sizeof p->err, "No .vgeob layers in this archive."); return 0; }
    // counts per layer (after overlaps are resolved)
    for (size_t bi = 0; bi < nb; bi++) if (p->fill[bi])
        for (int i = 0; i < 512; i++) { int f = p->fill[bi][i]; if (f > VOX_ISO_INT) { p->matCount[p->mat[bi][i]]++; p->solid++; } else if (f) p->surface++; }
    return 1;
}

static void VoxMatColour(const VoxPart *p, int id, unsigned char out[3]);
/* ---------------- Phive collision shapes (.bphsh): "Phive" header + Havok 2022 tagfile with an hknpMeshShape ----------------
   The tagfile describes its own types (TNA1/TBDY), so member offsets are looked up by name.
   Vertices: 3 x u16 per vertex, position = (value + sectionOffset) * bitScale16Inv; a section whose
   sectionOffset.x is 0x7FFFFFFF holds plain floats instead. Primitives: 4 vertex indices (a triangle if c == d).
   Materials: shapeTagTable gives each shape key ((section << 9) | (primitive << 1)) range an index into the
   Phive material table that follows the tagfile (16 bytes each). */
typedef struct { const unsigned char *b; size_t o, e; int bad; } HkR;
static uint64_t HkVi(HkR *r) {
    if (r->o >= r->e) { r->bad = 1; return 0; }
    unsigned b0 = r->b[r->o++]; int n; uint64_t v;
    if (b0 < 0x80) return b0;
    if ((b0 & 0xC0) == 0x80) { n = 1; v = b0 & 0x3F; }
    else if ((b0 & 0xE0) == 0xC0) { n = 2; v = b0 & 0x1F; }
    else if ((b0 & 0xF8) == 0xE0) { n = 3; v = b0 & 7; }
    else if ((b0 & 0xF8) == 0xE8) { n = 4; v = b0 & 7; }
    else if ((b0 & 0xF8) == 0xF0) { n = 7; v = b0 & 7; }
    else if (b0 == 0xF8 || b0 == 0xF9) { n = 8; v = 0; }
    else { r->bad = 1; return 0; }
    for (int i = 0; i < n; i++) { if (r->o >= r->e) { r->bad = 1; return 0; } v = (v << 8) | r->b[r->o++]; }
    return v;
}
typedef struct { int name, parent, format, subtype, size; int nmem; int memName[64], memOff[64], memType[64]; } HkType;
typedef struct { const unsigned char *b; size_t n; HkType *T; int nt; const char **tst; int ntst; const char **fst; int nfst; size_t d0, itemA, itemE; } HkFile;
static int HkFind(const unsigned char *b, size_t o, size_t e, const char *tag, size_t *a, size_t *z) {
    while (o + 8 <= e) {
        uint32_t h = ((uint32_t)b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]; size_t sz = h & 0x3FFFFFFF;
        if (sz < 8 || o + sz > e) return 0;
        if (!memcmp(b + o + 4, tag, 4)) { *a = o + 8; *z = o + sz; return 1; }
        if ((h >> 30) == 0 && (!memcmp(b + o + 4, "TAG0", 4) || !memcmp(b + o + 4, "TYPE", 4) || !memcmp(b + o + 4, "INDX", 4)))
            if (HkFind(b, o + 8, o + sz, tag, a, z)) return 1;
        o += sz;
    }
    return 0;
}
static const char **HkStrings(const unsigned char *b, size_t a, size_t e, int *n) {
    int c = 0; for (size_t i = a; i < e; i++) if (!b[i]) c++;
    const char **s = (const char **)calloc(c + 1, sizeof(char *)); if (!s) return NULL;
    int k = 0; size_t st = a;
    for (size_t i = a; i < e && k < c; i++) if (!b[i]) { s[k++] = (const char *)b + st; st = i + 1; }
    *n = k; return s;
}
static void HkFreeFile(HkFile *h) { free(h->T); free(h->tst); free(h->fst); }
static int HkOpen(HkFile *h, const unsigned char *b, size_t n, size_t start) {
    memset(h, 0, sizeof(*h)); h->b = b; h->n = n;
    size_t a, e, ta, te, fa, fe;
    if (!HkFind(b, start, n, "DATA", &a, &e)) return 0;
    h->d0 = a;
    if (!HkFind(b, start, n, "TST1", &ta, &te) || !HkFind(b, start, n, "FST1", &fa, &fe)) return 0;
    h->tst = HkStrings(b, ta, te, &h->ntst); h->fst = HkStrings(b, fa, fe, &h->nfst);
    if (!h->tst || !h->fst) return 0;
    if (!HkFind(b, start, n, "TNA1", &a, &e)) return 0;
    HkR r = { b, a, e, 0 };
    int nt = (int)HkVi(&r); if (nt <= 0 || nt > 100000) return 0;
    h->T = (HkType *)calloc(nt, sizeof(HkType)); h->nt = nt; if (!h->T) return 0;
    for (int i = 1; i < nt && !r.bad; i++) {
        h->T[i].name = (int)HkVi(&r); int np = (int)HkVi(&r);
        for (int k = 0; k < np; k++) { HkVi(&r); HkVi(&r); }
    }
    if (!HkFind(b, start, n, "TBDY", &a, &e)) return 0;
    r.o = a; r.e = e;
    while (r.o < r.e && !r.bad) {
        int ti = (int)HkVi(&r); if (!ti || ti >= nt) break;
        HkType *t = &h->T[ti];
        t->parent = (int)HkVi(&r); int fl = (int)HkVi(&r);
        if (fl & 1) t->format = (int)HkVi(&r);
        if (fl & 2) t->subtype = (int)HkVi(&r);
        if (fl & 4) HkVi(&r);
        if (fl & 8) { t->size = (int)HkVi(&r); HkVi(&r); }
        if (fl & 0x10) HkVi(&r);
        if (fl & 0x20) {
            int nm = (int)HkVi(&r);
            for (int k = 0; k < nm; k++) {
                int a1 = (int)HkVi(&r); int mf = (int)HkVi(&r); if (mf & 0x80) HkVi(&r);   // (an attribute index first)
                int a3 = (int)HkVi(&r); int a4 = (int)HkVi(&r);
                if (t->nmem < 64) { t->memName[t->nmem] = a1; t->memOff[t->nmem] = a3; t->memType[t->nmem] = a4; t->nmem++; }
            }
        }
        if (fl & 0x40) { int ni = (int)HkVi(&r); for (int k = 0; k < ni; k++) { HkVi(&r); HkVi(&r); } }
        if (fl & 0x80) HkVi(&r);
    }
    if (!HkFind(b, start, n, "ITEM", &h->itemA, &h->itemE)) return 0;
    return !r.bad;
}
static const char *HkName(const HkFile *h, int ti) { return ti > 0 && ti < h->nt && h->T[ti].name < h->ntst ? h->tst[h->T[ti].name] : ""; }
static int HkIsA(const HkFile *h, int ti, const char *nm) {
    for (int g = 0; ti > 0 && ti < h->nt && g < 64; g++, ti = h->T[ti].parent) if (!strcmp(HkName(h, ti), nm)) return 1;
    return 0;
}
static int HkBody(const HkFile *h, int ti) {            // follow typedef-like entries
    for (int g = 0; ti > 0 && ti < h->nt && !h->T[ti].format && h->T[ti].parent && g < 64; g++) ti = h->T[ti].parent;
    return ti;
}
// offset (and type) of a member, searching parents too
static int HkMember(const HkFile *h, int ti, const char *nm, int *off, int *type) {
    for (int g = 0; ti > 0 && ti < h->nt && g < 64; g++, ti = h->T[ti].parent)
        for (int k = 0; k < h->T[ti].nmem; k++)
            if (h->T[ti].memName[k] < h->nfst && !strcmp(h->fst[h->T[ti].memName[k]], nm)) { *off = h->T[ti].memOff[k]; if (type) *type = h->T[ti].memType[k]; return 1; }
    return 0;
}
static int HkSize(const HkFile *h, int ti) {
    for (int g = 0; ti > 0 && ti < h->nt && g < 64; g++, ti = h->T[ti].parent) if (h->T[ti].size) return h->T[ti].size;
    return 0;
}
static float BfFloat_(const unsigned char *b) { uint32_t u = VoxRd32(b); float f; memcpy(&f, &u, 4); return f; }
/* ---------------- Havok objects: following pointers, arrays and strings (items) ---------------- */
static int HkItemAt(const HkFile *h, uint32_t idx, int *type, size_t *addr, int *cnt) {
    size_t o = h->itemA + (size_t)idx * 12;
    if (!idx || o + 12 > h->itemE) return 0;
    *type = (int)(VoxRd32(h->b + o) & 0xFFFFFF); *addr = h->d0 + VoxRd32(h->b + o + 4); *cnt = (int)VoxRd32(h->b + o + 8);
    return *addr < h->n;
}
static int HkKind(const HkFile *h, int ti) { ti = HkBody(h, ti); return ti > 0 ? (h->T[ti].format & 0xF) : 0; }
static int HkNMem(const HkFile *h, int ti) { int n = 0; for (int g = 0; ti > 0 && ti < h->nt && g < 64; g++, ti = h->T[ti].parent) n += h->T[ti].nmem; return n; }
// unwrap one-member wrappers (hkRefPtr, hkStringPtr...) and follow pointers to the object
static int HkResolve(const HkFile *h, int *ti, size_t *addr) {
    for (int g = 0; g < 16; g++) {
        int b = HkBody(h, *ti); if (b <= 0) return 0;
        int kind = h->T[b].format & 0xF;
        if (kind == 7 && HkNMem(h, b) == 1) {
            int t2 = b; while (t2 > 0 && !h->T[t2].nmem) t2 = h->T[t2].parent;
            if (t2 <= 0) return 0;
            *addr += (size_t)h->T[t2].memOff[0]; *ti = h->T[t2].memType[0]; continue;
        }
        if (kind == 6) { int it, c; size_t ad; if (*addr + 4 > h->n || !HkItemAt(h, VoxRd32(h->b + *addr), &it, &ad, &c)) return 0; *ti = it; *addr = ad; continue; }
        return 1;
    }
    return 0;
}
static int HkField(const HkFile *h, int *ti, size_t *addr, const char *nm) {
    int off, t; if (!HkMember(h, HkBody(h, *ti), nm, &off, &t)) return 0;
    *ti = t; *addr += (size_t)off; return 1;
}
// an hkArray member: element type, first element, count
static int HkArr(const HkFile *h, int ti, size_t addr, const char *nm, int *et, size_t *e0, int *cnt) {
    if (nm && !HkField(h, &ti, &addr, nm)) return 0;
    int b = HkBody(h, ti); if (b <= 0 || (h->T[b].format & 0xF) != 8 || (h->T[b].format & 0x20)) return 0;
    if (addr + 4 > h->n) return 0;
    uint32_t idx = VoxRd32(h->b + addr); if (!idx) { *cnt = 0; return 1; }
    return HkItemAt(h, idx, et, e0, cnt);
}
static void HkStr(const HkFile *h, int ti, size_t addr, const char *nm, char *out, size_t n) {
    out[0] = 0;
    if (nm && !HkField(h, &ti, &addr, nm)) return;
    if (!HkResolve(h, &ti, &addr)) return;
    if (HkKind(h, ti) == 3) { int it, c; size_t ad; if (addr + 4 > h->n || !HkItemAt(h, VoxRd32(h->b + addr), &it, &ad, &c)) return; addr = ad; }
    size_t i = 0; while (addr + i < h->n && h->b[addr + i] && i < n - 1) { out[i] = (char)h->b[addr + i]; i++; } out[i] = 0;
}
static float HkF32(const HkFile *h, int ti, size_t addr, const char *nm) {
    if (nm && !HkField(h, &ti, &addr, nm)) return 0;
    return addr + 4 <= h->n ? BfFloat_(h->b + addr) : 0;
}

/* ---------------- Phive cloth (.bphcl, Havok Cloth): particles, links and collision shapes to draw ---------------- */
typedef struct { char name[64]; int type; float T[16]; float a[4], b[4], r; } ClShape;      // type 0 capsule, 1 sphere, 2 plane
typedef struct { char name[64]; int *pair; int n; } ClLinks;
typedef struct { char name[64]; float *pos; int np; unsigned char *pinned; float rad; ClLinks sets[12]; int nsets; ClShape *sh; int nsh; } ClothVis;
static void ClothFree(ClothVis *c) { free(c->pos); free(c->pinned); for (int i = 0; i < c->nsets; i++) free(c->sets[i].pair); free(c->sh); memset(c, 0, sizeof *c); }
static int ClothRead(ClothVis *c, const unsigned char *b, size_t n, char *err, size_t en) {
    memset(c, 0, sizeof *c);
    if (n < 0x40 || memcmp(b, "Phive", 5)) { snprintf(err, en, "Not a Phive file."); return 0; }
    HkFile h; if (!HkOpen(&h, b, n, 0x30)) { HkFreeFile(&h); snprintf(err, en, "The Havok data couldn't be read."); return 0; }
    int ok = 0, ti, cnt; size_t addr;
    if (!HkItemAt(&h, 1, &ti, &addr, &cnt)) { snprintf(err, en, "No root object."); goto done; }
    int vt; size_t v0; int nv;
    if (!HkArr(&h, ti, addr, "namedVariants", &vt, &v0, &nv)) { snprintf(err, en, "No named variants."); goto done; }
    int es = HkSize(&h, vt);
    for (int i = 0; i < nv; i++) {
        size_t va = v0 + (size_t)i * es; char cls[64]; HkStr(&h, vt, va, "className", cls, sizeof cls);
        if (strcmp(cls, "hclClothContainer")) continue;
        int ct = vt; size_t ca = va;
        if (!HkField(&h, &ct, &ca, "variant") || !HkResolve(&h, &ct, &ca)) continue;
        // collision shapes
        int et; size_t e0; int ne;
        if (HkArr(&h, ct, ca, "collidables", &et, &e0, &ne) && ne > 0) {
            c->sh = (ClShape *)calloc(ne, sizeof(ClShape));
            int ees = HkSize(&h, et);
            for (int k = 0; k < ne && c->sh; k++) {
                int t = et; size_t a = e0 + (size_t)k * ees; if (!HkResolve(&h, &t, &a)) continue;
                ClShape *S = &c->sh[c->nsh];
                HkStr(&h, t, a, "name", S->name, sizeof S->name);
                int tt = t; size_t ta = a; if (HkField(&h, &tt, &ta, "transform") && ta + 64 <= h.n) for (int q = 0; q < 16; q++) S->T[q] = BfFloat_(h.b + ta + q * 4);
                int st = t; size_t sa = a; if (!HkField(&h, &st, &sa, "shape") || !HkResolve(&h, &st, &sa)) continue;
                const char *sn = HkName(&h, HkBody(&h, st));
                if (HkIsA(&h, st, "hclCapsuleShape")) {
                    S->type = 0; int q; size_t qa;
                    q = st; qa = sa; if (HkField(&h, &q, &qa, "start")) for (int j = 0; j < 4; j++) S->a[j] = BfFloat_(h.b + qa + j * 4);
                    q = st; qa = sa; if (HkField(&h, &q, &qa, "end")) for (int j = 0; j < 4; j++) S->b[j] = BfFloat_(h.b + qa + j * 4);
                    S->r = HkF32(&h, st, sa, "radius");
                } else if (HkIsA(&h, st, "hclSphereShape")) {
                    S->type = 1; int q = st; size_t qa = sa;
                    if (HkField(&h, &q, &qa, "sphere")) { for (int j = 0; j < 4; j++) S->a[j] = BfFloat_(h.b + qa + j * 4); S->r = S->a[3]; }
                } else if (HkIsA(&h, st, "hclPlaneShape")) {
                    S->type = 2; int q = st; size_t qa = sa;
                    if (HkField(&h, &q, &qa, "planeEquation")) for (int j = 0; j < 4; j++) S->a[j] = BfFloat_(h.b + qa + j * 4);
                } else { (void)sn; continue; }
                c->nsh++;
            }
        }
        // the first simulated cloth
        if (!HkArr(&h, ct, ca, "clothDatas", &et, &e0, &ne) || ne < 1) continue;
        int dt = et; size_t da = e0; if (!HkResolve(&h, &dt, &da)) continue;
        HkStr(&h, dt, da, "name", c->name, sizeof c->name);
        int st; size_t s0; int ns;
        if (!HkArr(&h, dt, da, "simClothDatas", &st, &s0, &ns) || ns < 1) continue;
        int sd = st; size_t sda = s0; if (!HkResolve(&h, &sd, &sda)) continue;
        int pt; size_t p0; int np;
        if (HkArr(&h, sd, sda, "simClothPoses", &pt, &p0, &np) && np > 0) {
            int pp = pt; size_t pa = p0;
            int qt; size_t q0; int nq;
            if (HkResolve(&h, &pp, &pa) && HkArr(&h, pp, pa, "positions", &qt, &q0, &nq) && nq > 0 && nq < 100000) {
                c->pos = (float *)malloc((size_t)nq * 12); c->pinned = (unsigned char *)calloc(nq, 1);
                int qs = HkSize(&h, qt); if (qs < 12) qs = 16;
                for (int k = 0; k < nq && c->pos; k++) for (int j = 0; j < 3; j++) c->pos[k * 3 + j] = BfFloat_(h.b + q0 + (size_t)k * qs + j * 4);
                c->np = c->pos ? nq : 0;
            }
        }
        if (HkArr(&h, sd, sda, "fixedParticles", &pt, &p0, &np))
            for (int k = 0; k < np; k++) { unsigned f = VoxRd16(h.b + p0 + (size_t)k * 2); if ((int)f < c->np) c->pinned[f] = 1; }
        if (HkArr(&h, sd, sda, "particleDatas", &pt, &p0, &np) && np > 0) c->rad = HkF32(&h, pt, p0, "radius");
        if (HkArr(&h, sd, sda, "staticConstraintSets", &pt, &p0, &np)) {
            int ps = HkSize(&h, pt);
            for (int k = 0; k < np && c->nsets < 12; k++) {
                int cs = pt; size_t csa = p0 + (size_t)k * ps; if (!HkResolve(&h, &cs, &csa)) continue;
                const char *kind = HkIsA(&h, cs, "hclStandardLinkConstraintSet") ? "Standard" : HkIsA(&h, cs, "hclStretchLinkConstraintSet") ? "Stretch" :
                                   HkIsA(&h, cs, "hclBendLinkConstraintSet") ? "Bend" : NULL;
                if (!kind) continue;
                int lt; size_t l0; int nl;
                if (!HkArr(&h, cs, csa, "links", &lt, &l0, &nl) || nl <= 0 || nl > 200000) continue;
                int oa, ob; if (!HkMember(&h, HkBody(&h, lt), "particleA", &oa, NULL) || !HkMember(&h, HkBody(&h, lt), "particleB", &ob, NULL)) continue;
                ClLinks *L = &c->sets[c->nsets]; char sn[48]; HkStr(&h, cs, csa, "name", sn, sizeof sn);
                snprintf(L->name, sizeof L->name, "%s", kind); (void)sn;
                L->pair = (int *)malloc((size_t)nl * 8); if (!L->pair) continue;
                int ls = HkSize(&h, lt);
                for (int q = 0; q < nl; q++) { L->pair[q * 2] = VoxRd16(h.b + l0 + (size_t)q * ls + oa); L->pair[q * 2 + 1] = VoxRd16(h.b + l0 + (size_t)q * ls + ob); }
                L->n = nl; c->nsets++;
            }
        }
        ok = c->np > 0 || c->nsh > 0;
        break;
    }
    if (!ok && !err[0]) snprintf(err, en, "No cloth was found in this file.");
done:
    HkFreeFile(&h);
    return ok;
}
/* ---------------- cloth simulation (DK's tie and hair) ----------------
   The game's cloth is a few particles per strand; each simulated bone of the model sits on one particle (a hair tip)
   or halfway between two (a tie segment, a hair joint). Pinned particles ride on the animated skeleton; the rest
   fall under gravity, held by the cloth's link constraints (Standard / Stretch = keep length, Bend = stiffer shape)
   and pushed out of its collision capsules. The bones on simulated particles are then placed from them: position =
   the particles' centre, rotation = the turn from the bind pose (towards the next bone along the strand, and for a
   pair, across it). Position-based dynamics, a few substeps per frame. */
static void VoxXf(const float *M, const float *in, float *out);
static uint16_t VoxNearestBone(const VoxPart *p, const float *pt);
typedef struct { int a, b; float rest; unsigned char kind; } ClSimLink;              // kind 0 standard, 1 stretch, 2 bend
typedef struct { int bone, p[2], np, child; float bindAlong[3], bindAcross[3], bindCen[3]; int hasAcross; } ClSimBone;
typedef struct { int bone, type; float a[3], b[3], r; } ClSimShape;                   // in model space at bind; follows bone
typedef struct {
    int np; float *bind, *pos, *prev; unsigned char *pinned; uint16_t *pinBone; float rad;
    ClSimLink *L; int nl; ClSimBone *B; int nb; ClSimShape *S; int ns; int ok;
} ClothSim;
static void ClothSimFree(ClothSim *c) { free(c->bind); free(c->pos); free(c->prev); free(c->pinned); free(c->pinBone); free(c->L); free(c->B); free(c->S); memset(c, 0, sizeof *c); }
static float ClDist(const float *a, const float *b) { float d0 = a[0] - b[0], d1 = a[1] - b[1], d2 = a[2] - b[2]; return sqrtf(d0 * d0 + d1 * d1 + d2 * d2); }
static void ClSkin(const float *M, const float *in, float *out) { for (int r = 0; r < 3; r++) out[r] = M[r * 4] * in[0] + M[r * 4 + 1] * in[1] + M[r * 4 + 2] * in[2] + M[r * 4 + 3]; }
// lo..hi: the bones of the model the cloth belongs to (an attached tie / head), so another model's bones aren't taken
static int ClothSimInit(ClothSim *c, const ClothVis *v, const float *X, const VoxPart *p, int lo, int hi) {
    memset(c, 0, sizeof *c);
    if (v->np < 2 || !p->skN || !p->skBindW) return 0;
    int n = v->np; c->np = n; c->rad = v->rad > 0 ? v->rad : 0.02f;
    c->bind = (float *)malloc((size_t)n * 12); c->pos = (float *)malloc((size_t)n * 12); c->prev = (float *)malloc((size_t)n * 12);
    c->pinned = (unsigned char *)calloc(n, 1); c->pinBone = (uint16_t *)malloc((size_t)n * 2);
    if (!c->bind || !c->pos || !c->prev || !c->pinned || !c->pinBone) { ClothSimFree(c); return 0; }
    if (hi > p->skN || hi <= lo) { lo = 0; hi = p->skN; }
    for (int i = 0; i < n; i++) {
        VoxXf(X, v->pos + i * 3, c->bind + i * 3); c->pinned[i] = v->pinned ? v->pinned[i] : 0;
        // the bone carrying this particle when it isn't simulated: the nearest joint of the cloth's own model
        int best = -1; float bd = 1e30f; const float *q = c->bind + i * 3;
        for (int k = lo; k < hi; k++) { const char *nm = p->skName[k]; if (!strcmp(nm, "nw4f_root") || !strcmp(nm, "Model")) continue;
            const float *W = p->skBindW + (size_t)k * 12; float d = (W[3] - q[0]) * (W[3] - q[0]) + (W[7] - q[1]) * (W[7] - q[1]) + (W[11] - q[2]) * (W[11] - q[2]); if (d < bd) { bd = d; best = k; } }
        c->pinBone[i] = best < 0 ? 0 : (uint16_t)best;
    }
    memcpy(c->pos, c->bind, (size_t)n * 12); memcpy(c->prev, c->bind, (size_t)n * 12);
    int nl = 0; for (int s = 0; s < v->nsets; s++) nl += v->sets[s].n;
    c->L = (ClSimLink *)malloc((size_t)(nl + 1) * sizeof(ClSimLink));
    for (int s = 0; s < v->nsets && c->L; s++) {
        unsigned char kind = !strcmp(v->sets[s].name, "Stretch") ? 1 : !strcmp(v->sets[s].name, "Bend") ? 2 : 0;
        for (int q = 0; q < v->sets[s].n; q++) { int a = v->sets[s].pair[q * 2], b = v->sets[s].pair[q * 2 + 1]; if (a >= n || b >= n || a == b) continue;
            ClSimLink *L = &c->L[c->nl++]; L->a = a; L->b = b; L->kind = kind; L->rest = ClDist(c->bind + a * 3, c->bind + b * 3); }
    }
    // bones on particles (one, or the middle of two)
    float ext = 0; for (int i = 1; i < n; i++) { float d = ClDist(c->bind, c->bind + i * 3); if (d > ext) ext = d; }
    float tol = ext * 0.03f + 1e-4f;                 // bones sit near (not exactly on) their particles' centre
    c->B = (ClSimBone *)calloc((size_t)(hi - lo) + 1, sizeof(ClSimBone));
    int *boneSlot = (int *)malloc((size_t)p->skN * sizeof(int)); if (!c->B || !boneSlot) { free(boneSlot); ClothSimFree(c); return 0; }
    for (int k = 0; k < p->skN; k++) boneSlot[k] = -1;
    for (int k = lo; k < hi; k++) {
        const float *W = p->skBindW + (size_t)k * 12; float J[3] = { W[3], W[7], W[11] };
        int pa = -1, pb = -1;
        for (int i = 0; i < n && pa < 0; i++) if (ClDist(c->bind + i * 3, J) < tol) pa = i;
        if (pa < 0) {
            float bestd = tol;
            for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) {
                float m[3] = { (c->bind[i * 3] + c->bind[j * 3]) / 2, (c->bind[i * 3 + 1] + c->bind[j * 3 + 1]) / 2, (c->bind[i * 3 + 2] + c->bind[j * 3 + 2]) / 2 };
                float d = ClDist(m, J); if (d < bestd && ClDist(c->bind + i * 3, c->bind + j * 3) < ext * 0.6f) { bestd = d; pa = i; pb = j; }
            }
        }
        if (pa < 0) continue;
        if (c->pinned[pa] && (pb < 0 || c->pinned[pb])) continue;       // rides on the animation anyway
        ClSimBone *Bn = &c->B[c->nb]; Bn->bone = k; Bn->p[0] = pa; Bn->p[1] = pb; Bn->np = pb < 0 ? 1 : 2; Bn->child = -1;
        if (pb >= 0) { for (int q = 0; q < 3; q++) Bn->bindAcross[q] = c->bind[pb * 3 + q] - c->bind[pa * 3 + q]; Bn->hasAcross = 1; }
        for (int q = 0; q < 3; q++) Bn->bindCen[q] = pb >= 0 ? (c->bind[pa * 3 + q] + c->bind[pb * 3 + q]) / 2 : c->bind[pa * 3 + q];
        boneSlot[k] = c->nb++;
    }
    // along the strand: towards the child bone that is also simulated
    for (int t = 0; t < c->nb; t++) {
        ClSimBone *Bn = &c->B[t];
        for (int u = 0; u < c->nb; u++) if (p->skParent[c->B[u].bone] == Bn->bone) { Bn->child = u; break; }
        if (Bn->child >= 0) for (int q = 0; q < 3; q++) Bn->bindAlong[q] = c->B[Bn->child].bindCen[q] - Bn->bindCen[q];
    }
    free(boneSlot);
    // collision shapes (skipping the Bananza forms' ones), each riding on the bone nearest its centre
    c->S = (ClSimShape *)calloc((size_t)v->nsh + 1, sizeof(ClSimShape));
    for (int i = 0; i < v->nsh && c->S; i++) {
        const ClShape *S = &v->sh[i]; if (strstr(S->name, "Style") || S->type == 2) continue;
        float M[12] = { S->T[0], S->T[4], S->T[8], S->T[12], S->T[1], S->T[5], S->T[9], S->T[13], S->T[2], S->T[6], S->T[10], S->T[14] }, A[3], Bq[3];
        VoxXf(M, S->a, A); VoxXf(M, S->type == 0 ? S->b : S->a, Bq);
        ClSimShape *T = &c->S[c->ns]; T->type = S->type; T->r = S->r; VoxXf(X, A, T->a); VoxXf(X, Bq, T->b);
        float ctr[3] = { (T->a[0] + T->b[0]) / 2, (T->a[1] + T->b[1]) / 2, (T->a[2] + T->b[2]) / 2 }; T->bone = VoxNearestBone(p, ctr);
        if (T->bone == 0xFFFF) T->bone = 0;
        c->ns++;
    }
    c->ok = c->nb > 0;
    return c->ok;
}
// put every particle where the skeleton has it (start, or after a jump in the animation)
static void ClothSimReset(ClothSim *c, const float *skinM) {
    for (int i = 0; i < c->np; i++) { ClSkin(skinM + (size_t)c->pinBone[i] * 12, c->bind + i * 3, c->pos + i * 3); memcpy(c->prev + i * 3, c->pos + i * 3, 12); }
}
static void ClothSimStep(ClothSim *c, const float *skinM, float dt, float gravity) {
    const int sub = 3; float h = dt / sub;
    for (int s = 0; s < sub; s++) {
        float tgt[3];
        for (int i = 0; i < c->np; i++) {
            float *P = c->pos + i * 3, *Q = c->prev + i * 3;
            if (c->pinned[i]) {                                       // pinned: follow the bone, blended over the substeps
                ClSkin(skinM + (size_t)c->pinBone[i] * 12, c->bind + i * 3, tgt);
                memcpy(Q, P, 12); float f = (float)(s + 1) / sub; for (int k = 0; k < 3; k++) P[k] += (tgt[k] - P[k]) * f;
                continue;
            }
            for (int k = 0; k < 3; k++) { float vel = (P[k] - Q[k]) * 0.985f; Q[k] = P[k]; P[k] += vel + (k == 1 ? -gravity * h * h : 0); }
        }
        for (int it = 0; it < 8; it++) {
            for (int l = 0; l < c->nl; l++) {
                ClSimLink *L = &c->L[l]; float *A = c->pos + L->a * 3, *B = c->pos + L->b * 3;
                float d[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] }, len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); if (len < 1e-7f) continue;
                if (L->kind == 1 && len <= L->rest) continue;                // stretch: only stops it getting longer
                float k = L->kind == 2 ? 0.25f : 1.0f, diff = (len - L->rest) / len * k;
                float wa = c->pinned[L->a] ? 0.f : 1.f, wb = c->pinned[L->b] ? 0.f : 1.f, ws = wa + wb; if (ws <= 0) continue;
                for (int q = 0; q < 3; q++) { A[q] += d[q] * diff * wa / ws; B[q] -= d[q] * diff * wb / ws; }
            }
            for (int t = 0; t < c->ns; t++) {                                 // push out of the capsules / spheres
                const ClSimShape *S = &c->S[t]; float a[3], b[3]; ClSkin(skinM + (size_t)S->bone * 12, S->a, a); ClSkin(skinM + (size_t)S->bone * 12, S->b, b);
                float ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, ll = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2], R = S->r + c->rad;
                for (int i = 0; i < c->np; i++) {
                    if (c->pinned[i]) continue;
                    float *P = c->pos + i * 3, t0 = ll > 1e-9f ? ((P[0] - a[0]) * ab[0] + (P[1] - a[1]) * ab[1] + (P[2] - a[2]) * ab[2]) / ll : 0;
                    t0 = t0 < 0 ? 0 : t0 > 1 ? 1 : t0;
                    float cp[3] = { a[0] + ab[0] * t0, a[1] + ab[1] * t0, a[2] + ab[2] * t0 }, dv[3] = { P[0] - cp[0], P[1] - cp[1], P[2] - cp[2] };
                    float dl = sqrtf(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]);
                    if (dl < R && dl > 1e-6f) for (int q = 0; q < 3; q++) P[q] = cp[q] + dv[q] / dl * R;
                }
            }
        }
    }
}
static void ClNorm(float *v) { float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > 1e-9f) { v[0] /= l; v[1] /= l; v[2] /= l; } }
static void ClCross(const float *a, const float *b, float *o) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }
// a frame (3 columns) from a direction and, optionally, a second direction across it
static void ClFrame(const float *along, const float *across, int hasAcross, float F[9]) {
    float a[3] = { along[0], along[1], along[2] }; ClNorm(a);
    float c[3];
    if (hasAcross) { memcpy(c, across, 12); float d = c[0] * a[0] + c[1] * a[1] + c[2] * a[2]; for (int k = 0; k < 3; k++) c[k] -= a[k] * d; }
    else { float t[3] = { fabsf(a[0]) < 0.9f ? 1.f : 0.f, fabsf(a[0]) < 0.9f ? 0.f : 1.f, 0 }; ClCross(a, t, c); }
    ClNorm(c); float b[3]; ClCross(a, c, b);
    for (int r = 0; r < 3; r++) { F[r * 3] = a[r]; F[r * 3 + 1] = c[r]; F[r * 3 + 2] = b[r]; }
}
// the simulated bones' skinning matrices (and world matrices) from the particles
static void ClothSimApply(const ClothSim *c, const VoxPart *p, float *skinM, float *worldM) {
    float (*R)[9] = (float (*)[9])malloc((size_t)(c->nb + 1) * sizeof(float[9])); if (!R) return;
    for (int t = 0; t < c->nb; t++) {
        const ClSimBone *Bn = &c->B[t]; float cen[3] = { 0, 0, 0 };
        for (int k = 0; k < Bn->np; k++) for (int q = 0; q < 3; q++) cen[q] += c->pos[Bn->p[k] * 3 + q] / Bn->np;
        float Rm[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
        if (Bn->child >= 0) {
            const ClSimBone *Cb = &c->B[Bn->child]; float cc[3] = { 0, 0, 0 };
            for (int k = 0; k < Cb->np; k++) for (int q = 0; q < 3; q++) cc[q] += c->pos[Cb->p[k] * 3 + q] / Cb->np;
            float along[3] = { cc[0] - cen[0], cc[1] - cen[1], cc[2] - cen[2] }, across[3] = { 0, 0, 0 };
            if (Bn->hasAcross) for (int q = 0; q < 3; q++) across[q] = c->pos[Bn->p[1] * 3 + q] - c->pos[Bn->p[0] * 3 + q];
            float F0[9], F1[9]; ClFrame(Bn->bindAlong, Bn->bindAcross, Bn->hasAcross, F0); ClFrame(along, across, Bn->hasAcross, F1);
            for (int r = 0; r < 3; r++) for (int k = 0; k < 3; k++) Rm[r * 3 + k] = F1[r * 3] * F0[k * 3] + F1[r * 3 + 1] * F0[k * 3 + 1] + F1[r * 3 + 2] * F0[k * 3 + 2];
        } else {                                                     // the tip: turns like its parent
            int par = p->skParent[Bn->bone];
            for (int u = 0; u < t; u++) if (c->B[u].bone == par) { memcpy(Rm, R[u], sizeof Rm); break; }
        }
        memcpy(R[t], Rm, sizeof Rm);
        // world = R * bind rotation, at the centre; skin = world * inverse(bind world)
        const float *W = p->skBindW + (size_t)Bn->bone * 12;
        float Wn[12];
        // the joint keeps its small bind offset from the particles' centre, turned with them
        float J[3] = { W[3], W[7], W[11] }, off[3] = { J[0] - Bn->bindCen[0], J[1] - Bn->bindCen[1], J[2] - Bn->bindCen[2] }, Jn[3];
        for (int r = 0; r < 3; r++) Jn[r] = cen[r] + Rm[r * 3] * off[0] + Rm[r * 3 + 1] * off[1] + Rm[r * 3 + 2] * off[2];
        for (int r = 0; r < 3; r++) { for (int k = 0; k < 3; k++) Wn[r * 4 + k] = Rm[r * 3] * W[k] + Rm[r * 3 + 1] * W[4 + k] + Rm[r * 3 + 2] * W[8 + k]; Wn[r * 4 + 3] = Jn[r]; }
        float *S = skinM + (size_t)Bn->bone * 12;                  // S = Wn * W^-1 = [Rm | Jn - Rm * joint]
        for (int r = 0; r < 3; r++) { for (int k = 0; k < 3; k++) S[r * 4 + k] = Rm[r * 3 + k]; S[r * 4 + 3] = Jn[r] - (Rm[r * 3] * J[0] + Rm[r * 3 + 1] * J[1] + Rm[r * 3 + 2] * J[2]); }
        if (worldM) memcpy(worldM + (size_t)Bn->bone * 12, Wn, 48);
    }
    free(R);
}

// a part (material slot) for the overlay; -1 if the model has no room left
static int VoxNewPart(VoxPart *p, const char *name, int hidden) {
    if (p->nmat >= 255) return -1;
    int m = p->nmat++; p->sections = p->nmat;
    snprintf(p->matName[m], sizeof p->matName[m], "%s", name); p->matCount[m] = 0; p->texAlb[m][0] = p->texOpc[m][0] = p->texMix[m][0] = 0; if (m < 256) p->texEmm[m][0] = 0; p->startHidden[m] = (unsigned char)hidden; p->partBone[m] = -1;
    return m;
}
static void VoxAddLine(VoxPart *p, const float *a, const float *b, int m) {
    if (m < 0) return;
    if (p->nl == p->lcap) { size_t c = p->lcap ? p->lcap * 2 : 256; float *q = (float *)realloc(p->lv, c * 24); unsigned char *r = (unsigned char *)realloc(p->lmat, c);
        uint16_t *lb = (uint16_t *)realloc(p->lbone, c * 4); if (q) p->lv = q; if (r) p->lmat = r; if (lb) p->lbone = lb; if (!q || !r || !lb) return; p->lcap = c; }
    memcpy(p->lv + p->nl * 6, a, 12); memcpy(p->lv + p->nl * 6 + 3, b, 12);
    p->lbone[p->nl * 2] = p->lbCur[0]; p->lbone[p->nl * 2 + 1] = p->lbCur[1];
    p->lmat[p->nl++] = (unsigned char)m; p->matCount[m]++;
}
static void VoxXf(const float *M, const float *in, float *out) {         // M: 3x4 row-major (NULL = none)
    if (!M) { memcpy(out, in, 12); return; }
    for (int r = 0; r < 3; r++) out[r] = M[r * 4] * in[0] + M[r * 4 + 1] * in[1] + M[r * 4 + 2] * in[2] + M[r * 4 + 3];
}
// a circle of 24 segments around centre c with axes u, v
static void VoxCircle(VoxPart *p, const float *X, const float *c, const float *u, const float *v, float r, int m, int half) {
    float prev[3], cur[3], w[3]; int segs = half ? 12 : 24;
    for (int i = 0; i <= segs; i++) {
        float t = (float)i / 24 * 6.2831853f;
        for (int k = 0; k < 3; k++) w[k] = c[k] + r * (cosf(t) * u[k] + sinf(t) * v[k]);
        VoxXf(X, w, cur);
        if (i) VoxAddLine(p, prev, cur, m);
        memcpy(prev, cur, 12);
    }
}
static void VoxPerp(const float *d, float *u, float *v) {
    float a[3] = { fabsf(d[0]) < 0.9f ? 1.0f : 0.0f, fabsf(d[0]) < 0.9f ? 0.0f : 1.0f, 0 };
    u[0] = d[1] * a[2] - d[2] * a[1]; u[1] = d[2] * a[0] - d[0] * a[2]; u[2] = d[0] * a[1] - d[1] * a[0];
    float l = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]); if (l > 0) for (int k = 0; k < 3; k++) u[k] /= l;
    v[0] = d[1] * u[2] - d[2] * u[1]; v[1] = d[2] * u[0] - d[0] * u[2]; v[2] = d[0] * u[1] - d[1] * u[0];
}
// adds the cloth to a model (or an empty part): particles as small spheres, links and collision shapes as lines.
// X (optional, 3x4 row-major) moves everything (an attached head / tie). Returns the number of parts added.
// the skeleton bone whose bind-pose joint is nearest pt (the overlay of cloth / collision follows it when animated)
static uint16_t VoxNearestBone(const VoxPart *p, const float *pt) {
    int best = -1; float bd = 1e30f;
    for (int i = 0; i < p->skN; i++) {
        const char *n = p->skName[i];
        if (!strcmp(n, "nw4f_root") || !strcmp(n, "Model") || !strcmp(n, "ProgramRoot") || !strcmp(n, "Root")) continue;
        const float *W = p->skBindW + (size_t)i * 12; float dx = W[3] - pt[0], dy = W[7] - pt[1], dz = W[11] - pt[2], d = dx * dx + dy * dy + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    return best < 0 ? 0xFFFF : (uint16_t)best;
}
static int VoxAddCloth(VoxPart *p, const ClothVis *c, const float *X, const char *prefix) {
    int before = p->nmat; char nm[96];
    uint16_t *pb = (uint16_t *)malloc(((size_t)c->np + 1) * sizeof(uint16_t));       // each particle's bone
    if (!pb) return 0;
    for (int i = 0; i < c->np; i++) { float q[3]; VoxXf(X, c->pos + i * 3, q); pb[i] = VoxNearestBone(p, q); }
    float pr = c->rad > 0 ? c->rad : 0.02f;
    if (c->np) {
        // the size the spheres are drawn at: at least 1.2% of the cloth's extent so they can be seen
        float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
        for (int i = 0; i < c->np; i++) for (int k = 0; k < 3; k++) { float q = c->pos[i * 3 + k]; if (q < lo[k]) lo[k] = q; if (q > hi[k]) hi[k] = q; }
        float ext = 0; for (int k = 0; k < 3; k++) ext = fmaxf(ext, hi[k] - lo[k]);
        if (pr < ext * 0.012f) pr = ext * 0.012f;
        snprintf(nm, sizeof nm, "%sCloth particles (simulated)", prefix); int mFree = VoxNewPart(p, nm, 0);
        snprintf(nm, sizeof nm, "%sCloth particles (pinned)", prefix); int mPin = VoxNewPart(p, nm, 0);
        if (mFree < 0 || mPin < 0) { free(pb); p->lbCur[0] = p->lbCur[1] = 0xFFFF; return p->nmat - before; }
        static const float ov[6][3] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
        static const int of[8][3] = { {0,2,4}, {2,1,4}, {1,3,4}, {3,0,4}, {2,0,5}, {1,2,5}, {3,1,5}, {0,3,5} };
        size_t nv0 = p->ntv, nt0 = p->nti, nv = nv0 + (size_t)c->np * 6, nt = nt0 + (size_t)c->np * 8;
        float *tv = (float *)realloc(p->tv, nv * 12 + 12); if (!tv) { free(pb); p->lbCur[0] = p->lbCur[1] = 0xFFFF; return p->nmat - before; } p->tv = tv;
        uint32_t *ti = (uint32_t *)realloc(p->ti, nt * 12 + 12); if (!ti) { free(pb); p->lbCur[0] = p->lbCur[1] = 0xFFFF; return p->nmat - before; } p->ti = ti;
        uint16_t *tm = (uint16_t *)realloc(p->tmat, (nt + 1) * sizeof(uint16_t)); if (!tm) { free(pb); p->lbCur[0] = p->lbCur[1] = 0xFFFF; return p->nmat - before; } p->tmat = tm;
        if (p->tn) { signed char *q = (signed char *)realloc(p->tn, nv * 4 + 4); if (!q) { free(pb); p->lbCur[0] = p->lbCur[1] = 0xFFFF; return p->nmat - before; } p->tn = q; }
        if (p->tuv) { float *q = (float *)realloc(p->tuv, nv * 8 + 8); if (!q) { free(pb); p->lbCur[0] = p->lbCur[1] = 0xFFFF; return p->nmat - before; } p->tuv = q; }
        if (p->vBone) {
            uint16_t *q = (uint16_t *)realloc(p->vBone, nv * 16 + 16); float *w = (float *)realloc(p->vW, nv * 32 + 32);
            if (q) p->vBone = q; if (w) p->vW = w; if (!q || !w) { free(pb); p->lbCur[0] = p->lbCur[1] = 0xFFFF; return p->nmat - before; }
        }
        for (int i = 0; i < c->np; i++) {
            float ctr[3]; VoxXf(X, c->pos + i * 3, ctr);
            int m = c->pinned && c->pinned[i] ? mPin : mFree; if (m < 0) continue;
            for (int k = 0; k < 6; k++) {
                size_t vi = nv0 + (size_t)i * 6 + k;
                for (int j = 0; j < 3; j++) p->tv[vi * 3 + j] = ctr[j] + ov[k][j] * pr;
                if (p->tn) { p->tn[vi * 4] = (signed char)(ov[k][0] * 127); p->tn[vi * 4 + 1] = (signed char)(ov[k][1] * 127); p->tn[vi * 4 + 2] = (signed char)(ov[k][2] * 127); p->tn[vi * 4 + 3] = 0; }
                if (p->tuv) p->tuv[vi * 2] = p->tuv[vi * 2 + 1] = 0;
                if (p->vBone) { for (int q = 0; q < 8; q++) { p->vBone[vi * 8 + q] = 0; p->vW[vi * 8 + q] = 0; } p->vW[vi * 8] = 1; if (pb[i] != 0xFFFF) p->vBone[vi * 8] = pb[i]; }
            }
            for (int f = 0; f < 8; f++) {
                size_t t = p->nti++;
                for (int j = 0; j < 3; j++) p->ti[t * 3 + j] = (uint32_t)(nv0 + (size_t)i * 6 + of[f][j]);
                p->tmat[t] = (uint16_t)m; p->matCount[m]++;
            }
        }
        p->ntv = nv0 + (size_t)c->np * 6; (void)nt;
        for (int s = 0; s < c->nsets; s++) {
            snprintf(nm, sizeof nm, "%sCloth links: %s", prefix, c->sets[s].name);
            int m = VoxNewPart(p, nm, 0);
            for (int q = 0; q < c->sets[s].n; q++) {
                int a = c->sets[s].pair[q * 2], b = c->sets[s].pair[q * 2 + 1]; if (a >= c->np || b >= c->np) continue;
                float A[3], B[3]; VoxXf(X, c->pos + a * 3, A); VoxXf(X, c->pos + b * 3, B);
                p->lbCur[0] = pb[a]; p->lbCur[1] = pb[b]; VoxAddLine(p, A, B, m);
            }
        }
    }
    for (int i = 0; i < c->nsh; i++) {
        const ClShape *S = &c->sh[i];
        // the shape's own placement (rotation columns + translation), then X
        float M[12] = { S->T[0], S->T[4], S->T[8], S->T[12], S->T[1], S->T[5], S->T[9], S->T[13], S->T[2], S->T[6], S->T[10], S->T[14] };
        float MX[12];
        if (X) { for (int r = 0; r < 3; r++) { for (int k = 0; k < 3; k++) MX[r * 4 + k] = X[r * 4] * M[k] + X[r * 4 + 1] * M[4 + k] + X[r * 4 + 2] * M[8 + k];
                 MX[r * 4 + 3] = X[r * 4] * M[3] + X[r * 4 + 1] * M[7] + X[r * 4 + 2] * M[11] + X[r * 4 + 3]; } }
        const float *T = X ? MX : M;
        // Bananza forms' shapes start hidden (they are only used while DK is in that form)
        int hide = strstr(S->name, "Style") != NULL;
        snprintf(nm, sizeof nm, "%sCollision %s", prefix, strncmp(S->name, "Collidable_", 11) ? S->name : S->name + 11); int m = VoxNewPart(p, nm, hide);
        if (m < 0) break;
        { float o0[3] = { 0, 0, 0 }, ctr[3];                 // the whole shape follows the bone nearest its centre
          if (S->type == 0) for (int k = 0; k < 3; k++) o0[k] = (S->a[k] + S->b[k]) / 2; else if (S->type == 1) memcpy(o0, S->a, 12);
          VoxXf(T, o0, ctr); p->lbCur[0] = p->lbCur[1] = VoxNearestBone(p, ctr); }
        if (S->type == 0) {
            float d[3] = { S->b[0] - S->a[0], S->b[1] - S->a[1], S->b[2] - S->a[2] }, l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (l > 1e-6f) for (int k = 0; k < 3; k++) d[k] /= l; else { d[0] = 1; d[1] = d[2] = 0; }
            float u[3], v[3]; VoxPerp(d, u, v);
            VoxCircle(p, T, S->a, u, v, S->r, m, 0); VoxCircle(p, T, S->b, u, v, S->r, m, 0);
            float nd[3] = { -d[0], -d[1], -d[2] };
            VoxCircle(p, T, S->b, u, d, S->r, m, 1); VoxCircle(p, T, S->b, v, d, S->r, m, 1);      // end caps (half circles)
            VoxCircle(p, T, S->a, u, nd, S->r, m, 1); VoxCircle(p, T, S->a, v, nd, S->r, m, 1);
            for (int k = 0; k < 4; k++) {
                float t = k * 1.5707963f, A[3], B[3], wa[3], wb[3];
                for (int j = 0; j < 3; j++) { float o = S->r * (cosf(t) * u[j] + sinf(t) * v[j]); wa[j] = S->a[j] + o; wb[j] = S->b[j] + o; }
                VoxXf(T, wa, A); VoxXf(T, wb, B); VoxAddLine(p, A, B, m);
            }
        } else if (S->type == 1) {
            float ex[3] = { 1, 0, 0 }, ey[3] = { 0, 1, 0 }, ez[3] = { 0, 0, 1 };
            VoxCircle(p, T, S->a, ex, ey, S->r, m, 0); VoxCircle(p, T, S->a, ey, ez, S->r, m, 0); VoxCircle(p, T, S->a, ex, ez, S->r, m, 0);
        } else {
            float nrm[3] = { S->a[0], S->a[1], S->a[2] }, l = sqrtf(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]); if (l < 1e-6f) continue;
            for (int k = 0; k < 3; k++) nrm[k] /= l;
            float ctr[3] = { -S->a[3] * nrm[0] / l, -S->a[3] * nrm[1] / l, -S->a[3] * nrm[2] / l }, u[3], v[3]; VoxPerp(nrm, u, v);
            float sz = 0.5f;
            for (int g = -2; g <= 2; g++) {          // a small grid on the plane
                float A[3], B[3], wa[3], wb[3];
                for (int j = 0; j < 3; j++) { wa[j] = ctr[j] + u[j] * sz * g / 2 - v[j] * sz; wb[j] = ctr[j] + u[j] * sz * g / 2 + v[j] * sz; }
                VoxXf(T, wa, A); VoxXf(T, wb, B); VoxAddLine(p, A, B, m);
                for (int j = 0; j < 3; j++) { wa[j] = ctr[j] + v[j] * sz * g / 2 - u[j] * sz; wb[j] = ctr[j] + v[j] * sz * g / 2 + u[j] * sz; }
                VoxXf(T, wa, A); VoxXf(T, wb, B); VoxAddLine(p, A, B, m);
            }
        }
    }
    { free(pb); p->lbCur[0] = p->lbCur[1] = 0xFFFF; return p->nmat - before; }
}

/* ---------------- BFRES models (Switch / Switch 2 version 10): the shapes' first level of detail, posed in the
   bind pose (rigidly attached parts moved onto their bones). Layout as in BfresLibrary. ---------------- */
static uint64_t BfU64(const unsigned char *b) { return (uint64_t)VoxRd32(b) | ((uint64_t)VoxRd32(b + 4) << 32); }
static float BfF(const unsigned char *b) { uint32_t u = VoxRd32(b); float f; memcpy(&f, &u, 4); return f; }
static float BfHalf(unsigned h) {
    unsigned s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023; float v;
    if (e == 0) v = m / 16777216.0f; else if (e == 31) v = m ? 0 : 65504; else v = ldexpf(1 + m / 1024.0f, (int)e - 15);
    return s ? -v : v;
}
static void BfStr(const unsigned char *b, size_t n, uint64_t at, char *out, int outN) {
    out[0] = 0; if (at + 8 > n) return;
    uint64_t p = BfU64(b + at); if (!p || p + 2 > n) return;
    int l = VoxRd16(b + p); if (p + 2 + l > n) l = (int)(n - p - 2);
    if (l > outN - 1) l = outN - 1;
    memcpy(out, b + p + 2, l); out[l] = 0;
}
// a material's shader option (BFRES 10, MKW): FMAT +0x10 shader info -> +0 shader assign (+0x40: option name dict);
// shader info +0x46 = number of on / off options (they come first), +0x30 = the other options' value strings.
// The value as text ("true" / "false" for on / off options), "" if it isn't there.
static void BfMatOption(const unsigned char *b, size_t n, size_t mt, const char *key, char *out, int outN) {
    out[0] = 0;
    if (mt + 0x18 > n) return;
    uint64_t si = BfU64(b + mt + 0x10); if (!si || si + 0x50 > n) return;
    uint64_t sa = BfU64(b + si); if (!sa || sa + 0x48 > n) return;
    uint64_t d = BfU64(b + sa + 0x40); if (!d || d + 8 > n) return;
    uint32_t cnt = VoxRd32(b + d + 4); unsigned nb = VoxRd16(b + si + 0x46); uint64_t bits = BfU64(b + si + 0x28), vals = BfU64(b + si + 0x30);
    if (cnt > 4096 || nb > cnt) return;
    size_t kl = strlen(key);
    for (uint32_t i = 0; i < cnt; i++) {
        size_t node = d + 8 + 16 * (size_t)(i + 1); if (node + 16 > n) return;
        uint64_t sp = BfU64(b + node + 8); if (!sp || sp + 2 + kl > n) continue;
        if (VoxRd16(b + sp) != kl || memcmp(b + sp + 2, key, kl)) continue;
        if (i < nb) { if (bits && bits + i / 8 < n) snprintf(out, outN, "%s", (b[bits + i / 8] >> (i % 8)) & 1 ? "true" : "false"); return; }
        if (vals) BfStr(b, n, vals + 8 * (size_t)(i - nb), out, outN);
        return;
    }
}
// one attribute value -> up to 4 floats; returns component count (0 = format not handled)
static int BfAttr(const unsigned char *b, size_t n, size_t at, unsigned fmt, float *o) {
    unsigned lay = fmt & 0xFF, typ = fmt >> 8;
    o[0] = o[1] = o[2] = 0; o[3] = 1;
    if (lay == 0x0E) {                                  // 10_10_10_2
        if (at + 4 > n) return 0;
        uint32_t v = VoxRd32(b + at);
        for (int i = 0; i < 3; i++) { int x = (v >> (10 * i)) & 0x3FF; if (typ == 2) { if (x & 0x200) x -= 0x400; o[i] = x / 511.0f; if (o[i] < -1) o[i] = -1; } else o[i] = x / 1023.0f; }
        return 3;
    }
    int cnt, bits;
    switch (lay) {
    case 0x02: cnt = 1; bits = 8; break; case 0x09: cnt = 2; bits = 8; break; case 0x0B: cnt = 4; bits = 8; break;
    case 0x0A: cnt = 1; bits = 16; break; case 0x12: cnt = 2; bits = 16; break; case 0x15: cnt = 4; bits = 16; break;
    case 0x14: case 0x16: cnt = 1; bits = 32; break; case 0x17: cnt = 2; bits = 32; break; case 0x18: cnt = 3; bits = 32; break; case 0x19: cnt = 4; bits = 32; break;
    default: return 0;
    }
    if (at + (size_t)cnt * bits / 8 > n) return 0;
    for (int i = 0; i < cnt; i++) {
        const unsigned char *q = b + at + i * bits / 8; float x;
        if (bits == 32) { if (typ == 5) x = BfF(q); else if (typ == 4 || typ == 0xA) x = (float)(int32_t)VoxRd32(q); else x = (float)VoxRd32(q); }
        else if (bits == 16) {
            unsigned u = VoxRd16(q);
            if (typ == 5) x = BfHalf(u); else if (typ == 1) x = u / 65535.0f; else if (typ == 2) { x = (int16_t)u / 32767.0f; if (x < -1) x = -1; }
            else if (typ == 4 || typ == 0xA) x = (float)(int16_t)u; else x = (float)u;
        } else {
            unsigned u = q[0];
            if (typ == 1) x = u / 255.0f; else if (typ == 2) { x = (signed char)u / 127.0f; if (x < -1) x = -1; }
            else if (typ == 4 || typ == 0xA) x = (float)(signed char)u; else x = (float)u;
        }
        o[i] = x;
    }
    return cnt;
}
typedef struct { float m[3][4]; } BfM;
static BfM BfLocal(const float *s, const float *r, const float *t, int euler) {
    float R[3][3];
    if (euler) {
        float cx = cosf(r[0]), sx = sinf(r[0]), cy = cosf(r[1]), sy = sinf(r[1]), cz = cosf(r[2]), sz = sinf(r[2]);
        float A[3][3] = { { cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz }, { cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz }, { -sy, sx * cy, cx * cy } };
        memcpy(R, A, sizeof R);
    } else {
        float x = r[0], y = r[1], z = r[2], w = r[3];
        float A[3][3] = { { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w) }, { 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w) },
                          { 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) } };
        memcpy(R, A, sizeof R);
    }
    BfM M; for (int i = 0; i < 3; i++) { for (int j = 0; j < 3; j++) M.m[i][j] = R[i][j] * s[j]; M.m[i][3] = t[i]; }
    return M;
}
static BfM BfMul(const BfM *a, const BfM *b) {
    BfM o;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 4; j++)
        o.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j] + (j == 3 ? a->m[i][3] : 0);
    return o;
}
static int VoxLoadBfres(VoxPart *p, const VOXCHAR *path) {
    memset(p, 0, sizeof(*p));
    for (int i = 0; i < 256; i++) p->partBone[i] = -1;
    p->isTriMesh = 1; p->isModel = 1; p->scale[0] = p->scale[1] = p->scale[2] = 1;
    size_t zn; unsigned char *z = VoxReadFile(path, &zn);
    if (!z) { snprintf(p->err, sizeof p->err, "The file couldn't be read from the dump."); return 0; }
    size_t n; unsigned char *b = VoxUnzstd(z, zn, &n); free(z);
    if (!b || n < 0x100 || memcmp(b, "FRES", 4)) { free(b); snprintf(p->err, sizeof p->err, "Not a BFRES file."); return 0; }
    if (VoxRd16(b + 0xA) < 9) { int ver = VoxRd16(b + 0xA); free(b); snprintf(p->err, sizeof p->err, "This BFRES version (%d) isn't supported.", ver); return 0; }
    uint64_t bi = BfU64(b + 0xB0), base = bi && bi + 16 <= n ? BfU64(b + bi + 8) : 0;
    uint64_t mArr = BfU64(b + 0x28); int nModels = VoxRd16(b + 0xDC);
    // count first
    size_t capV = 0, capT = 0;
    for (int mi = 0; mi < nModels && mArr; mi++) {
        size_t mo = mArr + (size_t)mi * 0x78; if (mo + 0x78 > n) break;
        uint64_t sArr = BfU64(b + mo + 0x28), vArr = BfU64(b + mo + 0x20); int ns = VoxRd16(b + mo + 0x6A);
        for (int si = 0; si < ns && sArr; si++) {
            size_t so = sArr + (size_t)si * 0x60; if (so + 0x60 > n) break;
            uint64_t mesh = BfU64(b + so + 0x18); int vbi = VoxRd16(b + so + 0x56);
            if (!mesh || mesh + 0x38 > n || !b[so + 0x5B]) continue;
            size_t vb = vArr + (size_t)vbi * 0x58; if (vb + 0x58 > n) continue;
            capV += VoxRd32(b + vb + 0x50); capT += VoxRd32(b + mesh + 0x2C) / 3;
        }
    }
    if (!capT) {
        free(b);
        snprintf(p->err, sizeof p->err, nModels ? "This model has no meshes (only a skeleton or settings)." : "This BFRES file has no models (it holds animations).");
        return 0;
    }
    p->tv = (float *)malloc(capV * 3 * sizeof(float) + 16); p->tn = (signed char *)malloc(capV * 4 + 16);
    p->ti = (uint32_t *)malloc(capT * 3 * sizeof(uint32_t) + 16); p->tmat = (uint16_t *)malloc((capT + 16) * sizeof(uint16_t));
    p->tuv = (float *)calloc(capV * 2 + 4, sizeof(float));
    if (nModels == 1) { p->vBone = (uint16_t *)calloc(capV * 8 + 8, sizeof(uint16_t)); p->vW = (float *)calloc(capV * 8 + 8, sizeof(float)); }
    if (!p->tv || !p->tn || !p->ti || !p->tmat || !p->tuv) { free(b); snprintf(p->err, sizeof p->err, "Out of memory."); return 0; }
    size_t cv = 0, ct = 0; int part = 0;
    for (int mi = 0; mi < nModels; mi++) {
        size_t mo = mArr + (size_t)mi * 0x78; if (mo + 0x78 > n) break;
        char mname[64]; BfStr(b, n, mo + 8, mname, sizeof mname);
        if (mi < 3) { size_t l = strlen(p->modelName); snprintf(p->modelName + l, sizeof p->modelName - l, "%s%s", l ? ", " : "", mname); }
        p->nmodels++;
        // skeleton: world matrices of the bind pose
        uint64_t sk = BfU64(b + mo + 0x18); int nb = 0; BfM *W = NULL; uint16_t *m2b = NULL; int nm2b = 0;
        if (sk && sk + 0x40 <= n) {
            nb = VoxRd16(b + sk + 0x38); nm2b = VoxRd16(b + sk + 0x3A) + VoxRd16(b + sk + 0x3C);
            uint64_t ba = BfU64(b + sk + 0x10), mb = BfU64(b + sk + 0x18);
            W = (BfM *)calloc(nb + 1, sizeof(BfM));
            if (W) for (int i = 0; i < nb; i++) {
                size_t bo = ba + (size_t)i * 0x58; if (bo + 0x58 > n) { nb = i; break; }
                float s3[3], r4[4], t3[3]; for (int k = 0; k < 3; k++) { s3[k] = BfF(b + bo + 0x30 + k * 4); t3[k] = BfF(b + bo + 0x4C + k * 4); }
                for (int k = 0; k < 4; k++) r4[k] = BfF(b + bo + 0x3C + k * 4);
                int eul = (VoxRd32(b + bo + 0x2C) & 0x1000) != 0;
                BfM L = BfLocal(s3, r4, t3, eul);
                int par = (int16_t)VoxRd16(b + bo + 0x22);
                W[i] = par >= 0 && par < i ? BfMul(&W[par], &L) : L;
                if (mi == 0 && nModels == 1) {
                    if (!p->skName) {
                        p->skName = (char (*)[64])calloc(nb + 1, 64); p->skParent = (short *)calloc(nb + 1, sizeof(short));
                        p->skBind = (float (*)[10])calloc(nb + 1, sizeof(float[10])); p->skEuler = (unsigned char *)calloc(nb + 1, 1);
                        p->skBindW = (float *)calloc((size_t)(nb + 1) * 12, sizeof(float));
                    }
                    if (p->skName && p->skParent && p->skBind && p->skEuler && p->skBindW) {
                        BfStr(b, n, bo, p->skName[i], 64); p->skParent[i] = (short)par; p->skEuler[i] = (unsigned char)eul;
                        memcpy(p->skBind[i], s3, 12); memcpy(p->skBind[i] + 3, r4, 16); memcpy(p->skBind[i] + 7, t3, 12);
                        memcpy(p->skBindW + (size_t)i * 12, W[i].m, 48);
                        p->skN = i + 1;
                    }
                }
            }
            if (mb && nm2b && mb + (size_t)nm2b * 2 <= n) { m2b = (uint16_t *)malloc(nm2b * 2); if (m2b) for (int i = 0; i < nm2b; i++) m2b[i] = VoxRd16(b + mb + i * 2); }
            p->nbones += nb;
        }
        uint64_t sArr = BfU64(b + mo + 0x28), vArr = BfU64(b + mo + 0x20); int ns = VoxRd16(b + mo + 0x6A);
        p->nmaterials += VoxRd16(b + mo + 0x6C);
        for (int si = 0; si < ns && sArr; si++) {
            size_t so = sArr + (size_t)si * 0x60; if (so + 0x60 > n) break;
            uint64_t mesh = BfU64(b + so + 0x18); int vbi = VoxRd16(b + so + 0x56), bone = VoxRd16(b + so + 0x54), skin = b[so + 0x5A];
            if (!mesh || mesh + 0x38 > n || !b[so + 0x5B]) continue;
            size_t vb = vArr + (size_t)vbi * 0x58; if (vb + 0x58 > n) continue;
            uint32_t prim = VoxRd32(b + mesh + 0x24), ifmt = VoxRd32(b + mesh + 0x28), icnt = VoxRd32(b + mesh + 0x2C), first = VoxRd32(b + mesh + 0x30);
            if (prim != 3) continue;
            // the vertex buffer's attributes and buffers
            uint64_t aArr = BfU64(b + vb + 8), szA = BfU64(b + vb + 0x30), stA = BfU64(b + vb + 0x38);
            int na = b[vb + 0x4C], nbuf = b[vb + 0x4D]; uint32_t nv = VoxRd32(b + vb + 0x50); unsigned align = VoxRd16(b + vb + 0x56); if (!align) align = 1;
            size_t bufOff[16] = { 0 }, stride[16] = { 0 };
            size_t q = base + (size_t)(int32_t)VoxRd32(b + vb + 0x48);
            for (int k = 0; k < nbuf && k < 16; k++) {
                q = (q + align - 1) / align * align;
                bufOff[k] = q; stride[k] = VoxRd32(b + stA + k * 16); q += VoxRd32(b + szA + k * 16);
            }
            // a material whose second colour texture (_a1) is what shows (drive_enable_multi_albedo, calc type 1 - MKW's
            // AppleFS000: _a0 is a dark grain, the red / yellow apple is _a1) uses that texture and its own texture
            // coordinates (texcoord1 = _u<drive_texcoord1_input>, e.g. _u2) instead of _a0 on _u0
            char uvName[8] = "_u0"; int useA1 = 0;
            {   int mie = VoxRd16(b + so + 0x52); uint64_t mae = BfU64(b + mo + 0x38); size_t mte = mae + (size_t)mie * 0xB0;
                if (mae && mie < VoxRd16(b + mo + 0x6C) && mte + 0xB0 <= n) {
                    char on[16], ct[16], ti[16];
                    BfMatOption(b, n, mte, "drive_enable_multi_albedo", on, sizeof on);
                    BfMatOption(b, n, mte, "drive_multi_albedo_calc_type", ct, sizeof ct);
                    BfMatOption(b, n, mte, "drive_texcoord1_input", ti, sizeof ti);
                    // ... but not when _a1 is a blend tint (the ice cliffs' CmnTex_CliffIC_Wall_IceBlend_Alb: most ice cliffs
                    // were pink in .209-.213). Other shared textures do show (Ice Cream island's CmnTex_Stage_IceBuildings_...:
                    // _a0 is a plain grain, Face_00, the colours are _a1 - dark in .214)
                    char a1n[128] = "";
                    {   int nt = b[mte + 0xA3]; uint64_t tn = BfU64(b + mte + 0x20), sd = BfU64(b + mte + 0x38);
                        for (int k = 0; k < nt && tn && sd && sd + 8 + (size_t)(k + 2) * 16 <= n; k++) {
                            char smp[16]; BfStr(b, n, sd + 8 + (size_t)(k + 1) * 16 + 8, smp, sizeof smp);
                            if (!strcmp(smp, "_a1")) { BfStr(b, n, tn + (size_t)k * 8, a1n, sizeof a1n); break; }
                        }
                    }
                    if (!strcmp(on, "true") && !strcmp(ct, "1") && a1n[0] && !strstr(a1n, "Blend")) {
                        useA1 = 1;
                        if (ti[0] >= '0' && ti[0] <= '9' && !ti[1]) { uvName[2] = ti[0]; }
                    }
                }
            }
            int aP = -1, aN = -1, aI = -1, aU = -1, aI1 = -1, aW0 = -1, aW1 = -1; unsigned fP = 0, fN = 0, fI = 0, fU = 0, fI1 = 0, fW0 = 0, fW1 = 0;
            size_t oP = 0, oN = 0, oI = 0, oU = 0, oI1 = 0, oW0 = 0, oW1 = 0;
            int aU0 = -1; unsigned fU0 = 0; size_t oU0 = 0;          // (_u0 too: to fall back to when _a1 is only a tint)
            for (int k = 0; k < na; k++) {
                size_t ao = aArr + (size_t)k * 16; if (ao + 16 > n) break;
                char an[16]; BfStr(b, n, ao, an, sizeof an);
                unsigned fmt = (b[ao + 8] << 8) | b[ao + 9]; int bufi = b[ao + 0x0E]; if (bufi >= nbuf || bufi >= 16) continue;
                size_t off = bufOff[bufi] + VoxRd16(b + ao + 0x0C);
                if (!strcmp(an, "_u0")) { aU0 = bufi; fU0 = fmt; oU0 = off; }
                if (!strcmp(an, "_p0")) { aP = bufi; fP = fmt; oP = off; }
                else if (!strcmp(an, "_n0")) { aN = bufi; fN = fmt; oN = off; }
                else if (!strcmp(an, "_i0")) { aI = bufi; fI = fmt; oI = off; }
                else if (!strcmp(an, uvName)) { aU = bufi; fU = fmt; oU = off; }
                else if (!strcmp(an, "_i1")) { aI1 = bufi; fI1 = fmt; oI1 = off; }
                else if (!strcmp(an, "_w0")) { aW0 = bufi; fW0 = fmt; oW0 = off; }
                else if (!strcmp(an, "_w1")) { aW1 = bufi; fW1 = fmt; oW1 = off; }
            }
            if (aP < 0) continue;
            // (no texcoord1 in this shape: _a0 on _u0)
            if (useA1 && aU < 0) { useA1 = 0; aU = aU0; fU = fU0; oU = oU0; }
            // _a1 repeated many times over the shape is a small detail pattern laid over _a0, not the colour (MKW's Big
            // Tree jump leaves: JumpLeafAddAlb on _u2 spanning 28 x 37 - the leaf showed a tiled scale pattern; _a0 on _u0
            // covers the leaf once). The colour _a1s span about 0-1.2 (apple, Ice Cream island); DK Pass's walls up to ~10.
            if (useA1 && aU >= 0) {
                float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
                for (uint32_t i = 0; i < nv; i++) { float U4[4]; if (!BfAttr(b, n, oU + i * stride[aU], fU, U4)) break;
                    for (int q = 0; q < 2; q++) { if (U4[q] < lo[q]) lo[q] = U4[q]; if (U4[q] > hi[q]) hi[q] = U4[q]; } }
                if (hi[0] - lo[0] > 16.0f || hi[1] - lo[1] > 16.0f) { useA1 = 0; aU = aU0; fU = fU0; oU = oU0; }
            }
            size_t v0 = cv;
            for (uint32_t i = 0; i < nv && cv < capV; i++, cv++) {
                float P4[4], N4[4] = { 0, 1, 0, 0 }, I4[4] = { 0 };
                if (!BfAttr(b, n, oP + i * stride[aP], fP, P4)) { P4[0] = P4[1] = P4[2] = 0; }
                if (aN >= 0) BfAttr(b, n, oN + i * stride[aN], fN, N4);
                const BfM *M = NULL;
                if (skin == 0 && W && bone < nb) M = &W[bone];
                else if (skin == 1 && W && aI >= 0 && BfAttr(b, n, oI + i * stride[aI], fI, I4)) { int mx = (int)I4[0]; if (m2b && mx >= 0 && mx < nm2b && m2b[mx] < nb) M = &W[m2b[mx]]; }
                float x = P4[0], y = P4[1], zz = P4[2], nx = N4[0], ny = N4[1], nz = N4[2];
                if (M) {
                    x = M->m[0][0] * P4[0] + M->m[0][1] * P4[1] + M->m[0][2] * P4[2] + M->m[0][3];
                    y = M->m[1][0] * P4[0] + M->m[1][1] * P4[1] + M->m[1][2] * P4[2] + M->m[1][3];
                    zz = M->m[2][0] * P4[0] + M->m[2][1] * P4[1] + M->m[2][2] * P4[2] + M->m[2][3];
                    nx = M->m[0][0] * N4[0] + M->m[0][1] * N4[1] + M->m[0][2] * N4[2];
                    ny = M->m[1][0] * N4[0] + M->m[1][1] * N4[1] + M->m[1][2] * N4[2];
                    nz = M->m[2][0] * N4[0] + M->m[2][1] * N4[1] + M->m[2][2] * N4[2];
                }
                float l = sqrtf(nx * nx + ny * ny + nz * nz); if (l > 0) { nx /= l; ny /= l; nz /= l; }
                p->tv[cv * 3] = x; p->tv[cv * 3 + 1] = y; p->tv[cv * 3 + 2] = zz;
                if (p->vBone && p->vW && W) {                      // which bones move this vertex, and how much
                    uint16_t *vb8 = p->vBone + cv * 8; float *vw8 = p->vW + cv * 8;
                    if (skin == 0) { vb8[0] = (uint16_t)(bone < nb ? bone : 0); vw8[0] = 1; }
                    else if (skin == 1) { int mx = (int)I4[0]; vb8[0] = (uint16_t)(m2b && mx >= 0 && mx < nm2b && m2b[mx] < nb ? m2b[mx] : 0); vw8[0] = 1; }
                    else {
                        float ii[8] = { 0 }, ww[8] = { 0 }, t4[4];
                        if (aI >= 0 && BfAttr(b, n, oI + i * stride[aI], fI, t4)) memcpy(ii, t4, 16);
                        if (aI1 >= 0) { int c = BfAttr(b, n, oI1 + i * stride[aI1], fI1, t4); for (int k = 0; k < c; k++) ii[4 + k] = t4[k]; }
                        if (aW0 >= 0 && BfAttr(b, n, oW0 + i * stride[aW0], fW0, t4)) memcpy(ww, t4, 16);
                        if (aW1 >= 0) { int c = BfAttr(b, n, oW1 + i * stride[aW1], fW1, t4); for (int k = 0; k < c; k++) ww[4 + k] = t4[k]; }
                        float sum = 0;
                        for (int k = 0; k < skin && k < 8; k++) {
                            int mx = (int)ii[k];
                            vb8[k] = (uint16_t)(m2b && mx >= 0 && mx < nm2b && m2b[mx] < nb ? m2b[mx] : 0); vw8[k] = ww[k]; sum += ww[k];
                        }
                        if (sum > 0) for (int k = 0; k < 8; k++) vw8[k] /= sum; else { vw8[0] = 1; }
                    }
                }
                if (aU >= 0) { float U4[4]; if (BfAttr(b, n, oU + i * stride[aU], fU, U4)) { p->tuv[cv * 2] = U4[0]; p->tuv[cv * 2 + 1] = U4[1]; } }
                p->tn[cv * 4] = (signed char)(nx * 127); p->tn[cv * 4 + 1] = (signed char)(ny * 127); p->tn[cv * 4 + 2] = (signed char)(nz * 127); p->tn[cv * 4 + 3] = 0;
            }
            // key shapes (morph targets): dictionary at +0x30; key k's positions are attribute _p<k>
            uint64_t ksd = BfU64(b + so + 0x30);
            if (ksd && ksd + 8 <= n && cv > v0) {
                int nks = (int)VoxRd32(b + ksd + 4);
                for (int k = 1; k < nks && k < 64; k++) {
                    size_t e = ksd + 8 + (size_t)(k + 1) * 16; if (e + 16 > n) break;
                    char want[8]; snprintf(want, sizeof want, "_p%d", k);
                    int aK = -1; unsigned fK = 0; size_t oK = 0;
                    for (int a = 0; a < na; a++) {
                        size_t ao = aArr + (size_t)a * 16; if (ao + 16 > n) break;
                        char an[16]; BfStr(b, n, ao, an, sizeof an);
                        if (!strcmp(an, want)) { int bufi = b[ao + 0x0E]; if (bufi < nbuf && bufi < 16) { aK = bufi; fK = (b[ao + 8] << 8) | b[ao + 9]; oK = bufOff[bufi] + VoxRd16(b + ao + 0x0C); } break; }
                    }
                    if (aK < 0) continue;
                    struct VoxKey *nk = (struct VoxKey *)realloc(p->keys, (size_t)(p->nkeys + 1) * sizeof *nk); if (!nk) break; p->keys = nk;
                    struct VoxKey *K = &p->keys[p->nkeys];
                    uint32_t kn = (uint32_t)(cv - v0); K->d = (float *)calloc((size_t)kn * 3, sizeof(float)); if (!K->d) break;
                    K->part = part < 255 ? part : 254; K->v0 = v0; K->nv = kn; BfStr(b, n, e + 8, K->name, sizeof K->name);
                    const BfM *M = (skin == 0 && W && bone < nb) ? &W[bone] : NULL;
                    float I4[4] = { 0 }, P4[4], B4[4];
                    for (uint32_t i = 0; i < kn; i++) {
                        if (!BfAttr(b, n, oK + i * stride[aK], fK, P4) || !BfAttr(b, n, oP + i * stride[aP], fP, B4)) continue;
                        float dx = P4[0] - B4[0], dy = P4[1] - B4[1], dz = P4[2] - B4[2];
                        const BfM *Mv = M;
                        if (skin == 1 && W && aI >= 0 && BfAttr(b, n, oI + i * stride[aI], fI, I4)) { int mx = (int)I4[0]; if (m2b && mx >= 0 && mx < nm2b && m2b[mx] < nb) Mv = &W[m2b[mx]]; }
                        if (Mv) { float x = Mv->m[0][0] * dx + Mv->m[0][1] * dy + Mv->m[0][2] * dz, y = Mv->m[1][0] * dx + Mv->m[1][1] * dy + Mv->m[1][2] * dz, z = Mv->m[2][0] * dx + Mv->m[2][1] * dy + Mv->m[2][2] * dz; dx = x; dy = y; dz = z; }
                        K->d[i * 3] = dx; K->d[i * 3 + 1] = dy; K->d[i * 3 + 2] = dz;
                    }
                    p->nkeys++;
                }
            }
            size_t ib = base + VoxRd32(b + mesh + 0x20), isz = ifmt == 2 ? 4 : ifmt == 1 ? 2 : 1;
            if (ib + (size_t)icnt * isz > n) continue;
            int id = part < 255 ? part : 254;
            if (part < 255) {
                BfStr(b, n, so + 8, p->matName[id], VOX_MATN); if (!p->matName[id][0]) snprintf(p->matName[id], VOX_MATN, "shape %d", si);
                BfStr(b, n, so + 8, p->shapeFull[id], 96);
                // the material's albedo (_a0) and opacity (_o0) textures
                int mi2 = VoxRd16(b + so + 0x52); uint64_t matArr = BfU64(b + mo + 0x38);
                size_t mt = matArr + (size_t)mi2 * 0xB0;
                if (matArr && mi2 < VoxRd16(b + mo + 0x6C) && mt + 0xB0 <= n) {
                    BfStr(b, n, mt + 8, p->matFull[id], 96);
                    int nt = b[mt + 0xA3]; uint64_t tn = BfU64(b + mt + 0x20), sd = BfU64(b + mt + 0x38);
                    for (int k = 0; k < nt && tn && sd && sd + 8 + (size_t)(k + 2) * 16 <= n; k++) {
                        char smp[16]; BfStr(b, n, sd + 8 + (size_t)(k + 1) * 16 + 8, smp, sizeof smp);
                        if (!strcmp(smp, useA1 ? "_a1" : "_a0")) BfStr(b, n, tn + (size_t)k * 8, p->texAlb[id], 128);
                        else if (!strcmp(smp, "_o0")) BfStr(b, n, tn + (size_t)k * 8, p->texOpc[id], 128);
                        else if (!strcmp(smp, "_e0")) BfStr(b, n, tn + (size_t)k * 8, p->texEmm[id], 128);
                        else if (!strcmp(smp, "_msk0")) {
                            // the mask cuts the shape when the material takes its see-through from it (drive_alpha_color 4;
                            // e.g. the far-off suspension bridge's cable cards, the coaster net wire) - used like an opacity map
                            char ac[16]; BfMatOption(b, n, mt, "drive_alpha_color", ac, sizeof ac);
                            if (!strcmp(ac, "4") && !p->texOpc[id][0]) BfStr(b, n, tn + (size_t)k * 8, p->texOpc[id], 128);
                        }
                        else { char tnm[128]; BfStr(b, n, tn + (size_t)k * 8, tnm, 128); if (strstr(tnm, "EmmMix")) snprintf(p->texMix[id], 128, "%s", tnm); }
                    }
                    if (useA1) {
                        // _a1 is the colour only when it isn't darker than _a0: "a1~a0" - the viewer reads both and keeps the
                        // brighter (MKW's apple: red AppleBody_03 over a dark grain; Ice Cream island: colours over a grain
                        // Face_00) - a dark _a1 is a pattern blended on top (DK Pass: SnowLine_00, near-black lines over
                        // the snow - the roads were black in .209-.215; SnowGradation_00 over the metal roof)
                        char a0n[128] = "";
                        for (int k = 0; k < nt && tn && sd && sd + 8 + (size_t)(k + 2) * 16 <= n; k++) {
                            char smp[16]; BfStr(b, n, sd + 8 + (size_t)(k + 1) * 16 + 8, smp, sizeof smp);
                            if (!strcmp(smp, "_a0")) BfStr(b, n, tn + (size_t)k * 8, a0n, sizeof a0n);
                        }
                        if (!p->texAlb[id][0]) snprintf(p->texAlb[id], 128, "%s", a0n);         // (no _a1 after all: its _a0)
                        else if (a0n[0] && strlen(p->texAlb[id]) + strlen(a0n) + 2 <= 128) {
                            char a1n2[128]; snprintf(a1n2, sizeof a1n2, "%s", p->texAlb[id]);
                            snprintf(p->texAlb[id], 128, "%s~%s", a1n2, a0n);
                        }
                    }
                    // no colour texture and drive_albedo_color 900 (MKW: the colour is black - Boo Cinema's film world: the
                    // castle walls, the cloud sky): its glow texture is what shows (the clouds), else draw it near-black
                    if (!p->texAlb[id][0]) {
                        char ac[16]; BfMatOption(b, n, mt, "drive_albedo_color", ac, sizeof ac);
                        // (not water - e.g. the Aurora track's Mt_Water: drawn with the viewer's water look, as before .208, not
                        // its aurora glow stripes)
                        if (!strcmp(ac, "900") && !strstr(p->matName[id], "Water")) {
                            // a strong glow (drive_emission_scale other than the usual 110 - MKW's boost arches
                            // AccelRing001: 202, white-cyan lightning in the game; their glow texture is a mid grey-teal):
                            // drawn brightened, "*Glow:<emission>"
                            char es[16]; BfMatOption(b, n, mt, "drive_emission_scale", es, sizeof es);
                            if (p->texEmm[id][0] && es[0] && strcmp(es, "110")) snprintf(p->texAlb[id], 128, "*Glow:%s", p->texEmm[id]);
                            else snprintf(p->texAlb[id], 128, "%s", p->texEmm[id][0] ? p->texEmm[id] : "*Dark");
                        }
                    }
                    // the Aurora track's road (CmnObj_SpecialSurfaceAurora000: Mt_Water, no colour texture, its glow is
                    // CmnTex_Special_AuroraRiver_Emm - a dark band with cyan edges): bright cyan water with the glow on top,
                    // as in the game ("*Aurora:<glow>"; .208-.212 drew the glow alone - stripes; .213-.217 the plain water look)
                    if (!p->texAlb[id][0] && strstr(p->matName[id], "Water") && !strcmp(p->texEmm[id], "CmnTex_Special_AuroraRiver_Emm"))
                        // (with the speckled glow of the water below it, CmnTex_Special_AuroraRiver_UnderWater_Emm - the
                        // road is see-through in the game; its own band glow gave stripes all along the road in .218)
                        snprintf(p->texAlb[id], 128, "*Aurora:CmnTex_Special_AuroraRiver_UnderWater_Emm");
                }
                p->partBone[id] = (short)(W && bone < nb && mi == 0 && nModels == 1 ? bone : -1);
                // parts on a hidden bone (alternative eyelids, mouths...) start hidden, as in the game
                if (W && bone < nb) {
                    uint64_t ba = BfU64(b + sk + 0x10); size_t bo = ba + (size_t)bone * 0x58;
                    if (bo + 0x58 <= n && !(VoxRd32(b + bo + 0x2C) & 1)) p->startHidden[id] = 1;
                }
            }
            for (uint32_t k = 0; k + 2 < icnt && ct < capT; k += 3) {
                uint32_t t3[3];
                for (int c = 0; c < 3; c++) { size_t a = ib + (size_t)(k + c) * isz; t3[c] = (isz == 4 ? VoxRd32(b + a) : isz == 2 ? VoxRd16(b + a) : b[a]) + first; }
                if (t3[0] >= nv || t3[1] >= nv || t3[2] >= nv) continue;
                p->ti[ct * 3] = (uint32_t)(v0 + t3[0]); p->ti[ct * 3 + 1] = (uint32_t)(v0 + t3[1]); p->ti[ct * 3 + 2] = (uint32_t)(v0 + t3[2]);
                p->tmat[ct++] = (uint16_t)id; p->matCount[id]++;
            }
            part++;
        }
        free(W); free(m2b);
    }
    free(b);
    p->ntv = cv; p->nti = ct; p->sections = part; p->nmat = part < 255 ? part : 255;
    snprintf(p->shapeType, sizeof p->shapeType, "BFRES model");
    if (!ct) { snprintf(p->err, sizeof p->err, "No triangles could be read from this model."); return 0; }
    return 1;
}

/* ---------------- BFRES skeletal animations (FSKA): curves per bone; pose the model's skeleton and skin it ---------------- */
typedef struct { int target; int ctype, nkey; float scale, offset; float *fr; float *key; int epk; } BfCurve;
typedef struct { int skBone; int baseFlags; float base[10]; int ncurve; BfCurve *c; } BfBoneAnim;   // base: s3 r4 t3
typedef struct { char name[96]; int frames, loop, euler; int nb; BfBoneAnim *b; } BfSkelAnim;
// an X.anim.bfres file: its skeletal (FSKA) and shape / face (FSHA) clips, merged by name
typedef struct { unsigned char *b; size_t n; int count; char (*names)[96]; int *frames; int *sk, *sh, *ma, *vi; uint64_t arr, shArr, maArr, viArr; int nsk, nplay; } BfAnimFile;

static void BfAnimFileFree(BfAnimFile *f) { free(f->b); free(f->names); free(f->frames); free(f->sk); free(f->sh); free(f->ma); free(f->vi); memset(f, 0, sizeof *f); }
static void BfSkelAnimFree(BfSkelAnim *a) {
    for (int i = 0; i < a->nb; i++) { for (int k = 0; k < a->b[i].ncurve; k++) { free(a->b[i].c[k].fr); free(a->b[i].c[k].key); } free(a->b[i].c); }
    free(a->b); memset(a, 0, sizeof *a);
}
static int BfAnimFileOpen(BfAnimFile *f, const VOXCHAR *path) {
    memset(f, 0, sizeof *f);
    size_t zn; unsigned char *z = VoxReadFile(path, &zn); if (!z) return 0;
    f->b = VoxUnzstd(z, zn, &f->n); free(z);
    if (!f->b || f->n < 0x100 || memcmp(f->b, "FRES", 4)) { BfAnimFileFree(f); return 0; }
    // skeletal (FSKA, 0x50 each), shape (FSHA, 0x50), material (FMAA, 0x70) and bone visibility (FBVS, 0x60) clips
    int cnt = VoxRd16(f->b + 0xE2), mac = VoxRd16(f->b + 0xE4), vic = VoxRd16(f->b + 0xE6), shc = VoxRd16(f->b + 0xE8);
    f->arr = BfU64(f->b + 0x58); f->maArr = BfU64(f->b + 0x68); f->viArr = BfU64(f->b + 0x78); f->shArr = BfU64(f->b + 0x88);
    if (!f->arr) cnt = 0;
    if (!f->shArr) shc = 0;
    if (!f->maArr) mac = 0;
    if (!f->viArr) vic = 0;
    int cap = cnt + shc + mac + vic; if (!cap) return 1;
    f->names = (char (*)[96])calloc(cap, 96); f->frames = (int *)calloc(cap, sizeof(int));
    f->sk = (int *)malloc(cap * sizeof(int)); f->sh = (int *)malloc(cap * sizeof(int)); f->ma = (int *)malloc(cap * sizeof(int)); f->vi = (int *)malloc(cap * sizeof(int));
    if (!f->names || !f->frames || !f->sk || !f->sh || !f->ma || !f->vi) { BfAnimFileFree(f); return 0; }
    struct { int n; uint64_t arr; int stride, frOff; const char *magic; int which; } kinds[4] = {
        { cnt, f->arr, 0x50, 0x40, "FSKA", 0 }, { shc, f->shArr, 0x50, 0x40, "FSHA", 1 }, { mac, f->maArr, 0x70, 0x58, "FMAA", 2 }, { vic, f->viArr, 0x60, 0x50, "FBVS", 3 } };
    for (int t = 0; t < 4; t++) {
        for (int i = 0; i < kinds[t].n; i++) {
            size_t o = kinds[t].arr + (size_t)i * kinds[t].stride; if (o + kinds[t].stride > f->n || memcmp(f->b + o, kinds[t].magic, 4)) break;
            char nm[96]; BfStr(f->b, f->n, o + 8, nm, 96);
            int k = -1; for (int j = 0; j < f->count; j++) if (!strcmp(f->names[j], nm)) { k = j; break; }
            if (k < 0) { k = f->count++; snprintf(f->names[k], 96, "%s", nm); f->sk[k] = f->sh[k] = f->ma[k] = f->vi[k] = -1; f->frames[k] = 0; }
            int *slot = t == 0 ? f->sk : t == 1 ? f->sh : t == 2 ? f->ma : f->vi; slot[k] = i;
            int fr = (int)VoxRd32(f->b + o + kinds[t].frOff); if (fr > f->frames[k]) f->frames[k] = fr;
        }
    }
    // nsk: clips that move bones; nplay: clips this viewer can show something for (a file of material colour
    // clips only, e.g. DonkeyKongAnimation, counts as having none)
    int body = 0; for (int k = 0; k < f->count; k++) { if (f->sk[k] >= 0) f->nsk++; if (f->sk[k] >= 0 || f->sh[k] >= 0 || f->vi[k] >= 0) body++; }
    f->nplay = body ? f->count : 0;
    return 1;
}
// one animation curve (0x30 bytes at c) -> cv; 0 if unusable
static int BfCurveRead(const unsigned char *b, size_t n, size_t c, BfCurve *cv) {
            uint64_t fo = BfU64(b + c), ko = BfU64(b + c + 8);
            unsigned cf = VoxRd16(b + c + 0x10); int nk = VoxRd16(b + c + 0x12);
            cv->target = (int)VoxRd32(b + c + 0x14); cv->scale = BfF(b + c + 0x24); cv->offset = BfF(b + c + 0x28);   // (0x18 start, 0x1C end, 0x20 delta)
            int ft = cf & 3, kt = (cf >> 2) & 3; cv->ctype = (cf >> 4) & 7; cv->nkey = nk;
            if (cf & 0x4000) {
                // Switch 2 (Ounce) curves: Hermite keys = (value, slope per frame). Frames: f32 / u16 / u8, times the
                // frame scale at +0x20 when bit 0x400 is set (else u16 is 1/32 frame). Quantized keys (u16 / u8)
                // start with the slope's scale and offset (2 x f32); values use the curve's scale and offset.
                float fsc = (cf & 0x400) ? BfF(b + c + 0x20) : (ft == 1 ? 1 / 32.0f : 1);
                if (fsc == 0) fsc = ft == 1 ? 1 / 32.0f : 1;
                int fs = ft == 0 ? 4 : ft == 1 ? 2 : 1, ks = kt == 0 ? 4 : kt == 1 ? 2 : 1;
                size_t kpre = kt ? 8 : 0;
                cv->ctype = 8; cv->epk = 2;
                cv->fr = (float *)malloc((nk + 1) * sizeof(float)); cv->key = (float *)malloc(((size_t)nk * 2 + 1) * sizeof(float));
                if (!cv->fr || !cv->key || !nk || fo + (size_t)nk * fs > n || ko + kpre + (size_t)nk * 2 * ks > n) { free(cv->fr); free(cv->key); cv->fr = cv->key = NULL; return 0; }
                for (int j = 0; j < nk; j++) cv->fr[j] = ft == 0 ? BfF(b + fo + j * 4) : ft == 1 ? VoxRd16(b + fo + j * 2) * fsc : b[fo + j] * fsc;
                float ss = kt ? BfF(b + ko) : 1, so = kt ? BfF(b + ko + 4) : 0;
                for (int j = 0; j < nk; j++) {
                    size_t q = ko + kpre + (size_t)j * 2 * ks;
                    float val, slope;
                    if (kt == 0) { val = BfF(b + q); slope = BfF(b + q + 4); }
                    else if (kt == 1) { val = VoxRd16(b + q) * cv->scale + cv->offset; slope = VoxRd16(b + q + 2) * ss + so; }
                    else { val = b[q] * cv->scale + cv->offset; slope = b[q + 1] * ss + so; }
                    cv->key[j * 2] = val; cv->key[j * 2 + 1] = slope;
                }
                cv->scale = 1; cv->offset = 0;          // already applied
                return 1;
            }
            cv->epk = cv->ctype == 0 ? 4 : cv->ctype == 1 ? 2 : 1;
            cv->fr = (float *)malloc((nk + 1) * sizeof(float)); cv->key = (float *)malloc(((size_t)nk * cv->epk + 1) * sizeof(float));
            if (!cv->fr || !cv->key || !nk) { free(cv->fr); free(cv->key); cv->fr = cv->key = NULL; return 0; }
            int fs = ft == 0 ? 4 : ft == 1 ? 2 : 1, ks = kt == 0 ? 4 : kt == 1 ? 2 : 1;
            if (fo + (size_t)nk * fs > n || ko + (size_t)nk * cv->epk * ks > n) { free(cv->fr); free(cv->key); cv->fr = cv->key = NULL; return 0; }
            for (int j = 0; j < nk; j++) cv->fr[j] = ft == 0 ? BfF(b + fo + j * 4) : ft == 1 ? (int16_t)VoxRd16(b + fo + j * 2) / 32.0f : b[fo + j];
            for (int j = 0; j < nk * cv->epk; j++) {
                float x;
                if (kt == 0) x = (cv->ctype == 5 || cv->ctype == 6) ? (float)VoxRd32(b + ko + j * 4) : BfF(b + ko + j * 4);
                else if (kt == 1) x = (float)(int16_t)VoxRd16(b + ko + j * 2);
                else x = (float)(signed char)b[ko + j];
                cv->key[j] = x;
            }
            return 1;
}
// bones are matched by name among skeleton bones lo..hi-1 (an attached head or tie has its own range; the roots
// of an attached range - joined to the body - are left alone)
static int BfSkelAnimLoadRange(const BfAnimFile *f, int idx, const VoxPart *p, BfSkelAnim *a, int lo, int hi) {
    memset(a, 0, sizeof *a);
    if (idx < 0 || idx >= f->count) return 0;
    if (f->sk[idx] < 0) {                      // a face-only clip: no bones move
        snprintf(a->name, sizeof a->name, "%s", f->names[idx]); a->frames = f->frames[idx]; a->loop = 1; return 1;
    }
    const unsigned char *b = f->b; size_t n = f->n, o = f->arr + (size_t)f->sk[idx] * 0x50;
    uint32_t fl = VoxRd32(b + o + 4);
    snprintf(a->name, sizeof a->name, "%s", f->names[idx]); a->frames = f->frames[idx]; a->loop = (fl & 4) != 0; a->euler = (fl & 0x1000) != 0;
    int nb = VoxRd16(b + o + 0x4C); uint64_t ba = BfU64(b + o + 0x28);
    a->b = (BfBoneAnim *)calloc(nb + 1, sizeof(BfBoneAnim)); if (!a->b) return 0;
    for (int i = 0; i < nb; i++) {
        size_t bo = ba + (size_t)i * 0x38; if (bo + 0x38 > n) break;
        BfBoneAnim *ab = &a->b[a->nb++];
        char nm[64]; BfStr(b, n, bo, nm, sizeof nm);
        ab->skBone = -1;
        for (int k = lo; k < hi && k < p->skN; k++) {
            if (lo > 0 && p->skParent[k] < lo) continue;
            if (!strcmp(p->skName[k], nm)) { ab->skBone = k; break; }
        }
        uint64_t co = BfU64(b + bo + 8), bd = BfU64(b + bo + 0x10);
        uint32_t bf = VoxRd32(b + bo + 0x28); ab->baseFlags = bf & 0x38;
        int ncv = b[bo + 0x2E];
        size_t q = bd;
        if (bd) {
            if (bf & 8) { for (int k = 0; k < 3; k++) ab->base[k] = BfF(b + q + k * 4); q += 12; }
            if (bf & 16) { for (int k = 0; k < 4; k++) ab->base[3 + k] = BfF(b + q + k * 4); q += 16; }
            if (bf & 32) { for (int k = 0; k < 3; k++) ab->base[7 + k] = BfF(b + q + k * 4); q += 12; }
        }
        ab->c = (BfCurve *)calloc(ncv + 1, sizeof(BfCurve));
        for (int k = 0; k < ncv && ab->c; k++) {
            size_t c = co + (size_t)k * 0x30; if (c + 0x30 > n) break;
            BfCurve *cv = &ab->c[ab->ncurve];
            if (BfCurveRead(b, n, c, cv)) ab->ncurve++;
        }
    }
    return 1;
}
static float BfCurveEval(const BfCurve *c, float f);
/* ---------------- BFRES shape animations (FSHA): key shape weights over time (DK's face) ---------------- */
typedef struct { int key; float base; int curve; } BfShKey;               // key: index into p->keys
typedef struct { char name[96]; int frames, loop, nk, nc; BfShKey *k; BfCurve *c; } BfShapeAnim;
static void BfShapeAnimFree(BfShapeAnim *a) { for (int i = 0; i < a->nc; i++) { free(a->c[i].fr); free(a->c[i].key); } free(a->c); free(a->k); memset(a, 0, sizeof *a); }
// clip idx of f -> weights for p's key shapes on parts partLo..partHi-1 (matched by part name ending and key name)
static int BfShapeAnimLoad(const BfAnimFile *f, int idx, const VoxPart *p, int partLo, int partHi, BfShapeAnim *a) {
    memset(a, 0, sizeof *a);
    if (idx < 0 || idx >= f->count || f->sh[idx] < 0 || !p->nkeys) return 0;
    const unsigned char *b = f->b; size_t n = f->n, o = f->shArr + (size_t)f->sh[idx] * 0x50;
    snprintf(a->name, sizeof a->name, "%s", f->names[idx]); a->frames = (int)VoxRd32(b + o + 0x40); a->loop = (VoxRd16(b + o + 4) & 4) != 0;
    int nv = VoxRd16(b + o + 0x4A), nkTot = VoxRd16(b + o + 0x4C); uint64_t va = BfU64(b + o + 0x28);
    a->k = (BfShKey *)calloc(nkTot + 1, sizeof(BfShKey)); a->c = (BfCurve *)calloc(64, sizeof(BfCurve));
    if (!a->k || !a->c) { BfShapeAnimFree(a); return 0; }
    for (int v = 0; v < nv; v++) {
        size_t q = va + (size_t)v * 0x30; if (q + 0x30 > n) break;
        char shape[64]; BfStr(b, n, q, shape, sizeof shape);
        int nc = VoxRd16(b + q + 0x20), nk = VoxRd16(b + q + 0x22); uint64_t co = BfU64(b + q + 8), bd = BfU64(b + q + 0x10), ki = BfU64(b + q + 0x18);
        int c0 = a->nc;
        for (int c = 0; c < nc && a->nc < 64; c++) { size_t cc = co + (size_t)c * 0x30; if (cc + 0x30 > n || !BfCurveRead(b, n, cc, &a->c[a->nc])) { memset(&a->c[a->nc], 0, sizeof(BfCurve)); } a->nc++; }
        for (int k = 0; k < nk; k++) {
            size_t e = ki + (size_t)k * 16; if (e + 16 > n) break;
            char kn[32]; BfStr(b, n, e, kn, sizeof kn);
            int ci = b[e + 8];
            if (k == 0) continue;                      // the first key is the base shape itself; weights start at key 1
            float base = bd && bd + (size_t)k * 4 <= n ? BfF(b + bd + (size_t)(k - 1) * 4) : 0;
            int found = -1;
            for (int j = 0; j < p->nkeys; j++) {
                const struct VoxKey *K = &p->keys[j]; if (K->part < partLo || K->part >= partHi || strcmp(K->name, kn)) continue;
                const char *pn = p->matName[K->part]; size_t pl = strlen(pn), sl = strlen(shape);
                if (pl >= sl && !strcmp(pn + pl - sl, shape)) { found = j; break; }
            }
            if (found < 0) continue;                   // the base shape itself, or a part this model doesn't have
            BfShKey *S = &a->k[a->nk++]; S->key = found; S->base = base; S->curve = ci != 0xFF && c0 + ci < a->nc ? c0 + ci : -1;
        }
    }
    return a->nk > 0;
}
// posed-shape positions: tv plus each key shape's offset times its weight at frame f (out: ntv * 3 floats)
static void BfShapeApply(const VoxPart *p, const BfShapeAnim *const *as, int na, float f0, float *out) {
    memcpy(out, p->tv, p->ntv * 12);
    for (int ai = 0; ai < na; ai++) {
        const BfShapeAnim *a = as[ai]; if (!a) continue;
        float f = f0, last = a->frames > 1 ? (float)(a->frames - 1) : 0;
        if (f > last) f = a->loop && last > 0 ? fmodf(f, last) : last;
        for (int i = 0; i < a->nk; i++) {
            const BfShKey *S = &a->k[i]; float w = S->curve >= 0 ? BfCurveEval(&a->c[S->curve], f) : S->base;
            if (w == 0) continue;
            const struct VoxKey *K = &p->keys[S->key];
            for (uint32_t v = 0; v < K->nv && K->v0 + v < p->ntv; v++) { float *o = out + (K->v0 + v) * 3; const float *d = K->d + v * 3; o[0] += w * d[0]; o[1] += w * d[1]; o[2] += w * d[2]; }
        }
    }
}
/* ---------------- bone visibility clips (FBVS: DK's blinking eyelids) ---------------- */
typedef struct { int bone; int on0; int nk; float *fr; unsigned char *on; } BfVisTrack;
typedef struct { char name[96]; int frames, loop, n; BfVisTrack *t; } BfVisAnim;
static void BfVisAnimFree(BfVisAnim *a) { for (int i = 0; i < a->n; i++) { free(a->t[i].fr); free(a->t[i].on); } free(a->t); memset(a, 0, sizeof *a); }
static int BfVisAnimLoad(const BfAnimFile *f, int idx, const VoxPart *p, int lo, int hi, BfVisAnim *a) {
    memset(a, 0, sizeof *a);
    if (idx < 0 || idx >= f->count || f->vi[idx] < 0) return 0;
    const unsigned char *b = f->b; size_t n = f->n, o = f->viArr + (size_t)f->vi[idx] * 0x60;
    snprintf(a->name, sizeof a->name, "%s", f->names[idx]); a->frames = (int)VoxRd32(b + o + 0x50); a->loop = 1;
    int nn = VoxRd16(b + o + 0x58), nc = VoxRd16(b + o + 0x5A); uint64_t cv = BfU64(b + o + 0x28), bits = BfU64(b + o + 0x30), names = BfU64(b + o + 0x38);
    if (!names || nn <= 0) return 0;
    a->t = (BfVisTrack *)calloc(nn, sizeof(BfVisTrack)); if (!a->t) return 0;
    int *map = (int *)malloc(nn * sizeof(int)); if (!map) { free(a->t); a->t = NULL; return 0; }
    for (int i = 0; i < nn; i++) {
        char nm[64]; BfStr(b, n, names + (size_t)i * 8, nm, sizeof nm); map[i] = -1;
        int bone = -1; for (int k = lo; k < hi && k < p->skN; k++) if (!strcmp(p->skName[k], nm)) { bone = k; break; }
        if (bone < 0) continue;
        BfVisTrack *T = &a->t[a->n]; T->bone = bone; T->on0 = bits && bits + (size_t)(i / 8) < n ? (b[bits + i / 8] >> (i & 7)) & 1 : 1;
        map[i] = a->n++;
    }
    for (int c = 0; c < nc && cv; c++) {
        size_t q = cv + (size_t)c * 0x30; if (q + 0x30 > n) break;
        int tgt = (int)VoxRd32(b + q + 0x14); if (tgt < 0 || tgt >= nn || map[tgt] < 0) continue;
        unsigned cf = VoxRd16(b + q + 0x10); int nk = VoxRd16(b + q + 0x12), ft = cf & 3;
        uint64_t fo = BfU64(b + q), ko = BfU64(b + q + 8);
        float fsc = (cf & 0x400) ? BfF(b + q + 0x20) : (ft == 1 ? 1 / 32.0f : 1); if (fsc == 0) fsc = ft == 1 ? 1 / 32.0f : 1;
        int fs = ft == 0 ? 4 : ft == 1 ? 2 : 1;
        if (nk <= 0 || fo + (size_t)nk * fs > n || ko + (size_t)(nk + 7) / 8 > n) continue;
        BfVisTrack *T = &a->t[map[tgt]];
        T->fr = (float *)malloc(nk * sizeof(float)); T->on = (unsigned char *)malloc(nk); if (!T->fr || !T->on) { free(T->fr); free(T->on); T->fr = NULL; T->on = NULL; continue; }
        for (int j = 0; j < nk; j++) {
            T->fr[j] = ft == 0 ? BfF(b + fo + j * 4) : ft == 1 ? VoxRd16(b + fo + j * 2) * fsc : b[fo + j] * fsc;
            T->on[j] = (b[ko + j / 8] >> (j & 7)) & 1;            // one bit per key (step)
        }
        T->nk = nk;
    }
    free(map);
    return a->n > 0;
}
static int BfVisAt(const BfVisTrack *T, float f) {
    if (!T->nk) return T->on0;
    int k = 0; while (k + 1 < T->nk && T->fr[k + 1] <= f) k++;
    return T->on[k];
}

/* ---------------- material clips (FMAA): texture UV moves (DK's eyes looking around) ---------------- */
typedef struct { int part, which; float c[6]; int cur[6]; } BfUvTrack;     // which: 0 tex_mtx_albedo, 1 tex_mtx1 (eye shine)         // srt: mode, sx, sy, rot, tx, ty; cur: curve index or -1
typedef struct { char name[96]; int frames, loop, n, nc; BfUvTrack *t; BfCurve *c; } BfMatAnim;
static void BfMatAnimFree(BfMatAnim *a) { for (int i = 0; i < a->nc; i++) { free(a->c[i].fr); free(a->c[i].key); } free(a->c); free(a->t); memset(a, 0, sizeof *a); }
// only the albedo texture's matrix (tex_mtx_albedo) is used; parts are matched by material name (part "X__<material>")
static int BfMatAnimLoad(const BfAnimFile *f, int idx, const VoxPart *p, int partLo, int partHi, BfMatAnim *a) {
    memset(a, 0, sizeof *a);
    if (idx < 0 || idx >= f->count || f->ma[idx] < 0) return 0;
    const unsigned char *b = f->b; size_t n = f->n, o = f->maArr + (size_t)f->ma[idx] * 0x70;
    snprintf(a->name, sizeof a->name, "%s", f->names[idx]); a->frames = (int)VoxRd32(b + o + 0x58); a->loop = 1;
    int nm = VoxRd16(b + o + 0x62); uint64_t ma = BfU64(b + o + 0x28);
    a->t = (BfUvTrack *)calloc(256, sizeof(BfUvTrack)); a->c = (BfCurve *)calloc(64, sizeof(BfCurve));
    if (!a->t || !a->c) { BfMatAnimFree(a); return 0; }
    for (int m = 0; m < nm && ma; m++) {
        size_t q = ma + (size_t)m * 0x40; if (q + 0x40 > n) break;
        char mat[64]; BfStr(b, n, q, mat, sizeof mat);
        uint64_t pi = BfU64(b + q + 8), cv = BfU64(b + q + 0x18), cs = BfU64(b + q + 0x20);
        int np = VoxRd16(b + q + 0x32);
        for (int k = 0; k < np && pi; k++) {                 // param info: name, begin curve, curves, begin constant, -, constants
            size_t e = pi + (size_t)k * 0x18; if (e + 0x18 > n) break;
            char pn[48]; BfStr(b, n, e, pn, sizeof pn);
            int which = !strcmp(pn, "tex_mtx_albedo") ? 0 : !strcmp(pn, "tex_mtx1") ? 1 : -1; if (which < 0) continue;
            int bc = VoxRd16(b + e + 8), ncu = VoxRd16(b + e + 10), bk = VoxRd16(b + e + 14), nko = VoxRd16(b + e + 16);
            float c6[6] = { 0, 1, 1, 0, 0, 0 }; int cu[6] = { -1, -1, -1, -1, -1, -1 };
            for (int j = 0; j < nko && cs; j++) { size_t z = cs + (size_t)(bk + j) * 8; if (z + 8 > n) break; int off = (int)VoxRd32(b + z) / 4; if (off >= 0 && off < 6) c6[off] = BfF(b + z + 4); }
            for (int j = 0; j < ncu && cv && a->nc < 64; j++) {
                size_t z = cv + (size_t)(bc + j) * 0x30; if (z + 0x30 > n) break;
                BfCurve *C = &a->c[a->nc]; if (!BfCurveRead(b, n, z, C)) continue;
                int off = C->target / 4; if (off >= 0 && off < 6) cu[off] = a->nc; a->nc++;
            }
            for (int part = partLo; part < partHi && part < p->nmat && a->n < 256; part++) {
                const char *s2 = strstr(p->matName[part], "__"); if (!s2 || strcmp(s2 + 2, mat)) continue;
                BfUvTrack *T = &a->t[a->n++]; T->part = part; T->which = which; memcpy(T->c, c6, sizeof c6); memcpy(T->cur, cu, sizeof cu);
            }
        }
    }
    return a->n > 0;
}
/* ---------------- material colour clips (FMAA): DK's fur / pants colours, damage flashes, adrenaline glow ----------------
   Per material: texture pattern entries (sampler "_a0" = albedo; its constant is an index into the clip's texture-name
   list at FMAA +0x38) and shader params by name: basecolor_const (multiplies the albedo; 1 = as modelled),
   emissioncolor_const (glow), const_color3 (the damage flash colour). Param info (0x18 bytes): name, begin curve u16
   +8, float curves +10, int curves +12, begin constant +14, constants +16. A curve's target / 4 is the channel. */
typedef struct { int part; char albedo[128]; float v[3][3]; int cur[3][3]; unsigned char has[3];
                 char mask[64]; float pOff[3], pScale[3], pCol[4]; unsigned char hasPat; } BfColTrack;
/* pattern (FurColor18 and up, the "gradient" furs): a grey mask (DonkeyKongMask_Msk.N, sampler _u0 / _u1) projected
   onto the model from the front: mask u = (x - offset.x) / scale.x + 0.5, v = 1 - ((y - offset.y) / scale.y + 0.5)
   (pattern_projection_offset / _scale; this reading matches the game's look: shoulders keep the fur colour, hands,
   forearms and hips take the pattern colour). White = pattern_projection_color (rgb, a = strength). */   // v/cur: [base, emission, flash][rgb]
typedef struct { char name[96]; int frames, n, nc, animated; BfColTrack *t; BfCurve *c; } BfColAnim;
static void BfColAnimFree(BfColAnim *a) { for (int i = 0; i < a->nc; i++) { free(a->c[i].fr); free(a->c[i].key); } free(a->c); free(a->t); memset(a, 0, sizeof *a); }
static int BfColAnimLoad(const BfAnimFile *f, int idx, const VoxPart *p, BfColAnim *a) {
    memset(a, 0, sizeof *a);
    if (idx < 0 || idx >= f->count || f->ma[idx] < 0 || (f->sk && f->sk[idx] >= 0)) return 0;
    const unsigned char *b = f->b; size_t n = f->n, o = f->maArr + (size_t)f->ma[idx] * 0x70;
    snprintf(a->name, sizeof a->name, "%s", f->names[idx]); a->frames = (int)VoxRd32(b + o + 0x58);
    int nm = VoxRd16(b + o + 0x62), ntex = VoxRd16(b + o + 0x68); uint64_t ma = BfU64(b + o + 0x28), texNames = BfU64(b + o + 0x38);
    a->t = (BfColTrack *)calloc(256, sizeof(BfColTrack)); a->c = (BfCurve *)calloc(256, sizeof(BfCurve));
    if (!a->t || !a->c) { BfColAnimFree(a); return 0; }
    for (int m = 0; m < nm && ma; m++) {
        size_t q = ma + (size_t)m * 0x40; if (q + 0x40 > n) break;
        char mat[64]; BfStr(b, n, q, mat, sizeof mat);
        uint64_t pi = BfU64(b + q + 8), tp = BfU64(b + q + 0x10), cv = BfU64(b + q + 0x18), cs = BfU64(b + q + 0x20);
        int np = VoxRd16(b + q + 0x32), nt = VoxRd16(b + q + 0x34);
        BfColTrack T; memset(&T, 0, sizeof T);
        for (int g = 0; g < 3; g++) for (int c = 0; c < 3; c++) { T.v[g][c] = g == 0 ? 1.f : 0.f; T.cur[g][c] = -1; }
        int any = 0;
        for (int k = 0; k < nt && tp; k++) {               // albedo swap, pattern mask
            size_t e = tp + (size_t)k * 0x10; if (e + 0x10 > n) break;
            char smp[16]; BfStr(b, n, e, smp, sizeof smp);
            int isAlb = !strcmp(smp, "_a0"), isU = !strcmp(smp, "_u0") || !strcmp(smp, "_u1");
            if (!isAlb && !isU) continue;
            size_t z = cs + (size_t)VoxRd16(b + e + 0xA) * 8; if (!cs || z + 8 > n) continue;
            int ti = (int)VoxRd32(b + z + 4); if (ti < 0 || ti >= ntex || !texNames) continue;
            char tn[128]; BfStr(b, n, texNames + (size_t)ti * 8, tn, sizeof tn);
            if (isAlb) { snprintf(T.albedo, sizeof T.albedo, "%s", tn); if (T.albedo[0]) any = 1; }
            else if (strstr(tn, "Mask_Msk")) snprintf(T.mask, sizeof T.mask, "%s", tn);
        }
        for (int k = 0; k < np && pi; k++) {
            size_t e = pi + (size_t)k * 0x18; if (e + 0x18 > n) break;
            char pn[48]; BfStr(b, n, e, pn, sizeof pn);
            int pp = !strcmp(pn, "pattern_projection_offset") ? 0 : !strcmp(pn, "pattern_projection_scale") ? 1 : !strcmp(pn, "pattern_projection_color") ? 2 : -1;
            if (pp >= 0) {                                   // pattern settings: constants only
                int bk = VoxRd16(b + e + 14), nko = VoxRd16(b + e + 16);
                float *dst = pp == 0 ? T.pOff : pp == 1 ? T.pScale : T.pCol;
                for (int j = 0; j < nko && cs; j++) { size_t z = cs + (size_t)(bk + j) * 8; if (z + 8 > n) break; int ch = (int)VoxRd32(b + z) / 4; if (ch >= 0 && ch < (pp == 2 ? 4 : 3)) dst[ch] = BfF(b + z + 4); }
                T.hasPat |= (unsigned char)(1 << pp);
                continue;
            }
            int g = !strcmp(pn, "basecolor_const") ? 0 : !strcmp(pn, "emissioncolor_const") ? 1 : !strcmp(pn, "const_color3") ? 2 : -1;
            if (g < 0) continue;
            int bc = VoxRd16(b + e + 8), ncu = VoxRd16(b + e + 10), bk = VoxRd16(b + e + 14), nko = VoxRd16(b + e + 16);
            T.has[g] = 1; any = 1;
            for (int j = 0; j < nko && cs; j++) { size_t z = cs + (size_t)(bk + j) * 8; if (z + 8 > n) break; int ch = (int)VoxRd32(b + z) / 4; if (ch >= 0 && ch < 3) T.v[g][ch] = BfF(b + z + 4); }
            for (int j = 0; j < ncu && cv && a->nc < 256; j++) {
                size_t z = cv + (size_t)(bc + j) * 0x30; if (z + 0x30 > n) break;
                BfCurve *C = &a->c[a->nc]; if (!BfCurveRead(b, n, z, C)) continue;
                int ch = C->target / 4; if (ch >= 0 && ch < 3) { T.cur[g][ch] = a->nc; a->animated = 1; } a->nc++;
            }
        }
        T.hasPat = T.hasPat == 7 && T.mask[0] && T.pCol[3] > 0 && T.pScale[0] != 0 && T.pScale[1] != 0;
        if (!any) continue;
        for (int part = 0; part < p->nmat && a->n < 256; part++) {
            const char *s2 = strstr(p->matName[part], "__"); if (!s2 || strcmp(s2 + 2, mat)) continue;
            // a texture swap only goes to parts using the same texture family (DonkeyKongFur_Alb.0 -> .5): ties
            // also have an "Mt_Body" material, with their own textures
            if (T.albedo[0]) {
                const char *d1 = strrchr(T.albedo, '.'), *d2 = strrchr(p->texAlb[part], '.');
                size_t l1 = d1 ? (size_t)(d1 - T.albedo) : strlen(T.albedo), l2 = d2 ? (size_t)(d2 - p->texAlb[part]) : strlen(p->texAlb[part]);
                if (l1 != l2 || strncmp(T.albedo, p->texAlb[part], l1)) continue;
            }
            a->t[a->n] = T; a->t[a->n].part = part; a->n++;
        }
    }
    if (!a->n) { BfColAnimFree(a); return 0; }
    return 1;
}
// a track's base colour, emission and flash colour at frame f
static void BfColAt(const BfColAnim *a, const BfColTrack *T, float f, float out[3][3]) {
    for (int g = 0; g < 3; g++) for (int c = 0; c < 3; c++) out[g][c] = T->cur[g][c] >= 0 ? BfCurveEval(&a->c[T->cur[g][c]], f) : T->v[g][c];
}

// 2x3 UV matrix of track T at frame f (Maya-style texture SRT, as in Nintendo's tools)
static void BfUvAt(const BfMatAnim *a, const BfUvTrack *T, float f, float *M) {
    float v[6]; for (int k = 0; k < 6; k++) v[k] = T->cur[k] >= 0 ? BfCurveEval(&a->c[T->cur[k]], f) : T->c[k];
    float sx = v[1], sy = v[2], r = v[3], tx = v[4], ty = v[5], c = cosf(r), s = sinf(r);
    M[0] = sx * c; M[1] = sx * s; M[2] = sx * ((-0.5f * c) - (0.5f * s - 0.5f) - tx);
    M[3] = -sy * s; M[4] = sy * c; M[5] = sy * ((0.5f * s) - (0.5f * c - 0.5f) + ty);
}
static inline int BfSkelAnimLoad(const BfAnimFile *f, int idx, const VoxPart *p, BfSkelAnim *a) { return BfSkelAnimLoadRange(f, idx, p, a, 0, p->skN); }
static int BfSkelAnimFind(const BfAnimFile *f, const char *name) { for (int i = 0; i < f->count; i++) if (!strcmp(f->names[i], name)) return i; return -1; }
static float BfCurveEval(const BfCurve *c, float f) {
    if (!c->fr || !c->nkey) return 0;
    int k = 0;
    if (f <= c->fr[0]) k = 0;
    else { int lo = 0, hi = c->nkey - 1; while (lo < hi) { int mid = (lo + hi + 1) / 2; if (c->fr[mid] <= f) lo = mid; else hi = mid - 1; } k = lo; }
    const float *K = c->key + (size_t)k * c->epk;
    float t = 0;
    if (k + 1 < c->nkey && c->fr[k + 1] > c->fr[k]) t = (f - c->fr[k]) / (c->fr[k + 1] - c->fr[k]);
    if (t < 0) t = 0;
    float v;
    if (c->ctype == 8) {                                                       // Hermite: value + slope per frame at each key
        if (k + 1 >= c->nkey || c->fr[k + 1] <= c->fr[k]) return K[0];
        float d = c->fr[k + 1] - c->fr[k], t2 = t * t, t3 = t2 * t;
        const float *N = K + 2;
        return (2 * t3 - 3 * t2 + 1) * K[0] + (t3 - 2 * t2 + t) * d * K[1] + (-2 * t3 + 3 * t2) * N[0] + (t3 - t2) * d * N[1];
    }
    if (c->ctype == 0) v = ((K[3] * t + K[2]) * t + K[1]) * t + K[0];         // cubic (Hermite coefficients)
    else if (c->ctype == 1) v = K[0] + K[1] * t;                               // linear: value, delta
    else v = K[0];                                                              // baked / step
    return v * c->scale + c->offset;
}
// the animation's pose at frame f -> each skinning matrix (12 floats per bone: animated world x inverse bind world)
// skinM: 12 floats per bone (posed world x inverse bind); worldOut (optional): 12 per bone, the posed world matrices
static void BfQuatFromM(const BfM *M, float q[4]);
static void BfPoseMulti(const VoxPart *p, const BfSkelAnim *const *as, int na, float f0, float *skinM, float *worldOut) {
    int n = p->skN; if (n <= 0) return;
    float (*loc)[10] = (float (*)[10])malloc((size_t)n * sizeof(float[10]));
    unsigned char *eul = (unsigned char *)malloc(n);
    BfM *W = (BfM *)malloc((size_t)n * sizeof(BfM));
    if (!loc || !eul || !W) { free(loc); free(eul); free(W); return; }
    for (int i = 0; i < n; i++) { memcpy(loc[i], p->skBind[i], sizeof loc[i]); eul[i] = p->skEuler[i]; }
    for (int ai = 0; ai < na; ai++) {
      const BfSkelAnim *a = as[ai]; if (!a) continue;
      float f = f0, last = a->frames > 1 ? (float)(a->frames - 1) : 0;   // a head/tie clip may be a different length
      if (f > last) f = a->loop && last > 0 ? fmodf(f, last) : last;
      for (int i = 0; i < a->nb; i++) {
        const BfBoneAnim *ab = &a->b[i]; int k = ab->skBone; if (k < 0 || k >= n) continue;
        float *L = loc[k];
        int rotSet = (ab->baseFlags & 16) != 0;
        for (int c = 0; c < ab->ncurve && !rotSet; c++) if (ab->c[c].target >= 0x20 && ab->c[c].target <= 0x2C) rotSet = 1;
        if (rotSet && eul[k] != (unsigned char)a->euler) {           // the clip gives this bone's rotation in its own form
            if (!(ab->baseFlags & 16)) {                               // curves only: start from the bind rotation, converted
                BfM R0 = BfLocal((const float[3]){ 1, 1, 1 }, L + 3, (const float[3]){ 0, 0, 0 }, eul[k]);
                if (a->euler) { float sy = -R0.m[2][0]; if (sy > 1) sy = 1; if (sy < -1) sy = -1;
                    L[4] = asinf(sy); L[3] = atan2f(R0.m[2][1], R0.m[2][2]); L[5] = atan2f(R0.m[1][0], R0.m[0][0]); L[6] = 1; }
                else BfQuatFromM(&R0, L + 3);
            }
            eul[k] = (unsigned char)a->euler;
        }
        if (ab->baseFlags & 8) memcpy(L, ab->base, 12);
        if (ab->baseFlags & 16) memcpy(L + 3, ab->base + 3, 16);
        if (ab->baseFlags & 32) memcpy(L + 7, ab->base + 7, 12);
        for (int c = 0; c < ab->ncurve; c++) {
            float v = BfCurveEval(&ab->c[c], f); int t = ab->c[c].target;
            if (t >= 0x04 && t <= 0x0C) L[(t - 4) / 4] = v;           // scale
            else if (t >= 0x10 && t <= 0x18) L[7 + (t - 0x10) / 4] = v;  // translation
            else if (t >= 0x20 && t <= 0x2C) L[3 + (t - 0x20) / 4] = v;  // rotation
        }
      }
    }
    for (int i = 0; i < n; i++) {
        float r[4]; memcpy(r, loc[i] + 3, 16);
        if (!eul[i]) { float l = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2] + r[3] * r[3]); if (l > 0) for (int k = 0; k < 4; k++) r[k] /= l; }
        BfM Lm = BfLocal(loc[i], r, loc[i] + 7, eul[i]);
        int par = p->skParent[i];
        W[i] = par >= 0 && par < i ? BfMul(&W[par], &Lm) : Lm;
    }
    for (int i = 0; i < n; i++) {
        // inverse of the bind world matrix (affine)
        const float *B = p->skBindW + (size_t)i * 12;
        float a00 = B[0], a01 = B[1], a02 = B[2], a10 = B[4], a11 = B[5], a12 = B[6], a20 = B[8], a21 = B[9], a22 = B[10];
        float det = a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) + a02 * (a10 * a21 - a11 * a20);
        BfM I; memset(&I, 0, sizeof I);
        if (fabsf(det) > 1e-12f) {
            float id = 1 / det;
            I.m[0][0] = (a11 * a22 - a12 * a21) * id; I.m[0][1] = (a02 * a21 - a01 * a22) * id; I.m[0][2] = (a01 * a12 - a02 * a11) * id;
            I.m[1][0] = (a12 * a20 - a10 * a22) * id; I.m[1][1] = (a00 * a22 - a02 * a20) * id; I.m[1][2] = (a02 * a10 - a00 * a12) * id;
            I.m[2][0] = (a10 * a21 - a11 * a20) * id; I.m[2][1] = (a01 * a20 - a00 * a21) * id; I.m[2][2] = (a00 * a11 - a01 * a10) * id;
            for (int r2 = 0; r2 < 3; r2++) I.m[r2][3] = -(I.m[r2][0] * B[3] + I.m[r2][1] * B[7] + I.m[r2][2] * B[11]);
        } else { I.m[0][0] = I.m[1][1] = I.m[2][2] = 1; }
        BfM S = BfMul(&W[i], &I);
        memcpy(skinM + (size_t)i * 12, S.m, 48);
        if (worldOut) memcpy(worldOut + (size_t)i * 12, W[i].m, 48);
    }
    free(loc); free(eul); free(W);
}
static inline void BfPoseEx(const VoxPart *p, const BfSkelAnim *a, float f, float *skinM, float *worldOut) { BfPoseMulti(p, &a, 1, f, skinM, worldOut); }
static inline void BfPose(const VoxPart *p, const BfSkelAnim *a, float f, float *skinM) { BfPoseMulti(p, &a, 1, f, skinM, NULL); }
static int BfBoneIndex(const VoxPart *p, const char *name) { for (int i = 0; i < p->skN; i++) if (!strcmp(p->skName[i], name)) return i; return -1; }

// rotation (orthonormal 3x3 of M) -> quaternion x, y, z, w
static void BfQuatFromM(const BfM *M, float q[4]) {
    float m00 = M->m[0][0], m11 = M->m[1][1], m22 = M->m[2][2], tr = m00 + m11 + m22;
    if (tr > 0) { float s = sqrtf(tr + 1) * 2; q[3] = 0.25f * s; q[0] = (M->m[2][1] - M->m[1][2]) / s; q[1] = (M->m[0][2] - M->m[2][0]) / s; q[2] = (M->m[1][0] - M->m[0][1]) / s; }
    else if (m00 > m11 && m00 > m22) { float s = sqrtf(1 + m00 - m11 - m22) * 2; q[3] = (M->m[2][1] - M->m[1][2]) / s; q[0] = 0.25f * s; q[1] = (M->m[0][1] + M->m[1][0]) / s; q[2] = (M->m[0][2] + M->m[2][0]) / s; }
    else if (m11 > m22) { float s = sqrtf(1 + m11 - m00 - m22) * 2; q[3] = (M->m[0][2] - M->m[2][0]) / s; q[0] = (M->m[0][1] + M->m[1][0]) / s; q[1] = 0.25f * s; q[2] = (M->m[1][2] + M->m[2][1]) / s; }
    else { float s = sqrtf(1 + m22 - m00 - m11) * 2; q[3] = (M->m[1][0] - M->m[0][1]) / s; q[0] = (M->m[0][2] + M->m[2][0]) / s; q[1] = (M->m[1][2] + M->m[2][1]) / s; q[2] = 0.25f * s; }
}
static BfM BfRigidInverse(const BfM *A) {      // inverse of a rotation + translation
    BfM I; for (int r = 0; r < 3; r++) { for (int c = 0; c < 3; c++) I.m[r][c] = A->m[c][r]; I.m[r][3] = -(A->m[0][r] * A->m[0][3] + A->m[1][r] * A->m[1][3] + A->m[2][r] * A->m[2][3]); }
    return I;
}

// Attach model src (a head, a tie...) to dst at dst's bone 'attach' (e.g. NeckPoint): src's geometry, parts and
// skeleton are appended to dst, src's root bones become children of 'attach'. src is placed in the attach bone's
// frame (models made around their own origin) - or left where it is if it was modelled in place on the body.
// The bind pose is baked in, so the merged model looks right unposed and every bone animates as before.
// Returns the first new bone index (bones first..dst->skN-1 belong to src), or -1.
// fix: NULL = guess the placement (DK's head and ties); else 6 floats from the actor pack's FixInfo, LocalRotate X Y Z
// in degrees then LocalTrans X Y Z: src's origin sits at the bone times that local transform, as the game places it
static int VoxAttachModel(VoxPart *dst, const VoxPart *src, int attach, const char *label, int *partLo, float *TbOut, const float *fix) {
    if (!dst->skN || !src->skN || attach < 0 || attach >= dst->skN || !src->vBone || !dst->vBone) return -1;
    if (dst->nmat + src->nmat > 256) return -1;
    BfM A; memcpy(A.m, dst->skBindW + (size_t)attach * 12, 48);
    // in place or around the origin? compare the centre of src with the origin and the attach point
    double c[3] = { 0, 0, 0 };
    for (size_t i = 0; i < src->ntv; i++) for (int k = 0; k < 3; k++) c[k] += src->tv[i * 3 + k];
    if (src->ntv) for (int k = 0; k < 3; k++) c[k] /= (double)src->ntv;
    double d0 = c[0] * c[0] + c[1] * c[1] + c[2] * c[2];
    double d1 = (c[0] - A.m[0][3]) * (c[0] - A.m[0][3]) + (c[1] - A.m[1][3]) * (c[1] - A.m[1][3]) + (c[2] - A.m[2][3]) * (c[2] - A.m[2][3]);
    BfM rootL;                                  // the joining bone's local transform under 'attach'
    int useFix = 0;
    if (fix) {
        const float d2r = 3.14159265f / 180, r[3] = { fix[0] * d2r, fix[1] * d2r, fix[2] * d2r };
        rootL = BfLocal((const float[3]){ 1, 1, 1 }, r, fix + 3, 1);
        // safety: parts are modelled upright, so in the bind pose the result should be close to upright (within ~60
        // degrees); if not, the angles were read wrongly - place it at the bone, upright, as for DK
        BfM T = BfMul(&A, &rootL); useFix = T.m[0][0] + T.m[1][1] + T.m[2][2] > 2.0f;
    }
    if (useFix) {}
    else if (fix) { BfM R = A; R.m[0][3] = R.m[1][3] = R.m[2][3] = 0; rootL = BfRigidInverse(&R); rootL.m[0][3] = rootL.m[1][3] = rootL.m[2][3] = 0; }
    else if (d1 < d0) rootL = BfRigidInverse(&A);    // modelled in place: follow the attach bone, no move at bind
    else { BfM R = A; R.m[0][3] = R.m[1][3] = R.m[2][3] = 0; rootL = BfRigidInverse(&R); rootL.m[0][3] = rootL.m[1][3] = rootL.m[2][3] = 0; }
    BfM Tb = BfMul(&A, &rootL);                 // where src's origin ends up in the bind pose
    if (TbOut) memcpy(TbOut, Tb.m, 48);
    size_t nv0 = dst->ntv, nv = nv0 + src->ntv, nt0 = dst->nti, nt = nt0 + src->nti;
    int nb0 = dst->skN, nb = nb0 + src->skN, pm = dst->nmat;
    float *tv = (float *)realloc(dst->tv, nv * 12 + 12); if (tv) dst->tv = tv; else return -1;
    signed char *tn = (signed char *)realloc(dst->tn, nv * 4 + 4); if (tn) dst->tn = tn; else return -1;
    if (dst->tuv || src->tuv) {
        float *uv = (float *)realloc(dst->tuv, nv * 8 + 8); if (!uv) return -1;
        if (!dst->tuv) memset(uv, 0, nv0 * 8);
        dst->tuv = uv;
        if (src->tuv) memcpy(uv + nv0 * 2, src->tuv, src->ntv * 8); else memset(uv + nv0 * 2, 0, src->ntv * 8);
    }
    uint16_t *vb = (uint16_t *)realloc(dst->vBone, nv * 16 + 16); if (vb) dst->vBone = vb; else return -1;
    float *vw = (float *)realloc(dst->vW, nv * 32 + 32); if (vw) dst->vW = vw; else return -1;
    uint32_t *ti = (uint32_t *)realloc(dst->ti, nt * 12 + 12); if (ti) dst->ti = ti; else return -1;
    uint16_t *tm = (uint16_t *)realloc(dst->tmat, (nt + 1) * sizeof(uint16_t)); if (tm) dst->tmat = tm; else return -1;
    char (*sn)[64] = (char (*)[64])realloc(dst->skName, (size_t)(nb + 1) * 64); if (sn) dst->skName = sn; else return -1;
    short *sp = (short *)realloc(dst->skParent, (size_t)(nb + 1) * sizeof(short)); if (sp) dst->skParent = sp; else return -1;
    float (*sbd)[10] = (float (*)[10])realloc(dst->skBind, (size_t)(nb + 1) * sizeof(float[10])); if (sbd) dst->skBind = sbd; else return -1;
    unsigned char *se = (unsigned char *)realloc(dst->skEuler, nb + 1); if (se) dst->skEuler = se; else return -1;
    float *sw = (float *)realloc(dst->skBindW, (size_t)(nb + 1) * 48); if (sw) dst->skBindW = sw; else return -1;
    for (size_t i = 0; i < src->ntv; i++) {
        const float *P = src->tv + i * 3; float *o = dst->tv + (nv0 + i) * 3;
        for (int r = 0; r < 3; r++) o[r] = Tb.m[r][0] * P[0] + Tb.m[r][1] * P[1] + Tb.m[r][2] * P[2] + Tb.m[r][3];
        const signed char *N = src->tn ? src->tn + i * 4 : NULL; signed char *on = dst->tn + (nv0 + i) * 4;
        if (N) { for (int r = 0; r < 3; r++) { float x = Tb.m[r][0] * N[0] + Tb.m[r][1] * N[1] + Tb.m[r][2] * N[2]; on[r] = (signed char)(x > 127 ? 127 : x < -127 ? -127 : x); } on[3] = N[3]; }
        else memset(on, 0, 4);
        for (int k = 0; k < 8; k++) { dst->vBone[(nv0 + i) * 8 + k] = (uint16_t)(src->vBone[i * 8 + k] + nb0); dst->vW[(nv0 + i) * 8 + k] = src->vW[i * 8 + k]; }
    }
    for (size_t t = 0; t < src->nti; t++) {
        for (int k = 0; k < 3; k++) dst->ti[(nt0 + t) * 3 + k] = (uint32_t)(src->ti[t * 3 + k] + nv0);
        dst->tmat[nt0 + t] = (uint16_t)(src->tmat[t] + pm);
    }
    for (int i = 0; i < src->skN; i++) {
        int k = nb0 + i;
        memcpy(dst->skName[k], src->skName[i], 64);
        memcpy(dst->skBind[k], src->skBind[i], sizeof(float[10])); dst->skEuler[k] = src->skEuler[i];
        BfM W0; memcpy(W0.m, src->skBindW + (size_t)i * 12, 48); BfM W1 = BfMul(&Tb, &W0); memcpy(dst->skBindW + (size_t)k * 12, W1.m, 48);
        if (src->skParent[i] >= 0) dst->skParent[k] = (short)(src->skParent[i] + nb0);
        else {                                   // a root: hang it under the attach bone (its own local transform kept after the join)
            dst->skParent[k] = (short)attach;
            float r4[4]; memcpy(r4, src->skBind[i] + 3, 16);
            if (!src->skEuler[i]) { float l = sqrtf(r4[0] * r4[0] + r4[1] * r4[1] + r4[2] * r4[2] + r4[3] * r4[3]); if (l > 0) for (int q = 0; q < 4; q++) r4[q] /= l; }
            BfM L0 = BfLocal(src->skBind[i], r4, src->skBind[i] + 7, src->skEuler[i]);
            BfM L1 = BfMul(&rootL, &L0);
            float q[4]; BfQuatFromM(&L1, q);
            float *b = dst->skBind[k]; b[0] = b[1] = b[2] = 1; memcpy(b + 3, q, 16); b[7] = L1.m[0][3]; b[8] = L1.m[1][3]; b[9] = L1.m[2][3];
            dst->skEuler[k] = 0;
        }
    }
    if (src->nkeys) {                            // key shapes move with the vertices
        struct VoxKey *nk = (struct VoxKey *)realloc(dst->keys, (size_t)(dst->nkeys + src->nkeys) * sizeof *nk);
        if (nk) {
            dst->keys = nk;
            for (int k = 0; k < src->nkeys; k++) {
                struct VoxKey *K = &dst->keys[dst->nkeys + k]; *K = src->keys[k];
                K->v0 += nv0; K->part += pm;
                K->d = (float *)malloc((size_t)K->nv * 12 + 4); if (!K->d) { K->nv = 0; continue; }
                memcpy(K->d, src->keys[k].d, (size_t)K->nv * 12);
                for (uint32_t i = 0; i < K->nv; i++) {
                    float *d = K->d + i * 3, x = Tb.m[0][0] * d[0] + Tb.m[0][1] * d[1] + Tb.m[0][2] * d[2], y = Tb.m[1][0] * d[0] + Tb.m[1][1] * d[1] + Tb.m[1][2] * d[2], z = Tb.m[2][0] * d[0] + Tb.m[2][1] * d[1] + Tb.m[2][2] * d[2];
                    d[0] = x; d[1] = y; d[2] = z;
                }
            }
            dst->nkeys += src->nkeys;
        }
    }
    for (int m = 0; m < src->nmat; m++) {
        snprintf(dst->matName[pm + m], VOX_MATN, "%s: %s", label, src->matName[m]);
        dst->matCount[pm + m] = src->matCount[m];
        memcpy(dst->texAlb[pm + m], src->texAlb[m], 128); memcpy(dst->texOpc[pm + m], src->texOpc[m], 128); if (pm + m < 256) memcpy(dst->texEmm[pm + m], src->texEmm[m], 128); memcpy(dst->texMix[pm + m], src->texMix[m], 128);
        dst->startHidden[pm + m] = src->startHidden[m];
        dst->partBone[pm + m] = (short)(src->partBone[m] >= 0 ? src->partBone[m] + nb0 : -1);
    }
    dst->ntv = nv; dst->nti = nt; dst->skN = nb; dst->nmat = pm + src->nmat; dst->sections = dst->nmat;
    dst->nbones += src->nbones; dst->nmaterials += src->nmaterials;
    if (partLo) *partLo = pm;
    return nb0;
}
// posed position / normal of model vertex v
static void BfSkinVertexAt(const VoxPart *p, const float *skinM, size_t v, const float *P, float *pos, float *nrm);
static inline void BfSkinVertex(const VoxPart *p, const float *skinM, size_t v, float *pos, float *nrm) { BfSkinVertexAt(p, skinM, v, p->tv + v * 3, pos, nrm); }
// as BfSkinVertex, with the vertex's (morphed) bind position given
static void BfSkinVertexAt(const VoxPart *p, const float *skinM, size_t v, const float *P, float *pos, float *nrm) {
    const signed char *N = p->tn + v * 4;
    float n0 = N[0] / 127.0f, n1 = N[1] / 127.0f, n2 = N[2] / 127.0f;
    float o[3] = { 0, 0, 0 }, on[3] = { 0, 0, 0 };
    for (int k = 0; k < 8; k++) {
        float w = p->vW[v * 8 + k]; if (w <= 0) continue;
        int bi = p->vBone[v * 8 + k]; if (bi >= p->skN) continue;
        const float *M = skinM + (size_t)bi * 12;
        for (int r = 0; r < 3; r++) {
            o[r] += w * (M[r * 4] * P[0] + M[r * 4 + 1] * P[1] + M[r * 4 + 2] * P[2] + M[r * 4 + 3]);
            on[r] += w * (M[r * 4] * n0 + M[r * 4 + 1] * n1 + M[r * 4 + 2] * n2);
        }
    }
    memcpy(pos, o, 12);
    float l = sqrtf(on[0] * on[0] + on[1] * on[1] + on[2] * on[2]); if (l > 0) for (int k = 0; k < 3; k++) on[k] /= l;
    memcpy(nrm, on, 12);
}

// growing triangle soup for Phive shapes: positions + per-triangle 16-bit local material tag
typedef struct { float *v; size_t nv, cv; uint32_t *t; uint16_t *tag; size_t nt, ct; } PhSoup;
static int PhGrow(PhSoup *S, size_t addV, size_t addT) {
    if (S->nv + addV > S->cv) { size_t c = S->cv ? S->cv : 4096; while (c < S->nv + addV) c *= 2; float *v = (float *)realloc(S->v, c * 12); if (!v) return 0; S->v = v; S->cv = c; }
    if (S->nt + addT > S->ct) { size_t c = S->ct ? S->ct : 4096; while (c < S->nt + addT) c *= 2;
        uint32_t *t = (uint32_t *)realloc(S->t, c * 12); if (!t) return 0; S->t = t;
        uint16_t *g = (uint16_t *)realloc(S->tag, c * 2); if (!g) return 0; S->tag = g; S->ct = c; }
    return 1;
}
// X: 3x4 row-major transform (NULL = identity)
static void PhPut(PhSoup *S, const float *X, float x, float y, float z) {
    float *o = S->v + S->nv++ * 3;
    if (!X) { o[0] = x; o[1] = y; o[2] = z; return; }
    for (int r = 0; r < 3; r++) o[r] = X[r * 4] * x + X[r * 4 + 1] * y + X[r * 4 + 2] * z + X[r * 4 + 3];
}
// hknpMeshShape at 'base' -> soup. returns 1 ok, 0 bad data, -1 out of memory
static int PhMeshAt(const HkFile *h, int rti, size_t base, const float *X, PhSoup *S) {
    const unsigned char *b = h->b; size_t n = h->n;
    int oV, tV, oInv, oTag, oGs, tGs, oPr, oVb, oSo;
    if (!(HkMember(h, rti, "vertexConversionUtil", &oV, &tV) && HkMember(h, HkBody(h, tV), "bitScale16Inv", &oInv, NULL) &&
          HkMember(h, rti, "shapeTagTable", &oTag, NULL) && HkMember(h, rti, "geometrySections", &oGs, &tGs))) return 0;
    int gsub = h->T[HkBody(h, tGs)].subtype, gsz = HkSize(h, gsub);
    if (gsz <= 0 || !HkMember(h, gsub, "primitives", &oPr, NULL) || !HkMember(h, gsub, "vertexBuffer", &oVb, NULL) ||
        !HkMember(h, gsub, "sectionOffset", &oSo, NULL) || base + 160 > n) return 0;
    float inv; memcpy(&inv, b + base + oV + oInv, 4);
    int32_t toff = (int32_t)VoxRd32(b + base + oTag); uint32_t tcnt = VoxRd32(b + base + oTag + 4);
    int32_t goff = (int32_t)VoxRd32(b + base + oGs); uint32_t gcnt = VoxRd32(b + base + oGs + 4);
    size_t ta = base + oTag + toff, ga0 = base + oGs + goff;
    if (ta + (size_t)tcnt * 8 > n || ga0 + (size_t)gcnt * gsz > n) return 0;
    uint32_t tag = 0;
    for (uint32_t s = 0; s < gcnt; s++) {
        size_t ga = ga0 + (size_t)s * gsz;
        int32_t so[3] = { (int32_t)VoxRd32(b + ga + oSo), (int32_t)VoxRd32(b + ga + oSo + 4), (int32_t)VoxRd32(b + ga + oSo + 8) };
        int32_t po = (int32_t)VoxRd32(b + ga + oPr); uint32_t pc = VoxRd32(b + ga + oPr + 4);
        int32_t vo = (int32_t)VoxRd32(b + ga + oVb); uint32_t vc = VoxRd32(b + ga + oVb + 4);
        if (ga + oPr + po + (size_t)pc * 4 > n || ga + oVb + vo + (size_t)vc > n) return 0;
        const unsigned char *vb = b + ga + oVb + vo;
        size_t sv = so[0] == 0x7FFFFFFF ? vc / 12 : vc / 6;
        if (!PhGrow(S, sv, (size_t)pc * 2)) return -1;
        size_t v0 = S->nv;
        for (size_t i = 0; i < sv; i++) {
            float q[3];
            if (so[0] == 0x7FFFFFFF) memcpy(q, vb + i * 12, 12);
            else for (int k = 0; k < 3; k++) q[k] = (float)((double)(vb[i * 6 + k * 2] | (vb[i * 6 + k * 2 + 1] << 8)) + so[k]) * inv;
            PhPut(S, X, q[0], q[1], q[2]);
        }
        size_t ve = S->nv;
        for (uint32_t k = 0; k < pc; k++) {
            const unsigned char *q = b + ga + oPr + po + k * 4;
            uint32_t key = (s << 9) | (k << 1);
            while (tag + 1 < tcnt && VoxRd32(b + ta + (tag + 1) * 8) <= key) tag++;
            uint16_t m = 0xFFFF;
            if (tcnt && VoxRd32(b + ta + tag * 8) <= key) m = (uint16_t)(b[ta + tag * 8 + 4] | (b[ta + tag * 8 + 5] << 8));
            uint32_t ia = (uint32_t)(v0 + q[0]), ib = (uint32_t)(v0 + q[1]), ic = (uint32_t)(v0 + q[2]), id = (uint32_t)(v0 + q[3]);
            if (ia >= ve || ib >= ve || ic >= ve || id >= ve) continue;
            uint32_t *t = S->t + S->nt * 3; t[0] = ia; t[1] = ib; t[2] = ic; S->tag[S->nt++] = m;
            if (q[2] != q[3]) { t = S->t + S->nt * 3; t[0] = ia; t[1] = ic; t[2] = id; S->tag[S->nt++] = m; }
        }
    }
    return 1;
}
// convex hull (hknpBoxShape / convex polytope): vertices + faces as index fans, tag 0
static int PhHullAt(const HkFile *h, int rti, size_t base, const float *X, PhSoup *S) {
    const unsigned char *b = h->b; size_t n = h->n; int ho, ht = 0;
    if (!HkMember(h, rti, "hull", &ho, &ht) || base + ho + 32 > n) return 0;
    // vertex size: hkVector4 (16); Havok 2025 (Switch Sports...) stores hkFloat3 (12)
    size_t vs = 16; { int vo, vt = 0; if (ht > 0 && ht < h->nt && HkMember(h, ht, "vertices", &vo, &vt) && vt > 0 && vt < h->nt) { int st = h->T[vt].subtype; if (st > 0 && st < h->nt && HkSize(h, st) == 12) vs = 12; } }
    size_t A[4]; uint32_t C[4];
    for (int k = 0; k < 4; k++) { size_t f = base + ho + k * 8; A[k] = f + (int32_t)VoxRd32(b + f); C[k] = VoxRd32(b + f + 4); }
    if (A[0] + (size_t)C[0] * vs > n || A[2] + (size_t)C[2] * 4 > n || A[3] + C[3] > n || C[0] > 4096) return 0;
    size_t nt = 0;
    for (uint32_t f = 0; f < C[2]; f++) { unsigned num = b[A[2] + f * 4 + 2]; if (num >= 3) nt += num - 2; }
    if (!PhGrow(S, C[0], nt)) return -1;
    size_t v0 = S->nv;
    for (uint32_t i = 0; i < C[0]; i++) PhPut(S, X, BfFloat_(b + A[0] + i * vs), BfFloat_(b + A[0] + i * vs + 4), BfFloat_(b + A[0] + i * vs + 8));
    for (uint32_t f = 0; f < C[2]; f++) {
        unsigned first = b[A[2] + f * 4] | (b[A[2] + f * 4 + 1] << 8), num = b[A[2] + f * 4 + 2];
        if (first + num > C[3]) continue;
        for (unsigned k = 1; k + 1 < num; k++) {
            unsigned i0 = b[A[3] + first], i1 = b[A[3] + first + k], i2 = b[A[3] + first + k + 1];
            if (i0 >= C[0] || i1 >= C[0] || i2 >= C[0]) continue;
            uint32_t *t = S->t + S->nt * 3; t[0] = (uint32_t)(v0 + i0); t[1] = (uint32_t)(v0 + i1); t[2] = (uint32_t)(v0 + i2); S->tag[S->nt++] = 0;
        }
    }
    return 1;
}
static int PhShapeAt(const HkFile *h, int ti, size_t base, const float *X, PhSoup *S) {
    if (HkIsA(h, ti, "hknpMeshShape")) return PhMeshAt(h, ti, base, X, S);
    int ho; if (HkMember(h, ti, "hull", &ho, NULL)) return PhHullAt(h, ti, base, X, S);
    return 0;
}
// item whose data starts exactly at addr -> its type (0 if none)
static int HkTypeAtAddr(const HkFile *h, size_t addr) {
    for (size_t o = h->itemA; o + 12 <= h->itemE; o += 12) {
        int t = (int)(VoxRd32(h->b + o) & 0xFFFFFF);
        if (t && h->d0 + VoxRd32(h->b + o + 4) == addr) return t;
    }
    return 0;
}
// material name -> index (adds it; max 254)
static int PhMat(VoxPart *p, const char *name) {
    for (int i = 0; i < p->nmat; i++) if (!strcmp(p->matName[i], name)) return i;
    if (p->nmat >= VOX_MAXMAT - 2) return VOX_MAXMAT - 2;
    snprintf(p->matName[p->nmat], VOX_MATN, "%s", name); return p->nmat++;
}

static int VoxLoadPhive(VoxPart *p, const VOXCHAR *path) {
    memset(p, 0, sizeof(*p));
    p->isTriMesh = 1; p->scale[0] = p->scale[1] = p->scale[2] = 1;
    size_t zn; unsigned char *z = VoxReadFile(path, &zn);
    if (!z) { snprintf(p->err, sizeof p->err, "The file couldn't be read from the dump."); return 0; }
    size_t n; unsigned char *b = VoxUnzstd(z, zn, &n); free(z);
    if (!b || n < 0x40 || memcmp(b, "Phive\0", 6)) { free(b); snprintf(p->err, sizeof p->err, "Not a Phive file."); return 0; }
    int v2 = b[6] == 2;
    size_t start = v2 ? VoxRd32(b + 0x0C + 8 * 4) : 0x30;
    HkFile h;
    if (start >= n || !HkOpen(&h, b, n, start)) { HkFreeFile(&h); free(b); snprintf(p->err, sizeof p->err, "Couldn't read the Havok data in this file."); return 0; }
    int ok = 0; const char *why = "No collision mesh in this file.";
    PhSoup S; memset(&S, 0, sizeof S);
    int *tmat = NULL;                                // per-triangle material index, before narrowing to bytes
    if (!v2) {
        int rti = 0; size_t base = 0;
        for (size_t o = h.itemA; o + 12 <= h.itemE; o += 12) {
            int t = (int)(VoxRd32(b + o) & 0xFFFFFF);
            if (t && HkIsA(&h, t, "hknpShape")) { rti = t; base = h.d0 + VoxRd32(b + o + 4); break; }
        }
        if (rti) snprintf(p->shapeType, sizeof p->shapeType, "%s", HkName(&h, rti));
        int r = rti ? PhShapeAt(&h, rti, base, NULL, &S) : 0, oG;
        p->sections = 1;
        if (r > 0 && HkMember(&h, rti, "geometrySections", &oG, NULL) && base + oG + 8 <= n) p->sections = (int)VoxRd32(b + base + oG + 4);
        if (r < 0) why = "Out of memory.";
        else if (rti && !r) why = HkIsA(&h, rti, "hknpMeshShape") ? "The collision mesh's data doesn't fit in the file." : "This shape type isn't drawn yet.";
        if (S.nt) {
            // material table after the tagfile: (o1, size1) at 0x10 / 0x20 -> 16 bytes per material, two numbers
            uint32_t o1 = VoxRd32(b + 0x10), s1 = VoxRd32(b + 0x20); int nm = 0;
            for (uint32_t i = 0; i + 16 <= s1 && o1 + i + 16 <= n && nm < 254; i += 16, nm++)
                snprintf(p->matName[nm], VOX_MATN, "material %u / %u", VoxRd32(b + o1 + i), VoxRd32(b + o1 + i + 4));
            p->nmat = nm;
            tmat = (int *)malloc(S.nt * sizeof(int));
            if (tmat) for (size_t t = 0; t < S.nt; t++) tmat[t] = S.tag[t] < nm ? S.tag[t] : PhMat(p, "(no material)");
        }
    } else {
        // static compound: instances (64 bytes each) in a free-list rel array; per-instance material ranges before the tagfile
        int rt, cnt; size_t base; int oI;
        uint32_t off[12]; for (int k = 0; k < 12; k++) off[k] = VoxRd32(b + 0x0C + k * 4);
        if (HkItemAt(&h, 1, &rt, &base, &cnt) && HkIsA(&h, rt, "hknpCompoundShape") && HkMember(&h, rt, "instances", &oI, NULL) && base + oI + 16 <= n) {
            snprintf(p->shapeType, sizeof p->shapeType, "%s", HkName(&h, rt));
            size_t ia = base + oI; int64_t eoff = (int64_t)BfU64(b + ia);
            // numAllocated: the int after the elements view + firstFree
            uint32_t ni = (uint32_t)((off[5] - off[4]) / 20);
            size_t e0 = ia + eoff;
            if (e0 + (size_t)ni * 64 > n) ni = 0;
            // shapes are shared; remember type per address
            size_t cacheA[1024]; int cacheT[1024]; int nc = 0;
            size_t tcap = 0;
            int bad = 0, drawn = 0;
            for (uint32_t i = 0; i < ni; i++) {
                const unsigned char *q = b + e0 + (size_t)i * 64;
                float qx = BfFloat_(q), qy = BfFloat_(q + 4), qz = BfFloat_(q + 8), qw = BfFloat_(q + 12);
                float sx = BfFloat_(q + 32), sy = BfFloat_(q + 36), sz = BfFloat_(q + 40);
                int64_t sp = (int64_t)BfU64(q + 48); if (!sp) continue;
                size_t sa = e0 + (size_t)i * 64 + 48 + sp;
                int st = 0;
                for (int c = 0; c < nc; c++) if (cacheA[c] == sa) { st = cacheT[c]; break; }
                if (!st) { st = HkTypeAtAddr(&h, sa); if (nc < 1024) { cacheA[nc] = sa; cacheT[nc++] = st; } }
                if (!st) { bad++; continue; }
                float X[12] = {
                    (1 - 2 * (qy * qy + qz * qz)) * sx, 2 * (qx * qy - qz * qw) * sy, 2 * (qx * qz + qy * qw) * sz, BfFloat_(q + 16),
                    2 * (qx * qy + qz * qw) * sx, (1 - 2 * (qx * qx + qz * qz)) * sy, 2 * (qy * qz - qx * qw) * sz, BfFloat_(q + 20),
                    2 * (qx * qz - qy * qw) * sx, 2 * (qy * qz + qx * qw) * sy, (1 - 2 * (qx * qx + qy * qy)) * sz, BfFloat_(q + 24) };
                size_t t0 = S.nt;
                int r = PhShapeAt(&h, st, sa, X, &S);
                if (r < 0) { why = "Out of memory."; break; }
                if (!r) { bad++; continue; }
                drawn++;
                // material: sec4[i] = (idx, flags, a, b, matBase u16, 0, matCount u32) -> sec5[matBase + tag] = (x, y, ...)
                uint32_t mb = 0, mc = 0;
                size_t r4 = off[4] + (size_t)i * 20;
                if (r4 + 20 <= off[5] && VoxRd32(b + r4) == i) { mb = b[r4 + 12] | (b[r4 + 13] << 8); mc = VoxRd32(b + r4 + 16); }
                if (S.nt > tcap) { size_t c = tcap ? tcap : 4096; while (c < S.nt) c *= 2; int *m = (int *)realloc(tmat, c * sizeof(int)); if (!m) { why = "Out of memory."; break; } tmat = m; tcap = c; }
                for (size_t t = t0; t < S.nt; t++) {
                    unsigned g = S.tag[t]; char nm[40];
                    size_t me = off[5] + (size_t)(mb + g) * 16;
                    if (g != 0xFFFF && g < (mc ? mc : 1) && me + 16 <= off[6]) snprintf(nm, sizeof nm, "material %u / %u", VoxRd32(b + me), VoxRd32(b + me + 4));
                    else snprintf(nm, sizeof nm, "(no material)");
                    tmat[t] = PhMat(p, nm);
                }
            }
            p->sections = drawn;
            if (!S.nt && bad) why = "None of the compound's shapes could be decoded.";
        } else why = "The root of this file isn't a compound shape.";
    }
    if (S.nt && tmat) {
        p->tv = S.v; p->ntv = S.nv; p->ti = S.t; p->nti = S.nt; S.v = NULL; S.t = NULL;
        p->tmat = (uint16_t *)malloc((p->nti + 1) * sizeof(uint16_t));
        if (p->tmat) {
            for (size_t t = 0; t < p->nti; t++) { int m = tmat[t] < VOX_MAXMAT - 1 ? tmat[t] : VOX_MAXMAT - 2; p->tmat[t] = (uint16_t)m; p->matCount[m]++; if (m >= p->nmat) p->nmat = m + 1; }
            p->solid = p->nti; ok = 1;
        }
    }
    if (!ok) snprintf(p->err, sizeof p->err, "%s", why);
    free(S.v); free(S.t); free(S.tag); free(tmat);
    HkFreeFile(&h); free(b);
    return ok;
}

/* Mario Kart World's 62 terrain materials, in the order the .tscb and .mate number them: the list in
   System\GymlArchive\general_format.Product_100.sarc.zs\Gyml\TerrainMaterialSetGParam\Product_Proc (each one is
   TerrainMaterialGParam\CmnTex_Tera_Product_Proc_<name>). Area codes: CM common, WL wild/woods, DT desert, PL plains,
   RT reef, JG jungle, FS forest, VL volcano, IC ice/snow, OS ocean shore. */
static const char *const kTeraMatNames[62] = {
    "CM00RoadSoil", "CM00RoadSoilBright", "CM00RoadSand", "CM00RoadCenterRutSoil", "CM00RoadCenterRutSoilBright", "CM00RoadCenterRutSand",
    "CM00RoadCenterRutSnow", "CM00RoadWetSoil", "CM00RoadWetSoilBright", "CM00RoadWetSand", "CM00RoadWetSnow", "CM00RoadSideMaterialHardSoil",
    "CM00RoadSideMaterialSoftGrass", "CM00RoadSideMaterialSoftGrass", "CM00RoadSideMaterialHardSand", "CM00RoadSideMaterialHardSand_Road", "CM00RoadSideMaterialHardSandHighDirt", "CM00River",
    "CM00RiverGravel", "CM00RiverSoil", "CM00RiverSoilGravel", "CM00River_Road", "CM00Beach", "CM00BeachGravel",
    "CM00Beach_Road", "CM00BeachGravel_Road", "CliffRock", "CliffUnderRock", "CliffFS00Grass", "CM00Line",
    "CM00BaseGrass", "CM00BaseGrassHighDirt", "CM00Rock", "CM00Soil", "CM00Mud", "OS00Base",
    "WL00Base", "WL00Grass", "WL00RockSurface", "DT00Base", "DT00Road", "DT00Gravel",
    "DT00Riverside", "DT00RiversideGravel", "DT00BaseHighDirt", "PL00Field", "PL02Grass", "RT00ReefRock",
    "RT00ReefSand", "RT00Karst", "JG00RuinsUnderTree", "JG00DinoWoodChip", "FS00Moss", "FS00Mud",
    "VL00Base", "VL00Ash", "VL00ColdLava", "IC00Base", "IC00RoadForest", "IC00RoadGelaende",
    "IC00BaseHighDirt", "IC00Beach" };
// the same in plain English (the list's names; the game's name is the entry's tooltip). Guesses: "dirtier" for HighDirt,
// "off-road" for Gelaende (German: terrain), "Line" (CM00Line) unknown.
static const char *const kTeraMatPlain[62] = {
    "Dirt road", "Dirt road, sandy", "Sand road", "Dirt road, wheel ruts", "Dirt road, wheel ruts, sandy", "Sand road, wheel ruts",
    "Snow road, wheel ruts", "Wet dirt road", "Wet dirt road, sandy", "Wet sand road", "Wet snow road", "Roadside, hard dirt",
    "Roadside, soft grass", "Roadside, soft grass (2)", "Roadside, hard sand", "Roadside, hard sand (road)", "Roadside, hard sand, dirtier", "River bed",
    "River gravel", "River bank soil", "River bank soil and gravel", "River bed (road)", "Beach sand", "Beach gravel",
    "Beach sand (road)", "Beach gravel (road)", "Cliff rock", "Rock below cliffs", "Forest cliff grass", "Line",
    "Grass", "Grass, dirtier", "Rock", "Soil", "Mud", "Ocean shore ground",
    "Wilds ground", "Wilds grass", "Wilds rock surface", "Desert sand", "Desert road", "Desert gravel",
    "Desert riverside", "Desert riverside gravel", "Desert sand, dirtier", "Plains field", "Plains grass", "Reef rock",
    "Reef sand", "Reef limestone (karst)", "Jungle ruins, under trees", "Jungle wood chips (dinosaur area)", "Forest moss", "Forest mud",
    "Volcano ground", "Volcanic ash", "Cooled lava", "Snow", "Snowy forest road", "Snowy off-road track",
    "Snow, dirtier", "Snowy beach" };
// each material's layer in Tex\CmnTex_Tera_Product_Proc_Alb.bntx.zs (46 layers): the materials' TextureName
// (TerrainMaterialGParam; missing = the material's own name) in the order they first appear; an empty TextureName
// gets a layer of its own. Checked by eye: the "Bright" road layers are the sandy ones, 36 wood chips, 41 cold lava.
static const signed char kTeraMatLayer[62] = {
    0, 1, 0, 2, 3, 2, 2, 4, 5, 4, 4, 6, 7, 7, 6, 6, 6, 8, 9, 10, 11, 8, 12, 13, 12, 13, 14, 15, 16, 17, 18,
    18, 19, 20, 21, 22, 23, 24, 25, 22, 26, 27, 28, 29, 22, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42,
    43, 44, 42, 45 };
static float VoxTeraUVScale(unsigned k) { return k == 45 ? 0.1f : 1.0f; }   // PL00Field: UVScale 0.1 (10 x bigger)
static void VoxTerrainMatName(char *out, size_t n, unsigned k) {
    if (k < 62) snprintf(out, n, "%u %s", k, kTeraMatNames[k]); else snprintf(out, n, "ground material %u", k);   // (the game's names; kTeraMatPlain unused)
}
/* ---------------- Mario Kart World terrain tile (.hght "TARB"): a 257 x 257 height grid as a triangle mesh ----------------
   Area names 6N...: N = level (0 = whole world, size 64; each level halves). One square = size x 250 units; heights
   0-65535 = 0-2000 units. Colours: the tile's ground materials (the .mate beside it) if there, else height bands. */
static int VoxLoadTerrain(VoxPart *p, const VOXCHAR *path) {
    memset(p, 0, sizeof(*p));
    p->isTriMesh = 1; p->scale[0] = p->scale[1] = p->scale[2] = 1;
    size_t fn; unsigned char *f = VoxReadFile(path, &fn);
    if (!f) { snprintf(p->err, sizeof p->err, "The file couldn't be read from the dump."); return 0; }
    size_t n; unsigned char *d = TarbUnpack(f, fn, &n); free(f);
    uint16_t *h = (uint16_t *)malloc(257 * 257 * 2);
    if (!d || !h || !TarbHeights(d, n, h)) { free(d); free(h); snprintf(p->err, sizeof p->err, "Not a terrain height tile."); return 0; }
    free(d);
    const VOXCHAR *nm = path; for (const VOXCHAR *c = path; *c; c++) if (*c == '\\' || *c == '/') nm = c + 1;
    int lvl = (nm[0] == '6' && nm[1] >= '0' && nm[1] <= '9') ? nm[1] - '0' : 0;
    float size = 64.0f / (float)(1 << lvl) * 250.0f, step = size / 256.0f;
    // ground materials from the .mate beside it
    unsigned char *mat = NULL;
    {
        VOXCHAR mp[1100]; size_t L = 0; while (path[L] && L < 1090) { mp[L] = path[L]; L++; } mp[L] = 0;
        if (L > 5 && mp[L - 5] == '.' && (mp[L - 4] | 32) == 'h' && (mp[L - 3] | 32) == 'g') {
            mp[L - 4] = 'm'; mp[L - 3] = 'a'; mp[L - 2] = 't'; mp[L - 1] = 'e';
            size_t mn; unsigned char *mf = VoxReadFile(mp, &mn);
            if (mf) {
                size_t dn; unsigned char *md = TarbUnpack(mf, mn, &dn); free(mf);
                if (md && dn >= 0x34 + 257 * 4 && !memcmp(md + 4, "\x06\x06\x06\x06", 4)) {
                    unsigned char *lin = (unsigned char *)malloc(257 * 257 * 4);
                    if (lin) { TrUntile(md + 0x34, dn - 0x34, lin, 257, 257, 4); mat = (unsigned char *)malloc(257 * 257);
                        if (mat) for (int i = 0; i < 257 * 257; i++) mat[i] = lin[i * 4]; free(lin); }
                }
                free(md);
            }
        }
    }
    p->ntv = 257 * 257; p->nti = 256 * 256 * 2;
    p->tv = (float *)malloc(p->ntv * 3 * sizeof(float)); p->ti = (uint32_t *)malloc(p->nti * 3 * sizeof(uint32_t));
    p->tmat = (uint16_t *)malloc((p->nti + 1) * sizeof(uint16_t));
    if (!p->tv || !p->ti || !p->tmat) { free(h); free(mat); snprintf(p->err, sizeof p->err, "Out of memory."); return 0; }
    unsigned lo = 65535, hi = 0; for (int i = 0; i < 257 * 257; i++) { if (h[i] < lo) lo = h[i]; if (h[i] > hi) hi = h[i]; }
    for (int z = 0; z < 257; z++) for (int x = 0; x < 257; x++) {
        float *v = p->tv + ((size_t)z * 257 + x) * 3;
        v[0] = x * step; v[1] = h[z * 257 + x] / 65535.0f * 2000.0f; v[2] = z * step;
    }
    int idOf[256]; for (int i = 0; i < 256; i++) idOf[i] = -1;
    size_t t = 0;
    for (int z = 0; z < 256; z++) for (int x = 0; x < 256; x++) {
        uint32_t a = z * 257 + x, b = a + 1, c = a + 257, e = c + 1;
        int m;
        if (mat) { unsigned k = mat[a]; if (idOf[k] < 0) { char nmb[64]; VoxTerrainMatName(nmb, sizeof nmb, k); idOf[k] = PhMat(p, nmb); } m = idOf[k]; }
        else { unsigned band = hi > lo ? (h[a] - lo) * 10u / (hi - lo + 1) : 0; if (idOf[band] < 0) { char nmb[40]; snprintf(nmb, sizeof nmb, "height band %u (of 10)", band + 1); idOf[band] = PhMat(p, nmb); } m = idOf[band]; }
        p->ti[t * 3] = a; p->ti[t * 3 + 1] = c; p->ti[t * 3 + 2] = b; p->tmat[t++] = (uint16_t)m;
        p->ti[t * 3] = b; p->ti[t * 3 + 1] = c; p->ti[t * 3 + 2] = e; p->tmat[t++] = (uint16_t)m;
    }
    for (size_t k = 0; k < p->nti; k++) p->matCount[p->tmat[k]]++;
    p->solid = p->nti; p->sections = 1;
    snprintf(p->shapeType, sizeof p->shapeType, "terrain tile, %.0f units wide, heights %.1f-%.1f", size, lo / 65535.0 * 2000, hi / 65535.0 * 2000);
    free(h); free(mat);
    return 1;
}
/* ---------------- Mario Kart World: the whole terrain stitched together from the .tscb ----------------
   The .tscb lists every terrain square (a quad-tree: one size-64 square for the whole world, then 4 x 32, 16 x 16 ...).
   Each square's centre (x, z) and size are in world units / world_scale; its tiles sit in the folder named after the
   .tscb (Game03_Proc.tscb -> Game03_Proc\6100000000.hght ...). Every tile is 257 x 257 samples (rows = z, columns = x),
   edges shared with its neighbours. Squares are painted big to small onto one grid of grid+1 x grid+1 points, using
   only the levels whose samples are at least as far apart as the grid's (finer ones would add nothing at that size). */
static unsigned char *VoxTerrainMate(const VOXCHAR *hghtPath) {     // material number per sample from the .mate beside a .hght
    VOXCHAR mp[4096]; size_t L = 0; while (hghtPath[L] && L < 4090) { mp[L] = hghtPath[L]; L++; } mp[L] = 0;
    if (L < 5 || mp[L - 5] != '.' || (mp[L - 4] | 32) != 'h') return NULL;
    mp[L - 4] = 'm'; mp[L - 3] = 'a'; mp[L - 2] = 't'; mp[L - 1] = 'e';
    size_t mn; unsigned char *mf = VoxReadFile(mp, &mn), *mat = NULL;
    if (!mf) return NULL;
    size_t dn; unsigned char *md = TarbUnpack(mf, mn, &dn); free(mf);
    if (md && dn >= 0x34 + 257 * 4 && !memcmp(md + 4, "\x06\x06\x06\x06", 4)) {
        unsigned char *lin = (unsigned char *)malloc(257 * 257 * 4);
        if (lin) { TrUntile(md + 0x34, dn - 0x34, lin, 257, 257, 4); mat = (unsigned char *)malloc(257 * 257);
            if (mat) for (int i = 0; i < 257 * 257; i++) mat[i] = lin[i * 4]; free(lin); }
    }
    free(md); return mat;
}
/* area: rx, rz = its corner, rs = its size, all in .tscb units (rs <= 0: the whole world). Only squares overlapping the
   area are read; heights are sampled smoothly (bilinear) so a coarser square under a gap in the finer ones isn't blocky. */
// a tile's colour layer (<area>.<ext> beside its .hght, e.g. albedo_far.extm): 256 x 256 colours inside a 2-texel border
// (260 x 260 stored). Texel j's centre sits at (j + 0.5) / 256 across the tile. NULL if missing or not readable.
static unsigned char *VoxTerrainLayerRGBA(const VOXCHAR *hghtPath, const char *ext) {
    VOXCHAR ap[4096]; size_t L = 0; while (hghtPath[L] && L < 4070) { ap[L] = hghtPath[L]; L++; }
    if (L < 5) return NULL;
    L -= 4;                                                     // keep the "."
    for (const char *c = ext; *c; c++) ap[L++] = (VOXCHAR)*c; ap[L] = 0;
    size_t fn; unsigned char *f = VoxReadFile(ap, &fn); if (!f) return NULL;
    int side = 0; char desc[160], err[160];
    unsigned char *rgba = TarbToRGBA(f, fn, &side, desc, sizeof desc, err, sizeof err); free(f);
    if (rgba && (side != 260 || strncmp(desc, "colour", 6))) { free(rgba); rgba = NULL; }
    return rgba;
}
/* "Colour the ground by...": instead of the game's colours, one of the terrain's other layers, to see where its values
   are high or low. 0 = game colours. Set per loading thread (each 3D window loads on its own thread). */
enum { VOV_GAME, VOV_SKY_R, VOV_SKY_G, VOV_SKY_B, VOV_SKY_RGB, VOV_BAKE_GI, VOV_BAKE_LIGHT, VOV_H2, VOV_H3, VOV_SLOPE, VOV_COUNT };
static const char *kVovNames[VOV_COUNT] = { "Game colours", "sky_bounce red (sun shadow)", "sky_bounce green (night light, inverted)", "sky_bounce blue (occlusion)", "sky_bounce (all three)",
    "bake_gi (baked light)", "bake_light (baked light)", ".hght map 1+2 (fine height detail)", ".hght map 3 (height correction: red +, blue -)", "Steepness (from the normals)" };
static _Thread_local int g_voxOverlay;
// value 0-255 -> blue, cyan, green, yellow, red
static void VoxHeat(unsigned v, unsigned char *o) {
    float t = v / 255.0f, r, g, b;
    if (t < 0.25f) { r = 0; g = t * 4; b = 1; } else if (t < 0.5f) { r = 0; g = 1; b = 1 - (t - 0.25f) * 4; }
    else if (t < 0.75f) { r = (t - 0.5f) * 4; g = 1; b = 0; } else { r = 1; g = 1 - (t - 0.75f) * 4; b = 0; }
    o[0] = (unsigned char)(r * 255); o[1] = (unsigned char)(g * 255); o[2] = (unsigned char)(b * 255); o[3] = 255;
}
static unsigned char *VoxTerrainOverlay(const VOXCHAR *hghtPath, int mode) {
    VOXCHAR ap[4096]; size_t L = 0; while (hghtPath[L] && L < 4070) { ap[L] = hghtPath[L]; L++; }
    if (L < 5) return NULL;
    const char *ext = mode >= VOV_SKY_R && mode <= VOV_SKY_RGB ? "sky_bounce.extm" : mode == VOV_BAKE_GI ? "bake_gi.extm" :
                      mode == VOV_BAKE_LIGHT ? "bake_light.extm" : mode == VOV_SLOPE ? "norm" : "hght";
    size_t k = L - 4; for (const char *c = ext; *c; c++) ap[k++] = (VOXCHAR)*c; ap[k] = 0;
    size_t fn; unsigned char *f = VoxReadFile(ap, &fn); if (!f) return NULL;
    unsigned char *out = (unsigned char *)malloc(260 * 260 * 4);
    if (!out) { free(f); return NULL; }
    if (mode == VOV_H2 || mode == VOV_H3) {                 // .hght after 0x34: two 4-bit planes (rows of 130 bytes) = map 1+2, then 260 x 260 bytes = map 3
        size_t dn; unsigned char *d = TarbUnpack(f, fn, &dn); free(f);
        if (!d || dn < 0x34 + 4 * 130 * 260) { free(d); free(out); return NULL; }
        const unsigned char *m = d + 0x34; const size_t P = 130 * 260;
        for (int i = 0; i < 260 * 260; i++) {
            #define NIB(pl) ((m[(pl) * P + i / 2] >> ((i & 1) * 4)) & 15)
            unsigned v = mode == VOV_H2 ? NIB(1) * 16 + NIB(0) : (i % 260 == 0 || i % 260 >= 257) ? 0 : m[2 * P + i];   // (mask: edge columns are padding)
            #undef NIB
            if (mode == VOV_H3) {                            // signed (-128..127): grey 0, red positive, blue negative (full at +/-12)
                int sv = (int)(signed char)(unsigned char)v; float t = (sv < 0 ? -sv : sv) / 12.0f; if (t > 1) t = 1;
                unsigned char *o = out + i * 4; o[3] = 255;
                o[0] = (unsigned char)(70 + (sv > 0 ? 185 : -70) * t); o[1] = (unsigned char)(70 - 70 * t); o[2] = (unsigned char)(70 + (sv < 0 ? 185 : -70) * t);
            }
            else VoxHeat(v, out + i * 4);
        }
        free(d); return out;
    }
    int side = 0; char desc[160], err[160];
    unsigned char *rgba = TarbToRGBA(f, fn, &side, desc, sizeof desc, err, sizeof err); free(f);
    if (!rgba || (side != 257 && side != 260 && side != 520)) { free(rgba); free(out); return NULL; }
    for (int j = 0; j < 260; j++) for (int i = 0; i < 260; i++) {
        unsigned char px[4];
        if (side == 520) {                                   // 2 x 2 average (same border layout at twice the detail)
            for (int c = 0; c < 4; c++) { const unsigned char *q = rgba + ((size_t)(j * 2) * 520 + i * 2) * 4 + c;
                px[c] = (unsigned char)((q[0] + q[4] + q[520 * 4] + q[520 * 4 + 4] + 2) / 4); }
        } else if (side == 257) {                            // texel j's centre (j - 1.5) of 256
            int si = i - 2 < 0 ? 0 : i - 2 > 256 ? 256 : i - 2, sj = j - 2 < 0 ? 0 : j - 2 > 256 ? 256 : j - 2;
            memcpy(px, rgba + ((size_t)sj * 257 + si) * 4, 4);
        } else memcpy(px, rgba + ((size_t)j * 260 + i) * 4, 4);
        unsigned char *o = out + ((size_t)j * 260 + i) * 4;
        if (mode == VOV_SKY_R) VoxHeat(px[0], o);
        else if (mode == VOV_SKY_G) VoxHeat(px[1], o);
        else if (mode == VOV_SKY_B) VoxHeat(px[2], o);
        else if (mode == VOV_SLOPE) {                        // normals picture: green = up (128-255)
            unsigned up = px[1] < 128 ? 0 : (px[1] - 128) * 2; VoxHeat(255 - (up > 255 ? 255 : up), o);
        } else {                                             // colours (bake_gi is dim: x4)
            int g = mode == VOV_BAKE_GI ? 4 : 1;
            for (int c = 0; c < 3; c++) { int q = px[c] * g; o[c] = (unsigned char)(q > 255 ? 255 : q); } o[3] = 255;
        }
    }
    free(rgba); return out;
}
// the tile's colour: its color_mul2 layer (on every tile size down to the smallest, about 9% brighter than
// albedo_far), else albedo_far brightened to match. Same 260 x 260 layout.
static unsigned char *VoxTerrainAlbedo(const VOXCHAR *hghtPath) {
    if (g_voxOverlay > 0 && g_voxOverlay < VOV_COUNT) return VoxTerrainOverlay(hghtPath, g_voxOverlay);
    unsigned char *c = VoxTerrainLayerRGBA(hghtPath, "color_mul2.extm");
    if (c) return c;
    c = VoxTerrainLayerRGBA(hghtPath, "albedo_far.extm");
    if (c) for (size_t i = 0; i < 260 * 260; i++) for (int k = 0; k < 3; k++) { unsigned v = c[i * 4 + k] * 109u / 100u; c[i * 4 + k] = (unsigned char)(v > 255 ? 255 : v); }
    return c;
}
static int VoxLoadTerrainRegion(VoxPart *p, const VOXCHAR *tscbPath, int grid, float rx, float rz, float rs) {
    memset(p, 0, sizeof(*p));
    p->isTriMesh = 1; p->scale[0] = p->scale[1] = p->scale[2] = 1;
    size_t n; unsigned char *b = VoxReadFile(tscbPath, &n);
    if (!b) { snprintf(p->err, sizeof p->err, "The .tscb file couldn't be read from the dump."); return 0; }
    if (n < 0x80 || memcmp(b, "TSCB", 4)) { free(b); snprintf(p->err, sizeof p->err, "Not a TSCB terrain scene."); return 0; }
    float ws, mh; memcpy(&ws, b + 0x10, 4); memcpy(&mh, b + 0x14, 4);
    if (!(ws > 0 && ws < 1e6f)) ws = 250; if (!(mh > 0 && mh < 1e6f)) mh = 2000;
    uint32_t nAreas = TrRd32(b + 0x38); if (nAreas > 200000) nAreas = 200000;
    size_t A = 0x78 + (size_t)TrRd32(b + 0x78);
    typedef struct { float x, z, s; char name[24]; } TwArea;
    TwArea *ar = (TwArea *)calloc(nAreas + 1, sizeof(TwArea)); int na = 0;
    float big = 0, wx0 = 0, wz0 = 0;
    for (uint32_t i = 0; ar && i < nAreas; i++) {
        if (A + 4 * i + 4 > n) break;
        size_t o = A + 4 * i + TrRd32(b + A + 4 * i);
        if (o + 0x64 > n) break;
        TwArea *t = &ar[na]; memcpy(&t->x, b + o, 4); memcpy(&t->z, b + o + 4, 4); memcpy(&t->s, b + o + 8, 4);
        size_t no = o + 0x60 + TrRd32(b + o + 0x60); int k = 0;
        while (no + k < n && b[no + k] && k < 23) { t->name[k] = (char)b[no + k]; k++; } t->name[k] = 0;
        if (!(t->s > 0) || !k) continue;
        if (t->s > big) { big = t->s; wx0 = t->x - t->s / 2; wz0 = t->z - t->s / 2; }
        na++;
    }
    free(b);
    if (!na) { free(ar); snprintf(p->err, sizeof p->err, "The .tscb lists no terrain squares."); return 0; }
    int whole = !(rs > 0);
    if (whole) { rx = wx0; rz = wz0; rs = big; }
    {   // Mario Kart World's scene (Game03_Proc.tscb) uses the game's material names; any other is "generic"
        size_t e = 0; while (tscbPath[e]) e++; size_t bgn = e; while (bgn > 0 && tscbPath[bgn - 1] != '\\' && tscbPath[bgn - 1] != '/') bgn--;
        const char *mk = "game03_proc.tscb"; int same = e - bgn == strlen(mk);
        for (size_t i = 0; same && i < e - bgn; i++) { int ch = (int)tscbPath[bgn + i]; if (ch >= 'A' && ch <= 'Z') ch += 32; if (ch != mk[i]) same = 0; }
        p->terrGeneric = !same;
    }
    // the tiles' folder: the .tscb's path without ".tscb"
    VOXCHAR dir[4096]; size_t L = 0; while (tscbPath[L] && L < 4000) { dir[L] = tscbPath[L]; L++; } dir[L] = 0;
    if (L > 5 && dir[L - 5] == '.') { L -= 5; dir[L] = 0; }
    int G = grid, G1 = grid + 1;
    uint16_t *H = (uint16_t *)calloc((size_t)G1 * G1, 2); unsigned char *M = (unsigned char *)calloc((size_t)G1 * G1, 1);
    uint16_t *h = (uint16_t *)malloc(257 * 257 * 2);
    unsigned char *C = (unsigned char *)calloc((size_t)G1 * G1, 3); int haveCol = 0;     // the game's colours, if found
    float *WH = (float *)malloc((size_t)G1 * G1 * sizeof(float)); unsigned char *WK = (unsigned char *)calloc((size_t)G1 * G1, 1);
    int haveWater = 0;                                                                  // water / lava surface height and kind
    if (WH) for (size_t i = 0; i < (size_t)G1 * G1; i++) WH[i] = -1;
    unsigned char *wcell = (unsigned char *)malloc(68 * 68 * 8);
    if (!C) { free(ar); free(H); free(M); free(h); snprintf(p->err, sizeof p->err, "Out of memory."); return 0; }
    if (!H || !M || !h) { free(C); free(ar); free(H); free(M); free(h); snprintf(p->err, sizeof p->err, "Out of memory."); return 0; }
    int haveMat = 0, painted = 0, missing = 0;
    float minS = rs * 256.0f / G * 0.999f;          // smallest square whose sample spacing still reaches the grid's
    for (float lv = big; lv >= minS; lv /= 2) {      // big squares first, smaller ones paint over them
        for (int i = 0; i < na; i++) {
            TwArea *t = &ar[i];
            if (fabsf(t->s - lv) > lv * 1e-3f) continue;
            float tx0 = t->x - t->s / 2, tz0 = t->z - t->s / 2;
            if (tx0 >= rx + rs || tz0 >= rz + rs || tx0 + t->s <= rx || tz0 + t->s <= rz) continue;   // outside the area
            // grid points inside this square
            float gpu = G / rs;                                             // grid points per .tscb unit
            int X0 = (int)ceilf((tx0 - rx) * gpu - 1e-3f), X1 = (int)floorf((tx0 + t->s - rx) * gpu + 1e-3f);
            int Z0 = (int)ceilf((tz0 - rz) * gpu - 1e-3f), Z1 = (int)floorf((tz0 + t->s - rz) * gpu + 1e-3f);
            if (X0 < 0) X0 = 0; if (Z0 < 0) Z0 = 0; if (X1 > G) X1 = G; if (Z1 > G) Z1 = G;
            if (X0 > X1 || Z0 > Z1) continue;
            VOXCHAR fp[4096]; size_t q = 0;
            for (; q < L; q++) fp[q] = dir[q];
            fp[q++] = VOXSEP[0];
            for (const char *c = t->name; *c && q < 4080; c++) fp[q++] = (VOXCHAR)*c;
            const char *ext = ".hght"; for (const char *c = ext; *c; c++) fp[q++] = (VOXCHAR)*c; fp[q] = 0;
            size_t fn; unsigned char *f = VoxReadFile(fp, &fn);
            size_t dn = 0; unsigned char *d = f ? TarbUnpack(f, fn, &dn) : NULL; free(f);
            if (!d || !TarbHeights(d, dn, h)) { free(d); missing++; continue; }
            free(d);
            unsigned char *mat = VoxTerrainMate(fp); if (mat) haveMat = 1;
            unsigned char *alb = VoxTerrainAlbedo(fp); if (alb) haveCol = 1;
            int wat = 0;                                                    // its water layer, if it has one
            if (WH && WK && wcell) {
                VOXCHAR wp[4096]; size_t wl = 0; while (fp[wl] && wl < 4070) { wp[wl] = fp[wl]; wl++; }
                wl -= 4; const char *we = "water.extm"; for (const char *c = we; *c; c++) wp[wl++] = (VOXCHAR)*c; wp[wl] = 0;
                size_t wn; unsigned char *wf = VoxReadFile(wp, &wn);
                if (wf) { size_t wdn; unsigned char *wd = TarbUnpack(wf, wn, &wdn); free(wf); if (wd && TarbWater(wd, wdn, wcell)) wat = 1; free(wd); }
                if (wat) for (int ci = 0; ci < 68 * 68; ci++) if (wcell[(size_t)ci * 8 + 6]) p->waterKindN[wcell[(size_t)ci * 8 + 7]]++;   // (what kinds there are)
            }   // (a tile without one keeps the bigger tile's colour)
            float spu = 256.0f / t->s;                                      // tile samples per .tscb unit
            for (int Z = Z0; Z <= Z1; Z++) {
                float fz = (rz + Z / gpu - tz0) * spu; if (fz < 0) fz = 0; if (fz > 256) fz = 256;
                int iz = (int)fz; if (iz > 255) iz = 255; float az = fz - iz;
                for (int X = X0; X <= X1; X++) {
                    float fx = (rx + X / gpu - tx0) * spu; if (fx < 0) fx = 0; if (fx > 256) fx = 256;
                    int ix = (int)fx; if (ix > 255) ix = 255; float ax = fx - ix;
                    const uint16_t *r0 = h + iz * 257 + ix, *r1 = r0 + 257;
                    float hv = (r0[0] * (1 - ax) + r0[1] * ax) * (1 - az) + (r1[0] * (1 - ax) + r1[1] * ax) * az;
                    H[(size_t)Z * G1 + X] = (uint16_t)(hv + 0.5f);
                    if (mat) M[(size_t)Z * G1 + X] = mat[(int)(fz + 0.5f) * 257 + (int)(fx + 0.5f)];
                    if (wat) {                                  // water: 64 cells (stored from 2), centres at (j + 0.5) / 64; bilinear over real cells
                        float wx = 1.5f + fx / 4.0f, wz = 1.5f + fz / 4.0f;          // fx, fz run 0-256 across the tile
                        int ix2 = (int)wx, iz2 = (int)wz; if (ix2 > 66) ix2 = 66; if (iz2 > 66) iz2 = 66;
                        float ax2 = wx - ix2, az2 = wz - iz2, sum = 0, wsum = 0; int kind = 0;
                        for (int dz = 0; dz < 2; dz++) for (int dx = 0; dx < 2; dx++) {
                            const unsigned char *q = wcell + ((size_t)(iz2 + dz) * 68 + ix2 + dx) * 8;
                            if (!q[6]) continue;
                            float wgt = (dx ? ax2 : 1 - ax2) * (dz ? az2 : 1 - az2) + 1e-6f;
                            sum += (q[0] | (q[1] << 8)) * wgt; wsum += wgt;
                            if (wgt > 0.25f) kind = q[7];
                        }
                        if (wsum > 0) { WH[(size_t)Z * G1 + X] = sum / wsum; WK[(size_t)Z * G1 + X] = (unsigned char)kind; haveWater = 1; }
                    }
                    if (alb) {                                  // colour: bilinear between texel centres
                        float ux = 1.5f + fx, uz = 1.5f + fz;          // fx, fz run 0-256 across the tile
                        int jx = (int)ux, jz = (int)uz; if (jx > 258) jx = 258; if (jz > 258) jz = 258;
                        float bx = ux - jx, bz = uz - jz;
                        const unsigned char *c0 = alb + ((size_t)jz * 260 + jx) * 4, *c1 = c0 + 260 * 4;
                        unsigned char *o = C + ((size_t)Z * G1 + X) * 3;
                        for (int k = 0; k < 3; k++) o[k] = (unsigned char)((c0[k] * (1 - bx) + c0[4 + k] * bx) * (1 - bz) + (c1[k] * (1 - bx) + c1[4 + k] * bx) * bz + 0.5f);
                    }
                }
            }
            free(mat); free(alb); painted++;
        }
    }
    free(ar); free(h);
    if (!haveCol) { free(C); C = NULL; }
    if (!painted) { free(WH); free(WK); free(wcell); free(H); free(M); free(C); snprintf(p->err, sizeof p->err, "None of the terrain tiles listed in the .tscb were found beside it."); return 0; }
    p->ntv = (size_t)G1 * G1; p->nti = (size_t)G * G * 2;
    p->tv = (float *)malloc(p->ntv * 3 * sizeof(float)); p->ti = (uint32_t *)malloc(p->nti * 3 * sizeof(uint32_t));
    p->tmat = (uint16_t *)malloc((p->nti + 1) * sizeof(uint16_t));
    if (!p->tv || !p->ti || !p->tmat) { free(WH); free(WK); free(wcell); free(H); free(M); free(C); snprintf(p->err, sizeof p->err, "Out of memory."); return 0; }
    p->gcol = C;
    p->overlay = g_voxOverlay > 0 && g_voxOverlay < VOV_COUNT ? g_voxOverlay : 0;
    float step = rs * ws / G, ox = (rx - wx0) * ws, oz = (rz - wz0) * ws;
    unsigned lo = 65535, hi = 0; for (size_t i = 0; i < p->ntv; i++) { if (H[i] < lo) lo = H[i]; if (H[i] > hi) hi = H[i]; }
    for (int z = 0; z < G1; z++) for (int x = 0; x < G1; x++) {
        float *v = p->tv + ((size_t)z * G1 + x) * 3;
        v[0] = ox + x * step; v[1] = H[(size_t)z * G1 + x] / 65535.0f * mh; v[2] = oz + z * step;
    }
    int idOf[256]; for (int i = 0; i < 256; i++) idOf[i] = -1;
    for (int i = 0; i < VOX_MAXMAT; i++) p->terrMat[i] = -1;
    size_t t = 0;
    for (int z = 0; z < G; z++) for (int x = 0; x < G; x++) {
        uint32_t a = (uint32_t)(z * G1 + x), bb = a + 1, c = a + G1, e = c + 1;
        int m;
        if (haveMat) { unsigned k = M[a]; if (idOf[k] < 0) { char nmb[64]; if (p->terrGeneric) snprintf(nmb, sizeof nmb, "ground material %u", k); else VoxTerrainMatName(nmb, sizeof nmb, k); idOf[k] = PhMat(p, nmb); if (idOf[k] >= 0 && idOf[k] < VOX_MAXMAT) p->terrMat[idOf[k]] = (int)k; } m = idOf[k]; }
        else { unsigned band = hi > lo ? (H[a] - lo) * 10u / (hi - lo + 1) : 0; if (idOf[band] < 0) { char nmb[40]; snprintf(nmb, sizeof nmb, "height band %u (of 10)", band + 1); idOf[band] = PhMat(p, nmb); } m = idOf[band]; }
        if (m < 0) m = 0;
        p->ti[t * 3] = a; p->ti[t * 3 + 1] = c; p->ti[t * 3 + 2] = bb; p->tmat[t++] = (uint16_t)m;
        p->ti[t * 3] = bb; p->ti[t * 3 + 1] = c; p->ti[t * 3 + 2] = e; p->tmat[t++] = (uint16_t)m;
    }
    // water and lava: flat surfaces (a coarser grid) on the cells where the water sits above the ground; the ground
    // cuts the shore line where they meet. Each kind is its own entry in the list, so it can be hidden.
    size_t nWaterTri = 0;
    if (haveWater) {
        int cs = G / 512 > 1 ? G / 512 : 1, CG = G / cs, CG1 = CG + 1;   // one water cell per cs x cs ground cells
        unsigned char *use = (unsigned char *)calloc((size_t)CG * CG, 1);
        float *cw = (float *)malloc((size_t)CG1 * CG1 * sizeof(float)); unsigned char *ck = (unsigned char *)calloc((size_t)CG1 * CG1, 1);
        size_t ncell = 0;
        if (use && cw && ck) {
            for (int z = 0; z < CG1; z++) for (int x = 0; x < CG1; x++) {          // water height at the coarse corners
                size_t i = (size_t)(z * cs) * G1 + x * cs; cw[(size_t)z * CG1 + x] = WH[i]; ck[(size_t)z * CG1 + x] = WK[i];
            }
            for (int z = 0; z < CG; z++) for (int x = 0; x < CG; x++) {           // a cell is used if water is above ground anywhere in it
                int any = 0;
                for (int zz = 0; zz <= cs && !any; zz++) for (int xx = 0; xx <= cs && !any; xx++) {
                    size_t i = (size_t)(z * cs + zz) * G1 + x * cs + xx;
                    if (WH[i] >= 0 && WH[i] > H[i] + 1) any = 1;
                }
                if (!any) continue;
                use[(size_t)z * CG + x] = 1; ncell++;
                float best = -1;                                                     // corners without water: the cell's highest water
                for (int k = 0; k < 4; k++) { float wv = cw[(size_t)(z + k / 2) * CG1 + x + k % 2]; if (wv > best) best = wv; }
                for (int k = 0; k < 4; k++) { size_t ci = (size_t)(z + k / 2) * CG1 + x + k % 2; if (cw[ci] < 0) cw[ci] = best; }
            }
        }
        if (ncell) {
            size_t nv0 = p->ntv, nt0 = p->nti;
            float *ntv = (float *)realloc(p->tv, (nv0 + ncell * 4) * 3 * sizeof(float));
            if (ntv) p->tv = ntv;
            uint32_t *nti = ntv ? (uint32_t *)realloc(p->ti, (nt0 + ncell * 2) * 3 * sizeof(uint32_t)) : NULL;
            if (nti) p->ti = nti;
            uint16_t *ntm = nti ? (uint16_t *)realloc(p->tmat, (nt0 + ncell * 2 + 1) * sizeof(uint16_t)) : NULL;
            if (ntm) p->tmat = ntm;
            if (ntv && nti && ntm) {
                static const char *wname[4] = { "Water", "Muddy water", "Lava", "Ghost swamp water" };
                int wid[256]; for (int k = 0; k < 256; k++) wid[k] = -1;
                size_t nv = nv0, nt = nt0;
                for (int z = 0; z < CG; z++) for (int x = 0; x < CG; x++) {
                    if (!use[(size_t)z * CG + x]) continue;
                    unsigned kind = ck[(size_t)z * CG1 + x];                 // (the raw byte: values past 3 get entries of their own)
                    if (wid[kind] < 0) { char wn2[32]; if (kind < 4) snprintf(wn2, sizeof wn2, "%s", wname[kind]); else snprintf(wn2, sizeof wn2, "Water kind %u", kind); wid[kind] = PhMat(p, wn2); }
                    for (int k = 0; k < 4; k++) {
                        int vx = x + k % 2, vz = z + k / 2;
                        float *v = p->tv + (nv + k) * 3;
                        v[0] = ox + vx * cs * step; v[1] = cw[(size_t)vz * CG1 + vx] / 65535.0f * mh; v[2] = oz + vz * cs * step;
                    }
                    uint32_t a = (uint32_t)nv, bb = a + 1, c = a + 2, e = a + 3;
                    p->ti[nt * 3] = a; p->ti[nt * 3 + 1] = c; p->ti[nt * 3 + 2] = bb; p->tmat[nt++] = (uint16_t)wid[kind];
                    p->ti[nt * 3] = bb; p->ti[nt * 3 + 1] = c; p->ti[nt * 3 + 2] = e; p->tmat[nt++] = (uint16_t)wid[kind];
                    nv += 4;
                }
                p->ntv = nv; p->nti = nt; nWaterTri = nt - nt0;
                // texture coordinates for the water's ripple texture: world-based, one repeat per TERA_WATER_REPEAT units
                float *tu = (float *)calloc((p->ntv + 1) * 2, sizeof(float));
                if (tu) { for (size_t i = nv0; i < p->ntv; i++) { tu[i * 2] = p->tv[i * 3] / TERA_WATER_REPEAT; tu[i * 2 + 1] = p->tv[i * 3 + 2] / TERA_WATER_REPEAT; }
                          free(p->tuv); p->tuv = tu; }
            }
        }
        free(use); free(cw); free(ck);
    }
    free(WH); free(WK); free(wcell);
    for (size_t k = 0; k < p->nti; k++) p->matCount[p->tmat[k]]++;
    p->solid = p->nti; p->sections = painted; p->gridW = G1;
    p->terrWorld[0] = wx0; p->terrWorld[1] = wz0; p->terrWorld[2] = ws; p->terrWorld[3] = big;
    p->terrArea[0] = rx; p->terrArea[1] = rz; p->terrArea[2] = whole ? 0 : rs;
    snprintf(p->shapeType, sizeof p->shapeType, "terrain %s, %d tile%s stitched%s, %.0f units wide, a point every %.2f, heights %.1f-%.1f",
             whole ? "world" : "area", painted, painted == 1 ? "" : "s", missing ? " (some missing)" : "", rs * ws, step, lo / 65535.0 * mh, hi / 65535.0 * mh);
    (void)nWaterTri;
    free(H); free(M);
    return 1;
}
static int VoxLoadTerrainWorld(VoxPart *p, const VOXCHAR *tscbPath, int grid) { return VoxLoadTerrainRegion(p, tscbPath, grid, 0, 0, 0); }
// flat-shaded mesh from the triangle list (each triangle gets its own 3 vertices so its normal is sharp)
static void VoxMatColour(const VoxPart *p, int id, unsigned char out[3]);
static int VoxBuildTriMesh(const VoxPart *p, VoxMesh *m, const unsigned char *hidden) {
    memset(m, 0, sizeof(*m));
    if (p->gridW > 1 && p->ntv >= (size_t)p->gridW * p->gridW) {     // height grid: one vertex per point, smooth normals (+ water quads after it)
        size_t G1 = (size_t)p->gridW, keep = 0;
        for (size_t t = 0; t < p->nti; t++) if (!hidden || !hidden[p->tmat[t]]) keep++;
        m->pos = (float *)malloc(p->ntv * 3 * sizeof(float)); m->nrm = (signed char *)malloc(p->ntv * 4);
        m->col = (unsigned char *)malloc(p->ntv * 4); m->idx = (uint32_t *)malloc((keep * 3 + 1) * sizeof(uint32_t));
        if (!m->pos || !m->nrm || !m->col || !m->idx) { VoxFreeMesh(m); return 0; }
        memcpy(m->pos, p->tv, p->ntv * 3 * sizeof(float)); m->nv = p->ntv;
        float st = p->tv[3] - p->tv[0]; if (!(st > 0)) st = 1;
        for (size_t z = 0; z < G1; z++) for (size_t x = 0; x < G1; x++) {
            size_t i = z * G1 + x;
            float hl = p->tv[(z * G1 + (x ? x - 1 : x)) * 3 + 1], hr = p->tv[(z * G1 + (x + 1 < G1 ? x + 1 : x)) * 3 + 1];
            float hd = p->tv[((z ? z - 1 : z) * G1 + x) * 3 + 1], hu = p->tv[((z + 1 < G1 ? z + 1 : z) * G1 + x) * 3 + 1];
            float dx = (x && x + 1 < G1) ? 2 * st : st, dz = (z && z + 1 < G1) ? 2 * st : st;
            float nx = -(hr - hl) / dx, ny = 1, nz = -(hu - hd) / dz, l = sqrtf(nx * nx + ny * ny + nz * nz);
            m->nrm[i * 4] = (signed char)(nx / l * 127); m->nrm[i * 4 + 1] = (signed char)(ny / l * 127); m->nrm[i * 4 + 2] = (signed char)(nz / l * 127); m->nrm[i * 4 + 3] = 0;
            size_t cz = z < G1 - 1 ? z : z - 1, cx = x < G1 - 1 ? x : x - 1;          // the cell this point starts
            unsigned mat = p->tmat[(cz * (G1 - 1) + cx) * 2];
            unsigned char rgb[3];
            if (p->gcol) memcpy(rgb, p->gcol + i * 3, 3); else VoxMatColour(p, (int)mat, rgb);
            m->col[i * 4] = rgb[0]; m->col[i * 4 + 1] = rgb[1]; m->col[i * 4 + 2] = rgb[2]; m->col[i * 4 + 3] = (unsigned char)mat;
        }
        if (p->ntv > G1 * G1) {                            // water / lava quads after the grid: flat, coloured by kind
            size_t ne = p->ntv - G1 * G1;                  // normals: summed from the triangles (flat water stays straight up)
            float *acc = (float *)calloc(ne * 3 + 3, sizeof(float));
            for (size_t t = (G1 - 1) * (G1 - 1) * 2; acc && t < p->nti; t++) {
                const uint32_t *q = p->ti + t * 3; const float *A = p->tv + q[0] * 3, *B = p->tv + q[1] * 3, *C = p->tv + q[2] * 3;
                float e1[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] }, e2[3] = { C[0] - A[0], C[1] - A[1], C[2] - A[2] };
                float n3[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
                for (int c = 0; c < 3; c++) if (q[c] >= G1 * G1) for (int k = 0; k < 3; k++) acc[(q[c] - G1 * G1) * 3 + k] += n3[k];
            }
            for (size_t i = G1 * G1; i < p->ntv; i++) {
                float *a3 = acc ? acc + (i - G1 * G1) * 3 : NULL, l = a3 ? sqrtf(a3[0] * a3[0] + a3[1] * a3[1] + a3[2] * a3[2]) : 0;
                if (l > 0) { m->nrm[i * 4] = (signed char)(a3[0] / l * 127); m->nrm[i * 4 + 1] = (signed char)(a3[1] / l * 127); m->nrm[i * 4 + 2] = (signed char)(a3[2] / l * 127); }
                else { m->nrm[i * 4] = 0; m->nrm[i * 4 + 1] = 127; m->nrm[i * 4 + 2] = 0; }
                m->nrm[i * 4 + 3] = 0; m->col[i * 4] = m->col[i * 4 + 1] = m->col[i * 4 + 2] = 128; m->col[i * 4 + 3] = 0;
            }
            free(acc);
            for (size_t t = (G1 - 1) * (G1 - 1) * 2; t < p->nti; t++) {
                unsigned mat = p->tmat[t]; const char *nm = mat < (unsigned)p->nmat ? p->matName[mat] : "";
                unsigned char rgb[3] = { 40, 110, 190 };                                         // water
                if (strstr(nm, "WaterSurface")) { }                                                  // the game's water surface models: water
                else if (!strncmp(nm, "Muddy", 5)) { rgb[0] = 105; rgb[1] = 85; rgb[2] = 55; }
                else if (!strncmp(nm, "Lava", 4)) { rgb[0] = 240; rgb[1] = 90; rgb[2] = 25; }
                else if (!strncmp(nm, "Ghost", 5)) { rgb[0] = 95; rgb[1] = 70; rgb[2] = 120; }
                else if (!strncmp(nm, "Chocolate", 9)) { rgb[0] = 95; rgb[1] = 55; rgb[2] = 30; }
                else if (!strncmp(nm, "Aurora", 6)) { rgb[0] = 150; rgb[1] = 220; rgb[2] = 210; }
                else if (!strncmp(nm, "Cliff: CliffIC", 14)) { rgb[0] = 205; rgb[1] = 228; rgb[2] = 242; }   // (by the game's area code)
                else if (!strncmp(nm, "Iceberg:", 8)) { rgb[0] = 225; rgb[1] = 240; rgb[2] = 250; }
                else if (!strncmp(nm, "Cliff: CliffVL", 14)) { rgb[0] = 70; rgb[1] = 62; rgb[2] = 60; }
                else if (!strncmp(nm, "Cliff: CliffDT", 14)) { rgb[0] = 196; rgb[1] = 140; rgb[2] = 88; }
                else if (!strncmp(nm, "Cliff: CliffRT", 14)) { rgb[0] = 176; rgb[1] = 168; rgb[2] = 150; }
                else if (!strncmp(nm, "Cliff: CliffJG", 14)) { rgb[0] = 110; rgb[1] = 104; rgb[2] = 84; }
                else if (!strncmp(nm, "Cliff: CliffFS", 14)) { rgb[0] = 104; rgb[1] = 108; rgb[2] = 92; }
                else if (!strncmp(nm, "Cliff:", 6)) { rgb[0] = 140; rgb[1] = 128; rgb[2] = 115; }
                else if (!strncmp(nm, "Scenery", 7)) { rgb[0] = 150; rgb[1] = 150; rgb[2] = 145; }
                else if (!strncmp(nm, "Stage:", 6)) { rgb[0] = 178; rgb[1] = 172; rgb[2] = 166; }
                if (p->ttex && p->ttex[t] != 0xFFFF) { rgb[0] = rgb[1] = rgb[2] = 255; }      // textured: the texture gives the colour
                for (int k = 0; k < 3; k++) { uint32_t vi = p->ti[t * 3 + k]; m->col[vi * 4] = rgb[0]; m->col[vi * 4 + 1] = rgb[1]; m->col[vi * 4 + 2] = rgb[2]; m->col[vi * 4 + 3] = (unsigned char)mat; }
            }
        }
        int haveTerr = 0; for (int k = 0; k < p->nmat && k < VOX_MAXMAT; k++) if (p->terrMat[k] >= 0) haveTerr = 1;
        if (haveTerr && p->terrWorld[2] > 0) {             // ground textures: world-based texture coordinates, triangles grouped by material
            m->uv = (float *)malloc(p->ntv * 2 * sizeof(float)); m->tmatOut = (uint16_t *)malloc((keep + 1) * sizeof(uint16_t));
            m->tkeyOut = (uint32_t *)malloc((keep + 1) * sizeof(uint32_t));
            if (!m->uv || !m->tmatOut || !m->tkeyOut) { VoxFreeMesh(m); return 0; }
            for (size_t i = 0; i < p->ntv; i++) {          // ground: world-based (repeating detail); placed models: their own
                if (p->tuv && i >= G1 * G1) { m->uv[i * 2] = p->tuv[i * 2]; m->uv[i * 2 + 1] = p->tuv[i * 2 + 1]; }
                else { m->uv[i * 2] = p->tv[i * 3] / TERA_TEX_REPEAT; m->uv[i * 2 + 1] = p->tv[i * 3 + 2] / TERA_TEX_REPEAT; }
            }
            // counting sort by draw group: ground materials 0-(VOX_MAXMAT-1), model textures VOX_MAXMAT + slot
            uint32_t maxKey = VOX_MAXMAT; if (p->ttex) for (size_t t = 0; t < p->nti; t++) if (p->ttex[t] != 0xFFFF && (uint32_t)VOX_MAXMAT + p->ttex[t] + 1 > maxKey) maxKey = (uint32_t)VOX_MAXMAT + p->ttex[t] + 1;
            size_t *start = (size_t *)calloc(maxKey + 1, sizeof(size_t));
            if (!start) { VoxFreeMesh(m); return 0; }
            #define VOX_TKEY(t) ((p->ttex && p->ttex[t] != 0xFFFF) ? (uint32_t)VOX_MAXMAT + p->ttex[t] : (uint32_t)p->tmat[t])
            for (size_t t = 0; t < p->nti; t++) if (!hidden || !hidden[p->tmat[t]]) start[VOX_TKEY(t) + 1]++;
            for (uint32_t k = 0; k < maxKey; k++) start[k + 1] += start[k];
            for (size_t t = 0; t < p->nti; t++) {
                unsigned mat = p->tmat[t]; if (hidden && hidden[mat]) continue;
                uint32_t key = VOX_TKEY(t);
                size_t o = start[key]++;
                m->tmatOut[o] = (uint16_t)mat; m->tkeyOut[o] = key;
                m->idx[o * 3] = p->ti[t * 3]; m->idx[o * 3 + 1] = p->ti[t * 3 + 1]; m->idx[o * 3 + 2] = p->ti[t * 3 + 2];
            }
            #undef VOX_TKEY
            free(start);
            m->ni = keep * 3;
        } else
        for (size_t t = 0; t < p->nti; t++) {
            if (hidden && hidden[p->tmat[t]]) continue;
            m->idx[m->ni++] = p->ti[t * 3]; m->idx[m->ni++] = p->ti[t * 3 + 1]; m->idx[m->ni++] = p->ti[t * 3 + 2];
        }
        float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
        for (size_t t = 0; t < p->ntv; t++) for (int k = 0; k < 3; k++) { float v = p->tv[t * 3 + k]; if (v < lo[k]) lo[k] = v; if (v > hi[k]) hi[k] = v; }
        for (int k = 0; k < 3; k++) { m->centre[k] = (lo[k] + hi[k]) / 2; m->bmin[k] = lo[k] - m->centre[k]; m->bmax[k] = hi[k] - m->centre[k]; }
        for (size_t v = 0; v < m->nv; v++) for (int k = 0; k < 3; k++) m->pos[v * 3 + k] -= m->centre[k];
        return 1;
    }
    size_t keep = 0;
    for (size_t t = 0; t < p->nti; t++) if (!hidden || !hidden[p->tmat[t]]) keep++;
    m->pos = (float *)malloc((keep * 3 + 1) * 3 * sizeof(float)); m->nrm = (signed char *)malloc((keep * 3 + 1) * 4);
    m->col = (unsigned char *)malloc((keep * 3 + 1) * 4); m->idx = (uint32_t *)malloc((keep * 3 + 1) * sizeof(uint32_t));
    if (p->tuv) m->uv = (float *)malloc((keep * 3 + 1) * 2 * sizeof(float));
    if (p->vBone) m->src = (uint32_t *)malloc((keep * 3 + 1) * sizeof(uint32_t));
    if (!m->pos || !m->nrm || !m->col || !m->idx || (p->tuv && !m->uv)) { VoxFreeMesh(m); return 0; }
    for (size_t t = 0; t < p->nti; t++) {
        unsigned mat = p->tmat[t];
        if (hidden && hidden[mat]) continue;
        const float *A = p->tv + p->ti[t * 3] * 3, *B = p->tv + p->ti[t * 3 + 1] * 3, *C = p->tv + p->ti[t * 3 + 2] * 3;
        float e1[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] }, e2[3] = { C[0] - A[0], C[1] - A[1], C[2] - A[2] };
        float nx = e1[1] * e2[2] - e1[2] * e2[1], ny = e1[2] * e2[0] - e1[0] * e2[2], nz = e1[0] * e2[1] - e1[1] * e2[0];
        float l = sqrtf(nx * nx + ny * ny + nz * nz); if (l > 0) { nx /= l; ny /= l; nz /= l; }
        unsigned char rgb[3]; VoxMatColour(p, (int)mat, rgb);
        const float *P[3] = { A, B, C };
        for (int k = 0; k < 3; k++) {
            size_t v = m->nv++;
            memcpy(m->pos + v * 3, P[k], 12);
            if (m->uv) memcpy(m->uv + v * 2, p->tuv + (size_t)p->ti[t * 3 + k] * 2, 8);
            if (m->src) m->src[v] = p->ti[t * 3 + k];
            if (p->tn) memcpy(m->nrm + v * 4, p->tn + (size_t)p->ti[t * 3 + k] * 4, 4);     // the model's own smooth normals
            else { m->nrm[v * 4] = (signed char)(nx * 127); m->nrm[v * 4 + 1] = (signed char)(ny * 127); m->nrm[v * 4 + 2] = (signed char)(nz * 127); m->nrm[v * 4 + 3] = 0; }
            m->col[v * 4] = rgb[0]; m->col[v * 4 + 1] = rgb[1]; m->col[v * 4 + 2] = rgb[2]; m->col[v * 4 + 3] = (unsigned char)mat;
            m->idx[m->ni++] = (uint32_t)v;
        }
    }
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (size_t t = 0; t < p->ntv; t++) for (int k = 0; k < 3; k++) { float v = p->tv[t * 3 + k]; if (v < lo[k]) lo[k] = v; if (v > hi[k]) hi[k] = v; }
    for (size_t t = 0; t < p->nl * 2; t++) {          // lines (cloth links, collision shapes) that are shown count too
        if (hidden && hidden[p->lmat[t / 2]]) continue;
        for (int k = 0; k < 3; k++) { float v = p->lv[t * 3 + k]; if (v < lo[k]) lo[k] = v; if (v > hi[k]) hi[k] = v; }
    }
    if (lo[0] > hi[0]) for (int k = 0; k < 3; k++) lo[k] = hi[k] = 0;
    for (int k = 0; k < 3; k++) { m->centre[k] = (lo[k] + hi[k]) / 2; m->bmin[k] = lo[k] - m->centre[k]; m->bmax[k] = hi[k] - m->centre[k]; }
    for (size_t v = 0; v < m->nv; v++) for (int k = 0; k < 3; k++) m->pos[v * 3 + k] -= m->centre[k];
    return 1;
}

/* ---------------- material colours: from the name, with a small per-id variation ---------------- */
static int VoxStartsWith(const char *s, const char *w) { return strncmp(s, w, strlen(w)) == 0; }
static void VoxMatColour(const VoxPart *p, int id, unsigned char out[3]) {
    if (p->terrGeneric && id >= 0 && id < VOX_MAXMAT && p->terrMat[id] >= 0) { TrMatColour((unsigned)p->terrMat[id], out); return; }   // (another scene's ground: a steady colour per number)
    const char *n = id < p->nmat ? p->matName[id] : "";
    int r = 160, g = 160, b = 160;
    struct { const char *w; int r, g, b; } t[] = {
        {"Grass", 96, 168, 64}, {"Soil", 150, 108, 70}, {"Mud", 112, 82, 56}, {"BaseRock", 74, 72, 78},
        {"Rock", 132, 126, 120}, {"Stone", 140, 140, 136}, {"Sand", 214, 190, 130}, {"MoneyCheap", 196, 168, 80},
        {"Money", 236, 190, 40}, {"DarkGold", 150, 120, 40}, {"Obsidian", 60, 46, 80}, {"Lava", 230, 80, 30},
        {"HardWater", 70, 120, 200}, {"Water", 70, 140, 230}, {"Ice", 170, 220, 240}, {"Crystal", 210, 130, 220},
        {"Bomb", 210, 50, 40}, {"Concrete", 178, 178, 172}, {"Iron", 120, 130, 140}, {"ElectricIron", 120, 150, 190},
        {"Salt", 236, 236, 228}, {"Light", 250, 240, 170}, {"Tree", 100, 80, 50}, {"Apple", 200, 40, 40},
        {"Seal", 70, 150, 140}, {"Void", 120, 60, 160}, {"Thorn", 60, 100, 50}, {"Rubber", 50, 50, 55},
        {"Birdlime", 200, 210, 140}, {"Tombstone", 110, 110, 115}, {"VoxelN", 230, 90, 200}, {"Rocket", 200, 90, 60},
        {"TimerBreaker", 240, 150, 40}, {"Merged", 150, 150, 150}, {"Character", 230, 120, 120}, {"Object", 150, 150, 170},
        {"Collision", 255, 0, 255}, {"Invalid", 255, 0, 255}, {"Undefined", 255, 0, 255}, {"NoReaction", 200, 200, 200},
    };
    int found = 0;
    {   // MKW invisible areas: coral (out of bounds) through orange / yellow to green (course)
        struct { const char *w; int r, g, b; } c[] = { {"Area: WorldEndArea", 255, 112, 84}, {"Area: RescueArea", 255, 150, 70},
            {"Area: RideKeepOutArea", 250, 190, 70}, {"Area: ValleyArea", 235, 225, 80}, {"Area: WaterShallowArea", 180, 225, 90},
            {"Area: GlideArea", 120, 215, 110}, {"Area: CourseArea", 60, 200, 110},
            {"Area:", 140, 170, 235}, {"Marker: PSwitch", 40, 110, 255}, {"Marker: SpecialCoin", 255, 120, 200},
            {"Marker: HatenaPanel", 255, 210, 40}, {"Marker:", 235, 80, 235} };   // (the game's collectible lists: blue / pink / yellow)      // (other areas: blue; markers for objects without a model: magenta)
        for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) if (!strncmp(n, c[i].w, strlen(c[i].w))) { out[0] = (unsigned char)c[i].r; out[1] = (unsigned char)c[i].g; out[2] = (unsigned char)c[i].b; return; }
    }
    {                                                     // cloth physics overlay (any model): fixed, clearly different colours
        struct { const char *w; int r, g, b; } c[] = { {"Cloth particles (pinned)", 240, 60, 50}, {"Cloth particles", 250, 220, 60},
            {"Cloth links: Standard", 60, 220, 240}, {"Cloth links: Stretch", 90, 240, 120}, {"Cloth links: Bend", 250, 150, 40},
            {"Cloth links", 200, 200, 255}, {"Collision", 230, 80, 230} };
        const char *q = strstr(n, ": Cloth "); q = q ? q + 2 : strstr(n, ": Collision ") ? strstr(n, ": Collision ") + 2 : n;
        for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) if (VoxStartsWith(q, c[i].w)) { out[0] = (unsigned char)c[i].r; out[1] = (unsigned char)c[i].g; out[2] = (unsigned char)c[i].b; return; }
    }
    if (p->isModel) {                                     // model parts: an even spread of soft colours
        static const unsigned char pal[12][3] = { {214,150,96}, {120,180,220}, {150,200,120}, {220,190,90}, {190,130,200}, {110,190,170},
                                                  {230,120,120}, {160,160,220}, {200,170,140}, {120,150,110}, {220,160,190}, {170,200,220} };
        out[0] = pal[id % 12][0]; out[1] = pal[id % 12][1]; out[2] = pal[id % 12][2];
        return;
    }
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
        if (VoxStartsWith(n, t[i].w)) { r = t[i].r; g = t[i].g; b = t[i].b; found = 1; break; }
    unsigned h = (unsigned)id * 2654435761u;
    int d = (int)((h >> 24) % 41) - 20;                   // -20..20 so neighbouring materials differ
    if (!found) { r = 90 + (h & 127); g = 90 + ((h >> 8) & 127); b = 90 + ((h >> 16) & 127); d = 0; }
    r += d; g += d; b += d;
    out[0] = (unsigned char)(r < 0 ? 0 : r > 255 ? 255 : r);
    out[1] = (unsigned char)(g < 0 ? 0 : g > 255 ? 255 : g);
    out[2] = (unsigned char)(b < 0 ? 0 : b > 255 ? 255 : b);
}

/* ---------------- surface nets ---------------- */
typedef struct { uint64_t *keys; uint32_t *vals; size_t cap, n; } VoxHash;
static int VoxHashInit(VoxHash *h, size_t cap) { h->cap = cap; h->n = 0; h->keys = (uint64_t *)malloc(cap * 8); h->vals = (uint32_t *)malloc(cap * 4); if (!h->keys || !h->vals) return 0; memset(h->keys, 0xFF, cap * 8); return 1; }
static void VoxHashFree(VoxHash *h) { free(h->keys); free(h->vals); }
static size_t VoxHashSlot(uint64_t k, size_t cap) { k ^= k >> 33; k *= 0xff51afd7ed558ccdULL; k ^= k >> 33; return (size_t)(k & (cap - 1)); }
static int VoxHashPut(VoxHash *h, uint64_t k, uint32_t v) {
    if ((h->n + 1) * 2 > h->cap) {
        VoxHash nh; if (!VoxHashInit(&nh, h->cap * 2)) return 0;
        for (size_t i = 0; i < h->cap; i++) if (h->keys[i] != ~0ULL) {
            size_t s = VoxHashSlot(h->keys[i], nh.cap); while (nh.keys[s] != ~0ULL) s = (s + 1) & (nh.cap - 1);
            nh.keys[s] = h->keys[i]; nh.vals[s] = h->vals[i]; nh.n++;
        }
        VoxHashFree(h); *h = nh;
    }
    size_t s = VoxHashSlot(k, h->cap);
    while (h->keys[s] != ~0ULL) { if (h->keys[s] == k) { h->vals[s] = v; return 1; } s = (s + 1) & (h->cap - 1); }
    h->keys[s] = k; h->vals[s] = v; h->n++;
    return 1;
}
static int VoxHashGet(const VoxHash *h, uint64_t k, uint32_t *v) {
    size_t s = VoxHashSlot(k, h->cap);
    while (h->keys[s] != ~0ULL) { if (h->keys[s] == k) { *v = h->vals[s]; return 1; } s = (s + 1) & (h->cap - 1); }
    return 0;
}
#define VOXKEY(x, y, z) (((uint64_t)(uint32_t)((x) + 1) << 42) | ((uint64_t)(uint32_t)((y) + 1) << 21) | (uint64_t)(uint32_t)((z) + 1))

// fill / material of one voxel (fine grid); hidden materials count as empty
static inline void VoxAt(const VoxPart *p, const unsigned char *hidden, int x, int y, int z, int *f, int *m) {
    *f = 0; *m = 0;
    if (x < 0 || y < 0 || z < 0 || x >= p->bx * 8 || y >= p->by * 8 || z >= p->bz * 8) return;
    size_t b = ((size_t)(z >> 3) * p->by + (y >> 3)) * p->bx + (x >> 3);
    const unsigned char *F = p->fill[b];
    if (!F) return;
    int i = ((z & 7) * 8 + (y & 7)) * 8 + (x & 7);
    int mm = p->mat[b][i];
    if (hidden && hidden[mm]) return;
    *f = F[i]; *m = mm;
}
// coarse sample (lod = 1 or 2): average fill, material of the fullest voxel
static void VoxSample(const VoxPart *p, const unsigned char *hidden, int lod, int x, int y, int z, int *f, int *m) {
    if (lod == 1) { VoxAt(p, hidden, x, y, z, f, m); return; }
    int sum = 0, best = -1, bm = 0;
    for (int dz = 0; dz < 2; dz++) for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++) {
        int ff, mm; VoxAt(p, hidden, x * 2 + dx, y * 2 + dy, z * 2 + dz, &ff, &mm);
        sum += ff; if (ff > best) { best = ff; bm = mm; }
    }
    *f = sum / 8; *m = bm;
}

static int VoxGrowV(VoxMesh *m) {
    size_t nc = m->capv ? m->capv * 2 : 65536;
    float *p = (float *)realloc(m->pos, nc * 12); if (!p) return 0; m->pos = p;
    signed char *n = (signed char *)realloc(m->nrm, nc * 4); if (!n) return 0; m->nrm = n;
    unsigned char *c = (unsigned char *)realloc(m->col, nc * 4); if (!c) return 0; m->col = c;
    m->capv = nc; return 1;
}
static int VoxGrowI(VoxMesh *m) {
    size_t nc = m->capi ? m->capi * 2 : 196608;
    uint32_t *i = (uint32_t *)realloc(m->idx, nc * 4); if (!i) return 0; m->idx = i;
    m->capi = nc; return 1;
}

#define VOX_ISO 127.5f
// lod 1 = full detail, 2 = half. hidden[256]: materials to leave out (NULL = none).
// cancel: polled; set non-zero to stop early (returns 0). progress (0..1000) written if not NULL.
static int VoxBuildMesh(const VoxPart *p, VoxMesh *m, int lod, const unsigned char *hidden, volatile long *cancel, volatile long *progress) {
    if (p->isTriMesh) { int r = VoxBuildTriMesh(p, m, hidden); if (progress) *progress = 1000; return r; }
    memset(m, 0, sizeof(*m));
    if (lod != 2) lod = 1;
    const int C = 8 / lod;                      // cells per block edge
    const int nbt = p->bx * p->by * p->bz;
    // chunks to visit: blocks with data, plus their -1 neighbours (cells there can reach into a block)
    unsigned char *visit = (unsigned char *)calloc(nbt, 1);
    if (!visit) return 0;
    for (int bz = 0; bz < p->bz; bz++) for (int by = 0; by < p->by; by++) for (int bx = 0; bx < p->bx; bx++) {
        if (!p->fill[((size_t)bz * p->by + by) * p->bx + bx]) continue;
        for (int dz = -1; dz <= 0; dz++) for (int dy = -1; dy <= 0; dy++) for (int dx = -1; dx <= 0; dx++) {
            int x = bx + dx, y = by + dy, z = bz + dz;
            if (x < 0 || y < 0 || z < 0) continue;      // cells at -1 are handled by the chunk at 0 below
            visit[((size_t)z * p->by + y) * p->bx + x] = 1;
        }
    }
    // also the -1 layer of cells outside the part (closes the surface at the low edges)
    VoxHash h; if (!VoxHashInit(&h, 1 << 20)) { free(visit); return 0; }
    const int L = C + 2;                         // local samples: -1 .. C
    int *lf = (int *)malloc(sizeof(int) * L * L * L), *lm = (int *)malloc(sizeof(int) * L * L * L);
    if (!lf || !lm) { free(lf); free(lm); free(visit); VoxHashFree(&h); return 0; }
    #define LI(x, y, z) ((((z) + 1) * L + ((y) + 1)) * L + ((x) + 1))
    float sx = p->scale[0] * lod, sy = p->scale[1] * lod, sz = p->scale[2] * lod;
    int ok = 1;
    for (int pass = 0; pass < 2 && ok; pass++) {
        for (int bi = 0; bi < nbt && ok; bi++) {
            if (!visit[bi]) continue;
            if (cancel && *cancel) { ok = 0; break; }
            if (progress && (bi & 255) == 0) *progress = (long)(pass * 500 + (long long)bi * 500 / nbt);
            int bx = bi % p->bx, by = (bi / p->bx) % p->by, bz = bi / (p->bx * p->by);
            int ox = bx * C, oy = by * C, oz = bz * C;   // chunk origin in cells
            // gather samples -1..C (cells at the low edge of the part use the -1 samples)
            int any = 0, anyEmpty = 0;
            for (int z = -1; z <= C; z++) for (int y = -1; y <= C; y++) for (int x = -1; x <= C; x++) {
                int f, mm; VoxSample(p, hidden, lod, ox + x, oy + y, oz + z, &f, &mm);
                lf[LI(x, y, z)] = f; lm[LI(x, y, z)] = mm;
                if (f > VOX_ISO) any = 1; else anyEmpty = 1;
            }
            if (!any || !anyEmpty) continue;
            // cells -1..C-1 when at the low edge of the part, else 0..C-1
            int x0 = bx == 0 ? -1 : 0, y0 = by == 0 ? -1 : 0, z0 = bz == 0 ? -1 : 0;
            for (int z = z0; z < C && ok; z++) for (int y = y0; y < C; y++) for (int x = x0; x < C; x++) {
                int c[8], mask = 0;
                for (int k = 0; k < 8; k++) {
                    c[k] = lf[LI(x + (k & 1), y + ((k >> 1) & 1), z + (k >> 2))];
                    if (c[k] > VOX_ISO) mask |= 1 << k;
                }
                if (mask == 0 || mask == 255) continue;
                int gx = ox + x, gy = oy + y, gz = oz + z;
                if (pass == 0) {
                    // vertex: average of the edge crossings
                    static const int E[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
                    float ax = 0, ay = 0, az = 0; int ne = 0;
                    for (int e = 0; e < 12; e++) {
                        int a = E[e][0], b = E[e][1];
                        if (((mask >> a) & 1) == ((mask >> b) & 1)) continue;
                        float t = (VOX_ISO - c[a]) / (float)(c[b] - c[a]);
                        ax += (a & 1) + t * ((b & 1) - (a & 1));
                        ay += ((a >> 1) & 1) + t * (((b >> 1) & 1) - ((a >> 1) & 1));
                        az += (a >> 2) + t * ((b >> 2) - (a >> 2));
                        ne++;
                    }
                    ax /= ne; ay /= ne; az /= ne;
                    float nxv = -((c[1] + c[3] + c[5] + c[7]) - (c[0] + c[2] + c[4] + c[6])) * sy * sz;
                    float nyv = -((c[2] + c[3] + c[6] + c[7]) - (c[0] + c[1] + c[4] + c[5])) * sx * sz;
                    float nzv = -((c[4] + c[5] + c[6] + c[7]) - (c[0] + c[1] + c[2] + c[3])) * sx * sy;
                    float len = sqrtf(nxv * nxv + nyv * nyv + nzv * nzv); if (len < 1e-6f) { nyv = 1; len = 1; }
                    // material: fullest corner
                    int best = 0; for (int k = 1; k < 8; k++) if (c[k] > c[best]) best = k;
                    int mat = lm[LI(x + (best & 1), y + ((best >> 1) & 1), z + (best >> 2))];
                    if (m->nv == m->capv && !VoxGrowV(m)) { ok = 0; break; }
                    size_t v = m->nv++;
                    m->pos[v * 3 + 0] = (gx + ax) * sx;     // samples sit at integer positions
                    m->pos[v * 3 + 1] = (gy + ay) * sy;
                    m->pos[v * 3 + 2] = (gz + az) * sz;
                    m->nrm[v * 4 + 0] = (signed char)(nxv / len * 127); m->nrm[v * 4 + 1] = (signed char)(nyv / len * 127);
                    m->nrm[v * 4 + 2] = (signed char)(nzv / len * 127); m->nrm[v * 4 + 3] = 0;
                    unsigned char rgb[3]; VoxMatColour(p, mat, rgb);
                    m->col[v * 4 + 0] = rgb[0]; m->col[v * 4 + 1] = rgb[1]; m->col[v * 4 + 2] = rgb[2]; m->col[v * 4 + 3] = (unsigned char)mat;
                    if (!VoxHashPut(&h, VOXKEY(gx, gy, gz), (uint32_t)v)) { ok = 0; break; }
                } else {
                    // faces: for each of the 3 edges leaving corner 0 that crosses the surface,
                    // join the 4 cells around that edge
                    for (int a = 0; a < 3; a++) {
                        int kb = 1 << a;
                        int in0 = mask & 1, in1 = (mask >> kb) & 1;
                        if (in0 == in1) continue;
                        int b1 = (a + 1) % 3, b2 = (a + 2) % 3;
                        int d1[3] = { 0, 0, 0 }, d2[3] = { 0, 0, 0 }; d1[b1] = 1; d2[b2] = 1;
                        int q[4][3] = {
                            { gx, gy, gz },
                            { gx - d1[0], gy - d1[1], gz - d1[2] },
                            { gx - d1[0] - d2[0], gy - d1[1] - d2[1], gz - d1[2] - d2[2] },
                            { gx - d2[0], gy - d2[1], gz - d2[2] } };
                        uint32_t vi[4]; int okq = 1;
                        for (int k = 0; k < 4 && okq; k++) okq = VoxHashGet(&h, VOXKEY(q[k][0], q[k][1], q[k][2]), &vi[k]);
                        if (!okq) continue;
                        while (m->ni + 6 > m->capi) if (!VoxGrowI(m)) { ok = 0; break; }
                        if (!ok) break;
                        uint32_t *o = m->idx + m->ni;
                        if (in0) { o[0] = vi[0]; o[1] = vi[1]; o[2] = vi[2]; o[3] = vi[0]; o[4] = vi[2]; o[5] = vi[3]; }
                        else     { o[0] = vi[0]; o[1] = vi[2]; o[2] = vi[1]; o[3] = vi[0]; o[4] = vi[3]; o[5] = vi[2]; }
                        m->ni += 6;
                    }
                }
            }
        }
    }
    #undef LI
    free(lf); free(lm); free(visit); VoxHashFree(&h);
    if (!ok) { VoxFreeMesh(m); return 0; }
    // centre the model
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (size_t v = 0; v < m->nv; v++) for (int k = 0; k < 3; k++) { float t = m->pos[v * 3 + k]; if (t < lo[k]) lo[k] = t; if (t > hi[k]) hi[k] = t; }
    if (!m->nv) { for (int k = 0; k < 3; k++) lo[k] = hi[k] = 0; }
    for (int k = 0; k < 3; k++) { m->centre[k] = (lo[k] + hi[k]) / 2; m->bmin[k] = lo[k] - m->centre[k]; m->bmax[k] = hi[k] - m->centre[k]; }
    for (size_t v = 0; v < m->nv; v++) for (int k = 0; k < 3; k++) m->pos[v * 3 + k] -= m->centre[k];
    if (progress) *progress = 1000;
    return 1;
}
#endif
