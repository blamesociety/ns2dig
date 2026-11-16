/* AAMP parameter archives (version 2): the AGL environment / lighting files in romfs\Env\*.genvb.
   Portable C. Parses a file into a flat list of settings: "object path" + name + typed value. */
#ifndef AAMP_CORE_H
#define AAMP_CORE_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "aamp_names.h"

enum { AM_BOOL = 0, AM_F32, AM_INT, AM_VEC2, AM_VEC3, AM_VEC4, AM_COLOR, AM_STR32, AM_STR64, AM_CURVE1, AM_CURVE2, AM_CURVE3,
       AM_CURVE4, AM_BUFINT, AM_BUFF32, AM_STR256, AM_QUAT, AM_U32, AM_BUFU32, AM_BUFBIN, AM_STRREF };

typedef struct {
    char obj[128];      // where it sits: list/object names joined with '/', e.g. "HemisphereLight/HemisphereLight0"
    char name[64];      // the setting's name, or 0x........ if it couldn't be recovered
    int named;          // name recovered
    int type;
    float f[4];         // numbers (bool / int also here)
    int nf;             // how many of f are used
    char text[160];     // strings, buffers, curves: shown as text
} AmParam;

typedef struct {
    char ptype[32];     // "projgfxbase", "aglblm"...
    AmParam *p; int np, cap;
    int unnamed;
    char err[160];
} AmFile;

static uint32_t AmCrcT[256];
static void AmCrcInit(void) {
    if (AmCrcT[1]) return;
    for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; AmCrcT[i] = c; }
}
static uint32_t AmCrc(const char *s) { uint32_t c = 0xFFFFFFFFu; while (*s) c = AmCrcT[(c ^ (uint8_t)*s++) & 255] ^ (c >> 8); return ~c; }

// hash -> name: the fixed list plus numbered names ("0".."99", param0, data1, curve2...)
typedef struct { uint32_t h; char n[48]; } AmName;
static AmName *AmTab; static int AmTabN;
static int AmCmp(const void *a, const void *b) { uint32_t x = ((const AmName *)a)->h, y = ((const AmName *)b)->h; return x < y ? -1 : x > y; }
static void AmNamesInit(void) {
    if (AmTab) return;
    AmCrcInit();
    static const char *num[] = { "%d", "param%d", "data%d", "curve%d", "color%d", "Light%d", "DirectionalLight%d", "Fog%d",
        /* lens flare elements (aglofx), from the game executable */
        "Position%d", "AngleCenter%d", "AnglePower%d", "AngleWidth%d", "BlendMode%d", "CenterPosAlphaPow%d", "CenterPosAlphaRate%d", "CenterPosScalingPow%d", "CenterPosScalingRate%d", "Color%d", "EdgeScaleRate%d", "Intensity%d", "IsEnableAngleOcclusion%d", "IsEnableDraw%d", "IsEnableEdgeScaling%d", "IsEnableOccludedAlpha%d", "IsEnableOccludedDec%d", "IsEnableOccludedScaling%d", "IsEnableOctagon%d", "IsEnableRotate%d", "IsEnableRotateInv%d", "IsEnableRotatePos%d", "IsSizeZoom%d", "Size%d", "Rotate%d", "RotatePosRate%d", "Texture2Idx%d", "TextureIdx%d", "IsQuarter%d", "RefTexName%d", "TextureInfo%d", "CenterPos%d", "BaseAxis%d" , /* multi filter (aglmf) */ "reduce_%d", "blur_%d", "change_format_%d", "color_drift_%d", "trimming_%d", "hdr_compose_%d", "expand_%d", "color_correction_%d", "data_index%d", "depth_shadow_parameter_%d", "projection_shadow_%d", "config_%d", "deferred_shading_model%d", "depth_shadow_cascade_num_%d", "depth_shadow_clip_plane_%d", "depth_shadow_far_%d", "depth_shadow_offset_far_%d", "depth_shadow_offset_near_%d", "depth_shadow_refvalue_threshold_%d", "depth_shadow_use_uniform_mtx_%d", "env_obj_gen_name%d", "env_obj_gen_num%d", "light_map_name_%d", "user_visualize_clr_name_%d", "user_visualize_tex_name_%d", "config_%02d",
        /* light maps (agllmap), matched from their hashes */ "name%d", "intensity%d" };
    int nf = (int)(sizeof AmNames / sizeof AmNames[0]), nn = (int)(sizeof num / sizeof num[0]);
    AmTab = (AmName *)calloc((size_t)nf + nn * 100, sizeof(AmName)); if (!AmTab) return;
    for (int i = 0; i < nf; i++) { snprintf(AmTab[AmTabN].n, 48, "%s", AmNames[i]); AmTab[AmTabN].h = AmCrc(AmTab[AmTabN].n); AmTabN++; }
    for (int k = 0; k < nn; k++) for (int i = 0; i < 100; i++) { snprintf(AmTab[AmTabN].n, 48, num[k], i); AmTab[AmTabN].h = AmCrc(AmTab[AmTabN].n); AmTabN++; }
    static const struct { uint32_t h; const char *n; } part[] = { { 0x70b1b8f2u, "?r" }, { 0x1d6c5c19u, "?g" }, { 0x6d06a896u, "?b" }, { 0xf40ff92cu, "?a" } };
    AmName *q = (AmName *)realloc(AmTab, ((size_t)AmTabN + 4) * sizeof(AmName));
    if (q) { AmTab = q; for (int i = 0; i < 4; i++) { AmTab[AmTabN].h = part[i].h; snprintf(AmTab[AmTabN].n, 48, "%s", part[i].n); AmTabN++; } }
    qsort(AmTab, AmTabN, sizeof(AmName), AmCmp);
}
static const char *AmLookup(uint32_t h) {
    int lo = 0, hi = AmTabN - 1;
    while (lo <= hi) { int m = (lo + hi) / 2; if (AmTab[m].h == h) return AmTab[m].n; if (AmTab[m].h < h) lo = m + 1; else hi = m - 1; }
    return NULL;
}

