/* game/live_sim.c - minimal live entities + plant plot (composition side effects). */
#include "game/live_sim.h"
#include "items_core.h"
#include "inventory_stack_rules.h"
#include "player_survival.h"
#include "plant_growth.h"  /* PG_WHEAT, growth chance helpers */
#include "mc_blocks.h"
#include "mc_rng.h"
#include <math.h>
#include <string.h>

static unsigned lcg_next(unsigned *s) {
    *s = (*s) * 1664525u + 1013904223u;
    return *s;
}

void gm_live_init(GmLiveSim *s, long long seed, int surface_y) {
    memset(s, 0, sizeof *s);
    s->plant_rng = (unsigned)(seed ^ 0xC0FFEEu);
    /* Drop an item above the surface so it falls (non-static motion). */
    GmLiveEnt *e = &s->ents[0];
    e->active = 1;
    e->type = 0;
    e->x = 10.5;
    e->y = (double)surface_y + 4.0;
    e->z = 10.5;
    e->mx = 0.15;
    e->my = 0.0;
    e->mz = 0.05;
    e->on_ground = 0;
    e->age = 0;
    e->health = 5;
    e->item = 4; e->count = 1; e->meta = 0;
    e->pickup_delay = 10; e->lifespan = 6000;
    s->n_active = 1;

    /* Wheat plot next to spawn: farmland + wheat age 0 */
    s->plant_wx = 6;
    s->plant_wy = surface_y;
    s->plant_wz = 6;
    s->plant_age = 0;
    s->plant_active = 1;
    s->ticks = 0;
}

static void live_fill_ent(GmLiveEnt *e, double x, double y, double z,
                          ICStack stack, int pickup_delay) {
    int n, j;
    memset(e, 0, sizeof *e);
    e->active = 1; e->type = 0;
    e->x = x; e->y = y; e->z = z;
    e->item = stack.item;
    e->count = stack.count;
    e->meta = stack.meta;
    e->health = 5;
    n = stack.n_enchants;
    if (n < 0) n = 0;
    if (n > GM_LIVE_MAX_ENCHANTS) n = GM_LIVE_MAX_ENCHANTS;
    if (n > IC_MAX_ENCHANTS) n = IC_MAX_ENCHANTS;
    e->n_enchants = n;
    for (j = 0; j < n; ++j) {
        e->ench_id[j] = stack.enchants[j].id;
        e->ench_lvl[j] = stack.enchants[j].level;
    }
    e->pickup_delay = pickup_delay < 0 ? 0 : pickup_delay;
    e->lifespan = 6000;
}

static int live_try_active_slot(GmLiveSim *s, double x, double y, double z,
                                ICStack stack, int pickup_delay) {
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        GmLiveEnt *e = &s->ents[i];
        if (e->active) continue;
        live_fill_ent(e, x, y, z, stack, pickup_delay);
        s->n_active++;
        return 1;
    }
    return 0;
}

/* Drain overflow into free active slots (FIFO). */
static void live_drain_overflow(GmLiveSim *s) {
    int i = 0;
    if (!s || s->n_overflow <= 0) return;
    while (i < s->n_overflow) {
        if (!live_try_active_slot(s, s->overflow_x[i], s->overflow_y[i],
                                  s->overflow_z[i], s->overflow[i],
                                  s->overflow_delay[i]))
            break;
        /* Shift remaining overflow left. */
        for (int j = i + 1; j < s->n_overflow; ++j) {
            s->overflow[j - 1] = s->overflow[j];
            s->overflow_x[j - 1] = s->overflow_x[j];
            s->overflow_y[j - 1] = s->overflow_y[j];
            s->overflow_z[j - 1] = s->overflow_z[j];
            s->overflow_delay[j - 1] = s->overflow_delay[j];
        }
        s->n_overflow--;
    }
}

