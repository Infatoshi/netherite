/* game/live_sim.c - minimal live entities + plant plot (composition side effects). */
#include "game/live_sim.h"
#include "items_core.h"
#include "inventory_stack_rules.h"
#include "player_survival.h"
#include "plant_growth.h"  /* PG_WHEAT, growth chance helpers */
#include "mc_blocks.h"
#include "mc_rng.h"
#include "game/block_normal_cube_1_11_2.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static unsigned lcg_next(unsigned *s) {
    *s = (*s) * 1664525u + 1013904223u;
    return *s;
}

static int live_process_spawn_limit;

void gm_live_set_process_spawn_limit(int limit) {
    live_process_spawn_limit = limit > 0
        && limit <= GM_LIVE_OVERFLOW_SAFETY_MAX ? limit : 0;
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
    e->fire = -1;
    e->air = 300;
    e->first_update = 1;
    e->random_seed48 = 0;
    s->n_active = 1;

    /* Wheat plot next to spawn: farmland + wheat age 0 */
    s->plant_wx = 6;
    s->plant_wy = surface_y;
    s->plant_wz = 6;
    s->plant_age = 0;
    s->plant_active = 1;
    s->ticks = 0;
}

void gm_live_destroy(GmLiveSim *s) {
    if (!s) return;
    free(s->overflow);
    s->overflow = NULL;
    s->n_overflow = 0;
    s->overflow_slots = 0;
    s->overflow_cap = 0;
}

int gm_live_entity_slot_count(const GmLiveSim *s) {
    return s ? GM_LIVE_MAX + s->overflow_slots : 0;
}

GmLiveEnt *gm_live_entity_mut(GmLiveSim *s, int slot) {
    if (!s || slot < 0) return NULL;
    if (slot < GM_LIVE_MAX) return &s->ents[slot];
    slot -= GM_LIVE_MAX;
    if (slot >= s->overflow_slots || !s->overflow[slot].occupied)
        return NULL;
    return &s->overflow[slot].entity;
}

