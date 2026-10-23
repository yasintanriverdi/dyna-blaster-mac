# Dyna Blaster (DOS) — Players, Input, Movement

Sources: `build/DYNA_UNP.EXE`. Function addresses are linear (`0x1XXXX` = `FUN_1000_XXXX`), and `DS:nnnn` means data segment 0x1965.
I checked everything below against the real assembly (REA `procedure_assembly` plus a capstone sweep), not just the pseudocode.
`[?]` marks items I'm not sure about.

Global mode variable `DS:4F88`: **0 = battle mode**, 1..8 = story world/area number (`DS:4F8A` = stage 1..8, `DS:4F8C` = lives, starting at 2).
Several rules below depend on it.

---

## 1. Tick rate / frame loop

* `INT 1Ch` handler at `0x11355` (installed by `0x1155c`). On each tick it sets `DS:204F=FF`, increments `DS:1C`, the 16-bit tick counter, and runs the match clock: every 18 ticks (`DS:204C` counts 0..0x12) it decrements `DS:37D0` seconds and `DS:37CE` minutes. Seconds wrap 0 → 59, and 0:00 sets `DS:23=FF`, which ends the round on time.
* The battle loop in `0x100a3` runs once per frame: `1623` (wait for VGA vertical retrace start, port 3DA bit 3) … game logic … `0448` (page flip; toggles `DS:22` between 00 and FF; swaps `DS:18/1A`) … `1633` (wait for retrace end) … `163C` (busy-wait until `DS:204F` is set, then clear it).
* **One logic tick = one INT 1Ch tick ≈ 18.2 Hz** (BIOS rate; PIT ch0 divisor 0xFFFF is restored at `0x1752c`). The 18-ticks-per-clock-second rule backs this up. `[?]` The music driver segment (around `0x18acd`) reprograms PIT ch0 to its own tempo. I assume it still chains to BIOS INT 8 at 18.2 Hz, but I haven't verified that.
* `DS:1E` = **4**, a constant animation/timer step added each tick to many counters. I found no writes to it in the game DS.
* `DS:1C` (tick counter) drives blinking (`&1`, `&2`, `&0xC`). `DS:22` toggles every frame.

## 2. Player struct (4 × 0x20 bytes at `DS:3C5E`; pointer table `DS:3F9E` = 3C5E,3C7E,3C9E,3CBE)

| off | name | meaning |
|---|---|---|
| +00 | `x` | pixel X of the player's **tile-centre reference point**. `-1` (0xFFFF) = slot unused or player gone |
| +02 | `y` | pixel Y (same reference) |
| +04 | `xacc` | 8.8 sub-pixel accumulator for horizontal moves (only the low byte persists) |
| +06 | `yacc` | 8.8 accumulator for vertical moves |
| +08 | `anim` | walk frame 0..3. **Negative = dying** (−1..−13, death animation index) |
| +0A | `animcnt` | walk: pixel-step counter 0..9. Dying: tick counter. `-1` = story "entering exit door" state |
| +0C | `maxBombs` | bomb capacity (battle start 1; story start 1; HUDSONSOFT cheat sets 8) |
| +0E | `bombsOut` | bombs currently placed (decremented when one explodes, `0x144bf`) |
| +10 | — | not initialised or used by the player code `[?]` |
| +12 | `fire` | flame length (battle start **2**, story start 1) |
| +14 | `diseaseTimer` | battle skull timer, counts down each tick (0 = none) |
| +16 | `flags` | **battle:** disease type (−1 none, 0..3). **Story:** power-up bits: 1 wall-pass, 2 bomb-pass, 4 remote control, 8 fire-proof (also set when entering the exit) |
| +18 | `id` | player/colour index 0..3 (sprite bank, bomb owner) |
| +1A | `input` | input bits this tick (see §3) |
| +1C | `facing` | sprite base: **down=0, right=3, left=6, up=9** |
| +1E | `wins` | battle round wins (shown by `0x135c4`; reset at match start `0x10136..`) |

