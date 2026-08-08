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

static void print_vec3(double x, double y, double z) {
    printf("[\"%016" PRIx64 "\",\"%016" PRIx64
           "\",\"%016" PRIx64 "\"]",
        double_bits(x), double_bits(y), double_bits(z));
}

static void print_travel_trace_state(
        const GmRuntime *runtime, int slot, int tick) {
    const GmMobLive *m = &runtime->mobs;
    const EwStore *s = store(m);
    printf("{\"position_bits\":");
    print_vec3(s->x[slot], s->y[slot], s->z[slot]);
    printf(",\"motion_bits\":");
    print_vec3(s->vx[slot], s->vy[slot], s->vz[slot]);
    printf(",\"on_ground\":%s,\"yaw_bits\":\"%08" PRIx32
           "\",\"prev_yaw_bits\":\"%08" PRIx32
           "\",\"pitch_bits\":\"%08" PRIx32
           "\",\"render_yaw_bits\":\"%08" PRIx32
           "\",\"head_yaw_bits\":\"%08" PRIx32
           "\",\"step_height_bits\":\"%08" PRIx32
           "\",\"jump_factor_bits\":\"%08" PRIx32
           "\",\"ai_speed_bits\":\"%08" PRIx32
           "\",\"prev_limb_amount_bits\":\"%08" PRIx32
           "\",\"limb_amount_bits\":\"%08" PRIx32
           "\",\"limb_swing_bits\":\"%08" PRIx32
           "\",\"riding\":true,\"player_position_bits\":",
        s->on_ground[slot] ? "true" : "false",
        float_bits(s->yaw[slot]), float_bits(m->pig_prev_yaw[slot]),
        float_bits(m->pig_pitch[slot]), float_bits(m->pig_render_yaw[slot]),
        float_bits(m->pig_head_yaw[slot]),
        float_bits(m->pig_step_height[slot]),
        float_bits(m->pig_jump_factor[slot]),
        float_bits(m->pig_ai_speed[slot]),
        float_bits(m->pig_prev_limb_amount[slot]),
        float_bits(m->pig_limb_amount[slot]),
        float_bits(m->pig_limb_swing[slot]));
    print_vec3(
        s->x[slot],
        runtime->player.ent.posY,
        s->z[slot]);
    printf(",\"boosting\":%s,\"boost_time\":%d,"
           "\"boost_total\":%d,\"entity_seed48\":%" PRIu64
           ",\"tick\":%d,\"aabb_min_bits\":",
        m->pig_boosting[slot] ? "true" : "false",
        m->pig_boost_time[slot], m->pig_boost_total[slot],
        (uint64_t)m->entity_random[slot].random.seed, tick);
    print_vec3(
        m->entity_box_min_x[slot], m->entity_box_min_y[slot],
        m->entity_box_min_z[slot]);
    printf(",\"aabb_max_bits\":");
    print_vec3(
        m->entity_box_max_x[slot], m->entity_box_max_y[slot],
        m->entity_box_max_z[slot]);
    printf(",\"fall_distance_bits\":\"%08" PRIx32
           "\",\"collided_horizontal\":%s,"
           "\"collided_vertical\":%s,\"alive\":%s,\"is_in_water\":%s,"
           "\"is_in_lava\":%s,"
           "\"is_in_web\":%s,"
           "\"is_on_ladder\":%s,"
           "\"player_motion_bits\":",
        float_bits(m->entity_fall_distance[slot]),
        m->entity_collided_horizontal[slot] ? "true" : "false",
        m->entity_collided_vertical[slot] ? "true" : "false",
        s->alive[slot] ? "true" : "false",
        m->entity_in_water[slot] ? "true" : "false",
        m->entity_in_lava[slot] ? "true" : "false",
        m->entity_in_web[slot] ? "true" : "false",
        gm_world_block(
            runtime->world, mc_floor(s->x[slot]), mc_floor(s->y[slot]),
            mc_floor(s->z[slot])) == 65 ? "true" : "false");
    print_vec3(
        runtime->player.ent.motionX, runtime->player.ent.motionY,
        runtime->player.ent.motionZ);
    printf(",\"player_on_ground\":%s,\"player_riding_eid\":%d}",
        runtime->player.ent.onGround ? "true" : "false", s->id[slot]);
}

static int boost_mode(int argc, char **argv) {
    int hand, item, meta, other_item, other_meta, creative, eid;
    int boosting, boost_time, boost_total;
    uint64_t seed48;
    if (argc != 13 || !parse_int(argv[2], &hand)
            || !parse_int(argv[3], &item) || !parse_int(argv[4], &meta)
            || !parse_int(argv[5], &other_item)
            || !parse_int(argv[6], &other_meta)
            || !parse_int(argv[7], &creative)
            || !parse_u48(argv[8], &seed48)
            || !parse_int(argv[9], &eid)
            || !parse_int(argv[10], &boosting)
            || !parse_int(argv[11], &boost_time)
            || !parse_int(argv[12], &boost_total)
            || (hand != 0 && hand != 1)
            || item < 0 || item > 32767 || other_item < 0
            || other_item > 32767 || meta < 0 || meta > 32767
            || other_meta < 0 || other_meta > 32767
            || (creative != 0 && creative != 1)
            || (boosting != 0 && boosting != 1) || boost_time < 0
            || boost_total < 0 || (boosting && boost_total <= 0)
            || eid <= 0)
        return 2;
    GmMobLive mobs;
    IsrInv inventory;
    gm_mobs_init(&mobs, 0);
    isr_init(&inventory);
    inventory.current_item = 0;
    isr_set_stack(&inventory, hand ? ISR_OFFHAND_SLOT : 0,
        item ? ic_mk(item, 1, meta) : ic_empty());
    isr_set_stack(&inventory, hand ? 0 : ISR_OFFHAND_SLOT,
        other_item ? ic_mk(other_item, 1, other_meta) : ic_empty());
    if (gm_mobs_spawn_exact(
            &mobs, EW_TYPE_PIG, eid, 2.5, 220.0, 0.5,
            0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0) < 0
            || !gm_mobs_set_pig_saddled(&mobs, eid, 1)
            || !gm_mobs_pig_mount(&mobs, eid)
            || !gm_mobs_set_entity_random_state(
                &mobs, eid, seed48, 0, 0.0)
            || !gm_mobs_set_pig_boost_state(
                &mobs, eid, boosting, boost_time, boost_total))
        return 1;
    int result = gm_mobs_pig_boost(
        &mobs, &inventory, hand ? ISR_OFFHAND_SLOT : 0, creative);
    ICStack after = isr_get_stack(
        &inventory, hand ? ISR_OFFHAND_SLOT : 0);
    int got_boosting, got_time, got_total;
    if (!gm_mobs_get_pig_boost_state(
            &mobs, eid, &got_boosting, &got_time, &got_total))
        return 1;
    int slot = mobs.pig_ride;
    printf("{\"ok\":true,\"mode\":\"boost\","
           "\"result\":\"%s\",\"item\":%d,\"count\":%d,"
           "\"meta\":%d,\"boosting\":%s,\"boost_time\":%d,"
           "\"boost_total\":%d,\"entity_seed48\":%" PRIu64 "}\n",
        result ? "success" : "pass",
        after.count > 0 ? after.item : 0,
        after.count > 0 ? after.count : 0,
        after.count > 0 ? after.meta : 0,
        got_boosting ? "true" : "false", got_time, got_total,
        (uint64_t)mobs.entity_random[slot].random.seed);
    return 0;
}

