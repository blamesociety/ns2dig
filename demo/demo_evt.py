"""demo_evt.py - the ns2dig demo's event scripts (used by make_demo.py), all made up:

  Event/EventFlow/DemoIsland_Islander.bfevfl.zs   a conversation with an islander: entry points Talk, Farewell and
        Wait; dialogue, a question with 3 answers, a coin check with 2 outcomes, a fork (camera + sound at the same
        time), a loop back to the question, a sub-flow (Farewell)
  Event/EventFlow/DemoIsland_Arrival.bfevfl.zs    a cutscene: a flowchart (entry point Event) that plays a timeline
        (camera pans, the player waving, music, captions) and runs a sub-flow from the other file
  Mals/<lang>.../EventFlowMsg/<flow>.msbt         their text in 4 languages, and DemoProject.msbp (text colours)

BFEVFL (version 0.3, little-endian). Strings are u16 length + bytes + 0; pointers are file offsets (u64). Header
0x48: "BFEVFL\\0\\0", version, BOM, 0x10 file name (to its characters), 0x1C size, 0x20 flowchart / 0x22 timeline
counts, 0x28 flowchart pointers, 0x30 their names (DIC), 0x38 timeline pointers, 0x40 their names. Flowchart "EVFL"
0x48: counts at 0x10 (actors) 0x12 (actions) 0x14 (queries) 0x16 (events) 0x18 (entry points), 0x20 name, 0x28
actors (0x38 each), 0x30 events (0x28 each), 0x38 entry point names (DIC), 0x40 entry points (0x20 each, main event
at 0x1C). Timeline "TLIN" 0x60: 0x10 length (frames, 60 a second), 0x14 actors, 0x18 clips, 0x1A one-shots, 0x20 name,
0x28 actors, 0x30 clips (0x18: start, length, actor, action, parameters), 0x38 one-shots (0x18: start, actor, action,
parameters). Parameters: containers (type 1 + names DIC + child pointers; 2 int, 3 bool, 4 float, 5 string).
"""
import struct
from demo_ui import _z

ACTION, SWITCH, FORK, JOIN, SUBFLOW = 0, 1, 2, 3, 4


class _W:
    """a file builder: blocks are appended; strings go into a pool at the end, their pointers patched then"""
    def __init__(self, head):
        self.b = bytearray(head); self.str_patch = []; self.cstr_patch = []
    def align(self, a=8):
        while len(self.b) % a: self.b.append(0)
    def put(self, n, a=8):
        self.align(a); o = len(self.b); self.b += bytes(n); return o
    def q(self, at, v): struct.pack_into("<Q", self.b, at, v)
    def s(self, at, text): self.str_patch.append((at, text))          # u64 pointer to a pooled string
    def finish(self):
        self.align(8); pool = {}
        for at, t in self.str_patch + self.cstr_patch:
            if t not in pool:
                pool[t] = len(self.b); e = t.encode("utf-8"); self.b += struct.pack("<H", len(e)) + e + b"\0"; self.align(2)
        for at, t in self.str_patch: self.q(at, pool[t])
        for at, t in self.cstr_patch: struct.pack_into("<I", self.b, at, pool[t] + 2)
        return pool


def _dic(w, names):
    o = w.put(8 + 16 * (len(names) + 1)); w.b[o:o + 4] = b"DIC "; struct.pack_into("<i", w.b, o + 4, len(names))
    struct.pack_into("<iHH", w.b, o + 8, -1, 1, 0); w.s(o + 16, "")
    for i, n in enumerate(names):
        e = o + 8 + 16 * (i + 1); struct.pack_into("<iHH", w.b, e, i, i + 1, i + 1); w.s(e + 8, n)
    return o


def _params(w, d):
    """a parameter container from a dict (str / int / bool / float values); 0 for none"""
    if not d: return 0
    kids = []
    for k, v in d.items():
        o = w.put(24)
        if isinstance(v, bool): w.b[o] = 3; struct.pack_into("<H", w.b, o + 2, 1); struct.pack_into("<I", w.b, o + 16, 1 if v else 0)
        elif isinstance(v, int): w.b[o] = 2; struct.pack_into("<H", w.b, o + 2, 1); struct.pack_into("<i", w.b, o + 16, v)
        elif isinstance(v, float): w.b[o] = 4; struct.pack_into("<H", w.b, o + 2, 1); struct.pack_into("<f", w.b, o + 16, v)
        else: w.b[o] = 5; struct.pack_into("<H", w.b, o + 2, 1); w.s(o + 16, v)
        kids.append(o)
    c = w.put(16 + 8 * len(kids)); w.b[c] = 1; struct.pack_into("<H", w.b, c + 2, len(kids))
    w.q(c + 8, _dic(w, list(d)))
    for i, k in enumerate(kids): w.q(c + 16 + 8 * i, k)
    return c


