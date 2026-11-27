# ns2dig

A Windows app for exploring the game data of Nintendo Switch 2 games. Point it at a decrypted RomFS
dump and it turns every game file into readable JSON, then lets you browse, search and view it:

- **3D viewer** – models with skeletons, skinning, animations (bone, shape, visibility and colour),
  attachments, cloth, collision, terrain and voxel worlds
  - **Walk the world** – explore a whole terrain world in first person: mouse to look, W A S D to move.
    Full detail streams in around you while the rest stays at low detail. Collisions let you walk with
    gravity, jump, climb stairs and ramps and slide along walls; press Space twice to fly, or C to fly
    through everything. Sliders set the walking speed (also the mouse wheel), player height, jump height
    and how far full detail reaches (500 × 500 up to 16000 × 16000, the whole Mario Kart World map).
    The info box shows how many textures are loaded and roughly how much graphics memory they use –
    the whole map needs about 6,250 textures and 1.1 GB.
  - **Water** – seas, lakes and rivers drawn with a water shader built from the game's own water colours:
    teal, see-through shallows to dark deep water, shore foam, sun glints and moving ripples, with
    sliders for the ripple size and speed. Waterfalls are tinted from their materials and flow.
- **Texture viewer** – BNTX textures in BC, ASTC and plain formats, with mipmaps, arrays and cube maps
- **Layout viewer** – UI screens and their animations, with translated text and fonts
- **Font viewer**, **Lighting viewer**, **Event viewer** (flowcharts and cutscene timelines)
- **Video player** (VP9 WebM) and **sound player** (BWAV / BARS)
- **Search** across a whole scan, bookmarks and a file-type filter

It comes with a **demo ROM**: a small made-up island world in the same file formats, so you can try
every viewer without a game dump. Nothing in it comes from any game.

## Download

Get the latest `ns2dig.zip` from the [Releases](../../releases) page, unzip it anywhere, and read
`README.txt` inside for setup (you need Python 3 for the scanner). If your ROM dump is in NUP format
then use https://github.com/a55uka/nup to extract it first. To try it straight away, scan the
`demo` folder.

## Repository layout

| Folder | What's in it |
|---|---|
| `source/` | The app (C) and the scanner (`ns2dig.py`), plus the bundled open-source libraries |
| `README.txt` | The full manual (also shipped in the release zip) |

## Building

On Windows, with Python 3 installed, run `source\build.bat`. It installs the Zig compiler with pip
the first time, then builds `ns2dig.exe` in the top folder and copies `ns2dig.py` next to it.

## Trying the demo ROM

The release zip includes a `demo` folder: a small made-up island world stored in the same file formats
as a real game, so you can try every viewer without a game dump.

**Set up (once)**

1. Install Python 3 from [python.org](https://www.python.org) and tick **Add python.exe to PATH**. With
   Python 3.13 or older, also run `pip install zstandard` in a command prompt. Python 3.14 and newer need
   nothing else.
2. Unzip `ns2dig.zip` anywhere. Keep `ns2dig.exe` and `ns2dig.py` in the same folder.

**Extract the demo**

3. Start `ns2dig.exe`.
4. Next to **Game folder**, click **Browse...** and choose the `demo` folder beside `ns2dig.exe`.
5. Type a **Scan name**, for example `Demo`, and click **Extract**. It takes a few seconds, and the app
   recognises the scan as **ns2dig demo**.

**View it**

6. Click **Bookmarks** to go straight to the highlights. Each one opens its file in the tree; then click
   the button above the viewer, or double-click the file, to open its viewer.
   - **Island** – the whole 3 km terrain in 3D. Right-click the ground and choose **Open this area in
     full detail** for a closer look. Press **T** for a top view.
   - **Player model** – pick **Idle**, **Walk**, **Wave**, **Blink** or **Talk** in the animation list
     and press **Play**. Try the shirt colours in the **Colours** list.
   - **Crate model**, **Flag (cloth)**, **Voxel rock arch**, **Collision**
   - **Seagulls (video)** and **Music: Golden Tides**
   - **Title screen (layout)** – switch between English, French, German and Spanish, and play the
     animations.
   - **Font: DemoBeads** and **Font: DemoBlocks**
   - **Lighting** – compare the default, Sunset, Night and Storm setups.
   - **Events** – a conversation with an islander and an arrival cutscene with a timeline.
7. Browse the rest in the tree on the left, or use **Search** to find text across the whole scan.
   `base\exefs` holds the program files (`main`, `main.npdm`, `rtld` and `sdk`).

**Export it**

8. Everything you extract is already saved as readable JSON files, one per game file, in
   `output\<scan name>\` next to `ns2dig.exe`. Click **Show in Explorer** to open that folder.
9. The viewers can also save what they show in everyday formats:

| What | Button | Saves as | Opens in |
|---|---|---|---|
| 3D models, terrain, collision, voxels | **Export 3D file** | **GLB** (one file with its textures), **OBJ** (with a .mtl and PNG textures), **PLY** (with vertex colours) or **STL** (for 3D printing) | Blender, Windows 3D Viewer, Unity, Unreal, Godot, MeshLab, slicers |
| The 3D view as you see it | **Export PNG** | PNG image | any image viewer |
| Textures | **Save PNG...** or **Save all listed as PNG...** | PNG images | any image editor |
| Layouts (UI screens) | **Save view as PNG...** | PNG image | any image viewer |
| Videos | **Export video...** | the original WebM (VP9), not re-encoded | VLC, web browsers |
| Videos (one frame) | **Save frame as PNG...** | PNG image | any image viewer |
| Music and sound effects | **Save as WAV...** | WAV audio | any audio player or editor |
| Files and folders | right-click → **Create ZIP of this folder / file** | ZIP of the extracted JSON | |

3D exports are always in the model's resting pose, not the current animation frame.

`demo\README.txt` in the release zip lists everything in the demo and where to find it.

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
