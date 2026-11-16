"""demo_havok.py - the ns2dig demo's Havok physics files (used by make_demo.py), written as Phive files holding a
Havok 2022 tagfile:

  Phive/Shape/DemoIsland.bphsh.zs   collision for the whole 3 km map: an hknpMeshShape cut into geometry sections,
                                    16-bit vertices, quads and triangles, 3 collision materials (grass, sand, sea bed)
  Phive/Shape/DemoCrate.bphsh.zs    collision for the crate: one section with full-precision (float) vertices
  Phive/Cloth/DemoFlag.bphcl.zs     cloth for the flag (Model/DemoFlag): 20 particles (the pole side pinned),
                                    standard / stretch / bend links, a capsule round the pole

Phive v1 header (0x30): "Phive", 0, 1; at 0x10: material table offset, (unused) offset, file size, tagfile size,
material table size, 0. The tagfile starts at 0x30; the material table (16 bytes each: two numbers) follows it.

Havok tagfile: sections with a big-endian u32 header (2 flag bits + size) and a 4-letter tag. TAG0 holds SDKV
(version text), DATA (the objects), TYPE (TST1 type names, TNA1 the type list, FST1 member names, TBDY type bodies)
and INDX (ITEM: 12 bytes per item - type, offset in DATA, count). Numbers in TNA1 / TBDY are packed integers.
Types: format & 0xF = kind (3 string, 4 integer, 5 float, 6 pointer, 7 record, 8 array; 0x20 = fixed size [N]).
In DATA a pointer, string or hkArray is the u32 index of an item; hkRelArrayView is (s32 offset from itself, u32 count).
"""
import math, struct

