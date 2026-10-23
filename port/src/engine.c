/* Engine layer: original-data loading (LZEXE unpack of DYNA.EXE), ILBM sheets,
 * software 256x232 indexed renderer, RNG and input.  All tables (tiles, sprites,
 * fonts) are read from the original data segment, see docs/re/engine.md. */
#include "engine.h"
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

uint8_t *DS;
int DS_len;
static char g_dir[1024];
const char *game_dir(void) { return g_dir; }

static uint8_t *read_file(const char *name, int *len) {
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", g_dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(n + 16);
    if (fread(b, 1, n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    fclose(f);
    if (len) *len = (int)n;
    return b;
}

/* ---- LZEXE 0.91 decompressor (same algorithm as the stub in DYNA.EXE) ---- */
static uint8_t *unlzexe(const uint8_t *d, int len, int *outlen) {
    int hdr = (d[8] | d[9] << 8) * 16;
    const uint8_t *src = d + hdr;
    int srclen = len - hdr, si = 0, cnt = 16;
    uint8_t *out = malloc(0x30000);
    int o = 0;
    uint16_t bp;
#define WORD() (si += 2, (uint16_t)(src[si - 2] | src[si - 1] << 8))
    bp = WORD();
#define BIT() ({ int _b = bp & 1; bp >>= 1; if (--cnt == 0) { bp = WORD(); cnt = 16; } _b; })
    while (si < srclen) {
        if (BIT()) { out[o++] = src[si++]; continue; }
        int n, off;
        if (!BIT()) {
            n = (BIT() << 1);
            n |= BIT();
            n += 2;
            off = (int)src[si++] - 256;
        } else {
            int lo = src[si], hi = src[si + 1];
            si += 2;
            off = (((((hi >> 3) | 0xE0) << 8) | lo)) - 0x10000;
            n = hi & 7;
            if (n) n += 2;
            else {
                int b = src[si++];
                if (b == 0) break;
                if (b == 1) continue;
                n = b + 1;
            }
        }
        while (n--) { out[o] = out[o + off]; o++; }
    }
#undef BIT
#undef WORD
    *outlen = o;
    return out;
}

bool data_load(const char *gamedir) {
    snprintf(g_dir, sizeof g_dir, "%s", gamedir);
    int len;
    uint8_t *exe = read_file("DYNA.EXE", &len);
    if (!exe) return false;
    int ilen;
    uint8_t *img = unlzexe(exe, len, &ilen);
    free(exe);
    /* entry: B8 xx xx  = mov ax, DSEG ; mov ds, ax */
    if (img[0] != 0xB8) return false;
    int dseg = (img[1] | img[2] << 8) * 16;
    DS_len = 0x10000;
    DS = calloc(1, DS_len);
    memcpy(DS, img + dseg, ilen - dseg < DS_len ? ilen - dseg : DS_len);
    free(img);
    return true;
}

/* ---------------- graphics ---------------- */
Sheet sheets[20];
Sheet *seg_sheet[3];
uint8_t fb[SCR_W * SCR_H];
uint8_t palette[768];
float pal_bright = 1.0f;
int pal_flash = 0;
int cam_x, cam_y;
static int clip_x0 = 0, clip_y0 = 0, clip_x1 = SCR_W, clip_y1 = SCR_H;

void gfx_set_clip(int x0, int y0, int x1, int y1) { clip_x0 = x0; clip_y0 = y0; clip_x1 = x1; clip_y1 = y1; }

static bool load_ilbm(int num, Sheet *s) {
    char name[32];
    snprintf(name, sizeof name, "GFX/%02d.IMG", num);
    int len;
    uint8_t *d = read_file(name, &len);
    if (!d) return false;
    /* palette: fixed offset 0x30 like 0x10503 (6-bit DAC emulation) */
    for (int i = 0; i < 768; i++) { int v = d[0x30 + i] >> 2; s->pal[i] = (uint8_t)(v << 2 | v >> 4); }
    int p = 0;
    while (p + 4 < len && memcmp(d + p, "BODY", 4)) p++;
    p += 8;
    uint8_t row[320];
    memset(s->pix, 0, sizeof s->pix);
    for (int y = 0; y < 200 && p < len; y++) {
        int n = 0;
        while (n < 320 && p < len) {
            int c = d[p++];
            if (c < 128) { for (int i = 0; i <= c && n < 320; i++) row[n++] = d[p++]; }
            else if (c > 128) { int v = d[p++]; for (int i = 0; i < 257 - c && n < 320; i++) row[n++] = (uint8_t)v; }
        }
        uint8_t *o = s->pix + y * 320;
        for (int pl = 0; pl < 8; pl++)
            for (int x = 0; x < 320; x++)
                if (row[pl * 40 + (x >> 3)] & (0x80 >> (x & 7))) o[x] |= (uint8_t)(1 << pl);
        s->rows = y + 1;
    }
    free(d);
    s->ok = true;
    return true;
}

bool gfx_load_all(void) {
    for (int i = 1; i <= 19; i++)
        if (!load_ilbm(i, &sheets[i])) { fprintf(stderr, "missing GFX/%02d.IMG\n", i); return false; }
    gfx_bind(5, 15, 2);
    return true;
}

void gfx_bind(int s5, int s7, int s8) {
    seg_sheet[SEG_S5] = &sheets[s5];
    seg_sheet[SEG_S7] = &sheets[s7];
    seg_sheet[SEG_S8] = &sheets[s8];
}

void gfx_set_palette_from(int n) { memcpy(palette, sheets[n].pal, 768); }

int seg_from_raw(uint16_t seg) {
    switch (seg) {
    case 0x1463: return SEG_S5;
    case 0x3170: return SEG_S7;
    case 0x4110: return SEG_S8;
    default: return SEG_NONE;
    }
}

void clear_fb(uint8_t c) { memset(fb, c, sizeof fb); }

void fill_rect(int x, int y, int w, int h, uint8_t c) {
    for (int j = y; j < y + h; j++) {
        if (j < clip_y0 || j >= clip_y1) continue;
        for (int i = x; i < x + w; i++)
            if (i >= clip_x0 && i < clip_x1) fb[j * SCR_W + i] = c;
    }
}

static void blit_sheet(const Sheet *s, int sx, int sy, int w, int h, int dx, int dy, int mode, uint8_t col) {
    for (int j = 0; j < h; j++) {
        int y = dy + j, syy = sy + j;
        if (y < clip_y0 || y >= clip_y1 || syy < 0 || syy >= 200) continue;
        const uint8_t *src = s->pix + syy * 320;
        for (int i = 0; i < w; i++) {
            int x = dx + i, sxx = sx + i;
            if (x < clip_x0 || x >= clip_x1 || sxx < 0 || sxx >= 320) continue;
            uint8_t c = src[sxx];
            if (mode == 0) fb[y * SCR_W + x] = c;
            else if (mode == 3) { if (c != 0x48) fb[y * SCR_W + x] = col; }
            else if (c != 3) fb[y * SCR_W + x] = mode == 2 ? col : c;
        }
    }
}

void blit(int seg, int sx, int sy, int w, int h, int dx, int dy, bool masked) {
    if (seg == SEG_NONE) return;
    blit_sheet(seg_sheet[seg], sx, sy, w, h, dx, dy, masked ? 1 : 0, 0);
}

static void far_xy(uint16_t off, uint16_t seg, int *s, int *x, int *y) {
    *s = seg_from_raw(seg);
    int r = off - (*s == SEG_S5 ? 0 : 8);
    if (r < 0) r = 0;
    *x = r % 320;
    *y = r / 320;
}

void blit_far(uint16_t off, uint16_t seg, int w, int h, int dx, int dy, bool masked) {
    int s, x, y;
    far_xy(off, seg, &s, &x, &y);
    if (s == SEG_NONE) return;
    blit_sheet(seg_sheet[s], x, y, w, h, dx, dy, masked ? 1 : 0, 0);
}

void blit_far_solid(uint16_t off, uint16_t seg, int w, int h, int dx, int dy, uint8_t col) {
    int s, x, y;
    far_xy(off, seg, &s, &x, &y);
    if (s == SEG_NONE) return;
    blit_sheet(seg_sheet[s], x, y, w, h, dx, dy, 2, col);
}

void draw_tile(int idx, int dx, int dy) {
    int o = 0x0EE2 + idx * 8;
    blit_far(dsw(o), dsw(o + 2), dsw(o + 4) * 4, dsw(o + 6), dx, dy, false);
}

void draw_sprite(int idx, int dx, int dy, bool silhouette) {
    int o = 0x180C + idx * 16;
    if (silhouette) blit_far_solid(dsw(o), dsw(o + 2), dsw(o + 4) * 4, dsw(o + 6), dx, dy, 15);
    else blit_far(dsw(o), dsw(o + 2), dsw(o + 4) * 4, dsw(o + 6), dx, dy, true);
}

/* brightest entry of the current palette: the font is drawn in a solid colour so it
 * stays legible under every screen palette */
static uint8_t text_colour(void) {
    int best = 15, bv = -1;
    for (int i = 0; i < 256; i++) {
        int v = palette[i * 3] + palette[i * 3 + 1] + palette[i * 3 + 2];
        if (v > bv) { bv = v; best = i; }
    }
    return (uint8_t)best;
}

/* 8x7 font: char map DS:383A, offset table DS:3865, glyphs in 15.IMG at off+0xC808 */
void draw_text(int x, int y, const char *s) {
    const char *map = (const char *)DS + 0x383A;
    int maplen = 0;
    while (map[maplen] && maplen < 43) maplen++;
    for (; *s; s++, x += 8) {
        char c = *s;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        int k = -1;
        for (int i = 0; i < maplen; i++) if (map[i] == c) { k = i; break; }
        if (k < 0) continue;
        int off = dsw(0x3865 + k * 2) + 0xC808 - 8;
        blit_sheet(&sheets[15], off % 320, off / 320, 8, 7, x, y, 3, text_colour());
    }
}

void draw_bigdigit(int d, int x, int y) { blit_sheet(&sheets[1], 200 + 8 * d, 95, 8, 13, x, y, 1, 0); }

/* ---------------- RNG: exact port of 0x1399B / 0x139AC ---------------- */
static uint8_t rng_tab[140];
static int rng_idx;
void rng_seed(void) {
    memcpy(rng_tab, DS + 0x38BB, 132);
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    int hs = (int)(GetTime() * 100) % 100;
    rng_idx = (hs ^ lt->tm_hour ^ lt->tm_min ^ lt->tm_sec) & 0x7F;
}
uint16_t rnd(uint16_t n) {
    uint8_t *p = rng_tab + rng_idx;
    uint16_t v = (uint16_t)(p[0] | p[1] << 8);
    rng_idx = (rng_idx + v + 1) & 0x7F;
    uint16_t x = (uint16_t)(p[1] | p[2] << 8);
    p[0] ^= (uint8_t)x;
    p[1] ^= (uint8_t)(x >> 8);
    return n ? (uint16_t)(v % n) : 0;
}

/* ---------------- input ---------------- */
static int xt2ray(int sc) {
    static const int t[0x60] = {
        [0x01] = KEY_ESCAPE, [0x02] = KEY_ONE, [0x03] = KEY_TWO, [0x04] = KEY_THREE, [0x05] = KEY_FOUR,
        [0x06] = KEY_FIVE, [0x07] = KEY_SIX, [0x08] = KEY_SEVEN, [0x09] = KEY_EIGHT, [0x0A] = KEY_NINE,
        [0x0B] = KEY_ZERO, [0x0E] = KEY_BACKSPACE, [0x0F] = KEY_TAB, [0x10] = KEY_Q, [0x11] = KEY_W,
        [0x12] = KEY_E, [0x13] = KEY_R, [0x14] = KEY_T, [0x15] = KEY_Y, [0x16] = KEY_U, [0x17] = KEY_I,
        [0x18] = KEY_O, [0x19] = KEY_P, [0x1C] = KEY_ENTER, [0x1D] = KEY_LEFT_CONTROL, [0x1E] = KEY_A,
        [0x1F] = KEY_S, [0x20] = KEY_D, [0x21] = KEY_F, [0x22] = KEY_G, [0x23] = KEY_H, [0x24] = KEY_J,
        [0x25] = KEY_K, [0x26] = KEY_L, [0x2A] = KEY_LEFT_SHIFT, [0x2C] = KEY_Z, [0x2D] = KEY_X,
        [0x2E] = KEY_C, [0x2F] = KEY_V, [0x30] = KEY_B, [0x31] = KEY_N, [0x32] = KEY_M,
        [0x36] = KEY_RIGHT_SHIFT, [0x38] = KEY_LEFT_ALT, [0x39] = KEY_SPACE, [0x47] = KEY_HOME,
        [0x48] = KEY_UP, [0x49] = KEY_PAGE_UP, [0x4B] = KEY_LEFT, [0x4D] = KEY_RIGHT, [0x4F] = KEY_END,
        [0x50] = KEY_DOWN, [0x51] = KEY_PAGE_DOWN, [0x52] = KEY_INSERT, [0x53] = KEY_DELETE,
    };
    return (sc > 0 && sc < 0x60) ? t[sc] : 0;
}

static int keyset[2][6];
static int device[4] = {0, 1, 2, 3};

void input_init(void) {
    int len = 0;
    uint8_t *cfg = read_file("DYNA.CFG", &len);
    static const uint8_t def[28] = {0xC8, 0x48, 0xD0, 0x50, 0xCB, 0x4B, 0xCD, 0x4D, 0xB9, 0x39, 0x9D, 0x1D,
                                    0x97, 0x17, 0xA5, 0x25, 0xA4, 0x24, 0xA6, 0x26, 0x9E, 0x1E, 0, 1, 2, 3, 0, 0};
    const uint8_t *c = (cfg && len >= 26) ? cfg : def;
    for (int i = 0; i < 6; i++) keyset[0][i] = xt2ray(c[1 + 2 * i]);
    for (int i = 0; i < 5; i++) keyset[1][i] = xt2ray(c[13 + 2 * i]);
    keyset[1][5] = 0;
    for (int i = 0; i < 4; i++) device[i] = c[22 + i] <= 5 ? c[22 + i] : i;
    free(cfg);
}

static uint8_t keys_bits(int set) {
    uint8_t b = 0;
    for (int i = 0; i < 6; i++)
        if (keyset[set][i] && IsKeyDown(keyset[set][i])) b |= (uint8_t)(1 << i);
    return b;
}

static uint8_t pad_bits(int pad) {
    if (!IsGamepadAvailable(pad)) return 0;
    uint8_t b = 0;
    float ax = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_LEFT_X), ay = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_LEFT_Y);
    if (ay < -0.5f || IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_UP)) b |= IN_UP;
    if (ay > 0.5f || IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) b |= IN_DOWN;
    if (ax < -0.5f || IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) b |= IN_LEFT;
    if (ax > 0.5f || IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) b |= IN_RIGHT;
    if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) b |= IN_BOMB;
    if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) b |= IN_B;
    return b;
}

uint8_t input_player(int p) {
    int d = device[p & 3];
    uint8_t b;
    switch (d) {
    case 0: b = keys_bits(0); break;
    case 1: b = keys_bits(1); break;
    case 2: case 4: b = pad_bits(0); break;
    default: b = pad_bits(1); break;
    }
    if (p == 0 && d < 2) b |= pad_bits(0);   /* pad 0 also drives P1 */
    return b;
}

bool key_pressed_any(void) {
    if (GetKeyPressed()) return true;
    for (int p = 0; p < 2; p++)
        if (IsGamepadAvailable(p) && (IsGamepadButtonPressed(p, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) ||
                                     IsGamepadButtonPressed(p, GAMEPAD_BUTTON_MIDDLE_RIGHT)))
            return true;
    return false;
}

int menu_input(void) {
    static uint8_t prev;
    uint8_t cur = keys_bits(0) | pad_bits(0);
    if (IsKeyDown(KEY_ENTER) || IsKeyDown(KEY_SPACE)) cur |= IN_BOMB;
    if (IsKeyDown(KEY_ESCAPE)) cur |= 0x80;
    uint8_t edge = cur & ~prev;
    prev = cur;
    return edge;
}