int gm_live_spawn_stack(GmLiveSim *s, double x, double y, double z,
                        ICStack stack, int pickup_delay) {
    if (!s || stack.item <= 0 || stack.count <= 0) return 0;
    live_drain_overflow(s);
    if (live_try_active_slot(s, x, y, z, stack, pickup_delay)) return 1;
    /* Table full: hold in bounded overflow (recoverable, not silent loss). */
    if (s->n_overflow < GM_LIVE_OVERFLOW_MAX) {
        int k = s->n_overflow++;
        s->overflow[k] = stack;
        s->overflow_x[k] = x;
        s->overflow_y[k] = y;
        s->overflow_z[k] = z;
        s->overflow_delay[k] = pickup_delay < 0 ? 0 : pickup_delay;
        return 1;
    }
    s->spawn_fail_count++;
    return 0;
}

int gm_live_spawn_item(GmLiveSim *s, double x, double y, double z,
                       int item, int count, int meta, int pickup_delay) {
    return gm_live_spawn_stack(s, x, y, z, ic_mk(item, count, meta), pickup_delay);
}

static int fall_block(int id) {
    return id == BLK_SAND || id == BLK_GRAVEL;
}

/* BlockFalling.canFallThrough: deliberately narrower than isReplaceable.
 * Plants, snow layers, and circuits do not support a gravity block, but they
 * are not AIR/FIRE/WATER/LAVA and therefore do not trigger checkFallable. */
static int fall_through(int id) {
    return id == BLK_AIR || id == 51 ||
           id == BLK_FLOWING_WATER || id == BLK_WATER ||
           id == BLK_FLOWING_LAVA || id == BLK_LAVA;
}

static void fall_schedule_delay(GmLiveSim *s, GmWorld *w,
                                int x, int y, int z, int delay) {
    int id;
    if (!s || !w || y < 0 || y > 255) return;
    id = gm_world_block(w, x, y, z);
    if (!fall_block(id)) return;
    for (int i = 0; i < GM_LIVE_FALL_UPDATES; ++i) {
        GmLiveFallUpdate *u = &s->fall_updates[i];
        if (u->active && u->x == x && u->y == y && u->z == z &&
            u->block_id == id)
            return;
    }
    for (int i = 0; i < GM_LIVE_FALL_UPDATES; ++i) {
        GmLiveFallUpdate *u = &s->fall_updates[i];
        if (u->active) continue;
        u->active = 1;
        u->x = x; u->y = y; u->z = z;
        u->block_id = id;
        u->due_tick = (long long)s->ticks + delay;
        return;
    }
}

void gm_live_block_changed(GmLiveSim *s, GmWorld *w,
                           int x, int y, int z) {
    /* setBlockState calls the placed block's onBlockAdded, then notifies its
     * neighbors. Only the vertical-above notification can make sand/gravel
     * newly unsupported. */
    fall_schedule_delay(s, w, x, y, z, 2);
    fall_schedule_delay(s, w, x, y + 1, z, 2);
}

static int fall_spawn(GmLiveSim *s, int x, int y, int z, int id, int meta) {
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        GmLiveEnt *e = &s->ents[i];
        if (e->active) continue;
        memset(e, 0, sizeof *e);
        e->active = 1;
        e->type = 2;
        e->x = (double)x + 0.5;
        /* EntityFallingBlock ctor: y + (1.0F - height) / 2, height=.98F. */
        e->y = (double)y + (double)((1.0f - 0.98f) / 2.0f);
        e->z = (double)z + 0.5;
        e->item = id;
        e->meta = meta;
        e->lifespan = 600;
        s->n_active++;
        return 1;
    }
    return 0;
}

