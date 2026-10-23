# Dyna Blaster (DOS) – engine plumbing

Source: `build/DYNA_UNP.EXE`. Addresses are Ghidra linear addresses (CS = 0x1000, so `0x1xxxx` = `1000:xxxx`).
`DS:` offsets refer to `build/rea/dseg.bin`. Segment values in the data tables are **unrelocated**: add 0x1000 to get the Ghidra
segment (e.g. raw `0x3170` = Ghidra `0x4170` = linear `0x41700`). Instruction-level evidence: REA `procedure_assembly`
dumps in `build/rea/engine/asm_*.json` and a capstone linear disassembly in `build/rea/engine/lin.asm`
(this one also covers the ISRs and drivers that Ghidra never made into procedures). Note: REA/Ghidra prints
`MOV Sreg,AX` as `MOV AX, Sreg`; `MOV AX,0x3403 / MOV AX,DS` really means `DS = 0x3403`.

## 0. Memory map (segments)

| name used here | raw seg | linear | content |
|---|---|---|---|
| DS | 0x0965 | 0x19650 | data segment (`dseg.bin`) |
| **S5** (CODE_5u) | 0x1463 | 0x24630 | sheet buffer 1: world tiles (06–13.IMG), 05.IMG (battle), 17/18/19.IMG; image at offset 0 |
| **S6** (CODE_6u) | 0x2403 | 0x34030 | raw file-read buffer (≤0xD6D8 bytes), later reused for sprite *save-under* buffers |
| **S7** (CODE_7u) | 0x3170 | 0x41700 | sheet buffer 2: 04.IMG (story enemies), 15.IMG (battle UI), 14/03/16.IMG; image at offset 8 |
| **S8** (CODE_8u) | 0x4110 | 0x51100 | sheet buffer 3: 01.IMG (story HUD/player), 02.IMG (battle bombers); image at offset 8 |
| S9 (CODE_9u) | 0x50b0 | 0x60b00 | MUSIC.DAT (loaded at offset 8, 0xAF13 bytes) |
| DATA | 0x5b9b | 0x6b9b0 | DIGI.DAT (loaded at offset 8, 0xFE49 bytes) |
| CODE_1 | 0x077e | 0x177e0 | linked Creative **CT-VOICE.DRV** (signature `CT-VOICE`, ©1989-90) |
| CODE_2 | 0x081a | 0x181a0 | linked Creative **SBFMDRV** (signature `FMDRV`) |

All sheets are 320-pixel-wide linear images; sprites are addressed as `seg:offset` of their top-left pixel
(`offset = y*320 + x (+8 for S7/S8)`).

## 1. File loading, ILBM decoding, palette

### 1.1 Loader `0x158F8` (AX = file index)
1. `SI = word[DS:0x56BC + 2*AX]` → 20-byte file record (below).
2. `0x15A11`: DOS open (3D00h) `DS:SI+8`, read `CX=0xD6D8` bytes to **S6:0000**, close. Error → `byte[DS:0x21]=0x65`.
3. `0x1051C`: ILBM decode from S6 into `ES:DI = rec.seg:rec.off`, `CX = rec.rows` rows.
   It scans for the literal `BODY` tag (so the 'XXXX'-overwritten FORM/BMHD tags of files 03–19 are irrelevant),
   then per row: ByteRun1-unpack 320 bytes (8 planes × 40 bytes, interleaved) to DS:0x16C2 and convert the 8 bitplanes
   to chunky (plane 0 = LSB). Output is a linear 320-byte-pitch 8-bpp image. Width is hard-coded 320, header ignored.
4. If `rec.list != 0xFFFF`: `0x1596B` walks the *planarisation list*; else `0x15940` converts the whole sheet as a
   20×10 grid of 16×18 cells (used for 04.IMG = enemies).
   List entry = 4 words `off, seg, w4, rows` (terminator 0xFFFF). `0x159C1` converts each row of the rect **in place**
   from chunky to Mode-X planar order: `w4` bytes of plane 0 (pixels 0,4,8,…), then `w4` bytes plane 1, plane 2, plane 3.
   `rows == 0xFFFF` means "20 consecutive 16×18 cells starting here" (template at DS:0x56B0: `w4=4, rows=0x12`).
   So **the planarisation lists are the authoritative list of every sprite rectangle in every sheet** (Appendix A).

### 1.2 File table DS:0x56BC (20-byte records: `list, rows, seg, off, "gfx\NN.img\0"`)

| idx | file | dest | rows | list | loaded by (context) |
|---|---|---|---|---|---|
| 0 | 05.img | S5:0000 | 101 | 0x6982 | `0x100A3` battle (palette source) |
| 1 | 15.img | S7:0008 | 200 | 0x624E | `0x100A3` battle (HUD bar, digits, font, battle UI) |
| 2 | 02.img | S8:0008 | 200 | 0x6650 | `0x100A3` battle (4 bombers) |
| 3 | 06.img | S5:0000 | 200 | 0x5FB4 | story world 1 (via world table) |
| 4 | 01.img | S8:0008 | 200 | 0x5D62 | `0x1531A` story (HUD, white bomber, fonts) |
| 5 | 04.img | S7:0008 | 200 | 0xFFFF → 16×18 cells | `0x1531A` story enemies |
| 6 | 14.img | S7:0008 | 200 | 0x586A | `0x1531A` world 8 stage 8 (final boss) instead of 04 |
| 7 | – | – | – | – | (null pointer) |
| 8 | 19.img | S5:0000 | 200 | 0x5D10 | `0x12447` title / main menu (also `0x17002`) |
| 9 | 18.img | S5:0000 | 200 | 0x5CA6 | `0x12285` world map / "ROUND n" intro |
| 10 | 07.img | S5:0000 | 200 | 0x5FB4 | world 2 |
| 11 | 08.img | S5:0000 | 200 | 0x5FB4 | world 3 |
| 12 | 09.img | S5:0000 | 200 | 0x59AC | world 4 |
| 13 | 10.img | S5:0000 | 200 | 0x5FB4 | world 5 |
| 14 | 11.img | S5:0000 | 200 | 0x59AC | world 6 |
| 15 | 12.img | S5:0000 | 200 | 0x59AC | world 7 |
| 16 | 13.img | S5:0000 | 200 | 0x59AC | world 8 |
| 17 | 03.img | S7:0008 | 200 | 0x5860 (256×168) | `0x17002` ending/credits background |
| 18 | 16.img | S7:0008 | 200 | 0x5856 (256×200) | `0x16EBE` ending (vertical scroller) |
| 19 | 17.img | S5:0000 | 200 | 0x747E | `0x16EBE`/`0x17002` ending sprites |

World → tileset: table DS:0x4FBC[world] → descriptor whose first word is the file index
(w1→3, w2→10, w3→11, w4→12, w5→13, w6→14, w7→15, w8→16), followed by 8 stage-descriptor pointers
(stage *s* at `desc + 2 + 2*s`, copied to DS:0x4F78). Stage descriptor word 0 = **scroll mode** (§3), e.g.
world 1: stages 1–4 & 8 → 0, 5–7 → 2.
Story sequence `0x1531A`: load 5 (04 → S7), 4 (01 → S8), world tileset (→ S5), then palette; on W8-S8 load 6 (14 → S7).
Battle `0x100A3`: load 0 (+palette), 1, 2.

### 1.3 Palette
* `0x10503`: copies the CMAP of the file *currently in S6* from fixed offset **S6:0x30** (assumes standard
  FORM/ILBM/BMHD layout), 768 bytes, each `>>2` → 6-bit, to `ES:DI`.
  - story: after loading the world tileset → **palette = world sheet (06–13.IMG) CMAP**, stored at DS:0x08DE;
  - battle: after loading 05.IMG → palette = 05.IMG CMAP (DS:0x08DE);
  - ending `0x16EBE`: CMAP of 16.IMG → DS:0x34BC.
  The CMAPs of 01/02/04/15 are therefore *ignored* in-game (they share the world palette layout).
* DS:0x08DE = target palette, DS:0x0BDE = working (displayed) palette. `0x104E5` uploads 256 entries from `DS:BX`
  (port 0x3C8/0x3C9). `0x1046A` fade-in: each vsync increment every working byte < target, upload, until equal.
  `0x1049A` fade-out: decrement to 0. (One step per frame → max 63 frames.)

## 2. Blitting

### 2.1 Video mode – tweaked Mode X, 256×232, chained-off (`0x11235`)
INT 10h mode 12h, then CRTC regs 0–0x17 from DS:0x2075 and attribute regs from DS:0x208E, then:
SEQ 4 &= ~8 (chain-4 off, planar), CRTC 0x14 &= ~0x40, CRTC 0x17 |= 0x40 (byte mode), GC 5 = 0x40 (256-colour),
CRTC 1 = 0x3F (**256 px** visible width), 3 = 0x82, 4 = 0x4C, 6 = 0x0B, 7 = 0x3E, 9 = 0x41 (double scan),
0x10 = 0xE2, 0x11 = 0x0C, 0x12 = 0xCF (464/2 = **232 visible lines**), 0x13 = 0x40 (**128 bytes/line = 512 px virtual**), 0x15 = 0xE7.
Mode 2 scrolling sets CRTC 0x13 = 0x20 (64 bytes/line = 256 px virtual), e.g. `0x13065`, `0x16EDD`.
This is the PC-Engine 256-pixel-wide screen. VRAM pages: DS:0x18 / DS:0x1A = **0x0000 / 0x7A00** (244 lines × 128);
HUD master copy at VRAM **0xFA00** (256×24 planar, 64 bytes/line). `0x1099B` clears all VRAM, `0x109E0` = text mode 3.

