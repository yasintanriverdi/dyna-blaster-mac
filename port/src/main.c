/* Dyna Blaster native port - main loop, screens and menus.
 * Logic runs at the original 18.2065 Hz (INT 1Ch) rate; rendering at 60 FPS with
 * interpolated sprite positions. Usage: dynablaster [path-to-game-files] */
#include "game.h"
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { S_TITLE, S_BSETUP, S_BATTLE, S_BRESULT, S_WORLDMAP, S_SINTRO, S_STORY, S_GAMEOVER, S_PASSWORD, S_ENDING, S_QUIT } Scene;
static Scene scene = S_TITLE;
static double scene_time;
static int menu_sel;
static char pw_buf[9];
static int pw_len;
static char pw_show[9];
static bool match_over;
static bool paused;

static void set_scene(Scene s) { scene = s; scene_time = 0; menu_sel = 0; pal_bright = 0; }

static int world_sheet(int world) {
    int idx = dsw(dsw(0x4FBC + world * 2));
    int rec = dsw(0x56BC + 2 * idx);
    const char *nm = (const char *)DS + rec + 8;     /* "gfx\NN.img" */
    return atoi(nm + 4);
}

static void center_text(int y, const char *s) { draw_text((SCR_W - (int)strlen(s) * 8) / 2, y, s); }

/* ---------------- scene entry helpers ---------------- */
static void start_battle_round(void) {
    gfx_bind(5, 15, 2);
    gfx_set_palette_from(5);
    battle_new_round();
    music_play(0);
    set_scene(S_BATTLE);
}

static void start_story_stage(void) {
    int ws = world_sheet(G.mode);
    gfx_bind(ws, (G.mode == 8 && G.stage == 8) ? 14 : 4, 1);
    gfx_set_palette_from(ws);
    set_scene(S_SINTRO);
    music_play(3);
}

static void new_story_game(int world, int stage, int bombs, int fire, int speed) {
    memset(&G, 0, sizeof G);
    G.mode = world; G.stage = stage; G.lives = 2; G.continues = 3;
    G.pl[0].maxbombs = bombs; G.pl[0].fire = fire; G.speed = speed;
    if (stage == 1) { set_scene(S_WORLDMAP); music_play(8); gfx_set_palette_from(18); }
    else start_story_stage();
}

/* ---------------- per-scene update/draw ---------------- */
static void start_demo(void) {
    char path[1200];
    snprintf(path, sizeof path, "%s/DEMO.DAT", game_dir());
    int len = 0;
    unsigned char *d = LoadFileData(path, &len);
    if (!d) return;
    memset(&G, 0, sizeof G);
    G.nplayers = 4; G.wins_needed = 1; G.skulls = 0;
    G.demo = true; G.demo_data = d; G.demo_len = len;
    start_battle_round();
}

static void title_scene(int in) {
    if (scene_time > 20.0) { start_demo(); return; }
    gfx_bind(19, 15, 1);
    gfx_set_palette_from(19);
    clear_fb(0);
    blit(SEG_S5, 0, 0, 256, 146, 0, 8, false);
    static const char *items[4] = {"game start", "battle", "password", "exit"};
    for (int i = 0; i < 4; i++) {
        draw_text(96, 164 + i * 12, items[i]);
        if (i == menu_sel) draw_text(80, 164 + i * 12, ">");
    }
    center_text(218, "copyright 1991.1992 hudson soft");
    if (in & IN_UP) menu_sel = (menu_sel + 3) % 4;
    if (in & IN_DOWN) menu_sel = (menu_sel + 1) % 4;
    if (in & IN_BOMB) {
        sfx_play(4);
        switch (menu_sel) {
        case 0: new_story_game(1, 1, 1, 1, 0x240); break;
        case 1: memset(&G, 0, sizeof G); G.nplayers = 2; G.wins_needed = 3; G.skulls = 1; set_scene(S_BSETUP); music_play(13); break;
        case 2: pw_len = 0; memset(pw_buf, 0, sizeof pw_buf); set_scene(S_PASSWORD); break;
        case 3: scene = S_QUIT; break;
        }
    }
    if (in & 0x80) scene = S_QUIT;
}

