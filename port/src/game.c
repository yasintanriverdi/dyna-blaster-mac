/* Gameplay: faithful re-implementation of the DYNA.EXE game logic.
 * Function addresses in comments refer to the unpacked DYNA.EXE (docs/re). */
#include "game.h"
#include <stdlib.h>
#include <string.h>

Game G;
static int tilemap[MAXW * MAXH + 64];   /* static tile index per cell (walls/decor) */
static int itemtile[128];
static int softtile, floortile;

static const int EDX[4] = {0, 1, 0, -1}, EDY[4] = {-1, 0, 1, 0};
enum { UP, RIGHT, DOWN, LEFT };

/* ---------------- map helpers (0x13902 / 0x1394F / 0x13962) ---------------- */
static inline int pcol(int x) { return (x - 8) >> 4; }
static inline int prow(int y) { return (y - 24) >> 4; }
static inline uint8_t tile(int c, int r) {
    if (c < 0 || r < 0 || c >= G.W || r >= G.H) return '*';
    return G.map[r * G.W + c];
}
static inline void settile(int c, int r, uint8_t v) {
    if (c >= 0 && r >= 0 && c < G.W && r < G.H) G.map[r * G.W + c] = v;
}
static inline bool story(void) { return G.mode != 0; }

/* ---------------- level construction ---------------- */
static void scatter_last(int count, uint8_t newc, uint8_t match, int *lc, int *lr) {   /* 0x132BD */
    int guard = 0;
    while (count-- > 0) {
        int x, y;
        do {
            y = rnd((uint16_t)(G.H + 1));
            x = rnd((uint16_t)(G.W + 1));
            if (x == G.W) { x = 0; y++; }            /* x==W aliases next row, col 0 */
            if (++guard > 200000) return;
        } while (y >= G.H || tile(x, y) != match);
        settile(x, y, newc);
        if (lc) { *lc = x; *lr = y; }
    }
}

static void build_tilemap(void) {
    int world = G.mode;
    int cl = dsw(0x271C + world * 4), tt = dsw(0x271E + world * 4);
    for (int r = 0; r < G.H; r++)
        for (int c = 0; c < G.W; c++) {
            uint8_t ch = G.map[r * G.W + c];
            int k = 0;
            while (DS[cl + k] != 0xFF && DS[cl + k] != ch) k++;
            int t = dsw(tt + k * 2);
            if (story() && (t == 0 || t == 0x69)) {                       /* 0x1312E floor shadow */
                if (r != 1 && (r & 1) && !(c & 1)) t = 0x6A;
            }
            tilemap[r * G.W + c] = t;
        }
    /* decor chars become solid walls (0x157EC) */
    const uint8_t *decor = DS + 0x4FA0;
    for (int i = 0; i < G.W * G.H; i++)
        for (int k = 0; decor[k] != 0xFF; k++)
            if (G.map[i] == decor[k]) { G.map[i] = '*'; break; }
    /* soft block & floor tiles */
    int k = 0;
    uint8_t want = story() ? 'E' : 'A';
    while (DS[cl + k] != 0xFF && DS[cl + k] != want) k++;
    softtile = dsw(tt + k * 2);
    floortile = story() ? 0x69 : 0;
    /* item tiles from the block->result table DS:3A80 */
    memset(itemtile, 0, sizeof itemtile);
    for (int L = 0; L < 22; L++) {
        uint8_t res = DS[0x3A80 + L * 4];
        int ptr = dsw(0x3A80 + L * 4 + 2);
        if (res >= 'a' && res <= 'u' && ptr) {
            bool st = L >= 4;        /* E.. are story blocks */
            if (st == story()) itemtile[res] = dsw(ptr);
        }
    }
    itemtile['o'] = 0x9A; itemtile['s'] = 0x9E; itemtile['p'] = 0x9C;
}

static void reset_objects(void) {
    memset(G.bomb, 0, sizeof G.bomb);
    memset(G.crumble, 0, sizeof G.crumble);
    memset(G.burn, 0, sizeof G.burn);
    memset(G.en, 0, sizeof G.en);
    G.enemy_count = 0;
    G.nflame = 0;
    G.serial = 0;
}

static bool safe_tile(int c, int r) {                 /* 0x152F6: safe list DS:4204 */
    for (int o = 0x4204; dsw(o) != 0xFFFF; o += 2) {
        uint16_t w = dsw(o);
        if ((w & 0xFF) == c && (w >> 8) == r) return true;
    }
    return false;
}

static int spawn_enemy(int c, int r, int type, int frozen) {
    for (int i = 0; i < 15; i++) {
        Enemy *e = &G.en[i];
        if (e->used) continue;
        memset(e, 0, sizeof *e);
        int t = 0x42D4 + type * 16;
        e->used = true;
        e->x = e->px = c * 16 + 8;
        e->y = e->py = r * 16 + 24;
        e->anim_base = e->anim = dsw(t);
        e->flen = dsw(t + 4);
        e->flags = dsw(t + 6);
        e->type = type;
        e->dir = frozen;
        G.enemy_count++;
        return i;
    }
    return -1;
}

static void spawn_random(int type, int frozen) {       /* 0x14ACA body */
    int c, r, guard = 0;
    do {
        c = rnd((uint16_t)G.W);
        r = rnd((uint16_t)G.H);
        if (++guard > 100000) return;
    } while (tile(c, r) != '.' || safe_tile(c, r));
    spawn_enemy(c, r, type, frozen);
}

