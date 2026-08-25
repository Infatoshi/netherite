/* Torch place/break: blaze cu_world_set_state BLOCK nibble vs magma
 * light_set_state + light_ensure (magma/world/light.c:751 / :650
 * compute_blocklight :434). World.checkLightFor BLOCK (World.java:3025)
 * + getRawLight (World.java:2967). Packed sky nibble stays independent. */
#define _POSIX_C_SOURCE 200809L
#include "blaze_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (cond)
        fprintf(stderr, "OK: %s\n", msg);
    else {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails = 1;
    }
}

static int blaze_blk(const Blaze *e, int wx, int wy, int wz) {
    long i = cu_region_idx(e, wx, wy, wz);
    if (i < 0) return -1;
    return (int)(e->light[i] & 15);
}

static int blaze_sky(const Blaze *e, int wx, int wy, int wz) {
    long i = cu_region_idx(e, wx, wy, wz);
    if (i < 0) return -1;
    return (int)(e->light[i] >> 4);
}

int magma_torch_scene(int *at, int *d1, int *d2, int *d13, int *d14,
                      int *after_break, int *sky_at);

static int setup_env(Blaze *e, u16 **cells, u8 **light, Chunk **window,
                     int rnx, int rny, int rnz, int sky_nibble) {
    long rvol = (long)rnx * rny * rnz;
    long i;
    *cells = (u16 *)calloc((size_t)rvol, sizeof(u16));
    *light = (u8 *)malloc((size_t)rvol);
    *window = (Chunk *)calloc((size_t)PSV_NCHUNKS, sizeof(Chunk));
    if (!*cells || !*light || !*window) {
        fprintf(stderr, "FAIL: alloc\n");
        return 0;
    }
    for (i = 0; i < rvol; ++i)
        (*light)[i] = (u8)((sky_nibble & 15) << 4);
    memset(e, 0, sizeof *e);
    e->cells = *cells;
    e->light = *light;
    e->window = *window;
    e->rx0 = 0;
    e->ry0 = 0;
    e->rz0 = 0;
    e->rnx = rnx;
    e->rny = rny;
    e->rnz = rnz;
    e->rvol = rvol;
    e->light_valid = 1;
    return 1;
}

