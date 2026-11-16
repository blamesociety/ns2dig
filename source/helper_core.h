/* Helper bones (Phive\HelperBone.alsarc.zs\<model>.bphhb, an AAMP archive): extra "Sub_" bones the game moves from
   the rotation of other bones so shoulders, elbows, chest and fingers keep their shape. Portable C.
   driver_bone_list: a bone measured against a base bone (its rotation away from the bind pose, as X / Y / Z angles);
   connection_curve_list: driver_bone_id + which angle (0 X, 1 Y, 2 Z) -> a Hermite curve of keys [angle, value,
   in slope, out slope] (slopes per radian); output_list: output -> curve;
   driven_bone_list: bone + its pose_driven entry; pose_driven_list: base bone, and per channel translate X/Y/Z and
   rotate X/Y/Z (unnamed hashes) either a fixed value or an output (..._id). At rest every curve gives 0, so the
   values are offsets from the bind pose: translate in the base bone's space, rotate as X / Y / Z angles. */
#ifndef HELPER_CORE_H
#define HELPER_CORE_H
#include "aamp_core.h"

typedef struct { int driver, comp, nk; float k[8][4]; } HbCurve;
typedef struct { int bone, base; } HbDriver;
typedef struct { int bone, base; float t[3], r[3]; int tOut[3], rOut[3]; } HbDriven;
typedef struct {
    char bone[200][48]; int nbone; int boneIdx[200];             // names, and their index in the model's skeleton
    HbCurve *c; int nc; int out[400]; int nout;
    HbDriver d[200]; int nd;
    HbDriven *v; int nv;
    int ok;
} HelperBones;
static void HbFree(HelperBones *h) { free(h->c); free(h->v); memset(h, 0, sizeof *h); }

