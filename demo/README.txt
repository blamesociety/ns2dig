ns2dig demo "romfs" - terrain, water, small islands and a player
=================================================================

A tiny made-up world for trying ns2dig without a game dump. Nothing in it comes from any game: the heights,
textures and every file / folder name were made up by make_demo.py (in the separate demo tools download); the one video is real footage
(seagulls over an island), not game footage, and the music (Golden Tides) was supplied for the demo. The title screen, its texts and the font
are made up by demo_ui.py, the lighting by demo_env.py, the event scripts by demo_evt.py, the exefs files by demo_exefs.py, collision and cloth by
demo_havok.py, the sound bank by demo_sound.py, the texture formats by demo_tex.py, the voxels by demo_voxel.py,
the dictionary files and size table by demo_data.py. It is NOT a playable game or a
real dump - just files in the formats ns2dig reads. Needs ns2dig 2026-11-16.171 or later. It comes in the "demo" folder beside ns2dig.exe.

The world: 3 x 3 km, a sea with a big island and a mountain in the middle (peak about 410 m), 5 small islands
round it (about half grass, half sand), water surface at 100 m. The player stands on the small island at
x 839, z 760 (height 147), facing the big island. A wooden crate (1 m cube) stands on the small island at
x -1135, z -445 (height 141).