Battle round init (`0x101b1`). For i < `DS:10` (number of players):
```
ax = word[DS:36 + 4*i]   ; AL=col, AH=row
(x,y) = tile2pix(col,row)
maxBombs=1; bombsOut=0; fire=2; diseaseTimer=0; flags=2;
id = word[DS:38 + 4*i]; input=0; facing=0; animcnt=0; anim=0;
```
Just before play starts, `diseaseTimer` is set to 1 for all players (`0x10290..`). Players therefore start with "disease 2 = cannot bomb", which expires after 1 tick (flags → −1). Unused slots keep x = −1.

**Spawn table `DS:36`** (col,row,id): P0 (1,1) id0 · P1 (13,11) id1 · P2 (13,1) id2 · P3 (1,11) id3. These are the four corners of the 15×13 map.

Round time (`DS:2A + 4*(players-2)`, minutes then seconds): 2P 3:00, 3P 3:30, 4P 4:00.

## 3. Input

### Bit encoding (`player.input`, also the per-player demo byte)
`0x01` up · `0x02` down · `0x04` left · `0x08` right · `0x10` bomb (button A) · `0x20` button B / remote detonate (**player 0 only**, battle/story).

### Keyboard — INT 9 handler `0x11410`
* Reads port 0x60. For each of two key sets it calls `0x11501`, which maintains a held-key bitmask:
  * make code `== set[2k+1]` → set bit k
  * code `== set[2k]` (the break code, make|0x80) → clear bit k
  * k = 0..4 → bits 1, 2, 4, 8, 0x10
* Set 1 at `DS:205D` (12 bytes) → mask `DS:37F7`. Byte `set1[0x0B]` (make code) sets `DS:204E=FF`, the B button.
* Set 2 at `DS:2069` (10 bytes) → mask `DS:37F8`.
* Defaults:
  * Set 1: Up/Down/Left/Right arrows (48/50/4B/4D), bomb = Enter (1C), B = Alt (38).
  * Set 2: R (13) up, C (2E) down, D (20) left, F (21) right, bomb = Space (39).
* Last scancode → `DS:20`. The main loop treats `0x19` (P) as pause and `0x01` (Esc) as quit. In the ISR, `0x32` (M) toggles `DS:4F98` and `0x1F` (S) toggles `DS:4F99`; both look like music/sound toggles `[?]`.
* Cheat: the scancode sequence at `DS:2052` spells **HUDSONSOFT**. Completing it sets `DS:2050=FF`, P0 maxBombs=8, P0 fire=8, speed `DS:3A54=0x3C0`.
* The handler chains to the old INT 9.

### Joystick — via BIOS INT 15h AH=84h (in `0x1365a`)
* **Startup (`0x132e6`):** reads the axes once and stores the resting positions as centres.
  * Joystick A: X → `DS:37BC`, Y → `DS:37BE`. If A is present, `DS:37F9=0` (otherwise FF).
  * Joystick B: X → `DS:37C0`, Y → `DS:37C2`. If B is present, `DS:37FA=0` (otherwise FF).
* **Buttons** (DX=0; a bit reads 0 while the button is pressed):
  * A button 1 (bit 0x10) → bomb `0x10`.
  * A button 2 (bit 0x20) → sets `DS:204E` on the press edge only (tracked in `DS:37FB`).
  * B button 1 (bit 0x40) → bomb.
  * B button 2 (bit 0x80) → sets `DS:204E` (level-triggered).
* **Axes** (DX=1), handled by `0x13769`. Only the low byte of each axis is compared:
  * `X > cx+0x20` → 8 (right); `X <= cx-0x20` → 4 (left)
  * `Y > cy+0x20` → 2 (down); `Y <= cy-0x20` → 1 (up)
  * `X == 0` → whole byte = 0
* Results go to `DS:37F5` (joystick A) and `DS:37F6` (joystick B).

