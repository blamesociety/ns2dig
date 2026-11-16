#!/usr/bin/env python3
"""
ns2dig demo "romfs" generator - step 1: terrain and water; step 2: a stick-figure player model;
step 3: small islands and the player placed on one; step 4: a skeleton and 3 animations (Idle, Walk, Wave);
step 5: a wooden crate (textured) on another small island; step 6: a video and step 7: music (both copied in, see below);
step 8: a title screen layout with animations, text in 4 languages and a made-up font (demo_ui.py);
step 9: lighting - 4 setups and a lens flare (demo_env.py);
step 10: event scripts - a conversation and a cutscene with a timeline (demo_evt.py);
step 11: exefs - tiny made-up main, rtld, sdk and main.npdm (demo_exefs.py);
step 12: a hat joined to the player by actor data; face key shapes, visibility and colour clips;
step 13: physics - collision meshes and a cloth flag (demo_havok.py); step 14: a sound bank (demo_sound.py);
step 15: texture formats (demo_tex.py); step 16: voxel terrain (demo_voxel.py);
step 17: zstd-dictionary files and a resource size table (demo_data.py).

Builds a tiny, made-up world in the file formats ns2dig reads, so the app can be tried without a game dump.
Nothing in it comes from any game: every height, texture and name is generated here.

  python make_demo.py [output folder]          (default: ./demo)

The result is <output>/base/romfs/...  - scan that folder with ns2dig like a real dump, then open
Terrain/DemoIsland.tscb in the 3D view. All names here are made up (needs ns2dig 2026-11-16.168 or later,
which reads any terrain scene, and a scene's own Tex/<scene>_Ground / _Water textures; 2026-11-16.171 or later
for the placed player, from Banc/World_<scene>/).

The world: 3 x 3 km, a big island with one mountain in the middle of a sea, 5 small islands round it (grass and
sand patches), the player standing on one of them. Water surface at 100 m.
Needs: Python 3, the "zstandard" package (pip install zstandard).
"""
import math, os, random, shutil, struct, sys
import zstandard

OUT = sys.argv[1] if len(sys.argv) > 1 else "demo"
ROMFS = os.path.join(OUT, "base", "romfs")

WS = 250.0            # world_scale: metres per .tscb unit
WORLD = 12.0          # the whole world's square, in .tscb units (12 x 250 = 3000 m)
MAX_H = 500.0         # heights 0-65535 = 0-500 m
WATER_H = 100.0       # sea level (m)
LEVELS = 3            # quad-tree levels: 1 square of 3000 m, 4 of 1500 m, 16 of 750 m
N = 257               # samples per tile side
SCENE = "DemoIsland"  # the terrain scene: Terrain/DemoIsland.tscb, its tiles in Terrain/DemoIsland/

MAT_GRASS, MAT_SAND = 0, 1     # ground material numbers = layers of Tex/DemoIsland_Ground


