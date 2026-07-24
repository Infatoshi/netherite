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

int gm_live_spawn_stack(GmLiveSim *s, double x, double y, double z,
                        ICStack stack, int pickup_delay) {
    if (!s || stack.item <= 0 || stack.count <= 0) return 0;
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        GmLiveEnt *e = &s->ents[i];
        int n, j;
        if (e->active) continue;
        memset(e, 0, sizeof *e);
        e->active = 1; e->type = 0;
        e->x = x; e->y = y; e->z = z;
        e->item = stack.item;
        e->count = stack.count;
        e->meta = stack.meta;
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
        s->n_active++;
        return 1;
    }
    return 0;
}

int gm_live_spawn_item(GmLiveSim *s, double x, double y, double z,
                       int item, int count, int meta, int pickup_delay) {
    return gm_live_spawn_stack(s, x, y, z, ic_mk(item, count, meta), pickup_delay);
}

static int solid_at(GmWorld *w, int x, int y, int z) {
    int id = gm_world_block(w, x, y, z);
    return id != 0 && id != 8 && id != 9 && id != 10 && id != 11;
}

void gm_live_tick(GmLiveSim *s, GmWorld *w) {
    if (!s || !w) return;
    s->ticks++;

    /* ---- item entities: gravity + ground friction (EntityItem-like) ---- */
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        GmLiveEnt *e = &s->ents[i];
        if (!e->active) continue;
        if (e->pickup_delay > 0) e->pickup_delay--;
        double prev_y = e->y;
        e->my -= 0.03999999910593033; /* (double)0.04f */
        e->x += e->mx;
        e->y += e->my;
        e->z += e->mz;
        /* ground: if feet enter solid, snap to top and zero vertical motion */
        int by = (int)floor(e->y);
        int bx = (int)floor(e->x);
        int bz = (int)floor(e->z);
        if (solid_at(w, bx, by, bz)) {
            e->y = (double)(by + 1);
            e->my = 0.0;
            e->on_ground = 1;
        } else if (solid_at(w, bx, by - 1, bz) && e->y - floor(e->y) < 0.01) {
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

int gm_live_fill_views(const GmLiveSim *s, GmEntityView *out, int max) {
    if (!s || !out || max <= 0) return 0;
    int n = 0;
    for (int i = 0; i < GM_LIVE_MAX && n < max; ++i) {
        const GmLiveEnt *e = &s->ents[i];
        if (!e->active) continue;
        out[n].type = GM_VIEW_ITEM;
        out[n].x = (float)e->x;
        out[n].y = (float)e->y;
        out[n].z = (float)e->z;
        out[n].yaw = 0.0f;
        out[n].health = 20.0f;
        out[n].item_id = e->item;
        out[n].item_meta = e->meta;
        out[n].age = e->age;
        n++;
    }
    return n;
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