static void spawn_list(int p) {
    for (; dss(p) > 0; p += 4)
        for (int n = 0; n < dss(p); n++) spawn_random(dss(p + 2), 0);
}

static void place_player(Player *p, int c, int r) {
    p->x = p->px = c * 16 + 16;
    p->y = p->py = r * 16 + 32;
    p->xacc = p->yacc = 0;
    p->anim = p->animcnt = 0;
    p->bombs = 0;
    p->facing = 0;
    p->input = 0;
}

void battle_new_round(void) {                          /* 0x100A3 round setup */
    G.mode = 0;
    G.W = 15; G.H = 13; G.scroll = 0; G.cam = 0x80;
    reset_objects();
    memcpy(G.map, DS + 0x46, 0xC3);
    int cnt = 0x2704 + (G.nplayers - 2) * 8;
    scatter_last(dsw(cnt), '.', 'A', 0, 0);
    scatter_last(dsw(cnt + 2), 'B', 'A', 0, 0);
    scatter_last(dsw(cnt + 4), 'C', 'A', 0, 0);
    if (G.skulls) scatter_last(dsw(cnt + 6), 'D', 'A', 0, 0);
    build_tilemap();
    for (int i = 0; i < 4; i++) {
        Player *p = &G.pl[i];
        int wins = p->wins;
        memset(p, 0, sizeof *p);
        p->wins = wins;
        if (i >= G.nplayers) { p->x = p->px = -1; continue; }
        uint16_t cell = dsw(0x36 + i * 4);
        place_player(p, cell & 0xFF, cell >> 8);
        p->id = dsw(0x38 + i * 4);
        p->maxbombs = 1; p->fire = 2;
        p->flags = 2; p->dtimer = 1;
    }
    G.speed = 0x300;
    int t = 0x2A + (G.nplayers - 2) * 4;
    G.tmin = dsw(t); G.tsec = dsw(t + 2); G.tsub = 0; G.tstop = false; G.timeup = false;
    G.round_over = false; G.round_ctr = 0; G.winner = -1;
    if (G.demo && G.demo_data) {                       /* 0x139D7: DEMO.DAT map + recorded input */
        memcpy(G.map, G.demo_data, 0xC3);
        build_tilemap();
        G.demo_pos = 0x1B8;
    } else spawn_list(dsw(0x42B4 + (G.nplayers - 2) * 2));
}

void story_start_stage(void) {                         /* 0x1531A retry: */
    int rec = dsw(dsw(0x4FBC + G.mode * 2) + 2 + G.stage * 2);
    G.scroll = dss(rec);
    static const int tw[3] = {15, 31, 15}, th[3] = {13, 13, 29}, tpl[3] = {0x1FE, 0x3B6, 0x56E};
    G.W = tw[G.scroll]; G.H = th[G.scroll];
    G.cam = 0x80;
    reset_objects();
    memcpy(G.map, DS + tpl[G.scroll], 0x1B8);
    /* 0x13219 story branch */
    scatter_last(dss(rec + 2), '.', 'E', 0, 0);
    G.soft_left = dsw(0x34B6 + G.scroll * 2) - dss(rec + 2);
    G.hidden_pos = -1;
    if (dss(rec + 4) != -1) { int c, r; scatter_last(1, (uint8_t)dss(rec + 4), 'E', &c, &r); G.hidden_pos = r * G.W + c; }
    if (dss(rec + 6) != -1) scatter_last(1, (uint8_t)dss(rec + 6), 'E', 0, 0);
    build_tilemap();
    Player *p = &G.pl[0];
    int mb = p->maxbombs, fi = p->fire, fl = p->flags;
    memset(G.pl, 0, sizeof G.pl);
    for (int i = 1; i < 4; i++) G.pl[i].x = G.pl[i].px = -1;
    place_player(p, 1, 1);
    p->maxbombs = mb; p->fire = fi; p->flags = fl & ~8;   /* flame-proof cleared each stage */
    G.saved_bombs = mb; G.saved_fire = fi; G.saved_speed = G.speed;
    G.tmin = 4; G.tsec = 0; G.tsub = 0; G.tstop = false; G.timeup = false; G.timeup_ctr = 0;
    G.enemy_killed = G.stage == 8;
    spawn_list(rec + 8);
    G.result = 0; G.dead_ctr = 0; G.exit_ctr = 0; G.exit_ready = false; G.idle_ctr = 0;
}

/* ---------------- items / blocks / explosions ---------------- */
static void spawn_bonus(uint8_t ch) {                  /* random free '.' with no enemy */
    for (int guard = 0; guard < 100000; guard++) {
        int c = rnd((uint16_t)G.W), r = rnd((uint16_t)G.H);
        if (tile(c, r) != '.') continue;
        bool occ = false;
        for (int i = 0; i < 15; i++)
            if (G.en[i].used && pcol(G.en[i].x + 8) == c && prow(G.en[i].y + 8) == r) occ = true;
        if (occ) continue;
        settile(c, r, ch);
        return;
    }
}

static void destroy_soft(int c, int r, uint8_t t) {    /* 0x1436D */
    if (story() && G.soft_left == 2 && !G.enemy_killed) spawn_bonus('o');
    G.soft_left--;
    if (r * G.W + c == G.hidden_pos) G.hidden_pos = -1;
    for (int i = 0; i < 8; i++) {
        Crumble *s = &G.crumble[i];
        if (s->used) continue;
        s->used = true; s->cx = c; s->cy = r; s->cnt = 0; s->idx = t - 'A';
        s->anim = dsw(0x3A80 + s->idx * 4 + 2);
        settile(c, r, 'V');
        return;
    }
}

