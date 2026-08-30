/* Worklist sky flood vs 15-pass box oracle (cu_light_after_opacity_ref).
 * Same unique raise-only max-flood. Magma water cube is test_skylight_water. */
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

static unsigned rng_state = 0xC0A1u;
static unsigned rng_next(void) {
    unsigned x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return rng_state = x;
}

static int setup_env(Blaze *e, u16 **cells, u8 **light, Chunk **window,
                     int **light_q, int **sky_q, int rnx, int rny, int rnz) {
    long rvol = (long)rnx * rny * rnz;
    long i;
    *cells = (u16 *)calloc((size_t)rvol, sizeof(u16));
    *light = (u8 *)malloc((size_t)rvol);
    *window = (Chunk *)calloc((size_t)PSV_NCHUNKS, sizeof(Chunk));
    *light_q = (int *)malloc((size_t)CU_LIGHT_Q * sizeof(int));
    *sky_q = (int *)malloc((size_t)CU_SKY_Q * sizeof(int));
    if (!*cells || !*light || !*window || !*light_q || !*sky_q) {
        fprintf(stderr, "FAIL: alloc\n");
        return 0;
    }
    for (i = 0; i < rvol; ++i)
        (*light)[i] = 0;
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
    e->light_q = *light_q;
    e->sky_q = *sky_q;
    return 1;
}

static void copy_world(Blaze *dst, const Blaze *src) {
    memcpy(dst->cells, src->cells, (size_t)src->rvol * sizeof(u16));
    memcpy(dst->light, src->light, (size_t)src->rvol);
}

static int light_eq(const Blaze *a, const Blaze *b, const char *msg) {
    if (memcmp(a->light, b->light, (size_t)a->rvol) == 0)
        return 1;
    {
        long i, n = 0, first = -1;
        for (i = 0; i < a->rvol; ++i) {
            if (a->light[i] == b->light[i]) continue;
            if (first < 0) first = i;
            n++;
        }
        fprintf(stderr, "FAIL: %s  ndiff=%ld first=%ld a=%02x b=%02x\n",
                msg, n, first,
                first >= 0 ? a->light[first] : 0,
                first >= 0 ? b->light[first] : 0);
        fails = 1;
    }
    return 0;
}

static void relight_all_ref(Blaze *e) {
    int cx, cz;
    for (cx = e->rx0 >> 4; cx <= (e->rx0 + e->rnx - 1) >> 4; ++cx)
        for (cz = e->rz0 >> 4; cz <= (e->rz0 + e->rnz - 1) >> 4; ++cz)
            cu_skylight_chunk(e, cx << 4, cz << 4);
    cu_skylight_spread_box_ref(e, e->rx0, e->ry0, e->rz0,
                               e->rx0 + e->rnx - 1,
                               e->ry0 + e->rny - 1,
                               e->rz0 + e->rnz - 1);
}

static void apply_opacity(Blaze *live, Blaze *ref, int x, int y, int z,
                          int id, int meta) {
    long i = cu_region_idx(live, x, y, z);
    u16 old, neu;
    int old_op, new_op;
    if (i < 0) return;
    old = live->cells[i];
    neu = mc_state(id, meta);
    live->cells[i] = neu;
    ref->cells[i] = neu;
    old_op = cu_sky_opacity(mc_state_id(old));
    new_op = cu_sky_opacity(id);
    if (old_op != new_op) {
        cu_light_after_opacity(live, x, y, z);
        cu_light_after_opacity_ref(ref, x, y, z);
    }
    if (old != neu) {
        cu_check_light_for_block(live, x, y, z);
        cu_check_light_for_block(ref, x, y, z);
    }
}

static void fill_terrain(Blaze *e) {
    int x, y, z, h;
    for (x = 0; x < e->rnx; ++x)
        for (z = 0; z < e->rnz; ++z) {
            h = 40 + (int)((rng_next() >> 8) % 25);
            if (h >= e->rny - 2) h = e->rny - 3;
            for (y = 0; y <= h; ++y) {
                long i = cu_region_idx(e, x, y, z);
                int id = (y == h) ? BLK_GRASS : (y > h - 3 ? BLK_DIRT : BLK_STONE);
                e->cells[i] = mc_state(id, 0);
            }
        }
    /* overhangs / leaf blobs */
    for (h = 0; h < 40; ++h) {
        int ox = (int)(rng_next() % (unsigned)e->rnx);
        int oz = (int)(rng_next() % (unsigned)e->rnz);
        int oy = 50 + (int)(rng_next() % 40);
        int dx, dy, dz;
        for (dx = -2; dx <= 2; ++dx)
            for (dz = -2; dz <= 2; ++dz)
                for (dy = 0; dy <= 2; ++dy) {
                    long i = cu_region_idx(e, ox + dx, oy + dy, oz + dz);
                    if (i < 0) continue;
                    e->cells[i] = mc_state(BLK_LEAVES, 0);
                }
    }
    /* caves: punch air through stone */
    for (h = 0; h < 30; ++h) {
        int cx = (int)(rng_next() % (unsigned)e->rnx);
        int cz = (int)(rng_next() % (unsigned)e->rnz);
        int cy = 10 + (int)(rng_next() % 30);
        int k;
        for (k = 0; k < 18; ++k) {
            long i = cu_region_idx(e, cx, cy, cz);
            if (i >= 0) e->cells[i] = mc_state(BLK_AIR, 0);
            cx += (int)(rng_next() % 3) - 1;
            cy += (int)(rng_next() % 3) - 1;
            cz += (int)(rng_next() % 3) - 1;
        }
    }
    /* rails sit on the surface (opacity 0, cu_sky_opacity special-cases 66) */
    for (h = 0; h < 50; ++h) {
        int rx = (int)(rng_next() % (unsigned)e->rnx);
        int rz = (int)(rng_next() % (unsigned)e->rnz);
        int ry;
        for (ry = e->rny - 1; ry >= 0; --ry) {
            long i = cu_region_idx(e, rx, ry, rz);
            if (i < 0) continue;
            if (mc_state_id(e->cells[i]) != BLK_AIR) {
                long j = cu_region_idx(e, rx, ry + 1, rz);
                if (j >= 0) e->cells[j] = mc_state(66, 0);
                break;
            }
        }
    }
}

