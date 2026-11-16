"""demo_sound.py - the ns2dig demo's sound bank (used by make_demo.py): Sound/DemoSounds.bars.zs

  SE_Seagull     made-up gull cries (16-bit PCM, mono, 48 kHz)
  SE_CoinGet     a two-note chime (16-bit PCM, mono)
  SE_CrateOpen   a wooden knock (DSP-ADPCM, mono)
  SE_Splash      a splash of noise (DSP-ADPCM, stereo)
  SE_Wind        a loop of wind (16-bit PCM, mono, loops)
  GoldenTides    only the first 2 seconds of the music (a "prefetch" entry): the player finds the full sound in
                 Sound/Stream/GoldenTides.bwav, as Mario Kart World does with its music

BARS: "BARS", u32 size, BOM, u16 version, u32 count, count x u32 name hash (CRC32, sorted), then count x
(u32 AMTA offset, u32 BWAV offset). AMTA (0x30+): "AMTA", BOM, version, size, ... at 0x24 a u32 offset (from that
field) of the sound's name. Each BWAV is a whole sound file (see tools/make_bwav.py for the layout); a prefetch
entry has u16 1 at 0x0C and stores fewer samples than the sound has.
"""
import math, os, struct, subprocess, tempfile, zlib
import numpy as np

RATE = 48000


def _rng(seed): return np.random.default_rng(seed)


def seagull():
    t = np.arange(int(RATE * 1.4)) / RATE; out = np.zeros_like(t)
    for st, dur, f0 in ((0.0, 0.32, 2300), (0.42, 0.28, 2500), (0.8, 0.45, 2200)):
        m = (t >= st) & (t < st + dur); u = (t[m] - st) / dur
        f = f0 * (1 + 0.35 * np.sin(np.pi * u) - 0.25 * u)                 # rising then falling cry
        ph = 2 * np.pi * np.cumsum(f) / RATE
        env = np.sin(np.pi * u) ** 0.6
        out[m] += env * (np.sin(ph) + 0.4 * np.sin(2 * ph) + 0.2 * np.sin(3 * ph + 1)) / 1.6
    return out * 0.7


def coin():
    t = np.arange(int(RATE * 0.6)) / RATE
    a = np.where(t < 0.08, np.sin(2 * np.pi * 988 * t) * np.exp(-t * 12), 0)
    b = np.where(t >= 0.08, np.sin(2 * np.pi * 1319 * (t - 0.08)) * np.exp(-(t - 0.08) * 7), 0)
    return (a + b) * 0.6


def knock():
    t = np.arange(int(RATE * 0.5)) / RATE; out = np.zeros_like(t); r = _rng(3)
    for st in (0.0, 0.16):
        m = t >= st; u = t[m] - st
        out[m] += (np.sin(2 * np.pi * 180 * u) + 0.6 * np.sin(2 * np.pi * 410 * u) + 0.3 * r.standard_normal(m.sum())) * np.exp(-u * 30)
    return out * 0.5


def splash():
    n = int(RATE * 1.2); r = _rng(5); t = np.arange(n) / RATE
    noise = r.standard_normal((2, n))
    k = np.exp(-np.arange(64) / 8.0); k /= k.sum()
    noise = np.stack([np.convolve(c, k, "same") for c in noise]) * 3          # softened
    env = np.minimum(t / 0.02, 1) * np.exp(-t * 3.5)
    return noise * env * 0.6


def wind():
    n = RATE * 2; r = _rng(9); x = r.standard_normal(n + 4096)
    k = np.exp(-np.arange(400) / 90.0); k /= k.sum(); y = np.convolve(x, k, "same")[2048:2048 + n] * 6
    y *= 0.6 + 0.4 * np.sin(2 * np.pi * np.arange(n) / n)                     # one gust per loop (ends where it starts)
    fade = np.minimum(1, np.minimum(np.arange(n), n - 1 - np.arange(n)) / 2000)
    return y * (0.3 + 0.7 * fade) * 0.5


def _pcm16(x): return (np.clip(x, -1, 1) * 32767).astype("<i2")


def _dsp(x, enc):
    """DSP-ADPCM one channel with the C encoder (tools/dspadpcm_enc): (coefficients 32 bytes, frames)"""
    d = tempfile.mkdtemp(); raw, out = os.path.join(d, "a.raw"), os.path.join(d, "a.dsp")
    _pcm16(x).tofile(raw); subprocess.run([enc, raw, out], check=True, capture_output=True)
    b = open(out, "rb").read(); return b[:32], b[32:]


