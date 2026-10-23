# Dyna Blaster (DOS) – map cells, bombs, explosions, blocks, items

Source: `build/DYNA_UNP.EXE`, confirmed against the assembly (`build/rea/bombs/all.asm` is a dump of every procedure).
Addresses: `0x1XXXX` = code (`FUN_1000_XXXX`). `DS:NNNN` = data segment 0x1965.
"Frame" = one main-loop iteration. The loop waits on flag `DS:204F`, which the INT 1Ch handler at
`1000:1355` sets on every timer tick. The PIT is left at its default rate, so a frame is ≈ 1/18.2 s (55 ms).
Global `DS:1E` = 4 (it is never written) and is the per-frame step that most timers add or subtract.
Lines marked **(?)** are uncertain.

---------------------------------------------------------------------------------------------------
## 1. Map storage & coordinates

* Live map: `DS:0726`, 1 byte per cell, row-major, stride `W = [DS:26F4]`, height `H = [DS:26F6]`
  (buffer 0x1B8 = 440 bytes). Battle: 15×13. Story: type 0 = 15×13, type 1 = 31×13, type 2 = 15×29 (`0x13044`).
* A cell is packed in a word: `AL = x`, `AH = y`. The offset is `y*W + x`. Read: `0x13962`. Write (cell = DL): `0x138E4`.
* Cell → pixel (centre) `0x13902`: `px = x*16 + 16`, `py = y*16 + 0x28`.
  Pixel → cell `0x1394F`: `x = (px-8)>>4`, `y = (py-0x18)>>4`.
  Player structs keep centre-pixel coordinates, so a player's "cell" is the cell under their centre.

### Runtime cell encoding (ASCII)

| byte | meaning |
|---|---|
| `*` (and any other char not listed) | hard/indestructible. Story templates also use decor chars `0-9 = # ) \| ( / % & 0x94 0x84 0x81`, which are all solid |
| `.` 0x2E | empty floor |
| `+` 0x2B | bomb (ticking) |
| `-` 0x2D | centre of an exploding bomb (flame stops here, but players can walk on it) |
| `A`..`T`, `U` 0x41–0x55 | soft (destructible) block. The letter says what it turns into (table below) |
| `V` 0x56 | soft block in the middle of crumbling. Blocks movement and flames |
| `a`..`t` 0x61–0x74 | power-up lying on the floor (walkable, picked up, burnt by flames) |
| `u` 0x75 | exit door (walkable; not burnt) |

Soft-block → result table `DS:3A80` (4 bytes per letter: `[result char, crumble-anim ptr]`, index = char−'A'):

| block | result | crumble anim (tiles, 2 frames each) | used in |
|---|---|---|---|
| `A` | `.` | 3,4,5,6,7,8,0,0 (8 steps = 16 fr) | battle plain block |
| `B` | `a` bomb-up | 0xC×3 (6 fr) | battle |
| `C` | `b` fire-up | 0xD×3 | battle |
| `D` | `c` skull | 0xE×3 | battle (only if skull option on) |
| `E` | `.` | 0x85..0x8B,0x69,0x69 (9 steps = 18 fr) | story plain block |
| `F` | `f` fire-up | 0x91×3 | story |
| `G` | `g` bomb-up | | story |
| `H` | `h` remote control | | story |
| `I` | `i` speed-up | | story |
| `J` | `j` bomb-pass | | story |
| `K` | `k` wall-pass (walk through soft blocks) | | story |
| `L` | `l` flame-proof (stage only) | | story |
| `M` | `m` 1-UP | | story |
| `N`..`T` | 0 (unused) | – | |
| `U` | `u` exit door | 0xA0×3 | story |
| `V` | `.` | | |

Battle tile mapping (`DS:2747`, char list `DS:2740` = `"*.ABCD"`): `*`→tile 1, `.`→0, `A`/`B`/`C`/`D`→2. Hidden
items look exactly like plain blocks.

