/* blaze_snapshot.c - host-side .bsnp loader (format: blaze_snapshot.h; the
 * writer is game/rl_mode.c rl_snapshot_write). Mirrors rl_snapshot_load's
 * reads plus the trailing coal list the game-side loader skips, and flags
 * regions containing liquids (ids 8-11) so snapshot_requirements can demand
 * BP_FLUIDS. */
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
                        char *err, int err_cap, int no_ore_xy) {
    FILE *f;
    long vol;
    unsigned i;

    memset(out, 0, sizeof *out);
    f = fopen(path, "rb");
    if (!f) return snap_fail(err, err_cap, "cannot open", path);
    if (fread(&out->head, sizeof out->head, 1, f) != 1 ||
        memcmp(out->head.magic, "BSNP", 4) != 0 ||
        out->head.version < 1 ||
        out->head.version > BLAZE_SNAP_VERSION) {
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
    if (out->head.version >= BLAZE_SNAP_VERSION_LIGHT) {
        out->light = (unsigned char *)malloc((size_t)vol);
        if (!out->light ||
            fread(out->light, 1, (size_t)vol, f) != (size_t)vol) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            free(out->light); out->light = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp light", path);
        }
    }
    out->n_mobs = 0;
    if (out->head.version >= 3) {
        if (fread(&out->n_mobs, sizeof out->n_mobs, 1, f) != 1 ||
            out->n_mobs > BLAZE_SNAP_MAX_MOBS) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            free(out->light); out->light = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp mob count", path);
        }
        if (out->n_mobs &&
            fread(out->mobs, sizeof out->mobs[0], out->n_mobs, f) !=
                out->n_mobs) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            free(out->light); out->light = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp mobs", path);
        }
    }
    fclose(f);

    /* spatial index over the static ore list (bucketed coal-candidate
     * rebuild); a malloc failure or non-writer-ordered list just leaves
     * xy_off NULL - consumers fall back to the full scan. no_ore_xy != 0
     * forces the fallback (legacy full-scan A/B; load-time only, no tick
     * cost). */
    out->xy_off = NULL;
    if (out->ncoal && !no_ore_xy) {
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

int blaze_snapshot_write(const char *path, const CuSnapshot *s,
                         char *err, int err_cap) {
    FILE *f;
    long vol;
    int ok = 1;
    unsigned version;

    if (!s || !s->cells) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "empty snapshot: %s",
                     path ? path : "(null)");
        return 0;
    }
    version = s->head.version;
    if (version < 1 || version > BLAZE_SNAP_VERSION) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "bad .bsnp version %u: %s",
                     version, path ? path : "(null)");
        return 0;
    }
    if (s->head.n_items > BLAZE_SNAP_MAX_ITEMS ||
        s->n_mobs > BLAZE_SNAP_MAX_MOBS ||
        s->head.rnx <= 0 || s->head.rny <= 0 || s->head.rnz <= 0) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "implausible .bsnp counts: %s",
                     path ? path : "(null)");
        return 0;
    }
    vol = (long)s->head.rnx * s->head.rny * s->head.rnz;
    if (version >= BLAZE_SNAP_VERSION_LIGHT && !s->light) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "missing light plane: %s",
                     path ? path : "(null)");
        return 0;
    }
    f = fopen(path, "wb");
    if (!f) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "cannot open %s",
                     path ? path : "(null)");
        return 0;
    }
    ok = ok && fwrite(&s->head, sizeof s->head, 1, f) == 1;
    ok = ok && (s->head.n_items == 0 ||
                fwrite(s->items, sizeof s->items[0], s->head.n_items, f) ==
                    s->head.n_items);
    ok = ok && fwrite(s->cells, sizeof *s->cells, (size_t)vol, f) ==
                   (size_t)vol;
    ok = ok && fwrite(&s->ncoal, sizeof s->ncoal, 1, f) == 1;
    ok = ok && (s->ncoal == 0 ||
                fwrite(s->coal, 3 * sizeof *s->coal, s->ncoal, f) == s->ncoal);
    if (version >= BLAZE_SNAP_VERSION_LIGHT)
        ok = ok && fwrite(s->light, 1, (size_t)vol, f) == (size_t)vol;
    if (version >= 3) {
        ok = ok && fwrite(&s->n_mobs, sizeof s->n_mobs, 1, f) == 1;
        ok = ok && (s->n_mobs == 0 ||
                    fwrite(s->mobs, sizeof s->mobs[0], s->n_mobs, f) ==
                        s->n_mobs);
    }
    if (fclose(f) != 0) ok = 0;
    if (!ok && err && err_cap > 0)
        snprintf(err, (size_t)err_cap, "write failed: %s", path);
    return ok;
}

void blaze_snapshot_free(CuSnapshot *s) {
    if (!s) return;
    free(s->cells);  s->cells = NULL;
    free(s->light);  s->light = NULL;
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
