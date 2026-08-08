#include "game/runtime.h"

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_int(const char *text, int *out) {
    char *end = NULL;
    long value = strtol(text, &end, 0);
    if (!text[0] || !end || *end || value < INT_MIN || value > INT_MAX)
        return 0;
    *out = (int)value;
    return 1;
}

static int parse_u48(const char *text, uint64_t *out) {
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 0);
    if (!text[0] || !end || *end || value >= (UINT64_C(1) << 48))
        return 0;
    *out = (uint64_t)value;
    return 1;
}

static int parse_double(const char *text, double *out) {
    char *end = NULL;
    double value = strtod(text, &end);
    if (!text[0] || !end || *end || !isfinite(value)) return 0;
    *out = value;
    return 1;
}

static uint64_t java_lcg_rewind(uint64_t seed, int steps) {
    const uint64_t mask = (UINT64_C(1) << 48) - UINT64_C(1);
    const uint64_t inverse = UINT64_C(246154705703781);
    while (steps-- > 0)
        seed = ((seed - UINT64_C(0xB)) * inverse) & mask;
    return seed;
}

static void write_drops(const GmLiveSim *items) {
    int first = 1;
    putchar('[');
    for (int slot = 0; slot < GM_LIVE_MAX; ++slot) {
        const GmLiveEnt *item = &items->ents[slot];
        if (!item->active || item->type != 0) continue;
        if (!first) putchar(',');
        first = 0;
        printf("{\"eid\":%d,\"x\":%.17g,\"y\":%.17g,"
               "\"z\":%.17g,\"vx\":%.17g,\"vy\":%.17g,"
               "\"vz\":%.17g,\"yaw\":%.9g,\"item\":%d,"
               "\"count\":%d,\"meta\":%d,\"age\":%d,"
               "\"pickup_delay\":%d,\"health\":%d,"
               "\"lifespan\":%d,\"hover_start\":%.9g,"
               "\"on_ground\":%s,\"is_dead\":false}",
            item->eid, item->x, item->y, item->z,
            item->mx, item->my, item->mz, (double)item->yaw,
            item->item, item->count, item->meta, item->age,
            item->pickup_delay, item->health, item->lifespan,
            (double)item->hover_start,
            item->on_ground ? "true" : "false");
    }
    putchar(']');
}

int main(int argc, char **argv) {
    int saddled, do_mob_loot, burning, next_id;
    uint64_t entity_seed_at_death, math_seed_at_death;
    double x, z;
    if (argc != 9 || !parse_int(argv[1], &saddled)
            || !parse_int(argv[2], &do_mob_loot)
            || !parse_int(argv[3], &burning)
            || !parse_u48(argv[4], &entity_seed_at_death)
            || !parse_u48(argv[5], &math_seed_at_death)
            || !parse_int(argv[6], &next_id)
            || !parse_double(argv[7], &x) || !parse_double(argv[8], &z)
            || (saddled != 0 && saddled != 1)
            || (do_mob_loot != 0 && do_mob_loot != 1)
            || (burning != 0 && burning != 1)
            || next_id <= 0 || next_id >= INT_MAX - 3)
        return 2;

    GmMobLive mobs;
    GmLiveSim items;
    const double y = 220.0;
    const int pig_eid = next_id;
    int drop_next_id = next_id + 1;
    uint64_t math_seed = java_lcg_rewind(math_seed_at_death, 2);
    McAABB impact = mc_aabb_make(
        x - 0.5, y, z - 0.5, x + 0.5, y + 1.0, z + 0.5);
    gm_mobs_init(&mobs, 0);
    mobs.active_dimension = 0;
    memset(&items, 0, sizeof items);
    int slot = gm_mobs_spawn_exact(
        &mobs, EW_TYPE_PIG, pig_eid, x, y, z,
        0.0, 0.0, 0.0, 0.0F, 4.0F, 1, 0, 0, 0);
    if (slot < 0
            || !gm_mobs_set_entity_random_state(
                &mobs, pig_eid,
                java_lcg_rewind(entity_seed_at_death, 4), 0, 0.0)
            || !gm_mobs_set_pig_saddled(&mobs, pig_eid, saddled)
            || !gm_mobs_set_entity_fire_ticks(
                &mobs, pig_eid, burning ? 100 : -1)
            || gm_mobs_falling_anvil_damage_controlled_passives(
                &mobs, 0, &impact, 4.0F, &math_seed, &items,
                &drop_next_id, do_mob_loot) != 1)
        return 1;

    const EwStore *store = mobs.current ? &mobs.b : &mobs.a;
    printf("{\"ok\":true,\"saddled\":%s,\"do_mob_loot\":%s,"
           "\"burning\":%s,\"pig_eid\":%d,\"pig_x\":%.17g,"
           "\"pig_y\":%.17g,\"pig_z\":%.17g,\"health\":%.9g,"
           "\"living_dead\":%s,\"entity_is_dead\":false,"
           "\"entity_seed48\":%" PRIu64 ",\"drops\":",
        saddled ? "true" : "false",
        do_mob_loot ? "true" : "false",
        burning ? "true" : "false", pig_eid,
        store->x[slot], store->y[slot], store->z[slot],
        (double)store->health[slot],
        mobs.entity_dead[slot] ? "true" : "false",
        (uint64_t)mobs.entity_random[slot].random.seed);
    write_drops(&items);
    printf(",\"math_seed48\":%" PRIu64
           ",\"next_entity_id\":%d}\n",
        math_seed, drop_next_id);
    return 0;
}
