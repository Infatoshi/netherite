/* Oracle for the incremental per-edit skylight update.
 *
 * The parity target is the pre-optimisation cu_light_after_opacity: rebuild
 * every column of the edited chunk (Chunk.generateSkylightMap, Chunk.java:238,
 * via magma cr_k17_skylight_column, light.c:69), then raise-only relax the
 * chunk +-15 box over the full region height (magma compute_skylight_spread,
 * light.c:541). That reference is copied into this file with a ref_ prefix so
 * blaze_core.h can change freely.
 *
 * Each edit runs on TWO independent light planes over ONE shared cells plane:
 * A steps with the reference, B with the live cu_light_after_opacity. The
 * planes must stay bit-identical after every edit, so any drift is caught on
 * the edit that creates it, not many edits later.
 *
 * Coverage: scripted scenarios first (chunk hops, chunk-border columns, a
 * stacked column dug down and filled back, a column opened to the sky then
 * closed, and an edit right after a snapshot restore), then a long
 * randomized stream, then a walking-player stream shaped like the trainer's
 * own edits.
 *
 * Usage: test_skylight_incr [snapshot.bsnp ...]
 * With no argument it loads the fixtures under verify/fixtures/port.
 * Env: SKY_EDITS=n       random edits per stream (default 400)
 *      SKY_ONLY=ref|new  run one path alone, to time it
 *      SKY_DBG=1         print region dims and the first spills
 *      SKY_AUDIT=1       prove every sky_dirty claim after every edit
 *                        (one full rebuild per chunk per edit; slow) */
#define _POSIX_C_SOURCE 200809L
#include "blaze_core.h"
#include "blaze_snapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
static long long total_edits;
static long long total_skipped;
static long long total_incr;
static long long walk_edits, walk_incr;
static long long spills, spills_full, firsts;
static int mode_ref, mode_new;   /* SKY_ONLY=ref|new: time one path alone */
static int dbg;
static int audit;                /* SKY_AUDIT=1: prove every sky_dirty claim */
static long long audit_clean, audit_box;

/* ------------------------------------------------------------------ */
/* Reference (pre-optimisation) implementation, verbatim copy.         */
/* ------------------------------------------------------------------ */

static void ref_relax_cell(Blaze *e, int x, int y, int z) {
    static const int dx[6] = {1, -1, 0, 0, 0, 0};
    static const int dy[6] = {0, 0, 1, -1, 0, 0};
    static const int dz[6] = {0, 0, 0, 0, 1, -1};
    long i = cu_region_idx(e, x, y, z);
    int opacity, sky, best, q;
    if (i < 0) return;
    opacity = cu_sky_opacity(mc_state_id(e->cells[i]));
    if (opacity > 15) opacity = 15;
    if (opacity < 1) opacity = 1;
    sky = e->light[i] >> 4;
    best = sky;
    for (q = 0; q < 6; ++q) {
        long ni = cu_region_idx(e, x + dx[q], y + dy[q], z + dz[q]);
        int candidate;
        if (ni < 0) continue;
        candidate = (e->light[ni] >> 4) - opacity;
        if (candidate > best) best = candidate;
    }
    if (best > sky)
        e->light[i] = (u8)((best << 4) | (e->light[i] & 15));
}

static void ref_column(Blaze *e, int wx, int wz) {
    int y, k1, j1, wy0, wy1;
    long i;
    wy0 = e->ry0;
    wy1 = e->ry0 + e->rny - 1;
    for (y = wy0; y <= wy1; ++y) {
        i = cu_region_idx(e, wx, y, wz);
        if (i < 0) continue;
        e->light[i] = (u8)(e->light[i] & 15);
    }
    k1 = 15;
    y = wy1;
    if (y < 0) return;
    while (1) {
        i = cu_region_idx(e, wx, y, wz);
        j1 = 0;
        if (i >= 0)
            j1 = cu_sky_opacity(mc_state_id(e->cells[i]));
        if (j1 == 0 && k1 != 15) j1 = 1;
        k1 -= j1;
        if (k1 > 0 && i >= 0)
            e->light[i] = (u8)((k1 << 4) | (e->light[i] & 15));
        --y;
        if (y <= 0 || k1 <= 0) break;
    }
}