### Per-player source mapping
At the end of `0x1365a`:
```
for i in 0..3: player[i].input = *(byte*) word[DS:3800 + 2*src[i]]   ; src = DS:37FC[i]
player[0].input = (player[0].input & ~0x20) | (DS:204E ? 0x20 : 0);  DS:204E = 0
```
Source table `DS:3800`: 0 → kbd set 1 (`37F3`), 1 → kbd set 2 (`37F4`), 2 → `37F1`, 3 → `37F2`, 4 → joystick A (`37F5`), 5 → joystick B (`37F6`).
`37F1/37F2` are always 0: they come from stub `0x137ac`, probably a removed 4-player adapter `[?]`. Default `src = 00 01 02 03`.
The whole read is skipped if `DS:37E4 == -1`; that value is set by stub detection `0x135f6`/`0x13623`.
In demo playback (`DS:26`≠0), `0x1365a` is not called. Instead 4 bytes per frame are copied from `*(DS:0C)` into `input` of players 0..3, until a 0xFF byte.

### DYNA.CFG (28 bytes, load `0x13a31`, save `0x13aa0`, filename at `DS:3815`)
`[0..11]` kbd set 1 → `DS:205D` · `[12..21]` kbd set 2 → `DS:2069` · `[22..25]` source map → `DS:37FC` · `[26]` → `DS:24` (an option byte `[?]`) · `[27]` unused.

## 4. Coordinates

Tiles are **16×16 px**. The map lives at `DS:0726`, row-major, width `DS:26F4` (15 in battle; 13 rows).
```
tile2pix(col,row) (0x13902):  x = col*16 + 16 ;  y = row*16 + 32      // tile centre reference
pix2tile(x,y)     (0x1394f):  col = (x-8)>>4 ;   row = (y-24)>>4     // = nearest tile
tile(col,row)     (0x13962):  map[row*W + col]
```
Tile (c,r) therefore covers x ∈ [16c+8, 16c+23] and y ∈ [16r+24, 16r+39].
* The player sprite is drawn at (x−11, y−13) (`0x13d63`).
* Sprite number = `word[DS:3B7A+2*anim] + word[DS:3E4C+2*id] + facing`.
  * `DS:3B7A` = 0,1,0,2: the walk cycle (stand, step A, stand, step B).
  * `DS:3E4C` = 0, 20, 40, 60: each player has a bank of 20 sprites, where 0–11 are walk sprites and 12–19 are death sprites.

## 5. Movement (`0x13b71` → `0x13c59` per player → `0x14167`)

`DS:3A54` = **global speed in 8.8 px/tick, shared by all players**.
* Battle: reset to **0x300 (3 px/tick ≈ 55 px/s)** before each player is processed. Skull diseases change it: slow = 0x100, fast = 0xC00 (12 px/tick).
* Story: starts at 0x240 (2.25 px/tick). Each speed item ('i') steps it 0x240 → 0x300 → 0x3C0 (max). The value is saved and restored across a life loss (`DS:4F76`).
* Battle has **no speed power-up**.

