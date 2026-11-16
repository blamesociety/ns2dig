"""demo_data.py - the ns2dig demo's packaging files (used by make_demo.py):

  Pack/ZsDic.pack.zs            a SARC holding a zstd dictionary (zs.zsdic), trained on the game data below
  GameData/*.byml.zs            5 small BYML data files (items, quests, characters, shop, save flags) compressed WITH
                                that dictionary - they can only be read once the dictionary is loaded, as in games
                                that use .zsdic files
  System/Resource/ResourceSizeTable.Product.100.rsizetable.zs
                                a RESTBL: for every file in the demo romfs, the CRC32 of its path (without .zs) and
                                the memory it needs (its unpacked size, rounded up); two paths are also stored by name

(The app's viewers read files compressed without a dictionary; the dictionary-compressed ones here are data files
that only the scanner reads.)
"""
import os, random, struct, zlib
import zstandard


def _data_files(byml_file, seed=0):
    """{GameData/<name>.byml: bytes} - made-up game data"""
    r = random.Random(seed)
    items = [{"Name": n, "Price": p, "MaxCount": m, "Category": c, "IsSellable": s} for n, p, m, c, s in (
        ("DemoCrate", 50, 9, "Furniture", True), ("Shell", 5, 99, "Material", True), ("Driftwood", 3, 99, "Material", True),
        ("Coconut", 8, 20, "Food", True), ("SeagullFeather", 12, 30, "Material", True), ("PropellerHat", 300, 1, "Clothing", False),
        ("Map", 0, 1, "KeyItem", False), ("FlagCloth", 40, 5, "Material", True))]
    quests = [{"Name": "Q_Arrival", "Giver": "Islander", "Steps": ["TalkToIslander", "BuyCrate"], "Reward": {"Item": "Shell", "Count": 5}},
              {"Name": "Q_FindTheCrate", "Giver": "Islander", "Steps": ["VisitSmallIsland2", "OpenCrate"], "Reward": {"Item": "Coconut", "Count": 3}},
              {"Name": "Q_RaiseTheFlag", "Giver": "Islander", "Steps": ["CollectFlagCloth", "VisitSmallIsland3"], "Reward": {"Item": "PropellerHat", "Count": 1}}]
    chars = [{"Name": "DemoPlayer", "Model": "DemoPlayer", "Hat": "DemoHat", "Speed": 4.5, "JumpHeight": 1.2},
             {"Name": "Islander", "Model": "DemoPlayer", "Hat": "", "Speed": 2.0, "JumpHeight": 0.5}]
    shop = {"Shopkeeper": "Islander", "Stock": [{"Item": i["Name"], "Price": i["Price"], "Stock": r.randint(1, 9)} for i in items if i["IsSellable"]]}
    flags = {"Flags": [{"Name": "TalkedToIslander", "Type": "Bool", "Default": False}, {"Name": "CratesBought", "Type": "Int", "Default": 0},
                       {"Name": "FlagRaised", "Type": "Bool", "Default": False}, {"Name": "PlayTimeSeconds", "Type": "Float", "Default": 0.0}]}
    return {"GameData/DemoItems.byml": byml_file({"Items": items}), "GameData/DemoQuests.byml": byml_file({"Quests": quests}),
            "GameData/DemoCharacters.byml": byml_file({"Characters": chars}), "GameData/DemoShop.byml": byml_file(shop),
            "GameData/DemoSaveFlags.byml": byml_file(flags)}


def _dictionary(byml_file):
    """train a 4 KB zstd dictionary on many variations of the data files"""
    samples = []
    for s in range(300):
        samples += list(_data_files(byml_file, s).values())
    return zstandard.train_dictionary(4096, samples, dict_id=0x0DE0D1C7, level=19)


def build_dict_files(romfs, byml_file, sarc):
    d = _dictionary(byml_file)
    cctx = zstandard.ZstdCompressor(level=19, dict_data=d)
    for rel, data in _data_files(byml_file).items():
        p = os.path.join(romfs, *rel.split("/")) + ".zs"; os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, "wb").write(cctx.compress(data))
    os.makedirs(os.path.join(romfs, "Pack"), exist_ok=True)
    open(os.path.join(romfs, "Pack", "ZsDic.pack.zs"), "wb").write(zstandard.ZstdCompressor(level=19).compress(sarc({"zs.zsdic": d.as_bytes()})))
    return d.dict_id()


def restbl(romfs, decompress):
    """RESTBL v1 for every file under romfs: (crc32 of the path without .zs, size) sorted by hash; 2 named entries"""
    entries = {}
    for dp, _, fs in os.walk(romfs):
        for fn in fs:
            full = os.path.join(dp, fn); rel = os.path.relpath(full, romfs).replace(os.sep, "/")
            if rel.startswith("System/Resource/"): continue
            data = open(full, "rb").read()
            if rel.endswith(".zs"):
                rel = rel[:-3]
                try: data = decompress(data)
                except Exception: pass
            entries[rel] = (len(data) + 0x1FF) // 0x200 * 0x200 + 0x100     # unpacked size, rounded, plus a little
    named = ["Model/DemoPlayer.DemoPlayer.bfres", "Event/EventFlow/DemoIsland_Islander.bfevfl"]   # (as if their hashes collided)
    hashed = sorted((zlib.crc32(p.encode()), s) for p, s in entries.items() if p not in named)
    out = bytearray(b"RESTBL" + struct.pack("<4I", 1, 160, len(hashed), len(named)))
    for h, s in hashed: out += struct.pack("<II", h, s)
    for p in named: out += p.encode().ljust(160, b"\0") + struct.pack("<I", entries.get(p, 0))
    p = os.path.join(romfs, "System", "Resource"); os.makedirs(p, exist_ok=True)
    open(os.path.join(p, "ResourceSizeTable.Product.100.rsizetable.zs"), "wb").write(zstandard.ZstdCompressor(level=19).compress(bytes(out)))
    return len(entries)