---------------------------------------------------------------------------------------------------
## 2. RNG (`0x1399B` seed, `0x139AC` rand)

```c
// seed, called once at program start (0x1002F)
DOS_GetTime(&h,&m,&s,&hs);               // INT 21h AH=2Ch: CH=h CL=m DH=s DL=hs
rng_idx /*DS:37CC*/ = (hs ^ h ^ m ^ s) & 0xFF;   // in practice always < 128

uint16 rand(uint16 n) {                  // returns 0..n-1
    uint8 *p = &DS[0x38BB + rng_idx];    // table: 130 initial bytes at DS:38BB, mutated in place
    uint16 v = p[0] | p[1]<<8;
    rng_idx = (rng_idx + v + 1) & 0x7F;
    uint16 x = p[1] | p[2]<<8;           // unaligned word at p+1, read before the write
    p[0] ^= x & 0xFF;  p[1] ^= x >> 8;   // i.e. *(u16*)p ^= *(u16*)(p+1)
    return v % n;
}
```
Every random draw in the game (map setup, skull effect, enemy AI, enemy spawns, bonus placement) goes through
this one generator.

---------------------------------------------------------------------------------------------------
## 3. Map randomisation (`0x13219`, helper `0x132BD`)

`0x132BD(count CX, DL=newChar, DH=matchChar)`: does the following `count` times: pick a random cell until it
holds `DH`, then write `DL` there. The return value AX is the last cell picked.
```c
void scatter(int count, char newc, char match) {
    while (count--) {
        do { y = rand(H+1); x = rand(W+1); }          // NOTE: W+1 / H+1 -> may pick x==W or y==H
        while (map[y*W + x] != match);                 // x==W aliases (0,y+1) which is always '*';
        map[y*W + x] = newc;                           // y==H reads past the map (stale buffer tail) (?)
    }
}
```
**Battle** (`[DS:4F88]==0`). The template `DS:0046` (15×13) is copied to `DS:0726` first (`0x100A3`). Every
non-corner inner cell is `A`, which gives 101 `A` cells. The counts come from `DS:2704 + (players-2)*8`
(4 words):

| players | `A`→`.` | `A`→`B` (bomb) | `A`→`C` (fire) | `A`→`D` (skull, only if `[DS:12]`≠0) | plain blocks left |
|---|---|---|---|---|---|
| 2 | 44 | 6 | 6 | 4 | 41 (45 without skull) |
| 3 | 30 | 6 | 6 | 4 | 55 (59) |
| 4 | 22 | 8 | 8 | 6 | 57 (63) |

Order: the `.` removals are done first, then B, then C, then D.
Demo mode then overwrites the map with 0x1B8 bytes from `DEMO.DAT` (`0x139D7`).

**Story**. The level record is at `[DS:4F78]` = `*(DS:[DS:4FBC + world*2] + 2 + stage*2)`:
`+0` map type, `+2` number of `E` blocks to clear, `+4` hidden power-up letter (or −1), `+6` exit letter (`U`, or −1),
`+8` enemy list.
```c
scatter(rec[1], '.', 'E');
soft_left /*DS:4F6E*/ = total_E[type] /*DS:34B6: 110,246,270*/ - rec[1];
hidden_item_pos /*DS:4F92*/ = 0;
if (rec[2] != -1) hidden_item_pos = scatter(1, rec[2], 'E');   // e.g. 'F','G','H'..'M'
if (rec[3] != -1) scatter(1, rec[3], 'E');                     // 'U' = exit door
```
Each stage has exactly one hidden power-up and one exit. Stage 8 of each world is the boss stage (all blocks
cleared, no item, no exit). Examples: 1-1 → `F`/`U`, clear 78 of 110. 1-2 → `G`. 1-3 → `I`. 1-4 → `H`.
1-5 → `J`. 1-6 → `M`. 1-7 → `K`. 2-2 → `L`. (The full list is easy to dump from `DS:4FBC`.)