int main(void) {
    const int rnx = 48, rny = 128, rnz = 48;
    Blaze live, ref;
    u16 *cells_l, *cells_r;
    u8 *light_l, *light_r;
    Chunk *win_l, *win_r;
    int *lq_l, *lq_r, *sq_l, *sq_r;
    int n, k;
    static const int k_ids[] = {BLK_AIR, BLK_STONE, BLK_DIRT, BLK_LEAVES,
                                BLK_WATER, BLK_GLASS, 66};

    if (!setup_env(&live, &cells_l, &light_l, &win_l, &lq_l, &sq_l,
                   rnx, rny, rnz) ||
        !setup_env(&ref, &cells_r, &light_r, &win_r, &lq_r, &sq_r,
                   rnx, rny, rnz))
        return 1;

    /* directed: 3x3x3 water in open sky, both paths from sky=15 air. */
    {
        int x, y, z;
        for (n = 0; n < live.rvol; ++n)
            live.light[n] = (u8)(15 << 4);
        copy_world(&ref, &live);
        for (y = 99; y <= 101; ++y)
            for (z = 7; z <= 9; ++z)
                for (x = 7; x <= 9; ++x)
                    apply_opacity(&live, &ref, x, y, z, BLK_WATER, 0);
        expect(light_eq(&live, &ref, "water cube worklist vs 15-pass"),
               "water cube worklist vs 15-pass");
        for (y = 99; y <= 101; ++y)
            for (z = 7; z <= 9; ++z)
                for (x = 7; x <= 9; ++x)
                    apply_opacity(&live, &ref, x, y, z, BLK_AIR, 0);
        expect(light_eq(&live, &ref, "water cube air restore"),
               "water cube air restore");
    }

    /* random heightmap / overhang / cave / rails, then 500 edits. */
    rng_state = 0xC0A1u;
    memset(live.cells, 0, (size_t)live.rvol * sizeof(u16));
    for (n = 0; n < live.rvol; ++n)
        live.light[n] = 0;
    fill_terrain(&live);
    relight_all_ref(&live);
    copy_world(&ref, &live);
    expect(light_eq(&live, &ref, "terrain copy"), "terrain copy");

    for (k = 0; k < 500; ++k) {
        int x = (int)(rng_next() % (unsigned)rnx);
        int z = (int)(rng_next() % (unsigned)rnz);
        int y = 8 + (int)(rng_next() % (unsigned)(rny - 16));
        int id = k_ids[rng_next() % (unsigned)(sizeof k_ids / sizeof k_ids[0])];
        char buf[64];
        apply_opacity(&live, &ref, x, y, z, id, 0);
        snprintf(buf, sizeof buf, "edit %d (%d,%d,%d id=%d)", k, x, y, z, id);
        if (!light_eq(&live, &ref, buf))
            break;
    }
    if (k == 500)
        expect(1, "500 random break/place worklist vs 15-pass");

    /* NULL sky_q uses z-inner 15-pass fallback; still the same bits. */
    live.sky_q = NULL;
    copy_world(&live, &ref);
    apply_opacity(&live, &ref, 20, 60, 20, BLK_STONE, 0);
    expect(light_eq(&live, &ref, "fallback vs 15-pass"),
           "NULL sky_q fallback vs 15-pass");
    live.sky_q = sq_l;

    free(cells_l); free(cells_r);
    free(light_l); free(light_r);
    free(win_l); free(win_r);
    free(lq_l); free(lq_r);
    free(sq_l); free(sq_r);
    if (fails) {
        fprintf(stderr, "FAIL\n");
        return 1;
    }
    fprintf(stderr, "PASS\n");
    return 0;
}