def bwav(chans, codec, rate=RATE, loop=None, prefetch=None, enc=None, coefs=None):
    """chans: list of float arrays (or, for codec 1 with coefs given, frames bytes); loop: (start, end) samples;
    prefetch: (stored samples, total samples) for a start-only entry"""
    nch = len(chans)
    if coefs is None:                                                    # no peak above 70 %
        pk = max(float(np.abs(c).max()) for c in chans); chans = [c * min(1.0, 0.7 / pk) for c in chans] if pk > 0 else chans
    if codec == 0: datas = [_pcm16(c).tobytes() for c in chans]; ns = len(chans[0]); cf = [bytes(32)] * nch
    elif coefs is not None: datas = chans; cf = coefs; ns = prefetch[0]
    else:
        pairs = [_dsp(c, enc) for c in chans]; cf = [p[0] for p in pairs]; datas = [p[1] for p in pairs]; ns = len(chans[0])
    total = prefetch[1] if prefetch else ns; stored = prefetch[0] if prefetch else ns
    head = bytearray(0x10 + 0x4C * nch); head[0:4] = b"BWAV"
    struct.pack_into("<HHIHH", head, 4, 0xFEFF, 1, 0, 1 if prefetch else 0, nch)
    off = (len(head) + 0x3F) & ~0x3F; body = bytearray(off - len(head))
    for i in range(nch):
        o = 0x10 + 0x4C * i
        struct.pack_into("<HHIII", head, o, codec, i if nch == 2 else 2, rate, total, stored)
        head[o + 0x10:o + 0x30] = cf[i]
        ls, le = loop if loop else (0, 0xFFFFFFFF)
        first = datas[i][0] if codec == 1 and datas[i] else 0
        struct.pack_into("<IIIIIHhh", head, o + 0x30, off, off, 1 if loop else 0, le, ls, first, 0, 0)
        body += datas[i]
        while len(body) % 0x40: body.append(0)
        off = len(head) + len(body)
    return bytes(head) + bytes(body)


def _amta(name):
    b = bytearray(0x30); b[0:4] = b"AMTA"; struct.pack_into("<HHI", b, 4, 0xFEFF, 0x500, 0)
    struct.pack_into("<I", b, 0x24, 0x30 - 0x24); b += name.encode() + b"\0"
    while len(b) % 4: b.append(0)
    struct.pack_into("<I", b, 8, len(b))
    return bytes(b)


def bars(entries):
    """entries: [(name, bwav bytes)] -> BARS (entries in name-hash order, as the game keeps them)"""
    entries = sorted(entries, key=lambda e: zlib.crc32(e[0].encode()))
    n = len(entries); head = 0x10 + 4 * n + 8 * n
    body = bytearray(); offs = []
    for name, bw in entries:
        while (head + len(body)) % 4: body.append(0)
        a = head + len(body); body += _amta(name)
        while (head + len(body)) % 0x40: body.append(0)
        w = head + len(body); body += bw; offs.append((a, w))
    out = bytearray(b"BARS" + struct.pack("<IHHI", 0, 0xFEFF, 0x102, n))
    for name, _ in entries: out += struct.pack("<I", zlib.crc32(name.encode()))
    for a, w in offs: out += struct.pack("<II", a, w)
    out += body; struct.pack_into("<I", out, 4, len(out))
    return bytes(out)


def prefetch_of(full, seconds=2.0):
    """the first seconds of a DSP-ADPCM BWAV, as a start-only BWAV"""
    nch = struct.unpack_from("<H", full, 0x0E)[0]; c = 0x10
    codec, _, rate, total = struct.unpack_from("<HHII", full, c)
    keep = int(rate * seconds) // 14 * 14
    frames, coefs = [], []
    for i in range(nch):
        o = 0x10 + 0x4C * i; data = struct.unpack_from("<I", full, o + 0x30)[0]
        frames.append(full[data:data + keep // 14 * 8]); coefs.append(full[o + 0x10:o + 0x30])
    return bwav(frames, 1, rate=rate, loop=(0, total), prefetch=(keep, total), coefs=coefs)


def build(romfs, here):
    """Sound/DemoSounds.bars.zs (needs tools/dspadpcm_enc for the ADPCM ones, and the music's BWAV for the prefetch)"""
    from demo_ui import _z
    enc = os.path.join(here, "tools", "dspadpcm_enc.exe" if os.name == "nt" else "dspadpcm_enc")
    music = os.path.join(romfs, "Sound", "Stream", "GoldenTides.bwav")
    if not os.path.isfile(enc) or not os.path.isfile(music):
        dst = os.path.join(romfs, "Sound", "DemoSounds.bars.zs")
        for src in (os.path.join(here, "media", "DemoSounds.bars.zs"), os.path.join(here, "base", "romfs", "Sound", "DemoSounds.bars.zs")):
            if os.path.isfile(src):
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                if os.path.abspath(src) != os.path.abspath(dst): open(dst, "wb").write(open(src, "rb").read())
                return "copied"
        return "skipped"
    W = wind()
    E = [("SE_Seagull", bwav([seagull()], 0)), ("SE_CoinGet", bwav([coin()], 0)), ("SE_CrateOpen", bwav([knock()], 1, enc=enc)),
         ("SE_Splash", bwav(list(splash()), 1, enc=enc)), ("SE_Wind", bwav([W], 0, loop=(0, len(W)))),
         ("GoldenTides", prefetch_of(open(music, "rb").read()))]
    z = _z(bars(E))
    os.makedirs(os.path.join(romfs, "Sound"), exist_ok=True)
    open(os.path.join(romfs, "Sound", "DemoSounds.bars.zs"), "wb").write(z)
    os.makedirs(os.path.join(here, "media"), exist_ok=True)
    open(os.path.join(here, "media", "DemoSounds.bars.zs"), "wb").write(z)
    return "made"