def _actors(w, actors):
    """actors: [(name, [actions], [queries])] -> offset of the 0x38-byte records"""
    o = w.put(0x38 * len(actors))
    for i, (name, acts, qs) in enumerate(actors):
        a = o + 0x38 * i; w.s(a, name); w.s(a + 8, ""); w.s(a + 0x10, "")
        for off, lst in ((0x18, acts), (0x20, qs)):
            if lst:
                arr = w.put(8 * len(lst)); w.q(a + off, arr)
                for k, s in enumerate(lst): w.s(arr + 8 * k, s)
        struct.pack_into("<HHH", w.b, a + 0x30, len(acts), len(qs), 0xFFFF)
    return o


def _flowchart(w, name, actors, events, entries):
    """events: dicts with type, name and: action (actor, item, next, params) / switch (actor, item, params, cases
    [(value, event)]) / fork (branches, join) / join (next) / subflow (flow, entry, next); entries: [(name, event)]"""
    an = {a[0]: i for i, a in enumerate(actors)}
    fc = w.put(0x48); w.b[fc:fc + 4] = b"EVFL"
    struct.pack_into("<HHHHH", w.b, fc + 0x10, len(actors), sum(len(a[1]) for a in actors), sum(len(a[2]) for a in actors), len(events), len(entries))
    w.s(fc + 0x20, name)
    w.q(fc + 0x28, _actors(w, actors))
    ev = w.put(0x28 * len(events)); w.q(fc + 0x30, ev)
    for i, e in enumerate(events):
        o = ev + 0x28 * i; w.s(o, e["name"]); w.b[o + 8] = e["type"]
        nx = e.get("next"); nx = 0xFFFF if nx is None else nx
        if e["type"] == ACTION:
            ai = an[e["actor"]]; struct.pack_into("<HHH", w.b, o + 0xA, nx, ai, actors[ai][1].index(e["item"]))
            w.q(o + 0x10, _params(w, e.get("params")))
        elif e["type"] == SWITCH:
            ai = an[e["actor"]]; struct.pack_into("<HHH", w.b, o + 0xA, len(e["cases"]), ai, actors[ai][2].index(e["item"]))
            w.q(o + 0x10, _params(w, e.get("params")))
            cs = w.put(8 * len(e["cases"])); w.q(o + 0x18, cs)
            for k, (val, to) in enumerate(e["cases"]): struct.pack_into("<IH", w.b, cs + 8 * k, val, to)
        elif e["type"] == FORK:
            struct.pack_into("<HH", w.b, o + 0xA, len(e["branches"]), e["join"])
            br = w.put(2 * len(e["branches"]), 8); w.q(o + 0x10, br)
            for k, to in enumerate(e["branches"]): struct.pack_into("<H", w.b, br + 2 * k, to)
        elif e["type"] == JOIN:
            struct.pack_into("<H", w.b, o + 0xA, nx)
        elif e["type"] == SUBFLOW:
            struct.pack_into("<H", w.b, o + 0xA, nx); w.s(o + 0x18, e.get("flow", "")); w.s(o + 0x20, e["entry"])
    w.q(fc + 0x38, _dic(w, [n for n, _ in entries]))
    en = w.put(0x20 * len(entries)); w.q(fc + 0x40, en)
    for i, (_, main) in enumerate(entries): struct.pack_into("<H", w.b, en + 0x20 * i + 0x1C, main)
    return fc


def _timeline(w, name, frames, actors, clips, oneshots):
    """clips: [(start, length, actor, action, params)]; oneshots: [(frame, actor, action, params)] - frames (60/s)"""
    an = {a[0]: i for i, a in enumerate(actors)}
    tl = w.put(0x60); w.b[tl:tl + 4] = b"TLIN"
    struct.pack_into("<fHHHH", w.b, tl + 0x10, float(frames), len(actors), sum(len(a[1]) for a in actors), len(clips), len(oneshots))
    w.s(tl + 0x20, name); w.q(tl + 0x28, _actors(w, actors))
    c = w.put(0x18 * len(clips)); w.q(tl + 0x30, c)
    for i, (st, ln, ac, act, prm) in enumerate(clips):
        o = c + 0x18 * i; ai = an[ac]
        struct.pack_into("<ffHH", w.b, o, float(st), float(ln), ai, actors[ai][1].index(act)); w.q(o + 0x10, _params(w, prm))
    s = w.put(0x18 * len(oneshots)); w.q(tl + 0x38, s)
    for i, (st, ac, act, prm) in enumerate(oneshots):
        o = s + 0x18 * i; ai = an[ac]
        struct.pack_into("<fHH", w.b, o, float(st), ai, actors[ai][1].index(act)); w.q(o + 0x10, _params(w, prm))
    return tl