### 2.2 Page flipping / frame
* `0x10448` swaps DS:0x18↔0x1A (draw page/show page), swaps the restore-list pointers DS:0xEDE↔0xEE0, `NOT byte[0x22]`.
* `0x1118B` (AX = page base) writes CRTC 0x0C/0x0D start address: mode 0 → AX; mode 1 → AX + (camX−0x80)/4;
  mode 2 → AX + (camY−0x80)*64. `0x11204` (mode 1 only) sets attribute reg 0x13 pel-panning = (camX & 3) × (1 if
  `byte[DS:0x24]==1` else 2) – see "test vga", §3.
* Vsync: `0x11623` waits until port 0x3DA bit 3 set (retrace start); `0x11633` waits until bit 3 clear.
  Typical frame tail: `0x11633`, `0x10448`, `0x1118B`, `0x11623`, `0x11204`, …, `0x1163C` (wait game tick, §4).

### 2.3 Blit primitives (all write A000:DI, map mask via 3C4/3C5 index 2)
| addr | pitch | op |
|---|---|---|
| `0x105C9` / `0x1060C` | 128 / 64 | opaque, source = planar rect inside a 320-pitch sheet (`DS:SI`, DX=w4, CX=h). DI must be 4-px aligned. |
| `0x1064E` / `0x106DE` | 128 / 64 | **transparent** (skip colour **3**), AX = x&3: start plane mask `0x11 ROL (x&3)`, DI++ when mask wraps → arbitrary x. |
| `0x10B0C` / `0x10A82` | 128 / 64 | same shape but writes a solid colour (BL) for every non-3 pixel (hit-flash silhouette). |
| `0x1076D` / `0x107AA` | 128 / 64 | restore save-under buffer (planar, contiguous) to screen. |
| `0x107E6` | 128/64 | save screen rect to buffer (read map select GC 4). |
| `0x10A52` | – | VRAM→VRAM latch copy (GC mode 0x41). |
| `0x10DE3` / `0x10DC4` | 128/64 | solid fill rectangle (one / both pages). |

**Transparency colour index = 3** (hard-coded `CMP AL,3`).

### 2.4 Descriptor tables
* **Opaque tile/picture table DS:0x0EE2** – 202 × 8 bytes (`off, seg, w/4, h`). `0x1086A` (AX=index, DI=dest)
  draws one; `0x10DB1` draws to both pages; `0x10E10` draws a list of (DI, idx) pairs. Used for map tiles
  (idx 0–56 = S5 rows y=0,16,32 16×16: floor/walls/bricks, bombs, items, flame pieces, brick-break frames),
  HUD bars, digits, titles. Full table: Appendix B.
* **Masked sprite table DS:0x180C** – 131 × 16 bytes (`off, seg, w/4, h, save0 far, save1 far`).
  `0x1089B` (AX=index, DX=coords via `0x1397C`, adds page base DS:0x18) → records save-under (`0x10904`, list of 20
  entries × 10 bytes at DS:0x1532 / DS:0x15FA, one per page), saves background (w4+1 bytes/plane), draws transparent.
  `0x109E6` is the silhouette variant. `0x1094E` restores the current page's list in reverse order and clears it.
  idx 0–79: S8 bomber frames (20 or 24 px × 22, 24-px grid; 01.IMG row 0 = white bomber in story, 02.IMG = 4 coloured
  bombers in battle); idx 80–130: S5 17.IMG ending sprites. Appendix C.
* Enemies (story) use their own object records (DS:0x72A4, 12 × 0x26 bytes) that embed the same 16-byte descriptor
  format (e.g. `w4=4, h=0x12` = **16×18**, save buffers S6:3E80/S6:3FE8 …) and frame tables of far pointers into S7
  (e.g. DS:0x70F2…0x72A4, `seg 0x3170`, terminated by 0xFFFF). 04.IMG is entirely converted as 16×18 cells; any
  16-px-aligned x and any y can be used as frame origin.

### 2.5 Fonts
* 8×7 font: char map DS:0x383A `" abcdefghijklmnopqrstuvwxyz!.<EN>0123456789"` → offset table DS:0x3865, glyph at
  `S7:(off + 0xC808)` = 15.IMG rows y=160/168 (8×7 cells, `0x13501`, drawn on both pages with `0x105C9`, w4=2, h=7).
* Big digits 8×13: tile idx 173–182 (01.IMG x=200..272, y=95). "STAGE"/"GAME START" etc. are pictures in 01.IMG.

### 2.6 HUD (top 24 lines)
* Bar picture: story = 01.IMG (0,148) 256×24, drawn directly from S8:B908 by `0x10B97` (both pages) /
  `0x10D95` (to the VRAM 0xFA00 master copy); battle = 15.IMG (0,176) 256×24 = tile idx 17 (`0x100A3`). Bar shows `SC`, clock icon, `:`, bomber head (lives), `HI`.
* Digits 8×7: `0x1386D` draws `idx = (d & 0xF) + 0x12` (battle: tiles 18–27 = 15.IMG x=256..312,y=176 and 256/264,183)
  or `+0x90` in story (tiles 162–171 = 01.IMG x=256..312,y=148 and 256/264,155).
* Positions (byte offsets in the page, mode 0 pitch 128, 1 byte = 4 px; row 9 of the bar):

| item | source | mode 0 | mode 2 (pitch 64) | mode 1 (HUD copy at 0xFA00, pitch 64) | x px |
|---|---|---|---|---|---|
| score (5 digits + "00") | DS:0x3A50 | +0x488 | +0x248 | 0xFC48 | 32 |
| time m : ss | DS:0x37CE / 0x37D0 | +0x49A | +0x25A | 0xFC5A | 104 (m), 120,128 (ss) |
| lives | DS:0x4F8C | +0x4A6 | +0x266 | 0xFC66 | 152 |
| hi-score | DS:0x4F90 | +0x4B0 | +0x270 | 0xFC70 | 192 |

  (`0x1334E`/`0x13394`/`0x133DA`/`0x13430`, called via `0x1332F` for both pages; `0x1583C` redraws only on change.)
* Mode 1: `0x10C04` copies the HUD from VRAM 0xFA00 into the back page at the current camera X each time the camera
  moves (DS:0x1802 = last camX). Mode 2: `0x10CAE` latch-copies the 24-line strip into the window top and saves what
  was there (save slots DS:0x1804/0x1808 → VRAM 0x7A00/0xF400); `0x10D2B` restores it.

## 3. Scrolling (`word[DS:0x26F2]` = per-stage scroll mode, from stage descriptor word 0, `0x13044`)
| mode | map | camera | hardware |
|---|---|---|---|
| 0 | single screen (battle, 15/17-wide story maps) | – | start address only (page flip) |
| 1 | horizontal (31-wide maps, DS:0x03B6 template) | `camX = player.x` (DS:0x3C5E+0) clamped 0x80..0x180 (`0x1111F`) | start = page + (camX−0x80)/4 bytes, **pel panning** = camX&3 (×1 or ×2) |
| 2 | vertical (15-wide tall maps, DS:0x056E template) | `camY = player.y` (+2) clamped 0x80..0x180 | CRTC offset 0x20 (256-px lines), start = page + (camY−0x80)*64 |

Camera word = DS:0x203E (0x80 = left/top). With a 512-px virtual line the whole 31×16 = 496-px map is resident
in each page; scrolling is purely start-address + pel-pan (no redraw).
**"test vga"** (setup menu, `0x12447`): left/right toggles `byte[DS:0x24]` between 0 and 1 while scrolling a
test picture; it selects the pel-panning register unit (0 → value×2 = standard VGA 256-colour, 1 → value×1 for
cards that pan 1 px per unit). Saved as byte 0x1A of DYNA.CFG.

## 4. Timing
* **The game never reprograms PIT channel 0 itself.** Only the linked drivers do: SBFMDRV (`0x18ACD`, divisor per
  song = `(CMF.ticks_per_sec << 8) >> 1` = 96×128 = 12288 → **97.1 Hz**, set by `0x16D40` via FMDRV fn 4) and
  CT-VOICE's OPL-delay calibration (`0x174E3`: temporary divisor 0x1B58, restored to 0xFFFF). FMDRV's INT 8
  (`0x191F2`) accumulates its divisor and chains the original INT 8 whenever ≥ 65536 → BIOS rate is preserved.
* Hooks (`0x1155C`, restored by `0x115DE`): INT 1Ch → `1000:1355`, INT 09h → `1000:1410`, INT 1Bh → `1000:15DD`
  (old vectors saved at DS:0x2040/0x2044/0x2048 and self-modified into far jumps at CS:0x13F0/0x14DE).