---------------------------------------------------------------------------------------------------
## 4. Bombs

### Table `DS:3CDE`: 8 slots × 0x16 bytes, shared by all players (hard cap of 8 bombs on the field)

| off | meaning |
|---|---|
| +0x00 | screen address (DI); **0xFFFF = slot free** |
| +0x02 | ticking: anim frame 0..3. Exploding: −1 … −11 (phase = −value) |
| +0x04 | anim sub-counter (ticking: +4/frame, wraps at ≥15. Exploding: +6/frame, wraps at ≥5) |
| +0x06 | fuse (signed). Explodes when it goes **negative** |
| +0x08 | cell (AL=x, AH=y) |
| +0x0A/+0x0C/+0x0E/+0x10 | arm lengths up / down / left / right (encoding in §5) |
| +0x12 | owner player index (`player+0x18`). The owner struct is `DS:[3F9E + idx*2]` |
| +0x14 | serial number from `DS:3A4E++` (remote control detonates the oldest first) |

### Player fields that matter here (struct `DS:3C5E + i*0x20`)
`+0 px, +2 py, +8 death anim (<0 = dying), +0xC max bombs, +0xE bombs on field, +0x12 fire range, +0x14 skull
timer (battle), +0x16 battle: skull type (0xFFFF = none) / story: ability bits, +0x18 player index, +0x1A input bits
(1,2,4,8 directions, 0x10 bomb, 0x20 detonate), +0x1E battle wins`.
Battle start (`0x101CB`): bombs 1, range 2, speed 0x300. Story start (`0x10044`): bombs 1, range 1, speed 0x240.

### Placing a bomb `0x141E0` (bomb button, bit 0x10 of `+0x1A`, handled in `0x14167`)
```c
slot = first bomb with [0]==0xFFFF; if none return;
c = cell under player centre;  if (map[c] != '.') return;
if (p->maxBombs == p->bombsOut) return;   p->bombsOut++;
map[c] = '+';
slot = { scr(c), frame 0, sub 0, fuse 0xD2 /*210*/, c, owner=p->idx, serial=serial++ };
if (story && (player0.abilities & 4 /*remote*/)) slot.fuse = 0x7FFF;
```

### Per-frame bomb update `0x144BF` (runs once a frame, before players and enemies move)
```c
flameList = DS:3E54; flameEnd = flameList;              // reset the flame-cell list each frame
for each used slot b:
    b.fuse -= 4;                                        // [DS:1E]
    if (b.fuse < 0)        { explode(b); continue; }     // 0x145AC
    if (b.frame < 0)       { explodingTick(b); continue; }  // 0x1461E
    b.sub += 4; if (b.sub >= 15) { b.sub = 0; b.frame = (b.frame+1)&3; }
    draw tile {0,1,0,2}[b.frame] + 9 (+0x84 in story)   // DS:3B7A
// remote control (story): after the loop, if player0 has ability 4 and input bit 0x20 is held:
//   the bomb with the lowest serial gets fuse=-1, serial=0x7FFF  -> it explodes on the next frame
```
* **Fuse**: 210 − 4·n < 0 → explosion on the **53rd frame** after the bomb is placed (≈ 2.9 s).
  A remote bomb has fuse 0x7FFF, which is effectively infinite (≈ 8191 frames).
* **Chain reaction**: when a flame reaches a cell holding `+`, that bomb's fuse is set to **4**. It goes off once
  its fuse drops below 0: on the next frame if its slot comes after the exploding bomb in the table, otherwise
  one frame later.
* The owner gets the bomb back (`bombsOut--`) at the moment of the explosion, not when the flames end.

---------------------------------------------------------------------------------------------------
## 5. Explosion