static void burn_item(int c, int r) {                  /* 0x14400 */
    for (int i = 0; i < 3; i++) {
        Burn *b = &G.burn[i];
        if (b->used) continue;
        if (b->cx == c && b->cy == r && b->used) return;
        b->used = true; b->cx = c; b->cy = r; b->step = 0; b->cnt = 0;
        return;
    }
}

static Bomb *bomb_at(int c, int r) {
    for (int i = 0; i < 8; i++)
        if (G.bomb[i].used && G.bomb[i].cx == c && G.bomb[i].cy == r) return &G.bomb[i];
    return NULL;
}

static int arm(int c, int r, int dx, int dy, int range) {   /* 0x1427E */
    for (int k = 0;;) {
        c += dx; r += dy;
        uint8_t t = tile(c, r);
        if (t == '.') { if (++k == range) return -k; continue; }
        if (t == '+') { Bomb *b = bomb_at(c, r); if (b) b->fuse = 4; return k; }
        if (t >= 'A' && t <= 'T') { destroy_soft(c, r, t); return k; }
        if (t == 'U') { if (story() && G.soft_left == 1) spawn_bonus('s'); destroy_soft(c, r, t); return k; }
        if (t >= 'a' && t <= 't') { burn_item(c, r); return k; }
        if (t == 'u') {
            if (G.pl[0].animcnt >= 0) {
                static const int types[4] = {9, 5, 3, 4};
                int ty = types[rnd(4)];
                for (int n = 0; n < 4; n++) spawn_enemy(c, r, ty, -25);
            }
            return k;
        }
        return k;
    }
}

static void explode(Bomb *b) {                         /* 0x145AC */
    b->sub = 0; b->frame = -1; b->fuse = 0x7FFF;
    Player *o = &G.pl[b->owner];
    o->bombs--;
    settile(b->cx, b->cy, '-');
    int range = o->fire;
    b->arm[0] = arm(b->cx, b->cy, 1, 0, range);
    b->arm[1] = arm(b->cx, b->cy, 0, 1, range);
    b->arm[2] = arm(b->cx, b->cy, -1, 0, range);
    b->arm[3] = arm(b->cx, b->cy, 0, -1, range);
    sfx_play(4);
}

static void add_flame(int c, int r) {
    if (G.nflame < 512) { G.flame[G.nflame][0] = c; G.flame[G.nflame][1] = r; G.nflame++; }
}

static void bombs_update(void) {                        /* 0x144BF */
    G.nflame = 0;
    for (int i = 0; i < 8; i++) {
        Bomb *b = &G.bomb[i];
        if (!b->used) continue;
        b->fuse -= 4;
        if (b->fuse < 0) { explode(b); continue; }
        if (b->frame < 0) {                              /* 0x1461E */
            b->sub += 6;
            if (b->sub >= 5) { b->sub = 0; b->frame--; }
            if (b->frame < -11) { b->used = false; settile(b->cx, b->cy, '.'); continue; }
            static const int dx[4] = {1, 0, -1, 0}, dy[4] = {0, 1, 0, -1};
            for (int d = 0; d < 4; d++) {
                add_flame(b->cx, b->cy);
                int n = abs(b->arm[d]);
                for (int k = 1; k <= n; k++) add_flame(b->cx + dx[d] * k, b->cy + dy[d] * k);
            }
            continue;
        }
        b->sub += 4;
        if (b->sub >= 15) { b->sub = 0; b->frame = (b->frame + 1) & 3; }
    }
    /* remote control (story): oldest bomb */
    Player *p = &G.pl[0];
    if (story() && (p->flags & 4) && (p->input & IN_B)) {
        Bomb *best = NULL;
        for (int i = 0; i < 8; i++)
            if (G.bomb[i].used && G.bomb[i].frame >= 0 && (!best || G.bomb[i].serial < best->serial)) best = &G.bomb[i];
        if (best) { best->fuse = -1; best->serial = 0x7FFF; }
    }
}

static void crumble_update(void) {                      /* 0x13B08 */
    for (int i = 0; i < 8; i++) {
        Crumble *s = &G.crumble[i];
        if (!s->used) continue;
        s->cnt += 4;
        if (s->cnt >= 5) { s->cnt = 0; s->anim += 2; }
        if (dsw(s->anim) == 0xFFFF) {
            settile(s->cx, s->cy, DS[0x3A80 + s->idx * 4]);
            s->used = false;
        }
    }
}

static void burn_update(void) {                         /* 0x14445 */
    for (int i = 0; i < 3; i++) {
        Burn *b = &G.burn[i];
        if (!b->used) continue;
        b->cnt += 4;
        if (b->cnt >= 8) { b->cnt = 0; if (++b->step >= 7) { b->used = false; settile(b->cx, b->cy, '.'); } }
    }
}

static bool in_flames(int c, int r) {
    for (int i = 0; i < G.nflame; i++)
        if (G.flame[i][0] == c && G.flame[i][1] == r) return true;
    return false;
}

/* ---------------- players ---------------- */
static bool passable(const Player *p, uint8_t t) {      /* 0x149A8 */
    if (t == '.' || t == '-' || (t >= 'a' && t <= 'u')) return true;
    if (story() && (p->flags & 1) && t >= 'A' && t <= 'V') return true;
    if (story() && (p->flags & 2) && t == '+') return true;
    return false;
}

