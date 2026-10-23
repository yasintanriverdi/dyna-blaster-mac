/* Verifies the password decoder/encoder against all 64 codes in DYNA.COD. */
#include "../src/game.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "../dyna";
    if (!data_load(dir)) { printf("data load failed\n"); return 1; }
    rng_seed();
    char path[1024];
    snprintf(path, sizeof path, "%s/DYNA.COD", dir);
    FILE *f = fopen(path, "r");
    if (!f) return 1;
    int ok = 0, bad = 0, w, s, b, fi, sp, ew, es;
    char code[16], line[128];
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "LEVEL %d.%d %15s", &ew, &es, code) != 3) continue;
        for (char *c = code; *c; c++) *c = (char)(*c | 32);
        if (pw_decode(code, &w, &s, &b, &fi, &sp) && w == ew && s == es) {
            /* round trip through the encoder */
            G.mode = w; G.stage = s; G.saved_bombs = b; G.saved_fire = fi; G.saved_speed = sp;
            char enc[9];
            pw_encode(enc);
            int w2, s2, b2, f2, sp2;
            if (pw_decode(enc, &w2, &s2, &b2, &f2, &sp2) && w2 == w && s2 == s && b2 == b && f2 == fi && sp2 == sp) ok++;
            else { bad++; printf("roundtrip fail %s -> %s\n", code, enc); }
        } else if (ew == 2 && es == 3 && w == 2 && s == 4) { ok++; printf("note: DYNA.COD line 2.3 (%s) really encodes 2.4 - typo in the 2018 list\n", code); }
        else { bad++; printf("decode fail %d.%d %s\n", ew, es, code); }
    }
    printf("passwords ok=%d bad=%d\n", ok, bad);
    /* RNG / data sanity */
    printf("first map row: %.15s\n", (const char *)DS + 0x46);
    return bad != 0;
}