// the name AmParse gives a hash: the known name, or 0x........
static void HbNameOf(const char *s, char *out, size_t n) { uint32_t hsh = AmCrc(s); const char *k = AmLookup(hsh); if (k) snprintf(out, n, "%s", k); else snprintf(out, n, "0x%08x", hsh); }
static int HbIs(const char *have, const char *want) { char w[64]; HbNameOf(want, w, sizeof w); return !strcmp(have, w); }
// "<family>_<n>" in the last path part -> n, or -1
static int HbIndex(const char *objPath, const char *family, int max) {
    const char *s = strrchr(objPath, '/'); s = s ? s + 1 : objPath;
    char w[64];
    // a quick check on known names, then the hashes of family_0..max
    size_t fl = strlen(family);
    if (!strncmp(s, family, fl) && s[fl] == '_') return atoi(s + fl + 1);
    if (strncmp(s, "0x", 2)) return -1;
    for (int i = 0; i < max; i++) { char nm[64]; snprintf(nm, sizeof nm, "%s_%d", family, i); HbNameOf(nm, w, sizeof w); if (!strcmp(s, w)) return i; }
    return -1;
}
static int HbParse(HelperBones *h, const unsigned char *b, size_t n) {
    memset(h, 0, sizeof *h);
    AmFile f; if (!AmParse(b, n, &f)) return 0;
    AmCrcInit();
    // counts from the header
    int nb = 0, nc = 0, no = 0, nd = 0, nv = 0, np = 0;
    for (int i = 0; i < f.np; i++) {
        const AmParam *p = &f.p[i]; if (!strstr(p->obj, "helper_bone_header") && !HbIs(strrchr(p->obj, '/') ? strrchr(p->obj, '/') + 1 : p->obj, "helper_bone_header")) continue;
        int v = (int)p->f[0];
        if (HbIs(p->name, "bone_num")) nb = v; else if (HbIs(p->name, "connection_curve_num")) nc = v; else if (HbIs(p->name, "output_num")) no = v;
        else if (HbIs(p->name, "driver_bone_num")) nd = v; else if (HbIs(p->name, "driven_bone_num")) nv = v; else if (HbIs(p->name, "pose_driven_num")) np = v;
    }
    if (nb > 200) nb = 200; if (nd > 200) nd = 200; if (no > 400) no = 400; if (nc > 400) nc = 400; if (nv > 400) nv = 400; if (np > 400) np = 400;
    h->nbone = nb; h->nc = nc; h->nout = no; h->nd = nd; h->nv = nv;
    h->c = (HbCurve *)calloc((size_t)nc + 1, sizeof(HbCurve));
    HbDriven *pose = (HbDriven *)calloc((size_t)np + 1, sizeof(HbDriven)); int *drvPose = (int *)calloc((size_t)nv + 1, sizeof(int)), *drvBone = (int *)calloc((size_t)nv + 1, sizeof(int));
    if (!h->c || !pose || !drvPose || !drvBone) { free(pose); free(drvPose); free(drvBone); AmFree(&f); HbFree(h); return 0; }
    for (int i = 0; i < np; i++) for (int k = 0; k < 3; k++) { pose[i].tOut[k] = pose[i].rOut[k] = -1; }
    for (int i = 0; i < no; i++) h->out[i] = -1;
    static const char *tN[3] = { "translateX", "translateY", "translateZ" }, *tI[3] = { "translateX_id", "translateY_id", "translateZ_id" };
    static const char *rN[3] = { "0x2eb532ce", "0x12f7552a", "0xe8f86849" }, *rI[3] = { "0xab0b6d26", "0xb78ec8ba", "0x0748e816" };   // rotate X/Y/Z (names not recovered)
    for (int i = 0; i < f.np; i++) {
        const AmParam *p = &f.p[i]; int k;
        if (strstr(p->obj, "bone_list") || 1) {
            if ((k = HbIndex(p->obj, "bone", nb)) >= 0 && !strstr(p->obj, "driver") && !strstr(p->obj, "driven") && HbIs(p->name, "name")) { snprintf(h->bone[k], 48, "%s", p->text); continue; }
        }
        if ((k = HbIndex(p->obj, "connection_curve", nc)) >= 0) {
            HbCurve *C = &h->c[k];
            if (HbIs(p->name, "driver_bone_id")) C->driver = (int)p->f[0];
            else if (!strcmp(p->name, "0x047e2314")) C->comp = (int)p->f[0];
            else { for (int q = 0; q < 8; q++) { char kn[16]; snprintf(kn, sizeof kn, "key_%d", q); if (HbIs(p->name, kn)) { memcpy(C->k[q], p->f, 16); if (q + 1 > C->nk) C->nk = q + 1; } } }
            continue;
        }
        if ((k = HbIndex(p->obj, "output", no)) >= 0) { if (!strcmp(p->name, "0x8e7f3ca5")) h->out[k] = (int)p->f[0]; continue; }
        if ((k = HbIndex(p->obj, "driver_bone", nd)) >= 0) {
            if (HbIs(p->name, "bone_id")) h->d[k].bone = (int)p->f[0]; else if (HbIs(p->name, "base_bone_id")) h->d[k].base = (int)p->f[0];
            continue;
        }
        if ((k = HbIndex(p->obj, "driven_bone", nv)) >= 0) {
            if (HbIs(p->name, "bone_id")) drvBone[k] = (int)p->f[0];
            else if (HbIs(p->name, "translate_driven_id") || HbIs(p->name, "rotate_driven_id")) drvPose[k] = (int)p->f[0];
            continue;
        }
        if ((k = HbIndex(p->obj, "pose_driven", np)) >= 0) {
            HbDriven *P = &pose[k];
            if (HbIs(p->name, "base_bone_id")) P->base = (int)p->f[0];
            for (int a = 0; a < 3; a++) {
                if (HbIs(p->name, tN[a])) P->t[a] = p->f[0]; else if (HbIs(p->name, tI[a])) P->tOut[a] = (int)p->f[0];
                else if (!strcmp(p->name, rN[a])) P->r[a] = p->f[0]; else if (!strcmp(p->name, rI[a])) P->rOut[a] = (int)p->f[0];
            }
            continue;
        }
    }
    h->v = (HbDriven *)calloc((size_t)nv + 1, sizeof(HbDriven));
    for (int i = 0; i < nv && h->v; i++) { int pi = drvPose[i] >= 0 && drvPose[i] < np ? drvPose[i] : i; h->v[i] = pose[pi]; h->v[i].bone = drvBone[i]; }
    free(pose); free(drvPose); free(drvBone); AmFree(&f);
    h->ok = h->v && nb > 0 && nv > 0;
    return h->ok;
}
// the skeleton's bone for each helper-bone name (-1 if the model doesn't have it)
static int HbBind(HelperBones *h, const VoxPart *p) {
    int found = 0;
    for (int i = 0; i < h->nbone; i++) { h->boneIdx[i] = h->bone[i][0] ? BfBoneIndex(p, h->bone[i]) : -1; if (h->boneIdx[i] >= 0) found++; }
    return found;
}
static float HbCurveAt(const HbCurve *C, float x) {
    if (C->nk <= 0) return 0;
    if (x <= C->k[0][0]) return C->k[0][1];
    if (x >= C->k[C->nk - 1][0]) return C->k[C->nk - 1][1];
    int i = 0; while (i + 1 < C->nk && C->k[i + 1][0] < x) i++;
    const float *A = C->k[i], *B = C->k[i + 1]; float d = B[0] - A[0]; if (d <= 0) return A[1];
    float t = (x - A[0]) / d, t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * A[1] + (t3 - 2 * t2 + t) * d * A[3] + (-2 * t3 + 3 * t2) * B[1] + (t3 - t2) * d * B[2];
}
static void Hb3Inv(const float *M, float *I) {          // 3x3 of a 3x4 row-major matrix, inverted (into 9 floats)
    float a = M[0], b = M[1], c = M[2], d = M[4], e = M[5], f = M[6], g = M[8], hh = M[9], i = M[10];
    float A = e * i - f * hh, B = -(d * i - f * g), C = d * hh - e * g, det = a * A + b * B + c * C; if (fabsf(det) < 1e-12f) det = 1e-12f;
    I[0] = A / det; I[1] = -(b * i - c * hh) / det; I[2] = (b * f - c * e) / det;
    I[3] = B / det; I[4] = (a * i - c * g) / det; I[5] = -(a * f - c * d) / det;
    I[6] = C / det; I[7] = -(a * hh - b * g) / det; I[8] = (a * e - b * d) / det;
}
static void Hb34Inv(const float *M, float *O) {         // inverse of a 3x4 affine matrix
    float I[9]; Hb3Inv(M, I);
    for (int r = 0; r < 3; r++) { for (int k = 0; k < 3; k++) O[r * 4 + k] = I[r * 3 + k]; O[r * 4 + 3] = -(I[r * 3] * M[3] + I[r * 3 + 1] * M[7] + I[r * 3 + 2] * M[11]); }
}
static void Hb34Mul(const float *A, const float *B, float *O) {
    float T[12];
    for (int r = 0; r < 3; r++) { for (int k = 0; k < 4; k++) T[r * 4 + k] = A[r * 4] * B[k] + A[r * 4 + 1] * B[4 + k] + A[r * 4 + 2] * B[8 + k] + (k == 3 ? A[r * 4 + 3] : 0); }
    memcpy(O, T, 48);
}
// rotation part only, with any scale taken out (columns normalised)
static void HbRot(const float *M, float R[9]) {
    for (int k = 0; k < 3; k++) { float l = sqrtf(M[k] * M[k] + M[4 + k] * M[4 + k] + M[8 + k] * M[8 + k]); if (l < 1e-9f) l = 1; for (int r = 0; r < 3; r++) R[r * 3 + k] = M[r * 4 + k] / l; }
}
/* move the driven bones for the current pose: skinM / worldM hold the animated pose (12 floats per bone) */
static void HbApply(const HelperBones *h, const VoxPart *p, float *skinM, float *worldM) {
    if (!h->ok || !worldM) return;
    float (*ang)[3] = (float (*)[3])calloc((size_t)h->nd + 1, sizeof(float[3])); if (!ang) return;
    for (int i = 0; i < h->nd; i++) {                      // each driver's rotation away from the bind pose, as X / Y / Z angles
        int b = h->d[i].bone >= 0 && h->d[i].bone < h->nbone ? h->boneIdx[h->d[i].bone] : -1, a = h->d[i].base >= 0 && h->d[i].base < h->nbone ? h->boneIdx[h->d[i].base] : -1;
        if (b < 0 || a < 0) continue;
        float Ra[9], Rb[9], Ba[9], Bb[9], cur[9], bind[9], D[9];
        HbRot(worldM + (size_t)a * 12, Ra); HbRot(worldM + (size_t)b * 12, Rb); HbRot(p->skBindW + (size_t)a * 12, Ba); HbRot(p->skBindW + (size_t)b * 12, Bb);
        for (int r = 0; r < 3; r++) for (int k = 0; k < 3; k++) { cur[r * 3 + k] = Ra[r] * Rb[k] + Ra[3 + r] * Rb[3 + k] + Ra[6 + r] * Rb[6 + k]; bind[r * 3 + k] = Ba[r] * Bb[k] + Ba[3 + r] * Bb[3 + k] + Ba[6 + r] * Bb[6 + k]; }
        for (int r = 0; r < 3; r++) for (int k = 0; k < 3; k++) D[r * 3 + k] = bind[r] * cur[k] + bind[3 + r] * cur[3 + k] + bind[6 + r] * cur[6 + k];   // bind^T * cur
        // swing-twist about the bone's own axis (X, the aim axis): X = the twist, Y / Z = the swing's rotation-vector parts
        float w = sqrtf(fmaxf(0.f, 1 + D[0] + D[4] + D[8])) / 2, qx, qy, qz;
        if (w > 1e-4f) { qx = (D[7] - D[5]) / (4 * w); qy = (D[2] - D[6]) / (4 * w); qz = (D[3] - D[1]) / (4 * w); }
        else { qx = sqrtf(fmaxf(0.f, 1 + D[0] - D[4] - D[8])) / 2; qy = (D[1] + D[3]) / (4 * (qx > 1e-4f ? qx : 1e-4f)); qz = (D[2] + D[6]) / (4 * (qx > 1e-4f ? qx : 1e-4f)); }
        float tl = sqrtf(w * w + qx * qx); float tw = tl > 1e-6f ? w / tl : 1, tx = tl > 1e-6f ? qx / tl : 0;   // twist = (tw, tx, 0, 0)
        ang[i][0] = 2 * atan2f(tx, tw); if (ang[i][0] > 3.14159265f) ang[i][0] -= 6.2831853f; if (ang[i][0] < -3.14159265f) ang[i][0] += 6.2831853f;
        // swing = q * conj(twist)
        float sw = w * tw + qx * tx, sx2 = qx * tw - w * tx, sy2 = qy * tw + qz * tx, sz2 = qz * tw - qy * tx; (void)sx2;
        float sl = sqrtf(sy2 * sy2 + sz2 * sz2), sa = 2 * atan2f(sl, sw);
        ang[i][1] = sl > 1e-7f ? sy2 / sl * sa : 0; ang[i][2] = sl > 1e-7f ? sz2 / sl * sa : 0;
    }
    for (int i = 0; i < h->nv; i++) {
        const HbDriven *V = &h->v[i];
        int d = V->bone >= 0 && V->bone < h->nbone ? h->boneIdx[V->bone] : -1, a = V->base >= 0 && V->base < h->nbone ? h->boneIdx[V->base] : -1;
        if (d < 0 || a < 0) continue;
        float t[3], r[3];
        for (int k = 0; k < 3; k++) {
            t[k] = V->t[k]; r[k] = V->r[k];
            if (V->tOut[k] >= 0 && V->tOut[k] < h->nout) { int c = h->out[V->tOut[k]]; if (c >= 0 && c < h->nc && h->c[c].driver < h->nd) t[k] = HbCurveAt(&h->c[c], ang[h->c[c].driver][h->c[c].comp % 3]); }
            if (V->rOut[k] >= 0 && V->rOut[k] < h->nout) { int c = h->out[V->rOut[k]]; if (c >= 0 && c < h->nc && h->c[c].driver < h->nd) r[k] = HbCurveAt(&h->c[c], ang[h->c[c].driver][h->c[c].comp % 3]); }
        }
        // local (in the base bone) = bind local, turned by r and moved by t
        float iA[12], L[12], E[12], W[12], iB[12];
        Hb34Inv(p->skBindW + (size_t)a * 12, iA); Hb34Mul(iA, p->skBindW + (size_t)d * 12, L);
        float cx = cosf(r[0]), sx = sinf(r[0]), cy = cosf(r[1]), syy = sinf(r[1]), cz = cosf(r[2]), sz = sinf(r[2]);
        float Rr[9] = { cz * cy, cz * syy * sx - sz * cx, cz * syy * cx + sz * sx,  sz * cy, sz * syy * sx + cz * cx, sz * syy * cx - cz * sx,  -syy, cy * sx, cy * cx };
        for (int rr = 0; rr < 3; rr++) { for (int k = 0; k < 3; k++) E[rr * 4 + k] = Rr[rr * 3] * L[k] + Rr[rr * 3 + 1] * L[4 + k] + Rr[rr * 3 + 2] * L[8 + k]; E[rr * 4 + 3] = L[rr * 4 + 3] + t[rr]; }
        Hb34Mul(worldM + (size_t)a * 12, E, W);
        memcpy(worldM + (size_t)d * 12, W, 48);
        Hb34Inv(p->skBindW + (size_t)d * 12, iB); Hb34Mul(W, iB, skinM + (size_t)d * 12);
    }
    free(ang);
}
#endif