### Start `0x145AC`
```c
b.sub = 0; b.frame = -1; b.fuse = 0x7FFF;
owner->bombsOut--;
map[b.cell] = '-';
range = owner->fireRange;                  // read now, from the owner's struct (+0x12)
b.armR = arm(b.cell, +1,     range);       // order: right, down, left, up
b.armD = arm(b.cell, +0x100, range);       // (step is added bytewise: AL += dx, AH += dy)
b.armL = arm(b.cell, +0xFF,  range);
b.armU = arm(b.cell, +0xFF00,range);
sound(4);
```
Arm lengths are computed **once**, when the bomb goes off. Every side effect (destroying blocks, chaining bombs,
burning items, spawning enemies) also happens only at that moment.

### Arm propagation `0x1427E`
Return value: `-range` if the arm ran its full length over empty floor. Otherwise `k` ≥ 0 = the number of empty
cells crossed before the obstacle (the obstacle cell itself is not part of the flame).
```c
int arm(cell c, step d, int range) {
    for (int k = 0;;) {
        c += d; char t = map[c];
        if (t == '.') { if (++k == range) return -k; continue; }
        if (t == '+') { bomb_at(c).fuse = 4; return k; }                    // chain
        if (t >= 'A' && t <= 'T') { destroySoft(c, t); return k; }
        if (t == 'U') { if (story && soft_left == 1) spawnBonus('s', tile 0x9E);
                        destroySoft(c, t); return k; }
        if (t >= 'a' && t <= 't') { burnItem(c); return k; }               // item stops the flame
        if (t == 'u') {                                                     // exit door
            if (player0.anim(+0xA) >= 0) {
                type = {9,5,3,4}[rand(4)];  spawnEnemies(c, type, 4);        // 0x14A38
                if (hidden_item_pos) drawTile(hidden_item_pos, 0x6B);       // (?) hint graphic
            }
            return k;
        }
        return k;          // '*', decor, '-', 'V' and anything else
    }
}
void destroySoft(cell c, char t) {            // 0x1436D
    if (story && soft_left == 2 && !enemyKilledThisStage /*DS:4F94*/) spawnBonus('o', tile 0x9A);
    soft_left--;
    if (c == hidden_item_pos) hidden_item_pos = 0;
    s = free slot in crumble table DS:3D8E (8 × 10 bytes); if (!s) return;  // table full -> block stays as is!
    s = { scr(c), animPtr = DS:3A80[(t-'A')*4+2], cnt=0/period=5, cell=c, idx=t-'A' };
    map[c] = 'V';
}
void burnItem(cell c) {                       // 0x14400
    s = free slot in DS:3DFE (3 × 0x1A bytes); if (!s) return;   // full -> item survives
    s = { px-0xE, py-0x13, animPtr=DS:3A60, cnt=0, cell=c };    // map cell keeps the item char while it burns
}
spawnBonus(ch, tile): pick a random '.' cell that no enemy is standing on (0x152F6 / 0x1514B), write ch, draw tile.
```

### Flame lifetime and damage `0x1461E` (each frame while `b.frame < 0`)
```c
b.sub += 6; if (b.sub >= 5) { b.sub = 0; b.frame--; }   // fires every frame (the code loads [1E] and then overwrites it with 6)
if (b.frame < -11) { free slot; map[b.cell] = '.'; return; }
phase = -b.frame;                                        // 2..11
draw centre tile 0x34 + DS:3B82[phase]                   // phase 2..11 -> 36,35,34,34,35,35,36,38, then blank
for each dir: addFlameCells(b.cell, step, armLen);       // 0x14A18: |len|+1 cells, centre included
              drawArm(dir, armLen, phase);               // 0x146F6
```
* Total: the explosion frame itself (phase 1, nothing drawn, no damage), then **10 frames of damage** (phases
  2–11), then the slot is freed. That is ≈ 0.6 s.
  Phases 10–11 already draw blank tiles but still deal damage.
