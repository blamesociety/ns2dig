# Third-party code and data

ns2dig's own code is under the MIT licence (see `LICENSE`). It also bundles the following open-source
code, each under its own licence, which is included next to it:

| Component | Used for | Licence | Licence file |
|---|---|---|---|
| [zstd](https://github.com/facebook/zstd) | Zstandard decompression (`source/zstd.c`, `zstd.h`) | BSD | `source/zstd_LICENSE` |
| [libvpx](https://chromium.googlesource.com/webm/libvpx) | VP9 video decoding (`source/vpx/`) | BSD + patent grant | `source/vpx/LICENSE_libvpx`, `source/vpx/PATENTS_libvpx` |
| [texture2ddecoder](https://github.com/K0lb3/texture2ddecoder) | ASTC and BC texture decoding (`source/t2d/`) | MIT | `source/t2d/LICENSE_texture2ddecoder` |
| [SPIRV-Headers](https://github.com/KhronosGroup/SPIRV-Headers) | SPIR-V instruction tables in `ns2dig.py` | MIT-style | `source/LICENSE_SPIRV-Headers.txt` |

The demo ROM's media (the seagulls video and the "Golden Tides" music) are original works by the
project author and are released with the rest of the project under the MIT licence.
