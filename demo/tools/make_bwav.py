"""make_bwav.py - an audio file -> BWAV (DSP-ADPCM, looping) for the ns2dig demo.
usage: python make_bwav.py in.mp3 out.bwav [rate]     needs ffmpeg, and dspadpcm_enc (built from dspadpcm_enc.c, beside this)
Layout (as ns2dig's player reads it): "BWAV", BOM FEFF, u16 version 1, u32 0, u16 prefetch 0, u16 channels; then per
channel 0x4C bytes: u16 codec 1 (DSP-ADPCM), u16 pan, u32 rate, u32 samples, u32 samples, 16 x s16 coefficients,
u32 data offset, u32 data offset, u32 loop flag, u32 loop end, u32 loop start, u16 first frame header, s16 hist x 2;
then each channel's frames (8 bytes = 14 samples), 0x40-aligned. The whole sound loops."""
import os, struct, subprocess, sys, tempfile

def main(src, dst, rate=44100):
    here = os.path.dirname(os.path.abspath(__file__))
    enc = os.path.join(here, "dspadpcm_enc.exe" if os.name == "nt" else "dspadpcm_enc")
    tmp = tempfile.mkdtemp()
    chans = []
    for c in (0, 1):
        raw, dsp = os.path.join(tmp, "c%d.raw" % c), os.path.join(tmp, "c%d.dsp" % c)
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", src, "-af", "pan=mono|c0=c%d" % c,
                        "-ar", str(rate), "-f", "s16le", raw], check=True)
        subprocess.run([enc, raw, dsp], check=True)
        d = open(dsp, "rb").read()
        chans.append((d[:32], d[32:], os.path.getsize(raw) // 2))
    ns = chans[0][2]
    head = bytearray(0x10 + 0x4C * len(chans))
    head[0:4] = b"BWAV"; struct.pack_into("<HHIHH", head, 4, 0xFEFF, 1, 0, 0, len(chans))
    off = (len(head) + 0x3F) & ~0x3F; body = bytearray(off - len(head))
    for i, (coef, frames, _) in enumerate(chans):
        o = 0x10 + 0x4C * i
        struct.pack_into("<HHIII", head, o, 1, i, rate, ns, ns)
        head[o + 0x10:o + 0x30] = coef
        struct.pack_into("<IIIIIHhh", head, o + 0x30, off, off, 1, ns, 0, frames[0], 0, 0)
        body += frames
        while len(body) % 0x40: body += b"\0"
        off = len(head) + len(body)
    open(dst, "wb").write(bytes(head) + bytes(body))
    print("%s: %d channels, %d Hz, %.1f s, %d bytes" % (dst, len(chans), rate, ns / rate, len(head) + len(body)))

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 44100)
