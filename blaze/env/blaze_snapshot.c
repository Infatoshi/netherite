/* blaze_snapshot.c - host-side .bsnp loader (format: blaze_snapshot.h; the
 * writer is game/rl_mode.c rl_snapshot_write). Mirrors rl_snapshot_load's
 * reads plus the trailing coal list the game-side loader skips, and flags
 * regions containing liquids (ids 8-11) so snapshot_requirements can demand
 * BP_FLUIDS. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blaze_snapshot.h"

/* jrand_set(&r, 0) internal cursor (mc_rng.h). Keep mc_rng.h out of this
 * TU so -Werror test binaries do not trip the host-only probe statics. */
#define SNAP_JR_MULT 0x5DEECE66DULL
#define SNAP_JR_MASK ((1ULL << 48) - 1)

static unsigned long long snap_world_rand_default(void) {
    return (0ULL ^ SNAP_JR_MULT) & SNAP_JR_MASK;
}

static size_t snap_mob_disk_size(unsigned version) {
    if (version >= BLAZE_SNAP_VERSION_RESUME) return BLAZE_SNAP_MOB_SIZE_V10;
    if (version >= BLAZE_SNAP_VERSION_ENDER) return BLAZE_SNAP_MOB_SIZE_V7;
    return BLAZE_SNAP_MOB_SIZE_V6;
}

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
    out->n_orbs = 0;
    if (out->head.version >= BLAZE_SNAP_VERSION_MOBS) {
        if (fread(&out->n_mobs, sizeof out->n_mobs, 1, f) != 1 ||
            out->n_mobs > BLAZE_SNAP_MAX_MOBS) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            free(out->light); out->light = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp mob count", path);
        }
        if (out->n_mobs) {
            unsigned mi;
            size_t mob_sz = snap_mob_disk_size(out->head.version);
            memset(out->mobs, 0, sizeof out->mobs);
            for (mi = 0; mi < out->n_mobs; ++mi) {
                if (fread(&out->mobs[mi], mob_sz, 1, f) != 1) {
                    free(out->cells); out->cells = NULL;
                    free(out->coal); out->coal = NULL;
                    free(out->light); out->light = NULL;
                    fclose(f);
                    return snap_fail(err, err_cap, "truncated .bsnp mobs",
                                     path);
                }
            }
        }
    }
    if (out->head.version >= BLAZE_SNAP_VERSION_ORBS) {
        if (fread(&out->n_orbs, sizeof out->n_orbs, 1, f) != 1 ||
            out->n_orbs > BLAZE_SNAP_MAX_ORBS) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            free(out->light); out->light = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp orb count", path);
        }
        if (out->n_orbs &&
            fread(out->orbs, sizeof out->orbs[0], out->n_orbs, f) !=
                out->n_orbs) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            free(out->light); out->light = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp orbs", path);
        }
    }
    out->world_rand_seed = snap_world_rand_default();
    out->update_lcg = 0;
    if (out->head.version >= BLAZE_SNAP_VERSION_WORLD_RAND) {
        if (fread(&out->world_rand_seed, sizeof out->world_rand_seed, 1, f) !=
            1) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            free(out->light); out->light = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp world_rand", path);
        }
        out->world_rand_seed &= SNAP_JR_MASK;
    }
    if (out->head.version >= BLAZE_SNAP_VERSION_UPDATE_LCG) {
        if (fread(&out->update_lcg, sizeof out->update_lcg, 1, f) != 1) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            free(out->light); out->light = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp update_lcg", path);
        }
    }
    {
        long bvol = (long)out->head.rnx * (long)out->head.rnz;
        out->biome = (unsigned char *)malloc((size_t)bvol);
        if (!out->biome) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            free(out->light); out->light = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "biome plane alloc", path);
        }
        if (out->head.version >= BLAZE_SNAP_VERSION_BIOME) {
            if (fread(out->biome, 1, (size_t)bvol, f) != (size_t)bvol) {
                free(out->cells); out->cells = NULL;
                free(out->coal); out->coal = NULL;
                free(out->light); out->light = NULL;
                free(out->biome); out->biome = NULL;
                fclose(f);
                return snap_fail(err, err_cap, "truncated .bsnp biome", path);
            }
        } else {
            /* v7 and older: plains 1. Old fixtures keep HS_BIOME/freeze
             * plains semantics (hostile_spawn.h / randtick_live.h). */
            memset(out->biome, BLAZE_SNAP_BIOME_PLAINS, (size_t)bvol);
        }
    }
    out->player_fire = 0;
    out->player_air = 300;
    if (out->head.version >= BLAZE_SNAP_VERSION_HAZARDS) {
        if (fread(&out->player_fire, sizeof out->player_fire, 1, f) != 1 ||
            fread(&out->player_air, sizeof out->player_air, 1, f) != 1) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            free(out->light); out->light = NULL;
            free(out->biome); out->biome = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp hazards", path);
        }
    }
    out->n_potions = 0;
    memset(out->potions, 0, sizeof out->potions);
    out->ww_total_time = 0;
    out->ww_world_time = 0;
    out->ww_rain_time = 0;
    out->ww_thunder_time = 0;
    out->ww_raining = 0;
    out->ww_thundering = 0;
    out->ww_rand_seed48 = 0;
    out->rt_mutations = 0;
    if (out->head.version >= BLAZE_SNAP_VERSION_RESUME) {
        if (fread(&out->ww_total_time, sizeof out->ww_total_time, 1, f) != 1 ||
            fread(&out->ww_world_time, sizeof out->ww_world_time, 1, f) != 1 ||
            fread(&out->ww_rain_time, sizeof out->ww_rain_time, 1, f) != 1 ||
            fread(&out->ww_thunder_time, sizeof out->ww_thunder_time, 1, f) !=
                1 ||
            fread(&out->ww_raining, sizeof out->ww_raining, 1, f) != 1 ||
            fread(&out->ww_thundering, sizeof out->ww_thundering, 1, f) != 1 ||
            fread(&out->ww_rand_seed48, sizeof out->ww_rand_seed48, 1, f) !=
                1 ||
            fread(&out->rt_mutations, sizeof out->rt_mutations, 1, f) != 1) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            free(out->light); out->light = NULL;
            free(out->biome); out->biome = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp v10 clock", path);
        }
        out->ww_rand_seed48 &= SNAP_JR_MASK;
        if (fread(&out->n_proj, sizeof out->n_proj, 1, f) != 1 ||
            out->n_proj > BLAZE_SNAP_MAX_PROJ) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            free(out->light); out->light = NULL;
            free(out->biome); out->biome = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp v10 proj count",
                             path);
        }
        if (out->n_proj &&
            fread(out->proj, sizeof out->proj[0], out->n_proj, f) !=
                out->n_proj) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            free(out->light); out->light = NULL;
            free(out->biome); out->biome = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp v10 proj", path);
        }
        if (fread(&out->parity_proj_hits, sizeof out->parity_proj_hits, 1,
                  f) != 1 ||
            fread(&out->n_fall, sizeof out->n_fall, 1, f) != 1 ||
            out->n_fall > BLAZE_SNAP_MAX_FALL) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            free(out->light); out->light = NULL;
            free(out->biome); out->biome = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp v10 fall count",
                             path);
        }
        if (out->n_fall &&
            fread(out->falls, sizeof out->falls[0], out->n_fall, f) !=
                out->n_fall) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            free(out->light); out->light = NULL;
            free(out->biome); out->biome = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp v10 falls", path);
        }
        if (fread(&out->n_fall_upd, sizeof out->n_fall_upd, 1, f) != 1 ||
            out->n_fall_upd > BLAZE_SNAP_MAX_FALL_UPD) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            free(out->light); out->light = NULL;
            free(out->biome); out->biome = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp v10 fall upd",
                             path);
        }
        if (out->n_fall_upd &&
            fread(out->fall_upd, sizeof out->fall_upd[0], out->n_fall_upd,
                  f) != out->n_fall_upd) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            free(out->light); out->light = NULL;
            free(out->biome); out->biome = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp v10 fall upd",
                             path);
        }
        if (fread(&out->n_fall_land, sizeof out->n_fall_land, 1, f) != 1 ||
            out->n_fall_land > BLAZE_SNAP_MAX_FALL) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            free(out->light); out->light = NULL;
            free(out->biome); out->biome = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp v10 fall land",
                             path);
        }
        if (out->n_fall_land &&
            fread(out->fall_land, sizeof out->fall_land[0], out->n_fall_land,
                  f) != out->n_fall_land) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            free(out->light); out->light = NULL;
            free(out->biome); out->biome = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp v10 fall land",
                             path);
        }
        if (fread(&out->fall_mutations, sizeof out->fall_mutations, 1, f) !=
                1 ||
            fread(&out->live_ticks, sizeof out->live_ticks, 1, f) != 1) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            free(out->light); out->light = NULL;
            free(out->biome); out->biome = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp v10 fall mut",
                             path);
        }
        if (fread(&out->n_furn, sizeof out->n_furn, 1, f) != 1 ||
            out->n_furn > BLAZE_SNAP_MAX_FURN ||
            (out->n_furn &&
             fread(out->furn, sizeof out->furn[0], out->n_furn, f) !=
                 out->n_furn) ||
            fread(&out->active_furnace, sizeof out->active_furnace, 1, f) !=
                1 ||
            fread(&out->n_chest, sizeof out->n_chest, 1, f) != 1 ||
            out->n_chest > BLAZE_SNAP_MAX_CHEST ||
            (out->n_chest &&
             fread(out->chest, sizeof out->chest[0], out->n_chest, f) !=
                 out->n_chest) ||
            fread(&out->active_chest, sizeof out->active_chest, 1, f) != 1 ||
            fread(out->craft, sizeof out->craft, 1, f) != 1 ||
            fread(out->cursor, sizeof out->cursor, 1, f) != 1 ||
            fread(&out->craft_attempts, sizeof out->craft_attempts, 1, f) !=
                1 ||
            fread(&out->craft_successes, sizeof out->craft_successes, 1, f) !=
                1 ||
            fread(&out->container_opens, sizeof out->container_opens, 1, f) !=
                1 ||
            fread(&out->left_click_counter, sizeof out->left_click_counter, 1,
                  f) != 1 ||
            fread(&out->eat_ticks, sizeof out->eat_ticks, 1, f) != 1 ||
            fread(&out->eat_item, sizeof out->eat_item, 1, f) != 1 ||
            fread(&out->bow_ticks, sizeof out->bow_ticks, 1, f) != 1 ||
            fread(&out->bow_drawing, sizeof out->bow_drawing, 1, f) != 1 ||
            fread(&out->xp_level, sizeof out->xp_level, 1, f) != 1 ||
            fread(&out->xp_total, sizeof out->xp_total, 1, f) != 1 ||
            fread(&out->xp_cooldown, sizeof out->xp_cooldown, 1, f) != 1 ||
            fread(&out->xp_experience, sizeof out->xp_experience, 1, f) != 1 ||
            fread(out->armor, sizeof out->armor, 1, f) != 1 ||
            fread(&out->fluid_dim, sizeof out->fluid_dim, 1, f) != 1 ||
            fread(out->fluid, sizeof out->fluid, 1, f) != 1 ||
            fread(&out->fluid_mutations, sizeof out->fluid_mutations, 1, f) !=
                1 ||
            fread(&out->boat_ride, sizeof out->boat_ride, 1, f) != 1 ||
            fread(&out->explosion_pending, sizeof out->explosion_pending, 1,
                  f) != 1 ||
            fread(&out->explosion_smoking, sizeof out->explosion_smoking, 1,
                  f) != 1 ||
            fread(&out->explosion_flaming, sizeof out->explosion_flaming, 1,
                  f) != 1 ||
            fread(&out->explosion_x, sizeof out->explosion_x, 1, f) != 1 ||
            fread(&out->explosion_y, sizeof out->explosion_y, 1, f) != 1 ||
            fread(&out->explosion_z, sizeof out->explosion_z, 1, f) != 1 ||
            fread(&out->explosion_size, sizeof out->explosion_size, 1, f) !=
                1 ||
            fread(&out->xtra, sizeof out->xtra, 1, f) != 1) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            free(out->light); out->light = NULL;
            free(out->biome); out->biome = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp v10 te/player",
                             path);
        }
    }
    if (out->head.version >= BLAZE_SNAP_VERSION_POTIONS) {
        int np = 0;
        if (fread(&np, sizeof np, 1, f) != 1 || np < 0 ||
            np > BLAZE_SNAP_POTION_MAX) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            free(out->light); out->light = NULL;
            free(out->biome); out->biome = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp potions", path);
        }
        out->n_potions = np;
        if (np > 0 &&
            fread(out->potions, sizeof out->potions[0], (size_t)np, f) !=
                (size_t)np) {
            free(out->cells); out->cells = NULL;
            free(out->coal); out->coal = NULL;
            free(out->light); out->light = NULL;
            free(out->biome); out->biome = NULL;
            fclose(f);
            return snap_fail(err, err_cap, "truncated .bsnp potions", path);
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
        s->n_orbs > BLAZE_SNAP_MAX_ORBS ||
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
    if (version >= BLAZE_SNAP_VERSION_MOBS) {
        ok = ok && fwrite(&s->n_mobs, sizeof s->n_mobs, 1, f) == 1;
        if (s->n_mobs) {
            unsigned mi;
            size_t mob_sz = snap_mob_disk_size(version);
            for (mi = 0; mi < s->n_mobs; ++mi)
                ok = ok && fwrite(&s->mobs[mi], mob_sz, 1, f) == 1;
        }
    }
    if (version >= BLAZE_SNAP_VERSION_ORBS) {
        ok = ok && fwrite(&s->n_orbs, sizeof s->n_orbs, 1, f) == 1;
        ok = ok && (s->n_orbs == 0 ||
                    fwrite(s->orbs, sizeof s->orbs[0], s->n_orbs, f) ==
                        s->n_orbs);
    }
    if (version >= BLAZE_SNAP_VERSION_WORLD_RAND) {
        unsigned long long seed = s->world_rand_seed & SNAP_JR_MASK;
        ok = ok && fwrite(&seed, sizeof seed, 1, f) == 1;
    }
    if (version >= BLAZE_SNAP_VERSION_UPDATE_LCG) {
        int lcg = s->update_lcg;
        ok = ok && fwrite(&lcg, sizeof lcg, 1, f) == 1;
    }
    if (version >= BLAZE_SNAP_VERSION_BIOME) {
        long bvol = (long)s->head.rnx * (long)s->head.rnz;
        if (s->biome)
            ok = ok && fwrite(s->biome, 1, (size_t)bvol, f) == (size_t)bvol;
        else {
            unsigned char *tmp = (unsigned char *)malloc((size_t)bvol);
            if (!tmp) ok = 0;
            else {
                memset(tmp, BLAZE_SNAP_BIOME_PLAINS, (size_t)bvol);
                ok = ok && fwrite(tmp, 1, (size_t)bvol, f) == (size_t)bvol;
                free(tmp);
            }
        }
    }
    if (version >= BLAZE_SNAP_VERSION_HAZARDS) {
        int fire = s->player_fire;
        int air = s->player_air;
        ok = ok && fwrite(&fire, sizeof fire, 1, f) == 1;
        ok = ok && fwrite(&air, sizeof air, 1, f) == 1;
    }
    if (version >= BLAZE_SNAP_VERSION_RESUME) {
        long long tot = s->ww_total_time, wtm = s->ww_world_time;
        int rtm = s->ww_rain_time, ttm = s->ww_thunder_time;
        int rn = s->ww_raining, th = s->ww_thundering;
        unsigned long long wr = s->ww_rand_seed48 & SNAP_JR_MASK;
        unsigned rtmute = s->rt_mutations;
        ok = ok && fwrite(&tot, sizeof tot, 1, f) == 1;
        ok = ok && fwrite(&wtm, sizeof wtm, 1, f) == 1;
        ok = ok && fwrite(&rtm, sizeof rtm, 1, f) == 1;
        ok = ok && fwrite(&ttm, sizeof ttm, 1, f) == 1;
        ok = ok && fwrite(&rn, sizeof rn, 1, f) == 1;
        ok = ok && fwrite(&th, sizeof th, 1, f) == 1;
        ok = ok && fwrite(&wr, sizeof wr, 1, f) == 1;
        ok = ok && fwrite(&rtmute, sizeof rtmute, 1, f) == 1;
        ok = ok && fwrite(&s->n_proj, sizeof s->n_proj, 1, f) == 1;
        ok = ok && (s->n_proj == 0 ||
                    fwrite(s->proj, sizeof s->proj[0], s->n_proj, f) ==
                        s->n_proj);
        ok = ok && fwrite(&s->parity_proj_hits, sizeof s->parity_proj_hits, 1,
                          f) == 1;
        ok = ok && fwrite(&s->n_fall, sizeof s->n_fall, 1, f) == 1;
        ok = ok && (s->n_fall == 0 ||
                    fwrite(s->falls, sizeof s->falls[0], s->n_fall, f) ==
                        s->n_fall);
        ok = ok && fwrite(&s->n_fall_upd, sizeof s->n_fall_upd, 1, f) == 1;
        ok = ok && (s->n_fall_upd == 0 ||
                    fwrite(s->fall_upd, sizeof s->fall_upd[0], s->n_fall_upd,
                           f) == s->n_fall_upd);
        ok = ok && fwrite(&s->n_fall_land, sizeof s->n_fall_land, 1, f) == 1;
        ok = ok && (s->n_fall_land == 0 ||
                    fwrite(s->fall_land, sizeof s->fall_land[0],
                           s->n_fall_land, f) == s->n_fall_land);
        ok = ok && fwrite(&s->fall_mutations, sizeof s->fall_mutations, 1,
                          f) == 1;
        ok = ok && fwrite(&s->live_ticks, sizeof s->live_ticks, 1, f) == 1;
        ok = ok && fwrite(&s->n_furn, sizeof s->n_furn, 1, f) == 1;
        ok = ok && (s->n_furn == 0 ||
                    fwrite(s->furn, sizeof s->furn[0], s->n_furn, f) ==
                        s->n_furn);
        ok = ok && fwrite(&s->active_furnace, sizeof s->active_furnace, 1,
                          f) == 1;
        ok = ok && fwrite(&s->n_chest, sizeof s->n_chest, 1, f) == 1;
        ok = ok && (s->n_chest == 0 ||
                    fwrite(s->chest, sizeof s->chest[0], s->n_chest, f) ==
                        s->n_chest);
        ok = ok && fwrite(&s->active_chest, sizeof s->active_chest, 1, f) ==
                       1;
        ok = ok && fwrite(s->craft, sizeof s->craft, 1, f) == 1;
        ok = ok && fwrite(s->cursor, sizeof s->cursor, 1, f) == 1;
        ok = ok && fwrite(&s->craft_attempts, sizeof s->craft_attempts, 1,
                          f) == 1;
        ok = ok && fwrite(&s->craft_successes, sizeof s->craft_successes, 1,
                          f) == 1;
        ok = ok && fwrite(&s->container_opens, sizeof s->container_opens, 1,
                          f) == 1;
        ok = ok && fwrite(&s->left_click_counter, sizeof s->left_click_counter,
                          1, f) == 1;
        ok = ok && fwrite(&s->eat_ticks, sizeof s->eat_ticks, 1, f) == 1;
        ok = ok && fwrite(&s->eat_item, sizeof s->eat_item, 1, f) == 1;
        ok = ok && fwrite(&s->bow_ticks, sizeof s->bow_ticks, 1, f) == 1;
        ok = ok && fwrite(&s->bow_drawing, sizeof s->bow_drawing, 1, f) == 1;
        ok = ok && fwrite(&s->xp_level, sizeof s->xp_level, 1, f) == 1;
        ok = ok && fwrite(&s->xp_total, sizeof s->xp_total, 1, f) == 1;
        ok = ok && fwrite(&s->xp_cooldown, sizeof s->xp_cooldown, 1, f) == 1;
        ok = ok && fwrite(&s->xp_experience, sizeof s->xp_experience, 1, f) ==
                       1;
        ok = ok && fwrite(s->armor, sizeof s->armor, 1, f) == 1;
        ok = ok && fwrite(&s->fluid_dim, sizeof s->fluid_dim, 1, f) == 1;
        ok = ok && fwrite(s->fluid, sizeof s->fluid, 1, f) == 1;
        ok = ok && fwrite(&s->fluid_mutations, sizeof s->fluid_mutations, 1,
                          f) == 1;
        ok = ok && fwrite(&s->boat_ride, sizeof s->boat_ride, 1, f) == 1;
        ok = ok && fwrite(&s->explosion_pending, sizeof s->explosion_pending, 1,
                          f) == 1;
        ok = ok && fwrite(&s->explosion_smoking, sizeof s->explosion_smoking, 1,
                          f) == 1;
        ok = ok && fwrite(&s->explosion_flaming, sizeof s->explosion_flaming, 1,
                          f) == 1;
        ok = ok && fwrite(&s->explosion_x, sizeof s->explosion_x, 1, f) == 1;
        ok = ok && fwrite(&s->explosion_y, sizeof s->explosion_y, 1, f) == 1;
        ok = ok && fwrite(&s->explosion_z, sizeof s->explosion_z, 1, f) == 1;
        ok = ok && fwrite(&s->explosion_size, sizeof s->explosion_size, 1, f) ==
                       1;
        ok = ok && fwrite(&s->xtra, sizeof s->xtra, 1, f) == 1;
    }
    if (version >= BLAZE_SNAP_VERSION_POTIONS) {
        int np = s->n_potions;
        if (np < 0) np = 0;
        if (np > BLAZE_SNAP_POTION_MAX) np = BLAZE_SNAP_POTION_MAX;
        ok = ok && fwrite(&np, sizeof np, 1, f) == 1;
        ok = ok && (np == 0 ||
                    fwrite(s->potions, sizeof s->potions[0], (size_t)np, f) ==
                        (size_t)np);
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
    free(s->biome);  s->biome = NULL;
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
                if (id != 58 && id != 61 && id != 62 && id != 54) continue;
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
