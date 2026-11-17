#!/usr/bin/env python3
import glob
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import regions
import segments as seg

CLANG = os.environ.get("CLANG", "clang")
LLD = os.environ.get("LLD", "ld.lld")
NM = os.environ.get("NM", "llvm-nm")
OBJCOPY = os.environ.get("OBJCOPY", "llvm-objcopy")

ARM = ["--target=armv5te-none-eabi", "-mcpu=arm926ej-s", "-marm", "-mfloat-abi=soft", "-ffreestanding", "-fno-builtin",
       "-O2", "-fno-pic", "-fno-stack-protector", "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
       "-fsigned-char", "-ffunction-sections", "-fdata-sections"]
DEFS = ["-D_FINALROM=1", "-DUSE_EXT_RAM"]
SEGMENT_END_SLACK = 32


def run(cmd, **kw):
    try:
        return subprocess.run(cmd, check=True, **kw)
    except subprocess.CalledProcessError:
        sys.exit("command failed: %s" % " ".join(cmd[:1] + [a for a in cmd if not a.startswith("-")][1:3]))


class Paths:
    def __init__(self, decomp, repo, gen, build):
        self.decomp = decomp
        self.repo = repo
        self.gen = gen
        self.build = build
        self.port = os.path.join(repo, "port")
        self.runtime = os.path.join(repo, "runtime")

    def decomp_includes(self):
        inc = [os.path.join(self.port, "include"), os.path.join(self.decomp, "include"),
               os.path.join(self.decomp, "include/libc"), os.path.join(self.decomp, "src"), self.decomp, self.gen,
               os.path.join(self.gen, "include"), os.path.join(self.gen, "assets"),
               os.path.join(self.runtime, "include"), self.port]
        return ["-I" + i for i in inc]


def obj_path(build, path):
    return os.path.join(build, "obj", re.sub(r"[^A-Za-z0-9_.]", "_", path) + ".o")


def compile_decomp(path, out, paths):
    os.makedirs(os.path.dirname(out), exist_ok=True)
    if os.path.exists(out) and os.path.getmtime(out) > os.path.getmtime(path) and not os.environ.get("VF_CLEAN"):
        return
    run([CLANG] + ARM + ["-nostdinc", "-std=gnu90", "-w"] + DEFS + paths.decomp_includes() + ["-c", path, "-o", out])


def compile_port(path, out, paths):
    os.makedirs(os.path.dirname(out), exist_ok=True)
    extra = ["-DVF_TRACE"] if os.environ.get("VF_TRACE") else []
    run([CLANG] + ARM + ["-nostdinc", "-std=gnu99", "-Wall", "-Wno-unused-parameter", "-Wno-unknown-pragmas"] + extra + DEFS
        + paths.decomp_includes() + ["-I" + os.path.join(paths.decomp, "src/game"), "-c", path, "-o", out])


def compile_runtime(path, out, paths):
    os.makedirs(os.path.dirname(out), exist_ok=True)
    run([CLANG] + ARM + ["-nostdlib", "-Wall", "-Wextra", "-I" + os.path.join(paths.runtime, "include"), "-c", path,
                         "-o", out])


def main_sources(decomp):
    files = []
    for pat in ["src/engine/*.c", "src/game/*.c", "src/audio/*.c", "src/menu/*.c", "src/buffers/*.c",
                "src/goddard/*.c", "lib/src/gu*.c", "lib/src/math/*.c"]:
        files += sorted(glob.glob(os.path.join(decomp, pat)))
    skip = ("src/game/main.c", "src/game/crash_screen.c", "lib/src/math/sinf.c", "lib/src/math/cosf.c")
    return [f for f in files if not f.endswith(".inc.c") and not f.endswith(skip)]


def port_sources(port):
    return sorted(glob.glob(os.path.join(port, "*.c")) + glob.glob(os.path.join(port, "*.S")))


def runtime_sources(runtime):
    return sorted(glob.glob(os.path.join(runtime, "*.c")) + glob.glob(os.path.join(runtime, "*.S"))
                  + glob.glob(os.path.join(runtime, "lib", "*.c")) + glob.glob(os.path.join(runtime, "lib", "*.S")))


def defined_symbols(elf):
    out = subprocess.run([NM, "--defined-only", "--extern-only", elf], check=True, capture_output=True, text=True).stdout
    syms = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)
    return syms


def write_syms(path, symbols, skip=()):
    with open(path, "w") as f:
        for n, v in sorted(symbols.items()):
            if n not in skip:
                f.write("%s = 0x%X;\n" % (n, v))


def segment_rom_size(s, raw):
    if s.compressed:
        return raw + raw // 255 + SEGMENT_END_SLACK
    return raw


def write_romsyms(path, segs, raw_sizes):
    with open(path, "w") as f:
        for s in segs:
            if s.external:
                continue
            start = s.id << seg.ROM_WINDOW_SHIFT
            f.write("_%sSegmentRomStart = 0x%X;\n" % (s.name, start))
            f.write("_%sSegmentRomEnd = 0x%X;\n" % (s.name, start + segment_rom_size(s, raw_sizes.get(s.name, 0))))
        f.write("_engineSegmentRomStart = 0;\n_engineSegmentRomEnd = 0;\n")
        for name in ("rspF3DBootStart", "rspF3DBootEnd", "rspF3DStart", "rspF3DDataStart", "rspAspMainStart",
                     "rspAspMainDataStart", "rspAspMainDataEnd"):
            f.write("%s = 0;\n" % name)
        for s in segs:
            if s.external:
                f.write("%s = 0x%X;\n" % (seg.EXTERNAL_SEGMENTS[s.name], s.id << seg.ROM_WINDOW_SHIFT))