static void bsetup_scene(int in) {
    gfx_bind(5, 15, 2);
    gfx_set_palette_from(5);
    clear_fb(0);
    center_text(30, "please select game mode");
    char b[64];
    snprintf(b, sizeof b, "how many bombers   %d", G.nplayers);
    draw_text(40, 80, b);
    snprintf(b, sizeof b, "fighting for      %d", G.wins_needed);
    draw_text(40, 100, b);
    snprintf(b, sizeof b, "skulls            %s", G.skulls ? "on" : "off");
    draw_text(40, 120, b);
    draw_text(40, 140, "start");
    draw_text(24, 80 + menu_sel * 20, ">");
    for (int i = 0; i < G.nplayers; i++) draw_sprite(dsw(0x3E4C + 2 * i), 60 + i * 40, 170, false);
    center_text(205, "press left.right to change");
    if (in & IN_UP) menu_sel = (menu_sel + 3) % 4;
    if (in & IN_DOWN) menu_sel = (menu_sel + 1) % 4;
    int d = (in & IN_RIGHT) ? 1 : (in & IN_LEFT) ? -1 : 0;
    if (menu_sel == 0 && d) G.nplayers = 2 + (G.nplayers - 2 + d + 3) % 3;
    if (menu_sel == 1 && d) G.wins_needed = 1 + (G.wins_needed - 1 + d + 5) % 5;
    if (menu_sel == 2 && d) G.skulls ^= 1;
    if ((in & IN_BOMB) && menu_sel == 3) { match_over = false; for (int i = 0; i < 4; i++) G.pl[i].wins = 0; start_battle_round(); }
    if (in & 0x80) { set_scene(S_TITLE); music_play(7); }
}

static void bresult_scene(int in) {
    clear_fb(0);
    if (G.winner < 0) center_text(30, "draw game");
    else {
        char b[40];
        snprintf(b, sizeof b, "player %d wins", G.winner + 1);
        center_text(30, b);
    }
    for (int i = 0; i < G.nplayers; i++) {
        int y = 60 + i * 36;
        draw_sprite(dsw(0x3E4C + 2 * G.pl[i].id), 40, y, false);
        for (int w = 0; w < G.pl[i].wins; w++) draw_tile(57, 80 + w * 24, y);  /* trophy */
    }
    if (match_over) center_text(210, "push fire button !");
    if (scene_time > 1.5 && (in & IN_BOMB)) {
        if (match_over) { set_scene(S_TITLE); music_play(7); }
        else start_battle_round();
    }
}

static void worldmap_scene(int in) {
    gfx_bind(18, 15, 1);
    gfx_set_palette_from(18);
    clear_fb(0);
    blit(SEG_S5, 0, 0, 256, 160, 0, 24, false);
    char b[32];
    snprintf(b, sizeof b, "round %d", G.mode);
    center_text(200, b);
    if (scene_time > 4.0 || (scene_time > 0.5 && (in & IN_BOMB))) start_story_stage();
}

static void sintro_scene(int in) {
    (void)in;
    clear_fb(0);
    char b[32];
    snprintf(b, sizeof b, "stage %d-%d", G.mode, G.stage);
    blit(SEG_S8, 208, 64, 112, 30, 72, 80, true);   /* "STAGE" picture */
    draw_bigdigit(G.mode, 112, 120);
    draw_text(122, 123, "-");
    draw_bigdigit(G.stage, 132, 120);
    if (scene_time > 2.2) {
        story_start_stage();
        music_play(G.stage == 8 ? 11 : (G.stage & 1) ? 12 : 13);
        set_scene(S_STORY);
        pal_bright = 0;
    }
}

static void gameover_scene(int in) {
    clear_fb(0);
    blit(SEG_S8, 200, 108, 116, 27, 70, 40, true);   /* "GAME OVER" */
    center_text(90, "password");
    center_text(102, pw_show);
    char b[32];
    snprintf(b, sizeof b, "continue  %d", G.continues);
    if (G.continues > 0) {
        center_text(140, b);
        draw_text(96, 160, "yes");
        draw_text(96, 172, "no");
        draw_text(80, 160 + menu_sel * 12, ">");
        if (in & (IN_UP | IN_DOWN)) menu_sel ^= 1;
    }
    if (scene_time > 1.0 && (in & IN_BOMB)) {
        if (G.continues > 0 && menu_sel == 0) {
            G.continues--; G.score = 0; G.lives = 2;
            start_story_stage();
        } else { set_scene(S_TITLE); music_play(7); }
    }
}

static void password_scene(int in) {
    gfx_bind(19, 15, 1);
    gfx_set_palette_from(19);
    clear_fb(0);
    center_text(40, "enter password ......");
    center_text(60, pw_buf);
    static const char *rows[3] = {"a b c d e f g h i j", "k l m n o p q r s t", "u v w x y z < >  "};
    for (int i = 0; i < 3; i++) center_text(100 + i * 14, rows[i]);
    center_text(170, "type letters. return to confirm");
    int c;
    while ((c = GetCharPressed()) != 0) {
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c >= 'a' && c <= 'z' && pw_len < 8) { pw_buf[pw_len++] = (char)c; pw_buf[pw_len] = 0; }
    }
    if (IsKeyPressed(KEY_BACKSPACE) && pw_len > 0) pw_buf[--pw_len] = 0;
    if (IsKeyPressed(KEY_ENTER) && pw_len == 8) {
        int w, s, bo, fi, sp;
        if (pw_decode(pw_buf, &w, &s, &bo, &fi, &sp)) { sfx_play(1); new_story_game(w, s, bo, fi, sp); }
        else { pw_len = 0; pw_buf[0] = 0; sfx_play(0); }
    }
    if (in & 0x80) { set_scene(S_TITLE); }
}