static void ref_chunk(Blaze *e, int wx, int wz) {
    int bx = (wx >> 4) << 4, bz = (wz >> 4) << 4;
    int lx, lz;
    for (lx = 0; lx < 16; ++lx)
        for (lz = 0; lz < 16; ++lz) {
            int cx = bx + lx, cz = bz + lz;
            if (cu_region_idx(e, cx, e->ry0, cz) < 0) continue;
            ref_column(e, cx, cz);
        }
}

/* Returns the number of passes that ran. 15 means the cap may have cut the
 * relaxation short of its fixed point. */
static int ref_spread_box(Blaze *e, int x0, int y0, int z0, int x1, int y1,
                          int z1) {
    int pass, x, y, z, changed;
    int rx1 = e->rx0 + e->rnx - 1, ry1 = e->ry0 + e->rny - 1,
        rz1 = e->rz0 + e->rnz - 1;
    if (x0 < e->rx0) x0 = e->rx0;
    if (y0 < e->ry0) y0 = e->ry0;
    if (z0 < e->rz0) z0 = e->rz0;
    if (x1 > rx1) x1 = rx1;
    if (y1 > ry1) y1 = ry1;
    if (z1 > rz1) z1 = rz1;
    if (x0 > x1 || y0 > y1 || z0 > z1) return 0;
    for (pass = 0; pass < 15; ++pass) {
        changed = 0;
        for (x = x0; x <= x1; ++x)
            for (z = z0; z <= z1; ++z)
                for (y = y0; y <= y1; ++y) {
                    long i = cu_region_idx(e, x, y, z);
                    int before;
                    if (i < 0) continue;
                    before = e->light[i] >> 4;
                    ref_relax_cell(e, x, y, z);
                    if ((e->light[i] >> 4) > before) changed = 1;
                }
        if (!changed) return pass + 1;
    }
    return 15;
}

static int ref_after_opacity(Blaze *e, int wx, int wy, int wz) {
    int cx, cz, x0, x1, z0, z1;
    (void)wy;
    if (!e->light_valid) return 0;
    ref_chunk(e, wx, wz);
    cx = wx >> 4;
    cz = wz >> 4;
    x0 = (cx << 4) - 15;
    x1 = (cx << 4) + 30;
    z0 = (cz << 4) - 15;
    z1 = (cz << 4) + 30;
    return ref_spread_box(e, x0, e->ry0, z0, x1, e->ry0 + e->rny - 1, z1);
}

/* ------------------------------------------------------------------ */

/* xorshift64*, fixed seed: the edit stream is reproducible. */
static unsigned long long rng_s = 0x9E3779B97F4A7C15ull;
static unsigned rnd(unsigned n) {
    rng_s ^= rng_s >> 12;
    rng_s ^= rng_s << 25;
    rng_s ^= rng_s >> 27;
    return (unsigned)((rng_s * 0x2545F4914F6CDD1Dull) >> 33) % (n ? n : 1);
}

static const int EDIT_IDS[12] = {
    0,    /* air, opacity 0 */
    1,    /* stone, 255 */
    9,    /* still water, 3 */
    18,   /* leaves, 1 */
    20,   /* glass, 0 */
    8,    /* flowing water, 3 */
    79,   /* ice, 3 */
    89,   /* glowstone, 255 + emit */
    66,   /* rail: state_opacity override 0 */
    175,  /* double plant: override 0 */
    5,    /* planks, 255 */
    44    /* slab */
};

/* One fixture under test: two light planes over one shared cells plane. */
typedef struct {
    CuSnapshot s;
    Blaze a, b;                  /* a = reference plane, b = live plane */
    u8 *light_a, *light_b;
    u16 *cells0;                 /* pristine cells, for the restore case */
    Chunk *win_a, *win_b;
    int *q_a, *q_b;
    long rvol;
    const char *path;
    int cap15;                   /* the reference hit its 15-pass cap */
    int edits;
    int walk;                    /* count this edit in the walking stream */
} Pair;

/* top non-air y of a region column, or ry0 - 1 when the column is empty */
static int col_top(const Blaze *e, int wx, int wz) {
    int y;
    for (y = e->ry0 + e->rny - 1; y >= e->ry0; --y) {
        long i = cu_region_idx(e, wx, y, wz);
        if (i >= 0 && mc_state_id(e->cells[i]) != 0) return y;
    }
    return e->ry0 - 1;
}