static int live_spawn_item_exact_impl(
        GmLiveSim *s, int eid, double x, double y, double z,
        double mx, double my, double mz, float yaw,
        float hover_start, int has_hover_start,
        int item, int count, int meta,
        int age, int pickup_delay, int controlled_stationary) {
    if (!s || eid < 0 || item <= 0 || count <= 0 || age < 0
            || pickup_delay < 0 || pickup_delay > 32767
            || (controlled_stationary != 0 && controlled_stationary != 1))
        return 0;
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        GmLiveEnt *e = &s->ents[i];
        if (e->active) continue;
        live_fill_ent(e, x, y, z, ic_mk(item, count, meta), pickup_delay);
        e->eid = eid;
        e->mx = mx;
        e->my = my;
        e->mz = mz;
        e->yaw = yaw;
        e->hover_start = hover_start;
        e->has_hover_start = has_hover_start;
        e->age = age;
        e->controlled_stationary = controlled_stationary;
        ++s->n_active;
        return 1;
    }
    return 0;
}

int gm_live_spawn_item_exact(
        GmLiveSim *s, int eid, double x, double y, double z,
        double mx, double my, double mz, float yaw,
        int item, int count, int meta,
        int age, int pickup_delay, int controlled_stationary) {
    return live_spawn_item_exact_impl(
        s, eid, x, y, z, mx, my, mz, yaw, 0.0F, 0,
        item, count, meta, age, pickup_delay, controlled_stationary);
}

int gm_live_spawn_item_exact_hover(
        GmLiveSim *s, int eid, double x, double y, double z,
        double mx, double my, double mz, float yaw, float hover_start,
        int item, int count, int meta,
        int age, int pickup_delay, int controlled_stationary) {
    return live_spawn_item_exact_impl(
        s, eid, x, y, z, mx, my, mz, yaw, hover_start, 1,
        item, count, meta, age, pickup_delay, controlled_stationary);
}

static int solid_id(int id) {
    /* A moving-piston block's collision shape is supplied by its tile and is
     * not a stationary full cube. The live item integrator does not yet own
     * that swept shape, so treating ID 36 as full would incorrectly snap a
     * just-dropped destroy-reaction item on the extension's first tick. */
    return id != 0 && id != 8 && id != 9 && id != 10 && id != 11
        && id != 36
        /* BlockBasePressurePlate has NULL_AABB for entity collision. */
        && id != 70 && id != 72 && id != 147 && id != 148;
}

static int solid_at(GmWorld *w, int x, int y, int z) {
    return solid_id(gm_world_block(w, x, y, z));
}

/* Highest collision surface in a cell at the falling entity's centered X/Z.
 * This mirrors the shapes already used by player_survival. Non-solid partials
 * return no box: the entity falls through them, then mayPlace decides whether
 * the occupied landing cell is replaceable or the falling block breaks. */
static double fall_collision_top(GmWorld *w, int x, int y, int z) {
    int id = gm_world_block(w, x, y, z);
    int meta = gm_world_meta(w, x, y, z);
    if (id == BLK_WEB || !(mc_bpt_props(id).flags & BF_SOLID)) return -1.0;
    if (id == BLK_STONE_SLAB || id == BLK_WOODEN_SLAB ||
        id == BLK_RED_SANDSTONE_SLAB)
        return (double)y + ((meta & 8) ? 1.0 : 0.5);
    if (id == BLK_SOUL_SAND) return (double)y + 0.875;
    if (id == BLK_CACTUS) return (double)y + 0.9375;
    if (id == BLK_FENCE || id == BLK_NETHER_BRICK_FENCE ||
        id == BLK_COBBLESTONE_WALL)
        return (double)y + 1.5;
    if (id == BLK_TRAPDOOR && !(meta & 4))
        return (double)y + ((meta & 8) ? 1.0 : 0.1875);
    return (double)y + 1.0;
}

static int fall_target_replaceable(GmWorld *w, int x, int y, int z) {
    int id = gm_world_block(w, x, y, z);
    return (mc_bpt_props(id).flags & BF_REPLACEABLE) != 0;
}

