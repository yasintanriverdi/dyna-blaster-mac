# Dyna Blaster — story mode, level data & enemies

Source: `build/DYNA_UNP.EXE` (CS linear base 0x10000, DS=0x1965). Addresses `0x1XXXX` = `FUN_1000_XXXX`; `DS:xxxx` = data segment offset (= offset in `build/rea/dseg.bin`).
Everything below is checked against the assembly (`build/rea/story/asm_*.s`) unless marked **(?)**.
Helper scripts: `build/rea/story/pw.py` (password decoder, verified against all 64 DYNA.COD codes), `build/rea/story/tables.md` (generated tables).

## 0. Conventions / helpers

| helper | meaning |
|---|---|
| `0x1394f` | pixel→tile: `col=(x-8)>>4`, `row=(y-0x18)>>4` (returns AL=col, AH=row) |
| `0x13902` | tile→pixel *centre*: `x=col*16+16`, `y=row*16+32` |
| `0x13962` / `0x138e4` | read / write map char `DS:0x726 + row*W + col`, `W=DS:26f4` |
| `0x139ac` | `random(n)` → 0..n-1 (see RNG) |

Entity coords: player (x,y) are tile *centre* based (`0x13902` result); enemies/bosses store top-left = centre-8 (aligned enemy: `x=col*16+8`, `y=row*16+24`); enemy tile tests use `(x+8,y+8)` → `0x1394f`.

**RNG `0x139ac`** (port exactly to reproduce): `si=0x38bb+idx(DS:37cc); ax=word[si]; idx=(idx+ax+1)&0x7f; word[si]^=word[si+1] (unaligned byte+1); return ax % n` (`ax` read before the XOR). Seed table = 129+ bytes at `DS:38bb`.

**Map chars** (DS:0x726, `W×H` = 15×13 / 31×13 / 15×29):
`.` floor · `*` hard wall (template digits/border glyphs in list `DS:4fa0` = `0123456789\x94\x84|\x81#%&/()=` are drawn by `0x130bb` then turned into `*` by `0x157ec`) · `E` plain soft block · `F..M` soft block hiding item · `U` soft block hiding exit · `V` block being destroyed · `+` bomb · `-` flame · `a..m` revealed item (block char+0x20, via table `DS:3a80[(c-'A')*4]`; `A`/`E`→`.`) · `u` exit door · `o`,`s` bonus items.

## 1. Story mode flow — `0x1531a`

Entry (`0x10000`) before the menu: world `4f88=1`, stage `4f8a=1`, lives `4f8c=2` (extra lives), max bombs `3c6a=1`, fire `3c70=1`, speed `3a54=0x240`. Menu `0x12447` picks story (`26fc=0` → `4f88=1,4f8a=1`, intro `0x16ebe`), battle (`26fc=1` → `4f88=0`), password (`26fc=3`) or quit (`4f88=-1`).

```c
void story(void) {                       // 0x1531a
  continues(4f8e)=3; score(3a50)=0;
  load_gfx(5 /*gfx\04.img enemies*/); load_gfx(4 /*gfx\01.img*/);
next_world:
  if (stage==1) world_intro_map();        // 0x12285 (~0x6c frames or key)
  load_gfx(world_tbl[world].gfx);         // DS:4fbc[world] -> word0: 3,10..16 = gfx\06,07..13.img
retry:
  if (world==8 && stage==8) load_gfx(6 /*gfx\14.img final boss*/) else load_gfx(5);
  saved = {3c6a bombs, 3c70 fire, 3a54 speed};      // 4f72/74/76
  stageData(4f78) = *(world_tbl[world] + 2 + stage*2);
  stage_intro_screen();                  // 0x123a9 -> 0x13559
  set_scroll_mode();                     // 0x13044: 26f2=stageData[0]; W,H = 15x13 / 31x13 / 15x29; camera 203e=0x80
  player: tile (1,1), status flags 3c74=0, alive;
  timer 37ce:37d0 = 4:00, 204c=-1, timer frozen (byte 23=ff);
  memcpy(map, template[26f2] /*DS:1fe, 3b6, 56e*/, 0x1b8);
  place_blocks_items();                  // 0x13219
  draw_map(); walls_to_star();           // 0x130bb, 0x157ec
  enemy_count(4200)=0; spawn_enemies();  // 0x14aca (stage 8: + bosses 0x15a42)
  bonusFlag(4f94)=0;
  music = (stage&1)? 0x0c : (stage==8 ? (4f94=0xff, 0x0b) : 0x0d);
  timer runs (23=0, 204c=0); result(4f7c)=0;
  for (;;) {                             // per frame
    0d2b; 094e; 365a input; 402a; 44bf bombs; 3b08 block-break anims; 4445 burning items;
    57c0 (hint: if 4200==0 && hidden item block exists, redraw it) (?);
    3efe pickup(player); 4b73 enemies(+boss 5e9b on stage 8); 3b71; 111f camera; 583c HUD; 0c04;
    if (!debug(2050)) 51bb player_death_checks();
    else { keys '1'..'8' -> stage=n, result=3; F1..F8 -> world=n, result=4; }   // cheat
    if (result) break;
    if (player.x==-1 && ++4f7a >= 0x12) result=1;        // dead: 18 frames after death anim
    if (timer==0:00 && 204c==0x11) time_up();             // 0x1580b
    ...render...; if (key==0x19 /*P*/ && stage!=8) pause(); if (key==ESC) return;
  }
  fade();
  switch (result) {
   case 1:  // died
     restore saved bombs/fire/speed; 3c74=0 (lose wall/bomb-pass, remote, flame-proof);
     if (lives) { lives--; goto retry; }
     game_over_screen();                 // 0x12182: shows password (0x10fa2) + continue? (0x12e47)
     if (no || continues==0) return;     // back to menu
     score=0; continues--; lives=2; goto retry;
   case 2:  // stage cleared
     if (stage<8) { stage++; goto retry; }
     if (world<8) { world++; stage=1; goto next_world; }
     ending();  /*0x17002*/ return;
   case 3: goto retry; case 4: goto next_world;  // cheats
  }
}
```