int main(void) {
    const int rnx = 48, rny = 128, rnz = 48;
    const int tx = 24, ty = 80, tz = 24;
    Blaze e;
    int light_q[CU_LIGHT_Q];
    u16 *cells;
    u8 *light;
    Chunk *window;
    int d;

    if (!setup_env(&e, &cells, &light, &window, rnx, rny, rnz, 10))
        return 1;
    e.light_q = light_q;

    /* Place torch in air. Opacity 0==0 so sky rebuild is skipped; blight
     * must still flood. Block.java:320 setLightLevel -> emit 14. Air
     * getRawLight opacity clamps to 1 (World.java:2985), so Manhattan d
     * has blight 14-d for d=0..13 and 0 at d>=14. */
    cu_world_set_state(&e, tx, ty, tz, BLK_TORCH, 0);
    expect(blaze_blk(&e, tx, ty, tz) == 14, "torch cell BLOCK light == 14");
    expect(blaze_blk(&e, tx + 1, ty, tz) == 13, "Manhattan 1 BLOCK light == 13");
    expect(blaze_blk(&e, tx + 2, ty, tz) == 12, "Manhattan 2 BLOCK light == 12");
    expect(blaze_blk(&e, tx + 13, ty, tz) == 1, "Manhattan 13 BLOCK light == 1");
    expect(blaze_blk(&e, tx + 14, ty, tz) == 0, "Manhattan 14 BLOCK light == 0");
    expect(blaze_sky(&e, tx, ty, tz) == 10, "torch place leaves sky nibble 10");
    expect(blaze_sky(&e, tx + 1, ty, tz) == 10, "neighbor sky nibble stays 10");
    {
        int md = 1;
        for (d = 0; d <= 13; ++d)
            if (blaze_blk(&e, tx + d, ty, tz) != 14 - d)
                md = 0;
        expect(md, "east axis 14-d for Manhattan 0..13");
    }

    /* Magma compute_blocklight (light.c:434) after light_set_state + ensure. */
    {
        int at, d1, d2, d13, d14, brk, sky;
        if (!magma_torch_scene(&at, &d1, &d2, &d13, &d14, &brk, &sky)) {
            expect(0, "magma light_set_state + light_ensure torch");
        } else {
            fprintf(stderr,
                    "magma torch at=%d d1=%d d2=%d d13=%d d14=%d break=%d sky=%d\n",
                    at, d1, d2, d13, d14, brk, sky);
            fprintf(stderr,
                    "blaze torch at=%d d1=%d d2=%d d13=%d d14=%d\n",
                    blaze_blk(&e, tx, ty, tz),
                    blaze_blk(&e, tx + 1, ty, tz),
                    blaze_blk(&e, tx + 2, ty, tz),
                    blaze_blk(&e, tx + 13, ty, tz),
                    blaze_blk(&e, tx + 14, ty, tz));
            expect(blaze_blk(&e, tx, ty, tz) == at,
                   "torch cell matches magma light_ensure");
            expect(blaze_blk(&e, tx + 1, ty, tz) == d1,
                   "Manhattan 1 matches magma");
            expect(blaze_blk(&e, tx + 2, ty, tz) == d2,
                   "Manhattan 2 matches magma");
            expect(blaze_blk(&e, tx + 13, ty, tz) == d13,
                   "Manhattan 13 matches magma");
            expect(blaze_blk(&e, tx + 14, ty, tz) == d14,
                   "Manhattan 14 matches magma");
            expect(sky == 15, "magma direct-sky air at torch stays 15");
        }
    }

    /* Second torch: overlapping fields. Break the first; the second must
     * keep its 14 and the overlap uses max (checkLightFor decrease then
     * spread, World.java:3046 then :3100). */
    cu_world_set_state(&e, tx + 2, ty, tz, BLK_TORCH, 0);
    expect(blaze_blk(&e, tx + 2, ty, tz) == 14, "second torch cell == 14");
    expect(blaze_blk(&e, tx + 1, ty, tz) == 13, "overlap stays 13");
    cu_world_set_state(&e, tx, ty, tz, 0, 0);
    expect(blaze_blk(&e, tx, ty, tz) == 12,
           "break first torch: cell now 12 from remaining torch at +2");
    expect(blaze_blk(&e, tx + 2, ty, tz) == 14, "remaining torch still 14");
    expect(blaze_sky(&e, tx, ty, tz) == 10, "torch break leaves sky nibble");

    cu_world_set_state(&e, tx + 2, ty, tz, 0, 0);
    expect(blaze_blk(&e, tx + 2, ty, tz) == 0, "break last torch: origin 0");
    expect(blaze_blk(&e, tx + 1, ty, tz) == 0, "break last torch: neighbor 0");
    expect(blaze_sky(&e, tx + 2, ty, tz) == 10, "last break leaves sky 10");

    {
        int at, d1, d2, d13, d14, brk, sky;
        if (magma_torch_scene(&at, &d1, &d2, &d13, &d14, &brk, &sky))
            expect(brk == 0, "magma torch break BLOCK light == 0");
    }

    /* Stone between torches would be a later edit; opaque cell getRawLight
     * returns 0 (World.java:2990) so blight does not cross it. */
    cu_world_set_state(&e, tx, ty, tz, BLK_TORCH, 0);
    cu_world_set_state(&e, tx + 1, ty, tz, BLK_STONE, 0);
    expect(blaze_blk(&e, tx, ty, tz) == 14, "torch still 14 next to stone");
    expect(blaze_blk(&e, tx + 1, ty, tz) == 0, "stone cell BLOCK light == 0");
    /* 6-connected flood goes around one cube: torch-up-east-east-down is
     * 4 steps, 14-4=10. Through the stone would be 12 (World.java:2990
     * opaque getRawLight 0, so that path is closed). */
    expect(blaze_blk(&e, tx + 2, ty, tz) == 10,
           "air behind one stone is 10 (around, not through)");

    free(cells);
    free(light);
    free(window);
    if (fails) {
        fprintf(stderr, "FAIL\n");
        return 1;
    }
    fprintf(stderr, "PASS\n");
    return 0;
}