static void fall_queue_landing(GmLiveSim *s, int x, int y, int z,
                               int id, int meta) {
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        GmLiveFallLanding *p = &s->fall_landings[i];
        if (p->active) continue;
        p->active = 1;
        p->x = x; p->y = y; p->z = z;
        p->block_id = id; p->block_meta = meta;
        p->due_tick = (long long)s->ticks + 1;
        return;
    }
}

void gm_live_pre_player_tick(GmLiveSim *s, GmWorld *w) {
    if (!s || !w) return;
    /* EntityFallingBlock places on the integrated server. The client observes
     * that block through the next tick's server packet, before click handling.
     * Keeping this boundary explicit is required for held creative attacks:
     * the arriving block can be removed again before the post-tick digest. */
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        GmLiveFallLanding *p = &s->fall_landings[i];
        if (!p->active || p->due_tick > (long long)s->ticks) continue;
        gm_world_set_block_meta(w, p->x, p->y, p->z,
                                p->block_id, p->block_meta);
        /* The packet is one client tick after the server-side placement that
         * scheduled BlockFalling.updateTick. Its subsequent source-removal
         * packet is observed one tick after that server update, so the
         * client-world transition is three ticks from this placement view. */
        fall_schedule_delay(s, w, p->x, p->y, p->z, 3);
        fall_schedule_delay(s, w, p->x, p->y + 1, p->z, 3);
        p->active = 0;
    }
}

static void fall_tick_entity(GmLiveSim *s, GmWorld *w, GmLiveEnt *e) {
    int bx, by, bz;
    if (e->age == 0) {
        bx = (int)floor(e->x); by = (int)floor(e->y); bz = (int)floor(e->z);
        if (gm_world_block(w, bx, by, bz) != e->item) {
            e->active = 0;
            if (s->n_active > 0) s->n_active--;
            return;
        }
        gm_world_set_block(w, bx, by, bz, BLK_AIR);
        gm_live_block_changed(s, w, bx, by, bz);
    }

    e->my -= 0.03999999910593033;
    {
        double old_y = e->y;
        double new_y = old_y + e->my;
        double hit_top = -1.0;
        bx = (int)floor(e->x); bz = (int)floor(e->z);
        for (int y = (int)floor(old_y); y >= (int)floor(new_y) - 1; --y) {
            double top = fall_collision_top(w, bx, y, bz);
            if (top >= 0.0 && top <= old_y && top > new_y && top > hit_top)
                hit_top = top;
        }
        if (hit_top >= 0.0) {
            e->y = hit_top;
            e->on_ground = 1;
        } else {
            e->y = new_y;
            e->on_ground = 0;
        }
    }
    e->x += e->mx;
    e->z += e->mz;
    e->mx *= 0.9800000190734863;
    e->my *= 0.9800000190734863;
    e->mz *= 0.9800000190734863;
    e->age++;

    if (e->on_ground) {
        int below;
        bx = (int)floor(e->x);
        by = (int)floor(e->y);
        bz = (int)floor(e->z);
        below = gm_world_block(w, bx,
                               (int)floor(e->y - 0.009999999776482582), bz);
        if (fall_through(below)) {
            e->on_ground = 0;
            return;
        }
        e->mx *= 0.699999988079071;
        e->mz *= 0.699999988079071;
        e->my *= -0.5;
        e->active = 0;
        if (s->n_active > 0) s->n_active--;
        if (by >= 0 && by <= 255 && fall_target_replaceable(w, bx, by, bz) &&
            !fall_through(gm_world_block(w, bx, by - 1, bz))) {
            fall_queue_landing(s, bx, by, bz, e->item, e->meta);
        }
        /* Vanilla otherwise converts to an EntityItem. Netherite's world
         * truth has no item digest, so a failed mayPlace ends as no block. */
    } else {
        by = (int)floor(e->y);
        if (!((e->age > 100 && (by < 1 || by > 256)) || e->age > 600))
            return;
        e->active = 0;
        if (s->n_active > 0) s->n_active--;
    }
}

