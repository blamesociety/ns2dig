"""demo_tex.py - the ns2dig demo's texture-format test set (used by make_demo.py):

  Tex/DemoFormats.bntx.zs      Switch 2 style ("Ounc" block-linear tiling): one test picture in many GPU formats -
                               BC1, BC1 sRGB, BC3, BC4, BC5, BC7, BC7 sRGB, ASTC 4x4 / 5x5 / 6x6 / 8x8 / 10x10 / 12x12,
                               ASTC 8x8 sRGB, RGBA8 sRGB, R8, RG8, RGBA16F - every one with its full mipmap chain,
                               plus a 6-layer cube map
  Tex/DemoFormats_NX.bntx.zs   the same picture in Switch 1 style ("NX" block-linear tiling): RGBA8, BC1, BC7, ASTC 4x4

The compressed ones need the Python packages etcpak (BC) and astc-encoder-py (ASTC); when they are missing,
make_demo.py copies the two files made earlier (media/ beside the script, or the demo folder's own Tex/).
Tiling: each mip level is cut into GOBs (64 bytes x 8 rows), stacked 2^bhl GOBs high; Switch 2 swaps two address
bits inside a GOB (see TexUntile in ns2dig's tex_core.h, which this is the reverse of).
"""
import math, os, struct
import numpy as np