static void anim_step(Player *p) {                      /* 0x1476D */
    if (p->animcnt >= 0 && ++p->animcnt >= 10) { p->animcnt = 0; p->anim = (p->anim + 1) & 3; }
}

static void step(Player *p, int ddx, int ddy) {         /* 0x1483C & rotations */
    int c = pcol(p->x), r = prow(p->y);
    int dx = ((p->x - 16 + 8) & 15) - 8, dy = ((p->y - 32 + 8) & 15) - 8;
    int along = ddx ? dx : dy, q = ddx ? dy : dx;
    int *perp = ddx ? &p->y : &p->x;
    if (along != 0 || passable(p, tile(c + ddx, r + ddy))) {
        if (q < 0) (*perp)++; else if (q > 0) (*perp)--;
        p->x += ddx; p->y += ddy;
    } else if (q != 0) {
        int s = q < 0 ? -1 : 1;
        uint8_t diag = ddx ? tile(c + ddx, r + s) : tile(c + s, r + ddy);
        if (passable(p, diag)) *perp += s;
    }
}

static bool exit_check(Player *p) {                     /* 0x14107 */
    if (!story() || !G.exit_ready) return false;
    if (tile(pcol(p->x), prow(p->y)) != 'u') return false;
    if (((p->x - 16) & 15) || ((p->y - 32) & 15)) return false;
    p->animcnt = -1; p->flags |= 8; G.tstop = true; G.exit_ctr = 0;
    music_play(10);
    return true;
}

static void move(Player *p, int ddx, int ddy, int speed) {
    int *acc = ddx ? &p->xacc : &p->yacc;
    *acc += speed;
    int n = *acc >> 8;
    *acc &= 0xFF;
    while (n--) {
        step(p, ddx, ddy);
        anim_step(p);
        if (exit_check(p)) return;
    }
}

static void place_bomb(Player *p) {                     /* 0x141E0 */
    int s;
    for (s = 0; s < 8 && G.bomb[s].used; s++) {}
    if (s == 8) return;
    int c = pcol(p->x), r = prow(p->y);
    if (tile(c, r) != '.') return;
    if (p->bombs == p->maxbombs) return;
    p->bombs++;
    settile(c, r, '+');
    Bomb *b = &G.bomb[s];
    memset(b, 0, sizeof *b);
    b->used = true; b->fuse = 0xD2; b->cx = c; b->cy = r;
    b->owner = (int)(p - G.pl); b->serial = G.serial++;
    if (story() && (G.pl[0].flags & 4)) b->fuse = 0x7FFF;
}

static void pickup(Player *p) {                         /* 0x13EFE */
    int c = pcol(p->x), r = prow(p->y);
    uint8_t t = tile(c, r);
    if (t < 'a' || t > 'u') return;
    for (int i = 0; i < 3; i++) if (G.burn[i].used && G.burn[i].cx == c && G.burn[i].cy == r) return;
    if (!story()) {
        if (t == 'a') p->maxbombs++;
        else if (t == 'b') p->fire++;
        else if (t == 'c') { p->dtimer = 0xB4; p->flags = rnd(4); }
        else return;
    } else {
        switch (t) {
        case 'g': p->maxbombs++; break;
        case 'f': p->fire++; break;
        case 'i': G.speed = G.speed < 0x300 ? 0x300 : 0x3C0; break;
        case 'h': p->flags |= 4; break;
        case 'j': p->flags |= 2; break;
        case 'k': p->flags |= 1; break;
        case 'l': p->flags |= 8; break;
        case 'm': G.lives++; break;
        case 'o': case 'p': G.score += 100; break;
        case 's': G.score += 300; break;
        case 'u': if (G.enemy_count == 0) G.exit_ready = true; return;
        default: return;
        }
        if (G.score > 64000) G.score = 64000;
    }
    settile(c, r, '.');
    sfx_play(1);
}

static void player_tick(Player *p) {                    /* 0x13C59 */
    if (p->x == -1 || p->anim < 0) return;
    if (p->animcnt < 0) { if (++G.exit_ctr >= 36) G.result = 2; return; }
    if (!story()) {                                     /* disease 0x13E96 */
        if (p->dtimer > 0 && --p->dtimer == 0) p->flags = -1;
        G.speed = 0x300;
        switch (p->flags) {
        case 0: G.speed = 0x100; break;
        case 1: G.speed = 0xC00; break;
        case 2: p->input &= 0x0F; break;
        case 3: p->input ^= G.frametoggle & 0x10; break;
        }
    }
    if (p->input & IN_BOMB) place_bomb(p);
    if (p->input & IN_UP) { move(p, 0, -1, G.speed); p->facing = 9; anim_step(p); }
    else if (p->input & IN_DOWN) { move(p, 0, 1, G.speed); p->facing = 0; anim_step(p); }
    else if (p->input & IN_LEFT) { move(p, -1, 0, G.speed); p->facing = 6; anim_step(p); }
    else if (p->input & IN_RIGHT) { move(p, 1, 0, G.speed); p->facing = 3; anim_step(p); }
    else { p->anim = 0; if (p->animcnt > 0) p->animcnt = 0; }
    if (p->animcnt >= 0) pickup(p);
}