* **INT 1Ch handler `0x11355` (18.2065 Hz)**: sets tick flag DS:0x204F, `inc word[0x1C]` (free-running tick);
  unless paused (`byte[0x23]`), every **18 ticks** (≈0.99 s) decrements the stage clock (DS:0x37D0 seconds,
  DS:0x37CE minutes; 0:00 → sets `[0x23]=0xFF`, `[0x204C]=0xFFFF` = time up); steps the PC-speaker sequence (§5.3).
* **Game tick**: every main loop ends with `0x1163C` (wait until DS:0x204F set, then clear) → logic runs at
  **18.2 Hz** (55 ms) with page flips synced to vertical retrace. Battle clock initial value from table DS:0x2A
  indexed by player count: 2 → 3:00, 3 → 3:30, 4 → 4:00; story clock = 4:00 (`0x1531A`).

## 5. Sound

### 5.1 Initialisation `0x16CEA`
`0x17400` probes SB base 0x220..0x260 step 0x10, then 0x210 (`0x1742A`: port echo test base+6/base+0xA with
0xC6/0x39, DSP reset `0x1766A` expecting 0xAA, DSP cmd 0xE0 invert test, OPL timer test via base+8, delay
calibration `0x174E3`); `0x172F4` finds the IRQ by hooking vectors 0x0A/0x0B/0x0D/0x0F (IRQ 2/3/5/7) and issuing
DSP cmd 0xF2; `0x1769A` initialises FMDRV (fns −3, −1, fn 1 = status byte at 0964:0010 = DS:0000); `0x17715` FMDRV reset (fn 8);
`0x175A9` CT-VOICE fn 1 (base port, 0964:0012 = DS:0002), fn 2 (IRQ, DS:0004), fn 3 (init), fn 5 (status word ptr).
(The glue code addresses its variables through segment 0x0964 = DS − 1 paragraph.) Flags: `byte[DS:0x7052]` = FM music
available, `byte[DS:0x7053]` = digitised SFX available. Then `0x16E5A` loads `music.dat` (DS:0x7054) to S9:0008 and
`0x16E8C` loads `digi.dat` (DS:0x705E) to DATA:0008. Driver glue: `0x175E1…0x1765A` → `call far 077E:0000` (CT-VOICE,
BX=fn), `0x176D9…0x177D5` → `call far 081A:0000` (FMDRV, BX=fn).

### 5.2 Music – MUSIC.DAT = 15 concatenated CMF files
`0x16D40` (AX = song): stop (fn 7), `ptr = far[DS:0x70A2 + 4*AX]`, set clock (fn 4, see §4), set instruments
(fn 2, `ptr+CMF[6]`, count `CMF[0x24]`), play (fn 6, `ptr+CMF[8]`); if music muted (`byte[0x4F98]`) pause (fn 9).
`0x16CBB` restarts the song when FMDRV's status byte says it ended (loop). `0x16CA5`/`0x16C9A` pause/resume (fn 9/10),
`0x16CB0` stop. Keys: scancode 0x32 ('M') toggles music, 0x1F ('S') toggles SFX (INT 9 handler).
All CMFs: version 1.01, 48 ticks/quarter, 96 ticks/s, tempo 120.

| song | file offset | #instr | channels | used by |
|---|---|---|---|---|
| 0 | 0x0000 | 10 | 0-3,11 | `0x100A3` battle in-game |
| 1 | 0x16DD | 10 | 0-5 | `0x11C11` |
| 2 | 0x1966 | 10 | 0-3,11 | `0x11C11` |
| 3 | 0x1BFD | 8 | 0-2 | `0x11B3C`, `0x123A9` |
| 4 | 0x1D9E | 11 | 0-4 | `0x119CE` |
| 5 | 0x2523 | 10 | 0-3 | `0x151BB` |
| 6 | 0x286F | 9 | 0-3,11,12 | `0x12182` |
| 7 | 0x2D2D | 11 | 0-4,11,12,14,15 | `0x12447` title / main menu |
| 8 | 0x3A7B | 9 | 0-3 | `0x12285` world map / round intro |
| 9 | 0x3DE7 | 10 | 0-5,11,12,15 | `0x17002` credits |
| 10 | 0x45BE | 8 | 0-2,11 | `0x14107`, `0x16C40` |
| 11 | 0x4905 | 9 | 0-2,11,12 | story stage 8 (boss) `0x1531A` |
| 12 | 0x5C88 | 14 | 0-3,11,12 | story odd stages `0x1531A` |
| 13 | 0x7430 | 15 | 0-4,11,12 | story even stages; battle setup `0x100A3` |
| 14 | 0x96B0 | 12 | 0-5 | `0x16EBE` ending |

(Song table DS:0x70A2 entries are `off, seg=0x50B0`; file offset = off − 8.)

### 5.3 Sound effects
`0x16DD1` (AX = effect): return if SFX muted (`byte[0x4F99]`); if SB: CT-VOICE stop (fn 8), `ES:DI = far[DS:0x70DE + 4*AX]`
(**seg first, then offset**), skip VOC header via `word[ES:DI+0x14]`, CT-VOICE output (fn 6).
**DIGI.DAT = 5 concatenated complete VOC files** (each "Creative Voice File" v1.10, one type-1 block, 20000 Hz
(TC 0xCE), 8-bit unsigned mono, then terminator):

| fx | file offset | VOC data block | samples | dur | callers |
|---|---|---|---|---|---|
| 0 | 0x0000 | 0x1A, 15989 | 15989 | 0.80 s | `0x15241`, `0x15295`, `0x1680F` |
| 1 | 0x3E96 | 0x3EB0, 10535 | 10535 | 0.53 s | INT 9 cheat-code complete, `0x13EFE` |
| 2 | 0x67DE | 0x67F8, 11819 | 11819 | 0.59 s | `0x16A6B` |
| 3 | 0x962A | 0x9644, 10474 | 10474 | 0.52 s | `0x103B2` (battle round-end check), `0x11C11` |
| 4 | 0xBF35 | 0xBF4F, 16015 | 16015 | 0.80 s | `0x12447` (menu), `0x144BF` (bomb explosion logic) |

(sample count = block length − 2.) Slice with: `voc[i] = DIGI[off_i : off_{i+1}]`.

**PC-speaker fallback** (no SB): only fx 0, 1 and 4 have sounds. `0x16E16/0x16E1D/0x16E24` program PIT ch2
(mode 3, 0xB6 → port 0x43/0x42) with the first divisor and enable the speaker (port 0x61 |= 3); the INT 1Ch handler
then loads the next divisor every tick (18.2 Hz) from `word[DS:0x704C]` until a 0 word, then speaker off.

| fx | list | divisors (Hz = 1193182/div) |
|---|---|---|
| 0 | DS:0x7068 | 18000,20000,22000,20000,18000,16000,18000,20000,22000,20000 (66–75 Hz warble) |
| 1 | DS:0x707E | 10000,8000,6000,4000,6000,8000,10000 (119→298→119 Hz) |
| 4 | DS:0x708E | 14000,16000,18000,20000,21000,21500,22000,22500,23000 (85→52 Hz sweep down) |

## 6. Keyboard (INT 9 handler `0x11410`) and DYNA.CFG

* Reads port 0x60; stores every make code in `byte[DS:0x20]` (last key; 0x01 = Esc, 0x19 = 'P' pause → `0x156EC`).
* Cheat sequence: pointer DS:0x203C walks a 0xFF-terminated scancode string at DS:0x2052
  (`23 16 20 1F 18 31 1F 18 21 14` = **H U D S O N S O F T**); a wrong key resets it. On completion sets
  `byte[0x2050]=0xFF`, bombs = 8, flame = 8 (DS:0x3C6A/0x3C70), speed DS:0x3A54 = 0x3C0 and plays fx 1.
  While `[0x2050]` set, keys 2–9 select stage, F1–F8 (0x3B–0x42) select world (`0x1531A`).
* Player key state bytes: DS:0x37F7 (keyset 1, table DS:0x205D) and DS:0x37F8 (keyset 2, DS:0x2069), built by
  `0x11501`: table = pairs (break, make); make sets bit, break clears bit:
  bit0 = up, bit1 = down, bit2 = left, bit3 = right, bit4 = fire. Keyset-1 6th make code (DS:0x2068) sets
  `byte[0x204E]=0xFF` (remote-control detonate key).
* `0x13A31` loads / `0x13AA0` saves **DYNA.CFG (28 bytes)**:

| bytes | → DS | meaning | default file |
|---|---|---|---|
| 0x00–0x0B | 0x205D | keyset 1: (break,make) × up, down, left, right, fire, remote | `C8 48 D0 50 CB 4B CD 4D B9 39 9D 1D` = cursor keys, Space, Ctrl |
| 0x0C–0x15 | 0x2069 | keyset 2: up, down, left, right, fire | `97 17 A5 25 A4 24 A6 26 9E 1E` = I, K, J, L, A |
| 0x16–0x19 | 0x37FC | input device for players 1–4 (0 keyboard 1, 1 keyboard 2, 2 joystick 1, 3 joystick 2, 4 analog 1, 5 analog 2) | `00 01 02 03` |
| 0x1A | 0x24 | "test vga" pel-pan mode (0 = ×2, 1 = ×1) | `00` |
| 0x1B | – | unused | `00` |

