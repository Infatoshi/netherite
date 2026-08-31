/* 3x3x3 still-water cube: blaze cu_world_set_state sky nibbles vs magma
 * light_set_state + light_ensure (magma/tests/test_water_skylight.c,
 * magma/world/light.c:751 / :650). Block.java:2412-2413 opacity 3.
 * Magma rebuilds Chunk.generateSkylightMap (Chunk.java:238) then raise-only
 * spread; lockstep fixture is unchanged. */
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

static int blaze_sky(const Blaze *e, int wx, int wy, int wz) {
    long i = cu_region_idx(e, wx, wy, wz);
    if (i < 0) return -1;
    return (int)(e->light[i] >> 4);
}

int magma_water_cube_skies(int *top, int *mid, int *bot, int *edge);

int main(void) {
    const int rnx = 48, rny = 128, rnz = 48;
    const int y0 = 99;
    long rvol;
    Blaze e;
    int light_q[CU_LIGHT_Q];
    int *sky_q;
    u16 *cells;
    u8 *light;
    Chunk *window;
    int x, y, z;

    rvol = (long)rnx * rny * rnz;
    cells = (u16 *)calloc((size_t)rvol, sizeof(u16));
    light = (u8 *)malloc((size_t)rvol);
    window = (Chunk *)calloc((size_t)PSV_NCHUNKS, sizeof(Chunk));
    if (!cells || !light || !window) {
        fprintf(stderr, "FAIL: alloc\n");
        return 1;
    }
    for (x = 0; x < rvol; ++x)
        light[x] = (u8)(15 << 4);

    memset(&e, 0, sizeof e);
    e.cells = cells;
    e.light = light;
    e.window = window;
    e.rx0 = 0;
    e.ry0 = 0;
    e.rz0 = 0;
    e.rnx = rnx;
    e.rny = rny;
    e.rnz = rnz;
    e.rvol = rvol;
    e.light_valid = 1;
    e.light_q = light_q;
    sky_q = (int *)malloc((size_t)CU_SKY_Q * sizeof(int));
    if (!sky_q) {
        fprintf(stderr, "FAIL: alloc sky_q\n");
        return 1;
    }
    e.sky_q = sky_q;
    {
        long nch = (long)CU_SEC_SPAN(rnx) * CU_SEC_SPAN(rnz);
        e.sky_clean = (u8 *)malloc((size_t)nch);
        if (!e.sky_clean) {
            fprintf(stderr, "FAIL: alloc sky_clean\n");
            return 1;
        }
        memset(e.sky_clean, 1, (size_t)nch);
        e.sky_cx0 = 0;
        e.sky_cz0 = 0;
        e.sky_cnx = CU_SEC_SPAN(rnx);
        e.sky_cnz = CU_SEC_SPAN(rnz);
    }

    for (y = y0; y <= y0 + 2; ++y)
        for (z = 7; z <= 9; ++z)
            for (x = 7; x <= 9; ++x)
                cu_world_set_state(&e, x, y, z, BLK_WATER, 0);

    {
        int mt, mm, mb, me, bt, bm, bb, be;
        bt = blaze_sky(&e, 8, y0 + 2, 8);
        bm = blaze_sky(&e, 8, y0 + 1, 8);
        bb = blaze_sky(&e, 8, y0, 8);
        be = blaze_sky(&e, 7, y0 + 1, 7);
        if (!magma_water_cube_skies(&mt, &mm, &mb, &me)) {
            expect(0, "magma light_set_state + light_ensure");
        } else {
            fprintf(stderr, "magma skies top=%d mid=%d bot=%d edge=%d\n",
                    mt, mm, mb, me);
            fprintf(stderr, "blaze skies top=%d mid=%d bot=%d edge=%d\n",
                    bt, bm, bb, be);
            expect(bt == mt, "top centre matches magma light_ensure");
            expect(bm == mm, "mid centre matches magma light_ensure");
            expect(bb == mb, "bot centre matches magma light_ensure");
            expect(be == me, "edge matches magma light_ensure");
        }
    }

    for (y = y0; y <= y0 + 2; ++y)
        for (z = 7; z <= 9; ++z)
            for (x = 7; x <= 9; ++x)
                cu_world_set_state(&e, x, y, z, 0, 0);
    expect(blaze_sky(&e, 8, y0 + 1, 8) == 15, "air restored to 15");

    free(cells);
    free(light);
    free(window);
    free(sky_q);
    free(e.sky_clean);
    if (fails) {
        fprintf(stderr, "FAIL\n");
        return 1;
    }
    fprintf(stderr, "PASS\n");
    return 0;
}