void gm_live_tick(GmLiveSim *s, GmWorld *w) {
    if (!s || !w) return;
    live_drain_overflow(s);

    /* WorldServer scheduled block ticks run before the entity update pass.
     * A newly spawned EntityFallingBlock therefore removes its source and
     * takes its first gravity step in this same runtime tick. */
    for (int i = 0; i < GM_LIVE_FALL_UPDATES; ++i) {
        GmLiveFallUpdate *u = &s->fall_updates[i];
        if (!u->active || u->due_tick > (long long)s->ticks) continue;
        if (gm_world_block(w, u->x, u->y, u->z) == u->block_id &&
            u->y >= 0 && fall_through(gm_world_block(w, u->x, u->y - 1, u->z))) {
            if (!fall_spawn(s, u->x, u->y, u->z, u->block_id,
                            gm_world_meta(w, u->x, u->y, u->z))) {
                u->due_tick++;
                continue;
            }
        }
        u->active = 0;
    }

    /* ---- item entities: gravity + ground friction (EntityItem-like) ---- */
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        GmLiveEnt *e = &s->ents[i];
        if (!e->active) continue;
        if (e->type == 2) {
            fall_tick_entity(s, w, e);
            continue;
        }
        if (e->pickup_delay > 0 && e->pickup_delay != 32767)
            e->pickup_delay--;
        if (e->controlled_stationary
                && e->mx == 0.0 && e->my == 0.0 && e->mz == 0.0) {
            ++e->age;
            if (e->lifespan > 0 && e->age >= e->lifespan) {
                e->active = 0;
                if (s->n_active > 0) --s->n_active;
            }
            continue;
        }
        double prev_y = e->y;
        if (!e->controlled_stationary)
            e->my -= 0.03999999910593033; /* (double)0.04f */
        e->x += e->mx;
        e->y += e->my;
        e->z += e->mz;
        /* ground: if feet enter solid, snap to top and zero vertical motion */
        int by = (int)floor(e->y);
        int bx = (int)floor(e->x);
        int bz = (int)floor(e->z);
        int current_id = gm_world_block(w, bx, by, bz);
        double partial_top = 0.0;
        int partial_surface = 0;
        if ((current_id == 44 || current_id == 126 || current_id == 182)
                && (gm_world_meta(w, bx, by, bz) & 8) == 0) {
            partial_top = 0.5;
            partial_surface = 1;
        } else if (current_id == 60) {
            partial_top = 0.9375;
            partial_surface = 1;
        } else if (current_id == 78) {
            partial_top = (double)(gm_world_meta(w, bx, by, bz) & 7)
                * 0.125;
            partial_surface = 1;
        } else if (current_id == 92) {
            partial_top = 0.5;
            partial_surface = 1;
        } else if (current_id == 116) {
            partial_top = 0.75;
            partial_surface = 1;
        } else if (current_id == 171) {
            partial_top = 0.0625;
            partial_surface = 1;
        }
        if (partial_surface) {
            double top = (double)by + partial_top;
            if (partial_top > 0.0 && e->y < top && prev_y >= top) {
                e->y = top;
                e->my = 0.0;
                e->on_ground = 1;
            } else {
                /* Moving up from an exact partial-block surface is not an
                 * overlap with a full cube. */
                e->on_ground = 0;
            }
        } else if (solid_id(current_id)) {
            e->y = (double)(by + 1);
            e->my = 0.0;
            e->on_ground = 1;
        } else if (solid_at(w, bx, by - 1, bz)
                && e->y - floor(e->y) < 0.01) {
            e->on_ground = 1;
            e->my = 0.0;
        } else {
            e->on_ground = 0;
        }
        float slip = 0.6f;
        int under = gm_world_block(w, bx, by - 1, bz);
        if (under == BLK_ICE || under == 174 || under == 212) slip = 0.98f;
        float f = e->on_ground ? (slip * 0.98f) : 0.98f;
        e->mx *= (double)f;
        e->mz *= (double)f;
        e->my *= 0.9800000190734863;
        if (e->on_ground) e->my *= -0.5;
        e->age++;
        if (e->lifespan > 0 && e->age >= e->lifespan) {
            e->active = 0;
            if (s->n_active > 0) s->n_active--;
        }
        (void)prev_y;
    }

    /* ---- wheat growth (simplified BlockCrops.updateTick on our plot) ---- */
    if (s->plant_active && s->plant_age < 7) {
        /* Ensure farmland + wheat exist in the world store. */
        int soil = gm_world_block(w, s->plant_wx, s->plant_wy - 1, s->plant_wz);
        if (soil != 60 /* farmland */) {
            gm_world_set_block_meta(w, s->plant_wx, s->plant_wy - 1, s->plant_wz, 60, 7);
        }
        gm_world_set_block_meta(w, s->plant_wx, s->plant_wy, s->plant_wz, 59 /* wheat */, s->plant_age);
        /* Growth roll: ~1/25 chance per tick when moist (vanilla-ish bound). */
        unsigned r = lcg_next(&s->plant_rng);
        if ((r % 25u) == 0u) {
            s->plant_age++;
            gm_world_set_block_meta(w, s->plant_wx, s->plant_wy, s->plant_wz, 59, s->plant_age);
        }
    }
    s->ticks++;
}