static void infection(void) {                           /* 0x13BE3 */
    if (story() || !G.skulls) return;
    for (int i = 0; i < 4; i++) {
        Player *a = &G.pl[i];
        if (a->x == -1 || a->anim < 0 || a->flags < 0 || a->dtimer <= 0) continue;
        for (int j = 0; j < 4; j++) {
            Player *b = &G.pl[j];
            if (j == i || b->x == -1 || b->anim < 0 || b->flags >= 0) continue;
            if (pcol(a->x) == pcol(b->x) && prow(a->y) == prow(b->y)) { b->flags = a->flags; b->dtimer = 0xB4; }
        }
    }
}

static void kill_player(Player *p) { p->anim = -1; p->animcnt = 0; sfx_play(0); if (story()) music_play(5); }

static void death_checks(void) {                        /* 0x151BB */
    for (int i = 0; i < 4; i++) {
        Player *p = &G.pl[i];
        if (p->x == -1 || p->anim < 0 || p->animcnt < 0) continue;
        int c = pcol(p->x), r = prow(p->y);
        bool dead = false;
        for (int k = 0; k < 15 && !dead; k++) {          /* 0x15241 */
            Enemy *e = &G.en[k];
            if (e->used && e->type >= 0 && pcol(e->x + 8) == c && prow(e->y + 8) == r) dead = true;
        }
        if (!dead && in_flames(c, r) && !(story() && (p->flags & 8))) dead = true;   /* 0x15295 */
        if (dead) kill_player(p);
    }
}

static void death_anim(Player *p) {                     /* 0x13D63 */
    if (p->x == -1 || p->anim >= 0) return;
    p->animcnt += 4;
    if (p->animcnt >= 9) {
        p->animcnt = 0;
        p->anim--;
        if (dsw(0x3B5E + 2 * (-p->anim)) == 0xFFFF) p->x = p->px = -1;
    }
}

/* ---------------- enemies (0x14B73 / 0x14D80) ---------------- */
static bool e_blocked(Enemy *e, int d) {                /* 0x14FEE */
    uint8_t c = tile(pcol(e->x + 8) + EDX[d], prow(e->y + 8) + EDY[d]);
    return (c == '+' && !(e->flags & 0x80)) || (c >= 'A' && c <= 'U' && !(e->flags & 1)) || c == '*';
}

static int pick_dir(Enemy *e) {                         /* 0x150CA */
    int list[10], n = 0;
    for (int d = 0; d < 4; d++) {
        uint8_t c = tile(pcol(e->x + 8) + EDX[d], prow(e->y + 8) + EDY[d]);
        if (c != '*' && c != '+' && ((e->flags & 1) || !(c >= 'A' && c <= 'U'))) { list[n++] = d; list[n++] = d; }
    }
    if (!(e->flags & 2)) list[n++] = -8;
    if (n == 0) return -1;
    if (n == 1) return list[0];
    return list[rnd((uint16_t)n)];
}

static bool chase(Enemy *e) {                           /* 0x14ECC */
    Player *p = &G.pl[0];
    if (p->x == -1) { e->cool = 4; return false; }
    int pc = pcol(p->x), pr = prow(p->y), mc = pcol(e->x), mr = prow(e->y);
    if (mc == pc) { e->dir = mr < pr ? DOWN : UP; if (!e_blocked(e, e->dir)) return true; }
    else if (mr == pr) { e->dir = mc < pc ? RIGHT : LEFT; if (!e_blocked(e, e->dir)) return true; }
    else {
        bool hf = abs(mc - pc) > abs(mr - pr);
        int H = mc < pc ? RIGHT : LEFT, V = mr < pr ? DOWN : UP;
        e->dir = hf ? H : V; if (!e_blocked(e, e->dir)) return true;
        e->dir = hf ? V : H; if (!e_blocked(e, e->dir)) return true;
    }
    e->cool = 4;
    return false;
}

static void avoid_ahead(Enemy *e) {                     /* 0x14E7D */
    int c = pcol(e->x + 8), r = prow(e->y + 8);
    for (int k = 0; k < 40; k++) {
        c += EDX[e->dir]; r += EDY[e->dir];
        uint8_t t = tile(c, r);
        if (t == '+' || t == '-') { e->dir = (e->dir + 1) & 3; return; }
        if (t != '.') return;
    }
}

static void enemy_step(Enemy *e) {                      /* 0x14D80 */
    if (e->dir < 0) goto frozen;
    if (!(e->flags & 0x80) && tile(pcol(e->x + 8), prow(e->y + 8)) == '+') {
        if (e->dir & 1) e->dir = (e->x & 15) < 8 ? LEFT : RIGHT;
        else e->dir = ((e->y - 0x10) & 15) < 8 ? UP : DOWN;
    }
    bool aligned = (e->dir & 1) ? ((e->x - 8) & 15) == 0 : ((e->y - 0x18) & 15) == 0;
    if (aligned) {
        if (e->flags & 0x40) avoid_ahead(e);
        bool chased = false;
        if (e->flags & 0x10) { if (e->cool) e->cool--; else chased = chase(e); }
        if (!chased) {
            int c = dsw(0x42D4 + e->type * 16 + 0xA);
            if (c && c >= rnd(100)) {
                e->dir = pick_dir(e);
                if (e->dir < 0) goto frozen;
                goto mv;
            }
        }
        if (e_blocked(e, e->dir)) { e->dir = pick_dir(e); if (e->dir < 0) goto frozen; }
    }
mv:
    e->x += EDX[e->dir]; e->y += EDY[e->dir];
    return;
frozen:
    if (++e->dir == 0) e->dir = pick_dir(e);
}

