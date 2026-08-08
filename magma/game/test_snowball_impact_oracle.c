#include "game/runtime.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int main(int argc, char **argv) {
    GmConfig config;
    GmRuntime runtime;
    GmMobDeathContext death_context;
    EwStore *now, *next;
    char error[256];
    int type, slot, hurt_resistant, on_ground, result;
    int living_owner;
    const char *target_name;
    char revenge_fragment[64] = "";
    float health, last_damage, damage;
    uint64_t target_seed, math_seed;
    if (argc != 8) return 2;
    living_owner = !strncmp(argv[1], "snowman_", 8);
    target_name = living_owner ? argv[1] + 8 : argv[1];
    type = !strcmp(target_name, "pig") ? EW_TYPE_PIG
        : !strcmp(target_name, "blaze") ? EW_TYPE_BLAZE
        : !strcmp(target_name, "zombie") ? EW_TYPE_ZOMBIE
        : !strcmp(target_name, "player") ? -1 : 0;
    health = strtof(argv[2], NULL);
    hurt_resistant = atoi(argv[3]);
    last_damage = strtof(argv[4], NULL);
    on_ground = atoi(argv[5]);
    target_seed = strtoull(argv[6], NULL, 10);
    math_seed = strtoull(argv[7], NULL, 10);
    if (!type || health <= 0.0F
            || health > (type == EW_TYPE_PIG ? 10.0F : 20.0F)
            || (living_owner && type == -1)
            || hurt_resistant < 0 || hurt_resistant > 20
            || last_damage < 0.0F
            || (on_ground != 0 && on_ground != 1)
            || target_seed >= (UINT64_C(1) << 48)
            || math_seed >= (UINT64_C(1) << 48))
        return 2;

    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.seed = 0;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    gm_runtime_set_pose(&runtime, 24.5, 220.0, 24.5, 0.0F, 0.0F);
    runtime.player.ent.motionX = 0.0;
    runtime.player.ent.motionY = 0.0;
    runtime.player.ent.motionZ = 0.0;
    runtime.player.ent.onGround = 1;
    runtime.math_random_seed48 = math_seed;
    runtime.mobs.active_dimension = runtime.dimension;
    if (type == -1) {
        gm_runtime_set_pose(
            &runtime, 25.5, 220.0, 24.5, 0.0F, 0.0F);
        runtime.player.ent.motionX = 0.0;
        runtime.player.ent.motionY = 0.0;
        runtime.player.ent.motionZ = 0.0;
        runtime.player.ent.onGround = on_ground;
        runtime.vitals.health = health;
        runtime.player.health = health;
        runtime.server_player.health = health;
        runtime.mobs.player_entity_age = 42;
        runtime.mobs.player_hurt_time = 0;
        runtime.mobs.player_hurt_resistant = hurt_resistant;
        runtime.mobs.player_last_damage = last_damage;
        if (!gm_runtime_set_player_random_seed48(
                &runtime, target_seed)) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
        result = gm_runtime_player_throwable_self_hit_now(&runtime);
        if (!result) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
        printf("{\"ok\":true,\"target_health_bits\":\"%08" PRIx32
               "\",\"target_motion_bits\":[\"%016" PRIx64
               "\",\"%016" PRIx64 "\",\"%016" PRIx64
               "\"],\"target_hurt_time\":%d"
               ",\"target_max_hurt_time\":%d"
               ",\"target_hurt_resistant_time\":%d"
               ",\"target_last_damage_bits\":\"%08" PRIx32
               "\",\"target_recently_hit\":%d"
               ",\"target_attacking_player\":%s"
               ",\"target_revenge_player\":%s"
               ",\"target_entity_age\":%d"
               ",\"target_limb_swing_amount_bits\":\"%08" PRIx32
               "\",\"target_attacked_yaw_bits\":\"%08" PRIx32
               "\",\"target_velocity_changed\":%s"
               ",\"target_seed48\":%" PRIu64
               ",\"math_seed48\":%" PRIu64
               ",\"snowball_dead\":true}\n",
               float_bits(runtime.vitals.health),
               double_bits(runtime.player.ent.motionX),
               double_bits(runtime.player.ent.motionY),
               double_bits(runtime.player.ent.motionZ),
               runtime.mobs.player_hurt_time,
               0,
               runtime.mobs.player_hurt_resistant,
               float_bits(runtime.mobs.player_last_damage),
               0,
               "false",
               "false",
               runtime.mobs.player_entity_age,
               float_bits(0.0F),
               float_bits(0.0F),
               "false",
               runtime.mobs.player_random.seed,
               runtime.math_random_seed48);
        gm_runtime_destroy(&runtime);
        return 0;
    }
    if (living_owner) {
        if (gm_mobs_spawn_exact(
                    &runtime.mobs, EW_TYPE_SNOWMAN, 100,
                    24.5, 220.0, 24.5, 0.0, 0.0, 0.0,
                    0.0F, 4.0F, 0, 0, 0, 0) <= 0) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
        slot = gm_mobs_spawn_exact(
            &runtime.mobs, type, 101,
            25.5, 220.0, 24.5, 0.0, 0.0, 0.0,
            0.0F, type == EW_TYPE_PIG ? 10.0F : 20.0F,
            0, 0, 0, 0);
    } else {
        slot = gm_mobs_spawn(
            &runtime.mobs, type, 25.5, 220.0, 24.5);
    }
    if (slot <= 0) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    now = runtime.mobs.current ? &runtime.mobs.b : &runtime.mobs.a;
    next = runtime.mobs.current ? &runtime.mobs.a : &runtime.mobs.b;
    now->health[slot] = health;
    now->vx[slot] = now->vy[slot] = now->vz[slot] = 0.0;
    now->yaw[slot] = 0.0F;
    now->on_ground[slot] = (unsigned char)on_ground;
    *next = *now;
    runtime.mobs.entity_age[slot] = 42;
    runtime.mobs.entity_hurt_time[slot] = 0;
    runtime.mobs.entity_max_hurt_time[slot] = 0;
    runtime.mobs.entity_hurt_resistant[slot] = hurt_resistant;
    runtime.mobs.entity_last_damage[slot] = last_damage;
    runtime.mobs.entity_recently_hit[slot] = 0;
    runtime.mobs.entity_attacking_player[slot] = 0;
    runtime.mobs.entity_attacked_yaw[slot] = 0.0F;
    runtime.mobs.entity_limb_swing_amount[slot] = 0.0F;
    runtime.mobs.entity_velocity_changed[slot] = 0;
    runtime.mobs.hurt_aggro[slot] = 0;
    if (!gm_mobs_set_entity_random_state(
            &runtime.mobs, now->id[slot], target_seed, 0, 0.0)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    death_context = (GmMobDeathContext){
        runtime.do_mob_loot,
        &runtime.math_random_seed48,
        &runtime.next_entity_id
    };
    damage = type == EW_TYPE_BLAZE ? 3.0F : 0.0F;
    result = living_owner
        ? gm_mobs_entity_throwable_hit(
            &runtime.mobs, runtime.world, slot, 100,
            24.5, 24.5, damage, &runtime.entities, &death_context)
        : gm_mobs_player_throwable_hit(
            &runtime.mobs, runtime.world, slot,
            runtime.player.ent.posX + (double)runtime.ox,
            runtime.player.ent.posZ + (double)runtime.oz,
            damage, &runtime.entities, &death_context);
    if (!result) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    now = runtime.mobs.current ? &runtime.mobs.b : &runtime.mobs.a;
    if (living_owner)
        snprintf(revenge_fragment, sizeof revenge_fragment,
                 ",\"target_revenge_eid\":%d",
                 runtime.mobs.entity_revenge_eid[slot]);
    printf("{\"ok\":true,\"target_health_bits\":\"%08" PRIx32
           "\",\"target_motion_bits\":[\"%016" PRIx64
           "\",\"%016" PRIx64 "\",\"%016" PRIx64
           "\"],\"target_hurt_time\":%d"
           ",\"target_max_hurt_time\":%d"
           ",\"target_hurt_resistant_time\":%d"
           ",\"target_last_damage_bits\":\"%08" PRIx32
           "\",\"target_recently_hit\":%d"
           ",\"target_attacking_player\":%s"
           ",\"target_revenge_player\":%s"
           "%s"
           ",\"target_entity_age\":%d"
           ",\"target_limb_swing_amount_bits\":\"%08" PRIx32
           "\",\"target_attacked_yaw_bits\":\"%08" PRIx32
           "\",\"target_velocity_changed\":%s"
           ",\"target_seed48\":%" PRIu64
           ",\"math_seed48\":%" PRIu64
           ",\"snowball_dead\":true}\n",
           float_bits(now->health[slot]),
           double_bits(now->vx[slot]), double_bits(now->vy[slot]),
           double_bits(now->vz[slot]),
           runtime.mobs.entity_hurt_time[slot],
           runtime.mobs.entity_max_hurt_time[slot],
           runtime.mobs.entity_hurt_resistant[slot],
           float_bits(runtime.mobs.entity_last_damage[slot]),
           runtime.mobs.entity_recently_hit[slot],
           runtime.mobs.entity_attacking_player[slot] ? "true" : "false",
           runtime.mobs.hurt_aggro[slot] ? "true" : "false",
           revenge_fragment,
           runtime.mobs.entity_age[slot],
           float_bits(runtime.mobs.entity_limb_swing_amount[slot]),
           float_bits(runtime.mobs.entity_attacked_yaw[slot]),
           runtime.mobs.entity_velocity_changed[slot] ? "true" : "false",
           runtime.mobs.entity_random[slot].random.seed,
           runtime.math_random_seed48);
    gm_runtime_destroy(&runtime);
    return 0;
}
