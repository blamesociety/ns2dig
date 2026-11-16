"""demo_ui.py - the ns2dig demo's user interface files (used by make_demo.py): a title screen layout with its
animations and textures, its text in 4 languages, and a made-up bitmap font. Everything here is invented.

  Layout/DemoTitle.blarc.zs          SARC: blyt/DemoTitle.bflyt, blyt/DemoButton.bflyt, anim/*.bflan, timg/__Combined.bntx
  Mals/<lang>.Product.100.sarc.zs    SARC: LayoutMsg/DemoTitle.msbt, LayoutMsg/DemoButton.msbt   (USen, EUfr, EUde, EUes)
  Font/Font.100.bfarc.zs             SARC: fcpx/DemoBeads.bfcpx, font/DemoBeads.bffnt  (the "DemoBeads" bead font), and
                                     fcpx/DemoBlocks.bfcpx, font/DemoBlocks.bffnt (a second one: outlined blocks, unused)
"""
import math, os, struct
import numpy as np
import zstandard

def _z(b): return zstandard.ZstdCompressor(level=19).compress(bytes(b))
def _pad(b, a, fill=b"\0"):
    while len(b) % a: b += fill
    return b


# ---------------------------------------------------------------- containers
def bntx_raw(textures):
    """BNTX, linear RGBA8 textures: [(name, w, h, [layer bytes...])]. Uncompressed bytes."""
    n = len(textures)
    head = bytearray(0x40); head[0:4] = b"BNTX"; head[0x20:0x24] = b"NX  "
    tab = 0x40; brti0 = (tab + 8 * n + 0xF) & ~0xF
    strs = brti0 + 0xA0 * n
    names = bytearray(); name_at = []
    for nm, *_ in textures:
        name_at.append(strs + len(names)); e = nm.encode("ascii"); names += struct.pack("<H", len(e)) + e + b"\0"; names = _pad(names, 4)
    mips = (strs + len(names) + 0xF) & ~0xF
    data0 = (mips + 8 * n + 0x1FF) & ~0x1FF
    b = bytearray(data0)
    b[0:0x40] = head; struct.pack_into("<IQ", b, 0x24, n, tab)
    b[strs:strs + len(names)] = names
    off = data0; blobs = []
    for i, (nm, w, h, layers) in enumerate(textures):
        data = b"".join(layers); o = brti0 + 0xA0 * i
        struct.pack_into("<Q", b, tab + 8 * i, o)
        b[o:o + 4] = b"BRTI"
        struct.pack_into("<H", b, o + 0x12, 1); struct.pack_into("<H", b, o + 0x16, 1)
        struct.pack_into("<IIIIIII", b, o + 0x1C, 0x0B01, 0, w, h, 1, len(layers), 0)
        struct.pack_into("<II", b, o + 0x50, len(data), 0x200)
        b[o + 0x58:o + 0x5C] = bytes([2, 3, 4, 5]); b[o + 0x5C] = 1
        struct.pack_into("<Q", b, o + 0x60, name_at[i]); struct.pack_into("<Q", b, o + 0x70, mips + 8 * i)
        struct.pack_into("<Q", b, mips + 8 * i, off)
        blob = _pad(bytearray(data), 0x200); blobs.append(blob); off += len(blob)
    return bytes(b) + b"".join(bytes(x) for x in blobs)


