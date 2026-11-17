#!/usr/bin/env python3
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from regions import REGIONS

HEADER = '''#include "asset_table.h"

#define CAT(a, b) a##b
#define XCAT(a, b) CAT(a, b)
#define STR(x) #x
#define XSTR(x) STR(x)
#define SYM(x) XSTR(XCAT(__USER_LABEL_PREFIX__, x))

'''


def main():
    if len(sys.argv) < 4:
        sys.exit("usage: gen_regions.py <output.c> <image-dir> <region>...")
    out, images, names = sys.argv[1], sys.argv[2], sys.argv[3:]
    text = HEADER
    rows = []
    for n in names:
        path = os.path.join(images, n, "BOOT.BIN")
        size = os.path.getsize(path)
        text += 'extern const asset_t asset_table_%s[];\n' % n
        text += 'extern const unsigned char game_image_%s[];\n' % n
        text += ('__asm__(".data\\n.balign 16\\n.globl " SYM(game_image_%s) "\\n" SYM(game_image_%s) ":\\n'
                 '.incbin \\"%s\\"\\n.previous");\n\n') % (n, n, path.replace("\\", "/"))
        count = int(open(os.path.join(images, n, "gen", "asset_count.txt")).read())
        rows.append((n, REGIONS[n]["sha1"], size, count, REGIONS[n]["sound"]))
    text += "const region_t regions[] = {\n"
    for n, sha, size, count, snd in rows:
        text += '\t{ "%s", "%s", game_image_%s, %d, asset_table_%s, %d, { %d, %d, %d, %d } },\n' % (
            (n, sha, n, size, n, count) + tuple(snd))
    text += "};\n\nconst unsigned region_count = %d;\n" % len(rows)
    open(out, "w").write(text)


main()