* Key-name table for "redefine keys": DS:0x32C4… (8-byte entries `scancode, name[7]`).

## 7. Misc engine notes
* Main dispatcher `entry 0x10000`: init calls `0x135F6`, `0x132E6`, `0x1155C` (ISRs), `0x16CEA` (sound detect),
  `0x11235` (video), palette upload, `0x16E5A`/`0x16E8C` (load music/digi); then title loop `0x12447`; `[0x4F88]` = world (0 = battle `0x100A3`, −1 = quit, else story `0x1531A`).
* `0x156EC` = pause (stops music, waits for 'P' or Esc; in scrolling modes recomputes camera).
* DS:0x0726: current 0x1B8-byte map buffer; DS:0x0046 / 0x01FE / 0x03B6 / 0x056E are map templates.

---

## Appendices (generated by `build/rea/engine/gen_tables.py` from `dseg.bin`)
Sheet coordinates are in the 320×200 PNGs in `build/assets/NN.png`. S5/S7/S8 meaning depends on the screen
(see §1.2): story S5 = world sheet, S7 = 04.IMG, S8 = 01.IMG; battle S5 = 05.IMG, S7 = 15.IMG, S8 = 02.IMG;
title S5 = 19.IMG; round intro S5 = 18.IMG; ending S5 = 17.IMG, S7 = 16.IMG/03.IMG.

### A. Per-file planarisation lists (= every sprite/tile rect in each sheet)

Format `x,y wxh` in sheet pixels (w already rounded to a multiple of 4). `cells16x18` = whole sheet converted as 20x10 cells of 16x18 (`0x15940`).

