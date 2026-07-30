/* blaze_snapshot.c - host-side .bsnp loader (format: blaze_snapshot.h; the
 * writer is game/rl_mode.c rl_snapshot_write). Mirrors rl_snapshot_load's
 * reads plus the trailing coal list the game-side loader skips, and flags
 * regions containing liquids (ids 8-11) - fluids are not simulated in blaze. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>

#include "blaze_snapshot.h"

typedef unsigned short cu_u16;

#define BLAZE_SNAP_MAX_CELLS ((size_t)1 << 24)

static int snap_fail(char *err, int cap, const char *msg, const char *path) {
    if (err && cap > 0) snprintf(err, (size_t)cap, "%s: %s", msg, path);
    return 0;
}

static int snap_checked_volume(const RlSnapHead *h, size_t *out) {
    size_t nx, ny, nz, xy;
    if (!h || !out || h->rnx <= 0 || h->rny <= 0 || h->rnz <= 0)
        return 0;
    nx = (size_t)h->rnx;
    ny = (size_t)h->rny;
    nz = (size_t)h->rnz;
    if (nx > BLAZE_SNAP_MAX_CELLS / ny) return 0;
    xy = nx * ny;
    if (xy > BLAZE_SNAP_MAX_CELLS / nz) return 0;
    *out = xy * nz;
    return 1;
}

static int snap_extent_fits_int(int origin, int extent) {
    int64_t end = (int64_t)origin + (int64_t)extent - 1;
    return end >= INT_MIN && end <= INT_MAX;
}

static int64_t snap_floor_div16(int v) {
    int64_t x = (int64_t)v;
    return x >= 0 ? x / 16 : -((-x + 15) / 16);
}

static int snap_window_origin_safe(int origin) {
    int64_t chunk = snap_floor_div16(origin);
    int64_t lo = (chunk - 1) * 16;
    int64_t hi = (chunk + 1) * 16 + 15;
    return lo >= INT_MIN && hi <= INT_MAX;
}

static int snap_coordinates_safe(const RlSnapHead *h) {
    return snap_extent_fits_int(h->rx0, h->rnx) &&
           snap_extent_fits_int(h->ry0, h->rny) &&
           snap_extent_fits_int(h->rz0, h->rnz) &&
           snap_window_origin_safe(h->ox) &&
           snap_window_origin_safe(h->oz);
}

int blaze_snapshot_load(const char *path, CuSnapshot *out,
                        char *err, int err_cap) {
    FILE *f;
    size_t vol;
    unsigned i;

    memset(out, 0, sizeof *out);
    f = fopen(path, "rb");
    if (!f) return snap_fail(err, err_cap, "cannot open", path);
    if (fread(&out->head, sizeof out->head, 1, f) != 1 ||
        memcmp(out->head.magic, "BSNP", 4) != 0 || out->head.version != 1) {
        fclose(f);
        return snap_fail(err, err_cap, "bad .bsnp header", path);
    }
    if (out->head.n_items > BLAZE_SNAP_MAX_ITEMS ||
        !snap_checked_volume(&out->head, &vol)) {
        fclose(f);
        return snap_fail(err, err_cap, "implausible .bsnp counts", path);
    }
    if (!snap_coordinates_safe(&out->head)) {
        fclose(f);
        return snap_fail(err, err_cap, "implausible .bsnp coordinates", path);
    }
    for (i = 0; i < out->head.n_items; ++i)
        if (fread(&out->items[i], sizeof out->items[i], 1, f) != 1) {
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp items", path);
        }
    out->cells = (unsigned short *)malloc(vol * sizeof *out->cells);
    if (!out->cells || fread(out->cells, sizeof *out->cells, vol, f) != vol) {
        free(out->cells); out->cells = NULL;
        fclose(f);
        return snap_fail(err, err_cap, "truncated .bsnp region", path);
    }
    if (fread(&out->ncoal, sizeof out->ncoal, 1, f) != 1 ||
        (size_t)out->ncoal > vol) {
        free(out->cells); out->cells = NULL;
        fclose(f);
        return snap_fail(err, err_cap, "truncated .bsnp coal count", path);
    }
    if (out->ncoal) {
        out->coal = (int *)malloc((size_t)out->ncoal * 3 * sizeof *out->coal);
        if (!out->coal ||
            fread(out->coal, 3 * sizeof *out->coal, out->ncoal, f) !=
                out->ncoal) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp coal list", path);
        }
        for (i = 0; i < out->ncoal; ++i) {
            int64_t ix = (int64_t)out->coal[i * 3 + 0] - out->head.rx0;
            int64_t iy = (int64_t)out->coal[i * 3 + 1] - out->head.ry0;
            int64_t iz = (int64_t)out->coal[i * 3 + 2] - out->head.rz0;
            if (ix < 0 || iy < 0 || iz < 0 || ix >= out->head.rnx ||
                iy >= out->head.rny || iz >= out->head.rnz) {
                free(out->cells); out->cells = NULL;
                free(out->coal); out->coal = NULL;
                fclose(f);
                return snap_fail(err, err_cap,
                                 "coal coordinate outside .bsnp region", path);
            }
        }
    }
    fclose(f);

    /* spatial index over the static ore list (bucketed coal-candidate
     * rebuild); a malloc failure or non-writer-ordered list just leaves
     * xy_off NULL - consumers fall back to the full scan. BLAZE_NO_ORE_XY=1
     * forces the fallback (legacy full-scan A/B; load-time only, no tick
     * cost). */
    out->xy_off = NULL;
    if (out->ncoal && !(getenv("BLAZE_NO_ORE_XY") &&
                        atoi(getenv("BLAZE_NO_ORE_XY")))) {
        size_t ncell = (size_t)out->head.rnx * (size_t)out->head.rny;
        out->xy_off = (int *)malloc((ncell + 1) * sizeof *out->xy_off);
        if (out->xy_off &&
            !blaze_build_ore_xy(out->coal, (int)out->ncoal,
                                out->head.rx0, out->head.ry0, out->head.rz0,
                                out->head.rnx, out->head.rny, out->head.rnz,
                                out->xy_off)) {
            free(out->xy_off);
            out->xy_off = NULL;
        }
    }

    out->has_liquid = 0;
    for (i = 0; (size_t)i < vol; ++i) {
        int id = out->cells[i] >> 4;
        if (id >= 8 && id <= 11) { out->has_liquid = 1; break; }
    }

    /* container list (interact-candidate cache seed). malloc failure ->
     * ncont = -1, consumers keep the full window scan (value-identical). */
    out->cont = (int *)malloc((size_t)BLAZE_SNAP_MAX_CONT * 3 *
                              sizeof *out->cont);
    out->ncont = out->cont
        ? blaze_build_containers(out->cells, out->head.rx0, out->head.ry0,
                                 out->head.rz0, out->head.rnx, out->head.rny,
                                 out->head.rnz, out->cont,
                                 BLAZE_SNAP_MAX_CONT)
        : -1;
    if (out->ncont < 0) { free(out->cont); out->cont = NULL; }
    return 1;
}