static void enemies_update(void) {
    G.kills_this_frame = 0;
    for (int i = 0; i < 15; i++) {
        Enemy *e = &G.en[i];
        if (!e->used) continue;
        e->px = e->x; e->py = e->y;
        if (e->type < 0) {                               /* death / score popup */
            e->timer += 4;
            if (e->timer >= 0x78) {
                e->timer = 0x64;
                e->anim += 4;
                if (dsw(e->anim) == 0xFFFF) {
                    if (e->dir != -1) {
                        int o = 0x422E + e->dir * 4;
                        G.score += dsw(o);
                        if (G.score > 64000) G.score = 64000;
                        if (dsw(o + 2)) { e->dir = -1; e->timer = 0; e->anim = dsw(o + 2); }
                        else { e->used = false; continue; }
                    } else { e->used = false; continue; }
                }
            }
            e->spr_off = dsw(e->anim); e->spr_seg = dsw(e->anim + 2);
            continue;
        }
        int spd = 0x100;
        if (e->flags & 4) spd = ((e->flags & 0xC) == 0xC) ? 0x300 : 0x200;
        e->acc += spd;
        int n = e->acc >> 8;
        e->acc &= 0xFF;
        while (n--) enemy_step(e);
        e->facc += 4;
        if (e->facc >= e->flen) { e->facc = 0; e->anim += 16; if (dsw(e->anim) == 0xFFFF) e->anim = e->anim_base; }
        int a = e->anim + (e->dir >= 0 ? (e->dir & 3) * 4 : 0);
        e->spr_off = dsw(a); e->spr_seg = dsw(a + 2);
        if (e->type != 0x17 && in_flames(pcol(e->x + 8), prow(e->y + 8))) {   /* 0x14D0A */
            int t = 0x42D4 + e->type * 16;
            e->anim = dsw(t + 2);
            int base = dsw(t + 8);
            e->dir = base ? base + G.kills_this_frame : 0;
            if (G.kills_this_frame < 6) G.kills_this_frame++;
            e->type = -1;
            e->timer = 0x50;
            G.enemy_count--;
            G.enemy_killed = true;
            e->spr_off = dsw(e->anim); e->spr_seg = dsw(e->anim + 2);
        }
    }
}

/* ---------------- per-tick driver ---------------- */
static void clock_tick(void) {                          /* INT 1Ch handler 0x11355 */
    if (G.tstop) return;
    if (++G.tsub >= 18) {
        G.tsub = 0;
        if (--G.tsec < 0) {
            G.tsec = 59;
            if (--G.tmin < 0) { G.tmin = G.tsec = 0; G.tstop = true; G.timeup = true; }
        }
    }
}

static void time_up(void) {                             /* 0x1580B */
    if (G.stage == 8) { if (G.pl[0].anim >= 0) kill_player(&G.pl[0]); return; }
    for (int i = 0; i < 15; i++) G.en[i].used = false;
    G.enemy_count = 0;
    for (int n = 0; n < 8; n++) spawn_random(8, -25);
}

static void battle_round_check(void) {                  /* 0x103B2 */
    if (G.round_over) return;
    int alive = 0, who = -1;
    for (int i = 0; i < G.nplayers; i++) {
        Player *p = &G.pl[i];
        if (p->x != -1 && p->anim < 0) return;          /* someone mid-death */
        if (p->x != -1) { alive++; who = i; }
    }
    if (alive >= 2) return;
    if (G.round_ctr == 0) { G.winner = who; for (int i = 0; i < 4; i++) { G.pl[i].flags = 2; G.pl[i].dtimer = 0; } }
    for (int i = 0; i < 8; i++) if (G.bomb[i].used) return;
    if (alive == 0) G.winner = -1;
    G.round_ctr++;
    if (G.round_ctr == 2 || G.round_ctr == 9 || G.round_ctr == 16) sfx_play(3);
    if (G.round_ctr >= 18) G.round_over = true;
}

void game_tick(void) {
    G.tick++;
    G.frametoggle ^= 0x10;
    clock_tick();
    for (int i = 0; i < 4; i++) {
        Player *p = &G.pl[i];
        p->px = p->x; p->py = p->y;
        if (story() && i > 0) continue;
        if (G.demo) continue;
        if (p->x != -1 && p->anim >= 0) p->input = input_player(i);
        else p->input = 0;
    }
    if (G.demo && G.demo_pos + 4 <= G.demo_len && G.demo_data[G.demo_pos] != 0xFF) {
        for (int i = 0; i < 4; i++) G.pl[i].input = G.demo_data[G.demo_pos + i];
        G.demo_pos += 4;
    }
    if (story() && G.pl[0].input == 0 && G.mode == 1 && G.stage == 1) {     /* 'p' idle easter egg */
        if (++G.idle_ctr == 0xCA8) spawn_bonus('p');
    } else G.idle_ctr = 0;
    for (int i = 0; i < 4; i++) death_anim(&G.pl[i]);
    bombs_update();
    crumble_update();
    burn_update();
    enemies_update();
    infection();
    for (int i = 0; i < 4; i++) player_tick(&G.pl[i]);
    /* camera 0x1111F */
    Player *p0 = &G.pl[0];
    if (story() && p0->x != -1 && p0->anim >= 0) {
        int v = G.scroll == 1 ? p0->x : G.scroll == 2 ? p0->y : 0x80;
        G.cam = v < 0x80 ? 0x80 : v > 0x180 ? 0x180 : v;
    }
    if (G.result == 0 || !story()) death_checks();
    if (story()) {
        if (G.timeup && ++G.timeup_ctr == 17) time_up();
        if (p0->x == -1 && ++G.dead_ctr >= 0x12) G.result = 1;
        /* boss stage placeholder exit (0x16C40): door when everything is gone */
        if (G.stage == 8 && G.enemy_count == 0 && !G.exit_ready) {
            bool any = false, door = false;
            for (int i = 0; i < 8; i++) if (G.bomb[i].used) any = true;
            for (int i = 0; i < G.W * G.H; i++) if (G.map[i] == 'u') door = true;
            if (!any && !door) { spawn_bonus('u'); music_play(10); }
        }
    } else {
        if (G.timeup) { G.round_over = true; G.winner = -1; }
        battle_round_check();
    }
}

