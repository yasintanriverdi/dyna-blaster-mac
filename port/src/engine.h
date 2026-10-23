/* Dyna Blaster native port - engine layer (data, graphics, audio, input). */
#ifndef ENGINE_H
#define ENGINE_H
#include <stdint.h>
#include <stdbool.h>

#define SCR_W 256
#define SCR_H 232
#define TICK_HZ 18.2065

/* ---------- original data segment (read from DYNA.EXE at runtime) ---------- */
extern uint8_t *DS;          /* initialised data segment of the unpacked DYNA.EXE */
extern int DS_len;
static inline uint16_t dsw(int off) { return (uint16_t)(DS[off] | (DS[off + 1] << 8)); }
static inline int16_t dss(int off) { return (int16_t)dsw(off); }
bool data_load(const char *gamedir);  /* unpack LZEXE, set DS */
const char *game_dir(void);

/* ---------- graphics ---------- */
enum { SEG_S5, SEG_S7, SEG_S8, SEG_NONE };
typedef struct { uint8_t pix[320 * 200]; uint8_t pal[768]; int rows; bool ok; } Sheet;
extern Sheet sheets[20];          /* indexed by GFX file number 1..19 */
extern Sheet *seg_sheet[3];       /* what S5/S7/S8 currently point to */
extern uint8_t fb[SCR_W * SCR_H]; /* indexed framebuffer */
extern uint8_t palette[768];      /* 8-bit RGB current target palette */
extern float pal_bright;          /* 0..1 fade */
extern int pal_flash;             /* >0: invert/flash effect */

bool gfx_load_all(void);
void gfx_bind(int s5, int s7, int s8);  /* GFX numbers */
void gfx_set_palette_from(int gfxnum);
int  seg_from_raw(uint16_t seg);
/* blit a rect of a sheet; transparent colour 3 when masked */
void blit(int sheet_seg, int sx, int sy, int w, int h, int dx, int dy, bool masked);
void blit_far(uint16_t off, uint16_t seg, int w, int h, int dx, int dy, bool masked);
void blit_far_solid(uint16_t off, uint16_t seg, int w, int h, int dx, int dy, uint8_t col);
void draw_tile(int idx, int dx, int dy);            /* opaque table DS:0EE2 */
void draw_sprite(int idx, int dx, int dy, bool silhouette); /* masked table DS:180C */
void fill_rect(int x, int y, int w, int h, uint8_t c);
void clear_fb(uint8_t c);
void draw_text(int x, int y, const char *s);        /* 8x7 font from 15.img */
void draw_bigdigit(int d, int x, int y);
void gfx_set_clip(int x0, int y0, int x1, int y1);
extern int cam_x, cam_y;   /* world->screen offset applied by draw_*_world */

/* ---------- audio ---------- */
void audio_init(void);
void audio_shutdown(void);
void sfx_play(int n);
void music_play(int song);
void music_stop(void);
void audio_update(void);
extern bool music_on, sfx_on;

/* ---------- input ---------- */
enum { IN_UP = 1, IN_DOWN = 2, IN_LEFT = 4, IN_RIGHT = 8, IN_BOMB = 0x10, IN_B = 0x20 };
void input_init(void);
uint8_t input_player(int p);      /* bits for player p using DYNA.CFG device mapping */
bool key_pressed_any(void);
int  menu_input(void);            /* edge-triggered bits for menus (keyboard 1 + pad 0) */

/* ---------- RNG (exact port of 0x139AC) ---------- */
void rng_seed(void);
uint16_t rnd(uint16_t n);

#endif
