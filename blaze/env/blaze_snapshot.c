/* blaze_snapshot.c - host-side .bsnp loader (format: blaze_snapshot.h; the
 * writer is game/rl_mode.c rl_snapshot_write). Mirrors rl_snapshot_load's
 * reads plus the trailing coal list the game-side loader skips, and flags
 * regions containing liquids (ids 8-11) - fluids are not simulated in blaze. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blaze_snapshot.h"

typedef unsigned short cu_u16;

static int snap_fail(char *err, int cap, const char *msg, const char *path) {
    if (err && cap > 0) snprintf(err, (size_t)cap, "%s: %s", msg, path);
    return 0;
}

int blaze_snapshot_load(const char *path, CuSnapshot *out,
                        char *err, int err_cap) {
    FILE *f;
    long vol;
    unsigned i;

    memset(out, 0, sizeof *out);
    f = fopen(path, "rb");
    if (!f) return snap_fail(err, err_cap, "cannot open", path);
    if (fread(&out->head, sizeof out->head, 1, f) != 1 ||
        memcmp(out->head.magic, "BSNP", 4) != 0 || out->head.version != 1) {
        fclose(f);
        return snap_fail(err, err_cap, "bad .bsnp header", path);
    }
    if (out->head.n_items > BLAZE_SNAP_MAX_ITEMS || out->head.rnx <= 0 ||
        out->head.rny <= 0 || out->head.rnz <= 0 ||
        (long)out->head.rnx * out->head.rny * out->head.rnz > (long)1 << 24) {
        fclose(f);
        return snap_fail(err, err_cap, "implausible .bsnp counts", path);
    }
    for (i = 0; i < out->head.n_items; ++i)
        if (fread(&out->items[i], sizeof out->items[i], 1, f) != 1) {
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp items", path);
        }
    vol = (long)out->head.rnx * out->head.rny * out->head.rnz;
    out->cells = (unsigned short *)malloc((size_t)vol * sizeof *out->cells);
    if (!out->cells || fread(out->cells, sizeof *out->cells, (size_t)vol, f) !=
                           (size_t)vol) {
        free(out->cells); out->cells = NULL;
        fclose(f);
        return snap_fail(err, err_cap, "truncated .bsnp region", path);
    }
    if (fread(&out->ncoal, sizeof out->ncoal, 1, f) != 1 ||
        out->ncoal > (unsigned)vol) {
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
        long ncell = (long)out->head.rnx * out->head.rny;
        out->xy_off = (int *)malloc(((size_t)ncell + 1) * sizeof *out->xy_off);
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
    for (i = 0; i < (unsigned)vol; ++i) {
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
        long ix = ore[i * 3 + 0] - rx0;
        long iy = ore[i * 3 + 1] - ry0;
        long iz = ore[i * 3 + 2] - rz0;
        long key;
        if (ix < 0 || iy < 0 || iz < 0 || ix >= rnx || iy >= rny || iz >= rnz)
            return 0;
        key = (ix * rny + iy) * rnz + iz;
        if (key <= prev) return 0;
        prev = key;
    }
    for (cell = 0; cell <= ncell; ++cell) off[cell] = 0;
    for (i = 0; i < nore; ++i)
        off[(long)(ore[i * 3 + 0] - rx0) * rny + (ore[i * 3 + 1] - ry0) + 1]++;
    for (cell = 1; cell <= ncell; ++cell) off[cell] += off[cell - 1];
    return 1;
}
