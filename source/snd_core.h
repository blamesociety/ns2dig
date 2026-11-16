// snd_core.h - Nintendo BWAV / BARS parsing and decoding (portable C, no Windows headers).
//
// BWAV:  "BWAV", BOM FEFF, u16 version, u32 crc, u16 prefetch, u16 channel count at 0x0E,
//        then per channel 0x4C bytes from 0x10:
//          u16 codec (0 PCM16, 1 DSP-ADPCM), u16 pan, u32 rate, u32 samples, u32 samples (prefetch),
//          16 x s16 DSP coefficients, u32 data offset, u32 data offset (prefetch),
//          u32 loop flag, u32 loop end (FFFFFFFF = no loop), u32 loop start, u16 ps, s16 hist1, s16 hist2
//        Offsets are from the start of the BWAV. u16 0x0C = 1 with a smaller prefetch sample count means only
//        the start is stored (Mario Kart World BARS); the full sound is a separate stream BWAV.
// BARS:  "BARS", u32 size, BOM, u16 version, u32 count, count x u32 name hashes,
//        then count x (u32 AMTA offset, u32 BWAV offset). AMTA + 0x24: u32 offset (from that field) of the name.
#ifndef SND_CORE_H
#define SND_CORE_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define SND_MAX_CH 8
typedef struct {
    char name[128];
    size_t base;                  // offset of the BWAV in the file
    int nch, codec, rate;
    uint32_t samples, loopStart, loopEnd;   // loopEnd = 0 when the sound doesn't loop
    uint32_t stored;              // samples actually stored here (less than samples for a prefetch-only BWAV)
    int prefetch;                 // 1 = only the start is stored; the rest is in a stream file (Stream\<name>.bwav)
    int ok;
} SndEntry;

typedef struct {
    unsigned char *data; size_t size;
    SndEntry *e; int n;
    char err[200];
    int isBars;
} SndFile;

static uint32_t SndU32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t SndU16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static int SndParseBwav(const unsigned char *d, size_t size, size_t base, SndEntry *e) {
    if (base + 0x10 > size || memcmp(d + base, "BWAV", 4)) return 0;
    int nch = SndU16(d + base + 0x0E);
    if (nch < 1 || nch > SND_MAX_CH || base + 0x10 + (size_t)nch * 0x4C > size) return 0;
    const unsigned char *c = d + base + 0x10;
    e->base = base; e->nch = nch;
    e->codec = SndU16(c); e->rate = (int)SndU32(c + 4); e->samples = SndU32(c + 8);
    uint32_t le = SndU32(c + 0x3C), ls = SndU32(c + 0x40);
    e->loopEnd = (le == 0xFFFFFFFFu || le > e->samples || ls >= le) ? 0 : le;
    e->loopStart = e->loopEnd ? ls : 0;
    if (e->codec != 0 && e->codec != 1) return 0;
    if (e->rate < 1000 || e->rate > 192000 || !e->samples) return 0;
    // prefetch-only (MKW BARS): u16 at 0x0C = 1 and the channel's prefetch sample count is smaller
    uint32_t ps = SndU32(c + 0x0C);
    e->prefetch = (SndU16(d + base + 0x0C) == 1 && ps > 0 && ps < e->samples);
    e->stored = e->prefetch ? ps : e->samples;
    for (int ch = 0; ch < nch; ch++) {           // every channel's data must fit
        const unsigned char *cc = d + base + 0x10 + ch * 0x4C;
        uint32_t off = SndU32(cc + 0x30);
        // DSP: 8-byte frames of 14 samples; the last frame is stored only as far as it is used
        size_t rem = e->stored % 14;
        size_t need = e->codec == 1 ? (size_t)(e->stored / 14) * 8 + (rem ? 1 + (rem + 1) / 2 : 0) : (size_t)e->stored * 2;
        if (base + off + need > size) return 0;
    }
    e->ok = 1;
    return 1;
}