* **Time limit**: 4:00 every stage. The timer is decremented in the timer ISR (code at ~`0x11360`, not a Ghidra procedure): `if(!byte23){ if(++204c==0x12){204c=0; sec--; if(sec<0){sec=59; min--; if(min<0){min=sec=0; 204c=-1; byte23=0xff;}}}}` (18 ISR ticks per second; PIT rate assumed default 18.2 Hz **(?)**). The main loop fires `time_up` once, on the 17th tick after the display reaches 0:00.
* **Time up `0x1580b`**: normal stage → *all* enemies are removed and **8 enemies of type 8** (fast wall-passing chasers) are spawned at random free `.` tiles not in the safe list `DS:4204` (corner tiles (1,1)(1,2)(1,3)(2,1)(3,1)(13,1)(12,1)(11,1)(13,2)(13,3)(13,11)(12,11)(11,11)(13,10)(13,9)(1,11)(2,11)(3,11)(1,10)(1,9) — col,row), each frozen 25 steps. Boss stage → player is killed (`3c66=-1`).
* **Clear condition**: walk onto `u` while `enemy_count(4200)==0` (`0x13efe` sets `3a5e`), and be tile-aligned (`0x14107`): player snaps into the door, gets flag 8 (invulnerable), timer stops; when the exit animation list (`3a46`) ends `0x13c59` sets `result=2`. Final stage 8.8 instead clears when all 5 final-boss parts have finished their death animation (`0x16558`: `6b70` counts 4→-1).
* **Lives**: `4f8c` = extra lives (starts 2). Only the `M` item gives +1; no score-based extra life was found. Death restores the bombs/fire/speed you had when entering the stage and clears status flags.
* **Continues**: 3 per game (`4f8e`); continuing zeroes the score and restarts the same stage with 2 extra lives.
* **Score**: `DS:3a50`, displayed with two extra `0` digits (×100), capped at 64000 (= 6,400,000); hi-score `4f90`.
* **Camera** (`0x1111f`): scroll mode 1 → `cam(203e)=player.x`, mode 2 → `cam=player.y`, clamped to [0x80,0x180]; no dead zone; only updated while player alive. Enemies are drawn only if within `cam-0x80 .. cam+0x80` (x, mode 1) or `cam-0x7c .. cam+0x6c` (y, mode 2) (`0x1516a`); they are simulated everywhere.

## 2. Level data

`DS:4fbc[world]` (word ptr, world 1..8) → `{ word gfx_index; word 0; word stage_ptr[8] }` (stage s at +2+2s).
Stage record: `{ int16 scroll_mode; int16 blocks_removed; int16 item_char; int16 exit_char; {int16 count, type}... ; -1 }`.

`place_blocks_items` `0x13219` (story):
1. `soft_blocks(4f6e) = Ecount[mode] - blocks_removed` where `Ecount = DS:34b6 = {110, 246, 270}` = number of `E` in each template.
2. Replace `blocks_removed` random `E` with `.` (`0x132bd`: pick `col=random(W+1)`, `row=random(H+1)` until the tile matches; note the +1).
3. If `item_char!=-1`, turn one random `E` into it; its position is stored in `4f92`. Then `exit_char` (`U`) the same way.
Templates: all have `E` in every free cell except the start corner (1,1),(2,1),(1,2) which are `.`.

