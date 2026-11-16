"""demo_env.py - the ns2dig demo's lighting (used by make_demo.py): Env/DemoIsland.genvb.zs, a SARC of AAMP
parameter files - a full "default" (midday) setup, and Sunset, Night and Storm setups that only hold the files they
change - plus a lens flare (default/default.bofx: a BFRES holding the flare settings and textures). Made up.

AAMP version 2 (little-endian): 0x30 header ("AAMP", 2, flags 3, file size, 0, type-name size, lists, objects,
params, data size, string size, 0), the type name ("aglenv"...), then lists (12 bytes: name hash, offset/count of
child lists and of objects, offsets in 4-byte units from the entry), objects (8: hash, offset/count of params),
params (8: hash, data offset in 4-byte units | type << 24), the data, the strings. Names are CRC32 hashes.
"""
import math, struct, zlib
import numpy as np
from demo_ui import sarc, bntx_raw, _z

BOOL, F32, INT, VEC2, VEC3, VEC4, COLOR, STR32, STR64 = 0, 1, 2, 3, 4, 5, 6, 7, 8


def _h(s): return zlib.crc32(s.encode()) & 0xFFFFFFFF


def aamp(ptype, root):
    """root: {"lists": {name: list}, "objects": {name: {param: (type, value)}}}; returns bytes"""
    lists, objs, params, data, strs = [], [], [], bytearray(), bytearray()
    # breadth-first so each node's children sit together
    order = [("param_root", root)]
    i = 0
    while i < len(order):
        _, node = order[i]; i += 1
        for nm, sub in node.get("lists", {}).items(): order.append((nm, sub))
    list_idx = {id(node): k for k, (_, node) in enumerate(order)}
    obj_order = []
    for _, node in order:
        for nm, ob in node.get("objects", {}).items(): obj_order.append((nm, ob))
    nL, nO = len(order), len(obj_order)
    nP = sum(len(ob) for _, ob in obj_order)
    base_l, base_o = 0, 12 * nL
    base_p = base_o + 8 * nO
    base_d = base_p + 8 * nP
    # data and strings (strings go after the data)
    pentries = []
    for _, ob in obj_order:
        for pn, (t, v) in ob.items():
            if t in (STR32, STR64):
                pentries.append((pn, t, ("s", len(strs)))); strs += v.encode() + b"\0"
                while len(strs) % 4: strs += b"\0"
            else:
                if t == BOOL: d = struct.pack("<I", 1 if v else 0)
                elif t == F32: d = struct.pack("<f", v)
                elif t == INT: d = struct.pack("<i", v)
                else: d = struct.pack("<%df" % len(v), *v)
                pentries.append((pn, t, ("d", len(data)))); data += d
    body = bytearray(base_d + len(data) + len(strs))
    child_l, child_o = 1, 0                      # next free list / object index
    pi = 0
    for k, (nm, node) in enumerate(order):
        at = base_l + 12 * k
        nl, no = len(node.get("lists", {})), len(node.get("objects", {}))
        lo = (base_l + 12 * child_l - at) // 4 if nl else 0
        oo = (base_o + 8 * child_o - at) // 4 if no else 0
        struct.pack_into("<IHHHH", body, at, _h(nm), lo, nl, oo, no)
        child_l += nl; child_o += no
    for k, (nm, ob) in enumerate(obj_order):
        at = base_o + 8 * k
        po = (base_p + 8 * pi - at) // 4 if ob else 0
        struct.pack_into("<IHH", body, at, _h(nm), po, len(ob))
        for _ in ob:
            pn, t, (where, off) = pentries[pi]; pat = base_p + 8 * pi
            target = base_d + off if where == "d" else base_d + len(data) + off
            struct.pack_into("<II", body, pat, _h(pn), ((target - pat) // 4) | (t << 24))
            pi += 1
    body[base_d:base_d + len(data)] = data; body[base_d + len(data):] = strs
    pt = ptype.encode() + b"\0"
    while len(pt) % 4: pt += b"\0"
    head = b"AAMP" + struct.pack("<11I", 2, 3, 0x30 + len(pt) + len(body), 0, len(pt), nL, nO, nP, len(data), len(strs), 0)
    return head + pt + bytes(body)


def _obj(**kw): return kw
def C(r, g, b, a=1.0): return (COLOR, (r, g, b, a))
def V3(x, y, z): return (VEC3, (x, y, z))
def F(x): return (F32, float(x))
def B(x): return (BOOL, x)
def I(x): return (INT, x)


def _dir(elev, az):
    """the main light's Direction (it shines from the sun: the reverse of the sun's direction)"""
    e, a = math.radians(elev), math.radians(az)
    return V3(*(round(-c, 4) for c in (math.cos(e) * math.sin(a), math.sin(e), math.cos(e) * math.cos(a))))


def env_file(amb, amb_i, diffuse, spec, elev, az, sun_i, sky, ground, hemi_i):
    return aamp("aglenv", {"lists": {
        "AmbientLight": {"objects": {"AmbientLight0": {"Color": C(*amb), "Intensity": F(amb_i)}}},
        "DirectionalLight": {"objects": {"DirectionalLight0": {"DiffuseColor": C(*diffuse), "SpecularColor": C(*spec),
                             "BacksideColor": C(0.1, 0.1, 0.12), "Direction": _dir(elev, az), "Intensity": F(sun_i), "ViewCoordinate": B(False)}}},
        "HemisphereLight": {"objects": {"HemisphereLight0": {"SkyColor": C(*sky), "GroundColor": C(*ground), "Intensity": F(hemi_i),
                            "Direction": V3(0, -1, 0)}}}}})


def sky_file(dyn, ground, fog, fog_near, fog_far, sun_size, sun_i, sun_col):
    return aamp("aglsky", {"objects": {"sky": {"dynamic_color": V3(*dyn), "ground_color": V3(*ground), "adhoc_fog_color": V3(*fog),
        "adhoc_fog_near": F(fog_near), "adhoc_fog_far": F(fog_far), "render_sun_size": F(sun_size), "render_sun_intensity": F(sun_i),
        "sun_color": V3(*sun_col), "rayleigh_amplifier_rendering": F(1.0), "mie_amplifier_rendering": F(1.0)}}})


def proj_file(sun=None, moon=(0.35, 0.4, 0.55), sun_x=60.0, sun_y=30.0, fog_near=300, fog_far=2500, fog_col=(0.75, 0.85, 0.95),
              yfog=(100, 400), wind=(45, 2.5), stars=(0.0, 0), extra=None):
    pg = {}
    if sun: pg["SunLightColor"] = V3(*sun)
    pg.update({"MoonLightColor": V3(*moon), "SunXRotDegreeInit": F(sun_x), "SunYRotDegree": F(sun_y), "SunZRotDegree": F(0),
               "TimeScale": F(0), "InitTime": F(12.0), "StarFieldIntensity": F(stars[0]), "StarNum": I(stars[1]),
               "WindDegree": F(wind[0]), "WindStrength": F(wind[1]), "WaterAlbedo": V3(0.1, 0.4, 0.55), "WaterRoughness": F(0.2)})
    if extra: pg.update(extra)
    fog = {"FogNear": F(fog_near), "FogFar": F(fog_far), "FogDensity": F(0.4), "YFogColor": C(*fog_col), "YFogStart": F(yfog[0]),
           "YFogEnd": F(yfog[1]), "FogNoiseEnable": B(False)}
    return aamp("projgfxbase", {"objects": {"project_graphic_base": pg, "Fog": fog}})


def bloom_file(on, intensity, threshold):
    return aamp("aglblm", {"objects": {"bloom": {"enable": B(on), "intensity": F(intensity), "threshhold": F(threshold),
                                                  "threshhold_balance": F(0.5), "level": I(3)}}})


def ccr_file(sat, vig_col, vig, lum=1.0):
    return aamp("aglccr", {"objects": {"color_correction": {"enable": B(True), "saturate_min": F(sat), "luminance_intensity": F(lum),
        "enable_vignetting_color": B(vig > 0), "vignetting_color": C(*vig_col), "vignetting_blend": F(vig)}}})


def dof_file(on, dist, rng, fnum):
    return aamp("agldof", {"objects": {"dof": {"enable": B(on), "doftype": I(1), "mFocalDistance": F(dist), "mFocalRange": F(rng),
        "mFNumber": F(fnum), "mAperture": F(1.0 / fnum), "mMaxCoCSize": F(12), "enable_dof_far_max": B(True), "dof_far_max": F(0.8)}}})


def shadow_file(start, end, scale):
    return aamp("aglshpp", {"objects": {"ShadowPrePass": {"ShadowFadeStart": F(start), "ShadowFadeEnd": F(end), "ShadowScale": F(scale),
        "DepthShadowOffset": F(0.02), "dynamicShadowFarFadeStart": F(start), "dynamicShadowFarFadeEnd": F(end)}}})


def shafts_file(on, intensity, threshold):
    return aamp("aglvolm", {"objects": {"param": {"enable": B(on), "shaft_intensity": F(intensity), "shaft_threshhold": F(threshold),
                                                   "shaft_expand": F(1.2), "shaft_finalgather": F(0.6)}}})


# ---------------------------------------------------------------- lens flare (default/default.bofx)
def _flare_textures(n=64):
    y, x = (np.mgrid[0:n, 0:n] + 0.5) / n - 0.5
    r = np.sqrt(x * x + y * y)
    glow = np.clip(1 - r / 0.5, 0, 1) ** 2
    ring = np.clip(1 - np.abs(r - 0.36) / 0.05, 0, 1) * 0.8
    a = np.arctan2(y, x); hexr = 0.42 * math.cos(math.pi / 6) / np.cos(((a + math.pi / 6) % (math.pi / 3)) - math.pi / 6)
    hexa = np.clip((hexr - r) * 40, 0, 1) * 0.35
    out = []
    for al in (glow, ring, hexa):
        img = np.stack([np.full_like(al, 255)] * 3 + [al * 255], axis=2).astype(np.uint8)
        out.append(img.tobytes())
    return [("DemoFlare_Glow", n, n, [out[0]]), ("DemoFlare_Ring", n, n, [out[1]]), ("DemoFlare_Hex", n, n, [out[2]])]


def flare_settings():
    elems = [  # texture, position along the sun -> centre line, size, colour, intensity
        (0, 0.0, 1.6, (1.0, 0.95, 0.8, 1)), (2, 0.45, 0.35, (0.6, 0.9, 1.0, 1)), (1, 0.8, 0.7, (1.0, 0.7, 0.4, 1)),
        (2, 1.3, 0.25, (0.7, 1.0, 0.6, 1)), (1, 1.7, 1.1, (0.5, 0.6, 1.0, 1))]
    impl = {}
    for k, (tex, pos, size, col) in enumerate(elems):
        impl.update({"IsEnableDraw%d" % k: B(True), "TextureIdx%d" % k: I(tex), "Position%d" % k: F(pos), "Size%d" % k: F(size),
                     "Color%d" % k: C(*col), "Intensity%d" % k: F(0.8 if k else 1.0), "IsEnableRotate%d" % k: B(k == 0)})
    return aamp("aglofx", {"objects": {"TextureList": {"RefTexName0": (STR64, "DemoFlare_Glow"), "RefTexName1": (STR64, "DemoFlare_Ring"),
                                                        "RefTexName2": (STR64, "DemoFlare_Hex")},
                                        "OfxLensFlareImpl": impl}})


def bofx():
    """a BFRES whose embedded files (array at 0xB8, count at 0xEC, names in the dictionary at 0xC0) are the flare
    settings (AAMP) and textures (BNTX)"""
    files = [("DemoFlare.baglofx", flare_settings()), ("DemoFlare.bntx", bntx_raw(_flare_textures()))]
    b = bytearray(0x100)
    b[0:8] = b"FRES    "; b[8], b[9] = 0, 2; struct.pack_into("<H", b, 0x0A, 10); struct.pack_into("<H", b, 0x0C, 0xFEFF)
    def string(s):
        o = len(b); e = s.encode(); b.extend(struct.pack("<H", len(e)) + e + b"\0")
        while len(b) % 4: b.append(0)
        return o
    struct.pack_into("<Q", b, 0x20, string("DemoFlare"))
    names = [string(n) for n, _ in files]
    while len(b) % 8: b.append(0)
    dic = len(b); b.extend(b"_DIC" + struct.pack("<i", len(files)) + bytes(16 * (len(files) + 1)))
    for i, no in enumerate(names): struct.pack_into("<Q", b, dic + 8 + 16 * (i + 1) + 8, no)
    arr = len(b); b.extend(bytes(16 * len(files)))
    for i, (_, d) in enumerate(files):
        while len(b) % 0x1000: b.append(0)
        struct.pack_into("<QI", b, arr + 16 * i, len(b), len(d)); b.extend(d)
    struct.pack_into("<Q", b, 0xB8, arr); struct.pack_into("<Q", b, 0xC0, dic); struct.pack_into("<H", b, 0xEC, len(files))
    struct.pack_into("<I", b, 0x1C, len(b))
    return bytes(b)


# ---------------------------------------------------------------- the setups
def archive():
    F_ = {}
    # midday: everything
    F_["default.bprojgfxbase"] = proj_file(sun=(1.6, 1.55, 1.4), extra={"DemoMadeUpSetting": F(42.0)})   # (a name the viewer can't know)
    F_["default.baglenv"] = env_file((0.55, 0.6, 0.7), 1.0, (1.0, 0.96, 0.88), (1, 1, 1), 60, 30, 3.0, (0.45, 0.65, 1.0), (0.45, 0.6, 0.35), 1.0)
    F_["default.baglsky"] = sky_file((0.75, 0.88, 1.0), (0.3, 0.55, 0.65), (0.8, 0.88, 0.95), 300, 2500, 1.0, 1.0, (1.0, 0.97, 0.9))
    F_["default.baglblm"] = bloom_file(True, 0.4, 1.0)
    F_["default.baglccr"] = ccr_file(1.0, (0, 0, 0), 0.0)
    F_["default.bagldof"] = dof_file(False, 50, 200, 8)
    F_["default.baglshpp"] = shadow_file(150, 300, 1.0)
    F_["default.baglvolm"] = shafts_file(False, 0.0, 1.0)
    # sunset: a low orange sun behind the big island, warm haze, more glow, light shafts
    F_["Sunset.bprojgfxbase"] = proj_file(sun=(1.8, 0.85, 0.4), sun_x=6, sun_y=-60, fog_near=150, fog_far=1500, fog_col=(1.0, 0.62, 0.42), yfog=(100, 300))
    F_["Sunset.baglenv"] = env_file((0.5, 0.4, 0.45), 0.8, (1.0, 0.6, 0.3), (1.0, 0.7, 0.4), 6, -60, 2.2, (0.35, 0.35, 0.65), (0.4, 0.25, 0.2), 0.8)
    F_["Sunset.baglsky"] = sky_file((1.0, 0.55, 0.3), (0.35, 0.22, 0.25), (1.0, 0.6, 0.4), 150, 1500, 1.6, 1.4, (1.0, 0.6, 0.3))
    F_["Sunset.baglblm"] = bloom_file(True, 0.9, 0.7)
    F_["Sunset.baglccr"] = ccr_file(1.15, (0.35, 0.1, 0.05), 0.25)
    F_["Sunset.baglvolm"] = shafts_file(True, 0.8, 0.6)
    # night: no sun (its light colour isn't set), a pale moon is the main light, stars, dark fog
    F_["Night.bprojgfxbase"] = proj_file(None, moon=(0.55, 0.65, 0.9), sun_x=-30, fog_near=200, fog_far=1800, fog_col=(0.08, 0.1, 0.2), stars=(1.0, 2000))
    F_["Night.baglenv"] = env_file((0.12, 0.14, 0.25), 0.6, (0.55, 0.65, 0.9), (0.6, 0.7, 1.0), 40, 120, 0.6, (0.06, 0.08, 0.2), (0.04, 0.05, 0.06), 0.5)
    F_["Night.baglsky"] = sky_file((0.1, 0.12, 0.3), (0.03, 0.05, 0.1), (0.08, 0.1, 0.2), 200, 1800, 0.6, 0.3, (0.6, 0.7, 0.9))
    F_["Night.baglccr"] = ccr_file(0.7, (0, 0, 0.05), 0.4, 0.8)
    # storm: grey, dense close fog, a dim hidden sun, blurred distance
    F_["Storm.bprojgfxbase"] = proj_file(sun=(0.5, 0.52, 0.56), sun_x=50, fog_near=40, fog_far=600, fog_col=(0.45, 0.48, 0.52), yfog=(100, 500), wind=(200, 14.0))
    F_["Storm.baglenv"] = env_file((0.4, 0.42, 0.46), 1.0, (0.5, 0.52, 0.56), (0.3, 0.3, 0.3), 50, 10, 0.6, (0.42, 0.45, 0.5), (0.25, 0.27, 0.27), 0.9)
    F_["Storm.baglsky"] = sky_file((0.45, 0.48, 0.52), (0.22, 0.26, 0.28), (0.45, 0.48, 0.52), 40, 600, 0.05, 0.1, (0.5, 0.5, 0.5))
    F_["Storm.bagldof"] = dof_file(True, 30, 120, 4)
    F_["Storm.baglshpp"] = shadow_file(40, 80, 0.3)
    # the list of what the archive holds
    names = sorted(F_)
    F_["DemoIsland.bgenv"] = aamp("genv", {"objects": {"env_obj_ref_array": {str(i): (STR64, n) for i, n in enumerate(names + ["default/default.bofx"])}}})
    F_["default/default.bofx"] = bofx()
    return sarc(F_)


def build(romfs):
    import os
    p = os.path.join(romfs, "Env"); os.makedirs(p, exist_ok=True)
    open(os.path.join(p, "DemoIsland.genvb.zs"), "wb").write(_z(archive()))