static void ending_scene(int in) {
    gfx_bind(17, 16, 1);
    gfx_set_palette_from(16);
    clear_fb(0);
    blit(SEG_S7, 0, 0, 256, 200, 0, (int)(232 - scene_time * 12) % 432 - 200, false);
    center_text(110, "congratulations");
    if (scene_time > 5 && (in & IN_BOMB)) { set_scene(S_TITLE); music_play(7); }
}

/* ---------------- game-data folder discovery ----------------
 * The original files are not shipped. Order: command line, saved choice in
 * ~/Library/Application Support/DynaBlaster/datadir, folders next to the binary /
 * .app bundle, ./dyna, then a native "choose folder" dialog (osascript). */
static bool has_data(const char *d) {
    char p[1200];
    snprintf(p, sizeof p, "%s/DYNA.EXE", d);
    return FileExists(p);
}

static const char *find_data_dir(int argc, char **argv) {
    static char buf[1024];
    if (argc > 1 && argv[1][0] != '-') return argv[1];
    const char *home = getenv("HOME");
    char cfg[1100] = "";
    if (home) {
        snprintf(cfg, sizeof cfg, "%s/Library/Application Support/DynaBlaster", home);
        char f[1200];
        snprintf(f, sizeof f, "%s/datadir", cfg);
        FILE *fp = fopen(f, "r");
        if (fp) {
            if (fgets(buf, sizeof buf, fp)) { buf[strcspn(buf, "\r\n")] = 0; }
            fclose(fp);
            if (has_data(buf)) return buf;
        }
    }
    const char *app = GetApplicationDirectory();   /* .../Dyna Blaster.app/Contents/MacOS/ */
    const char *cands[] = {"dyna", "../dyna", "../../../dyna", "../../..", "../../../DYNA"};
    for (size_t i = 0; i < sizeof cands / sizeof *cands; i++) {
        snprintf(buf, sizeof buf, "%s%s", app, cands[i]);
        if (has_data(buf)) return buf;
    }
    if (has_data("dyna")) return "dyna";
    FILE *p = popen("osascript -e 'POSIX path of (choose folder with prompt "
                    "\"Select your original Dyna Blaster folder (with DYNA.EXE, GFX, MUSIC.DAT, DIGI.DAT)\")' 2>/dev/null", "r");
    if (p) {
        buf[0] = 0;
        if (fgets(buf, sizeof buf, p)) buf[strcspn(buf, "\r\n")] = 0;
        pclose(p);
        size_t n = strlen(buf);
        if (n > 1 && buf[n - 1] == '/') buf[n - 1] = 0;
        if (has_data(buf) && cfg[0]) {
            char cmd[1300];
            snprintf(cmd, sizeof cmd, "mkdir -p \"%s\"", cfg);
            system(cmd);
            char f[1200];
            snprintf(f, sizeof f, "%s/datadir", cfg);
            FILE *fp = fopen(f, "w");
            if (fp) { fprintf(fp, "%s\n", buf); fclose(fp); }
            return buf;
        }
    }
    return NULL;
}