def bfevfl(name, flowcharts=(), timelines=()):
    """flowcharts: [(name, actors, events, entries)]; timelines: [(name, frames, actors, clips, oneshots)]"""
    head = bytearray(0x48); head[0:8] = b"BFEVFL\0\0"; head[8:12] = bytes([0, 3, 0, 0]); struct.pack_into("<H", head, 0x0C, 0xFEFF)
    w = _W(head); w.cstr_patch.append((0x10, name))
    struct.pack_into("<HH", w.b, 0x20, len(flowcharts), len(timelines))
    fcs = [_flowchart(w, *f) for f in flowcharts]
    tls = [_timeline(w, *t) for t in timelines]
    for off, lst, names in ((0x28, fcs, [f[0] for f in flowcharts]), (0x38, tls, [t[0] for t in timelines])):
        if lst:
            arr = w.put(8 * len(lst)); w.q(off, arr)
            for i, o in enumerate(lst): w.q(arr + 8 * i, o)
            w.q(off + 8, _dic(w, names))
    w.finish()
    struct.pack_into("<I", w.b, 0x1C, len(w.b))
    return bytes(w.b)


# ---------------------------------------------------------------- the conversation
def islander():
    A = [("Islander", ["LookAt", "Talk", "PlayAnim"], []),
         ("Player", ["Wait"], ["CheckCoins"]),
         ("EventSystemActor", ["SubtractCoins", "AddItem", "SetFlag"], ["SelectChoice"]),
         ("Camera", ["MoveTo"], []), ("Sound", ["PlaySE"], [])]
    def talk(n, lab, nx): return {"name": n, "type": ACTION, "actor": "Islander", "item": "Talk", "next": nx, "params": {"MessageLabel": lab}}
    E = [
        {"name": "Event0", "type": ACTION, "actor": "Islander", "item": "LookAt", "next": 1, "params": {"Target": "Player", "Frames": 20}},  # 0
        {"name": "Event1", "type": FORK, "branches": [2, 3], "join": 4},                                                                   # 1
        {"name": "Event2", "type": ACTION, "actor": "Camera", "item": "MoveTo", "next": 4, "params": {"Preset": "TalkClose", "Frames": 30}},
        {"name": "Event3", "type": ACTION, "actor": "Sound", "item": "PlaySE", "next": 4, "params": {"Name": "SE_Seagull", "Volume": 0.6}},
        {"name": "Event4", "type": JOIN, "next": 5},                                                                                       # 4
        talk("Event5", "Hello1", 6), talk("Event6", "Hello2", 7),                                                                          # 5, 6
        {"name": "Event7", "type": SWITCH, "actor": "EventSystemActor", "item": "SelectChoice", "params": {"MessageLabel": "Ask", "Price": 50},
         "cases": [(0, 8), (1, 14), (2, 16)]},                                                                                             # 7
        {"name": "Event8", "type": SWITCH, "actor": "Player", "item": "CheckCoins", "params": {"Amount": 50}, "cases": [(0, 9), (1, 10)]},  # 8
        talk("Event9", "NoCoins", 16),                                                                                                     # 9
        {"name": "Event10", "type": ACTION, "actor": "EventSystemActor", "item": "SubtractCoins", "next": 11, "params": {"Amount": 50}},
        {"name": "Event11", "type": ACTION, "actor": "EventSystemActor", "item": "AddItem", "next": 12, "params": {"Item": "DemoCrate", "Count": 1}},
        {"name": "Event12", "type": ACTION, "actor": "Islander", "item": "PlayAnim", "next": 13, "params": {"Anim": "Wave", "Loop": False}},
        talk("Event13", "Bought", 16),                                                                                                     # 13
        talk("Event14", "Island1", 15), talk("Event15", "Island2", 7),                                                                     # 14, 15 (back to the question)
        {"name": "Event16", "type": SUBFLOW, "entry": "Farewell", "next": None},                                                          # 16
        talk("Event17", "Bye", 18),                                                                                                        # 17 (Farewell)
        {"name": "Event18", "type": ACTION, "actor": "EventSystemActor", "item": "SetFlag", "next": None, "params": {"Flag": "TalkedToIslander", "Value": True}},
        talk("Event19", "Idle", None),                                                                                                     # 19 (Wait)
    ]
    return bfevfl("DemoIsland_Islander", [("DemoIsland_Islander", A, E, [("Talk", 0), ("Farewell", 17), ("Wait", 19)])])