Item chars: `F` fire+1 (`+0x12`), `G` bomb+1 (`+0xc`), `H` remote control (flag 4), `I` speed 0x240→0x300→0x3c0, `J` bomb-pass (flag 2), `K` wall-pass (flag 1, soft blocks), `L` flame-proof (flag 8), `M` 1UP. Pickup `0x13efe` (story branch). `o`/`p` = +100 (10,000 pts), `s` = +300 (30,000 pts).

| Stage | DS ptr | mode | soft blocks left | hidden item | exit | enemies (count x type) |
|---|---|---|---|---|---|---|
| 1.1 | 0x506e | 0 fixed 15x13 | 32 | F fire+1 | U | 3x t2 |
| 1.2 | 0x507c | 0 fixed 15x13 | 32 | G bomb+1 | U | 3x t2, 1x t6 |
| 1.3 | 0x508e | 0 fixed 15x13 | 32 | I speed | U | 2x t2, 1x t6 |
| 1.4 | 0x50a0 | 0 fixed 15x13 | 32 | H remote | U | 2x t2, 2x t6, 1x t4 |
| 1.5 | 0x50b6 | 2 V-scroll 15x29 | 66 | J bomb-pass | U | 2x t2, 2x t6, 2x t4 |
| 1.6 | 0x50cc | 2 V-scroll 15x29 | 66 | M 1UP | U | 2x t2, 2x t6, 1x t4 |
| 1.7 | 0x50e2 | 2 V-scroll 15x29 | 66 | K wall-pass | U | 2x t2, 2x t4, 1x t9, 1x t5 |
| 1.8 | 0x50fc | 0 fixed 15x13 | 0 | - | - | 2x t5 |
| 2.1 | 0x510a | 0 fixed 15x13 | 32 | F fire+1 | U | 2x t10, 2x t11, 1x t4 |
| 2.2 | 0x5120 | 0 fixed 15x13 | 32 | L flame-proof | U | 2x t10, 2x t11, 2x t4 |
| 2.3 | 0x5136 | 2 V-scroll 15x29 | 74 | I speed | U | 2x t10, 1x t9, 2x t4 |
| 2.4 | 0x514c | 2 V-scroll 15x29 | 74 | J bomb-pass | U | 2x t10, 2x t11, 1x t9, 1x t3 |
| 2.5 | 0x5166 | 1 H-scroll 31x13 | 60 | H remote | U | 2x t9, 2x t4, 1x t3, 1x t5 |
| 2.6 | 0x5180 | 1 H-scroll 31x13 | 60 | M 1UP | U | 2x t11, 2x t9, 2x t4, 1x t3, 1x t5 |
| 2.7 | 0x519e | 2 V-scroll 15x29 | 74 | K wall-pass | U | 4x t10, 2x t9, 2x t4 |
| 2.8 | 0x51b4 | 1 H-scroll 31x13 | 0 | - | - | 1x t3, 1x t5 |
| 3.1 | 0x51c6 | 2 V-scroll 15x29 | 74 | H remote | U | 1x t13, 1x t14, 2x t9, 2x t4, 1x t3 |
| 3.2 | 0x51e4 | 1 H-scroll 31x13 | 60 | G bomb+1 | U | 2x t13, 1x t9, 2x t4, 2x t3 |
| 3.3 | 0x51fe | 1 H-scroll 31x13 | 60 | L flame-proof | U | 1x t13, 2x t14, 2x t9, 1x t4 |
| 3.4 | 0x5218 | 2 V-scroll 15x29 | 74 | J bomb-pass | U | 1x t13, 2x t14, 2x t9, 1x t4 |
| 3.5 | 0x5232 | 1 H-scroll 31x13 | 60 | M 1UP | U | 2x t14, 3x t9, 1x t4, 1x t5 |
| 3.6 | 0x524c | 1 H-scroll 31x13 | 60 | I speed | U | 2x t13, 1x t14, 1x t9, 3x t4, 1x t5 |
| 3.7 | 0x526a | 2 V-scroll 15x29 | 74 | K wall-pass | U | 3x t13, 2x t14, 2x t9, 1x t4, 1x t3 |
| 3.8 | 0x5288 | 1 H-scroll 31x13 | 0 | - | - | 1x t3, 1x t5 |
| 4.1 | 0x529a | 2 V-scroll 15x29 | 84 | G bomb+1 | U | 2x t15, 2x t9, 2x t4, 1x t3 |
| 4.2 | 0x52b4 | 1 H-scroll 31x13 | 70 | M 1UP | U | 3x t15, 1x t16, 2x t9, 2x t4 |
| 4.3 | 0x52ce | 1 H-scroll 31x13 | 70 | J bomb-pass | U | 1x t15, 2x t9, 2x t4, 2x t3 |
| 4.4 | 0x52e8 | 2 V-scroll 15x29 | 88 | H remote | U | 2x t15, 1x t16, 3x t9, 2x t4, 1x t5 |
| 4.5 | 0x5306 | 2 V-scroll 15x29 | 88 | F fire+1 | U | 2x t15, 1x t16, 3x t9, 1x t4, 2x t5 |
| 4.6 | 0x5324 | 1 H-scroll 31x13 | 76 | J bomb-pass | U | 1x t15, 3x t16, 3x t9, 1x t4 |
| 4.7 | 0x533e | 1 H-scroll 31x13 | 76 | G bomb+1 | U | 5x t16, 3x t9 |
| 4.8 | 0x5350 | 1 H-scroll 31x13 | 0 | - | - | 2x t5 |
| 5.1 | 0x535e | 1 H-scroll 31x13 | 76 | K wall-pass | U | 2x t17, 3x t18, 2x t9, 1x t4 |
| 5.2 | 0x5378 | 1 H-scroll 31x13 | 76 | F fire+1 | U | 2x t17, 3x t18, 1x t9, 2x t4 |
| 5.3 | 0x5392 | 2 V-scroll 15x29 | 90 | L flame-proof | U | 3x t17, 3x t18, 1x t5, 2x t3 |
| 5.4 | 0x53ac | 1 H-scroll 31x13 | 81 | G bomb+1 | U | 3x t17, 3x t9, 2x t3 |
| 5.5 | 0x53c2 | 1 H-scroll 31x13 | 81 | H remote | U | 2x t18, 3x t9, 2x t4, 1x t3, 1x t5 |
| 5.6 | 0x53e0 | 2 V-scroll 15x29 | 95 | J bomb-pass | U | 2x t17, 1x t18, 3x t9, 3x t4 |
| 5.7 | 0x53fa | 1 H-scroll 31x13 | 81 | M 1UP | U | 4x t18, 3x t9, 1x t3, 2x t5 |
| 5.8 | 0x5414 | 1 H-scroll 31x13 | 0 | - | - | 1x t5, 1x t3 |
| 6.1 | 0x5426 | 1 H-scroll 31x13 | 81 | G bomb+1 | U | 2x t19, 3x t20, 3x t9, 2x t4 |
| 6.2 | 0x5440 | 2 V-scroll 15x29 | 95 | J bomb-pass | U | 3x t19, 2x t20, 3x t9, 2x t4 |
| 6.3 | 0x545a | 1 H-scroll 31x13 | 81 | F fire+1 | U | 1x t19, 3x t20, 3x t9, 1x t4, 2x t5 |
| 6.4 | 0x5478 | 2 V-scroll 15x29 | 105 | G bomb+1 | U | 2x t19, 2x t20, 3x t4, 1x t3, 2x t5 |
| 6.5 | 0x5496 | 2 V-scroll 15x29 | 105 | F fire+1 | U | 2x t19, 3x t9, 3x t4, 2x t5 |
| 6.6 | 0x54b0 | 1 H-scroll 31x13 | 91 | M 1UP | U | 4x t9, 4x t4 |
| 6.7 | 0x54c2 | 2 V-scroll 15x29 | 105 | H remote | U | 1x t19, 2x t20, 4x t9, 3x t4 |
| 6.8 | 0x54dc | 1 H-scroll 31x13 | 0 | - | - | (boss only) |
| 7.1 | 0x54e6 | 1 H-scroll 31x13 | 91 | G bomb+1 | U | 1x t21, 2x t22, 3x t9, 3x t4, 1x t5 |
| 7.2 | 0x5504 | 2 V-scroll 15x29 | 105 | M 1UP | U | 2x t21, 2x t22, 4x t9, 2x t4, 2x t3 |
| 7.3 | 0x5522 | 2 V-scroll 15x29 | 105 | J bomb-pass | U | 2x t21, 3x t22, 4x t9, 3x t4, 2x t3 |
| 7.4 | 0x5540 | 1 H-scroll 31x13 | 101 | H remote | U | 3x t22, 4x t9, 3x t4, 2x t5 |
| 7.5 | 0x555a | 1 H-scroll 31x13 | 101 | M 1UP | U | 5x t22, 4x t9, 2x t4, 2x t3, 1x t5 |
| 7.6 | 0x5578 | 1 H-scroll 31x13 | 101 | G bomb+1 | U | 3x t21, 2x t22, 4x t9, 3x t4, 2x t5 |
| 7.7 | 0x5596 | 2 V-scroll 15x29 | 115 | K wall-pass | U | 3x t21, 3x t22, 4x t4, 2x t5 |
| 7.8 | 0x55b0 | 1 H-scroll 31x13 | 0 | - | - | (boss only) |
| 8.1 | 0x55ba | 1 H-scroll 31x13 | 106 | M 1UP | U | 3x t11, 2x t2, 4x t9, 2x t4, 2x t3, 1x t5 |
| 8.2 | 0x55dc | 1 H-scroll 31x13 | 106 | F fire+1 | U | 3x t11, 2x t2, 1x t7, 3x t9, 2x t4, 3x t5 |
| 8.3 | 0x55fe | 2 V-scroll 15x29 | 120 | J bomb-pass | U | 2x t11, 2x t2, 1x t7, 2x t9, 3x t4, 3x t5 |
| 8.4 | 0x5620 | 1 H-scroll 31x13 | 111 | H remote | U | 3x t11, 2x t7, 2x t9, 2x t4, 2x t3, 3x t5 |
| 8.5 | 0x5642 | 1 H-scroll 31x13 | 116 | G bomb+1 | U | 3x t11, 2x t2, 2x t7, 2x t9, 3x t4, 1x t3, 2x t5 |
| 8.6 | 0x5668 | 2 V-scroll 15x29 | 135 | M 1UP | U | 3x t7, 3x t9, 3x t4 |
| 8.7 | 0x567e | 1 H-scroll 31x13 | 126 | J bomb-pass | U | 2x t11, 2x t2, 3x t7, 2x t9, 3x t4, 3x t5 |
| 8.8 | 0x56a0 | 1 H-scroll 31x13 | 0 | - | - | (boss only) |