# ---------------------------------------------------------------- the test picture
def picture(n, label, seed=0):
    """a colourful n x n RGBA test card: hue sweep, a checker band, grey ramp, a see-through circle, the label"""
    from PIL import Image, ImageDraw, ImageFont
    y, x = np.mgrid[0:n, 0:n] / (n - 1)
    h = (x + seed * 0.13) % 1.0
    r = np.clip(np.abs(h * 6 - 3) - 1, 0, 1); g = np.clip(2 - np.abs(h * 6 - 2), 0, 1); b = np.clip(2 - np.abs(h * 6 - 4), 0, 1)
    v = 0.35 + 0.65 * (1 - y)
    img = np.stack([r * v, g * v, b * v], axis=2)
    band = (y > 0.62) & (y < 0.74)
    chk = ((np.floor(x * 16) + np.floor(y * 16)) % 2)[..., None]
    img = np.where(band[..., None], chk * 0.95 + 0.05, img)
    ramp = (y >= 0.74) & (y < 0.82)
    img = np.where(ramp[..., None], x[..., None], img)
    a = np.ones((n, n))
    d = np.sqrt((x - 0.78) ** 2 + (y - 0.25) ** 2)
    a = np.where(d < 0.16, np.clip((d - 0.06) / 0.1, 0.0, 1.0), a)       # a soft hole: alpha 0 in the middle
    im = Image.fromarray((np.dstack([img, a]) * 255).astype(np.uint8), "RGBA")
    dr = ImageDraw.Draw(im)
    try: font = ImageFont.truetype("DejaVuSans-Bold.ttf", max(10, n // 9))
    except OSError: font = ImageFont.load_default()
    dr.rectangle([0, int(n * 0.84), n, n], fill=(20, 20, 30, 255))
    dr.text((n * 0.04, n * 0.855), label, fill=(255, 255, 255, 255), font=font)
    return np.asarray(im)


def mips(img):
    out = [img]
    while out[-1].shape[0] > 1 or out[-1].shape[1] > 1:
        p = out[-1].astype(np.float32); h, w = p.shape[:2]
        h2, w2 = max(1, h // 2), max(1, w // 2)
        p = p[:h2 * 2 if h > 1 else 1, :w2 * 2 if w > 1 else 1]
        if h > 1: p = (p[0::2] + p[1::2]) / 2
        if w > 1: p = (p[:, 0::2] + p[:, 1::2]) / 2
        out.append((p + 0.5).astype(np.uint8))
    return out


# ---------------------------------------------------------------- formats: (BNTX format code, block w, h, bytes per block, encoder)
def _bc(fn):
    import etcpak
    f = getattr(etcpak, fn)
    def enc(img):
        h, w = img.shape[:2]; pw, ph = (w + 3) // 4 * 4, (h + 3) // 4 * 4
        p = np.zeros((ph, pw, 4), np.uint8); p[:h, :w] = img
        if h < ph: p[h:, :w] = img[-1:]
        if w < pw: p[:, w:] = p[:, w - 1:w]
        return f(p.tobytes(), pw, ph)
    return enc


def _astc(bw, bh, srgb=False):
    import astc_encoder as a
    def enc(img):
        h, w = img.shape[:2]
        ctx = a.ASTCContext(a.ASTCConfig(a.ASTCProfile.LDR_SRGB if srgb else a.ASTCProfile.LDR, bw, bh, 1, 60))
        return ctx.compress(a.ASTCImage(a.ASTCType.U8, w, h, 1, np.ascontiguousarray(img).tobytes()), a.ASTCSwizzle())
    return enc


def _plain(kind):
    def enc(img):
        if kind == "R8": return img[..., 0].tobytes()
        if kind == "RG8": return img[..., :2].tobytes()
        if kind == "RGBA16F": return (img.astype(np.float32) / 255).astype(np.float16).tobytes()
        return img.tobytes()
    return enc


ASTC_CODES = {(4, 4): 0x2d, (5, 4): 0x2e, (5, 5): 0x2f, (6, 5): 0x30, (6, 6): 0x31, (8, 5): 0x32, (8, 6): 0x33, (8, 8): 0x34,
              (10, 5): 0x35, (10, 6): 0x36, (10, 8): 0x37, (10, 10): 0x38, (12, 10): 0x39, (12, 12): 0x3a}


def formats():
    """[(texture name, label, format code, block w, block h, bytes per block, encoder)]"""
    F = [("Fmt_RGBA8_sRGB", "RGBA8 sRGB", 0x0b06, 1, 1, 4, _plain("RGBA8")),
         ("Fmt_R8", "R8", 0x0201, 1, 1, 1, _plain("R8")),
         ("Fmt_RG8", "RG8", 0x0901, 1, 1, 2, _plain("RG8")),
         ("Fmt_RGBA16F", "RGBA16F", 0x1505, 1, 1, 8, _plain("RGBA16F")),
         ("Fmt_BC1", "BC1", 0x1a01, 4, 4, 8, _bc("compress_bc1")),
         ("Fmt_BC1_sRGB", "BC1 sRGB", 0x1a06, 4, 4, 8, _bc("compress_bc1")),
         ("Fmt_BC3", "BC3", 0x1c01, 4, 4, 16, _bc("compress_bc3")),
         ("Fmt_BC4", "BC4", 0x1d01, 4, 4, 8, _bc("compress_bc4")),
         ("Fmt_BC5", "BC5", 0x1e01, 4, 4, 16, _bc("compress_bc5")),
         ("Fmt_BC7", "BC7", 0x2001, 4, 4, 16, _bc("compress_bc7")),
         ("Fmt_BC7_sRGB", "BC7 sRGB", 0x2006, 4, 4, 16, _bc("compress_bc7"))]
    for bw, bh in ((4, 4), (5, 5), (6, 6), (8, 8), (10, 10), (12, 12)):
        F.append(("Fmt_ASTC_%dx%d" % (bw, bh), "ASTC %dx%d" % (bw, bh), ASTC_CODES[(bw, bh)] << 8 | 1, bw, bh, 16, _astc(bw, bh)))
    F.append(("Fmt_ASTC_8x8_sRGB", "ASTC 8x8 sRGB", ASTC_CODES[(8, 8)] << 8 | 6, 8, 8, 16, _astc(8, 8, True)))
    return F


# ---------------------------------------------------------------- block-linear tiling (the reverse of the app's TexUntile)
def _gob_bhl(hb):
    g = 16
    while g > 1 and hb <= (g // 2) * 8: g //= 2
    return int(math.log2(g))


def tile(lin, wb, hb, bpe, bhl, ounce):
    """linear blocks (hb rows of wb blocks of bpe bytes) -> block-linear bytes"""
    gobh = 1 << bhl; wg = (wb * bpe + 63) // 64
    rows = (hb + 8 * gobh - 1) // (8 * gobh) * 8 * gobh
    out = bytearray(rows // 8 * wg * 512 if rows else 512)
    src = np.frombuffer(lin, np.uint8).reshape(hb, wb * bpe)
    for y in range(hb):
        rowbase = (y // (8 * gobh)) * (512 * gobh * wg) + ((y % (8 * gobh)) // 8) * 512
        yy = y & 7
        for x in range(wb):
            xb = x * bpe; a = rowbase + (xb // 64) * (512 * gobh); xx = xb & 63
            if ounce: inn = (xx & 15) | ((yy & 1) << 4) | (((yy >> 1) & 1) << 5) | (((xx >> 4) & 1) << 6) | (((yy >> 2) & 1) << 7) | (((xx >> 5) & 1) << 8)
            else: inn = (xx & 15) | ((yy & 1) << 4) | (((xx >> 4) & 1) << 5) | (((yy >> 1) & 3) << 6) | (((xx >> 5) & 1) << 8)
            out[a + inn:a + inn + bpe] = src[y, xb:xb + bpe].tobytes()
    return bytes(out)


def texture(name, img_levels, fmt, bw, bh, bpe, enc, ounce, layers=1):
    """-> (name, fmt, w, h, layers, mips, bhl, data, mip offsets (relative)) with every level tiled"""
    h, w = img_levels[0][0].shape[:2]
    bhl0 = _gob_bhl((h + bh - 1) // bh)
    layer_data = []; offs = []
    for L in range(layers):                                   # layer after layer, each with all its mip levels
        data = bytearray()
        for lv in range(len(img_levels[0])):
            lh, lw = img_levels[L][lv].shape[:2]; wb, hb = (lw + bw - 1) // bw, (lh + bh - 1) // bh
            bhl = min(bhl0, _gob_bhl(hb))
            while len(data) % 512: data.append(0)
            if L == 0: offs.append(len(data))
            lin = enc(img_levels[L][lv]); assert len(lin) == wb * hb * bpe, (name, lv, len(lin), wb * hb * bpe)
            data += tile(lin, wb, hb, bpe, bhl, ounce)
        while len(data) % 512: data.append(0)
        layer_data.append(bytes(data))
    return name, fmt, w, h, layers, len(img_levels[0]), bhl0, b"".join(layer_data), offs


def bntx(textures, ounce):
    """BNTX with tiled textures (tile mode 0), mipmaps and layers"""
    n = len(textures)
    tab = 0x40; brti0 = (tab + 8 * n + 0xF) & ~0xF; strs = brti0 + 0xA0 * n
    names = bytearray(); name_at = []
    for t in textures:
        name_at.append(strs + len(names)); e = t[0].encode(); names += struct.pack("<H", len(e)) + e + b"\0"
        while len(names) % 4: names.append(0)
    mip_tabs = (strs + len(names) + 0xF) & ~0xF
    mt = []; p = mip_tabs
    for t in textures: mt.append(p); p += 8 * t[5]
    data0 = (p + 0xFFF) & ~0xFFF
    b = bytearray(data0); b[0:4] = b"BNTX"; b[0x20:0x24] = b"Ounc" if ounce else b"NX  "
    struct.pack_into("<IQ", b, 0x24, n, tab); b[strs:strs + len(names)] = names
    off = data0; blobs = []
    for i, (name, fmt, w, h, layers, nm, bhl, data, offs) in enumerate(textures):
        o = brti0 + 0xA0 * i; struct.pack_into("<Q", b, tab + 8 * i, o)
        b[o:o + 4] = b"BRTI"; b[o + 0x10] = 1; b[o + 0x11] = 1 if layers == 1 else 7     # flags; dimension 2D / cube
        struct.pack_into("<HHH", b, o + 0x12, 0, 0, nm)                                 # tile mode 0 = block linear; mips
        struct.pack_into("<IIIIIII", b, o + 0x1C, fmt, 0, w, h, 1, layers, bhl)
        struct.pack_into("<II", b, o + 0x50, len(data), 0x200)
        b[o + 0x58:o + 0x5C] = bytes([2, 3, 4, 5])
        if fmt >> 8 == 0x1d: b[o + 0x58:o + 0x5C] = bytes([2, 2, 2, 1])                # BC4: grey, opaque
        if fmt >> 8 == 0x1e: b[o + 0x58:o + 0x5C] = bytes([2, 3, 0, 1])                # BC5: red, green
        if fmt >> 8 == 0x02: b[o + 0x58:o + 0x5C] = bytes([2, 2, 2, 1])
        if fmt >> 8 == 0x09: b[o + 0x58:o + 0x5C] = bytes([2, 3, 0, 1])
        b[o + 0x5C] = 1 if layers == 1 else 3
        struct.pack_into("<Q", b, o + 0x60, name_at[i]); struct.pack_into("<Q", b, o + 0x70, mt[i])
        for k, mo in enumerate(offs): struct.pack_into("<Q", b, mt[i] + 8 * k, off + mo)
        blob = bytearray(data)
        while len(blob) % 0x1000: blob.append(0)
        blobs.append(blob); off += len(blob)
    return bytes(b) + b"".join(bytes(x) for x in blobs)


def build_files():
    """-> {"Tex/DemoFormats.bntx": bytes, "Tex/DemoFormats_NX.bntx": bytes} (uncompressed)"""
    T = []
    for i, (name, label, fmt, bw, bh, bpe, enc) in enumerate(formats()):
        T.append(texture(name, [mips(picture(256, label, i))], fmt, bw, bh, bpe, enc, True))
    # a cube map: 6 faces (layers), RGBA8, with mips
    faces = [mips(picture(128, "Cube face %d" % k, 7 * k)) for k in range(6)]
    T.append(texture("Fmt_Cube_RGBA8", faces, 0x0b01, 1, 1, 4, _plain("RGBA8"), True, layers=6))
    sw2 = bntx(T, True)
    T1 = []
    for i, (name, label, fmt, bw, bh, bpe, enc) in enumerate([("NX_RGBA8", "RGBA8 (Switch 1)", 0x0b01, 1, 1, 4, _plain("RGBA8")),
                                                               ("NX_BC1", "BC1 (Switch 1)", 0x1a01, 4, 4, 8, _bc("compress_bc1")),
                                                               ("NX_BC7", "BC7 (Switch 1)", 0x2001, 4, 4, 16, _bc("compress_bc7")),
                                                               ("NX_ASTC_4x4", "ASTC 4x4 (Switch 1)", 0x2d01, 4, 4, 16, _astc(4, 4))]):
        T1.append(texture(name, [mips(picture(256, label, i + 3))], fmt, bw, bh, bpe, enc, False))
    return {"Tex/DemoFormats.bntx": sw2, "Tex/DemoFormats_NX.bntx": bntx(T1, False)}


def build(romfs, here):
    """makes the files (or copies the saved ones when the encoders aren't installed)"""
    from demo_ui import _z
    import shutil
    names = ["DemoFormats.bntx.zs", "DemoFormats_NX.bntx.zs"]
    os.makedirs(os.path.join(romfs, "Tex"), exist_ok=True)
    try:
        import etcpak, astc_encoder  # noqa: F401
        files = build_files()
        for rel, data in files.items():
            z = _z(data); open(os.path.join(romfs, rel + ".zs"), "wb").write(z)
            os.makedirs(os.path.join(here, "media"), exist_ok=True)
            open(os.path.join(here, "media", os.path.basename(rel) + ".zs"), "wb").write(z)
        return "made"
    except ImportError:
        for nm in names:
            for src in (os.path.join(here, "media", nm), os.path.join(here, "base", "romfs", "Tex", nm)):
                if os.path.isfile(src):
                    if os.path.abspath(src) != os.path.abspath(os.path.join(romfs, "Tex", nm)): shutil.copyfile(src, os.path.join(romfs, "Tex", nm))
                    break
        return "copied"
