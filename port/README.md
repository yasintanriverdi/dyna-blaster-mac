# Dyna Blaster — native Apple Silicon port

A native macOS arm64 re-implementation of *Dyna Blaster* (Hudson Soft / Ubi Soft, DOS 1992), built from the reverse-engineering notes in [`../docs/re`](../docs/re/ARCHITECTURE.md).
It uses C11 and raylib, built with clang.

The port contains **no game data**. At startup it reads your original files:
- `DYNA.EXE`: unpacked from LZEXE in memory. Every table is read from its data segment: tiles, sprite rectangles, fonts, map templates, stage data, enemy types, the RNG seed table and the password tables.
- `GFX/*.IMG`: decoded as IFF ILBM images.
- `DIGI.DAT`: 5 VOC sound effects.
- `MUSIC.DAT`: 15 CMF songs, played through the Nuked-OPL3 emulator.
- `DEMO.DAT`: the attract-mode demo.
- `DYNA.CFG`: key bindings.

## Build & run
```sh
brew install raylib          # once
make                         # -> ./dynablaster (arm64, raylib linked statically)
./dynablaster ../dyna        # path to the folder with DYNA.EXE, GFX/, *.DAT
make test                    # checks the password codec against all 64 codes in DYNA.COD
```

## Controls
Keys come from `DYNA.CFG`, exactly as in the original. Default layout:

| | P1 (keyboard 1) | P2 (keyboard 2) |
|---|---|---|
| move | arrow keys | I J K L |
| bomb | Space | A |
| detonate (remote) | Left Ctrl | – |

- **Gamepads:** gamepad 1 and 2 are the joystick devices for players 3 and 4. Gamepad 1 also controls P1.
- **Other keys:** `P` pause, `M` music on/off, `S` sound effects on/off, `Esc` back to the title, `F11` / `⌘F` borderless fullscreen.
- The original **HUDSONSOFT** cheat is not wired up yet.

## Fidelity notes
- **Timing:** game logic runs at the original **18.2065 Hz** timer-tick rate (INT 1Ch), so speeds, fuses and timers match the DOS version. The screen redraws at 60 FPS, with player and enemy positions interpolated between ticks.
- **Screen:** an indexed 256×232 framebuffer (the original's tweaked Mode X), scaled with square pixels.
- **Exact ports of the original code:**
  - random-number generator (`0x139AC`)
  - block and item placement (`0x13219`)
  - bomb fuse and chain reactions, and explosion spread (`0x1427E`)
  - player movement with corner sliding (`0x1483C`)
  - pickups and skull diseases
  - enemy AI (`0x14D80`: random turns, chase, bomb avoidance)
  - scoring with same-frame kill combos
  - battle round end and timer
  - story stage flow, the time-up penalty, and exit-door enemy spawns
  - password encode/decode
- **Demo:** after 20 s on the title screen, `DEMO.DAT` is replayed with its original recorded inputs.

## Known gaps
- **Boss stages (x.8):** the boss objects are not implemented yet; their sprite pointers into the world sheets are not fully decoded. A boss stage spawns its regular enemies only, and an exit door appears once they are cleared (the same rule as `0x16C40`).
- **Simplified screens:** the title menu, world map, stage intro, game over and ending screens are simplified compared with the original.
- **Music:** the CMF player is a compact reimplementation of SBFMDRV, so voice allocation and volume curves are approximations.