(soft blocks left = blocks after step 2, including the item and exit blocks). Worlds' tileset: W1 `gfx\06.img`, W2..W8 `gfx\07..13.img`.

**Enemy spawn** `0x14aca`: for each (count,type): pick a random `.` tile with `random(W)`,`random(H)` that is not in the safe list `DS:4204` (`0x152f6`); enemy is aligned on that tile, `dir=0`(up), no freeze. Up to 15 enemy slots.

**Bonus items** (in the flame ray code `0x1427e`):
* when a soft block is destroyed while `soft_blocks==2` (i.e. it leaves one block) and **no enemy has died yet** this stage (`4f94==0`; also forced 0xff on x.8): an `o` (+10,000) appears on a random free tile.
* when the `U` exit block is destroyed while it is the **last** soft block (`soft_blocks==1`): an `s` (+30,000) appears on a random free tile.

**Bombing the exit door** (flame reaches `u`, and player not in exit animation): spawns **4 enemies at the door**, one type chosen per explosion by `random(4)` → {0:type 9, 1:type 5, 2:type 3, 3:type 4}, frozen for 25 steps (`dir=-25`). Repeats on every explosion that touches the door (limited by 15 slots). Revealed items hit by flames burn and disappear (`0x14400`/`0x14445`). Flames stop at soft blocks, items, the door and chain-detonate bombs.

