#!/usr/bin/env python3
import json
import os
import struct
import subprocess
import sys
import tempfile

ROMTAB_OFFSET = 0x100
ROMTAB_ENTRIES = 240
ENTRY_SIZE = 16


def lz4_compress(data, tool):
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "in")
        dst = os.path.join(tmp, "out")
        open(src, "wb").write(data)
        subprocess.check_call([tool, src, dst])
        return open(dst, "rb").read()


def main():
    build, out, tool = sys.argv[1], sys.argv[2], sys.argv[3]
    code = bytearray(open(os.path.join(build, "boot_code.bin"), "rb").read())
    manifest = json.load(open(os.path.join(build, "segments.json")))
    base_size = (len(code) + 15) & ~15
    code += b"\0" * (base_size - len(code))
    entries = {}
    for m in manifest:
        if m["external"]:
            continue
        raw = open(os.path.join(build, m["file"]), "rb").read()
        stored = lz4_compress(raw, tool) if m["compressed"] else raw
        entries[m["id"]] = (len(code), len(stored), len(raw), int(m["compressed"]))
        code += stored
        code += b"\0" * (-len(code) & 15)
    for sid, (off, stored, raw, kind) in entries.items():
        struct.pack_into("<IIII", code, ROMTAB_OFFSET + sid * ENTRY_SIZE, off, stored, raw, kind)
    struct.pack_into("<I", code, 4, len(code))
    open(out, "wb").write(code)
    print("image bytes:", len(code))


main()