// takes ownership of data (malloc'd)
static int SndOpenData(SndFile *f, unsigned char *data, size_t size) {
    memset(f, 0, sizeof(*f));
    f->data = data; f->size = size;
    if (size >= 4 && !memcmp(data, "BWAV", 4)) {
        f->e = (SndEntry *)calloc(1, sizeof(SndEntry)); f->n = 1;
        if (!SndParseBwav(data, size, 0, &f->e[0])) { snprintf(f->err, sizeof f->err, "This BWAV file uses a layout or encoding the player doesn't know."); return 0; }
        snprintf(f->e[0].name, sizeof f->e[0].name, "(the whole file)");
        return 1;
    }
    if (size >= 0x10 && !memcmp(data, "BARS", 4)) {
        f->isBars = 1;
        uint32_t n = SndU32(data + 0x0C);
        if (n == 0 || n > 100000 || 0x10 + (size_t)n * 12 > size) { snprintf(f->err, sizeof f->err, "This BARS file has an odd header."); return 0; }
        f->e = (SndEntry *)calloc(n, sizeof(SndEntry)); f->n = (int)n;
        if (!f->e) { snprintf(f->err, sizeof f->err, "Out of memory."); return 0; }
        size_t tab = 0x10 + (size_t)n * 4;
        int good = 0;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t amta = SndU32(data + tab + i * 8), bw = SndU32(data + tab + i * 8 + 4);
            SndEntry *e = &f->e[i];
            snprintf(e->name, sizeof e->name, "sound %u", i);
            if (amta + 0x28 <= size && !memcmp(data + amta, "AMTA", 4)) {
                size_t no = amta + 0x24 + SndU32(data + amta + 0x24);
                if (no < size) {
                    size_t k = 0;
                    while (no + k < size && data[no + k] && k < sizeof e->name - 1) { char ch = (char)data[no + k]; e->name[k] = (ch >= 32 && ch < 127) ? ch : '?'; k++; }
                    if (k) e->name[k] = 0;
                }
            }
            if (SndParseBwav(data, size, bw, e)) good++;
        }
        if (!good) { snprintf(f->err, sizeof f->err, "None of the %u sounds in this BARS file could be read.", n); return 0; }
        return 1;
    }
    snprintf(f->err, sizeof f->err, "Not a BWAV or BARS sound file.");
    return 0;
}

static void SndClose(SndFile *f) { free(f->data); free(f->e); memset(f, 0, sizeof(*f)); }

// decode one channel to s16
static void SndDecodeChannel(const SndFile *f, const SndEntry *e, int ch, int16_t *out) {
    const unsigned char *c = f->data + e->base + 0x10 + ch * 0x4C;
    const unsigned char *src = f->data + e->base + SndU32(c + 0x30);
    uint32_t ns = e->samples;
    if (e->codec == 0) { for (uint32_t i = 0; i < ns; i++) out[i] = (int16_t)SndU16(src + i * 2); return; }
    int16_t coef[16];
    for (int k = 0; k < 16; k++) coef[k] = (int16_t)SndU16(c + 0x10 + k * 2);
    int h1 = 0, h2 = 0;
    uint32_t i = 0;
    for (size_t fr = 0; i < ns; fr++) {
        const unsigned char *p = src + fr * 8;
        int pred = (p[0] >> 4) & 7, scale = 1 << (p[0] & 15);
        int c1 = coef[pred * 2], c2 = coef[pred * 2 + 1];
        for (int k = 0; k < 14 && i < ns; k++, i++) {
            int nib = (k & 1) ? (p[1 + k / 2] & 15) : (p[1 + k / 2] >> 4);
            if (nib >= 8) nib -= 16;
            int s = ((nib * scale) << 11) + 1024 + c1 * h1 + c2 * h2;
            s >>= 11;
            if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
            out[i] = (int16_t)s; h2 = h1; h1 = s;
        }
    }
}

// decode a whole entry to interleaved s16 with 1 or 2 output channels (more channels are mixed to stereo)
static int16_t *SndDecode(const SndFile *f, const SndEntry *e, int *outCh) {
    int oc = e->nch == 1 ? 1 : 2;
    size_t ns = e->samples;
    int16_t *pcm = (int16_t *)malloc(ns * oc * sizeof(int16_t));
    int16_t *tmp = (int16_t *)malloc(ns * sizeof(int16_t));
    int32_t *acc = e->nch > 2 ? (int32_t *)calloc(ns * 2, sizeof(int32_t)) : NULL;
    if (!pcm || !tmp || (e->nch > 2 && !acc)) { free(pcm); free(tmp); free(acc); return NULL; }
    for (int ch = 0; ch < e->nch; ch++) {
        SndDecodeChannel(f, e, ch, tmp);
        if (e->nch <= 2) { for (size_t i = 0; i < ns; i++) pcm[i * oc + ch] = tmp[i]; }
        else { int side = ch & 1; for (size_t i = 0; i < ns; i++) acc[i * 2 + side] += tmp[i]; }
    }
    if (acc) {
        int per = (e->nch + 1) / 2;
        for (size_t i = 0; i < ns * 2; i++) { int v = acc[i] / per; pcm[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
    }
    free(tmp); free(acc);
    *outCh = oc;
    return pcm;
}
#endif