# ---------------------------------------------------------------- the cutscene
def arrival():
    A = [("Fader", ["FadeIn", "FadeOut"], []), ("Timeline", ["Play"], [])]
    E = [{"name": "Event0", "type": ACTION, "actor": "Fader", "item": "FadeIn", "next": 1, "params": {"Frames": 30, "Color": "Black"}},
         {"name": "Event1", "type": ACTION, "actor": "Timeline", "item": "Play", "next": 2, "params": {"Name": "DemoIsland_Arrival"}},
         {"name": "Event2", "type": SUBFLOW, "flow": "DemoIsland_Islander", "entry": "Wait", "next": 3},
         {"name": "Event3", "type": ACTION, "actor": "Fader", "item": "FadeOut", "next": None, "params": {"Frames": 30, "Color": "Black"}}]
    TA = [("Camera", ["Pan", "Cut"], []), ("Player", ["PlayAnim"], []), ("Music", ["Play"], []), ("Caption", ["Show"], []), ("Sound", ["PlaySE"], [])]
    clips = [(0, 300, "Camera", "Pan", {"From": "HighOverSea", "To": "BigIsland", "Ease": "InOut"}),
             (300, 240, "Camera", "Pan", {"From": "BigIsland", "To": "SmallIsland1", "Ease": "Out"}),
             (60, 180, "Caption", "Show", {"MessageLabel": "Caption1"}),
             (330, 170, "Caption", "Show", {"MessageLabel": "Caption2"}),
             (380, 120, "Player", "PlayAnim", {"Model": "DemoPlayer", "Anim": "Wave"})]
    oneshots = [(0, "Music", "Play", {"Stream": "GoldenTides", "FadeFrames": 60}), (300, "Sound", "PlaySE", {"Name": "SE_Seagull"}),
                (540, "Camera", "Cut", {"To": "PlayerClose"})]
    return bfevfl("DemoIsland_Arrival", [("DemoIsland_Arrival", A, E, [("Event", 0)])],
                  [("DemoIsland_Arrival", 600, TA, clips, oneshots)])


# ---------------------------------------------------------------- the text ([word] = highlight colour, {Name} = a number the game fills in)
ISLANDER = {
    "Idle": ("Lovely breeze today.", "Quelle belle brise aujourd'hui.", "Schöne Brise heute.", "Qué brisa tan agradable hoy."),
    "Hello1": ("Ahoy there, traveller! Welcome to Demo Island.", "Ohé, voyageur ! Bienvenue sur l'île de démo.",
               "Ahoi, Reisender! Willkommen auf der Demo-Insel.", "¡Hola, viajero! Bienvenido a la isla de demo."),
    "Hello2": ("I make the finest [wooden crates] on all five islands.", "Je fabrique les plus belles [caisses en bois] des cinq îles.",
               "Ich baue die besten [Holzkisten] auf allen fünf Inseln.", "Hago las mejores [cajas de madera] de las cinco islas."),
    "Ask_Query": ("What can I do for you?", "Que puis-je faire pour toi ?", "Was kann ich für dich tun?", "¿Qué puedo hacer por ti?"),
    "Ask_Case1": ("Buy a crate ({Price} coins)", "Acheter une caisse ({Price} pièces)", "Eine Kiste kaufen ({Price} Münzen)", "Comprar una caja ({Price} monedas)"),
    "Ask_Case2": ("Tell me about the island", "Parle-moi de l'île", "Erzähl mir von der Insel", "Háblame de la isla"),
    "Ask_Case3": ("Bye!", "Au revoir !", "Tschüss!", "¡Adiós!"),
    "NoCoins": ("Oh dear, you're a little short. Come back with more coins!", "Oh là là, il te manque des pièces. Reviens plus tard !",
                "Oje, das reicht nicht ganz. Komm mit mehr Münzen wieder!", "Vaya, te faltan monedas. ¡Vuelve con más!"),
    "Bought": ("One crate, freshly made! Mind the splinters.", "Une caisse toute neuve ! Attention aux échardes.",
               "Eine Kiste, frisch gebaut! Vorsicht, Splitter.", "¡Una caja recién hecha! Cuidado con las astillas."),
    "Island1": ("The big island has a mountain you can see from anywhere.", "La grande île a une montagne qu'on voit de partout.",
                "Die große Insel hat einen Berg, den man von überall sieht.", "La isla grande tiene una montaña que se ve desde todas partes."),
    "Island2": ("Five little islands sit around it.\nSomeone left a [crate] on one of them...", "Cinq petites îles l'entourent.\nQuelqu'un a laissé une [caisse] sur l'une d'elles...",
                "Fünf kleine Inseln liegen drumherum.\nJemand hat auf einer davon eine [Kiste] vergessen ...", "Cinco islas pequeñas la rodean.\nAlguien dejó una [caja] en una de ellas..."),
    "Bye": ("Fair winds, friend!", "Bon vent, l'ami !", "Gute Fahrt, Freund!", "¡Buen viento, amigo!"),
}
ARRIVAL = {
    "Caption1": ("Somewhere in a made-up sea...", "Quelque part, dans une mer inventée...", "Irgendwo in einem erfundenen Meer ...", "En algún lugar de un mar inventado..."),
    "Caption2": ("...a traveller arrives on [Demo Island].", "...un voyageur arrive sur l'[île de démo].",
                 "... kommt ein Reisender auf der [Demo-Insel] an.", "...un viajero llega a la [isla de demo]."),
}
LANGS = ["USen", "EUfr", "EUde", "EUes"]