/* Is the whole light plane a fixed point of the raise-only relax? */
static int is_relax_fixed_point(Blaze *e, long *bad_idx) {
    static const int dx[6] = {1, -1, 0, 0, 0, 0};
    static const int dy[6] = {0, 0, 1, -1, 0, 0};
    static const int dz[6] = {0, 0, 0, 0, 1, -1};
    int x, y, z, q;
    for (x = e->rx0; x < e->rx0 + e->rnx; ++x)
        for (z = e->rz0; z < e->rz0 + e->rnz; ++z)
            for (y = e->ry0; y < e->ry0 + e->rny; ++y) {
                long i = cu_region_idx(e, x, y, z);
                int op, sky;
                if (i < 0) continue;
                op = cu_sky_opacity(mc_state_id(e->cells[i]));
                if (op > 15) op = 15;
                if (op < 1) op = 1;
                sky = e->light[i] >> 4;
                for (q = 0; q < 6; ++q) {
                    long ni = cu_region_idx(e, x + dx[q], y + dy[q],
                                            z + dz[q]);
                    if (ni < 0) continue;
                    if ((e->light[ni] >> 4) - op > sky) {
                        if (bad_idx) *bad_idx = i;
                        return 0;
                    }
                }
            }
    return 1;
}

/* SKY_AUDIT=1: prove the sky_dirty claim on plane B directly instead of
 * waiting for a later edit to expose it. For every chunk column of the
 * region, run the reference full rebuild on a COPY of plane B and require
 *   CU_SKY_CLEAN -> not one cell moves;
 *   CU_SKY_BOX   -> every cell that moves is inside the recorded box.
 * CU_SKY_ALL claims nothing, so it is never checked. The cost is one full
 * rebuild per chunk per edit, so run it with a small SKY_EDITS. */
static u8 *audit_buf;
static int audit_pair(Pair *p, const char *tag) {
    Blaze c = p->b;
    int ix, iz, ncx = cu_sky_nc(p->b.rx0, p->b.rnx),
        ncz = cu_sky_nc(p->b.rz0, p->b.rnz);
    int cx00 = p->b.rx0 >> 4, cz00 = p->b.rz0 >> 4;
    if (!audit_buf) audit_buf = (u8 *)malloc((size_t)p->rvol);
    if (!audit_buf) return 1;
    c.light = audit_buf;
    for (ix = 0; ix < ncx; ++ix)
        for (iz = 0; iz < ncz; ++iz) {
            int ci = ix * CU_SKY_NC + iz, st = p->b.sky_dirty[ci].state;
            const CuSkyDirty *d = &p->b.sky_dirty[ci];
            int wx = (cx00 + ix) << 4, wz = (cz00 + iz) << 4;
            long j;
            if (st == CU_SKY_ALL) continue;
            if (cu_region_idx(&p->b, wx, p->b.ry0, wz) < 0 &&
                cu_region_idx(&p->b, wx + 15, p->b.ry0, wz + 15) < 0)
                continue;
            memcpy(audit_buf, p->light_b, (size_t)p->rvol);
            ref_after_opacity(&c, wx, p->b.ry0, wz);
            if (st == CU_SKY_CLEAN) ++audit_clean; else ++audit_box;
            for (j = 0; j < p->rvol; ++j) {
                int lx, ly, lz;
                if (audit_buf[j] == p->light_b[j]) continue;
                lz = (int)(j % p->b.rnz);
                ly = (int)((j / p->b.rnz) % p->b.rny);
                lx = (int)(j / ((long)p->b.rnz * p->b.rny));
                if (st == CU_SKY_BOX && lx >= d->x0 && lx <= d->x1 &&
                    ly >= d->y0 && ly <= d->y1 && lz >= d->z0 && lz <= d->z1)
                    continue;
                fprintf(stderr,
                        "AUDIT: %s [%s] chunk (%d,%d) state %d: cell "
                        "(%d,%d,%d) moved %d -> %d, box x %d..%d y %d..%d "
                        "z %d..%d\n",
                        p->path, tag, cx00 + ix, cz00 + iz, st, lx, ly, lz,
                        p->light_b[j] >> 4, audit_buf[j] >> 4,
                        d->x0, d->x1, d->y0, d->y1, d->z0, d->z1);
                fails = 1;
                return 0;
            }
        }
    return 1;
}