What's inside (base\exefs\...): tiny made-up stand-ins for a game's program files, in the real formats (they
don't run; the scanner describes each one):
  main                                   "the game" (NSO): a few instructions, its module name, version and middleware
                                         strings, source file names, 3 exported functions (stored LZ4-compressed)
  rtld, sdk                              "the loader" and "the system library" (NSO), 1 and 2 exported functions
  main.npdm                              program settings: name DemoIsland, program id 0100DE0000D3E000, main thread,
                                         8 services, file-system permissions, kernel capabilities

What's inside (base\romfs\...):
  Terrain\DemoIsland.tscb               the terrain scene: lists 21 square tiles (1 of 3 km, 4 of 1.5 km, 16 of 750 m)
  Terrain\DemoIsland\L<level>_X<column>_Z<row>.hght        heights (257 x 257 per tile)
  Terrain\DemoIsland\L<level>_X<column>_Z<row>.mate        ground material per point: 0 = grass, 1 = sand
  Terrain\DemoIsland\L<level>_X<column>_Z<row>.water.extm  the water surface
  Tex\DemoIsland_Ground.bntx.zs          the ground textures (layer 0 grass, layer 1 sand)
  Tex\DemoIsland_Water.bntx.zs           the water ripple texture
  Model\DemoPlayer.DemoPlayer.bfres.zs    the player: a 1.8 m stick figure (parts Head, Body, Arms, Legs) on a
                                         12-bone skeleton (Root, Hips, Spine, Head, upper / lower arms and legs)
  Model\DemoPlayer.anim.bfres.zs          its animations, looping: Idle (90 frames: breathing, looking round),
                                         Walk (60: a walk on the spot), Wave (60: right arm up, waving)
  Model\DemoCrate.DemoCrate.bfres.zs      the crate: a 1 m cube (one part, Crate) with one texture
  Tex\DemoCrate_Wood.bntx.zs             the crate's planks texture
  Movie\DemoSeagulls.webm                 a video: seagulls over an island (VP9, 1280 x 720, 24 fps, 10 s, no sound)
  Sound\Stream\GoldenTides.bwav          music: Golden Tides (BWAV, DSP-ADPCM, stereo, 44.1 kHz, 10 s, loops)
  Layout\DemoTitle.blarc.zs              a title screen: layouts DemoTitle (sky, sun, waves, title, a menu window
                                         with 3 buttons, a hint, a version line, a hidden pane) and DemoButton (the
                                         button, used 3 times as a part); animations DemoTitle_Intro, _Idle (loops),
                                         _Secret and DemoButton_Select (loops); 5 textures
  Mals\<lang>.Product.100.sarc.zs        the title screen's text in English (USen), French (EUfr), German (EUde)
                                         and Spanish (EUes): LayoutMsg\DemoTitle.msbt, LayoutMsg\DemoButton.msbt
  Font\Font.100.bfarc.zs                 two made-up bitmap fonts: DemoBeads (letters drawn as beads; A-Z, a-z, 0-9,
                                         punctuation and the accented letters the 4 languages need; the title screen
                                         uses it) and DemoBlocks (the same letters as outlined tiles, in colour; unused)
  Env\DemoIsland.genvb.zs                the island's lighting: a full "default" (midday) setup - sun, sky, fog,
                                         lights, bloom, colour correction, depth of field, shadows, light shafts -
                                         and Sunset, Night and Storm setups holding only the files they change;
                                         default\default.bofx = a lens flare (settings + 3 textures); one setting
                                         in default.bprojgfxbase has a made-up name the viewer can't know
  Event\EventFlow\DemoIsland_Islander.bfevfl.zs   a conversation with an islander: entry points Talk, Farewell,
                                         Wait; a question with 3 answers, a coin check, a fork, a loop, a sub-flow
  Event\EventFlow\DemoIsland_Arrival.bfevfl.zs    a cutscene: a flowchart that plays a 10-second timeline (camera
                                         pans, captions, the player waving, music) and runs a sub-flow from the other file
                                         (their text: Mals\<lang>...\EventFlowMsg\*.msbt; text colours: DemoProject.msbp)
  Model\DemoHat.DemoHat.bfres.zs (+ .anim)   a propeller hat; its propeller spins in clips named like the player's
  Pack\Actor\DemoPlayer.pack.zs, DemoPlayerHat.pack.zs   actor data: the hat (DemoPlayerHat -> model DemoHat) is
                                         joined to the player's Head bone - the 3D view puts it on by itself
  Model\DemoFlag.DemoFlag.bfres.zs        a flag on a pole (one bone per cloth particle), texture Tex\DemoFlag_Cloth
  Phive\Cloth\DemoFlag.bphcl.zs           its cloth: 20 particles (pole side pinned), standard / stretch / bend links,
                                         a capsule round the pole - tick the cloth simulation and it droops
  Phive\Shape\DemoIsland.bphsh.zs         collision for the whole map (25 sections, 3 materials: grass, sand, sea bed)
  Phive\Shape\DemoCrate.bphsh.zs          collision for the crate (full-precision vertices)
  Tex\DemoFormats.bntx.zs                 one test picture in 18 GPU formats (BC1/3/4/5/7, ASTC 4x4-12x12, sRGB
                                         ones, R8, RG8, RGBA16F), tiled the Switch 2 way, each with all its mipmaps,
                                         plus a 6-face cube map
  Tex\DemoFormats_NX.bntx.zs              the same picture tiled the Switch 1 way (RGBA8, BC1, BC7, ASTC 4x4)
  Sound\DemoSounds.bars.zs                a sound bank: 5 made-up sound effects (PCM and DSP-ADPCM, mono and stereo,
                                         one looping) and GoldenTides as a start-only entry (the player takes the
                                         full sound from Sound\Stream\GoldenTides.bwav)
  Voxel\DemoRocks\Arch\...               voxel terrain: a rock arch on a grassy slab, with gold ore
  Pack\ZsDic.pack.zs                      a zstd dictionary (zs.zsdic); GameData\*.byml.zs are compressed with it
                                         (made-up items, quests, characters, shop, save flags)
  System\Resource\ResourceSizeTable.Product.100.rsizetable.zs   how much memory each file needs (every demo file)
  Banc\World_DemoIsland\DemoIsland_Main.bcett.byml.zs   the placed objects: the player, the crate and the flag (Name,
                                         position, rotation in radians, scale)
                                         - and a flag on the small island at x 120, z -1208

How to use:
  1. In ns2dig, scan this "demo" folder like a dump. (If you scanned an earlier demo, delete that scan first -
     its old files would still be there and make the app think it is another game.) The app recognises it as "ns2dig demo"
     (Bookmarks has its own entries).
  2. Bookmarks > Island (whole terrain), or open Terrain\DemoIsland.tscb and click "3D view".
  3. Right-click the ground > "Open this area in full detail" for a closer look; T = top view.
  The tiles and textures can also be opened on their own (terrain layers, Textures viewer).
  4. Bookmarks > Player model, or open Model\DemoPlayer.DemoPlayer.bfres.zs and click "3D view". Pick Idle, Walk
     or Wave in the animation list and press Play.
  5. The player in the world: in the island's 3D view, right-click the small island at x 839, z 760 > "Open this
     area in full detail". At 1.8 m tall it is a speck on the whole 3 km map. It is listed as "Other: DemoPlayer".
  6. The crate: Bookmarks > Crate model; in the world, open the small island at x -1135, z -445 in full detail
     ("Other: DemoCrate").
  7. The video: Bookmarks > Seagulls (video), or open Movie\DemoSeagulls.webm and click Play.
  8. The music: Bookmarks > Music: Golden Tides, or open Sound\Stream\GoldenTides.bwav and click Play.
  9. The title screen: Bookmarks > Title screen (layout). Pick the language in the text list (English, French,
     German, Spanish, or the layout's own text), play the animations, tick "show hidden" for the secret pane, and
     pick DemoButton in the layout list for the button on its own. The fonts: Bookmarks > Font: DemoBeads / Font: DemoBlocks.
  10. Lighting: Bookmarks > Lighting. Pick a Sunset / Night / Storm file for that setup's preview (files a setup
     doesn't have come from default), compare it with default, tick "show unnamed" for the made-up setting, and
     open the lens flare textures. (The island's 3D view doesn't use these settings.)
  11. Events: Bookmarks > Event: talking to the islander / Event: arrival cutscene. Switch the language; the
     highlighted words show in colour; the left list has the entry points (Talk, Farewell, Wait) and the timeline.
  12. The program files: open base\exefs in the tree and select main, rtld, sdk or main.npdm.
  13. The player again: the hat is on by itself (actor data). In the animation list: Blink (eyes, a key shape),
     Talk (the mouths swap: a visibility clip), Wave (also talks); the hat's propeller spins along. The Colours
     list: ShirtRed, ShirtGreen (still colours), ShirtGlow (animated).
  14. Bookmarks > Collision: ... / Flag (cloth) / Cloth on its own / Voxel rock arch / Texture formats / Sound
     effects / Game data / Resource size table.

To rebuild it (with the demo tools download - make_demo.py, demo_*.py, tools\ - unzipped into this folder):
  python make_demo.py [output folder]   (needs Python 3 and: pip install zstandard)
  Optional: pip install numpy pillow etcpak astc-encoder-py (without the last two the texture-format files are
  copied, not made); tools\dspadpcm_enc (built from tools\dspadpcm_enc.c) for the ADPCM sounds (else copied).
  The video isn't generated: make_demo.py copies DemoSeagulls.webm from a "media" folder beside it, or from this
  demo folder's own base\romfs\Movie. The music is copied the same way (GoldenTides.bwav); to make a BWAV from
  another audio file: tools\make_bwav.py (needs ffmpeg and dspadpcm_enc built from tools\dspadpcm_enc.c).


