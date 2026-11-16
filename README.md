# ns2dig

A Windows app for exploring the game data of Nintendo Switch 2 games. Point it at a decrypted RomFS
dump and it turns every game file into readable JSON, then lets you browse, search and view it:

- **3D viewer** – models with skeletons, skinning, animations (bone, shape, visibility and colour),
  attachments, cloth, collision, terrain and voxel worlds
- **Texture viewer** – BNTX textures in BC, ASTC and plain formats, with mipmaps, arrays and cube maps
- **Layout viewer** – UI screens and their animations, with translated text and fonts
- **Font viewer**, **Lighting viewer**, **Event viewer** (flowcharts and cutscene timelines)
- **Video player** (VP9 WebM) and **sound player** (BWAV / BARS)
- **Search** across a whole scan, bookmarks and a file-type filter

It comes with a **demo ROM**: a small made-up island world in the same file formats, so you can try
every viewer without a game dump. Nothing in it comes from any game.

## Download

Get the latest `ns2dig.zip` from the [Releases](../../releases) page, unzip it anywhere, and read
`README.txt` inside for setup (you need Python 3 for the scanner). To try it straight away, scan the
`demo` folder.

## Repository layout

| Folder | What's in it |
|---|---|
| `source/` | The app (C) and the scanner (`ns2dig.py`), plus the bundled open-source libraries |
| `demo/` | The demo ROM generator (`make_demo.py`, `demo_*.py`), its tools and its media |
| `README.txt` | The full manual (also shipped in the release zip) |

## Building

On Windows, with Python 3 installed, run `source\build.bat`. It installs the Zig compiler with pip
the first time, then builds `ns2dig.exe` in the top folder and copies `ns2dig.py` next to it.

To rebuild the demo ROM, see `demo/README.txt`.

## Legal

ns2dig contains no Nintendo code and no game content. Every file reader was written from scratch by
working out how the file formats are laid out. You need your own decrypted dump of a game you own;
ns2dig does not decrypt games and includes no keys. See the "Legal notes" section of `README.txt`.

ns2dig is not made by, endorsed by or connected with Nintendo. "Nintendo Switch 2" is a trademark of
Nintendo.

## About

ns2dig and its demo ROM were written entirely with [Claude](https://claude.ai) (Anthropic) as an
AI-assisted ("vibe coded") project: every file reader, viewer, the scanner and the made-up demo game
were produced by Claude from my instructions, testing and feedback.

## Licence

MIT – see [LICENSE](LICENSE). The bundled third-party code keeps its own licence; see
[THIRD_PARTY.md](THIRD_PARTY.md).