static uint32_t AmRd32(const unsigned char *b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }
static uint16_t AmRd16(const unsigned char *b) { return (uint16_t)(b[0] | (b[1] << 8)); }
static float AmRdF(const unsigned char *b) { float f; memcpy(&f, b, 4); return f; }

static void AmName_(uint32_t h, char *out, size_t n, int *named) {
    const char *s = AmLookup(h);
    if (s) { snprintf(out, n, "%s", s); if (named) *named = 1; }
    else { snprintf(out, n, "0x%08x", h); if (named) *named = 0; }
}

static void AmAdd(AmFile *f, const AmParam *p) {
    if (f->np == f->cap) { int c = f->cap ? f->cap * 2 : 128; AmParam *q = (AmParam *)realloc(f->p, (size_t)c * sizeof(AmParam)); if (!q) return; f->p = q; f->cap = c; }
    f->p[f->np++] = *p;
}

static void AmList(AmFile *f, const unsigned char *b, size_t n, size_t o, const char *path, int depth) {
    if (o + 12 > n || depth > 32) return;
    int lo = AmRd16(b + o + 4), ln = AmRd16(b + o + 6), oo = AmRd16(b + o + 8), on = AmRd16(b + o + 10);
    for (int i = 0; i < on; i++) {
        size_t q = o + (size_t)oo * 4 + (size_t)i * 8; if (q + 8 > n) break;
        char oname[64]; AmName_(AmRd32(b + q), oname, sizeof oname, NULL);
        int po = AmRd16(b + q + 4), pn = AmRd16(b + q + 6);
        for (int k = 0; k < pn; k++) {
            size_t r = q + (size_t)po * 4 + (size_t)k * 8; if (r + 8 > n) break;
            uint32_t x = AmRd32(b + r + 4); size_t d = r + (size_t)(x & 0xFFFFFF) * 4; int t = (int)(x >> 24);
            AmParam p; memset(&p, 0, sizeof p);
            if (path[0]) snprintf(p.obj, sizeof p.obj, "%s/%s", path, oname); else snprintf(p.obj, sizeof p.obj, "%s", oname);
            AmName_(AmRd32(b + r), p.name, sizeof p.name, &p.named);
            if (!p.named) f->unnamed++;
            p.type = t;
            if (d + 4 > n) { snprintf(p.text, sizeof p.text, "(out of range)"); AmAdd(f, &p); continue; }
            switch (t) {
            case AM_BOOL: p.f[0] = AmRd32(b + d) ? 1.0f : 0.0f; p.nf = 1; break;
            case AM_F32: p.f[0] = AmRdF(b + d); p.nf = 1; break;
            case AM_INT: p.f[0] = (float)(int32_t)AmRd32(b + d); p.nf = 1; break;
            case AM_U32: p.f[0] = (float)AmRd32(b + d); p.nf = 1; break;
            case AM_VEC2: case AM_VEC3: case AM_VEC4: case AM_COLOR: case AM_QUAT: {
                int m = t == AM_VEC2 ? 2 : t == AM_VEC3 ? 3 : 4;
                if (d + (size_t)m * 4 > n) break;
                for (int j = 0; j < m; j++) p.f[j] = AmRdF(b + d + j * 4);
                p.nf = m; break;
            }
            case AM_STR32: case AM_STR64: case AM_STR256: case AM_STRREF: {
                size_t e = d; while (e < n && b[e] && e - d < sizeof p.text - 1) e++;
                memcpy(p.text, b + d, e - d); p.text[e - d] = 0; break;
            }
            case AM_BUFINT: case AM_BUFF32: case AM_BUFU32: case AM_BUFBIN: {
                uint32_t cnt = AmRd32(b + d - 4); size_t at = 0;
                at += snprintf(p.text + at, sizeof p.text - at, "%u values:", cnt);
                for (uint32_t j = 0; j < cnt && j < 12 && d + j * 4 + 4 <= n && at < sizeof p.text - 16; j++) {
                    if (t == AM_BUFF32) at += snprintf(p.text + at, sizeof p.text - at, " %g", AmRdF(b + d + j * 4));
                    else if (t == AM_BUFBIN) at += snprintf(p.text + at, sizeof p.text - at, " %02x", b[d + j]);
                    else at += snprintf(p.text + at, sizeof p.text - at, " %d", (int32_t)AmRd32(b + d + j * 4));
                }
                if (cnt > 12 && at < sizeof p.text - 6) snprintf(p.text + at, sizeof p.text - at, " ...");
                break;
            }
            default:
                if (t >= AM_CURVE1 && t <= AM_CURVE4) snprintf(p.text, sizeof p.text, "curve (%d)", t - AM_CURVE1 + 1);
                else snprintf(p.text, sizeof p.text, "(type %d)", t);
            }
            AmAdd(f, &p);
        }
    }
    for (int i = 0; i < ln; i++) {
        size_t q = o + (size_t)lo * 4 + (size_t)i * 12; if (q + 12 > n) break;
        char lname[64]; AmName_(AmRd32(b + q), lname, sizeof lname, NULL);
        char sub[128];
        if (path[0]) snprintf(sub, sizeof sub, "%s/%s", path, lname); else snprintf(sub, sizeof sub, "%s", lname);
        AmList(f, b, n, q, sub, depth + 1);
    }
}

