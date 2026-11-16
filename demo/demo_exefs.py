"""demo_exefs.py - the ns2dig demo's exefs (used by make_demo.py): tiny made-up stand-ins for a game's program
files, in the real formats, so the app lists and describes them. They don't run.

  exefs/main       NSO0 - "the game": a few instructions, a module name, version strings, 3 exported functions
  exefs/rtld       NSO0 - "the dynamic loader"
  exefs/sdk        NSO0 - "the system libraries"
  exefs/main.npdm  META - the program's name, main thread settings, services, file-system and kernel permissions

NSO0: 0x100 header - "NSO0", version, flags (bit 0-2 = segment LZ4-compressed, 3-5 = hash checked), text/ro/data
(file offset, memory offset, size; +0xC module name offset / size, bss size), build id at 0x40, compressed sizes at
0x60, dynamic string / symbol tables (in .ro) at 0x90 / 0x98, SHA-256 of each segment at 0xA0. .ro starts with the
module path (u32 0, u32 length, path).
"""
import hashlib, os, struct

PROGRAM_ID = 0x0100DE0000D3E000          # made up (the 0100 prefix of a game's id, then "DE...D3E" for demo)


def _lz4_literals(data):
    """an LZ4 block with only literals (valid, not smaller): the scanner's LZ4 decoder still has to read it"""
    n = len(data); out = bytearray([min(n, 15) << 4])
    if n >= 15:
        r = n - 15
        while r >= 255: out.append(255); r -= 255
        out.append(r)
    return bytes(out + data)


def nso(module, text, strings, exports, compress=False):
    """module: name; text: machine code; strings: list of str for .ro; exports: [(name, text offset)]"""
    path = ("D:/ns2dig/demo/build/%s.nss" % module).encode()
    ro = bytearray(struct.pack("<II", 0, len(path)) + path + b"\0")
    while len(ro) % 8: ro.append(0)
    for s in strings: ro += s.encode() + b"\0"
    while len(ro) % 8: ro.append(0)
    dynstr = bytearray(b"\0"); offs = []
    for nm, _ in exports: offs.append(len(dynstr)); dynstr += nm.encode() + b"\0"
    while len(dynstr) % 8: dynstr.append(0)
    dstr_off = len(ro); ro += dynstr
    dsym_off = len(ro); ro += bytes(24)                                  # symbol 0 is empty
    for (nm, val), o in zip(exports, offs): ro += struct.pack("<IBBHQQ", o, 0x12, 0, 1, val, 8)   # global function in .text
    data = bytearray(b"DEMO" + struct.pack("<I", 1) + bytes(8))
    segs = [bytes(text), bytes(ro), bytes(data)]
    stored = [_lz4_literals(s) if compress and i < 2 else s for i, s in enumerate(segs)]
    h = bytearray(0x100); h[0:4] = b"NSO0"
    flags = (3 if compress else 0) | 0x38                                 # hashes are filled in and checked
    struct.pack_into("<III", h, 4, 0, 0, flags)
    foff, moff = 0x100, 0
    for i, (s, st) in enumerate(zip(segs, stored)):
        struct.pack_into("<III", h, 0x10 + 0x10 * i, foff, moff, len(s)); foff += len(st)
        moff += (len(s) + 0xFFF) & ~0xFFF
        struct.pack_into("<I", h, 0x60 + 4 * i, len(st))
        h[0xA0 + 32 * i:0xC0 + 32 * i] = hashlib.sha256(s).digest()
    struct.pack_into("<I", h, 0x1C, 0); struct.pack_into("<I", h, 0x2C, 0)
    struct.pack_into("<I", h, 0x3C, 0x1000)                               # bss
    h[0x40:0x54] = hashlib.sha1(("ns2dig demo " + module).encode()).digest()   # build id (20 bytes, rest 0)
    struct.pack_into("<IIIIII", h, 0x88, 0, len(path) + 8, dstr_off, len(dynstr), dsym_off, 24 * (len(exports) + 1))
    return bytes(h) + b"".join(stored)


# AArch64: mov w0, #0 / ret, and nop
_RET0, _NOP = bytes.fromhex("00008052c0035fd6"), bytes.fromhex("1f2003d5")


def main_nso():
    text = _RET0 + _NOP * 2 + _RET0 + _NOP * 2 + _RET0
    return nso("main", text, ["ns2dig demo - Demo Island", "Demo Island version 1.0 (made up for ns2dig)",
                              "SDK MW+ns2dig+DemoEngine-1_0_0-Release", "SDK MW+ns2dig+DemoAudio-0_9_2-Release",
                              "Terrain/DemoIsland.tscb", "Layout/DemoTitle.blarc.zs", "Event/EventFlow/DemoIsland_Islander.bfevfl.zs",
                              "D:/ns2dig/demo/src/main.cpp", "D:/ns2dig/demo/src/island.cpp", "Copyright (C) 2026 ns2dig demo - nothing here is real"],
               [("nnMain", 0x00), ("_ZN4demo4Game6UpdateEv", 0x10), ("_ZN4demo6Island4LoadEPKc", 0x20)], compress=True)