## 3. Password system

Generated on the game-over screen (`0x10fa2`) from (world, stage, max bombs `3c6a`, fire `3c70`, speed==0x3c0) — i.e. the values saved at stage entry. Decoded by `0x10e26` when entered in the menu. Alphabet `DS:20cf`, 4 rows of 16 (row = character pair index):
`muroyagcqxskiwhb`, `wreckfvbyhoznagl`, `newslogimphjtvbq`, `nuclearhpstvzygk`.

Decode (exact):
```c
b[i] = idx(alpha[i], pw[2i]) | idx(alpha[i], pw[2i+1])<<4;      // i=0..3, low nibble first
for (k=3,m=b[0]&0xe0; k>=1; k--) { m<<=1; if (m&0x100) b[k]=~b[k]; }  // bit7->b3, bit6->b2, bit5->b1
if ((b[0]&0x18)!=0x10) fail;
repeat (b[0]&7) { v=(b[1]|b[2]<<8|b[3]<<16)>>1; store back b1..b3; }   // 24-bit shift right
field(maskTbl, byteTbl, n): r=0; for i<n: r=r<<1 | ((b[byteTbl[i]&3] & maskTbl[i])!=0);
world = field(20a3,20b9,4) (1..8)   stage = field(20a7,20bd,4) (1..8)
bombs = field(20ab,20c1,4)          fire  = field(20af,20c5,4)
speed = field(20b3,20c9,1) ? 0x3c0 : 0x300
check = field(20b4,20ca,1) must be 0   // encoder writes popcount(all set bits) in 5 bits there; decoder only tests its MSB
```
| field | masks (MSB first) | byte index |
|---|---|---|
| world | 08 04 10 04 | 1 3 2 2 |
| stage | 40 40 01 08 | 3 2 1 3 |
| bombs | 80 10 01 20 | 2 1 2 3 |
| fire | 10 08 02 02 | 3 2 3 1 |
| speed | 20 | 1 |
| check(5 bits) | 02 … (DS:20b4) | DS:20ca |

Encoder: clear b0..b3, OR in the fields (bit i of value → table entry n-1-i), `b0=random(2)` = shift amount, shift b1..b3 left by it (`0x110ae`), `inv=random(8)`, `b0|=inv<<5|0x10`, invert b1/b2/b3 for inv bits 0/1/2, emit chars `alpha[(pair<<4|nibble)&0x3f]`, low nibble first. So many passwords map to the same state. All 64 DYNA.COD codes decode to their LEVEL w.s (bombs/fire grow from 1/1 to 9/7, speed bit always 1).

