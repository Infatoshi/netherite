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

int main(int argc, char **argv) {
    const char *layout;
    double yaw_value, horse_x, horse_y, horse_z;
    int left_handed, player_eid, next_eid;
    if (argc != 9
            || (strcmp(layout = argv[1], "open")
                && strcmp(layout, "first_blocked")
                && strcmp(layout, "twice_blocked"))
            || !parse_double(argv[2], &yaw_value)
            || !parse_int(argv[3], &left_handed)
            || (left_handed != 0 && left_handed != 1)
            || !parse_double(argv[4], &horse_x)
            || !parse_double(argv[5], &horse_y)
            || !parse_double(argv[6], &horse_z)
            || !parse_int(argv[7], &player_eid)
            || !parse_int(argv[8], &next_eid)
            || next_eid <= 0 || next_eid >= INT_MAX - 1) {
        fprintf(stderr,
            "usage: %s LAYOUT YAW LEFT_HANDED HORSE_X HORSE_Y HORSE_Z "
            "PLAYER_EID NEXT_EID\n",
            argv[0]);
        return 2;
    }

    GmConfig config;
    GmRuntime runtime;
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
    int base_x = mc_floor(horse_x);
    int base_y = mc_floor(horse_y);
    int base_z = mc_floor(horse_z);
    float side = ((float)MC_PI / 2.0F)
        * (left_handed ? 1.0F : -1.0F);
    float angle = -(float)yaw_value * 0.017453292F
        - (float)MC_PI + side;
    float offset_x = -mc_sin(&runtime.sin_table, angle);
    float offset_z = -mc_cos(&runtime.sin_table, angle);
    double distance = (double)(0.6F / 2.0F + 1.3964844F / 2.0F) + 0.4D;
    double divisor = fabsf(offset_x) > fabsf(offset_z)
        ? (double)fabsf(offset_x) : (double)fabsf(offset_z);
    double scale = divisor > 0.0D ? distance / divisor : 0.0D;
    int first_x = mc_floor(horse_x + (double)offset_x * scale);
    int first_z = mc_floor(horse_z + (double)offset_z * scale);
    for (int bx = base_x - 3; bx <= base_x + 3; ++bx)
        for (int bz = base_z - 3; bz <= base_z + 3; ++bz)
            for (int by = base_y; by <= base_y + 4; ++by)
                gm_world_set_block(runtime.world, bx, by, bz, 0);
    if (!strcmp(layout, "first_blocked")
            || !strcmp(layout, "twice_blocked"))
        gm_world_set_block(
            runtime.world, first_x, base_y + 1, first_z, 1);
    if (!strcmp(layout, "twice_blocked"))
        gm_world_set_block(
            runtime.world, first_x, base_y + 2, first_z, 1);
    gm_world_fill_window(
        runtime.world, runtime.ccx, runtime.ccz,
        (struct Chunk *)runtime.window);
    if (!gm_runtime_spawn_horse_fixture(
            &runtime, GM_MOB_HORSE, next_eid,
            horse_x, horse_y, horse_z, 0.0, 0.0, 0.0,
            (float)yaw_value, 20.0F, 1, 20.0, 0.225, 0.7,
            0, GM_HORSE_TAME | GM_HORSE_SADDLED, 0,
            0, 0, 0, 0, 0, 0, 0, 0)
            || !gm_mobs_set_horse_inventory(
                &runtime.mobs, next_eid, 0, ic_mk(329, 1, 0))
            || !gm_mobs_horse_mount(&runtime.mobs, next_eid)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    runtime.player.yaw = (float)yaw_value;
    runtime.player.pitch = 17.0F;
    runtime.player.ent.posX = horse_x - runtime.ox;
    runtime.player.ent.posY = horse_y
        + (double)1.6F * 0.75D - 0.35D;
    runtime.player.ent.posZ = horse_z - runtime.oz;
    runtime.player.ent.box = psv_player_box(
        runtime.player.ent.posX, runtime.player.ent.posY,
        runtime.player.ent.posZ);
    runtime.player.ent.motionX = 0.125D;
    runtime.player.ent.motionY = -0.25D;
    runtime.player.ent.motionZ = 0.375D;
    runtime.player.ent.onGround = 0;
    runtime.player.fall_distance = 4.5F;
    gm_mobs_horse_dismount_explicit_side(
        &runtime.mobs, runtime.world,
        (const struct Chunk *)runtime.window,
        (const struct McSinTable *)&runtime.sin_table,
        (struct PsvPlayer *)&runtime.player, !left_handed,
        runtime.ox, runtime.oz);

    printf("{\"ok\":true,\"layout\":\"%s\",\"left_handed\":%s,"
           "\"horse_eid\":%d,"
           "\"horse_passenger_count\":0,\"horse_position_bits\":",
        layout, left_handed ? "true" : "false", next_eid);
    print_vec3(horse_x, horse_y, horse_z);
    printf(",\"horse_yaw_bits\":\"%08" PRIx32
           "\",\"math_rng_unchanged\":true,\"next_entity_id\":%d,"
           "\"player_aabb_max_bits\":",
        float_bits((float)yaw_value), next_eid + 1);
    print_vec3(
        runtime.player.ent.box.maxX + runtime.ox,
        runtime.player.ent.box.maxY,
        runtime.player.ent.box.maxZ + runtime.oz);
    printf(",\"player_aabb_min_bits\":");
    print_vec3(
        runtime.player.ent.box.minX + runtime.ox,
        runtime.player.ent.box.minY,
        runtime.player.ent.box.minZ + runtime.oz);
    printf(",\"player_eid\":%d,\"player_fall_distance_bits\":\"%08"
           PRIx32 "\",\"player_motion_bits\":",
        player_eid, float_bits(runtime.player.fall_distance));
    print_vec3(
        runtime.player.ent.motionX, runtime.player.ent.motionY,
        runtime.player.ent.motionZ);
    printf(",\"player_on_ground\":false,\"player_pitch_bits\":\"%08"
           PRIx32 "\",\"player_position_bits\":",
        float_bits(runtime.player.pitch));
    print_vec3(
        runtime.player.ent.posX + runtime.ox,
        runtime.player.ent.posY,
        runtime.player.ent.posZ + runtime.oz);
    printf(",\"player_ride_cooldown\":0,\"player_riding_eid\":-1,"
           "\"player_rng_unchanged\":true,\"player_yaw_bits\":\"%08"
           PRIx32 "\",\"world_rng_unchanged\":true}\n",
        float_bits(runtime.player.yaw));
    gm_runtime_destroy(&runtime);
    return 0;
}
