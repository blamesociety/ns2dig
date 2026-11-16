/* dspadpcm_enc - raw s16le mono PCM -> Nintendo DSP-ADPCM (for the ns2dig demo's music).
   usage: dspadpcm_enc in.raw out.dsp      out: 16 x s16 coefficients (LE), then the 8-byte frames (14 samples each).
   Coefficients: each 14-sample block's least-squares predictor, grouped into 8 by k-means. Frames: every predictor
   and scale tried, encoded exactly as the decoder rebuilds them (history = decoded samples), least error kept. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
static int clamp16(int s) { return s > 32767 ? 32767 : s < -32768 ? -32768 : s; }
int main(int argc, char **argv) {
    if (argc < 3) return 1;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 1;
    fseek(f, 0, 2); long nb = ftell(f); fseek(f, 0, 0);
    size_t n = nb / 2; int16_t *x = malloc(nb + 64); fread(x, 2, n, f); fclose(f);
    size_t nf = (n + 13) / 14;
    /* per-block least-squares predictor (x[i] ~ a x[i-1] + b x[i-2]) */
    double *A = malloc(nf * sizeof(double)), *B = malloc(nf * sizeof(double)), *Wt = malloc(nf * sizeof(double));
    for (size_t k = 0; k < nf; k++) {
        double r11 = 0, r12 = 0, r22 = 0, r01 = 0, r02 = 0;
        for (int j = 0; j < 14; j++) {
            long i = (long)k * 14 + j; if (i >= (long)n) break;
            double x0 = x[i], x1 = i > 0 ? x[i - 1] : 0, x2 = i > 1 ? x[i - 2] : 0;
            r11 += x1 * x1; r12 += x1 * x2; r22 += x2 * x2; r01 += x0 * x1; r02 += x0 * x2;
        }
        double det = r11 * r22 - r12 * r12; double a = 0, b = 0;
        if (fabs(det) > 1e-3) { a = (r01 * r22 - r02 * r12) / det; b = (r02 * r11 - r01 * r12) / det; }
        if (a > 3.9) a = 3.9; if (a < -3.9) a = -3.9; if (b > 3.9) b = 3.9; if (b < -3.9) b = -3.9;
        A[k] = a; B[k] = b; Wt[k] = sqrt(r11 + 1);
    }
    /* k-means into 8 (start: spread presets) */
    double ca[8] = { 0, 1, 2, 1.9, 1.5, 1.7, 0.9, 1.2 }, cb[8] = { 0, 0, -1, -0.95, -0.6, -0.8, -0.1, -0.4 };
    for (int it = 0; it < 30; it++) {
        double sa[8] = { 0 }, sb[8] = { 0 }, sw[8] = { 0 };
        for (size_t k = 0; k < nf; k++) {
            int best = 0; double bd = 1e30;
            for (int c = 1; c < 8; c++) { double d = (A[k] - ca[c]) * (A[k] - ca[c]) + (B[k] - cb[c]) * (B[k] - cb[c]); if (d < bd) { bd = d; best = c; } }
            sa[best] += A[k] * Wt[k]; sb[best] += B[k] * Wt[k]; sw[best] += Wt[k];
        }
        for (int c = 1; c < 8; c++) if (sw[c] > 0) { ca[c] = sa[c] / sw[c]; cb[c] = sb[c] / sw[c]; }
    }
    int16_t coef[16];
    for (int c = 0; c < 8; c++) {                       /* predictor 0 stays 0, 0 (silence / noise) */
        double a = c ? ca[c] : 0, b = c ? cb[c] : 0;
        int ia = (int)lround(a * 2048), ib = (int)lround(b * 2048);
        coef[c * 2] = (int16_t)(ia > 32767 ? 32767 : ia < -32768 ? -32768 : ia);
        coef[c * 2 + 1] = (int16_t)(ib > 32767 ? 32767 : ib < -32768 ? -32768 : ib);
    }
    FILE *o = fopen(argv[2], "wb"); fwrite(coef, 2, 16, o);
    int h1 = 0, h2 = 0; double toterr = 0, totsig = 0;
    for (size_t k = 0; k < nf; k++) {
        unsigned char bestF[8]; double bestE = 1e300; int bh1 = 0, bh2 = 0;
        for (int p = 0; p < 8; p++) {
            int c1 = coef[p * 2], c2 = coef[p * 2 + 1];
            for (int sc = 0; sc < 12; sc++) {
                int scale = 1 << sc, g1 = h1, g2 = h2; double e = 0; unsigned char fr[8] = { (unsigned char)((p << 4) | sc) };
                for (int j = 0; j < 14; j++) {
                    long i = (long)k * 14 + j; int tgt = i < (long)n ? x[i] : 0;
                    int base = 1024 + c1 * g1 + c2 * g2;
                    /* nib with ((nib*scale)<<11) + base >> 11 closest to tgt */
                    double want = ((double)tgt * 2048 - base) / (2048.0 * scale);
                    int nib = (int)lround(want); if (nib > 7) nib = 7; if (nib < -8) nib = -8;
                    int bestS = 0; double bestD = 1e30; int bestN = nib;
                    for (int d = -1; d <= 1; d++) { int nn = nib + d; if (nn > 7 || nn < -8) continue;
                        int s = clamp16((((nn * scale) << 11) + base) >> 11); double dd = (double)(s - tgt) * (s - tgt);
                        if (dd < bestD) { bestD = dd; bestS = s; bestN = nn; } }
                    e += bestD; if (e >= bestE) break;
                    fr[1 + j / 2] |= (unsigned char)((bestN & 15) << ((j & 1) ? 0 : 4));
                    g2 = g1; g1 = bestS;
                }
                if (e < bestE) { bestE = e; memcpy(bestF, fr, 8); bh1 = g1; bh2 = g2; }
            }
        }
        fwrite(bestF, 1, 8, o); h1 = bh1; h2 = bh2; toterr += bestE;
        for (int j = 0; j < 14; j++) { long i = (long)k * 14 + j; if (i < (long)n) totsig += (double)x[i] * x[i]; }
    }
    fclose(o);
    fprintf(stderr, "%zu samples, %zu frames, SNR %.1f dB\n", n, nf, 10 * log10(totsig / (toterr + 1)));
    return 0;
}