/* ---------------- rendering ---------------- */
static int offx, offy;
static void wtile(int idx, int wx, int wy) { draw_tile(idx, wx - offx, wy - offy); }

void game_render(float t) {
    offx = (G.scroll == 1) ? G.cam - 0x80 : 0;
    offy = (G.scroll == 2) ? G.cam - 0x80 : 0;
    gfx_set_clip(0, 24, SCR_W, SCR_H);
    clear_fb(0);
    /* edge strips (0x1317E) */
    int etl = story() ? 0x83 : 0x0F, etr = story() ? 0x7C : 0x10;
    int rx = 8 + G.W * 16;
    for (int r = 0; r < G.H; r++) { wtile(etl, 0, 24 + r * 16); wtile(etr, rx, 24 + r * 16); }
    if (story()) { wtile(0x84, 0, 24 + (G.H - 1) * 16); wtile(0x7D, rx, 24 + (G.H - 1) * 16); }
    /* cells */
    for (int r = 0; r < G.H; r++)
        for (int c = 0; c < G.W; c++) {
            int wx = 8 + c * 16, wy = 24 + r * 16;
            uint8_t ch = G.map[r * G.W + c];
            int ft = floortile;
            if (story() && !(r != 1 && (r & 1) && !(c & 1))) ft = 0x69; else if (story()) ft = 0x6A;
            if (ch == '*') wtile(tilemap[r * G.W + c], wx, wy);
            else if (ch >= 'A' && ch <= 'U') wtile(softtile, wx, wy);
            else wtile(ft, wx, wy);
            if (ch >= 'a' && ch <= 'u' && itemtile[ch]) wtile(itemtile[ch], wx, wy);
        }
    for (int i = 0; i < 8; i++) {
        Crumble *s = &G.crumble[i];
        if (s->used && dsw(s->anim) != 0xFFFF) wtile(dsw(s->anim), 8 + s->cx * 16, 24 + s->cy * 16);
    }
    /* bombs and flames */
    for (int i = 0; i < 8; i++) {
        Bomb *b = &G.bomb[i];
        if (!b->used) continue;
        int wx = 8 + b->cx * 16, wy = 24 + b->cy * 16;
        if (b->frame >= 0) {
            static const int seq[4] = {0, 1, 0, 2};
            wtile(seq[b->frame] + 9 + (story() ? 0x84 : 0), wx, wy);
            continue;
        }
        int ph = -b->frame;
        if (ph < 2 || ph > 11) continue;
        uint16_t cv = dsw(0x3B82 + ph * 2);
        if (cv < 0x8000) wtile(0x34 + cv, wx, wy);
        static const int dofs[4] = {0x90, 0x30, 0x60, 0x00};   /* right, down, left, up */
        static const int dx[4] = {1, 0, -1, 0}, dy[4] = {0, 1, 0, -1};
        for (int d = 0; d < 4; d++) {
            int mid = dsw(0x3B9E + dofs[d] + ph * 4), end = dsw(0x3B9E + dofs[d] + ph * 4 + 2);
            int len = b->arm[d], n = len >= 0 ? len : -len - 1;
            for (int k = 1; k <= n; k++) if (mid) wtile(mid, wx + dx[d] * 16 * k, wy + dy[d] * 16 * k);
            if (len < 0 && end) wtile(end, wx + dx[d] * 16 * (-len), wy + dy[d] * 16 * (-len));
        }
    }
    for (int i = 0; i < 3; i++) {
        Burn *b = &G.burn[i];
        if (!b->used) continue;
        int o = 0x3A60 + b->step * 4;
        blit_far(dsw(o), dsw(o + 2), 28, 27, b->cx * 16 + 16 - 0xE - offx, b->cy * 16 + 32 - 0x13 - offy, true);
    }
    /* enemies */
    for (int i = 0; i < 15; i++) {
        Enemy *e = &G.en[i];
        if (!e->used || !e->spr_seg) continue;
        int x = e->type >= 0 ? (int)(e->px + (e->x - e->px) * t) : e->x;
        int y = e->type >= 0 ? (int)(e->py + (e->y - e->py) * t) : e->y;
        blit_far(e->spr_off, e->spr_seg, 16, 18, x - offx, y - 2 - offy, true);
    }
    /* players */
    for (int i = 3; i >= 0; i--) {
        Player *p = &G.pl[i];
        if (p->x == -1) continue;
        if (p->animcnt < 0 && story()) continue;   /* inside the exit door */
        int bank = story() ? 0 : dsw(0x3E4C + 2 * p->id);
        int spr;
        if (p->anim < 0) { int v = dsw(0x3B5E + 2 * (-p->anim)); if (v == 0xFFFF) continue; spr = v + bank; }
        else spr = dsw(0x3B7A + 2 * p->anim) + bank + p->facing;
        int x = (int)(p->px + (p->x - p->px) * t), y = (int)(p->py + (p->y - p->py) * t);
        if (p->px == -1) { x = p->x; y = p->y; }
        bool sil = (story() && (p->flags & 8) && !(G.tick & 2)) || (!story() && p->dtimer > 0 && (G.tick & 4));
        draw_sprite(spr, x - 11 - offx, y - 13 - offy, sil);
    }
    gfx_set_clip(0, 0, SCR_W, SCR_H);
    draw_hud();
}