/* One edit on both planes, then a bit-for-bit compare of the two light
 * planes. Returns 0 on the first divergence. tag names the scenario. */
static int p_edit(Pair *p, int wx, int wy, int wz, int id, int meta,
                  const char *tag) {
    long i = cu_region_idx(&p->a, wx, wy, wz);
    int old_op, new_op;
    u16 old_state;

    if (i < 0) { ++total_skipped; return 1; }
    old_state = p->s.cells[i];
    old_op = cu_sky_opacity(mc_state_id(old_state));
    new_op = cu_sky_opacity(id);
    p->s.cells[i] = mc_state(id, meta);
    /* cu_world_set_state only calls the sky path when opacity moved. */
    if (old_op == new_op) { ++total_skipped; return 1; }

    if (!mode_new) {
        int nbit = cu_sky_ci(&p->b, wx >> 4, wz >> 4);
        int passes = ref_after_opacity(&p->a, wx, wy, wz);
        if (passes >= 15) p->cap15 = 1;
        if (nbit >= 0 && p->b.sky_dirty[nbit].state == CU_SKY_ALL) ++firsts;
        else {
            ++total_incr;
            if (p->walk) ++walk_incr;
        }
        if (p->walk) ++walk_edits;
    }
    if (!mode_ref) {
        int u0 = 0, u1 = 0, ui, was, nc = CU_SKY_NC * CU_SKY_NC;
        for (ui = 0; ui < nc; ++ui)
            u0 += p->b.sky_dirty[ui].state == CU_SKY_ALL;
        was = cu_sky_ci(&p->b, wx >> 4, wz >> 4);
        was = was >= 0 ? p->b.sky_dirty[was].state : -1;
        cu_light_after_opacity(&p->b, wx, wy, wz, old_op);
        for (ui = 0; ui < nc; ++ui)
            u1 += p->b.sky_dirty[ui].state == CU_SKY_ALL;
        if (u1 > u0) {
            ++spills;
            if (was == CU_SKY_ALL) ++spills_full;
            if (dbg && spills <= 12)
                fprintf(stderr, "  spill [%s] (%d,%d,%d) op %d->%d state %d\n",
                        tag, wx, wy, wz, old_op, new_op, was);
        }
    }
    ++p->edits;
    ++total_edits;

    if (mode_ref || mode_new) return 1;
    if (memcmp(p->light_a, p->light_b, (size_t)p->rvol) == 0)
        return audit ? audit_pair(p, tag) : 1;
    {
        long j, first = -1, ndiff = 0;
        for (j = 0; j < p->rvol; ++j)
            if (p->light_a[j] != p->light_b[j]) {
                if (first < 0) first = j;
                ++ndiff;
            }
        fprintf(stderr,
                "FAIL: %s [%s] edit %d at (%d,%d,%d) id %d (op %d->%d): "
                "%ld cells differ, first idx %ld ref %d new %d\n",
                p->path, tag, p->edits, wx, wy, wz, id, old_op, new_op,
                ndiff, first, p->light_a[first] >> 4,
                p->light_b[first] >> 4);
        fails = 1;
    }
    return 0;
}

/* Snapshot restore: cells and both light planes go back to the loaded
 * state and the live env drops its sky_dirty tracking, exactly as
 * blaze_reset_scalar does. */
static void p_restore(Pair *p) {
    memcpy(p->s.cells, p->cells0, (size_t)p->rvol * sizeof(u16));
    memcpy(p->light_a, p->s.light, (size_t)p->rvol);
    memcpy(p->light_b, p->s.light, (size_t)p->rvol);
    cu_sky_all_unknown(&p->b);
}

/* ---------------- scripted pathological scenarios ------------------- */