# ---------------------------------------------------------------- the tagfile writer
def _vi(v):
    """Havok packed integer"""
    if v < 0x80: return bytes([v])
    if v < 0x4000: return bytes([0x80 | (v >> 8), v & 0xFF])
    if v < 0x200000: return bytes([0xC0 | (v >> 16), (v >> 8) & 0xFF, v & 0xFF])
    if v < 0x8000000: return bytes([0xE0 | (v >> 24), (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF])
    return bytes([0xF8]) + v.to_bytes(8, "big")


def _sec(tag, body, leaf=True):
    return struct.pack(">I", (0x40000000 if leaf else 0) | (8 + len(body))) + tag + body


class Tagfile:
    def __init__(self):
        self.types = [None]; self.byname = {}; self.data = bytearray(); self.items = [(0, 0, 0)]
    # -- types
    def t(self, name, parent=0, fmt=None, sub=None, size=None, align=None, members=(), params=()):
        """members: [(name, offset, type)]; returns the type index"""
        key = (name, tuple(params), sub, fmt)
        if key in self.byname: return self.byname[key]
        self.types.append(dict(name=name, parent=parent, fmt=fmt, sub=sub, size=size, align=align or (min(size, 8) if size else None),
                               members=list(members), params=list(params)))
        self.byname[key] = len(self.types) - 1
        return len(self.types) - 1
    def basics(self):
        T = self.t
        self.u8 = T("hkUint8", fmt=4, size=1); self.u16 = T("hkUint16", fmt=4, size=2); self.u32 = T("hkUint32", fmt=4, size=4)
        self.i32 = T("hkInt32", fmt=4 | 0x200, size=4); self.u64 = T("hkUint64", fmt=4, size=8); self.f32 = T("float", fmt=5, size=4)
        self.bool_ = T("hkBool", fmt=2, size=1); self.char = T("char", fmt=4, size=1)
        self.cstr = T("char*", fmt=3, sub=self.char, size=4, align=4)
        self.strptr = T("hkStringPtr", fmt=7, size=8, members=[("stringAndFlag", 0, self.cstr)])
        self.vec4 = self.fixed("hkVector4f", self.f32, 4)
        self.obj = T("hkReferencedObject", fmt=7, size=0x10)
    def fixed(self, name, sub, n):
        es = self.types[sub]["size"]
        return self.t(name, fmt=8 | 0x20 | (n << 8), sub=sub, size=es * n, align=self.types[sub]["align"])
    def arr(self, sub):
        return self.t("hkArray", fmt=8, sub=sub, size=16, align=8, params=[("tT", sub)])
    def relarr(self, sub):
        return self.t("hkRelArrayView", fmt=8, sub=sub, size=8, align=4, params=[("tT", sub)])
    def ptr(self, sub):
        return self.t("T*", fmt=6, sub=sub, size=8, align=8, params=[("tT", sub)])
    def refptr(self, sub):
        return self.t("hkRefPtr", fmt=7, size=8, members=[("ptr", 0, self.ptr(sub))], params=[("tT", sub)])
    # -- data
    def alloc(self, n, align=16):
        while len(self.data) % align: self.data.append(0)
        o = len(self.data); self.data += bytes(n); return o
    def item(self, ti, off, count=1):
        self.items.append((ti, off, count)); return len(self.items) - 1
    def string(self, s):
        e = s.encode() + b"\0"; o = self.alloc(len(e), 1); self.data[o:o + len(e)] = e
        return self.item(self.char, o, len(e))
    def put_u32(self, at, v): struct.pack_into("<I", self.data, at, v)
    # -- output
    def bytes(self):
        tst, fst = [], []
        def ts(s):
            if s not in tst: tst.append(s)
            return tst.index(s)
        def fs(s):
            if s not in fst: fst.append(s)
            return fst.index(s)
        tna = bytearray(_vi(len(self.types)))
        for t in self.types[1:]:
            tna += _vi(ts(t["name"])) + _vi(len(t["params"]))
            for pn, pv in t["params"]: tna += _vi(ts(pn)) + _vi(pv)
        body = bytearray()
        for i, t in enumerate(self.types[1:], 1):
            fl = (1 if t["fmt"] is not None else 0) | (2 if t["sub"] else 0) | (8 if t["size"] is not None else 0) | (0x20 if t["members"] else 0)
            body += _vi(i) + _vi(t["parent"]) + _vi(fl)
            if fl & 1: body += _vi(t["fmt"])
            if fl & 2: body += _vi(t["sub"])
            if fl & 8: body += _vi(t["size"]) + _vi(t["align"] or 1)
            if fl & 0x20:
                body += _vi(len(t["members"]))
                for mn, mo, mt in t["members"]: body += _vi(fs(mn)) + _vi(0) + _vi(mo) + _vi(mt)
        def strs(lst):
            b = b"".join(s.encode() + b"\0" for s in lst)
            return b + bytes((-len(b)) % 4)
        def pad4(b): return bytes(b) + bytes((-len(b)) % 4)
        typ = _sec(b"TST1", strs(tst)) + _sec(b"TNA1", pad4(tna)) + _sec(b"FST1", strs(fst)) + _sec(b"TBDY", pad4(body))
        item = b"".join(struct.pack("<III", ti, off, c) for ti, off, c in self.items)
        data = bytes(self.data) + bytes((-len(self.data)) % 16)
        inner = _sec(b"SDKV", b"20220100") + _sec(b"DATA", data) + _sec(b"TYPE", typ, False) + _sec(b"INDX", _sec(b"ITEM", item), False)
        return _sec(b"TAG0", inner, False)


def phive(tag, materials=()):
    """Phive v1 file: header, tagfile, material table (16 bytes each)"""
    tf = tag + bytes((-len(tag)) % 16)
    o1 = 0x30 + len(tf); mt = b"".join(struct.pack("<II8x", a, b) for a, b in materials)
    h = bytearray(0x30); h[0:8] = b"Phive\0\x01\x00"
    struct.pack_into("<6I", h, 0x10, o1, o1 + len(mt), o1 + len(mt), len(tag), len(mt), 0)
    return bytes(h) + tf + mt


# ---------------------------------------------------------------- collision meshes (hknpMeshShape)
def mesh_shape(sections, tags, inv=1 / 32.0):
    """sections: [(vertices [(x,y,z)], primitives [(a,b,c,d)], use_floats)]; tags: [(shape key, material)]"""
    H = Tagfile(); H.basics(); T = H.t
    vcu = T("hknpMeshShapeVertexConversionUtil", fmt=7, size=8, members=[("bitScale16", 0, H.f32), ("bitScale16Inv", 4, H.f32)])
    tagent = T("hknpShapeTagTableEntry", fmt=7, size=8, members=[("shapeKey", 0, H.u32), ("tag", 4, H.u16)])
    prim = H.fixed("hknpMeshShapePrimitive", H.u8, 4)
    sec = T("hknpMeshShapeGeometrySection", fmt=7, size=0x20, members=[("sectionOffset", 0, H.fixed("hkInt32[3]", H.i32, 3)),
            ("bitScale8Inv", 0xC, H.f32), ("primitives", 0x10, H.relarr(prim)), ("vertexBuffer", 0x18, H.relarr(H.u8))])
    shape = T("hknpShape", H.obj, fmt=7, size=0x30, members=[("type", 0x10, H.u8), ("dispatchType", 0x11, H.u8), ("numShapeKeyBits", 0x12, H.u8),
              ("flags", 0x14, H.u16), ("convexRadius", 0x18, H.f32), ("userData", 0x20, H.u64)])
    comp = T("hknpCompositeShape", shape, fmt=7, size=0x40)
    meshT = T("hknpMeshShape", comp, fmt=7, size=0xA0, members=[("vertexConversionUtil", 0x40, vcu), ("shapeTagTable", 0x50, H.relarr(tagent)),
              ("geometrySections", 0x58, H.relarr(sec))])
    root = H.alloc(0xA0); H.item(meshT, root)
    d = H.data
    d[root + 0x10] = 4; d[root + 0x11] = 9; d[root + 0x12] = 9 + max(1, math.ceil(math.log2(max(2, len(sections)))))
    struct.pack_into("<ff", d, root + 0x40, 1 / inv, inv)
    ta = H.alloc(8 * len(tags), 4)
    for i, (k, m) in enumerate(tags): struct.pack_into("<IH", H.data, ta + 8 * i, k, m)
    struct.pack_into("<iI", H.data, root + 0x50, ta - (root + 0x50), len(tags))
    sa = H.alloc(0x20 * len(sections), 16)
    struct.pack_into("<iI", H.data, root + 0x58, sa - (root + 0x58), len(sections))
    for s, (V, P, floats) in enumerate(sections):
        g = sa + 0x20 * s
        if floats:
            struct.pack_into("<3i", H.data, g, 0x7FFFFFFF, 0, 0)
            vb = b"".join(struct.pack("<3f", *v) for v in V)
        else:
            so = [int(math.floor(min(v[k] for v in V) / inv)) for k in range(3)]
            struct.pack_into("<3i", H.data, g, *so)
            vb = b"".join(struct.pack("<3H", *(int(round(v[k] / inv)) - so[k] for k in range(3))) for v in V)
        pa = H.alloc(4 * len(P), 4)
        for i, p in enumerate(P): H.data[pa + 4 * i:pa + 4 * i + 4] = bytes(p)
        va = H.alloc(len(vb), 4); H.data[va:va + len(vb)] = vb
        struct.pack_into("<iI", H.data, g + 0x10, pa - (g + 0x10), len(P))
        struct.pack_into("<iI", H.data, g + 0x18, va - (g + 0x18), len(vb))
    return H.bytes()


def island_collision(height, material, half=1500.0, cells=64, patch=14, water=100.0):
    """the whole map as a grid mesh: (phive bytes, triangles). material(x, z, h) -> 0 grass, 1 sand; under water: sea bed"""
    step = 2 * half / cells
    def P(i, j): x, z = -half + i * step, -half + j * step; return (x, height(x, z), z)
    sections, tags = [], []; last = None
    for sj in range(0, cells, patch):
        for si in range(0, cells, patch):
            ni, nj = min(patch, cells - si), min(patch, cells - sj)
            V = [P(si + i, sj + j) for j in range(nj + 1) for i in range(ni + 1)]
            prims, mats = [], []
            for j in range(nj):
                for i in range(ni):
                    a = j * (ni + 1) + i; b = a + 1; c = a + ni + 2; d = a + ni + 1
                    cx, cz = -half + (si + i + 0.5) * step, -half + (sj + j + 0.5) * step; h = height(cx, cz)
                    m = 2 if h < water - 2 else material(cx, cz, h)
                    if (i + j) % 7 == 3:                                    # some as two triangles (c == d) instead of a quad
                        prims.append((a, d, c, c)); mats.append(m); prims.append((a, c, b, b)); mats.append(m)
                    else:
                        prims.append((a, d, c, b)); mats.append(m)
            s = len(sections); sections.append((V, prims, False))
            for k, m in enumerate(mats):
                if m != last: tags.append(((s << 9) | (k << 1), m)); last = m
    ntri = sum(sum(2 if p[2] != p[3] else 1 for p in S[1]) for S in sections)
    return phive(mesh_shape(sections, tags), [(1, 0), (2, 0), (3, 0)]), ntri, len(sections)


def crate_collision(size=1.0):
    h = size / 2
    V = [(x, y, z) for y in (0.0, size) for z in (-h, h) for x in (-h, h)]
    Q = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    return phive(mesh_shape([(V, Q, True)], [(0, 0)]), [(4, 0)])


# ---------------------------------------------------------------- cloth (hclClothContainer)
FLAG_COLS, FLAG_ROWS, FLAG_STEP = 5, 4, 0.25
POLE_H = 2.6


def flag_particles():
    """(x, y, z) per particle, row-major from the top; column 0 is at the pole (pinned)"""
    return [(0.06 + c * FLAG_STEP, POLE_H - 0.05 - r * FLAG_STEP, 0.0) for r in range(FLAG_ROWS) for c in range(FLAG_COLS)]


def cloth_file():
    H = Tagfile(); H.basics(); T = H.t
    named = T("hkRootLevelContainerNamedVariant", fmt=7, size=0x18, members=[("name", 0, H.strptr), ("className", 8, H.strptr),
              ("variant", 0x10, T("hkRefVariant", fmt=7, size=8, members=[("ptr", 0, H.ptr(H.obj))]))])
    rootT = T("hkRootLevelContainer", fmt=7, size=0x10, members=[("namedVariants", 0, H.arr(named))])
    shapeT = T("hclShape", H.obj, fmt=7, size=0x18, members=[("type", 0x10, H.i32)])
    capsule = T("hclCapsuleShape", shapeT, fmt=7, size=0x50, members=[("start", 0x20, H.vec4), ("end", 0x30, H.vec4), ("radius", 0x40, H.f32)])
    m44 = H.fixed("hkMatrix4f", H.f32, 16)
    collid = T("hclCollidable", H.obj, fmt=7, size=0x70, members=[("transform", 0x10, m44), ("name", 0x50, H.strptr), ("enabled", 0x58, H.bool_),
               ("shape", 0x60, H.refptr(shapeT))])
    pdata = T("hclSimClothDataParticleData", fmt=7, size=0x10, members=[("mass", 0, H.f32), ("invMass", 4, H.f32), ("radius", 8, H.f32), ("friction", 12, H.f32)])
    pose = T("hclSimClothPose", H.obj, fmt=7, size=0x30, members=[("name", 0x10, H.strptr), ("positions", 0x18, H.arr(H.vec4))])
    cset = T("hclConstraintSet", H.obj, fmt=7, size=0x20, members=[("name", 0x10, H.strptr)])
    lstd = T("hclStandardLinkConstraintSetLink", fmt=7, size=0xC, members=[("particleA", 0, H.u16), ("particleB", 2, H.u16), ("restLength", 4, H.f32), ("stiffness", 8, H.f32)])
    lstr = T("hclStretchLinkConstraintSetLink", fmt=7, size=0xC, members=[("particleA", 0, H.u16), ("particleB", 2, H.u16), ("restLength", 4, H.f32), ("stiffness", 8, H.f32)])
    lbnd = T("hclBendLinkConstraintSetLink", fmt=7, size=0x14, members=[("particleA", 0, H.u16), ("particleB", 2, H.u16), ("bendMinLength", 4, H.f32),
             ("stretchMaxLength", 8, H.f32), ("bendStiffness", 12, H.f32), ("stretchStiffness", 16, H.f32)])
    std = T("hclStandardLinkConstraintSet", cset, fmt=7, size=0x30, members=[("links", 0x20, H.arr(lstd))])
    stre = T("hclStretchLinkConstraintSet", cset, fmt=7, size=0x30, members=[("links", 0x20, H.arr(lstr))])
    bend = T("hclBendLinkConstraintSet", cset, fmt=7, size=0x30, members=[("links", 0x20, H.arr(lbnd))])
    siminfo = T("hclSimClothDataSimulationInfo", fmt=7, size=0x20, members=[("gravity", 0, H.vec4), ("globalDampingPerSecond", 0x10, H.f32)])
    sim = T("hclSimClothData", H.obj, fmt=7, size=0xA0, members=[("name", 0x10, H.strptr), ("simulationInfo", 0x20, siminfo),
            ("particleDatas", 0x40, H.arr(pdata)), ("fixedParticles", 0x50, H.arr(H.u16)), ("simClothPoses", 0x60, H.arr(H.refptr(pose))),
            ("staticConstraintSets", 0x70, H.arr(H.refptr(cset))), ("totalMass", 0x80, H.f32), ("maxParticleRadius", 0x84, H.f32),
            ("landscapeCollisionEnabled", 0x88, H.bool_)])
    cdata = T("hclClothData", H.obj, fmt=7, size=0x30, members=[("name", 0x10, H.strptr), ("simClothDatas", 0x18, H.arr(H.refptr(sim)))])
    cont = T("hclClothContainer", H.obj, fmt=7, size=0x40, members=[("collidables", 0x10, H.arr(H.refptr(collid))), ("clothDatas", 0x20, H.arr(H.refptr(cdata)))])
    d = lambda: H.data
    def sp(at, s): H.put_u32(at, H.string(s))
    def array(at, ti, elems, es):
        """elements: bytes each (es long) -> an item; its index goes in the hkArray at 'at'"""
        o = H.alloc(es * len(elems), 16)
        for i, e in enumerate(elems): H.data[o + i * es:o + i * es + len(e)] = e
        H.put_u32(at, H.item(ti, o, len(elems))); struct.pack_into("<I", H.data, at + 8, len(elems))
    def ref(ti, off): return struct.pack("<I4x", H.item(ti, off))
    # the root first (item 1)
    root = H.alloc(0x10); H.item(rootT, root)
    nv = H.alloc(0x18, 16); nv_item = H.item(named, nv, 1)
    H.put_u32(root, nv_item); struct.pack_into("<I", H.data, root + 8, 1)
    sp(nv, "DemoFlag"); sp(nv + 8, "hclClothContainer")
    c = H.alloc(0x40); H.put_u32(nv + 0x10, H.item(cont, c))
    # collision: a capsule round the pole
    col = H.alloc(0x70); cap = H.alloc(0x50)
    struct.pack_into("<16f", H.data, col + 0x10, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
    sp(col + 0x50, "PoleCapsule"); H.data[col + 0x58] = 1
    struct.pack_into("<4f4ff", H.data, cap + 0x20, 0, 0, 0, 0, 0, POLE_H, 0, 0, 0.05)
    H.put_u32(col + 0x60, H.item(capsule, cap))
    array(c + 0x10, H.refptr(collid), [ref(collid, col)], 8)
    # the cloth
    cd = H.alloc(0x30); sp(cd + 0x10, "DemoFlag")
    s = H.alloc(0xA0); sp(s + 0x10, "FlagSim")
    struct.pack_into("<4ff", H.data, s + 0x20, 0, -9.8, 0, 0, 0.4)
    P = flag_particles(); n = len(P)
    array(s + 0x40, pdata, [struct.pack("<4f", 0.05, 0 if i % FLAG_COLS == 0 else 20.0, 0.03, 0.3) for i in range(n)], 0x10)
    array(s + 0x50, H.u16, [struct.pack("<H", r * FLAG_COLS) for r in range(FLAG_ROWS)], 2)
    ps = H.alloc(0x30); sp(ps + 0x10, "Rest")
    array(ps + 0x18, H.vec4, [struct.pack("<4f", x, y, z, 0) for x, y, z in P], 16)
    array(s + 0x60, H.refptr(pose), [ref(pose, ps)], 8)
    def idx(r, cc): return r * FLAG_COLS + cc
    def L(a, b): return math.dist(P[a], P[b])
    near = [(idx(r, cc), idx(r, cc + 1)) for r in range(FLAG_ROWS) for cc in range(FLAG_COLS - 1)] + \
           [(idx(r, cc), idx(r + 1, cc)) for r in range(FLAG_ROWS - 1) for cc in range(FLAG_COLS)]
    diag = [(idx(r, cc + 1), idx(r + 1, cc)) for r in range(FLAG_ROWS - 1) for cc in range(FLAG_COLS - 1)]   # (lets it droop)
    far = [(idx(r, cc), idx(r, cc + 2)) for r in range(FLAG_ROWS) for cc in range(FLAG_COLS - 2)] + \
          [(idx(r, cc), idx(r + 2, cc)) for r in range(FLAG_ROWS - 2) for cc in range(FLAG_COLS)]
    sets = []
    for ti, links, name, pack in ((std, near, "Standard", lambda a, b: struct.pack("<HHff", a, b, L(a, b), 1.0)),
                                  (stre, diag, "Stretch", lambda a, b: struct.pack("<HHff", a, b, L(a, b), 0.8)),
                                  (bend, far, "Bend", lambda a, b: struct.pack("<HHffff", a, b, L(a, b) * 0.8, L(a, b), 0.3, 1.0))):
        o = H.alloc(0x30); sp(o + 0x10, name)
        el = lstd if ti == std else lstr if ti == stre else lbnd
        array(o + 0x20, el, [pack(a, b) for a, b in links], H.types[el]["size"]); sets.append(ref(ti, o))
    array(s + 0x70, H.refptr(cset), sets, 8)
    struct.pack_into("<ff", H.data, s + 0x80, 0.05 * n, 0.03); H.data[s + 0x88] = 0
    array(cd + 0x18, H.refptr(sim), [ref(sim, s)], 8)
    array(c + 0x20, H.refptr(cdata), [ref(cdata, cd)], 8)
    return phive(H.bytes())