* `drawArm`: `len ≥ 0` → `len` middle pieces, no end cap (arm blocked). `len < 0` → `|len|−1` middle pieces + one
  end cap (arm at full range). Tile table `DS:3B9E + dirOff + phase*4` gives (middle, end) pairs.
  dirOff: up 0x00, down 0x30, left 0x60, right 0x90. Per phase 2..11 the size is 2,3,4,4,3,3,2,1,blank,blank.
  - up: middle 0x2C–0x2F, end 0x1C–0x1F
  - down: middle 0x2C–0x2F, end 0x24–0x27
  - left: middle 0x30–0x33, end 0x28–0x2B
  - right: middle 0x30–0x33, end 0x20–0x23
* Flame cells are **not** written into the map. Only the centre becomes `-`. Arm cells stay `.`, so you can walk
  into them.
* Damage uses the per-frame list `DS:3E54..[DS:3A52]` (cell words):
  - players: `0x15295`. If the player's centre cell is in the list → `+8 = -1` (dying), sound 0.
    Story player 0 with ability bit 8 (flame-proof) is skipped. Touching an enemy (`0x15241`) still kills.
  - enemies: `0x14D0A`, same cell test. Score `DS:422E[type]` plus a multi-kill bonus `DS:4202`.
    Enemies are covered in their own document.

### Crumbling blocks `0x13B08` (each frame)
`cnt += 4`. When `cnt ≥ 5` the anim pointer moves one entry, so each tile lasts 2 frames.
When the entry is 0xFFFF: `map[cell] = DS:3A80[idx*4]` (the result char from §1) and the slot is freed.
Durations: battle `A` 16 frames. `B`/`C`/`D`/story items 6 frames (the item tile shows straight away).
Story `E` 18 frames. While crumbling the cell is `V`, which blocks flames and movement (unless the player has wall-pass).

### Burning items `0x14445` (each frame)
`cnt += 4`. When `cnt ≥ 8` the next anim entry is used (7 entries at `DS:3A60` → 14 frames).
Then the cell is set to `.` and floor is drawn.

---------------------------------------------------------------------------------------------------
## 6. Power-ups (pickup `0x13EFE`, cell under the player's centre must be in `a`..`u`)

After a pickup the cell becomes `.`, floor is drawn and sound 1 plays (except for the exit door).
**No caps** are checked on bombs or fire. The only practical bomb cap is the 8 shared slots.

**Battle** (`[DS:4F88]==0`):

| item | effect |
|---|---|
| `a` | bomb up: `+0xC++` |
| `b` | fire up: `+0x12++` |
| `c` | skull: `+0x14 = 0xB4` (180 frames ≈ 10 s), `+0x16 = rand(4)` |

Skull effects (`0x13C59`, applied every frame before input is handled; speed in `DS:3A54`, normally 0x300):
* 0: slow (0x100)
* 1: very fast (0xC00)
* 2: can't drop bombs (input &= 0x0F)
* 3: drops bombs constantly (bomb bit XORed with a 0x00/0x10 value that flips every frame)

`0x13E96` counts the timer down (palette flash every 4 frames). At 0 the effect is cleared (`+0x16 = 0xFFFF`).
Infection `0x13BE3` (only when the skull option `[DS:12]` is on): an infected player who shares a cell with a
healthy player gives them the same skull type with a fresh 0xB4 timer.
Note: players start a battle with `+0x16 = 2, +0x14 = 1`. The timer clears the effect on the first frame.

**Story** (speed values are px/frame × 256):

| item | effect |
|---|---|
| `g` | bomb up |
| `f` | fire up |
| `i` | speed: 0x240 → 0x300 → 0x3C0 (max) |
| `h` | remote control (ability bit 4): bombs get fuse 0x7FFF, detonate button (input bit 0x20) sets off the oldest |
| `j` | bomb-pass (bit 2): can walk over `+` |
| `k` | wall-pass (bit 1): can walk through `E`, `A`..`V` |
| `l` | flame-proof (bit 8): immune to flames, sprite blinks. Bit is cleared at every stage start (`0x15440`) |
| `m` | 1-UP: `DS:4F8C++` (lives) |
| `o`, `p` | `DS:3A50 += 100` (score is shown ×100 → 10 000 pts) |
| `s` | `DS:3A50 += 300` (30 000 pts) |
| `u` | exit door. Only works when the enemy count `DS:4200 == 0`. `DS:3A5E = 0xFF`, then `0x14107` needs the player exactly on the tile centre → `[DS:23] = 0xFF` stage clear |

