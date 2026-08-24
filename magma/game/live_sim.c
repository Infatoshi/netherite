/* game/live_sim.c - minimal live entities + plant plot (composition side effects). */
#include "game/live_sim.h"
#include "items_core.h"
#include "inventory_stack_rules.h"
#include "player_survival.h"
#include "plant_growth.h"  /* PG_WHEAT, growth chance helpers */
#include "mc_blocks.h"
#include "mc_rng.h"
#include "entity_item.h"
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
    e->pickup_delay = EI_PICKUP_DEFAULT; e->lifespan = EI_LIFESPAN;
    e->health = EI_HEALTH;
    e->fire = -EI_FIRE_IMMUNE_TICKS;
    e->ticks_existed = 0;
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
    e->lifespan = EI_LIFESPAN;
    e->health = EI_HEALTH;
    e->fire = -EI_FIRE_IMMUNE_TICKS;
    e->ticks_existed = 0;
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

#define IL_OV_STORE GmLiveSim
#define il_ov_fill_free(s, x, y, z, stack, delay) \
    live_try_active_slot((s), (x), (y), (z), (stack), (delay))
#include "item_overflow.h"

int gm_live_spawn_stack(GmLiveSim *s, double x, double y, double z,
                        ICStack stack, int pickup_delay) {
    return il_overflow_spawn(s, x, y, z, stack, pickup_delay);
}

int gm_live_spawn_item(GmLiveSim *s, double x, double y, double z,
                       int item, int count, int meta, int pickup_delay) {
    return gm_live_spawn_stack(s, x, y, z, ic_mk(item, count, meta), pickup_delay);
}

int gm_live_spawn_item_capped(GmLiveSim *s, double x, double y, double z,
                              int item, int count, int meta, int pickup_delay) {
    if (!s || item <= 0 || count <= 0) return 0;
    if (live_try_active_slot(s, x, y, z, ic_mk(item, count, meta),
                             pickup_delay))
        return 1;
    s->spawn_fail_count++;
    return 0;
}

#define FL_W GmWorld
#define fl_id(w, x, y, z) gm_world_block((w), (x), (y), (z))
#define fl_meta(w, x, y, z) (gm_world_meta((w), (x), (y), (z)) & 15)
#define fl_set(w, x, y, z, id, meta) \
    gm_world_set_block_meta((w), (x), (y), (z), (id), (meta))
#define FL_STORE GmLiveSim
#define FL_MAX GM_LIVE_MAX
#define FL_UPDATES GM_LIVE_FALL_UPDATES
#include "falling_live.h"

#define IL_W GmWorld
#define il_id(w, x, y, z) gm_world_block((w), (x), (y), (z))
#define il_meta(w, x, y, z) (gm_world_meta((w), (x), (y), (z)) & 15)
#include "item_live.h"

static void live_to_mc(const GmLiveEnt *e, McItem *it) {
    memset(it, 0, sizeof *it);
    ei_set_position(it, e->x, e->y, e->z);
    it->motionX = e->mx;
    it->motionY = e->my;
    it->motionZ = e->mz;
    it->onGround = e->on_ground;
    it->age = e->age;
    it->delayBeforeCanPickup = e->pickup_delay;
    it->ticksExisted = e->ticks_existed;
    it->lifespan = e->lifespan > 0 ? e->lifespan : EI_LIFESPAN;
    it->dead = 0;
    it->item = e->item;
    it->count = e->count;
    it->meta = e->meta;
    it->hasSubtypes = 1;
    it->hasTag = e->n_enchants > 0;
    it->maxStack = isr_max_stack_size(e->item, e->meta);
    it->health = e->health > 0 ? e->health : EI_HEALTH;
    it->fire = e->fire;
}

static void live_from_mc(GmLiveEnt *e, const McItem *it, GmLiveSim *s) {
    if (it->dead || it->count <= 0) {
        e->active = 0;
        e->count = 0;
        if (s->n_active > 0) s->n_active--;
        return;
    }
    e->x = it->posX;
    e->y = it->posY;
    e->z = it->posZ;
    e->mx = it->motionX;
    e->my = it->motionY;
    e->mz = it->motionZ;
    e->on_ground = it->onGround;
    e->age = it->age;
    e->pickup_delay = it->delayBeforeCanPickup;
    e->ticks_existed = it->ticksExisted;
    e->lifespan = it->lifespan;
    e->item = it->item;
    e->count = it->count;
    e->meta = it->meta;
    e->health = it->health;
    e->fire = it->fire;
}

void gm_live_block_changed(GmLiveSim *s, GmWorld *w,
                           int x, int y, int z) {
    fl_block_changed(s, w, x, y, z);
}

void gm_live_pre_player_tick(GmLiveSim *s, GmWorld *w) {
    fl_pre_player_tick(s, w);
}

void gm_live_tick(GmLiveSim *s, GmWorld *w) {
    if (!s || !w) return;
    il_overflow_drain(s);

    /* WorldServer scheduled block ticks run before the entity update pass.
     * A newly spawned EntityFallingBlock therefore removes its source and
     * takes its first gravity step in this same runtime tick. */
    fl_tick_scheduled(s, w);

    /* ---- item entities: shared EntityItem.onUpdate (item_live.h) ---- */
    {
        McItem its[GM_LIVE_MAX];
        int map[GM_LIVE_MAX];
        int n = 0, k;
        for (int i = 0; i < GM_LIVE_MAX; ++i) {
            GmLiveEnt *e = &s->ents[i];
            if (!e->active) continue;
            if (e->type == 2) {
                fl_tick_entity(s, w, i);
                continue;
            }
            live_to_mc(e, &its[n]);
            map[n] = i;
            n++;
        }
        for (k = 0; k < n; ++k)
            il_tick_item(w, its, n, k);
        for (k = 0; k < n; ++k)
            live_from_mc(&s->ents[map[k]], &its[k], s);
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
    {
        McAABB pbox = psv_player_box(px, py, pz);
        McAABB vol = il_pickup_volume(&pbox);
        for (int i = 0; i < GM_LIVE_MAX; ++i) {
            GmLiveEnt *e = &s->ents[i];
            McItem it;
            int j, n;
            if (!e->active || e->type != 0) continue;
            live_to_mc(e, &it);
            if (it.delayBeforeCanPickup > 0) continue;
            if (!mc_aabb_intersects(&it.box, &vol)) continue;
            {
                ICStack incoming = ic_mk(e->item, e->count, e->meta);
                n = e->n_enchants;
                if (n > IC_MAX_ENCHANTS) n = IC_MAX_ENCHANTS;
                if (n > GM_LIVE_MAX_ENCHANTS) n = GM_LIVE_MAX_ENCHANTS;
                incoming.n_enchants = n;
                for (j = 0; j < n; ++j) {
                    incoming.enchants[j].id = e->ench_id[j];
                    incoming.enchants[j].level = e->ench_lvl[j];
                }
                isr_add_item_stack_to_inventory(&pl->inv, &incoming);
                e->count = incoming.count;
                if (e->count <= 0) {
                    e->active = 0;
                    e->n_enchants = 0;
                    if (s->n_active > 0) s->n_active--;
                }
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
        out[n].yaw = 0.0f;
        out[n].health = 20.0f;
        out[n].item_id = e->item;
        out[n].item_meta = e->meta;
        out[n].age = e->age;
        n++;
    }
    return n;
}

int gm_live_fill_views(const GmLiveSim *s, GmEntityView *out, int max) {
    return gm_live_fill_views_filtered(s, out, max, 0);
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