static void digits(int v, int n, int x, int base, bool zeros) {
    char buf[12];
    for (int i = n - 1; i >= 0; i--) { buf[i] = (char)(v % 10); v /= 10; }
    bool lead = true;
    for (int i = 0; i < n; i++) {
        if (buf[i] == 0 && lead && !zeros && i < n - 1) continue;
        lead = false;
        draw_tile(base + buf[i], x + i * 8, 9);
    }
}

void draw_hud(void) {
    if (story()) {
        blit(SEG_S8, 0, 148, 256, 24, 0, 0, false);
        if (G.score) { digits(G.score, 5, 32, 0xA2, false); draw_tile(0xA2, 72, 9); draw_tile(0xA2, 80, 9); }
        else draw_tile(0xA2, 80, 9);
        digits(G.tmin, 1, 104, 0xA2, true);
        digits(G.tsec, 2, 120, 0xA2, true);
        digits(G.lives, 1, 152, 0xA2, true);
        if (G.hiscore) { digits(G.hiscore, 5, 192, 0xA2, false); draw_tile(0xA2, 232, 9); draw_tile(0xA2, 240, 9); }
        else draw_tile(0xA2, 240, 9);
    } else {
        draw_tile(17, 0, 0);
        digits(G.tmin, 1, 104, 18, true);
        digits(G.tsec, 2, 120, 18, true);
    }
}

/* ---------------- passwords (0x10FA2 encode / 0x10E26 decode) ---------------- */
static const char *ALPHA[4] = {"muroyagcqxskiwhb", "wreckfvbyhoznagl", "newslogimphjtvbq", "nuclearhpstvzygk"};
static const int F_N[5] = {4, 4, 4, 4, 1};

/* field tables straight from the data segment: masks DS:20A3.., byte indices DS:20B9.. */
static const int F_MOFS[5] = {0x20A3, 0x20A7, 0x20AB, 0x20AF, 0x20B3};
#define FMASK(f, i) DS[F_MOFS[f] + (i)]
#define FBYTE(f, i) (DS[F_MOFS[f] + 0x16 + (i)] & 3)
static int field(const uint8_t *b, int f) {
    int r = 0;
    for (int i = 0; i < F_N[f]; i++) r = r << 1 | ((b[FBYTE(f, i)] & FMASK(f, i)) != 0);
    return r;
}

bool pw_decode(const char *pw, int *world, int *stage, int *bombs, int *fire, int *speed) {
    uint8_t b[4];
    for (int i = 0; i < 4; i++) {
        const char *a = ALPHA[i];
        const char *p0 = strchr(a, pw[2 * i]), *p1 = strchr(a, pw[2 * i + 1]);
        if (!p0 || !p1 || !pw[2 * i] || !pw[2 * i + 1]) return false;
        b[i] = (uint8_t)((p0 - a) | (p1 - a) << 4);
    }
    int m = b[0] & 0xE0;
    for (int k = 3; k >= 1; k--) { m <<= 1; if (m & 0x100) b[k] = (uint8_t)~b[k]; }
    if ((b[0] & 0x18) != 0x10) return false;
    for (int n = b[0] & 7; n > 0; n--) {
        uint32_t v = (uint32_t)(b[1] | b[2] << 8 | b[3] << 16) >> 1;
        b[1] = (uint8_t)v; b[2] = (uint8_t)(v >> 8); b[3] = (uint8_t)(v >> 16);
    }
    *world = field(b, 0); *stage = field(b, 1); *bombs = field(b, 2); *fire = field(b, 3);
    *speed = field(b, 4) ? 0x3C0 : 0x300;
    return *world >= 1 && *world <= 8 && *stage >= 1 && *stage <= 8;
}

void pw_encode(char out[9]) {
    uint8_t b[4] = {0, 0, 0, 0};
    int vals[5] = {G.mode, G.stage, G.saved_bombs, G.saved_fire, G.saved_speed == 0x3C0};
    for (int f = 0; f < 5; f++)
        for (int i = 0; i < F_N[f]; i++)
            if (vals[f] >> (F_N[f] - 1 - i) & 1) b[FBYTE(f, i)] |= (uint8_t)FMASK(f, i);
    int sh = rnd(2);
    for (int n = 0; n < sh; n++) {
        uint32_t v = (uint32_t)(b[1] | b[2] << 8 | b[3] << 16) << 1;
        b[1] = (uint8_t)v; b[2] = (uint8_t)(v >> 8); b[3] = (uint8_t)(v >> 16);
    }
    int inv = rnd(8);
    b[0] = (uint8_t)(sh | inv << 5 | 0x10);
    for (int k = 1; k <= 3; k++) if (inv >> (k - 1) & 1) b[k] = (uint8_t)~b[k];
    for (int i = 0; i < 4; i++) { out[2 * i] = ALPHA[i][b[i] & 15]; out[2 * i + 1] = ALPHA[i][b[i] >> 4]; }
    out[8] = 0;
}