static int scenarios(Pair *p) {
    Blaze *e = &p->a;
    int cx0 = ((e->rx0 + 24) >> 4) << 4;     /* an interior chunk origin */
    int cz0 = ((e->rz0 + 24) >> 4) << 4;
    int k, y;

    /* 1. one edit per chunk, marching across four chunks in x then z: the
     *    debt one chunk hands its neighbours has to survive the hop. */
    for (k = 0; k < 4; ++k) {
        int wx = cx0 + (k << 4) + 8, wz = cz0 + 8;
        if (!p_edit(p, wx, col_top(e, wx, wz), wz, 0, 0, "hop-x")) return 0;
    }
    for (k = 0; k < 4; ++k) {
        int wx = cx0 + 8, wz = cz0 + (k << 4) + 8;
        if (!p_edit(p, wx, col_top(e, wx, wz), wz, 0, 0, "hop-z")) return 0;
    }
    /* back over the same chunks, now placing: the second visit runs on the
     * incremental path with a stale box already recorded. */
    for (k = 3; k >= 0; --k) {
        int wx = cx0 + (k << 4) + 8, wz = cz0 + 8;
        if (!p_edit(p, wx, col_top(e, wx, wz) + 1, wz, 1, 0, "hop-back"))
            return 0;
    }

    /* 2. a line of columns crossing a chunk border, so the spread crosses
     *    the boundary in both directions. */
    for (k = -3; k <= 3; ++k) {
        int wx = cx0 + 16 + k, wz = cz0 + 7;
        if (!p_edit(p, wx, col_top(e, wx, wz), wz, 0, 0, "border-x"))
            return 0;
    }
    for (k = -3; k <= 3; ++k) {
        int wx = cx0 + 7, wz = cz0 + 16 + k;
        if (!p_edit(p, wx, col_top(e, wx, wz), wz, 0, 0, "border-z"))
            return 0;
    }
    /* the four columns that meet at a chunk corner */
    for (k = 0; k < 4; ++k) {
        int wx = cx0 + 16 + (k & 1) - 1, wz = cz0 + 16 + (k >> 1) - 1;
        if (!p_edit(p, wx, col_top(e, wx, wz), wz, 1, 0, "border-corner"))
            return 0;
    }

    /* 3. stacked edits in one column: dig 24 blocks straight down, then
     *    fill the shaft back up. Every step moves the column baseline. */
    {
        int wx = cx0 + 5, wz = cz0 + 5, top = col_top(e, wx, wz);
        for (y = top; y > top - 24 && y > e->ry0; --y)
            if (!p_edit(p, wx, y, wz, 0, 0, "dig-down")) return 0;
        for (y = top - 23; y <= top; ++y)
            if (!p_edit(p, wx, y, wz, 1, 0, "fill-up")) return 0;
    }

    /* 4. a column opened to the sky and then closed again: light 15 floods
     *    down the shaft and the lid takes it all back out. */
    {
        int wx = cx0 + 11, wz = cz0 + 11, top = col_top(e, wx, wz);
        for (y = top; y > top - 8 && y > e->ry0; --y)
            if (!p_edit(p, wx, y, wz, 0, 0, "open-sky")) return 0;
        if (!p_edit(p, wx, top, wz, 1, 0, "close-lid")) return 0;
        if (!p_edit(p, wx, top, wz, 0, 0, "reopen")) return 0;
        if (!p_edit(p, wx, top, wz, 20, 0, "glass-lid")) return 0;
        if (!p_edit(p, wx, top, wz, 1, 0, "stone-lid")) return 0;
    }

    /* 5. an edit right after a restore: the live env must rebuild from
     *    scratch, because the stored light is not a relaxed fixed point
     *    and the recorded boxes describe a world that is gone. */
    for (k = 0; k < 3; ++k) {
        int wx = cx0 + 3 + k, wz = cz0 + 13;
        p_restore(p);
        if (!p_edit(p, wx, col_top(e, wx, wz), wz, 0, 0, "post-restore"))
            return 0;
        if (!p_edit(p, wx, col_top(e, wx, wz), wz, 1, 0, "post-restore-2"))
            return 0;
    }
    p_restore(p);
    return 1;
}

/* ------------------------- random streams --------------------------- */

