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
    GmMobPotionDamageOwner damage_owner;
    const GmMobPotionDamageOwner *owner = NULL;
    EwStore *now, *next;
    char error[256];
    int type, slot = -1, owner_slot = -1, hurt_resistant, on_ground;
    int amplifier, result;
    float health, last_damage, max_health;
    double factor;
    uint64_t target_seed, math_seed;
    if (argc != 11) return 2;
    type = !strcmp(argv[1], "pig") ? EW_TYPE_PIG
        : !strcmp(argv[1], "witch") ? EW_TYPE_WITCH
        : !strcmp(argv[1], "zombie") ? EW_TYPE_ZOMBIE
        : !strcmp(argv[1], "player") ? -1 : 0;
    health = strtof(argv[3], NULL);
    hurt_resistant = atoi(argv[4]);
    last_damage = strtof(argv[5], NULL);
    on_ground = atoi(argv[6]);
    target_seed = strtoull(argv[7], NULL, 10);
    math_seed = strtoull(argv[8], NULL, 10);
    amplifier = atoi(argv[9]);
    factor = strtod(argv[10], NULL);
    max_health = type == EW_TYPE_PIG ? 10.0F
        : type == EW_TYPE_WITCH ? 26.0F : 20.0F;
    if (!type || (strcmp(argv[2], "player")
                && strcmp(argv[2], "cow") && strcmp(argv[2], "none"))
            || health <= 0.0F || health > max_health
            || hurt_resistant < 0 || hurt_resistant > 20
            || last_damage < 0.0F
            || (on_ground != 0 && on_ground != 1)
            || target_seed >= (UINT64_C(1) << 48)
            || math_seed >= (UINT64_C(1) << 48)
            || amplifier < 0 || amplifier > 8
            || factor < 0.0 || factor > 1.0)
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
    runtime.mobs.active_dimension = runtime.dimension;
    if (type == -1) {
        gm_runtime_set_pose_state(
            &runtime, 25.5, 220.0, 24.5, 0.0F, 0.0F,
            0.0, 0.0, 0.0, on_ground, 0.0F);
    } else {
        slot = gm_mobs_spawn(
            &runtime.mobs, type, 25.5, 220.0, 24.5);
        if (slot <= 0) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
    }
    if (!strcmp(argv[2], "cow")) {
        owner_slot = gm_mobs_spawn(
            &runtime.mobs, EW_TYPE_COW, 24.5, 220.0, 24.5);
        if (owner_slot <= 0) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
    }
    now = runtime.mobs.current ? &runtime.mobs.b : &runtime.mobs.a;
    next = runtime.mobs.current ? &runtime.mobs.a : &runtime.mobs.b;
    if (type == -1) {
        runtime.vitals.health = health;
        runtime.player.health = health;
        runtime.server_player.health = health;
        runtime.mobs.player_entity_age = 42;
        runtime.mobs.player_hurt_time = 0;
        runtime.mobs.player_max_hurt_time = 0;
        runtime.mobs.player_hurt_resistant = hurt_resistant;
        runtime.mobs.player_last_damage = last_damage;
        runtime.mobs.player_recently_hit = 0;
        runtime.mobs.player_attacking_player = 0;
        runtime.mobs.player_revenge_present = 0;
        runtime.mobs.player_attacked_yaw = 0.0F;
        runtime.mobs.player_limb_swing_amount = 0.0F;
        runtime.mobs.player_velocity_changed = 0;
        runtime.mobs.player_absorption = 0.0F;
        runtime.mobs.player_resistance_amplifier = -1;
        if (!gm_runtime_set_player_random_seed48(
                &runtime, target_seed)) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
    } else {
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
    }
    runtime.math_random_seed48 = math_seed;
    if (!strcmp(argv[2], "player")) {
        damage_owner = (GmMobPotionDamageOwner){
            runtime.player_entity_id, 1,
            runtime.player.ent.posX + (double)runtime.ox,
            runtime.player.ent.posZ + (double)runtime.oz,
            0
        };
        owner = &damage_owner;
    } else if (owner_slot > 0) {
        damage_owner = (GmMobPotionDamageOwner){
            now->id[owner_slot], 0,
            now->x[owner_slot], now->z[owner_slot], 0
        };
        owner = &damage_owner;
    }
    death_context = (GmMobDeathContext){
        0, &runtime.math_random_seed48, &runtime.next_entity_id
    };
    result = type == -1
        ? gm_runtime_apply_player_instant_potion_indirect(
            &runtime, 7, amplifier, factor, owner)
        : gm_mobs_apply_instant_potion_indirect(
            &runtime.mobs, runtime.world, slot, 7, amplifier, factor,
            owner, &runtime.entities, &death_context);
    if (!result) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    now = runtime.mobs.current ? &runtime.mobs.b : &runtime.mobs.a;
    if (type == -1) {
        printf("{\"ok\":true,\"target_health_bits\":\"%08" PRIx32
               "\",\"target_motion_bits\":[\"%016" PRIx64
               "\",\"%016" PRIx64 "\",\"%016" PRIx64
               "\"],\"target_hurt_time\":%d"
               ",\"target_max_hurt_time\":%d"
               ",\"target_hurt_resistant_time\":%d"
               ",\"target_last_damage_bits\":\"%08" PRIx32
               "\",\"target_recently_hit\":%d"
               ",\"target_attacking_player\":%s"
               ",\"target_revenge_present\":%s"
               ",\"target_revenge_owner\":%s"
               ",\"target_entity_age\":%d"
               ",\"target_limb_swing_amount_bits\":\"%08" PRIx32
               "\",\"target_attacked_yaw_bits\":\"%08" PRIx32
               "\",\"target_velocity_changed\":%s"
               ",\"target_velocity_packet_present\":%s",
               float_bits(runtime.vitals.health),
               double_bits(runtime.server_player.ent.motionX),
               double_bits(runtime.server_player.ent.motionY),
               double_bits(runtime.server_player.ent.motionZ),
               runtime.mobs.player_hurt_time,
               runtime.mobs.player_max_hurt_time,
               runtime.mobs.player_hurt_resistant,
               float_bits(runtime.mobs.player_last_damage),
               runtime.mobs.player_recently_hit,
               runtime.mobs.player_attacking_player ? "true" : "false",
               runtime.mobs.player_revenge_present ? "true" : "false",
               runtime.mobs.player_revenge_present && owner
                   && runtime.mobs.player_revenge_eid == owner->eid
                   && runtime.mobs.player_revenge_is_player
                        == owner->is_player ? "true" : "false",
               runtime.mobs.player_entity_age,
               float_bits(runtime.mobs.player_limb_swing_amount),
               float_bits(runtime.mobs.player_attacked_yaw),
               runtime.mobs.player_velocity_changed ? "true" : "false",
               runtime.mobs.player_velocity_changed ? "true" : "false");
        if (runtime.mobs.player_velocity_changed) {
            double packet_x = runtime.server_player.ent.motionX;
            double packet_y = runtime.server_player.ent.motionY;
            double packet_z = runtime.server_player.ent.motionZ;
            if (packet_x < -3.9) packet_x = -3.9;
            else if (packet_x > 3.9) packet_x = 3.9;
            if (packet_y < -3.9) packet_y = -3.9;
            else if (packet_y > 3.9) packet_y = 3.9;
            if (packet_z < -3.9) packet_z = -3.9;
            else if (packet_z > 3.9) packet_z = 3.9;
            printf(",\"target_velocity_packet_bits\":[\"%016" PRIx64
                   "\",\"%016" PRIx64 "\",\"%016" PRIx64 "\"]",
                   double_bits((double)(int)(packet_x * 8000.0) / 8000.0),
                   double_bits((double)(int)(packet_y * 8000.0) / 8000.0),
                   double_bits((double)(int)(packet_z * 8000.0) / 8000.0));
        }
        printf(",\"target_seed48\":%" PRIu64
               ",\"math_seed48\":%" PRIu64 "}\n",
               runtime.mobs.player_random.seed,
               runtime.math_random_seed48);
        gm_runtime_destroy(&runtime);
        return 0;
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
           ",\"target_revenge_present\":%s"
           ",\"target_revenge_owner\":%s"
           ",\"target_entity_age\":%d"
           ",\"target_limb_swing_amount_bits\":\"%08" PRIx32
           "\",\"target_attacked_yaw_bits\":\"%08" PRIx32
           "\",\"target_velocity_changed\":%s"
           ",\"target_seed48\":%" PRIu64
           ",\"math_seed48\":%" PRIu64 "}\n",
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
           runtime.mobs.hurt_aggro[slot] && owner ? "true" : "false",
           runtime.mobs.entity_age[slot],
           float_bits(runtime.mobs.entity_limb_swing_amount[slot]),
           float_bits(runtime.mobs.entity_attacked_yaw[slot]),
           runtime.mobs.entity_velocity_changed[slot] ? "true" : "false",
           runtime.mobs.entity_random[slot].random.seed,
           runtime.math_random_seed48);
    gm_runtime_destroy(&runtime);
    return 0;
}