* **#0 05.img** -> S5, list DS:0x6982: `0,0 16x16; 16,0 16x16; 32,0 16x16; 48,0 16x16; 64,0 16x16; 80,0 16x16; 96,0 16x16; 112,0 16x16; 128,0 16x16; 144,0 16x16; 160,0 16x16; 176,0 16x16; 192,0 16x16; 208,0 16x16; 224,0 16x16; 240,0 8x16; 248,0 8x16; 0,65 64x37; 64,65 64x37; 128,65 64x37; 192,65 64x37; 0,16 16x16; 16,16 16x16; 32,16 16x16; 48,16 16x16; 64,16 16x16; 80,16 16x16; 96,16 16x16; 112,16 16x16; 128,16 16x16; 144,16 16x16; 160,16 16x16; 176,16 16x16; 192,16 16x16; 208,16 16x16; 224,16 16x16; 240,16 16x16; 256,16 16x16; 272,16 16x16; 288,16 16x16; 304,16 16x16; 0,32 16x16; 16,32 16x16; 32,32 16x16; 48,32 16x16; 64,32 16x16; 80,32 16x16; 96,32 16x16; 112,32 16x16; 128,32 16x16; 144,32 16x16; 160,32 16x16; 176,32 16x16; 192,32 16x16; 208,32 16x16; 224,32 16x16; 240,32 16x16; 256,32 16x16; 272,32 16x16; 288,32 16x16; 304,32 16x16`
* **#1 15.img** -> S7, list DS:0x624e: `0,176 256x24; 256,176 8x7; 264,176 8x7; 272,176 8x7; 280,176 8x7; 288,176 8x7; 296,176 8x7; 304,176 8x7; 312,176 8x7; 256,183 8x7; 264,183 8x7; 120,64 16x21; 48,152 8x8; 56,152 8x8; 64,152 8x8; 72,152 8x8; 80,152 8x8; 88,152 8x8; 96,152 8x8; 104,152 8x8; 0,64 24x23; 24,64 24x23; 48,64 24x23; 72,64 24x23; 0,152 8x8; 8,152 8x8; 16,152 8x8; 24,152 8x8; 40,152 8x8; 192,88 8x15; 200,88 8x15; 208,88 8x15; 216,88 8x15; 224,88 8x15; 144,104 92x15; 256,24 16x15; 256,40 16x15; 256,56 16x15; 256,72 16x15; 256,88 16x15; 272,24 16x15; 272,40 16x15; 272,56 16x15; 272,72 16x15; 272,88 16x15; 288,24 16x15; 288,40 16x15; 288,56 16x15; 288,72 16x15; 288,88 16x15; 304,24 16x15; 304,40 16x15; 304,56 16x15; 304,72 16x15; 304,88 16x15; 0,160 8x7; 8,160 8x7; 16,160 8x7; 24,160 8x7; 32,160 8x7; 40,160 8x7; 48,160 8x7; 56,160 8x7; 64,160 8x7; 72,160 8x7; 80,160 8x7; 88,160 8x7; 96,160 8x7; 104,160 8x7; 112,160 8x7; 120,160 8x7; 128,160 8x7; 136,160 8x7; 144,160 8x7; 152,160 8x7; 160,160 8x7; 168,160 8x7; 176,160 8x7; 184,160 8x7; 192,160 8x7; 200,160 8x7; 0,168 8x7; 8,168 8x7; 16,168 8x7; 24,168 8x7; 32,168 8x7; 40,168 8x7; 48,168 8x7; 56,168 8x7; 64,168 8x7; 72,168 8x7; 80,168 8x7; 88,168 8x7; 96,168 8x7; 104,168 8x7; 112,168 8x7; 120,168 8x7; 152,136 56x23; 212,136 56x23; 252,0 68x23; 0,35 24x29; 24,35 24x29; 48,35 24x29; 72,35 24x29; 0,0 24x30; 24,0 24x30; 48,0 24x30; 72,0 24x30; 120,0 20x30; 140,0 20x30; 160,0 20x30; 180,0 20x30; 0,120 112x15; 116,120 116x15; 4,136 148x15; 120,32 8x21; 128,32 8x21; 136,32 8x21; 144,32 8x21; 152,32 8x21; 160,32 8x21; 224,3 24x85; 0,88 68x15; 72,88 68x15; 144,88 48x15; 0,104 144x15; 136,68 32x16; 236,104 12x15`
* **#2 02.img** -> S8, list DS:0x6650: `0,0 20x22; 24,0 20x22; 48,0 20x22; 72,0 20x22; 96,0 20x22; 120,0 20x22; 144,0 20x22; 168,0 20x22; 192,0 20x22; 216,0 20x22; 240,0 20x22; 264,0 20x22; 288,0 20x22; 0,24 24x22; 24,24 24x22; 48,24 24x22; 72,24 24x22; 96,24 24x22; 120,24 24x22; 144,24 24x22; 168,24 20x22; 192,24 20x22; 216,24 20x22; 240,24 20x22; 264,24 20x22; 288,24 20x22; 0,48 20x22; 24,48 20x22; 48,48 20x22; 72,48 20x22; 96,48 20x22; 120,48 20x22; 144,48 20x22; 168,48 24x22; 192,48 24x22; 216,48 24x22; 240,48 24x22; 264,48 24x22; 288,48 24x22; 0,72 24x22; 24,72 20x22; 48,72 20x22; 72,72 20x22; 96,72 20x22; 120,72 20x22; 144,72 20x22; 168,72 20x22; 192,72 20x22; 216,72 20x22; 240,72 20x22; 264,72 20x22; 288,72 20x22; 0,96 24x22; 24,96 24x22; 48,96 24x22; 72,96 24x22; 96,96 24x22; 120,96 24x22; 144,96 24x22; 168,96 24x22; 192,96 20x22; 216,96 20x22; 240,96 20x22; 264,96 20x22; 288,96 20x22; 0,120 20x22; 24,120 20x22; 48,120 20x22; 72,120 20x22; 96,120 20x22; 120,120 20x22; 144,120 20x22; 168,120 24x22; 192,120 24x22; 216,120 24x22; 240,120 24x22; 264,120 24x22; 288,120 24x22; 264,144 24x22; 288,144 24x22; 0,172 28x27; 32,172 28x27; 64,172 28x27; 96,172 28x27; 128,172 28x27; 160,172 28x27; 192,172 28x27; 0,145 16x18; 16,145 16x18; 32,145 16x18; 144,145 16x18; 160,145 16x18; 176,145 16x18; 192,145 16x18; 208,145 16x18; 48,145 16x18; 64,145 16x18; 80,145 16x18; 96,145 16x18; 112,145 16x18; 128,145 16x18; 264,192 44x8`
* **#3 06.img** -> S5, list DS:0x5fb4: `0,0 16x16; 16,0 16x16; 32,0 16x16; 48,0 16x16; 64,0 16x16; 80,0 16x16; 96,0 16x16; 112,0 16x16; 128,0 16x16; 144,0 16x16; 160,0 16x16; 176,0 16x16; 192,0 16x16; 208,0 16x16; 224,0 16x16; 240,0 16x16; 256,0 16x16; 272,0 16x16; 288,0 16x16; 304,0 8x16; 0,48 8x16; 16,48 8x16; 32,48 8x16; 48,48 8x16; 64,48 8x16; 80,48 8x16; 96,48 8x16; 112,48 8x16; 128,48 16x16; 144,48 16x16; 160,48 16x16; 176,48 16x16; 192,48 16x16; 208,48 16x16; 224,48 16x16; 240,48 16x16; 0,16 16x16; 16,16 16x16; 32,16 16x16; 48,16 16x16; 64,16 16x16; 80,16 16x16; 96,16 16x16; 112,16 16x16; 128,16 16x16; 144,16 16x16; 160,16 16x16; 176,16 16x16; 192,16 16x16; 208,16 16x16; 224,16 16x16; 240,16 16x16; 256,16 16x16; 272,16 16x16; 288,16 16x16; 304,16 16x16; 0,32 16x16; 16,32 16x16; 32,32 16x16; 48,32 16x16; 64,32 16x16; 80,32 16x16; 96,32 16x16; 112,32 16x16; 128,32 16x16; 144,32 16x16; 160,32 16x16; 176,32 16x16; 192,32 16x16; 208,32 16x16; 224,32 16x16; 240,32 16x16; 256,32 16x16; 272,32 16x16; 288,32 16x16; 304,32 16x16; 0,72 row of 20 16x18; 0,90 row of 20 16x18; 0,108 row of 20 16x18; 0,126 row of 20 16x18; 0,144 row of 20 16x18; 0,162 row of 20 16x18; 0,180 row of 20 16x18`
* **#4 01.img** -> S8, list DS:0x5d62: `0,0 20x22; 24,0 20x22; 48,0 20x22; 72,0 20x22; 96,0 20x22; 120,0 20x22; 144,0 20x22; 168,0 20x22; 192,0 20x22; 216,0 20x22; 240,0 20x22; 264,0 20x22; 288,0 20x22; 0,24 24x22; 24,24 24x22; 48,24 24x22; 72,24 24x22; 96,24 24x22; 120,24 24x22; 144,24 24x22; 0,172 28x27; 32,172 28x27; 64,172 28x27; 96,172 28x27; 128,172 28x27; 160,172 28x27; 192,172 28x27; 0,48 16x16; 16,48 16x16; 32,48 16x16; 48,48 16x16; 64,48 16x16; 80,48 16x16; 96,48 16x16; 112,48 16x16; 128,48 16x16; 144,48 16x16; 160,48 16x16; 176,48 16x16; 192,48 16x16; 208,48 16x16; 224,48 16x16; 240,48 16x16; 0,148 256x24; 256,148 8x7; 264,148 8x7; 272,148 8x7; 280,148 8x7; 288,148 8x7; 296,148 8x7; 304,148 8x7; 312,148 8x7; 256,155 8x7; 264,155 8x7; 168,24 32x16; 200,24 32x16; 228,172 32x16; 260,172 32x16; 208,64 112x30; 200,95 8x13; 208,95 8x13; 216,95 8x13; 224,95 8x13; 232,95 8x13; 240,95 8x13; 248,95 8x13; 256,95 8x13; 264,95 8x13; 272,95 8x13; 0,82 196x66; 264,24 40x27; 200,108 116x27; 128,72 8x8; 264,192 44x8`
* **#5 04.img** -> S7, list DS:0xffff: `cells16x18` (rows 0..179)
* **#6 14.img** -> S7, list DS:0x586a: `0,0 row of 20 16x18; 0,18 row of 20 16x18; 0,36 row of 20 16x18; 0,54 row of 20 16x18; 0,72 row of 20 16x18; 64,156 32x32; 96,156 32x32; 128,156 32x32; 0,90 24x22; 32,90 24x22; 64,90 24x22; 96,90 24x22; 128,90 24x22; 160,90 24x22; 192,90 24x22; 224,90 24x22; 256,90 24x22; 288,90 24x22; 0,112 24x22; 32,112 24x22; 64,112 24x22; 96,112 24x22; 128,112 24x22; 160,112 24x22; 192,112 24x22; 224,112 24x22; 256,112 24x22; 288,112 24x22; 0,134 24x22; 32,134 24x22; 64,134 24x22; 96,134 24x22; 128,134 24x22; 160,134 24x22; 192,134 24x22; 224,134 24x22; 256,134 24x22; 288,134 24x22; 0,156 24x22; 32,156 24x22`
* **#8 19.img** -> S5, list DS:0x5d10: `0,0 256x146; 0,146 40x52; 40,146 56x51; 104,146 168x34; 104,181 8x8; 256,0 20x22; 276,0 20x22; 296,0 20x22; 256,189 64x3; 256,192 8x3`
* **#9 18.img** -> S5, list DS:0x5ca6: `0,0 256x160; 0,160 24x23; 24,160 24x23; 48,160 24x23; 72,160 56x15; 136,160 16x15; 152,160 16x15; 168,160 16x15; 184,160 16x15; 200,160 16x15; 216,160 16x15; 232,160 16x15; 248,160 16x15`
* **#10 07.img** -> S5, list DS:0x5fb4: `0,0 16x16; 16,0 16x16; 32,0 16x16; 48,0 16x16; 64,0 16x16; 80,0 16x16; 96,0 16x16; 112,0 16x16; 128,0 16x16; 144,0 16x16; 160,0 16x16; 176,0 16x16; 192,0 16x16; 208,0 16x16; 224,0 16x16; 240,0 16x16; 256,0 16x16; 272,0 16x16; 288,0 16x16; 304,0 8x16; 0,48 8x16; 16,48 8x16; 32,48 8x16; 48,48 8x16; 64,48 8x16; 80,48 8x16; 96,48 8x16; 112,48 8x16; 128,48 16x16; 144,48 16x16; 160,48 16x16; 176,48 16x16; 192,48 16x16; 208,48 16x16; 224,48 16x16; 240,48 16x16; 0,16 16x16; 16,16 16x16; 32,16 16x16; 48,16 16x16; 64,16 16x16; 80,16 16x16; 96,16 16x16; 112,16 16x16; 128,16 16x16; 144,16 16x16; 160,16 16x16; 176,16 16x16; 192,16 16x16; 208,16 16x16; 224,16 16x16; 240,16 16x16; 256,16 16x16; 272,16 16x16; 288,16 16x16; 304,16 16x16; 0,32 16x16; 16,32 16x16; 32,32 16x16; 48,32 16x16; 64,32 16x16; 80,32 16x16; 96,32 16x16; 112,32 16x16; 128,32 16x16; 144,32 16x16; 160,32 16x16; 176,32 16x16; 192,32 16x16; 208,32 16x16; 224,32 16x16; 240,32 16x16; 256,32 16x16; 272,32 16x16; 288,32 16x16; 304,32 16x16; 0,72 row of 20 16x18; 0,90 row of 20 16x18; 0,108 row of 20 16x18; 0,126 row of 20 16x18; 0,144 row of 20 16x18; 0,162 row of 20 16x18; 0,180 row of 20 16x18`
* **#11 08.img** -> S5, list DS:0x5fb4: `0,0 16x16; 16,0 16x16; 32,0 16x16; 48,0 16x16; 64,0 16x16; 80,0 16x16; 96,0 16x16; 112,0 16x16; 128,0 16x16; 144,0 16x16; 160,0 16x16; 176,0 16x16; 192,0 16x16; 208,0 16x16; 224,0 16x16; 240,0 16x16; 256,0 16x16; 272,0 16x16; 288,0 16x16; 304,0 8x16; 0,48 8x16; 16,48 8x16; 32,48 8x16; 48,48 8x16; 64,48 8x16; 80,48 8x16; 96,48 8x16; 112,48 8x16; 128,48 16x16; 144,48 16x16; 160,48 16x16; 176,48 16x16; 192,48 16x16; 208,48 16x16; 224,48 16x16; 240,48 16x16; 0,16 16x16; 16,16 16x16; 32,16 16x16; 48,16 16x16; 64,16 16x16; 80,16 16x16; 96,16 16x16; 112,16 16x16; 128,16 16x16; 144,16 16x16; 160,16 16x16; 176,16 16x16; 192,16 16x16; 208,16 16x16; 224,16 16x16; 240,16 16x16; 256,16 16x16; 272,16 16x16; 288,16 16x16; 304,16 16x16; 0,32 16x16; 16,32 16x16; 32,32 16x16; 48,32 16x16; 64,32 16x16; 80,32 16x16; 96,32 16x16; 112,32 16x16; 128,32 16x16; 144,32 16x16; 160,32 16x16; 176,32 16x16; 192,32 16x16; 208,32 16x16; 224,32 16x16; 240,32 16x16; 256,32 16x16; 272,32 16x16; 288,32 16x16; 304,32 16x16; 0,72 row of 20 16x18; 0,90 row of 20 16x18; 0,108 row of 20 16x18; 0,126 row of 20 16x18; 0,144 row of 20 16x18; 0,162 row of 20 16x18; 0,180 row of 20 16x18`
* **#12 09.img** -> S5, list DS:0x59ac: `0,0 16x16; 16,0 16x16; 32,0 16x16; 48,0 16x16; 64,0 16x16; 80,0 16x16; 96,0 16x16; 112,0 16x16; 128,0 16x16; 144,0 16x16; 160,0 16x16; 176,0 16x16; 192,0 16x16; 208,0 16x16; 224,0 16x16; 240,0 16x16; 256,0 16x16; 272,0 16x16; 288,0 16x16; 304,0 8x16; 0,48 8x16; 16,48 8x16; 32,48 8x16; 48,48 8x16; 64,48 8x16; 80,48 8x16; 96,48 8x16; 112,48 8x16; 128,48 16x16; 144,48 16x16; 160,48 16x16; 176,48 16x16; 192,48 16x16; 208,48 16x16; 224,48 16x16; 240,48 16x16; 0,16 16x16; 16,16 16x16; 32,16 16x16; 48,16 16x16; 64,16 16x16; 80,16 16x16; 96,16 16x16; 112,16 16x16; 128,16 16x16; 144,16 16x16; 160,16 16x16; 176,16 16x16; 192,16 16x16; 208,16 16x16; 224,16 16x16; 240,16 16x16; 256,16 16x16; 272,16 16x16; 288,16 16x16; 304,16 16x16; 0,32 16x16; 16,32 16x16; 32,32 16x16; 48,32 16x16; 64,32 16x16; 80,32 16x16; 96,32 16x16; 112,32 16x16; 128,32 16x16; 144,32 16x16; 160,32 16x16; 176,32 16x16; 192,32 16x16; 208,32 16x16; 224,32 16x16; 240,32 16x16; 256,32 16x16; 272,32 16x16; 288,32 16x16; 304,32 16x16; 0,72 row of 20 16x18; 0,90 row of 20 16x18; 0,108 row of 20 16x18; 0,126 row of 20 16x18; 0,145 16x24; 16,145 16x24; 32,145 16x24; 48,145 16x24; 64,145 16x24; 80,145 16x24; 96,145 16x24; 112,145 16x24; 128,145 32x30; 160,145 32x30; 192,145 32x24; 224,145 32x24; 256,145 32x24; 288,145 32x24; 0,169 32x24`
* **#13 10.img** -> S5, list DS:0x5fb4: `0,0 16x16; 16,0 16x16; 32,0 16x16; 48,0 16x16; 64,0 16x16; 80,0 16x16; 96,0 16x16; 112,0 16x16; 128,0 16x16; 144,0 16x16; 160,0 16x16; 176,0 16x16; 192,0 16x16; 208,0 16x16; 224,0 16x16; 240,0 16x16; 256,0 16x16; 272,0 16x16; 288,0 16x16; 304,0 8x16; 0,48 8x16; 16,48 8x16; 32,48 8x16; 48,48 8x16; 64,48 8x16; 80,48 8x16; 96,48 8x16; 112,48 8x16; 128,48 16x16; 144,48 16x16; 160,48 16x16; 176,48 16x16; 192,48 16x16; 208,48 16x16; 224,48 16x16; 240,48 16x16; 0,16 16x16; 16,16 16x16; 32,16 16x16; 48,16 16x16; 64,16 16x16; 80,16 16x16; 96,16 16x16; 112,16 16x16; 128,16 16x16; 144,16 16x16; 160,16 16x16; 176,16 16x16; 192,16 16x16; 208,16 16x16; 224,16 16x16; 240,16 16x16; 256,16 16x16; 272,16 16x16; 288,16 16x16; 304,16 16x16; 0,32 16x16; 16,32 16x16; 32,32 16x16; 48,32 16x16; 64,32 16x16; 80,32 16x16; 96,32 16x16; 112,32 16x16; 128,32 16x16; 144,32 16x16; 160,32 16x16; 176,32 16x16; 192,32 16x16; 208,32 16x16; 224,32 16x16; 240,32 16x16; 256,32 16x16; 272,32 16x16; 288,32 16x16; 304,32 16x16; 0,72 row of 20 16x18; 0,90 row of 20 16x18; 0,108 row of 20 16x18; 0,126 row of 20 16x18; 0,144 row of 20 16x18; 0,162 row of 20 16x18; 0,180 row of 20 16x18`
* **#14 11.img** -> S5, list DS:0x59ac: `0,0 16x16; 16,0 16x16; 32,0 16x16; 48,0 16x16; 64,0 16x16; 80,0 16x16; 96,0 16x16; 112,0 16x16; 128,0 16x16; 144,0 16x16; 160,0 16x16; 176,0 16x16; 192,0 16x16; 208,0 16x16; 224,0 16x16; 240,0 16x16; 256,0 16x16; 272,0 16x16; 288,0 16x16; 304,0 8x16; 0,48 8x16; 16,48 8x16; 32,48 8x16; 48,48 8x16; 64,48 8x16; 80,48 8x16; 96,48 8x16; 112,48 8x16; 128,48 16x16; 144,48 16x16; 160,48 16x16; 176,48 16x16; 192,48 16x16; 208,48 16x16; 224,48 16x16; 240,48 16x16; 0,16 16x16; 16,16 16x16; 32,16 16x16; 48,16 16x16; 64,16 16x16; 80,16 16x16; 96,16 16x16; 112,16 16x16; 128,16 16x16; 144,16 16x16; 160,16 16x16; 176,16 16x16; 192,16 16x16; 208,16 16x16; 224,16 16x16; 240,16 16x16; 256,16 16x16; 272,16 16x16; 288,16 16x16; 304,16 16x16; 0,32 16x16; 16,32 16x16; 32,32 16x16; 48,32 16x16; 64,32 16x16; 80,32 16x16; 96,32 16x16; 112,32 16x16; 128,32 16x16; 144,32 16x16; 160,32 16x16; 176,32 16x16; 192,32 16x16; 208,32 16x16; 224,32 16x16; 240,32 16x16; 256,32 16x16; 272,32 16x16; 288,32 16x16; 304,32 16x16; 0,72 row of 20 16x18; 0,90 row of 20 16x18; 0,108 row of 20 16x18; 0,126 row of 20 16x18; 0,145 16x24; 16,145 16x24; 32,145 16x24; 48,145 16x24; 64,145 16x24; 80,145 16x24; 96,145 16x24; 112,145 16x24; 128,145 32x30; 160,145 32x30; 192,145 32x24; 224,145 32x24; 256,145 32x24; 288,145 32x24; 0,169 32x24`
* **#15 12.img** -> S5, list DS:0x59ac: `0,0 16x16; 16,0 16x16; 32,0 16x16; 48,0 16x16; 64,0 16x16; 80,0 16x16; 96,0 16x16; 112,0 16x16; 128,0 16x16; 144,0 16x16; 160,0 16x16; 176,0 16x16; 192,0 16x16; 208,0 16x16; 224,0 16x16; 240,0 16x16; 256,0 16x16; 272,0 16x16; 288,0 16x16; 304,0 8x16; 0,48 8x16; 16,48 8x16; 32,48 8x16; 48,48 8x16; 64,48 8x16; 80,48 8x16; 96,48 8x16; 112,48 8x16; 128,48 16x16; 144,48 16x16; 160,48 16x16; 176,48 16x16; 192,48 16x16; 208,48 16x16; 224,48 16x16; 240,48 16x16; 0,16 16x16; 16,16 16x16; 32,16 16x16; 48,16 16x16; 64,16 16x16; 80,16 16x16; 96,16 16x16; 112,16 16x16; 128,16 16x16; 144,16 16x16; 160,16 16x16; 176,16 16x16; 192,16 16x16; 208,16 16x16; 224,16 16x16; 240,16 16x16; 256,16 16x16; 272,16 16x16; 288,16 16x16; 304,16 16x16; 0,32 16x16; 16,32 16x16; 32,32 16x16; 48,32 16x16; 64,32 16x16; 80,32 16x16; 96,32 16x16; 112,32 16x16; 128,32 16x16; 144,32 16x16; 160,32 16x16; 176,32 16x16; 192,32 16x16; 208,32 16x16; 224,32 16x16; 240,32 16x16; 256,32 16x16; 272,32 16x16; 288,32 16x16; 304,32 16x16; 0,72 row of 20 16x18; 0,90 row of 20 16x18; 0,108 row of 20 16x18; 0,126 row of 20 16x18; 0,145 16x24; 16,145 16x24; 32,145 16x24; 48,145 16x24; 64,145 16x24; 80,145 16x24; 96,145 16x24; 112,145 16x24; 128,145 32x30; 160,145 32x30; 192,145 32x24; 224,145 32x24; 256,145 32x24; 288,145 32x24; 0,169 32x24`
* **#16 13.img** -> S5, list DS:0x59ac: `0,0 16x16; 16,0 16x16; 32,0 16x16; 48,0 16x16; 64,0 16x16; 80,0 16x16; 96,0 16x16; 112,0 16x16; 128,0 16x16; 144,0 16x16; 160,0 16x16; 176,0 16x16; 192,0 16x16; 208,0 16x16; 224,0 16x16; 240,0 16x16; 256,0 16x16; 272,0 16x16; 288,0 16x16; 304,0 8x16; 0,48 8x16; 16,48 8x16; 32,48 8x16; 48,48 8x16; 64,48 8x16; 80,48 8x16; 96,48 8x16; 112,48 8x16; 128,48 16x16; 144,48 16x16; 160,48 16x16; 176,48 16x16; 192,48 16x16; 208,48 16x16; 224,48 16x16; 240,48 16x16; 0,16 16x16; 16,16 16x16; 32,16 16x16; 48,16 16x16; 64,16 16x16; 80,16 16x16; 96,16 16x16; 112,16 16x16; 128,16 16x16; 144,16 16x16; 160,16 16x16; 176,16 16x16; 192,16 16x16; 208,16 16x16; 224,16 16x16; 240,16 16x16; 256,16 16x16; 272,16 16x16; 288,16 16x16; 304,16 16x16; 0,32 16x16; 16,32 16x16; 32,32 16x16; 48,32 16x16; 64,32 16x16; 80,32 16x16; 96,32 16x16; 112,32 16x16; 128,32 16x16; 144,32 16x16; 160,32 16x16; 176,32 16x16; 192,32 16x16; 208,32 16x16; 224,32 16x16; 240,32 16x16; 256,32 16x16; 272,32 16x16; 288,32 16x16; 304,32 16x16; 0,72 row of 20 16x18; 0,90 row of 20 16x18; 0,108 row of 20 16x18; 0,126 row of 20 16x18; 0,145 16x24; 16,145 16x24; 32,145 16x24; 48,145 16x24; 64,145 16x24; 80,145 16x24; 96,145 16x24; 112,145 16x24; 128,145 32x30; 160,145 32x30; 192,145 32x24; 224,145 32x24; 256,145 32x24; 288,145 32x24; 0,169 32x24`
* **#17 03.img** -> S7, list DS:0x5860: `0,0 256x168`
* **#18 16.img** -> S7, list DS:0x5856: `0,0 256x200`
* **#19 17.img** -> S5, list DS:0x747e: `0,0 256x15; 256,0 48x26; 0,15 40x32; 0,47 40x48; 0,95 40x32; 0,127 40x32; 0,159 40x32; 40,15 40x32; 40,47 40x32; 80,15 40x49; 120,15 40x49; 160,15 40x49; 200,15 40x38; 80,64 20x23; 100,64 20x23; 120,64 20x23; 140,64 20x23; 160,64 20x23; 180,64 20x23; 200,64 20x23; 220,64 20x23; 240,64 20x23; 260,64 20x23; 240,26 20x30; 40,87 28x27; 68,87 28x27; 96,87 28x27; 124,87 28x27; 152,87 28x27; 180,87 28x27; 208,87 28x27; 236,87 28x27; 264,87 28x27; 260,26 28x30; 304,0 16x16; 304,16 16x16; 304,32 16x16; 304,48 16x16; 80,115 20x23; 100,115 20x23; 120,115 20x23; 140,115 20x23; 160,115 20x23; 180,115 20x23; 200,115 20x28; 220,115 20x23; 240,115 20x23; 260,115 20x23; 280,115 20x23; 300,117 20x20; 80,138 24x30; 104,138 24x30`