static inline void AmFree(AmFile *f) { free(f->p); memset(f, 0, sizeof *f); }
static int AmParse(const unsigned char *b, size_t n, AmFile *f) {
    memset(f, 0, sizeof *f);
    AmNamesInit();
    if (n < 0x30 || memcmp(b, "AAMP", 4)) { snprintf(f->err, sizeof f->err, "Not an AAMP file."); return 0; }
    if (AmRd32(b + 4) != 2) { snprintf(f->err, sizeof f->err, "AAMP version %u isn't supported.", AmRd32(b + 4)); return 0; }
    uint32_t pio = AmRd32(b + 0x14);
    if (0x30 + (size_t)pio + 12 > n) { snprintf(f->err, sizeof f->err, "The file is cut short."); return 0; }
    size_t i = 0; while (i < pio && i < sizeof f->ptype - 1 && b[0x30 + i]) { f->ptype[i] = (char)b[0x30 + i]; i++; }
    AmList(f, b, n, 0x30 + pio, "", 0);     // the root list (param_root) isn't part of the paths
    return 1;
}
// a setting by object (the last part of its path, or "" for any) and name
static inline const AmParam *AmFind(const AmFile *f, const char *obj, const char *name) {
    if (!f) return NULL;
    for (int i = 0; i < f->np; i++) {
        if (strcmp(f->p[i].name, name)) continue;
        if (!obj || !obj[0]) return &f->p[i];
        const char *s = strrchr(f->p[i].obj, '/'); s = s ? s + 1 : f->p[i].obj;
        if (!strcmp(s, obj) || strstr(f->p[i].obj, obj)) return &f->p[i];
    }
    return NULL;
}
#endif