def rtld_nso():
    return nso("rtld", _RET0 + _NOP * 2, ["ns2dig demo dynamic loader (does nothing)", "D:/ns2dig/demo/src/rtld.cpp"],
               [("__demo_rtld_start", 0x00)])


def sdk_nso():
    text = _RET0 + _NOP * 2 + _RET0
    return nso("sdk", text, ["ns2dig demo system library (does nothing)", "SDK MW+ns2dig+DemoSdk-2_6_0-Release",
                             "D:/ns2dig/demo/src/sdk/os.cpp", "D:/ns2dig/demo/src/sdk/fs.cpp"],
               [("_ZN4demo3sdk2os5SleepEl", 0x00), ("_ZN4demo3sdk2fs8MountRomEv", 0x10)])


def _kcap_thread(lo_prio, hi_prio, core_lo, core_hi): return 0x7 | (hi_prio << 4) | (lo_prio << 10) | (core_lo << 16) | (core_hi << 24)
def _kcap_syscalls(index, mask): return 0xF | (mask << 5) | (index << 29)


def npdm():
    """META + ACID (limits; its signature and key left blank) + ACI0 (what the program gets)"""
    services = ["fsp-srv", "hid", "vi:u", "audout:u", "time:u", "acc:u", "pl:u", "set"]
    sac = b"".join(bytes([len(s) - 1]) + s.encode() for s in services)
    kac = [_kcap_thread(24, 59, 0, 2), _kcap_syscalls(0, 0x3FFFFE), _kcap_syscalls(1, 0x00FFFF),
           0x1FFF | (0 << 14),                       # program type 0 (application)
           0x3FFF | (3 << 15) | (13 << 19),          # kernel version 13.3
           0x7FFF | (512 << 16),                     # handle table size 512
           0xFFFF | (1 << 17)]                       # debug flags (bit 16 ends the type mark): allow debug
    kacb = b"".join(struct.pack("<I", v) for v in kac)
    fah = struct.pack("<IQ", 1, (1 << 0) | (1 << 12) | (1 << 21)) + struct.pack("<4I", 0x1C, 0, 0x1C, 0)   # ApplicationInfo, ImageManager? (see list) , SdCard
    fac = struct.pack("<I", 1) + bytes(3) + struct.pack("<Q", (1 << 0) | (1 << 21)) + bytes(4)
    def pad(b, a=16): return b + bytes((-len(b)) % a)
    # ACID: signature (0x100) + key (0x100) + "ACID" header 0x40, then FAC, services, kernel caps
    acid_h = bytearray(0x240); acid_h[0x200:0x204] = b"ACID"
    o_fac = 0x240; o_sac = o_fac + len(pad(fac)); o_kac = o_sac + len(pad(sac))
    struct.pack_into("<IIIQQIIIIII", acid_h, 0x204, o_kac + len(kacb), 0, 1, PROGRAM_ID, PROGRAM_ID + 0xFFF,
                     o_fac, len(fac), o_sac, len(sac), o_kac, len(kacb))
    acid = pad(bytes(acid_h) + pad(fac) + pad(sac) + kacb)
    # ACI0: header 0x40, then FAH, services, kernel caps
    aci_h = bytearray(0x40); aci_h[0:4] = b"ACI0"; struct.pack_into("<Q", aci_h, 0x10, PROGRAM_ID)
    a_fah = 0x40; a_sac = a_fah + len(pad(fah)); a_kac = a_sac + len(pad(sac))
    struct.pack_into("<6I", aci_h, 0x20, a_fah, len(fah), a_sac, len(sac), a_kac, len(kacb))
    aci = pad(bytes(aci_h) + pad(fah) + pad(sac) + kacb)
    m = bytearray(0x80); m[0:4] = b"META"
    m[0x0C], m[0x0E], m[0x0F] = 0x07, 44, 0                                # 64-bit, 64-bit address space; priority 44, core 0
    struct.pack_into("<III", m, 0x14, 0, 0x10000, 0x100000)               # system resources, version 1.0 (0x10000), 1 MB stack
    m[0x20:0x30] = b"DemoIsland".ljust(16, b"\0"); m[0x30:0x40] = b"NS2DIG-DEMO".ljust(16, b"\0")
    struct.pack_into("<IIII", m, 0x70, 0x80 + len(acid), len(aci), 0x80, len(acid))
    return bytes(m) + acid + aci


def build(out_base):
    p = os.path.join(out_base, "exefs"); os.makedirs(p, exist_ok=True)
    for name, data in (("main", main_nso()), ("rtld", rtld_nso()), ("sdk", sdk_nso()), ("main.npdm", npdm())):
        open(os.path.join(p, name), "wb").write(data)
