ns2dig - Nintendo Switch 2 game RomFS extractor and browser
(Donkey Kong Bananza, Mario Kart World; formerly "ns2digger" and before that "Alsarc Dump")

DEMO
    The "demo" folder beside ns2dig.exe is a tiny made-up world (an island with a mountain in a sea) in the file
    formats ns2dig reads, for trying the app without a game dump. Scan the demo folder like a dump; see
    demo\README.txt. Nothing in it comes from any game.

SETUP (once)
  1. Install Python 3 from python.org (tick "Add python.exe to PATH").
  2. Python 3.14 or newer: nothing else to install (zstd support is built in).
     Python 3.13 or older: in a command prompt run  pip install zstandard
  3. Keep ns2dig.exe and ns2dig.py in the same folder.
  4. Optional, for newer games (e.g. Nintendo Switch Sports Resort): their exefs\main is compressed with
     ZBIC. To decode it, run  pip install zbic  (a separate open-source package, not part of ns2dig).
     Without it, main.json only has the file's header and a note.

UPGRADING FROM "NS2DIGGER" (or "ALSARC DUMP")
  Unzip into your existing ns2digger folder, then delete the old ns2digger.exe and ns2digger.py
  (the folder itself can be renamed to ns2dig if you like). On first start ns2dig copies
  ns2digger.ini (or the older AlsarcDumpGUI.ini) to ns2dig.ini - your scans, paths and recent
  searches - and keeps using the output and search_cache folders next to it.
  Scan from a command prompt with
  py -3 -u "D:\Downloads\Emu\ns2dig\ns2dig.py" "<game dump>" "<output folder>"

BOOKMARKS
  The "Bookmarks" button (next to "File types") opens a menu of highlights per kind - 3D, Video, Audio,
  Textures, Layouts, Fonts, Lighting, Events, Other - for the game in the open scan (a few built-in ones for
  Mario Kart World and Donkey Kong Bananza, plus your own). Click one to go to it in the tree.
  The star button left of "Open JSON" bookmarks whatever is shown (filled star = already bookmarked; click
  again to remove it - built-in ones too). Bookmarks are kept in ns2dig.ini per game, not per scan, so they
  survive re-extracting and are shared by every scan of the same game. The game is recognised from the
  scan's files (other games: by the program id in exefs\main.npdm), whatever the scan is called.

EXTRACT
  Choose a RomFS folder, or the top folder of a dump (base, updates and dlc are all
  scanned into one tree),
  type a scan name (e.g. "Update" or "DLC"), click Extract. Each scan is saved in
  output\<scan name>\ next to the EXE.
  While extracting, the Extract button becomes Stop: it finishes the current file and stops,
  keeping everything extracted so far (forced after 10 seconds if a file takes too long).
  Each scan's log and summary is saved as _extraction_log.txt in its folder; the "Scan log"
  button (next to Open scan) shows it again for the scan that's open.
  Every game file becomes one .json file; archives (.alsarc.zs, .pack.zs, .sarc.zs...)
  become folders. Each format the tool understands is decoded into readable JSON (see
  the list of file types below); anything not recognised gets a short note with its type.
  Switch executables (exefs/main etc.) are decompressed and every readable text string
  is listed with its memory address in <name>.json - search it like any other file.
  Tip: extract base game, update and DLC as separate scans.

BROWSE
  The tree on the left shows the current scan; click a file to view its JSON.
  To switch to an earlier scan, pick it from the Scan name list.
  Each extraction (from build .170 on) remembers its game folder: picking a scan fills in the folder it was
  extracted from, and browsing to a game folder opens its scan (the most recent one if it was extracted more than once).
  "Show in Explorer" opens the scan's folder.
  Click a folder (or the scan name at the top) to see its contents as a plain-text
  tree you can copy (click the text, Ctrl+A, Ctrl+C). Long file lists are shortened
  to 12 files per folder plus a summary; tick "List every file" to show them all.
  Right-click a file or folder in the tree, or a search result, to copy its full path,
  copy its path within the scan (e.g. Pack/Actor/X.pack.zs/...), or show it in Explorer.
  "Open folder" (above the viewer) opens the folder containing the file being shown.
  Double-click a file (in the tree or the search results) to open its viewer straight away -
  the same one its button above the viewer opens: 3D view, Play, Textures, Layout or Font.
  Go to a path: paste a path in the box above the tree and press Enter (or click Go).
  It accepts a full path, a path within the scan (forward or back slashes, with or without
  .json), a line copied from the search results or a folder listing, a path inside a romfs
  folder (e.g. Pack/Actor/BananamondEmerush.pack.zs) or a game path such as
  Work/LevelInfo/Lake.ss__LevelInfo.gyml. If the whole path doesn't exist it goes to the
  nearest folder that does, or finds the file by name.

RE-EXTRACTING A SCAN
  Extracting into a scan name that already exists asks what to do with the old one:
  "Replace it" moves the old folder aside instantly and deletes it in the background while
  the new extraction runs (much faster than deleting it in Explorer, which uses the Recycle
  Bin); "Overwrite in place" just writes over it, leaving behind any files the new
  extraction doesn't produce. If the app is closed before the old folder is gone, it carries
  on deleting it the next time it starts (those folders are called output\_deleting_...).

SEARCH
  Type a word, value or file name and press Enter. Matches across all JSON files
  (and file names) appear under Results; click one to open the file at that line.
  Tick "Only in selected folder" to search just the folder selected in the tree (and its
  subfolders); if a file is selected, its folder is searched. Otherwise the whole scan is searched.
  Wildcards: * = any characters, ? = one character, e.g. Hill*Parts3? or ToolType*Sketchup.
  A pattern matches anywhere in a line or file name (never across lines).
  F3 = next match in the open file, Ctrl+F = search box. Search shows up to 20,000 matches.
  A progress bar next to the Results label shows how far the search has got (Stop cancels it).
  "Copy all" (or right-click a result > Copy all results) copies every result as text.
  The search box is also a drop-down: click its arrow to pick one of your last 25 searches.
  Finished searches are saved for each scan (in search_cache\<scan name>\ next to the EXE),
  so running the same search again - same text, Match case and folder - shows the results
  straight away. A scan's saved searches are deleted when you extract that scan again;
  right-click a result > "Search again" to refresh them by hand. Stopped searches aren't saved.

RIGHT-CLICK MENU (TREE AND SEARCH RESULTS)
  JSON section: copy the output file's full path or its path within the scan, show it in
  Explorer, or ZIP it. Game Dump section: the same for the original file in your game dump
  (copy its Windows path, show it in Explorer, ZIP it). For a file that was inside an archive
  (e.g. a .bgyml inside a .alsarc.zs) these act on that archive - the heading says which.
  With several search results selected, both ZIP items take all of them (the Game Dump one
  zips their originals, each archive once). Greyed out if it can't be found; "Locate the game dump folder..." lets you point to the dump.

FILE TYPE FILTER
  Click "File types" above the tree to show only some kinds of file - e.g. tick bars and bwav, or
  click "+ Sounds" / "+ 3D" / "+ Text". The tree, the folder view and the search results then only
  show those files, and folders that don't contain any of them are hidden. Double-click a type to
  show just that one. Click a column heading to sort by it (again to reverse).
  "Show all types" turns the filter off. A file's type is its extension
  (".zs" ignored); the files of a voxel terrain part count as "voxel terrain".
  The first time you use it on a scan it lists every folder to find the types (a few seconds to a
  minute for a full dump); that is saved with the scan's search cache and redone after re-extracting.

