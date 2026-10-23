/* Audio: DIGI.DAT (5 concatenated VOC files) as sound effects, MUSIC.DAT (15
 * concatenated CMF songs) played through a CMF sequencer driving Nuked-OPL3,
 * replacing the original Creative CT-VOICE / SBFMDRV drivers. */
#include "engine.h"
#include "raylib.h"
#include "../third_party/nuked/opl3.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 44100

bool music_on = true, sfx_on = true;
static Sound sfx[8];
static int nsfx;
static uint8_t *music_dat;
static int music_len;
static int song_off[16], nsongs;

/* ---------------- CMF sequencer ---------------- */
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static opl3_chip chip;
static AudioStream stream;
static const uint8_t *cmf;          /* current song */
static const uint8_t *ins;          /* instrument block */
static int nins, mpos, mstart, cmf_len;
static double tick_acc, samples_per_tick;
static int wait_ticks;
static uint8_t running;
static bool playing, rhythm;
static int chan_prog[16];
static struct { int chan, note, age; bool on; } voice[9];
static int age_ctr;
static uint8_t bd_bits;

static const uint8_t op_mod[9] = {0, 1, 2, 8, 9, 10, 16, 17, 18};

static void wr(int r, int v) { OPL3_WriteReg(&chip, (uint16_t)r, (uint8_t)v); }

static void set_op(int op, const uint8_t *i, bool car) {
    int k = car ? 1 : 0;
    wr(0x20 + op, i[0 + k]);
    wr(0x40 + op, i[2 + k]);
    wr(0x60 + op, i[4 + k]);
    wr(0x80 + op, i[6 + k]);
    wr(0xE0 + op, i[8 + k]);
}

static const uint8_t *instr(int ch) {
    int p = chan_prog[ch];
    if (p >= nins) p = 0;
    return ins + p * 16;
}

static void freq(int v, int note, bool on) {
    static const uint16_t fn[12] = {0x157, 0x16B, 0x181, 0x198, 0x1B0, 0x1CA, 0x1E5, 0x202, 0x220, 0x241, 0x263, 0x287};
    int blk = note / 12 - 1;
    if (blk < 0) blk = 0;
    if (blk > 7) blk = 7;
    int f = fn[note % 12];
    wr(0xA0 + v, f & 0xFF);
    wr(0xB0 + v, (on ? 0x20 : 0) | blk << 2 | f >> 8);
}

static void note_off(int ch, int note) {
    if (rhythm && ch >= 11) {
        bd_bits &= (uint8_t)~(1 << (15 - ch));
        wr(0xBD, 0x20 | bd_bits);
        return;
    }
    int nv = rhythm ? 6 : 9;
    for (int v = 0; v < nv; v++)
        if (voice[v].on && voice[v].chan == ch && voice[v].note == note) {
            voice[v].on = false;
            freq(v, note, false);
        }
}

static void note_on(int ch, int note, int vel) {
    if (vel == 0) { note_off(ch, note); return; }
    const uint8_t *i = instr(ch);
    int atten = (127 - vel) >> 2;
    if (rhythm && ch >= 11) {
        int bit = 15 - ch; /* 11 BD(4) 12 SD(3) 13 TOM(2) 14 CYM(1) 15 HH(0) */
        switch (ch) {
        case 11: set_op(op_mod[6], i, false); set_op(op_mod[6] + 3, i, true); wr(0xC0 + 6, i[10]); freq(6, note, false); break;
        case 12: set_op(op_mod[7] + 3, i, false); freq(7, note, false); break;
        case 13: set_op(op_mod[8], i, false); freq(8, note, false); break;
        case 14: set_op(op_mod[8] + 3, i, false); freq(8, note, false); break;
        case 15: set_op(op_mod[7], i, false); freq(7, note, false); break;
        }
        bd_bits &= (uint8_t)~(1 << bit);
        wr(0xBD, 0x20 | bd_bits);
        bd_bits |= (uint8_t)(1 << bit);
        wr(0xBD, 0x20 | bd_bits);
        return;
    }
    int nv = rhythm ? 6 : 9, best = 0, bestage = 1 << 30;
    for (int v = 0; v < nv; v++) {
        int a = voice[v].on ? voice[v].age + (1 << 20) : voice[v].age;
        if (a < bestage) { bestage = a; best = v; }
    }
    int v = best;
    freq(v, voice[v].note, false);
    set_op(op_mod[v], i, false);
    set_op(op_mod[v] + 3, i, true);
    int tl = (i[3] & 0x3F) + atten;
    wr(0x40 + op_mod[v] + 3, (i[3] & 0xC0) | (tl > 63 ? 63 : tl));
    wr(0xC0 + v, i[10] | 0x30);
    voice[v].chan = ch; voice[v].note = note; voice[v].on = true; voice[v].age = ++age_ctr;
    freq(v, note, true);
}

static int varlen(void) {
    int v = 0;
    for (int k = 0; k < 4 && mpos < cmf_len; k++) {
        int b = cmf[mpos++];
        v = v << 7 | (b & 0x7F);
        if (!(b & 0x80)) break;
    }
    return v;
}

static void all_off(void) {
    for (int v = 0; v < 9; v++) { voice[v].on = false; wr(0xB0 + v, 0); }
    bd_bits = 0;
    wr(0xBD, rhythm ? 0x20 : 0);
}