/* ---------------- main ---------------- */
int main(int argc, char **argv) {
    const char *dir = find_data_dir(argc, argv);
    if (!dir) {
        system("osascript -e 'display alert \"Dyna Blaster\" message \"The original game files were not found. "
               "Start again and select the folder that contains DYNA.EXE.\" as critical' >/dev/null 2>&1");
        return 1;
    }
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(SCR_W * 4, SCR_H * 4, "Dyna Blaster");
    SetExitKey(0);
    SetTargetFPS(60);
    if (!data_load(dir) || !gfx_load_all()) {
        fprintf(stderr, "Could not load game data from '%s' (needs DYNA.EXE, GFX/, DIGI.DAT, MUSIC.DAT)\n", dir);
        CloseWindow();
        return 1;
    }
    rng_seed();
    input_init();
    audio_init();
    Image img = GenImageColor(SCR_W, SCR_H, BLACK);
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    static uint32_t rgba[SCR_W * SCR_H];
    music_play(7);
    int shot_frames = -1, frame_no = 0;
    const char *shot_file = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--demo")) start_demo();
        else if (!strcmp(argv[i], "--battle")) { memset(&G, 0, sizeof G); G.nplayers = 4; G.wins_needed = 3; G.skulls = 1; start_battle_round(); }
        else if (!strcmp(argv[i], "--story") && i + 2 < argc) { new_story_game(atoi(argv[i + 1]), atoi(argv[i + 2]), 1, 1, 0x240); i += 2; }
        else if (!strcmp(argv[i], "--shot") && i + 2 < argc) { shot_frames = atoi(argv[i + 1]); shot_file = argv[i + 2]; i += 2; }
    }
    const double step = 1.0 / TICK_HZ;
    double acc = 0;
    while (!WindowShouldClose() && scene != S_QUIT) {
        double dt = GetFrameTime();
        if (dt > 0.25) dt = 0.25;
        scene_time += dt;
        if (IsKeyPressed(KEY_F11) || (IsKeyDown(KEY_LEFT_SUPER) && IsKeyPressed(KEY_F))) ToggleBorderlessWindowed();
        if (IsKeyPressed(KEY_M)) { music_on = !music_on; }
        if (IsKeyPressed(KEY_S) && scene != S_PASSWORD) sfx_on = !sfx_on;
        int in = menu_input();
        if (pal_bright < 1.0f) { pal_bright += (float)dt * 3.0f; if (pal_bright > 1) pal_bright = 1; }

        if (scene == S_BATTLE || scene == S_STORY) {
            if (IsKeyPressed(KEY_P) && !(scene == S_STORY && G.stage == 8)) paused = !paused;
            if ((in & 0x80) || (G.demo && (in || (scene == S_BATTLE && G.round_over)))) {
                paused = false;
                if (G.demo) { UnloadFileData(G.demo_data); G.demo = false; G.demo_data = NULL; }
                set_scene(S_TITLE); music_play(7); continue;
            }
            if (!paused) {
                acc += dt;
                while (acc >= step) {
                    acc -= step;
                    game_tick();
                    if (scene == S_BATTLE && G.round_over) break;
                    if (scene == S_STORY && G.result) break;
                }
            }
            if (G.score > G.hiscore) G.hiscore = G.score;
            game_render(paused ? 1.0f : (float)(acc / step));
            if (paused) center_text(120, "pause");
            if (scene == S_BATTLE && G.round_over) {
                if (G.winner >= 0 && ++G.pl[G.winner].wins >= G.wins_needed) { match_over = true; music_play(4); }
                else music_play(G.winner >= 0 ? 1 : 2);
                set_scene(S_BRESULT);
            } else if (scene == S_STORY && G.result == 1) {
                G.pl[0].maxbombs = G.saved_bombs; G.pl[0].fire = G.saved_fire; G.speed = G.saved_speed;
                G.pl[0].flags = 0;
                if (G.lives > 0) { G.lives--; start_story_stage(); }
                else { pw_encode(pw_show); set_scene(S_GAMEOVER); music_play(6); }
            } else if (scene == S_STORY && G.result == 2) {
                if (G.stage < 8) { G.stage++; start_story_stage(); }
                else if (G.mode < 8) { G.mode++; G.stage = 1; set_scene(S_WORLDMAP); music_play(8); }
                else { set_scene(S_ENDING); music_play(14); }
            }
        } else {
            acc = 0;
            switch (scene) {
            case S_TITLE: title_scene(in); break;
            case S_BSETUP: bsetup_scene(in); break;
            case S_BRESULT: bresult_scene(in); break;
            case S_WORLDMAP: worldmap_scene(in); break;
            case S_SINTRO: sintro_scene(in); break;
            case S_GAMEOVER: gameover_scene(in); break;
            case S_PASSWORD: password_scene(in); break;
            case S_ENDING: ending_scene(in); break;
            default: break;
            }
        }

        for (int i = 0; i < SCR_W * SCR_H; i++) {
            const uint8_t *c = palette + fb[i] * 3;
            uint32_t r = (uint32_t)(c[0] * pal_bright), g = (uint32_t)(c[1] * pal_bright), b = (uint32_t)(c[2] * pal_bright);
            rgba[i] = 0xFF000000u | b << 16 | g << 8 | r;
        }
        UpdateTexture(tex, rgba);
        if (shot_file && ++frame_no == shot_frames) {
            Image si = {.data = rgba, .width = SCR_W, .height = SCR_H, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            ExportImage(si, shot_file);
            break;
        }
        BeginDrawing();
        ClearBackground(BLACK);
        float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
        float scale = sw / SCR_W < sh / SCR_H ? sw / SCR_W : sh / SCR_H;
        float w = SCR_W * scale, h = SCR_H * scale;
        DrawTexturePro(tex, (Rectangle){0, 0, SCR_W, SCR_H}, (Rectangle){(sw - w) / 2, (sh - h) / 2, w, h}, (Vector2){0, 0}, 0, WHITE);
        EndDrawing();
    }
    UnloadTexture(tex);
    audio_shutdown();
    CloseWindow();
    return 0;
}