static int random_stream(Pair *p, int n_edits) {
    Blaze *e = &p->a;
    int hot_x = e->rx0 + e->rnx / 2, hot_z = e->rz0 + e->rnz / 2;
    int k;

    for (k = 0; k < n_edits * 2; ++k) {
        int wx, wy, wz, id, meta, mode;
        p->walk = k >= n_edits;
        if (p->walk) {
            /* a slowly wandering player editing within reach: the shape of
             * the trainer's own edit stream. */
            if ((k & 7) == 0) {
                hot_x += (int)rnd(5) - 2;
                hot_z += (int)rnd(5) - 2;
                if (hot_x < e->rx0 + 20) hot_x = e->rx0 + 20;
                if (hot_x > e->rx0 + e->rnx - 21) hot_x = e->rx0 + e->rnx - 21;
                if (hot_z < e->rz0 + 20) hot_z = e->rz0 + 20;
                if (hot_z > e->rz0 + e->rnz - 21) hot_z = e->rz0 + e->rnz - 21;
            }
            mode = 3;
        } else {
            mode = (int)rnd(6);
        }
        if (mode == 0) {                    /* anywhere */
            wx = e->rx0 + (int)rnd((unsigned)e->rnx);
            wz = e->rz0 + (int)rnd((unsigned)e->rnz);
            wy = e->ry0 + 1 + (int)rnd((unsigned)e->rny - 1);
        } else if (mode == 1) {             /* around the surface */
            wx = e->rx0 + (int)rnd((unsigned)e->rnx);
            wz = e->rz0 + (int)rnd((unsigned)e->rnz);
            wy = col_top(e, wx, wz) + (int)rnd(9) - 4;
        } else if (mode == 2) {             /* chunk border columns */
            int bx = (e->rx0 + (int)rnd((unsigned)e->rnx)) & ~15;
            int bz = (e->rz0 + (int)rnd((unsigned)e->rnz)) & ~15;
            wx = bx + (rnd(2) ? 15 : 0) + (int)rnd(2) - 1;
            wz = bz + (rnd(2) ? 15 : 0) + (int)rnd(2) - 1;
            wy = col_top(e, wx, wz) + (int)rnd(7) - 3;
        } else if (mode == 3) {             /* within reach of the player */
            wx = hot_x + (int)rnd(11) - 5;
            wz = hot_z + (int)rnd(11) - 5;
            wy = col_top(e, wx, wz) + (int)rnd(7) - 3;
        } else if (mode == 4) {             /* dig/build one hot column */
            wx = hot_x + (int)rnd(3) - 1;
            wz = hot_z + (int)rnd(3) - 1;
            wy = e->ry0 + 1 + (int)rnd((unsigned)e->rny - 1);
        } else {                            /* deep, under the surface */
            wx = e->rx0 + (int)rnd((unsigned)e->rnx);
            wz = e->rz0 + (int)rnd((unsigned)e->rnz);
            wy = e->ry0 + 1 + (int)rnd(40);
        }
        if (wy < e->ry0) wy = e->ry0;
        if (wy > e->ry0 + e->rny - 1) wy = e->ry0 + e->rny - 1;

        id = EDIT_IDS[rnd(12)];
        if (rnd(3) == 0) id = 0;            /* bias toward breaking */
        meta = (int)rnd(4);
        if (!p_edit(p, wx, wy, wz, id, meta, p->walk ? "walk" : "rand"))
            return 0;
    }
    p->walk = 0;
    return 1;
}