### B. Opaque tile/picture table DS:0x0EE2 (index used by `0x1086A`/`0x10DB1`; 8 bytes: off, seg, w/4, h)

```
idx  seg  x   y   w   h
  0  S5   0   0  16  16
  1  S5  16   0  16  16
  2  S5  32   0  16  16
  3  S5  48   0  16  16
  4  S5  64   0  16  16
  5  S5  80   0  16  16
  6  S5  96   0  16  16
  7  S5 112   0  16  16
  8  S5 128   0  16  16
  9  S5 144   0  16  16
 10  S5 160   0  16  16
 11  S5 176   0  16  16
 12  S5 192   0  16  16
 13  S5 208   0  16  16
 14  S5 224   0  16  16
 15  S5 240   0   8  16
 16  S5 248   0   8  16
 17  S7   0 176 256  24
 18  S7 256 176   8   7
 19  S7 264 176   8   7
 20  S7 272 176   8   7
 21  S7 280 176   8   7
 22  S7 288 176   8   7
 23  S7 296 176   8   7
 24  S7 304 176   8   7
 25  S7 312 176   8   7
 26  S7 256 183   8   7
 27  S7 264 183   8   7
 28  S5   0  16  16  16
 29  S5  16  16  16  16
 30  S5  32  16  16  16
 31  S5  48  16  16  16
 32  S5  64  16  16  16
 33  S5  80  16  16  16
 34  S5  96  16  16  16
 35  S5 112  16  16  16
 36  S5 128  16  16  16
 37  S5 144  16  16  16
 38  S5 160  16  16  16
 39  S5 176  16  16  16
 40  S5 192  16  16  16
 41  S5 208  16  16  16
 42  S5 224  16  16  16
 43  S5 240  16  16  16
 44  S5 256  16  16  16
 45  S5 272  16  16  16
 46  S5 288  16  16  16
 47  S5 304  16  16  16
 48  S5   0  32  16  16
 49  S5  16  32  16  16
 50  S5  32  32  16  16
 51  S5  48  32  16  16
 52  S5  64  32  16  16
 53  S5  80  32  16  16
 54  S5  96  32  16  16
 55  S5 112  32  16  16
 56  S5 128  32  16  16
 57  S7 120  64  16  21
 58  S7  48 152   8   8
 59  S7  56 152   8   8
 60  S7  64 152   8   8
 61  S7  72 152   8   8
 62  S7  80 152   8   8
 63  S7  88 152   8   8
 64  S7  96 152   8   8
 65  S7 104 152   8   8
 66  S7   0  64  24  23
 67  S7  24  64  24  23
 68  S7  48  64  24  23
 69  S7  72  64  24  23
 70  S7   0 152   8   8
 71  S7   8 152   8   8
 72  S7  16 152   8   8
 73  S7  24 152   8   8
 74  S7  40 152   8   8
 75  S7 192  88   8  15
 76  S7 200  88   8  15
 77  S7 208  88   8  15
 78  S7 216  88   8  15
 79  S7 224  88   8  15
 80  S7 144 104  92  15
 81  S7 152 136  56  23
 82  S7 212 136  56  23
 83  S7 252   0  68  23
 84  S7   0  35  24  29
 85  S7  24  35  24  29
 86  S7  48  35  24  29
 87  S7  72  35  24  29
 88  S7   0   0  24  30
 89  S7  24   0  24  30
 90  S7  48   0  24  30
 91  S7  72   0  24  30
 92  S7 120   0  20  30
 93  S7 140   0  20  30
 94  S7 160   0  20  30
 95  S7 180   0  20  30
 96  S7   0 120 112  15
 97  S7 116 120 116  15
 98  S7   4 136 148  15
 99  S7   0  88  68  15
100  S7  72  88  68  15
101  S7 144  88  48  15
102  S7   0 104 144  15
103  S7 136  68  32  16
104  S7 236 104  12  15
105  S5   0   0  16  16
106  S5  16   0  16  16
107  S5  32   0  16  16
108  S5  48   0  16  16
109  S5  64   0  16  16
110  S5  80   0  16  16
111  S5  96   0  16  16
112  S5 112   0  16  16
113  S5 128   0  16  16
114  S5 144   0  16  16
115  S5 160   0  16  16
116  S5 176   0  16  16
117  S5 192   0  16  16
118  S5 208   0  16  16
119  S5 224   0  16  16
120  S5 240   0  16  16
121  S5 256   0  16  16
122  S5 272   0  16  16
123  S5 288   0  16  16
124  S5 304   0   8  16
125  S5   0  48   8  16
126  S5  16  48   8  16
127  S5  32  48   8  16
128  S5  48  48   8  16
129  S5  64  48   8  16
130  S5  80  48   8  16
131  S5  96  48   8  16
132  S5 112  48   8  16
133  S5 128  48  16  16
134  S5 144  48  16  16
135  S5 160  48  16  16
136  S5 176  48  16  16
137  S5 192  48  16  16
138  S5 208  48  16  16
139  S5 224  48  16  16
140  S5 240  48  16  16
141  S5 144  32  16  16
142  S5 160  32  16  16
143  S5 176  32  16  16
144  S5 192  32  16  16
145  S8   0  48  16  16
146  S8  16  48  16  16
147  S8  32  48  16  16
148  S8  48  48  16  16
149  S8  64  48  16  16
150  S8  80  48  16  16
151  S8  96  48  16  16
152  S8 112  48  16  16
153  S8 128  48  16  16
154  S8 144  48  16  16
155  S8 160  48  16  16
156  S8 176  48  16  16
157  S8 192  48  16  16
158  S8 208  48  16  16
159  S8 224  48  16  16
160  S8 240  48  16  16
161  S5   0   0 256 146
162  S8 256 148   8   7
163  S8 264 148   8   7
164  S8 272 148   8   7
165  S8 280 148   8   7
166  S8 288 148   8   7
167  S8 296 148   8   7
168  S8 304 148   8   7
169  S8 312 148   8   7
170  S8 256 155   8   7
171  S8 264 155   8   7
172  S8 208  64 112  30
173  S8 200  95   8  13
174  S8 208  95   8  13
175  S8 216  95   8  13
176  S8 224  95   8  13
177  S8 232  95   8  13
178  S8 240  95   8  13
179  S8 248  95   8  13
180  S8 256  95   8  13
181  S8 264  95   8  13
182  S8 272  95   8  13
183  S5   0   0 256 160
184  S5  72 160  56  15
185  S5 136 160  16  14
186  S5 152 160  16  14
187  S5 168 160  16  14
188  S5 184 160  16  14
189  S5 200 160  16  14
190  S5 216 160  16  14
191  S5 232 160  16  14
192  S5 248 160  16  14
193  S8   0  82 196  66
194  S8 264  24  40  27
195  S8 200 108 116  27
196  S5 256 189  64   3
197  S5 256 192   8   3
198  S7   0   0 256 168
199  S7   0   0 256 200
200  S5   0   0 256  15
201  S8 264 192  44   8
```