```c
void player_tick(P *p) {                       // 0x13c59
  if (p->x == -1 || p->anim < 0) return;       // gone / dying (death anim handled in 0x13d63)
  if (p->animcnt < 0) { exit_door_anim(); return; }   // story only
  disease_tick(p);                             // 0x13e96
  if (mode == BATTLE) {
    speed = 0x300;
    switch (p->flags) {                        // disease type
      case 0: speed = 0x100; break;            // slow
      case 1: speed = 0xC00; break;            // ultra fast
      case 2: p->input &= 0x0F; break;         // can't bomb (low byte: clears 0x10,0x20)
      case 3: p->input ^= (frameToggle /*DS:22*/ & 0x10); break; // bomb bit flips every frame -> diarrhoea
    }
  }
  handle_input(p);                             // 0x14167
  pickup_item(p);                              // 0x13efe (tile under pix2tile(x,y))
}

void handle_input(P *p) {                      // 0x14167 — bomb first, then ONE direction
  if (p->input & 0x10) place_bomb(p);          // 0x141e0
  if      (p->input & 1) { move(p, UP);    p->facing = 9; }   // 0x147e6 (yacc)
  else if (p->input & 2) { move(p, DOWN);  p->facing = 0; }   // 0x14811 (yacc)
  else if (p->input & 4) { move(p, LEFT);  p->facing = 6; }   // 0x147bb (xacc)
  else if (p->input & 8) { move(p, RIGHT); p->facing = 3; }   // 0x14790 (xacc)
  else { DS_3A5D = 0; p->anim = 0; p->animcnt = 0; }          // idle
  // after a move call, anim_step(p) is called once more (so steps+1 per tick)
}

void move(P *p, dir) {                         // 0x14790/47bb/47e6/4811
  acc = (dir is LEFT/RIGHT) ? &p->xacc : &p->yacc;
  *acc += speed;  n = *acc >> 8;  *acc &= 0xFF;          // whole pixels this tick
  while (n--) {                                          // (n==0 would loop 65536; speed >= 0x100 so never)
    look(p);                                             // 0x14073
    step1(p, dir);                                       // 0x1483c (R) 0x14897 (D) 0x148f2 (L) 0x1494d (U)
    anim_step(p);                                        // 0x1476d
    if (story_exit_check(p)) return;                     // 0x14107, always false in battle
  }
}

void look(P *p) {                              // 0x14073: 3x3 neighbourhood + centre offsets
  (c,r) = pix2tile(p->x, p->y);
  NW=t(c-1,r-1) N=t(c,r-1) NE=t(c+1,r-1) E=t(c+1,r) SE=t(c+1,r+1) S=t(c,r+1) SW=t(c-1,r+1) W=t(c-1,r);
  dx = ((p->x - 16 + 8) & 15) - 8;   // -8..7, 0 = horizontally centred on tile
  dy = ((p->y - 32 + 8) & 15) - 8;   // -8..7
}

// One pixel step to the RIGHT (0x1483c). The other three directions are exact rotations:
//   DOWN : swap roles x<->y, ahead=S, diagonals SW (dx<0) / SE (dx>0)
//   LEFT : ahead=W, diagonals NW (dy<0) / SW (dy>0), x--
//   UP   : ahead=N, diagonals NW (dx<0) / NE (dx>0), y--
void step_right(P *p) {
  if (dx != 0 || passable(p, E)) {      // off-centre along the move axis => no check at all
    if (dy < 0) p->y++; else if (dy > 0) p->y--;   // pull 1px toward row centre
    p->x++;
    return;
  }
  // centred in x and the tile ahead is blocked: corner sliding
  if (dy < 0) { if (passable(p, NE)) p->y--; }   // slide further up, into the free row above
  else if (dy > 0) { if (passable(p, SE)) p->y++; }
  // dy == 0 -> stuck
}
```
Consequences for a faithful port:
* The block check happens **only when the player is exactly centred on the movement axis** (dx==0 for horizontal moves). After that the player moves freely until the next centre. This is also how a player walks off a bomb they just placed: only neighbour tiles are checked, never the current tile.
* **Cornering:** if the way ahead is blocked and the player is off-centre perpendicular to the move (by 1..8 px), they slide 1 px per step away from the centre, toward the diagonal tile on that side, as long as that diagonal is passable. Crossing the tile boundary flips the sign of the offset, so the next steps pull them onto the new row/column and the move continues. If the way ahead is free, the perpendicular offset is corrected toward the centre by 1 px per pixel moved.
* Walking animation (`0x1476d`): `if (animcnt>=0 && ++animcnt >= 10) { animcnt=0; anim=(anim+1)&3; }`, called once per pixel stepped plus once per tick (even when blocked).

### Passability (`0x149a8`)
`passable(t)` is true if `t=='.'`, `t=='-'`, or `t` is in `'a'..'u'` (items/exit can be walked onto).
**Story only:**
* wall-pass flag (flags&1) also allows `'A'..'V'` (soft blocks)
* bomb-pass flag (flags&2) also allows `'+'` (bomb)

Everything else blocks: `'*'` hard wall, soft blocks `'A'..'V'`, bomb `'+'`, and other codes.
Players never block each other. In battle there is no wall-pass and no bomb-pass.

### Item pickup (`0x13efe`)
Uses the tile at `pix2tile(x,y)`, checked once per tick after moving. The tile is then cleared to `'.'` and a sound plays.
* Battle:
  * `'a'` → maxBombs++
  * `'b'` → fire++
  * `'c'` → skull: diseaseTimer = 0xB4 (180 ticks ≈ 10 s), flags = rand()%4 (`0x139ac`)