static int run_one(const char *path, int n_edits) {
    Pair p;
    char err[256];
    int ok;

    memset(&p, 0, sizeof p);
    p.path = path;
    if (!blaze_snapshot_load(path, &p.s, err, sizeof err, 1)) {
        fprintf(stderr, "FAIL: load %s: %s\n", path, err);
        return 0;
    }
    if (!p.s.light) {
        fprintf(stderr, "SKIP: %s has no light plane\n", path);
        blaze_snapshot_free(&p.s);
        return 1;
    }
    p.rvol = (long)p.s.head.rnx * p.s.head.rny * p.s.head.rnz;
    p.light_a = (u8 *)malloc((size_t)p.rvol);
    p.light_b = (u8 *)malloc((size_t)p.rvol);
    p.cells0 = (u16 *)malloc((size_t)p.rvol * sizeof(u16));
    p.win_a = (Chunk *)calloc((size_t)PSV_NCHUNKS, sizeof(Chunk));
    p.win_b = (Chunk *)calloc((size_t)PSV_NCHUNKS, sizeof(Chunk));
    p.q_a = (int *)malloc(sizeof(int) * CU_LIGHT_Q);
    p.q_b = (int *)malloc(sizeof(int) * CU_LIGHT_Q);
    if (!p.light_a || !p.light_b || !p.cells0 || !p.win_a || !p.win_b ||
        !p.q_a || !p.q_b) {
        fprintf(stderr, "FAIL: alloc\n");
        return 0;
    }
    memcpy(p.light_a, p.s.light, (size_t)p.rvol);
    memcpy(p.light_b, p.s.light, (size_t)p.rvol);
    memcpy(p.cells0, p.s.cells, (size_t)p.rvol * sizeof(u16));

    p.a.cells = p.s.cells;
    p.a.light = p.light_a;
    p.a.window = p.win_a;
    p.a.rx0 = p.s.head.rx0; p.a.ry0 = p.s.head.ry0; p.a.rz0 = p.s.head.rz0;
    p.a.rnx = p.s.head.rnx; p.a.rny = p.s.head.rny; p.a.rnz = p.s.head.rnz;
    p.a.rvol = p.rvol;
    p.a.light_valid = 1;
    p.a.light_q = p.q_a;
    p.b = p.a;
    p.b.light = p.light_b;
    p.b.window = p.win_b;
    p.b.light_q = p.q_b;
    /* A zeroed Blaze reads as CU_SKY_ALL everywhere, which is what a fresh
     * load must be: the stored light is not a relaxed fixed point. */

    if (dbg) {
        long bad = -1;
        fprintf(stderr, "  %s: r0 (%d,%d,%d) n (%d,%d,%d) chunks %dx%d\n",
                path, p.a.rx0, p.a.ry0, p.a.rz0, p.a.rnx, p.a.rny, p.a.rnz,
                cu_sky_nc(p.a.rx0, p.a.rnx), cu_sky_nc(p.a.rz0, p.a.rnz));
        if (!is_relax_fixed_point(&p.a, &bad))
            fprintf(stderr, "  %s: loaded light is NOT a relax fixed point "
                            "(first idx %ld)\n", path, bad);
    }

    ok = scenarios(&p) && random_stream(&p, n_edits);

    if (ok)
        fprintf(stderr, "OK: %s %d opacity edits identical%s\n", path,
                p.edits, p.cap15 ? " (WARN: a 15-pass cap was hit)" : "");
    free(p.light_a);
    free(p.light_b);
    free(p.cells0);
    free(p.win_a);
    free(p.win_b);
    free(p.q_a);
    free(p.q_b);
    blaze_snapshot_free(&p.s);
    return ok;
}

static const char *DEFAULT_PATHS[] = {
    "verify/fixtures/port/s10_t0_r64_no_liquid.bsnp",
    "verify/fixtures/port/s10_t0_r64_placement.bsnp",
    "verify/fixtures/port/s10_t0_r64_randtick.bsnp",
    "verify/fixtures/port/s10_t0_r64_fluid_spread.bsnp",
    "verify/fixtures/port/s10_t0_r64_hazards.bsnp",
    "verify/fixtures/port/s10_t0_r64_mobs.bsnp",
    "verify/fixtures/port/s14_t0_r48_no_liquid.bsnp",
    "verify/fixtures/port/s42_t0_r64_biome_plane_ice.bsnp"
};

int main(int argc, char **argv) {
    int n_edits = 400;
    int i;
    const char *env_n = getenv("SKY_EDITS");
    const char *only = getenv("SKY_ONLY");
    if (env_n) n_edits = atoi(env_n);
    if (only && !strcmp(only, "ref")) mode_ref = 1;
    if (only && !strcmp(only, "new")) mode_new = 1;
    dbg = getenv("SKY_DBG") != NULL;
    audit = getenv("SKY_AUDIT") != NULL;

    if (argc > 1) {
        for (i = 1; i < argc; ++i)
            if (!run_one(argv[i], n_edits)) fails = 1;
    } else {
        for (i = 0; i < (int)(sizeof DEFAULT_PATHS / sizeof *DEFAULT_PATHS);
             ++i)
            if (!run_one(DEFAULT_PATHS[i], n_edits)) fails = 1;
    }
    fprintf(stderr, "%s: %lld opacity edits compared (%lld on the "
                    "incremental path), %lld positions skipped\n",
            fails ? "FAIL" : "OK", total_edits, total_incr, total_skipped);
    fprintf(stderr, "full-path edits %lld, spill events %lld (%lld on a full "
                    "rebuild)\n", firsts, spills, spills_full);
    if (audit)
        fprintf(stderr, "sky_dirty claims audited: %lld clean, %lld box\n",
                audit_clean, audit_box);
    fprintf(stderr, "walking-player edits: %lld, incremental %lld (%.1f%%)\n",
            walk_edits, walk_incr,
            walk_edits ? 100.0 * (double)walk_incr / (double)walk_edits : 0.0);
    return fails;
}