## 4. Enemy struct (15 slots × 0x24 at `DS:4d50`)

| off | meaning |
|---|---|
| +0,+2 | x, y (top-left; aligned = `col*16+8`, `row*16+24`) |
| +4 | walk animation base ptr (type `+0`) |
| +6 | current animation ptr (4 dirs × {sprite,seg}, 16 bytes per frame, -1 terminated) |
| +8 | frame timer: low byte = frame length (type `+4`), high byte = accumulator |
| +0xa | direction 0=up 1=right 2=down 3=left; <0 = frozen counter (counts up to 0); after death: score index |
| +0xc | type (≥0), -1 = dying |
| +0xe | flags (type `+6`) |
| +0x10 | speed accumulator (high byte = steps this frame) |
| +0x12 | chase cooldown (in steps) |
| +0x14,+0x16 | current sprite |
| +0x18,+0x1a | 4, 0x12 (sprite size / hitbox?) |

Type table `DS:42d4 + type*16`: `+0 walk anim, +2 death anim, +4 frame len, +6 flags, +8 score base index, +0xa random-turn chance (0..100), +0xc,+0xe unused(0)`.
Flags: `0x01` passes soft blocks (`A..U`), `0x02` never pauses (no "stand still" option), `0x04` speed 2 px/step-frame, `0x04|0x08` speed 3, `0x10` chases player 1, `0x20` unused/unknown, `0x40` turns away from bombs/flames seen ahead, `0x80` passes bombs.

| type | walk anim | death anim | frame len | flags | speed px/step | wallpass | bombpass | chase | avoid bomb | no-pause | score base | turn % | uses |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 0x44a8 | 0x44ec | 20 | 0x03 | 1 | Y |  |  |  | Y | 0 | 21 | 0 |
| 1 | 0x4464 | 0x4508 | 20 | 0x02 | 1 |  |  |  |  | Y | 0 | 21 | 0 |
| 2 | 0x4524 | 0x4568 | 20 | 0x02 | 1 |  |  |  |  | Y | 1 | 0 | 26 |
| 3 | 0x457e | 0x45c2 | 10 | 0x07 | 2 | Y |  |  |  | Y | 3 | 21 | 31 |
| 4 | 0x45e0 | 0x4624 | 10 | 0x02 | 1 |  |  |  |  | Y | 10 | 21 | 102 |
| 5 | 0x4642 | 0x4686 | 10 | 0x07 | 2 | Y |  |  |  | Y | 10 | 21 | 48 |
| 6 | 0x46a4 | 0x46e8 | 20 | 0x02 | 1 |  |  |  |  | Y | 3 | 21 | 8 |
| 7 | 0x4706 | 0x474a | 6 | 0x07 | 2 | Y |  |  |  | Y | 2 | 21 | 12 |
| 8 | 0x4706 | 0x474a | 6 | 0x1f | 3 | Y |  | Y |  | Y | 2 | 21 | 0 |
| 9 | 0x4760 | 0x47a4 | 20 | 0x7e | 3 |  |  | Y | Y | Y | 11 | 51 | 116 |
| 10 | 0x47ba | 0x47fe | 14 | 0x02 | 1 |  |  |  |  | Y | 1 | 21 | 12 |
| 11 | 0x481c | 0x4860 | 20 | 0x02 | 1 |  |  |  |  | Y | 3 | 21 | 24 |
| 12 | 0x4876 | 0x497a | 20 | 0x03 | 1 | Y |  |  |  | Y | 0 | 21 | 0 |
| 13 | 0x4998 | 0x49dc | 14 | 0x02 | 1 |  |  |  |  | Y | 1 | 21 | 10 |
| 14 | 0x49fa | 0x4a3e | 20 | 0x02 | 1 |  |  |  |  | Y | 3 | 21 | 10 |
| 15 | 0x4a5c | 0x4aa0 | 10 | 0x02 | 1 |  |  |  |  | Y | 1 | 21 | 11 |
| 16 | 0x4abe | 0x4b02 | 14 | 0x02 | 1 |  |  |  |  | Y | 3 | 21 | 11 |
| 17 | 0x4b20 | 0x4b64 | 10 | 0x02 | 1 |  |  |  |  | Y | 1 | 21 | 12 |
| 18 | 0x4b82 | 0x4bc6 | 10 | 0x02 | 1 |  |  |  |  | Y | 3 | 21 | 16 |
| 19 | 0x4be4 | 0x4bf8 | 10 | 0x06 | 2 |  |  |  |  | Y | 3 | 21 | 11 |
| 20 | 0x4c16 | 0x4c5a | 10 | 0x02 | 1 |  |  |  |  | Y | 1 | 21 | 12 |
| 21 | 0x4c78 | 0x4cbc | 10 | 0x06 | 2 |  |  |  |  | Y | 3 | 21 | 11 |
| 22 | 0x4cda | 0x4d1e | 10 | 0x02 | 1 |  |  |  |  | Y | 1 | 21 | 20 |

