#!/usr/bin/env python3
import json
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import regions

MAGIC = bytes([0x7F, 0x56, 0x46, 0x41])
KIND_RAW = 0
KIND_SKYBOX = 1
CAKES = {"cake.png": ("cake", "", 3200), "cake_eu.png": ("cake_eu", "eu_", 4096)}

REGION = "us"
CPP_DEFS = []
TEXT_DIRS = {"us": ["us"], "jp": ["jp"], "eu": ["us", "de", "fr"]}


def run(cmd, **kw):
    subprocess.run(cmd, check=True, **kw)


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)


def marker_bytes(asset_id, size):
    data = bytearray(size)
    data[0:4] = MAGIC
    data[4:8] = asset_id.to_bytes(4, "little")
    return data


def c_bytes(data):
    return ",".join(str(b) for b in data) + ",\n"


class Table:
    def __init__(self):
        self.rows = []

    def add(self, size, mio0, pos, kind):
        self.rows.append((size, mio0, pos, kind))
        return len(self.rows) - 1


def region_location(entry):
    loc = entry[-1].get(REGION)
    if not loc:
        return None
    return (loc[0] if len(loc) > 1 else -1), loc[-1]


def gen_text(decomp, out):
    tools = os.path.join(out, "tools")
    os.makedirs(tools, exist_ok=True)
    textconv = os.path.join(tools, "textconv")
    t = os.path.join(decomp, "tools")
    run(["gcc", "-O2", "-o", textconv, os.path.join(t, "textconv.c"), os.path.join(t, "utf8.c"), os.path.join(t, "hashtable.c")])
    cpp = ["gcc", "-E", "-P", "-x", "c", "-Wno-trigraphs", "-D_LANGUAGE_ASSEMBLY",
           "-I", os.path.join(decomp, "include"), "-I", os.path.join(decomp, "include/libc"),
           "-I", os.path.join(decomp, "src"), "-I", decomp] + CPP_DEFS
    charmap = os.path.join(out, "charmap.txt")
    charmap_dbg = os.path.join(out, "charmap.debug.txt")
    run(cpp + ["-DBUILD_DIR=" + out, os.path.join(decomp, "charmap.txt"), "-o", charmap])
    run(cpp + ["-DCHARMAP_DEBUG", "-DBUILD_DIR=" + out, os.path.join(decomp, "charmap.txt"), "-o", charmap_dbg])
    os.makedirs(os.path.join(out, "include"), exist_ok=True)
    run([textconv, charmap, os.path.join(decomp, "include/text_strings.h.in"), os.path.join(out, "include/text_strings.h")])
    run([textconv, os.path.join(decomp, "charmap_menu.txt"), os.path.join(decomp, "include/text_menu_strings.h.in"),
         os.path.join(out, "include/text_menu_strings.h")])

    def piped(src, cmap, dst, inc):
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        p = subprocess.run(cpp + [src, "-o", "-", "-I", inc], check=True, capture_output=True)
        run([textconv, cmap, "-", dst], input=p.stdout)

    for lang in TEXT_DIRS[REGION]:
        inc = os.path.join(decomp, "text", lang) + "/"
        for name in ("define_courses", "define_text"):
            piped(os.path.join(decomp, "text", name + ".inc.c"), charmap, os.path.join(out, "text", lang, name + ".inc.c"), inc)
    piped(os.path.join(decomp, "text/debug_text.inc.c"), charmap_dbg, os.path.join(out, "text/debug_text.raw.inc.c"),
          os.path.join(decomp, "text/us/"))
    p = subprocess.run(cpp + ["-I", decomp, os.path.join(decomp, "levels/level_headers.h.in")], check=True, capture_output=True, text=True)
    lines = ['#include "%s"' % l for l in p.stdout.splitlines() if l.strip()]
    write(os.path.join(out, "include/level_headers.h"), "\n".join(lines) + "\n")


def gen_textures(decomp, out, table):
    assets = json.load(open(os.path.join(decomp, "assets.json")))
    skip_prefix = ("textures/ipl3_raw/", "textures/crash_screen/")
    for name, entry in assets.items():
        if name.startswith("@") or not name.endswith(".png") or name.startswith(skip_prefix):
            continue
        loc = region_location(entry)
        if not loc:
            continue
        size = entry[-2]
        if name.startswith("textures/skyboxes/"):
            gen_skybox(out, table, name, size, loc)
        elif name.startswith("levels/ending/cake"):
            gen_cake(out, table, name, size, loc)
        else:
            asset_id = table.add(size, loc[0], loc[1], KIND_RAW)
            write(os.path.join(out, name[:-4] + ".inc.c"), c_bytes(marker_bytes(asset_id, size)))