def sarc(files):
    """SARC archive (little-endian) from {name: bytes}; entries sorted by name hash, data 0x80-aligned"""
    def h(s):
        x = 0
        for c in s.encode("utf-8"): x = (x * 0x65 + c) & 0xFFFFFFFF
        return x
    items = sorted(files.items(), key=lambda kv: h(kv[0]))
    sfnt = bytearray(); noff = []
    for nm, _ in items: noff.append(len(sfnt)); sfnt += nm.encode("utf-8") + b"\0"; sfnt = _pad(sfnt, 4)
    sfat = bytearray(b"SFAT" + struct.pack("<HHI", 0xC, len(items), 0x65))
    data = bytearray(); spans = []
    for nm, d in items:
        data = _pad(data, 0x80); spans.append((len(data), len(data) + len(d))); data += d
    for (nm, _), no, (a, z) in zip(items, noff, spans):
        sfat += struct.pack("<IIII", h(nm), 0x01000000 | (no // 4), a, z)
    body = sfat + b"SFNT" + struct.pack("<HH", 8, 0) + sfnt
    doff = (0x14 + len(body) + 0x7F) & ~0x7F
    out = bytearray(b"SARC" + struct.pack("<HHIIHH", 0x14, 0xFEFF, doff + len(data), doff, 0x100, 0)) + body
    out += bytes(doff - len(out)) + data
    return bytes(out)


def msbt(entries):
    """MSBT (UTF-16, version 3) from [(label, text)]: LBL1 (101-slot label hash table), ATR1 (empty), TXT2"""
    slots = [[] for _ in range(101)]
    for i, (lab, _) in enumerate(entries):
        x = 0
        for c in lab.encode("ascii"): x = (x * 0x492 + c) & 0xFFFFFFFF
        slots[x % 101].append((lab, i))
    tbl = bytearray(struct.pack("<I", 101)); lbl = bytearray(); base = 4 + 101 * 8
    for s in slots:
        tbl += struct.pack("<II", len(s), base + len(lbl))
        for lab, i in s: e = lab.encode("ascii"); lbl += bytes([len(e)]) + e + struct.pack("<I", i)
    lbl1 = tbl + lbl
    atr1 = struct.pack("<II", len(entries), 0)
    txt = bytearray(); offs = []; base = 4 + 4 * len(entries)
    for _, t in entries: offs.append(base + len(txt)); txt += (t if isinstance(t, bytes) else t.encode("utf-16-le")) + b"\0\0"
    txt2 = struct.pack("<I", len(entries)) + b"".join(struct.pack("<I", o) for o in offs) + txt
    out = bytearray()
    for tag, d in ((b"LBL1", lbl1), (b"ATR1", atr1), (b"TXT2", txt2)):
        out += tag + struct.pack("<I", len(d)) + bytes(8) + d; out = _pad(out, 16, b"\xAB")
    head = bytearray(b"MsgStdBn" + b"\xFF\xFE" + b"\0\0" + bytes([1, 3]) + struct.pack("<HHI", 3, 0, 0x20 + len(out)))
    head += bytes(0x20 - len(head))
    return bytes(head + out)


# ---------------------------------------------------------------- the made-up font: "DemoBeads"
# Each letter is a grid of beads, 5 wide (some narrower). Rows 0-6 run from the top of a capital to the baseline;
# rows 7-8 hold descenders; lower-case letters start at row 2. Accents are added on top (see _ACCENTS).
_G = {
 'A': ".###.|#...#|#...#|#####|#...#|#...#|#...#", 'B': "####.|#...#|#...#|####.|#...#|#...#|####.",
 'C': ".###.|#...#|#....|#....|#....|#...#|.###.", 'D': "####.|#...#|#...#|#...#|#...#|#...#|####.",
 'E': "#####|#....|#....|####.|#....|#....|#####", 'F': "#####|#....|#....|####.|#....|#....|#....",
 'G': ".###.|#...#|#....|#.###|#...#|#...#|.####", 'H': "#...#|#...#|#...#|#####|#...#|#...#|#...#",
 'I': "###|.#.|.#.|.#.|.#.|.#.|###",              'J': "..###|...#.|...#.|...#.|...#.|#..#.|.##..",
 'K': "#...#|#..#.|#.#..|##...|#.#..|#..#.|#...#", 'L': "#....|#....|#....|#....|#....|#....|#####",
 'M': "#...#|##.##|#.#.#|#.#.#|#...#|#...#|#...#", 'N': "#...#|##..#|#.#.#|#..##|#...#|#...#|#...#",
 'O': ".###.|#...#|#...#|#...#|#...#|#...#|.###.", 'P': "####.|#...#|#...#|####.|#....|#....|#....",
 'Q': ".###.|#...#|#...#|#...#|#.#.#|#..#.|.##.#", 'R': "####.|#...#|#...#|####.|#.#..|#..#.|#...#",
 'S': ".####|#....|#....|.###.|....#|....#|####.", 'T': "#####|..#..|..#..|..#..|..#..|..#..|..#..",
 'U': "#...#|#...#|#...#|#...#|#...#|#...#|.###.", 'V': "#...#|#...#|#...#|#...#|#...#|.#.#.|..#..",
 'W': "#...#|#...#|#...#|#.#.#|#.#.#|#.#.#|.#.#.", 'X': "#...#|#...#|.#.#.|..#..|.#.#.|#...#|#...#",
 'Y': "#...#|#...#|.#.#.|..#..|..#..|..#..|..#..", 'Z': "#####|....#|...#.|..#..|.#...|#....|#####",
 'a': ".....|.....|.###.|....#|.####|#...#|.####", 'b': "#....|#....|####.|#...#|#...#|#...#|####.",
 'c': ".....|.....|.###.|#....|#....|#...#|.###.", 'd': "....#|....#|.####|#...#|#...#|#...#|.####",
 'e': ".....|.....|.###.|#...#|#####|#....|.###.", 'f': "..##.|.#..#|.#...|###..|.#...|.#...|.#...",
 'g': ".....|.....|.####|#...#|#...#|#...#|.####|....#|.###.", 'h': "#....|#....|#.##.|##..#|#...#|#...#|#...#",
 'i': ".#.|...|##.|.#.|.#.|.#.|###",              'j': "...#|....|..##|...#|...#|...#|...#|#..#|.##.",
 'k': "#...|#...|#..#|#.#.|##..|#.#.|#..#",        'l': "##.|.#.|.#.|.#.|.#.|.#.|###",
 'm': ".....|.....|##.#.|#.#.#|#.#.#|#.#.#|#.#.#", 'n': ".....|.....|#.##.|##..#|#...#|#...#|#...#",
 'o': ".....|.....|.###.|#...#|#...#|#...#|.###.", 'p': ".....|.....|####.|#...#|#...#|#...#|####.|#....|#....",
 'q': ".....|.....|.####|#...#|#...#|#...#|.####|....#|....#", 'r': ".....|.....|#.##.|##..#|#....|#....|#....",
 's': ".....|.....|.####|#....|.###.|....#|####.", 't': ".#...|.#...|####.|.#...|.#...|.#..#|..##.",
 'u': ".....|.....|#...#|#...#|#...#|#..##|.##.#", 'v': ".....|.....|#...#|#...#|#...#|.#.#.|..#..",
 'w': ".....|.....|#...#|#...#|#.#.#|#.#.#|.#.#.", 'x': ".....|.....|#...#|.#.#.|..#..|.#.#.|#...#",
 'y': ".....|.....|#...#|#...#|#...#|#...#|.####|....#|.###.", 'z': ".....|.....|#####|...#.|..#..|.#...|#####",
 '0': ".###.|#...#|#..##|#.#.#|##..#|#...#|.###.", '1': "..#..|.##..|..#..|..#..|..#..|..#..|.###.",
 '2': ".###.|#...#|....#|...#.|..#..|.#...|#####", '3': "#####|...#.|..#..|...#.|....#|#...#|.###.",
 '4': "...#.|..##.|.#.#.|#..#.|#####|...#.|...#.", '5': "#####|#....|####.|....#|....#|#...#|.###.",
 '6': "..##.|.#...|#....|####.|#...#|#...#|.###.", '7': "#####|....#|...#.|..#..|.#...|.#...|.#...",
 '8': ".###.|#...#|#...#|.###.|#...#|#...#|.###.", '9': ".###.|#...#|#...#|.####|....#|...#.|.##..",
 '.': ".|.|.|.|.|.|#", ',': "..|..|..|..|..|.#|.#|#.", '!': "#|#|#|#|#|.|#", '?': ".###.|#...#|....#|...#.|..#..|.....|..#..",
 '¡': ".|.|#|.|#|#|#|#|#", '¿': ".....|.....|..#..|.....|..#..|.#...|#....|#...#|.###.",
 '-': "....|....|....|####|....|....|....", ':': ".|.|#|.|.|#|.", "'": "#|#|.|.|.|.|.",
 '(': "..#|.#.|#..|#..|#..|.#.|..#", ')': "#..|.#.|..#|..#|..#|.#.|#..", '/': "....#|...#.|...#.|..#..|.#...|.#...|#....",
 'ß': ".##..|#..#.|#..#.|#.#..|#..#.|#...#|#.##.",
 'ı': ".|.|#|#|#|#|#",          # dotless i (for i with an accent)
}
_ACCENTS = {'acute': ("...#.", "..#.."), 'grave': (".#...", "..#.."), 'circ': ("..#..", ".#.#."),
            'diaer': (".....", ".#.#."), 'tilde': (".##.#", "#.##.")}
_COMPOSED = {'é': ('e', 'acute'), 'è': ('e', 'grave'), 'ê': ('e', 'circ'), 'ë': ('e', 'diaer'),
             'á': ('a', 'acute'), 'à': ('a', 'grave'), 'â': ('a', 'circ'), 'ä': ('a', 'diaer'),
             'í': ('ı', 'acute'), 'î': ('ı', 'circ'), 'ï': ('ı', 'diaer'),
             'ó': ('o', 'acute'), 'ô': ('o', 'circ'), 'ö': ('o', 'diaer'), 'ú': ('u', 'acute'),
             'û': ('u', 'circ'), 'ü': ('u', 'diaer'), 'ù': ('u', 'grave'), 'ñ': ('n', 'tilde'),
             'É': ('E', 'acute'), 'È': ('E', 'grave'), 'À': ('A', 'grave'), 'Á': ('A', 'acute'),
             'Î': ('I', 'circ'), 'Ä': ('A', 'diaer'), 'Ö': ('O', 'diaer'), 'Ü': ('U', 'diaer'),
             'Ñ': ('N', 'tilde'), 'Ó': ('O', 'acute'), 'Ú': ('U', 'acute')}
BEAD = 5                           # pixels per bead
ROWS = 11                          # 2 rows for capitals' accents, 7, 2 for descenders
CELL_W, CELL_H = 5 * BEAD, ROWS * BEAD
ASCENT = 9 * BEAD                  # from the cell's top to the baseline


def _grid(ch):
    """the glyph as a list of 11 row strings (accent rows on top)"""
    if ch in _COMPOSED:
        base, acc = _COMPOSED[ch]; g = _grid(base); w = len(g[2])
        a = [r[(5 - w) // 2:(5 - w) // 2 + w] if w < 5 else r for r in _ACCENTS[acc]]
        if base.isupper(): g[0], g[1] = a[0], a[1]          # above a capital
        else: g[2], g[3] = a[0], a[1]                         # in a lower-case letter's ascender rows
        return g
    rows = _G[ch].split("|"); w = len(rows[0])
    return ["." * w] * 2 + rows + ["." * w] * (9 - len(rows))


def _bead_sheet_glyph(g):
    """white beads with soft edges, alpha = coverage; (w x CELL_H) float array"""
    w = len(g[0]) * BEAD; img = np.zeros((CELL_H, w))
    yy, xx = np.mgrid[0:CELL_H, 0:w] + 0.5
    for r, row in enumerate(g):
        for c, on in enumerate(row):
            if on != "#": continue
            cx, cy = (c + 0.5) * BEAD, (r + 0.5) * BEAD
            d = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
            img = np.maximum(img, np.clip(BEAD * 0.46 - d + 0.5, 0, 1))
    return img


def _block_sheet_glyph(g):
    """square tiles with a dark outline (a coloured bitmap font: white tiles, black edges), RGBA floats"""
    w = len(g[0]) * BEAD; m = np.zeros((CELL_H, w), bool)
    for r, row in enumerate(g):
        for c, on in enumerate(row):
            if on == "#": m[r * BEAD + 1:(r + 1) * BEAD - 1, c * BEAD + 1:(c + 1) * BEAD - 1] = True
    edge = np.zeros_like(m)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1): edge |= np.roll(np.roll(m, dy, 0), dx, 1)
    edge &= ~m
    shade = 1.0 - 0.25 * (np.arange(CELL_H) / CELL_H)[:, None] * np.ones((1, w))      # a little darker towards the bottom
    rgb = np.where(m[..., None], np.stack([shade] * 3, axis=2), 0.0)
    return np.dstack([rgb, (m | edge).astype(float)])


def font_files(name="DemoBeads", style="beads"):
    """(bffnt bytes, bfcpx bytes, characters)"""
    chars = sorted(set(_G) | set(_COMPOSED) | {' '}); chars.remove('ı'); chars.append('ı'); chars.sort()
    SW = SH = 512
    cols, rows = (SW - 1) // (CELL_W + 1), (SH - 1) // (CELL_H + 1)
    assert len(chars) <= cols * rows
    sheet = np.zeros((SH, SW, 4))
    widths = []
    for gi, ch in enumerate(chars):
        if ch == ' ': widths.append((0, 0, 3 * BEAD)); continue
        g = _grid(ch)
        if style == "blocks": img = _block_sheet_glyph(g)
        else: al = _bead_sheet_glyph(g); img = np.dstack([np.ones(al.shape + (3,)), al])
        w = img.shape[1]
        x0, y0 = (gi % cols) * (CELL_W + 1) + 1, (gi // cols) * (CELL_H + 1) + 1
        sheet[y0:y0 + CELL_H, x0:x0 + w] = img
        widths.append((0, w, w + BEAD))
    rgba = (sheet * 255 + 0.5).astype(np.uint8)
    if style != "blocks": rgba[..., 0:3] = 255
    rgba = rgba[::-1]                                          # sheets are stored bottom row first
    tex = bntx_raw([(name + "_Sheet", SW, SH, [rgba.tobytes()])])
    # FFNT: header, FINF, TGLP (-> the BNTX), CWDH, CMAP; block pointers point just past each block's 8-byte header
    finf = 0x14; tglp = finf + 0x20; cwdh = tglp + 0x20; cw_n = 8 + 8 + 3 * len(chars)
    cmap = (cwdh + cw_n + 3) & ~3; cm_n = 8 + 16 + 4 + 8 * len(chars)
    sheet_at = (cmap + cm_n + 0xFFF) & ~0xFFF
    b = bytearray(sheet_at)
    b[0:4] = b"FFNT"; struct.pack_into("<HHIIH", b, 4, 0xFEFF, 0x14, 0x04010000, sheet_at + len(tex), 4)
    b[finf:finf + 4] = b"FINF"; struct.pack_into("<I", b, finf + 4, 0x20)
    fi = finf + 8; alt = chars.index('?')
    b[fi:fi + 4] = bytes([1, CELL_H, CELL_W, ASCENT]); struct.pack_into("<HH", b, fi + 4, CELL_H + BEAD, alt)
    b[fi + 8:fi + 12] = bytes([0, CELL_W, CELL_W + BEAD, 1])        # default widths; encoding 1 = UTF-16
    struct.pack_into("<III", b, fi + 12, tglp + 8, cwdh + 8, cmap + 8)
    b[tglp:tglp + 4] = b"TGLP"; struct.pack_into("<I", b, tglp + 4, 0x20)
    t = tglp + 8
    b[t:t + 4] = bytes([CELL_W, CELL_H, 1, CELL_W + BEAD])
    struct.pack_into("<IHHHHHHI", b, t + 4, len(tex), ASCENT, 0, cols, rows, SW, SH, sheet_at)
    b[cwdh:cwdh + 4] = b"CWDH"; struct.pack_into("<I", b, cwdh + 4, cw_n)
    struct.pack_into("<HHI", b, cwdh + 8, 0, len(chars) - 1, 0)
    for i, (l, gw, cw) in enumerate(widths): struct.pack_into("<bBB", b, cwdh + 16 + 3 * i, l, gw, cw)
    b[cmap:cmap + 4] = b"CMAP"; struct.pack_into("<I", b, cmap + 4, cm_n)
    struct.pack_into("<IIHHII", b, cmap + 8, 0, 0xFFFF, 2, 0, 0, len(chars))
    for i, ch in enumerate(chars): struct.pack_into("<II", b, cmap + 28 + 8 * i, ord(ch), i)
    ffnt = bytes(b) + tex
    # FCPX: a font combination naming the bitmap font (block kind 0 at 0x18, the file name after it)
    fc = bytearray(0x18 + 0x24); fc[0:4] = b"FCPX"; struct.pack_into("<HHI", fc, 4, 0xFEFF, 0x18, 0x01000000)
    struct.pack_into("<II", fc, 0x18, 0, 0x24); fc += (name + ".bffnt").encode() + b"\0"; fc = _pad(fc, 4)
    struct.pack_into("<I", fc, 0x0C, len(fc))
    return ffnt, bytes(fc), chars


# ---------------------------------------------------------------- layouts (BFLYT 9) and animations (BFLAN)
def _rgba(c): return bytes(c) if len(c) == 4 else bytes(c) + b"\xff"


class Mat:
    def __init__(self, name, tex=None, white=(255, 255, 255, 255), black=(0, 0, 0, 0), wrap=(0, 0)):
        self.name, self.tex, self.white, self.black, self.wrap = name, tex, white, black, wrap


def _material(m, texIndex):
    nt = 1 if m.tex else 0
    flags = nt | (nt << 2) | (nt << 4)                     # texture maps, texture SRTs, texture coordinate generators
    b = bytearray(m.name.encode("ascii").ljust(0x1C, b"\0")[:0x1C])
    b += struct.pack("<II", flags, 0) + _rgba(m.black) + _rgba(m.white)
    if nt:
        b += struct.pack("<HBB", texIndex[m.tex], m.wrap[0], m.wrap[1])
        b += struct.pack("<5f", 0, 0, 0, 1, 1)
        b += bytes([1, 0]) + bytes(14)
    return bytes(b)


def _pane(kind, name, pos=(0, 0), size=(100, 100), scale=(1, 1), rot=0.0, visible=True, influence=False, alpha=255, origin=0):
    b = bytearray(kind.encode() + b"\0\0\0\0")
    b[8:12] = bytes([(1 if visible else 0) | (2 if influence else 0), origin, alpha, 0])
    b += name.encode("ascii").ljust(0x18, b"\0")[:0x18] + bytes(8)
    b += struct.pack("<3f3f2f2f", pos[0], pos[1], 0, 0, 0, rot, scale[0], scale[1], size[0], size[1])
    return b


def _section(b):
    b = _pad(bytearray(b), 4); struct.pack_into("<I", b, 4, len(b)); return bytes(b)


def pic(name, mat, uv=None, colors=None, **kw):
    b = _pane("pic1", name, **kw)
    for c in (colors or [(255, 255, 255, 255)] * 4): b += _rgba(c)
    b += struct.pack("<HBB", mat, 1, 0)
    u = uv or (0, 0, 1, 0, 0, 1, 1, 1)
    b += struct.pack("<8f", *u)
    return _section(b)


def text(name, mat, font, s, fsize, top=(255, 255, 255, 255), bottom=None, pos_flags=0, **kw):
    b = _pane("txt1", name, **kw)
    t = s.encode("utf-16-le")
    b += struct.pack("<HHHHBBBB", len(t) + 2, len(t), mat, font, pos_flags, 0, 0, 0)
    b += struct.pack("<fI", 0, 0xA8) + _rgba(top) + _rgba(bottom or top)
    b += struct.pack("<4fI", fsize, fsize, 0, 0, 0)
    b += struct.pack("<4f", 0, 0, 1, 1) + _rgba((0, 0, 0, 255)) + _rgba((0, 0, 0, 255)) + struct.pack("<fII", 0, 0, 0)
    assert len(b) == 0xA8
    b += t + b"\0\0"
    return _section(b)


def window(name, mat, frame_mat, **kw):
    b = _pane("wnd1", name, **kw)
    b += struct.pack("<4h4H", 0, 0, 0, 0, 16, 16, 16, 16) + struct.pack("<BBHII", 1, 0, 0, 0x70, 0xA4)
    for _ in range(4): b += _rgba((255, 255, 255, 255))
    b += struct.pack("<HBB", mat, 1, 0) + struct.pack("<8f", 0, 0, 1, 0, 0, 1, 1, 1)
    b += struct.pack("<I", 0xA8) + struct.pack("<HBB", frame_mat, 0, 0)
    return _section(b)


def part(name, layout, **kw):
    b = _pane("prt1", name, **kw)
    b += struct.pack("<I2f", 0, 1, 1) + layout.encode("ascii").ljust(0x20, b"\0")
    return _section(b)


def null(name, **kw): return _section(_pane("pan1", name, **kw))


def bflyt(name, w, h, textures, fonts, mats, tree):
    """tree: (pane bytes, [children trees])"""
    secs = []
    secs.append(_section(b"lyt1\0\0\0\0" + bytes([1, 0, 0, 0]) + struct.pack("<4f", w, h, w, h) + name.encode() + b"\0"))
    for tag, lst in ((b"txl1", textures), (b"fnl1", fonts)):
        strs = bytearray(); offs = []
        for s in lst: offs.append(4 * len(lst) + len(strs)); strs += s.encode() + b"\0"
        secs.append(_section(tag + bytes(4) + struct.pack("<HH", len(lst), 0) + b"".join(struct.pack("<I", o) for o in offs) + strs))
    ti = {t: i for i, t in enumerate(textures)}
    body = bytearray(); offs = []
    for m in mats: offs.append(0xC + 4 * len(mats) + len(body)); body += _material(m, ti)
    secs.append(_section(b"mat1" + bytes(4) + struct.pack("<HH", len(mats), 0) + b"".join(struct.pack("<I", o) for o in offs) + body))
    def walk(node):
        pane, kids = node; secs.append(pane)
        if kids:
            secs.append(b"pas1" + struct.pack("<I", 8))
            for k in kids: walk(k)
            secs.append(b"pae1" + struct.pack("<I", 8))
    walk(tree)
    data = b"".join(secs)
    return b"FLYT" + struct.pack("<HHIIHH", 0xFEFF, 0x14, 0x09000000, 0x14 + len(data), len(secs), 0) + data


def bflan(name, frames, loop, contents, textures=()):
    """contents: [(target name, is material, [(tag, [(index, channel, [(frame, value, slope)...])...])...])]"""
    pat = bytearray(b"pat1" + bytes(4)) + struct.pack("<HHIIhhB3x", 0, 0, 0x1C, 0, 0, frames, 0) + name.encode() + b"\0"
    pat = _section(pat)
    pai = bytearray(b"pai1" + bytes(4)) + struct.pack("<HBBHHI", frames, 1 if loop else 0, 0, len(textures), len(contents), 0)
    if textures:
        strs = bytearray(); offs = []
        for t in textures: offs.append(4 * len(textures) + len(strs)); strs += t.encode() + b"\0"
        pai += b"".join(struct.pack("<I", o) for o in offs) + strs
    pai = _pad(pai, 4)
    table_at = len(pai); struct.pack_into("<I", pai, 0x10, table_at)
    pai += bytes(4 * len(contents))
    for ci, (target, is_mat, infos) in enumerate(contents):
        co = len(pai); struct.pack_into("<I", pai, table_at + 4 * ci, co)
        c = bytearray(target.encode().ljust(0x1C, b"\0")[:0x1C]) + bytes([len(infos), 1 if is_mat else 0, 0, 0]) + bytes(4 * len(infos))
        for ii, (tag, targets) in enumerate(infos):
            io = len(c); struct.pack_into("<I", c, 0x20 + 4 * ii, io)
            info = bytearray(tag.encode() + bytes([len(targets), 0, 0, 0])) + bytes(4 * len(targets))
            for ti, (index, channel, keys) in enumerate(targets):
                to = len(info); struct.pack_into("<I", info, 8 + 4 * ti, to)
                info += struct.pack("<BBBBHHI", index, channel, 2, 0, len(keys), 0, 0xC)
                for f, v, s in keys: info += struct.pack("<3f", f, v, s)
            c += info
        pai += c
    pai = _section(pai)
    data = pat + pai
    return b"FLAN" + struct.pack("<HHIIHH", 0xFEFF, 0x14, 0x09000000, 0x14 + len(data), 2, 0) + data


def _keys(fn, frames, step):
    """sample fn(frame) every 'step' frames into hermite keys (value, slope per frame)"""
    out = []
    for f in list(range(0, frames, step)) + [frames]:
        out.append((float(f), float(fn(f)), float((fn(f + 0.01) - fn(f - 0.01)) / 0.02)))
    return out


def _ease(a, b, f0, f1):
    """a -> b between frames f0 and f1, smooth; held outside"""
    return [(float(f0), float(a), 0.0), (float(f1), float(b), 0.0)]


# ---------------------------------------------------------------- textures for the title screen
def _tex_sky(w=256, h=256):
    y, x = np.mgrid[0:h, 0:w] / np.array([h - 1, w - 1])[:, None, None]
    horizon = 0.62
    sky_top, sky_low = np.array([40, 110, 200]), np.array([170, 215, 245])
    sea_top, sea_low = np.array([30, 150, 190]), np.array([10, 60, 120])
    t = np.clip(y / horizon, 0, 1)[..., None]
    sky = sky_top * (1 - t) + sky_low * t
    s = np.clip((y - horizon) / (1 - horizon), 0, 1)[..., None]
    sea = sea_top * (1 - s) + sea_low * s
    img = np.where((y < horizon)[..., None], sky, sea)
    # the island on the horizon: a low hump with a peak
    bump = horizon - 0.10 * np.exp(-((x - 0.20) / 0.05) ** 2) - 0.04 * np.exp(-((x - 0.15) / 0.09) ** 2)
    land = (y >= bump) & (y < horizon + 0.004) & (bump < horizon - 0.004)
    img = np.where(land[..., None], np.array([50, 110, 70]) * (0.8 + 0.4 * (y - bump)[..., None] * 8), img)
    # light ripples on the sea
    rip = (y > horizon) * (0.5 + 0.5 * np.sin(x * 90 + np.sin(y * 40) * 3)) * (y - horizon) * 0.25
    img = img + rip[..., None] * 120
    rgba = np.concatenate([np.clip(img, 0, 255), np.full((h, w, 1), 255)], axis=2).astype(np.uint8)
    return rgba.tobytes()


def _tex_sun(n=128):
    y, x = (np.mgrid[0:n, 0:n] + 0.5) / n - 0.5
    r = np.sqrt(x * x + y * y); a = np.arctan2(y, x)
    core = np.clip((0.22 - r) * 60, 0, 1)
    rays = np.clip((0.48 - r) * 6, 0, 1) * np.clip(np.cos(a * 12) * 3 - 1.5, 0, 1) * (r > 0.2)
    glow = np.clip(0.5 - r, 0, 0.5) * 0.9
    al = np.clip(np.maximum(np.maximum(core, rays * 0.9), glow), 0, 1)
    rgb = np.stack([np.full_like(r, 255), 230 - 60 * np.clip(r * 2, 0, 1), 120 - 80 * np.clip(r * 2, 0, 1)], axis=2)
    return np.concatenate([rgb, al[..., None] * 255], axis=2).astype(np.uint8).tobytes()


def _rounded(w, h, rad, shade):
    y, x = np.mgrid[0:h, 0:w] + 0.5
    dx = np.maximum(np.maximum(rad - x, x - (w - rad)), 0); dy = np.maximum(np.maximum(rad - y, y - (h - rad)), 0)
    d = np.sqrt(dx * dx + dy * dy)
    al = np.clip(rad - d, 0, 1)
    v = shade(x / w, y / h)
    return np.stack([v, v, v, al * 255], axis=2).clip(0, 255).astype(np.uint8).tobytes()


def _tex_wave(w=128, h=32):
    y, x = (np.mgrid[0:h, 0:w] + 0.5) / np.array([h, w])[:, None, None]
    crest = 0.35 + 0.15 * np.sin(x * 2 * math.pi * 2)
    al = np.clip((y - crest) * 20, 0, 1) * 0.85
    foam = np.clip(1 - np.abs(y - crest) * 25, 0, 1)
    rgb = np.stack([60 + 195 * foam, 140 + 115 * foam, 190 + 65 * foam], axis=2)
    return np.concatenate([rgb, np.maximum(al, foam)[..., None] * 255], axis=2).clip(0, 255).astype(np.uint8).tobytes()


# ---------------------------------------------------------------- the texts, per language
FONT = "DemoBeads.bfcpx"
TEXTS = {   # label: (stored in the layout = USen, then per language)
    "T_Title": {"USen": "Demo Island", "EUfr": "Île de démo", "EUde": "Demo-Insel", "EUes": "Isla de demo"},
    "T_Hint": {"USen": "Press A to begin", "EUfr": "Appuie sur A pour commencer", "EUde": "Drücke A, um zu beginnen", "EUes": "Pulsa A para empezar"},
    "T_Secret": {"USen": "You found the hidden pane!", "EUfr": "Tu as trouvé le panneau caché !", "EUde": "Du hast das versteckte Feld gefunden!", "EUes": "¡Has encontrado el panel oculto!"},
    "T_Version": {"USen": "Version 1.0", "EUfr": "Version 1.0", "EUde": "Version 1.0", "EUes": "Versión 1.0"},
    "P_Start-T_Label": {"USen": "Start", "EUfr": "Commencer", "EUde": "Starten", "EUes": "Empezar"},
    "P_Options-T_Label": {"USen": "Options", "EUfr": "Options", "EUde": "Optionen", "EUes": "Opciones"},
    "P_Credits-T_Label": {"USen": "Credits", "EUfr": "Crédits", "EUde": "Mitwirkende", "EUes": "Créditos"},
}
BUTTON_TEXT = {"T_Label": {"USen": "Button", "EUfr": "Bouton", "EUde": "Knopf", "EUes": "Botón"}}
LANGS = ["USen", "EUfr", "EUde", "EUes"]


def layout_archive():
    """the title screen archive (SARC bytes): 2 layouts, 4 animations, 4 textures in one BNTX"""
    W, H = 1280, 720
    tex = ["Tex_Sky", "Tex_Sun", "Tex_Panel", "Tex_Button", "Tex_Wave"]
    # the title screen
    mats = [Mat("M_Sky", "Tex_Sky"), Mat("M_Sun", "Tex_Sun"), Mat("M_Wave", "Tex_Wave", wrap=(1, 0)),
            Mat("M_Text"), Mat("M_Menu", "Tex_Panel", white=(20, 40, 90, 200)), Mat("M_MenuFrame", "Tex_Panel", white=(255, 255, 255, 255))]
    M = {m.name: i for i, m in enumerate(mats)}
    gold, orange = (255, 236, 120, 255), (255, 140, 40, 255)
    tree = (null("RootPane", size=(W, H)), [
        (pic("P_Sky", M["M_Sky"], size=(W, H)), []),
        (pic("P_Sun", M["M_Sun"], pos=(430, 230), size=(200, 200)), []),
        (pic("P_Waves", M["M_Wave"], pos=(0, -320), size=(W, 80), uv=(0, 0, 6, 0, 0, 1, 6, 1)), []),
        (null("N_TitleGroup", pos=(0, 200), size=(1000, 140), influence=True), [
            (text("T_Title", M["M_Text"], 0, TEXTS["T_Title"]["USen"], 1.6, top=gold, bottom=orange, size=(1000, 120)), []),
        ]),
        (window("W_Menu", M["M_Menu"], M["M_MenuFrame"], pos=(0, -60), size=(440, 300), influence=True), [
            (null("N_Buttons", size=(400, 260), influence=True), [
                (part("P_Start", "DemoButton", pos=(0, 80), size=(360, 64)), []),
                (part("P_Options", "DemoButton", pos=(0, 0), size=(360, 64)), []),
                (part("P_Credits", "DemoButton", pos=(0, -80), size=(360, 64)), []),
            ]),
        ]),
        (text("T_Hint", M["M_Text"], 0, TEXTS["T_Hint"]["USen"], 0.55, size=(900, 44), pos=(0, -255)), []),
        (text("T_Version", M["M_Text"], 0, TEXTS["T_Version"]["USen"], 0.4, size=(300, 30), pos=(-480, -330), pos_flags=1,
              top=(220, 230, 255, 255)), []),
        (text("T_Secret", M["M_Text"], 0, TEXTS["T_Secret"]["USen"], 0.5, size=(1100, 44), pos=(0, 320), visible=False,
              top=(255, 120, 200, 255)), []),
    ])
    title = bflyt("DemoTitle", W, H, tex, [FONT], mats, tree)
    # the button (used 3 times by the title screen, as parts)
    bm = [Mat("M_Button", "Tex_Button", white=(200, 225, 255, 255)), Mat("M_Label")]
    btree = (null("RootPane", size=(360, 64)), [
        (pic("P_Base", 0, size=(360, 64)), []),
        (text("T_Label", 1, 0, BUTTON_TEXT["T_Label"]["USen"], 0.62, top=(20, 40, 90, 255), size=(330, 50)), []),
    ])
    button = bflyt("DemoButton", 360, 64, tex, [FONT], bm, btree)
    # animations
    intro = bflan("DemoTitle_Intro", 60, False, [
        ("N_TitleGroup", False, [("FLPA", [(0, 1, _ease(420, 200, 0, 30))]), ("FLVC", [(0, 16, _ease(0, 255, 0, 25))])]),
        ("W_Menu", False, [("FLPA", [(0, 6, _ease(0, 1, 18, 42)), (0, 7, _ease(0, 1, 18, 42))]), ("FLVC", [(0, 16, _ease(0, 255, 18, 34))])]),
        ("T_Hint", False, [("FLVC", [(0, 16, _ease(0, 255, 42, 60))])]),
        ("P_Sun", False, [("FLPA", [(0, 1, _ease(60, 230, 0, 50))])]),
    ])
    idle = bflan("DemoTitle_Idle", 120, True, [
        ("N_TitleGroup", False, [("FLPA", [(0, 1, _keys(lambda f: 200 + 8 * math.sin(2 * math.pi * f / 120), 120, 15)),
                                           (0, 5, _keys(lambda f: 1.5 * math.sin(2 * math.pi * f / 60), 120, 15))])]),
        ("P_Sun", False, [("FLPA", [(0, 5, [(0.0, 0.0, -0.75), (120.0, -90.0, -0.75)])])]),
        ("T_Hint", False, [("FLVC", [(0, 16, _keys(lambda f: 167 + 88 * math.cos(2 * math.pi * f / 60), 120, 10))])]),
        ("M_Wave", True, [("FLTS", [(0, 0, [(0.0, 0.0, 1 / 120), (120.0, 1.0, 1 / 120)])])]),
    ])
    secret = bflan("DemoTitle_Secret", 40, False, [
        ("T_Secret", False, [("FLVI", [(0, 0, [(0.0, 1.0, 0.0)])]), ("FLPA", [(0, 6, _ease(0.2, 1, 0, 20)), (0, 7, _ease(0.2, 1, 0, 20))]),
                             ("FLVC", [(0, 16, _ease(0, 255, 0, 15))])]),
    ])
    sel = bflan("DemoButton_Select", 30, True, [
        ("P_Base", False, [("FLPA", [(0, 6, _keys(lambda f: 1.04 + 0.04 * math.sin(2 * math.pi * f / 30 - math.pi / 2), 30, 5)),
                                     (0, 7, _keys(lambda f: 1.04 + 0.04 * math.sin(2 * math.pi * f / 30 - math.pi / 2), 30, 5))])]),
        ("M_Button", True, [("FLMC", [(0, 4 + c, _keys(lambda f, a=a, b=b: a + (b - a) * (0.5 - 0.5 * math.cos(2 * math.pi * f / 30)), 30, 5))
                                      for c, (a, b) in enumerate(((200, 255), (225, 245), (255, 140)))])]),
    ])
    sky = _tex_sky(); sun = _tex_sun()
    panel = _rounded(64, 64, 14, lambda u, v: 255 - 30 * v)
    btn = _rounded(256, 48, 22, lambda u, v: 255 - 70 * v + 25 * (v < 0.45))
    wave = _tex_wave()
    timg = bntx_raw([("Tex_Sky", 256, 256, [sky]), ("Tex_Sun", 128, 128, [_tex_sun()]), ("Tex_Panel", 64, 64, [panel]),
                     ("Tex_Button", 256, 48, [btn]), ("Tex_Wave", 128, 32, [wave])])
    return sarc({"blyt/DemoTitle.bflyt": title, "blyt/DemoButton.bflyt": button, "anim/DemoTitle_Intro.bflan": intro,
                 "anim/DemoTitle_Idle.bflan": idle, "anim/DemoTitle_Secret.bflan": secret,
                 "anim/DemoButton_Select.bflan": sel, "timg/__Combined.bntx": timg})


def build(romfs):
    """writes the layout archive, the message archives and the font archive under romfs"""
    def put(rel, data):
        p = os.path.join(romfs, *rel.split("/")); os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, "wb").write(data)
    put("Layout/DemoTitle.blarc.zs", _z(layout_archive()))
    ffnt, fcpx, chars = font_files()
    ffnt2, fcpx2, _ = font_files("DemoBlocks", "blocks")                # a second made-up font (not used by a layout)
    put("Font/Font.100.bfarc.zs", _z(sarc({"font/DemoBeads.bffnt": ffnt, "fcpx/DemoBeads.bfcpx": fcpx,
                                            "font/DemoBlocks.bffnt": ffnt2, "fcpx/DemoBlocks.bfcpx": fcpx2})))
    missing = set()
    for lang in LANGS:
        title = [(k, v[lang]) for k, v in TEXTS.items()]
        button = [(k, v[lang]) for k, v in BUTTON_TEXT.items()]
        for _, t in title + button: missing |= {c for c in t if c not in chars}
        files = {"LayoutMsg/DemoTitle.msbt": msbt(title), "LayoutMsg/DemoButton.msbt": msbt(button)}
        import demo_evt; files.update(demo_evt.messages(lang))           # the event scripts' text (demo_evt.py)
        put("Mals/%s.Product.100.sarc.zs" % lang, _z(sarc(files)))
    if missing: print("(the font has no", "".join(sorted(missing)), ")")
    return len(chars)
