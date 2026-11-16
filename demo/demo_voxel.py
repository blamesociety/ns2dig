"""demo_voxel.py - the ns2dig demo's voxel terrain (used by make_demo.py): a rock arch on a grassy slab, with gold
ore in it, as a Donkey Kong Bananza style voxel part:

  Voxel/DemoRocks/MaterialTable.bin    u16 version, u16 count, count x 40-byte material names
  Voxel/DemoRocks/Arch/Info.bin        "VX", u16 version, two u32, size in voxels (3 x u32) at 12, voxel size (3 x f32) at 24
  Voxel/DemoRocks/Arch/vhead.bin       1 bit per 8x8x8 block (first bit = bit 7; x fastest, then y, then z): has data
  Voxel/DemoRocks/Arch/vbody.bin       per block with data: u32 size + a zstd frame of 512 voxels x 8 bytes
                                       (byte 0 fill: 0 empty, 255 solid, between = the surface; bytes 1-2 materials)
  Voxel/DemoRocks/Arch/ehead.bin, ebody.bin   the edge layer, in the same layout (9 bytes per voxel; left empty here)
"""
import math, os, struct
import numpy as np
import zstandard

NX, NY, NZ = 64, 48, 64
SCALE = 0.5                                # metres per voxel
MATS = ["(none)", "DemoRock", "DemoGrass", "DemoGoldOre"]


def _sdf():
    """signed distance (in voxels, negative inside) of the slab + arch, and a material per voxel"""
    z, y, x = np.mgrid[0:NZ, 0:NY, 0:NX].astype(np.float32) + 0.5
    slab = y - (6 + 1.5 * np.sin(x / 7) * np.cos(z / 9))                       # ground: inside below ~y 6
    # the arch: a thick ring standing upright (axis along z), cut at the ground
    cx, cy = NX / 2, 8.0
    ring = np.sqrt((np.sqrt((x - cx) ** 2 + (y - cy) ** 2) - 22) ** 2 + ((z - NZ / 2) / 1.4) ** 2) - 7
    ring += 1.2 * np.sin(x / 3.1) * np.sin(y / 2.7) * np.sin(z / 3.3)              # rough rock
    d = np.minimum(slab, ring)
    mat = np.where(ring < slab, 1, 1).astype(np.uint8)
    top = (slab <= ring) & (slab > -2.5)                                        # the slab's top layer: grass
    mat[top] = 2
    ore = (ring < 0) & (np.sin(x * 0.9) * np.sin(y * 1.1) * np.sin(z * 0.8) > 0.8)
    mat[ore] = 3
    return d, mat


def build(romfs):
    d, mat = _sdf()
    fill = np.clip(127.5 - d * 64, 0, 255).astype(np.uint8)                      # 0 outside, 255 inside, a 4-voxel ramp
    part = os.path.join(romfs, "Voxel", "DemoRocks", "Arch"); os.makedirs(part, exist_ok=True)
    mt = struct.pack("<HH", 1, len(MATS)) + b"".join(m.encode().ljust(40, b"\0") for m in MATS)
    open(os.path.join(romfs, "Voxel", "DemoRocks", "MaterialTable.bin"), "wb").write(mt)
    info = bytearray(48); info[0:2] = b"VX"; struct.pack_into("<HII", info, 2, 0, 0, 0)
    struct.pack_into("<3I3f", info, 12, NX, NY, NZ, SCALE, SCALE, SCALE)
    open(os.path.join(part, "Info.bin"), "wb").write(info)
    bx, by, bz = NX // 8, NY // 8, NZ // 8
    bits = []; body = bytearray(); cz = zstandard.ZstdCompressor(level=19); nblocks = 0
    for kz in range(bz):
        for ky in range(by):
            for kx in range(bx):
                f = fill[kz * 8:kz * 8 + 8, ky * 8:ky * 8 + 8, kx * 8:kx * 8 + 8]
                if not f.any(): bits.append(0); continue
                m = mat[kz * 8:kz * 8 + 8, ky * 8:ky * 8 + 8, kx * 8:kx * 8 + 8]
                rec = np.zeros((8, 8, 8, 8), np.uint8); rec[..., 0] = f; rec[..., 1] = np.where(f > 0, m, 0)
                c = cz.compress(rec.tobytes()); body += struct.pack("<I", len(c)) + c; bits.append(1); nblocks += 1
    head = bytearray((len(bits) + 7) // 8)
    for i, b in enumerate(bits):
        if b: head[i >> 3] |= 0x80 >> (i & 7)
    open(os.path.join(part, "vhead.bin"), "wb").write(head); open(os.path.join(part, "vbody.bin"), "wb").write(body)
    open(os.path.join(part, "ehead.bin"), "wb").write(bytes(len(head))); open(os.path.join(part, "ebody.bin"), "wb").write(b"")
    return nblocks, len(bits)
