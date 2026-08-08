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

static uint64_t double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static const EwStore *store(const GmMobLive *m) {
    return m->current ? &m->b : &m->a;
}

static EwStore *mutable_store(GmMobLive *m) {
    return m->current ? &m->b : &m->a;
}

static EwStore *mutable_other_store(GmMobLive *m) {
    return m->current ? &m->a : &m->b;
}

static void print_vec3(double x, double y, double z) {
    printf("[\"%016" PRIx64 "\",\"%016" PRIx64
           "\",\"%016" PRIx64 "\"]",
        double_bits(x), double_bits(y), double_bits(z));
}

static void print_items(const GmLiveSim *items) {
    int first = 1;
    putchar('[');
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        const GmLiveEnt *item = &items->ents[i];
        if (!item->active || item->type != 0) continue;
        if (!first) putchar(',');
        first = 0;
        printf("{\"eid\":%d,\"item\":%d,\"count\":%d,"
               "\"meta\":%d,\"age\":%d,\"pickup_delay\":%d,"
               "\"health\":%d,\"lifespan\":%d,"
               "\"on_ground\":%s,\"is_dead\":false,"
               "\"position_bits\":",
            item->eid, item->item, item->count, item->meta,
            item->age, item->pickup_delay, item->health, item->lifespan,
            item->on_ground ? "true" : "false");
        print_vec3(item->x, item->y, item->z);
        printf(",\"motion_bits\":");
        print_vec3(item->mx, item->my, item->mz);
        printf(",\"yaw_bits\":\"%08" PRIx32
               "\",\"hover_start_bits\":\"%08" PRIx32 "\"}",
            float_bits(item->yaw), float_bits(item->hover_start));
    }
    putchar(']');
}

static void print_update_order(
        const GmRuntime *runtime, int pig_eid, int player_eid, int tick) {
    if (tick == 0) {
        fputs("[]", stdout);
        return;
    }
    printf("[%d,%d", pig_eid, player_eid);
    for (int i = 0; i < GM_LIVE_MAX; ++i)
        if (runtime->entities.ents[i].active
                && runtime->entities.ents[i].type == 0)
            printf(",%d", runtime->entities.ents[i].eid);
    putchar(']');
}

static void print_snapshot(
        const GmRuntime *runtime, int slot, int pig_eid,
        int player_eid, int tick) {
    const EwStore *s = store(&runtime->mobs);
    const PsvPlayer *player = &runtime->player;
    const double pig_half = (double)(0.9F / 2.0F);
    const double pig_height = (double)0.9F;
    printf("{\"tick\":%d,\"update_order\":", tick);
    print_update_order(runtime, pig_eid, player_eid, tick);
    printf(",\"player_position_bits\":");
    print_vec3(
        player->ent.posX + runtime->ox, player->ent.posY,
        player->ent.posZ + runtime->oz);
    printf(",\"player_motion_bits\":");
    print_vec3(
        player->ent.motionX, player->ent.motionY, player->ent.motionZ);
    printf(",\"player_yaw_bits\":\"%08" PRIx32
           "\",\"player_pitch_bits\":\"%08" PRIx32
           "\",\"player_on_ground\":%s,"
           "\"player_fall_distance_bits\":\"%08" PRIx32
           "\",\"player_riding_eid\":%d,"
           "\"player_aabb_min_bits\":",
        float_bits(player->yaw), float_bits(player->pitch),
        player->ent.onGround ? "true" : "false",
        float_bits(player->fall_distance),
        runtime->mobs.pig_ride == slot ? pig_eid : -1);
    print_vec3(
        player->ent.box.minX + runtime->ox, player->ent.box.minY,
        player->ent.box.minZ + runtime->oz);
    printf(",\"player_aabb_max_bits\":");
    print_vec3(
        player->ent.box.maxX + runtime->ox, player->ent.box.maxY,
        player->ent.box.maxZ + runtime->oz);
    printf(",\"pig_death_time\":%d,\"pig_living_dead\":%s,"
           "\"pig_entity_is_dead\":false,\"pig_loaded\":%s,"
           "\"pig_health_bits\":\"%08" PRIx32
           "\",\"pig_hurt_time\":%d,"
           "\"pig_hurt_resistant_time\":%d,"
           "\"pig_recently_hit\":%d,"
           "\"pig_attacking_player\":%s,"
           "\"pig_saddled\":%s,\"pig_on_ground\":%s,"
           "\"pig_yaw_bits\":\"%08" PRIx32
           "\",\"pig_pitch_bits\":\"%08" PRIx32
           "\",\"pig_passenger_eids\":",
        runtime->mobs.entity_death_time[slot],
        runtime->mobs.entity_dead[slot] ? "true" : "false",
        s->alive[slot] ? "true" : "false", float_bits(s->health[slot]),
        runtime->mobs.entity_hurt_time[slot],
        runtime->mobs.entity_hurt_resistant[slot],
        runtime->mobs.entity_recently_hit[slot],
        runtime->mobs.entity_attacking_player[slot] ? "true" : "false",
        runtime->mobs.pig_saddled[slot] ? "true" : "false",
        s->on_ground[slot] ? "true" : "false",
        float_bits(s->yaw[slot]), float_bits(runtime->mobs.pig_pitch[slot]));
    if (runtime->mobs.pig_ride == slot) printf("[%d]", player_eid);
    else fputs("[]", stdout);
    printf(",\"pig_position_bits\":");
    print_vec3(s->x[slot], s->y[slot], s->z[slot]);
    printf(",\"pig_motion_bits\":");
    print_vec3(s->vx[slot], s->vy[slot], s->vz[slot]);
    printf(",\"pig_aabb_min_bits\":");
    print_vec3(
        s->x[slot] - pig_half, s->y[slot], s->z[slot] - pig_half);
    printf(",\"pig_aabb_max_bits\":");
    print_vec3(
        s->x[slot] + pig_half, s->y[slot] + pig_height,
        s->z[slot] + pig_half);
    printf(",\"items\":");
    print_items(&runtime->entities);
    printf(",\"pig_rng_seed48\":%" PRIu64
           ",\"world_seed48\":%" PRIu64
           ",\"math_seed48\":%" PRIu64
           ",\"next_entity_id\":%d}",
        (uint64_t)runtime->mobs.entity_random[slot].random.seed,
        runtime->world_random_seed48, runtime->math_random_seed48,
        runtime->next_entity_id);
}

