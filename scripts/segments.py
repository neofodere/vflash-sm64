import glob
import os
import re

TEXTURE_BIN_BASE = {
    "title_screen_bg": 0x0A000000,
    "debug_level_select": 0x07000000,
    "effect": 0x0B000000,
}

ACTOR_GROUPS = (
    [("group0", 0x04000000, 0x17000000)]
    + [("group%d" % i, 0x05000000, 0x0C000000) for i in range(1, 12)]
    + [("group%d" % i, 0x06000000, 0x0D000000) for i in range(12, 18)]
    + [("common0", 0x08000000, 0x0F000000), ("common1", 0x03000000, 0x16000000)]
)

ROM_WINDOW_SHIFT = 22
EXTERNAL_FIRST_ID = 236


EXTERNAL_SEGMENTS = {
    "sound_ctl": "gSoundDataADSR",
    "sound_tbl": "gSoundDataRaw",
    "sound_seq": "gMusicData",
    "sound_banksets": "gBankSetsData",
}


class Segment:
    def __init__(self, name, base, compressed, sources, external=False):
        self.external = external
        self.name = name
        self.base = base
        self.compressed = compressed
        self.sources = sources
        self.id = None
        self.blob = None
        self.symbols = {}


def level_folders(decomp):
    return sorted(os.path.basename(os.path.dirname(p)) for p in glob.glob(os.path.join(decomp, "levels/*/header.h")))


def level_texture_bins(decomp):
    text = open(os.path.join(decomp, "levels/level_defines.h")).read()
    bins = {}
    for m in re.finditer(r"DEFINE_LEVEL\(\s*\"[^\"]*\"\s*,\s*\w+\s*,\s*\w+\s*,\s*(\w+)\s*,\s*(\w+)\s*,", text):
        bins[m.group(1)] = m.group(2)
    bins["intro"] = "generic"
    bins["menu"] = "generic"
    return bins


def build_list(decomp, gen, region):
    segs = []

    def src(p):
        return os.path.join(decomp, p)

    segs.append(Segment("segment2_mio0", 0x02000000, True, [src("bin/segment2.c")]))
    for path in sorted(glob.glob(src("bin/*.c"))):
        n = os.path.basename(path)[:-2]
        if n == "segment2":
            continue
        segs.append(Segment(n + "_mio0", TEXTURE_BIN_BASE.get(n, 0x09000000), True, [path]))
    if region == "eu":
        for lang in ("en", "fr", "de"):
            segs.append(Segment("translation_%s_mio0" % lang, 0x19000000, True, [src("bin/eu/translation_%s.c" % lang)]))
    for path in sorted(glob.glob(os.path.join(gen, "bin/*_skybox.c"))):
        n = os.path.basename(path)[:-2]
        segs.append(Segment(n + "_mio0", 0x0A000000, True, [path]))
    for name, base, geo_base in ACTOR_GROUPS:
        segs.append(Segment(name + "_mio0", base, True, [src("actors/%s.c" % name)]))
        segs.append(Segment(name + "_geo", geo_base, False, [src("actors/%s_geo.c" % name)]))
    for f in level_folders(decomp) + ["intro", "menu"]:
        sbase = 0x14000000 if f in ("intro", "menu") else 0x0E000000
        segs.append(Segment(f + "_segment_7", 0x07000000, True, [src("levels/%s/leveldata.c" % f)]))
        segs.append(Segment(f, sbase, False, [src("levels/%s/script.c" % f), src("levels/%s/geo.c" % f)]))
    segs.append(Segment("behavior", 0x13000000, False, [src("data/behavior_data.c")]))
    segs.append(Segment("entry", 0x10000000, False, [src("levels/entry.c")]))
    segs.append(Segment("scripts", 0x15000000, False, [src("levels/scripts.c")]))
    segs.append(Segment("gd_dynlists", 0x04000000, False, sorted(glob.glob(src("src/goddard/dynlists/*.c")))))
    segs.append(Segment("assets", 0, False, [os.path.join(gen, "assets/mario_anim_data.c"), os.path.join(gen, "assets/demo_data.c")]))
    for name in EXTERNAL_SEGMENTS:
        segs.append(Segment(name, 0, False, [], external=True))
    internal = [s for s in segs if not s.external]
    external = [s for s in segs if s.external]
    for i, s in enumerate(internal):
        s.id = i + 1
        if s.name == "assets":
            s.base = s.id << ROM_WINDOW_SHIFT
    for i, s in enumerate(external):
        s.id = EXTERNAL_FIRST_ID + i
    return segs
