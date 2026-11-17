# Status

Nothing here has run on real hardware. Everything was checked in the FlashEm and V.Dream emulators.

| Area | State |
| --- | --- |
| Disc image writer (ISO 9660, MODE1/2352, EDC/ECC, `.cue`) | Done; the BIOS mounts and boots it |
| Command-line tool (ROM byte order, SHA-1, release detection, MIO0, asset patching, LZ4) | Done; unit-tested and run on real US, Japanese and European ROMs |
| Release workflow (Linux x64, Windows x64, Windows x86) | Written; not yet run on GitHub, Windows builds never compiled |
| ARM runtime library (soft-float, integer division, string, math) | Done; host-tested bit-exact against hardware floats |
| Game code (decompilation, patched copy built with clang for ARM) | One image per release (US, Japan, Europe); boots, runs the scheduler, level scripts and Goddard intro |
| Graphics (F3D display lists to the graphics engine) | Textured, lit, depth-tested triangles, rectangles (including flipped and mirrored), alpha, texture generation |
| Controller | Works through the UART packet protocol; colour buttons are matched between press events and report bits |
| Audio | Experimental: sound data is converted from the ROM by the tool, the RSP audio commands are mixed in software and streamed to the sound core; effects and voices play on the US release, music is wrong (steady noise), Japan and Europe untested |
| Save data | Not implemented (saves are not persisted) |
| Shindou and iQue releases | Not supported |

## Verification

The converter was run on real US, Japanese and European ROMs and the resulting
discs were booted in the emulators.

- US: title screen, file select with text, intro, castle grounds.
- Japan: title screen with the start prompt, file select, castle grounds, HUD and
  Japanese dialogue.
- Europe: title screen, sound and language select, file select, the letter from the
  princess and the castle grounds with the HUD and English dialogue.

## Known limitations

- Per-vertex colour is approximated: unlit geometry gets one tint per triangle plus
  per-vertex intensity.
- Palette (CI) textures, fog and bilinear filtering are not implemented; CI textures
  render white.
- Combiner modes are interpreted through a small set of patterns; unusual ones may
  look wrong.
- Frame rate on the emulators is well below 30 fps in busy scenes. Behaviour on
  hardware is unknown.
- Memory use with the full ROM data has not been measured on hardware.