"speed" = pixels moved per game frame (exactly 1/2/3, `0x14b73`: accumulator += 0x100/0x200/0x300, steps = high byte). "turn %" = chance per tile-aligned step of a random re-pick (`chance >= random(100)` → (c+1)%). Type 8 is only used by the time-up event; types 0,1,6? (6 is used in W1), 12 is the W2/W5 boss minion. Sprites come from `gfx\04.img`; identification of which picture is which type is left to the graphics notes **(?)**.

**Scoring** (`DS:422e + idx*4` = {points/100, popup anim}): idx 1..9 = 100, 200, 400, 800, 1000, 3000, 6000, 6000, 6000; idx 10..16 = 1000, 2000, 4000, 8000, 8000, 8000, 8000. On death (`0x14d0a`): `idx = base ? base + combo : 0`, where `combo(DS:4202)` counts kills already made *in the same frame* (reset every frame, saturates at 6). Points are added when the death animation ends and a score popup is shown. base 0 → no points.

## 5. Enemy AI (`0x14b73` → `0x14d80`), reimplementation pseudocode

```c
// per frame, for each live slot e:
if (e.type < 0) { death/score popup anim (timer += dt; >0x77 -> next frame; at end add score, show popup, then free) ; continue; }
spd = 0x100; if (e.flags&4) spd = ((e.flags&0xc)==0xc) ? 0x300 : 0x200;
e.acc += spd; n = e.acc>>8; e.acc &= 0xff;
while (n--) enemy_step(e);
animate (frame len), draw if on screen;
if (e.type != 23 && tile(e.x+8,e.y+8) is in the current explosion tile list) kill(e);   // 0x14d0a: enemy_count--, 4f94=0xff

void enemy_step(E *e) {                                    // 0x14d80, one pixel
  if (e->dir < 0) goto frozen;
  // 0x15054: a bomb appeared on my own (centre) tile -> back out to the nearer side
  if (!(e->flags&0x80) && tile_at(e->x+8,e->y+8)=='+') {
    if (e->dir&1) e->dir = ((e->x)&15) < 8 ? LEFT : RIGHT;
    else          e->dir = ((e->y-0x10)&15) < 8 ? UP : DOWN;
  }
  bool aligned = (e->dir&1) ? ((e->x-8)&15)==0 : ((e->y-0x18)&15)==0;   // only the moving axis
  if (aligned) {
    if (e->flags&0x40) avoid_ahead(e);                     // 0x14e7d
    bool chased = false;
    if (e->flags&0x10) {
      if (e->cool) e->cool--;
      else chased = chase(e);                              // 0x14ecc
    }
    if (!chased) {
      int c = type[e->type].turn;
      if (c && c >= random(100)) { e->dir = pick_dir(e); if (e->dir<0) goto frozen; goto move; }
    }
    if (blocked(e, e->dir)) { e->dir = pick_dir(e); if (e->dir<0) goto frozen; }
  }
move:
  e->x += DX[e->dir]; e->y += DY[e->dir];                  // DS:4444: up(0,-1) right(1,0) down(0,1) left(-1,0)
  return;
frozen:
  if (++e->dir == 0) e->dir = pick_dir(e);                 // may return -8 again (pause)
}

bool blocked(E *e, int d) {                                // 0x14fee (tile in front of centre tile)
  c = tile(col(e->x+8)+TDX[d], row(e->y+8)+TDY[d]);
  return (c=='+' && !(e->flags&0x80)) || (c>='A'&&c<='U' && !(e->flags&1)) || c=='*';
  // everything else ('.', items, door, flames '-', 'V') is walkable for enemies
}

int pick_dir(E *e) {                                       // 0x150ca + 0x14fcc
  list = []; 
  for d in 0..3: c = tile ahead in d;
     if (c!='*' && c!='+' && ((e->flags&1) || !(c>='A'&&c<='U'))) list += {d, d};   // each free dir twice; bombs ALWAYS excluded here
  if (!(e->flags&2)) list += {-8};                          // "pause 8 steps" option (no current type uses it)
  if (list.n==0) return -1;  if (list.n==1) return list[0];
  return list[random(list.n)];
}

bool chase(E *e) {                                         // 0x14ecc, toward player 1 (DS:3c5e)
  P = tile(player.x, player.y); M = tile(e->x, e->y);      // note: no +8 here
  if (M.col == P.col)      { e->dir = (M.row < P.row) ? DOWN : UP;    if (!blocked(e,e->dir)) return true; }
  else if (M.row == P.row) { e->dir = (M.col < P.col) ? RIGHT : LEFT; if (!blocked(e,e->dir)) return true; }
  else {
    bool horizFirst = abs(M.col-P.col) > abs(M.row-P.row);
    e->dir = horizFirst ? H : V;  if (!blocked(e,e->dir)) return true;
    e->dir = horizFirst ? V : H;  if (!blocked(e,e->dir)) return true;
  }
  e->cool = 4; return false;                               // falls back to random logic for 4 aligned steps
}

void avoid_ahead(E *e) {                                   // 0x14e7d
  for (t = my tile + delta[dir]; ; t += delta[dir]) {
    c = tile(t); if (c=='+'||c=='-') { e->dir = (e->dir+1)&3; return; } if (c!='.') return;
  }
}
```
**Contact with player** (`0x15241`, every frame unless debug): kill player if `tile(player.x,player.y) == tile(e.x+8,e.y+8)` for any live enemy (any type; flame-proof does not help). Flames kill the player via `0x15295` unless status flag 8 (flame-proof / exiting).

