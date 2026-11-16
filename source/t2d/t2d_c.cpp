// C entry point for the texture decoders (texture2ddecoder, MIT licence - see LICENSE_texture2ddecoder)
#include <stdint.h>
#include "astc.h"
#include "bcn.h"
extern "C" int T2D_Decode(int kind, const uint8_t *data, int w, int h, int bw, int bh, uint32_t *out) {
    switch (kind) {
    case 1: return decode_bc1(data, w, h, out);
    case 3: return decode_bc3(data, w, h, out);
    case 4: return decode_bc4(data, (uint32_t)w, (uint32_t)h, out);
    case 5: return decode_bc5(data, (uint32_t)w, (uint32_t)h, out);
    case 6: return decode_bc6(data, (uint32_t)w, (uint32_t)h, out);
    case 7: return decode_bc7(data, (uint32_t)w, (uint32_t)h, out);
    case 100: return decode_astc(data, w, h, bw, bh, out);
    }
    return 0;
}
