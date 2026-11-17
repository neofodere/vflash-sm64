# SM64 port for VTech V.Flash

A port of Super Mario 64 to the VTech V.Flash/V.Smile Pro, built on the
[n64decomp/sm64](https://github.com/n64decomp/sm64) decompilation.

No Nintendo code or assets are distributed here. You supply your own ROM and the
command-line tool builds a V.Flash disc image from it.

## Status

Work in progress. See [docs/STATUS.md](docs/STATUS.md) for details.

**This port has not been tested on real hardware.** It has only been run in the
emulators [FlashEm](https://github.com/WizzardSK/flashem-libretro) by WizzardSK
and [V.Dream](https://github.com/fodsoft/vdream) by FODSOFT.

What works in the emulators: boot, title screen, file select, the intro, and
walking Mario around the castle grounds with the controller, with the US, Japanese
and European releases. Audio is experimental: sound effects and voices play on the US release, but music is
not correct yet. Rendering is approximate (see the status document).

## Usage

You need a Super Mario 64 ROM (`z64`, `v64` or `n64` byte order, 8 MiB) of one of
these releases. The tool identifies it by SHA-1 and picks the matching game image.

| Release | SHA-1 |
| --- | --- |
| US | `9bef1128717f958171a4afac3ed78ee2bb4e86ce` |
| Japan | `8a20a5c83d6ceb0f0506cfc9fa20d8f438cafe51` |
| Europe | `4ac5721683d0e0b6bbb561b58a71740845dceea9` |

The Shindou and iQue releases are not supported.

```
vflash-sm64 baserom.z64 SuperMario64
```

This writes `SuperMario64.cue` and `SuperMario64.bin`. Burn the pair to a CD-R or
load the `.cue` in an emulator together with a V.Flash BIOS dump. `--region us|jp|eu`
forces a release when the checksum is not recognised.

The tool copies the textures, models, level data and animations it needs from your
ROM into the disc image; the executable itself contains only the port's code.

Release archives contain a static executable for Linux x64, Windows x64 and
Windows x86. Creating a git tag that starts with `v` publishes them through the
GitHub Actions workflow in `.github/workflows/release.yml`.

## Controls

| V.Flash | Nintendo 64 |
| --- | --- |
| Joystick | Control stick |
| Green | A |
| Red | B |
| Yellow | Z |
| Enter | Start |
| Blue | C buttons (with the joystick) |

The controller reports the colour buttons both as press events that name the colour
and as bits whose positions differ between controllers and emulators. The game
matches the two so that the colours above apply to either layout; the mapping is
learned the first time each button is pressed. This was only exercised with FlashEm.

## Building

Requirements: `clang`, `lld`, `llvm` (for the ARM image), a C compiler for the host
tool, `make` and `python3`. The decompilation is expected in `third_party/sm64` at
the commit pinned by the workflows; a copy is patched inside the build directory.

```
git clone https://github.com/n64decomp/sm64 third_party/sm64
git -C third_party/sm64 checkout 9921382a68bb0c865e5e45eb594d9c64db59b1af
for r in us jp eu; do make image REGION=$r; done
make cli test
```

`make image REGION=<us|jp|eu>` builds one game image into `build/<region>/BOOT.BIN`;
`make cli` embeds the images listed in `REGIONS` (default `us jp eu`) into one
executable at `build/vflash-sm64`. Use `DECOMP=<path>` to point at another checkout,
`BUILD=<dir>` to change the output directory and `IMAGES=<dir>` to take prebuilt
images from `<dir>/<region>/`.

The console image targets the ZEVIO 1020 SoC (ARM926EJ-S, no FPU, 16 MB RAM). It is
built freestanding with clang and a small runtime in `runtime/lib` (soft-float,
integer division, string and math routines). Setting `VF_TRACE=1` while building the
game adds log output on the console UART.

## Layout

| Path | Contents |
| --- | --- |
| `tool/` | Command-line tool: ROM loading, SHA-1, MIO0 and LZ4, asset patching, ISO 9660 MODE1/2352 writer |
| `port/` | Console port: scheduler, display-list renderer for the graphics engine, input, ROM access |
| `runtime/` | Bare-metal start-up code and hardware drivers for the console |
| `runtime/lib/` | Compiler runtime with host-side tests |
| `scripts/` | Decompilation patching, asset table generation, game build and image packing |

## Licence

The code in this repository is released under the MIT licence. Super Mario 64 isthe property of Nintendo; the decompilation is fetched at build time and is not part of this repository.