def seg_script(path, base):
    with open(path, "w") as f:
        f.write("SECTIONS {\n\t. = 0x%X;\n\t.seg : { *(.text*) *(.rodata*) *(.data*) *(.sdata*) *(COMMON) *(.bss*) }\n"
                "\t/DISCARD/ : { *(.ARM.*) *(.comment) *(.note*) *(.debug*) }\n}\n" % base)


def link_segment(s, build, stage, extra):
    objs = [obj_path(build, p) for p in s.sources]
    ld = os.path.join(build, "seg", "%s.%s.ld" % (s.name, stage))
    os.makedirs(os.path.dirname(ld), exist_ok=True)
    seg_script(ld, s.base)
    elf = os.path.join(build, "seg", "%s.%s.elf" % (s.name, stage))
    cmd = [LLD, "-nostdlib", "-e", "0", "--no-gc-sections", "-T", ld, "--unresolved-symbols=ignore-all"]
    for e in extra:
        cmd += ["-T", e]
    run(cmd + objs + ["-o", elf])
    return elf


def link_main(objs, build, extra, out):
    cmd = [LLD, "-nostdlib", "-T", os.path.join(os.path.dirname(build.rstrip("/")), "runtime", "link.ld")]
    return cmd


def main():
    if len(sys.argv) != 6 or sys.argv[5] not in regions.REGIONS:
        sys.exit("usage: build_game.py <decomp> <repo> <gen> <build> <%s>" % "|".join(regions.REGIONS))
    decomp, repo, gen, build = [os.path.abspath(a) for a in sys.argv[1:5]]
    DEFS.extend(regions.defines(sys.argv[5]))
    paths = Paths(decomp, repo, gen, build)
    segs = seg.build_list(decomp, gen, sys.argv[5])
    seg_units = sorted({p for s in segs for p in s.sources})
    for p in seg_units:
        compile_decomp(p, obj_path(build, p), paths)

    main_objs = []
    for p in main_sources(decomp):
        compile_decomp(p, obj_path(build, p), paths)
        main_objs.append(obj_path(build, p))
    for p in port_sources(paths.port):
        o = obj_path(build, p)
        if p.endswith(".S"):
            os.makedirs(os.path.dirname(o), exist_ok=True)
            run([CLANG] + ARM + ["-c", p, "-o", o])
        else:
            compile_port(p, o, paths)
        main_objs.append(o)
    for p in runtime_sources(paths.runtime):
        o = obj_path(build, p)
        compile_runtime(p, o, paths)
        main_objs.append(o)

    linked = [s for s in segs if not s.external]
    owner = {}
    for s in linked:
        s.symbols = defined_symbols(link_segment(s, build, "a", []))
        for n in s.symbols:
            owner.setdefault(n, s.name)
    raw_sizes = {}
    for s in linked:
        elf = link_segment(s, build, "a", [])
        tmp = os.path.join(build, "seg", s.name + ".a.bin")
        run([OBJCOPY, "-O", "binary", "--only-section=.seg", elf, tmp])
        raw_sizes[s.name] = os.path.getsize(tmp)
    os.makedirs(os.path.join(build, "link"), exist_ok=True)
    romsyms = os.path.join(build, "link", "romsyms.ld")
    write_romsyms(romsyms, segs, raw_sizes)
    segsyms = os.path.join(build, "link", "segsyms.ld")
    all_seg_syms = {n: v for s in linked for n, v in s.symbols.items() if owner[n] == s.name}
    write_syms(segsyms, all_seg_syms)

    main_elf = os.path.join(build, "link", "boot.elf")
    cmd = [LLD, "-nostdlib", "-T", os.path.join(paths.runtime, "link.ld"), "-T", romsyms, "-T", segsyms,
           "--unresolved-symbols=report-all", "-Map=" + os.path.join(build, "link", "boot.map")]
    run(cmd + main_objs + ["-o", main_elf])
    main_syms = defined_symbols(main_elf)
    mainsyms = os.path.join(build, "link", "mainsyms.ld")
    write_syms(mainsyms, main_syms)

    for s in linked:
        ext = os.path.join(build, "seg", s.name + ".syms.ld")
        others = {}
        for other in linked:
            if other is not s:
                for n, v in other.symbols.items():
                    if n not in s.symbols and owner[n] == other.name:
                        others[n] = v
        write_syms(ext, others)
        elf = link_segment(s, build, "b", [ext, mainsyms, romsyms])
        blob = os.path.join(build, "seg", s.name + ".bin")
        run([OBJCOPY, "-O", "binary", "--only-section=.seg", elf, blob])
        s.blob = blob

    manifest = []
    for s in segs:
        entry = {"name": s.name, "id": s.id, "base": s.base, "compressed": s.compressed, "external": s.external}
        if not s.external:
            entry["size"] = os.path.getsize(s.blob)
            entry["file"] = os.path.relpath(s.blob, build)
        manifest.append(entry)
    json.dump(manifest, open(os.path.join(build, "segments.json"), "w"), indent=1)
    code = os.path.join(build, "boot_code.bin")
    run([OBJCOPY, "-O", "binary", main_elf, code])
    print("segments: %d, raw bytes: %d, code image: %d" % (len(manifest), sum(m.get("size", 0) for m in manifest),
                                                          os.path.getsize(code)))


main()