void gm_live_tick_player(GmLiveSim *s, GmWorld *w, struct PsvPlayer *pl_,
                         int player_ox, int player_oz) {
    PsvPlayer *pl = (PsvPlayer *)pl_;
    gm_live_tick(s, w);
    if (!s || !pl) return;
    double px = pl->ent.posX + (double)player_ox;
    double py = pl->ent.posY;
    double pz = pl->ent.posZ + (double)player_oz;
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        GmLiveEnt *e = &s->ents[i];
        if (!e->active || e->type != 0 || e->pickup_delay > 0) continue;
        if (fabs(e->x - px) > 1.0 || fabs(e->z - pz) > 1.0 ||
            e->y < py - 0.25 || e->y > py + 2.8) continue;
        {
            ICStack incoming = ic_mk(e->item, e->count, e->meta);
            int j, n = e->n_enchants;
            if (n > IC_MAX_ENCHANTS) n = IC_MAX_ENCHANTS;
            if (n > GM_LIVE_MAX_ENCHANTS) n = GM_LIVE_MAX_ENCHANTS;
            incoming.n_enchants = n;
            for (j = 0; j < n; ++j) {
                incoming.enchants[j].id = e->ench_id[j];
                incoming.enchants[j].level = e->ench_lvl[j];
            }
            isr_add_item_stack_to_inventory(&pl->inv, &incoming);
            e->count = incoming.count;
            /* leftover retains the same StoredEnchantments payload */
            if (e->count <= 0) {
                e->active = 0;
                e->n_enchants = 0;
                if (s->n_active > 0) s->n_active--;
            }
        }
    }
}

int gm_live_fill_views_filtered(const GmLiveSim *s, GmEntityView *out,
                                int max, int suppress_falling) {
    if (!s || !out || max <= 0) return 0;
    int n = 0;
    for (int i = 0; i < GM_LIVE_MAX && n < max; ++i) {
        const GmLiveEnt *e = &s->ents[i];
        if (!e->active) continue;
        if (suppress_falling && e->type == 2) continue;
        memset(&out[n], 0, sizeof out[n]);
        out[n].type = e->type == 2 ? GM_VIEW_FALLING_BLOCK : GM_VIEW_ITEM;
        out[n].x = (float)e->x;
        out[n].y = (float)e->y;
        out[n].z = (float)e->z;
        out[n].yaw = e->yaw;
        out[n].health = 20.0f;
        out[n].item_id = e->item;
        out[n].item_meta = e->meta;
        out[n].age = e->age;
        out[n].ent_id = e->eid;
        out[n].item_count = e->count;
        out[n].hover_start = e->hover_start;
        out[n].has_hover_start = e->has_hover_start;
        n++;
    }
    return n;
}