* Story:
  * `'g'` → bombs+1, `'f'` → fire+1
  * `'k'` → wall-pass, `'j'` → bomb-pass, `'h'` → remote, `'l'` → fire-proof
  * `'i'` → speed step
  * `'m'` → extra life (`DS:4F8C`++)
  * `'s'` → +300 to `DS:3A50`; `'o'`/`'p'` → +100 (score `[?]`)
  * `'u'` → exit. It just sets `DS:3A5E` if no enemies are left (`DS:4200==0`) and is not cleared.
  * Story exit (`0x14107`): when on the exit, exactly tile-centred and `DS:3A5E` set, the player enters the door: x−=11, y−=13, animcnt=−1, flags|=8, round end.

### Disease (battle) — `0x13e96`, `0x13be3`
* `diseaseTimer` decrements each tick. Every 4 ticks it triggers a palette blink (`0x104c7`, table `DS:3FAC`). At 0 it sets flags=−1 and speed=0x300.
* Transmission (`0x13be3`, only if `DS:12`≠0, which seems to be the "skulls enabled" option `[?]`): a diseased player standing in the same tile (`pix2tile`) as a healthy player of a different id passes on the disease. The healthy player gets flags = carrier's flags and timer = 0xB4.

## 6. Bomb placement (`0x141e0`, triggered by input bit 0x10 each tick, level-triggered)
```c
slot = first of 8 bomb slots (DS:3CDE, stride 0x16) with word0 == -1;  if none return;
(c,r) = pix2tile(p->x, p->y);            // nearest tile, player need not be centred
if (tile(c,r) != '.') return;            // can't stack / place on items
if (p->bombsOut == p->maxBombs) return;
p->bombsOut++;  tile(c,r) = '+';
slot: [0]=screen addr, [2]=0, [4]=0, [6]=fuse 0xD2, [8]=(r<<8)|c, [0x12]=p->id, [0x14]=DS:3A4E++ (placement order)
story && P0 has remote (DS:3C74 & 4): fuse = 0x7FFF (infinite)
```
The fuse is decremented by `DS:1E`=4 per tick, so 210/4 → explodes after **53 ticks ≈ 2.9 s**.
Remote (story): P0's button B (`input&0x20`) detonates the bomb with the lowest placement number (`0x144bf`).
Holding the bomb button drops a bomb as soon as conditions allow, e.g. right after stepping into a new empty tile.

## 7. Death (`0x151bb` → `0x15295` flames, `0x15241` enemies)

Checks run each tick for every live player with anim ≥ 0:
* **Flames** (`0x15295`): dies if `pix2tile(x,y)` equals any tile in the current explosion tile list (`DS:3E54 .. DS:3A52`, built during explosions). The hitbox is therefore "the tile you are mostly in". In story, P0 with fire-proof (flags&8) is immune.
* **Enemies** (`0x15241`): dies if `pix2tile(x,y) == pix2tile(enemy.x+8, enemy.y+8)` for any live enemy (15 slots at `DS:4D50`, stride 0x24). Battle mode also spawns enemies (`0x14aca`, table `DS:42B4` by player count) `[?]` — the enemy notes have the details.

On death: anim = −1, animcnt = 0, sound 0 (`0x16dd1`).

Death animation (`0x13d63`):
* Each tick: `animcnt += 4; if (animcnt >= 9) { animcnt = 0; anim--; }`, so the frame changes every 3 ticks.
* Sprite = `word[DS:3B5E + 2*(-anim)] + bank` → sequence 12,13,12,13,12,13,14,15,16,17,18,19, then 0xFFFF.
* At the end x = −1 (removed). Total ≈ 12×3 = 36 ticks ≈ 2 s. Drawn at (x−11, y−13).
* Story fire-proof players are drawn flickering (`0x109e6` when `tick&2==0`).

Round end (`0x103b2`):
* While anyone is dying, nothing happens.
* Once ≤1 player is left, all flags are set to 2 (no bombing) and the game waits for all bombs to clear.
* Then a short timer runs (`DS:0A`). `DS:0E` = winner id, or −1 for a draw.