void blaze_snapshot_free(CuSnapshot *s) {
    if (!s) return;
    free(s->cells);  s->cells = NULL;
    free(s->coal);   s->coal = NULL;
    free(s->xy_off); s->xy_off = NULL;
    free(s->cont);   s->cont = NULL;
}

int blaze_build_containers(const unsigned short *cells,
                           int rx0, int ry0, int rz0,
                           int rnx, int rny, int rnz, int *out, int cap) {
    long i = 0;
    int ix, iy, iz, n = 0;
    for (ix = 0; ix < rnx; ++ix)
        for (iy = 0; iy < rny; ++iy)
            for (iz = 0; iz < rnz; ++iz, ++i) {
                int id = cells[i] >> 4;
                if (id != 58 && id != 61 && id != 62) continue;
                if (n >= cap) return -1;
                out[n * 3 + 0] = rx0 + ix;
                out[n * 3 + 1] = ry0 + iy;
                out[n * 3 + 2] = rz0 + iz;
                ++n;
            }
    return n;
}

int blaze_build_ore_xy(const int *ore, int nore,
                       int rx0, int ry0, int rz0,
                       int rnx, int rny, int rnz, int *off) {
    long ncell = (long)rnx * rny;
    long prev = -1, cell;
    int i;
    for (i = 0; i < nore; ++i) {   /* verify strict writer (lex x,y,z) order */
        long ix = (long)ore[i * 3 + 0] - (long)rx0;
        long iy = (long)ore[i * 3 + 1] - (long)ry0;
        long iz = (long)ore[i * 3 + 2] - (long)rz0;
        long key;
        if (ix < 0 || iy < 0 || iz < 0 || ix >= rnx || iy >= rny || iz >= rnz)
            return 0;
        key = (ix * rny + iy) * rnz + iz;
        if (key <= prev) return 0;
        prev = key;
    }
    for (cell = 0; cell <= ncell; ++cell) off[cell] = 0;
    for (i = 0; i < nore; ++i)
        off[((long)ore[i * 3 + 0] - (long)rx0) * rny +
            ((long)ore[i * 3 + 1] - (long)ry0) + 1]++;
    for (cell = 1; cell <= ncell; ++cell) off[cell] += off[cell - 1];
    return 1;
}