static int tick_mode(int argc, char **argv) {
    double x, z, motion_x, motion_y, motion_z;
    double rider_yaw_d, rider_pitch_d, ai_speed_d;
    double limb_amount_d, limb_swing_d;
    int main_item, main_meta, off_item, off_meta;
    int boosting, boost_time, boost_total, eid;
    int trace = !strcmp(argv[1], "trace");
    int trace_ticks = 1;
    const char *layout = "flat";
    uint64_t seed48;
    if (argc != (trace ? 23 : 21) || !parse_double(argv[2], &x)
            || !parse_double(argv[3], &z)
            || !parse_int(argv[4], &main_item)
            || !parse_int(argv[5], &main_meta)
            || !parse_int(argv[6], &off_item)
            || !parse_int(argv[7], &off_meta)
            || !parse_double(argv[8], &rider_yaw_d)
            || !parse_double(argv[9], &rider_pitch_d)
            || !parse_double(argv[10], &motion_x)
            || !parse_double(argv[11], &motion_y)
            || !parse_double(argv[12], &motion_z)
            || !parse_double(argv[13], &ai_speed_d)
            || !parse_double(argv[14], &limb_amount_d)
            || !parse_double(argv[15], &limb_swing_d)
            || !parse_int(argv[16], &boosting)
            || !parse_int(argv[17], &boost_time)
            || !parse_int(argv[18], &boost_total)
            || !parse_u48(argv[19], &seed48)
            || !parse_int(argv[20], &eid)
            || (trace && (!(layout = argv[21])[0]
                || !parse_int(argv[22], &trace_ticks)))
            || main_item < 0 || main_item > 32767 || off_item < 0
            || off_item > 32767 || main_meta < 0 || main_meta > 32767
            || off_meta < 0 || off_meta > 32767
            || (boosting != 0 && boosting != 1) || boost_time < 0
            || boost_total < 0 || (boosting && boost_total <= 0)
            || eid <= 0 || (trace && (trace_ticks < 1 || trace_ticks > 64
                || (strcmp(layout, "one_block_step")
                    && strcmp(layout, "two_block_wall")
                    && strcmp(layout, "two_cell_gap")
                    && strcmp(layout, "bottom_slab")
                    && strcmp(layout, "stone_floor")
                    && strcmp(layout, "soul_sand_floor")
                    && strcmp(layout, "web_corridor")
                    && strcmp(layout, "ladder_clear")
                    && strcmp(layout, "ladder_north_wall")
                    && strcmp(layout, "stone_bounce")
                    && strcmp(layout, "slime_bounce")
                    && strcmp(layout, "stone_low_landing")
                    && strcmp(layout, "slime_low_landing")
                    && strcmp(layout, "still_water")
                    && strcmp(layout, "water_entry")
                    && strcmp(layout, "water_entry_flow")
                    && strcmp(layout, "water_fall_entry")
                    && strcmp(layout, "water_edge_climb")
                    && strcmp(layout, "water_edge_blocked")
                    && strcmp(layout, "still_lava")
                    && strcmp(layout, "lava_entry")
                    && strcmp(layout, "lava_edge_climb")
                    && strcmp(layout, "lava_edge_blocked")
                    && strcmp(layout, "water_lava_overlap")))))
        return 2;
    float rider_yaw = (float)rider_yaw_d;
    float rider_pitch = (float)rider_pitch_d;
    float ai_speed = (float)ai_speed_d;
    float limb_amount = (float)limb_amount_d;
    float limb_swing = (float)limb_swing_d;
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
    int bounce_layout = !strcmp(layout, "stone_bounce")
        || !strcmp(layout, "slime_bounce");
    int low_landing_layout = !strcmp(layout, "stone_low_landing")
        || !strcmp(layout, "slime_low_landing");
    int water_fall_layout = !strcmp(layout, "water_fall_entry");
    int water_edge_layout = !strcmp(layout, "water_edge_climb")
        || !strcmp(layout, "water_edge_blocked");
    int lava_edge_layout = !strcmp(layout, "lava_edge_climb")
        || !strcmp(layout, "lava_edge_blocked");
    const double y = water_fall_layout ? 221.0
        : low_landing_layout ? 220.01
        : bounce_layout ? 220.5 : 220.0;
    int base_x = mc_floor(x), base_z = mc_floor(z);
    for (int bx = base_x - 3; bx <= base_x + 3; ++bx)
        for (int bz = base_z - 3;
                bz <= base_z + (trace ? 9 : 3); ++bz)
            for (int by = 219; by <= 222; ++by)
                gm_world_set_block(runtime.world, bx, by, bz,
                    by == 219
                        ? trace && (!strcmp(layout, "slime_bounce")
                                || !strcmp(layout, "slime_low_landing"))
                            ? 165
                        : trace && !strcmp(layout, "soul_sand_floor")
                            ? 88 : 1
                        : trace && !strcmp(layout, "web_corridor")
                                && by == 220
                            ? 30
                        : trace && !strcmp(layout, "still_water")
                                && by == 220
                            ? 9
                        : trace && !strcmp(layout, "still_lava")
                                && by == 220
                            ? 11
                        : 0);
    if (trace) {
        int obstacle_z = base_z + 2;
        if (!strcmp(layout, "water_entry")
                || !strcmp(layout, "water_entry_flow"))
            gm_world_set_block(
                runtime.world, base_x, 220, base_z + 1, 9);
        if (!strcmp(layout, "water_entry_flow"))
            gm_world_set_block_meta(
                runtime.world, base_x + 1, 220, base_z + 1, 8, 1);
        if (water_fall_layout) {
            gm_world_set_block(runtime.world, base_x, 219, base_z, 0);
            gm_world_set_block(runtime.world, base_x, 220, base_z, 9);
        }
        if (!strcmp(layout, "water_edge_climb")
                || !strcmp(layout, "water_edge_blocked")) {
            gm_world_set_block(runtime.world, base_x, 220, base_z, 9);
            gm_world_set_block(runtime.world, base_x, 220, base_z + 1, 1);
        }
        if (!strcmp(layout, "water_edge_blocked"))
            gm_world_set_block(runtime.world, base_x, 221, base_z, 9);
        if (!strcmp(layout, "lava_entry"))
            gm_world_set_block(
                runtime.world, base_x, 220, base_z + 1, 11);
        if (!strcmp(layout, "lava_edge_climb")
                || !strcmp(layout, "lava_edge_blocked")) {
            gm_world_set_block(runtime.world, base_x, 220, base_z, 11);
            gm_world_set_block(runtime.world, base_x, 220, base_z + 1, 1);
        }
        if (!strcmp(layout, "lava_edge_blocked"))
            gm_world_set_block(runtime.world, base_x, 221, base_z, 11);
        if (!strcmp(layout, "water_lava_overlap")) {
            gm_world_set_block(runtime.world, base_x, 220, base_z, 9);
            gm_world_set_block(runtime.world, base_x, 220, base_z + 1, 11);
        }
        if (!strcmp(layout, "one_block_step")
                || !strcmp(layout, "two_block_wall"))
            gm_world_set_block(
                runtime.world, base_x, 220, obstacle_z, 1);
        if (!strcmp(layout, "two_block_wall"))
            gm_world_set_block(
                runtime.world, base_x, 221, obstacle_z, 1);
        if (!strcmp(layout, "two_cell_gap")) {
            gm_world_set_block(
                runtime.world, base_x, 219, obstacle_z, 0);
            gm_world_set_block(
                runtime.world, base_x, 219, obstacle_z + 1, 0);
        }
        if (!strcmp(layout, "bottom_slab"))
            gm_world_set_block_meta(
                runtime.world, base_x, 220, obstacle_z, 44, 0);
        if (!strcmp(layout, "ladder_north_wall"))
            for (int by = 220; by <= 221; ++by) {
                gm_world_set_block(
                    runtime.world, base_x, by, base_z + 1, 1);
                gm_world_set_block_meta(
                    runtime.world, base_x, by, base_z, 65, 2);
            }
    }
    gm_mobs_init(&runtime.mobs, 0);
    runtime.mobs.active_dimension = runtime.dimension;
    runtime.mobs.tick = 1;
    gm_runtime_set_pose(&runtime, x, y + 0.325, z, rider_yaw, rider_pitch);
    runtime.player.ent.onGround = 1;
    gm_world_fill_window(
        runtime.world, runtime.ccx, runtime.ccz,
        (struct Chunk *)runtime.window);
    runtime.player.inv.current_item = 0;
    isr_set_stack(&runtime.player.inv, 0,
        main_item ? ic_mk(main_item, 1, main_meta) : ic_empty());
    isr_set_stack(&runtime.player.inv, ISR_OFFHAND_SLOT,
        off_item ? ic_mk(off_item, 1, off_meta) : ic_empty());
    int slot = gm_mobs_spawn_exact(
        &runtime.mobs, EW_TYPE_PIG, eid, x, y, z,
        motion_x, motion_y, motion_z, 0.0F, 10.0F, 1, 0, 0, 0);
    if (slot < 0 || !gm_mobs_set_pig_saddled(&runtime.mobs, eid, 1)
            || !gm_mobs_pig_mount(&runtime.mobs, eid)
            || !gm_mobs_set_entity_random_state(
                &runtime.mobs, eid, seed48, 0, 0.0)
            || !gm_mobs_set_pig_boost_state(
                &runtime.mobs, eid, boosting, boost_time, boost_total)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    runtime.mobs.a.on_ground[slot] =
        bounce_layout || low_landing_layout || water_fall_layout
            || water_edge_layout || lava_edge_layout ? 0 : 1;
    runtime.mobs.b.on_ground[slot] =
        bounce_layout || low_landing_layout || water_fall_layout
            || water_edge_layout || lava_edge_layout ? 0 : 1;
    runtime.mobs.pig_ai_speed[slot] = ai_speed;
    runtime.mobs.pig_limb_amount[slot] = limb_amount;
    runtime.mobs.pig_prev_limb_amount[slot] = limb_amount;
    runtime.mobs.pig_limb_swing[slot] = limb_swing;
    if (trace) runtime.mobs.entity_living_sound_time[slot] = -80;
    if (trace) {
        printf("{\"ok\":true,\"mode\":\"trace\","
               "\"start_position_bits\":");
        print_vec3(x, y, z);
        printf(",\"layout\":\"%s\",\"trace\":[", layout);
        for (int tick = 0; tick < trace_ticks; ++tick) {
            gm_mobs_tick(
                &runtime.mobs, runtime.world,
                (const struct Chunk *)runtime.window,
                (const struct McSinTable *)&runtime.sin_table,
                (struct PsvPlayer *)&runtime.player,
                (struct PvStats *)&runtime.vitals,
                runtime.ox, runtime.oz, runtime.dimension,
                runtime.clock.world_time, runtime.mob_griefing,
                &runtime.world_random_seed48, &runtime.math_random_seed48,
                &runtime.next_entity_id, runtime.do_mob_loot,
                &runtime.entities, 0.0F, 0.0F);
            if (tick) putchar(',');
            print_travel_trace_state(&runtime, slot, tick);
        }
        printf("]}\n");
        gm_runtime_destroy(&runtime);
        return 0;
    }
    gm_mobs_tick(
        &runtime.mobs, runtime.world, (const struct Chunk *)runtime.window,
        (const struct McSinTable *)&runtime.sin_table,
        (struct PsvPlayer *)&runtime.player,
        (struct PvStats *)&runtime.vitals,
        runtime.ox, runtime.oz, runtime.dimension,
        runtime.clock.world_time, runtime.mob_griefing,
        &runtime.world_random_seed48, &runtime.math_random_seed48,
        &runtime.next_entity_id, runtime.do_mob_loot, &runtime.entities,
        0.0F, 0.0F);
    const EwStore *s = store(&runtime.mobs);
    printf("{\"ok\":true,\"mode\":\"tick\",\"start_position_bits\":");
    print_vec3(x, y, z);
    printf(",\"position_bits\":");
    print_vec3(s->x[slot], s->y[slot], s->z[slot]);
    printf(",\"motion_bits\":");
    print_vec3(s->vx[slot], s->vy[slot], s->vz[slot]);
    printf(",\"on_ground\":%s,\"yaw_bits\":\"%08" PRIx32
           "\",\"prev_yaw_bits\":\"%08" PRIx32
           "\",\"pitch_bits\":\"%08" PRIx32
           "\",\"render_yaw_bits\":\"%08" PRIx32
           "\",\"head_yaw_bits\":\"%08" PRIx32
           "\",\"step_height_bits\":\"%08" PRIx32
           "\",\"jump_factor_bits\":\"%08" PRIx32
           "\",\"ai_speed_bits\":\"%08" PRIx32
           "\",\"prev_limb_amount_bits\":\"%08" PRIx32
           "\",\"limb_amount_bits\":\"%08" PRIx32
           "\",\"limb_swing_bits\":\"%08" PRIx32
           "\",\"riding\":true,\"player_position_bits\":",
        s->on_ground[slot] ? "true" : "false",
        float_bits(s->yaw[slot]),
        float_bits(runtime.mobs.pig_prev_yaw[slot]),
        float_bits(runtime.mobs.pig_pitch[slot]),
        float_bits(runtime.mobs.pig_render_yaw[slot]),
        float_bits(runtime.mobs.pig_head_yaw[slot]),
        float_bits(runtime.mobs.pig_step_height[slot]),
        float_bits(runtime.mobs.pig_jump_factor[slot]),
        float_bits(runtime.mobs.pig_ai_speed[slot]),
        float_bits(runtime.mobs.pig_prev_limb_amount[slot]),
        float_bits(runtime.mobs.pig_limb_amount[slot]),
        float_bits(runtime.mobs.pig_limb_swing[slot]));
    print_vec3(
        runtime.player.ent.posX + runtime.ox,
        runtime.player.ent.posY,
        runtime.player.ent.posZ + runtime.oz);
    printf(",\"boosting\":%s,\"boost_time\":%d,"
           "\"boost_total\":%d,\"entity_seed48\":%" PRIu64 "}\n",
        runtime.mobs.pig_boosting[slot] ? "true" : "false",
        runtime.mobs.pig_boost_time[slot],
        runtime.mobs.pig_boost_total[slot],
        (uint64_t)runtime.mobs.entity_random[slot].random.seed);
    gm_runtime_destroy(&runtime);
    return 0;
}

static void print_lava_contact_state(
        const GmRuntime *runtime, int slot, int tick) {
    const GmMobLive *m = &runtime->mobs;
    const EwStore *s = store(m);
    McAABB box = m->entity_box_valid[slot]
        ? mc_aabb_make(
            m->entity_box_min_x[slot], m->entity_box_min_y[slot],
            m->entity_box_min_z[slot], m->entity_box_max_x[slot],
            m->entity_box_max_y[slot], m->entity_box_max_z[slot])
        : mc_aabb_make(
            s->x[slot] - (double)0.9F * 0.5, s->y[slot],
            s->z[slot] - (double)0.9F * 0.5,
            s->x[slot] + (double)0.9F * 0.5,
            s->y[slot] + (double)0.9F,
            s->z[slot] + (double)0.9F * 0.5);
    printf("{\"tick\":%d,\"position_bits\":", tick);
    print_vec3(s->x[slot], s->y[slot], s->z[slot]);
    printf(",\"aabb_min_bits\":");
    print_vec3(box.minX, box.minY, box.minZ);
    printf(",\"aabb_max_bits\":");
    print_vec3(box.maxX, box.maxY, box.maxZ);
    printf(",\"fall_distance_bits\":\"%08" PRIx32
           "\",\"is_in_water\":%s,\"is_in_lava\":%s,"
           "\"health_bits\":\"%08" PRIx32
           "\",\"fire_ticks\":%d,\"hurt_time\":%d,"
           "\"hurt_resistant_time\":%d,"
           "\"fire_resistance_ticks\":%d,"
           "\"last_damage_bits\":\"%08" PRIx32
           "\",\"alive\":%s,\"entity_seed48\":%" PRIu64
           ",\"math_seed48\":%" PRIu64 "}",
        float_bits(m->entity_server_fall_distance[slot]),
        m->entity_server_in_water[slot] ? "true" : "false",
        m->entity_server_in_lava[slot] ? "true" : "false",
        float_bits(s->health[slot]), m->fire_ticks[slot],
        m->entity_hurt_time[slot], m->entity_hurt_resistant[slot],
        m->entity_server_fire_resistance_ticks[slot],
        float_bits(m->entity_last_damage[slot]),
        s->alive[slot] ? "true" : "false",
        (uint64_t)m->entity_server_random[slot].random.seed,
        (uint64_t)runtime->math_random_seed48);
}

static void print_packet_contact_checkpoint(
        const GmPigPacketContactCheckpoint *state, int tick) {
    printf("{\"tick\":%d,\"position_bits\":", tick);
    print_vec3(state->x, state->y, state->z);
    printf(",\"aabb_min_bits\":");
    print_vec3(state->box.minX, state->box.minY, state->box.minZ);
    printf(",\"aabb_max_bits\":");
    print_vec3(state->box.maxX, state->box.maxY, state->box.maxZ);
    printf(",\"fall_distance_bits\":\"%08" PRIx32
           "\",\"is_in_water\":%s,\"is_in_lava\":%s,"
           "\"health_bits\":\"%08" PRIx32
           "\",\"fire_ticks\":%d,\"hurt_time\":%d,"
           "\"hurt_resistant_time\":%d,"
           "\"fire_resistance_ticks\":%d,"
           "\"last_damage_bits\":\"%08" PRIx32
           "\",\"alive\":%s,\"entity_seed48\":%" PRIu64
           ",\"math_seed48\":%" PRIu64 "}",
        float_bits(state->fall_distance),
        state->is_in_water ? "true" : "false",
        state->is_in_lava ? "true" : "false",
        float_bits(state->health), state->fire_ticks,
        state->hurt_time, state->hurt_resistant_time,
        state->fire_resistance_ticks,
        float_bits(state->last_damage),
        state->alive ? "true" : "false",
        state->entity_seed48, state->math_seed48);
}

static const char *vehicle_move_result_name(int result) {
    if (result == GM_PIG_VEHICLE_MOVE_ACCEPTED) return "accepted";
    if (result == GM_PIG_VEHICLE_MOVE_CORRECTED_COLLISION)
        return "corrected_collision";
    return "corrected_speed";
}

static void print_vehicle_move_state(
        const GmRuntime *runtime, int slot, int tick,
        const GmPigVehicleMoveResult *move, int post_base) {
    const GmMobLive *m = &runtime->mobs;
    const EwStore *s = store(m);
    McAABB box = m->entity_box_valid[slot]
        ? mc_aabb_make(
            m->entity_box_min_x[slot], m->entity_box_min_y[slot],
            m->entity_box_min_z[slot], m->entity_box_max_x[slot],
            m->entity_box_max_y[slot], m->entity_box_max_z[slot])
        : mc_aabb_make(
            s->x[slot] - (double)0.9F * 0.5, s->y[slot],
            s->z[slot] - (double)0.9F * 0.5,
            s->x[slot] + (double)0.9F * 0.5,
            s->y[slot] + (double)0.9F,
            s->z[slot] + (double)0.9F * 0.5);
    printf("{\"tick\":%d,\"position_bits\":", tick);
    print_vec3(s->x[slot], s->y[slot], s->z[slot]);
    printf(",\"aabb_min_bits\":");
    print_vec3(box.minX, box.minY, box.minZ);
    printf(",\"aabb_max_bits\":");
    print_vec3(box.maxX, box.maxY, box.maxZ);
    printf(",\"fall_distance_bits\":\"%08" PRIx32
           "\",\"is_in_water\":%s,\"is_in_lava\":%s,"
           "\"health_bits\":\"%08" PRIx32
           "\",\"fire_ticks\":%d,\"hurt_time\":%d,"
           "\"hurt_resistant_time\":%d,"
           "\"fire_resistance_ticks\":%d,"
           "\"last_damage_bits\":\"%08" PRIx32
           "\",\"alive\":%s,\"entity_seed48\":%" PRIu64
           ",\"math_seed48\":%" PRIu64 ",\"motion_bits\":",
        float_bits(m->entity_server_fall_distance[slot]),
        m->entity_server_in_water[slot] ? "true" : "false",
        m->entity_server_in_lava[slot] ? "true" : "false",
        float_bits(s->health[slot]), m->fire_ticks[slot],
        m->entity_hurt_time[slot], m->entity_hurt_resistant[slot],
        m->entity_server_fire_resistance_ticks[slot],
        float_bits(m->entity_last_damage[slot]),
        s->alive[slot] ? "true" : "false",
        (uint64_t)m->entity_server_random[slot].random.seed,
        (uint64_t)runtime->math_random_seed48);
    print_vec3(s->vx[slot], s->vy[slot], s->vz[slot]);
    printf(",\"yaw_bits\":\"%08" PRIx32
           "\",\"pitch_bits\":\"%08" PRIx32
           "\",\"on_ground\":%s,\"lowest_ridden_bits\":",
        float_bits(s->yaw[slot]), float_bits(m->pig_pitch[slot]),
        s->on_ground[slot] ? "true" : "false");
    if (post_base)
        print_vec3(s->x[slot], s->y[slot], s->z[slot]);
    else
        print_vec3(move->lowest_x, move->lowest_y, move->lowest_z);
    printf(",\"lowest_ridden1_bits\":");
    print_vec3(move->lowest_x1, move->lowest_y1, move->lowest_z1);
    printf(",\"result\":\"%s\"}",
        vehicle_move_result_name(move->result));
}

static void print_runtime_vehicle_move_state_tick(
        const GmPigVehicleMoveCheckpoint *state, int tick) {
    printf("{\"tick\":%d,\"position_bits\":", tick);
    print_vec3(state->x, state->y, state->z);
    printf(",\"aabb_min_bits\":");
    print_vec3(state->box.minX, state->box.minY, state->box.minZ);
    printf(",\"aabb_max_bits\":");
    print_vec3(state->box.maxX, state->box.maxY, state->box.maxZ);
    printf(",\"fall_distance_bits\":\"%08" PRIx32
           "\",\"is_in_water\":%s,\"is_in_lava\":%s,"
           "\"health_bits\":\"%08" PRIx32
           "\",\"fire_ticks\":%d,\"hurt_time\":%d,"
           "\"hurt_resistant_time\":%d,"
           "\"fire_resistance_ticks\":%d,"
           "\"last_damage_bits\":\"%08" PRIx32
           "\",\"alive\":%s,\"entity_seed48\":%" PRIu64
           ",\"math_seed48\":%" PRIu64 ",\"motion_bits\":",
        float_bits(state->fall_distance),
        state->is_in_water ? "true" : "false",
        state->is_in_lava ? "true" : "false",
        float_bits(state->health), state->fire_ticks,
        state->hurt_time, state->hurt_resistant_time,
        state->fire_resistance_ticks,
        float_bits(state->last_damage),
        state->alive ? "true" : "false",
        state->entity_seed48, state->math_seed48);
    print_vec3(state->vx, state->vy, state->vz);
    printf(",\"yaw_bits\":\"%08" PRIx32
           "\",\"pitch_bits\":\"%08" PRIx32
           "\",\"on_ground\":%s,\"lowest_ridden_bits\":",
        float_bits(state->yaw), float_bits(state->pitch),
        state->on_ground ? "true" : "false");
    print_vec3(
        state->move.lowest_x, state->move.lowest_y,
        state->move.lowest_z);
    printf(",\"lowest_ridden1_bits\":");
    print_vec3(
        state->move.lowest_x1, state->move.lowest_y1,
        state->move.lowest_z1);
    printf(",\"result\":\"%s\"}",
        vehicle_move_result_name(state->move.result));
}

static void print_runtime_vehicle_move_state(
        const GmPigVehicleMoveCheckpoint *state) {
    print_runtime_vehicle_move_state_tick(state, 0);
}

static int runtime_vehicle_post_state(
        const GmRuntime *runtime, int slot,
        const GmPigVehicleMoveCheckpoint *packet,
        GmPigVehicleMoveCheckpoint *post) {
    GmPigVehicleServerState server;
    const EwStore *client;
    if (!runtime || !packet || !post || slot <= 0
            || slot >= EW_MAX_ENTITIES
            || !gm_mobs_get_pig_vehicle_server_state(
                &runtime->mobs, &server)
            || server.eid != packet->eid)
        return 0;
    client = runtime->mobs.current
        ? &runtime->mobs.b : &runtime->mobs.a;
    if (!client->alive[slot] || client->id[slot] != packet->eid)
        return 0;
    *post = *packet;
    post->x = server.x;
    post->y = server.y;
    post->z = server.z;
    post->vx = server.vx;
    post->vy = server.vy;
    post->vz = server.vz;
    post->box = server.box;
    post->yaw = server.yaw;
    post->pitch = server.pitch;
    post->on_ground = server.on_ground;
    post->fall_distance = server.fall_distance;
    post->is_in_water = runtime->mobs.entity_server_in_water[slot] ? 1 : 0;
    post->is_in_lava = runtime->mobs.entity_server_in_lava[slot] ? 1 : 0;
    post->health = client->health[slot];
    post->fire_ticks = runtime->mobs.fire_ticks[slot];
    post->hurt_time = runtime->mobs.entity_hurt_time[slot];
    post->hurt_resistant_time =
        runtime->mobs.entity_hurt_resistant[slot];
    post->fire_resistance_ticks =
        runtime->mobs.entity_server_fire_resistance_ticks[slot];
    post->last_damage = runtime->mobs.entity_last_damage[slot];
    post->alive = client->alive[slot] ? 1 : 0;
    post->entity_seed48 =
        runtime->mobs.entity_server_random[slot].random.seed;
    post->math_seed48 = runtime->math_random_seed48;
    post->move = packet->move;
    post->move.lowest_x = server.x;
    post->move.lowest_y = server.y;
    post->move.lowest_z = server.z;
    return 1;
}

static int runtime_vehicle_delivery_matches(
        const GmRuntime *runtime, int slot,
        const GmPigVehicleMoveCheckpoint *checkpoint) {
    const GmRuntimePigVehiclePacket *head = &runtime->pig_vehicle_packet;
    const GmRuntimePigVehiclePacket *deferred =
        &runtime->pig_vehicle_packet_deferred;
    const GmRuntimePigVehicleCorrection *correction =
        &runtime->pig_vehicle_last_correction;
    const EwStore *client = runtime->mobs.current
        ? &runtime->mobs.b : &runtime->mobs.a;
    if (!head->pending || head->eid != checkpoint->eid
            || !client->alive[slot]
            || client->id[slot] != checkpoint->eid)
        return 0;
    if (!checkpoint->move.correction_count) {
        return !correction->valid && !deferred->pending
            && head->seq == checkpoint->seq + 1
            && double_bits(head->x) == double_bits(client->x[slot])
            && double_bits(head->y) == double_bits(client->y[slot])
            && double_bits(head->z) == double_bits(client->z[slot])
            && float_bits(head->yaw) == float_bits(client->yaw[slot])
            && float_bits(head->pitch)
                == float_bits(runtime->mobs.pig_pitch[slot]);
    }
    const GmPigVehicleMoveResult *move = &checkpoint->move;
    return correction->valid && correction->eid == checkpoint->eid
        && correction->source_seq == checkpoint->seq
        && correction->ack_seq == checkpoint->seq + 1
        && double_bits(correction->x) == double_bits(move->correction_x)
        && double_bits(correction->y) == double_bits(move->correction_y)
        && double_bits(correction->z) == double_bits(move->correction_z)
        && float_bits(correction->yaw)
            == float_bits(move->correction_yaw)
        && float_bits(correction->pitch)
            == float_bits(move->correction_pitch)
        && head->seq == correction->ack_seq
        && double_bits(head->x) == double_bits(correction->x)
        && double_bits(head->y) == double_bits(correction->y)
        && double_bits(head->z) == double_bits(correction->z)
        && float_bits(head->yaw) == float_bits(correction->yaw)
        && float_bits(head->pitch) == float_bits(correction->pitch)
        && deferred->pending && deferred->eid == checkpoint->eid
        && deferred->seq == correction->ack_seq + 1
        && double_bits(deferred->x) == double_bits(client->x[slot])
        && double_bits(deferred->y) == double_bits(client->y[slot])
        && double_bits(deferred->z) == double_bits(client->z[slot])
        && float_bits(deferred->yaw) == float_bits(client->yaw[slot])
        && float_bits(deferred->pitch)
            == float_bits(runtime->mobs.pig_pitch[slot]);
}

static void print_client_vehicle_correction_state(
        const GmMobLive *m, int slot,
        double player_x, double player_y, double player_z) {
    const EwStore *client = m->current ? &m->b : &m->a;
    printf("{\"vehicle_position_bits\":");
    print_vec3(client->x[slot], client->y[slot], client->z[slot]);
    printf(",\"vehicle_aabb_min_bits\":");
    print_vec3(
        m->entity_box_min_x[slot],
        m->entity_box_min_y[slot],
        m->entity_box_min_z[slot]);
    printf(",\"vehicle_aabb_max_bits\":");
    print_vec3(
        m->entity_box_max_x[slot],
        m->entity_box_max_y[slot],
        m->entity_box_max_z[slot]);
    printf(",\"vehicle_motion_bits\":");
    print_vec3(client->vx[slot], client->vy[slot], client->vz[slot]);
    printf(",\"vehicle_yaw_bits\":\"%08" PRIx32
           "\",\"vehicle_pitch_bits\":\"%08" PRIx32
           "\",\"vehicle_on_ground\":%s,\"player_position_bits\":",
        float_bits(client->yaw[slot]), float_bits(m->pig_pitch[slot]),
        client->on_ground[slot] ? "true" : "false");
    print_vec3(player_x, player_y, player_z);
    putchar('}');
}

static int client_vehicle_correction_mode(int argc, char **argv) {
    double predicted_x, predicted_y, predicted_z;
    double correction_x, correction_y, correction_z;
    double predicted_yaw_d, predicted_pitch_d;
    double correction_yaw_d, correction_pitch_d;
    double motion_x, motion_y, motion_z;
    int on_ground, eid;
    if (argc != 17
            || !parse_double(argv[2], &predicted_x)
            || !parse_double(argv[3], &predicted_y)
            || !parse_double(argv[4], &predicted_z)
            || !parse_double(argv[5], &correction_x)
            || !parse_double(argv[6], &correction_y)
            || !parse_double(argv[7], &correction_z)
            || !parse_double(argv[8], &predicted_yaw_d)
            || !parse_double(argv[9], &predicted_pitch_d)
            || !parse_double(argv[10], &correction_yaw_d)
            || !parse_double(argv[11], &correction_pitch_d)
            || !parse_double(argv[12], &motion_x)
            || !parse_double(argv[13], &motion_y)
            || !parse_double(argv[14], &motion_z)
            || !parse_int(argv[15], &on_ground)
            || (on_ground != 0 && on_ground != 1)
            || !parse_int(argv[16], &eid) || eid <= 0
            || predicted_yaw_d < -180.0 || predicted_yaw_d > 180.0
            || predicted_pitch_d < -90.0 || predicted_pitch_d > 90.0
            || correction_yaw_d < -180.0 || correction_yaw_d > 180.0
            || correction_pitch_d < -90.0 || correction_pitch_d > 90.0)
        return 2;

    GmMobLive mobs;
    gm_mobs_init(&mobs, 0);
    int slot = gm_mobs_spawn_exact(
        &mobs, EW_TYPE_PIG, eid,
        predicted_x, predicted_y, predicted_z,
        motion_x, motion_y, motion_z,
        (float)predicted_yaw_d, 10.0F, 1, 0, 0, 0);
    if (slot < 0 || !gm_mobs_set_pig_saddled(&mobs, eid, 1)
            || !gm_mobs_pig_mount(&mobs, eid)
            || !gm_mobs_pig_apply_client_vehicle_correction(
                &mobs, eid,
                predicted_x, predicted_y, predicted_z,
                (float)predicted_yaw_d, (float)predicted_pitch_d))
        return 1;
    EwStore *client = mobs.current ? &mobs.b : &mobs.a;
    client->vx[slot] = motion_x;
    client->vy[slot] = motion_y;
    client->vz[slot] = motion_z;
    client->on_ground[slot] = (unsigned char)on_ground;
    double player_x = predicted_x;
    double player_y = predicted_y + 0.325D;
    double player_z = predicted_z;

    printf("{\"ok\":true,\"mode\":\"client_vehicle_correction\","
           "\"before\":");
    print_client_vehicle_correction_state(
        &mobs, slot, player_x, player_y, player_z);
    if (!gm_mobs_pig_apply_client_vehicle_correction(
            &mobs, eid,
            correction_x, correction_y, correction_z,
            (float)correction_yaw_d, (float)correction_pitch_d))
        return 1;
    printf(",\"after\":");
    print_client_vehicle_correction_state(
        &mobs, slot, player_x, player_y, player_z);
    printf(",\"ack\":{\"position_bits\":");
    print_vec3(correction_x, correction_y, correction_z);
    printf(",\"yaw_bits\":\"%08" PRIx32
           "\",\"pitch_bits\":\"%08" PRIx32 "\"}}\n",
        float_bits((float)correction_yaw_d),
        float_bits((float)correction_pitch_d));
    return 0;
}

static void place_packet_contact_layout(
        GmRuntime *runtime, const char *layout,
        int base_x, int base_y, int base_z) {
    if (!strcmp(layout, "cactus"))
        gm_world_set_block(runtime->world, base_x, base_y, base_z, 81);
    else if (!strcmp(layout, "cactus_fire"))
        gm_world_set_block(runtime->world, base_x - 1, base_y, base_z, 81);
    if (!strcmp(layout, "fire"))
        gm_world_set_block(runtime->world, base_x, base_y, base_z, 51);
    else if (!strcmp(layout, "cactus_fire"))
        gm_world_set_block(runtime->world, base_x, base_y, base_z, 51);
    if (!strcmp(layout, "water"))
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                gm_world_set_block(
                    runtime->world, base_x + dx, base_y, base_z + dz, 9);
    if (!strcmp(layout, "lava"))
        gm_world_set_block(runtime->world, base_x, base_y, base_z, 11);
}

static int lava_contact_mode(int argc, char **argv) {
    const char *layout;
    int ticks, eid, fire_resistance_ticks;
    double x, z;
    uint64_t entity_seed48, math_seed48;
    if (argc != 10 || !(layout = argv[2])[0]
            || (strcmp(layout, "lava") && strcmp(layout, "dry"))
            || !parse_int(argv[3], &ticks) || ticks < 1 || ticks > 20
            || !parse_double(argv[4], &x) || !parse_double(argv[5], &z)
            || !parse_int(argv[6], &eid) || eid <= 0
            || !parse_u48(argv[7], &entity_seed48)
            || !parse_u48(argv[8], &math_seed48)
            || !parse_int(argv[9], &fire_resistance_ticks)
            || fire_resistance_ticks < 0
            || fire_resistance_ticks > 32767)
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
    int base_x = mc_floor(x), base_z = mc_floor(z);
    for (int bx = base_x - 3; bx <= base_x + 3; ++bx)
        for (int bz = base_z - 3; bz <= base_z + 3; ++bz)
            for (int by = 219; by <= 222; ++by)
                gm_world_set_block(
                    runtime.world, bx, by, bz, by == 219 ? 1 : 0);
    if (!strcmp(layout, "lava"))
        gm_world_set_block(runtime.world, base_x, 220, base_z, 11);

    gm_mobs_init(&runtime.mobs, 0);
    runtime.mobs.active_dimension = runtime.dimension;
    runtime.mobs.tick = 1;
    runtime.next_entity_id = eid + 1;
    runtime.math_random_seed48 = math_seed48;
    gm_runtime_set_pose(&runtime, x, 220.325, z, 0.0F, 0.0F);
    runtime.player.ent.onGround = 1;
    runtime.player.inv.current_item = 0;
    isr_set_stack(&runtime.player.inv, 0, ic_mk(398, 1, 0));
    isr_set_stack(&runtime.player.inv, ISR_OFFHAND_SLOT, ic_empty());
    gm_world_fill_window(
        runtime.world, runtime.ccx, runtime.ccz,
        (struct Chunk *)runtime.window);
    int slot = gm_mobs_spawn_exact(
        &runtime.mobs, EW_TYPE_PIG, eid, x, 220.0, z,
        0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0);
    if (slot < 0 || !gm_mobs_set_pig_saddled(&runtime.mobs, eid, 1)
            || !gm_mobs_pig_mount(&runtime.mobs, eid)
            || !gm_mobs_set_entity_random_state(
                &runtime.mobs, eid, entity_seed48, 0, 0.0)
            || !gm_mobs_set_entity_fire_ticks(&runtime.mobs, eid, -1)
            || !gm_mobs_set_pig_server_fire_resistance(
                &runtime.mobs, eid, fire_resistance_ticks)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    runtime.mobs.a.on_ground[slot] = 1;
    runtime.mobs.b.on_ground[slot] = 1;
    runtime.mobs.entity_fall_distance[slot] = 2.5F;
    runtime.mobs.entity_server_fall_distance[slot] = 2.5F;
    runtime.mobs.pig_vehicle_server.on_ground = 1;
    runtime.mobs.pig_vehicle_server.fall_distance = 2.5F;
    runtime.mobs.entity_living_sound_time[slot] = -80;
    runtime.mobs.entity_server_living_sound_time[slot] = -80;
    /* EntityPlayerMP is not the local user, so this authoritative server pig
     * cannot passenger-steer during its ordinary base tick. */
    isr_set_stack(&runtime.player.inv, 0, ic_empty());
    printf("{\"ok\":true,\"mode\":\"lava_contact\","
           "\"layout\":\"%s\",\"start_position_bits\":",
        layout);
    print_vec3(x, 220.0, z);
    printf(",\"trace\":[");
    for (int tick = 0; tick < ticks; ++tick) {
        gm_mobs_tick(
            &runtime.mobs, runtime.world,
            (const struct Chunk *)runtime.window,
            (const struct McSinTable *)&runtime.sin_table,
            (struct PsvPlayer *)&runtime.player,
            (struct PvStats *)&runtime.vitals,
            runtime.ox, runtime.oz, runtime.dimension,
            runtime.clock.world_time, runtime.mob_griefing,
            &runtime.world_random_seed48, &runtime.math_random_seed48,
            &runtime.next_entity_id, runtime.do_mob_loot,
            &runtime.entities, 0.0F, 0.0F);
        if (tick) putchar(',');
        print_lava_contact_state(&runtime, slot, tick);
    }
    printf("]}\n");
    gm_runtime_destroy(&runtime);
    return 0;
}

static int packet_contact_mode(
        int argc, char **argv, int runtime_dispatch) {
    const char *layout;
    int ticks, eid, fire_resistance_ticks;
    double x, y, z;
    uint64_t entity_seed48, math_seed48;
    if (argc != 11 || !(layout = argv[2])[0]
            || (strcmp(layout, "dry") && strcmp(layout, "cactus")
                && strcmp(layout, "fire") && strcmp(layout, "cactus_fire")
                && strcmp(layout, "water") && strcmp(layout, "lava"))
            || !parse_int(argv[3], &ticks) || ticks < 1 || ticks > 20
            || !parse_double(argv[4], &x) || !parse_double(argv[5], &y)
            || !parse_double(argv[6], &z)
            || !parse_int(argv[7], &eid) || eid <= 0
            || !parse_u48(argv[8], &entity_seed48)
            || !parse_u48(argv[9], &math_seed48)
            || !parse_int(argv[10], &fire_resistance_ticks)
            || fire_resistance_ticks < 0
            || fire_resistance_ticks > 32767)
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
    int base_x = mc_floor(x), base_z = mc_floor(z);
    for (int bx = base_x - 3; bx <= base_x + 3; ++bx)
        for (int bz = base_z - 3; bz <= base_z + 3; ++bz)
            for (int by = 219; by <= 222; ++by)
                gm_world_set_block(
                    runtime.world, bx, by, bz, by == 219 ? 1 : 0);
    if (!runtime_dispatch)
        place_packet_contact_layout(
            &runtime, layout, base_x, 220, base_z);

    gm_mobs_init(&runtime.mobs, 0);
    runtime.mobs.active_dimension = runtime.dimension;
    runtime.mobs.tick = 1;
    runtime.next_entity_id = eid + 1;
    runtime.math_random_seed48 = math_seed48;
    gm_runtime_set_pose(&runtime, x, y + 0.325, z, 0.0F, 0.0F);
    runtime.player.ent.onGround = 1;
    runtime.player.inv.current_item = 0;
    isr_set_stack(&runtime.player.inv, 0, ic_mk(398, 1, 0));
    isr_set_stack(&runtime.player.inv, ISR_OFFHAND_SLOT, ic_empty());
    gm_world_fill_window(
        runtime.world, runtime.ccx, runtime.ccz,
        (struct Chunk *)runtime.window);
    int slot = gm_mobs_spawn_exact(
        &runtime.mobs, EW_TYPE_PIG, eid, x, y, z,
        0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0);
    if (slot < 0 || !gm_mobs_set_pig_saddled(&runtime.mobs, eid, 1)
            || !gm_mobs_pig_mount(&runtime.mobs, eid)
            || !gm_mobs_set_entity_random_state(
                &runtime.mobs, eid, entity_seed48, 0, 0.0)
            || !gm_mobs_set_entity_fire_ticks(
                &runtime.mobs, eid, !strcmp(layout, "water") ? 100 : -1)
            || !gm_mobs_set_pig_server_fire_resistance(
                &runtime.mobs, eid, fire_resistance_ticks)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    runtime.mobs.a.on_ground[slot] = 1;
    runtime.mobs.b.on_ground[slot] = 1;
    runtime.mobs.entity_fall_distance[slot] = 2.5F;
    runtime.mobs.entity_server_fall_distance[slot] = 2.5F;
    runtime.mobs.pig_vehicle_server.on_ground = 1;
    runtime.mobs.pig_vehicle_server.fall_distance = 2.5F;
    runtime.mobs.entity_server_in_water[slot] =
        (unsigned char)(!strcmp(layout, "water"));
    runtime.mobs.entity_server_in_lava[slot] =
        (unsigned char)(!strcmp(layout, "lava"));
    runtime.mobs.entity_living_sound_time[slot] = -80;
    runtime.mobs.entity_server_living_sound_time[slot] = -80;

    if (runtime_dispatch) {
        GmMobLive *initial_mobs =
            (GmMobLive *)malloc(sizeof *initial_mobs);
        if (!initial_mobs) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
        *initial_mobs = runtime.mobs;
        PsvPlayer initial_player = runtime.player;
        PsvPlayer initial_server_player = runtime.server_player;
        PvStats initial_vitals = runtime.vitals;
        GmLiveSim initial_entities = runtime.entities;
        GmWorldClock initial_clock = runtime.clock;
        uint64_t initial_world_seed = runtime.world_random_seed48;
        uint64_t initial_math_seed = runtime.math_random_seed48;
        int initial_next_entity_id = runtime.next_entity_id;
        long long initial_tick = runtime.tick;
        runtime.mobs_enabled = 1;
        GmAction prime;
        memset(&prime, 0, sizeof prime);
        prime.hotbar_sel = -1;
        gm_runtime_tick(&runtime, prime);
        int queued = runtime.pig_vehicle_packet.pending
            && runtime.pig_vehicle_packet.eid == eid;
        runtime.mobs = *initial_mobs;
        free(initial_mobs);
        runtime.player = initial_player;
        runtime.server_player = initial_server_player;
        runtime.vitals = initial_vitals;
        runtime.entities = initial_entities;
        runtime.clock = initial_clock;
        runtime.world_random_seed48 = initial_world_seed;
        runtime.math_random_seed48 = initial_math_seed;
        runtime.next_entity_id = initial_next_entity_id;
        runtime.tick = initial_tick;
        memset(&runtime.player_move_packet, 0,
               sizeof runtime.player_move_packet);
        runtime.player_position_packet_pending = 0;
        if (!queued) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
        runtime.pig_vehicle_packet = (GmRuntimePigVehiclePacket){
            .pending = 1,
            .eid = eid,
            .seq = 1,
            .x = x,
            .y = y,
            .z = z,
            .yaw = 0.0F,
            .pitch = 0.0F
        };
        place_packet_contact_layout(
            &runtime, layout, base_x, 220, base_z);
        gm_world_fill_window(
            runtime.world, runtime.ccx, runtime.ccz,
            (struct Chunk *)runtime.window);
        runtime.mobs.entity_server_in_water[slot] =
            (unsigned char)(!strcmp(layout, "water"));
        runtime.mobs.entity_server_in_lava[slot] =
            (unsigned char)(!strcmp(layout, "lava"));
    }
    /* The Java fixture is an authoritative server pig. Even though its
     * passenger holds the client steering item, EntityPlayerMP is not the
     * local user and canPassengerSteer is false, so the server base tick does
     * not perform client pig travel. The runtime mode used the stick only for
     * its preceding client packet emission. */
    isr_set_stack(&runtime.player.inv, 0, ic_empty());
    printf("{\"ok\":true,\"mode\":\"packet_contact\","
           "\"layout\":\"%s\",\"start_position_bits\":",
        layout);
    print_vec3(x, y, z);
    printf(",\"trace\":[");
    for (int tick = 0; tick < ticks; ++tick) {
        if (runtime_dispatch) {
            uint64_t before_seq =
                runtime.mobs.pig_packet_contact_checkpoint.seq;
            GmAction action;
            memset(&action, 0, sizeof action);
            action.hotbar_sel = -1;
            gm_runtime_tick(&runtime, action);
            GmPigPacketContactCheckpoint checkpoint;
            if (!gm_mobs_get_pig_packet_contact_checkpoint(
                    &runtime.mobs, &checkpoint)
                    || checkpoint.seq != before_seq + 1
                    || checkpoint.eid != eid
                    || runtime.pig_vehicle_packet.pending) {
                gm_runtime_destroy(&runtime);
                return 1;
            }
            if (tick) putchar(',');
            printf("{\"packet_state\":");
            print_packet_contact_checkpoint(&checkpoint, tick);
            printf(",\"post_state\":");
            print_lava_contact_state(&runtime, slot, tick);
            putchar('}');
            continue;
        }
        if (!gm_mobs_pig_packet_contact_world_exact(
                &runtime.mobs, (const struct Chunk *)runtime.window,
                runtime.ox, runtime.oz, eid,
                &runtime.math_random_seed48)) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
        if (tick) putchar(',');
        printf("{\"packet_state\":");
        print_lava_contact_state(&runtime, slot, tick);
        gm_mobs_tick(
            &runtime.mobs, runtime.world,
            (const struct Chunk *)runtime.window,
            (const struct McSinTable *)&runtime.sin_table,
            (struct PsvPlayer *)&runtime.player,
            (struct PvStats *)&runtime.vitals,
            runtime.ox, runtime.oz, runtime.dimension,
            runtime.clock.world_time, runtime.mob_griefing,
            &runtime.world_random_seed48, &runtime.math_random_seed48,
            &runtime.next_entity_id, runtime.do_mob_loot,
            &runtime.entities, 0.0F, 0.0F);
        printf(",\"post_state\":");
        print_lava_contact_state(&runtime, slot, tick);
        putchar('}');
    }
    printf("]}\n");
    gm_runtime_destroy(&runtime);
    return 0;
}

static int packet_move_mode(int argc, char **argv, int runtime_dispatch) {
    const char *layout;
    double x, y, z, target_x, target_y, target_z;
    double yaw_d, pitch_d;
    int eid;
    uint64_t entity_seed48, math_seed48;
    const char *source = runtime_dispatch && argc == 15 ? argv[14] : "direct";
    if (argc != (runtime_dispatch ? 15 : 14) || !(layout = argv[2])[0]
            || (strcmp(layout, "dry") && strcmp(layout, "wall")
                && strcmp(layout, "ceiling")
                && strcmp(layout, "move_cactus")
                && strcmp(layout, "move_fire")
                && strcmp(layout, "move_lava")
                && strcmp(layout, "wall_beyond_fire")
                && strcmp(layout, "wall_beyond_cactus")
                && strcmp(layout, "dry_to_water")
                && strcmp(layout, "water_to_fire"))
            || !parse_double(argv[3], &x)
            || !parse_double(argv[4], &y)
            || !parse_double(argv[5], &z)
            || !parse_double(argv[6], &target_x)
            || !parse_double(argv[7], &target_y)
            || !parse_double(argv[8], &target_z)
            || !parse_double(argv[9], &yaw_d)
            || !parse_double(argv[10], &pitch_d)
            || yaw_d < -180.0 || yaw_d > 180.0
            || pitch_d < -90.0 || pitch_d > 90.0
            || !parse_int(argv[11], &eid) || eid <= 0
            || !parse_u48(argv[12], &entity_seed48)
            || !parse_u48(argv[13], &math_seed48)
            || (runtime_dispatch && strcmp(source, "emitted_client")
                && strcmp(source, "injected_packet")))
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
    int base_x = mc_floor(x), base_y = mc_floor(y), base_z = mc_floor(z);
    for (int bx = base_x - 3; bx <= base_x + 12; ++bx)
        for (int bz = base_z - 3; bz <= base_z + 3; ++bz)
            for (int by = base_y - 1; by <= base_y + 2; ++by)
                gm_world_set_block(runtime.world, bx, by, bz,
                    by == base_y - 1 ? 1 : 0);
    if (!strcmp(layout, "wall") || !strcmp(layout, "wall_beyond_fire")
            || !strcmp(layout, "wall_beyond_cactus"))
        for (int wall_y = base_y; wall_y <= base_y + 1; ++wall_y)
            gm_world_set_block(
                runtime.world, base_x + 1, wall_y, base_z, 1);
    if (!strcmp(layout, "ceiling"))
        gm_world_set_block(
            runtime.world, base_x, base_y + 1, base_z, 1);
    if (!strcmp(layout, "move_cactus"))
        gm_world_set_block(
            runtime.world, base_x + 1, base_y, base_z, 81);
    if (!strcmp(layout, "move_fire"))
        gm_world_set_block(
            runtime.world, base_x + 1, base_y, base_z, 51);
    if (!strcmp(layout, "move_lava"))
        gm_world_set_block(
            runtime.world, base_x + 1, base_y, base_z, 11);
    if (!strcmp(layout, "wall_beyond_fire"))
        gm_world_set_block(
            runtime.world, base_x + 2, base_y, base_z, 51);
    if (!strcmp(layout, "wall_beyond_cactus"))
        gm_world_set_block(
            runtime.world, base_x + 2, base_y, base_z, 81);
    if (!strcmp(layout, "dry_to_water"))
        gm_world_set_block(
            runtime.world, base_x + 1, base_y, base_z, 9);
    if (!strcmp(layout, "water_to_fire")) {
        gm_world_set_block(runtime.world, base_x, base_y, base_z, 9);
        gm_world_set_block(
            runtime.world, base_x + 1, base_y, base_z, 51);
    }

    gm_mobs_init(&runtime.mobs, 0);
    runtime.mobs.active_dimension = runtime.dimension;
    runtime.mobs.tick = 1;
    runtime.next_entity_id = eid + 1;
    runtime.math_random_seed48 = math_seed48;
    gm_runtime_set_pose(&runtime, x, y + 0.325, z, 0.0F, 0.0F);
    runtime.player.ent.onGround = 1;
    runtime.player.inv.current_item = 0;
    isr_set_stack(&runtime.player.inv, 0, ic_empty());
    isr_set_stack(&runtime.player.inv, ISR_OFFHAND_SLOT, ic_empty());
    gm_world_fill_window(
        runtime.world, runtime.ccx, runtime.ccz,
        (struct Chunk *)runtime.window);
    int slot = gm_mobs_spawn_exact(
        &runtime.mobs, EW_TYPE_PIG, eid, x, y, z,
        0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0);
    if (slot < 0 || !gm_mobs_set_pig_saddled(&runtime.mobs, eid, 1)
            || !gm_mobs_pig_mount(&runtime.mobs, eid)
            || !gm_mobs_set_entity_random_state(
                &runtime.mobs, eid, entity_seed48, 0, 0.0)
            || !gm_mobs_set_entity_fire_ticks(&runtime.mobs, eid, -1)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    runtime.mobs.a.on_ground[slot] = 1;
    runtime.mobs.b.on_ground[slot] = 1;
    runtime.mobs.entity_fall_distance[slot] = 2.5F;
    runtime.mobs.entity_server_fall_distance[slot] = 2.5F;
    runtime.mobs.entity_living_sound_time[slot] = -80;
    runtime.mobs.entity_server_living_sound_time[slot] = -80;
    runtime.mobs.entity_server_in_water[slot] =
        (unsigned char)(!strcmp(layout, "water_to_fire"));
    if (!strcmp(layout, "water_to_fire"))
        runtime.mobs.fire_ticks[slot] = 0;

    if (runtime_dispatch) {
        GmRuntimePigVehiclePacket packet;
        memset(&packet, 0, sizeof packet);
        if (!strcmp(source, "emitted_client")) {
            isr_set_stack(
                &runtime.player.inv, 0, ic_mk(398, 1, 0));
            GmMobLive *initial_mobs =
                (GmMobLive *)malloc(sizeof *initial_mobs);
            if (!initial_mobs) {
                gm_runtime_destroy(&runtime);
                return 1;
            }
            *initial_mobs = runtime.mobs;
            PsvPlayer initial_player = runtime.player;
            PsvPlayer initial_server_player = runtime.server_player;
            PvStats initial_vitals = runtime.vitals;
            GmLiveSim initial_entities = runtime.entities;
            GmWorldClock initial_clock = runtime.clock;
            uint64_t initial_world_seed = runtime.world_random_seed48;
            uint64_t initial_math_seed = runtime.math_random_seed48;
            int initial_next_entity_id = runtime.next_entity_id;
            long long initial_tick = runtime.tick;
            runtime.mobs_enabled = 1;
            GmAction prime;
            memset(&prime, 0, sizeof prime);
            prime.hotbar_sel = -1;
            gm_runtime_tick(&runtime, prime);
            packet = runtime.pig_vehicle_packet;
            runtime.mobs = *initial_mobs;
            free(initial_mobs);
            runtime.player = initial_player;
            runtime.server_player = initial_server_player;
            runtime.vitals = initial_vitals;
            runtime.entities = initial_entities;
            runtime.clock = initial_clock;
            runtime.world_random_seed48 = initial_world_seed;
            runtime.math_random_seed48 = initial_math_seed;
            runtime.next_entity_id = initial_next_entity_id;
            runtime.tick = initial_tick;
            if (!packet.pending || packet.eid != eid
                    || double_bits(packet.x) != double_bits(target_x)
                    || double_bits(packet.y) != double_bits(target_y)
                    || double_bits(packet.z) != double_bits(target_z)
                    || float_bits(packet.yaw) != float_bits((float)yaw_d)
                    || float_bits(packet.pitch) != float_bits((float)pitch_d)) {
                fprintf(stderr,
                    "emitted packet pending=%d eid=%d xyz=%016" PRIx64
                    ",%016" PRIx64 ",%016" PRIx64
                    " yaw=%08" PRIx32 " pitch=%08" PRIx32 "\n",
                    packet.pending, packet.eid,
                    double_bits(packet.x), double_bits(packet.y),
                    double_bits(packet.z), float_bits(packet.yaw),
                    float_bits(packet.pitch));
                gm_runtime_destroy(&runtime);
                return 1;
            }
        } else {
            packet = (GmRuntimePigVehiclePacket){
                .pending = 1,
                .eid = eid,
                .seq = 1,
                .x = target_x,
                .y = target_y,
                .z = target_z,
                .yaw = (float)yaw_d,
                .pitch = (float)pitch_d
            };
        }

        EwStore *client = runtime.mobs.current
            ? &runtime.mobs.b : &runtime.mobs.a;
        McAABB client_box = mc_aabb_make(
            target_x - (double)0.9F * 0.5, target_y,
            target_z - (double)0.9F * 0.5,
            target_x + (double)0.9F * 0.5,
            target_y + (double)0.9F,
            target_z + (double)0.9F * 0.5);
        client->x[slot] = target_x;
        client->y[slot] = target_y;
        client->z[slot] = target_z;
        client->yaw[slot] = (float)yaw_d;
        runtime.mobs.pig_pitch[slot] = (float)pitch_d;
        runtime.mobs.entity_box_min_x[slot] = client_box.minX;
        runtime.mobs.entity_box_min_y[slot] = client_box.minY;
        runtime.mobs.entity_box_min_z[slot] = client_box.minZ;
        runtime.mobs.entity_box_max_x[slot] = client_box.maxX;
        runtime.mobs.entity_box_max_y[slot] = client_box.maxY;
        runtime.mobs.entity_box_max_z[slot] = client_box.maxZ;
        runtime.mobs.entity_box_valid[slot] = 1;
        runtime.mobs.pig_vehicle_server.on_ground = 1;
        runtime.mobs.pig_vehicle_server.fall_distance = 2.5F;
        runtime.player.ent.posX = target_x - runtime.ox;
        runtime.player.ent.posY = target_y + 0.325;
        runtime.player.ent.posZ = target_z - runtime.oz;
        runtime.player.ent.box = psv_player_box(
            runtime.player.ent.posX, runtime.player.ent.posY,
            runtime.player.ent.posZ);
        runtime.pig_vehicle_packet = packet;
        runtime.mobs_enabled = 1;
        memset(&runtime.player_move_packet, 0,
               sizeof runtime.player_move_packet);
        runtime.player_position_packet_pending = 0;
        /* The Java EntityPlayerMP retains the steering item while the pig's
         * base tick aligns its rotation and rejects server-side travel. */
        isr_set_stack(&runtime.player.inv, 0, ic_mk(398, 1, 0));
        GmAction consume;
        memset(&consume, 0, sizeof consume);
        consume.hotbar_sel = -1;
        gm_runtime_tick(&runtime, consume);
        GmPigVehicleMoveCheckpoint checkpoint;
        GmPigVehicleMoveCheckpoint post;
        if (!gm_mobs_get_pig_vehicle_move_checkpoint(
                &runtime.mobs, &checkpoint)
                || checkpoint.seq != 1 || checkpoint.eid != eid
                || !runtime.pig_vehicle_packet.pending
                || runtime.pig_vehicle_packet.seq != 2
                || runtime.pig_vehicle_packet.eid != eid
                || !runtime_vehicle_delivery_matches(
                    &runtime, slot, &checkpoint)
                || double_bits(checkpoint.client_x) != double_bits(target_x)
                || double_bits(checkpoint.client_y) != double_bits(target_y)
                || double_bits(checkpoint.client_z) != double_bits(target_z)
                || !runtime_vehicle_post_state(
                    &runtime, slot, &checkpoint, &post)) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
        printf("{\"ok\":true,\"mode\":\"runtime_packet_move\","
               "\"layout\":\"%s\",\"source\":\"%s\","
               "\"start_position_bits\":",
            layout, source);
        print_vec3(x, y, z);
        printf(",\"target_position_bits\":");
        print_vec3(target_x, target_y, target_z);
        printf(",\"target_yaw_bits\":\"%08" PRIx32
               "\",\"target_pitch_bits\":\"%08" PRIx32
               "\",\"packet_seq\":%" PRIu64
               ",\"packet_pending\":true,\"next_packet_seq\":%" PRIu64
               ",\"client_position_bits\":",
            float_bits((float)yaw_d), float_bits((float)pitch_d),
            checkpoint.seq, runtime.pig_vehicle_packet.seq);
        print_vec3(
            checkpoint.client_x, checkpoint.client_y,
            checkpoint.client_z);
        printf(",\"server_packet_state\":");
        print_runtime_vehicle_move_state(&checkpoint);
        printf(",\"server_post_state\":");
        print_runtime_vehicle_move_state(&post);
        printf("}\n");
        if (checkpoint.move.correction_count) {
            /* The correction acknowledgement and the later client prediction
             * are distinct packets. Drain both from the next server epoch
             * with steering disabled, proving the fixed two-entry FIFO did
             * not overwrite either one. */
            isr_set_stack(&runtime.player.inv, 0, ic_empty());
            isr_set_stack(
                &runtime.player.inv, ISR_OFFHAND_SLOT, ic_empty());
            gm_runtime_tick(&runtime, consume);
            if (runtime.pig_vehicle_packet.pending
                    || runtime.pig_vehicle_packet_deferred.pending) {
                gm_runtime_destroy(&runtime);
                return 1;
            }
        }
        gm_runtime_destroy(&runtime);
        return 0;
    }

    GmPigVehicleMoveResult move;
    if (!gm_mobs_pig_packet_move_dry_exact(
            &runtime.mobs, (const struct Chunk *)runtime.window,
            runtime.ox, runtime.oz, eid,
            target_x, target_y, target_z,
            (float)yaw_d, (float)pitch_d, &move)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    /* This older direct seam intentionally treats EwStore as the one server
     * pig body. The runtime cases below own and validate the independent
     * client/server shadow; do not let that unused shadow overwrite this
     * direct packet result during the following base tick. */
    runtime.mobs.pig_vehicle_server.valid = 0;
    printf("{\"ok\":true,\"mode\":\"packet_move\","
           "\"layout\":\"%s\",\"start_position_bits\":",
        layout);
    print_vec3(x, y, z);
    printf(",\"target_position_bits\":");
    print_vec3(target_x, target_y, target_z);
    printf(",\"target_yaw_bits\":\"%08" PRIx32
           "\",\"target_pitch_bits\":\"%08" PRIx32
           "\",\"trace\":[{\"packet_state\":",
        float_bits((float)yaw_d), float_bits((float)pitch_d));
    print_vehicle_move_state(&runtime, slot, 0, &move, 0);

    /* The real server passenger still holds the steering item, so
     * EntityPig aligns to the passenger's zero look before canPassengerSteer
     * rejects authoritative travel.  Emptying native input prevents client
     * travel; these two assignments retain that server rotation boundary. */
    EwStore *current = runtime.mobs.current
        ? &runtime.mobs.b : &runtime.mobs.a;
    current->yaw[slot] = runtime.player.yaw;
    runtime.mobs.pig_pitch[slot] = runtime.player.pitch * 0.5F;
    gm_mobs_tick(
        &runtime.mobs, runtime.world,
        (const struct Chunk *)runtime.window,
        (const struct McSinTable *)&runtime.sin_table,
        (struct PsvPlayer *)&runtime.player,
        (struct PvStats *)&runtime.vitals,
        runtime.ox, runtime.oz, runtime.dimension,
        runtime.clock.world_time, runtime.mob_griefing,
        &runtime.world_random_seed48, &runtime.math_random_seed48,
        &runtime.next_entity_id, runtime.do_mob_loot,
        &runtime.entities, 0.0F, 0.0F);
    printf(",\"post_state\":");
    print_vehicle_move_state(&runtime, slot, 0, &move, 1);
    printf("}]}\n");
    gm_runtime_destroy(&runtime);
    return 0;
}

static int packet_chain_mode(int argc, char **argv) {
    const char *layout;
    double x, y, z, yaw_d, pitch_d;
    int eid;
    uint64_t entity_seed48, math_seed48;
    if (argc != 11 || !(layout = argv[2])[0]
            || (strcmp(layout, "chain_same_epoch")
                && strcmp(layout, "chain_vertical_epoch")
                && strcmp(layout, "chain_mixed_rejections")
                && strcmp(layout, "chain_epoch_reseed")
                && strcmp(layout, "chain_later_water")
                && strcmp(layout, "chain_preticked_water"))
            || !parse_double(argv[3], &x)
            || !parse_double(argv[4], &y)
            || !parse_double(argv[5], &z)
            || !parse_double(argv[6], &yaw_d)
            || !parse_double(argv[7], &pitch_d)
            || yaw_d < -180.0 || yaw_d > 180.0
            || pitch_d < -90.0 || pitch_d > 90.0
            || !parse_int(argv[8], &eid) || eid <= 0
            || !parse_u48(argv[9], &entity_seed48)
            || !parse_u48(argv[10], &math_seed48))
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

    int base_x = mc_floor(x), base_y = mc_floor(y), base_z = mc_floor(z);
    for (int bx = base_x - 3; bx <= base_x + 12; ++bx)
        for (int bz = base_z - 3; bz <= base_z + 3; ++bz)
            for (int by = base_y - 1; by <= base_y + 2; ++by)
                gm_world_set_block(runtime.world, bx, by, bz,
                    by == base_y - 1 ? 1 : 0);
    if (!strcmp(layout, "chain_later_water")
            || !strcmp(layout, "chain_preticked_water"))
        gm_world_set_block(
            runtime.world, base_x + 1, base_y, base_z, 9);
    if (!strcmp(layout, "chain_mixed_rejections"))
        for (int wall_y = base_y; wall_y <= base_y + 1; ++wall_y)
            gm_world_set_block(
                runtime.world, base_x + 2, wall_y, base_z, 1);

    gm_mobs_init(&runtime.mobs, 0);
    runtime.mobs.active_dimension = runtime.dimension;
    runtime.mobs.tick = 1;
    runtime.next_entity_id = eid + 1;
    runtime.math_random_seed48 = math_seed48;
    gm_runtime_set_pose(&runtime, x, y + 0.325, z, 0.0F, 0.0F);
    runtime.player.ent.onGround = 1;
    runtime.player.inv.current_item = 0;
    isr_set_stack(&runtime.player.inv, 0, ic_mk(398, 1, 0));
    isr_set_stack(&runtime.player.inv, ISR_OFFHAND_SLOT, ic_empty());
    gm_world_fill_window(
        runtime.world, runtime.ccx, runtime.ccz,
        (struct Chunk *)runtime.window);
    int slot = gm_mobs_spawn_exact(
        &runtime.mobs, EW_TYPE_PIG, eid, x, y, z,
        0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0);
    if (slot < 0) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    runtime.mobs.a.on_ground[slot] = 1;
    runtime.mobs.b.on_ground[slot] = 1;
    runtime.mobs.entity_fall_distance[slot] = 2.5F;
    runtime.mobs.entity_server_fall_distance[slot] = 2.5F;
    runtime.mobs.entity_living_sound_time[slot] = -80;
    runtime.mobs.entity_server_living_sound_time[slot] = -80;
    if (!gm_mobs_set_pig_saddled(&runtime.mobs, eid, 1)
            || !gm_mobs_set_entity_random_state(
                &runtime.mobs, eid, entity_seed48, 0, 0.0)
            || !gm_mobs_set_entity_fire_ticks(&runtime.mobs, eid, -1)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    if (!strcmp(layout, "chain_preticked_water"))
        runtime.mobs.entity_ticks_existed[slot] = 1;
    if (!gm_mobs_pig_mount(&runtime.mobs, eid)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }

    int packet_count = !strcmp(layout, "chain_mixed_rejections") ? 4 : 2;
    double target_x[4] = {x, x, x, x};
    double target_y[4] = {y, y, y, y};
    double target_z[4] = {z, z, z, z};
    if (!strcmp(layout, "chain_same_epoch")) {
        target_x[0] = x + 0.25;
        target_x[1] = x + 0.5;
    } else if (!strcmp(layout, "chain_vertical_epoch")) {
        target_y[0] = y + 0.25;
        target_y[1] = y + 0.5;
    } else if (!strcmp(layout, "chain_mixed_rejections")) {
        target_x[0] = x + 0.25;
        target_x[1] = x + 2.0;
        target_x[2] = x + 10.1;
        target_x[3] = x + 0.5;
    } else if (!strcmp(layout, "chain_epoch_reseed")) {
        target_x[0] = x + 0.25;
        target_x[1] = x + 10.1;
    } else if (!strcmp(layout, "chain_later_water")) {
        target_z[0] = z + 0.125;
        target_x[1] = x + 0.75;
        target_z[1] = z + 0.125;
    } else {
        target_x[0] = x + 0.75;
        target_x[1] = x + 0.875;
    }

    printf("{\"ok\":true,\"mode\":\"packet_chain\","
           "\"layout\":\"%s\",\"start_position_bits\":",
        layout);
    print_vec3(x, y, z);
    printf(",\"target_positions_bits\":[");
    for (int packet_index = 0; packet_index < packet_count;
            ++packet_index) {
        if (packet_index) putchar(',');
        print_vec3(
            target_x[packet_index], target_y[packet_index],
            target_z[packet_index]);
    }
    printf("],\"target_yaw_bits\":\"%08" PRIx32
           "\",\"target_pitch_bits\":\"%08" PRIx32
           "\",\"trace\":[",
        float_bits((float)yaw_d), float_bits((float)pitch_d));

    for (int packet_index = 0; packet_index < packet_count; ++packet_index) {
        uint64_t seed_before =
            runtime.mobs.entity_server_random[slot].random.seed;
        int first_update_before =
            runtime.mobs.pig_vehicle_server.first_update ? 1 : 0;
        if (!gm_mobs_pig_packet_move_runtime_dry_exact(
                &runtime.mobs, (const struct Chunk *)runtime.window,
                runtime.ox, runtime.oz, eid,
                target_x[packet_index], target_y[packet_index],
                target_z[packet_index],
                (float)yaw_d, (float)pitch_d,
                &runtime.math_random_seed48)) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
        GmPigVehicleMoveCheckpoint checkpoint;
        if (!gm_mobs_get_pig_vehicle_move_checkpoint(
                &runtime.mobs, &checkpoint)) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
        if (packet_index) putchar(',');
        printf("{\"seed_before_packet48\":%" PRIu64
               ",\"first_update_before\":%s,"
               "\"first_update_after\":%s,\"packet_state\":",
            seed_before,
            first_update_before ? "true" : "false",
            runtime.mobs.pig_vehicle_server.first_update
                ? "true" : "false");
        print_runtime_vehicle_move_state_tick(
            &checkpoint, packet_index);
        putchar('}');

        if (packet_index == 0
                && strcmp(layout, "chain_same_epoch")
                && strcmp(layout, "chain_vertical_epoch")
                && strcmp(layout, "chain_mixed_rejections")) {
            gm_mobs_tick(
                &runtime.mobs, runtime.world,
                (const struct Chunk *)runtime.window,
                (const struct McSinTable *)&runtime.sin_table,
                (struct PsvPlayer *)&runtime.player,
                (struct PvStats *)&runtime.vitals,
                runtime.ox, runtime.oz, runtime.dimension,
                runtime.clock.world_time, runtime.mob_griefing,
                &runtime.world_random_seed48,
                &runtime.math_random_seed48,
                &runtime.next_entity_id, runtime.do_mob_loot,
                &runtime.entities, 0.0F, 0.0F);
        }
    }
    printf("]}\n");
    gm_runtime_destroy(&runtime);
    return 0;
}

static int dismount_mode(int argc, char **argv) {
    double yaw_d, pig_x, pig_y, pig_z;
    int player_eid, next_entity_id;
    uint64_t entity_seed48;
    const char *layout;
    if (argc != 10 || !(layout = argv[2])[0]
            || !parse_double(argv[3], &yaw_d)
            || !parse_double(argv[4], &pig_x)
            || !parse_double(argv[5], &pig_y)
            || !parse_double(argv[6], &pig_z)
            || !parse_int(argv[7], &player_eid)
            || !parse_u48(argv[8], &entity_seed48)
            || !parse_int(argv[9], &next_entity_id)
            || (strcmp(layout, "flat") && strcmp(layout, "first_blocked")
                && strcmp(layout, "water") && strcmp(layout, "all_blocked")
                && strcmp(layout, "support_stone")
                && strcmp(layout, "support_top_slab")
                && strcmp(layout, "support_bottom_slab")
                && strcmp(layout, "support_snow8")
                && strcmp(layout, "support_snow7")
                && strcmp(layout, "support_water"))
            || (yaw_d != 0.0 && yaw_d != 90.0)
            || player_eid <= 0 || next_entity_id <= 0
            || next_entity_id >= INT_MAX)
        return 2;
    float yaw = (float)yaw_d;
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
    for (int bx = base_x - 4; bx <= base_x + 4; ++bx)
        for (int bz = base_z - 4; bz <= base_z + 4; ++bz)
            for (int by = base_y - 1; by <= base_y + 4; ++by)
                gm_world_set_block(runtime.world, bx, by, bz,
                    by == base_y - 1
                        && (strncmp(layout, "support_", 8)
                            || (bx == base_x && bz == base_z))
                        ? (!strcmp(layout, "water") ? 9 : 1) : 0);
    int first_x = base_x - 1;
    int first_z = base_z;
    if (yaw == 90.0F) {
        first_x = base_x;
        first_z = base_z - 1;
    }
    if (!strcmp(layout, "support_stone"))
        gm_world_set_block(
            runtime.world, first_x, base_y - 1, first_z, 1);
    else if (!strcmp(layout, "support_top_slab"))
        gm_world_set_block_meta(
            runtime.world, first_x, base_y - 1, first_z, 44, 8);
    else if (!strcmp(layout, "support_bottom_slab"))
        gm_world_set_block_meta(
            runtime.world, first_x, base_y - 1, first_z, 44, 0);
    else if (!strcmp(layout, "support_snow8"))
        gm_world_set_block_meta(
            runtime.world, first_x, base_y - 1, first_z, 78, 7);
    else if (!strcmp(layout, "support_snow7"))
        gm_world_set_block_meta(
            runtime.world, first_x, base_y - 1, first_z, 78, 6);
    else if (!strcmp(layout, "support_water"))
        gm_world_set_block(
            runtime.world, first_x, base_y - 1, first_z, 9);
    if (!strcmp(layout, "first_blocked")
            || !strcmp(layout, "all_blocked"))
        gm_world_set_block(runtime.world, first_x, base_y, first_z, 1);
    if (!strcmp(layout, "all_blocked"))
        for (int bx = base_x - 1; bx <= base_x + 1; ++bx)
            for (int bz = base_z - 1; bz <= base_z + 1; ++bz)
                if (bx != base_x || bz != base_z)
                    gm_world_set_block(runtime.world, bx, base_y, bz, 1);

    gm_mobs_init(&runtime.mobs, 0);
    runtime.mobs.active_dimension = runtime.dimension;
    int slot = gm_mobs_spawn_exact(
        &runtime.mobs, EW_TYPE_PIG, next_entity_id,
        pig_x, pig_y, pig_z, 0.0, 0.0, 0.0,
        yaw, 10.0F, 1, 0, 0, 0);
    double mounted_y = pig_y + (double)0.9F * 0.75D - 0.35D;
    gm_runtime_set_pose(&runtime, pig_x, mounted_y, pig_z, yaw, 0.0F);
    gm_world_fill_window(
        runtime.world, runtime.ccx, runtime.ccz,
        (struct Chunk *)runtime.window);
    if (slot < 0
            || !gm_mobs_set_pig_saddled(
                &runtime.mobs, next_entity_id, 1)
            || !gm_mobs_pig_mount(&runtime.mobs, next_entity_id)
            || !gm_mobs_set_entity_random_state(
                &runtime.mobs, next_entity_id, entity_seed48, 0, 0.0)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    gm_mobs_pig_dismount_explicit(
        &runtime.mobs, runtime.world, (const struct Chunk *)runtime.window,
        (struct PsvPlayer *)&runtime.player, runtime.ox, runtime.oz);
    const EwStore *s = store(&runtime.mobs);
    const double pig_half = (double)(0.9F / 2.0F);
    const double pig_height = (double)0.9F;
    printf("{\"ok\":true,\"layout\":\"%s\",\"pig_eid\":%d,"
           "\"player_eid\":%d,\"player_position_bits\":",
        layout, next_entity_id, player_eid);
    print_vec3(
        runtime.player.ent.posX + runtime.ox,
        runtime.player.ent.posY,
        runtime.player.ent.posZ + runtime.oz);
    printf(",\"player_motion_bits\":");
    print_vec3(
        runtime.player.ent.motionX, runtime.player.ent.motionY,
        runtime.player.ent.motionZ);
    printf(",\"player_yaw_bits\":\"%08" PRIx32
           "\",\"player_pitch_bits\":\"%08" PRIx32
           "\",\"player_on_ground\":%s,"
           "\"player_fall_distance_bits\":\"%08" PRIx32
           "\",\"player_ride_cooldown\":0,\"player_riding_eid\":-1,"
           "\"player_aabb_min_bits\":",
        float_bits(runtime.player.yaw), float_bits(runtime.player.pitch),
        runtime.player.ent.onGround ? "true" : "false",
        float_bits(runtime.player.fall_distance));
    print_vec3(
        runtime.player.ent.box.minX + runtime.ox,
        runtime.player.ent.box.minY,
        runtime.player.ent.box.minZ + runtime.oz);
    printf(",\"player_aabb_max_bits\":");
    print_vec3(
        runtime.player.ent.box.maxX + runtime.ox,
        runtime.player.ent.box.maxY,
        runtime.player.ent.box.maxZ + runtime.oz);
    printf(",\"pig_position_bits\":");
    print_vec3(s->x[slot], s->y[slot], s->z[slot]);
    printf(",\"pig_yaw_bits\":\"%08" PRIx32
           "\",\"pig_aabb_min_bits\":",
        float_bits(s->yaw[slot]));
    print_vec3(
        s->x[slot] - pig_half, s->y[slot], s->z[slot] - pig_half);
    printf(",\"pig_aabb_max_bits\":");
    print_vec3(
        s->x[slot] + pig_half, s->y[slot] + pig_height,
        s->z[slot] + pig_half);
    printf(",\"pig_passenger_eids\":[],\"pig_entity_seed48\":%" PRIu64
           ",\"player_rng_unchanged\":true,"
           "\"world_rng_unchanged\":true,\"math_rng_unchanged\":true,"
           "\"next_entity_id\":%d}\n",
        entity_seed48, next_entity_id + 1);
    gm_runtime_destroy(&runtime);
    return 0;
}

static int death_dismount_mode(int argc, char **argv) {
    double yaw_d, pig_x, pig_y, pig_z;
    int player_eid, next_entity_id;
    uint64_t entity_seed48;
    const char *layout;
    if (argc != 10 || !(layout = argv[2])[0]
            || !parse_double(argv[3], &yaw_d)
            || !parse_double(argv[4], &pig_x)
            || !parse_double(argv[5], &pig_y)
            || !parse_double(argv[6], &pig_z)
            || !parse_int(argv[7], &player_eid)
            || !parse_u48(argv[8], &entity_seed48)
            || !parse_int(argv[9], &next_entity_id)
            || (strcmp(layout, "flat") && strcmp(layout, "all_blocked"))
            || (yaw_d != 0.0 && yaw_d != 90.0)
            || player_eid <= 0 || next_entity_id <= 0
            || next_entity_id >= INT_MAX)
        return 2;
    float yaw = (float)yaw_d;
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
    for (int bx = base_x - 4; bx <= base_x + 4; ++bx)
        for (int bz = base_z - 4; bz <= base_z + 4; ++bz)
            for (int by = base_y - 1; by <= base_y + 4; ++by)
                gm_world_set_block(runtime.world, bx, by, bz,
                    by == base_y - 1 ? 1 : 0);
    if (!strcmp(layout, "all_blocked"))
        for (int bx = base_x - 1; bx <= base_x + 1; ++bx)
            for (int bz = base_z - 1; bz <= base_z + 1; ++bz)
                if (bx != base_x || bz != base_z)
                    gm_world_set_block(runtime.world, bx, base_y, bz, 1);

    gm_mobs_init(&runtime.mobs, 0);
    runtime.mobs.active_dimension = runtime.dimension;
    runtime.next_entity_id = next_entity_id + 1;
    int slot = gm_mobs_spawn_exact(
        &runtime.mobs, EW_TYPE_PIG, next_entity_id,
        pig_x, pig_y, pig_z, 0.0, 0.0, 0.0,
        yaw, 0.0F, 1, 0, 19, 0);
    double mounted_y = pig_y + (double)0.9F * 0.75D - 0.35D;
    gm_runtime_set_pose(&runtime, pig_x, mounted_y, pig_z, yaw, 0.0F);
    gm_world_fill_window(
        runtime.world, runtime.ccx, runtime.ccz,
        (struct Chunk *)runtime.window);
    runtime.player.ent.motionX = 0.125D;
    runtime.player.ent.motionY = -0.25D;
    runtime.player.ent.motionZ = 0.375D;
    runtime.player.fall_distance = 2.5F;
    runtime.player.ent.onGround = 0;
    if (slot < 0
            || !gm_mobs_set_pig_saddled(
                &runtime.mobs, next_entity_id, 1)
            || !gm_mobs_pig_mount(&runtime.mobs, next_entity_id)
            || !gm_mobs_set_entity_random_state(
                &runtime.mobs, next_entity_id, entity_seed48, 0, 0.0)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    runtime.mobs.entity_dead[slot] = 1;
    uint64_t world_seed_before = runtime.world_random_seed48;
    uint64_t math_seed_before = runtime.math_random_seed48;
    gm_mobs_tick_controlled(
        &runtime.mobs, runtime.world, (const struct Chunk *)runtime.window,
        (struct PsvPlayer *)&runtime.player,
        runtime.ox, runtime.oz, runtime.dimension, runtime.do_mob_loot,
        &runtime.world_random_seed48, &runtime.math_random_seed48,
        &runtime.next_entity_id);
    const EwStore *s = store(&runtime.mobs);
    const double pig_half = (double)(0.9F / 2.0F);
    const double pig_height = (double)0.9F;
    printf("{\"ok\":true,\"layout\":\"%s\",\"pig_eid\":%d,"
           "\"player_eid\":%d,\"before\":{"
           "\"death_time\":19,\"living_dead\":true,"
           "\"entity_is_dead\":false,\"health_bits\":\"%08" PRIx32
           "\",\"saddled\":true,\"player_riding_eid\":%d,"
           "\"pig_passenger_count\":1},\"update_order\":["
           "{\"eid\":%d,\"riding_eid\":-1,\"pig_death_time\":19,"
           "\"pig_entity_is_dead\":false,\"pig_passenger_count\":1},"
           "{\"eid\":%d,\"riding_eid\":%d,\"pig_death_time\":20,"
           "\"pig_entity_is_dead\":true,\"pig_passenger_count\":1}],"
           "\"player_position_bits\":",
        layout, next_entity_id, player_eid, float_bits(0.0F),
        next_entity_id, next_entity_id,
        player_eid, next_entity_id);
    print_vec3(
        runtime.player.ent.posX + runtime.ox,
        runtime.player.ent.posY,
        runtime.player.ent.posZ + runtime.oz);
    printf(",\"player_motion_bits\":");
    print_vec3(
        runtime.player.ent.motionX, runtime.player.ent.motionY,
        runtime.player.ent.motionZ);
    printf(",\"player_yaw_bits\":\"%08" PRIx32
           "\",\"player_pitch_bits\":\"%08" PRIx32
           "\",\"player_on_ground\":%s,"
           "\"player_fall_distance_bits\":\"%08" PRIx32
           "\",\"player_aabb_min_bits\":",
        float_bits(runtime.player.yaw), float_bits(runtime.player.pitch),
        runtime.player.ent.onGround ? "true" : "false",
        float_bits(runtime.player.fall_distance));
    print_vec3(
        runtime.player.ent.box.minX + runtime.ox,
        runtime.player.ent.box.minY,
        runtime.player.ent.box.minZ + runtime.oz);
    printf(",\"player_aabb_max_bits\":");
    print_vec3(
        runtime.player.ent.box.maxX + runtime.ox,
        runtime.player.ent.box.maxY,
        runtime.player.ent.box.maxZ + runtime.oz);
    printf(",\"player_riding_eid\":-1,\"pig_death_time\":%d,"
           "\"pig_living_dead\":true,\"pig_entity_is_dead\":true,"
           "\"pig_loaded\":false,\"pig_health_bits\":\"%08" PRIx32
           "\",\"pig_saddled\":%s,\"pig_passenger_count\":0,"
           "\"pig_position_bits\":",
        runtime.mobs.entity_death_time[slot], float_bits(s->health[slot]),
        runtime.mobs.pig_saddled[slot] ? "true" : "false");
    print_vec3(s->x[slot], s->y[slot], s->z[slot]);
    printf(",\"pig_aabb_min_bits\":");
    print_vec3(
        s->x[slot] - pig_half, s->y[slot], s->z[slot] - pig_half);
    printf(",\"pig_aabb_max_bits\":");
    print_vec3(
        s->x[slot] + pig_half, s->y[slot] + pig_height,
        s->z[slot] + pig_half);
    printf(",\"pig_entity_seed48\":%" PRIu64
           ",\"player_rng_unchanged\":true,"
           "\"world_rng_unchanged\":%s,\"math_rng_unchanged\":%s,"
           "\"next_entity_id\":%d}\n",
        (uint64_t)runtime.mobs.entity_random[slot].random.seed,
        world_seed_before == runtime.world_random_seed48 ? "true" : "false",
        math_seed_before == runtime.math_random_seed48 ? "true" : "false",
        runtime.next_entity_id);
    gm_runtime_destroy(&runtime);
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "boost"))
        return boost_mode(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "tick"))
        return tick_mode(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "trace"))
        return tick_mode(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "lava_contact"))
        return lava_contact_mode(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "packet_contact"))
        return packet_contact_mode(argc, argv, 0);
    if (argc >= 2 && !strcmp(argv[1], "runtime_packet_contact"))
        return packet_contact_mode(argc, argv, 1);
    if (argc >= 2 && !strcmp(argv[1], "packet_move"))
        return packet_move_mode(argc, argv, 0);
    if (argc >= 2 && !strcmp(argv[1], "runtime_packet_move"))
        return packet_move_mode(argc, argv, 1);
    if (argc >= 2 && !strcmp(argv[1], "client_vehicle_correction"))
        return client_vehicle_correction_mode(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "packet_chain"))
        return packet_chain_mode(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "dismount"))
        return dismount_mode(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "death_dismount"))
        return death_dismount_mode(argc, argv);
    fprintf(stderr,
        "usage: %s boost|tick|trace|lava_contact|packet_contact|"
        "runtime_packet_contact|packet_move|runtime_packet_move|"
        "client_vehicle_correction|"
        "packet_chain|dismount|death_dismount ...\n",
        argv[0]);
    return 2;
}