static void seq_events(void) {
    while (playing && wait_ticks <= 0) {
        if (mpos >= cmf_len) { mpos = mstart; all_off(); wait_ticks = varlen(); continue; }
        int st = cmf[mpos];
        if (st & 0x80) mpos++; else st = running;
        if (st < 0xF0) running = (uint8_t)st;
        int ch = st & 15;
        switch (st & 0xF0) {
        case 0x80: { int n = cmf[mpos++]; mpos++; note_off(ch, n); break; }
        case 0x90: { int n = cmf[mpos++], v = cmf[mpos++]; note_on(ch, n, v); break; }
        case 0xA0: mpos += 2; break;
        case 0xB0: {
            int c = cmf[mpos++], v = cmf[mpos++];
            if (c == 0x67) { rhythm = v != 0; all_off(); }
            break;
        }
        case 0xC0: chan_prog[ch] = cmf[mpos++]; break;
        case 0xD0: mpos++; break;
        case 0xE0: mpos += 2; break;
        default:
            if (st == 0xFF) {
                int type = cmf[mpos++];
                int len = varlen();
                mpos += len;
                if (type == 0x2F) { mpos = mstart; all_off(); }
            } else if (st == 0xF0 || st == 0xF7) { int len = varlen(); mpos += len; }
            break;
        }
        wait_ticks += varlen();
    }
}

static void stream_cb(void *buf, unsigned int frames) {
    int16_t *o = buf;
    pthread_mutex_lock(&mtx);
    for (unsigned i = 0; i < frames; i++) {
        if (playing && music_on) {
            tick_acc += 1.0;
            while (tick_acc >= samples_per_tick) { tick_acc -= samples_per_tick; wait_ticks--; seq_events(); }
        }
        int16_t s[2];
        OPL3_GenerateResampled(&chip, s);
        if (!music_on) s[0] = s[1] = 0;
        o[2 * i] = s[0];
        o[2 * i + 1] = s[1];
    }
    pthread_mutex_unlock(&mtx);
}

void music_play(int song) {
    if (song < 0 || song >= nsongs) return;
    pthread_mutex_lock(&mtx);
    OPL3_Reset(&chip, RATE);
    wr(0x01, 0x20);
    cmf = music_dat + song_off[song];
    cmf_len = (song + 1 < nsongs ? song_off[song + 1] : music_len) - song_off[song];
    int ioff = cmf[6] | cmf[7] << 8;
    mstart = cmf[8] | cmf[9] << 8;
    int tps = cmf[12] | cmf[13] << 8;
    nins = cmf[0x24] | cmf[0x25] << 8;
    ins = cmf + ioff;
    samples_per_tick = (double)RATE / (tps ? tps : 96);
    memset(chan_prog, 0, sizeof chan_prog);
    for (int c = 0; c < 16; c++) chan_prog[c] = c < nins ? c : 0;
    memset(voice, 0, sizeof voice);
    rhythm = false;
    bd_bits = 0;
    mpos = mstart;
    running = 0;
    tick_acc = 0;
    wait_ticks = varlen();
    playing = true;
    pthread_mutex_unlock(&mtx);
}

void music_stop(void) {
    pthread_mutex_lock(&mtx);
    playing = false;
    all_off();
    pthread_mutex_unlock(&mtx);
}

/* ---------------- SFX ---------------- */
void sfx_play(int n) {
    if (!sfx_on || n < 0 || n >= nsfx) return;
    for (int i = 0; i < nsfx; i++) StopSound(sfx[i]);   /* CT-VOICE: one voice, stop then play */
    PlaySound(sfx[n]);
}

static uint8_t *slurp(const char *name, int *len) {
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", game_dir(), name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *len = (int)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*len);
    if (fread(b, 1, *len, f) != (size_t)*len) { free(b); fclose(f); return NULL; }
    fclose(f);
    return b;
}

void audio_init(void) {
    InitAudioDevice();
    int len;
    uint8_t *d = slurp("DIGI.DAT", &len);
    if (d) {
        for (int p = 0; p + 26 < len && nsfx < 8; p++) {
            if (memcmp(d + p, "Creative Voice File", 19)) continue;
            int b = p + (d[p + 20] | d[p + 21] << 8);
            if (d[b] != 1) continue;
            int bl = d[b + 1] | d[b + 2] << 8 | d[b + 3] << 16;
            int rate = 1000000 / (256 - d[b + 4]);
            Wave w = {.frameCount = (unsigned)(bl - 2), .sampleRate = (unsigned)rate, .sampleSize = 8, .channels = 1};
            w.data = malloc(bl - 2);
            memcpy(w.data, d + b + 6, bl - 2);
            sfx[nsfx++] = LoadSoundFromWave(w);
            UnloadWave(w);
            p = b + 4 + bl;
        }
        free(d);
    }
    music_dat = slurp("MUSIC.DAT", &music_len);
    if (music_dat)
        for (int p = 0; p + 4 < music_len && nsongs < 16; p++)
            if (!memcmp(music_dat + p, "CTMF", 4)) song_off[nsongs++] = p;
    OPL3_Reset(&chip, RATE);
    SetAudioStreamBufferSizeDefault(2048);
    stream = LoadAudioStream(RATE, 16, 2);
    SetAudioStreamCallback(stream, stream_cb);
    PlayAudioStream(stream);
}

void audio_update(void) {}

void audio_shutdown(void) {
    music_stop();
    UnloadAudioStream(stream);
    for (int i = 0; i < nsfx; i++) UnloadSound(sfx[i]);
    CloseAudioDevice();
}