Bonus items that are not hidden in blocks:
* `s`: the exit block `U` is destroyed while it is the last soft block left.
* `o`: the second-to-last soft block is destroyed and no enemy has been killed yet this stage.
* `p` (?): only in world 1-1 (`0x13B71`). If the player stands still for 0xCA8 = 3240 frames (~3 min), a `p` appears
  at a random empty cell (looks like an easter egg).

Story abilities (`+0x16`) carry over between stages. On death (`0x1567B`) bombs, fire and speed go back to the
values saved at stage start (`DS:4F72/74/76`), every ability bit is cleared, and a life is lost.

Movement passability (`0x149A8`; a neighbour cell counts as passable if this returns `.`):
`.`, `-`, `a`..`u` always. `+` with bomb-pass. `E`/`A`..`V` with wall-pass.
A player can always step off the bomb they are standing on.

---------------------------------------------------------------------------------------------------
## 7. Scoring for blocks
Destroying a soft block gives **no points** (`0x1436D` has no score code).
Points come from enemies (`DS:422E` × 100: 100, 200, 400, 800, 1000, 3000, 6000, …) and from the `o`/`p`/`s`
bonuses. The score display `0x1334E` prints `[DS:3A50]` followed by "00".

---------------------------------------------------------------------------------------------------
## 8. Battle mode flow & end conditions (`0x100A3`, `0x103B2`, ISR `1000:1355`, results `0x11C11`)

* Settings:
  - players `[DS:10]` = 2..4 (menu `0x1183B`)
  - wins needed `[DS:14]` = 1..5 (menu `0x1170D`, wraps around)
  - skull option `[DS:12]` = 0/1 (`0x1193C`)
* Spawn cells (`DS:36`, (x,y)): P0 (1,1), P1 (13,11), P2 (13,1), P3 (1,11).
* Round timer (minutes:seconds at `DS:37CE:37D0`, from `DS:2A + (players-2)*4`): 2P **3:00**, 3P **3:30**, 4P **4:00**.
  The ISR subtracts one second every 18 ticks. At 0:00 it sets `[DS:23] = 0xFF` and the round ends as a
  **draw** (`[DS:E] = -1`).
* Last player standing, `0x103B2` (every frame):
  1. Wait while any player is mid-death (`+8 < 0`).
  2. If 2 or more players are alive, keep playing.
  3. Otherwise `[DS:E]` = the survivor's index (−1 if nobody is left), and every player gets `+0x16 = 2`
     (no more bombs).
  4. Wait until all 8 bomb slots are free.
  5. Count 18 frames (`[DS:A]`, jingle at frames 2, 9 and 16), then end the round. The survivor can still die
     during the wait, which turns the round into a draw.
* Wins: the winner's `+0x1E` is incremented (`0x11DEA`). When it equals `[DS:14]` the match is over
  (`[DS:16] = 1`, then `0x119CE`). Otherwise a new round starts with a freshly randomised map, all stats reset,
  and wins kept.

---------------------------------------------------------------------------------------------------
## 9. Uncertain / not verified
* What tile 0x6B is (drawn over the still-hidden power-up when the exit door is bombed).
* `p` idle bonus: the condition is understood (world 1, stage 1 only), the purpose is not.
* Story stage time limit is 4:00 (`0x15450`), but what happens at time-out in story mode was not analysed.
* The `y==H` out-of-range pick in `scatter` reads the stale buffer tail. It normally holds no `A`/`E`, so it
  just retries.