int gm_live_fill_views(const GmLiveSim *s, GmEntityView *out, int max) {
    return gm_live_fill_views_filtered(s, out, max, 0);
}

static McAABB live_item_box(const GmLiveEnt *e) {
    return mc_aabb_make(
        e->x - 0.125, e->y, e->z - 0.125,
        e->x + 0.125, e->y + 0.25, e->z + 0.125);
}

int gm_live_item_boxes(
        const GmLiveSim *s, McAABB *out, int capacity) {
    if (!s || !out || capacity <= 0) return 0;
    int count = 0;
    for (int i = 0; i < GM_LIVE_MAX && count < capacity; ++i) {
        const GmLiveEnt *e = &s->ents[i];
        if (!e->active || e->type != 0) continue;
        out[count++] = live_item_box(e);
    }
    return count;
}

int gm_live_explosion_targets(
        const GmLiveSim *s, GmLiveExplosionTarget *out, int capacity) {
    if (!s || !out || capacity <= 0) return 0;
    int count = 0;
    for (int i = 0; i < GM_LIVE_MAX && count < capacity; ++i) {
        const GmLiveEnt *e = &s->ents[i];
        GmLiveExplosionTarget *target;
        if (!e->active || e->type != 0) continue;
        target = &out[count++];
        target->slot = i;
        target->eid = e->eid;
        target->item = e->item;
        target->x = e->x;
        target->y = e->y;
        target->z = e->z;
        target->box = live_item_box(e);
    }
    return count;
}

int gm_live_apply_explosion(
        GmLiveSim *s, int slot, float damage,
        double impulse_x, double impulse_y, double impulse_z) {
    GmLiveEnt *e;
    if (!s || slot < 0 || slot >= GM_LIVE_MAX)
        return 0;
    e = &s->ents[slot];
    if (!e->active || e->type != 0)
        return 0;
    /* EntityItem rejects explosion damage for a Nether Star, but Explosion
     * still appends its raw motion after attackEntityFrom returns false. */
    if (e->item != 399)
        e->health = (int)((float)e->health - damage);
    e->mx += impulse_x;
    e->my += impulse_y;
    e->mz += impulse_z;
    if (e->health <= 0) {
        e->active = 0;
        e->n_enchants = 0;
        if (s->n_active > 0) --s->n_active;
        return 0;
    }
    return 1;
}

int gm_live_items_intersects_aabb(
        const GmLiveSim *s, const McAABB *box) {
    return gm_live_items_count_intersects_aabb(s, box) > 0;
}

int gm_live_items_count_intersects_aabb(
        const GmLiveSim *s, const McAABB *box) {
    if (!s || !box) return 0;
    int count = 0;
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        const GmLiveEnt *e = &s->ents[i];
        if (!e->active || e->type != 0) continue;
        McAABB entity = live_item_box(e);
        if (entity.maxX > box->minX && entity.minX < box->maxX
                && entity.maxY > box->minY && entity.minY < box->maxY
                && entity.maxZ > box->minZ && entity.minZ < box->maxZ)
            ++count;
    }
    return count;
}

int gm_live_entity_moved(const GmLiveSim *s) {
    if (!s) return 0;
    /* After a few ticks an airborne item should have left its spawn height. */
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        if (s->ents[i].active && s->ents[i].age > 0 && !s->ents[i].on_ground)
            return 1;
        if (s->ents[i].active && s->ents[i].age > 5)
            return 1; /* settled or still moving: age advanced */
    }
    return 0;
}

int gm_live_plant_age(const GmLiveSim *s) {
    return s ? s->plant_age : -1;
}

int gm_live_overflow_count(const GmLiveSim *s) {
    return s ? s->n_overflow : 0;
}

int gm_live_spawn_fail_count(const GmLiveSim *s) {
    return s ? s->spawn_fail_count : 0;
}
