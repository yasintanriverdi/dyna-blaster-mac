/* Gameplay state reconstructed from DYNA.EXE (see docs/re). */
#ifndef GAME_H
#define GAME_H
#include "engine.h"

#define MAXW 31
#define MAXH 29

typedef struct {
    int x, y;            /* tile-centre pixel coordinates; x == -1: gone   (+0,+2) */
    int xacc, yacc;      /* 8.8 sub-pixel accumulators                     (+4,+6) */
    int anim;            /* walk frame 0..3, <0 dying                       (+8)    */
    int animcnt;         /* <0: story exit animation                        (+A)    */
    int maxbombs, bombs; /* (+C,+E) */
    int fire;            /* (+12) */
    int dtimer;          /* disease timer (+14) */
    int flags;           /* battle: disease type / -1; story: ability bits (+16) */
    int id;              /* (+18) */
    int input;           /* (+1A) */
    int facing;          /* down 0, right 3, left 6, up 9 (+1C) */
    int wins;            /* (+1E) */
    int px, py;          /* previous tick position (render interpolation) */
} Player;

typedef struct {
    bool used;
    int frame, sub, fuse, cx, cy;
    int arm[4];          /* right, down, left, up (bomb table order of computation) */
    int owner, serial;
} Bomb;

typedef struct { bool used; int cx, cy, anim, cnt, idx; } Crumble;
typedef struct { bool used; int cx, cy, x, y, step, cnt; } Burn;

typedef struct {
    bool used;
    int x, y, px, py;    /* top-left; aligned = col*16+8,row*16+24 */
    int anim_base, anim; /* DS offsets of animation frame lists */
    int flen, facc;      /* frame length / accumulator */
    int dir;             /* 0 up 1 right 2 down 3 left; <0 frozen; after death: score index */
    int type;            /* >=0, -1 dying */
    int flags, acc, cool, timer;
    uint16_t spr_off, spr_seg;
} Enemy;

typedef struct {
    int mode;            /* 0 battle, 1..8 story world      DS:4F88 */
    int stage;           /* DS:4F8A */
    int lives, continues;
    int score, hiscore;
    int W, H, scroll;    /* map size, scroll mode 0/1/2 */
    uint8_t map[MAXW * MAXH + 64];
    int nplayers, wins_needed, skulls;
    Player pl[4];
    Bomb bomb[8];
    Crumble crumble[8];
    Burn burn[3];
    Enemy en[15];
    int enemy_count, kills_this_frame;
    bool enemy_killed;   /* DS:4F94 */
    int soft_left, hidden_pos;
    int flame[512][2], nflame;
    int serial;
    int speed;           /* DS:3A54, shared */
    int tmin, tsec, tsub; bool tstop, timeup; int timeup_ctr;
    int round_ctr;       /* DS:0A */
    int winner;          /* DS:0E */
    bool round_over;
    int dead_ctr;        /* story: frames after death */
    int exit_ctr;        /* story: exit animation */
    bool exit_ready;     /* DS:3A5E */
    int result;          /* story: 1 died, 2 cleared */
    int saved_bombs, saved_fire, saved_speed;
    int tick;
    int cam;             /* DS:203E */
    int frametoggle;
    int idle_ctr;
    bool demo; uint8_t *demo_data; int demo_len, demo_pos;
} Game;

extern Game G;

/* setup */
void battle_new_round(void);
void story_start_stage(void);
/* one 18.2 Hz logic tick; returns nothing, state in G */
void game_tick(void);
/* render scene; t = interpolation fraction between ticks */
void game_render(float t);
void draw_hud(void);

/* passwords (0x10fa2 / 0x10e26) */
void pw_encode(char out[9]);
bool pw_decode(const char *pw, int *world, int *stage, int *bombs, int *fire, int *speed);

#endif