## 6. Boss stages (x.8)

Stage 8 loads extra objects via `0x15a42` into a boss table of 12 slots × 0x26 at `DS:72a4` (fields: +0 x,+2 y,+4 anim base,+6 anim,+8 timer,+0xa dir,+0xc kind (<0 dying),+0xe flags,+0x12 hit-flash,+0x14 HP,+0x16/18 sprite,+0x1a/1c size). Regular enemies from the stage list are spawned too. Per frame `0x15e9b` runs the world-specific logic. Common rules:
* **Damage** `0x16a6b`: every frame a boss segment's tile is in the explosion list → `HP--` and flash; at `HP<0` it dies (points added immediately). So damage is per frame of flame overlap.
* **Contact** `0x1680f`: player dies when on the same tile as a live boss segment.
* **Exit**: `0x16c40` — when no boss segment, no regular enemy and no bomb remain, a `u` door is put on a random free tile (music change); clear as usual. (W8 has no door — see above.) Time-up kills the player. No pause key on stage 8.

| world | bosses (`0x15a42`) | behaviour | HP | score |
|---|---|---|---|---|
| 1 | 1 **chain** of 6 segments at tile (7,7) (`0x15d59`: head/body/tail anims 6b7a/6bbe/6c02) | `0x169a7` ×2/frame (2 px/frame): head chases player with `chase()` at alignments, else `pick_dir`; body segments follow the head's direction history | 50 each segment; any segment dying kills the whole chain | +200 (20,000) |
| 2 | 1 big boss (`0x15d04`, kind 8, flags 0x83, HP 30) | `0x166dd` (1 px/frame): at alignment, if `enemy_count<8` keep dir unless blocked and **spawn a type-12 minion** at its tile (frozen 72 steps); if `enemy_count>=8`: `random(100)<20` → keep unless blocked, else re-pick (`pick_dir`) | 30 | +200 |
| 3 | 2 chains at (7,7) and (1,7) | as W1 | 50 | +200 each |
| 4 | 4 small (`0x15c5a`, kind 1, HP 15) + 1 big (`0x15caf`, kind 0, HP 40) | `0x1614a` random walk: at alignment `random(100)<21` → re-pick, else continue unless blocked; sprite changes with HP | 15 / 40 | small +10, big +300 |
| 5 | 3 × W2 boss | as W2 | 30 | +200 |
| 6 | 8 small + 3 big | as W4 | 15 / 40 | +10 / +300 |
| 7 | 4 × `0x15bfd` (kind 1, HP 25) | `0x1614a` ×1, invulnerable phases toggle with frame counter bit 5 (`6b78`) | 25 | +300 |
| 8 | 5 parts from `DS:702a` ({tile,anim,kind}: (9,6) k1, (11,4) k2, (13,2) k3, (7,8) k4, (5,10) k5) via `0x15b90` | `0x1614a` ×2; phase machine on `6b72`/`6b79` (every 0xb4 frames the parts turn into enemy-slot objects of type 0x17 — immune to flames — and back); only kind 1 vulnerable while `6b78`, others while `6b79==0` **(?)** | 14 (kind 1: 25) | +10 per part; stage clears after all 5 death anims |

Boss details for W8 phases and sprite selection are approximate **(?)**; movement/damage/contact rules above are exact.

## 7. Uncertain / not covered
* Real-time speed: everything is frame-locked (movement not scaled by the tick delta `DS:1e`, animations are); the frame rate itself is set elsewhere (see main loop/vsync notes).
* Flag 0x20 (type 9) has no reader in the enemy code found.
* `0x157c0` hint redraw of the hidden item block once all enemies are dead — purpose inferred.
* Enemy names ↔ type ids not established (needs sprite sheet `gfx\04.img` cross-reference of anim tables at `DS:4464..4d1e`).