# ---------------------------------------------------------------- the world itself
def smoothstep(a, b, x):
    t = min(1.0, max(0.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


# the small islands round the big one: (x, z, radius, peak height) - placed at random (fixed seed, so every run
# makes the same world), kept apart from the big island, each other and the map's edge
def _small_islands(count=5, seed=2026):
    rnd = random.Random(seed); out = []
    lim = WORLD * WS / 2 - 60.0
    while len(out) < count:
        a = rnd.uniform(0, 2 * math.pi); d = rnd.uniform(1000.0, 1300.0)
        x, z = d * math.cos(a), d * math.sin(a)
        r = rnd.uniform(140.0, 230.0)
        if abs(x) > lim - r or abs(z) > lim - r: continue
        if any(math.hypot(x - q[0], z - q[1]) < r + q[2] + 160.0 for q in out): continue
        out.append((x, z, r, rnd.uniform(124.0, 150.0)))
    return out


SMALL = _small_islands()
PLAYER_ISLAND = 0                  # the small island the player stands on


def _small_at(x, z):
    """(index, distance / radius) of the small island nearest x, z"""
    best, bd = -1, 1e9
    for i, (ix, iz, r, _) in enumerate(SMALL):
        d = math.hypot(x - ix, z - iz) / r
        if d < bd: best, bd = i, d
    return best, bd


def height(x, z):
    """ground height in metres at world x, z (metres, 0, 0 = the middle of the map)"""
    r = math.hypot(x, z)
    shore = 1.0 - smoothstep(520.0, 760.0, r)                    # 1 on the big island, 0 out at sea
    h = 40.0 + 75.0 * shore                                      # sea floor 40 m, island plain 115 m
    m = 290.0 * math.exp(-((x - 80.0) ** 2 + (z + 60.0) ** 2) / (2 * 210.0 ** 2))   # the mountain
    h += m * (1.0 - smoothstep(480.0, 680.0, r))
    h += shore * (5.0 * math.sin(x / 41.0) * math.cos(z / 57.0) + 3.0 * math.sin((x + z) / 23.0))   # gentle bumps
    for (ix, iz, ir, peak) in SMALL:                             # the small islands: low domes rising from the sea floor
        d = math.hypot(x - ix, z - iz) / ir
        if d < 1.6:
            dome = 1.0 - smoothstep(0.0, 1.45, d)
            hi = 40.0 + (peak - 40.0) * dome * (1.0 + 0.04 * math.sin(x / 13.0) * math.cos(z / 17.0))
            h = max(h, hi)
    return max(0.0, min(MAX_H, h))


def _patches(x, z):
    """a smooth -1..1 pattern for the small islands' grass patches"""
    return (math.sin(x / 29.0 + 1.3 * math.sin(z / 37.0)) * math.cos(z / 33.0 + 1.1 * math.sin(x / 41.0))
            + 0.5 * math.sin((x - z) / 17.0)) / 1.5


def material(x, z, h):
    if h < WATER_H + 6.0: return MAT_SAND
    i, d = _small_at(x, z)
    if d < 1.6 and math.hypot(x, z) > 800.0:                     # on a small island: patches, about half grass
        return MAT_GRASS if _patches(x, z) > GRASS_CUT else MAT_SAND
    return MAT_GRASS


GRASS_CUT = 0.0                    # (set below so the small islands' land is about half grass)


def _calibrate_grass():
    vals = []
    for (ix, iz, ir, _) in SMALL:
        n = 60
        for a in range(n):
            for b in range(n):
                x = ix + (a / (n - 1) * 2 - 1) * ir * 1.6; z = iz + (b / (n - 1) * 2 - 1) * ir * 1.6
                if height(x, z) >= WATER_H: vals.append(_patches(x, z) if height(x, z) >= WATER_H + 6.0 else -9.0)
    vals.sort()
    return vals[len(vals) // 2]


GRASS_CUT = _calibrate_grass()


def player_spot():
    """the player: on the top of its small island, facing the big island. (x, y, z), y rotation (radians)"""
    ix, iz, ir, _ = SMALL[PLAYER_ISLAND]
    best = None
    for a in range(-20, 21):
        for b in range(-20, 21):
            x, z = ix + a * ir / 40, iz + b * ir / 40
            h = height(x, z)
            if best is None or h > best[1]: best = (x, h, z)
    x, y, z = best
    return (round(x, 2), round(y, 2), round(z, 2)), math.atan2(-x, -z)


# ---------------------------------------------------------------- file format helpers
def tile_nvn2(lin, w, h, bpe):
    """linear rows -> the terrain tiles' block-linear layout (1-GOB-tall blocks), the inverse of the app's TrUntile"""
    rb = w * bpe
    gx = (rb + 63) // 64
    out = bytearray(((h + 7) // 8) * gx * 512)
    for y in range(h):
        yy = y & 7
        by = (y // 8) * gx * 512
        row = lin[y * rb:(y + 1) * rb]
        for xb in range(rb):
            xx = xb & 63
            a = by + (xb // 64) * 512 + ((xx & 15) | ((yy & 1) << 4) | (((yy >> 1) & 1) << 5) | (((xx >> 4) & 1) << 6) |
                                        (((yy >> 2) & 1) << 7) | (((xx >> 5) & 1) << 8))
            out[a] = row[xb]
    return bytes(out)


def tarb(payload):
    """TARB container: 'TARB', version, chunk count, header size, (size, compressed size) per chunk, zstd chunks"""
    z = zstandard.ZstdCompressor(level=19).compress(payload)
    hdr = b"TARB" + struct.pack("<IHH", 1, 1, 0x14) + struct.pack("<II", len(payload), len(z))
    return hdr + z


def words(*w):
    """the tiles' 0x34-byte inner header (13 words)"""
    w = list(w) + [0] * (13 - len(w))
    return struct.pack("<13I", *w)


def hght_file(hs):
    """height tile: second section = 257 x 257 u16 heights, tiled. (A first section of zeros stands in for the
    four small 4-bit maps the real files have.)"""
    lin = struct.pack("<%dH" % (N * N), *hs)
    sec2 = tile_nvn2(lin, N, N, 2)
    sec1 = bytes(4 * 130 * 260)
    off1, off2 = 0x34, 0x34 + len(sec1)
    hdr = bytearray(words(0, 0, 0, N))
    hdr[4:8] = b"\x09\0\0\0"
    struct.pack_into("<II", hdr, 0x1C, off1, len(sec1))            # words 7 / 8: first section
    struct.pack_into("<II", hdr, 0x2C, off2 - 0x2C, len(sec2))     # words 11 / 12: second section (from 0x2C)
    return tarb(bytes(hdr) + sec1 + sec2)


def mate_file(ms):
    """material tile: 257 x 257 texels of 4 bytes after the header, tiled; byte 0 = material number"""
    lin = bytearray(N * N * 4)
    for i, m in enumerate(ms):
        lin[i * 4] = m
    hdr = bytearray(words(0, 0, 0, N))
    hdr[4:8] = b"\x06\x06\x06\x06"
    return tarb(bytes(hdr) + tile_nvn2(bytes(lin), N, N, 4))


def water_file():
    """water tile: 68 x 68 cells of 8 bytes (a 2-cell border round 64 x 64): u16 surface height, two u16 ~0x7FFF,
    byte 6 = 3 for a real cell, byte 7 = kind (0 = water)"""
    wh = int(round(WATER_H / MAX_H * 65535))
    cells = bytearray()
    for j in range(68):
        for i in range(68):
            real = 2 <= i < 66 and 2 <= j < 66
            cells += struct.pack("<HHHBB", wh, 0x7FFF, 0x7FFF, 3 if real else 0, 0)
    hdr = bytearray(words(0x00024400))
    hdr[4:8] = b"\x09\x09\x09\x09"
    struct.pack_into("<II", hdr, 0x1C, 0x34 - 0x1C, len(cells))    # first section (from 0x1C)
    return tarb(bytes(hdr) + bytes(cells))


def tscb_file(areas):
    """TSCB terrain scene: world_scale / max_height, counts, then the list of squares (centre x, z, size, tile name)"""
    head = bytearray(0x80)
    head[0:4] = b"TSCB"
    head[4], head[5] = 12, 9                                       # version 9.12
    struct.pack_into("<2f", head, 0x10, WS, MAX_H)
    struct.pack_into("<3I", head, 0x30, 0, 0, len(areas))           # layers, materials, squares
    table_at = 0x80
    rec_at = table_at + 4 * len(areas)
    rec_size = 0x64
    names_at = rec_at + rec_size * len(areas)
    struct.pack_into("<I", head, 0x78, table_at - 0x78)
    table = bytearray(); recs = bytearray(); names = bytearray()
    for i, (x, z, s, name) in enumerate(areas):
        ta = table_at + 4 * i
        ra = rec_at + rec_size * i
        table += struct.pack("<I", ra - ta)
        rec = bytearray(rec_size)
        struct.pack_into("<3f", rec, 0, x, z, s)
        na = names_at + len(names)
        struct.pack_into("<I", rec, 0x60, na - (ra + 0x60))
        recs += rec
        names += name.encode("ascii") + b"\0"
    return bytes(head) + bytes(table) + bytes(recs) + bytes(names)


def bntx_file(name, w, h, layers):
    """BNTX with one RGBA8 texture (linear, not tiled), 'layers' images of w x h, each a bytes of w*h*4"""
    data = b"".join(layers)
    b = bytearray(0x200)
    b[0:4] = b"BNTX"
    b[0x20:0x24] = b"NX  "
    struct.pack_into("<IQ", b, 0x24, 1, 0x40)                       # one texture; its offset at 0x40
    struct.pack_into("<Q", b, 0x40, 0x60)
    o = 0x60
    b[o:o + 4] = b"BRTI"
    struct.pack_into("<H", b, o + 0x12, 1)                          # linear
    struct.pack_into("<H", b, o + 0x16, 1)                          # one mip level
    struct.pack_into("<IIIIIII", b, o + 0x1C, 0x0B01, 0, w, h, 1, len(layers), 0)   # RGBA8 unorm
    struct.pack_into("<II", b, o + 0x50, len(data), 0x200)
    b[o + 0x58:o + 0x5C] = bytes([2, 3, 4, 5])                     # channels: R G B A
    b[o + 0x5C] = 1
    name_at, mip_at = 0x120, 0x180
    struct.pack_into("<Q", b, o + 0x60, name_at)
    struct.pack_into("<Q", b, o + 0x70, mip_at)
    nb = name.encode("ascii")
    struct.pack_into("<H", b, name_at, len(nb)); b[name_at + 2:name_at + 2 + len(nb)] = nb
    struct.pack_into("<Q", b, mip_at, 0x200)
    return zstandard.ZstdCompressor(level=19).compress(bytes(b) + data)


def land_texture(n=128, sand=False):
    """a soft grey speckle (the app scales it to mid-grey and lays it over the ground's colour); sand: finer, ripplier"""
    px = bytearray()
    for y in range(n):
        for x in range(n):
            if sand:
                v = 128 + 22 * math.sin(y * 0.45 + 1.5 * math.sin(x * 0.12)) + 12 * math.sin((x * 7 + y * 3) * 0.53)
            else:
                v = 128 + 30 * math.sin(x * 0.9 + 2 * math.sin(y * 0.31)) * math.sin(y * 0.7 + 2 * math.sin(x * 0.23))
                v += 18 * math.sin((x * 3 + y * 5) * 0.37)
            g = int(max(0, min(255, v)))
            px += bytes((g, g, g, 255))
    return bytes(px)


def water_texture(n=128):
    """gentle ripples, repeating (the app turns it into a faint grey ripple over the water's colour)"""
    px = bytearray()
    for y in range(n):
        for x in range(n):
            a, b2 = 2 * math.pi * x / n, 2 * math.pi * y / n
            v = 128 + 40 * math.sin(3 * a + math.sin(2 * b2)) + 30 * math.sin(4 * b2 + math.sin(3 * a))
            g = int(max(0, min(255, v)))
            px += bytes((g, g, g, 255))
    return bytes(px)


# ---------------------------------------------------------------- step 2: the stick-figure player (a BFRES model)
def _box(cx, cy, cz, sx, sy, sz):
    """an axis-aligned box: (positions, normals, indices) - 24 vertices so each face is flat"""
    P, Nn, I = [], [], []
    faces = [((1, 0, 0), (0, 1, 0), (0, 0, 1)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)), ((0, 1, 0), (0, 0, 1), (1, 0, 0)),
             ((0, -1, 0), (1, 0, 0), (0, 0, 1)), ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (0, 1, 0), (1, 0, 0))]
    for n, u, v in faces:
        b = len(P)
        for a, c in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            P.append((cx + (n[0] + a * u[0] + c * v[0]) * sx / 2, cy + (n[1] + a * u[1] + c * v[1]) * sy / 2,
                      cz + (n[2] + a * u[2] + c * v[2]) * sz / 2))
            Nn.append(n)
        I += [b, b + 1, b + 2, b, b + 2, b + 3]
    return P, Nn, I


def _limb(x0, y0, z0, x1, y1, z1, r, sides=8):
    """a thin tube from one point to another (radius r)"""
    d = (x1 - x0, y1 - y0, z1 - z0); L = math.sqrt(sum(c * c for c in d)); d = tuple(c / L for c in d)
    a = (1, 0, 0) if abs(d[0]) < 0.9 else (0, 0, 1)
    u = (d[1] * a[2] - d[2] * a[1], d[2] * a[0] - d[0] * a[2], d[0] * a[1] - d[1] * a[0]); ul = math.sqrt(sum(c * c for c in u)); u = tuple(c / ul for c in u)
    v = (d[1] * u[2] - d[2] * u[1], d[2] * u[0] - d[0] * u[2], d[0] * u[1] - d[1] * u[0])
    P, Nn, I = [], [], []
    for k in range(sides):
        t = 2 * math.pi * k / sides
        n = tuple(math.cos(t) * u[i] + math.sin(t) * v[i] for i in range(3))
        P.append((x0 + n[0] * r, y0 + n[1] * r, z0 + n[2] * r)); Nn.append(n)
        P.append((x1 + n[0] * r, y1 + n[1] * r, z1 + n[2] * r)); Nn.append(n)
    for k in range(sides):
        a0, a1, b0, b1 = 2 * k, 2 * k + 1, 2 * ((k + 1) % sides), 2 * ((k + 1) % sides) + 1
        I += [a0, b0, a1, a1, b0, b1]
    return P, Nn, I


def _ball(cx, cy, cz, r, rings=8, segs=12):
    P, Nn, I = [], [], []
    for j in range(rings + 1):
        ph = math.pi * j / rings
        for k in range(segs + 1):
            th = 2 * math.pi * k / segs
            n = (math.sin(ph) * math.cos(th), math.cos(ph), math.sin(ph) * math.sin(th))
            P.append((cx + n[0] * r, cy + n[1] * r, cz + n[2] * r)); Nn.append(n)
    for j in range(rings):
        for k in range(segs):
            a, b = j * (segs + 1) + k, (j + 1) * (segs + 1) + k
            I += [a, a + 1, b, a + 1, b + 1, b]
    return P, Nn, I


def _join(*parts):
    P, Nn, I = [], [], []
    for p, n, i in parts:
        b = len(P); P += p; Nn += n; I += [b + x for x in i]
    return P, Nn, I


# the player's skeleton: (bone, parent, joint position in the standing pose). Bones point nowhere in particular: each
# has no rotation in the standing pose, so an animation's rotations are plain angles (x, y, z, radians) at the joint.
BONES = [("Root", -1, (0, 0, 0)), ("Hips", 0, (0, 0.95, 0)), ("Spine", 1, (0, 0.95, 0)), ("Head", 2, (0, 1.46, 0)),
         ("UpperArmL", 2, (0.18, 1.40, 0)), ("LowerArmL", 4, (0.32, 1.12, 0.06)),
         ("UpperArmR", 2, (-0.18, 1.40, 0)), ("LowerArmR", 6, (-0.32, 1.12, 0.06)),
         ("UpperLegL", 1, (0.09, 0.95, 0)), ("LowerLegL", 8, (0.12, 0.48, 0.02)),
         ("UpperLegR", 1, (-0.09, 0.95, 0)), ("LowerLegR", 10, (-0.12, 0.48, 0.02)),
         ("MouthSmile", 3, (0, 1.555, 0.15)), ("MouthOpen", 3, (0, 1.555, 0.15), False)]   # (the open mouth starts hidden)
BONE = {b[0]: i for i, b in enumerate(BONES)}


def _on(bone, mesh):
    """tag every vertex of a piece with the bone that moves it"""
    P, Nn, I = mesh; return P, Nn, I, [BONE[bone]] * len(P)


def _joinb(*parts):
    P, Nn, I, B = [], [], [], []
    for p, n, i, bb in parts:
        o = len(P); P += p; Nn += n; I += [o + x for x in i]; B += bb
    return P, Nn, I, B


def _eyes(closed=False):
    """two eye beads on the face; closed: squashed flat (the "Closed" key shape)"""
    P, Nn, I = _join(_ball(-0.055, 1.66, 0.135, 0.03, 6, 8), _ball(0.055, 1.66, 0.135, 0.03, 6, 8))
    if closed: P = [(x, 1.655 + (y - 1.66) * 0.12, z - 0.005) for x, y, z in P]
    return P, Nn, I


def stick_figure():
    """the player: 1.8 m tall, standing on its own origin (feet at y 0, facing +z). Parts are named
    "Part__Material" (the colour clips work by material); each piece moves with one bone (left = +x, the figure's
    own left). The face: eyes with a "Closed" key shape (blinking), a smile and an open mouth on their own bones
    (the visibility clips swap them)."""
    eyes = _on("Head", _eyes()); closed = _eyes(True)[0]
    smile = _joinb(*[_on("MouthSmile", _box(x, 1.555 + 0.012 * (x / 0.04) ** 2, 0.15, 0.022, 0.012, 0.012)) for x in (-0.04, -0.02, 0, 0.02, 0.04)])
    opened = _on("MouthOpen", _ball(0, 1.55, 0.148, 0.028, 6, 8))
    return [("Head__Mt_Skin", _on("Head", _ball(0, 1.62, 0, 0.16))),
            ("Eyes__Mt_Eye", eyes, None, {"bone": BONE["Head"], "keys": [("Closed", closed)]}),
            ("MouthSmile__Mt_Mouth", smile, None, {"bone": BONE["MouthSmile"]}),
            ("MouthOpen__Mt_Mouth", opened, None, {"bone": BONE["MouthOpen"]}),
            ("Body__Mt_Shirt", _joinb(_on("Spine", _limb(0, 0.95, 0, 0, 1.46, 0, 0.05)), _on("Spine", _box(0, 1.40, 0, 0.36, 0.05, 0.08)))),
            ("Arms__Mt_Skin", _joinb(_on("UpperArmL", _limb(0.18, 1.40, 0, 0.32, 1.12, 0.06, 0.035)), _on("LowerArmL", _limb(0.32, 1.12, 0.06, 0.36, 0.86, 0.12, 0.03)),
                                     _on("UpperArmR", _limb(-0.18, 1.40, 0, -0.32, 1.12, 0.06, 0.035)), _on("LowerArmR", _limb(-0.32, 1.12, 0.06, -0.36, 0.86, 0.12, 0.03)))),
            ("Legs__Mt_Trousers", _joinb(_on("UpperLegL", _limb(0.09, 0.95, 0, 0.12, 0.48, 0.02, 0.045)), _on("LowerLegL", _limb(0.12, 0.48, 0.02, 0.13, 0.05, 0, 0.04)),
                                         _on("UpperLegR", _limb(-0.09, 0.95, 0, -0.12, 0.48, 0.02, 0.045)), _on("LowerLegR", _limb(-0.12, 0.48, 0.02, -0.13, 0.05, 0, 0.04)),
                                         _on("LowerLegL", _box(0.13, 0.03, 0.06, 0.10, 0.06, 0.22)), _on("LowerLegR", _box(-0.13, 0.03, 0.06, 0.10, 0.06, 0.22))))]


def _blink(f):
    """eyes shut quickly at frames 40-46 (weight 0 -> 1 -> 0)"""
    return max(0.0, 1 - abs(f - 43) / 3.0) if 40 <= f <= 46 else 0.0


def _talk(period):
    """open / closed mouth keys: [(frame, open)] alternating every period frames, for 60 frames"""
    return [(f, (f // period) % 2 == 1) for f in range(0, 61, period)]


PLAYER_SHAPE_CLIPS = [("Blink", 90, [("Eyes__Mt_Eye", [("Closed", _blink)])])]
PLAYER_VIS_CLIPS = [("Talk", 60, [("MouthOpen", _talk(8)), ("MouthSmile", [(f, not o) for f, o in _talk(8)])]),
                    ("Wave", 60, [("MouthOpen", _talk(12)), ("MouthSmile", [(f, not o) for f, o in _talk(12)])])]
PLAYER_MAT_CLIPS = [("ShirtRed", 0, [("Mt_Shirt", "basecolor_const", (1.0, 0.25, 0.2))]),
                    ("ShirtGreen", 0, [("Mt_Shirt", "basecolor_const", (0.3, 1.0, 0.35))]),
                    ("ShirtGlow", 60, [("Mt_Shirt", "emissioncolor_const", {0: lambda f: 0.6 * (0.5 - 0.5 * math.cos(2 * math.pi * f / 60)),
                                                                             1: lambda f: 0.45 * (0.5 - 0.5 * math.cos(2 * math.pi * f / 60)),
                                                                             2: lambda f: 0.1})])]


# ---------------------------------------------------------------- a hat (attached to the player's head by actor data)
HAT_BONES = [("Root", -1, (0, 0, 0)), ("Propeller", 0, (0, 0.17, 0))]
HAT_BONE = {nm: i for i, (nm, _, _) in enumerate(HAT_BONES)}


def hat_model():
    """a cap with a brim and a propeller on top; its origin is where it sits on the head"""
    cap = _ball(0, 0.0, 0, 0.17, 6, 12); cap = ([(x, max(y, 0.0), z) for x, y, z in cap[0]], cap[1], cap[2])
    brim = _box(0, 0.005, 0.12, 0.26, 0.012, 0.16)
    stem = _limb(0, 0.15, 0, 0, 0.19, 0, 0.012, 6)
    blades = _join(_box(0.08, 0.19, 0, 0.16, 0.006, 0.03), _box(-0.08, 0.19, 0, 0.16, 0.006, 0.03))
    def on(b, m): P, Nn, I = m; return P, Nn, I, [HAT_BONE[b]] * len(P)
    return [("Cap__Mt_Hat", _joinb(on("Root", cap), on("Root", brim))),
            ("Propeller__Mt_Propeller", _joinb(on("Propeller", stem), on("Propeller", blades)), None, {"bone": HAT_BONE["Propeller"]})]


def _spin(speed):
    return lambda f: {("Propeller", "ry"): 2 * math.pi * speed * f / 60}


HAT_CLIPS = [("Idle", 90, _spin(1 / 3)), ("Walk", 60, _spin(1)), ("Wave", 60, _spin(2))]   # (named like the player's)


# ---------------------------------------------------------------- a flag with cloth (Phive/Cloth/DemoFlag)
def flag_model():
    """a pole and a 1 x 0.75 m flag whose corners sit on the cloth particles (one bone per particle)"""
    import demo_havok as H
    Pt = H.flag_particles(); C, R = H.FLAG_COLS, H.FLAG_ROWS
    bones = [("Root", -1, (0, 0, 0))] + [("Flag_%d_%d" % (i // C, i % C), 0, p) for i, p in enumerate(Pt)]
    pole = _join(_limb(0, 0, 0, 0, H.POLE_H, 0, 0.035, 8), _ball(0, H.POLE_H + 0.03, 0, 0.05, 5, 8))
    P, Nn, I, B, UV = [], [], [], [], []
    for side, nz in ((0, 1.0), (1, -1.0)):                       # both sides
        base = len(P)
        for i, p in enumerate(Pt):
            P.append(p); Nn.append((0, 0, nz)); B.append(i + 1); UV.append(((i % C) / (C - 1), (i // C) / (R - 1)))
        for r in range(R - 1):
            for c in range(C - 1):
                a, b, d, e = base + r * C + c, base + r * C + c + 1, base + (r + 1) * C + c, base + (r + 1) * C + c + 1
                I += [a, d, b, b, d, e] if side == 0 else [a, b, d, b, e, d]
    return bones, [("Pole__Mt_Pole", (pole[0], pole[1], pole[2], [0] * len(pole[0]))),
                   ("Cloth__Mt_Flag", (P, Nn, I, B, UV), "DemoFlag_Cloth")]


def flag_texture(w=128, h=96):
    """a made-up flag: two colours, a wave line and a star"""
    from PIL import Image, ImageDraw
    im = Image.new("RGBA", (w, h), (30, 110, 200, 255)); d = ImageDraw.Draw(im)
    d.polygon([(0, h), (w, h), (w, h * 0.55), (0, h * 0.75)], fill=(250, 210, 60, 255))
    cx, cy, r = w * 0.32, h * 0.33, h * 0.2
    pts = [(cx + (r if k % 2 == 0 else r * 0.45) * math.sin(k * math.pi / 5), cy - (r if k % 2 == 0 else r * 0.45) * math.cos(k * math.pi / 5)) for k in range(10)]
    d.polygon(pts, fill=(255, 255, 255, 255))
    return im.tobytes()


class _Bin:
    """a little file builder: write blocks, patch 64-bit pointers to them afterwards"""
    def __init__(self, size=0):
        self.b = bytearray(size); self.patches = []
    def align(self, a):
        self.b += bytes((-len(self.b)) % a)
    def put(self, data, a=8):
        self.align(a); o = len(self.b); self.b += data; return o
    def ptr(self, at, target):
        struct.pack_into("<Q", self.b, at, target)
    def string(self, s):
        e = s.encode("ascii"); return self.put(struct.pack("<H", len(e)) + e + b"\0", 2)


def bfres_file(name, parts, bones=BONES):
    """BFRES (version 10) with one model: a skeleton (no rotations in the standing pose, Euler angles), and per part
    a shape, a material and a vertex buffer (float position + normal, a 1-byte bone index), triangle lists with
    16-bit indices. Each vertex moves with one bone (rigid skinning); positions are stored relative to that bone's
    joint, as the format wants. No textures."""
    W = _Bin(0x100)
    W.b[0:8] = b"FRES    "
    W.b[8], W.b[9] = 0, 2; struct.pack_into("<H", W.b, 0x0A, 10)       # version 10.2.0
    struct.pack_into("<H", W.b, 0x0C, 0xFEFF)
    W.ptr(0x20, W.string(name))
    n = len(parts)
    model = W.put(bytes(0x78))
    W.ptr(0x28, model); struct.pack_into("<H", W.b, 0xDC, 1)
    W.ptr(model + 8, W.string(name)); W.ptr(model + 0x10, W.string(name))
    struct.pack_into("<HHH", W.b, model + 0x68, n, n, n)              # vertex buffers, shapes, materials
    # the skeleton
    nb = len(bones)
    sk = W.put(bytes(0x50)); W.b[sk:sk + 4] = b"FSKL"
    barr = W.put(bytes(0x58 * nb)); m2b = W.put(struct.pack("<%dH" % nb, *range(nb)))
    W.ptr(sk + 0x10, barr); W.ptr(sk + 0x18, m2b)
    struct.pack_into("<HHH", W.b, sk + 0x38, nb, 0, nb)               # bones, smooth / rigid skinning matrices
    for i, bone in enumerate(bones):
        bn, par, pos = bone[:3]; shown = bone[3] if len(bone) > 3 else True
        bo = barr + 0x58 * i; pp = bones[par][2] if par >= 0 else (0, 0, 0)
        W.ptr(bo, W.string(bn))
        struct.pack_into("<HhhhhH", W.b, bo + 0x20, i, par, -1, i, -1, 0)
        struct.pack_into("<I", W.b, bo + 0x2C, 0x1000 | (1 if shown else 0))   # (in)visible, Euler rotation
        struct.pack_into("<3f4f3f", W.b, bo + 0x30, 1, 1, 1, 0, 0, 0, 1, *[pos[k] - pp[k] for k in range(3)])
    W.ptr(model + 0x18, sk)
    vbufs = W.put(bytes(0x58 * n)); shapes = W.put(bytes(0x60 * n)); mats = W.put(bytes(0xB0 * n))
    W.ptr(model + 0x20, vbufs); W.ptr(model + 0x28, shapes); W.ptr(model + 0x38, mats)
    names = {k: W.string(k) for k in ("_p0", "_n0", "_i0", "_u0", "_a0", "_p1", "_p2", "_p3")}
    data = bytearray()                                                 # the buffer data (vertices, then indices)
    for i, part in enumerate(parts):
        pname, mesh = part[0], part[1]; tex = part[2] if len(part) > 2 else None   # (name, mesh[, albedo texture[, extras]])
        extra = part[3] if len(part) > 3 else {}                            # extras: "bone" (the shape's bone), "keys"
        keys = extra.get("keys", [])                                        # key shapes: [(name, positions)] -> _p1, _p2...
        P, Nn, I, B = mesh[:4]; UV = mesh[4] if len(mesh) > 4 else [(0.0, 0.0)] * len(P)
        vb, sh, mt = vbufs + 0x58 * i, shapes + 0x60 * i, mats + 0xB0 * i
        stride = 36 + 12 * len(keys)
        alist = [("_p0", 0x05, 0x18, 0), ("_n0", 0x05, 0x18, 12), ("_i0", 0x03, 0x02, 24), ("_u0", 0x05, 0x17, 28)] + \
                [("_p%d" % (k + 1), 0x05, 0x18, 36 + 12 * k) for k in range(len(keys))]
        attrs = W.put(bytes(16 * len(alist)))
        for k, (an, typ, lay, off) in enumerate(alist):
            a = attrs + 16 * k
            W.ptr(a, names[an]); W.b[a + 8], W.b[a + 9] = typ, lay       # float x 3 / one unsigned byte
            struct.pack_into("<HH", W.b, a + 0x0C, off, 0)
        sizes = W.put(bytes(16)); strides = W.put(bytes(16))
        while len(data) % 8: data += b"\0"
        voff = len(data)
        for vi, (p, nn, bi, uv) in enumerate(zip(P, Nn, B, UV)):
            j = bones[bi][2]
            data += struct.pack("<6fB3x2f", p[0] - j[0], p[1] - j[1], p[2] - j[2], *nn, bi, *uv)
            for _, kp in keys: q = kp[vi]; data += struct.pack("<3f", q[0] - j[0], q[1] - j[1], q[2] - j[2])
        struct.pack_into("<I", W.b, sizes, stride * len(P)); struct.pack_into("<I", W.b, strides, stride)
        W.ptr(vb + 8, attrs); W.ptr(vb + 0x30, sizes); W.ptr(vb + 0x38, strides)
        struct.pack_into("<i", W.b, vb + 0x48, voff)
        W.b[vb + 0x4C], W.b[vb + 0x4D] = len(alist), 1
        struct.pack_into("<IHH", W.b, vb + 0x50, len(P), 1, 8)
        while len(data) % 8: data += b"\0"
        ioff = len(data); data += struct.pack("<%dH" % len(I), *I)
        mesh = W.put(bytes(0x38)); sub = W.put(struct.pack("<II", 0, len(I))); bsz = W.put(struct.pack("<I", 2 * len(I)))
        W.ptr(mesh, sub); W.ptr(mesh + 0x18, bsz)
        struct.pack_into("<IIIII", W.b, mesh + 0x20, ioff, 3, 1, len(I), 0)   # triangles, 16-bit indices
        struct.pack_into("<H", W.b, mesh + 0x34, 1)
        W.ptr(sh + 8, W.string(pname)); W.ptr(sh + 0x18, mesh)
        struct.pack_into("<HHH", W.b, sh + 0x52, i, extra.get("bone", 0), i)   # material, bone, vertex buffer
        if keys:                                                           # key shape names: root, the base, then each key
            kd = W.put(b"_DIC" + struct.pack("<i", len(keys) + 1) + bytes(16 * (len(keys) + 2)))
            struct.pack_into("<iHH", W.b, kd + 8, -1, 1, 0); W.ptr(kd + 0x10, W.string(""))
            for k, kn in enumerate(["Base"] + [n for n, _ in keys]): W.ptr(kd + 8 + 16 * (k + 1) + 8, W.string(kn))
            W.ptr(sh + 0x30, kd); W.b[sh + 0x5C] = len(keys) + 1
        W.b[sh + 0x5A] = 1                                                 # one bone per vertex (its _i0)
        W.b[sh + 0x5B] = 1                                                 # one mesh (no lower detail levels)
        W.ptr(mt + 8, W.string(pname.split("__")[-1]))                     # material: "Part__Material" -> Material
        if tex:                                                            # its albedo texture: Tex\<tex>.bntx.zs
            tn = W.put(struct.pack("<Q", W.string(tex)))
            sd = W.put(b"_DIC" + struct.pack("<i", 1) + bytes(32))         # sampler names: a root entry, then _a0
            struct.pack_into("<iHH", W.b, sd + 8, -1, 1, 0); W.ptr(sd + 0x10, W.string(""))
            W.ptr(sd + 0x20, names["_a0"])
            W.ptr(mt + 0x20, tn); W.ptr(mt + 0x38, sd); W.b[mt + 0xA2], W.b[mt + 0xA3] = 1, 1
    W.align(0x1000)
    base = len(W.b); W.b += data
    info = W.put(bytes(0x10)); struct.pack_into("<I", W.b, info + 4, len(data)); W.ptr(info + 8, base)
    W.ptr(0xB0, info)
    struct.pack_into("<I", W.b, 0x1C, len(W.b))                          # (file size)
    return zstandard.ZstdCompressor(level=19).compress(bytes(W.b))


# ---------------------------------------------------------------- step 4: the player's animations
def _walk(f):
    """60 frames, a walk on the spot: legs and arms swing, knees bend, hips bob, a little twist"""
    ph = 2 * math.pi * f / 60
    return {("UpperLegL", "rx"): -0.45 * math.sin(ph), ("UpperLegR", "rx"): 0.45 * math.sin(ph),
            ("LowerLegL", "rx"): 0.35 * (1 + math.sin(ph - 1.3)), ("LowerLegR", "rx"): 0.35 * (1 + math.sin(ph + math.pi - 1.3)),
            ("UpperArmL", "rx"): 0.40 * math.sin(ph), ("UpperArmR", "rx"): -0.40 * math.sin(ph),
            ("LowerArmL", "rx"): -0.30 + 0.12 * math.sin(ph), ("LowerArmR", "rx"): -0.30 - 0.12 * math.sin(ph),
            ("Hips", "ty"): 0.935 + 0.015 * math.cos(2 * ph), ("Spine", "ry"): 0.12 * math.sin(ph)}


def _wave(f):
    """60 frames: the right arm up, the hand waving; the head tilts along"""
    ph = 2 * math.pi * f / 60
    return {("UpperArmR", "rz"): -2.0 + 0.05 * math.sin(ph), ("LowerArmR", "rz"): -0.75 + 0.55 * math.sin(2 * ph),
            ("Head", "rz"): 0.10 * math.sin(2 * ph), ("Spine", "rz"): 0.04 * math.sin(2 * ph)}


def _idle(f):
    """90 frames: breathing, looking left and right"""
    ph = 2 * math.pi * f / 90
    return {("Spine", "rx"): 0.03 * math.sin(2 * ph), ("Head", "ry"): 0.45 * math.sin(ph),
            ("UpperArmL", "rz"): 0.06 + 0.03 * math.sin(2 * ph), ("UpperArmR", "rz"): -0.06 - 0.03 * math.sin(2 * ph)}


CLIPS = [("Idle", 90, _idle), ("Walk", 60, _walk), ("Wave", 60, _wave)]
_TARGET = {"tx": 0x10, "ty": 0x14, "tz": 0x18, "rx": 0x20, "ry": 0x24, "rz": 0x28}


def _curve(W, at, frames_keys, target):
    """a smooth curve (Switch 2 style: f32 frames, f32 value + slope per key) at 'at' (0x30 bytes)"""
    fr = [k[0] for k in frames_keys]
    fo = W.put(struct.pack("<%df" % len(fr), *fr), 4)
    ko = W.put(b"".join(struct.pack("<2f", v, d) for _, v, d in frames_keys), 4)
    W.ptr(at, fo); W.ptr(at + 8, ko)
    struct.pack_into("<HHI3f2f", W.b, at + 0x10, 0x4000, len(fr), target, fr[0], fr[-1], 1.0, 1.0, 0.0)


def _sample(fn, frames, step=2):
    """fn(frame) -> value: keys every 'step' frames with slopes"""
    return [(float(x), float(fn(x)), float((fn(x + 0.01) - fn(x - 0.01)) / 0.02)) for x in range(0, frames + 1, step)]


def anim_file(name, clips=None, bone_index=None, shape_clips=(), vis_clips=(), mat_clips=()):
    """X.anim.bfres with
      skeletal clips (FSKA): (name, frames, fn(frame) -> {(bone, "rx"/"ty"...): value}) - looping, Euler angles
      shape clips (FSHA): (name, frames, [(shape, [(key shape, fn(frame) -> weight)])]) - e.g. blinking eyelids
      visibility clips (FBVS): (name, frames, [(bone, [(frame, shown)])]) - parts on a bone shown / hidden
      material clips (FMAA): (name, frames, [(material, param, (r, g, b) or {channel: fn(frame)})]) - colour clips
    Clips of different kinds with the same name play together."""
    clips = CLIPS if clips is None else clips
    bone_index = bone_index or BONE
    W = _Bin(0x100)
    W.b[0:8] = b"FRES    "
    W.b[8], W.b[9] = 0, 2; struct.pack_into("<H", W.b, 0x0A, 10)
    struct.pack_into("<H", W.b, 0x0C, 0xFEFF)
    W.ptr(0x20, W.string(name))
    if clips:
        arr = W.put(bytes(0x50 * len(clips)))
        W.ptr(0x58, arr); struct.pack_into("<H", W.b, 0xE2, len(clips))
    for ci, (cname, frames, fn) in enumerate(clips):
        o = arr + 0x50 * ci
        W.b[o:o + 4] = b"FSKA"; struct.pack_into("<I", W.b, o + 4, 0x1004)    # Euler angles, looping
        W.ptr(o + 8, W.string(cname))
        struct.pack_into("<I", W.b, o + 0x40, frames + 1)                    # (keys 0..frames; the loop wraps at the last)
        tracks = sorted(fn(0).keys(), key=lambda bt: (bone_index[bt[0]], _TARGET[bt[1]]))
        bones = []
        for bn, t in tracks:
            if not bones or bones[-1][0] != bn: bones.append((bn, []))
            bones[-1][1].append(t)
        ba = W.put(bytes(0x38 * len(bones)))
        W.ptr(o + 0x28, ba); struct.pack_into("<H", W.b, o + 0x4C, len(bones))
        for bi, (bn, ts) in enumerate(bones):
            bo = ba + 0x38 * bi
            W.ptr(bo, W.string(bn))
            cur = W.put(bytes(0x30 * len(ts)))
            W.ptr(bo + 8, cur); W.b[bo + 0x2E] = len(ts)
            for k, t in enumerate(ts):
                _curve(W, cur + 0x30 * k, _sample(lambda x, bn=bn, t=t: fn(x)[(bn, t)], frames), _TARGET[t])
    # shape clips (FSHA, 0x50 each): vertex shape anims (0x30: shape, curves, base weights, key infos, counts)
    if shape_clips:
        arr = W.put(bytes(0x50 * len(shape_clips))); W.ptr(0x88, arr); struct.pack_into("<H", W.b, 0xE8, len(shape_clips))
    for ci, (cname, frames, shapes) in enumerate(shape_clips):
        o = arr + 0x50 * ci; W.b[o:o + 4] = b"FSHA"; struct.pack_into("<H", W.b, o + 4, 4)
        W.ptr(o + 8, W.string(cname)); struct.pack_into("<I", W.b, o + 0x40, frames + 1)
        va = W.put(bytes(0x30 * len(shapes))); W.ptr(o + 0x28, va)
        struct.pack_into("<HH", W.b, o + 0x4A, len(shapes), sum(len(k) + 1 for _, k in shapes))
        for si, (shape, keys) in enumerate(shapes):
            q = va + 0x30 * si; W.ptr(q, W.string(shape))
            cur = W.put(bytes(0x30 * len(keys))); W.ptr(q + 8, cur)
            W.ptr(q + 0x10, W.put(struct.pack("<%df" % len(keys), *[0.0] * len(keys)), 4))
            ki = W.put(bytes(16 * (len(keys) + 1))); W.ptr(q + 0x18, ki)
            W.ptr(ki, W.string("Base")); W.b[ki + 8] = 0xFF
            for k, (kn, fn) in enumerate(keys):
                W.ptr(ki + 16 * (k + 1), W.string(kn)); W.b[ki + 16 * (k + 1) + 8] = k
                _curve(W, cur + 0x30 * k, _sample(fn, frames, 1), 0)
            struct.pack_into("<HH", W.b, q + 0x20, len(keys), len(keys) + 1)
    # bone visibility clips (FBVS, 0x60 each): bone names, start bits, step curves (a bit per key)
    if vis_clips:
        arr = W.put(bytes(0x60 * len(vis_clips))); W.ptr(0x78, arr); struct.pack_into("<H", W.b, 0xE6, len(vis_clips))
    for ci, (cname, frames, tracks) in enumerate(vis_clips):
        o = arr + 0x60 * ci; W.b[o:o + 4] = b"FBVS"; W.ptr(o + 8, W.string(cname))
        struct.pack_into("<I", W.b, o + 0x50, frames + 1); struct.pack_into("<HH", W.b, o + 0x58, len(tracks), len(tracks))
        nm = W.put(bytes(8 * len(tracks))); W.ptr(o + 0x38, nm)
        bits = bytearray((len(tracks) + 7) // 8)
        cur = W.put(bytes(0x30 * len(tracks))); W.ptr(o + 0x28, cur)
        for ti, (bone, keys) in enumerate(tracks):
            W.ptr(nm + 8 * ti, W.string(bone))
            if keys[0][1]: bits[ti // 8] |= 1 << (ti % 8)
            c = cur + 0x30 * ti
            fo = W.put(struct.pack("<%df" % len(keys), *[float(k[0]) for k in keys]), 4)
            kb = bytearray((len(keys) + 7) // 8)
            for j, (_, on) in enumerate(keys):
                if on: kb[j // 8] |= 1 << (j % 8)
            ko = W.put(bytes(kb), 4); W.ptr(c, fo); W.ptr(c + 8, ko)
            struct.pack_into("<HHI3f2f", W.b, c + 0x10, 0, len(keys), ti, float(keys[0][0]), float(keys[-1][0]), 1.0, 1.0, 0.0)
        W.ptr(o + 0x30, W.put(bytes(bits), 4))
    # material clips (FMAA, 0x70 each): material anims (0x40), parameter infos (0x18), curves, constants
    if mat_clips:
        arr = W.put(bytes(0x70 * len(mat_clips))); W.ptr(0x68, arr); struct.pack_into("<H", W.b, 0xE4, len(mat_clips))
    for ci, (cname, frames, mats) in enumerate(mat_clips):
        o = arr + 0x70 * ci; W.b[o:o + 4] = b"FMAA"; W.ptr(o + 8, W.string(cname))
        struct.pack_into("<I", W.b, o + 0x58, frames + 1); struct.pack_into("<H", W.b, o + 0x62, len(mats))
        ma = W.put(bytes(0x40 * len(mats))); W.ptr(o + 0x28, ma)
        for mi, (mat, param, val) in enumerate(mats):
            q = ma + 0x40 * mi; W.ptr(q, W.string(mat))
            pi = W.put(bytes(0x18)); W.ptr(q + 8, pi); W.ptr(pi, W.string(param))
            if isinstance(val, dict):                                     # animated: a curve per channel
                cur = W.put(bytes(0x30 * len(val))); W.ptr(q + 0x18, cur)
                for k, (ch, fn) in enumerate(sorted(val.items())): _curve(W, cur + 0x30 * k, _sample(fn, frames), ch * 4)
                struct.pack_into("<HHHHH", W.b, pi + 8, 0, len(val), 0, 0, 0)
            else:                                                         # still: one constant per channel
                cs = W.put(b"".join(struct.pack("<If", ch * 4, v) for ch, v in enumerate(val))); W.ptr(q + 0x20, cs)
                struct.pack_into("<HHHHH", W.b, pi + 8, 0, 0, 0, 0, len(val))
            struct.pack_into("<H", W.b, q + 0x32, 1)
    struct.pack_into("<I", W.b, 0x1C, len(W.b))
    return zstandard.ZstdCompressor(level=19).compress(bytes(W.b))


# ---------------------------------------------------------------- step 5: a second object, a wooden crate
CRATE = 1.0                         # metres along each side


def crate_mesh(size=CRATE):
    """a cube standing on its own origin, each face mapped to the whole texture"""
    P, Nn, I = _box(0, size / 2, 0, size, size, size); B = [0] * len(P)
    h = size / 2; UV = []
    for p, n in zip(P, Nn):                                    # planks run across every side; top and bottom too
        if n[1]: UV.append(((p[0] + h) / size, (p[2] + h) / size))
        elif n[0]: UV.append(((p[2] + h) / size * (1 if n[0] < 0 else -1) % 1.0001, 1 - p[1] / size))
        else: UV.append(((p[0] + h) / size * (1 if n[2] > 0 else -1) % 1.0001, 1 - p[1] / size))
    return P, Nn, I, B, UV


def crate_texture(n=256):
    """planks with grain, a frame round the edge and a diagonal brace"""
    px = bytearray(); edge = n // 9
    for y in range(n):
        for x in range(n):
            frame = x < edge or x >= n - edge or y < edge or y >= n - edge
            brace = abs((x - y)) < edge * 0.7 and not frame
            if frame or brace:
                side = x < edge or x >= n - edge                                    # grain runs along each board
                u, w = ((x - y) * 1.4, x + y) if brace else ((x, y) if side else (y, x))
                g = math.sin(u * 0.9 + 2.5 * math.sin(w * 0.03 + u * 0.05)) * 0.5
                r, gg, b = 150 + 25 * g, 100 + 18 * g, 52 + 10 * g
                if frame and (min(x, y, n - 1 - x, n - 1 - y) in (0, 1, edge - 1, edge - 2)): r, gg, b = r * 0.55, gg * 0.55, b * 0.55
            else:
                plank = y * 5 // n                                                  # 5 horizontal planks
                g = math.sin(x * 0.11 + 4 * math.sin(y * 0.09 + plank * 1.7)) * 0.5 + 0.2 * math.sin(x * 0.9 + y * 0.3)
                r, gg, b = 190 + 28 * g - plank * 4, 135 + 20 * g - plank * 3, 75 + 12 * g
                if (y * 5) % n < 5: r, gg, b = r * 0.45, gg * 0.45, b * 0.45      # the gaps between planks
            if brace and abs(x - y) >= edge * 0.7 - 1.5: r, gg, b = r * 0.6, gg * 0.6, b * 0.6
            nail = any(abs(x - cx) < 3 and abs(y - cy) < 3 for cx in (edge // 2, n - edge // 2) for cy in (edge // 2, n - edge // 2))
            if nail: r = gg = b = 70
            px += bytes((int(max(0, min(255, r))), int(max(0, min(255, gg))), int(max(0, min(255, b))), 255))
    return bytes(px)


CRATE_ISLAND = 1                    # the small island the crate stands on


FLAG_ISLAND = 2                     # the small island the flag stands on


def flag_spot():
    ix, iz, ir, _ = SMALL[FLAG_ISLAND]
    x, z = ix - 0.1 * ir, iz + 0.05 * ir
    return (round(x, 2), round(height(x, z) - 0.2, 2), round(z, 2))


def crate_spot():
    """near the top of its island, on the flattest nearby ground; turned a little"""
    ix, iz, ir, _ = SMALL[CRATE_ISLAND]
    x, z = ix + 0.12 * ir, iz - 0.08 * ir
    h = min(height(x + dx, z + dz) for dx in (-0.5, 0.5) for dz in (-0.5, 0.5))    # the lowest corner: no floating corner
    return (round(x, 2), round(h, 2), round(z, 2)), 0.4


# ---------------------------------------------------------------- step 3: placing things (a BYML object list)
class U64(int):
    """a 64-bit unsigned BYML value (object ids)"""


def byml_file(root):
    """BYML version 7 (little-endian) from Python dicts / lists / str / float / int / bool / U64. Keys and strings go
    into sorted tables, as the format wants."""
    keys, strs = set(), set()
    def scan(o):
        if isinstance(o, dict):
            for k, v in o.items(): keys.add(k); scan(v)
        elif isinstance(o, list):
            for v in o: scan(v)
        elif isinstance(o, str): strs.add(o)
    scan(root)
    keys, strs = sorted(keys), sorted(strs)
    ki = {k: i for i, k in enumerate(keys)}; si = {t: i for i, t in enumerate(strs)}
    def table(items):
        out = bytearray(b"\xC2" + struct.pack("<I", len(items))[:3])
        offs = []; body = bytearray(); start = 4 + 4 * (len(items) + 1)
        for t in items: offs.append(start + len(body)); body += t.encode("utf-8") + b"\0"
        offs.append(start + len(body))
        for o in offs: out += struct.pack("<I", o)
        out += body
        while len(out) % 4: out += b"\0"
        return out
    b = bytearray(16)
    kt = len(b); b += table(keys)
    st = len(b); b += table(strs) if strs else b""
    def value(o):                      # (type, 32-bit value); containers / 64-bit values are written first
        if isinstance(o, bool): return 0xD0, int(o)
        if isinstance(o, U64):
            while len(b) % 4: b.append(0)
            at = len(b); b.extend(struct.pack("<Q", o)); return 0xD5, at
        if isinstance(o, int): return 0xD1, o & 0xFFFFFFFF
        if isinstance(o, float): return 0xD2, struct.unpack("<I", struct.pack("<f", o))[0]
        if isinstance(o, str): return 0xA0, si[o]
        if isinstance(o, dict):
            items = sorted(o.items(), key=lambda kv: kv[0]); vals = [value(v) for _, v in items]
            while len(b) % 4: b.append(0)
            at = len(b); b.extend(b"\xC1" + struct.pack("<I", len(items))[:3])
            for (k, _), (t, v) in zip(items, vals): b.extend(struct.pack("<I", ki[k])[:3] + bytes([t]) + struct.pack("<I", v))
            return 0xC1, at
        if isinstance(o, list):
            vals = [value(v) for v in o]
            while len(b) % 4: b.append(0)
            at = len(b); b.extend(b"\xC0" + struct.pack("<I", len(o))[:3] + bytes(t for t, _ in vals))
            while len(b) % 4: b.append(0)
            for _, v in vals: b.extend(struct.pack("<I", v))
            return 0xC0, at
        raise TypeError(type(o))
    _, rt = value(root)
    b[0:16] = b"YB" + struct.pack("<HIII", 7, kt, st if strs else 0, rt)
    return bytes(b)


def placement_file(actors):
    """an object list: Actors = [ { Name, Gyaml, Hash, Translate, Rotate (radians), Scale } ]"""
    out = []
    for i, (name, T, R) in enumerate(actors):
        out.append({"Name": name, "Gyaml": name, "Hash": U64(0x0DE0000000000001 + i),
                    "Translate": [float(c) for c in T], "Rotate": [float(c) for c in R], "Scale": [1.0, 1.0, 1.0]})
    return zstandard.ZstdCompressor(level=19).compress(byml_file({"Actors": out}))


# ---------------------------------------------------------------- build
def main():
    tdir = os.path.join(ROMFS, "Terrain", SCENE)
    os.makedirs(tdir, exist_ok=True)
    os.makedirs(os.path.join(ROMFS, "Tex"), exist_ok=True)
    areas = []
    for lv in range(LEVELS):
        k = 2 ** lv
        s = WORLD / k
        for j in range(k):
            for i in range(k):
                cx = -WORLD / 2 + (i + 0.5) * s
                cz = -WORLD / 2 + (j + 0.5) * s
                name = "L%d_X%d_Z%d" % (lv, i, j)                   # level, column, row
                areas.append((cx, cz, s, name))
    water = water_file()
    for (cx, cz, s, name) in areas:
        x0, z0 = (cx - s / 2) * WS, (cz - s / 2) * WS
        step = s * WS / (N - 1)
        hs, ms = [], []
        for r in range(N):                                         # rows = z, columns = x
            z = z0 + r * step
            for c in range(N):
                x = x0 + c * step
                hv = height(x, z)
                hs.append(int(round(hv / MAX_H * 65535)))
                ms.append(material(x, z, hv))
        with open(os.path.join(tdir, name + ".hght"), "wb") as f: f.write(hght_file(hs))
        with open(os.path.join(tdir, name + ".mate"), "wb") as f: f.write(mate_file(ms))
        with open(os.path.join(tdir, name + ".water.extm"), "wb") as f: f.write(water)
        print("tile", name, "size %g m" % (s * WS))
    with open(os.path.join(ROMFS, "Terrain", SCENE + ".tscb"), "wb") as f: f.write(tscb_file(areas))
    # the ground texture: one layer per ground material (0 grass, 1 sand); the water's ripples
    with open(os.path.join(ROMFS, "Tex", SCENE + "_Ground.bntx.zs"), "wb") as f:
        f.write(bntx_file(SCENE + "_Ground", 128, 128, [land_texture(), land_texture(sand=True)]))
    with open(os.path.join(ROMFS, "Tex", SCENE + "_Water.bntx.zs"), "wb") as f:
        f.write(bntx_file(SCENE + "_Water", 128, 128, [water_texture()]))
    # step 2: the player model
    os.makedirs(os.path.join(ROMFS, "Model"), exist_ok=True)
    with open(os.path.join(ROMFS, "Model", "DemoPlayer.DemoPlayer.bfres.zs"), "wb") as f:
        f.write(bfres_file("DemoPlayer", stick_figure()))
    with open(os.path.join(ROMFS, "Model", "DemoPlayer.anim.bfres.zs"), "wb") as f:      # step 4: its animations
        f.write(anim_file("DemoPlayer", shape_clips=PLAYER_SHAPE_CLIPS, vis_clips=PLAYER_VIS_CLIPS, mat_clips=PLAYER_MAT_CLIPS))
    # step 3: the player placed in the world (Banc/World_<scene>/: the scene's object lists)
    bdir = os.path.join(ROMFS, "Banc", "World_" + SCENE)
    os.makedirs(bdir, exist_ok=True)
    T, ry = player_spot()
    CT, cry = crate_spot()
    # step 5: the crate model and its texture
    with open(os.path.join(ROMFS, "Model", "DemoCrate.DemoCrate.bfres.zs"), "wb") as f:
        f.write(bfres_file("DemoCrate", [("Crate", crate_mesh(), "DemoCrate_Wood")], bones=[("Root", -1, (0, 0, 0))]))
    with open(os.path.join(ROMFS, "Tex", "DemoCrate_Wood.bntx.zs"), "wb") as f:
        f.write(bntx_file("DemoCrate_Wood", 256, 256, [crate_texture()]))
    FT = flag_spot()
    with open(os.path.join(bdir, SCENE + "_Main.bcett.byml.zs"), "wb") as f:
        f.write(placement_file([("DemoPlayer", T, (0.0, ry, 0.0)), ("DemoCrate", CT, (0.0, cry, 0.0)), ("DemoFlag", FT, (0.0, 0.0, 0.0))]))
    # step 12: the player's hat - its own model and animations, joined to the player's Head bone by actor data:
    # Pack/Actor/DemoPlayer.pack.zs (SubActorParam: CreatorList -> DemoPlayerHat on joint Head) and
    # Pack/Actor/DemoPlayerHat.pack.zs (ModelParam: ModelArcName DemoHat, AnimArc3 -> DemoHat)
    import demo_ui
    with open(os.path.join(ROMFS, "Model", "DemoHat.DemoHat.bfres.zs"), "wb") as f:
        f.write(bfres_file("DemoHat", hat_model(), bones=HAT_BONES))
    with open(os.path.join(ROMFS, "Model", "DemoHat.anim.bfres.zs"), "wb") as f:
        f.write(anim_file("DemoHat", HAT_CLIPS, HAT_BONE))
    pdir = os.path.join(ROMFS, "Pack", "Actor"); os.makedirs(pdir, exist_ok=True)
    def xyz(x, y, z): return {"X": float(x), "Y": float(y), "Z": float(z)}
    def model_param(model, anim): return byml_file({"ModelArcName": model, "AnimArc3": [{"ArchivePath": "Work/ActorData/Model/%s/workspace/%s.anim" % (anim, anim)}]})
    packs = {"DemoPlayer": {"Component/SubActorParam/DemoPlayer.game__actor__SubActorParam.bgyml": byml_file({"CreatorList": [
                 {"ArchiveName": "DemoPlayerHat", "IsAlive": True, "FixInfo": {"JointName": "Head", "LocalRotate": xyz(-8, 0, 0), "LocalTrans": xyz(0, 0.30, -0.01)}}]}),
                            "Component/ModelParam/DemoPlayer.game__Actor__ModelParam.bgyml": model_param("DemoPlayer", "DemoPlayer")},
             "DemoPlayerHat": {"Component/ModelParam/DemoPlayerHat.game__Actor__ModelParam.bgyml": model_param("DemoHat", "DemoHat")}}
    for actor, files in packs.items():
        with open(os.path.join(pdir, actor + ".pack.zs"), "wb") as f: f.write(demo_ui._z(demo_ui.sarc(files)))
    # step 13: physics - collision for the map and the crate, the flag (model + cloth) - demo_havok.py
    import demo_havok
    os.makedirs(os.path.join(ROMFS, "Phive", "Shape"), exist_ok=True); os.makedirs(os.path.join(ROMFS, "Phive", "Cloth"), exist_ok=True)
    isl, ntri, nsec = demo_havok.island_collision(height, material)
    with open(os.path.join(ROMFS, "Phive", "Shape", "DemoIsland.bphsh.zs"), "wb") as f: f.write(demo_ui._z(isl))
    with open(os.path.join(ROMFS, "Phive", "Shape", "DemoCrate.bphsh.zs"), "wb") as f: f.write(demo_ui._z(demo_havok.crate_collision()))
    with open(os.path.join(ROMFS, "Phive", "Cloth", "DemoFlag.bphcl.zs"), "wb") as f: f.write(demo_ui._z(demo_havok.cloth_file()))
    fb, fparts = flag_model()
    with open(os.path.join(ROMFS, "Model", "DemoFlag.DemoFlag.bfres.zs"), "wb") as f: f.write(bfres_file("DemoFlag", fparts, bones=fb))
    with open(os.path.join(ROMFS, "Tex", "DemoFlag_Cloth.bntx.zs"), "wb") as f: f.write(bntx_file("DemoFlag_Cloth", 128, 96, [flag_texture()]))
    # step 6: a video (Movie\DemoSeagulls.webm: VP9, 1280 x 720, 24 fps, 10 s, no sound). It is a real video, not
    # generated: copied from media\ beside this script, or from an earlier copy in this script's own demo folder.
    here = os.path.dirname(os.path.abspath(__file__))
    vid_to = os.path.join(ROMFS, "Movie", "DemoSeagulls.webm")
    for src in (os.path.join(here, "media", "DemoSeagulls.webm"), os.path.join(here, "base", "romfs", "Movie", "DemoSeagulls.webm")):
        if os.path.isfile(src):
            os.makedirs(os.path.dirname(vid_to), exist_ok=True)
            if os.path.abspath(src) != os.path.abspath(vid_to): shutil.copyfile(src, vid_to)
            break
    else:
        print("(no DemoSeagulls.webm found beside the script - the demo is made without its video)")
    # step 7: music (Sound\Stream\GoldenTides.bwav: DSP-ADPCM, stereo, 44.1 kHz, 10 s, loops). Made from an mp3 by
    # tools\make_bwav.py; copied in like the video.
    snd_to = os.path.join(ROMFS, "Sound", "Stream", "GoldenTides.bwav")
    for src in (os.path.join(here, "media", "GoldenTides.bwav"), os.path.join(here, "base", "romfs", "Sound", "Stream", "GoldenTides.bwav")):
        if os.path.isfile(src):
            os.makedirs(os.path.dirname(snd_to), exist_ok=True)
            if os.path.abspath(src) != os.path.abspath(snd_to): shutil.copyfile(src, snd_to)
            break
    else:
        print("(no GoldenTides.bwav found beside the script - the demo is made without its music)")
    # step 14: a sound bank (BARS) with made-up sound effects and a start-only entry for the music - demo_sound.py
    import demo_sound
    print("sound bank:", demo_sound.build(ROMFS, here))
    # step 8: a title screen (layout + animations), its text in 4 languages and a made-up font (demo_ui.py)
    sys.path.insert(0, here); import demo_ui
    nch = demo_ui.build(ROMFS)
    print("title screen layout, 4 languages, font DemoBeads (%d characters)" % nch)
    # step 9: lighting (Env/DemoIsland.genvb.zs: midday, sunset, night, storm, a lens flare) - demo_env.py
    import demo_env
    demo_env.build(ROMFS)
    # step 10: event scripts (a conversation and a cutscene with a timeline) - demo_evt.py; their text goes into the
    # message archives made in step 8
    import demo_evt
    demo_evt.build(ROMFS)
    # step 11: exefs - tiny stand-ins for the program files (main, rtld, sdk, main.npdm) - demo_exefs.py
    import demo_exefs
    demo_exefs.build(os.path.join(OUT, "base"))
    # step 15: texture formats - one picture in every GPU format the app decodes, tiled, with mipmaps - demo_tex.py
    import demo_tex
    print("texture formats:", demo_tex.build(ROMFS, here))
    # step 16: voxel terrain - a rock arch (Donkey Kong Bananza style voxel part) - demo_voxel.py
    import demo_voxel
    print("voxel arch: %d of %d blocks" % demo_voxel.build(ROMFS))
    # step 17: packaging - dictionary-compressed data files + their .zsdic, and (last: it lists every file) the
    # resource size table - demo_data.py
    import demo_data
    print("zstd dictionary id 0x%X" % demo_data.build_dict_files(ROMFS, byml_file, demo_ui.sarc))
    print("resource size table: %d files" % demo_data.restbl(ROMFS, lambda b: zstandard.ZstdDecompressor().decompress(b, max_output_size=1 << 28)))
    print("player at x %.1f, z %.1f, height %.1f (small island %d)" % (T[0], T[2], T[1], PLAYER_ISLAND + 1))
    print("crate at x %.1f, z %.1f, height %.1f (small island %d)" % (CT[0], CT[2], CT[1], CRATE_ISLAND + 1))
    for k, (ix, iz, ir, pk) in enumerate(SMALL):
        print("small island %d: centre x %.0f, z %.0f, top %.0f m" % (k + 1, ix, iz, height(ix, iz)))
    print("done:", os.path.abspath(ROMFS))


if __name__ == "__main__":
    main()