const GmLiveEnt *gm_live_entity_ref(const GmLiveSim *s, int slot) {
    if (!s || slot < 0) return NULL;
    if (slot < GM_LIVE_MAX) return &s->ents[slot];
    slot -= GM_LIVE_MAX;
    if (slot >= s->overflow_slots || !s->overflow[slot].occupied)
        return NULL;
    return &s->overflow[slot].entity;
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
    e->repair_cost = stack.repair_cost;
    e->custom_name = stack.custom_name;
    e->tag_id = stack.tag_id;
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
    e->fire = -1;
    e->air = 300;
    e->first_update = 1;
    e->random_seed48 = 0;
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

static int live_overflow_reserve(GmLiveSim *s, int need) {
    int new_cap;
    GmLiveOverflowEnt *grown;
    if (!s || need < 0 || need > GM_LIVE_OVERFLOW_SAFETY_MAX) return 0;
    if (need <= s->overflow_cap) return 1;
    new_cap = s->overflow_cap > 0
        ? s->overflow_cap : GM_LIVE_OVERFLOW_INITIAL;
    while (new_cap < need) {
        if (new_cap > GM_LIVE_OVERFLOW_SAFETY_MAX / 2) {
            new_cap = GM_LIVE_OVERFLOW_SAFETY_MAX;
            break;
        }
        new_cap *= 2;
    }
    grown = (GmLiveOverflowEnt *)realloc(
        s->overflow, (size_t)new_cap * sizeof *grown);
    if (!grown) return 0;
    memset(grown + s->overflow_cap, 0,
           (size_t)(new_cap - s->overflow_cap) * sizeof *grown);
    s->overflow = grown;
    s->overflow_cap = new_cap;
    return 1;
}

int gm_live_reserve_plain_spawns(GmLiveSim *s, int count) {
    int free_hot;
    int added_overflow;
    int final_slots;
    int spawn_limit;
    if (!s || count < 0 || s->n_active < 0 || s->n_active > GM_LIVE_MAX
            || s->n_overflow < 0
            || s->n_overflow > GM_LIVE_OVERFLOW_SAFETY_MAX)
        return 0;
    spawn_limit = s->item_spawn_limit > 0
        ? s->item_spawn_limit : live_process_spawn_limit;
    if (spawn_limit > 0
            && (count > spawn_limit
                || s->n_active > spawn_limit - count
                    - s->n_overflow)) {
        ++s->spawn_fail_count;
        return 0;
    }
    free_hot = GM_LIVE_MAX - s->n_active;
    if (count > GM_LIVE_OVERFLOW_SAFETY_MAX - s->n_overflow)
        return 0;
    added_overflow = count - free_hot;
    if (added_overflow < 0) added_overflow = 0;
    if (added_overflow > GM_LIVE_OVERFLOW_SAFETY_MAX - s->overflow_slots)
        return 0;
    final_slots = s->overflow_slots + added_overflow;
    if (!live_overflow_reserve(s, final_slots)) {
        ++s->spawn_fail_count;
        return 0;
    }
    return 1;
}

int gm_live_spawn_stack(GmLiveSim *s, double x, double y, double z,
                        ICStack stack, int pickup_delay) {
    if (!s || stack.item <= 0 || stack.count <= 0) return 0;
    if (!gm_live_reserve_plain_spawns(s, 1)) return 0;
    if (live_try_active_slot(s, x, y, z, stack, pickup_delay)) return 1;
    /* Table full: grow only the cold FIFO. The hot tick loop remains fixed,
     * while the exceptional cold tail participates in the same semantics. */
    if (live_overflow_reserve(s, s->overflow_slots + 1)) {
        int k = s->overflow_slots++;
        s->overflow[k] = (GmLiveOverflowEnt){
            .occupied = 1,
        };
        live_fill_ent(&s->overflow[k].entity,
            x, y, z, stack, pickup_delay);
        ++s->n_overflow;
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
        ICStack stack,
        int age, int pickup_delay, int controlled_stationary,
        int allow_empty) {
    int valid_stack = stack.item > 0 && stack.count > 0;
    if (allow_empty && stack.item == 0 && stack.count == 0) valid_stack = 1;
    if (!s || eid < 0 || !valid_stack || age < -32768
            || age > 32767 || pickup_delay < -32768
            || pickup_delay > 32767
            || (controlled_stationary != 0 && controlled_stationary != 1))
        return 0;
    if (!gm_live_reserve_plain_spawns(s, 1)) return 0;
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        GmLiveEnt *e = &s->ents[i];
        if (e->active) continue;
        live_fill_ent(e, x, y, z, stack, pickup_delay);
        e->eid = eid;
        e->mx = mx;
        e->my = my;
        e->mz = mz;
        e->yaw = yaw;
        e->hover_start = hover_start;
        e->has_hover_start = has_hover_start;
        e->age = age;
        e->pickup_delay = pickup_delay;
        e->controlled_stationary = controlled_stationary;
        e->no_gravity = controlled_stationary;
        ++s->n_active;
        return 1;
    }
    if (!live_overflow_reserve(s, s->overflow_slots + 1)) {
        ++s->spawn_fail_count;
        return 0;
    }
    {
        int k = s->overflow_slots++;
        GmLiveOverflowEnt *cold = &s->overflow[k];
        memset(cold, 0, sizeof *cold);
        cold->occupied = 1;
        live_fill_ent(&cold->entity, x, y, z, stack, pickup_delay);
        cold->entity.eid = eid;
        cold->entity.mx = mx;
        cold->entity.my = my;
        cold->entity.mz = mz;
        cold->entity.yaw = yaw;
        cold->entity.hover_start = hover_start;
        cold->entity.has_hover_start = has_hover_start;
        cold->entity.age = age;
        cold->entity.pickup_delay = pickup_delay;
        cold->entity.controlled_stationary = controlled_stationary;
        cold->entity.no_gravity = controlled_stationary;
        ++s->n_overflow;
    }
    return 1;
}

int gm_live_spawn_item_exact(
        GmLiveSim *s, int eid, double x, double y, double z,
        double mx, double my, double mz, float yaw,
        int item, int count, int meta,
        int age, int pickup_delay, int controlled_stationary) {
    return live_spawn_item_exact_impl(
        s, eid, x, y, z, mx, my, mz, yaw, 0.0F, 0,
        ic_mk(item, count, meta), age, pickup_delay,
        controlled_stationary, 0);
}

int gm_live_spawn_item_exact_hover(
        GmLiveSim *s, int eid, double x, double y, double z,
        double mx, double my, double mz, float yaw, float hover_start,
        int item, int count, int meta,
        int age, int pickup_delay, int controlled_stationary) {
    return live_spawn_item_exact_impl(
        s, eid, x, y, z, mx, my, mz, yaw, hover_start, 1,
        ic_mk(item, count, meta), age, pickup_delay,
        controlled_stationary, 0);
}

int gm_live_spawn_stack_exact_hover(
        GmLiveSim *s, int eid, double x, double y, double z,
        double mx, double my, double mz, float yaw, float hover_start,
        ICStack stack, int age, int pickup_delay, int controlled_stationary) {
    return live_spawn_item_exact_impl(
        s, eid, x, y, z, mx, my, mz, yaw, hover_start, 1,
        stack, age, pickup_delay, controlled_stationary, 0);
}

int gm_live_spawn_item_state_exact(
        GmLiveSim *s, int eid, double x, double y, double z,
        double mx, double my, double mz, float yaw, float hover_start,
        int item, int count, int meta, int age, int pickup_delay,
        int health, int lifespan, int on_ground, int no_gravity,
        int ticks_existed) {
    if (!s || eid < 0 || health <= 0 || health > 5
            || lifespan <= 0 || age < -32768 || age >= lifespan
            || ticks_existed < 0
            || (on_ground != 0 && on_ground != 1)
            || (no_gravity != 0 && no_gravity != 1))
        return 0;
    if (!live_spawn_item_exact_impl(
            s, eid, x, y, z, mx, my, mz, yaw, hover_start, 1,
            ic_mk(item, count, meta), age, pickup_delay, 0, 0))
        return 0;
    for (int i = 0; i < gm_live_entity_slot_count(s); ++i) {
        GmLiveEnt *e = gm_live_entity_mut(s, i);
        if (!e) continue;
        if (!e->active || e->type != 0 || e->eid != eid) continue;
        e->health = health;
        e->lifespan = lifespan;
        e->on_ground = on_ground;
        e->no_gravity = no_gravity;
        e->ticks_existed = ticks_existed;
        return 1;
    }
    return 0;
}

int gm_live_set_item_environment_state(
        GmLiveSim *s, int eid, unsigned long long random_seed48,
        int fire, int in_water, int first_update) {
    if (!s || eid < 0 || random_seed48 >= (1ULL << 48)
            || fire < -1 || fire > 32767
            || (in_water != 0 && in_water != 1)
            || (first_update != 0 && first_update != 1))
        return 0;
    for (int i = 0; i < gm_live_entity_slot_count(s); ++i) {
        GmLiveEnt *e = gm_live_entity_mut(s, i);
        if (!e) continue;
        if (!e->active || e->type != 0 || e->eid != eid) continue;
        e->random_seed48 = random_seed48;
        e->fire = fire;
        e->in_water = in_water;
        e->first_update = first_update;
        return 1;
    }
    return 0;
}

int gm_live_spawn_empty_item_exact_hover(
        GmLiveSim *s, int eid, double x, double y, double z,
        double mx, double my, double mz, float yaw, float hover_start) {
    return live_spawn_item_exact_impl(
        s, eid, x, y, z, mx, my, mz, yaw, hover_start, 1,
        ic_empty(), 0, 0, 0, 1);
}

static int solid_id(int id) {
    /* A moving-piston block's collision shape is supplied by its tile and is
     * not a stationary full cube. The live item integrator does not yet own
     * that swept shape, so treating ID 36 as full would incorrectly snap a
     * just-dropped destroy-reaction item on the extension's first tick. */
    return id != 0 && id != 8 && id != 9 && id != 10 && id != 11
        && id != 51
        && id != 36
        /* Saplings, BlockCrops, and BlockStem have no entity collision box.
         * This matters when an unsupported callback drops an item and its
         * stale-state growth body restores the plant before that EntityItem's
         * first move. */
        && id != 6 && id != 59 && id != 104 && id != 105
        && id != 141 && id != 142 && id != 207
        /* BlockDoublePlant has a full selection box but no entity collision
         * box. A bonemeal clone spawned inside it must remain airborne. */
        && id != 175
        /* BlockBasePressurePlate has NULL_AABB for entity collision. */
        && id != 70 && id != 72 && id != 147 && id != 148;
}

static int solid_at(GmWorld *w, int x, int y, int z) {
    return solid_id(gm_world_block(w, x, y, z));
}

static int live_item_tags_equal(const GmLiveEnt *a, const GmLiveEnt *b) {
    int n;
    if (!a || !b || a->repair_cost != b->repair_cost
            || a->custom_name != b->custom_name
            || a->tag_id != b->tag_id
            || a->n_enchants != b->n_enchants)
        return 0;
    n = a->n_enchants;
    if (n < 0 || n > GM_LIVE_MAX_ENCHANTS) return 0;
    for (int i = 0; i < n; ++i)
        if (a->ench_id[i] != b->ench_id[i]
                || a->ench_lvl[i] != b->ench_lvl[i])
            return 0;
    return 1;
}

static void live_retire_slot(GmLiveSim *s, int slot, GmLiveEnt *e) {
    if (!s || !e || !e->active) return;
    e->active = 0;
    if (slot < GM_LIVE_MAX) {
        if (s->n_active > 0) --s->n_active;
        return;
    }
    slot -= GM_LIVE_MAX;
    if (slot < 0 || slot >= s->overflow_slots
            || !s->overflow[slot].occupied)
        return;
    s->overflow[slot].occupied = 0;
    if (s->n_overflow > 0) --s->n_overflow;
    while (s->overflow_slots > 0
            && !s->overflow[s->overflow_slots - 1].occupied)
        --s->overflow_slots;
}

int gm_live_retire_entity_slot(GmLiveSim *s, int slot) {
    GmLiveEnt *e = gm_live_entity_mut(s, slot);
    if (!e || !e->active) return 0;
    live_retire_slot(s, slot, e);
    return 1;
}

static int live_item_meta_is_payload(int item) {
    /* Compact ICStack encodes these vanilla NBT payloads in meta. */
    return item == 373 || item == 401 || item == 402
        || item == 438 || item == 440 || item == 441;
}

/* EntityItem.combineItems. Represented valid stacks retain metadata exactly;
 * that is equivalent to Java for subtype items and harmlessly conservative
 * for malformed non-subtype stacks with nonzero damage. */
static int live_item_combine(GmLiveSim *s, int ai, int bi) {
    GmLiveEnt *a, *b;
    int max_stack;
    if (!s || ai < 0 || ai >= gm_live_entity_slot_count(s)
            || bi < 0 || bi >= gm_live_entity_slot_count(s) || ai == bi)
        return 0;
    a = gm_live_entity_mut(s, ai);
    b = gm_live_entity_mut(s, bi);
    if (!a || !b) return 0;
    if (!a->active || !b->active || a->type != 0 || b->type != 0
            || a->item <= 0 || b->item != a->item
            || a->pickup_delay == 32767 || b->pickup_delay == 32767
            || a->age == -32768 || b->age == -32768
            || ((isr_has_subtypes(a->item)
                    || live_item_meta_is_payload(a->item))
                && a->meta != b->meta)
            || !live_item_tags_equal(a, b))
        return 0;
    if (b->count < a->count)
        return live_item_combine(s, bi, ai);
    max_stack = isr_max_stack_size(b->item, b->meta);
    if (a->count <= 0 || b->count <= 0
            || b->count > max_stack - a->count)
        return 0;
    b->count += a->count;
    if (a->pickup_delay > b->pickup_delay)
        b->pickup_delay = a->pickup_delay;
    if (a->age < b->age)
        b->age = a->age;
    a->count = 0;
    live_retire_slot(s, ai, a);
    return 1;
}

static int live_item_search_nearby(
        GmLiveSim *s, int index, const int *slots, int slot_count) {
    GmLiveEnt *e;
    int iterations;
    if (!s || index < 0 || index >= gm_live_entity_slot_count(s)) return 0;
    e = gm_live_entity_mut(s, index);
    if (!e) return 0;
    if (!e->active || e->type != 0) return 0;
    iterations = slots ? slot_count : gm_live_entity_slot_count(s);
    for (int sequence = 0; sequence < iterations; ++sequence) {
        int j = slots ? slots[sequence] : sequence;
        GmLiveEnt *other = gm_live_entity_mut(s, j);
        /* this.box.expand(0.5,0,0.5), then strict AABB intersection. */
        if (!other || !other->active || other->type != 0
                || fabs(other->x - e->x) >= 0.75
                || other->y >= e->y + 0.25
                || other->y + 0.25 <= e->y
                || fabs(other->z - e->z) >= 0.75)
            continue;
        (void)live_item_combine(s, index, j);
        if (!e->active) return 1;
    }
    return 0;
}

static int live_ceil(double value) {
    int i = (int)value;
    return value > (double)i ? i + 1 : i;
}

static float live_item_random_float(GmLiveEnt *e) {
    JavaRandom random;
    float value;
    jrand_set_seed48(&random, e->random_seed48);
    value = jrand_float(&random);
    e->random_seed48 = random.seed;
    return value;
}

static int live_water_depth(const GmWorld *w, int x, int y, int z) {
    int id = gm_world_block(w, x, y, z);
    int meta;
    if (id != BLK_FLOWING_WATER && id != BLK_WATER) return -1;
    meta = gm_world_meta(w, x, y, z) & 15;
    return meta >= 8 ? 0 : meta;
}

static int live_flow_side_solid(const GmWorld *w, int x, int y, int z) {
    int id = gm_world_block(w, x, y, z);
    if (id == BLK_FLOWING_WATER || id == BLK_WATER || id == BLK_ICE)
        return 0;
    return solid_id(id);
}

/* BlockLiquid.getFlow, preserving Java's S,W,N,E accumulation order. */
static void live_water_cell_flow(
        const GmWorld *w, int bx, int by, int bz,
        double *fx, double *fy, double *fz) {
    static const int dx[4] = {0, -1, 0, 1};
    static const int dz[4] = {1, 0, -1, 0};
    double x = 0.0, y = 0.0, z = 0.0;
    int depth = live_water_depth(w, bx, by, bz);
    for (int face = 0; face < 4; ++face) {
        int nx = bx + dx[face], nz = bz + dz[face];
        int neighbor = live_water_depth(w, nx, by, nz);
        if (neighbor < 0) {
            if (!solid_id(gm_world_block(w, nx, by, nz))) {
                neighbor = live_water_depth(w, nx, by - 1, nz);
                if (neighbor >= 0) {
                    int weight = neighbor - (depth - 8);
                    x += (double)(dx[face] * weight);
                    z += (double)(dz[face] * weight);
                }
            }
        } else {
            int weight = neighbor - depth;
            x += (double)(dx[face] * weight);
            z += (double)(dz[face] * weight);
        }
    }
    if ((gm_world_meta(w, bx, by, bz) & 15) >= 8) {
        for (int face = 0; face < 4; ++face) {
            int nx = bx + dx[face], nz = bz + dz[face];
            if (live_flow_side_solid(w, nx, by, nz)
                    || live_flow_side_solid(w, nx, by + 1, nz)) {
                double length = sqrt(x * x + y * y + z * z);
                if (length < 1.0E-4) x = y = z = 0.0;
                else { x /= length; y /= length; z /= length; }
                y -= 6.0;
                break;
            }
        }
    }
    {
        double length = sqrt(x * x + y * y + z * z);
        if (length < 1.0E-4) *fx = *fy = *fz = 0.0;
        else { *fx = x / length; *fy = y / length; *fz = z / length; }
    }
}

/* Entity.handleWaterMovement calls World.handleMaterialAcceleration twice in
 * EntityItem.onUpdate: once through Entity.onEntityUpdate using the unusual
 * vertically contracted base box, then once using the exact item box. */
static int live_item_handle_water(
        GmLiveEnt *e, const GmWorld *w, int base_probe) {
    double min_x = e->x - 0.125;
    double max_x = e->x + 0.125;
    double min_y = e->y;
    double max_y = e->y + 0.25;
    double min_z = e->z - 0.125;
    double max_z = e->z + 0.125;
    double sum_x = 0.0, sum_y = 0.0, sum_z = 0.0;
    int found = 0;
    if (base_probe) {
        double a = min_y + 0.4000000059604645;
        double b = max_y - 0.4000000059604645;
        min_y = a < b ? a : b;
        max_y = (a > b ? a : b) - 0.001;
        max_x -= 0.001;
        max_z -= 0.001;
    }
    for (int x = (int)floor(min_x); x < live_ceil(max_x); ++x)
        for (int y = (int)floor(min_y); y < live_ceil(max_y); ++y)
            for (int z = (int)floor(min_z); z < live_ceil(max_z); ++z) {
                int id = gm_world_block(w, x, y, z);
                double fx, fy, fz;
                if (id != BLK_FLOWING_WATER && id != BLK_WATER) continue;
                found = 1;
                live_water_cell_flow(w, x, y, z, &fx, &fy, &fz);
                sum_x += fx; sum_y += fy; sum_z += fz;
            }
    {
        double length = sqrt(
            sum_x * sum_x + sum_y * sum_y + sum_z * sum_z);
        if (length > 0.0) {
            e->mx += sum_x / length * 0.014;
            e->my += sum_y / length * 0.014;
            e->mz += sum_z / length * 0.014;
        }
    }
    if (found) {
        if (!e->in_water && !e->first_update) {
            /* resetHeight: splash pitch, six bubble calls, six splash calls.
             * Particle payloads are a visual residual; the private cursor is
             * exact and therefore later lava/item behavior remains stable. */
            (void)live_item_random_float(e);
            (void)live_item_random_float(e);
            for (int i = 0; i < 6; ++i) {
                (void)live_item_random_float(e);
                (void)live_item_random_float(e);
                (void)live_item_random_float(e);
            }
            for (int i = 0; i < 6; ++i) {
                (void)live_item_random_float(e);
                (void)live_item_random_float(e);
            }
        }
        e->in_water = 1;
    } else {
        e->in_water = 0;
    }
    return found;
}

static int live_item_in_lava(const GmLiveEnt *e, const GmWorld *w) {
    double min_x = e->x - 0.025;
    double max_x = e->x + 0.025;
    double min_y = e->y - 0.15000000596046448;
    double max_y = e->y + 0.4000000059604645;
    double min_z = e->z - 0.025;
    double max_z = e->z + 0.025;
    for (int x = (int)floor(min_x); x < live_ceil(max_x); ++x)
        for (int y = (int)floor(min_y); y < live_ceil(max_y); ++y)
            for (int z = (int)floor(min_z); z < live_ceil(max_z); ++z) {
                int id = gm_world_block(w, x, y, z);
                if (id == BLK_FLOWING_LAVA || id == BLK_LAVA) return 1;
            }
    return 0;
}

static int live_item_in_fire(const GmLiveEnt *e, const GmWorld *w) {
    double min_x = e->x - 0.125;
    double max_x = e->x + 0.124;
    double min_y = e->y;
    double max_y = e->y + 0.249;
    double min_z = e->z - 0.125;
    double max_z = e->z + 0.124;
    for (int x = (int)floor(min_x); x < live_ceil(max_x); ++x)
        for (int y = (int)floor(min_y); y < live_ceil(max_y); ++y)
            for (int z = (int)floor(min_z); z < live_ceil(max_z); ++z) {
                int id = gm_world_block(w, x, y, z);
                if (id == 51 || id == BLK_FLOWING_LAVA || id == BLK_LAVA)
                    return 1;
            }
    return 0;
}

static int live_item_cactus_contacts(const GmLiveEnt *e, const GmWorld *w) {
    int contacts = 0;
    int min_x = (int)floor(e->x - 0.125 + 0.001);
    int max_x = (int)floor(e->x + 0.125 - 0.001);
    int min_y = (int)floor(e->y + 0.001);
    int max_y = (int)floor(e->y + 0.25 - 0.001);
    int min_z = (int)floor(e->z - 0.125 + 0.001);
    int max_z = (int)floor(e->z + 0.125 - 0.001);
    for (int x = min_x; x <= max_x; ++x)
        for (int y = min_y; y <= max_y; ++y)
            for (int z = min_z; z <= max_z; ++z)
                if (gm_world_block(w, x, y, z) == BLK_CACTUS)
                    ++contacts;
    return contacts;
}

static int live_item_full_cube(
        const GmWorld *w, int x, int y, int z) {
    /* World.isBlockFullCube derives this from the collision AABB's average
     * edge length, not IBlockState.isFullCube. Soul sand reports the latter
     * true but its 7/8-high collision box must remain an escape route. */
    if (gm_world_block(w, x, y, z) == BLK_SOUL_SAND) return 0;
    return gm_block_is_full_cube_1_11_2(
        gm_world_block(w, x, y, z), gm_world_meta(w, x, y, z));
}

static int live_item_collides_full_cube(
        const GmLiveEnt *e, const GmWorld *w) {
    McAABB item = mc_aabb_make(
        e->x - 0.125, e->y, e->z - 0.125,
        e->x + 0.125, e->y + 0.25, e->z + 0.125);
    int min_x = (int)floor(item.minX);
    int max_x = live_ceil(item.maxX);
    int min_y = (int)floor(item.minY);
    int max_y = live_ceil(item.maxY);
    int min_z = (int)floor(item.minZ);
    int max_z = live_ceil(item.maxZ);
    for (int x = min_x; x < max_x; ++x)
        for (int y = min_y; y < max_y; ++y)
            for (int z = min_z; z < max_z; ++z) {
                McAABB block;
                if (!live_item_full_cube(w, x, y, z)) continue;
                block = mc_aabb_make(
                    (double)x, (double)y, (double)z,
                    (double)x + 1.0, (double)y + 1.0,
                    (double)z + 1.0);
                if (mc_aabb_intersects(&item, &block)) return 1;
            }
    return 0;
}

static int live_item_push_out(GmLiveEnt *e, const GmWorld *w) {
    int bx, by, bz, direction = 4;
    double fx, fy, fz, nearest = INFINITY;
    float force, sign;
    if (!live_item_collides_full_cube(e, w)) return 0;
    bx = (int)floor(e->x);
    by = (int)floor(e->y + 0.125);
    bz = (int)floor(e->z);
    fx = e->x - (double)bx;
    fy = e->y + 0.125 - (double)by;
    fz = e->z - (double)bz;
    if (!live_item_full_cube(w, bx - 1, by, bz) && fx < nearest) {
        nearest = fx; direction = 0;
    }
    if (!live_item_full_cube(w, bx + 1, by, bz)
            && 1.0 - fx < nearest) {
        nearest = 1.0 - fx; direction = 1;
    }
    if (!live_item_full_cube(w, bx, by, bz - 1) && fz < nearest) {
        nearest = fz; direction = 2;
    }
    if (!live_item_full_cube(w, bx, by, bz + 1)
            && 1.0 - fz < nearest) {
        nearest = 1.0 - fz; direction = 3;
    }
    if (!live_item_full_cube(w, bx, by + 1, bz)
            && 1.0 - fy < nearest)
        direction = 4;
    force = live_item_random_float(e) * 0.2F + 0.1F;
    sign = (direction == 0 || direction == 2) ? -1.0F : 1.0F;
    if (direction <= 1) {
        e->mx = (double)(sign * force);
        e->my *= 0.75;
        e->mz *= 0.75;
    } else if (direction <= 3) {
        e->mx *= 0.75;
        e->my *= 0.75;
        e->mz = (double)(sign * force);
    } else {
        e->mx *= 0.75;
        e->my = (double)force;
        e->mz *= 0.75;
    }
    return 1;
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

static void live_tick_slots(
        GmLiveSim *s, GmWorld *w, const int *slots, int slot_count) {
    if (!s || !w) return;
    if ((slots && slot_count < 0) || (!slots && slot_count != 0)) return;

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
    int entity_iterations = slots
        ? slot_count : gm_live_entity_slot_count(s);
    for (int sequence = 0; sequence < entity_iterations; ++sequence) {
        int i = slots ? slots[sequence] : sequence;
        GmLiveEnt *e = gm_live_entity_mut(s, i);
        if (!e) continue;
        if (!e->active) continue;
        if (e->type == 2) {
            fall_tick_entity(s, w, e);
            continue;
        }
        if (e->item <= 0 || e->count <= 0) {
            live_retire_slot(s, i, e);
            continue;
        }
        int dead = 0;
        ++e->ticks_existed; /* Entity.onEntityUpdate, before item-specific work. */
        /* Entity.onEntityUpdate invokes EntityItem.handleWaterMovement through
         * virtual dispatch, so the full 0.25-cube probe runs here and again
         * after the item-specific drag/age phase. */
        (void)live_item_handle_water(e, w, 0);
        if (e->fire > 0) {
            if (e->fire % 20 == 0) {
                e->health = (int)((float)e->health - 1.0F);
                if (e->health <= 0) dead = 1;
            }
            --e->fire;
        }
        if (live_item_in_lava(e, w)) {
            e->health = (int)((float)e->health - 4.0F);
            if (e->health <= 0) dead = 1;
            if (e->fire < 300) e->fire = 300;
        }
        if (e->y < -64.0) dead = 1;
        e->first_update = 0;
        if (e->pickup_delay > 0 && e->pickup_delay != 32767)
            e->pickup_delay--;
        /* Oracle sidecar fixtures intentionally pin an item in place while
         * retaining normal aging, merge, and block-contact callbacks. This
         * is a harness contract rather than EntityItem.noGravity: do not let
         * the general motion/push-out path move those controlled fixtures. */
        if (e->controlled_stationary
                && e->mx == 0.0 && e->my == 0.0 && e->mz == 0.0) {
            if (e->ticks_existed % 25 == 0 && !dead)
                (void)live_item_search_nearby(
                    s, i, slots, slot_count);
            if (!e->active) continue;
            ++e->age;
            (void)live_item_handle_water(e, w, 0);
            if (e->lifespan > 0 && e->age >= e->lifespan) dead = 1;
            if (dead) {
                live_retire_slot(s, i, e);
            }
            continue;
        }
        double prev_x = e->x;
        double prev_y = e->y;
        double prev_z = e->z;
        if (!e->controlled_stationary && !e->no_gravity)
            e->my -= 0.03999999910593033; /* (double)0.04f */
        e->no_clip = live_item_push_out(e, w);
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
        if (e->no_clip) {
            /* Entity.move's noClip branch only offsets the AABB. */
        } else if (partial_surface) {
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
        } else if (e->my < 0.0 && solid_at(w, bx, by - 1, bz)
                && e->y - floor(e->y) < 0.01) {
            e->on_ground = 1;
            e->my = 0.0;
        } else {
            e->on_ground = 0;
        }
        {
            int wet = e->in_water;
            int cactus_contacts = e->no_clip
                ? 0 : live_item_cactus_contacts(e, w);
            while (cactus_contacts-- > 0) {
                e->health = (int)((float)e->health - 1.0F);
                if (e->health <= 0) dead = 1;
            }
            if (!e->no_clip && live_item_in_fire(e, w)) {
                e->health = (int)((float)e->health - 1.0F);
                if (e->health <= 0) dead = 1;
                if (!wet) {
                    ++e->fire;
                    if (e->fire == 0) e->fire = 160;
                }
            } else if (e->fire <= 0) {
                e->fire = -1;
            }
            if (wet && e->fire > 0) {
                /* ENTITY_GENERIC_EXTINGUISH_FIRE pitch consumes two floats. */
                (void)live_item_random_float(e);
                (void)live_item_random_float(e);
                e->fire = -1;
            }
        }
        if ((int)e->x != (int)prev_x
                || (int)e->y != (int)prev_y
                || (int)e->z != (int)prev_z
                || e->ticks_existed % 25 == 0) {
            if (gm_world_block(w,
                    (int)floor(e->x), (int)floor(e->y),
                    (int)floor(e->z)) == BLK_LAVA
                    || gm_world_block(w,
                        (int)floor(e->x), (int)floor(e->y),
                        (int)floor(e->z)) == BLK_FLOWING_LAVA) {
                float ax = (live_item_random_float(e)
                    - live_item_random_float(e)) * 0.2F;
                float az = (live_item_random_float(e)
                    - live_item_random_float(e)) * 0.2F;
                e->my = 0.20000000298023224;
                e->mx = (double)ax;
                e->mz = (double)az;
                (void)live_item_random_float(e); /* burn-sound pitch */
            }
            if (!dead) (void)live_item_search_nearby(
                s, i, slots, slot_count);
        }
        if (!e->active) continue;
        float slip = 0.6f;
        int under = gm_world_block(w, bx, by - 1, bz);
        if (under == BLK_ICE || under == 174 || under == 212) slip = 0.98f;
        float f = e->on_ground ? (slip * 0.98f) : 0.98f;
        e->mx *= (double)f;
        e->mz *= (double)f;
        e->my *= 0.9800000190734863;
        if (e->on_ground) e->my *= -0.5;
        e->age++;
        (void)live_item_handle_water(e, w, 0);
        if (e->lifespan > 0 && e->age >= e->lifespan) dead = 1;
        if (dead) {
            live_retire_slot(s, i, e);
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

void gm_live_tick(GmLiveSim *s, GmWorld *w) {
    live_tick_slots(s, w, NULL, 0);
}

static int live_item_collide_player(
        GmLiveSim *s, int index, PsvPlayer *pl) {
    GmLiveEnt *e;
    int before;
    if (!s || !pl || index < 0
            || index >= gm_live_entity_slot_count(s)) return 0;
    e = gm_live_entity_mut(s, index);
    if (!e) return 0;
    if (!e->active || e->type != 0 || e->pickup_delay > 0) return 0;
    before = e->count;
    {
            ICStack incoming = ic_mk(e->item, e->count, e->meta);
            incoming.repair_cost = e->repair_cost;
            incoming.custom_name = e->custom_name;
            incoming.tag_id = e->tag_id;
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
                e->n_enchants = 0;
                live_retire_slot(s, index, e);
            }
    }
    return before - e->count;
}

int gm_live_item_collide_player_exact(
        GmLiveSim *s, int eid, struct PsvPlayer *pl_) {
    if (!s || !pl_) return 0;
    for (int i = 0; i < gm_live_entity_slot_count(s); ++i) {
        const GmLiveEnt *e = gm_live_entity_ref(s, i);
        if (e && e->active && e->eid == eid)
            return live_item_collide_player(s, i, (PsvPlayer *)pl_);
    }
    return 0;
}

void gm_live_tick_player(GmLiveSim *s, GmWorld *w, struct PsvPlayer *pl_,
                         int player_ox, int player_oz) {
    gm_live_tick_player_ordered(
        s, w, pl_, player_ox, player_oz, NULL, 0);
}

void gm_live_tick_player_ordered(
        GmLiveSim *s, GmWorld *w, struct PsvPlayer *pl_,
        int player_ox, int player_oz, const int *slots, int slot_count) {
    PsvPlayer *pl = (PsvPlayer *)pl_;
    live_tick_slots(s, w, slots, slot_count);
    if (!s || !pl) return;
    if (pl->health <= 0.0f) return;
    double px = pl->ent.posX + (double)player_ox;
    double py = pl->ent.posY;
    double pz = pl->ent.posZ + (double)player_oz;
    int iterations = slots ? slot_count : gm_live_entity_slot_count(s);
    for (int sequence = 0; sequence < iterations; ++sequence) {
        int i = slots ? slots[sequence] : sequence;
        GmLiveEnt *e = gm_live_entity_mut(s, i);
        if (!e) continue;
        if (!e->active || e->type != 0 || e->pickup_delay > 0) continue;
        /* EntityPlayer.onLivingUpdate searches its 0.6 x 1.8 AABB expanded
         * by (1.0, 0.5, 1.0); EntityItem is 0.25 cubed. */
        if (fabs(e->x - px) >= 1.425 || fabs(e->z - pz) >= 1.425
                || e->y >= py + 2.3 || e->y + 0.25 <= py - 0.5)
            continue;
        (void)live_item_collide_player(s, i, pl);
    }
}

int gm_live_fill_views_filtered(const GmLiveSim *s, GmEntityView *out,
                                int max, int suppress_falling) {
    if (!s || !out || max <= 0) return 0;
    int n = 0;
    for (int i = 0; i < gm_live_entity_slot_count(s) && n < max; ++i) {
        const GmLiveEnt *e = gm_live_entity_ref(s, i);
        if (!e) continue;
        if (!e->active) continue;
        if (suppress_falling && e->type == 2) continue;
        if (e->type == 0 && (e->item <= 0 || e->count <= 0)) continue;
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
    for (int i = 0;
            i < gm_live_entity_slot_count(s) && count < capacity; ++i) {
        const GmLiveEnt *e = gm_live_entity_ref(s, i);
        if (!e) continue;
        if (!e->active || e->type != 0) continue;
        out[count++] = live_item_box(e);
    }
    return count;
}

int gm_live_explosion_targets(
        const GmLiveSim *s, GmLiveExplosionTarget *out, int capacity) {
    if (!s || !out || capacity <= 0) return 0;
    int count = 0;
    for (int i = 0;
            i < gm_live_entity_slot_count(s) && count < capacity; ++i) {
        const GmLiveEnt *e = gm_live_entity_ref(s, i);
        if (!e) continue;
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
    if (!s || slot < 0 || slot >= gm_live_entity_slot_count(s))
        return 0;
    e = gm_live_entity_mut(s, slot);
    if (!e || !e->active || e->type != 0)
        return 0;
    /* EntityItem rejects explosion damage for a Nether Star, but Explosion
     * still appends its raw motion after attackEntityFrom returns false. */
    if (e->item != 399)
        e->health = (int)((float)e->health - damage);
    e->mx += impulse_x;
    e->my += impulse_y;
    e->mz += impulse_z;
    if (e->health <= 0) {
        e->n_enchants = 0;
        live_retire_slot(s, slot, e);
        return 0;
    }
    return 1;
}

int gm_live_apply_damage(GmLiveSim *s, int slot, float damage) {
    GmLiveEnt *e;
    if (!s || slot < 0 || slot >= gm_live_entity_slot_count(s)
            || !isfinite(damage) || damage <= 0.0F)
        return 0;
    e = gm_live_entity_mut(s, slot);
    if (!e || !e->active || e->type != 0) return 0;
    e->health = (int)((float)e->health - damage);
    if (e->health <= 0) {
        e->n_enchants = 0;
        live_retire_slot(s, slot, e);
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
    for (int i = 0; i < gm_live_entity_slot_count(s); ++i) {
        const GmLiveEnt *e = gm_live_entity_ref(s, i);
        if (!e) continue;
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
    for (int i = 0; i < gm_live_entity_slot_count(s); ++i) {
        const GmLiveEnt *e = gm_live_entity_ref(s, i);
        if (!e) continue;
        if (e->active && e->age > 0 && !e->on_ground)
            return 1;
        if (e->active && e->age > 5)
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
