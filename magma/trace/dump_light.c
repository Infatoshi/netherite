/* dump_light.c - dump sky/block light from magma light.c for a block AABB.
 * Usage: dump_light --seed 0 --x0 -48 --y0 50 --z0 -32 --x1 63 --y1 120 --z1 79 --out f.csv
 *        dump_light --seed 0 --cx 0 --cz 2 --radius 3 --y0 50 --y1 120 --out f.csv
 */
#include "world/light.h"
#include "world/populate_mc.h"
#include "game/caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    long long seed = 0;
    int x0 = -32, y0 = 50, z0 = -16, x1 = 47, y1 = 120, z1 = 63;
    int have_box = 0;
    int cx = 0, cz = 2, rad = 3, use_chunk = 0;
    const char *out = "/tmp/c_light.csv";
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = atoll(argv[++i]);
        else if (!strcmp(argv[i], "--x0") && i + 1 < argc) { x0 = atoi(argv[++i]); have_box = 1; }
        else if (!strcmp(argv[i], "--y0") && i + 1 < argc) y0 = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--z0") && i + 1 < argc) z0 = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--x1") && i + 1 < argc) x1 = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--y1") && i + 1 < argc) y1 = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--z1") && i + 1 < argc) z1 = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cx") && i + 1 < argc) { cx = atoi(argv[++i]); use_chunk = 1; }
        else if (!strcmp(argv[i], "--cz") && i + 1 < argc) cz = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--radius") && i + 1 < argc) rad = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else { fprintf(stderr, "bad arg %s\n", argv[i]); return 2; }
    }
    if (use_chunk && !have_box) {
        x0 = (cx - rad) * 16; x1 = (cx + rad) * 16 + 15;
        z0 = (cz - rad) * 16; z1 = (cz + rad) * 16 + 15;
    }
    /* prep like pose_scene for seed 0 */
    int scx = 2, scz = 11;
    int bx0 = (x0 >> 4) - 1, bx1 = (x1 >> 4) + 1;
    int bz0 = (z0 >> 4) - 1, bz1 = (z1 >> 4) + 1;
    int nbases = 0, cap = 4096;
    int *bases = (int *)malloc((size_t)cap * 2 * sizeof(int));
    for (int bx = scx - 12; bx < scx + 12; ++bx)
        for (int bz = scz - 12; bz < scz + 12; ++bz) {
            if (nbases >= cap) { cap *= 2; bases = realloc(bases, (size_t)cap * 2 * sizeof(int)); }
            bases[nbases * 2] = bx; bases[nbases * 2 + 1] = bz; nbases++;
        }
    for (int bx = bx0; bx <= bx1; ++bx)
        for (int bz = bz0; bz <= bz1; ++bz) {
            int seen = 0;
            for (int i = 0; i < nbases; ++i)
                if (bases[i * 2] == bx && bases[i * 2 + 1] == bz) { seen = 1; break; }
            if (seen) continue;
            if (nbases >= cap) { cap *= 2; bases = realloc(bases, (size_t)cap * 2 * sizeof(int)); }
            bases[nbases * 2] = bx; bases[nbases * 2 + 1] = bz; nbases++;
        }
    int span = (bx1 - bx0 + 1); if (bz1 - bz0 + 1 > span) span = bz1 - bz0 + 1;
    int owr = span + 3; if (owr < 34) owr = 34;
    cr_caps_override("owr_d_min", owr);
    cr_caps_override("owr_cells_max", 49152);

    popmc_prepare(seed, bases, nbases);
    CrLight *L = light_create(seed);
    int ccx = (x0 + x1) / 2 / 16, ccz = (z0 + z1) / 2 / 16;
    int erad = rad + 2; if (!use_chunk) erad = ((x1 - x0) / 32) + 3;
    light_ensure(L, ccx, ccz, erad);

    FILE *f = fopen(out, "w");
    if (!f) { fprintf(stderr, "open %s\n", out); return 1; }
    fprintf(f, "wx wy wz sky blk\n");
    long n = 0;
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                int sky = light_sky(L, x, y, z);
                int blk = light_blk(L, x, y, z);
                fprintf(f, "%d %d %d %d %d\n", x, y, z, sky, blk);
                n++;
            }
    fclose(f);
    fprintf(stderr, "dump_light: %ld cells [%d,%d,%d]..[%d,%d,%d] -> %s\n",
            n, x0, y0, z0, x1, y1, z1, out);
    free(bases);
    return 0;
}