static void print_events(const GmMobLive *m) {
    putchar('[');
    for (int i = 0; i < gm_mobs_event_count(m); ++i) {
        GmMobEvent event;
        if (!gm_mobs_event_get(m, i, &event)) continue;
        if (i) putchar(',');
        printf("{\"kind\":%d,\"eid\":%d,\"data\":%d,"
               "\"position_bits\":",
            event.kind, event.eid, event.data);
        print_vec3(event.x, event.y, event.z);
        printf(",\"volume_bits\":\"%08" PRIx32
               "\",\"pitch_bits\":\"%08" PRIx32 "\"}",
            float_bits(event.volume), float_bits(event.pitch));
    }
    putchar(']');
}

int main(int argc, char **argv) {
    int do_mob_loot, burning, pig_eid, player_eid;
    double pig_x, pig_y, pig_z, yaw_d;
    uint64_t pig_seed48, world_seed48, math_seed48;
    if (argc != 12
            || !parse_int(argv[1], &do_mob_loot)
            || !parse_int(argv[2], &burning)
            || !parse_double(argv[3], &yaw_d)
            || !parse_u48(argv[4], &pig_seed48)
            || !parse_u48(argv[5], &world_seed48)
            || !parse_u48(argv[6], &math_seed48)
            || !parse_int(argv[7], &pig_eid)
            || !parse_int(argv[8], &player_eid)
            || !parse_double(argv[9], &pig_x)
            || !parse_double(argv[10], &pig_y)
            || !parse_double(argv[11], &pig_z)
            || (do_mob_loot != 0 && do_mob_loot != 1)
            || (burning != 0 && burning != 1)
            || (yaw_d != 0.0 && yaw_d != 90.0)
            || pig_eid <= 0 || pig_eid >= INT_MAX - 3
            || player_eid <= 0)
        return 2;

    GmConfig config;
    GmRuntime runtime;
    char error[256];
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "runtime init: %s\n", error);
        return 1;
    }
    int base_x = mc_floor(pig_x);
    int base_y = mc_floor(pig_y);
    int base_z = mc_floor(pig_z);
    for (int x = base_x - 4; x <= base_x + 4; ++x)
        for (int z = base_z - 4; z <= base_z + 4; ++z)
            for (int y = base_y - 1; y <= base_y + 4; ++y)
                gm_world_set_block(runtime.world, x, y, z,
                    y == base_y - 1 ? 1 : 0);

    gm_mobs_init(&runtime.mobs, 0);
    memset(&runtime.entities, 0, sizeof runtime.entities);
    runtime.mobs.active_dimension = runtime.dimension;
    runtime.mobs.tick = 1;
    runtime.mobs.next_id = pig_eid;
    runtime.world_random_seed48 = world_seed48;
    runtime.math_random_seed48 = math_seed48;
    runtime.next_entity_id = pig_eid + 1;
    int slot = gm_mobs_spawn(
        &runtime.mobs, EW_TYPE_PIG, pig_x, pig_y, pig_z);
    if (slot < 0 || store(&runtime.mobs)->id[slot] != pig_eid) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    EwStore *now = mutable_store(&runtime.mobs);
    EwStore *other = mutable_other_store(&runtime.mobs);
    now->vx[slot] = other->vx[slot] = 0.125D;
    now->vy[slot] = other->vy[slot] = -0.25D;
    now->vz[slot] = other->vz[slot] = 0.375D;
    now->yaw[slot] = other->yaw[slot] = (float)yaw_d;
    now->on_ground[slot] = other->on_ground[slot] = 0;
    double mounted_y = pig_y + (double)0.9F * 0.75D - 0.35D;
    gm_runtime_set_pose(
        &runtime, pig_x + 1.0D, mounted_y, pig_z,
        (float)yaw_d, 0.0F);
    runtime.player.ent.motionX = 0.125D;
    runtime.player.ent.motionY = -0.25D;
    runtime.player.ent.motionZ = 0.375D;
    runtime.player.ent.onGround = 0;
    runtime.player.fall_distance = 2.5F;
    runtime.player.inv.current_item = 0;
    for (int i = 0; i < 36; ++i)
        isr_set_stack(&runtime.player.inv, i, ic_mk(1, 64, 0));
    isr_set_stack(
        &runtime.player.inv, ISR_OFFHAND_SLOT, ic_mk(1, 64, 0));
    if (!gm_mobs_set_pig_saddled(&runtime.mobs, pig_eid, 1)
            || !gm_mobs_pig_mount(&runtime.mobs, pig_eid)
            || !gm_mobs_set_entity_random_state(
                &runtime.mobs, pig_eid, pig_seed48, 0, 0.0)
            || (burning && !gm_mobs_set_entity_fire_ticks(
                &runtime.mobs, pig_eid, 100))) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    GmMobDeathContext death_context = {
        do_mob_loot, &runtime.math_random_seed48,
        &runtime.next_entity_id
    };
    if (gm_mobs_player_damage_pig_exact(
            &runtime.mobs, pig_eid, pig_x + 1.0D, pig_z,
            1000.0F, &runtime.entities, &death_context) != 2) {
        gm_runtime_destroy(&runtime);
        return 1;
    }

    printf("{\"ok\":true,\"do_mob_loot\":%s,\"burning\":%s,"
           "\"pig_eid\":%d,\"player_eid\":%d,\"events\":",
        do_mob_loot ? "true" : "false",
        burning ? "true" : "false", pig_eid, player_eid);
    print_events(&runtime.mobs);
    printf(",\"ticks\":[");
    print_snapshot(&runtime, slot, pig_eid, player_eid, 0);
    for (int tick = 1; tick <= 19; ++tick) {
        gm_mobs_tick(
            &runtime.mobs, runtime.world, (const struct Chunk *)runtime.window,
            (const struct McSinTable *)&runtime.sin_table,
            (struct PsvPlayer *)&runtime.player,
            (struct PvStats *)&runtime.vitals,
            runtime.ox, runtime.oz, runtime.dimension,
            runtime.clock.world_time, runtime.mob_griefing,
            &runtime.world_random_seed48, &runtime.math_random_seed48,
            &runtime.next_entity_id, do_mob_loot, &runtime.entities,
            0.0F, 0.0F);
        gm_live_tick_player(
            &runtime.entities, runtime.world,
            (struct PsvPlayer *)&runtime.player,
            runtime.ox, runtime.oz);
        if (tick == 1 || tick == 19) {
            putchar(',');
            print_snapshot(&runtime, slot, pig_eid, player_eid, tick);
        }
    }
    printf("]}\n");
    gm_runtime_destroy(&runtime);
    return 0;
}