def gen_skybox(out, table, name, size, loc):
    sky = os.path.basename(name)[:-4]
    asset_id = table.add(size, loc[0], loc[1], KIND_SKYBOX)
    blob = marker_bytes(asset_id, size)
    text = '#include "types.h"\n\n#include "make_const_nonconst.h"\n\n'
    text += "ALIGNED8 const unsigned char %s_skybox_tiles[%d] = {\n%s};\n\n" % (sky, len(blob), c_bytes(blob))
    text += '__asm__(".global %s_skybox_ptrlist\\n.set %s_skybox_ptrlist, %s_skybox_tiles");\n' % (sky, sky, sky)
    write(os.path.join(out, "bin", sky + "_skybox.c"), text)


def gen_cake(out, table, name, size, loc):
    stem, infix, tile = CAKES.get(os.path.basename(name), (None, None, None))
    if not stem:
        return
    asset_id = table.add(size, loc[0], loc[1], KIND_RAW)
    text = "ALIGNED8 static const Texture cake_end_texture_%sdata[%d] = {\n%s};\n\n" % (infix, size, c_bytes(marker_bytes(asset_id, size)))
    for i in range(size // tile):
        text += "#define cake_end_texture_%s%d (cake_end_texture_%sdata + %d)\n" % (infix, i, infix, i * tile)
    write(os.path.join(out, "levels/ending", stem + ".inc.c"), text)


def gen_demos(decomp, out, table):
    assets = json.load(open(os.path.join(decomp, "assets.json")))
    for name, entry in assets.items():
        if name.startswith("@") or not name.endswith(".bin"):
            continue
        loc = region_location(entry)
        if not loc:
            continue
        size = entry[-2]
        asset_id = table.add(size, loc[0], loc[1], KIND_RAW)
        path = os.path.join(out, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(marker_bytes(asset_id, size))


def gen_tables(decomp, out):
    shutil.copytree(os.path.join(decomp, "assets/anims"), os.path.join(out, "assets/anims"), dirs_exist_ok=True)
    shutil.copy(os.path.join(decomp, "assets/demo_data.json"), os.path.join(out, "assets/demo_data.json"))
    tools = os.path.join(decomp, "tools")
    inc = ["-I" + os.path.join(decomp, d) for d in ("include", "include/libc", "src", ".")] + CPP_DEFS
    anims = subprocess.run([sys.executable, os.path.join(tools, "mario_anims_converter.py")], check=True, capture_output=True, text=True, cwd=out)
    write(os.path.join(out, "assets/mario_anim_data.c"), anims.stdout)
    demos = subprocess.run([sys.executable, os.path.join(tools, "demo_data_converter.py"), "assets/demo_data.json"] + inc,
                           check=True, capture_output=True, text=True, cwd=out)
    write(os.path.join(out, "assets/demo_data.c"), demos.stdout)


def write_table(out, table):
    lines = ['#include "asset_table.h"', "", "const asset_t asset_table_%s[] = {" % REGION]
    for size, mio0, pos, kind in table.rows:
        lines.append("\t{ %d, %d, %d, %d }," % (size, mio0, pos, kind))
    lines.append("};")
    write(os.path.join(out, "asset_table.c"), "\n".join(lines) + "\n")
    write(os.path.join(out, "asset_count.txt"), "%d\n" % len(table.rows))


def main():
    global REGION, CPP_DEFS
    if len(sys.argv) != 4 or sys.argv[3] not in regions.REGIONS:
        sys.exit("usage: gen_assets.py <decomp-dir> <out-dir> <%s>" % "|".join(regions.REGIONS))
    decomp = os.path.abspath(sys.argv[1])
    out = os.path.abspath(sys.argv[2])
    REGION = sys.argv[3]
    CPP_DEFS = regions.defines(REGION)
    os.makedirs(out, exist_ok=True)
    table = Table()
    gen_text(decomp, out)
    gen_textures(decomp, out, table)
    gen_demos(decomp, out, table)
    gen_tables(decomp, out)
    write_table(out, table)
    print("assets: %d patchable entries" % len(table.rows))


main()
