/* small deflate (fixed Huffman + LZ77) and CRC-32, shared copy of the ZIP writer's - used for PNG files */
#ifndef DEFLATE_LITE_H
#define DEFLATE_LITE_H
static DWORD g_crcTab[256];
static void CrcInit(void) {
    for (DWORD i = 0; i < 256; i++) { DWORD c = i; for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; g_crcTab[i] = c; }
}
static DWORD Crc32(const unsigned char *p, size_t n) {
    DWORD c = 0xFFFFFFFFu; while (n--) c = g_crcTab[(c ^ *p++) & 0xFF] ^ (c >> 8); return c ^ 0xFFFFFFFFu;
}

typedef struct { unsigned char *p; size_t n, cap; unsigned long long bb; int bits; BOOL fail; } BitOut;
static void BoGrow(BitOut *o, size_t need) {
    if (o->fail || o->n + need <= o->cap) return;
    size_t nc = o->cap ? o->cap * 2 : 65536; while (nc < o->n + need) nc *= 2;
    unsigned char *np = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, nc);
    if (!np) { o->fail = TRUE; return; }
    if (o->p) { memcpy(np, o->p, o->n); xfree(o->p); }
    o->p = np; o->cap = nc;
}
static void PutBits(BitOut *o, unsigned v, int n) {
    o->bb |= (unsigned long long)v << o->bits; o->bits += n;
    while (o->bits >= 8) {
        if (o->n >= o->cap) { BoGrow(o, 1); if (o->fail) { o->bits = 0; o->bb = 0; return; } }
        o->p[o->n++] = (unsigned char)o->bb; o->bb >>= 8; o->bits -= 8;
    }
}
static unsigned RevBits(unsigned v, int n) { unsigned r = 0; while (n--) { r = (r << 1) | (v & 1); v >>= 1; } return r; }
static void PutLit(BitOut *o, int v) {            // fixed Huffman literal/length codes
    if (v < 144) PutBits(o, RevBits(0x30 + v, 8), 8);
    else if (v < 256) PutBits(o, RevBits(0x190 + v - 144, 9), 9);
    else if (v < 280) PutBits(o, RevBits(v - 256, 7), 7);
    else PutBits(o, RevBits(0xC0 + v - 280, 8), 8);
}
static const unsigned short kLBase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const unsigned char  kLExt[29]  = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const unsigned short kDBase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const unsigned char  kDExt[30]  = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
static void PutMatch(BitOut *o, int len, int dist) {
    int i = 28; while (kLBase[i] > len) i--;
    PutLit(o, 257 + i); if (kLExt[i]) PutBits(o, (unsigned)(len - kLBase[i]), kLExt[i]);
    int j = 29; while (kDBase[j] > dist) j--;
    PutBits(o, RevBits((unsigned)j, 5), 5); if (kDExt[j]) PutBits(o, (unsigned)(dist - kDBase[j]), kDExt[j]);
}

#define ZHBITS 16
// raw deflate (one fixed-Huffman block, greedy LZ77 with hash chains); NULL if out of memory
static unsigned char *DeflateBuf(const unsigned char *in, size_t n, size_t *outn) {
    BitOut o = { 0 };
    BoGrow(&o, n / 3 + 4096);
    int *head = (int *)HeapAlloc(GetProcessHeap(), 0, sizeof(int) << ZHBITS);
    int *prev = (int *)HeapAlloc(GetProcessHeap(), 0, sizeof(int) * 32768);
    if (!head || !prev || o.fail) { xfree(head); xfree(prev); xfree(o.p); return NULL; }
    for (int i = 0; i < (1 << ZHBITS); i++) head[i] = -1;
    PutBits(&o, 1, 1); PutBits(&o, 1, 2);               // BFINAL = 1, BTYPE = 01 (fixed Huffman)
    size_t i = 0;
    #define ZHASH(p) ((((unsigned)in[p] << 11) ^ ((unsigned)in[(p) + 1] << 5) ^ in[(p) + 2] ^ ((unsigned)in[(p) + 1] >> 3)) & ((1u << ZHBITS) - 1))
    while (i < n && !o.fail) {
        size_t best = 0, bdist = 0;
        if (i + 2 < n) {
            unsigned h = ZHASH(i);
            int cand = head[h], chain = 64;
            size_t maxl = n - i < 258 ? n - i : 258;
            while (cand >= 0 && chain--) {
                size_t d = i - (size_t)cand;
                if (d > 32768) break;
                if (in[cand + best] == in[i + best] && in[cand] == in[i]) {
                    size_t l = 0; while (l < maxl && in[cand + l] == in[i + l]) l++;
                    if (l > best) { best = l; bdist = d; if (l == maxl) break; }
                }
                int pc = prev[cand & 32767];
                if (pc >= cand) break;                   // slot reused by a newer position
                cand = pc;
            }
            prev[i & 32767] = head[h]; head[h] = (int)i;
        }
        if (best >= 3) {
            PutMatch(&o, (int)best, (int)bdist);
            for (size_t q = 1; q < best; q++) { size_t j = i + q; if (j + 2 < n) { unsigned h = ZHASH(j); prev[j & 32767] = head[h]; head[h] = (int)j; } }
            i += best;
        } else { PutLit(&o, in[i]); i++; }
    }
    #undef ZHASH
    PutLit(&o, 256);
    if (o.bits) PutBits(&o, 0, 8 - o.bits);
    xfree(head); xfree(prev);
    if (o.fail) { xfree(o.p); return NULL; }
    *outn = o.n;
    return o.p;
}
#endif