### C. Masked sprite table DS:0x180C (index used by `0x1089B` normal / `0x109E6` solid-colour silhouette; 16 bytes: off, seg, w/4, h, save-under far ptr page0, page1)

```
idx  seg  x   y   w   h   save0       save1
  0  S8   0   0  20  22   S6:0000  S6:0280
  1  S8  24   0  20  22   S6:0000  S6:0280
  2  S8  48   0  20  22   S6:0000  S6:0280
  3  S8  72   0  20  22   S6:0000  S6:0280
  4  S8  96   0  20  22   S6:0000  S6:0280
  5  S8 120   0  20  22   S6:0000  S6:0280
  6  S8 144   0  20  22   S6:0000  S6:0280
  7  S8 168   0  20  22   S6:0000  S6:0280
  8  S8 192   0  20  22   S6:0000  S6:0280
  9  S8 216   0  20  22   S6:0000  S6:0280
 10  S8 240   0  20  22   S6:0000  S6:0280
 11  S8 264   0  20  22   S6:0000  S6:0280
 12  S8 288   0  20  22   S6:0000  S6:0280
 13  S8   0  24  24  22   S6:0000  S6:0280
 14  S8  24  24  24  22   S6:0000  S6:0280
 15  S8  48  24  24  22   S6:0000  S6:0280
 16  S8  72  24  24  22   S6:0000  S6:0280
 17  S8  96  24  24  22   S6:0000  S6:0280
 18  S8 120  24  24  22   S6:0000  S6:0280
 19  S8 144  24  24  22   S6:0000  S6:0280
 20  S8 168  24  20  22   S6:0500  S6:0780
 21  S8 192  24  20  22   S6:0500  S6:0780
 22  S8 216  24  20  22   S6:0500  S6:0780
 23  S8 240  24  20  22   S6:0500  S6:0780
 24  S8 264  24  20  22   S6:0500  S6:0780
 25  S8 288  24  20  22   S6:0500  S6:0780
 26  S8   0  48  20  22   S6:0500  S6:0780
 27  S8  24  48  20  22   S6:0500  S6:0780
 28  S8  48  48  20  22   S6:0500  S6:0780
 29  S8  72  48  20  22   S6:0500  S6:0780
 30  S8  96  48  20  22   S6:0500  S6:0780
 31  S8 120  48  20  22   S6:0500  S6:0780
 32  S8 144  48  20  22   S6:0500  S6:0780
 33  S8 168  48  24  22   S6:0500  S6:0780
 34  S8 192  48  24  22   S6:0500  S6:0780
 35  S8 216  48  24  22   S6:0500  S6:0780
 36  S8 240  48  24  22   S6:0500  S6:0780
 37  S8 264  48  24  22   S6:0500  S6:0780
 38  S8 288  48  24  22   S6:0500  S6:0780
 39  S8   0  72  24  22   S6:0500  S6:0780
 40  S8  24  72  20  22   S6:0a00  S6:0c80
 41  S8  48  72  20  22   S6:0a00  S6:0c80
 42  S8  72  72  20  22   S6:0a00  S6:0c80
 43  S8  96  72  20  22   S6:0a00  S6:0c80
 44  S8 120  72  20  22   S6:0a00  S6:0c80
 45  S8 144  72  20  22   S6:0a00  S6:0c80
 46  S8 168  72  20  22   S6:0a00  S6:0c80
 47  S8 192  72  20  22   S6:0a00  S6:0c80
 48  S8 216  72  20  22   S6:0a00  S6:0c80
 49  S8 240  72  20  22   S6:0a00  S6:0c80
 50  S8 264  72  20  22   S6:0a00  S6:0c80
 51  S8 288  72  20  22   S6:0a00  S6:0c80
 52  S8   0  96  24  22   S6:0a00  S6:0c80
 53  S8  24  96  24  22   S6:0a00  S6:0c80
 54  S8  48  96  24  22   S6:0a00  S6:0c80
 55  S8  72  96  24  22   S6:0a00  S6:0c80
 56  S8  96  96  24  22   S6:0a00  S6:0c80
 57  S8 120  96  24  22   S6:0a00  S6:0c80
 58  S8 144  96  24  22   S6:0a00  S6:0c80
 59  S8 168  96  24  22   S6:0a00  S6:0c80
 60  S8 192  96  20  22   S6:0f00  S6:1180
 61  S8 216  96  20  22   S6:0f00  S6:1180
 62  S8 240  96  20  22   S6:0f00  S6:1180
 63  S8 264  96  20  22   S6:0f00  S6:1180
 64  S8 288  96  20  22   S6:0f00  S6:1180
 65  S8   0 120  20  22   S6:0f00  S6:1180
 66  S8  24 120  20  22   S6:0f00  S6:1180
 67  S8  48 120  20  22   S6:0f00  S6:1180
 68  S8  72 120  20  22   S6:0f00  S6:1180
 69  S8  96 120  20  22   S6:0f00  S6:1180
 70  S8 120 120  20  22   S6:0f00  S6:1180
 71  S8 144 120  20  22   S6:0f00  S6:1180
 72  S8 168 120  24  22   S6:0f00  S6:1180
 73  S8 192 120  24  22   S6:0f00  S6:1180
 74  S8 216 120  24  22   S6:0f00  S6:1180
 75  S8 240 120  24  22   S6:0f00  S6:1180
 76  S8 264 120  24  22   S6:0f00  S6:1180
 77  S8 288 120  24  22   S6:0f00  S6:1180
 78  S8 264 144  24  22   S6:0f00  S6:1180
 79  S8 288 144  24  22   S6:0f00  S6:1180
 80  S5 256   0  48  26   S6:5590  S6:6280
 81  S5   0  15  40  32   S6:1400  S6:1ae0
 82  S5   0  47  40  48   S6:0000  S6:0a00
 83  S5   0  95  40  32   S6:2da0  S6:3340
 84  S5   0 127  40  32   S6:2da0  S6:3340
 85  S5   0 159  40  32   S6:21c0  S6:2800
 86  S5  40  15  40  32   S6:38e0  S6:3e80
 87  S5  40  47  40  32   S6:38e0  S6:3e80
 88  S5  80  15  40  49   S6:5590  S6:6280
 89  S5 120  15  40  49   S6:5590  S6:6280
 90  S5 160  15  40  49   S6:5590  S6:6280
 91  S5 200  15  40  38   S6:4810  S6:4ed0
 92  S5  80  64  20  23   S6:0000  S6:0500
 93  S5 100  64  20  23   S6:0000  S6:0500
 94  S5 120  64  20  23   S6:0000  S6:0500
 95  S5 140  64  20  23   S6:0000  S6:0500
 96  S5 160  64  20  23   S6:0000  S6:0500
 97  S5 180  64  20  23   S6:0000  S6:0500
 98  S5 200  64  20  23   S6:0000  S6:0500
 99  S5 220  64  20  23   S6:0000  S6:0500
100  S5 240  64  20  23   S6:0000  S6:0500
101  S5 240  26  20  30   S6:1400  S6:1ae0
102  S5  40  87  28  27   S6:0a00  S6:0f00
103  S5  68  87  28  27   S6:0a00  S6:0f00
104  S5  96  87  28  27   S6:0a00  S6:0f00
105  S5 124  87  28  27   S6:0a00  S6:0f00
106  S5 152  87  28  27   S6:0a00  S6:0f00
107  S5 180  87  28  27   S6:0a00  S6:0f00
108  S5 208  87  28  27   S6:0a00  S6:0f00
109  S5 236  87  28  27   S6:0a00  S6:0f00
110  S5 264  87  28  27   S6:0a00  S6:0f00
111  S5 260  26  28  30   S6:2800  S6:3340
112  S5 304   0  16  16   S6:5590  S6:6280
113  S5 304  16  16  16   S6:5590  S6:6280
114  S5 304  32  16  16   S6:5590  S6:6280
115  S5 304  48  16  16   S6:5590  S6:6280
116  S5  80 115  20  23   S6:0a00  S6:0f00
117  S5 100 115  20  23   S6:0a00  S6:0f00
118  S5 120 115  20  23   S6:0a00  S6:0f00
119  S5 140 115  20  23   S6:0a00  S6:0f00
120  S5 160 115  20  23   S6:0a00  S6:0f00
121  S5 180 115  20  23   S6:0a00  S6:0f00
122  S5 260  64  20  23   S6:0000  S6:0500
123  S5  80 138  24  30   S6:1400  S6:1ae0
124  S5 104 138  24  30   S6:1400  S6:1ae0
125  S5 200 115  20  28   S6:0a00  S6:0f00
126  S5 220 115  20  23   S6:0a00  S6:0f00
127  S5 240 115  20  23   S6:0a00  S6:0f00
128  S5 260 115  20  23   S6:0a00  S6:0f00
129  S5 280 115  20  23   S6:0a00  S6:0f00
130  S5 300 117  20  20   S6:5590  S6:6280
```
