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
    if (argc != 17) return 2;
    int target_type = atoi(argv[1]);
    float health = strtof(argv[2], NULL);
    int hurt_resistant = atoi(argv[3]);
    float last_damage = strtof(argv[4], NULL);
    int target_fire = atoi(argv[5]);
    int on_ground = atoi(argv[6]);
    uint64_t target_seed = strtoull(argv[7], NULL, 10);
    uint64_t arrow_seed = strtoull(argv[8], NULL, 10);
    double vx = strtod(argv[9], NULL);
    double vy = strtod(argv[10], NULL);
    double vz = strtod(argv[11], NULL);
    double arrow_damage = strtod(argv[12], NULL);
    int knockback = atoi(argv[13]);
    int critical = atoi(argv[14]);
    int arrow_fire = atoi(argv[15]);
    int player_owned = atoi(argv[16]);
    if ((target_type != 0 && target_type != 1)
            || health <= 0.0F || health > 20.0F
            || hurt_resistant < 0 || hurt_resistant > 20
            || last_damage < 0.0F || target_fire < -1
            || target_fire > 32767 || (on_ground != 0 && on_ground != 1)
            || target_seed >= (UINT64_C(1) << 48)
            || arrow_seed >= (UINT64_C(1) << 48)
            || arrow_damage <= 0.0 || knockback < 0 || knockback > 32
            || (critical != 0 && critical != 1)
            || arrow_fire < -1 || arrow_fire > 32767
            || (player_owned != 0 && player_owned != 1))
        return 2;

    GmConfig config;
    GmRuntime runtime;
    char error[256];
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
    runtime.player.inv.current_item = 0;
    isr_set_stack(&runtime.player.inv, 0, ic_empty());

    int type = target_type == 0 ? EW_TYPE_PIG : EW_TYPE_ZOMBIE;
    int slot = gm_mobs_spawn(
        &runtime.mobs, type, 25.5, 220.0, 24.5);
    if (slot <= 0) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    EwStore *now = runtime.mobs.current
        ? &runtime.mobs.b : &runtime.mobs.a;
    EwStore *next = runtime.mobs.current
        ? &runtime.mobs.a : &runtime.mobs.b;
    now->health[slot] = health;
    now->vx[slot] = now->vy[slot] = now->vz[slot] = 0.0;
    now->on_ground[slot] = (unsigned char)on_ground;
    *next = *now;
    runtime.mobs.entity_hurt_time[slot] = 0;
    runtime.mobs.entity_hurt_resistant[slot] = hurt_resistant;
    runtime.mobs.entity_last_damage[slot] = last_damage;
    runtime.mobs.fire_ticks[slot] = target_fire;
    runtime.mobs.arrow_count[slot] = 0;
    if (!gm_mobs_set_entity_random_state(
            &runtime.mobs, now->id[slot], target_seed, 0, 0.0)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }

    GmRuntimeProjectile *arrow = &runtime.projectiles[0];
    memset(arrow, 0, sizeof *arrow);
    arrow->active = 1;
    arrow->type = 1;
    arrow->eid = runtime.next_entity_id++;
    arrow->shooting_living = player_owned;
    arrow->player_thrower = player_owned;
    arrow->x = 24.5;
    arrow->y = 221.51999997615814;
    arrow->z = 24.5;
    arrow->vx = vx;
    arrow->vy = vy;
    arrow->vz = vz;
    arrow->arrow_damage = arrow_damage;
    arrow->arrow_knockback = knockback;
    arrow->arrow_critical = critical;
    arrow->arrow_pickup_status = 1;
    arrow->fire_ticks = arrow_fire;
    arrow->random_seed48 = arrow_seed;
    int result = gm_runtime_player_arrow_hit_now(&runtime, 0, slot);
    if (result == 0) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    now = runtime.mobs.current ? &runtime.mobs.b : &runtime.mobs.a;
    printf("{\"ok\":true,"
           "\"target_health_bits\":\"%08" PRIx32 "\","
           "\"target_vx_bits\":\"%016" PRIx64 "\","
           "\"target_vy_bits\":\"%016" PRIx64 "\","
           "\"target_vz_bits\":\"%016" PRIx64 "\","
           "\"target_fire_ticks\":%d,\"target_hurt_time\":%d,"
           "\"target_hurt_resistant_time\":%d,"
           "\"target_last_damage_bits\":\"%08" PRIx32 "\","
           "\"target_arrow_count\":%d,\"target_recently_hit\":%d,"
           "\"target_attacking_player\":%s,"
           "\"target_seed48\":%" PRIu64 ","
           "\"arrow_alive\":%s,"
           "\"arrow_vx_bits\":\"%016" PRIx64 "\","
           "\"arrow_vy_bits\":\"%016" PRIx64 "\","
           "\"arrow_vz_bits\":\"%016" PRIx64 "\","
           "\"arrow_yaw_bits\":\"%08" PRIx32 "\","
           "\"arrow_ticks_in_air\":%d,\"arrow_seed48\":%" PRIu64 "}\n",
           float_bits(now->health[slot]),
           double_bits(now->vx[slot]), double_bits(now->vy[slot]),
           double_bits(now->vz[slot]), runtime.mobs.fire_ticks[slot],
           runtime.mobs.entity_hurt_time[slot],
           runtime.mobs.entity_hurt_resistant[slot],
           float_bits(runtime.mobs.entity_last_damage[slot]),
           runtime.mobs.arrow_count[slot],
           runtime.mobs.entity_recently_hit[slot],
           runtime.mobs.entity_attacking_player[slot] ? "true" : "false",
           runtime.mobs.entity_random[slot].random.seed,
           arrow->active ? "true" : "false",
           double_bits(arrow->vx), double_bits(arrow->vy),
           double_bits(arrow->vz), float_bits(arrow->yaw),
           arrow->age, arrow->random_seed48);
    gm_runtime_destroy(&runtime);
    return 0;
}