ZIP FILES
  Shift+click and Ctrl+click results to select several (Ctrl+A in the results selects all),
  then right-click > "Create ZIP of the selected results' files". Each file goes in once,
  keeping its folders within the scan. You'll be asked where to save the ZIP.
  Right-click a folder in the tree > "Create ZIP of this folder" to zip it with everything
  under it, or right-click a file > "Create ZIP of this file". With a file-type filter on, a
  folder ZIP only takes the files of the chosen types (the menu says which).
  While a ZIP is being made, a "Stop ZIP" button appears next to Extract (also in the
  right-click menu); stopping deletes the unfinished ZIP. Closing the program stops it too.
  Bulk ZIP (button right of Bookmarks): paste file paths, one per line - full paths, paths within the scan
  (base\romfs\...), with or without .json, folders too - then:
    "ZIP JSON"  their JSON files from the scan's output folder
    "ZIP ROM"   their original files from the game dump (a file that was inside an archive: the archive)
    "ZIP both"  both, in folders JSON and ROM
  Lines that aren't paths (headings such as "Send JSON") are ignored. Paths that can't be found are listed when
  the ZIP is done; everything else is still zipped.

OTHER FORMATS
  .bnvib   HD Rumble vibration patterns: sample rate, length in seconds, loop points
  .bfttf / .bfotf  fonts (Nintendo-obfuscated TTF/OTF): font name, style and the XOR key
  .glsl and other text files: the text itself, one line per entry (searchable)
  .bnsh    compiled GPU shaders (each layout archive's bgsh\__ArchiveShader.bnsh, NVN): the number of
           compiled variations, and for each distinct interface which variations use it and what the
           vertex and pixel stages take and give - inputs (aPosition, aTexCoord...), outputs, textures
           (uTexture0...) and constant buffers (uDropShadowBlur...) with their slots - plus each
           variation's code size. The GPU code itself can't be turned back into source. (Some archives
           also hold a custom shader's original GLSL source, e.g. 2PCursor's 2PCursorGauge.glsl.)
  .sharcb  the engine's shader archives (Lib\agl, Lib\gsys, Shader\Project...): each program's
           stages and options (e.g. RENDER_PASS 0-3), how many variations that makes, per stage its
           uniform blocks, storage buffers, inputs/outputs, uniforms and textures, and for every
           option combination which compiled shader it uses (identical ones are numbered once).
  .bfsha   material shader archives (e.g. Shader\Project...\SilverStarMaterial...bfsha): each shading
           model's static and dynamic options with their choices and defaults, vertex attributes,
           textures, uniform blocks (with every uniform's offset) and each program - the options it
           sets away from default and its code size per stage.
  .gsh     old Wii U shaders (Gfx2, in Lib\gsys): attributes, uniforms, uniform blocks and textures.
  .pck     agl_ndsr.pck (NDSR): its vertex / fragment / compute shaders and data buffers - looks like
           a small neural network, probably the image upscaler.
  .baglmf  UI\MultiFilter: the blur filter chains behind menus and text (BgBlur, PauseBgBlur...):
           the steps switched on (shrink, blur, enlarge, colour correction) with their settings.
  .bin     Lib\sead\primitive_renderer\*_shader.bin: the debug-shape shaders. These keep readable code:
           the Switch 1 file its NVIDIA assembly, the Switch 2 file its SPIR-V (disassembled, with the
           original names such as BlockWVP and ColorRate).
  .bagst   Sound\Group\GroupSetting: the sound mixer - every sound group in its tree (Root/SeRoot/3D/
           Player...) with volume and the buses it feeds (Main, IndirectSound, FinalMix), the 117
           duckers (which groups get quieter while another plays, e.g. NPC talk lowers BGM to 0.6),
           which groups each pause type stops, and filter tables.
  .belnk / .bslnk  ELink2 / SLink2 (xlink2): which effects (or sounds) each actor plays. 2,812 users
           (KongStyle, DonkeyKongHelmet, CityLevelDecoModel51...), each with its actions (animation /
           state names) and the assets they trigger (with frame windows), and every asset's parameters
           - effect name, bone, scale, colour... Layout from dt-12345's open-source xlink2 tool.
  gsys.bgapkginfo / .bgapkglist / .bgmsconf / .bptclconf (Shader\Project...sarc): the shader
           package's name and file list, the renderer setups ("setups": which features Default, Map,
           Layout, Closet, Island... switch on - shadows, SSAO, DOF, HDR, the NDSR upscaler...) and
           the particle system's buffer sizes. Many renderer settings are still unnamed (see T.6).
  .raw     Lib\agl\...\arrow.raw: not a mesh but a 32x32 RGBA picture - the mouse pointer. The JSON
           draws it as text; press "Textures" to see it and save it as PNG.
  .rsizetable  System\Resource\ResourceSizeTable: the memory the game reserves for every file (133,504
           entries). Files are stored as path hashes; at the end of a scan the tool names every hash it can
           match against the files it scanned, then lists sizes by folder, the 100 largest and all of them.
  .listing not a game file: the Switch 2 dumping tool's FTP server writes one per folder ('ls -l' list
           of that folder). Shown as "Folder listing (made by the FTP dumping tool…)".
  Mario Kart World terrain (TerrainArc\Game03_Proc\*.hght / .norm / .mate / *.extm): select one and click
  Textures to see it as a picture (heights, slopes, ground materials, far colour map, baked light,
  water, off-road slow-down); right-click a .hght file > "Open 3D view of this terrain tile" for the
  landscape in 3D, coloured by its ground materials. 6000000000 = the whole island.
  Lib\sead\nvn2_font\nvn2_font.xtx: Textures shows the debug font.

  Mario Kart World (first pass - headers and lists; deeper data still being worked out):
  .tscb    TerrainArc\<scene>.tscb: the terrain layout - 14 layers (height, normal, material, water,
           road, dirt, biome, baked light...) and every terrain square with its position and size.
  .hght/.norm/.mate/.extm  TerrainArc\<scene>\: one terrain square's layer ("TARB"): unpacked and
           its header shown; the pixel layout inside isn't decoded yet.
  .dlpl/.dlpg/.dlpa/.dlpr/.dalp/.bkls/.dcgm  Env\: baked lighting (light probes and light sources):
           counts, bounds and positions where known.
  .bcul/.bbox/.bgrd  Cull\: what can be seen from where - ID lists, every object's box, on/off grids
           (drawn as # and .).
  .dapg    Sound\Acoustic\Probe: sound reflection grid (64 x 64 table + records).
  .ghost   StaffGhost\: the developers' time-trial ghosts - recording date shown; replay data packed.
  .ainb    AI\: AI / game-flow node graphs - counts and all names used.
  .xtx     Lib\sead\nvn2_font\nvn2_font.xtx: a plain NVN texture - the built-in debug font (128 x 128,
           8 x 16 characters), drawn in the JSON as # and . .
  .baatarc (newer, "ATRA") sound attenuation curves kept by name hash (names/curves not decoded yet).
  .pccg    Cull\Gridify: the object IDs a visibility grid covers (+ cell data, not decoded yet).
  .sharc   ("AAHS") older agl shader archive with the GLSL source text (Japanese comments shown properly).
  .bphsc   many Mario Kart World .bphsc files hold no collision at all: shown as "(empty - no collision)".
           *_Water.bphsc hold a water height grid instead (128 x 128 cells): the JSON draws it with '~'.
  .bfcpx   (Mario Kart World) several font lists in one file are shown as "variants".
  .bushvt  UI layout shader variation tables;
  .webm    videos (romfs\Movie: move guides, skill demos...; VP9, 960x540, 60 frames a second, no
           sound). Click one and press "Play" (or double-click it): the video player plays it in a
           loop. Space = play/pause, Left/Right arrows (or the arrow buttons) = one frame back or on,
           the slider jumps anywhere, speed 0.1x to 2x, "Save frame as PNG..." saves the frame shown.
           Decoded with libvpx (BSD licence, source\vpx\LICENSE_libvpx), so no Windows video add-ons
           are needed.
  .bfres   3D models and animations (Model\*.bfres.zs, about 5,800 files). JSON: for a model, its
           parts (shapes) with vertex/triangle counts and vertex formats, materials with the
           textures each uses (albedo, normal map, roughness...), the skeleton (bone tree with
           positions) and the list of textures needed; for an X.anim.bfres file, its skeletal,
           material and visibility animations with frame counts.
           Click a model file and press "3D view" (or double-click it) to see the model in its
           bind pose with its colour textures (read from romfs\Tex\<name>.bntx.zs - the newest
           update's copy, else the base game's); the "Textures" box switches them off. Parts
           the game keeps hidden until needed (alternative eyelids, mouths...) start unticked;
           untick/tick parts to hide/show them. The parts list has three
           columns: # (the part's number in the model, in file order; attached heads/ties are
           numbered after the body), the part name (shape__material; "Head: " / "Tie03: "
           for attached models) and its triangle count. Click a column header to sort by it
           (click again to reverse), or pick an order in the box above the list (biggest
           first, name A-Z, number, by model, shown first); type in "Find a part..." to
           narrow the list - Show all / Hide all then apply to just the parts listed. Exports like the terrain (OBJ/PLY/STL/GLB);
           GLB and OBJ exports include the colour textures (inside the GLB, or as PNG files
           next to the OBJ).
           Animations: the 3D view looks for animation files for the model - its own
           X.anim.bfres.zs, and shared ones the game uses under another name: DK's body
           (DonkeyKong, DonkeyKongNaked) uses PlayerAnimation (768 moves), his head
           PlayerHeadAnimation, his ties PlayerTieAnimation; Pauline outfits use
           PaulineAnimation... (newest update's copy first). Pick the file in the first list
           and an animation in the second; Play/Pause and the frame slider control it. The
           speed box next to Play: 1x is 60 frames a second (the game's speed); 0.25x, 0.5x
           and 2x (120) are there too. "Keep in place" (on by default) takes out the Root bone's travel,
           so cutscene moves that walk DK far across the scene stay in view.
           (These games store animation curves in a newer form - Hermite keys with a value and
           a slope each - which the viewer now reads.)
           "Other animation file..." loads any .anim.bfres.zs, e.g. a cutscene's
           Demo...PlayerAnimation for DK. Bones are matched by name. (Exports are always the
           bind pose.)
           DK in one piece: opening DonkeyKong (or DonkeyKongNaked, or a Bananza form such as
           KongStyle) also loads his head and attaches it to the body's NeckPoint bone, and for
           DonkeyKong all his ties on TiePoint (Tie00 shown; tick another "TieNN" part in the
           list to swap). The head and tie play the animation of the same name from their own
           files (PlayerHeadAnimation, PlayerTieAnimation) so the face and tie move with the
           body (PlayerTieAnimation only has cutscene and special clips - in normal play the
           tie is swung by cloth physics, which isn't simulated here). The "Head and tie
           attached" box hides or shows them.
           Other characters in one piece: for any other model, the viewer reads the actor's
           pack (Pack\Actor\<name>.pack.zs) the way the game does: its SubActorParam lists the
           models fixed to the body and which bone each goes on, and those models' own packs
           list theirs. Pauline: her head on NeckPoint, and her hair on the head's Head bone.
           Each joined model plays the clip of the same name from its own animation files
           (PaulineHairAnimation for her hair). Without a pack, a model called <name>Head is
           joined on NeckPoint as before.
           Colours: models whose animation files hold colour clips (DK: FurColor00-36,
           FurMaxColor, PantsColor00-09, PantsColorPattern00-09 in DonkeyKong(Animation).anim) get
           a "Colours" list under the animation controls (furs carry their in-game names, e.g.
           "FurColor32 - Fractone Gradient Fur", from Glossary\Cloth in the English messages). A still clip swaps the parts' colour
           texture and tint, and stays on while you pick more (fur from one, pants from another);
           an "(animated)" one (Damage..., FillAdrenaline...) plays its flash or glow on a loop,
           with or without a body animation. "Colours: as modelled" undoes them all. Exports use
           the swapped textures but not the tints. Gradient furs (FurColor18 and up): the clip's
           mask (DonkeyKongMask_Msk.N) is projected onto DK from the front and its pattern colour
           blended over the fur towards the hands, forearms, hips and the top of the head (Split
           furs: one side), per vertex - the game does it per pixel, so edges are softer here
           and colours approximate. Not drawn: the extra
           roughness / metal maps; glow is an even colour added over the part.
           Faces: DK's face moves by key shapes (morph targets: Joy, Angry, Chew, Shout... on the
           brow, mouth, tongue and teeth), driven by the shape clips in DonkeyKongHead.anim of
           the same name as the body clip - these play along with the body. Opening
           DonkeyKongHead on its own lists those face clips too.
           Eyes: material clips that move the eye texture (EyeCircle, EyeHighlight,
           MinecartTurnLeft... - the pupils look around) and visibility clips that swap the
           eyelid pieces (Rest - drowsy half-closed eyes, WaitEatHit...) also play, when the
           file has a clip of that name. The eyes' shine (the "EmmMix" texture) is added on top
           of the eye; EyeHighlight moves it. The animation list now includes every clip a file has
           (bones, face, eyes, visibility). Files with only colour clips (DonkeyKongAnimation,
           DonkeyKong.anim's trouser colours) are still skipped - those aren't shown yet.
           Cloth overlay while animating: particles, links and collision shapes follow the
           nearest bone of the skeleton, so they stay on the moving model (no swinging - that
           would need the cloth simulation). The neck band under the knot follows the tie
           that's ticked: its colour texture becomes DonkeyKongTie_Alb.<tie number> (as the
           game does); tie 09 uses the body's own Tie_09 band, and ties 10 and 11 bring their
           own neck piece so the band is hidden.
  (.bfres, .bars, .asb and .baev files up to 400 MB are read; other types up to 32 MB.)
  .msbp    message project (Mals\<language>.Product.*.sarc.zs\project.msbp): the named text colours
           (MainHintAccent, CharacterName, EmeraldAccent...). In .msbt JSON a colour tag is [0:3 NN-00]
           (colour NN from this list; [0:3 FF-FF] ends it). The event viewer draws coloured words in
           these colours (very bright ones darkened so they stay readable on the white page).
  .bfevfl  event scripts (Event\EventFlow, ~360 files): conversations, cutscenes, shops, credits.
           JSON: each flowchart's entry points (Talk, Wait...) with a readable "script" - the steps in
           order, e.g. "Event8: NpcCommon.TalkMessage(ActorName="NpcEel", MessageLabelSuffix=...)",
           "switch NpcCommon.TalkMessageSelect2(...)" with a "case" per answer, "at the same time:"
           for parallel branches, "go to EventN" where paths meet - then the actors (with the actions
           and questions each one offers) and every event in full. X_TL001 files are timelines: timed
           clips such as a cutscene's voice line (StartVoice LabelName=Talk_...).
           Event viewer: click a .bfevfl file and press "Event" (or double-click it). Left: the
           script's entry points (Talk, Wait...) and timelines. Right: the steps in order with the
           real dialogue under them, in the language picked at the top (read from romfs\Mals of
           the newest update): questions show each answer's text next to its branch, parallel
           steps are grouped, "continues at EventN" marks where paths meet. "Dialogue only" hides
           steps without text; "Show step settings" shows each step's parameters. Text can be
           selected and copied. Works without re-extracting.
           Version 4 files (Nintendo Switch Sports Resort) also have sub-flows that return a result
           ("return 1") and calls that branch on it ("run X and switch on its result" / "result 1:").
  Env\*.genvb.zs  lighting / environment archives, one per level, mini-game and menu. Inside, Nintendo AAMP
           parameter files (decoded to JSON; names are stored as hashes, about half are recovered -
           the rest show as 0x........):
             .bprojgfxbase  sun/moon/stars, fog, height fog, sky, clouds, wind, shadows, exposure
                            (default + one per area or cutscene, e.g. demodonkeydance002)
             .baglblm bloom  .bagldof depth of field  .baglccr colour correction  .baglsky sky
             .baglenv lights and fogs  .baglenvset presets  .baglshpp shadows  .bgenv file index
             .baglcube reflection cubemap capture points (name, position, near/far, on/off; the
                       JSON lists them under capture_points)
             .baglfila anti-aliasing (FXAA, in the *LevelMap archives; only some names known)
           Colours show as rgba plus #RRGGBB (bright HDR light colours: the hue, plus 'brightness').
           collect.genvres holds the level's environment textures: press "Textures" to view them.
           default.bgsdw (ProductCommon, Layout, ClosetScene...): which light casts the sun shadow,
           an optional projected shadow texture and the ambient occlusion (SSAO) settings - listed in
           the Lighting viewer as "Shadow light, projected shadow, SSAO".
           .baglvolm (ForestLevel maskon / maskoff): volumetric light - the sun shafts through the
           trees: each unit's distance range, layers, colour and shadow strength.
           default\default.bofx (some levels): the sun's lens flare - a BFRES with the flare textures and
           its settings (default.baglofx): which textures, and for each of up to 20 flare elements where
           along the light-to-screen line it sits, its size, colour and intensity. In the Lighting
           viewer it is listed as "Lens flare (sun)", with a "Lens flare textures" button.
           Lighting viewer: select a .genvb.zs archive folder in the tree (or any file in it) and press
           "Lighting" (or double-click a file). Left: the files, by kind (sun/fog/sky, lights, sky,
           bloom, depth of field, colour correction, shadows...). Right: a rough preview of the sky,
           sun position, ground and fog for the chosen setup (e.g. demodonkeydance002 uses its own
           files where it has them, otherwise the default ones), colour swatches for the main
           lights, and every setting of the chosen file with colour swatches. "Compare with" puts
           another file of the same kind alongside (a cutscene's file is compared with default
           automatically); differences are highlighted, "Only differences" hides the rest.
           Ctrl+C / right-click copies the selected rows. Reads the game files directly, so it
           works without re-extracting.
  .asb     animation state graphs (AS\*.root.asb.zs) and .baev animation events
           (AnimationEvent\AsNode\*.baev.zs): the node layout isn't decoded, but the JSON lists
           every animation, state and parameter name in the file (e.g. DonkeyKong.root has 1,365,
           such as Wait, RunStart, PunchChargeLeft, MinecartWait), so these names are searchable.
  .bphcl   cloth / hair / fur physics (Phive\Cloth, Havok Cloth 2022): for each simulated piece
           (e.g. DonkeyKongTie00 -> Cloth_Tie) its bones, particles (which are pinned to the
           animation), rest positions, mass, gravity and damping, the constraints holding it together
           with their stiffness, and the collision capsules / spheres / planes it can't pass through
           (DK's tie has a pair for each Bananza form). "settings" is the game's own cloth setup
           (AAMP: cloth name, base bone, preset such as SpringGravity0); "full_data" is everything.
           3D view: click a .bphcl and press "3D view" (or double-click it) - the model of the same name
           (e.g. DonkeyKongTie00) opens with its cloth drawn on it: particles as small diamonds
           (yellow = simulated, red = pinned to the animation), links as lines (cyan standard, green
           stretch, orange bend) and collision shapes as purple wireframes. Each is a part in the list
           (the Bananza forms' collision shapes start unticked); "Cloth physics" turns them all on/off.
           Models with a cloth file of their own name (DonkeyKongHead, BossKong...) show it too, and
           DK's attached head and tie bring theirs. The overlay follows the nearest bones when animated.
           "Simulate cloth while playing" (on by default) runs the cloth while an animation plays:
           the pinned particles ride on the skeleton, the rest fall and swing, held by the links and
           pushed out of the collision capsules, and the bones on them (Tie_2-6, Dyn_Hair_..._2/3)
           follow - so DK's tie and hair swing. Every tie gets it, not just the first. Jumping in the
           animation (slider, another clip) starts it again from the pose. An approximation of the
           game's Havok cloth: stiffness, damping and gravity are guessed.
           Helper bones: models with a .bphhb in Phive\HelperBone.alsarc.zs (DK, also used for
           DonkeyKongNaked) get "Helper bones" (on by default): while an animation plays, the Sub_
           bones are moved from the other bones' rotation as the file says - e.g. the top of the
           upper arm untwists with the shoulder, knees and elbows push out, finger joints bulge -
           so joints keep their shape. Each driver's turn from the bind pose is split into twist
           (X) and swing (Y, Z); the curves' values are moves in the base bone's space and turns
           about X / Y / Z (the three unnamed rotate settings).
  .bphsh   Phive collision shapes (Phive\Shape\Dcc, about 5,300 files): Havok 2022 hknpMeshShape -
           size, bounds, vertex/triangle counts and the collision materials each part uses.
           Click one and press "3D view" (or right-click) to see it, coloured by material;
           it exports like the terrain (OBJ/PLY/STL/GLB). These are what DK and objects collide
           with, not the visible models.
  .baatn / .baroc / .baudc / .baacl   sound distance attenuation (Sound\Attenuation\*.baatarc):
           .baatn presets (Near, Medium, Far, FarLog, Far6dB...) name the curves they use; .baroc
           roll-off curves (min/max distance and falloff), .baudc the ...Log falloff curves, .baacl
           distance low-pass curves. Fields ending in '?' are educated guesses at their meaning.
  .bphhb   helper bones (Phive\HelperBone.alsarc.zs: DonkeyKong, CandyKong, gorillas...): extra bones
           such as Sub_Chest_L or forearm twist bones that the game moves from other bones' rotation
           through small curves. JSON has the full data plus a 'summary' in words, e.g.
           "Sub_Chest_L (relative to Spine_2): moved by Y = curve 0 of Arm_1_L's rotation".
  .bphsc   Phive static compound bodies: all the fixed collision of one level scenario (e.g.
           ArenaLevelStageScenario01, CityLevelStageScenario01) - hundreds of collision meshes and
           boxes, each placed with a position, rotation and scale. JSON: level bounds, instance
           count, shape types, triangle totals, materials used, and an instance list (placement
           hash that links it to an actor, position, rotation, scale, triangles, materials).
           "3D view" draws the whole level's collision in place, coloured by material.
  .bntx    textures: name, size and format of each (JSON). Select one and click "Textures" (or
           right-click > View textures) to see them - filter by name, zoom with the mouse wheel,
           drag to move, double-click to fit, change the background - and save one or all as PNG.
           Switch 2 textures use a slightly different memory layout to the Switch's; both are read.
  .bflyt   UI screens and components (in Layout\*.blarc and UI\LayoutArchive\*.blarc, blyt
           folder): size, textures, fonts, materials, the pane tree (pictures, text, groups,
           parts = other layouts used as components) and groups. Select one and click
           "Layout" (or right-click > View layout): the screen is drawn from its pictures and
           text, with the parts filled in; pick an animation and Play it or drag the frame
           slider; tick panes off in the list to hide them; "Show hidden panes" reveals panes the
           game only shows during animations; "Outlines" draws every pane's box. Mouse wheel
           zooms, drag moves, double-click fits. "Save view as PNG..." saves what you see.
           Text language: the "Text:" list picks a language (English, Japanese, French...) from
           the game's message archives (romfs\Mals\<language>.Product.<version>.sarc.zs, the
           newest version found in the dump is used). Each text box gets the message whose label
           matches its pane name (LayoutMsg\<screen>.msbt). Text the game fills in while
           playing (numbers, names, which menu buttons appear) keeps the layout's sample text;
           the line above the picture says how many boxes came from the messages. Button icons
           names the game inserts show as {Name}.
           Fonts: text is drawn with the game's own fonts (romfs\Font\Font*.bfarc - the Chinese,
           Korean and Thai sets are used for those languages), including the controller-button
           and icon characters. Untick "Game fonts" to use a Windows font instead (buttons then
           show as (A), (ZL), (+)...).
           Approximation: panes drawn by GPU shaders (rounded boxes, neon outlines) show as
           half-transparent stand-ins.
  .bflan   UI layout animations (in Layout\*.blarc archives, anim folder): name, frame range,
           groups, and for each animated part (pane, material or user data) one line per curve -
           what moves (position, visibility, colour, texture scroll/swap...) and its keys as
           frame=value. Select one and click "Layout" to watch it play on its screen.
  fonts    (in Font\Font*.bfarc archives). Select one and click "Font" (or right-click > View font)
           to see every character it has (click one for details, double-click to add it to the
           sample line; type your own sample text; pick a size).
           .bfotf/.bfttf: real OpenType/TrueType fonts, lightly scrambled. JSON: family, style,
             glyph count and every character range.
           .bffnt: bitmap font (PictColor = the colour icons used in UI text). JSON: sizes, sheets
             and each character's glyph and width.
           .bfcpx: the font names layouts use (FontCaption.fcpx...): which real fonts are combined
             and which characters each one draws.
  exefs\main, sdk, rtld (type "executable"): program name and build path, build id, SDK and
           middleware versions, source file paths, exported/imported functions (C++ names
           shortened), and the readable strings (memory address + text)
  exefs\main.npdm  program settings: name, main thread, title id, the system services it may
           use, file-system permissions and kernel capabilities
  Nintendo Switch Sports Resort (NSSR) formats:
  Mii\*.dat (88 bytes)  a Mii (Switch CharInfo): id, nickname and every face / body setting.
  scft\*.otf  plain OpenType fonts (not scrambled): names, glyphs, characters; the Font viewer opens them.
  GolfSwing\*.gswg  recorded golf swings: two numbers per frame (meaning not fully known), their ranges and peak.
  BowlingReplay\*.bwr  recorded bowling throws: header, frame count, the 10 pins' flags and key frames
           (the frame-by-frame movement isn't decoded yet).
  Bake\Probe\Scene\<scene>\  baked light probes: meta.bkprmb = the grid (corner, block size, blocks),
           X_Y_Z.bkprb / resident.bkprcb = cells naming their 8 corner probes (the light values aren't decoded yet).
  Lib\NintendoSDK\IconBody.dat (NFIB)  the bodies drawn under Mii heads in icons: sizes and bounding box.
  VoiceText\...  the text-to-speech engine (ReadSpeaker VoiceText): each file identified (language, voice,
           engine version); word lists and the user dictionaries (how names are said) decoded in full.
  The extraction summary only lists formats it couldn't identify at all.

VOXEL TERRAIN
  Voxel/Const/<stage scenario>/ holds a stage's breakable terrain:
    MaterialTable.bin   the stage's numbered material list (Grass050, Rock131, Money001...)
    <part>/             one folder per terrain piece (a GUID or a name like HillLevelParts02_Default):
      Info.bin   size in voxels (x, y, z) and voxel size (1.0 or 1.2)
      vhead.bin  which 8x8x8 blocks have voxel data, listed per z layer
      vbody.bin  the voxels: solid / surface / empty counts, approximate volume, voxels per
                 material (named from MaterialTable.bin), and one line per block:
                 [x,y,z]  solid N  surface N  main materials
      ehead.bin / ebody.bin  surface edge data used to build the terrain mesh (summary only)
  Search for a material name (e.g. "Money001") to find every block that uses it.

3D VIEW OF TERRAIN
  Click any file in a terrain part folder (or the folder itself) and click "3D view" above the
  viewer, or right-click the part folder in the tree > "Open 3D view of this terrain part".
  A new window builds a smooth 3D surface of the part:
    drag = rotate, right-drag = move, mouse wheel = zoom, double-click or R = reset the view.
    Untick materials in the list to hide them (e.g. hide grass and soil to see what's underneath).
    "Cut off the top" slices the part at a height so you can look inside.
    "Low resolution" is ticked when a view opens so it loads quickly; untick it for full detail.
    Mario Kart World: right-click TerrainArc\<scene>.tscb.json -> "Open 3D view of the whole terrain" stitches
    the terrain tiles into one landscape (low resolution: the 21 biggest tiles; untick for 85 smaller ones).
    Right-click the ground in it -> "Open this area in full detail": a new view of the 1,000 x 1,000 units around
    that spot, built from the smallest tiles there (low resolution: a point every 1 unit; untick: every 0.5).
    The ground is coloured with the game's own colour maps (each tile's color_mul2 layer, which every tile size
    has down to the smallest; albedo_far where it's missing), with each ground material's detail texture (grass,
    gravel, sand... from Tex\CmnTex_Tera_Product_Proc_Alb.bntx.zs) laid over it - visible when you zoom in close.
    Seas, lakes, rivers and lava are drawn from each tile's water layer (.water.extm) as flat surfaces where they
    sit above the ground, plus the "special surface" objects placed in the world (the volcano's lava pools,
    chocolate, ghost swamp... from Banc\World_Game03\WorldResident.bcett.byml.zs and their models), listed as Water / Lava / Chocolate... in
    the materials list.
    Cliffs and ice placed in the world as models (the generated cliffs of each 1000 x 1000 square, hand-placed
    cliff pieces, ice shelves and icebergs) are added too, coloured by area.
    The whole-map view uses their lighter far versions where the game has them.
    The game's merged scenery pieces (Model\Piece_Gridify_Game03_X.._Z..: the fine 4 x 4 pieces of each square in
    full-detail areas, the coarse 2 x 2 ones on the whole map) and the race courses' own models (Stage_...) are
    added too; the generated cliffs and ice walls come from the pieces. Other scenery in them (rocks,
    buildings...) is listed as "Scenery (other)" and starts hidden.
    Cliffs, ice and courses are drawn with their own textures (made small: 256 pixels in full-detail areas, 64 on
    the whole map). Models and textures are loaded on all the processor's cores at once.
    Water, muddy water and ghost swamp water are drawn by a water shader built from the game's own water
    (Model\TeraModel_Water.TeraWater and Gyml\TerrainWaterMaterialGParam\Deep / Mud / GhostSwamp), lighting aside:
    the colour by depth (the game's SubColor in the shallows to its BaseColor in deep water), see-through in the
    shallows and more solid deeper, its SolidColor at low angles, a sun glint from its roughness, foam
    (WhiteWaveColor) where it's under about 1.2 m deep, and the game's ripple texture (Tex\CmnTex_Tera_Water_Emm)
    bending the surface. The ripple size isn't in the game's files: "Water ripple size" (20-400 units, 160 at
    first) changes it at once. The ripples move: two copies of the pattern drift in different directions at
    "Water ripple speed" (0-20 units a second, 4 at first; the game's speed isn't in its files either).
    Waterfalls drawn with the game's waterfall texture (e.g. BigWaterfallJG000_B, ShortWaterFall) slide their
    texture down the fall at a rate set by the same slider (our choice too). They're see-through like water (the
    teal about half solid, the white streaks solid - our choice; the game's blending isn't in its files). While water
    is on screen the view redraws about 30 times a second for this - set the speed to "still" (0) to stop that on
    slower computers. The depth range, see-through amounts and foam depth are our choices too. Other
    water surfaces (waterfalls, the game's placed water models) get the same game colours as if 4 m deep (our
    choice: they have no depth), with the gloss following their own slope. A stage's or landmark's river surface
    (a part whose colour texture is one of the game's WaterSurface textures, e.g. Stage: RaftingRiver_A's river) is
    listed on its own as "<entry> (WaterSurface)" and drawn the same way - its own texture is the riverbed the game
    draws its water over. Graphics
    without OpenGL 2 shaders get the older water (colour x ripple, slightly see-through). The placed special surfaces (lava pools,
    chocolate...) use their models' own textures (lava: Tex\CmnTex_Special_Lava_Alb).
    "Ground: ..." (under Low resolution) colours the ground by another of the terrain's layers instead of the
    game's colours, to see where its values are high or low: sky_bounce red (baked sun shadow: 255 = sunlit, offset
    like a cast shadow), green (baked night / artificial light, stored inverted: low = lit by lamps or headlights;
    underwater ground = 0), blue (baked occlusion from placed objects: low at tree bases, building footings, under
    bridges), all three; bake_gi, bake_light, the .hght map 1+2 (fine height detail: 120 = no change,
    follows the ground's small bumps up close, speckle on the whole map), the .hght map 3 (a signed 8-bit value: grey 0,
    red positive, blue negative; mostly 0 at full detail, on slopes in the distant squares - looks like a height
    correction between detail levels), steepness. Single values: blue (low) - cyan - green - yellow - red (high).
    Ground textures and water ripples are left off while a layer is shown.
    "Open this area in full detail" keeps the choice.
    Choosing a layer shows only the ground and the sea / lakes (cliffs, ice, courses and scenery unticked) and
    keeps the view you're looking at.
  "Light probes: ..." (MKW terrain views) shows the course's baked light probes (Env\World_<scene>.dlpr.zs: up to 4
    probes stacked above the corner of each 125-unit square, about 28,000) as dots coloured by one of their 32 values
    (blue low - red high, 1st-99th percentile) or by a set of 3 values as red, green, blue (square root, full at the
    95th percentile). The 32 values look like two lights of 4 colour sets each: values 1-12 = light A (1-3 always
    positive: bounced daylight, taking the colour of the ground around it - white over sand, green over grass, light
    grey over snow, brown over scrub, blue-green over the sea; 4-12 signed = how it changes with direction), 13-24 = light B
    (dark in open fields, red over lava, pink in Peach Stadium, yellow at farms and under the floating ship - nearby
    light sources), 25-28 = open sky (how much sky the probe can see: 25 the amount - about 1.77 = sqrt(pi), a
    whole open sky, over open sea; 26 how much is overhead - about 1.53 for open sky; 27 open to the south (+) or
    north (-); 28 open to the east (+) or west (-)), 29 = how much to fade out the sun / moon light and its
    shadow (0 in the open; about 3.5 = fully, under tunnels and in the volcano / storm / haunted areas; also high
    near the game's own lights - about 2.5-2.9 over boost panels, a ring round a buoy's lamp with a low spot right
    under it - where at night the kart's moon shadow fades away; slanting sheets behind raised rock), 30-32 = which
    way that fade increases around the probe (non-zero only at its edges; 30 up / down, 31-32 sideways).
    Full-detail areas show their own probes, larger, plus the finer probe grids of the squares that have one
    (Env\World_<scene>_X###_Z###_<n>.dlpg.zs - towns, tunnels, bridges...) as smaller dots.
  "Sound probes: ..." (full-detail areas): the course's sound reflection data (Sound\Acoustic\Probe\Game03\A_B.dapg.zs,
    32 x 32 files of 500 m, 64 x 64 cells of 7.8 m each) as dots at each record's lowest height, coloured by one of
    its fields: lowest / highest height, 2 counts, 4 values, 12 distances to the nearest surface (3 rings of 4: 1-4
    low - slightly down, 5-8 up, 9-12 steeply up; in each ring north-east, north-west, south-west, south-east; about
    214-222 = nothing in range) and for each direction how enclosed it is (0-100: about 100 under a roof or
    against a wall, 5-10 over open sea); value 1 = the distance from the sea in metres (negative out at sea,
    -7596 at the south edge, positive inland), values 2-4 = 5 / 0.2 / 1 at sea and 0 inland; the heights are the ground and 90 m
    above it; the 2 numbers (always equal) are the surface type under the probe, for the driving sounds:
    low on tarmac / concrete, grass in between, grind rails and ropes higher, highest (23) on water, see-through
    grates and at the edges where the track ends and you jump. Hover a dot for all its numbers. Which file
    number is x is worked out from the ground heights (shown on the info line).
  Landmarks (terrain views): the other objects that are always present in the world (WorldResident) -
    mountains such as the double-domed CmnObj_RoundMountain, cranes, mission gates... - drawn when their model
    files are found.
  Everything else placed in Banc\World_Game03\*.bcett and Banc\World_Game03-Free\*.bcett (free roam's objects:
    coins, item boxes...; an object at the same spot in both is drawn once) that has a model: CmnObj_ objects as
    "Props: <kind>" ("Props: Coin", "Props: StreetLight"...), any other kind as "Other: <kind>" ("Other: EffectObj
    WarioStadiumEngineSteam"...). Full-detail areas: all of them. The whole map: the large ones (25 m across or
    more) and the rare ones (30 or fewer in the world); the first whole-map opening loads every model once to
    measure it, so it takes longer. Objects without a model: a small magenta diamond, "Markers: <kind>" (full-detail
    areas, unticked at first). Objects merged into the ground pieces (Gridify) are drawn with those.
  Rainbow Road (Mario Kart World): drawn at its real place in the world, 2,800-5,100 m up, starting just above the
    long bay bridge and climbing out over the bay (x about -1,400 to 800, z 3,200 to 7,700). The game places it
    whole: WorldResident's SystemObj_PointRainbow_01 (no position, so the middle of the map) loads
    Banc\Rainbow_00_Fix.bcett.byml.zs, so that file's coordinates are the world's. Everything in it is one group in
    the list, "Rainbow Road" - untick the group to hide it. Its things without a model ("Rainbow Road: marker ...",
    e.g. SystemObj_RainbowChangeSpace - huge boxes, the switch between the world and the course) start unticked. Its Stage_RainbowGround_A (a full-size, low-poly copy of
    the island, what the old fall-off glitch landed you on) isn't drawn, as it would cover the real island.
  Areas: the invisible zones (both folders) drawn as see-through boxes / balls (their Scale is half the size):
    "Area: WorldEndArea" (SystemObj_WorldEndArea - drive in and you're put back; ticked at first),
    rescue, valley / drop, keep out, course, glide and shallow-water areas (unticked at first); any other area
    kind as "Area: <its name>" (blue, unticked). Colours run from coral (out of bounds) through orange and yellow
    to green (course); see-through with stronger edges; in a full-detail area they're cut off at the area's edges.
  The materials / parts list is a tree: entries sharing a group ("Props: Coin", "Props: StreetLight"... ->
    "Props"; terrain ground materials -> "Ground"; "Lava" with "Lava: walls"...) sit under one row with the
    group's name and count. Every placed object, course, landmark, cliff and special-surface part has its own
    entry, named by the game's own name and grouped by kind: Stage (Stage_ courses), Object (CmnObj_ objects), Other
    (other prefixes), Landmark, Cliff (all areas, incl. ice), Iceberg, Scenery (merged ground pieces), Surface (the
    special-surface objects and SFC... surfaces, one entry per object and part: "Surface: Lava000_A - <part>"),
    Marker (no model), Road (road meshes, one per 1 km map square: "Road: Game03_X04_Z12"), Area
    (the game's area names: "Area: WorldEndArea" = free-roam out of bounds, coral), Ground (the terrain materials). Its tick shows or hides the whole group (a coral box with a dark square: only some shown); click its
    arrow (first column) to open or close it. Finding text opens every group. Counts are short: 9999, 12K, 1.3M.
    Hovering an entry shows the game's own name(s) for it: the terrain material, a model part's full shape / material name, or the
    placed objects drawn as that entry with how many are placed. Right-click an entry > "List what this entry draws" for the full
    list in a window you can copy from (Ctrl+A selects all), including the ground-piece parts' own names. The # column is the view's own entry number
    (the order entries were met while loading), not the game's number.
    A view can have up to 4096 entries (was 255), names up to 63 characters.
  In terrain views everything is its own entry in the materials list (ground, water, cliffs, courses, scenery,
    landmarks, each kind of prop, areas) - tick or untick any of them; the choices are kept when the view
    reloads (next area, low resolution, layers). Choosing a ground layer shows just the ground and the sea.
  Full-detail areas have an arrow labelled N, S, E or W just beyond each edge: click one to load the next area in the same window (same view and
    choices).
  "Controls" opens a window listing the mouse and keyboard controls; it can stay open. At the bottom left: the
    place under the cursor - on terrain maps the game's world coordinates in metres (0 = the middle of the map),
    the height and the 125 m square (the X / Z in the probe file names); right-click the ground to copy them.
    Right-click the ground > "List placed objects near here" lists every object placed within 200 m (from all of
    Banc\World_*\*.bcett.byml.zs), nearest first, with x, y (height), z, the folder and file, whether its model
    file exists (or can't be read), and what this view did with it: [drawn: <the list entry to tick>] or
    [not drawn: why] - to find things the viewer doesn't draw.
    Placed objects get the model the game's own settings give them (System\GymlArchive: the actor's ActorGParam,
    following $parent to its ModelInfoRef, then the model info's Fmdb), so objects that borrow another object's
    model are drawn too (e.g. MissionGate001_S uses MissionGate000_S); the name-based guess is the fallback.
    Areas are drawn in the shape the game gives them (PrimitiveType: Cube = box, Sphere = ball, CylinderHighRes =
    cylinder, round across its width and its height along its own up axis), not by their name; the objects near
    list shows each object's [shape: ...]. Objects without a model that have a box, ball or cylinder shape (warp pipe zones,
    trigger boxes, the heliport point...) are drawn as that shape too (see-through, under Marker, full-detail areas);
    the others stay small diamonds.
    Collectibles come from the game's own lists (System\GymlArchive\general_format.Product_*.sarc.zs,
    Gyml/FreerunCollectionListGParam/): "Marker: PSwitch" (the 394 P-switches, CmnObj_MissionGate000-003_S),
    "Marker: SpecialCoin" (the 200 Peach medallions, CmnObj_Coin001_A) and "Marker: HatenaPanel" (the 150 ? panels,
    CmnObj_HatenaBlockPanel000_A). Each is drawn as the game's own model at its real size and turn, on the whole
    map and in area views. The size slider under the options ("P-switches, ? panels, medallions") enlarges them
    from real size up to 100 times (fine steps at the small end), from each one's lowest point so it stays on its
    ground; the view updates a moment after the slider stops (kept by Save). Hover over one for what it is,
    where, and its mission (text, time limit, sticker) or sticker. The entry's tooltip gives the list file, how many
    are in view and the objects.
    The sticker's picture shows just below that hover tip (P-switches: the mission's reward; ? panels: their own),
    from romfs\Tex\CmnTex_Sticker_<sticker>_Alb.bntx.zs, cut to shape by its _Msk.
    P-switch missions: each placed P-switch names its mission (Dynamic.WTAType -> Gyml/WTAMission/<mission>.
    game__WTAMission.bgyml: MessageId, LimitSec, RewardStickerId). The sticker id is the 32-bit MurmurHash3 of the
    sticker's name (Gyml/StickerInfoGParam/<name>), and the mission's text comes from Mission.msbt in
    romfs\Mals\USen\MsgMain.msarc (EUen / AUen if there's no USen). Right-click a collectible entry > List what this
    entry draws: every one with its position and its mission (text, time limit, sticker) or sticker (? panels). The
    objects-near list shows the same for each P-switch / ? panel / medallion found there.
    Islands: small areas of their own entered by a tunnel (Banc\Island_<name>.bcett.byml.zs and _Fix). The
    tunnel's sensor in WorldResident names its IslandInfo, which names the island's Banc. Collectibles inside an
    island (two P-switches: Island_C_00 in Island_ChocoMountain_00, Mission_WT_DT_14 in Island_ShyGuyBazaar_00) have
    the island's own coordinates, so they're drawn at the island's way in and listed as "inside <island>".
    With a light probe entry chosen, hovering a dot outlines it and shows its actual number(s) in a tip by the
    cursor (the colours are relative: blue and red are the lowest and highest in view, given on the info line).
    The dots grow as you zoom in.
  While a 3D view loads or builds for more than a moment, a status line and progress bar over the bottom of the
    view show the step (ground tiles, placed objects, models / placing / areas x of y, ground pieces, textures,
    probes, building the surface %).
  An area opened in full detail from a view opens at that view's window size (maximised if it is).
  A drop-down choice too long to show scrolls slowly while the mouse is over it.
  The left-hand panel can be made wider or narrower by dragging the gap between it and the view; its width and the
    list's column widths are kept with a saved view. "Controls" (beside Reset) opens the controls window.
  "Save" (beside Reset and Top) keeps how a 3D view looks - the camera (zoom, angle, position), the options
    (resolution / textures, ground layer, probes, sounds, animation, colours, check boxes, list order), the cut and
    every ticked / unticked list entry - in saved_views\ beside ns2dig.exe, one file per object or area. Opening
    that object or area again shows it exactly like that once it has loaded (nothing saved: the usual start). Closing
    a window with X after changing a saved view asks whether to save over it.
  "Top" (or T) looks straight down on anything in the 3D view (terrain maps: the world's square in the
    top-left corner; anything else centred); "Reset" (or R / double-click) goes back to the starting view.
  "Walk the world" (Mario Kart World's whole-world view): see the world from inside it, the right way up, at a
    tall person's eye height ("Player height" sets it: 1 to 10, 3.5 at first), dropping in just above the middle of the map. Click the view to look around with the mouse; WASD moves,
    Space goes up, Shift down, Ctrl is faster, the scroll wheel or "Walking speed" sets the speed (10x at
    first, 1x a notch, 1x to 100x); Esc lets the mouse go. Where you are shows under Show all / Hide all.
    Around you the world loads in full detail (everything the "Open this area in full detail" view shows: scenery,
    placed objects, collectibles, water) and the rest of the world stays at low detail. As you move, the next square
    loads in the background (the old one stays shown meanwhile). "Full detail around you" sets the square's size
    (500 x 500, then 1000 x 1000 to 16000 x 16000 in steps of 1000; 1000 at first): bigger looks further but takes
    longer to load and uses more memory. The list on the right shows the square's
    parts; untick any to hide them. Unticking "Walk the world" goes back to the whole map (it loads again).
    Graphics memory for textures: the info box's "Textures:" line (e.g. "Textures: 3,812, ~650 MB GPU") shows how
    many the view holds and about how much graphics-card memory they take. It depends on how many different textures are in view, not on the area - the
    world reuses the same ones (coins, item boxes, trees, snow), so it climbs fast at first, then levels off.
    Measured on Mario Kart World 1.8.0 with the default ticks:
      16000 x 16000 (the whole Game03 map): 6,250 textures, about 1.1 GB - the most any setting needs
      Smaller areas need less.
    The view holds up to 16,384 textures (about 2.6 times the whole map). If that ever fills, the line says how
    many didn't fit (those are drawn without their texture). Walking keeps textures it has passed so going back
    is quick, and lets go of unused ones when the table is three-quarters full.
    "Collisions (C)" (on at first; C turns it on / off): what's drawn near you is solid - you walk with gravity, Space
    jumps ("Jump height" sets how high), Ctrl runs, you slide along walls, step up kerbs and fall off ledges. Space twice quickly flies (Space / Shift
    up / down); flying down onto the ground lands, Space twice in the air drops you. Water isn't solid (you walk on
    the bottom); anything unticked in the list isn't solid either. With collisions off you fly through everything.
    B tints the far-off, low-detail part of the world red (to see where full detail ends).
    Save while walking makes a save point: where you are, which way you look, collisions / flying, the sliders and
    every tick. Ticking Walk the world again starts there. Without a save point, walking starts with the Area group
    off and only the P-switch, ? panel and special coin markers shown.
  "Export PNG" saves the 3D view exactly as shown, at the window's size (the name includes the ground layer).
    (Build .106 kept a "tex_cache" folder next to ns2dig.exe; it isn't used any more and can be deleted.)
  "Export 3D file" saves the part (at the current resolution, without hidden materials):
    Mario Kart World terrain: everything ticked goes in - ground, water and lava, cliffs and ice, courses and
    scenery - with the colour map as vertex colours, the ground / water / scenery textures as shown in the window
    (GLB: embedded; OBJ: PNG files beside it), drawn from both sides, water a little see-through. One part per
    material (and per texture for placed models). The message at the end lists what went in.
    GLB  one file with colours (binary glTF 2.0): Windows 3D Viewer, Blender, Unity, Unreal,
         Godot, web viewers. Each material is a separate named part, so it can be hidden or
         recoloured; gold and metal materials are a little shiny. Models keep their colour
         textures (embedded as PNG); fur cards use the opacity map as alpha.
    OBJ  opens in most 3D programs (Blender, Windows 3D Viewer...); a .mtl file with the
         material colours is saved next to it, and faces are grouped by material. Models
         also get their colour textures as <file>_<texture>.png beside the .obj.
    PLY  compact, with a colour per vertex (MeshLab, Blender, CloudCompare)
    STL  for 3D printing: no colour, Z up, resting on z = 0. Sizes are in game units
         (1 voxel = 1-1.2 units), so scale it down in your slicer. Parts are closed solids;
         a few edges where two surfaces just touch may be reported and auto-repaired by the slicer.
  The cut slider isn't applied to exports (a cut would leave the model open).
  The 3D view reads the terrain from your original game dump, not the output folder, so the dump
  must still be where it was when you extracted the scan. If it has moved, you'll be asked for the
  new location (remembered in ns2dig.ini). Works with scans made by older versions too.
  Decoding the voxels makes extraction slower (roughly 2 MB of terrain per second).

  Voxel objects (ObjectData\Voxel\<name>.alsarc.zs - Fractonium NPCs, breakable pieces, boss
  parts, the DK amiibo body...) open in the same window: click a .vgeob file inside one, or the
  archive folder, then "3D view" (or right-click > "Open 3D view of this voxel object"). Each
  .vgeob layer (Rock_Type_.., Soil_Type_.., Dummy000...) shows as one material. The grid size comes
  from Voxel\Geometry\<name>.alsarc.zs in the same scan, so that folder needs to be extracted too.

SOUND PLAYER
  Click a .bwav file (music, sound effects, voice lines) or a .bars sound archive in the tree and
  click "Play" above the viewer, or right-click it (in the tree or the search results) > Play sound.
  A player window lists the sounds in the file (a .bars archive holds several, by name):
    double-click a sound to play it; Play/Restart, Stop, and a slider to jump around
    "Loop" repeats music at the game's own loop points (only sounds with loop points can loop)
    "Save as WAV..." saves the selected sound as a normal .wav file
  Like the 3D view, it reads the sound from your original game dump, so nothing extra is made at
  extraction. Handles DSP-ADPCM and PCM sounds, mono, stereo and more channels (mixed to stereo).

MSBT control tags appear as [group:type PARAMS] ... [/group:type].
The RomFS folder, last scan, recent searches, last ZIP folder and window layout are remembered
in ns2dig.ini.
If Windows Defender or SmartScreen flags ns2dig.exe (a false positive - small unsigned
programs often trip its machine-learning check), build it on your own PC instead: double-click
source\build.bat. It installs the Zig compiler with pip and writes a fresh ns2dig.exe here.
Before unzipping a downloaded copy, right-click the ZIP > Properties > tick "Unblock" > OK.

If the program ever crashes it says so and writes ns2dig_crash.txt next to the EXE -
send that file (it says what the program was doing and where it failed).

Command line (no GUI):  python ns2dig.py <romfs folder> <output folder>

source\ has the full C source of ns2dig.exe (alsarc_gui.c, its look in ui_theme.h / ui_main.inc / ui_skin.c, and the viewers: vox3d_*, snd_*, tex_*, lyt_*,
font_*, lgt_*, evt_*, vid_*, terrain_core.h and the other *_core.h files) plus a copy of ns2dig.py.

THIRD-PARTY CODE AND DATA (all open source; each licence is included)
  zstd              Zstandard decompression (zstd.c / zstd.h). BSD licence - source\zstd_LICENSE.
  libvpx            VP9 video decoding for the video player (source\vpx). BSD licence plus Google's patent
                    grant - source\vpx\LICENSE_libvpx and source\vpx\PATENTS_libvpx.
  texture2ddecoder  ASTC and BC texture decoders (source\t2d). MIT licence - source\t2d\LICENSE_texture2ddecoder.
  SPIRV-Headers     The SPIR-V instruction tables inside ns2dig.py (used to disassemble Switch 2 shaders)
                    come from the Khronos Group's SPIRV-Headers (https://github.com/KhronosGroup/SPIRV-Headers),
                    MIT-style licence - source\LICENSE_SPIRV-Headers.txt.

To build: double-click source\build.bat (the commands are in it).
  (app.rc embeds app.ico and app.manifest; make_icon.py redraws app.ico)


ABOUT
ns2dig and its demo ROM were written entirely with Claude (Anthropic) as an AI-assisted ("vibe
coded") project: every file reader, viewer, the scanner and the made-up demo game were produced by
Claude from my instructions, testing and feedback.

LEGAL NOTES (COPYRIGHT AND ANTI-CIRCUMVENTION LAW)
I am not a lawyer, and this is not legal advice. It sets out why I believe ns2dig does not breach
copyright law or anti-circumvention laws (the US DMCA, or the equivalent technological protection
measure rules in Australia's Copyright Act 1968). If you need certainty for your situation, ask a lawyer
where you live.

Why I believe this:
  1. No Nintendo code. Every file reader and viewer in ns2dig was written from scratch by working out
     how each file format is laid out, which is interoperability work. No Nintendo SDK code, headers,
     libraries or tools are included, and no Nintendo program code is copied into the app.
  2. No game content. The download contains no game files, models, textures, sounds, videos, text or
     fonts. ns2dig only reads files that the user already has.
  3. No keys and no decryption. ns2dig contains no console or game keys and cannot decrypt anything.
     It only reads a RomFS that has already been decrypted and extracted, so the app itself does not get
     around the copy protection on Switch 2 games or consoles.
  4. Open-source parts only. All third-party code and data is open source and used under its licence,
     with each licence included (see THIRD-PARTY CODE AND DATA above).

Things to be aware of:
  - Parameter names. About 300 short parameter names (for example "FogDensity") in source\aamp_names.h
    and ns2dig.py were read from the game's executable. The game files store these names only as
    hashes, so the names are needed to make lighting files readable. They are single words used as
    labels, not program code, and short names like these are generally treated as facts rather than
    creative works.
  - Font obfuscation. Switch 2 games store some fonts with a simple 4-byte XOR scramble. To display or
    export those fonts, ns2dig undoes it. This is a format step done on a file that has already been
    decrypted, not decryption of the game; whether it could ever count as a protection measure has not
    been tested in court. The fonts themselves are commercial fonts owned by their makers.
  - Making the dump. ns2dig needs a RomFS that has already been decrypted. Making one from a game
    cartridge needs the console's keys. In the US, the DMCA's exemptions generally do not cover
    individuals doing this, even for games they own. Australia has similar rules. That step is outside
    ns2dig and is the user's own responsibility.
  - What you export. Everything ns2dig shows or exports (models, textures, sounds, videos, text,
    fonts) is still owned by Nintendo or other rights holders. Looking at it for your own private study
    is very different from sharing or publishing it.
  - Sharing ns2dig. Share only the app and its source as distributed: never include game files or keys,
    and keep the licence files with it.

ns2dig is not made by, endorsed by or connected with Nintendo. "Nintendo Switch 2", "Donkey Kong
Bananza" and "Mario Kart World" are trademarks of Nintendo, used here only to say which games the app
reads.
