#!/usr/bin/env python3
import os
import sys

PATCHES = [
    ("include/config.h", "#define STACKSIZE 0x2000", "#define STACKSIZE 0x10000"),
    ("include/segments.h", "#define SEG_START         0x8005C000", "#define SEG_START         0x80200000"),
    ("include/segments.h", "#define RDRAM_END         0x80800000", "#define RDRAM_END         0x80500000"),
    ("src/game/memory.c", "    bzero(startAddr, totalSize);\n    osWritebackDCacheAll();\n    dma_read(startAddr, _engineSegmentRomStart, _engineSegmentRomEnd);\n    osInvalICache(startAddr, totalSize);\n    osInvalDCache(startAddr, totalSize);\n", ""),
    ("include/platform_info.h", "#define IS_BIG_ENDIAN 1", "#define IS_BIG_ENDIAN 0"),
    ("src/audio/heap.c", "    while (gAudioFrameCount < frames) {\n        // spin\n    }", "    while (gAudioFrameCount < frames) {\n        osYieldThread();\n    }"),
    ("src/game/skybox.c", "            const u8 *const texture =\n                (*(SkyboxTexture *) segmented_to_virtual(sSkyboxTextures[background]))[tileIndex];",
     "            const u32 *tileList = (const u32 *) segmented_to_virtual(sSkyboxTextures[background]);\n            const u8 *const texture = (const u8 *) tileList + tileList[tileIndex];"),
    ("src/audio/load.c", "#ifdef TARGET_N64\n    // It seems boot.s doesn't clear the .bss area for audio, so do it here.", "#if 0", 2),
]


def main():
    root = sys.argv[1]
    for patch in PATCHES:
        rel, old, new = patch[:3]
        count = patch[3] if len(patch) > 3 else 1
        p = os.path.join(root, rel)
        s = open(p).read()
        if new in s and old not in s:
            continue
        if s.count(old) != count:
            sys.exit("patch target not found %d time(s) in %s: %r" % (count, rel, old[:40]))
        open(p, "w").write(s.replace(old, new))


main()