def _tag(group, typ, params): return struct.pack("<HHHH", 0x0E, group, typ, len(params)) + params


def _msg(text):
    """text with [highlight] and {Number} marks -> UTF-16 bytes with message tags"""
    out = bytearray(); i = 0
    while i < len(text):
        c = text[i]
        if c == "[": out += _tag(0, 3, struct.pack("<H", 0)); i += 1
        elif c == "]": out += _tag(0, 3, struct.pack("<H", 0xFFFF)); i += 1
        elif c == "{":
            j = text.index("}", i); nm = text[i + 1:j].encode("utf-16-le")
            out += _tag(16, 0, struct.pack("<H", len(nm)) + nm); i = j + 1
        else: out += c.encode("utf-16-le"); i += 1
    return bytes(out)


def msbp():
    """MsgPrjBn with the text colours: CLR1 (RGBA, R in the low byte) and CLB1 (their names)"""
    cols = [("Highlight", (255, 196, 60, 255)), ("Warning", (240, 80, 70, 255))]
    clr = struct.pack("<I", len(cols)) + b"".join(bytes(c) for _, c in cols)
    slots = [[] for _ in range(29)]
    for i, (n, _) in enumerate(cols):
        x = 0
        for ch in n.encode(): x = (x * 0x492 + ch) & 0xFFFFFFFF
        slots[x % 29].append((n, i))
    tbl = bytearray(struct.pack("<I", 29)); lbl = bytearray(); base = 4 + 29 * 8
    for s in slots:
        tbl += struct.pack("<II", len(s), base + len(lbl))
        for n, i in s: lbl += bytes([len(n)]) + n.encode() + struct.pack("<I", i)
    out = bytearray()
    for tag, d in ((b"CLR1", clr), (b"CLB1", bytes(tbl + lbl))):
        out += tag + struct.pack("<I", len(d)) + bytes(8) + d
        while len(out) % 16: out.append(0xAB)
    head = bytearray(b"MsgPrjBn\xFF\xFE\0\0" + bytes([1, 3]) + struct.pack("<HHI", 2, 0, 0x20 + len(out)))
    return bytes(head + bytes(0x20 - len(head)) + out)


def messages(lang):
    """the files this adds to the language's message archive"""
    from demo_ui import msbt
    k = LANGS.index(lang)
    return {"EventFlowMsg/DemoIsland_Islander.msbt": msbt([(lab, _msg(t[k])) for lab, t in ISLANDER.items()]),
            "EventFlowMsg/DemoIsland_Arrival.msbt": msbt([(lab, _msg(t[k])) for lab, t in ARRIVAL.items()]),
            "DemoProject.msbp": msbp()}


def build(romfs):
    import os
    p = os.path.join(romfs, "Event", "EventFlow"); os.makedirs(p, exist_ok=True)
    open(os.path.join(p, "DemoIsland_Islander.bfevfl.zs"), "wb").write(_z(islander()))
    open(os.path.join(p, "DemoIsland_Arrival.bfevfl.zs"), "wb").write(_z(arrival()))
