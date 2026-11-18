#!/usr/bin/env python3
"""
ns2dig.py - extract Nintendo Switch 2 game RomFS data (Donkey Kong Bananza, Mario Kart World...) to one JSON file per game file.

Scans EVERY file under the input folder and identifies it by content, not extension:
  - zstd-compressed files (.zs) are decompressed (shared .zsdic dictionaries are
    loaded automatically if present anywhere in the folder)
  - SARC archives (.alsarc, .sarc, .pack, ...) become folders, including nested ones
  - BYML files (.bgyml, .byml; versions 2-7) are converted to JSON
  - MSBT text files (.msbt) are converted to {label: text} JSON
  - Switch executables (NSO, e.g. exefs/main) are decompressed and their readable
    text strings listed with memory addresses (install "lz4" for speed: pip install lz4)
  - anything else (models, textures, voxel geometry, sound...) gets a small JSON
    note giving its format and size

Usage:
  pip install zstandard   (not needed on Python 3.14 or newer: zstd is built in)
  python ns2dig.py <romfs folder | file> [output folder]

Output mirrors the RomFS layout: romfs/Pack/A.pack.zs -> <output>/Pack/A.pack.zs/<inner files>.json
Default output folder: alsarc_out
"""
import collections, json, os, re, struct, sys

try:
    import zstandard
except ImportError:
    zstandard = None
if zstandard is None:
    # Python 3.14+ has zstd built in (compression.zstd): wrap it to look like the parts of
    # the zstandard package this script uses, so nothing needs installing.
    try:
        from compression import zstd as _cz
    except ImportError:
        sys.exit("Install the zstd module first:  pip install zstandard\n"
                 "(or use Python 3.14 or newer, which has zstd built in)")

    class _Dict:
        def __init__(self, data):
            self.d = _cz.ZstdDict(bytes(data))
            self._id = self.d.dict_id

        def dict_id(self):
            return self._id

    class _Frame:
        def __init__(self, did):
            self.dict_id = did

    class _Reader:
        def __init__(self, f, d):
            self.z = _cz.ZstdFile(f, "rb", zstd_dict=d)

        def __enter__(self):
            return self

        def __exit__(self, *a):
            self.z.close()

        def read(self, n=-1):
            return self.z.read(n)

    class _Decompressor:
        def __init__(self, dict_data=None):
            self.d = dict_data.d if dict_data is not None else None

        def decompress(self, data, max_output_size=0):
            # one frame (the usual case): the C decompressor directly - much faster than
            # compression.zstd.decompress(), which loops in Python
            dec = _cz.ZstdDecompressor(zstd_dict=self.d) if self.d is not None else _cz.ZstdDecompressor()
            out = dec.decompress(data)
            if dec.eof and not dec.unused_data:
                return out
            return _cz.decompress(bytes(data), zstd_dict=self.d)    # several frames (or truncated)

        def stream_reader(self, f):
            return _Reader(f, self.d)

    class _ZstdCompat:
        ZstdCompressionDict = _Dict
        ZstdDecompressor = _Decompressor

        @staticmethod
        def get_frame_parameters(head):
            return _Frame(_cz.get_frame_info(bytes(head)).dictionary_id)

    zstandard = _ZstdCompat()

ZSTD_MAGIC = b"\x28\xb5\x2f\xfd"
MAX_FULL_READ = 512 * 1024 * 1024   # never fully decompress anything bigger than this

# ---------- SARC ----------
def read_sarc(b):
    if b[:4] != b"SARC":
        raise ValueError("not a SARC archive (magic %r)" % b[:4])
    bom = b[6:8]
    e = "<" if bom == b"\xff\xfe" else ">"
    data_off = struct.unpack_from(e + "I", b, 0x0C)[0]
    sfat = 0x14
    count = struct.unpack_from(e + "H", b, sfat + 6)[0]
    entries = []
    for i in range(count):
        _h, name_attr, start, end = struct.unpack_from(e + "IIII", b, sfat + 12 + i * 16)
        entries.append((name_attr, start, end))
    sfnt = sfat + 12 + count * 16
    names_base = sfnt + 8
    files = {}
    for idx, (name_attr, start, end) in enumerate(entries):
        if name_attr & 0x01000000:
            p = names_base + (name_attr & 0xFFFFFF) * 4   # 24-bit offset (in 4-byte units)
            name = b[p:b.index(0, p)].decode("utf-8")
        else:
            name = "file_%04d" % idx
        files[name] = b[data_off + start:data_off + end]
    return files


# ---------- BYML (v2-v7, little-endian) ----------
def read_byml(b):
    """BYML versions 2-7, little-endian ("YB") or big-endian ("BY")."""
    if b[:2] == b"YB":
        e, bo = "<", "little"
    elif b[:2] == b"BY":
        e, bo = ">", "big"
    else:
        raise ValueError("not BYML (magic %r)" % b[:2])
    ver, key_off, str_off, root_off = struct.unpack_from(e + "HIII", b, 2)

    def u24(p):
        return int.from_bytes(b[p:p + 3], bo)

    def u32(p):
        return struct.unpack_from(e + "I", b, p)[0]

    def strtab(off):
        if not off:
            return []
        n = u24(off + 1)
        offs = struct.unpack_from(e + "%dI" % (n + 1), b, off + 4)
        return [b[off + o:b.index(0, off + o)].decode("utf-8", "replace") for o in offs[:n]]

    keys, strs = strtab(key_off), strtab(str_off)

    def value(t, v):
        if t == 0xA0: return strs[v]
        if t in (0xC0, 0xC1): return node(v)
        if t == 0xD0: return bool(v)
        if t == 0xD1: return struct.unpack(e + "i", struct.pack(e + "I", v))[0]
        if t == 0xD2: return round(struct.unpack(e + "f", struct.pack(e + "I", v))[0], 6)
        if t == 0xD3: return v
        if t == 0xD4: return struct.unpack_from(e + "q", b, v)[0]
        if t == 0xD5: return struct.unpack_from(e + "Q", b, v)[0]
        if t == 0xD6: return struct.unpack_from(e + "d", b, v)[0]
        if t == 0xA1:  # binary blob: u32 size + data
            size = u32(v)
            return {"_binary": b[v + 4:v + 4 + min(size, 64)].hex(" "), "_size": size}
        if t == 0xFF: return None
        return "<unknown type 0x%02X value 0x%X>" % (t, v)

    def node(off):
        t, n = b[off], u24(off + 1)
        if t == 0xC1:  # dictionary: entries of (u24 key index, u8 type, u32 value)
            out = {}
            for i in range(n):
                p = off + 4 + i * 8
                out[keys[u24(p)]] = value(b[p + 3], u32(p + 4))
            return out
        if t == 0xC0:  # array: n type bytes, padded to 4, then n u32 values
            types = b[off + 4:off + 4 + n]
            base = (off + 4 + n + 3) & ~3
            return [value(types[i], u32(base + 4 * i)) for i in range(n)]
        return "<unknown node 0x%02X>" % t

    return node(root_off) if root_off else None


# ---------- MSBT (MsgStdBn) ----------
def read_msbt(b):
    """Return {label: text}. Control tags are shown as [group:type PARAMS] and [/group:type]."""
    """(Same notation as the PyLibMS / LMS tools.)"""
    if b[:8] != b"MsgStdBn":
        raise ValueError("not MSBT")
    e = "<" if b[8:10] == b"\xff\xfe" else ">"
    enc_id = b[0x0C]
    cw = {0: 1, 1: 2, 2: 4}.get(enc_id, 2)                    # bytes per code unit
    codec = {1: "utf-8", 2: "utf-16-le" if e == "<" else "utf-16-be",
             4: "utf-32-le" if e == "<" else "utf-32-be"}[cw]
    unit_fmt = e + {1: "B", 2: "H", 4: "I"}[cw]
    nsec = struct.unpack_from(e + "H", b, 0x0E)[0]
    pos, labels, texts = 0x20, {}, []
    for _ in range(nsec):
        if pos + 16 > len(b):
            break
        magic = b[pos:pos + 4]
        size = struct.unpack_from(e + "I", b, pos + 4)[0]
        d = pos + 16
        if magic == b"LBL1":
            slots = struct.unpack_from(e + "I", b, d)[0]
            for s in range(slots):
                cnt, off = struct.unpack_from(e + "II", b, d + 4 + s * 8)
                p = d + off
                for _ in range(cnt):
                    ln = b[p]
                    name = b[p + 1:p + 1 + ln].decode("utf-8", "replace")
                    idx = struct.unpack_from(e + "I", b, p + 1 + ln)[0]
                    labels[idx] = name
                    p += 1 + ln + 4
        elif magic == b"TXT2":
            cnt = struct.unpack_from(e + "I", b, d)[0]
            offs = list(struct.unpack_from(e + "%dI" % cnt, b, d + 4)) + [size]
            for i in range(cnt):
                texts.append(_msbt_string(b, d + offs[i], d + offs[i + 1], cw, unit_fmt, codec, e))
        pos = d + ((size + 15) & ~15)
    out = {}
    for i, t in enumerate(texts):
        out[labels.get(i, "#%d" % i)] = t
    return out


def _msbt_string(b, p, end, cw, unit_fmt, codec, e):
    parts, run = [], bytearray()

    def unit(q):
        return struct.unpack_from(unit_fmt, b, q)[0]

    while p + cw <= end:
        c = unit(p)
        if c == 0:
            break
        if c == 0x0E:  # tag open: group, type, param size, params
            if run: parts.append(run.decode(codec, "replace")); run = bytearray()
            g, t, n = struct.unpack_from(e + "HHH", b, p + cw)
            params = b[p + cw + 6:p + cw + 6 + n]
            parts.append("[%d:%d%s]" % (g, t, _msbt_params(params) if n else ""))
            p += cw + 6 + n
            continue
        if c == 0x0F:  # tag close
            if run: parts.append(run.decode(codec, "replace")); run = bytearray()
            g, t = struct.unpack_from(e + "HH", b, p + cw)
            parts.append("[/%d:%d]" % (g, t))
            p += cw + 4
            continue
        run += b[p:p + cw]
        p += cw
    if run:
        parts.append(run.decode(codec, "replace"))
    return "".join(parts)



def _msbt_params(params):
    """Tag parameters as text: a leading name (u16 length + ASCII, used by Mario Kart World's value / variable tags)
    is shown as the name, the rest as hex."""
    if len(params) >= 2:
        ln = struct.unpack_from("<H", params, 0)[0]
        if 0 < ln <= len(params) - 2 and all(32 <= c < 127 for c in params[2:2 + ln]):
            rest = params[2 + ln:]
            return " %s%s" % (params[2:2 + ln].decode("ascii"), (" " + rest.hex("-").upper()) if rest.strip(b"\0\xcd") else "")
    return " " + params.hex("-").upper()

def _lms_labels(b, d, e="<"):
    """LMS hash-table label block (LBL1 / CLB1 / ALB1 / SLB1) at d -> {index: label}"""
    out = {}
    slots = struct.unpack_from(e + "I", b, d)[0]
    for s_ in range(slots):
        cnt, off = struct.unpack_from(e + "II", b, d + 4 + s_ * 8)
        p = d + off
        for _ in range(cnt):
            ln = b[p]; name = b[p + 1:p + 1 + ln].decode("utf-8", "replace")
            out[struct.unpack_from(e + "I", b, p + 1 + ln)[0]] = name
            p += 1 + ln + 4
    return out


def read_msbp(b):
    """MsgPrjBn: the message project - text colours, attributes, control tag definitions, styles, source files"""
    if b[:8] != b"MsgPrjBn": raise ValueError("not MSBP")
    e = "<" if b[8:10] == b"\xff\xfe" else ">"
    nsec = struct.unpack_from(e + "H", b, 0x0E)[0]
    sec, pos = {}, 0x20
    for _ in range(nsec):
        if pos + 16 > len(b): break
        sz = struct.unpack_from(e + "I", b, pos + 4)[0]
        sec[b[pos:pos + 4].decode("ascii", "replace")] = (pos + 16, sz)
        pos = pos + 16 + ((sz + 15) & ~15)
    out = {"_binary_format": "MSBP message project (LMS)",
           "_note": "Shared settings for all the game's text (.msbt) in this language archive. Text colour tags in "
                    "messages - [0:3 NN-00] in the .msbt JSON - pick colour NN from this list ([0:3 FF-FF] goes back to "
                    "normal). The event viewer shows these colours.",
           "sections": list(sec)}
    if "CLR1" in sec:
        d, sz = sec["CLR1"]; n = struct.unpack_from(e + "I", b, d)[0]
        labs = _lms_labels(b, sec["CLB1"][0], e) if "CLB1" in sec else {}
        cols = []
        for i in range(n):
            r, g, bb, a = b[d + 4 + i * 4:d + 8 + i * 4]
            cols.append({"index": i, "tag": "[0:3 %02X-%02X]" % (i & 255, i >> 8), "name": labs.get(i),
                         "rgba": [r, g, bb, a], "hex": "#%02X%02X%02X" % (r, g, bb) + ("" if a == 255 else " (alpha %d)" % a)})
        out["colors"] = cols
    if "ALB1" in sec:
        labs = _lms_labels(b, sec["ALB1"][0], e); out["attributes"] = [labs[k] for k in sorted(labs)]
    if "SLB1" in sec:
        labs = _lms_labels(b, sec["SLB1"][0], e); out["styles"] = [labs[k] for k in sorted(labs)]
    def strtab(name):                                  # count u16 + pad, then offsets to (u16 ids..., name) entries
        d, sz = sec[name]; n = struct.unpack_from(e + "H", b, d)[0]
        return d, n, [struct.unpack_from(e + "I", b, d + 4 + i * 4)[0] for i in range(n)]
    if "TGG2" in sec:
        try:
            d, n, offs = strtab("TGG2"); tags = []
            for o in offs:
                q = d + o; m = struct.unpack_from(e + "H", b, q)[0]
                ids = struct.unpack_from(e + "%dH" % m, b, q + 2); nm = _cstr(b, q + 2 + m * 2)
                tags.append({"group": nm, "tag_ids": list(ids)})
            out["tag_groups"] = tags
        except Exception: pass
    if "CTI1" in sec:
        try:
            d, sz = sec["CTI1"]; n = struct.unpack_from(e + "I", b, d)[0]
            out["source_files"] = [_cstr(b, d + struct.unpack_from(e + "I", b, d + 4 + i * 4)[0]) for i in range(n)]
        except Exception: pass
    return out


# ---------- NSO (Switch executable) ----------
def lz4_block_decompress(src, out_size):
    """Decompress one raw LZ4 block. Uses the 'lz4' module if installed (fast), else pure Python."""
    try:
        import lz4.block
        return lz4.block.decompress(src, uncompressed_size=out_size)
    except ImportError:
        pass
    dst = bytearray()
    i, n = 0, len(src)
    while i < n:
        token = src[i]; i += 1
        lit = token >> 4
        if lit == 15:
            while True:
                b = src[i]; i += 1; lit += b
                if b != 255: break
        dst += src[i:i + lit]; i += lit
        if i >= n: break
        off = src[i] | (src[i + 1] << 8); i += 2
        ml = token & 15
        if ml == 15:
            while True:
                b = src[i]; i += 1; ml += b
                if b != 255: break
        ml += 4
        start = len(dst) - off
        if off >= ml:
            dst += dst[start:start + ml]
        else:                       # overlapping copy
            for k in range(ml):
                dst.append(dst[start + k])
    return bytes(dst)


_STR_RE = re.compile(rb"(?:[\x20-\x7e]|[\xc2-\xdf][\x80-\xbf]|[\xe0-\xef][\x80-\xbf]{2}|[\xf0-\xf4][\x80-\xbf]{3}){5,}")
_JP = re.compile("[　-ヿ㐀-鿿＀-￯]")


def _good_string(t):
    """filter out the noise that machine code / packed data produces when read as text"""
    t = t.strip()
    if len(t) < 6:
        return False
    if any(ord(c) > 127 for c in t):                    # non-ASCII: keep only real Japanese text
        if any(ord(c) > 127 and not _JP.match(c) for c in t):
            return False
        return len(_JP.findall(t)) >= 2
    if len(set(t)) <= 3:                                # "UUUUUU", "mkmkmk"
        return False
    letters = sum(c.isalpha() for c in t)
    return letters >= 3 and letters >= len(t) * 0.4


def demangle(s):
    """Light Itanium C++ demangler for readability: returns 'ns::Class::Func(...)' (parameters not
    decoded), or None if it can't."""
    if not s.startswith("_Z"):
        return None
    for pre, what in (("_ZTV", "vtable for "), ("_ZTI", "typeinfo for "), ("_ZTS", "typeinfo name for ")):
        if s.startswith(pre):
            d = demangle("_Z" + s[4:])
            return what + d[:-2] if d and d.endswith("()") else (what + d if d else None)
    if s.startswith("_ZZ"):                             # local static: _ZZ <function> E <name>
        e = s.rfind("E")
        inner = demangle("_Z" + s[3:e]) if e > 3 else None
        tail = re.match(r"(\d+)", s[e + 1:]) if e > 0 else None
        if inner and tail:
            l = int(tail.group(1)); nm = s[e + 1 + len(tail.group(1)):][:l]
            return inner.split("(")[0] + "()::" + nm
        return None
    p = 2; parts = []; n = len(s)
    def ident(p):
        q = p
        while q < n and s[q].isdigit(): q += 1
        if q == p: return None, p
        l = int(s[p:q])
        return s[q:q + l], q + l
    def skip_template(p):                               # p at 'I'; returns index after the matching 'E'
        depth = 0
        while p < n:
            c = s[p]
            if c.isdigit():
                _, p = ident(p); continue
            if c == "L":                                # literal: L <type> <value> E
                e = s.find("E", p)
                if e < 0: return None
                p = e + 1; continue
            if c in "INX": depth += 1
            elif c == "E":
                depth -= 1
                if depth == 0: return p + 1
            p += 1
        return None
    nested = s.startswith("N", p)
    if nested: p += 1
    while p < n and s[p] in "rVKRO": p += 1
    if s.startswith("St", p): parts.append("std"); p += 2
    while p < n:
        c = s[p]
        if c.isdigit():
            nm, p = ident(p)
            if nm is None: return None
            parts.append(nm)
        elif c == "I":
            q = skip_template(p)
            if q is None: return None
            if parts: parts[-1] += "<…>"
            p = q
        elif c == "C" and p + 1 < n and s[p + 1] in "123":
            parts.append(parts[-1].split("<")[0] if parts else "ctor"); p += 2
        elif c == "D" and p + 1 < n and s[p + 1] in "012":
            parts.append("~" + (parts[-1].split("<")[0] if parts else "dtor")); p += 2
        elif c == "S" and p + 1 < n and s[p + 1] in "abtiso":
            parts.append("std"); p += 2
        elif c == "E" and nested:
            p += 1; break
        elif not nested:
            break
        elif c.islower() and p + 1 < n and s[p + 1].isalpha():
            parts.append("operator" + s[p:p + 2]); p += 2
        else:
            return None
        if not nested:
            break
    if not parts:
        return None
    rest = s[p:]
    return "::".join(parts) + ("()" if rest in ("", "v") else "(…)")


def read_nso(b):
    """Decompress an NSO and describe it: header, module, build id, SDK/middleware, source files,
    exported/imported functions, readable strings."""
    if b[:4] != b"NSO0":
        raise ValueError("not NSO")
    version, _r, flags = struct.unpack_from("<III", b, 4)
    segs = []
    zbic_missing = False
    for i, name in enumerate(("text", "ro", "data")):
        foff, moff, size = struct.unpack_from("<III", b, 0x10 + i * 0x10)
        csize = struct.unpack_from("<I", b, 0x60 + i * 4)[0]
        raw = b[foff:foff + (csize if flags & (1 << i) else size)]
        method = "none"
        if not flags & (1 << i):
            data = raw
        elif raw[:4] == b"ZBIC":
            # ZBIC (newer games, system 22.0+): zstd with a different magic and table coding - needs the zbic package
            method = "ZBIC"
            try:
                import zbic
                data = zbic.decompress(bytes(raw))
            except ImportError:
                zbic_missing, data = True, b""
        else:
            method, data = "LZ4", lz4_block_decompress(raw, size)
        segs.append((name, moff, data, csize, bool(flags & (1 << i)), bool(flags & (8 << i)),
                     b[0xA0 + i * 32:0xC0 + i * 32].hex(), method, size))
    bss = struct.unpack_from("<I", b, 0x3C)[0]
    build_id = b[0x40:0x60].hex().upper()
    while build_id.endswith("00") and len(build_id) > 40:
        build_id = build_id[:-2]
    ro = segs[1][2]
    emb_off, emb_size, dstr_off, dstr_size, dsym_off, dsym_size = struct.unpack_from("<6I", b, 0x88)
    out = {"_binary_format": "NSO0 (Switch executable)",
           "_note": "A program module from exefs: main = the game itself, rtld = the dynamic loader, sdk = the "
                    "Nintendo SDK libraries, subsdk* = other libraries. Decoded here: header, build id, the "
                    "middleware/SDK versions it was built with, source file paths left in it, its exported and "
                    "imported functions (C++ names shortened - parameters not decoded), and readable strings "
                    "(memory address, then text). The machine code itself is not disassembled."}
    # module path: start of .ro = u32 0, u32 length, path
    if len(ro) >= 8:
        z, l = struct.unpack_from("<II", ro, 0)
        if z == 0 and 0 < l < 0x200:
            path = ro[8:8 + l].split(b"\0")[0].decode("utf-8", "replace")
            out["module_name"] = re.split(r"[\\/]", path)[-1].rsplit(".", 1)[0]
            out["module_path"] = path
    out["build_id"] = build_id
    out["nso_version"] = version
    out["segments"] = {s[0]: {"memory_offset": "0x%X" % s[1], "size": s[8],
                              "compressed_size": s[3] if s[4] else s[8], "compression": s[7],
                              "hash_checked": s[5], "sha256": s[6]} for s in segs}
    out["bss_size"] = bss
    if zbic_missing:
        out["_warning"] = ("This program file is compressed with ZBIC (used by newer games). To decode its strings, "
                           "functions and SDK versions, run  pip install zbic  in a command prompt and extract again.")
        return out
    # SDK / middleware ("SDK MW+Vendor+Name-Version-Build")
    mw = []
    for m in re.finditer(rb"SDK MW\+[\x21-\x7e]+", ro):
        t = m.group().decode()[7:]
        if t not in mw: mw.append(t)
    if mw:
        out["sdk_and_middleware"] = mw
    # dynamic symbols
    def cstr(off):
        if not 0 <= off < len(ro): return ""
        e = ro.find(b"\0", off)
        return ro[off:e if e >= 0 else len(ro)].decode("utf-8", "replace")
    exports, imports = [], []
    if dsym_size and dsym_off + dsym_size <= len(ro) and dstr_off + dstr_size <= len(ro):
        for i in range(dsym_size // 24):
            st_name, info, other, shndx, value, size = struct.unpack_from("<IBBHQQ", ro, dsym_off + i * 24)
            if not st_name: continue
            nm = cstr(dstr_off + st_name)
            if not nm: continue
            kind = info & 0xF
            dm = demangle(nm)
            label = dm if dm else nm
            if shndx == 0:
                imports.append(label)
            else:
                exports.append("0x%08X  %s%s" % (value, label, "" if kind == 2 else "  [data]" if kind == 1 else ""))
        out["exported_symbol_count"] = len(exports)
        out["imported_symbol_count"] = len(imports)
    # strings
    strings, sources, notices = [], [], []
    dyn_lo, dyn_hi = min(dstr_off, dsym_off), max(dstr_off + dstr_size, dsym_off + dsym_size)
    counts = {}
    for name, moff, data, *_ in segs[1:]:
        k = 0
        for m in _STR_RE.finditer(data):
            if name == "ro" and dyn_lo <= m.start() < dyn_hi:
                continue                                  # symbol names: listed above
            if m.end() < len(data) and data[m.end()] != 0:
                continue                                  # real C strings end with a 0 byte
            t = m.group().decode("utf-8", "replace")
            if not _good_string(t): continue
            k += 1
            st = t.strip()
            if re.search(r"\.(?:cpp|cc|cxx|c|hpp|h|inl)$", st, re.I) and ("/" in st or "\\" in st):
                if st not in sources: sources.append(st)
            if re.search(r"copyright|\(c\)\s*\d|\u00a9|\bversion \d", st, re.I) and st not in notices:
                notices.append(st)
            strings.append("0x%08X  %s" % (moff + m.start(), t))
        counts[name] = k
    if sources:
        out["source_files"] = sorted(sources, key=str.lower)
    if notices:
        out["copyright_and_version_notices"] = notices
    out["string_counts"] = counts
    if exports: out["exported_symbols"] = sorted(exports)
    if imports: out["imported_symbols"] = sorted(set(imports), key=str.lower)
    out["strings"] = strings
    return out


_FS_PERMS = ["ApplicationInfo", "BootModeControl", "Calibration", "SystemSaveData", "GameCard", "SaveDataBackUp",
             "SaveDataManagement", "BisAllRaw", "GameCardRaw", "GameCardPrivate", "SetTime", "ContentManager",
             "ImageManager", "CreateSaveData", "SystemSaveDataManagement", "BisFileSystem", "SystemUpdate",
             "SaveDataMeta", "DeviceSaveData", "SettingsControl", "SystemData", "SdCard", "Host", "FillBis",
             "CorruptSaveData", "SaveDataForDebug", "FormatSdCard", "GetRightsId", "RegisterExternalKey",
             "RegisterUpdatePartition", "SaveDataTransfer", "DeviceDetection", "AccessFailureResolution",
             "SaveDataTransferVersion2", "RegisterProgramIndexMapInfo", "CreateOwnSaveData", "MoveCacheStorage"]


def _npdm_services(b, off, size):
    out = []; p = off
    while p < off + size and p < len(b):
        c = b[p]; l = (c & 7) + 1
        nm = b[p + 1:p + 1 + l].decode("ascii", "replace")
        out.append(nm + ("  (hosts it)" if c & 0x80 else ""))
        p += 1 + l
    return out


def _npdm_kernel(b, off, size):
    caps = []; syscalls = []
    for i in range(size // 4):
        v = struct.unpack_from("<I", b, off + i * 4)[0]
        if v == 0xFFFFFFFF: continue
        t = 0
        while t < 32 and (v >> t) & 1: t += 1
        if t == 3:
            caps.append("threads: priority %d-%d, CPU cores %d-%d" % ((v >> 10) & 63, (v >> 4) & 63, (v >> 16) & 255, (v >> 24) & 255))
        elif t == 4:
            idx = (v >> 29) & 7
            for bit in range(24):
                if (v >> (5 + bit)) & 1: syscalls.append(idx * 24 + bit)
        elif t == 6:
            caps.append("memory map: 0x%X..." % ((v >> 7) << 12))
        elif t == 7:
            caps.append("I/O memory page: 0x%X" % ((v >> 8) << 12))
        elif t == 10:
            caps.append("memory region map (0x%X)" % v)
        elif t == 11:
            caps.append("interrupts: %d, %d" % ((v >> 12) & 0x3FF, (v >> 22) & 0x3FF))
        elif t == 13:
            caps.append("program type: %d" % ((v >> 14) & 7))
        elif t == 14:
            caps.append("kernel version: %d.%d" % (v >> 19, (v >> 15) & 15))
        elif t == 15:
            caps.append("handle table size: %d" % ((v >> 16) & 0x3FF))
        elif t == 16:
            caps.append("debug flags: allow debug %s, force debug %s" % (bool(v >> 17 & 1), bool(v >> 18 & 1)))
        else:
            caps.append("0x%08X (type %d)" % (v, t))
    if syscalls:
        caps.append("allowed system calls (%d): %s" % (len(syscalls), " ".join("%02X" % s for s in sorted(syscalls))))
    return caps


def read_npdm(b):
    """main.npdm: "META" header + ACI0 (what this program is granted) + ACID (signed limits)."""
    out = {"_binary_format": "META (program metadata, main.npdm)",
           "_note": "Settings and permissions for the program in exefs: its name, main thread settings, the "
                    "system services it may use, file-system permissions and kernel capabilities."}
    if len(b) < 0x80 or b[:4] != b"META":
        out["_warning"] = "not a META file"; return out
    flags, _r, prio, core = b[0x0C], b[0x0D], b[0x0E], b[0x0F]
    sysres, ver, stack = struct.unpack_from("<III", b, 0x14)
    out["name"] = b[0x20:0x30].split(b"\0")[0].decode("ascii", "replace")
    pc = b[0x30:0x40].split(b"\0")[0].decode("ascii", "replace")
    if pc: out["product_code"] = pc
    out["version"] = ver
    out["64_bit"] = bool(flags & 1)
    out["address_space"] = {0: "32-bit", 1: "64-bit (old)", 2: "32-bit, no reserved", 3: "64-bit"}.get((flags >> 1) & 7, (flags >> 1) & 7)
    out["main_thread"] = {"priority": prio, "cpu_core": core, "stack_size": stack}
    if sysres: out["system_resource_size"] = sysres
    aci_off, aci_size, acid_off, acid_size = struct.unpack_from("<IIII", b, 0x70)
    if acid_off + 0x240 <= len(b) and b[acid_off + 0x200:acid_off + 0x204] == b"ACID":
        a = acid_off
        lo, hi = struct.unpack_from("<QQ", b, a + 0x210)
        out["ACID (signed limits)"] = {"program_id_range": "%016X - %016X" % (lo, hi),
                                      "flags": "0x%X" % struct.unpack_from("<I", b, a + 0x20C)[0]}
    if aci_off + 0x40 <= len(b) and b[aci_off:aci_off + 4] == b"ACI0":
        a = aci_off
        pid = struct.unpack_from("<Q", b, a + 0x10)[0]
        fah_o, fah_s, sac_o, sac_s, kac_o, kac_s = struct.unpack_from("<6I", b, a + 0x20)
        aci = {"program_id": "%016X" % pid}
        if fah_s >= 12:
            perm = struct.unpack_from("<Q", b, a + fah_o + 4)[0]
            names = [n for i, n in enumerate(_FS_PERMS) if perm >> i & 1]
            extra = [str(i) for i in range(len(_FS_PERMS), 64) if perm >> i & 1]
            aci["file_system_permissions"] = "0x%016X" % perm + (" = " + ", ".join(names) if names else "") + \
                                            (" + bits " + ",".join(extra) if extra else "")
        aci["services"] = _npdm_services(b, a + sac_o, sac_s)
        aci["kernel_capabilities"] = _npdm_kernel(b, a + kac_o, kac_s)
        out["ACI0 (granted)"] = aci
    return out


# ---------- voxel terrain (Voxel/.../<part>/Info.bin, vhead/vbody, ehead/ebody) ----------
# Info.bin: "VX", then size in voxels (x, y, z) at byte 12 and voxel scale (3 floats) at byte 24.
# The part is cut into 8x8x8-voxel blocks. *head.bin = bitmap, 1 bit per block (most significant
# bit first, x fastest, then y, then z); a set bit = that block has data. *body.bin = one entry per
# set bit, in order: u32 compressed size + zstd frame of 512 voxel records (x fastest, then y, then z).
#   v (voxels): 8 or 5 bytes per voxel (block = 4096 or 2560 bytes) - byte 0 fill (0 empty, 255 solid,
#               between = surface), bytes 1-2 material ids (MaterialTable.bin), the rest unknown
#   e (edges):  9 bytes per voxel  - probably surface crossings on the voxel's 3 edges + their normals
SPECIAL_EXTS = ("vgeob", "bphsh", "bflan", "bflyt", "bntx", "npdm", "bars", "bwav", "bnvib", "bfttf", "bfotf", "bfcpx", "bffnt", "bfres", "asb", "baev", "bfevfl", "bnsh", "sharcb", "bfsha", "gsh", "pck", "bphcl", "bphsc", "baglcube", "baglfila", "msbp", "bphhb", "baatn", "baroc", "baudc", "baacl", "bprojgfxbase", "bssprojgfxbase", "baglblm", "bagldof", "baglccr", "baglenv", "baglenvset", "baglsky", "baglshpp", "bgenv", "listing", "rsizetable", "bgapkginfo", "bgapkglist", "bgmsconf", "bptclconf", "bagst", "belnk", "bslnk", "bgsdw", "baglvolm", "baglmf", "bofx", "bushvt", "bin", "raw", "glsl", "hght", "norm", "mate", "extm", "tscb", "ainb", "ghost", "bcul", "bbox", "bgrd", "dapg", "pccg", "dalp", "dlpl", "dlpa", "dlpg", "dlpr", "bkls", "dcgm", "xtx", "sharc", "baatarc")
SPECIAL_EXTS = set(SPECIAL_EXTS) | {"dat", "gswg", "bwr", "bkprmb", "bkprb", "bkprcb", "otf", "win", "tree3", "rs", "csv", "txt2"}   # (NSSR)
VOXEL_FILES = ("info.bin", "vhead.bin", "vbody.bin", "ehead.bin", "ebody.bin", "materialtable.bin")
# MaterialTable.bin (in the stage scenario folder): u16 version, u16 count, then count x 40-byte names.
# A voxel's material id is an index into this list.
_mat_cache = {}


def voxel_materials(stage_dir):
    if stage_dir in _mat_cache:
        return _mat_cache[stage_dir]
    names = None
    try:
        with open(long_path(os.path.join(stage_dir, "MaterialTable.bin")), "rb") as f:
            b = f.read()
        n = struct.unpack_from("<H", b, 2)[0]
        if 4 + n * 40 <= len(b):
            names = [b[4 + i * 40:44 + i * 40].split(b"\0")[0].decode("ascii", "replace") for i in range(n)]
    except (OSError, struct.error):
        pass
    _mat_cache[stage_dir] = names
    return names


def voxel_info(path):
    info = os.path.join(os.path.dirname(path), "Info.bin")
    try:
        with open(long_path(info), "rb") as f:
            b = f.read()
    except OSError:
        return None
    if len(b) < 36 or b[:2] != b"VX":
        return None
    x, y, z = struct.unpack_from("<3I", b, 12)
    return {"size": [x, y, z], "blocks": [(x + 7) // 8, (y + 7) // 8, (z + 7) // 8],
            "scale": [round(v, 6) for v in struct.unpack_from("<3f", b, 24)],
            "other_fields": list(struct.unpack_from("<2xHII", b, 0))}


def voxel_bitmap(head, grid):
    bits = []
    for byte in head:
        for k in range(7, -1, -1):
            bits.append((byte >> k) & 1)
    total = grid[0] * grid[1] * grid[2] if grid else len(bits)
    return [i for i in range(min(total, len(bits))) if bits[i]]


def voxel_blocks(body, decomp):
    p, out = 0, []
    while p + 4 <= len(body):
        n = struct.unpack_from("<I", body, p)[0]
        out.append(decomp.decompress(body[p + 4:p + 4 + n], max_output_size=1 << 20))
        p += 4 + n
    return out, p


# ---------- other formats identified by name + contents ----------
def _printable(t):
    """text safe for the log: control characters (a NUL cuts the app's log off) shown as \\xNN"""
    return "".join(c if c >= " " or c in "\t" else "\\x%02x" % ord(c) for c in t)


def _ext(label):
    n = label.rsplit("/", 1)[-1].lower()
    if n.endswith(".zs"): n = n[:-3]
    return n.rsplit(".", 1)[-1] if "." in n else ""


def text_of(blob):
    """UTF-8 text (with or without a byte-order mark) -> str, else None"""
    b = blob[3:] if blob[:3] == b"\xef\xbb\xbf" else blob
    if not b or len(b) > 8 * 1024 * 1024 or b"\x00" in b[:4096]:
        return None
    try:
        t = b.decode("utf-8")
    except UnicodeDecodeError:
        return None
    bad = sum(1 for c in t[:20000] if ord(c) < 32 and c not in "\t\r\n")
    return t if bad == 0 else None


def read_bnvib(b):
    """Nintendo HD Rumble vibration: u32 info size (4, 0xC with loop, 0x10 with loop + interval),
    u16 format, u16 sample rate, [u32 loop start, u32 loop end, [u32 loop interval]], u32 data size, data
    (4 bytes per sample: amplitude/frequency for the low and high bands)."""
    info_size = struct.unpack_from("<I", b, 0)[0]
    fmt, rate = struct.unpack_from("<HH", b, 4)
    out = {"_binary_format": "BNVIB (Nintendo HD Rumble vibration pattern)",
           "_note": "Controller rumble played with an event or animation. Each sample = 4 bytes "
                    "(strength and pitch for the low and high frequency bands).",
           "format": fmt, "sample_rate_hz": rate}
    if info_size >= 0xC:
        ls, le = struct.unpack_from("<II", b, 8)
        out["loop_start_sample"] = ls; out["loop_end_sample"] = le
    if info_size >= 0x10:
        out["loop_interval"] = struct.unpack_from("<I", b, 0x10)[0]
    data_size = struct.unpack_from("<I", b, 4 + info_size)[0]
    samples = data_size // 4
    out["samples"] = samples
    if rate:
        out["seconds"] = round(samples / rate, 3)
    if 4 + info_size + 4 + data_size != len(b):
        out["_warning"] = "sizes in the header don't add up to the file size (%d vs %d)" % (8 + info_size + data_size, len(b))
    return out


def _sfnt_names(f):
    """family / style / full name from an OpenType/TrueType 'name' table"""
    try:
        n = struct.unpack_from(">H", f, 4)[0]
        for i in range(n):
            tag, _, off, ln = struct.unpack_from(">4sIII", f, 12 + i * 16)
            if tag != b"name":
                continue
            cnt, so = struct.unpack_from(">HH", f, off + 2)
            got = {}
            for k in range(cnt):
                plat, enc, lang, nid, l2, o2 = struct.unpack_from(">6H", f, off + 6 + k * 12)
                if nid not in (1, 2, 4) or nid in got:
                    continue
                raw = f[off + so + o2: off + so + o2 + l2]
                if plat in (0, 3):
                    if plat == 3 and lang not in (0x409, 0x411) and any(v for v in got.values()):
                        continue
                    got[nid] = raw.decode("utf-16-be", "replace")
                elif plat == 1:
                    got.setdefault(nid, raw.decode("latin-1"))
            return {k: v for k, v in (("family", got.get(1)), ("style", got.get(2)), ("full_name", got.get(4))) if v}
    except struct.error:
        pass
    return {}


def _sfnt_table(f, want):
    n = struct.unpack_from(">H", f, 4)[0]
    for i in range(n):
        tag, _, off, ln = struct.unpack_from(">4sIII", f, 12 + i * 16)
        if tag == want:
            return off, ln
    return None, 0


def _cp_ranges(cps):
    """[0x20, 0x21, 0x22, 0x30] -> ["U+0020-U+0022 ( !\")", "U+0030 (0)"]"""
    out, cps = [], sorted(cps)
    i = 0
    while i < len(cps):
        j = i
        while j + 1 < len(cps) and cps[j + 1] == cps[j] + 1:
            j += 1
        a, z = cps[i], cps[j]
        txt = "".join(chr(c) for c in range(a, min(z, a + 39) + 1) if c >= 32 and not 0xD800 <= c < 0xF900)
        if z - a >= 40:
            txt += "..."
        out.append(("U+%04X" % a if a == z else "U+%04X-U+%04X" % (a, z)) + (" (%s)" % txt if txt.strip() else ""))
        i = j + 1
    return out


def _sfnt_chars(f):
    """glyph count and the characters the font has (from its cmap)"""
    out = {}
    off, ln = _sfnt_table(f, b"maxp")
    if off is not None:
        out["glyphs"] = struct.unpack_from(">H", f, off + 4)[0]
    off, ln = _sfnt_table(f, b"head")
    if off is not None:
        out["units_per_em"] = struct.unpack_from(">H", f, off + 18)[0]
    off, ln = _sfnt_table(f, b"cmap")
    if off is None:
        return out
    nt = struct.unpack_from(">H", f, off + 2)[0]
    best = None
    for i in range(nt):
        pid, eid, so = struct.unpack_from(">HHI", f, off + 4 + i * 8)
        fmt = struct.unpack_from(">H", f, off + so)[0]
        if fmt == 12 or (fmt == 4 and best is None):
            best = (fmt, off + so)
    if not best:
        return out
    fmt, t = best
    cps = set()
    if fmt == 4:
        segs = struct.unpack_from(">H", f, t + 6)[0] // 2
        ends = struct.unpack_from(">%dH" % segs, f, t + 14)
        starts = struct.unpack_from(">%dH" % segs, f, t + 16 + segs * 2)
        deltas = struct.unpack_from(">%dH" % segs, f, t + 16 + segs * 4)
        ro_at = t + 16 + segs * 6
        ros = struct.unpack_from(">%dH" % segs, f, ro_at)
        for k in range(segs):
            for c in range(starts[k], ends[k] + 1):
                if c == 0xFFFF:
                    break
                if ros[k] == 0:
                    g = (c + deltas[k]) & 0xFFFF
                else:
                    g = struct.unpack_from(">H", f, ro_at + k * 2 + ros[k] + (c - starts[k]) * 2)[0]
                    g = (g + deltas[k]) & 0xFFFF if g else 0
                if g:
                    cps.add(c)
    else:
        ng = struct.unpack_from(">I", f, t + 12)[0]
        for k in range(ng):
            a, z, _ = struct.unpack_from(">III", f, t + 16 + k * 12)
            cps.update(range(a, z + 1))
    out["characters"] = len(cps)
    pua = sorted(c for c in cps if 0xE000 <= c <= 0xF8FF)
    if pua:
        out["private_use_characters"] = len(pua)
        out["_note_private_use"] = ("U+E000-U+F8FF are private-use codes for symbols. In NinDev-HardKey they are the "
                                   "controller buttons that message tags [12:...] insert (Glossary/PadStyle names them: "
                                   "DualButtonA = U+E0E0...)")
    rs = _cp_ranges(cps)
    out["character_ranges"] = rs if len(rs) <= 300 else rs[:300] + ["... %d more ranges" % (len(rs) - 300)]
    return out


def _bfcpx_block(b, X, out):
    """One font block at X: kind 0 = bitmap font name, 1 = list of fonts, 2 = several blocks (offsets from X)."""
    kind = struct.unpack_from("<I", b, X)[0]
    if kind == 0:
        no = X + struct.unpack_from("<I", b, X + 4)[0]
        out["bitmap_font"] = b[no:b.index(b"\0", no)].decode("ascii", "replace")
        return out
    if kind == 2:
        first = struct.unpack_from("<I", b, X + 4)[0]
        n = max(0, min((first - 4) // 4, 64))
        out["variants"] = []
        for k in range(n):
            off = struct.unpack_from("<I", b, X + 4 + 4 * k)[0]
            if not off or X + off + 0x24 > len(b):
                continue
            out["variants"].append(_bfcpx_block(b, X + off, {"variant": k}))
        return out
    if kind != 1:
        out["_warning"] = "unknown block kind %d at 0x%X" % (kind, X)
        return out
    out["em_size"] = round(struct.unpack_from("<f", b, X + 4)[0], 3)
    cnt = min(struct.unpack_from("<I", b, X + 0xC)[0], 32)
    fonts = []
    for k in range(cnt):
        E = X + 0x24 + k * 0x28
        if E + 0x28 > len(b):
            break
        no = X + struct.unpack_from("<I", b, E + 8)[0]
        name = b[no:b.index(b"\0", no)].decode("ascii", "replace") if no < len(b) else "?"
        shift = struct.unpack_from("<h", b, E + 14)[0]
        scale = struct.unpack_from("<f", b, E + 16)[0]
        nr, ro = struct.unpack_from("<II", b, E + 28)
        ro += X
        cps, bad = [], False
        for i in range(min(nr, 4096)):
            if ro + i * 8 + 8 > len(b):
                bad = True
                break
            a, z = struct.unpack_from("<II", b, ro + i * 8)
            if a > z or z > 0x10FFFF or len(cps) + (z - a) > 0x110000:
                bad = True
                break
            cps.extend(range(a, z + 1))
        if bad:
            out["_warning"] = "character ranges of font %d don't fit the known layout" % k
            out["_first_bytes"] = b[:0x80].hex(" ")
            break
        fonts.append({"font": name, "scale": round(scale, 3), "baseline_shift": shift,
                      "characters": _cp_ranges(cps) if nr else "every character the font has (not taken by an earlier font)"})
    out["fonts"] = fonts
    return out


def read_bfcpx(b):
    """FCPX: a font as the UI layouts name it (FontCaption.fcpx): either a bitmap font, or a list of real fonts
    tried in order, each for the characters listed (or, with none listed, every character it has). Mario Kart
    World also has kind 2: several such lists ("variants") in one file."""
    out = {"_binary_format": "Font combination (.bfcpx, NintendoWare font converter 'pair font')",
           "_note": "Layouts name this (e.g. FontCaption.fcpx). Each character is drawn with the first font in the "
                    "list that is meant for it and has it. Sizes are scales of em_size; baseline_shift moves a "
                    "font's characters up (+) or down (-) in em_size units."}
    return _bfcpx_block(b, 0x18, out)


def read_bffnt(b):
    """FFNT bitmap font (Switch 2: glyph sheets are one BNTX texture with a layer per sheet, stored bottom row first)."""
    out = {"_binary_format": "Bitmap font (.bffnt)",
           "_note": "Glyph pictures on texture sheets. In an icon font such as PictColor the character codes are just "
                    "slots: the layout's text (e.g. 'v') picks the picture. View the sheets with the Textures button."}
    hs = struct.unpack_from("<H", b, 6)[0]
    out["version"] = "%d.%d.%d" % (b[0xB], b[0xA], b[9])
    if b[hs:hs + 4] != b"FINF":
        out["_warning"] = "no FINF block"
        return out
    fi = hs + 8
    ftype, height, width, ascent, line_feed, alter = struct.unpack_from("<BBBBHH", b, fi)
    out.update({"height": height, "width": width, "ascent": ascent, "line_feed": line_feed,
                "replacement_glyph": alter, "encoding": {1: "UTF-16", 0: "UTF-8"}.get(b[fi + 11], b[fi + 11])})
    pG, pW, pM = struct.unpack_from("<III", b, fi + 12)
    cw, ch, nsheet, maxw, ssize, base, sfmt, cols, rows, sw, sh, sd = struct.unpack_from("<BBBBIHHHHHHI", b, pG)
    out["sheets"] = {"count": nsheet, "size": "%dx%d" % (sw, sh), "cell": "%dx%d" % (cw, ch), "cells_per_sheet": "%d x %d" % (cols, rows),
                     "baseline": base, "format_code": sfmt}
    if b[sd:sd + 4] == b"BNTX":
        try:
            bt = read_bntx(b[sd:])
            out["sheets"]["texture"] = bt.get("textures", bt)
        except Exception:
            pass
    widths = {}
    p = pW
    while p and p + 8 <= len(b):
        a, z, nx = struct.unpack_from("<HHI", b, p)
        if z < a or p + 8 + (z - a + 1) * 3 > len(b):
            break
        for i in range(a, z + 1):
            widths[i] = struct.unpack_from("<bBB", b, p + 8 + (i - a) * 3)
        if not nx or nx <= p:
            break
        p = nx
    cmap = {}
    p = pM
    while p and p + 16 <= len(b):
        a, z, meth, _, nx = struct.unpack_from("<IIHHI", b, p)
        d = p + 16
        if z < a or z > 0x10FFFF:
            out["_warning"] = "character map block at 0x%X has an unexpected range" % p
            break
        if meth == 0:
            o = struct.unpack_from("<H", b, d)[0]
            for c in range(a, z + 1):
                cmap[c] = c - a + o
        elif meth == 1:
            for c in range(a, z + 1):
                i = struct.unpack_from("<H", b, d + (c - a) * 2)[0]
                if i != 0xFFFF:
                    cmap[c] = i
        elif meth == 2:
            k = struct.unpack_from("<I", b, d)[0]
            for i in range(k):
                c, g = struct.unpack_from("<II", b, d + 4 + i * 8)
                cmap[c] = g
        if not nx or nx <= p:
            break
        p = nx
    out["glyph_count"] = len(widths)
    out["characters"] = len(cmap)
    out["character_ranges"] = _cp_ranges(cmap)
    per = max(cols * rows, 1)
    out["glyphs"] = ["U+%04X %s  glyph %d (sheet %d, column %d, row %d)  left %d, width %d, advance %d" % (
        c, chr(c) if c >= 32 and not 0xD800 <= c < 0xE000 else "", g, g // per, (g % per) % cols, (g % per) // cols, *widths.get(g, (0, 0, 0)))
        for c, g in sorted(cmap.items())]
    return out


def read_nintendo_font(b, ext):
    """Switch-style obfuscated font: u32 magic ^ key, u32 size ^ key, then the font XORed with the
    4-byte key (little-endian words). The key is found from the font's own first bytes."""
    out = {"_binary_format": "Nintendo obfuscated %s font (.%s)" % ("OpenType" if ext == "bfotf" else "TrueType", ext),
           "_note": "A normal OTF/TTF font XORed with a 4-byte key and given an 8-byte header."}
    if len(b) < 16:
        return out
    for sig in (b"OTTO", b"\x00\x01\x00\x00", b"true"):
        kb = bytes(b[8 + i] ^ sig[i] for i in range(4))           # the font starts with its signature
        size = struct.unpack(">I", bytes(b[4 + i] ^ kb[i] for i in range(4)))[0]
        if abs(size - (len(b) - 8)) <= 4:
            body = bytes(x ^ kb[i & 3] for i, x in enumerate(b[8:8 + min(size, 4 * 1024 * 1024)]))
            out["key"] = kb.hex()
            out["font_type"] = "OpenType (CFF)" if sig == b"OTTO" else "TrueType"
            out["font_size_bytes"] = size
            out.update(_sfnt_names(body))
            try:
                out.update(_sfnt_chars(body))
            except (struct.error, IndexError):
                pass
            return out
    out["_warning"] = "the XOR key couldn't be worked out"
    return out


def read_vgeob(b):
    """VGEOB voxel geometry: "VGEOB", u32 record count, then 9-byte run records over the grid (x fastest, then y,
    then z; GridNum from Voxel/Geometry/<name>.al__gen__VoxelGeometry).
    Record: s8 signed distance (negative = inside, positive = outside, surface at 0),
            u32 empty cells skipped before it, u32 extra cells repeating the same value.
    Skipped cells are empty (outside). Confirmed with ZainBodyCube and the DK amiibo body."""
    n = struct.unpack_from("<I", b, 5)[0]
    out = {"_binary_format": "VGEOB (voxel geometry of one material of a voxel object)",
           "_note": "Signed distance field for one material layer (named by the file, e.g. Rock_Type_00) of a voxel "
                    "object: negative = inside, positive = outside, surface where it crosses 0. Grid size and "
                    "bounding box are in Voxel/Geometry/<object>.al__gen__VoxelGeometry (GridNum, AABBMin/Max, "
                    "CellSize). Empty space is skipped and solid interiors are run-length coded.",
           "records": n}
    if 9 + n * 9 != len(b):
        out["_warning"] = "record count doesn't match the file size"
        return out
    pos = 0; inside = 0; outside = 0; vmin = 127; vmax = -128
    for i in range(n):
        v, skip, rep = struct.unpack_from("<bII", b, 9 + i * 9)
        pos += skip + rep + 1
        if v < 0: inside += rep + 1
        else: outside += rep + 1
        vmin = min(vmin, v); vmax = max(vmax, v)
    out["grid_cells_covered"] = pos
    out["cells_inside"] = inside
    out["cells_near_surface_outside"] = outside
    out["distance_range"] = [vmin, vmax]
    return out


def _bwav_info(b, base=0):
    """one BWAV (at base) -> dict, or None"""
    if base + 0x10 > len(b) or b[base:base + 4] != b"BWAV":
        return None
    nch = struct.unpack_from("<H", b, base + 0x0E)[0]
    if not 1 <= nch <= 16 or base + 0x10 + nch * 0x4C > len(b):
        return None
    codec, _pan, rate, samples = struct.unpack_from("<HHII", b, base + 0x10)
    le, ls = struct.unpack_from("<II", b, base + 0x10 + 0x3C)
    d = {"codec": {0: "PCM 16-bit", 1: "DSP-ADPCM"}.get(codec, "codec %d" % codec),
         "channels": nch, "sample_rate": rate, "samples": samples,
         "seconds": round(samples / rate, 3) if rate else None}
    pf = struct.unpack_from("<H", b, base + 0x0C)[0]
    ps = struct.unpack_from("<I", b, base + 0x10 + 0x0C)[0]
    if pf == 1 and 0 < ps < samples:
        d["prefetch_only"] = "only the first %.3f s is stored here; the rest is streamed from Stream/<name>.bwav" % (ps / rate if rate else 0)
    if le != 0xFFFFFFFF and ls < le <= samples:
        d["loop"] = "%.3f - %.3f s" % (ls / rate, le / rate) if rate else [ls, le]
    return d


def read_bwav(b):
    d = _bwav_info(b)
    out = {"_binary_format": "BWAV (Nintendo sound)",
           "_note": "A single sound. Select it in the GUI and click Play to listen or save it as WAV."}
    if d is None:
        out["_warning"] = "couldn't read the BWAV header"
    else:
        out.update(d)
    return out


def read_bars(b):
    """BARS: "BARS", u32 size, BOM, u16 version, u32 count, count x u32 name hash,
    count x (u32 AMTA offset, u32 BWAV offset). AMTA+0x24: u32 offset (from that field) of the name."""
    out = {"_binary_format": "BARS (Nintendo sound bank)",
           "_note": "A bank of sounds (each an AMTA name/metadata block + a BWAV). Select it in the GUI and click "
                    "Play to listen to them or save them as WAV."}
    if len(b) < 0x10:
        out["_warning"] = "too short"; return out
    n = struct.unpack_from("<I", b, 0x0C)[0]
    if not 0 < n <= 100000 or 0x10 + n * 12 > len(b):
        out["_warning"] = "odd header"; return out
    tab = 0x10 + n * 4
    sounds = []; total = 0.0
    for i in range(n):
        amta, bw = struct.unpack_from("<II", b, tab + i * 8)
        name = "sound %d" % i
        if amta + 0x28 <= len(b) and b[amta:amta + 4] == b"AMTA":
            no = amta + 0x24 + struct.unpack_from("<I", b, amta + 0x24)[0]
            if no < len(b):
                e = b.find(b"\0", no)
                nm = b[no:e if e >= 0 else len(b)][:200].decode("ascii", "replace")
                if nm: name = nm
        d = _bwav_info(b, bw)
        if d is None:
            sounds.append("%s  (no BWAV)" % name); continue
        total += d["seconds"] or 0
        line = "%s  %.2f s  %s  %d ch  %d Hz" % (name, d["seconds"] or 0, d["codec"], d["channels"], d["sample_rate"])
        if "loop" in d: line += "  loop %s" % d["loop"]
        if "prefetch_only" in d: line += "  (start only; full sound in Stream/%s.bwav)" % name
        sounds.append(line)
    out["sound_count"] = n
    out["total_seconds"] = round(total, 2)
    out["sounds"] = sounds
    return out


# ---------- Havok tagfiles (TAG0, Havok 2022) and Phive collision shapes (.bphsh) ----------
def _hk_sections(b, o, end, out):
    while o + 8 <= end:
        h = struct.unpack_from(">I", b, o)[0]; size = h & 0x3FFFFFFF; tag = b[o + 4:o + 8].decode("latin1")
        out[tag] = (o + 8, o + size)
        if (h >> 30) == 0 and tag in ("TAG0", "TYPE", "INDX"):
            _hk_sections(b, o + 8, o + size, out)
        if size < 8: break
        o += size


class _HkR:
    def __init__(s, b, o, e): s.b, s.o, s.e = b, o, e
    def u8(s): v = s.b[s.o]; s.o += 1; return v
    def vi(s):
        b0 = s.u8()
        if b0 < 0x80: return b0
        if b0 & 0xC0 == 0x80: return ((b0 & 0x3F) << 8) | s.u8()
        if b0 & 0xE0 == 0xC0: n, v = 2, b0 & 0x1F
        elif b0 & 0xF8 == 0xE0: n, v = 3, b0 & 7
        elif b0 & 0xF8 == 0xE8: n, v = 4, b0 & 7
        elif b0 & 0xF8 == 0xF0: n, v = 7, b0 & 7
        elif b0 in (0xF8, 0xF9): n, v = 8, 0
        else: raise ValueError("bad packed int 0x%X" % b0)
        for _ in range(n): v = (v << 8) | s.u8()
        return v


class HavokTagfile:
    """Reads a Havok 2022 tagfile (as used inside Phive files): its type descriptions and its objects."""
    def __init__(self, b, start=0):
        self.b = b; S = {}; _hk_sections(b, start, len(b), S); self.S = S
        if "TAG0" not in S or "DATA" not in S:
            raise ValueError("not a Havok tagfile")
        self.sdk = b[S["SDKV"][0]:S["SDKV"][1]].decode("latin1") if "SDKV" in S else ""
        self.d0 = S["DATA"][0]
        tst = [x.decode("latin1") for x in b[S["TST1"][0]:S["TST1"][1]].split(b"\0")]
        fst = [x.decode("latin1") for x in b[S["FST1"][0]:S["FST1"][1]].split(b"\0")]
        r = _HkR(b, *S["TNA1"]); n = r.vi(); T = [None]
        for _ in range(1, n):
            nm = tst[r.vi()]; params = [(tst[r.vi()], r.vi()) for _ in range(r.vi())]
            T.append({"name": nm, "params": params})
        r = _HkR(b, *S["TBDY"])
        while r.o < r.e:
            ti = r.vi()
            if ti == 0: break
            t = T[ti]; t["parent"] = r.vi(); fl = r.vi()
            if fl & 1: t["format"] = r.vi()
            if fl & 2: t["subtype"] = r.vi()
            if fl & 4: t["version"] = r.vi()
            if fl & 8: t["size"] = r.vi(); t["align"] = r.vi()
            if fl & 0x10: t["flags"] = r.vi()
            if fl & 0x20:
                ms = []
                for _ in range(r.vi()):
                    mn, mf = fst[r.vi()], r.vi()
                    if mf & 0x80: r.vi()                 # an attribute index comes before the offset
                    ms.append((mn, mf, r.vi(), r.vi()))
                t["members"] = ms
            if fl & 0x40: t["ifaces"] = [(r.vi(), r.vi()) for _ in range(r.vi())]
            if fl & 0x80: t["attr"] = r.vi()
        self.T = T
        self.items = []
        if "ITEM" in S:
            a, e = S["ITEM"]
            for o in range(a, e - 11, 12):
                f, off, cnt = struct.unpack_from("<III", b, o)
                self.items.append((f & 0xFFFFFF, off, cnt))

    def body(self, ti):                          # follow typedef-like entries to the type with the layout
        t = self.T[ti]
        while t is not None and not t.get("format") and t.get("parent"):
            t = self.T[t["parent"]]
        return t
    def size(self, ti):
        t = self.T[ti]
        while t is not None and "size" not in t and t.get("parent"): t = self.T[t["parent"]]
        return (t or {}).get("size", 0)
    def members(self, ti):
        t = self.T[ti]; out = []
        p = t.get("parent")
        if p and (self.T[p].get("format", 0) & 0xF) == 7: out += self.members(p)
        return out + (t.get("members") or [])
    def is_a(self, ti, name):
        while ti:
            if self.T[ti]["name"] == name: return True
            ti = self.T[ti].get("parent")
        return False

    def read(self, ti, addr, big=64):
        """object -> Python value. Arrays longer than `big` become a short description instead."""
        b = self.b; t = self.body(ti)
        if t is None: return None
        fmt = t.get("format", 0); kind = fmt & 0xF; tix = self.T.index(t)
        if kind == 7:
            return {nm: self.read(mt, addr + off, big) for nm, fl, off, mt in self.members(tix)}
        if kind == 2: return bool(b[addr])
        if kind == 4:
            sz = self.size(tix) or 4
            return int.from_bytes(b[addr:addr + sz], "little", signed=bool(fmt & 0x200))
        if kind == 5: return round(struct.unpack_from("<f", b, addr)[0], 6)
        if kind == 8 and fmt & 0x20:               # T[N]
            n = fmt >> 8; sub = t["subtype"]; es = self.size(sub)
            return [self.read(sub, addr + i * es, big) for i in range(n)]
        nm = t["name"]
        if nm in ("hkRelArrayView", "hkRelArray"):
            if nm == "hkRelArrayView": off, cnt = struct.unpack_from("<iI", b, addr)
            else: off = struct.unpack_from("<q", b, addr)[0]; cnt = struct.unpack_from("<I", b, addr + 8)[0] & 0x3FFFFFFF
            sub = t["subtype"]; es = self.size(sub)
            if cnt > big:
                return "(%d x %s)" % (cnt, self.T[sub]["name"])
            return [self.read(sub, addr + off + i * es, big) for i in range(cnt)]
        if nm == "hkRelPtr":
            off = struct.unpack_from("<q", b, addr)[0]
            return None if off == 0 else "(pointer to %s)" % self.T[t["subtype"]]["name"]
        return "(%s)" % nm


def _hk_value(hk, ti, addr, big=48, depth=0, seen=None):
    """a full Havok object -> Python value, following pointers, arrays and strings through the ITEM table.
    Long arrays of numbers are kept (up to 4x big); long arrays of records are cut to big entries."""
    b = hk.b
    if seen is None: seen = set()
    if depth > 24: return "(too deep)"
    t = hk.body(ti)
    if t is None: return None
    fmt = t.get("format", 0); kind = fmt & 0xF; tix = hk.T.index(t)
    def item(idx):
        return hk.items[idx] if 0 < idx < len(hk.items) else None
    if kind == 2: return bool(b[addr])
    if kind == 4:
        sz = hk.size(tix) or 4
        return int.from_bytes(b[addr:addr + sz], "little", signed=bool(fmt & 0x200))
    if kind == 5:
        x = struct.unpack_from("<f", b, addr)[0]
        return round(x, 6) if x == x and abs(x) != float("inf") else str(x)
    if kind == 3:                                      # char*: an item of chars
        it = item(struct.unpack_from("<I", b, addr)[0])
        if not it: return None
        s0 = hk.d0 + it[1]; e = b.find(b"\0", s0, s0 + it[2])
        return b[s0:e if e >= 0 else s0 + it[2]].decode("utf-8", "replace")
    if kind == 6:                                      # pointer: an item
        idx = struct.unpack_from("<I", b, addr)[0]; it = item(idx)
        if not it: return None
        if idx in seen: return "(see object #%d)" % idx
        seen.add(idx)
        v = _hk_value(hk, it[0], hk.d0 + it[1], big, depth + 1, seen) if it[2] == 1 else \
            [_hk_value(hk, it[0], hk.d0 + it[1] + k * hk.size(it[0]), big, depth + 1, seen) for k in range(min(it[2], big))]
        if isinstance(v, dict): v = dict({"_type": hk.T[it[0]]["name"]}, **v)
        return v
    if kind == 8 and fmt & 0x20:                       # T[N]
        n = fmt >> 8; sub = t["subtype"]; es = hk.size(sub)
        return [_hk_value(hk, sub, addr + i * es, big, depth + 1, seen) for i in range(n)]
    if kind == 8:                                      # hkArray: item index, size
        it = item(struct.unpack_from("<I", b, addr)[0])
        if not it: return []
        cnt = it[2]; es = hk.size(it[0]); sk = hk.body(it[0]); skind = (sk or {}).get("format", 0) & 0xF
        lim = big * 4 if skind in (2, 4, 5) or (sk and sk["name"] in ("hkVector4f", "hkVector4", "hkQuaternionf", "hkPackedVector3")) else big
        out = [_hk_value(hk, it[0], hk.d0 + it[1] + k * es, big, depth + 1, seen) for k in range(min(cnt, lim))]
        if cnt > lim: out.append("... %d more (%d in all)" % (cnt - lim, cnt))
        return out
    if kind == 7:
        d = {}
        for nm, fl, off, mt in hk.members(tix):
            d[nm] = _hk_value(hk, mt, addr + off, big, depth + 1, seen)
        if len(d) == 1:                                # wrappers (hkStringPtr, hkRefPtr, hkEnum, hkVector4f...): their one value
            return next(iter(d.values()))
        if set(d) == {"col0", "col1", "col2"} or set(d) == {"col0", "col1", "col2", "col3"}:
            return [d[k] for k in sorted(d)]
        return d
    return "(%s)" % t["name"]


def _member(hk, ti, name):
    for nm, fl, off, mt in hk.members(ti):
        if nm == name: return off, mt
    raise KeyError(name)


def _phive_mesh_at(hk, b, ti, base):
    """decode the hknpMeshShape at address base -> (verts [(x,y,z)], tris [(a,b,c,localtag)], sections)"""
    o_vcu, t_vcu = _member(hk, ti, "vertexConversionUtil")
    o_inv, _ = _member(hk, hk.body(t_vcu) and hk.T.index(hk.body(t_vcu)), "bitScale16Inv")
    inv = struct.unpack_from("<f", b, base + o_vcu + o_inv)[0]
    o_tags, _ = _member(hk, ti, "shapeTagTable")
    o_gs, t_gs = _member(hk, ti, "geometrySections")
    toff, tcnt = struct.unpack_from("<iI", b, base + o_tags)
    tags = [struct.unpack_from("<IH", b, base + o_tags + toff + i * 8) for i in range(tcnt)]   # (key, tag)
    goff, gcnt = struct.unpack_from("<iI", b, base + o_gs)
    gsub = hk.body(t_gs)["subtype"]; gsz = hk.size(gsub)
    o_pr, _ = _member(hk, gsub, "primitives"); o_vb, _ = _member(hk, gsub, "vertexBuffer"); o_so, _ = _member(hk, gsub, "sectionOffset")
    verts, tris = [], []
    ti_ = 0
    for s in range(gcnt):
        ga = base + o_gs + goff + s * gsz
        so = struct.unpack_from("<3i", b, ga + o_so)
        poff, pcnt = struct.unpack_from("<iI", b, ga + o_pr); vo, vcnt = struct.unpack_from("<iI", b, ga + o_vb)
        va = ga + o_vb + vo; v0 = len(verts)
        if so[0] == 0x7FFFFFFF:                       # full-precision section: float x, y, z
            for i in range(vcnt // 12): verts.append(struct.unpack_from("<3f", b, va + i * 12))
        else:                                         # 16-bit: (value + section offset) * bitScale16Inv
            for i in range(vcnt // 6):
                q = struct.unpack_from("<3H", b, va + i * 6)
                verts.append(((q[0] + so[0]) * inv, (q[1] + so[1]) * inv, (q[2] + so[2]) * inv))
        for p in range(pcnt):
            a_, b_, c_, d_ = b[ga + o_pr + poff + p * 4:ga + o_pr + poff + p * 4 + 4]
            key = (s << 9) | (p << 1)                  # shape key; the tag table gives each key range a material
            while ti_ + 1 < len(tags) and tags[ti_ + 1][0] <= key: ti_ += 1
            mat = tags[ti_][1] if tags and tags[ti_][0] <= key else 0xFFFF
            tris.append((v0 + a_, v0 + b_, v0 + c_, mat))
            if c_ != d_: tris.append((v0 + a_, v0 + c_, v0 + d_, mat))
    return verts, tris, gcnt


def _phive_hull_at(hk, b, ti, base):
    """convex hull (box / convex polytope) -> (verts, tris) with local tag 0"""
    ho, ht = _member(hk, ti, "hull")
    arr = []
    for k in range(4):                                # vertices, planes, faces, indices (hkRelArrayView)
        off, cnt = struct.unpack_from("<iI", b, base + ho + k * 8); arr.append((base + ho + k * 8 + off, cnt))
    (va, vn), _, (fa, fn), (ia, inn) = arr
    vstep = 16                                        # hkVector4; Havok 2025 (Switch Sports...) stores hkFloat3 = 12 bytes
    try:
        _, vt = _member(hk, ht, "vertices")
        st = hk.T[vt].get("subtype")
        if st and hk.T[st].get("name") == "hkFloat3": vstep = 12
    except Exception: pass
    verts = [struct.unpack_from("<3f", b, va + i * vstep) for i in range(vn)]
    tris = []
    for f in range(fn):
        first, num = struct.unpack_from("<HB", b, fa + f * 4)
        idx = [b[ia + first + k] for k in range(num)]
        for k in range(1, num - 1): tris.append((idx[0], idx[k], idx[k + 1], 0))
    return verts, tris


def read_phive_mesh(b):
    """-> dict with 'verts' [(x,y,z)], 'tris' [(a,b,c,material)], 'sections', 'materials' [(a,b)] for an
    hknpMeshShape inside a Phive file."""
    hk = HavokTagfile(b, 0x30)
    root = next((it for it in hk.items if it[0] and hk.is_a(it[0], "hknpShape")), None)
    if root is None: raise ValueError("no shape in this file")
    ti, roff, _ = root; base = hk.d0 + roff
    out = {"root_type": hk.T[ti]["name"], "hk": hk, "root": (ti, base)}
    if not hk.is_a(ti, "hknpMeshShape"):
        return out
    verts, tris, gcnt = _phive_mesh_at(hk, b, ti, base)
    # material table (after the tagfile): 16 bytes each, two numbers
    o1, o2, end, tsz, s1, s2 = struct.unpack_from("<6I", b, 0x10)
    mats = [struct.unpack_from("<II", b, o1 + i) for i in range(0, s1, 16)] if o1 + s1 <= len(b) else []
    out.update(verts=verts, tris=tris, sections=gcnt, materials=mats)
    return out


def _qrot(q, v):
    x, y, z, w = q; vx, vy, vz = v
    tx = 2 * (y * vz - z * vy); ty = 2 * (z * vx - x * vz); tz = 2 * (x * vy - y * vx)
    return (vx + w * tx + (y * tz - z * ty), vy + w * ty + (z * tx - x * tz), vz + w * tz + (x * ty - y * tx))


def read_phive_compound(b, want_geometry=True):
    """Phive v2 static compound (.bphsc): -> dict with instances [{pos, rot, scale, shape, hash, mat_base, mat_count}],
    shapes {addr: (type, verts, tris)}, materials [(a, c)]"""
    if b[:6] != b"Phive\0" or b[6] != 2: raise ValueError("not a Phive v2 compound")
    offs = struct.unpack_from("<12I", b, 0x0C)
    hk = HavokTagfile(b, offs[8])
    t, o, c = hk.items[1]
    if not hk.is_a(t, "hknpCompoundShape"): raise ValueError("root is %s, not a compound shape" % hk.T[t]["name"])
    base = hk.d0 + o
    io, _ = _member(hk, t, "instances")
    ia = base + io
    eoff, _ = struct.unpack_from("<qI", b, ia)
    nalloc = hk.read(t, base, big=1)["instances"].get("numAllocated", 0)
    e0 = ia + eoff
    # which item each address belongs to (to learn a shape's type)
    starts = sorted((hk.d0 + off, ti) for ti, off, cnt in hk.items if ti)
    import bisect
    keys = [s_[0] for s_ in starts]
    def type_at(addr):
        k = bisect.bisect_right(keys, addr) - 1
        return starts[k][1] if k >= 0 and starts[k][0] == addr else None
    # per-instance tables before the tagfile
    s1 = {}
    for k in range((offs[2] - offs[1]) // 32):
        i0, i1, h64, h32 = struct.unpack_from("<IIQI", b, offs[1] + k * 32)
        if i0 == i1 and i0 < nalloc and i0 not in s1: s1[i0] = h64
    s4 = {}
    for k in range((offs[5] - offs[4]) // 20):
        idx, fl, x1, x2, mb, _z, mc = struct.unpack_from("<IIHHHHI", b, offs[4] + k * 20)
        s4[idx] = (mb, mc)
    mats = [struct.unpack_from("<II", b, offs[5] + k * 16) for k in range((offs[6] - offs[5]) // 16)]
    inst, shapes = [], {}
    for i in range(nalloc):
        q = e0 + i * 64
        rot = struct.unpack_from("<4f", b, q); pos = struct.unpack_from("<3f", b, q + 16)
        flags = b[q + 28]; scale = struct.unpack_from("<3f", b, q + 32)
        sp = struct.unpack_from("<q", b, q + 48)[0]
        sa = q + 48 + sp if sp else 0
        st = type_at(sa) if sa else None
        if sa and want_geometry and sa not in shapes and st is not None:
            if hk.is_a(st, "hknpMeshShape"): v, tr, _ = _phive_mesh_at(hk, b, st, sa)
            elif any(m[0] == "hull" for m in hk.members(st)): v, tr = _phive_hull_at(hk, b, st, sa)
            else: v, tr = [], []
            shapes[sa] = (hk.T[st]["name"], v, tr)
        mb, mc = s4.get(i, (0, 0))
        inst.append({"pos": pos, "rot": rot, "scale": scale, "flags": flags, "shape": sa,
                     "shape_type": hk.T[st]["name"] if st is not None else None,
                     "hash": s1.get(i), "mat_base": mb, "mat_count": mc})
    return {"hk": hk, "root": (t, base), "instances": inst, "shapes": shapes, "materials": mats}


# ---------- Sound attenuation (Sound/Attenuation/*.baatarc): AATN presets + AROC / AUDC / AACL curves ----------
_ATN_NOTE = ("Sound distance attenuation: how a sound changes as it gets further from the listener. A .baatn preset "
             "names the curve files it uses (looked up by name in the same archive, any of .baroc/.baudc/.baacl); "
             "the curves hold distances in metres. Field names marked '?' are educated guesses.")


def read_aatn(b):
    if b[:4] != b"AATN": raise ValueError("not AATN")
    ver, so = struct.unpack_from("<HI", b, 6)
    w = struct.unpack_from("<14I", b, 12) if so >= 12 + 56 else ()
    def S(o): return _cstr(b, so + o) if so + o < len(b) else "?"
    out = {"_binary_format": "AATN sound attenuation preset", "_note": _ATN_NOTE, "version": ver}
    if len(w) == 14:
        slots = []
        for k in range(5):
            kind, nm = w[1 + k * 2], S(w[2 + k * 2])
            slots.append({"slot": k, "kind": kind, "curve": nm or None})
        out["curves"] = slots
        out["low_pass_curve?"] = S(w[11]) or None
        out["low_pass_enabled?"] = bool(w[12])
        out["raw_words"] = list(w)
        out["_slots_note"] = ("Five curve slots (kind, curve name). Slots 0-1 name the Z...Fx / Z... curves, slot 3 the main "
                              "curve (Far, FarS, MediumLog...). kind 2 seems to mean 'use the named curve'; the 6dB presets "
                              "have kind 0 in slot 0, which probably selects a built-in 6 dB-per-doubling falloff instead.")
    return out


def read_aroc(b):
    if b[:4] != b"AROC": raise ValueError("not AROC")
    ver, kind = struct.unpack_from("<HI", b, 6)
    mn, mx, p, a = struct.unpack_from("<4f", b, 12); flag = struct.unpack_from("<I", b, 28)[0]; c = struct.unpack_from("<f", b, 32)[0]
    out = {"_binary_format": "AROC roll-off curve", "_note": _ATN_NOTE, "version": ver, "kind": kind,
           "min_distance": round(mn, 4), "max_distance": round(mx, 4), "rolloff_factor?": round(p, 4),
           "value_a?": round(a, 4), "flag?": flag, "distance_c?": round(c, 4)}
    if kind == 1:
        out["_curve_note"] = ("kind 1: full volume up to min_distance, then rolls off towards max_distance; rolloff_factor? "
                              "sets how fast. distance_c? (0 for most) may be where a final fade to silence starts.")
    elif kind == 2:
        out["_curve_note"] = ("kind 2 (the Z... curves): between min_distance and max_distance; the third number (0.25-0.6) "
                              "looks like the level left at max_distance.")
    return out


def read_audc(b):
    if b[:4] != b"AUDC": raise ValueError("not AUDC")
    ver, x = struct.unpack_from("<HI", b, 6)
    f = struct.unpack_from("<6f", b, 12)
    return {"_binary_format": "AUDC distance curve (the ...Log presets)", "_note": _ATN_NOTE, "version": ver,
            "start_level?": round(f[0], 4), "end_level?": round(f[1], 4), "min_distance": round(f[2], 4),
            "distance_step?": round(f[3], 4), "ratio_per_step?": round(f[4], 4), "value_f?": round(f[5], 4),
            "_curve_note": "Probably a logarithmic falloff: past min_distance the level is multiplied by ratio_per_step? "
                           "(0.5 = -6 dB) every distance_step? metres."}


def read_aacl(b):
    if b[:4] != b"AACL": raise ValueError("not AACL")
    ver = struct.unpack_from("<H", b, 6)[0]
    f = struct.unpack_from("<3f", b, 8)
    return {"_binary_format": "AACL distance low-pass / cut-off curve?", "_note": _ATN_NOTE, "version": ver,
            "distance_a?": round(f[0], 4), "distance_b?": round(f[1], 4), "value_c?": round(f[2], 4),
            "_curve_note": "Same names as the main curves (Near, Medium, MedNear, Far, VeryFar); distance_a? grows with "
                           "the preset's range (50 / 90 / 120 / 250 / 650), so probably where muffling with distance ends."}


def _phhb_summary(o):
    """Phive helper bones: say in words which bone drives which"""
    o["_note"] = ("Helper bones: extra bones (Sub_..., cloth roots...) that the game moves automatically from the "
                  "rotation of other bones, so joints keep their shape (shoulders, chest, hips). driver_bone_list = "
                  "the bones that are measured (bone_id into bone_list, measured relative to base_bone_id); "
                  "connection_curve_list = curves turning a driver's angle (key x, in radians) into an amount (key y; "
                  "the other two key numbers look like in/out slopes); driven_bone_list + pose_driven_list = the bones "
                  "that are moved, and which curve output feeds each of their translate X/Y/Z (..._id) or a fixed "
                  "value. Three unnamed value/curve pairs work the same way and are probably rotate X/Y/Z (shown as 'rotate X?'). 'summary' puts this in words. A few setting names are still unknown (0x........). "
                  + o.get("_note", ""))
    d = o.get("param_root", {}).get("helper_bone_data", {})
    def lst(name): v = d.get(name, {}); return list(v.values()) if isinstance(v, dict) else []
    bones = [b.get("name", "?") for b in lst("bone_list")]
    def bn(i): return bones[i] if isinstance(i, int) and 0 <= i < len(bones) else "#%s" % i
    drivers = lst("driver_bone_list"); curves = lst("connection_curve_list"); outs = lst("output_list")
    driven = lst("driven_bone_list"); poses = lst("pose_driven_list")
    def curve_text(out_i):
        oi = outs[out_i] if isinstance(out_i, int) and 0 <= out_i < len(outs) else None
        ci = oi.get("0x8e7f3ca5", out_i) if isinstance(oi, dict) else out_i
        c = curves[ci] if isinstance(ci, int) and 0 <= ci < len(curves) else None
        if not isinstance(c, dict): return "curve %s" % ci
        dv = drivers[c.get("driver_bone_id", -1)] if 0 <= c.get("driver_bone_id", -1) < len(drivers) else {}
        return "curve %d of %s's rotation" % (ci, bn(dv.get("bone_id")))
    lines = []
    for i, db in enumerate(driven):
        pose = poses[db.get("translate_driven_id", i)] if 0 <= db.get("translate_driven_id", i) < len(poses) else {}
        parts = []
        for ax in "XYZ":
            if ("translate%s_id" % ax) in pose: parts.append("%s = %s" % (ax, curve_text(pose["translate%s_id" % ax])))
            elif ("translate%s" % ax) in pose and pose["translate%s" % ax]: parts.append("%s = %g" % (ax, pose["translate%s" % ax]))
        # unnamed pairs that behave the same way (value or curve id); probably rotate X / Y / Z
        for ax, fv, iv in (("X", "0x2eb532ce", "0xab0b6d26"), ("Y", "0x12f7552a", "0xb78ec8ba"), ("Z", "0xe8f86849", "0x0748e816")):
            if iv in pose: parts.append("rotate %s? = %s" % (ax, curve_text(pose[iv])))
            elif pose.get(fv): parts.append("rotate %s? = %g" % (ax, pose[fv]))
        lines.append("%s (relative to %s): moved by %s" % (bn(db.get("bone_id")), bn(pose.get("base_bone_id")),
                                                          ", ".join(parts) if parts else "nothing (stays at its base position)"))
    if lines:
        o["summary"] = lines
        o["drivers"] = ["%s (measured against %s)" % (bn(x.get("bone_id")), bn(x.get("base_bone_id"))) for x in drivers]


def _gmsconf_summary(o):
    """gmsconf: which rendering features each named setup switches on"""
    r = o.get("param_root", {})
    names = [v for k, v in sorted(r.get("name_array", {}).items()) if v]
    out = []
    for nm in names:
        c = r.get(nm)
        if not isinstance(c, dict): continue
        on = [k[7:] if k.startswith("create_") else k for k, v in c.items()
              if v is True and (k.startswith("create_") or k.startswith("use_") or k in ("z_pre_pass", "g_buffer", "forward_soft_edge", "cubemap_array"))]
        out.append("%s: %s" % (nm, ", ".join(on) or "(none of the named features)"))
    if out: o["setups"] = out


def _volm_summary(o):
    """aglvolm: the volumetric light units in plain words"""
    vm = o.get("param_root", {}).get("volume_mask", {})
    out = []
    c = vm.get("common", {})
    if c: out.append("common: %s, ray marching %s, shadow map reduced %s levels" % ("on" if c.get("enable") else "off",
                     "on" if c.get("use_raymarch") else "off", c.get("shadowmap_reduce_level")))
    for k, u in vm.items():
        if not k.startswith("unit_") or not isinstance(u, dict): continue
        col = u.get("render_mask_color")
        out.append("%s: %s, %s to %s away, %s layers (reduced %s levels), colour %s, shadow range %s to %s, blur %s" % (
            k, "on" if u.get("enable") else "off", u.get("range_near"), u.get("range_far"), u.get("layer_number"),
            u.get("layer_reduce_level"), col.get("hex") if isinstance(col, dict) else col,
            u.get("shadowmap_amp_low"), u.get("shadowmap_amp_high"), u.get("gaussian_kernel")))
    o["_note"] = ("Volume mask: volumetric light - the visible shafts of sunlight (god rays) where the air is lit or in "
                  "shadow. Each unit covers a distance range from the camera, split into layers, tinted with its "
                  "colour; the sun's shadow map decides which parts are lit. A level can have several files and "
                  "switch between them (ForestLevel: maskon / maskoff). " + o.get("_note", ""))
    if out: o["summary"] = out


def _gsdw_summary(o):
    """gsdw: shadows and ambient occlusion in plain words"""
    r = o.get("param_root", {})
    out = []
    for k, v in r.items():
        if not isinstance(v, dict): continue
        if k.startswith("depth_shadow_parameter"):
            out.append("Sun (depth) shadow cast by %s (%s)" % (v.get("light_name", "?"), v.get("light_type", "?")))
        elif k.startswith("projection_shadow"):
            nm = v.get("proj_name")
            out.append("Projected shadow texture: %s" % (nm if nm else "none") + (", density %s" % v.get("density") if nm else ""))
        elif k == "ssao_parameter":
            out.append("Ambient occlusion (SSAO): %s, type %s, radius %s, up to %s away, %s sample pairs" % (
                "on" if v.get("is_enable") else "off", v.get("ssao_type"), v.get("radius"), v.get("ao_far"), v.get("sample_pair_num")))
    o["_note"] = ("Shadow settings (gsys): which directional light casts the main shadow, an optional texture projected "
                  "as a shadow (e.g. cloud shadows; 'bias_*' move, rotate and scale it, '_drc' = the Wii U GamePad "
                  "version, an old leftover), and screen-space ambient occlusion - the soft darkening in corners. "
                  "The 'alchemy_' settings are for the Alchemy AO method. " + o.get("_note", ""))
    if out: o["summary"] = out


def _mf_summary(o):
    """aglmf: the filter steps that are switched on, in plain words"""
    root = o.get("param_root", {})
    steps = []
    WHAT = {"reduce": "shrink", "expand": "enlarge", "blur": "blur", "color_correction": "colour correction",
            "change_format": "change format", "color_drift": "colour shift", "trimming": "trim", "hdr_compose": "HDR compose"}
    for kind, slots in root.items():
        if not isinstance(slots, dict) or kind == "mf_root_param": continue
        for slot, sv in slots.items():
            pa = sv.get("param_array", {}) if isinstance(sv, dict) else {}
            if not pa.get("enable"): continue
            vals = ", ".join("%s %s" % (k, v) for k, v in pa.items() if k not in ("enable", "active", "save_index"))
            line = "%s (%s)%s: buffer %s%s" % (slot, WHAT.get(kind, kind), "" if pa.get("active") else " [not active]",
                                               pa.get("save_index"), (", " + vals) if vals else "")
            cc = sv.get("color_correction", {}).get("color_correction") if kind == "color_correction" else None
            if isinstance(cc, dict):
                used = []
                for i in range(12):
                    di = cc.get("data_index%d" % i)
                    if isinstance(di, int) and di >= 0:
                        d = sv["color_correction"].get("data%d" % i, {})
                        pi = d.get("param_index")
                        used.append("type %s -> param%s %s" % (d.get("type"), pi, cc.get("param%s" % pi)))
                if used: line += "; colour correction entries: " + "; ".join(used)
            steps.append(line)
    o["_note"] = ("Multi filter: a small chain of full-screen filters the UI uses to make a blurred copy of the picture "
                  "behind a menu or text (shrink the image, blur it, enlarge it again, adjust its colours). Each kind of "
                  "step has 4 slots; 'enable' switches a slot on, 'buffer' (save_index) is probably which work buffer it "
                  "writes. Values such as blur_type and reduce_scale are given as stored; what each number means "
                  "exactly isn't known. " + o.get("_note", ""))
    if steps: o["filter_steps"] = steps


def _ofx_summary(o):
    """aglofx: the lens flare presets in plain words"""
    root = o.get("param_root", {})
    texs = []
    tl = root.get("TextureList", {})
    for i in range(100):
        ti = tl.get("TextureInfo%d" % i)
        if ti is None: break
        texs.append(ti.get("RefTexName%d" % i, "?"))
    flares = {}
    for kind in ("OfxLensFlare", "OfxLensFlareDynamic", "OfxLensFlareImpl", "OfxLargeLensFlareImpl"):
        for _, p in sorted((root.get(kind) or {}).items()):
            if not isinstance(p, dict): continue
            head = "%s (%s)" % (p.get("PresetName") or p.get("name") or kind, kind)
            info = ["'enable' %s" % ("yes" if p.get("enable") else "no")]
            if "Position" in p: info.append("position %s" % [round(x, 1) for x in p["Position"]])
            if "Radius" in p: info.append("radius %s" % p["Radius"])
            if "CoreRadius" in p: info.append("core radius %s" % p["CoreRadius"])
            els = []
            for i in range(100):
                if "IsEnableDraw%d" % i not in p: break
                if not p["IsEnableDraw%d" % i]: continue
                ti = p.get("TextureIdx%d" % i, -1)
                col = p.get("Color%d" % i)
                sz = p.get("Size%d" % i, [])
                els.append("%d: %s, at %s along the line, size %s, %s, intensity %s" % (
                    i, texs[ti] if isinstance(ti, int) and 0 <= ti < len(texs) else "texture %s" % ti,
                    p.get("Position%d" % i), " x ".join("%g" % x for x in sz),
                    col.get("hex") if isinstance(col, dict) else col, p.get("Intensity%d" % i)))
            flares[head] = {"settings": ", ".join(info), "elements_drawn": els}
    o["_note"] = ("Lens flare: the sprites drawn across the screen when the camera looks towards the sun (or another "
                  "bright light). TextureList names the flare textures (they are in the same .bofx); each preset places "
                  "up to 20 elements along the line from the light through the screen centre ('at ... along the line': "
                  "0 seems to be at the light itself, larger values further across the screen), each with a texture, "
                  "size, colour and intensity. What the preset's own 'enable' switch does isn't known: the presets "
                  "in use have it off too. " + o.get("_note", ""))
    if texs: o["flare_textures"] = texs
    if flares: o["lens_flares"] = flares


def _cube_summary(o):
    """aglcube: list each cubemap capture point in plain words"""
    o["_note"] = ("Reflection cubemap settings: the points in the level where the game captures its surroundings into a "
                  "cube texture, used for reflections and image-based lighting on nearby surfaces. Each unit has a name "
                  "('Sequential' - probably re-captured a little each frame - or FixPoint00/01...), a position, and on/off. 'near'/'far' are the capture's clip "
                  "distances. A level's default.baglcube is used normally; other files (fixpoint00...) switch "
                  "which captures are on for particular areas or scenes. Two numbers per unit are still unnamed. " + o.get("_note", ""))
    units = []
    root = o.get("param_root", {})
    for k in sorted(root):
        if not k.startswith("cubemap_unit_"): continue
        u = root[k].get("cubemap_unit", {})
        units.append("%s: %s%s at %s, near %s, far %s" % (k[-1], u.get("name") or "(unused)", "" if u.get("enable") else " (off)",
                     [round(x, 1) for x in u.get("position", [])], u.get("near"), u.get("far")))
    if units: o["capture_points"] = units



def _read_phive_water(b, offs):
    """Phive static compound with a water height grid instead of a Havok part (Mario Kart World *_Water.bphsc)."""
    nm = b[0xCF30:b.find(b"\0", 0xCF30)].decode("ascii", "replace") if 32 < b[0xCF30] < 127 else ""
    scale, hmax, cell, x0, x1, z0 = struct.unpack_from("<6f", b, 0xCFC0)
    w, h = struct.unpack_from("<II", b, 0xCFD8)
    out = {"_binary_format": "Phive water grid (height field, no Havok part)",
           "_note": "Water collision for one square of the course: a %d x %d grid of cells, each with a water height "
                    "(0 - max_height) and a flag (0x80 = no water here). The picture marks '~' where there is water. "
                    "The small list at the end holds (x, z, 1) entries - probably the tiles this grid touches." % (w, h),
           "name": nm, "world_scale": scale, "max_height": hmax, "cell_size": cell,
           "grid": "%d x %d" % (w, h), "values": [x0, x1, z0]}
    o = offs[8]
    if w * h and w <= 1024 and h <= 1024 and o + w * h * 8 <= len(b):
        heights, rows, water = collections.Counter(), [], 0
        for r in range(h):
            row = ""
            for c in range(w):
                hv, a, bb, fl = struct.unpack_from("<4H", b, o + (r * w + c) * 8)
                if fl & 0x80:
                    row += "."
                else:
                    row += "~"
                    water += 1
                    heights[round(hv / 65535.0 * hmax, 2)] += 1
            rows.append(row)
        out["water_cells"] = water
        out["water_heights"] = {("%g" % k): v for k, v in sorted(heights.items())}
        out["picture"] = rows
    t = offs[10]
    if t + 4 <= len(b):
        n = struct.unpack_from("<I", b, t)[0]
        out["tiles"] = ["%d, %d, %d" % struct.unpack_from("<3I", b, t + 16 * (i + 1)) for i in range(min(n, 256))
                        if t + 16 * (i + 1) + 12 <= len(b)]
    return out

def read_bphsc(b):
    offs = struct.unpack_from("<12I", b, 0x0C)
    if offs[8] >= len(b):
        nm = b[0xCF30:b.find(b"\0", 0xCF30)].decode("ascii", "replace") if len(b) > 0xCF40 and 32 < b[0xCF30] < 127 else ""
        return {"_binary_format": "Phive static compound (empty - no collision)",
                "_note": "A static-collision file with no collision in it: the Havok part is missing, so this level or "
                         "scenario adds no fixed collision of its own (Mario Kart World has many of these for missions "
                         "and scenario variants).",
                "name": nm, "instances": 0}
    if b[offs[8] + 4:offs[8] + 8] != b"TAG0" and len(b) >= 0xD010:
        return _read_phive_water(b, offs)
    m = read_phive_compound(b)
    hk = m["hk"]; inst = m["instances"]; shapes = m["shapes"]; mats = m["materials"]
    out = {"_binary_format": "Phive static compound (Havok level collision)",
           "_note": "All the fixed collision of one level or scenario: every static object's collision mesh placed at "
                    "its position in the level. Click the file and press \"3D view\" to see the whole level's "
                    "collision; the colours are collision materials. 'hash' is the placement ID that links an "
                    "instance to an actor in the level's placement data."}
    out["havok_sdk"] = hk.sdk
    root = hk.read(*m["root"], big=1)
    ab = root.get("aabb") if isinstance(root, dict) else None
    if isinstance(ab, dict) and "min" in ab:
        out["bounds_min"] = [round(x, 3) for x in ab["min"][:3]]
        out["bounds_max"] = [round(x, 3) for x in ab["max"][:3]]
    elif isinstance(ab, dict) and isinstance(ab.get("minMax"), dict):     # Havok 2025: first = min, sec = max
        mm = ab["minMax"]
        out["bounds_min"] = [round(mm.get("first" + c, 0.0), 3) for c in "XYZ"]
        out["bounds_max"] = [round(mm.get("sec" + c, 0.0), 3) for c in "XYZ"]
    out["instances"] = len(inst)
    per_type = {}
    for x in inst: per_type[x["shape_type"]] = per_type.get(x["shape_type"], 0) + 1
    out["instances_by_shape_type"] = per_type
    out["unique_shapes"] = len(shapes)
    tv = tt = 0
    for x in inst:
        sh = shapes.get(x["shape"])
        if sh: tv += len(sh[1]); tt += len(sh[2])
    out["placed_vertices"] = tv; out["placed_triangles"] = tt
    # materials: count triangles per material pair
    usage = {}
    for x in inst:
        sh = shapes.get(x["shape"])
        if not sh: continue
        for tri in sh[2]:
            g = x["mat_base"] + tri[3] if tri[3] != 0xFFFF and tri[3] < max(x["mat_count"], 1) else None
            key = mats[g] if g is not None and g < len(mats) else None
            usage[key] = usage.get(key, 0) + 1
    out["materials_used"] = ["material %d / %d - %d triangles" % (k[0], k[1], n) if k else "no material - %d triangles" % n
                             for k, n in sorted(usage.items(), key=lambda kv: -kv[1])]
    out["material_table_entries"] = len(mats)
    lst = []
    for i, x in enumerate(inst):
        sh = shapes.get(x["shape"])
        e = {"index": i, "hash": "%016x" % x["hash"] if x["hash"] is not None else None,
             "shape": "%s @%x" % (x["shape_type"], x["shape"]) if x["shape"] else None,
             "position": [round(v, 3) for v in x["pos"]]}
        if any(abs(v) > 1e-6 for v in x["rot"][:3]): e["rotation_quat"] = [round(v, 5) for v in x["rot"]]
        if any(abs(v - 1) > 1e-5 for v in x["scale"]): e["scale"] = [round(v, 4) for v in x["scale"]]
        if sh: e["triangles"] = len(sh[2])
        e["materials"] = ["%d/%d" % mats[x["mat_base"] + k] for k in range(x["mat_count"]) if x["mat_base"] + k < len(mats)]
        lst.append(e)
    out["instance_list"] = lst
    return out


def _hk_clean(v):
    """drop Havok bookkeeping (reference counts, empty optional data) from a _hk_value tree"""
    if isinstance(v, dict):
        out = {}
        for k, x in v.items():
            if k in ("sizeAndFlags", "refCount", "m_capacityAndFlags"): continue
            x = _hk_clean(x)
            if x in ([], {}, None): continue
            out[k] = x
        return out
    if isinstance(v, list): return [_hk_clean(x) for x in v]
    return v


def read_bphcl(b):
    if b[:6] != b"Phive\0":
        raise ValueError("not a Phive file")
    hk = HavokTagfile(b, 0x30)
    t, o, c = hk.items[1]
    root = _hk_value(hk, t, hk.d0 + o)
    variants = root if isinstance(root, list) else [root]
    find = lambda cls: next((x["variant"] for x in variants if isinstance(x, dict) and x.get("className") == cls), None)
    cont, anim = find("hclClothContainer"), find("hkaAnimationContainer")
    out = {"_binary_format": "Phive cloth (Havok Cloth %s)" % hk.sdk.replace("20", "20", 1),
           "_note": "How a model's soft parts move: cloth, hair and fur that swing with the character (e.g. DK's tie, "
                    "his hair tufts). 'cloths' summarises each simulated piece - its bones, particles (pinned ones "
                    "follow the animation, the rest are simulated), the constraints holding the particles together "
                    "and how stiff they are - and 'collision_shapes' the capsules/spheres it can't pass through "
                    "(around the body, one set per Bananza form). Distances are in the model's units. "
                    "'settings' are the game's own cloth parameters (AAMP, names partly unknown); 'full_data' is everything."}
    def num(x): return round(x, 4) if isinstance(x, float) else x
    shapes = []
    for col in (cont or {}).get("collidables", []) or []:
        if not isinstance(col, dict): continue
        sh = col.get("shape") or {}; st = sh.get("_type", "") if isinstance(sh, dict) else ""
        pos = col.get("transform", [0] * 16)[12:15] if isinstance(col.get("transform"), list) else None
        if st == "hclCapsuleShape":
            a0, a1 = sh.get("start", [0, 0, 0]), sh.get("end", [0, 0, 0])
            ln = sum((a1[i] - a0[i]) ** 2 for i in range(3)) ** 0.5
            desc = "capsule, radius %s, length %s" % (num(sh.get("radius")), num(ln))
        elif st == "hclSphereShape":
            sp = sh.get("sphere", {}); sp = sp.get("pos") if isinstance(sp, dict) else sp
            desc = "sphere, radius %s" % (num(sp[3]) if isinstance(sp, list) and len(sp) > 3 else "?")
        elif st == "hclPlaneShape": desc = "plane"
        else: desc = st or "shape"
        shapes.append("%s: %s%s%s" % (col.get("name"), desc, ", at %s" % [num(x) for x in pos] if pos else "", "" if col.get("enabled", True) else " (off)"))
    cloths = []
    skel = {}
    for sk in (anim or {}).get("skeletons", []) or []:
        if not isinstance(sk, dict): continue
        bones = [bn.get("name") if isinstance(bn, dict) else bn for bn in sk.get("bones", [])]
        par = sk.get("parentIndices", [])
        chain = ["%s%s" % (bn, " (under %s)" % bones[par[i]] if i < len(par) and isinstance(par[i], int) and 0 <= par[i] < len(bones) else "") for i, bn in enumerate(bones)]
        skel[sk.get("name")] = chain
    for cd in (cont or {}).get("clothDatas", []) or []:
        if not isinstance(cd, dict): continue
        c_ = {"name": cd.get("name")}
        for tsd in cd.get("transformSetDefinitions", []) or []:
            if isinstance(tsd, dict) and tsd.get("name") in skel: c_["bones"] = skel[tsd["name"]]
        sims = []
        for sd in cd.get("simClothDatas", []) or []:
            if not isinstance(sd, dict): continue
            pd = [p for p in sd.get("particleDatas", []) if isinstance(p, dict)]
            fixed = [x for x in sd.get("fixedParticles", []) if isinstance(x, int)]
            info = sd.get("simulationInfo") or {}
            sim = {"name": sd.get("name"), "particles": len(pd), "pinned_particles": fixed,
                   "total_mass": num(sd.get("totalMass")), "gravity": [num(x) for x in (info.get("gravity") or [])[:3]],
                   "damping_per_second": num(info.get("globalDampingPerSecond")), "max_particle_radius": num(sd.get("maxParticleRadius"))}
            if pd:
                rad = [p.get("radius", 0) for p in pd]; fr = [p.get("friction", 0) for p in pd]
                sim["particle_radius"] = "%s - %s" % (num(min(rad)), num(max(rad))); sim["friction"] = "%s - %s" % (num(min(fr)), num(max(fr)))
            poses = [ps for ps in sd.get("simClothPoses", []) if isinstance(ps, dict)]
            if poses: sim["rest_positions (%s)" % poses[0].get("name")] = [[num(x) for x in v[:3]] for v in poses[0].get("positions", []) if isinstance(v, list)]
            cons = []
            for cs in sd.get("staticConstraintSets", []) or []:
                if not isinstance(cs, dict): continue
                kind = cs.get("_type", "constraints").replace("hcl", "").replace("ConstraintSet", "").replace("Constraint", "")
                arrs = [(k, v) for k, v in cs.items() if isinstance(v, list) and v and isinstance(v[0], dict)]
                n = sum(len([x for x in v if isinstance(x, dict)]) for k, v in arrs)
                stiff = [x.get("stiffness", x.get("bendStiffness")) for k, v in arrs for x in v if isinstance(x, dict) and ("stiffness" in x or "bendStiffness" in x)]
                stiff = [x for x in stiff if isinstance(x, float)]
                txt = "%s \"%s\": %d %s" % (kind, cs.get("name", ""), n, "entries" if n != 1 else "entry")
                if stiff: txt += ", stiffness %s" % (num(stiff[0]) if min(stiff) == max(stiff) else "%s - %s" % (num(min(stiff)), num(max(stiff))))
                cons.append(txt)
            sim["constraints"] = cons
            sim["landscape_collision"] = bool(sd.get("landscapeCollisionEnabled"))
            sims.append(sim)
        c_["simulations"] = sims
        c_["operators"] = ["%s (%s)" % (op.get("name"), op.get("_type", "").replace("hcl", "")) for op in cd.get("operators", []) if isinstance(op, dict)]
        c_["states"] = [st.get("name") for st in cd.get("clothStateDatas", []) if isinstance(st, dict)]
        cloths.append(c_)
    out["cloths"] = cloths
    if shapes: out["collision_shapes"] = shapes
    # the game's own parameters: an AAMP after the Havok data
    k = b.find(b"AAMP", 0x30)
    if k > 0:
        try:
            a_ = read_aamp(b[k:])
            out["settings"] = {kk: vv for kk, vv in a_.items() if not kk.startswith("_")}
        except Exception as e:
            out["settings"] = "(couldn't be read: %s)" % e
    out["havok_sdk"] = hk.sdk
    out["full_data"] = _hk_clean(variants)
    return out

def read_bphsh(b):
    if b[:6] != b"Phive\0":
        raise ValueError("not a Phive file")
    out = {"_binary_format": "Phive shape (Havok collision)",
           "_note": "The collision shape the game uses for physics (what Donkey Kong and objects bump into and "
                    "stand on), not the visible model. Click the file and press \"3D view\" to see it; the "
                    "colours are its collision materials."}
    try:
        m = read_phive_mesh(b)
    except ValueError:                               # not a shape: list what's inside instead
        hk = HavokTagfile(b, 0x30)
        out["_binary_format"] = "Phive (Havok data)"
        out["_note"] = "Havok physics data inside a Phive container."
        out["havok_sdk"] = hk.sdk
        out["havok_objects"] = ["%s x%d" % (hk.T[t]["name"], c) for t, off, c in hk.items if t]
        if hk.items and len(hk.items) > 1 and hk.items[1][0]:
            t, off, c = hk.items[1]
            out["root_type"] = hk.T[t]["name"]
            out["root"] = hk.read(t, hk.d0 + off, big=16)
        return out
    hk = m["hk"]
    out["havok_sdk"] = hk.sdk
    out["shape_type"] = m["root_type"]
    shape = hk.read(*m["root"], big=8)
    for k in ("type", "dispatchType", "flags", "numShapeKeyBits", "convexRadius", "userData"):
        if k in shape: out[k] = shape[k]
    if "verts" in m:
        V = m["verts"]; Tr = m["tris"]
        if V:
            lo = [round(min(v[k] for v in V), 3) for k in range(3)]; hi = [round(max(v[k] for v in V), 3) for k in range(3)]
            out["bounds_min"] = lo; out["bounds_max"] = hi
            out["size"] = [round(hi[k] - lo[k], 3) for k in range(3)]
        out["geometry_sections"] = m["sections"]
        out["vertices"] = len(V); out["triangles"] = len(Tr)
        per = {}
        for t in Tr: per[t[3]] = per.get(t[3], 0) + 1
        out["materials"] = ["%d: material %d / %d - %d triangles" % (i, a, c, per.get(i, 0)) for i, (a, c) in enumerate(m["materials"])]
        if per.get(0xFFFF): out["triangles_without_material"] = per[0xFFFF]
    else:
        out["shape"] = shape
    out["havok_objects"] = ["%s x%d" % (hk.T[t]["name"], c) for t, off, c in hk.items if t]
    return out


# ---------- BFLAN: Nintendo layout animation ----------
_FLAN_TARGETS = {
    "FLPA": ["move X", "move Y", "move Z", "rotate X", "rotate Y", "rotate Z", "scale X", "scale Y", "width", "height"],
    "FLVI": ["visible"],
    "FLVC": ["top-left R", "top-left G", "top-left B", "top-left A", "top-right R", "top-right G", "top-right B", "top-right A",
             "bottom-left R", "bottom-left G", "bottom-left B", "bottom-left A", "bottom-right R", "bottom-right G",
             "bottom-right B", "bottom-right A", "pane alpha"],
    "FLMC": ["black R", "black G", "black B", "black A", "white R", "white G", "white B", "white A"],
    "FLTS": ["scroll U", "scroll V", "rotate", "scale U", "scale V"],
    "FLTP": ["texture"],
    "FLIM": ["rotate", "scale U", "scale V"],
    "FLAC": ["alpha test value"],
    "FLCT": ["per-character offset", "per-character width"],
    "FLEU": ["value"],
}
_FLAN_TAGS = {"FLPA": "pane position/rotation/scale/size", "FLVI": "pane visibility", "FLVC": "pane vertex colours",
              "FLMC": "material colours", "FLTS": "texture scroll/rotate/scale", "FLTP": "texture swap",
              "FLIM": "indirect texture", "FLAC": "alpha test", "FLCT": "text per-character animation",
              "FLPS": "pane shape parameters (meaning of each channel not known yet)", "FLEU": "custom value (user data)",
              "FLMT": "material", "FLMA": "material colour (extra)"}
_FLAN_CONTENT = {0: "pane", 1: "material", 2: "user data"}


def _cstr(b, o, n=None):
    e = b.find(b"\0", o, o + n if n else len(b))
    return b[o:e if e >= 0 else (o + n if n else len(b))].decode("utf-8", "replace")


def read_bflan(b):
    if b[:4] != b"FLAN":
        raise ValueError("not FLAN")
    hs, ver = struct.unpack_from("<HI", b, 6)
    nsec = struct.unpack_from("<H", b, 0x10)[0]
    out = {"_binary_format": "BFLAN (layout animation)",
           "_note": "Animates parts of a UI screen (the .bflyt layout in the same archive's blyt folder) - select it and click "
                    "\"Layout\" to watch it play. "
                    "Each line under \"curves\": what is animated, then the keys as frame=value. "
                    "Values between keys are smoothly interpolated unless the line says \"(steps)\".",
           "version": "%d.%d.%d" % (ver >> 24, (ver >> 16) & 0xFF, (ver >> 8) & 0xFF)}
    o = hs
    for _ in range(nsec):
        if o + 8 > len(b): break
        tag = b[o:o + 4].decode("latin1"); size = struct.unpack_from("<I", b, o + 4)[0]
        if tag == "pat1":
            order, ngroups, noff, goff = struct.unpack_from("<HHII", b, o + 8)
            start, end, child = struct.unpack_from("<hhB", b, o + 0x18)
            out["name"] = _cstr(b, o + noff)
            out["frames"] = "%d to %d" % (start, end)
            out["groups"] = [_cstr(b, o + goff + i * 0x24, 0x24) for i in range(ngroups)]
            if child: out["child_binding"] = True
        elif tag == "pai1":
            fsize, loop = struct.unpack_from("<HB", b, o + 8)
            ntex, ncont, coff = struct.unpack_from("<HHI", b, o + 0xC)
            out["length_frames"] = fsize
            out["loops"] = bool(loop)
            if ntex:
                tt = o + 0x14
                out["textures"] = [_cstr(b, tt + struct.unpack_from("<I", b, tt + i * 4)[0]) for i in range(ntex)]
            parts = []
            for c in range(ncont):
                co = o + struct.unpack_from("<I", b, o + coff + c * 4)[0]
                name = _cstr(b, co, 0x1C)
                ninfo, ctype = b[co + 0x1C], b[co + 0x1D]
                p = co + 0x20; extra = None
                if ctype == 2:                                   # user data: table offset, name-entry offset
                    toff, eoff = struct.unpack_from("<II", b, p)
                    e = co + eoff
                    extra = _cstr(b, e + struct.unpack_from("<I", b, e)[0]) if e + 4 <= len(b) else None
                    p = co + toff
                curves = []
                for i in range(ninfo):
                    io = co + struct.unpack_from("<I", b, p + i * 4)[0]
                    itag = b[io:io + 4].decode("latin1"); nt = b[io + 4]
                    for t in range(nt):
                        to = io + struct.unpack_from("<I", b, io + 8 + t * 4)[0]
                        idx, tgt, ctyp = b[to], b[to + 1], b[to + 2]
                        nk, koff = struct.unpack_from("<H", b, to + 4)[0], struct.unpack_from("<I", b, to + 8)[0]
                        ko = to + koff; keys = []
                        for k in range(nk):
                            if ctyp == 1:                        # step: frame, u16 value
                                f, v = struct.unpack_from("<fH", b, ko + k * 8); keys.append("%g=%d" % (f, v))
                            else:                                # hermite: frame, value, slope
                                f, v, s = struct.unpack_from("<fff", b, ko + k * 12)
                                keys.append("%g=%g" % (round(f, 3), round(v, 4)))
                        names = _FLAN_TARGETS.get(itag, [])
                        what = names[tgt] if tgt < len(names) else "channel %d" % tgt
                        if itag == "FLTP" and "textures" in out:
                            keys = [k.split("=")[0] + "=" + (out["textures"][int(k.split("=")[1])] if int(k.split("=")[1]) < len(out["textures"]) else k.split("=")[1]) for k in keys]
                        lbl = "%s %s%s" % (itag, what, (" [%d]" % idx) if itag in ("FLTS", "FLTP", "FLIM", "FLMC") or idx else "")
                        if extra and itag == "FLEU": lbl = "FLEU %s" % extra
                        curves.append("%s%s:  %s" % (lbl, "  (steps)" if ctyp == 1 else "", "  ".join(keys)))
                parts.append({"part": name, "kind": _FLAN_CONTENT.get(ctype, "type %d" % ctype), "curves": curves})
            out["animated"] = parts
        o += size
    used = sorted({c.split()[0] for p in out.get("animated", []) for c in p["curves"]})
    if used: out["animation_types"] = ["%s = %s" % (t, _FLAN_TAGS.get(t, "?")) for t in used]
    return out


# ---------- BNTX textures: list them (the GUI's Textures window decodes and shows them) ----------
_BNTX_FMT = {0x02: "R8", 0x09: "RG8", 0x0b: "RGBA8", 0x0c: "BGRA8", 0x15: "RGBA16", 0x1a: "BC1", 0x1b: "BC2", 0x1c: "BC3", 0x1d: "BC4",
             0x1e: "BC5", 0x1f: "BC6H", 0x20: "BC7"}
for _i, (_bw, _bh) in enumerate([(4, 4), (5, 4), (5, 5), (6, 5), (6, 6), (8, 5), (8, 6), (8, 8), (10, 5), (10, 6), (10, 8),
                                 (10, 10), (12, 10), (12, 12)]):
    _BNTX_FMT[0x2d + _i] = "ASTC %dx%d" % (_bw, _bh)


def read_bntx(b):
    if b[:4] != b"BNTX":
        raise ValueError("not BNTX")
    plat = b[0x20:0x24].decode("latin1").strip()
    n, tab = struct.unpack_from("<IQ", b, 0x24)
    out = {"_binary_format": "BNTX (textures)",
           "_note": "Textures (images). Select this file and click \"Textures\" to see them and save them as PNG. "
                    "Each line: name, size, format, mip levels.",
           "platform": {"Ounc": "Switch 2", "NX": "Switch"}.get(plat, plat), "texture_count": n}
    lines = []; fmts = {}
    for i in range(n):
        o = struct.unpack_from("<Q", b, tab + i * 8)[0]
        if b[o:o + 4] != b"BRTI": continue
        mips = struct.unpack_from("<H", b, o + 0x16)[0]
        fmt, _acc, w, h, d, arr = struct.unpack_from("<IIIIII", b, o + 0x1C)
        no = struct.unpack_from("<Q", b, o + 0x60)[0]; l = struct.unpack_from("<H", b, no)[0]
        name = b[no + 2:no + 2 + l].decode("utf-8", "replace")
        f = _BNTX_FMT.get(fmt >> 8, "format 0x%04X" % fmt) + ("F" if fmt == 0x1505 else "") + (" sRGB" if fmt & 0xFF == 6 else "")
        fmts[f] = fmts.get(f, 0) + 1
        lines.append("%s  %dx%d  %s%s%s" % (name, w, h, f, "  mips %d" % mips if mips > 1 else "", "  layers %d" % arr if arr > 1 else ""))
    out["formats"] = ["%s x%d" % kv for kv in sorted(fmts.items(), key=lambda kv: -kv[1])]
    out["textures"] = lines
    return out


# ---------- BFLYT: Nintendo UI layout (screen) ----------
_LYT_PANE_KINDS = {"pan1": "null pane (group)", "pic1": "picture", "txt1": "text", "wnd1": "window (frame)", "prt1": "part (embedded layout)",
                   "bnd1": "bounding box", "ali1": "alignment", "scr1": "scissor (clip)"}


def _lyt_cstr(b, o, n=None):
    e = b.find(b"\0", o, o + n if n else len(b))
    return b[o:e if e >= 0 else (o + n if n else len(b))].decode("utf-8", "replace")


def _lyt_rgba(v):
    return "#%02X%02X%02X%02X" % (v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, v >> 24)


def _lyt_material(b, o):
    name = _lyt_cstr(b, o, 0x1C)
    f = struct.unpack_from("<I", b, o + 0x1C)[0]
    black = b[o + 0x24:o + 0x28]; white = b[o + 0x28:o + 0x2C]
    p = o + 0x2C
    m = {"name": name, "black": "#%02X%02X%02X%02X" % tuple(black), "white": "#%02X%02X%02X%02X" % tuple(white), "textures": [], "flags": f}
    for i in range(f & 3):
        ti, ws, wt = struct.unpack_from("<HBB", b, p); p += 4
        m["textures"].append({"index": ti, "wrap": (ws & 3, wt & 3)})
    m["srt"] = []
    for i in range((f >> 2) & 3):
        m["srt"].append(struct.unpack_from("<5f", b, p)); p += 20
    p += ((f >> 4) & 3) * 16
    p += ((f >> 6) & 7) * 4
    if f >> 9 & 1: p += 8
    if f >> 10 & 1:
        m["blend"] = tuple(b[p:p + 4]); p += 4
    if f >> 12 & 1: p += 4
    if f >> 14 & 1: p += 12
    p += ((f >> 15) & 3) * 20
    if f >> 17 & 1: p += 8
    if f >> 20 & 1:
        m["shader"] = _lyt_cstr(b, p, 0x60); p += 0x74
    if f >> 24 & 1: p += 88
    return m


def _lyt_pane(b, o, kind):
    fl, origin, alpha, mag = b[o + 8:o + 12]
    p = {"kind": kind, "name": _lyt_cstr(b, o + 0x0C, 0x18), "visible": bool(fl & 1), "alpha": alpha,
         "influence_alpha": bool(fl & 2), "origin": origin,
         "pos": struct.unpack_from("<3f", b, o + 0x2C), "rot": struct.unpack_from("<3f", b, o + 0x38),
         "scale": struct.unpack_from("<2f", b, o + 0x44), "size": struct.unpack_from("<2f", b, o + 0x4C), "children": []}
    if kind == "pic1":
        p["vtx"] = struct.unpack_from("<4I", b, o + 0x54)
        p["mat"], ntc = struct.unpack_from("<HB", b, o + 0x64)
        p["uv"] = [struct.unpack_from("<8f", b, o + 0x68 + i * 32) for i in range(ntc)]
    elif kind == "txt1":
        bl, sl, mat, font = struct.unpack_from("<HHHH", b, o + 0x54)
        tpos, align = b[o + 0x5C], b[o + 0x5D]
        toff = struct.unpack_from("<I", b, o + 0x64)[0]
        p["mat"] = mat; p["font"] = font; p["text_pos"] = tpos; p["align"] = align
        p["colors"] = struct.unpack_from("<2I", b, o + 0x68)
        p["font_size"] = struct.unpack_from("<2f", b, o + 0x70)
        p["text"] = b[o + toff:o + toff + sl].decode("utf-16-le", "replace").rstrip("\0") if toff and sl else ""
    elif kind == "wnd1":
        p["mat"] = None
        try:
            nfr = b[o + 0x65]; cmat = struct.unpack_from("<H", b, o + 0x80)[0]
            p["mat"] = cmat; p["frames"] = nfr
        except struct.error: pass
    elif kind == "prt1":
        n = struct.unpack_from("<I", b, o + 0x54)[0]
        p["magnify"] = struct.unpack_from("<2f", b, o + 0x58)
        p["part"] = _lyt_cstr(b, o + 0x60 + n * 0x28)
        p["overrides"] = [_lyt_cstr(b, o + 0x60 + i * 0x28, 0x18) for i in range(n)]
    return p


def parse_bflyt(b):
    if b[:4] != b"FLYT":
        raise ValueError("not FLYT")
    hs, ver = struct.unpack_from("<HI", b, 6); n = struct.unpack_from("<H", b, 0x10)[0]
    L = {"version": ver, "textures": [], "fonts": [], "materials": [], "root": None, "groups": []}
    o = hs; stack = []; last = None; gstack = []; lastg = None
    for _ in range(n):
        if o + 8 > len(b): break
        t = b[o:o + 4].decode("latin1"); sz = struct.unpack_from("<I", b, o + 4)[0]
        if t == "lyt1":
            L["centered"] = bool(b[o + 8]); L["width"], L["height"] = struct.unpack_from("<2f", b, o + 0xC)
            L["name"] = _lyt_cstr(b, o + 0x1C)
        elif t in ("txl1", "fnl1"):
            k = struct.unpack_from("<H", b, o + 8)[0]
            names = [_lyt_cstr(b, o + 0xC + struct.unpack_from("<I", b, o + 0xC + i * 4)[0]) for i in range(k)]
            L["textures" if t == "txl1" else "fonts"] = names
        elif t == "mat1":
            k = struct.unpack_from("<H", b, o + 8)[0]
            L["materials"] = [_lyt_material(b, o + struct.unpack_from("<I", b, o + 0xC + i * 4)[0]) for i in range(k)]
        elif t in _LYT_PANE_KINDS:
            pn = _lyt_pane(b, o, t)
            if stack: stack[-1]["children"].append(pn)
            elif L["root"] is None: L["root"] = pn
            last = pn
        elif t == "pas1":
            if last is not None: stack.append(last)
        elif t == "pae1":
            if stack: stack.pop()
        elif t == "grp1":
            g = {"name": _lyt_cstr(b, o + 8, 0x24), "panes": [], "children": []}
            k = struct.unpack_from("<H", b, o + 0x2C)[0] if sz > 0x2C else 0
            g["panes"] = [_lyt_cstr(b, o + 0x30 + i * 0x18, 0x18) for i in range(k)]
            (gstack[-1]["children"] if gstack else L["groups"]).append(g); lastg = g
        elif t == "grs1":
            if lastg is not None: gstack.append(lastg)
        elif t == "gre1":
            if gstack: gstack.pop()
        o += sz
    return L


def read_bflyt(b):
    L = parse_bflyt(b)
    out = {"_binary_format": "BFLYT (UI screen layout)",
           "_note": "Select this file and click \"Layout\" to see the screen drawn and play its animations. One screen or UI component: a tree of panes (pictures, text, groups, frames, and parts = other "
                    "layouts used as components). Its animations are the .bflan files in the archive's anim folder, "
                    "its pictures are in timg. Pane lines: kind, name, position (x, y), size, then details.",
           "name": L.get("name", ""), "size": "%g x %g" % (L.get("width", 0), L.get("height", 0)),
           "textures": L["textures"], "fonts": L["fonts"]}
    out["materials"] = ["%s: %s  colour %s..%s%s" % (m["name"], ", ".join(L["textures"][t["index"]] if t["index"] < len(L["textures"]) else str(t["index"]) for t in m["textures"]) or "(no texture)",
                                                  m["black"], m["white"], ("  shader " + m["shader"]) if m.get("shader") else "") for m in L["materials"]]
    def pane_line(p, depth):
        k = _LYT_PANE_KINDS.get(p["kind"], p["kind"]).split(" ")[0]
        s = "%s%s %s  (%g, %g)  %gx%g" % ("  " * depth, k, p["name"], round(p["pos"][0], 2), round(p["pos"][1], 2), round(p["size"][0], 2), round(p["size"][1], 2))
        if not p["visible"]: s += "  hidden"
        if p["alpha"] != 255: s += "  alpha %d" % p["alpha"]
        if p["rot"][2]: s += "  rot %g" % round(p["rot"][2], 2)
        if p["scale"] != (1.0, 1.0): s += "  scale %g,%g" % (round(p["scale"][0], 3), round(p["scale"][1], 3))
        if p.get("mat") is not None and p["kind"] in ("pic1", "wnd1") and p["mat"] < len(L["materials"]):
            s += "  material " + L["materials"][p["mat"]]["name"]
        if p["kind"] == "txt1": s += "  text \"%s\"" % p["text"].replace("\n", "\\n")
        if p["kind"] == "prt1": s += "  uses layout " + p["part"]
        lines.append(s)
        for c in p["children"]: pane_line(c, depth + 1)
    lines = []
    if L["root"]: pane_line(L["root"], 0)
    out["panes"] = lines
    def grp(g, d):
        glines.append("%s%s: %s" % ("  " * d, g["name"], ", ".join(g["panes"]) or "-"))
        for c in g["children"]: grp(c, d + 1)
    glines = []
    for g in L["groups"]: grp(g, 0)
    out["groups"] = glines
    return out


# ---------- BFRES (models / animations, version 10) ----------


class R:
    def __init__(self, b):
        self.b = b

    def u8(self, o): return self.b[o]
    def u16(self, o): return struct.unpack_from("<H", self.b, o)[0]
    def s16(self, o): return struct.unpack_from("<h", self.b, o)[0]
    def u32(self, o): return struct.unpack_from("<I", self.b, o)[0]
    def s32(self, o): return struct.unpack_from("<i", self.b, o)[0]
    def u64(self, o): return struct.unpack_from("<Q", self.b, o)[0]
    def f32(self, o): return struct.unpack_from("<f", self.b, o)[0]
    def fs(self, o, n): return list(struct.unpack_from("<%df" % n, self.b, o))

    def str(self, o):
        """string pointed to by the u64 at o (u16 length + bytes)"""
        p = self.u64(o)
        if not p or p + 2 > len(self.b):
            return None
        n = self.u16(p)
        return self.b[p + 2:p + 2 + n].decode("utf-8", "replace")

    def strs(self, o, n):
        p = self.u64(o)
        if not p:
            return []
        out = []
        for i in range(n):
            q = self.u64(p + i * 8)
            out.append(self.b[q + 2:q + 2 + self.u16(q)].decode("utf-8", "replace") if q else None)
        return out

    def dict_keys(self, o):
        """ResDict at the u64 offset at o -> names in order"""
        p = self.u64(o)
        if not p:
            return []
        n = self.s32(p + 4)
        keys = []
        for i in range(1, n + 1):
            e = p + 8 + i * 16
            keys.append(self.str(e + 8))
        return keys


ATTR_LAYOUT = {  # low byte of the format -> (component count, component size in bits, packed kind)
    0x02: (1, 8, None), 0x09: (2, 8, None), 0x0B: (4, 8, None), 0x0A: (1, 16, None), 0x12: (2, 16, None),
    0x15: (4, 16, None), 0x14: (1, 32, None), 0x16: (1, 32, None), 0x17: (2, 32, None), 0x18: (3, 32, None),
    0x19: (4, 32, None), 0x0E: (4, 0, "10_10_10_2"), 0x01: (2, 4, None), 0x03: (1, 16, None), 0x04: (2, 8, None),
    0x07: (2, 16, None), 0x0C: (4, 8, None),
}
ATTR_TYPE = {1: "unorm", 2: "snorm", 3: "uint", 4: "sint", 5: "float", 8: "uint_to_float", 0xA: "sint_to_float"}


def attr_format_name(f):
    lay, typ = f & 0xFF, f >> 8
    n, bits, packed = ATTR_LAYOUT.get(lay, (0, 0, None))
    if packed:
        return "10_10_10_2_" + ATTR_TYPE.get(typ, "?")
    return "%s_%s" % ("_".join([str(bits)] * n) if n else "fmt%02X" % lay, ATTR_TYPE.get(typ, "type%X" % typ))


def decode_attr(b, off, f):
    """one vertex's attribute value -> list of floats/ints"""
    lay, typ = f & 0xFF, f >> 8
    n, bits, packed = ATTR_LAYOUT.get(lay, (0, 0, None))
    if packed:
        v = struct.unpack_from("<I", b, off)[0]
        out = []
        for i in range(3):
            x = (v >> (10 * i)) & 0x3FF
            if typ == 2:
                if x & 0x200:
                    x -= 0x400
                out.append(max(x / 511.0, -1.0))
            else:
                out.append(x / 1023.0)
        out.append((v >> 30) & 3)
        return out
    if not n:
        return []
    if bits == 32:
        fmt = {5: "f", 3: "I", 4: "i", 8: "I", 0xA: "i"}.get(typ, "I")
        return list(struct.unpack_from("<%d%s" % (n, fmt), b, off))
    if bits == 16:
        if typ == 5:
            return list(struct.unpack_from("<%de" % n, b, off))
        raw = struct.unpack_from("<%d%s" % (n, "h" if typ in (2, 4, 0xA) else "H"), b, off)
        if typ == 1: return [x / 65535.0 for x in raw]
        if typ == 2: return [max(x / 32767.0, -1.0) for x in raw]
        return [float(x) if typ in (8, 0xA) else x for x in raw]
    if bits == 8:
        raw = struct.unpack_from("<%d%s" % (n, "b" if typ in (2, 4, 0xA) else "B"), b, off)
        if typ == 1: return [x / 255.0 for x in raw]
        if typ == 2: return [max(x / 127.0, -1.0) for x in raw]
        return [float(x) if typ in (8, 0xA) else x for x in raw]
    return []


class Bfres:
    def __init__(self, b):
        if b[:4] != b"FRES":
            raise ValueError("not a BFRES file")
        r = self.r = R(b)
        self.b = b
        self.version = "%d.%d.%d" % (b[0xA] | (b[0xB] << 8), b[9], b[8])
        self.major = r.u16(0xA)
        self.name = r.str(0x20)
        self.ext_flag = b[0xEE]
        # buffer info (vertex / index data)
        bi = r.u64(0xB0)
        self.buf_base = r.u64(bi + 8) if bi else 0
        self.buf_size = r.u32(bi + 4) if bi else 0
        self.models = [self.model(r.u64(0x28) + i * 0x78) for i in range(r.u16(0xDC))] if r.u64(0x28) else []
        names = r.dict_keys(0x30)
        self.counts = {"skeletal_anims": r.u16(0xE2), "material_anims": r.u16(0xE4), "bone_visibility_anims": r.u16(0xE6),
                       "shape_anims": r.u16(0xE8), "scene_anims": r.u16(0xEA), "external_files": r.u16(0xEC)}
        self.ext_names = r.dict_keys(0xC0)
        self.ext_files = []
        eo = r.u64(0xB8)
        for i, nm in enumerate(self.ext_names):
            if eo:
                o, sz = r.u64(eo + i * 16), r.u32(eo + i * 16 + 8)
                self.ext_files.append((nm, o, sz))
        self.skel_anim_names = r.dict_keys(0x60)
        self.skel_anims_ofs = r.u64(0x58)
        self.mat_anim_names = r.dict_keys(0x70)
        self.vis_anim_names = r.dict_keys(0x80)
        self.shape_anim_names = r.dict_keys(0x90)
        self.scene_anim_names = r.dict_keys(0xA0)

    # ---- models ----
    def model(self, o):
        r = self.r
        m = {"name": r.str(o + 8), "path": r.str(o + 0x10)}
        skl = r.u64(o + 0x18)
        vtx_arr = r.u64(o + 0x20)
        shp_arr, shp_dict = r.u64(o + 0x28), r.u64(o + 0x30)
        mat_arr, mat_dict = r.u64(o + 0x38), r.u64(o + 0x40)
        nvb, nshp, nmat = r.u16(o + 0x68), r.u16(o + 0x6A), r.u16(o + 0x6C)
        m["user_data"] = r.dict_keys(o + 0x58)
        m["skeleton"] = self.skeleton(skl) if skl else None
        m["vertex_buffers"] = [self.vbuf(vtx_arr + i * 0x58) for i in range(nvb)]
        m["materials"] = [self.material(mat_arr + i * 0xB0) for i in range(nmat)] if mat_arr else []
        m["shapes"] = [self.shape(shp_arr + i * 0x60) for i in range(nshp)] if shp_arr else []
        return m

    def skeleton(self, o):
        r = self.r
        flags = r.u32(o + 4)
        bone_arr = r.u64(o + 0x10)
        mtx2bone, invm = r.u64(o + 0x18), r.u64(o + 0x20)
        nb, nsm, nrig = r.u16(o + 0x38), r.u16(o + 0x3A), r.u16(o + 0x3C)
        bones = []
        for i in range(nb):
            b = bone_arr + i * 0x58
            fl = r.u32(b + 0x2C)
            bones.append({"name": r.str(b), "parent": r.s16(b + 0x22), "smooth_index": r.s16(b + 0x24), "rigid_index": r.s16(b + 0x26),
                          "flags": fl, "rotation_mode": "euler_xyz" if fl & 0x1000 else "quaternion", "visible": bool(fl & 1),
                          "scale": r.fs(b + 0x30, 3), "rotation": r.fs(b + 0x3C, 4), "translation": r.fs(b + 0x4C, 3)})
        n = nsm + nrig
        m2b = list(struct.unpack_from("<%dH" % n, self.b, mtx2bone)) if mtx2bone and n else []
        inv = [r.fs(invm + i * 48, 12) for i in range(nsm)] if invm else []
        return {"flags": flags, "bones": bones, "smooth_matrices": nsm, "rigid_matrices": nrig, "matrix_to_bone": m2b, "inverse_matrices": inv}

    def vbuf(self, o):
        r = self.r
        attr_arr = r.u64(o + 8)
        size_ofs, stride_ofs = r.u64(o + 0x30), r.u64(o + 0x38)
        buf_off = r.s32(o + 0x48)
        nattr, nbuf = self.b[o + 0x4C], self.b[o + 0x4D]
        nvert = r.u32(o + 0x50)
        skin = r.u16(o + 0x54) & 0xFF
        align = r.u16(o + 0x56) or 1
        attrs = []
        for i in range(nattr):
            a = attr_arr + i * 16
            fmt = struct.unpack_from(">H", self.b, a + 8)[0]
            attrs.append({"name": r.str(a), "format": fmt, "offset": r.u16(a + 0x0C), "buffer": r.u16(a + 0x0E) & 0xFF})
        bufs = []
        p = self.buf_base + buf_off
        for i in range(nbuf):
            sz = r.u32(size_ofs + i * 16)
            st = r.u32(stride_ofs + i * 16)
            p = (p + align - 1) // align * align
            bufs.append({"offset": p, "size": sz, "stride": st})
            p += sz
        return {"vertices": nvert, "skin_weights_per_vertex": skin, "attributes": attrs, "buffers": bufs}

    def read_attr(self, vb, name):
        """all vertices' values of one attribute (list of lists), or None"""
        for a in vb["attributes"]:
            if a["name"] == name:
                bf = vb["buffers"][a["buffer"]]
                return [decode_attr(self.b, bf["offset"] + i * bf["stride"] + a["offset"], a["format"]) for i in range(vb["vertices"])]
        return None

    def shape(self, o):
        r = self.r
        mesh_arr, skin_idx = r.u64(o + 0x18), r.u64(o + 0x20)
        s = {"name": r.str(o + 8), "flags": r.u32(o + 4)}
        s["material"], s["bone"], s["vertex_buffer"] = r.u16(o + 0x52), r.u16(o + 0x54), r.u16(o + 0x56)
        nskin, s["skin_weights_per_vertex"], nmesh, nkeys = r.u16(o + 0x58), self.b[o + 0x5A], self.b[o + 0x5B], self.b[o + 0x5C]
        s["key_shapes"] = r.dict_keys(o + 0x30)
        s["skin_bones"] = list(struct.unpack_from("<%dH" % nskin, self.b, skin_idx)) if nskin and skin_idx else []
        meshes = []
        for i in range(nmesh):
            m = mesh_arr + i * 0x38
            sub_arr = r.u64(m)
            bsz = r.u64(m + 0x18)
            face = r.u32(m + 0x20)
            prim, ifmt, cnt, first = r.u32(m + 0x24), r.u32(m + 0x28), r.u32(m + 0x2C), r.u32(m + 0x30)
            nsub = r.u16(m + 0x34)
            subs = [(r.u32(sub_arr + k * 8), r.u32(sub_arr + k * 8 + 4)) for k in range(nsub)]
            meshes.append({"primitive": prim, "index_format": ifmt, "indices": cnt, "first_vertex": first,
                           "data": self.buf_base + face, "size": r.u32(bsz) if bsz else 0, "submeshes": subs})
        s["meshes"] = meshes
        return s

    def read_indices(self, mesh):
        n, fmt = mesh["indices"], mesh["index_format"]
        if fmt == 0:
            return list(self.b[mesh["data"]:mesh["data"] + n])
        return list(struct.unpack_from("<%d%s" % (n, "H" if fmt == 1 else "I"), self.b, mesh["data"]))

    def material(self, o):
        r = self.r
        m = {"name": r.str(o + 8)}
        info = r.u64(o + 0x10)
        ntex = self.b[o + 0xA3]
        nsamp = self.b[o + 0xA2]
        m["textures"] = r.strs(o + 0x20, ntex)
        m["samplers"] = r.dict_keys(o + 0x38)
        if info:
            sa = r.u64(info)
            if sa:
                m["shader_archive"] = r.str(sa)
                m["shading_model"] = r.str(sa + 8)
                m["sampler_assign_keys"] = r.dict_keys(sa + 0x38)
            nsa = self.b[info + 0x45]
            m["sampler_assign"] = r.strs(info + 0x18, nsa)
        return m


# ---- poses ----
def _mat_euler(rx, ry, rz):
    import math
    cx, sx, cy, sy, cz, sz = math.cos(rx), math.sin(rx), math.cos(ry), math.sin(ry), math.cos(rz), math.sin(rz)
    # R = Rz * Ry * Rx
    return [[cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz],
            [cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz],
            [-sy, sx * cy, cx * cy]]


def _mat_quat(x, y, z, w):
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]


def local_matrix(scale, rot, trans, euler=True):
    R = _mat_euler(*rot[:3]) if euler else _mat_quat(*rot)
    return [[R[i][0] * scale[0], R[i][1] * scale[1], R[i][2] * scale[2], trans[i]] for i in range(3)]


def mat_mul(a, b):
    """3x4 * 3x4 (affine)"""
    return [[a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + (a[i][3] if j == 3 else 0) for j in range(4)] for i in range(3)]


def world_matrices(skel):
    out = []
    for b in skel["bones"]:
        lm = local_matrix(b["scale"], b["rotation"], b["translation"], b["rotation_mode"] == "euler_xyz")
        out.append(mat_mul(out[b["parent"]], lm) if 0 <= b["parent"] < len(out) else lm)
    return out


def xform(m, p):
    return [m[i][0] * p[0] + m[i][1] * p[1] + m[i][2] * p[2] + m[i][3] for i in range(3)]


def skeletal_anims(B):
    r = B.r
    out = []
    p = B.skel_anims_ofs
    for i, nm in enumerate(B.skel_anim_names):
        o = p + i * 0x50
        fl = r.u32(o + 4)
        nb = r.u16(o + 0x4C)
        ba = r.u64(o + 0x28)
        bones = [r.str(ba + k * 0x38) for k in range(min(nb, 4096))] if ba else []
        out.append({"name": r.str(o + 8), "frames": r.s32(o + 0x40), "loop": bool(fl & 4), "curves": r.s32(o + 0x44),
                    "bones_animated": nb, "bones": bones})
    return out


def _magic_list(B, arr, magic, count):
    """offsets of the first `count` blocks with this magic from arr on (anim arrays whose element size varies)"""
    out, p = [], arr
    while arr and len(out) < count:
        p = B.b.find(magic, p)
        if p < 0:
            break
        out.append(p)
        p += 4
    return out


ATTR_MEANING = {"_p": "position", "_n": "normal", "_t": "tangent", "_b": "bitangent", "_c": "vertex colour", "_u": "texture coordinates",
                "_i": "bone indices", "_w": "bone weights"}
SAMPLER_MEANING = {"_a": "albedo / colour", "_n": "normal map", "_e": "emission / glow", "_rgh": "roughness", "_mtl": "metalness",
                   "_ocl": "ambient occlusion", "_noise": "noise", "_msk": "mask", "_o": "opacity", "_s": "specular"}


def _meaning(table, name):
    best = ""
    for k, v in table.items():
        if name.startswith(k) and name[len(k):].isdigit() and len(k) > len(best):
            best = k
    return table.get(best, "")


def read_bfres(b):
    B = Bfres(b)
    out = {"_binary_format": "BFRES graphics resource (version %s)" % B.version,
           "_note": "Models (skeleton, meshes, materials and the textures they use) and/or animations. Model files can be "
                    "opened in the 3D view; textures live in romfs/Tex as separate .bntx.zs files.",
           "name": B.name}
    tex_used = set()
    models = []
    for m in B.models:
        sk = m["skeleton"]
        md = {"name": m["name"], "bones": len(sk["bones"]) if sk else 0, "shapes_count": len(m["shapes"]),
              "materials_count": len(m["materials"])}
        tv = tt = 0
        shapes = []
        for s in m["shapes"]:
            vb = m["vertex_buffers"][s["vertex_buffer"]] if s["vertex_buffer"] < len(m["vertex_buffers"]) else None
            mat = m["materials"][s["material"]]["name"] if s["material"] < len(m["materials"]) else "?"
            nv = vb["vertices"] if vb else 0
            tris = s["meshes"][0]["indices"] // 3 if s["meshes"] else 0
            tv += nv
            tt += tris
            attrs = ["%s %s%s" % (a["name"], attr_format_name(a["format"]),
                                  " (%s)" % _meaning(ATTR_MEANING, a["name"]) if _meaning(ATTR_MEANING, a["name"]) else "")
                     for a in (vb["attributes"] if vb else [])]
            sw = s["skin_weights_per_vertex"]
            shapes.append({"name": s["name"], "material": mat, "vertices": nv, "triangles": tris,
                           "levels_of_detail": [mm["indices"] // 3 for mm in s["meshes"]],
                           "skinning": "rigid: follows bone %s" % (sk["bones"][s["bone"]]["name"] if sk and s["bone"] < len(sk["bones"]) else s["bone"])
                           if sw == 0 else "rigid: each vertex follows one bone" if sw == 1 else "smooth: up to %d bones per vertex" % sw,
                           "attributes": attrs, "key_shapes": s["key_shapes"]})
        md["vertices"], md["triangles"] = tv, tt
        md["shapes"] = shapes
        mats = []
        for mt in m["materials"]:
            tex = {}
            for i, t in enumerate(mt["textures"]):
                smp = mt["samplers"][i] if i < len(mt["samplers"]) else "#%d" % i
                mean = _meaning(SAMPLER_MEANING, smp)
                tex["%s%s" % (smp, " (%s)" % mean if mean else "")] = t
                if t:
                    tex_used.add(t)
            mats.append({"name": mt["name"], "shader": "%s / %s" % (mt.get("shader_archive"), mt.get("shading_model")), "textures": tex})
        md["materials"] = mats
        if sk:
            lines = []
            depth = {}
            for i, bn in enumerate(sk["bones"]):
                d = depth[i] = depth.get(bn["parent"], -1) + 1 if bn["parent"] >= 0 else 0
                rot = bn["rotation"][:3] if bn["rotation_mode"] == "euler_xyz" else bn["rotation"]
                lines.append("%s%s   pos %s  rot %s%s%s" % ("  " * min(d, 30), bn["name"],
                             " ".join("%.3g" % x for x in bn["translation"]),
                             " ".join("%.3g" % x for x in rot), " (quaternion)" if bn["rotation_mode"] != "euler_xyz" else "",
                             "" if bn["scale"] == [1.0, 1.0, 1.0] else "  scale " + " ".join("%.3g" % x for x in bn["scale"])))
            md["skeleton"] = lines
        if m["user_data"]:
            md["user_data"] = m["user_data"]
        models.append(md)
    if models:
        out["models"] = models
        out["textures_used"] = sorted(tex_used)
    sa = skeletal_anims(B)
    if sa:
        out["skeletal_animations"] = ["%s: %d frames%s, %d bones" % (a["name"], a["frames"], ", loops" if a["loop"] else "", a["bones_animated"]) for a in sa]
    if B.mat_anim_names:
        pos = _magic_list(B, B.r.u64(0x68), b"FMAA", len(B.mat_anim_names))
        out["material_animations"] = ["%s: %d frames" % (nm, B.r.s32(pos[i] + 0x58)) if i < len(pos) else nm
                                      for i, nm in enumerate(B.mat_anim_names)]
    for key, names in (("bone_visibility_animations", B.vis_anim_names), ("shape_animations", B.shape_anim_names),
                       ("scene_animations", B.scene_anim_names)):
        if names:
            out[key] = names
    if B.ext_files:
        out["embedded_files"] = ["%s (%d bytes)" % (nm, sz) for nm, o, sz in B.ext_files]
        for nm, o, sz in B.ext_files:              # decode what's inside (e.g. a .bofx: textures + lens flare settings)
            blob = b[o:o + sz]
            try:
                if blob[:4] == b"AAMP":
                    a = read_aamp(blob)
                    if a.get("type") == "aglofx":
                        _ofx_summary(a)
                        out["_binary_format"] = "BOFX lens flare (BFRES %s with textures and settings)" % B.version
                        out["_note"] = ("Lens flare file (.bofx): a BFRES holding the flare textures and the lens flare "
                                        "settings (default.baglofx). Open it with the Lighting viewer (select the "
                                        ".genvb.zs archive or this file and click Lighting).")
                    out.setdefault("embedded", {})[nm] = a
                elif blob[:4] == b"BNTX":
                    tx = read_bntx(blob)
                    out.setdefault("embedded", {})[nm] = {k: tx[k] for k in ("texture_count", "formats", "textures") if k in tx}
            except Exception as e:
                out.setdefault("embedded", {})[nm] = "couldn't be read: %s" % e
    return out


# ---------- ASB / BAEV (animation graphs and animation events) ----------
def read_asb_like(blob, ext):
    """AS/*.root.asb (animation state graph) and AnimationEvent/*.baev: the node layout isn't decoded,
    but the string pool holds every animation, state and parameter name, which is what's worth searching."""
    magic = blob[:4].decode("latin-1")
    ver = struct.unpack_from("<I", blob, 4)[0] if len(blob) >= 8 else 0
    pool = None
    if magic == "ASB " and len(blob) >= 0x28:
        o = struct.unpack_from("<I", blob, 0x24)[0]
        if 0x28 <= o < len(blob):
            pool = o
    if pool is None:                # fall back to the longest run of NUL-separated printable text near the end
        m = list(re.finditer(rb"(?:[\x20-\x7e]{2,}\x00){8,}", blob))
        pool = max(m, key=lambda x: len(x.group())).start() if m else len(blob)
    names, seen = [], set()
    for m in re.finditer(rb"[\x20-\x7e]{2,}", blob[pool:]):
        t = m.group().decode("ascii")
        if t not in seen:
            seen.add(t); names.append(t)
    kind = "ASB animation state graph" if magic == "ASB " else ("BAEV animation events" if ext == "baev" else magic.strip() + " animation data")
    return {"_binary_format": "%s (version 0x%x)" % (kind, ver),
            "_note": "Node structure not decoded; 'names' lists the animation, state and parameter names from the file's string pool. "
                     "These are the names the game asks for at runtime; the motion itself lives in .anim.bfres files.",
            "string_pool_offset": pool, "name_count": len(names), "names": names}

# ---------- AAMP (parameter archives: AGL environment / lighting settings in romfs/Env/*.genvb) ----------
# Parameter names are stored only as CRC32 hashes. These names were recovered from the game's executable, the
# public BotW name list and by matching hashes; anything else is shown as its hash (0x........).
AAMP_NAMES = """BaseBone Name Preset connection_curve_list connection_curve_num OutputAxisOrder aim_axis aim_driven_list aim_driven_num base_bone_id base_rotate base_translate bone_id bone_list bone_num driven_bone_list driven_bone_num driver_bone_id driver_bone_list driver_bone_num helper_bone_data helper_bone_header key_num output_list output_num pose_driven_list pose_driven_num position_driven_list position_driven_num rotate_driven_id rotate_driven_list rotate_driven_num rotate_driven_type translateX translateX_id translateY translateY_id translateZ translateZ_id translate_driven_id translate_driven_type up_axis filter_aa subpix_param max_span fxaa_reprojection fxaa_reprojection_move_limit cubemap_mgr cubemap_unit cubemap_unit_0 cubemap_unit_1 cubemap_unit_2 cubemap_unit_3 cubemap_unit_4 cubemap_unit_5 cubemap_unit_6 cubemap_unit_7 near far position AmbientLight AmbientLight0 AmbientUnderPower Amplitude AutoExposureMax AutoExposureMin BacksideColor BloomObj CloudShapeScale CloudShapeTextureName Color DepthOfFieldObj DepthShadowOffset DiffuseColor Direction DirectionalLight DirectionalLight0 EnableCloudShape EnableDirLightCheck EnableHeightMapUpdate EnableHighQuality Fog FogBrightnessRate FogDensity FogFar FogNear FogNoiseBaseAdd FogNoiseEnable FogNoiseScale FogNoiseScroll FogNoiseWeight FogNoiseWeight2 FogSaturationRate GroundColor HeightMapUpdateInterval HeightScale HemisphereLight HemisphereLight0 InitTime Intensity IntensityMax IntensityMin MoonLightColor MoonSizeCos OceanSize Projector RimColor RimIntensity ShadowFadeEnd ShadowFadeStart ShadowPrePass ShadowScale SkyColor SpecularColor SsaoFadeEnd SsaoFadeStart SsaoShadingOffset StarFieldIntensity StarNum SunLightColor SunSizeCos SunXRotDegreeInit SunXRotSpeedDegree SunYRotDegree SunZRotDegree TimeScale ViewCoordinate WaterAlbedo WaterRoughness WindDegree WindStrength YFogColor YFogEnd YFogNoiseBaseAdd YFogNoiseEnable YFogNoiseScroll YFogNoiseUseColor YFogNoiseWeight YFogNoiseWeight2 YFogNoseScale YFogSecondBaseAdd YFogSecondNoiseEnable YFogSecondScale YFogSecondScroll YFogSecondUseColor YFogSecondWeight YFogSecondWeight2 YFogStart adhoc_fog_atten_grd adhoc_fog_atten_minscale_sky adhoc_fog_atten_sky adhoc_fog_color adhoc_fog_far adhoc_fog_near align arg aspect blend_scale bloom clamped_luminance color_correction color_ctrl_depth config convert_parts_array depth_blur depth_blur_add depth_offset_end depth_offset_start depth_offset_value dof dof_far_max doftype dynamicShadowFarFadeEnd dynamicShadowFarFadeStart dynamic_color dynamic_mie_amplifier dynamic_mie_symmetrical_prop dynamic_rayleigh_amplifier edit_type enable enable_clamped_luminance enable_color_control enable_color_reverse enable_depth_clamp enable_depth_offset enable_dof_far_max enable_indirect_from_full enable_luminance_offset enable_old_calc enable_reduce_draw enable_vignetting_2_shape enable_vignetting_blur enable_vignetting_color end env_obj_ref_array ex_iteration ex_type expand ext faceNormalBias far_enable far_end far_mul_color far_start file finalgather flg ground_color group indirect_depth_cancelenable indirect_enable indirect_scale indirect_tex_rotate indirect_tex_scale indirect_tex_trans intensity is_PcfSampleNum is_PcfShaderType is_UseDecalTrailSignedB is_UseDecalTrailSignedR is_enable is_farDepthTestDist is_useDecalAo is_useDepth2Normal is_useDepth2NormalBlur is_useFarDepthTest is_useFarFade is_useMipLevelBlur is_useMipLevelBlurReduce is_usePreCombSsao is_useStaticDepthShadow is_useUpdatableStaticDepthShadow level luminance_intensity mAperture mFNumber mFocalDistance mFocalRange mMaxCoCSize mQuality mScreenSpaceBlurRepNum mie_amplifier_rendering mie_base_height mie_scattering_coeff mie_symmetrical_prop mie_symmetrical_prop_rendering mipBlurRepNum mipBlurWidth name near_enable normal2ShadowMul normal2ShadowRatio param param_root parts pcfWidth project_graphic_base quality rayleigh_amplifier_rendering rayleigh_base_height refer render_sun_intensity render_sun_lerp render_sun_size resolutionMode saturate_min scatter_fog_atten scatter_fog_density scatter_fog_far scatter_fog_horz scatter_fog_near screenSpaceBlurType screenSpaceBlurWidth set_array setting shaft_expand shaft_finalgather shaft_intensity shaft_threshhold shaft_threshold_range signature sky ss_project_graphics_base start staticShadowFarFadeEnd staticShadowFarFadeStart static_mie_base_height static_mie_scattering_coeff static_mie_symmetrical_prop static_rayleigh_base_height static_rayleigh_scattering_coeff sun_color threshhold threshhold_balance threshold_range tool type version vignetting_blend vignetting_blur vignetting_color""".split()
_AAMP_HASH = None
def _aamp_names():
    global _AAMP_HASH
    if _AAMP_HASH is None:
        import zlib
        h = {}
        for n in AAMP_NAMES:
            h[zlib.crc32(n.encode())] = n
        h[zlib.crc32(b"AutoExposure")] = "AutoExposure"   # auto exposure (aglatex), matched from its hash
        for i in range(100):                       # numbered names: list items "0".."99", param0, data1, curve2...
            for f in ("%d", "param%d", "data%d", "curve%d", "color%d", "Light%d", "DirectionalLight%d", "Fog%d",
                      "name%d", "intensity%d"):           # light maps (agllmap): name0 / intensity0 ... matched from their hashes
                h[zlib.crc32((f % i).encode())] = f % i
        for n in ['OfxLensFlare', 'OfxLensFlareDynamic', 'OfxLensFlareImpl', 'OfxLargeLensFlareImpl', 'TextureList', 'Position', 'Radius', 'CoreRadius', 'DepthOffset', 'PresetName', 'CenterPos', 'BaseAxis']:
            h[zlib.crc32(n.encode())] = n
        for i in range(100):                       # lens flare (aglofx) elements and textures, names from the game executable
            for f in ('Position%d', 'AngleCenter%d', 'AnglePower%d', 'AngleWidth%d', 'BlendMode%d', 'CenterPosAlphaPow%d', 'CenterPosAlphaRate%d', 'CenterPosScalingPow%d', 'CenterPosScalingRate%d', 'Color%d', 'EdgeScaleRate%d', 'Intensity%d', 'IsEnableAngleOcclusion%d', 'IsEnableDraw%d', 'IsEnableEdgeScaling%d', 'IsEnableOccludedAlpha%d', 'IsEnableOccludedDec%d', 'IsEnableOccludedScaling%d', 'IsEnableOctagon%d', 'IsEnableRotate%d', 'IsEnableRotateInv%d', 'IsEnableRotatePos%d', 'IsSizeZoom%d', 'Size%d', 'Rotate%d', 'RotatePosRate%d', 'Texture2Idx%d', 'TextureIdx%d', 'IsQuarter%d', 'RefTexName%d', 'TextureInfo%d', 'CenterPos%d', 'BaseAxis%d'):
                h[zlib.crc32((f % i).encode())] = f % i
        for n in ['mf_root_param', 'reduce', 'blur', 'change_format', 'color_drift', 'trimming', 'hdr_compose', 'expand', 'color_correction', 'param_array', 'active', 'blur_type', 'blur_num', 'gaussian_kernel', 'format', 'trim_scale', 'trim_center', 'drift_r', 'drift_g', 'drift_b', 'param_index', 'reduce_scale', 'save_index']:                    # multi filter (aglmf), names from the game executable
            h[zlib.crc32(n.encode())] = n
        for i in range(100):
            for f in ('reduce_%d', 'blur_%d', 'change_format_%d', 'color_drift_%d', 'trimming_%d', 'hdr_compose_%d', 'expand_%d', 'color_correction_%d', 'data_index%d'):
                h[zlib.crc32((f % i).encode())] = f % i
        for n in ['ssao_parameter', 'ssao_type', 'ao_far', 'radius', 'dist_attn', 'variable_dist_min', 'depth_offset', 'sample_pair_num', 'alchemy_radius', 'alchemy_max_radius', 'alchemy_bias', 'alchemy_detection_intensity', 'alchemy_density', 'alchemy_sample_pair_num', 'mip_blur_num', 'light_name', 'light_type', 'bias_trans', 'bias_rotate', 'bias_scale', 'repeat', 'proj_name', 'density', 'density_drc', 'use_density_drc', 'enable_depth_gain', 'depth_gain_start', 'depth_gain_end', 'depth_gain_value', 'enable_shaft_d', 'shaft_d_start', 'shaft_d_end', 'shaft_d_value', 'anim_rot_speed']:                    # gsys shadow / bloom (gsdw, aglblm), from the executable + matching hashes
            h[zlib.crc32(n.encode())] = n
        for i in range(100):
            for f in ('depth_shadow_parameter_%d', 'projection_shadow_%d'):
                h[zlib.crc32((f % i).encode())] = f % i
        for n in ['volume_mask', 'common', 'unit_0', 'unit_1', 'unit_2', 'unit_3', 'autoAdjustNear', 'layer_reduce_level', 'layer_number', 'layer_dist_nonlinear', 'shadowmap_amp_low', 'shadowmap_amp_high', 'shadowmap_pcf_type', 'range_near', 'range_far', 'render_mask_color', 'shadowmap_reduce_level', 'depth_convolve_max', 'shadow_convolve_max', 'use_raymarch']:                    # volume mask (aglvolm), from the executable
            h[zlib.crc32(n.encode())] = n
        for n in ['Closet', 'Default', 'Layout', 'Map', 'ParticleConfig', 'agl_sharc_num', 'bfres_num', 'create_auto_exposure', 'create_bloom', 'create_color_correction', 'create_decal', 'create_depth_shadow', 'create_dof', 'create_filter_aa', 'create_hdr', 'create_light_map', 'create_light_probe', 'create_masked_light', 'create_ndsr', 'create_occluded_effect', 'create_occlusion_query', 'create_outline_silhouette', 'create_radial_blur', 'create_reflection', 'create_shadow_mask', 'create_snap_shot', 'create_ssocclusion', 'create_volume_mask', 'cubemap_array', 'deferred_shading_model', 'depth_reduce', 'env_obj_set_template', 'fog_num', 'forward_soft_edge', 'g3d_shader_binary', 'g3d_shader_text', 'g_buffer', 'geometry_culling_z_pre_pass', 'gsys_app_package_info', 'model_num', 'name_array', 'num', 'package_name', 'shader_num', 'shape_num', 'sharc', 'sharcb', 'template_name', 'use_decal_ao', 'use_decal_buffer', 'use_decal_trail', 'visualize_mask', 'z_pre_pass', 'Island', 'EnvMapHighQuality']:                    # gsys project config (gmsconf, ptclconf, gapkg*), executable + matched hashes
            h[zlib.crc32(n.encode())] = n
        for i in range(100):
            for f in ('config_%d', 'deferred_shading_model%d', 'depth_shadow_cascade_num_%d', 'depth_shadow_clip_plane_%d', 'depth_shadow_far_%d', 'depth_shadow_offset_far_%d', 'depth_shadow_offset_near_%d', 'depth_shadow_refvalue_threshold_%d', 'depth_shadow_use_uniform_mtx_%d', 'env_obj_gen_name%d', 'env_obj_gen_num%d', 'light_map_name_%d', 'user_visualize_clr_name_%d', 'user_visualize_tex_name_%d', 'config_%02d'):
                h[zlib.crc32((f % i).encode())] = f % i
        # change_format's four channel settings: only the last letter could be worked out (r, g, b, a)
        h.update({0x70b1b8f2: "?r", 0x1d6c5c19: "?g", 0x6d06a896: "?b", 0xf40ff92c: "?a"})
        for i in range(400):                       # helper bone (phhb) list items
            for f in ("bone_%d", "driver_bone_%d", "driven_bone_%d", "pose_driven_%d", "output_%d", "connection_curve_%d", "key_%d"):
                h[zlib.crc32((f % i).encode())] = f % i
        _AAMP_HASH = h
    return _AAMP_HASH

AAMP_KINDS = {
    "projgfxbase": "project graphics settings: sun / moon / stars, fog, height fog, sky, clouds, wind, shadows",
    "ssprojgfxbase": "project graphics settings (screen-space extras)",
    "aglblm": "bloom (glow) settings", "agldof": "depth of field (camera blur) settings",
    "aglccr": "colour correction (brightness, contrast, saturation, curves)", "aglenv": "lights and fog objects",
    "aglenvset": "environment sets: which lights / fogs each preset uses", "aglsky": "sky settings",
    "aglshpp": "shadow pre-pass settings", "gapkginfo": "shader package info: the package name and how many shader archives it has", "gapkglist": "shader package file list: the .sharcb / .bfsha files in the package", "gmsconf": "gsys renderer configuration: which rendering features each setup creates (shadows, bloom, depth of field, SSAO, decals, the NDSR upscaler...), buffer sizes and the debug view names", "ptclconf": "particle system configuration: buffer sizes and light map names", "aglvolm": "volume mask: light shafts / volumetric light through the sun's shadows (god rays), in layers over a distance range", "gsdw": "shadow settings: which light casts the sun shadow, an optional projected shadow texture, and screen-space ambient occlusion (SSAO)", "aglmf": "multi filter: a chain of screen filters (shrink, blur, enlarge, colour correction...) - the blurred backgrounds behind menus and text", "aglofx": "lens flare settings: the sun's flare - its textures and up to 20 flare elements per preset", "genv": "environment archive index: the files each part comes from",
}
AAMP_TYPES = {0: "bool", 1: "f32", 2: "int", 3: "vec2", 4: "vec3", 5: "vec4", 6: "color", 7: "string32", 8: "string64",
              9: "curve1", 10: "curve2", 11: "curve3", 12: "curve4", 13: "int buffer", 14: "float buffer", 15: "string256",
              16: "quat", 17: "u32", 18: "u32 buffer", 19: "binary buffer", 20: "string"}

def read_aamp(b):
    H = _aamp_names()
    if len(b) < 0x30:
        raise ValueError("too short")
    ver, flags, size, pver, pio = struct.unpack_from("<IIIII", b, 4)
    if ver != 2:
        raise ValueError("AAMP version %d isn't supported" % ver)
    nl, no, npar = struct.unpack_from("<3I", b, 0x18)
    ptype = b[0x30:0x30 + pio].split(b"\0")[0].decode("ascii", "replace")
    root = 0x30 + pio
    unknown = set()
    def nm(h):
        n = H.get(h)
        if n is None:
            unknown.add(h); n = "0x%08x" % h
        return n
    def rf(o):
        x = struct.unpack_from("<f", b, o)[0]
        return round(x, 6) if x == x and abs(x) != float("inf") else str(x)
    def val(o, t):
        if t == 0: return bool(struct.unpack_from("<I", b, o)[0])
        if t == 1: return rf(o)
        if t == 2: return struct.unpack_from("<i", b, o)[0]
        if t == 17: return struct.unpack_from("<I", b, o)[0]
        if t in (3, 4, 5, 6, 16):
            n = {3: 2, 4: 3, 5: 4, 6: 4, 16: 4}[t]
            v = [rf(o + 4 * k) for k in range(n)]
            if t == 6:   # colours: also as #RRGGBB; HDR light colours (above 1.0) are scaled down to show their hue
                rgb = [x if isinstance(x, float) else 0.0 for x in v[:3]]
                m = max(rgb + [0.0])
                k = 1.0 / m if m > 1.0 else 1.0
                c = tuple(max(0, min(255, int(round(x * k * 255)))) for x in rgb)
                out = {"rgba": v, "hex": "#%02X%02X%02X" % c}
                if m > 1.0:
                    out["brightness"] = round(m, 4)
                return out
            return v
        if t in (7, 8, 15, 20):
            e = b.find(b"\0", o)
            return b[o:e if e >= 0 else len(b)].decode("utf-8", "replace")
        if t in (13, 14, 18, 19):
            n = struct.unpack_from("<I", b, o - 4)[0]
            if t == 19: return b[o:o + n].hex(" ")
            if t == 14: return [rf(o + 4 * k) for k in range(n)]
            return list(struct.unpack_from("<%d%s" % (n, "i" if t == 13 else "I"), b, o))
        if 9 <= t <= 12:
            out = []
            for k in range(t - 8):
                q = o + k * 0x80
                a1, a2 = struct.unpack_from("<II", b, q)
                out.append({"a": a1, "b": a2, "floats": [rf(q + 8 + 4 * j) for j in range(30)]})
            return out
        return "(type %d)" % t
    def plist(o, depth=0):
        h, lo, ln, oo, on = struct.unpack_from("<IHHHH", b, o)
        d = {}
        for i in range(on):
            q = o + oo * 4 + i * 8
            oh, po, pn = struct.unpack_from("<IHH", b, q)
            obj = {}
            for k in range(pn):
                r = q + po * 4 + k * 8
                ph, x = struct.unpack_from("<II", b, r)
                obj[nm(ph)] = val(r + (x & 0xFFFFFF) * 4, x >> 24)
            d[nm(oh)] = obj
        if depth < 32:
            for i in range(ln):
                q = o + lo * 4 + i * 12
                d[nm(struct.unpack_from("<I", b, q)[0])] = plist(q, depth + 1)
        return d
    tree = {nm(struct.unpack_from("<I", b, root)[0]): plist(root)}
    out = {"_binary_format": "AAMP parameters (%s)" % ptype,
           "_note": "Nintendo parameter archive" + (": " + AAMP_KINDS[ptype] if ptype in AAMP_KINDS else "") +
                    ". Names are stored as hashes; names that couldn't be recovered show as 0x........ "
                    "Colours: rgba values plus #RRGGBB; for brighter-than-white (HDR) light colours the hex shows the hue and 'brightness' how many times brighter than white.",
           "type": ptype, "lists": nl, "objects": no, "parameters": npar}
    if unknown:
        out["unnamed_parameters"] = len(unknown)
    out.update(tree)
    return out

# ---------- BFEVFL (event flows: conversations, cutscenes, shops - Event/EventFlow) ----------
def read_bfevfl(b):
    if b[:8] != b"BFEVFL\0\0":
        raise ValueError("not a BFEVFL file")
    u8 = lambda o: b[o]
    u16 = lambda o: struct.unpack_from("<H", b, o)[0]
    u32 = lambda o: struct.unpack_from("<I", b, o)[0]
    f32 = lambda o: struct.unpack_from("<f", b, o)[0]
    u64 = lambda o: struct.unpack_from("<Q", b, o)[0]
    def s(o):                                   # length-prefixed string
        if not o or o + 2 > len(b): return None
        return b[o + 2:o + 2 + u16(o)].decode("utf-8", "replace")
    def cstr(o):
        e = b.find(b"\0", o); return b[o:e].decode("utf-8", "replace")
    def dic(o):                                 # DIC radix tree -> names in entry order
        if not o or b[o:o + 4] != b"DIC ": return []
        return [s(u64(o + 8 + 16 * i + 8)) for i in range(1, u32(o + 4) + 1)]
    def rf(x): return round(x, 6) if x == x else str(x)
    def container(o, depth=0):
        if not o or depth > 16: return None
        t, n, d = u8(o), u16(o + 2), u64(o + 8)
        q = o + 16
        if t == 1:
            names = dic(d)
            return {(names[i] if i < len(names) and names[i] is not None else str(i)): container(u64(q + 8 * i), depth + 1) for i in range(n)}
        if t == 0: return {"argument": s(u64(q))}
        if t in (2, 7): v = [struct.unpack_from("<i", b, q + 4 * i)[0] for i in range(n)]
        elif t in (3, 8): v = [bool(u32(q + 4 * i)) for i in range(n)]
        elif t in (4, 9): v = [rf(f32(q + 4 * i)) for i in range(n)]
        elif t in (5, 6, 10, 11): v = [s(u64(q + 8 * i)) for i in range(n)]
        elif t == 12: return {"actor": s(u64(q)), "secondary": s(u64(q + 8))}
        else: return "(type %d)" % t
        return v[0] if t in (2, 3, 4, 5, 6) and n == 1 else v
    def actor(o):
        na, nq = u16(o + 0x30), u16(o + 0x32)
        a = {"name": s(u64(o))}
        if s(u64(o + 8)): a["secondary_name"] = s(u64(o + 8))
        if s(u64(o + 16)): a["argument"] = s(u64(o + 16))
        a["actions"] = [s(u64(u64(o + 24) + 8 * k)) for k in range(na)] if na else []
        a["queries"] = [s(u64(u64(o + 32) + 8 * k)) for k in range(nq)] if nq else []
        if u64(o + 40): a["params"] = container(u64(o + 40))
        return a
    def flowchart(o):
        if b[o:o + 4] != b"EVFL": raise ValueError("flowchart magic missing")
        nact, nev, nent = u16(o + 0x10), u16(o + 0x16), u16(o + 0x18)
        actors = [actor(u64(o + 0x28) + 0x38 * i) for i in range(nact)]
        def aname(i): return actors[i]["name"] if 0 <= i < len(actors) else "actor%d" % i
        events = []
        for i in range(nev):
            e = u64(o + 0x30) + ev_size * i
            t = u8(e + 8); ev = {"index": i, "name": s(u64(e))}
            def nxt(x): return None if x == 0xFFFF else x
            if t == 0:
                ai, k = u16(e + 0xC), u16(e + 0xE)
                acts = actors[ai]["actions"] if 0 <= ai < len(actors) else []
                ev.update({"type": "action", "actor": aname(ai), "action": acts[k] if k < len(acts) else k, "next": nxt(u16(e + 0xA))})
                if u64(e + 0x10): ev["params"] = container(u64(e + 0x10))
            elif t == 1:
                ai, k, nc = u16(e + 0xC), u16(e + 0xE), u16(e + 0xA)
                qs = actors[ai]["queries"] if 0 <= ai < len(actors) else []
                ev.update({"type": "switch", "actor": aname(ai), "query": qs[k] if k < len(qs) else k})
                if u64(e + 0x10): ev["params"] = container(u64(e + 0x10))
                c = u64(e + 0x18)
                ev["cases"] = {str(u32(c + 8 * j)): u16(c + 8 * j + 4) for j in range(nc)}
            elif t == 2:
                nf = u16(e + 0xA); f = u64(e + 0x10)
                ev.update({"type": "fork", "branches": [u16(f + 2 * j) for j in range(nf)], "join": u16(e + 0xC)})
            elif t == 3:
                ev.update({"type": "join", "next": nxt(u16(e + 0xA))})
            elif t == 4:
                ev.update({"type": "sub_flow", "flowchart": s(u64(e + 0x18)) or "(this file)", "entry_point": s(u64(e + 0x20)), "next": nxt(u16(e + 0xA))})
                if u64(e + 0x10): ev["params"] = container(u64(e + 0x10))
            elif t == 5:                        # version 4: run a sub-flow, then branch on the value it returns
                nc, c = u16(e + 0xA), u64(e + 0x18)
                ev.update({"type": "sub_flow_switch", "flowchart": s(u64(e + 0x20)) or "(this file)", "entry_point": s(u64(e + 0x28))})
                if u64(e + 0x10): ev["params"] = container(u64(e + 0x10))
                ev["cases"] = {_evfl_val(u32(c + 8 * j)): u16(c + 8 * j + 4) for j in range(nc)}
            elif t == 6:                        # version 4: end this sub-flow, returning a value
                ev.update({"type": "return", "value": _evfl_val(u32(e + 0xC))})
            else:
                ev["type"] = "unknown (%d)" % t
            events.append(ev)
        names = dic(u64(o + 0x38))
        entries = {}
        for i in range(nent):
            p = u64(o + 0x40) + 0x20 * i
            ns = u16(p + 0x18)
            ent = {"main_event": _evfl_nxt(u16(p + 0x1C))}
            if ns: ent["sub_flow_events"] = [u16(u64(p) + 2 * j) for j in range(ns)]
            nv = u16(p + 0x1A)
            if nv: ent["variables"] = dic(u64(p + 0x10))
            entries[names[i] if i < len(names) else str(i)] = ent
        fc = {"name": s(u64(o + 0x20)), "entry_points": entries,
              "script": {n: _evfl_script(events, e["main_event"]) for n, e in entries.items()},
              "actors": actors, "events": events}
        return fc
    def timeline(o):
        if b[o:o + 4] != b"TLIN": raise ValueError("timeline magic missing")
        dur = rf(f32(o + 0x10)); nact, nclip, nos, nsub, ncut = u16(o + 0x14), u16(o + 0x18), u16(o + 0x1A), u16(o + 0x1C), u16(o + 0x1E)
        actors = [actor(u64(o + 0x28) + 0x38 * i) for i in range(nact)]
        clips = []
        for i in range(nclip):
            c = u64(o + 0x30) + 0x18 * i
            ai, k = u16(c + 8), u16(c + 10)
            acts = actors[ai]["actions"] if ai < len(actors) else []
            cl = {"start": rf(f32(c)), "duration": rf(f32(c + 4)), "actor": actors[ai]["name"] if ai < len(actors) else ai,
                  "action": acts[k] if k < len(acts) else k}
            if u64(c + 0x10): cl["params"] = container(u64(c + 0x10))
            clips.append(cl)
        oneshots = []
        for i in range(nos):
            c = u64(o + 0x38) + 0x18 * i
            ai, k = u16(c + 4), u16(c + 6)
            acts = actors[ai]["actions"] if ai < len(actors) else []
            os_ = {"time": rf(f32(c)), "actor": actors[ai]["name"] if ai < len(actors) else ai, "action": acts[k] if k < len(acts) else k}
            if u64(c + 0x10): os_["params"] = container(u64(c + 0x10))
            oneshots.append(os_)
        cuts = []
        for i in range(ncut):
            c = u64(o + 0x50) + 0x18 * i
            cu = {"time": rf(f32(c)), "name": s(u64(c + 8))}
            if u64(c + 0x10): cu["params"] = container(u64(c + 0x10))
            cuts.append(cu)
        t = {"name": s(u64(o + 0x20)), "duration_frames": dur, "actors": actors, "clips": clips}
        if oneshots: t["oneshots"] = oneshots
        if cuts: t["cuts"] = cuts
        if nsub: t["subtimelines"] = nsub
        if u64(o + 0x58): t["params"] = container(u64(o + 0x58))
        return t
    nfc, ntl = u16(0x20), u16(0x22)
    ev_size = 0x30 if b[9] >= 4 else 0x28       # version 4 (Switch Sports...) events are 8 bytes longer
    out = {"_binary_format": "BFEVFL event flow (version %d.%d)" % (b[9], b[10]),
           "_note": "An event script: flowcharts of actions (actor.action with parameters), switches (actor.query -> a branch "
                    "per answer), forks/joins and calls to other flows, started from named entry points; 'script' lists each "
                    "entry point as readable steps. Timelines are timed clips (frames at 60/s), e.g. a cutscene's voice lines. "
                    "Message labels (e.g. Talk_...) are the text's labels in the Mals message files.",
           "name": cstr(u32(0x10))}
    if nfc:
        names = dic(u64(0x30))
        out["flowcharts"] = [flowchart(u64(u64(0x28) + 8 * i)) for i in range(nfc)]
    if ntl:
        out["timelines"] = [timeline(u64(u64(0x38) + 8 * i)) for i in range(ntl)]
    return out

def _evfl_nxt(x): return None if x == 0xFFFF else x

def _evfl_fnv(t):
    h = 0x811C9DC5
    for c in t.encode(): h = ((h ^ c) * 0x01000193) & 0xFFFFFFFF
    return h
# version 4 sub-flows return a value as the FNV-1a hash of its text ("1" / "0" for yes / no)
_EVFL_VALS = {_evfl_fnv(t): t for t in ["true", "false", "True", "False", "None", ""] + [str(i) for i in range(-16, 256)]}
def _evfl_val(h): return _EVFL_VALS.get(h, "0x%08X" % h)

def _evfl_pv(v):
    if isinstance(v, dict): return "{" + ", ".join("%s=%s" % (k, _evfl_pv(x)) for k, x in v.items()) + "}"
    if isinstance(v, list): return "[" + ", ".join(_evfl_pv(x) for x in v) + "]"
    if isinstance(v, str): return '"%s"' % v
    return str(v)

def _evfl_script(events, start, limit=4000):
    """an entry point as readable lines: each event once; a step reached again shows 'go to EventN'"""
    lines, seen = [], set()
    def walk(i, ind, stop=None, branch=False):
        while i is not None and i != stop and len(lines) < limit:
            if not (0 <= i < len(events)): lines.append(ind + "(bad event %s)" % i); return
            e = events[i]
            if i in seen: lines.append(ind + "go to %s" % e["name"]); return
            seen.add(i)
            p = "(" + ", ".join("%s=%s" % (k, _evfl_pv(v)) for k, v in e.get("params", {}).items()) + ")" if isinstance(e.get("params"), dict) else "()"
            t = e["type"]
            if t == "action":
                lines.append(ind + "%s: %s.%s%s" % (e["name"], e["actor"], e["action"], p)); i = e["next"]
            elif t == "switch":
                lines.append(ind + "%s: switch %s.%s%s" % (e["name"], e["actor"], e["query"], p))
                for val, tgt in e["cases"].items():
                    lines.append(ind + "  case %s:" % val); walk(tgt, ind + "    ")
                return
            elif t == "fork":
                lines.append(ind + "%s: at the same time:" % e["name"])
                for k, br in enumerate(e["branches"]):
                    lines.append(ind + "  branch %d:" % (k + 1)); walk(br, ind + "    ", e["join"], True)
                j = events[e["join"]] if 0 <= e["join"] < len(events) else None
                lines.append(ind + "%s: (all branches done)" % (j["name"] if j else "join"))
                if j: seen.add(e["join"]); i = j.get("next")
                else: return
            elif t == "join":
                i = e["next"]
            elif t == "sub_flow":
                lines.append(ind + "%s: run %s : %s%s" % (e["name"], e["flowchart"], e["entry_point"], p if p != "()" else "")); i = e["next"]
            elif t == "sub_flow_switch":
                lines.append(ind + "%s: run %s : %s%s and switch on its result" % (e["name"], e["flowchart"], e["entry_point"], p if p != "()" else ""))
                for val, tgt in e["cases"].items():
                    lines.append(ind + "  result %s:" % val); walk(tgt, ind + "    ")
                return
            elif t == "return":
                lines.append(ind + "%s: return %s" % (e["name"], e["value"])); return
            else:
                lines.append(ind + "%s: %s" % (e["name"], t)); return
        if i is None and not branch and len(lines) < limit: lines.append(ind + "end")
    walk(start, "")
    return lines

# ---------- BNSH (compiled NVN shader containers: layout archives' bgsh/__ArchiveShader.bnsh) ----------
def read_bnsh(b):
    if b[:4] != b"BNSH": raise ValueError("not a BNSH file")
    u16 = lambda o: struct.unpack_from("<H", b, o)[0]
    u32 = lambda o: struct.unpack_from("<I", b, o)[0]
    i32 = lambda o: struct.unpack_from("<i", b, o)[0]
    u64 = lambda o: struct.unpack_from("<Q", b, o)[0] if o + 8 <= len(b) else 0
    def s(o):
        if not o or o + 2 > len(b): return None
        return b[o + 2:o + 2 + u16(o)].decode("utf-8", "replace")
    def dic(o):
        if not o or b[o:o + 4] != b"_DIC": return []
        return [s(u64(o + 8 + 16 * i + 8)) for i in range(1, u32(o + 4) + 1)]
    name = b[u32(0x10):b.find(b"\0", u32(0x10))].decode("ascii", "replace") if u32(0x10) < len(b) else ""
    g = u16(0x16)
    if b[g:g + 4] != b"grsc": raise ValueError("grsc section missing")
    api, apiver = u16(g + 0x10), u16(g + 0x12)
    nvar = u32(g + 0x1C); varr = u64(g + 0x20)
    STAGES = ("vertex", "hull", "domain", "geometry", "pixel", "compute")
    progs = []
    for v in range(nvar):
        vo = varr + 0x40 * v
        p = u64(vo + 0x10) or u64(vo + 8) or u64(vo)     # binary, else intermediate, else source program
        if not p: progs.append(None); continue
        prog = {"stages": {}}
        for k, st in enumerate(STAGES):
            c = u64(p + 8 + 8 * k)
            if c:
                prog["stages"][st] = {"code_bytes": u32(c + 0x18), "control_bytes": u32(c + 0x1C)}
        refl = u64(p + 0x78)
        if refl:
            for k, st in enumerate(STAGES):
                r = u64(refl + 8 * k)
                if not r: continue
                ins, outs, smp, cbs, uabs = dic(u64(r)), dic(u64(r + 8)), dic(u64(r + 16)), dic(u64(r + 24)), dic(u64(r + 32))
                offs = [i32(r + 40 + 4 * j) for j in range(4)]       # where outputs, samplers, constant buffers, UABs start in the slot array
                slot_arr = u64(r + 56)
                total = len(ins) + len(outs) + len(smp) + len(cbs) + len(uabs)
                slots = [i32(slot_arr + 4 * j) for j in range(total)] if slot_arr else []
                def with_slots(names, start):
                    return ", ".join("%s (slot %s)" % (n, slots[start + j] if start + j < len(slots) else "?") for j, n in enumerate(names))
                info = {}
                if ins: info["inputs"] = with_slots(ins, 0)
                if outs: info["outputs"] = with_slots(outs, offs[0])
                if smp: info["textures"] = with_slots(smp, offs[1])
                if cbs: info["constant_buffers"] = with_slots(cbs, offs[2])
                if uabs: info["storage_buffers"] = with_slots(uabs, offs[3])
                if info: prog["stages"].setdefault(st, {}).update(info)
        progs.append(prog)
    # group variations that have the same interface (names) - most differ only in compiled options
    groups = {}
    for i, pr in enumerate(progs):
        if pr is None: continue
        key = repr({st: {k: v for k, v in d.items() if isinstance(v, str)} for st, d in pr["stages"].items()})
        groups.setdefault(key, []).append(i)
    out = {"_binary_format": "BNSH shader container (%s)" % ("NVN" if api == 4 else "API %d" % api),
           "_note": "Compiled GPU shader programs (here: the UI layout shaders of this archive). Each variation is one "
                    "compiled combination of options (picked through the .bushvt table). The machine code can't be shown "
                    "as source; listed are each program's stages with their code sizes and its interface: vertex inputs, "
                    "outputs, textures and constant buffers by name with their binding slots.",
           "name": name, "api_version": apiver, "variations": nvar,
           "interfaces": [dict({"variations": ", ".join(map(str, idx))}, **{st: {k: v for k, v in d.items() if isinstance(v, str)} for st, d in progs[idx[0]]["stages"].items()})
                          for idx in groups.values()],
           "variation_code_bytes": ["%d: " % i + ", ".join("%s %d" % (st, d.get("code_bytes", 0)) for st, d in pr["stages"].items()) if pr else "%d: (none)" % i
                                    for i, pr in enumerate(progs)]}
    return out

# ---------- SHARCB (agl shader program archive, NVN: Lib/agl, Lib/gsys, Shader/Project) ----------
def read_sharcb(b):
    """Header 'BAHS' (SHAB): version, file size, ?, ?, data offset, code size, code offset. Names at 0x150
    (offsets below count from there). At data offset + 0x150: the compiled shaders (one per stage per
    variation), then the programs with their options."""
    if b[:4] != b"BAHS": raise ValueError("not a SHARCB file")
    u32 = lambda o: struct.unpack_from("<I", b, o)[0]
    ver, doff, csz, coff = u32(4), u32(0x14), u32(0x18), u32(0x1C)
    def s(off):
        a = 0x150 + off
        return b[a:b.find(b"\0", a)].decode("utf-8", "replace")
    REC_STAGE = {0: "vertex", 1: "fragment", 2: "geometry", 3: "compute"}
    GROUPS = ("uniform_blocks", "storage_buffers", "inputs_outputs", "uniforms", "samplers")
    o = doff + 0x150
    asz, cnt = u32(o), u32(o + 4)
    end, o = o + asz, o + 8
    recs, uniq = [], {}
    for i in range(cnt):
        sz, st, code = u32(o), u32(o + 4), u32(o + 0x10)
        ctl = o + 0x10 + struct.unpack_from("<i", b, o + 0x1C)[0]
        if b[ctl:ctl + 4] != b"\x34\x12\x76\x98": raise ValueError("shader %d: control data missing" % i)
        num = uniq.setdefault(ctl, len(uniq))
        res = {}
        for g, gname in enumerate(GROUPS):
            n, off = u32(o + 0x24 + 0xC * g), u32(o + 0x28 + 0xC * g)
            es, p, items = (16 if g < 2 else 12), o + 0x10 + off, []
            for k in range(n):
                q = p + k * es
                nm = s(u32(q))
                if g < 2: items.append("%s (binding %d, %d bytes)" % (nm, u32(q + 8), u32(q + 12)))
                elif g == 2: items.append("%s (location %d)" % (nm, u32(q + 8)))
                elif g == 3: items.append("%s (+%d)" % (nm, u32(q + 8)))
                else: items.append("%s (binding %d)" % (nm, u32(q + 8)))
            if items: res[gname] = items
        recs.append({"stage": REC_STAGE.get(st, "stage %d" % st), "num": num, "code": code,
                     "bytes": u32(ctl + 0x40), "regs": u32(ctl + 0x4C), "res": res})
        o += sz
    if o != end: raise ValueError("shader list size mismatch")
    psz, pc = u32(o), u32(o + 4)
    o += 8
    STAGE_BITS = ((1, "vertex"), (2, "fragment"), (4, "geometry"), (8, "compute"))
    progs, total_var = [], 0
    for i in range(pc):
        sz, nl, bits, base = u32(o), u32(o + 4), u32(o + 8), u32(o + 12)
        name = b[o + 16:o + 15 + nl].decode("utf-8", "replace")
        p, arrs = o + 16 + nl, []
        for k in range(3):
            asz2, ac = u32(p), u32(p + 4)
            q, items = p + 8, []
            for j in range(ac):
                isz, inl, nv = u32(q), u32(q + 4), u32(q + 8)
                nm = b[q + 16:q + 15 + inl].decode("utf-8", "replace")
                vals = [v.decode("utf-8", "replace") for v in b[q + 16 + inl:q + isz].split(b"\0")][:nv]
                items.append((nm, vals))
                q += isz
            arrs.append(items)
            p += asz2
        o += sz
        stages = [n for bit, n in STAGE_BITS if bits & bit]
        opts = arrs[0]
        nvar = 1
        for _, v in opts: nvar *= max(1, len(v))
        total_var += nvar
        prog = {"name": name, "stages": " + ".join(stages) or "none (%d)" % bits}
        if opts: prog["options"] = {nm: ", ".join(v) for nm, v in opts}
        for k, label in ((1, "other_options_1"), (2, "other_options_2")):
            if arrs[k]: prog[label] = {nm: ", ".join(v) for nm, v in arrs[k]}
        prog["variations"] = nvar
        mine = recs[base:base + nvar * len(stages)]
        distinct = {}
        for r in mine: distinct.setdefault(r["stage"], set()).add(r["num"])
        prog["distinct_shaders"] = ", ".join("%s %d" % (st, len(v)) for st, v in distinct.items())
        for st in dict.fromkeys(r["stage"] for r in mine):      # interface: everything any variation uses
            merged = {}
            for r in mine:
                if r["stage"] != st: continue
                for g, items in r["res"].items():
                    lst = merged.setdefault(g, [])
                    for it in items:
                        if it not in lst: lst.append(it)
            prog[st] = {g: ", ".join(v) for g, v in merged.items()} or "(no inputs, buffers or textures)"
        lines = []
        for v in range(nvar):
            # the first option changes slowest
            idx, rem = [], v
            for _, vals in reversed(opts):
                n = max(1, len(vals)); idx.append(vals[rem % n] if vals else "?"); rem //= n
            idx.reverse()
            label = ", ".join("%s=%s" % (nm, x) for (nm, _), x in zip(opts, idx)) or "(only one)"
            rs = mine[v * len(stages):(v + 1) * len(stages)]
            lines.append(label + ": " + ", ".join("%s #%d (%d bytes, %d registers)" % (r["stage"], r["num"], r["bytes"], r["regs"]) for r in rs))
        prog["variation_shaders"] = lines
        progs.append(prog)
    out = {"_binary_format": "SHARCB agl shader archive (NVN)",
           "_note": "The engine's own compiled GPU shader programs (lighting, shadows, post effects, voxel terrain...). "
                    "Each program has options (compile-time switches); every combination is a variation with its own "
                    "compiled shader per stage. Identical shaders are stored once and numbered (#N). The machine code "
                    "can't be shown as source; listed are each program's options, and per stage its uniform blocks "
                    "and storage buffers (binding, size), inputs and outputs (location), uniforms (offset in their block) and "
                    "textures (samplers, binding). Option order: the first one changes slowest.",
           "name": s(0), "version": ver, "programs": len(progs), "variations": total_var,
           "compiled_shaders": len(recs), "distinct_shaders": len(uniq), "code_bytes": csz,
           "program_list": progs}
    return out

# ---------- BFSHA (NintendoWare material shader archive, Switch-style 'FSHA    ') ----------
def read_bfsha(b):
    """Layout as in KillzXGaming's BfshaLibrary (versions 7-9): shading models with static / dynamic
    options, attributes, samplers, uniform blocks, programs (a key per program picks its option
    choices) and an embedded BNSH holding the compiled code."""
    if b[:8] != b"FSHA    ": raise ValueError("not a Switch BFSHA file")
    u8 = lambda o: b[o]
    u16 = lambda o: struct.unpack_from("<H", b, o)[0]
    i16 = lambda o: struct.unpack_from("<h", b, o)[0]
    u32 = lambda o: struct.unpack_from("<I", b, o)[0]
    i32 = lambda o: struct.unpack_from("<i", b, o)[0]
    u64 = lambda o: struct.unpack_from("<Q", b, o)[0]
    major = u16(0x0A)
    if major < 7: raise ValueError("version %d not supported" % major)
    def s(o):
        if not o or o + 2 > len(b): return ""
        return b[o + 2:o + 2 + u16(o)].decode("utf-8", "replace")
    def keys(d):
        if not d or d + 8 > len(b) or u32(d + 4) > 100000: return []
        return [s(u64(d + 8 + 16 * i + 8)) for i in range(1, u32(d + 4) + 1)]
    name, path = s(u64(0x38)), s(u64(0x40))
    mvals, mdict, nmodels = u64(0x48), u64(0x50), u16(0x78)
    MSIZE = 0x100 if major >= 8 else 0xF0
    models = []
    total_prog = 0
    for mi, mname in enumerate(keys(mdict) or [""] * nmodels):
        m = mvals + MSIZE * mi
        p = m + 8
        def pair():
            nonlocal p
            v, d = u64(p), u64(p + 8); p += 16
            return v, d
        st_v, st_d = pair(); dy_v, dy_d = pair(); at_v, at_d = pair(); sm_v, sm_d = pair()
        if major >= 8: pair()                                  # images
        ub_v, ub_d = pair()
        p += 8                                                 # uniform array
        sb_v, sb_d = pair(); p += 8
        prog_arr, key_tab = u64(p), u64(p + 8)
        bnsh_off = u64(p + 0x20)
        c = m + (0xD8 if major >= 8 else 0xC8)
        n_static, n_dyn, n_prog = u16(c + 0x0C), u16(c + 0x0E), u16(c + 0x10)
        skl, dkl = u8(c + 0x12), u8(c + 0x13)
        OPT = 40 if major >= 9 else 48
        def options(vals, d, dynamic):
            out, raw = {}, []
            for k, oname in enumerate(keys(d)):
                o = vals + OPT * k
                choices = keys(u64(o + 8))
                if major >= 9:
                    dflt, key_off = i16(o + 0x1A), u8(o + 0x1F)
                    mask, bidx, shift = u32(o + 0x20), u8(o + 0x24), u8(o + 0x25)
                else:
                    dflt, key_off = u8(o + 0x1B), u8(o + 0x1F)
                    bidx, shift, mask = u8(o + 0x20), u8(o + 0x21), u32(o + 0x22)
                dname = choices[dflt] if 0 <= dflt < len(choices) else None
                out[oname] = ", ".join(choices) + ("  (default %s)" % dname if dname is not None else "")
                raw.append((oname, choices, dname, bidx - (key_off if dynamic else 0), mask, shift))
            return out, raw
        static, st_raw = options(st_v, st_d, False)
        dyn, dy_raw = options(dy_v, dy_d, True)
        attrs = ["%s (location %d)" % (n, struct.unpack_from("<b", b, at_v + 2 * k + 1)[0]) for k, n in enumerate(keys(at_d))]
        samps = []
        for k, n in enumerate(keys(sm_d)):
            ann = s(u64(sm_v + 16 * k))
            samps.append(n + (" (%s)" % ann if ann and ann != n else ""))
        ROLES = ("material", "shape", "skeleton", "option")
        ubs = {}
        for k, n in enumerate(keys(ub_d)):
            o = ub_v + 0x20 * k
            unames = keys(u64(o + 8)); uarr = u64(o)
            us = []
            for j, un in enumerate(unames):
                off = u16(uarr + 16 * j + 0xC)          # stored as offset + 1; 0 = not in the buffer
                us.append("%s (+%d)" % (un, off - 1) if off else "%s (unused)" % un)
            typ = u8(o + 0x19)
            ubs[n] = {"bytes": u16(o + 0x1A)}
            if 1 <= typ <= 4: ubs[n]["role"] = ROLES[typ - 1]
            if us: ubs[n]["uniforms"] = ", ".join(us)
        sbs = keys(sb_d)
        # embedded BNSH: code sizes per variation
        var_bytes = {}
        try:
            if bnsh_off and b[bnsh_off:bnsh_off + 4] == b"BNSH":
                bs = u32(bnsh_off + 0x1C)
                info = read_bnsh(b[bnsh_off:bnsh_off + bs])
                for line in info.get("variation_code_bytes", []):
                    k, _, rest = line.partition(": ")
                    var_bytes[int(k)] = rest
        except Exception:
            pass
        progs = []
        PSZ = 0x40 if major >= 8 else 0x38
        for k in range(n_prog):
            po = prog_arr + PSZ * k
            voff = u64(po + (0x20 if major >= 8 else 0x18))
            vidx = (voff - (bnsh_off + 192)) // 64 if voff and bnsh_off else -1
            keyv = [i32(key_tab + 4 * ((skl + dkl) * k + j)) for j in range(skl + dkl)]
            diffs = []
            for oname, choices, dname, bi, mask, shift in st_raw:
                if 0 <= bi < skl:
                    ci = (keyv[bi] & mask) >> shift
                    ch = choices[ci] if ci < len(choices) else "?"
                    if ch != dname: diffs.append("%s=%s" % (oname, ch))
            for oname, choices, dname, bi, mask, shift in dy_raw:
                if 0 <= bi < dkl:
                    ci = (keyv[skl + bi] & mask) >> shift
                    ch = choices[ci] if ci < len(choices) else "?"
                    if ch != dname: diffs.append("%s=%s" % (oname, ch))
            line = "%d: %s" % (k, ", ".join(diffs) if diffs else "(all options at default)")
            if vidx in var_bytes: line += "  -> code: " + var_bytes[vidx]
            progs.append(line)
        total_prog += n_prog
        model = {"name": mname, "programs": n_prog,
                 "static_options": static}
        if dyn: model["dynamic_options"] = dyn
        if attrs: model["attributes"] = ", ".join(attrs)
        if samps: model["samplers"] = ", ".join(samps)
        if ubs: model["uniform_blocks"] = ubs
        if sbs: model["storage_buffers"] = ", ".join(sbs)
        model["program_list"] = progs
        models.append(model)
    return {"_binary_format": "BFSHA material shader archive (version %d.%d.%d)" % (major, b[9], b[8]),
            "_note": "The shaders that draw a group of materials. Each shading model has options (static: compiled in, "
                     "dynamic: switched at draw time) with their choices; a material picks choices and so one of the "
                     "compiled programs. Listed per model: options, vertex attributes, textures (samplers), uniform "
                     "blocks with each uniform's offset, and every program with the options it sets away from "
                     "their defaults plus its code size per stage. The GPU code itself can't be turned back into source.",
            "name": name, "path": path, "shading_models": len(models), "programs": total_prog, "models": models}

# ---------- GSH (Wii U GX2 shader, 'Gfx2' big-endian blocks) ----------
_GX2_TYPES = {0: "void", 1: "bool", 2: "int", 3: "uint", 4: "float", 9: "vec2", 10: "vec3", 11: "vec4",
              12: "bvec2", 13: "bvec3", 14: "bvec4", 15: "ivec2", 16: "ivec3", 17: "ivec4", 18: "uvec2",
              19: "uvec3", 20: "uvec4", 21: "mat2", 22: "mat2x3", 23: "mat2x4", 24: "mat3x2", 25: "mat3",
              26: "mat3x4", 27: "mat4x2", 28: "mat4x3", 29: "mat4"}
_GX2_SAMPLERS = {0: "sampler1D", 1: "sampler2D", 2: "sampler2DRect", 3: "sampler3D", 4: "samplerCube",
                 5: "sampler1DShadow", 6: "sampler2DShadow", 7: "sampler2DRectShadow", 8: "samplerCubeShadow",
                 9: "sampler1DArray", 10: "sampler2DArray", 11: "sampler1DArrayShadow", 12: "sampler2DArrayShadow",
                 13: "samplerCubeArray", 14: "samplerCubeArrayShadow", 15: "samplerBuffer"}
_GX2_BLOCKS = {1: "end of file", 2: "padding", 3: "vertex shader header", 5: "vertex shader program",
               6: "pixel shader header", 7: "pixel shader program", 8: "geometry shader header",
               9: "geometry shader program", 16: "geometry copy program", 20: "compute shader header",
               21: "compute shader program", 22: "user data"}

def read_gsh(b):
    """Wii U shader file: a 'Gfx2' header and 'BLK{' blocks (vertex/pixel/geometry header + program).
    Pointers inside a header block are offsets from the block's data, tagged 0xCA70.... / 0xD060...."""
    if b[:4] != b"Gfx2": raise ValueError("not a Gfx2 file")
    u32 = lambda o: struct.unpack_from(">I", b, o)[0]
    i32 = lambda o: struct.unpack_from(">i", b, o)[0]
    hsz, major, minor, gpu = u32(4), u32(8), u32(12), u32(16)
    o = hsz
    blocks, shaders = [], []
    while o + 0x20 <= len(b) and b[o:o + 4] == b"BLK{":
        bh, btype, dsz = u32(o + 4), u32(o + 16), u32(o + 20)
        d = o + bh
        blocks.append("%s (%d bytes)" % (_GX2_BLOCKS.get(btype, "type %d" % btype), dsz))
        if btype in (3, 6, 8):
            def ptr(p): return d + (u32(p) & 0x000FFFFF)
            def name(p):
                a = ptr(p); e = b.find(b"\0", a)
                return b[a:e].decode("ascii", "replace")
            stage = {3: "vertex", 6: "pixel", 8: "geometry"}[btype]
            regs = {3: 0xD0, 6: 0xA4, 8: 0x4C}[btype]
            h = d + regs
            sh = {"stage": stage, "code_bytes": u32(h)}
            if btype == 8: h += 8                                 # geometry: copy-program size + pointer
            mode = u32(h + 8)
            sh["mode"] = {0: "uniform registers", 1: "uniform blocks", 2: "uniform blocks (geometry)"}.get(mode, str(mode))
            nub, pub, nuv, puv = u32(h + 12), h + 16, u32(h + 20), h + 24
            nlp, nsm, psm = u32(h + 36), u32(h + 44), h + 48
            ubs = []
            for k in range(nub):
                e = ptr(pub) + 12 * k
                ubs.append("%s (slot %d, %d bytes)" % (name(e), u32(e + 4), u32(e + 8)))
            uvs = []
            for k in range(nuv):
                e = ptr(puv) + 20 * k
                t, cnt, off, blk = u32(e + 4), u32(e + 8), u32(e + 12), i32(e + 16)
                uvs.append("%s %s%s (%s%d)" % (_GX2_TYPES.get(t, "type%d" % t), name(e), "[%d]" % cnt if cnt > 1 else "",
                                               "block %d +" % blk if blk >= 0 else "register ", off))
            sms = []
            for k in range(nsm):
                e = ptr(psm) + 12 * k
                sms.append("%s %s (slot %d)" % (_GX2_SAMPLERS.get(u32(e + 4), "sampler"), name(e), u32(e + 8)))
            if ubs: sh["uniform_blocks"] = ", ".join(ubs)
            if uvs: sh["uniforms"] = ", ".join(uvs)
            if sms: sh["samplers"] = ", ".join(sms)
            if btype == 3:
                nat, pat = u32(h + 52), h + 56
                ats = []
                for k in range(nat):
                    e = ptr(pat) + 16 * k
                    t, cnt, loc = u32(e + 4), u32(e + 8), i32(e + 12)
                    ats.append("%s %s (location %d)" % (_GX2_TYPES.get(t, "type%d" % t), name(e), loc))
                if ats: sh["attributes"] = ", ".join(ats)
            if nlp: sh["loop_constants"] = nlp
            shaders.append(sh)
        o = d + dsz
        if btype == 1: break
    return {"_binary_format": "GSH Wii U shader (Gfx2 %d.%d)" % (major, minor),
            "_note": "A compiled shader for the Wii U's GPU (GX2), apparently left over from the engine's Wii U "
                     "days. Listed per stage: attributes, uniforms (type, name, where they live), uniform blocks and "
                     "textures (samplers). The R7xx machine code itself isn't disassembled.",
            "gpu_version": gpu, "shaders": shaders, "blocks": blocks}

# ---------- NDSR pack (agl_ndsr.pck: GPU shaders + weight tables, probably a neural upscaler) ----------
def read_ndsr(b):
    """'NDSR', u32 version, BOM, then 16-byte entries from 0x10: u32 descriptor offset, u16 kind,
    u16 0, u32 data offset, u32 (size/16 << 8 | type). Shader entries' descriptors are NVN control
    sections (0x98761234), their data is GPU code; type 0x0b entries are data buffers (weights?)."""
    if b[:4] != b"NDSR": raise ValueError("not an NDSR pack")
    u16 = lambda o: struct.unpack_from("<H", b, o)[0]
    u32 = lambda o: struct.unpack_from("<I", b, o)[0]
    TYPES = {1: "vertex shader", 4: "fragment shader", 7: "compute shader", 0x0a: "table", 0x0b: "data buffer"}
    o, items, counts, bufs, sizes = 0x10, [], {}, {}, {}
    while o + 16 <= min(len(b), 0x10 + 16 * 4096):
        desc, kind, data, sz = u32(o), u16(o + 4), u32(o + 8), u32(o + 12)
        if not (desc or kind or data or sz): break
        typ, nbytes = sz & 0xFF, (sz >> 8) * 16
        tname = TYPES.get(typ, "type %d" % typ)
        line = "%d: %s, %d bytes at 0x%x" % (len(items), tname, nbytes, data)
        if b[desc:desc + 4] == b"\x34\x12\x76\x98":
            line += ", %d registers" % u32(desc + 0x4C)
        elif typ == 0x0b:
            dims = [u32(desc + 4 * k) for k in range(20)] if desc + 80 <= len(b) else []
            if dims: line += ", header values %s" % " ".join(str(x) for x in dims).rstrip(" 0")
            bufs[data] = bufs.get(data, 0) + 1
        counts[tname] = counts.get(tname, 0) + 1
        sizes[tname] = sizes.get(tname, 0) + (nbytes if typ != 0x0b or bufs.get(data) == 1 else 0)
        items.append(line)
        o += 16
    return {"_binary_format": "NDSR GPU pack (version %d.%d)" % (u32(4) >> 8, u32(4) & 0xFF),
            "_note": "A pack of compiled GPU programs plus data buffers that the compute shaders read in turn. "
                     "Its layout (a chain of compute passes, each reading a slice of a data buffer) looks like a "
                     "neural network, so this is probably the engine's image upscaler; the name 'ndsr' isn't "
                     "explained anywhere in the game. The code can't be turned back into source and the data "
                     "buffers are raw numbers.",
            "entries": len(items),
            "summary": ", ".join("%d %s%s (%d bytes)" % (n, t, "s" if n > 1 else "", sizes[t]) for t, n in counts.items()),
            "distinct_data_buffers": len(bufs),
            "list": items}

_SPV_BLOB = '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'
# ---------- SPIR-V disassembler (small; tables from Khronos SPIRV-Headers) ----------
_SPV_TABLE = None
def _spv_table():
    global _SPV_TABLE
    if _SPV_TABLE is None:
        import base64, zlib
        t = json.loads(zlib.decompress(base64.b64decode(_SPV_BLOB)))
        t["ops"] = {int(k): v for k, v in t["ops"].items()}
        t["kinds"] = {k: (c, {int(v): e for v, e in en.items()}) for k, (c, en) in t["kinds"].items()}
        t["glsl"] = {int(k): v for k, v in t["glsl"].items()}
        _SPV_TABLE = t
    return _SPV_TABLE

def spirv_disassemble(words):
    """-> list of text lines like spirv-dis: '%name = OpX %a %b'. words: list of u32 (starting with the magic)."""
    T = _spv_table()
    ops, kinds = T["ops"], T["kinds"]
    if not words or words[0] != 0x07230203: return ["(not SPIR-V)"]
    bound = words[3]
    names, types, float_types, ext_sets = {}, {}, set(), {}
    # pass 1: names, float types, ext imports
    i = 5
    while i < len(words):
        wc, op = words[i] >> 16, words[i] & 0xFFFF
        if wc == 0: break
        ins = words[i:i + wc]
        if op == 5 and len(ins) > 2:                         # OpName
            names[ins[1]] = _spv_str(ins[2:])[0]
        elif op == 22 and len(ins) > 2:                      # OpTypeFloat
            float_types.add(ins[1])
        elif op == 11 and len(ins) > 2:                      # OpExtInstImport
            ext_sets[ins[1]] = _spv_str(ins[2:])[0]
        i += wc
    used = {}
    def idn(x):
        n = names.get(x)
        if n:
            n = "".join(c if c.isalnum() or c == "_" else "_" for c in n) or str(x)
            if used.setdefault(n, x) != x: n = "%s_%d" % (n, x)
            return "%" + n
        return "%" + str(x)
    in_func = False
    out = ["; SPIR-V %d.%d, generator 0x%08x, bound %d" % ((words[1] >> 16) & 255, (words[1] >> 8) & 255, words[2], bound)]
    i = 5
    while i < len(words):
        wc, op = words[i] >> 16, words[i] & 0xFFFF
        if wc == 0: break                                   # padding after the module
        if i + wc > len(words):
            if not any(l == "OpFunctionEnd" for l in out): out.append("; (stopped: bad instruction length)")
            break
        ins = words[i + 1:i + wc]
        info = ops.get(op)
        if not info:
            out.append("Op%d %s" % (op, " ".join(str(w) for w in ins))); i += wc; continue
        name, operands = info
        res, rtype, args, p = None, None, [], 0
        def take_kind(kind):
            nonlocal p
            if p >= len(ins): return None
            if kind in ("IdResultType",):
                v = ins[p]; p += 1; return ("type", v)
            if kind == "IdResult":
                v = ins[p]; p += 1; return ("res", v)
            if kind.startswith("Id"):
                v = ins[p]; p += 1; return idn(v)
            if kind == "LiteralString":
                s, n = _spv_str(ins[p:]); p += n; return '"%s"' % s
            if kind == "LiteralContextDependentNumber":
                v = ins[p]; p += 1
                if rtype in float_types: return "%g" % struct.unpack("<f", struct.pack("<I", v))[0]
                return str(v if v < 0x80000000 else v - 0x100000000)
            if kind == "LiteralExtInstInteger":
                v = ins[p]; p += 1
                return T["glsl"].get(v, str(v)) if ext_sets.get(ins[1] if len(ins) > 1 else -1, "").startswith("GLSL") else str(v)
            if kind.startswith("Literal"):
                v = ins[p]; p += 1; return str(v)
            if kind.startswith("Pair"):
                a, b = ins[p], ins[p + 1]; p += 2
                return "%s %s" % (idn(a) if "IdRef" in kind.split("Pair")[1][:5] else a, idn(b))
            if kind in kinds:
                cat, en = kinds[kind]
                v = ins[p]; p += 1
                if cat == "BitEnum":
                    if v == 0: return en.get(0, ["None"])[0]
                    parts, extra = [], []
                    for bit in sorted(en):
                        if bit and v & bit:
                            parts.append(en[bit][0]); extra += en[bit][1]
                    s = "|".join(parts) or hex(v)
                    for k2 in extra:
                        r = take_kind(k2)
                        if r is not None: s += " " + (r if isinstance(r, str) else str(r[1]))
                    return s
                e = en.get(v)
                if not e: return str(v)
                s = e[0]
                for k2 in e[1]:
                    r = take_kind(k2)
                    if r is not None: s += " " + (r if isinstance(r, str) else str(r[1]))
                return s
            v = ins[p]; p += 1; return str(v)
        for kind, q in operands:
            while True:
                if p >= len(ins): break
                r = take_kind(kind)
                if r is None: break
                if isinstance(r, tuple):
                    if r[0] == "type": rtype = r[1]; args.append(idn(r[1]))
                    else: res = r[1]
                else:
                    args.append(r)
                if q != "*": break
        if p < len(ins): args += [str(w) for w in ins[p:]]
        line = ("%s = " % idn(res) if res is not None else "") + name + (" " + " ".join(args) if args else "")
        if name == "OpFunction": out.append(""); in_func = True
        out.append(("    " if in_func and name not in ("OpFunction", "OpFunctionEnd", "OpLabel") else "") + line)
        if name == "OpFunctionEnd": in_func = False
        i += wc
    return out

def _spv_str(ws):
    bb = b"".join(struct.pack("<I", w) for w in ws)
    e = bb.find(b"\0")
    s = bb[:e if e >= 0 else len(bb)].decode("utf-8", "replace")
    return s, (len(s.encode()) // 4) + 1


# ---------- sead primitive renderer shaders (Lib/sead/primitive_renderer/*_shader.bin) ----------
def read_sead_shader_bin(b):
    """Header: N u32 offsets of NVN control sections, then N u32 offsets of their GPU code. NVN (Switch 1)
    controls carry the NVIDIA assembly text; NVN2 (Switch 2) controls carry the SPIR-V the code was built from."""
    u32 = lambda o: struct.unpack_from("<I", b, o)[0]
    first = u32(0)
    if first % 4 or first < 8 or first > 0x100 or b[first:first + 4] != b"\x34\x12\x76\x98": raise ValueError("no NVN control data")
    n = first // 8
    ctls = [u32(4 * k) for k in range(n)]
    codes = [u32(4 * (n + k)) for k in range(n)]
    ends = sorted(set(codes + [len(b)]))
    shaders = []
    for k, (c, co) in enumerate(zip(ctls, codes)):
        sh = {"control_at": "0x%x" % c, "code_at": "0x%x" % co}
        nxt = min([e for e in ends if e > co] or [len(b)])
        sh["code_bytes_with_padding"] = nxt - co
        if u32(c + 4) == 1:                                   # NVN (Switch 1): assembly text in the control section
            to, tl = u32(c + 0x14), u32(c + 0x18)
            txt = b[c + to:c + to + tl].split(b"\0")[0].decode("ascii", "replace")
            sh["stage"] = "vertex" if txt.startswith("!!NVvp") else "fragment" if txt.startswith("!!NVfp") else \
                          "geometry" if txt.startswith("!!NVgp") else "compute" if txt.startswith("!!NVcp") else "?"
            sh["assembly"] = txt.replace("\r", "").split("\n")
        else:                                                 # NVN2 (Switch 2): SPIR-V in the control section
            sh["code_bytes"] = u32(c + 0x40); sh["registers"] = u32(c + 0x4C)
            so, sl = u32(c + 0xBC) + 0x50, u32(c + 0xC0) - 0x50
            if b[c + so:c + so + 4] != b"\x03\x02\x23\x07":       # not where expected: search the control section
                p = b.find(b"\x03\x02\x23\x07", c, nxt)
                so, sl = (p - c, (ctls[k + 1] if k + 1 < n else co) - p) if p >= 0 else (0, 0)
            if sl > 20:
                words = list(struct.unpack_from("<%dI" % (sl // 4), b, c + so))
                lines = spirv_disassemble(words)
                for ln in lines:
                    if ln.startswith("OpEntryPoint "): sh["stage"] = ln.split()[1].lower()
                sh["spirv"] = lines
        shaders.append(sh)
    api = "NVN2 (Switch 2), with SPIR-V" if any("spirv" in s for s in shaders) else "NVN (Switch 1), with NVIDIA assembly"
    return {"_binary_format": "sead primitive renderer shaders (%s)" % api,
            "_note": "The shaders sead (Nintendo's base library) uses to draw simple debug shapes - lines, boxes, spheres "
                     "in one colour. Unlike most game shaders these still carry readable code: the Switch 1 version "
                     "keeps the NVIDIA assembly it was compiled from, the Switch 2 version keeps the SPIR-V (shown "
                     "disassembled; names like BlockWVP / ColorRate are the original GLSL names).",
            "shaders": shaders}


# ---------- AGST sound group settings (Sound/Group/GroupSetting.Product.*.bagst) ----------
def read_agst(b):
    """'AGST', BOM, u16 version, u32 size, then 7 section offsets: TREE (group hierarchy), GRP (one record per
    group), MAND (duckers, BYML), PASE (pause targets, BYML), GCCD (BYML), BFIL (filters, BYML), STRG (names)."""
    if b[:4] != b"AGST": raise ValueError("not AGST")
    u32 = lambda o: struct.unpack_from("<I", b, o)[0]
    f32 = lambda o: struct.unpack_from("<f", b, o)[0]
    ver = struct.unpack_from("<H", b, 6)[0]
    secs = {}
    for i in range(7):
        a = u32(0xC + 4 * i)
        if a + 8 <= len(b): secs[b[a:a + 4].decode("ascii", "replace").strip()] = a
    S = secs.get("STRG", 0) + 8
    def s(o):
        e = b.find(b"\0", S + o)
        return b[S + o:e].decode("utf-8", "replace") if e >= 0 else "?"
    def inl(a):
        e = b.find(b"\0", a)
        return b[a:e].decode("utf-8", "replace")
    # group records (index = tree index)
    names, groups = [], []
    g = secs.get("GRP")
    recs = []
    if g is not None:
        n = u32(g + 8)
        offs = [u32(g + 12 + 4 * k) for k in range(n)]
        for k, a in enumerate(offs):
            nm = s(u32(a))
            names.append(nm)
            rec = {"name": nm, "volume": round(f32(a + 0x10), 4), "value_2?": round(f32(a + 0x14), 4)}
            p = a + 180                                      # fixed part, then lists
            sends = []
            sz, cnt = u32(p), u32(p + 4)
            for j in range(min(cnt, 64)):
                e = p + 8 + 8 * j
                sends.append("%s %g" % (inl(e + u32(e)), round(f32(e + 4), 4)))
            if sends: rec["sends_to"] = ", ".join(sends)
            p += 4 + sz
            sz2 = u32(p)
            if sz2 > 4 and p + 4 + sz2 <= len(b):
                other = [x.decode("utf-8", "replace") for x in re.findall(rb"[\x20-\x7e]{3,}", b[p + 4:p + 4 + sz2])]
                if other: rec["also_names?"] = ", ".join(other)
            recs.append((a, rec))
        # fixed fields that differ from the usual value
        if recs:
            nw = 45
            cols = [[u32(a + 4 * i) for a, _ in recs] for i in range(nw)]
            common = [max(set(c), key=c.count) for c in cols]
            for (a, rec) in recs:
                diff = []
                for i in range(1, nw):
                    if i in (4, 5): continue
                    v = u32(a + 4 * i)
                    if v != common[i]:
                        fv = struct.unpack("<f", struct.pack("<I", v))[0]
                        diff.append("+0x%x=%s" % (4 * i, ("%g" % fv) if 0.0001 < abs(fv) < 100000 else str(v if v < 0x80000000 else v - 0x100000000)))
                if diff: rec["other_fields_unusual"] = " ".join(diff)
    # tree: node = u32 size, u32 index, u32 child count, children...
    paths = {}
    t = secs.get("TREE")
    if t is not None:
        def node(p, parent, depth):
            if depth > 32 or p + 12 > len(b): return
            idx, nc = u32(p + 4), u32(p + 8)
            nm = names[idx] if idx < len(names) else "#%d" % idx
            paths[idx] = (parent + "/" + nm) if parent else nm
            q = p + 12
            for _ in range(min(nc, 512)):
                node(q, paths[idx], depth + 1)
                q += u32(q)
        node(t + 0xC, "", 0)
    for k, (a, rec) in enumerate(recs):
        if k in paths: rec["path"] = paths[k]
        groups.append(rec)
    out = {"_binary_format": "AGST sound group settings (version %d)" % ver,
           "_note": "The sound mixer's groups: every group (BGM, voices, 3D sounds, ...) in its tree with its volume and "
                    "which buses it sends to (Main, IndirectSound = reverb, FinalMix...); the duckers - rules that turn "
                    "groups down while others play (names are the developers' Japanese notes); the groups paused by "
                    "each pause type; and filter tables. 'other_fields_unusual' lists a group's settings that differ "
                    "from most groups (offset in the record = value) where their meaning isn't known yet.",
           "groups": len(groups), "group_list": groups}
    for tag, key in (("MAND", "duckers"), ("PASE", "pause_settings"), ("GCCD", "gccd"), ("BFIL", "filters")):
        a = secs.get(tag)
        if a is None: continue
        sz = u32(a + 4)
        try:
            v = read_byml(b[a + 8:a + 8 + sz])
        except Exception as e:
            v = "couldn't be read: %s" % e
        if tag == "MAND" and isinstance(v, dict):
            summ = []
            for nm, d in v.items():
                tg = d.get("Targets", []) if isinstance(d, dict) else []
                summ.append("%s: %s" % (nm, ", ".join("%s to %s" % (x.get("TargetGroupName"), x.get("Volume")) for x in tg if isinstance(x, dict))))
            out["duckers_summary"] = summ
        if tag == "PASE" and isinstance(v, dict):
            out["pause_summary"] = ["%s: %s" % (nm, ", ".join(x.get("TargetGroupName", "?") for x in d.get("Targets", []) if isinstance(x, dict)))
                                    for nm, d in v.items() if isinstance(d, dict)]
        if v not in ({}, None, []): out[key] = v
    return out

# ---------- XLNK (xlink2 effect / sound link database: ELink2/*.belnk, SLink2/*.bslnk) ----------
def read_xlnk(b, max_users=None):
    """Layout as in dt-12345's xlink2 tool (TotK-era, 64-bit, version 0x24 ELink / 0x21-0x22 SLink).
    Per user (an actor or system that plays effects): its actions (animation names etc.), which assets each
    action triggers, and every asset with its parameters (the effect / sound name among them)."""
    if b[:4] != b"XLNK": raise ValueError("not XLNK")
    u16 = lambda o: struct.unpack_from("<H", b, o)[0]
    i16 = lambda o: struct.unpack_from("<h", b, o)[0]
    u32 = lambda o: struct.unpack_from("<I", b, o)[0]
    i32 = lambda o: struct.unpack_from("<i", b, o)[0]
    u64 = lambda o: struct.unpack_from("<Q", b, o)[0]
    f32 = lambda o: struct.unpack_from("<f", b, o)[0]
    size, ver = u32(4), u32(8)
    if ver < 0x21: raise ValueError("version 0x%x: only the 64-bit (TotK-era) layout is read" % ver)
    kind = "ELink (effects)" if ver >= 0x23 else "SLink (sounds)"
    n_loc_prop, n_loc_enum = u32(0x28), u32(0x2C)
    n_direct, n_random, n_curve = u32(0x30), u32(0x34), u32(0x38)
    loc_prop_pos = u64(0x20)
    n_user = u32(0x48)
    name_pos = u64(0x58)
    names = {}
    p = name_pos
    while p < size:
        e = b.find(b"\0", p, size)
        if e < 0: break
        names[p - name_pos] = b[p:e].decode("utf-8", "replace")
        p = e + 1
    S = lambda off: names.get(off, "?")
    hashes = [u32(0x60 + 4 * k) for k in range(n_user)]
    p = (0x60 + 4 * n_user + 7) & ~7
    uoffs = [u64(p + 8 * k) for k in range(n_user)]
    pdt = (p + 8 * n_user + 7) & ~7
    pdt_size, n_up, n_ap, n_cap, n_tp = u32(pdt), u32(pdt + 4), u32(pdt + 8), u32(pdt + 12), u32(pdt + 16)
    asset_param_pos = (pdt + pdt_size + 7) & ~7
    defs_pos = pdt + 0x18
    pdt_names_pos = defs_pos + (n_up + n_ap + n_tp) * 0x18
    pdt_names = {}
    q = pdt_names_pos
    while q < pdt + pdt_size:
        e = b.find(b"\0", q, pdt + pdt_size)
        if e < 0: break
        pdt_names[q - pdt_names_pos] = b[q:e].decode("utf-8", "replace"); q = e + 1
    TYPES = {0: "int", 1: "float", 2: "bool", 3: "enum", 4: "string", 5: "custom"}
    def pdef(i):
        o = defs_pos + 0x18 * i
        return pdt_names.get(u64(o), "?"), u32(o + 8)
    asset_defs = [pdef(n_up + i) for i in range(n_ap)]
    direct_pos = loc_prop_pos + 8 * (n_loc_prop + n_loc_enum)
    def param_value(packed, typ):
        v, ref = packed & 0xFFFFFF, packed >> 24
        if ref == 0:
            if typ == 1: return round(f32(direct_pos + 4 * v), 4)
            if typ == 2: return u32(direct_pos + 4 * v) != 0
            return i32(direct_pos + 4 * v)
        if ref == 1: return S(v)
        if ref == 2: return "curve #%d" % v
        if ref == 3 or ref >= 6: return "random #%d (%s)" % (v, {3: "linear", 6: "inflected x2", 7: "inflected x3", 8: "inflected x4", 9: "inflected x1.5", 10: "increasing x2", 11: "increasing x3", 12: "increasing x4", 13: "increasing x1.5"}.get(ref, "type %d" % ref))
        if ref == 4: return "arrange #%d" % v
        if ref == 5:                                   # bit field: a u32 from the direct value table
            bits = u32(direct_pos + 4 * v)
            return "bits %s" % ("+".join(str(i) for i in range(32) if bits >> i & 1) or "none")
        return "ref%d:%d" % (ref, v)
    # user names: crc32 of a name in the name table
    import zlib
    by_hash = {}
    for s in names.values():
        by_hash.setdefault(zlib.crc32(s.encode()), s)
    users = []
    asset_name_counts = {}
    for k in range(n_user if max_users is None else min(n_user, max_users)):
        base = uoffs[k]
        n_lp = i16(base + 4)
        n_ct, n_as, n_rc = u32(base + 8), u32(base + 12), u32(base + 16)
        n_slot, n_act, n_trig = u32(base + 20), u32(base + 24), u32(base + 28)
        n_prop, n_ptrig, n_atrig = u32(base + 32), u32(base + 36), u32(base + 40)
        trig_tab = base + u64(base + 48)
        q = base + 0x38
        local_props = [S(u64(q + 8 * i)) for i in range(max(0, n_lp))]
        q += 8 * max(0, n_lp)
        q += 4 * n_up                                       # user params
        q += 2 * n_ct; q = (q + 3) & ~3                      # sorted call table indices
        ct_pos = q
        cont_pos = ct_pos + 0x30 * n_ct
        cts = []
        for i in range(n_ct):
            o = ct_pos + 0x30 * i
            key = S(u64(o)); flags = u16(o + 10)
            entry = {"key": key}
            if flags & 1:
                entry["container"] = True
            else:
                ap = asset_param_pos + u64(o + 0x20)
                mask = u64(ap); j = 0; params = {}
                for bit in range(n_ap):
                    if mask >> bit & 1:
                        nm, typ = asset_defs[bit]
                        params[nm] = param_value(u32(ap + 8 + 4 * j), typ); j += 1
                entry["params"] = params
                for nm in ("AssetName", "EsetName", "ResName", "SeName", "AssetKey"):
                    if isinstance(params.get(nm), str):
                        asset_name_counts[params[nm]] = asset_name_counts.get(params[nm], 0) + 1
                        break
            cts.append(entry)
        # actions: slot -> actions -> triggers -> call table
        act_pos = trig_tab + 0x10 * n_slot
        atr_pos = act_pos + 0x10 * n_act
        actions = []
        for i in range(n_slot):
            o = trig_tab + 0x10 * i
            slot = S(u64(o)); a0, a1 = i16(o + 8), i16(o + 10)
            if a0 < 0: continue
            for a in range(a0, min(n_act - 1, a1) + 1):
                ao = act_pos + 0x10 * a
                aname = S(u64(ao)); t0, t1 = i16(ao + 8), i32(ao + 12)
                plays = []
                for t in range(max(0, t0), min(n_trig - 1, t1) + 1) if t1 >= t0 else ():
                    to = atr_pos + 0x28 * t
                    cto = u64(to + 8)
                    ci = cto // 0x30 if cto % 0x30 == 0 and cto // 0x30 < n_ct else None
                    key = cts[ci]["key"] if ci is not None else "?"
                    fl, sf, ef = u16(to + 0x1C), u32(to + 0x10), i32(to + 0x18)
                    if fl & (1 << 2): key += " (always)"
                    elif fl & (1 << 3): key += " (on leave)"
                    elif fl & (1 << 4): key += " (after %s)" % S(u64(to + 0x10))
                    else:
                        if ef < 0 or ef >= 0x7FFFFFFF: ef = None
                        if ef is not None: key += " (frames %d-%d)" % (sf, ef)
                        elif sf: key += " (from frame %d)" % sf
                    plays.append(key)
                actions.append("%s / %s: %s" % (slot, aname, ", ".join(plays) if plays else "-"))
        u = {"name": by_hash.get(hashes[k], "0x%08x" % hashes[k]),
             "counts": "%d assets, %d actions, %d property triggers, %d always triggers" % (n_ct, n_act, n_ptrig, n_atrig)}
        if local_props: u["local_properties"] = ", ".join(local_props)
        if actions: u["actions"] = actions
        u["assets"] = ["%s%s" % (c["key"], " (container)" if c.get("container") else ": " + ", ".join(
            "%s=%s" % (kk, vv) for kk, vv in c["params"].items())) for c in cts]
        users.append(u)
    return {"_binary_format": "XLNK %s database (version 0x%x)" % (kind, ver),
            "_note": "xlink2: which effects (ELink) or sounds (SLink) each 'user' - an actor, object or system - plays. "
                     "'actions' are the user's action slots and actions (usually animation / state names) with the "
                     "assets each one triggers; 'assets' lists every asset key with its parameters (the effect or sound "
                     "name, scale, colour, bone...). Containers (random / switch / blend groups) are marked but their "
                     "contents aren't expanded yet. Layout from dt-12345's xlink2 tool.",
            "users": n_user,
            "asset_parameters": ", ".join("%s (%s)" % (nm, TYPES.get(t, t)) for nm, t in asset_defs),
            "user_list": users}

# ---------- RESTBL (System/Resource/ResourceSizeTable: how much memory each file needs) ----------
def read_restbl(b, names=None):
    """'RESTBL', u32 version, u32 string size, u32 hash entries, u32 name entries; hash entries are
    (crc32 of the romfs path without .zs, size) sorted by hash; name entries are (path, size) for paths
    whose hashes collide. names: optional {crc32: path} built from the scanned dump to name the hashes."""
    if b[:6] != b"RESTBL": raise ValueError("not RESTBL")
    ver, ssz, nc, nn = struct.unpack_from("<4I", b, 6)
    p = 0x16
    pairs = struct.unpack_from("<%dI" % (2 * nc), b, p) if nc else ()
    p += 8 * nc
    named = []
    for i in range(nn):
        s = b[p:p + ssz].split(b"\0")[0].decode("utf-8", "replace")
        named.append((s, struct.unpack_from("<I", b, p + ssz)[0])); p += ssz + 4
    files, unresolved = {}, 0
    for i in range(nc):
        h, sz = pairs[2 * i], pairs[2 * i + 1]
        nm = names.get(h) if names else None
        if nm: files[nm] = sz
        else: unresolved += 1
    for s, sz in named: files[s] = sz
    total = sum(pairs[1::2]) + sum(sz for _, sz in named)
    out = {"_binary_format": "RESTBL resource size table (version %d)" % ver,
           "_note": "How many bytes of memory the game sets aside to load each file (its size once decompressed, plus "
                    "some extra for some kinds). Files are listed by a hash of their path; the names come from matching "
                    "those hashes against the files of this scan, so files the scan didn't include can't be named - "
                    "scan the whole dump (base and update) to name the most. Paths are without .zs.",
           "entries": nc + nn, "named": len(files), "not_named": unresolved,
           "total_bytes": total}
    if files:
        by = {}
        for path, sz in files.items():
            top = path.split("/")[0]
            c = by.setdefault(top, [0, 0]); c[0] += 1; c[1] += sz
        out["by_folder"] = ["%s: %d files, %.1f MB" % (k, v[0], v[1] / 1048576) for k, v in sorted(by.items(), key=lambda kv: -kv[1][1])]
        out["largest"] = ["%s: %.2f MB" % (k, v / 1048576) for k, v in sorted(files.items(), key=lambda kv: -kv[1])[:100]]
        out["files"] = dict(sorted(files.items()))
    if named: out["collision_names"] = ["%s: %d" % x for x in named]
    if names is None:
        out["_names_pending"] = "Names are added at the end of the scan."
    return out



# ---------------- Mario Kart World formats ----------------
def _zstd_dec(raw, size):
    return zstandard.ZstdDecompressor().decompress(raw, max_output_size=max(size, 1))


_TARB_KIND = {"hght": "height map", "norm": "normal map", "mate": "material map", "extm": "extra map layer"}



def _nvn2_deswizzle_bl(data, w, h, bpp, bh):
    """Block-linear (GOB) layout used by NVN2 textures in Mario Kart World, block height bh GOBs."""
    rb = w * bpp
    gx = (rb + 63) // 64
    out = bytearray(rb * h)
    for y in range(h):
        yi = y & 7
        by = (y // (8 * bh)) * gx * bh * 512 + ((y % (8 * bh)) // 8) * 512
        row = y * rb
        for xb in range(rb):
            xi = xb & 63
            a = by + (xb // 64) * bh * 512 + ((xi & 15) | ((yi & 1) << 4) | (((yi >> 1) & 1) << 5) |
                                              (((xi >> 4) & 1) << 6) | (((yi >> 2) & 1) << 7) | (((xi >> 5) & 1) << 8))
            if a < len(data):
                out[row + xb] = data[a]
    return bytes(out)


def _tarb_heights(data):
    """hght tile: second section = 257 x 257 16-bit heights (block-linear, 1 GOB tall blocks)."""
    w = struct.unpack_from("<I", data, 0x0C)[0]
    off2 = 0x2C + struct.unpack_from("<I", data, 0x2C)[0]
    size2 = struct.unpack_from("<I", data, 0x30)[0]
    if w != 257 or off2 + size2 > len(data):
        return None
    px = _nvn2_deswizzle_bl(data[off2:off2 + size2], w, w, 2, 1)
    hs = struct.unpack_from("<%dH" % (w * w), px, 0)
    lo, hi = min(hs), max(hs)
    ramp = " .:-=+*#%@"
    rows = []
    for r in range(32):
        y = min(w - 1, int((r + 0.5) * w / 32))
        line = ""
        for c in range(64):
            x = min(w - 1, int((c + 0.5) * w / 64))
            v = hs[y * w + x]
            line += ramp[0 if hi == lo else min(9, (v - lo) * 10 // (hi - lo + 1))]
        rows.append(line)
    return {"grid": "%d x %d" % (w, w), "lowest": lo, "highest": hi,
            "lowest_height": round(lo / 65535.0 * 2000, 2), "highest_height": round(hi / 65535.0 * 2000, 2),
            "picture": rows}


_RAMP = " .:-=+*#%@"


def _ascii_preview(vals, w, h, lo=None, hi=None, cols=64, rows=32):
    lo = min(vals) if lo is None else lo
    hi = max(vals) if hi is None else hi
    out = []
    for r in range(rows):
        y = min(h - 1, int((r + 0.5) * h / rows))
        line = ""
        for c in range(cols):
            x = min(w - 1, int((c + 0.5) * w / cols))
            v = vals[y * w + x]
            line += _RAMP[0 if hi <= lo else max(0, min(9, int((v - lo) * 10 // (hi - lo + 1e-9))))]
        out.append(line)
    return out


def _bc1_decode(blk, bw, bh_):
    """BC1 blocks (bw x bh_ blocks, already in row order) -> list of (r, g, b)."""
    w, h = bw * 4, bh_ * 4
    px = [(0, 0, 0)] * (w * h)
    for by in range(bh_):
        for bx in range(bw):
            o = (by * bw + bx) * 8
            c0, c1, idx = struct.unpack_from("<HHI", blk, o)
            p = [(((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31) for c in (c0, c1)]
            if c0 > c1:
                p += [tuple((2 * a + b) // 3 for a, b in zip(p[0], p[1])), tuple((a + 2 * b) // 3 for a, b in zip(p[0], p[1]))]
            else:
                p += [tuple((a + b) // 2 for a, b in zip(p[0], p[1])), (0, 0, 0)]
            for i in range(16):
                px[(by * 4 + i // 4) * w + bx * 4 + i % 4] = p[(idx >> (2 * i)) & 3]
    return px, w, h


def _tarb_layer(data, layer):
    """norm / mate / extm tiles: 257 x 257 texels after a 0x34-byte header, NVN2 block-linear (1-GOB blocks)."""
    fmt = data[4:8]
    body = data[0x34:]
    n = 257
    if layer == "norm" and fmt[:2] == b"\x06\x06" and fmt[2:] == b"\0\0":
        px = _nvn2_deswizzle_bl(body, n, n, 2, 1)
        steep = []
        for i in range(n * n):
            x, z = (px[2 * i] - 128) / 127.0, (px[2 * i + 1] - 128) / 127.0
            steep.append(min(1.0, (x * x + z * z) ** 0.5))
        return {"grid": "%d x %d" % (n, n), "channels": "X and Z of the surface normal (Y is worked out)",
                "steepest": round(max(steep), 3),
                "picture_note": "steepness: ' ' flat … '@' vertical", "picture": _ascii_preview(steep, n, n, 0.0, 1.0)}
    if fmt == b"\x06\x06\x06\x06":
        px = _nvn2_deswizzle_bl(body, n, n, 4, 1)
        if layer == "mate":
            a = collections.Counter(px[0::4][:n * n])
            b2 = collections.Counter(px[1::4][:n * n])
            mats = sorted(set(a) | set(b2))
            letters = {m: "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"[i % 62] for i, m in enumerate(mats)}
            rows = []
            for r in range(32):
                y = min(n - 1, int((r + 0.5) * n / 32))
                rows.append("".join(letters[px[4 * (y * n + min(n - 1, int((c + 0.5) * n / 64)))]] for c in range(64)))
            return {"grid": "%d x %d" % (n, n),
                    "channels": "1st material, 2nd material (index into the .tscb material list), blend between them",
                    "first_material_cells": {_tera_mat(k): v for k, v in sorted(a.items())},
                    "second_material_cells": {_tera_mat(k): v for k, v in sorted(b2.items())},
                    "picture_note": "1st material per cell: " + ", ".join("%s = %s" % (letters[m], _tera_mat(m)) for m in mats),
                    "picture": rows}
        lum = [(px[4 * i] * 3 + px[4 * i + 1] * 6 + px[4 * i + 2]) / 10.0 for i in range(n * n)]
        return {"grid": "%d x %d" % (n, n), "channels": "4 x 8-bit", "picture": _ascii_preview(lum, n, n)}
    words = struct.unpack_from("<13I", data, 0)
    if words[0] == 0x00024400 and fmt == b"\x09\x09\x09\x09":
        off2 = 0x2C + words[11]
        cells = 65
        px = _nvn2_deswizzle_bl(data[off2:off2 + words[12]], cells, cells, 8, 1)
        vals = [struct.unpack_from("<4H", px, 8 * i) for i in range(cells * cells)]
        hs = [v[0] for v in vals]
        flags = collections.Counter(v[3] for v in vals)
        return {"grid": "%d x %d cells (one per 4 x 4 terrain points)" % (cells, cells),
                "channels": "water surface height (0-65535), two values around 32768 (probably flow X / Z), flag (3 = water, 0 = edge padding)",
                "lowest": min(hs), "highest": max(hs),
                "highest_height": round(max(hs) / 65535.0 * 2000, 2),
                "flag_counts": {str(k): v for k, v in sorted(flags.items())},
                "picture_note": "water surface height where there is water (flag 3): ' ' lowest … '@' highest; "
                                "the edge cells (flag 0) are padding",
                "picture": _ascii_preview([v[0] if v[3] else min(hs) for v in vals], cells, cells, cols=65, rows=33)}
    if words[7] == 0x18 and fmt == b"\x06\0\0\0" and words[8] == 260 * 260:
        v = list(data[0x34:0x34 + 260 * 260])
        c = collections.Counter(v)
        return {"grid": "260 x 260 (one byte each)", "channels": "slow-down amount (0 = none, 255 = most)",
                "cells_with_no_slowdown": c.get(0, 0), "cells_at_255": c.get(255, 0),
                "picture_note": "slow-down: ' ' none … '@' most", "picture": _ascii_preview(v, 260, 260, 0, 255)}
    if fmt == b"\x08\x08\x07\x00":
        px = _nvn2_deswizzle_bl(body, n, n, 4, 1)

        def uf(x, mb):
            e, m_ = (x >> mb) & 31, x & ((1 << mb) - 1)
            return m_ / float(1 << mb) * 2.0 ** -14 if e == 0 else (1 + m_ / float(1 << mb)) * 2.0 ** (e - 15)
        rgb = []
        for i in range(n * n):
            v = struct.unpack_from("<I", px, 4 * i)[0]
            rgb.append((uf(v & 0x7FF, 6), uf((v >> 11) & 0x7FF, 6), uf(v >> 22, 5)))
        lum = [0.3 * r + 0.6 * g + 0.1 * b_ for r, g, b_ in rgb]
        srt = sorted(lum)
        return {"grid": "%d x %d" % (n, n), "channels": "baked light colour, packed floats (R11 G11 B10)",
                "average": [round(sum(c[k] for c in rgb) / len(rgb), 4) for k in range(3)],
                "brightest": round(srt[-1], 3),
                "picture_note": "brightness (scaled so the brightest 1% shows as '@')",
                "picture": _ascii_preview(lum, n, n, 0.0, srt[int(len(srt) * 0.99)])}
    bcfmt = words[5] >> 24
    if fmt[1] == 0x16 and bcfmt in (0x30, 0x34):
        bsz = 8 if bcfmt == 0x30 else 16
        scale = data[0] if data[0] in (1, 2, 4) else 1          # 1 = 260 x 260; sky_bounce has 2 = 520 x 520
        bw = (260 * scale) // 4
        need = ((bw * bsz + 63) // 64) * 64 * ((bw + 7) // 8) * 8
        if len(body) < need or words[12] != need:
            return {"_warning": "BC%d texture of %d bytes - layout not understood yet (expected %d)"
                    % (1 if bsz == 8 else 3, words[12], need)}
        blk = _nvn2_deswizzle_bl(body, bw, bw, bsz, 1)
        if bsz == 16:
            blk = b"".join(blk[i + 8:i + 16] for i in range(0, len(blk), 16))
        px, w, h = _bc1_decode(blk, bw, bw)
        avg = [round(sum(p[c] for p in px) / len(px), 1) for c in range(3)]
        lum = [(p[0] * 3 + p[1] * 6 + p[2]) / 10.0 for p in px]
        out = {"grid": "%d x %d (BC%d compressed colour)" % (w, h, 1 if bsz == 8 else 3), "average_colour": avg,
               "picture_note": "brightness", "picture": _ascii_preview(lum, w, h)}
        if scale > 1:
            out["grid"] += ", %d x the usual 260 x 260 detail" % scale
            out["channels"] = ("baked sky / bounce light for the ground (the terrain shader reads it as tera_sky_bounce, "
                               "next to tera_bake_light and tera_bake_gi); red, green and blue are separate values, so it "
                               "looks scrambled as a picture. Tested on square 65000001C3: blue follows bake_gi's brightness "
                               "almost exactly (correlation 0.96), green closely (0.87); red is mostly either 0 or ~255 "
                               "(0 where the ground is hidden under ice / cliff pieces - bake_gi is 0 there too)")
            out["channel_averages"] = avg
            out["red_picture"] = _ascii_preview([p[0] for p in px], w, h)
            out["green_picture"] = _ascii_preview([p[1] for p in px], w, h)
            out["blue_picture"] = _ascii_preview([p[2] for p in px], w, h)
            del out["picture"], out["picture_note"]
        return out
    return None

def _tarb_hght_maps(data):
    """hght tile, first section (0x34, 135200 bytes): two 260 x 260 maps. First half: two 4-bit planes (rows of 130
    bytes, low nibble first) = the low / high halves of one smooth 8-bit value. Second half: one 8-bit byte per point
    (rows of 260 bytes) - mostly 0, 255 in patches (columns 0 and 257-259 are edge padding)."""
    w = struct.unpack_from("<13I", data, 0)
    size1 = w[8]
    if size1 != 4 * 260 * 130 or 0x34 + size1 > len(data):
        return None
    sec = data[0x34:0x34 + size1]
    nib = bytearray(2 * size1)
    nib[0::2] = bytes(x & 15 for x in sec)
    nib[1::2] = bytes(x >> 4 for x in sec)
    N_ = 260 * 260
    m = [nib[k * N_:(k + 1) * N_] for k in range(2)]
    a = [m[1][i] * 16 + m[0][i] for i in range(N_)]
    ca = collections.Counter(a)
    srt = sorted(a)
    out = {"grid": "2 maps of 260 x 260 (one stored as two 4-bit planes, one as bytes)",
           "map_1_and_2": {"what": "one 8-bit value per point (map 1 = low 4 bits, map 2 = high 4 bits): fine height detail, "
                                   "probably the shader's second height texture (tera_height2). 120 = no change (sea and "
                                   "flat ground); in full-detail squares it follows the ground's small bumps (correlation "
                                   "0.87 with the height change along x, 0.1 along z - its points likely sit half a step "
                                   "across from the main heights); in big distant squares it is speckle (detail finer than "
                                   "the spacing)",
                           "lowest": srt[0], "highest": srt[-1], "most_common": [[v, c] for v, c in ca.most_common(3)],
                           "picture_note": "' ' lowest … '@' highest (1% / 99% of values)",
                           "picture": _ascii_preview(a, 260, 260, srt[len(srt) // 100], srt[len(srt) * 99 // 100])}}
    mk = sec[2 * N_ // 2:]                                       # second half: 260 x 260 bytes
    mk = list(mk[:N_])
    inner = [mk[y * 260 + x] for y in range(257) for x in range(1, 257)]
    c = collections.Counter(mk)
    sv = [v - 256 if v > 127 else v for v in mk]
    svi = [sv[y * 260 + x] for y in range(257) for x in range(1, 257)]     # (without the padding columns)
    pos = sum(1 for v in svi if v > 0); neg = sum(1 for v in svi if v < 0)
    out["map_3"] = {
        "what": "a SIGNED 8-bit value, one byte per point (rows of 260 bytes; columns 0 and 257-259 are edge padding). "
                "Mostly 0 in the small full-detail squares; in the big distant squares small +/- values (a few units) on "
                "slopes, positive on one side of a ridge and negative on the other (whole-world square: about as many "
                "positive as negative, size follows the slope, r 0.67). Probably a height correction between detail levels.",
        "positive_points": pos, "negative_points": neg, "lowest": min(svi), "highest": max(svi),
        "values": {str(v): n for v, n in sorted(collections.Counter(svi).items())},
        "picture_note": "where the non-zero values are (' ' 0 … '@' largest size)",
        "picture": _max_preview([abs(v) for v in sv], 260, 260)}
    return out


def read_tarb(b, label=""):
    """TARB: one terrain tile layer (TerrainArc/<scene>/<area>.<layer>): small header + zstd chunks."""
    ver, n, hs = struct.unpack_from("<IHH", b, 4)
    nm = label.rsplit("/", 1)[-1]
    layer = nm.split(".", 1)[1] if "." in nm else "?"
    kind = _TARB_KIND.get(layer.rsplit(".", 1)[-1], "terrain layer")
    out = {"_binary_format": "TARB terrain tile (%s)" % kind,
           "_note": "One square of the course terrain: the .tscb file in TerrainArc lists every square (its position and size) "
                    "and which layers it has (hght = heights, norm = normals, mate = ground material, *.extm = extra layers such "
                    "as baked light, water, road, dirt, biome). The data is zstd-compressed inside; its pixel layout isn't "
                    "decoded yet.",
           "version": ver, "chunks": n, "area": nm.split(".")[0], "layer": layer}
    p, data, chunks = 0x0C, b"", []
    for _ in range(min(n, 64)):
        ds, cs = struct.unpack_from("<II", b, p)
        chunks.append({"size": ds, "compressed": cs})
        p += 8
    q = hs
    for c in chunks:
        raw = b[q:q + c["compressed"]]
        q += c["compressed"]
        try:
            data += _zstd_dec(raw, c["size"]) if raw[:4] == ZSTD_MAGIC else raw
        except Exception as e:
            out["_warning"] = "chunk couldn't be decompressed: %s" % e
            break
    out["size_unpacked"] = len(data)
    if len(data) >= 0x34:
        w = struct.unpack_from("<13I", data, 0)
        if layer == "hght":
            try:
                hm = _tarb_heights(data)
            except Exception as e:
                hm = {"_warning": "heights couldn't be decoded: %s" % e}
            try:
                dm = _tarb_hght_maps(data)
            except Exception as e:
                dm = {"_warning": "the 4-bit maps couldn't be decoded: %s" % e}
            if dm:
                out["detail_maps"] = dm
            if hm:
                out["heights"] = hm
                out["_note"] = ("One square of the course terrain - its height map: 257 x 257 heights (0-65535, shown "
                                "also as world height assuming the .tscb max_height of 2000). 'picture' is a small "
                                "preview, darker = lower ( . : - = + * # % @ from low to high). The .tscb lists where "
                                "each square sits. The first part of the file holds four 260 x 260 4-bit maps "
                                "('detail_maps'); their meaning isn't known yet.")
        if layer != "hght":
            try:
                lay = _tarb_layer(data, layer.rsplit(".", 1)[-1] if layer.endswith("extm") else layer)
            except Exception as e:
                lay = {"_warning": "pixels couldn't be decoded: %s" % e}
            if lay:
                out["pixels"] = lay
                out["_note"] = out["_note"].replace("its pixel layout isn't decoded yet",
                                                    "its pixels are decoded below (257 x 257, small text preview)")
        out["inner_header"] = {"format": data[4:8].hex(" "), "a": w[2], "b": w[3], "format2": data[0x14:0x18].hex(" "),
                               "data_size": w[12], "header_words": " ".join("%08x" % x for x in w)}
    return out


# Mario Kart World's 62 terrain materials, in the order the .tscb / .mate number them (from
# Gyml/TerrainMaterialSetGParam/Product_Proc: each is TerrainMaterialGParam/CmnTex_Tera_Product_Proc_<name>)
TERA_MAT_NAMES = ['CM00RoadSoil', 'CM00RoadSoilBright', 'CM00RoadSand', 'CM00RoadCenterRutSoil', 'CM00RoadCenterRutSoilBright', 'CM00RoadCenterRutSand', 'CM00RoadCenterRutSnow', 'CM00RoadWetSoil', 'CM00RoadWetSoilBright', 'CM00RoadWetSand', 'CM00RoadWetSnow', 'CM00RoadSideMaterialHardSoil', 'CM00RoadSideMaterialSoftGrass', 'CM00RoadSideMaterialSoftGrass', 'CM00RoadSideMaterialHardSand', 'CM00RoadSideMaterialHardSand_Road', 'CM00RoadSideMaterialHardSandHighDirt', 'CM00River', 'CM00RiverGravel', 'CM00RiverSoil', 'CM00RiverSoilGravel', 'CM00River_Road', 'CM00Beach', 'CM00BeachGravel', 'CM00Beach_Road', 'CM00BeachGravel_Road', 'CliffRock', 'CliffUnderRock', 'CliffFS00Grass', 'CM00Line', 'CM00BaseGrass', 'CM00BaseGrassHighDirt', 'CM00Rock', 'CM00Soil', 'CM00Mud', 'OS00Base', 'WL00Base', 'WL00Grass', 'WL00RockSurface', 'DT00Base', 'DT00Road', 'DT00Gravel', 'DT00Riverside', 'DT00RiversideGravel', 'DT00BaseHighDirt', 'PL00Field', 'PL02Grass', 'RT00ReefRock', 'RT00ReefSand', 'RT00Karst', 'JG00RuinsUnderTree', 'JG00DinoWoodChip', 'FS00Moss', 'FS00Mud', 'VL00Base', 'VL00Ash', 'VL00ColdLava', 'IC00Base', 'IC00RoadForest', 'IC00RoadGelaende', 'IC00BaseHighDirt', 'IC00Beach']


def _tera_mat(k):
    return "%d %s" % (k, TERA_MAT_NAMES[k]) if 0 <= k < len(TERA_MAT_NAMES) else str(k)


def read_tscb(b):
    """TSCB v9.12 (Mario Kart World): terrain scene - layers, materials, and every terrain square."""
    def cs(o):
        try:
            return b[o:b.index(b"\0", o)].decode("utf-8")
        except Exception:
            return "?"
    out = {"_binary_format": "TSCB terrain scene",
           "_note": "The terrain of a whole world: a quad-tree of square areas (size 64 at the top, halved each level). Each "
                    "area's name is the base name of its tile files in the same folder (e.g. 6600000A4D.hght / .norm / .mate "
                    "/ .bake_gi.extm). x and z are in world units / world_scale.",
           "version": "%d.%d" % (b[5], b[4])}
    world_scale, max_height = struct.unpack_from("<2f", b, 0x10)
    n_layers, n_mats, n_areas = struct.unpack_from("<3I", b, 0x30)
    out.update({"world_scale": world_scale, "max_height": max_height, "layer_count": n_layers,
                "material_count": n_mats, "area_count": n_areas})
    # layer names: the strings before the first area name in the string pool
    A = 0x78 + struct.unpack_from("<I", b, 0x78)[0]
    offs = []
    for i in range(min(n_areas, 200000)):
        if A + 4 * i + 4 > len(b):
            break
        offs.append(A + 4 * i + struct.unpack_from("<I", b, A + 4 * i)[0])
    areas, sizes = [], collections.Counter()
    for o in offs:
        if o + 0x64 > len(b):
            break
        x, z, s = struct.unpack_from("<3f", b, o)
        nm = cs(o + 0x60 + struct.unpack_from("<I", b, o + 0x60)[0])
        sizes[s] += 1
        areas.append("%s  x %g  z %g  size %g" % (nm, x, z, s))
    first_area = min((o + 0x60 + struct.unpack_from("<I", b, o + 0x60)[0]) for o in offs[:50]) if offs else len(b)
    pool = b.find(b"height\0")
    if 0 < pool < first_area:
        strs = [x.decode("ascii") for x in re.findall(rb"[\x20-\x7e]+", b[pool:first_area])]
        strs = [x for x in strs if len(x) >= 3 and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.]*", x)]
        layers, cur = [], None
        for x in strs:
            if x.endswith("Resource") and cur:
                cur["resource"] = x
            elif cur and "." not in cur.get("file", "") and x != cur["name"] and "file" not in cur:
                cur["file"] = x
            else:
                cur = {"name": x}
                layers.append(cur)
        out["layers"] = ["%s (.%s%s)" % (l["name"], l.get("file", "?"), ", " + l["resource"] if "resource" in l else "") for l in layers]
    if n_mats == len(TERA_MAT_NAMES):
        out["materials"] = [_tera_mat(i) for i in range(n_mats)]
    out["areas_by_size"] = {("%g" % k): v for k, v in sorted(sizes.items(), key=lambda kv: -kv[0])}
    out["areas"] = areas
    return out


_DLP_KINDS = {
    b"DLPL": "baked light probes (list)", b"DLPG": "baked light probe grid", b"DLPA": "baked light probe area",
    b"DLPR": "baked light probes (whole world)", b"DALP": "baked light probes for one object", b"BKLS": "baked light sources",
    b"DCGM": "baked light data (empty)",
}


def _max_preview(vals, w, h, cols=64, rows=32):
    """like _ascii_preview, but each character shows the largest value in its block (so sparse marks stay visible)"""
    hi = max(vals) or 1
    out = []
    for r in range(rows):
        y0, y1 = r * h // rows, max(r * h // rows + 1, (r + 1) * h // rows)
        line = ""
        for c in range(cols):
            x0, x1 = c * w // cols, max(c * w // cols + 1, (c + 1) * w // cols)
            v = max(vals[y * w + x] for y in range(y0, y1) for x in range(x0, x1))
            line += _RAMP[max(0, min(9, int(v * 10 // (hi + 1e-9))))] if v else " "
        out.append(line)
    return out


def _r3(v):
    return [None if x != x or abs(x) > 1e30 else round(x, 3) for x in v]


def _probe40(f, o):
    """one 40-float probe record (DLPG / DLPR): 24 light values (8 RGB sets), 4 values, 4 'sky' values,
    then 3 floats (DLPG: position) + 0, 3 packed words, 0"""
    rec = {"light_rgb_sets": [_r3(f[o + 3 * k:o + 3 * k + 3]) for k in range(8)],
           "values": _r3(f[o + 24:o + 28]), "sky": _r3(f[o + 28:o + 32]),
           "_meaning": "light_rgb_sets 1-4 = bounced daylight (colour + direction), 5-8 = nearby light sources; values = "
                       "open sky (amount, overhead, south+/north-, east+/west-); sky = sun / moon fade (amount, then direction)"}
    return rec


def read_dlp(b):
    """Baked lighting data in Env/: header tag + version + packed floats (probe colours as spherical harmonics, positions)."""
    tag = b[:4]
    ver = b[4] if tag == b"DALP" else struct.unpack_from("<H", b, 4)[0]
    out = {"_binary_format": "%s %s" % (tag.decode(), _DLP_KINDS.get(tag, "baked lighting")),
           "_note": "Baked (precomputed) lighting for the course: light probes store the light arriving at points in the "
                    "world so moving karts and characters can be lit to match. Header and counts shown; the per-probe "
                    "records aren't decoded yet.",
           "version": ver, "size": len(b)}
    if tag == b"BKLS":
        n_pt = struct.unpack_from("<H", b, 6)[0]
        n_sp = b[8]
        out["_note"] = ("Baked light sources for one square of the course: point lights and spot lights (lamps, "
                        "glowing objects) used when the course lighting was baked. Colour is RGB (usually 0-255, higher for extra-bright lights), then "
                        "intensity; range = how far the light reaches.")
        out["point_lights"] = n_pt
        out["spot_lights"] = n_sp
        pts, sps, o = [], [], 0x0C
        for i in range(min(n_pt, 4096)):
            if o + 48 > len(b):
                break
            f = struct.unpack_from("<12f", b, o)
            pts.append("pos (%.1f, %.1f, %.1f)  kind %d  colour (%g, %g, %g)  intensity %g  range %g  falloff %g"
                       % (f[0], f[1], f[2], round(f[3]), f[4], f[5], f[6], round(f[7], 3), round(f[8], 3), round(f[9], 3)))
            o += 48
        for i in range(min(n_sp, 4096)):
            if o + 64 > len(b):
                break
            f = struct.unpack_from("<16f", b, o)
            sps.append("pos (%.1f, %.1f, %.1f)  kind %d  colour (%g, %g, %g)  intensity %g  direction (%.2f, %.2f, %.2f)  "
                       "cone %g  angles %g / %g  range %g  falloff %g"
                       % (f[0], f[1], f[2], round(f[3]), f[4], f[5], f[6], round(f[7], 3), f[8], f[9], f[10],
                          round(f[11], 3), round(f[12], 3), round(f[13], 3), round(f[14], 3), round(f[15], 3)))
            o += 64
        out["point_light_list"] = pts
        out["spot_light_list"] = sps
        if o < len(b):
            out["extra_bytes_at_end"] = len(b) - o
    elif tag == b"DLPA" and len(b) >= 0x88:
        f = struct.unpack_from("<32f", b, 8)
        sh = [f[12 + 3 * k:15 + 3 * k] for k in range(4)]
        out["_note"] = ("Baked light for one area (here: one menu / scene area): the light arriving there as four RGB "
                        "sets (spherical harmonics: overall light + how it changes from 3 directions) and 4 'sky' values. "
                        "The first 12 numbers are all 0 in the samples.")
        out["light_rgb_sets"] = [_r3(v) for v in sh]
        out["average_light_colour"] = _r3([c * 0.2821 for c in sh[0]])
        out["sky"] = _r3(f[28:32])
        if any(f[:12]):
            out["first_12"] = _r3(f[:12])
    elif tag == b"DLPG" and len(b) > 8:
        n = (len(b) - 8) // 4
        f = struct.unpack_from("<%df" % n, b, 8)
        probes, cols, o = [], [], 0
        while o + 40 <= n and len(probes) < 100000:
            stack = f[o + 1] > 0.01 and abs(f[o + 1] - f[o + 2]) < 1e-3 and abs(f[o + 2] - f[o + 3]) < 1e-3
            if not stack and f[o + 35] == 0.0 and abs(f[o + 39]) < 1e-30 and all(abs(f[o + 32 + k]) < 1e6 for k in range(3)):
                rec = _probe40(f, o)
                rec["pos"] = [round(x, 2) for x in f[o + 32:o + 35]]
                rec["packed"] = b[8 + 4 * (o + 36):8 + 4 * (o + 39)].hex()
                probes.append(rec)
                o += 40
            else:                                       # after each vertical stack: its base height and spacing
                cols.append(_r3(f[o:o + 4]))
                o += 4
        if o < n:
            cols.append(_r3(f[o:n]))
        out["_note"] = ("Baked light probe grid for one square of the course: probes on a regular 3D grid (here "
                        "stacks of probes above each ground point), each storing the light arriving there - 8 RGB sets "
                        "(spherical harmonics), 4 more values, 4 'sky' values - and its position. After each stack: its "
                        "lowest height and spacing.")
        out["probes"] = len(probes)
        xs = sorted(set(p_["pos"][0] for p_ in probes)); ys = sorted(set(p_["pos"][1] for p_ in probes)); zs = sorted(set(p_["pos"][2] for p_ in probes))
        if probes:
            out["grid"] = "%d x %d x %d (x, height, z)" % (len(xs), len(ys), len(zs))
            out["x_range"] = [xs[0], xs[-1]]; out["height_range"] = [ys[0], ys[-1]]; out["z_range"] = [zs[0], zs[-1]]
        out["stack_info"] = cols[:64]
        out["probe_list"] = [{"pos": p_["pos"], "light_rgb_sets": p_["light_rgb_sets"], "values": p_["values"],
                              "sky": p_["sky"], "packed": p_["packed"]} for p_ in probes[:64]]
    elif tag == b"DLPL" and len(b) >= 8:
        n = struct.unpack_from("<H", b, 6)[0]
        out["probes"] = n
        if n and (len(b) - 8) % n == 0 and (len(b) - 8) // n == 136:
            out["_note"] = ("Baked light probes for one placed area (a list rather than a grid): each probe has a "
                            "64-bit id (the list is sorted by it - probably a hash of the probe's place) and the same 32 "
                            "lighting values as the .dlpr / .dlpg probes (worked out in the 3D view): 1-12 bounced "
                            "daylight (1-3 its colour, taking the ground's colour; 4-12 how it changes with direction), "
                            "13-24 light from nearby sources - lava, lamps, windows (13-15 colour, 16-24 direction), "
                            "25-28 open sky (25 how much, about 1.77 = a whole open sky; 26 how much overhead; 27 open to "
                            "the south + / north -; 28 east + / west -), 29-32 how much to fade the sun / moon light and "
                            "its shadow (29 the amount: 0 in the open, about 3.5 under tunnels, high near the game's own "
                            "lights at night; 30-32 which way it increases, non-zero only at its edges). The first probes "
                            "are shown.")
            lst = []
            for i in range(min(n, 40)):
                o = 8 + i * 136
                v = struct.unpack_from("<33f", b, o + 4)
                lst.append({"id": "%016x" % struct.unpack_from("<Q", b, o)[0], "values": [round(x, 4) for x in v[1:]]})
            out["probe_list"] = lst
        elif n:
            out["bytes_per_probe"] = round((len(b) - 8) / n, 2)
    elif tag == b"DLPR" and len(b) >= 8 + 128 * 128:
        m = b[8:8 + 128 * 128]
        cnt = collections.Counter(m)
        out["_note"] = ("Baked light probes for the whole course: a 128 x 128 map of the world (125 units a square) "
                        "giving how many probe layers each square has (0-4), then the probe records - the same kind as "
                        "in the .dlpg files (light as 8 RGB sets, 4 values, 4 'sky' values), with each stack's base "
                        "height and spacing. Only the map and the first records are shown.")
        out["map"] = "128 x 128, probe layers per square"
        out["squares_with_probes"] = 128 * 128 - cnt.get(0, 0)
        out["layers_count"] = {str(k): v for k, v in sorted(cnt.items())}
        out["map_picture_note"] = "' ' none … '@' 4 layers"
        out["map_picture"] = _max_preview(list(m), 128, 128)
        n = min((len(b) - 8 - 16384) // 4, 4000)
        f = struct.unpack_from("<%df" % n, b, 8 + 16384)
        recs, o = [], 0
        while o + 40 <= n and len(recs) < 12:
            if abs(f[o]) > 50 and f[o + 1] > 0 and abs(f[o + 1] - f[o + 2]) < 1e-3:
                recs.append({"stack_info": _r3(f[o:o + 4])}); o += 4
                continue
            r = _probe40(f, o); r["packed"] = b[8 + 16384 + 4 * (o + 36):8 + 16384 + 4 * (o + 39)].hex()
            recs.append(r); o += 40
        out["first_records"] = recs
    elif tag == b"DALP":
        out["bounds_min"] = [round(v, 3) for v in struct.unpack_from("<3f", b, 8)]
        out["bounds_max"] = [round(v, 3) for v in struct.unpack_from("<3f", b, 0x14)]
        out["grid"] = list(struct.unpack_from("<2I", b, 0x20))
    elif len(b) <= 8:
        out["_note"] = "Empty baked-lighting file: only the header."
    return out


def read_ainb(b):
    """AINB (AIB, version 4.7): AI / sequence node graph - commands, nodes, their fixed parameters and the links
    between nodes (with the result each link follows, e.g. Success / Failure)."""
    v = struct.unpack_from("<29I", b, 4)
    sp = v[8]

    def cs(o):
        try:
            return b[sp + o:b.index(b"\0", sp + o)].decode("utf-8", "replace")
        except Exception:
            return "?"
    strs = [x.decode("utf-8", "replace") for x in b[sp:].split(b"\0") if x]
    out = {"_binary_format": "AINB AI node graph",
           "_note": "An AI / game-flow graph (node-based logic, like a flowchart): commands start the graph at a node; "
                    "each node has fixed parameters and links to the next nodes, usually chosen by its result "
                    "(Success / Failure...). 'flow' lists every node with its parameters and where it goes next.",
           "version": "%d.%d" % (v[0] >> 8, v[0] & 0xFF), "name": cs(v[1]), "commands": v[2], "nodes": v[3],
           "queries": v[4], "attachments": v[5], "outputs": v[6]}
    try:
        ncmd, nnode = v[2], v[3]
        # result names (transition table): an array of offsets to (name, flags) pairs
        trans = []
        for cand in (struct.unpack_from("<I", b, 0x30)[0], struct.unpack_from("<I", b, 0x50)[0]):
            if 0 < cand < len(b) - 4:
                first = struct.unpack_from("<I", b, cand)[0]
                if cand < first <= len(b) - 8 and (first - cand) % 4 == 0 and (first - cand) // 4 <= 4096:
                    cnt = (first - cand) // 4
                    tt = []
                    for i in range(cnt):
                        eo = struct.unpack_from("<I", b, cand + 4 * i)[0]
                        if eo + 8 > len(b):
                            tt = None
                            break
                        no = struct.unpack_from("<I", b, eo + 4)[0]          # (kind, name)
                        ok_ = 0 < no < len(b) - sp and b[sp + no - 1] == 0
                        tt.append(cs(no) if ok_ else None)
                    if tt:
                        trans = tt
                        break
        # fixed ("immediate") parameters: 6 lists (int, bool, float, string, vec3, user-defined)
        imm_tab = list(struct.unpack_from("<6I", b, v[10])) + [v[11]]
        imm_size = [12, 12, 12, 12, 20, 12]

        def imm(t, i):
            o = imm_tab[t] + i * imm_size[t]
            if o + imm_size[t] > len(b):
                return "?"
            name = cs(struct.unpack_from("<I", b, o)[0])
            if t == 0:
                val = struct.unpack_from("<i", b, o + 8)[0]
            elif t == 1:
                val = bool(struct.unpack_from("<I", b, o + 8)[0])
            elif t == 2:
                val = round(struct.unpack_from("<f", b, o + 8)[0], 4)
            elif t == 3:
                val = cs(struct.unpack_from("<I", b, o + 8)[0])
            elif t == 4:
                val = [round(x, 3) for x in struct.unpack_from("<3f", b, o + 8)]
            else:
                val = "(object)"
            return "%s = %s" % (name, val)
        cmds = []
        for i in range(min(ncmd, 4096)):
            o = 0x74 + i * 0x18
            nm, left = struct.unpack_from("<I", b, o)[0], struct.unpack_from("<H", b, o + 0x14)[0]
            cmds.append("%s -> node %d" % (cs(nm), left))
        node_base = 0x74 + ncmd * 0x18
        names, flow = [], []
        for i in range(min(nnode, 65535)):
            o = node_base + i * 0x3C
            ty, idx = struct.unpack_from("<HH", b, o)
            nm = cs(struct.unpack_from("<I", b, o + 8)[0])
            body = struct.unpack_from("<I", b, o + 0x14)[0]
            params, links = [], []
            if 0 < body < len(b) - 0xA4:
                for t in range(6):
                    st, cnt = struct.unpack_from("<II", b, body + 8 * t)
                    for k in range(min(cnt, 256)):
                        params.append(imm(t, st + k))
                lb = body + 0x90
                pairs = [struct.unpack_from("<BB", b, lb + 2 * k) for k in range(10)]
                total = sum(c for c, _ in pairs)
                if total <= 256:
                    for lt, (c, st) in enumerate(pairs):
                        for k in range(c):
                            po = lb + 20 + 4 * (st + k)
                            eo = struct.unpack_from("<I", b, po)[0]
                            if eo + 8 > len(b):
                                continue
                            tgt, x = struct.unpack_from("<II", b, eo)
                            res = trans[x] if trans and x < len(trans) and lt != 0 else None
                            links.append({"type": lt, "to": tgt, "when": res})
            names.append(nm)
            flow.append({"index": idx, "type": ty, "name": nm, "params": params, "links": links})
        tnames = {1: "S32 selector", 2: "Sequential", 3: "Simultaneous", 4: "F32 selector", 5: "String selector",
                  6: "Random selector", 7: "Bool selector", 8: "Fork", 9: "Join", 10: "Alert", 20: "Expression",
                  300: "Module child", 400: "End", 500: "Split timing"}
        for f in flow:                                  # unnamed nodes: their kind, plus a string parameter (e.g. the result they report)
            if not f["name"]:
                sp_ = [p_ for p_ in f["params"] if p_.startswith("TransitionCommand")]
                f["name"] = "(%s%s)" % (tnames.get(f["type"], "type %d" % f["type"]), ": " + sp_[0].split(" = ", 1)[1] if sp_ else "")
        lines = []
        for f in flow:
            ln = "node %d  %s" % (f["index"], f["name"])
            if f["params"]:
                ln += "  [" + "; ".join(f["params"]) + "]"
            for L in f["links"]:
                tn = flow[L["to"]]["name"] if L["to"] < len(flow) else "?"
                ln += "\n      -> %s%s (node %d)" % ("on %s: " % L["when"] if L["when"] else "", tn, L["to"])
            lines.append(ln)
        out["start"] = cmds
        if trans:
            out["results"] = [t for t in trans if t]
        out["flow"] = "\n".join(lines).split("\n")
    except Exception as ex:
        out["_warning"] = "node links couldn't be read: %s" % ex
    out["strings"] = strs
    return out


def read_ghost(b):
    """Staff ghost (.ghost): header with size and the date it was recorded, then packed replay data."""
    h, size, unk = struct.unpack_from("<III", b, 0)
    y, mo, d, hh, mi, ss = struct.unpack_from("<6I", b, 0x0C)
    out = {"_binary_format": "Staff ghost replay",
           "_note": "A time-trial ghost recorded by the developers: the kart's inputs/positions for the whole race. The "
                    "header is decoded; the replay data after it is packed and not decoded yet.",
           "id": "%08x" % h, "size_in_header": size, "size_matches_file": size == len(b)}
    if 2000 <= y <= 2100 and 1 <= mo <= 12 and 1 <= d <= 31:
        out["recorded"] = "%04d-%02d-%02d %02d:%02d:%02d" % (y, mo, d, hh, mi, ss)
    return out


def read_cull_ext(b, ext):
    if ext == "bcul":
        ver, n = struct.unpack_from("<II", b, 0)
        ids = ["%016x" % x for x in struct.unpack_from("<%dQ" % min(n, (len(b) - 8) // 8), b, 8)]
        return {"_binary_format": "Cull runtime ID list (.bcul)",
                "_note": "The objects (by runtime/placement ID) that can be seen from this cell of the course; anything not "
                         "listed is skipped when drawing. The IDs match the ones in Cull/Boxes.bbox.",
                "version": ver, "count": n, "ids": ids}
    if ext == "bbox":
        ver, n = struct.unpack_from("<II", b, 0)
        boxes = []
        for i in range(min(n, 100000)):
            o = 8 + i * 44
            if o + 44 > len(b):
                break
            i64 = struct.unpack_from("<Q", b, o)[0]
            f = struct.unpack_from("<9f", b, o + 8)
            boxes.append("%016x  centre (%.1f, %.1f, %.1f)  rot (%.3f, %.3f, %.3f)  size (%.1f, %.1f, %.1f)" % ((i64,) + f))
        return {"_binary_format": "Cull boxes (.bbox)",
                "_note": "A bounding box for every object that culling knows about (ID, centre, rotation in radians, size).",
                "version": ver, "count": n, "boxes": boxes}
    if ext == "bgrd":
        n = len(b) * 8
        side = int(round(n ** 0.5))
        out = {"_binary_format": "Cull grid (.bgrd)",
               "_note": "A grid of on/off cells (one bit each) over the course for one object/terrain ID: '#' where it can "
                        "be seen or exists, '.' where not. The folder name (Div16 … Div256) is the grid's resolution.",
               "bits": n}
        if side * side == n and side <= 256:
            rows = []
            for r in range(side):
                row = ""
                for c in range(side):
                    k = r * side + c
                    row += "#" if b[k >> 3] >> (k & 7) & 1 else "."
                rows.append(row)
            out["grid"] = rows
        return out
    if ext == "dapg":
        g = struct.unpack_from("<I", b, 0)[0]
        cells = g * g
        if not (0 < g <= 1024) or 4 + cells * 2 > len(b):
            return {"_binary_format": "Acoustic probe grid (.dapg)", "_warning": "unexpected header"}
        idx = struct.unpack_from("<%dH" % cells, b, 4)
        base = 4 + cells * 2
        nrec = idx[-1] if idx else 0
        size = (len(b) - base) // nrec if nrec else 0
        out = {"_binary_format": "Acoustic probe grid (.dapg)",
               "_note": ("Sound reflection / reverb data for one square of the course: a %d x %d grid of cells; the table "
                         "gives each cell's running record count, then one 124-byte record per cell: a height range, two "
                         "counts, 4 values (the first changes steadily from cell to cell - a sort key?; then 5 = the "
                         "spacing), then 3 groups of 4 (value, code) pairs - values look like distances / heights in game "
                         "units, codes like packed surface or material ids (03010100 seems to mean 'none'). Their exact "
                         "meaning isn't known yet; the first records are shown. Mario Kart World has 32 x 32 of these "
                         "files (Sound/Acoustic/Probe/Game03/A_B), so each covers 500 x 500 m of the 16 km world and each "
                         "cell 7.8 m (file A_B: A = x / east, B = z / south; cells row by row, 64 per row going east). Each record: 12 distances to the nearest surface in 3 rings of 4 directions (1-4 low, 5-8 up, 9-12 steeply up; each ring north-east, north-west, south-west, south-east; about 214-222 = nothing in range) with a code each. The ns2dig 3D terrain view places them in full-detail areas ('Sound probes: ...')." % (g, g)),
               "grid": "%d x %d" % (g, g), "records": nrec, "record_bytes": size}
        if size == 124 and nrec:
            recs = []
            for i in range(min(nrec, 24)):
                o = base + i * 124
                lo, hi = struct.unpack_from("<2f", b, o)
                c1, c2 = struct.unpack_from("<2H", b, o + 8)
                v = struct.unpack_from("<4f", b, o + 12)
                prs = [(struct.unpack_from("<f", b, o + 28 + 8 * k)[0], struct.unpack_from("<I", b, o + 32 + 8 * k)[0]) for k in range(12)]
                if i < 24:
                    recs.append({"heights": [round(lo, 2), round(hi, 2)], "counts": [c1, c2], "values": _r3(v),
                                 "groups": [["%.2f %08x" % prs[4 * gi + k] for k in range(4)] for gi in range(3)]})
            out["first_records"] = recs
        return out
    return None


def read_char_table(b):
    n = len(b) // 2
    cps = struct.unpack_from("<%dH" % n, b, 0)
    return {"_binary_format": "Character list (sead nvn2 system text table)",
            "_note": "The characters the built-in debug/system font can draw, in its glyph order (here the JIS level-1 set).",
            "count": n, "characters": _cp_ranges(cps)}


def _xtx_deswizzle(data, w, h, bpp):
    """NVN2 block-linear layout as found in nvn2_font.xtx: 512-byte GOBs (64 bytes x 8 rows) with the address bits
    x0-3, y0, y1, x4, y2, x5; blocks of up to 16 GOBs tall."""
    rb = w * bpp
    gx = (rb + 63) // 64
    bh = 1
    while bh < 16 and bh * 8 < h:
        bh *= 2
    out = bytearray(rb * h)
    for y in range(h):
        yi = y & 7
        base_y = (y // (8 * bh)) * gx * bh * 512 + ((y % (8 * bh)) // 8) * 512
        for xb in range(rb):
            xi = xb & 63
            a = base_y + (xb // 64) * bh * 512 + (xi & 15) | ((yi & 1) << 4) | (((yi >> 1) & 1) << 5) | \
                (((xi >> 4) & 1) << 6) | (((yi >> 2) & 1) << 7) | (((xi >> 5) & 1) << 8)
            if a < len(data):
                out[y * rb + xb] = data[a]
    return bytes(out)


def read_xtx(b):
    """XTX (DFvN): NVN texture container - HBvN blocks: 2 = texture info, 3 = texture data."""
    out = {"_binary_format": "XTX NVN texture (DFvN)",
           "_note": "A plain NVN texture (no BNTX around it). For nvn2_font.xtx it is the built-in debug font: a "
                    "128 x 128 sheet of 8 x 16 pixel characters, drawn below as text ('#' = ink)."}
    p, infos, datas = struct.unpack_from("<I", b, 4)[0], [], []
    while p + 0x24 <= len(b) and b[p:p + 4] == b"HBvN":
        hs, ds, doff, typ = struct.unpack_from("<IQQI", b, p + 4)
        if typ == 2:
            sz, align, w, h, d, tgt, fmt, mips = struct.unpack_from("<QIIIIIII", b, p + doff)
            infos.append({"width": w, "height": h, "depth": d, "format": fmt, "mips": mips, "data_size": sz})
        elif typ == 3:
            datas.append(b[p + doff:p + doff + ds])
        p += doff + ds
    texs = []
    for i, t in enumerate(infos):
        e = dict(t)
        e["format_name"] = {1: "R8 (8-bit grey)", 0x25: "RGBA8"}.get(t["format"], "NVN format %d" % t["format"])
        if t["format"] == 1 and i < len(datas) and t["width"] * t["height"] <= 512 * 512:
            px = _xtx_deswizzle(datas[i], t["width"], t["height"], 1)
            e["picture"] = ["".join("#" if px[y * t["width"] + x] >= 128 else "." for x in range(t["width"]))
                            for y in range(t["height"])]
        texs.append(e)
    out["textures"] = texs
    return out


def read_atra(b):
    """ATRA (.baatarc, new style): sound attenuation curves stored by name hash in a 512-slot table."""
    ver, size, slots = struct.unpack_from("<III", b, 4)
    ents = []
    for i in range(min(slots, 4096)):
        o = 0x10 + i * 8
        if o + 8 > len(b):
            break
        a, val, idx = struct.unpack_from("<HHB", b, o)
        if val or b[o + 5:o + 8] != b"\0\0\0":
            ents.append("slot %d: hash %06x, value %d" % (idx, int.from_bytes(b[o + 5:o + 8], "little"), val))
    return {"_binary_format": "ATRA sound attenuation archive",
            "_note": "The newer form of the sound attenuation archive (how sounds get quieter with distance): instead "
                     "of named AATN/AROC files, entries are kept by name hash in a %d-slot table, followed by the "
                     "packed curve data. Names and curves aren't decoded yet." % slots,
            "version": ver, "slots": slots, "used_slots": len(ents), "entries": ents,
            "data_bytes": len(b) - (0x10 + slots * 8)}


def read_gccp(b):
    n = struct.unpack_from("<I", b, 4)[0]
    ids = ["%016x" % x for x in struct.unpack_from("<%dQ" % min(n, (len(b) - 8) // 8), b, 8)]
    return {"_binary_format": "GCCP cull gridify data (.pccg)",
            "_note": "Visibility data for one grid of the course: the object IDs it covers (the same IDs as "
                     "Cull/Boxes.bbox), then the per-cell visibility data (not decoded yet).",
            "objects": n, "ids": ids, "cell_data_bytes": len(b) - 8 - n * 8}


def read_sharc_aahs(b):
    """AAHS: older agl shader *source* archive (.sharc): programs + GLSL source files."""
    ver, size, endian, nl = struct.unpack_from("<IIII", b, 4)
    name = b[0x14:0x14 + nl].rstrip(b"\0").decode("ascii", "replace")
    p = 0x14 + nl
    psize = struct.unpack_from("<I", b, p)[0]
    progs = [s.decode() for s in re.findall(rb"[A-Za-z_][A-Za-z0-9_]{2,}", b[p + 8:p + psize])]
    p += psize
    ssize, n = struct.unpack_from("<II", b, p)
    q, srcs = p + 8, {}
    for _ in range(min(n, 256)):
        es, nl2, sl, _sl2 = struct.unpack_from("<IIII", b, q)
        nm = b[q + 16:q + 16 + nl2].rstrip(b"\0").decode("ascii", "replace")
        txt = b[q + 16 + nl2:q + 16 + nl2 + sl].rstrip(b"\0")
        try:
            t = txt.decode("cp932")
        except Exception:
            t = txt.decode("latin-1")
        srcs[nm] = t.replace("\r\n", "\n").split("\n")
        q += es
    return {"_binary_format": "SHARC agl shader source archive (AAHS)",
            "_note": "An older-style agl shader archive that holds the GLSL source text itself (comments are Japanese, "
                     "converted from Shift-JIS). 'programs' lists the program and its uniform names.",
            "version": ver, "name": name, "programs": progs, "sources": srcs}

# ---------- Nintendo Switch Sports Resort (NSSR) and other newer-game formats ----------
_MII_FIELDS = ("font_region", "favorite_color", "gender", "height", "build", "type", "region_move", "faceline_type",
               "faceline_color", "faceline_wrinkle", "faceline_make", "hair_type", "hair_color", "hair_flip", "eye_type",
               "eye_color", "eye_scale", "eye_aspect", "eye_rotate", "eye_x", "eye_y", "eyebrow_type", "eyebrow_color",
               "eyebrow_scale", "eyebrow_aspect", "eyebrow_rotate", "eyebrow_x", "eyebrow_y", "nose_type", "nose_scale",
               "nose_y", "mouth_type", "mouth_color", "mouth_scale", "mouth_aspect", "mouth_y", "beard_color", "beard_type",
               "mustache_type", "mustache_scale", "mustache_y", "glass_type", "glass_color", "glass_scale", "glass_y",
               "mole_type", "mole_scale", "mole_x", "mole_y")


def read_mii_charinfo(b):
    """a Mii as the Switch stores it (nn::mii::CharInfo, 0x58 bytes): id, nickname, then one byte per feature"""
    nick = b[0x10:0x26].decode("utf-16-le", "replace").split("\0")[0]
    out = {"_binary_format": "Mii character (Switch CharInfo, 88 bytes)",
           "_note": "One Mii: its id, nickname and every face / body setting as the Mii editor numbers them (gender 0 = male, "
                    "1 = female; height and build 0-127). The file's first bytes are the Mii's random id, which is why "
                    "some of these files look like they have a header tag (Az, 7O8D, reMM...).",
           "create_id": b[0:16].hex(), "nickname": nick}
    for i, k in enumerate(_MII_FIELDS):
        out[k] = b[0x26 + i]
    out["gender"] = {0: "male", 1: "female"}.get(b[0x28], b[0x28])
    return out


def read_gswg(b):
    """golf swing replay: u32 frame count, then 2 floats per frame"""
    n = struct.unpack_from("<I", b, 0)[0]
    if 4 + 8 * n != len(b): raise ValueError("frame count doesn't match the size")
    fr = [struct.unpack_from("<2f", b, 4 + 8 * i) for i in range(n)]
    a = [x[0] for x in fr]; c = [x[1] for x in fr]
    pk = max(range(n), key=lambda i: c[i]) if n else 0
    return {"_binary_format": "Golf swing replay (.gswg)",
            "_note": "A recorded golf swing: two numbers per frame (60 frames a second). Their exact meaning isn't known "
                     "yet - the first looks like the club's position through the swing (about 0.5 at rest), the second "
                     "like its speed (it peaks at the moment of the swing).",
            "frames": n, "seconds_at_60fps": round(n / 60, 2),
            "value1_range": [round(min(a), 5), round(max(a), 5)] if n else None,
            "value2_range": [round(min(c), 5), round(max(c), 5)] if n else None,
            "value2_peak_frame": pk,
            "frame_values": [[round(x, 6), round(y, 6)] for x, y in fr]}


def _bwr_name(nm):
    out = {}
    for t in nm.split("_")[1:]:
        m = re.match(r"([A-Za-z]+)(\d+)$", t)
        if m: out[m.group(1)] = int(m.group(2))
        elif t.isdigit(): out["throw"] = int(t)
    return out


def read_bwr(b, label):
    """bowling replay (attract-mode 'AutoPlay' throws): header, the 10 pins' standing flags, key frames"""
    ver, frames, f0, z = struct.unpack_from("<IIfI", b, 0)
    pins = list(b[16:26])
    o = b.find(struct.pack("<I", frames), 26)
    keys = list(struct.unpack_from("<5I", b, o - 8)) if 34 <= o and o + 12 <= len(b) else None
    nm = label.rsplit("/", 1)[-1].split(".")[0]
    out = {"_binary_format": "Bowling replay (.bwr)",
           "_note": "A recorded bowling throw (BowlingReplay/lane<n>/AutoPlay_...: the throws the game plays by itself). "
                    "Decoded: the header and the 10 pins' flags; the frame-by-frame ball and pin movement after them "
                    "isn't decoded yet. The file name gives the lane, throw, total, pitch and pin set.",
           "name_parts": _bwr_name(nm),
           "version_or_id": ver, "frames": frames, "seconds_at_60fps": round(frames / 60, 2),
           "header_value": round(f0, 6), "pins_flags": pins}
    if keys: out["key_frames"] = keys
    return out


def read_probe_meta(b):
    """.bkprmb: the baked light-probe grid of one scene - where it starts, the block size and how many blocks"""
    f = struct.unpack_from("<4f", b, 0x10); n = struct.unpack_from("<3I", b, 0x20)
    rest = [x for x in struct.unpack_from("<%dI" % ((len(b) - 0x2C) // 4), b, 0x2C)]
    return {"_binary_format": "Baked light probes - scene info (.bkprmb)",
            "_note": "The light-probe grid of one scene (Bake/Probe/Scene/<scene>/): the grid's corner, the size of one "
                     "block and how many blocks there are in x, y and z - one X<x>_Y<y>_Z<z>_<n>.bkprb file each. "
                     "resident.bkprcb holds the coarse probes kept loaded all the time. The other numbers aren't named yet.",
            "version": struct.unpack_from("<I", b, 0)[0], "origin": [round(f[0], 3), round(f[1], 3), round(f[2], 3)],
            "block_size": round(f[3], 3), "blocks_xyz": list(n), "other_values": rest}


def read_probe_block(b, ext):
    """.bkprb / .bkprcb: light-probe cells - each names its 8 corner probes, then its place and size in the block"""
    n = struct.unpack_from("<I", b, 0x10)[0]
    if not 0 < n < 1000000 or 0x14 + 48 * n > len(b): raise ValueError("cell count %d doesn't fit" % n)
    cells = []
    for i in range(min(n, 2000)):
        r = struct.unpack_from("<8I4f", b, 0x14 + 48 * i)
        cells.append({"corner_probes": list(r[:8]), "offset": [round(x, 4) for x in r[8:11]], "size": round(r[11], 4)})
    return {"_binary_format": "Baked light probes - %s (.%s)" % ("resident (coarse) probes" if ext == "bkprcb" else "one block", ext),
            "_note": "Light probes baked for one block of a scene's grid (see meta.bkprmb): a list of cells, each naming "
                     "the 8 probes at its corners and its place / size inside the block (0-1). Decoded: the first cell "
                     "list; the probes' light values after it aren't decoded yet.",
            "version": struct.unpack_from("<I", b, 0)[0], "cells_in_first_list": n,
            "cells": cells, "decoded_bytes": 0x14 + 48 * n, "size": len(b)}


def read_nfib(b):
    """NFIB (Lib/NintendoSDK/IconBody.dat): the bodies the SDK draws under Mii heads in icons"""
    ver, size1, hdr = struct.unpack_from("<III", b, 4)
    m1 = struct.unpack_from("<12I", b, 0x10); bb = struct.unpack_from("<6f", b, 0x40)
    out = {"_binary_format": "NFIB Mii icon body (Nintendo SDK)",
           "_note": "The 3D bodies the system's Mii library draws under a Mii's head for icons (Lib/NintendoSDK, part of "
                    "every game using Mii icons). Two meshes with half-float vertex positions; decoded: the header, the "
                    "data sizes and the first body's bounding box.",
           "version": ver, "header_size": hdr, "mesh_data_sizes": [list(m1), list(struct.unpack_from("<12I", b, 0x60))],
           "bounds_min": [round(x, 3) for x in bb[:3]], "bounds_max": [round(x, 3) for x in bb[3:]],
           "vertices_first_body": m1[1] // 8 if m1[1] % 8 == 0 else None}
    return out


def read_opentype(b):
    out = {"_binary_format": "OpenType font (.otf)", "_note": "A normal OpenType (CFF) font, not scrambled: it opens in "
           "Windows and any font program. Its names, version, designer / copyright text and characters are listed."}
    out.update(_sfnt_names(b))
    try: out.update(_sfnt_chars(b))
    except (struct.error, IndexError): pass
    return out


_VT_LANGS = {"BPT": "Portuguese (Brazil)", "POR": "Portuguese (Portugal)", "BRE": "English (UK)", "ENG": "English (US)",
             "CFR": "French (Canada)", "FRE": "French (France)", "ESP": "Spanish (Spain)", "SPA": "Spanish (Latin America)",
             "GER": "German", "ITA": "Italian", "DUT": "Dutch", "POL": "Polish", "RUS": "Russian", "JPN": "Japanese",
             "KOR": "Korean", "CHI": "Chinese (Mandarin)", "TWN": "Chinese (Taiwan)", "THA": "Thai"}
# the text encoding each language's VoiceText files use (vtpath.ini's CodePage)
_VT_CODEPAGE = {"POL": "cp1250", "RUS": "cp1251", "THA": "cp874", "JPN": "cp932", "CHI": "cp936", "TWN": "cp950", "KOR": "cp949"}


def read_voicetext(b, label):
    """VoiceText (ReadSpeaker) speech-engine data under romfs/VoiceText: identified, word lists decoded"""
    parts = label.split("/")
    low = [x.lower() for x in parts]
    i = low.index("vt") if "vt" in low else -1
    lang = parts[i + 1] if 0 <= i < len(parts) - 1 else None
    voice = parts[i + 2] if 0 <= i < len(parts) - 2 else None
    name = parts[-1]; ext = _ext(label)
    out = {"_binary_format": "VoiceText speech data",
           "_note": "Data for the VoiceText text-to-speech engine (ReadSpeaker), which reads text aloud: per language "
                    "a dictionary and text rules, per voice its sound model. These are the engine's own formats; "
                    "what can be shown is listed (word lists in full, other files by what they are)."}
    if lang: out["language"] = "%s - %s" % (lang, _VT_LANGS.get(lang.upper(), "?"))
    if voice and "-" in voice or voice and "_" in voice: out["voice"] = voice
    if b[:2] == b"v\x0e" or (len(b) > 4 and b[0:1] == b"v" and 32 <= b[3] < 127):
        strs = [x.decode("latin-1") for x in re.findall(rb"[\x20-\x7e]{3,}", b[:64])]
        out["_binary_format"] = "VoiceText voice model"
        out.update({"engine": next((x for x in strs if x.startswith("vt_")), None), "header_strings": strs})
    elif b[:4] == b"NLP\x01":
        h = struct.unpack_from("<I8I", b, 4)
        out["_binary_format"] = "VoiceText NLP hash table"
        out.update({"hash_seed": "0x%08X" % h[0], "header_values": list(h[1:])})
    elif b[:3] == b"nlp":                               # "nlp" + text with every byte raised by 10
        cp = _VT_CODEPAGE.get((lang or "").upper(), "cp1252")
        t = bytes((x - 10) & 255 for x in b[3:]).decode(cp, "replace")
        w = [x for x in t.replace("\r\n", "\n").split("\n") if x]
        if w and w[0].startswith('"') and w[0].count('","') >= 1:      # a user dictionary: "written","reading","kind"
            import csv as _csv, io as _io
            rows = list(_csv.reader(_io.StringIO("\n".join(w))))
            out["_binary_format"] = "VoiceText user dictionary (lightly scrambled)"
            out.update({"entries": len(rows), "rows": [{"written": r[0], "say_as": r[1] if len(r) > 1 else "",
                        "kind": r[2] if len(r) > 2 else ""} for r in rows if r]})
        else:
            out["_binary_format"] = "VoiceText word list (lightly scrambled)"
            out.update({"words": len(w), "list": w})
    elif b[:3] == b"tts":                               # "tts" + text with every byte raised by 0x61 (+ "tts" at the end)
        body = b[3:-3] if b.endswith(b"tts") else b[3:]
        t = bytes((x - 0x61) & 255 for x in body).split(b"\0")[0].decode("latin-1", "replace")
        lines = [x for x in t.replace("\r\n", "\n").split("\n") if x]
        kind = {"lst": "list", "pat": "phoneme patterns"}.get(ext, "text")
        out["_binary_format"] = "VoiceText %s (lightly scrambled)" % kind
        if ext == "pat":
            out["_note"] += " A pattern table: each line is a phoneme rule with tab-separated fields (the phoneme names are listed in bp.lst)."
            out.update({"lines": len(lines), "rows": [x.split("\t") for x in lines]})
        else:
            out["_note"] += " bp.lst = the voice's phoneme names; ba.lst = its accent / stress marks."
            out.update({"entries": len(lines), "list": lines})
    elif ext == "csv":
        t = b.decode("utf-8") if text_of(b) is not None else b.decode("cp1252", "replace")
        import csv as _csv, io as _io
        rows = list(_csv.reader(_io.StringIO(t)))
        kinds = {"t": "text replaced by text", "a": "text replaced by spelling", "w": "word with phonemes"}
        out["_binary_format"] = "VoiceText user dictionary (CSV)"
        out["_note"] += " A user dictionary: how the game wants names and words said - written form, how to say it, kind."
        out.update({"entries": len(rows), "rows": [{"written": r[0], "say_as": r[1] if len(r) > 1 else "",
                    "kind": kinds.get(r[2], r[2]) if len(r) > 2 else ""} for r in rows if r]})
    elif b.count(b"\0") > 100 and all(32 <= c < 256 or c == 0 for c in b[:2000]) and b"\0N\0" in b[:2000]:
        p = b.split(b"\0"); words = p[0::2]; tags = p[1::2]
        tc = {}
        for t_ in tags: tc[t_.decode("latin-1")] = tc.get(t_.decode("latin-1"), 0) + 1
        out["_binary_format"] = "VoiceText word list with tags"
        out.update({"words": len([w for w in words if w]), "first": [w.decode("latin-1") for w in words[:30]],
                    "last": [w.decode("latin-1") for w in words if w][-10:],
                    "tags": dict(sorted(tc.items(), key=lambda kv: -kv[1])[:30])})
    elif ext == "win":
        out["_binary_format"] = "VoiceText window table"
        out["values"] = list(struct.unpack_from("<%dH" % (len(b) // 2), b, 0))
    elif ext == "tree3":
        out["_binary_format"] = "VoiceText decision tree"
        out["first_value"] = struct.unpack_from("<I", b, 0)[0]
    elif ext == "rs":
        out["_binary_format"] = "VoiceText compiled dictionary"
    else:
        out["_binary_format"] = "VoiceText engine data (%s)" % (ext or "no extension")
    out["file"] = name
    return out


def identify_nssr(label, blob, ext, name):
    if blob is None: return None
    low = "/" + label.lower()
    if ext == "dat" and len(blob) == 0x58 and "/mii/" in low:
        return read_mii_charinfo(blob)
    if ext == "gswg": return read_gswg(blob)
    if ext == "bwr" and len(blob) >= 64: return read_bwr(blob, label)
    if ext == "bkprmb" and len(blob) >= 0x2C: return read_probe_meta(blob)
    if ext in ("bkprb", "bkprcb") and len(blob) >= 0x44: return read_probe_block(blob, ext)
    if blob[:4] == b"NFIB" and len(blob) >= 0xA8: return read_nfib(blob)
    if blob[:4] == b"OTTO" and ext in ("otf", "ttf"): return read_opentype(blob)
    if "/voicetext/" in low:
        if ext == "listing" or (ext != "csv" and text_of(blob) is not None):   # folder listings and plain text: as usual
            return None
        return read_voicetext(blob, label)
    return None


def identify_special(label, blob, head):
    """-> JSON dict for formats recognised by name and contents, or None"""
    ext = _ext(label)
    name = label.rsplit("/", 1)[-1].lower()
    if blob is not None and blob[:5] == b"VGEOB" and len(blob) >= 9:
        return read_vgeob(blob)
    try:
        r = identify_nssr(label, blob, ext, name)
        if r is not None:
            return r
    except Exception as e:
        nm = {"bwr": "Bowling replay", "gswg": "Golf swing replay", "bkprmb": "Baked light probes - scene info",
              "bkprb": "Baked light probes - one block", "bkprcb": "Baked light probes - resident", "dat": "Mii character",
              "otf": "OpenType font"}.get(ext, "VoiceText speech data" if "/voicetext/" in "/" + label.lower() else (ext or "file") + " file")
        return {"_binary_format": "%s (couldn't be read: %s)" % (nm, e), "_first_bytes": bytes(head[:16]).hex(" ")}
    try:
        if blob is not None and blob[:4] == b"TARB":
            return read_tarb(blob, label)
        if blob is not None and blob[:4] == b"DFvN":
            return read_xtx(blob)
        if blob is not None and blob[:4] == b"ATRA":
            return read_atra(blob)
        if blob is not None and blob[:4] == b"GCCP":
            return read_gccp(blob)
        if blob is not None and blob[:4] == b"AAHS":
            return read_sharc_aahs(blob)
        if blob is not None and blob[:4] == b"TSCB":
            return read_tscb(blob)
        if blob is not None and blob[:4] in _DLP_KINDS:
            return read_dlp(blob)
        if blob is not None and blob[:4] == b"AIB ":
            return read_ainb(blob)
        if blob is not None and ext == "ghost" and len(blob) >= 0x24:
            return read_ghost(blob)
        if blob is not None and ext in ("bcul", "bbox", "bgrd", "dapg"):
            r = read_cull_ext(blob, ext)
            if r is not None:
                return r
        if blob is not None and name.endswith("_tbl.bin") and blob[:2] == b"\x20\x00":
            return read_char_table(blob)
    except Exception as e:
        tag = blob[:4].decode("latin-1") if blob else ""
        if not re.fullmatch(r"[A-Za-z0-9 _]{4}", tag): tag = (ext or "file") + " file"   # (raw bytes in a name break the log)
        return {"_binary_format": "%s (couldn't be read: %s)" % (tag, e), "_first_bytes": bytes(head[:16]).hex(" ")}
    if blob is not None and blob[:4] == b"FLYT":
        return read_bflyt(blob)
    if blob is not None and blob[:4] == b"BNTX":
        return read_bntx(blob)
    if blob is not None and blob[:4] == b"FLAN":
        return read_bflan(blob)
    if blob is not None and blob[:6] == b"Phive\0" and ext == "bphcl":
        try:
            return read_bphcl(blob)
        except Exception as e:
            return {"_binary_format": "Phive cloth (couldn't be read: %s)" % e, "_first_bytes": head[:16].hex(" ")}
    if blob is not None and blob[:6] == b"Phive\0" and len(blob) > 6 and blob[6] == 2:
        try:
            return read_bphsc(blob)
        except Exception as e:
            return {"_binary_format": "Phive v2 (couldn't be read: %s)" % e, "_first_bytes": head[:16].hex(" ")}
    if blob is not None and blob[:6] == b"Phive\0":
        return read_bphsh(blob)
    if blob is not None and blob[:4] in (b"AATN", b"AROC", b"AUDC", b"AACL") and blob[4:6] == b"\xff\xfe":
        try:
            return {b"AATN": read_aatn, b"AROC": read_aroc, b"AUDC": read_audc, b"AACL": read_aacl}[blob[:4]](blob)
        except Exception as e:
            return {"_binary_format": "sound attenuation (couldn't be read: %s)" % e, "_first_bytes": head[:16].hex(" ")}
    if blob is not None and blob[:8] == b"MsgPrjBn":
        try:
            return read_msbp(blob)
        except Exception as e:
            return {"_binary_format": "MSBP message project (couldn't be read: %s)" % e, "_first_bytes": head[:16].hex(" ")}
    if blob is not None and blob[:4] == b"META":
        return read_npdm(blob)
    if blob is not None and blob[:4] == b"BARS":
        return read_bars(blob)
    if blob is not None and blob[:4] == b"BWAV":
        return read_bwav(blob)
    if blob is not None and (blob[:4] == b"ASB " or ext in ("asb", "baev")) and len(blob) >= 16:
        try:
            return read_asb_like(blob, ext)
        except Exception as e:
            return {"_binary_format": "animation graph (couldn't be read: %s)" % e, "_first_bytes": head[:16].hex(" ")}
    if blob is not None and blob[:4] == b"BNSH":
        try:
            return read_bnsh(blob)
        except Exception as e:
            return {"_binary_format": "BNSH shader container (couldn't be read: %s)" % e, "_first_bytes": head[:16].hex(" ")}
    if blob is not None and blob[:8] == b"FSHA    ":
        try:
            return read_bfsha(blob)
        except Exception as e:
            return {"_binary_format": "BFSHA material shader archive (couldn't be read: %s)" % e, "_first_bytes": head[:16].hex(" ")}
    if blob is not None and blob[:4] == b"Gfx2":
        try:
            return read_gsh(blob)
        except Exception as e:
            return {"_binary_format": "GSH Wii U shader (couldn't be read: %s)" % e, "_first_bytes": head[:16].hex(" ")}
    if blob is not None and blob[:4] == b"NDSR":
        try:
            return read_ndsr(blob)
        except Exception as e:
            return {"_binary_format": "NDSR GPU pack (couldn't be read: %s)" % e, "_first_bytes": head[:16].hex(" ")}
    if blob is not None and blob[:4] == b"XLNK":
        try:
            return read_xlnk(blob)
        except Exception as e:
            return {"_binary_format": "XLNK effect / sound links (couldn't be read: %s)" % e, "_first_bytes": head[:16].hex(" ")}
    if blob is not None and blob[:4] == b"AGST":
        try:
            return read_agst(blob)
        except Exception as e:
            return {"_binary_format": "AGST sound group settings (couldn't be read: %s)" % e, "_first_bytes": head[:16].hex(" ")}
    if blob is not None and blob[:4] == b"BAHS":
        try:
            return read_sharcb(blob)
        except Exception as e:
            return {"_binary_format": "SHARCB agl shader archive (couldn't be read: %s)" % e, "_first_bytes": head[:16].hex(" ")}
    if blob is not None and blob[:8] == b"BFEVFL\0\0":
        try:
            return read_bfevfl(blob)
        except Exception as e:
            return {"_binary_format": "BFEVFL event flow (couldn't be read: %s)" % e, "_first_bytes": head[:16].hex(" ")}
    if blob is not None and blob[:4] == b"AAMP":
        try:
            o = read_aamp(blob)
            if o.get("type") == "aglcube": _cube_summary(o)
            if o.get("type") == "aglmf": _mf_summary(o)
            if o.get("type") == "gsdw": _gsdw_summary(o)
            if o.get("type") == "aglvolm": _volm_summary(o)
            if o.get("type") == "gmsconf": _gmsconf_summary(o)
            # a summary wrote its own description: drop the short kind description from the generic text
            k = o.get("type")
            n = o.get("_note", "")
            if k in AAMP_KINDS and n.count("Nintendo parameter archive") == 1 and not n.startswith("Nintendo parameter archive"):
                o["_note"] = n.replace("Nintendo parameter archive: " + AAMP_KINDS[k], "Nintendo parameter archive")
            if o.get("type") == "aglofx": _ofx_summary(o)
            if o.get("type") == "phhb":
                try: _phhb_summary(o)
                except Exception: pass
            if o.get("type") == "aglfila":
                o["_note"] = ("Anti-aliasing filter settings (agl FilterAA): FXAA edge smoothing, with optional "
                              "reprojection (reusing the previous frame). Only some setting names are known: subpix_param, "
                              "max_span, fxaa_reprojection, fxaa_reprojection_move_limit; the rest show as hashes. " + o.get("_note", ""))
            return o
        except Exception as e:
            return {"_binary_format": "AAMP parameters (couldn't be read: %s)" % e, "_first_bytes": head[:16].hex(" ")}
    if blob is not None and blob[:4] == b"FRES":
        try:
            return read_bfres(blob)
        except Exception as e:      # an unusual file shouldn't stop the extraction
            return {"_binary_format": "BFRES graphics resource (couldn't be read fully: %s)" % e, "_first_bytes": head[:16].hex(" ")}
    try:
        if blob is not None and blob[:4] == b"FCPX":
            return read_bfcpx(blob)
        if blob is not None and blob[:4] == b"FFNT":
            return read_bffnt(blob)
    except (struct.error, ValueError) as e:
        return {"_binary_format": "font (couldn't be read: %s)" % e, "_first_bytes": head[:16].hex(" ")}
    try:
        if ext == "bnvib" and blob is not None and len(blob) >= 12:
            return read_bnvib(blob)
        if ext in ("bfttf", "bfotf") and blob is not None:
            return read_nintendo_font(blob, ext)
    except struct.error as e:
        return {"_binary_format": "BNVIB / font (couldn't be read: %s)" % e, "_first_bytes": head[:16].hex(" ")}
    if ext == "bushvt":
        out = {"_binary_format": "BUSHVT (UI layout shader variation table)",
               "_note": "Part of a layout archive's shader set (bgsh/__ArchiveShader): lists the compiled "
                        "shader variations the layout uses. The first number is probably how many there are."}
        if len(head) >= 4:
            out["first_value"] = struct.unpack_from("<I", head, 0)[0]
        return out
    if ext == "bin" and "shader" in name and blob is not None:
        try:
            return read_sead_shader_bin(blob)
        except Exception:
            pass
    if ext == "bin" and "shader" in name:
        return {"_binary_format": "Compiled GPU shader (NVN)",
                "_note": "Compiled shader code for the Switch 2 graphics API, used by the engine to draw simple shapes."}
    if name == ".listing" and blob is not None and blob[:1] in (b"d", b"-", b"l"):
        folders, files = [], []
        for ln in blob.decode("utf-8", "replace").splitlines():
            m = re.match(r"([dl-])\S+\s+\d+\s+\S+\s+\S+\s+(\d+)\s+\w+\s+\d+\s+[\d:]+\s+(.+)$", ln.strip())
            if not m: continue
            if m.group(1) == "d": folders.append(m.group(3))
            else: files.append("%s (%s bytes)" % (m.group(3), m.group(2)))
        return {"_binary_format": "Folder listing (made by the FTP dumping tool, not a game file)",
                "_note": "The Switch 2 dumping tool's FTP server writes one of these in each folder it sends: an 'ls -l' "
                         "style list of that folder. It isn't part of the game; the sizes are the files' sizes on the "
                         "cartridge (compressed, for .zs).",
                "folders": folders, "files": files}
    if ext == "raw" and blob is not None:
        s = int(round((len(blob) / 4) ** 0.5))
        if s >= 4 and s * s * 4 == len(blob):
            ramp = " .:-=+*#%@"
            art = ["".join(ramp[min(9, blob[(y * s + x) * 4 + 3] * 10 // 256)] for x in range(s)).rstrip() for y in range(s)]
            used = [blob[i:i + 4] for i in range(0, len(blob), 4) if blob[i + 3]]
            return {"_binary_format": "Raw image (%dx%d RGBA, 8 bits per channel)" % (s, s),
                    "_note": "An uncompressed square picture with no header. agl's arrow.raw is the mouse pointer (white with a "
                             "grey edge) - probably for the debug menus or mouse support. Select the file and press "
                             "\"Textures\" to see it and save it as PNG. 'picture' shows its opacity as text.",
                    "visible_pixels": len(used),
                    "picture": art}
    if ext == "raw" and "/agl" in "/" + label.lower():
        return {"_binary_format": "Raw data (AGL graphics library)",
                "_note": "Raw data with no header; not recognised."}
    return None


# ---------- identification ----------
LONG_MAGICS = (b"VGEOB",)


def kind_of(head):
    if head[:4] == b"SARC": return "SARC"
    if head[:2] in (b"YB", b"BY"): return "BYML"
    if head[:8] == b"MsgStdBn": return "MSBT"
    if head[:4] == b"\x37\xa4\x30\xec": return "zstd dictionary"
    if head[:4] == b"\x1a\x45\xdf\xa3": return "WebM/Matroska video (EBML)"
    if head and all(32 <= c < 127 or c in (9, 10, 13) for c in head): return "text"
    for m in LONG_MAGICS:                       # labels longer than 4 letters
        if head[:len(m)] == m:
            return m.decode("ascii")
    n = 0
    while n < min(4, len(head)) and (48 <= head[n] <= 57 or 65 <= head[n] <= 90 or 97 <= head[n] <= 122):
        n += 1
    if n >= 2:
        # 4 letters at most: the bytes after a label are usually numbers (version, size...) that can
        # look like letters, which used to split one format into many (VGEOBI, VGEOB5t, ...)
        return head[:n].decode("ascii")
    return "unknown (%s)" % head[:4].hex(" ") if head else "empty"


_BAD = re.compile(r'[<>:"|?*\x00-\x1f]')


def long_path(p):
    """Allow paths over 260 characters on Windows."""
    p = os.path.abspath(p)
    if os.name == "nt" and not p.startswith("\\\\?\\"):
        p = "\\\\?\\UNC\\" + p[2:] if p.startswith("\\\\") else "\\\\?\\" + p
    return p


class Dumper:
    def __init__(self, out_root):
        self.out_root = out_root
        self.stats = {}
        self.problem_examples = {}   # "<type> - couldn't be read" -> first file
        self.exts = {}      # binary kind -> {extension: [count, example path]}
        self.dicts = {}
        self.written = 0
        self.labels = []        # every game path seen (for naming RESTBL hashes)
        self.restbls = []       # (label, blob) to name at the end

    def count(self, k, n=1):
        self.stats[k] = self.stats.get(k, 0) + n

    # --- output: one JSON file per game file ---
    def entry(self, label, obj):
        self.labels.append(label)
        parts = [_BAD.sub("_", x).rstrip(" .") or "_" for x in label.split("/") if x not in ("", ".", "..")]
        target = long_path(os.path.join(self.out_root, *parts) + ".json")
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "w", encoding="utf-8") as f:
            json.dump(obj, f, indent=2, ensure_ascii=False)
        self.written += 1

    def note_binary(self, label, kind, size, head, zstd):
        # files with no recognised header: one group for all of them (split by extension below), not one per first 4 bytes
        ckind = "unknown (no recognised header)" if kind.startswith("unknown (") else kind
        self.count(ckind)
        name = label.rsplit("/", 1)[-1]
        parts = name.split(".")
        ext = "(no extension)"
        if len(parts) > 1:
            ext = "." + parts[-1]
            if parts[-1].lower() == "zs" and len(parts) > 2: ext = "." + parts[-2] + ".zs"
        e = self.exts.setdefault(ckind, {}).setdefault(ext.lower(), [0, label])
        e[0] += 1
        info = {"_binary_format": kind, "_first_bytes": head[:16].hex(" ")}
        if size is not None: info["_size"] = size
        if zstd: info["_zstd_compressed"] = True
        self.entry(label, info)

    def problem(self, label, msg, head=b""):
        self.count("errors")
        self.entry(label, {"_error": msg, "_first_bytes": head[:16].hex(" ")})
        print("  [error] %s: %s" % (label, msg))

    # --- zstd ---
    def load_dict_blob(self, blob, label):
        try:
            d = zstandard.ZstdCompressionDict(blob)
            did = d.dict_id()
            if did and did not in self.dicts:
                self.dicts[did] = d
                print("Loaded zstd dictionary %s (id %d)" % (label, did))
        except Exception:
            pass

    def decompressor(self, head):
        did = zstandard.get_frame_parameters(head).dict_id
        if did == 0:
            return zstandard.ZstdDecompressor()
        if did not in self.dicts:
            raise LookupError("needs zstd dictionary id %d (not found)" % did)
        return zstandard.ZstdDecompressor(dict_data=self.dicts[did])

    def unzstd(self, blob):
        return self.decompressor(blob[:32]).decompress(blob, max_output_size=MAX_FULL_READ)

    def peek_file(self, path):
        with open(path, "rb") as f:
            raw = f.read(32)
            if raw[:4] != ZSTD_MAGIC:
                return raw[:16], False
            f.seek(0)
            with self.decompressor(raw).stream_reader(f) as r:
                return r.read(16), True

    # --- voxel terrain parts ---
    def handle_voxel(self, path, label, head):
        name = os.path.basename(path).lower()
        if name == "materialtable.bin":
            names = voxel_materials(os.path.dirname(path))
            if not names:
                return False
            self.entry(label, {"_binary_format": "Voxel material table",
                               "_note": "Materials used by the voxel terrain of this stage scenario. A voxel's material id "
                                        "(in the vbody.bin files of the part folders here) is the number in front of the name.",
                               "materials": {str(i): n for i, n in enumerate(names)}})
            self.count("voxel material tables")
            return True
        info = voxel_info(path)
        if name == "info.bin":
            if not info:
                return False
            self.entry(label, {"_binary_format": "Voxel part info (VX)",
                               "_note": "Size of this terrain part in voxels and in 8x8x8 blocks, and the size of one voxel. "
                                        "The other files in this folder hold the voxels (vhead/vbody) and surface edges (ehead/ebody).",
                               "size_voxels_xyz": info["size"], "size_blocks_xyz": info["blocks"],
                               "voxel_scale_xyz": info["scale"], "other_fields_unknown": info["other_fields"],
                               "_note_other_fields": "bytes 2-11; non-zero only in parts that use 5-byte voxels (meaning unknown)"})
            self.count("voxel part info (VX)")
            return True
        grid = info["blocks"] if info else None
        kind = name[0]                     # 'v' or 'e'
        if name.endswith("head.bin"):
            with open(long_path(path), "rb") as f:
                h = f.read()
            idx = voxel_bitmap(h, grid)
            out = {"_binary_format": "Voxel block bitmap (%s)" % ("voxels" if kind == "v" else "edges"),
                   "_note": "1 bit per 8x8x8 block of the part (x fastest, then y, then z); a set bit means %s.bin "
                            "has data for that block. Positions are in block units (multiply by 8 for voxels)." % (kind + "body"),
                   "blocks_with_data": len(idx)}
            if grid:
                bx, by, bz = grid
                out["grid_blocks_xyz"] = grid
                if idx:
                    xs = [i % bx for i in idx]; ys = [(i // bx) % by for i in idx]; zs = [i // (bx * by) for i in idx]
                    out["blocks_min_xyz"] = [min(xs), min(ys), min(zs)]
                    out["blocks_max_xyz"] = [max(xs), max(ys), max(zs)]
                # one line per z layer, so it stays readable and searchable
                rows = {}
                for i in idx:
                    rows.setdefault(i // (bx * by), []).append("%d,%d" % (i % bx, (i // bx) % by))
                out["blocks_by_z_note"] = "for each z (block units): the x,y block positions that have data"
                out["blocks_by_z"] = {str(z): " ".join(v) for z, v in sorted(rows.items())}
            self.entry(label, out)
            self.count("voxel block bitmaps")
            return True
        # body
        hpath = os.path.join(os.path.dirname(path), kind + "head.bin")
        with open(long_path(path), "rb") as f:
            body = f.read()
        blocks, used = voxel_blocks(body, zstandard.ZstdDecompressor())
        idx = []
        try:
            with open(long_path(hpath), "rb") as f:
                idx = voxel_bitmap(f.read(), grid)
        except OSError:
            pass
        out = {"_binary_format": "Voxel data (%s)" % ("voxels" if kind == "v" else "surface edges, 9 bytes each"),
               "blocks": len(blocks)}
        if used != len(body):
            out["_warning"] = "body has %d bytes left over after the last block" % (len(body) - used)
        if idx and len(idx) != len(blocks):
            out["_warning_head"] = "%s has %d blocks but %s has %d" % (kind + "head.bin", len(idx), kind + "body.bin", len(blocks))
        if kind == "v":
            mats = voxel_materials(os.path.dirname(os.path.dirname(path)))
            def mname(m):
                return "%d %s" % (m, mats[m]) if mats and m < len(mats) else str(m)
            stride = (len(blocks[0]) // 512) if blocks and len(blocks[0]) % 512 == 0 else 8
            out["bytes_per_voxel"] = stride
            out["_note"] = ("Each voxel is %d bytes. Byte 0 = how full it is (0 empty, 255 solid, in between = on the surface). "
                            "Bytes 1 and 2 = material ids from MaterialTable.bin in the folder above: byte 1 is the voxel's material; "
                            "byte 2 is a second material (in 8-byte parts it is set almost everywhere byte 1 is, in 5-byte "
                            "parts only where materials meet - probably for blending). "
                            "Bytes 3 and up are unknown; their most common values are listed." % stride)
            fill = collections.Counter(); m1 = collections.Counter(); m2 = collections.Counter()
            extra = {k: collections.Counter() for k in range(3, stride)}
            per_block = []
            bx, by = (grid[0], grid[1]) if grid else (0, 0)
            for n, c in enumerate(blocks):
                if len(c) != 512 * stride:
                    continue
                f0 = collections.Counter(c[0::stride])
                fill.update(f0)
                bm = collections.Counter(c[1::stride]); m1.update(bm); m2.update(c[2::stride])
                for k in extra:
                    extra[k].update(c[k::stride])
                if idx and grid and n < len(idx):
                    i = idx[n]
                    top = [mname(m) for m, _ in bm.most_common() if m][:3]
                    per_block.append("[%d,%d,%d]  solid %d  surface %d  %s" % (
                        i % bx, (i // bx) % by, i // (bx * by), f0.get(255, 0),
                        512 - f0.get(0, 0) - f0.get(255, 0), ", ".join(top)))
            full, empty = fill.get(255, 0), fill.get(0, 0)
            surf = sum(fill.values()) - full - empty
            out["voxels"] = {"solid (255)": full, "surface (1-254)": surf, "empty (0)": empty}
            if info:
                sx, sy, sz = info["scale"]
                out["solid_volume_approx"] = round((full + surf / 2) * sx * sy * sz, 1)
            if not mats:
                out["_materials"] = "MaterialTable.bin not found in the folder above, so materials are shown as numbers only"
            out["materials_byte1"] = {mname(k): v for k, v in m1.most_common() if k}
            out["materials_byte2"] = {mname(k): v for k, v in m2.most_common() if k}
            ex = {}
            for k, cnt in extra.items():
                nz = sum(v for val, v in cnt.items() if val)
                if nz:
                    ex["byte %d" % k] = {"non_zero_voxels": nz,
                                         "common_values": {str(val): v for val, v in cnt.most_common(9) if val}}
            if ex:
                out["other_bytes"] = ex
            if per_block:
                out["per_block_note"] = ("[x,y,z] = block position (multiply by 8 for voxels), then solid and surface "
                                         "voxel counts (out of 512) and the block's main materials")
                out["per_block"] = per_block
            self.count("voxel data (voxels)")
        else:
            out["_note"] = ("Each voxel is 9 bytes: bytes 0-2 are probably where the surface crosses the voxel's "
                            "x, y and z edges (0 = no crossing, else position along the edge out of 255), and bytes 3-8 "
                            "the surface normal at each crossing (2 bytes each). Used to build the terrain mesh.")
            cross = [0, 0, 0]
            for c in blocks:
                if len(c) != 4608:
                    continue
                for a in range(3):
                    cross[a] += 512 - c[a::9].count(0)
            out["edge_crossings_xyz"] = cross
            self.count("voxel data (edges)")
        self.entry(label, out)
        return True

    # --- formats recognised by name and contents, and plain text ---
    def handle_special(self, label, blob, head, kind, size=None):
        if blob is not None and blob[:6] == b"RESTBL":
            try:
                info = read_restbl(blob)
                self.restbls.append((label, blob))
            except Exception as e:
                info = {"_binary_format": "RESTBL resource size table (couldn't be read: %s)" % e, "_first_bytes": head[:16].hex(" ")}
            self.count("RESTBL resource size table")
            self.entry(label, info)
            return True
        try:
            info = identify_special(label, blob, head)
        except MemoryError:
            info = {"_binary_format": "%s (couldn't be read: ran out of memory - layout not understood)" % _ext(label),
                    "_first_bytes": bytes(head[:16]).hex(" ")}
        if info is not None:
            bf = info["_binary_format"]
            key = "fonts (Nintendo obfuscated)" if "obfuscated" in bf and "font" in bf else bf.split(" (")[0]
            if "couldn't be read" in bf or "_warning" in info:
                key = bf.split(" (")[0] + (" - couldn't be read" if "couldn't be read" in bf else " - partly read (warning)")
                self.problem_examples.setdefault(key, label)
            self.count(key)
            if "_size" not in info and (size is not None or blob is not None):
                info["_size"] = size if size is not None else len(blob)
            self.entry(label, info)
            return True
        if blob is not None and (kind == "text" or blob[:3] == b"\xef\xbb\xbf"):
            t = text_of(blob)
            if t is not None:
                ext = _ext(label)
                fmt = {"glsl": "GLSL shader source (text)", "txt": "text", "csv": "CSV text", "xml": "XML text",
                       "json": "JSON text", "yaml": "YAML text", "yml": "YAML text", "ini": "INI text"}.get(ext, "text")
                self.entry(label, {"_binary_format": fmt, "_size": len(blob),
                                   "lines": t.replace("\r\n", "\n").split("\n")})
                self.count("GLSL shader source" if ext == "glsl" else "text files")
                return True
        return False

    # --- content ---
    def handle_blob(self, label, blob, depth=0):
        if blob[:4] == ZSTD_MAGIC:
            try:
                blob = self.unzstd(blob)
            except Exception as e:
                self.problem(label, "zstd: %s" % e, blob)
                return
        kind = kind_of(blob[:16])
        if kind == "SARC" and depth < 8:
            try:
                files = read_sarc(blob)
            except Exception as e:
                self.problem(label, "SARC: %s" % e, blob)
                return
            self.count("SARC archives (opened as folders)")
            self.labels.append(label)           # (its own path: the resource size table lists archives too)
            for name in sorted(files):
                self.handle_blob(label + "/" + name, files[name], depth + 1)
        elif kind == "BYML":
            try:
                self.entry(label, read_byml(blob))
                self.count("BYML")
            except Exception as e:
                self.problem(label, "BYML: %s" % e, blob)
        elif kind == "MSBT":
            try:
                self.entry(label, read_msbt(blob))
                self.count("MSBT text")
            except Exception as e:
                self.problem(label, "MSBT: %s" % e, blob)
        else:
            if label.lower().endswith(".zsdic"):
                self.load_dict_blob(blob, label)
            elif self.handle_special(label, blob, blob[:16], kind):
                return
            self.note_binary(label, kind, len(blob), blob, False)

    def handle_file(self, path, label):
        try:
            head, zstd = self.peek_file(path)
        except Exception as e:
            self.problem(label, str(e))
            return
        kind = kind_of(head)
        if kind == "NSO0":
            try:
                with open(path, "rb") as f:
                    blob = f.read()
                print("  decompressing executable %s ..." % label)
                info = read_nso(blob)
                self.entry(label, info)
                self.count("NSO executables (decoded)")
                if "_warning" in info and "strings" not in info:
                    print("  %s: compressed with ZBIC - run  pip install zbic  to decode it (header saved)" % label)
                else:
                    print("  %s: %d exported / %d imported symbols, %d strings" % (
                        label, info.get("exported_symbol_count", 0), info.get("imported_symbol_count", 0), len(info.get("strings", []))))
            except Exception as e:
                self.problem(label, "NSO: %s" % e, head)
            return
        if os.path.basename(path).lower() in VOXEL_FILES and "/voxel/" in ("/" + label.lower()):
            try:
                if self.handle_voxel(path, label, head):
                    return
            except Exception as e:
                self.problem(label, "voxel: %s" % e, head)
                return
        if kind in ("SARC", "BYML", "MSBT") or label.lower().endswith(".zsdic"):
            if os.path.getsize(path) > MAX_FULL_READ:
                self.note_binary(label, kind + " (too large, skipped)", None, head, zstd)
                return
            with open(path, "rb") as f:
                self.handle_blob(label, f.read())
        else:
            blob = None
            # models/animations can be big (a character's full move set): allow them more room than other files
            limit = 400 * 1024 * 1024 if _ext(label) in ("bfres", "asb", "baev", "bars", "bntx") else 32 * 1024 * 1024
            vt = "/voicetext/" in "/" + label.lower()        # VoiceText speech data: any name (many have no extension)
            if (_ext(label) in SPECIAL_EXTS or vt or kind == "text" or head[:3] == b"\xef\xbb\xbf") and os.path.getsize(path) > limit:
                self.note_binary(label, kind + " (too large to read here: %d MB)" % (os.path.getsize(path) >> 20), None, head, zstd)
                return
            if (_ext(label) in SPECIAL_EXTS or vt or kind == "text" or head[:3] == b"\xef\xbb\xbf"):
                with open(path, "rb") as f:
                    # a plain BWAV only needs its header (channel info) - saves reading GBs of audio
                    blob = f.read(0x10 + 16 * 0x4C) if (_ext(label) == "bwav" and not zstd) else f.read()
                if zstd:
                    try:
                        blob = self.unzstd(blob)
                    except Exception:
                        blob = None
            if self.handle_special(label, blob, head if blob is None else blob[:16], kind,
                                   None if zstd else os.path.getsize(path)):
                return
            self.note_binary(label, kind, None if zstd else os.path.getsize(path), head, zstd)


def find_dictionaries(dumper, paths, root_dir):
    for p in paths:
        if "zsdic" not in os.path.basename(p).lower():
            continue
        try:
            with open(p, "rb") as f:
                blob = f.read()
            if blob[:4] == ZSTD_MAGIC:
                blob = zstandard.ZstdDecompressor().decompress(blob, max_output_size=MAX_FULL_READ)
            if blob[:4] == b"SARC":
                for name, data in read_sarc(blob).items():
                    if name.lower().endswith(".zsdic"):
                        dumper.load_dict_blob(data, name)
            else:
                dumper.load_dict_blob(blob, os.path.relpath(p, root_dir))
        except Exception as e:
            print("Could not load dictionary %s: %s" % (p, e))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    if len(sys.argv) > 3:
        sys.exit("ERROR: expected 2 arguments (input folder, output folder) but got %d:\n  %s"
                 % (len(sys.argv) - 1, "\n  ".join(sys.argv[1:])))
    src = sys.argv[1]
    if not os.path.exists(src):
        sys.exit("ERROR: the input folder does not exist: %s" % src)
    out_root = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else "alsarc_out")
    # folders never scanned: our own output (all scans live in its parent) and Windows system folders
    skip = {os.path.normcase(out_root), os.path.normcase(os.path.dirname(out_root))}
    skip_names = {"$recycle.bin", "system volume information"}
    if os.path.isdir(src):
        root_dir = src
        paths = []
        for root, dirs, names in os.walk(src):
            dirs[:] = sorted(d for d in dirs
                             if os.path.normcase(os.path.abspath(os.path.join(root, d))) not in skip
                             and d.lower() not in skip_names)
            paths += [os.path.join(root, n) for n in names]
    else:
        root_dir = os.path.dirname(os.path.abspath(src))
        paths = [src]
    paths.sort()
    print("Scanning %d files..." % len(paths))
    os.makedirs(out_root, exist_ok=True)

    d = Dumper(out_root)
    find_dictionaries(d, paths, root_dir)
    last_top = None
    import time
    last_prog = 0.0
    # machine-readable progress for the GUI (hidden from its log)
    print("@@PROGRESS 0 %d" % len(paths), flush=True)
    # The GUI asks us to stop by creating this file; we finish the current file and exit cleanly.
    stop_file = os.environ.get("ALSARC_STOP_FILE")
    stopped = False
    for i, p in enumerate(paths, 1):
        now = time.time()
        if now - last_prog > 0.2:
            print("@@PROGRESS %d %d" % (i - 1, len(paths)), flush=True)
            last_prog = now
            if stop_file and os.path.exists(stop_file):
                stopped = True
                print("\nStopped by user after %d of %d files. Everything extracted so far is kept." % (i - 1, len(paths)), flush=True)
                break
        rel = os.path.relpath(p, root_dir).replace(os.sep, "/")
        top = rel.split("/")[0] if "/" in rel else "(root files)"
        if top != last_top:
            print("[%d/%d] %s" % (i, len(paths), top))
            last_top = top
        try:
            d.handle_file(p, rel)
        except Exception as e:
            print("  %s: couldn't be read (%s: %s) - skipped - please report this file by opening an issue on the ns2dig GitHub page" % (rel, type(e).__name__, e), flush=True)

    if not stopped:
        print("@@PROGRESS %d %d" % (len(paths), len(paths)), flush=True)
    print("\nFile types found:")
    for kind, n in sorted(d.stats.items(), key=lambda kv: -kv[1]):
        print("  %-36s %d" % (_printable(kind), n))
    unk = [k for k in d.exts if k.startswith("unknown")]
    if unk:
        print("\nFormats without a recognised header, by file extension (with one example each):")
        for kind in sorted(unk, key=lambda k: -d.stats.get(k, 0)):
            print("  %s" % kind)
            items = sorted(d.exts[kind].items(), key=lambda kv: -kv[1][0])
            for ext, (n, ex) in items[:40]:
                print("      %-26s %6d   e.g. %s" % (ext, n, ex))
            if len(items) > 40:
                print("      ... and %d more extensions" % (len(items) - 40))
    tagged = [k for k in d.exts if re.fullmatch(r"[A-Za-z0-9 _]{2,5}", k) and not k.islower()]
    if tagged:
        print("\nFormats known only by their header tag - not decoded yet (with one example each):")
        for kind in sorted(tagged, key=lambda k: -d.stats.get(k, 0)):
            items = sorted(d.exts[kind].items(), key=lambda kv: -kv[1][0])
            print("  %-8s %s" % (kind, ", ".join("%s x%d" % (e, n) for e, (n, _x) in items[:6])))
            print("      e.g. %s" % items[0][1][1])
    if d.problem_examples:
        print("\nFiles a reader couldn't fully read (one example each - please report these by opening an issue on the ns2dig GitHub page):")
        for kind, ex in sorted(d.problem_examples.items()):
            print("  %-46s %6d   e.g. %s" % (_printable(kind), d.stats.get(kind, 0), ex))
    if d.restbls:
        import zlib
        names = {}
        for lab in d.labels:
            parts = lab.split("/")
            # a game path starts right after "romfs" or right after an archive (a part with a dot that has children);
            # other suffixes ("458752/...", "base/...") only make chance matches
            for k in range(1, len(parts)):
                prev = parts[k - 1]
                if prev != "romfs" and "." not in prev: continue
                s = "/".join(parts[k:])
                if s.endswith(".zs"): s = s[:-3]
                names.setdefault(zlib.crc32(s.encode("utf-8")), s)
        for lab, blob in d.restbls:
            try:
                info = read_restbl(blob, names)
                info["_size"] = len(blob)
                d.entry(lab, info); d.written -= 1
                print("Resource size table: named %d of %d files (%s)" % (info["named"], info["entries"], lab))
            except Exception as e:
                print("Resource size table couldn't be named: %s" % e)
    print("\nWrote %d JSON files to %s" % (d.written, out_root))
    if stopped:
        sys.exit(2)   # tells the GUI the scan is partial


if __name__ == "__main__":
    main()
