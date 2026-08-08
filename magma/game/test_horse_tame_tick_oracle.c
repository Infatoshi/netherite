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

static int parse_u64(const char *text, uint64_t *out) {
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 0);
    if (!text[0] || !end || *end) return 0;
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

static void print_vec3(double x, double y, double z) {
    printf("[\"%016" PRIx64 "\",\"%016" PRIx64
           "\",\"%016" PRIx64 "\"]",
        double_bits(x), double_bits(y), double_bits(z));
}

static int horse_type(const char *kind) {
    if (!strcmp(kind, "horse")) return GM_MOB_HORSE;
    if (!strcmp(kind, "donkey")) return GM_MOB_DONKEY;
    if (!strcmp(kind, "mule")) return GM_MOB_MULE;
    return 0;
}

static const char *sound_name(int data) {
    if (data == GM_MOB_SOUND_HORSE_ANGRY)
        return "minecraft:entity.horse.angry";
    if (data == GM_MOB_SOUND_DONKEY_ANGRY)
        return "minecraft:entity.donkey.angry";
    return "unknown";
}

int main(int argc, char **argv) {
    const char *kind;
    int type, temper, next_eid;
    double x, y, z;
    uint64_t seed48, owner_most, owner_least;
    if (argc != 10 || !(type = horse_type(kind = argv[1]))
            || !parse_int(argv[2], &temper)
            || !parse_u64(argv[3], &seed48)
            || !parse_double(argv[4], &x)
            || !parse_double(argv[5], &y)
            || !parse_double(argv[6], &z)
            || !parse_int(argv[7], &next_eid)
            || !parse_u64(argv[8], &owner_most)
            || !parse_u64(argv[9], &owner_least)
            || temper < 0 || temper > 100 || seed48 >= (UINT64_C(1) << 48)
            || y < 32.0 || y > 250.0 || next_eid <= 0
            || next_eid >= INT_MAX - 1) {
        fprintf(stderr, "usage: %s KIND TEMPER SEED48 X Y Z NEXT_EID "
            "OWNER_MOST OWNER_LEAST\n", argv[0]);
        return 2;
    }

    GmConfig config;
    GmRuntime runtime;
    GmHorseState state;
    char error[256];
    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "runtime init: %s\n", error);
        return 1;
    }
    gm_runtime_set_pose(&runtime, x, y, z, 0.0F, 0.0F);
    for (int dz = -7; dz <= 7; ++dz)
        for (int dx = -7; dx <= 7; ++dx)
            gm_world_set_block(runtime.world,
                mc_floor(x) + dx, mc_floor(y) - 1,
                mc_floor(z) + dz, 1);
    runtime.player_uuid_most = owner_most;
    runtime.player_uuid_least = owner_least;
    gm_mobs_set_represented_player_uuid(
        &runtime.mobs, owner_most, owner_least);
    if (!gm_runtime_spawn_horse_fixture(
            &runtime, type, next_eid,
            x, y, z, 0.0, 0.0, 0.0, 0.0F, 20.0F, 0,
            20.0, type == GM_MOB_HORSE ? 0.225 : 0.175,
            type == GM_MOB_HORSE ? 0.7 : 0.5,
            0, 0, temper, 0, 0, 0, 0, 0, 0, 0, 0)
            || !gm_runtime_set_mob_no_ai(&runtime, next_eid, 0)
            || !gm_mobs_horse_mount(&runtime.mobs, next_eid)
            || !gm_mobs_set_entity_random_state(
                &runtime.mobs, next_eid, seed48, 0, 0.0)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    int slot = gm_mobs_find_slot_by_eid(&runtime.mobs, next_eid);
    if (slot <= 0) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    runtime.mobs.a.on_ground[slot] = 0;
    runtime.mobs.b.on_ground[slot] = 0;
    runtime.mobs.horse_crazy_active[slot] = 1;
    runtime.mobs.sheep_ai_tick_count[slot] = 1;
    runtime.mobs.a.path_tx[slot] = x + 2.0;
    runtime.mobs.a.path_ty[slot] = y;
    runtime.mobs.a.path_tz[slot] = z;
    runtime.mobs.a.path_len[slot] = 1;
    runtime.mobs.b.path_tx[slot] = x + 2.0;
    runtime.mobs.b.path_ty[slot] = y;
    runtime.mobs.b.path_tz[slot] = z;
    runtime.mobs.b.path_len[slot] = 1;
    runtime.mobs.passive_nav_speed[slot] = 1.2;
    runtime.mobs_enabled = 1;
    runtime.controlled_mobs_enabled = 0;
    runtime.mobs.natural_spawning_enabled = 0;
    runtime.next_entity_id = next_eid + 1;
    gm_mobs_tick(
        &runtime.mobs, runtime.world,
        (const struct Chunk *)runtime.window,
        (const struct McSinTable *)&runtime.sin_table,
        (struct PsvPlayer *)&runtime.player,
        (struct PvStats *)&runtime.vitals,
        runtime.ox, runtime.oz, runtime.dimension,
        runtime.clock.world_time, &runtime.clock,
        runtime.mob_griefing, &runtime.world_random_seed48,
        &runtime.math_random_seed48, &runtime.next_entity_id,
        runtime.do_mob_loot, &runtime.entities, 0.0F, 0.0F);

    const EwStore *s = runtime.mobs.current
        ? &runtime.mobs.b : &runtime.mobs.a;
    if (!gm_mobs_get_horse_state(&runtime.mobs, next_eid, &state)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    printf("{\"ok\":true,\"horse_kind\":\"%s\",\"eid\":%d,"
           "\"temper\":%d,\"tame\":%s,\"rearing\":%s,"
           "\"ridden\":%s,\"player_riding\":%s,"
           "\"owner_present\":%s,\"owner_matches_player\":%s",
        kind, next_eid, state.temper,
        (state.status & GM_HORSE_TAME) ? "true" : "false",
        (state.status & GM_HORSE_REARING) ? "true" : "false",
        state.ridden ? "true" : "false",
        state.ridden ? "true" : "false",
        state.owner_present ? "true" : "false",
        state.owner_present && state.owner_uuid_most == owner_most
            && state.owner_uuid_least == owner_least ? "true" : "false");
    if (state.owner_present)
        printf(",\"owner_most\":%" PRId64 ",\"owner_least\":%" PRId64,
            (int64_t)state.owner_uuid_most, (int64_t)state.owner_uuid_least);
    printf(",\"entity_seed48\":%" PRIu64
           ",\"entity_have_next_gaussian\":%s,"
           "\"entity_next_gaussian_bits\":\"%016" PRIx64 "\","
           "\"ticks_existed\":%d,\"entity_age\":%d,"
           "\"living_sound_time\":%d,\"task_tick_count\":%u,"
           "\"tail_counter\":%d,\"path_present\":%s,"
           "\"on_ground\":%s,\"fall_distance_bits\":\"%08" PRIx32
           "\",\"yaw_bits\":\"%08" PRIx32
           "\",\"pitch_bits\":\"%08" PRIx32 "\",\"position_bits\":",
        (uint64_t)runtime.mobs.entity_random[slot].random.seed,
        runtime.mobs.entity_random[slot].have_next_next_gaussian
            ? "true" : "false",
        double_bits(runtime.mobs.entity_random[slot].next_next_gaussian),
        runtime.mobs.entity_ticks_existed[slot],
        runtime.mobs.entity_age[slot],
        runtime.mobs.entity_living_sound_time[slot],
        runtime.mobs.sheep_ai_tick_count[slot],
        runtime.mobs.horse_tail_counter[slot],
        s->path_len[slot] ? "true" : "false",
        s->on_ground[slot] ? "true" : "false",
        float_bits(runtime.mobs.entity_fall_distance[slot]),
        float_bits(s->yaw[slot]), 0u);
    print_vec3(s->x[slot], s->y[slot], s->z[slot]);
    printf(",\"motion_bits\":");
    print_vec3(s->vx[slot], s->vy[slot], s->vz[slot]);
    printf(",\"last_tick_position_bits\":");
    print_vec3(
        runtime.mobs.entity_last_tick_x[slot],
        runtime.mobs.entity_last_tick_y[slot],
        runtime.mobs.entity_last_tick_z[slot]);
    printf(",\"previous_position_bits\":");
    print_vec3(
        runtime.mobs.entity_prev_x[slot], runtime.mobs.entity_prev_y[slot],
        runtime.mobs.entity_prev_z[slot]);
    printf(",\"player_position_bits\":");
    print_vec3(
        runtime.player.ent.posX + runtime.ox, runtime.player.ent.posY,
        runtime.player.ent.posZ + runtime.oz);
    printf(",\"player_motion_bits\":");
    print_vec3(
        runtime.player.ent.motionX, runtime.player.ent.motionY,
        runtime.player.ent.motionZ);
    printf(",\"update_order\":[%d", next_eid);
    if (state.ridden) printf(",0");
    printf("],\"events\":[");
    for (int index = 0; index < gm_mobs_event_count(&runtime.mobs); ++index) {
        GmMobEvent event;
        if (!gm_mobs_event_get(&runtime.mobs, index, &event)) continue;
        if (index) putchar(',');
        if (event.kind == GM_MOB_EVENT_ENTITY_STATUS) {
            printf("{\"eid\":%d,\"kind\":\"status\",\"status\":%d}",
                event.eid, event.data);
        } else if (event.kind == GM_MOB_EVENT_SOUND) {
            printf("{\"category\":\"neutral\",\"eid\":%d,"
                   "\"kind\":\"sound\",\"pitch_bits\":\"%08" PRIx32 "\","
                   "\"sound\":\"%s\",\"volume_bits\":\"%08" PRIx32 "\","
                   "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g}",
                event.eid, float_bits(event.pitch), sound_name(event.data),
                float_bits(event.volume),
                event.x, event.y, event.z);
        }
    }
    printf("],\"next_entity_id\":%d}\n", runtime.next_entity_id);
    gm_runtime_destroy(&runtime);
    return 0;
}
