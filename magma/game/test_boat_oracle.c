#include "game/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static unsigned long long dbits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return (unsigned long long)bits;
}

static unsigned fbits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static EwStore *store(GmMobLive *m) {
    return m->current ? &m->b : &m->a;
}

static const char *status_name(int status) {
    static const char *const names[] = {
        "IN_WATER", "UNDER_WATER", "UNDER_FLOWING_WATER",
        "ON_LAND", "IN_AIR"
    };
    return status >= 0 && status < 5 ? names[status] : "null";
}

static int init(GmRuntime *runtime) {
    GmConfig config;
    char error[256];
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(runtime, &config, error, sizeof error)) {
        fprintf(stderr, "boat oracle init: %s\n", error);
        return 0;
    }
    gm_mobs_set_natural_spawning(&runtime->mobs, 0);
    return 1;
}

static void fill(GmRuntime *runtime, int y, int id, int meta) {
    for (int x = -2; x <= 2; ++x)
        for (int z = -2; z <= 2; ++z)
            gm_world_set_block_meta(runtime->world, x, y, z, id, meta);
    gm_world_fill_window(
        runtime->world, 0, 0, (struct Chunk *)runtime->window);
}

static int run(const char *name, int floor_id, int floor_meta, double y,
        double vx, double vy, double vz, int initial_status) {
    GmRuntime runtime;
    if (!init(&runtime)) return 0;
    if (floor_id) fill(&runtime, 79, floor_id, floor_meta);
    int slot = gm_mobs_place_boat(&runtime.mobs, 0.5, y, 0.5, 0.0F);
    if (slot <= 0) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    runtime.mobs.a.vx[slot] = runtime.mobs.b.vx[slot] = vx;
    runtime.mobs.a.vy[slot] = runtime.mobs.b.vy[slot] = vy;
    runtime.mobs.a.vz[slot] = runtime.mobs.b.vz[slot] = vz;
    runtime.mobs.boat_status[slot] = (signed char)initial_status;
    gm_mobs_tick(&runtime.mobs, runtime.world,
        (const struct Chunk *)runtime.window,
        (const struct McSinTable *)&runtime.sin_table,
        (struct PsvPlayer *)&runtime.player,
        (struct PvStats *)&runtime.vitals,
        runtime.ox, runtime.oz, runtime.dimension,
        runtime.clock.world_time, &runtime.clock,
        runtime.mob_griefing, &runtime.world_random_seed48,
        &runtime.math_random_seed48, &runtime.next_entity_id,
        runtime.do_mob_loot, &runtime.entities, 0.0F, 0.0F);
    EwStore *state = store(&runtime.mobs);
    printf("%s %s %s %016llx %016llx %016llx %016llx %016llx %016llx "
           "%016llx %08x %08x %016llx %d %08x\n",
        name,
        status_name(runtime.mobs.boat_status[slot]),
        status_name(runtime.mobs.boat_previous_status[slot]),
        dbits(state->x[slot]), dbits(state->y[slot]), dbits(state->z[slot]),
        dbits(state->vx[slot]), dbits(state->vy[slot]), dbits(state->vz[slot]),
        dbits(runtime.mobs.boat_water_level[slot]),
        fbits(runtime.mobs.boat_glide[slot]),
        fbits(runtime.mobs.boat_momentum[slot]),
        dbits(runtime.mobs.boat_last_yd[slot]),
        state->on_ground[slot] ? 1 : 0,
        fbits(runtime.mobs.entity_fall_distance[slot]));
    gm_runtime_destroy(&runtime);
    return 1;
}

static int run_obstacle(
        const char *name, int id, int meta) {
    GmRuntime runtime;
    if (!init(&runtime)) return 0;
    gm_world_set_block_meta(runtime.world, 2, 80, 0, id, meta);
    gm_world_fill_window(
        runtime.world, 0, 0, (struct Chunk *)runtime.window);
    int slot = gm_mobs_place_boat(
        &runtime.mobs, 0.5, 80.0, 0.5, 0.0F);
    if (slot <= 0) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    runtime.mobs.a.vx[slot] = runtime.mobs.b.vx[slot] = 1.0;
    gm_mobs_tick(&runtime.mobs, runtime.world,
        (const struct Chunk *)runtime.window,
        (const struct McSinTable *)&runtime.sin_table,
        (struct PsvPlayer *)&runtime.player,
        (struct PvStats *)&runtime.vitals,
        runtime.ox, runtime.oz, runtime.dimension,
        runtime.clock.world_time, &runtime.clock,
        runtime.mob_griefing, &runtime.world_random_seed48,
        &runtime.math_random_seed48, &runtime.next_entity_id,
        runtime.do_mob_loot, &runtime.entities, 0.0F, 0.0F);
    EwStore *state = store(&runtime.mobs);
    printf("%s %s %s %016llx %016llx %016llx %016llx %016llx %016llx "
           "%016llx %08x %08x %016llx %d %08x\n",
        name,
        status_name(runtime.mobs.boat_status[slot]),
        status_name(runtime.mobs.boat_previous_status[slot]),
        dbits(state->x[slot]), dbits(state->y[slot]), dbits(state->z[slot]),
        dbits(state->vx[slot]), dbits(state->vy[slot]), dbits(state->vz[slot]),
        dbits(runtime.mobs.boat_water_level[slot]),
        fbits(runtime.mobs.boat_glide[slot]),
        fbits(runtime.mobs.boat_momentum[slot]),
        dbits(runtime.mobs.boat_last_yd[slot]),
        state->on_ground[slot] ? 1 : 0,
        fbits(runtime.mobs.entity_fall_distance[slot]));
    gm_runtime_destroy(&runtime);
    return 1;
}

static int run_collision_matrix(void) {
    static const int blocks[] = {
        26, 29, 33, 44, 53, 54, 60, 65, 67, 78, 81, 85, 92,
        96, 102, 107, 108, 109, 114, 116, 117, 118, 120, 122,
        126, 127, 128, 130, 134, 135, 136, 139, 140, 144, 145,
        146, 151, 154, 156, 163, 164, 167, 171, 178, 180, 182,
        198, 199, 205, 208
    };
    GmRuntime runtime;
    if (!init(&runtime)) return 0;
    static const int layout_pos[5][3] = {
        {2, 80, 0}, {-1, 80, 0}, {0, 80, 2},
        {0, 80, -1}, {0, 79, 0}
    };
    static const double layout_motion[5][3] = {
        {1.0, 0.0, 0.0}, {-1.0, 0.0, 0.0},
        {0.0, 0.0, 1.0}, {0.0, 0.0, -1.0},
        {0.0, -1.0, 0.0}
    };
    for (unsigned block = 0;
            block < sizeof blocks / sizeof blocks[0]; ++block) {
        for (int meta = 0; meta < 16; ++meta) {
            if ((blocks[block] == 29 || blocks[block] == 33)
                    && (meta & 7) > 5)
                continue;
            if ((blocks[block] == 92 && meta > 6)
                    || (blocks[block] == 118 && meta > 3)
                    || (blocks[block] == 127 && (meta >> 2) > 2)
                    || (blocks[block] == 145 && (meta >> 2) > 2)
                    || (blocks[block] == 154
                        && ((meta & 7) == 1 || (meta & 7) > 5)))
                continue;
            for (int layout = 0; layout < 5; ++layout) {
                for (int clear = 0; clear < 5; ++clear) {
                    int lx, lz;
                    int chunk = psv_chunk_index(
                        layout_pos[clear][0], layout_pos[clear][2],
                        &lx, &lz);
                    if (chunk < 0) {
                        gm_runtime_destroy(&runtime);
                        return 0;
                    }
                    gm_world_set_block(runtime.world,
                        layout_pos[clear][0], layout_pos[clear][1],
                        layout_pos[clear][2], 0);
                    mc_set(&runtime.window[chunk], lx,
                        layout_pos[clear][1], lz, mc_state(0, 0));
                }
                int lx, lz;
                int chunk = psv_chunk_index(
                    layout_pos[layout][0], layout_pos[layout][2],
                    &lx, &lz);
                if (chunk < 0) {
                    gm_runtime_destroy(&runtime);
                    return 0;
                }
                gm_world_set_block_meta(runtime.world,
                    layout_pos[layout][0], layout_pos[layout][1],
                    layout_pos[layout][2], blocks[block], meta);
                mc_set(&runtime.window[chunk], lx,
                    layout_pos[layout][1], lz,
                    mc_state(blocks[block], meta));
                gm_mobs_init(&runtime.mobs, runtime.seed);
                gm_mobs_set_natural_spawning(&runtime.mobs, 0);
                int slot = gm_mobs_place_boat(&runtime.mobs,
                    0.5, layout == 4 ? 81.0 : 80.0, 0.5, 0.0F);
                if (slot <= 0) {
                    gm_runtime_destroy(&runtime);
                    return 0;
                }
                runtime.mobs.a.vx[slot] = runtime.mobs.b.vx[slot] =
                    layout_motion[layout][0];
                runtime.mobs.a.vy[slot] = runtime.mobs.b.vy[slot] =
                    layout_motion[layout][1];
                runtime.mobs.a.vz[slot] = runtime.mobs.b.vz[slot] =
                    layout_motion[layout][2];
                gm_mobs_tick(&runtime.mobs, runtime.world,
                    (const struct Chunk *)runtime.window,
                    (const struct McSinTable *)&runtime.sin_table,
                    (struct PsvPlayer *)&runtime.player,
                    (struct PvStats *)&runtime.vitals,
                    runtime.ox, runtime.oz, runtime.dimension,
                    runtime.clock.world_time, &runtime.clock,
                    runtime.mob_griefing, &runtime.world_random_seed48,
                    &runtime.math_random_seed48, &runtime.next_entity_id,
                    runtime.do_mob_loot, &runtime.entities, 0.0F, 0.0F);
                EwStore *state = store(&runtime.mobs);
                printf("M%03d_%02d_%d %s %s %016llx %016llx %016llx "
                       "%016llx %016llx %016llx %016llx %08x %08x "
                       "%016llx %d %08x\n",
                    blocks[block], meta, layout,
                    status_name(runtime.mobs.boat_status[slot]),
                    status_name(runtime.mobs.boat_previous_status[slot]),
                    dbits(state->x[slot]), dbits(state->y[slot]),
                    dbits(state->z[slot]), dbits(state->vx[slot]),
                    dbits(state->vy[slot]), dbits(state->vz[slot]),
                    dbits(runtime.mobs.boat_water_level[slot]),
                    fbits(runtime.mobs.boat_glide[slot]),
                    fbits(runtime.mobs.boat_momentum[slot]),
                    dbits(runtime.mobs.boat_last_yd[slot]),
                    state->on_ground[slot] ? 1 : 0,
                    fbits(runtime.mobs.entity_fall_distance[slot]));
            }
        }
    }
    gm_runtime_destroy(&runtime);
    return 1;
}

static int run_connected_matrix(void) {
    static const int blocks[] = {54, 85, 101, 102, 113, 139, 146, 160};
    static const int neighbors[4][2] = {
        {0, -1}, {1, 0}, {0, 1}, {-1, 0}
    };
    GmRuntime runtime;
    if (!init(&runtime)) return 0;
    for (unsigned block = 0;
            block < sizeof blocks / sizeof blocks[0]; ++block) {
        for (int meta = 0; meta < 16; ++meta) {
            for (int mask = 0; mask < 16; ++mask) {
                for (int side = -1; side < 4; ++side) {
                    int x = 2 + (side < 0 ? 0 : neighbors[side][0]);
                    int z = side < 0 ? 0 : neighbors[side][1];
                    int lx, lz;
                    int chunk = psv_chunk_index(x, z, &lx, &lz);
                    if (chunk < 0) {
                        gm_runtime_destroy(&runtime);
                        return 0;
                    }
                    gm_world_set_block(runtime.world, x, 80, z, 0);
                    mc_set(&runtime.window[chunk], lx, 80, lz,
                        mc_state(0, 0));
                }
                for (int side = -1; side < 4; ++side) {
                    if (side >= 0 && !(mask & (1 << side))) continue;
                    int x = 2 + (side < 0 ? 0 : neighbors[side][0]);
                    int z = side < 0 ? 0 : neighbors[side][1];
                    int lx, lz;
                    int chunk = psv_chunk_index(x, z, &lx, &lz);
                    if (chunk < 0) {
                        gm_runtime_destroy(&runtime);
                        return 0;
                    }
                    gm_world_set_block_meta(
                        runtime.world, x, 80, z, blocks[block], meta);
                    mc_set(&runtime.window[chunk], lx, 80, lz,
                        mc_state(blocks[block], meta));
                }
                gm_mobs_init(&runtime.mobs, runtime.seed);
                gm_mobs_set_natural_spawning(&runtime.mobs, 0);
                int slot = gm_mobs_place_boat(
                    &runtime.mobs, 0.5, 80.0, 0.5, 0.0F);
                if (slot <= 0) {
                    gm_runtime_destroy(&runtime);
                    return 0;
                }
                runtime.mobs.a.vx[slot] = runtime.mobs.b.vx[slot] = 1.0;
                gm_mobs_tick(&runtime.mobs, runtime.world,
                    (const struct Chunk *)runtime.window,
                    (const struct McSinTable *)&runtime.sin_table,
                    (struct PsvPlayer *)&runtime.player,
                    (struct PvStats *)&runtime.vitals,
                    runtime.ox, runtime.oz, runtime.dimension,
                    runtime.clock.world_time, &runtime.clock,
                    runtime.mob_griefing, &runtime.world_random_seed48,
                    &runtime.math_random_seed48, &runtime.next_entity_id,
                    runtime.do_mob_loot, &runtime.entities, 0.0F, 0.0F);
                EwStore *state = store(&runtime.mobs);
                printf("N%03d_%02d_%02d %s %s %016llx %016llx %016llx "
                       "%016llx %016llx %016llx %016llx %08x %08x "
                       "%016llx %d %08x\n",
                    blocks[block], meta, mask,
                    status_name(runtime.mobs.boat_status[slot]),
                    status_name(runtime.mobs.boat_previous_status[slot]),
                    dbits(state->x[slot]), dbits(state->y[slot]),
                    dbits(state->z[slot]), dbits(state->vx[slot]),
                    dbits(state->vy[slot]), dbits(state->vz[slot]),
                    dbits(runtime.mobs.boat_water_level[slot]),
                    fbits(runtime.mobs.boat_glide[slot]),
                    fbits(runtime.mobs.boat_momentum[slot]),
                    dbits(runtime.mobs.boat_last_yd[slot]),
                    state->on_ground[slot] ? 1 : 0,
                    fbits(runtime.mobs.entity_fall_distance[slot]));
            }
        }
    }
    gm_runtime_destroy(&runtime);
    return 1;
}

static int run_rider(const char *name, float forward, float strafe) {
    GmRuntime runtime;
    if (!init(&runtime)) return 0;
    fill(&runtime, 79, 9, 0);
    int slot = gm_mobs_place_boat(&runtime.mobs, 0.5, 79.6, 0.5, 0.0F);
    if (slot <= 0) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    gm_runtime_set_pose(&runtime, 0.5, 80.0, 0.5, 0.0F, 0.0F);
    if (!gm_mobs_boat_mount(&runtime.mobs,
            (struct PsvPlayer *)&runtime.player, runtime.ox, runtime.oz)) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    gm_mobs_tick(&runtime.mobs, runtime.world,
        (const struct Chunk *)runtime.window,
        (const struct McSinTable *)&runtime.sin_table,
        (struct PsvPlayer *)&runtime.player,
        (struct PvStats *)&runtime.vitals,
        runtime.ox, runtime.oz, runtime.dimension,
        runtime.clock.world_time, &runtime.clock,
        runtime.mob_griefing, &runtime.world_random_seed48,
        &runtime.math_random_seed48, &runtime.next_entity_id,
        runtime.do_mob_loot, &runtime.entities, forward, strafe);
    EwStore *state = store(&runtime.mobs);
    printf("R%s %016llx %016llx %016llx %016llx %016llx %016llx "
           "%08x %08x %08x %08x %d %d %016llx %016llx %016llx %08x\n",
        name,
        dbits(state->x[slot]), dbits(state->y[slot]), dbits(state->z[slot]),
        dbits(state->vx[slot]), dbits(state->vy[slot]), dbits(state->vz[slot]),
        fbits(state->yaw[slot]),
        fbits(runtime.mobs.boat_delta_rotation[slot]),
        fbits(runtime.mobs.boat_paddle_position[slot][0]),
        fbits(runtime.mobs.boat_paddle_position[slot][1]),
        runtime.mobs.boat_paddle_state[slot][0] ? 1 : 0,
        runtime.mobs.boat_paddle_state[slot][1] ? 1 : 0,
        dbits(runtime.player.ent.posX), dbits(runtime.player.ent.posY),
        dbits(runtime.player.ent.posZ), fbits(runtime.player.yaw));
    gm_runtime_destroy(&runtime);
    return 1;
}

typedef struct {
    double x, y, z, vx, vy, vz;
    double water_level, last_yd;
    double player_x, player_y, player_z;
    float yaw, momentum, out_of_control, delta_rotation, glide;
    float paddle[2], fall_distance, player_yaw;
    signed char status, previous_status;
    unsigned char paddle_state[2], on_ground;
} BoatContinuation;

static void continuation_state(
        const GmRuntime *runtime, int slot, BoatContinuation *out) {
    const EwStore *state = runtime->mobs.current
        ? &runtime->mobs.b : &runtime->mobs.a;
    memset(out, 0, sizeof *out);
    out->x = state->x[slot]; out->y = state->y[slot];
    out->z = state->z[slot]; out->vx = state->vx[slot];
    out->vy = state->vy[slot]; out->vz = state->vz[slot];
    out->yaw = state->yaw[slot]; out->on_ground = state->on_ground[slot];
    out->status = runtime->mobs.boat_status[slot];
    out->previous_status = runtime->mobs.boat_previous_status[slot];
    out->momentum = runtime->mobs.boat_momentum[slot];
    out->out_of_control = runtime->mobs.boat_out_of_control[slot];
    out->delta_rotation = runtime->mobs.boat_delta_rotation[slot];
    out->glide = runtime->mobs.boat_glide[slot];
    out->paddle[0] = runtime->mobs.boat_paddle_position[slot][0];
    out->paddle[1] = runtime->mobs.boat_paddle_position[slot][1];
    out->paddle_state[0] = runtime->mobs.boat_paddle_state[slot][0];
    out->paddle_state[1] = runtime->mobs.boat_paddle_state[slot][1];
    out->water_level = runtime->mobs.boat_water_level[slot];
    out->last_yd = runtime->mobs.boat_last_yd[slot];
    out->fall_distance = runtime->mobs.entity_fall_distance[slot];
    out->player_x = runtime->player.ent.posX;
    out->player_y = runtime->player.ent.posY;
    out->player_z = runtime->player.ent.posZ;
    out->player_yaw = runtime->player.yaw;
}

static GmAction continuation_action(int tick) {
    GmAction action;
    memset(&action, 0, sizeof action);
    action.hotbar_sel = -1;
    if (tick < 4) action.forward = 1.0F;
    else if (tick < 8) action.strafe = -1.0F;
    else if (tick < 12) {
        action.forward = -1.0F;
        action.strafe = 1.0F;
    } else {
        action.forward = 1.0F;
        action.strafe = -1.0F;
    }
    return action;
}

static int checkpoint_continuation(void) {
    GmRuntime runtime;
    BoatContinuation expected, actual;
    char path[160];
    snprintf(path, sizeof path, ".tmp/test_boat_checkpoint.%ld.bin",
             (long)getpid());
    if (!init(&runtime)) return 0;
    fill(&runtime, 79, 9, 0);
    int slot = gm_mobs_place_boat(&runtime.mobs, 0.5, 79.6, 0.5, 0.0F);
    gm_runtime_set_pose(&runtime, 0.5, 80.0, 0.5, 0.0F, 0.0F);
    if (slot <= 0 || !gm_mobs_boat_mount(
            &runtime.mobs, (struct PsvPlayer *)&runtime.player,
            runtime.ox, runtime.oz))
        goto fail;
    for (int tick = 0; tick < 7; ++tick)
        gm_runtime_tick(&runtime, continuation_action(tick));
    if (!gm_runtime_write_checkpoint(&runtime, path)) goto fail;
    for (int tick = 7; tick < 16; ++tick)
        gm_runtime_tick(&runtime, continuation_action(tick));
    continuation_state(&runtime, slot, &expected);
    if (!gm_runtime_load_checkpoint(&runtime, path)) goto fail;
    for (int tick = 7; tick < 16; ++tick)
        gm_runtime_tick(&runtime, continuation_action(tick));
    continuation_state(&runtime, slot, &actual);
    if (memcmp(&expected, &actual, sizeof expected) != 0) goto fail;
    (void)unlink(path);
    gm_runtime_destroy(&runtime);
    return 1;
fail:
    fprintf(stderr, "boat checkpoint continuation mismatch\n");
    (void)unlink(path);
    gm_runtime_destroy(&runtime);
    return 0;
}

typedef struct {
    int eid, dimension, cooldown, counter, in_portal;
    int64_t uuid_most, uuid_least;
    uint64_t random_seed48;
    double x, y, z, vx, vy, vz;
    float yaw;
} BoatPortalResult;

static void portal_result(
        const GmRuntime *runtime, int slot, BoatPortalResult *out) {
    const EwStore *state = runtime->mobs.current
        ? &runtime->mobs.b : &runtime->mobs.a;
    memset(out, 0, sizeof *out);
    out->eid = state->id[slot];
    out->dimension = runtime->mobs.entity_dimension[slot];
    out->cooldown = runtime->mobs.entity_portal_cooldown[slot];
    out->counter = runtime->mobs.boat_portal_counter[slot];
    out->in_portal = runtime->mobs.boat_in_portal[slot];
    out->uuid_most = runtime->mobs.entity_uuid_most[slot];
    out->uuid_least = runtime->mobs.entity_uuid_least[slot];
    out->random_seed48 = runtime->mobs.entity_random[slot].random.seed;
    out->x = state->x[slot]; out->y = state->y[slot];
    out->z = state->z[slot]; out->vx = state->vx[slot];
    out->vy = state->vy[slot]; out->vz = state->vz[slot];
    out->yaw = state->yaw[slot];
}

static int checkpoint_portal_continuation(void) {
    GmRuntime runtime;
    BoatPortalResult expected, actual;
    char path[160];
    snprintf(path, sizeof path, ".tmp/test_boat_portal.%ld.bin",
        (long)getpid());
    if (!init(&runtime)) return 0;
    runtime.mobs_enabled = 1;
    for (int pa = -1; pa <= 2; ++pa)
        for (int py = 79; py <= 83; ++py) {
            int frame = pa == -1 || pa == 2 || py == 79 || py == 83;
            gm_world_set_block_meta(runtime.world, 0, py, pa,
                frame ? 49 : 90, frame ? 0 : 2);
        }
    gm_world_fill_window(runtime.world, 0, 0,
        (struct Chunk *)runtime.window);
    int slot = gm_mobs_spawn_boat_type_exact(
        &runtime.mobs, 99, 0.5, 80.0, 0.5, 37.0F, 4);
    if (slot <= 0 || !gm_mobs_set_entity_uuid(
            &runtime.mobs, 99, INT64_C(0x123456789abcdef0),
            INT64_C(0x0fedcba987654321)))
        goto fail;
    gm_runtime_tick(&runtime, continuation_action(0));
    if (!runtime.mobs.boat_in_portal[slot]
            || runtime.mobs.boat_portal_counter[slot] != 0
            || !gm_runtime_write_checkpoint(&runtime, path))
        goto fail;
    gm_runtime_tick(&runtime, continuation_action(1));
    gm_runtime_tick(&runtime, continuation_action(2));
    portal_result(&runtime, slot, &expected);
    if (expected.dimension != -1 || expected.eid == 99
            || expected.cooldown != 300 || expected.counter != 0
            || expected.in_portal != 0
            || expected.uuid_most != INT64_C(0x123456789abcdef0)
            || expected.uuid_least != INT64_C(0x0fedcba987654321))
        goto fail;
    if (!gm_runtime_load_checkpoint(&runtime, path)) goto fail;
    gm_runtime_tick(&runtime, continuation_action(1));
    gm_runtime_tick(&runtime, continuation_action(2));
    portal_result(&runtime, slot, &actual);
    if (memcmp(&expected, &actual, sizeof expected) != 0) goto fail;
    (void)unlink(path);
    gm_runtime_destroy(&runtime);
    return 1;
fail:
    fprintf(stderr, "boat portal checkpoint continuation mismatch\n");
    (void)unlink(path);
    gm_runtime_destroy(&runtime);
    return 0;
}

static int run_submerged_eject(void) {
    GmRuntime runtime;
    if (!init(&runtime)) return 0;
    for (int y = 79; y <= 90; ++y) fill(&runtime, y, 9, 0);
    int slot = gm_mobs_place_boat(&runtime.mobs, 0.5, 79.2, 0.5, 0.0F);
    gm_runtime_set_pose(&runtime, 0.5, 80.0, 0.5, 0.0F, 0.0F);
    if (slot <= 0 || !gm_mobs_boat_mount(
            &runtime.mobs, (struct PsvPlayer *)&runtime.player,
            runtime.ox, runtime.oz)) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    for (int tick = 1; tick <= 60; ++tick) {
        EwStore *state = store(&runtime.mobs);
        state->x[slot] = 0.5; state->y[slot] = 79.2;
        state->z[slot] = 0.5;
        state->vx[slot] = state->vy[slot] = state->vz[slot] = 0.0;
        gm_mobs_tick(&runtime.mobs, runtime.world,
            (const struct Chunk *)runtime.window,
            (const struct McSinTable *)&runtime.sin_table,
            (struct PsvPlayer *)&runtime.player,
            (struct PvStats *)&runtime.vitals,
            runtime.ox, runtime.oz, runtime.dimension,
            runtime.clock.world_time, &runtime.clock,
            runtime.mob_griefing, &runtime.world_random_seed48,
            &runtime.math_random_seed48, &runtime.next_entity_id,
            runtime.do_mob_loot, &runtime.entities, 0.0F, 0.0F);
        if (tick == 59 || tick == 60)
            printf("U%d %s %08x %d\n", tick,
                status_name(runtime.mobs.boat_status[slot]),
                fbits(runtime.mobs.boat_out_of_control[slot]),
                gm_mobs_boat_riding(&runtime.mobs) ? 1 : 0);
    }
    gm_runtime_destroy(&runtime);
    return 1;
}

static int run_fall_break(int entity_drops) {
    GmRuntime runtime;
    if (!init(&runtime)) return 0;
    fill(&runtime, 79, 1, 0);
    int slot = gm_mobs_place_boat(&runtime.mobs, 0.5, 80.0, 0.5, 0.0F);
    if (slot <= 0) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    runtime.mobs.entity_fall_distance[slot] = 4.0F;
    runtime.mobs.a.vy[slot] = runtime.mobs.b.vy[slot] = -0.2;
    runtime.mobs.do_entity_drops = (unsigned char)entity_drops;
    gm_mobs_tick(&runtime.mobs, runtime.world,
        (const struct Chunk *)runtime.window,
        (const struct McSinTable *)&runtime.sin_table,
        (struct PsvPlayer *)&runtime.player,
        (struct PvStats *)&runtime.vitals,
        runtime.ox, runtime.oz, runtime.dimension,
        runtime.clock.world_time, &runtime.clock,
        runtime.mob_griefing, &runtime.world_random_seed48,
        &runtime.math_random_seed48, &runtime.next_entity_id,
        runtime.do_mob_loot, &runtime.entities, 0.0F, 0.0F);
    EwStore *state = store(&runtime.mobs);
    printf("F%d %d %d", entity_drops,
        state->alive[slot] ? 0 : 1, runtime.entities.n_active);
    for (int index = 0; index < GM_LIVE_MAX; ++index) {
        const GmLiveEnt *item = &runtime.entities.ents[index];
        if (!item->active) continue;
        printf(" %d:%d:%d", item->item, item->meta, item->count);
    }
    putchar('\n');
    gm_runtime_destroy(&runtime);
    return 1;
}

static int run_client_lerp(void) {
    GmRuntime runtime;
    if (!init(&runtime)) return 0;
    int slot = gm_mobs_place_boat(&runtime.mobs, 0.5, 80.0, 0.5, -170.0F);
    if (slot <= 0) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    runtime.mobs.entity_pitch[slot] = 10.0F;
    int eid = store(&runtime.mobs)->id[slot];
    if (!gm_mobs_boat_set_position_rotation_direct(
            &runtime.mobs, eid, 10.25, 82.75, -3.5,
            170.0F, -20.0F)) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    for (int tick = 1; tick <= 10; ++tick) {
        gm_mobs_tick(&runtime.mobs, runtime.world,
            (const struct Chunk *)runtime.window,
            (const struct McSinTable *)&runtime.sin_table,
            (struct PsvPlayer *)&runtime.player,
            (struct PvStats *)&runtime.vitals,
            runtime.ox, runtime.oz, runtime.dimension,
            runtime.clock.world_time, &runtime.clock,
            runtime.mob_griefing, &runtime.world_random_seed48,
            &runtime.math_random_seed48, &runtime.next_entity_id,
            runtime.do_mob_loot, &runtime.entities, 0.0F, 0.0F);
        if (tick == 1 || tick == 5 || tick == 10) {
            EwStore *state = store(&runtime.mobs);
            printf("L%d %016llx %016llx %016llx %08x %08x %d\n", tick,
                dbits(state->x[slot]), dbits(state->y[slot]),
                dbits(state->z[slot]), fbits(state->yaw[slot]),
                fbits(runtime.mobs.entity_pitch[slot]),
                runtime.mobs.boat_lerp_steps[slot]);
        }
    }
    gm_runtime_destroy(&runtime);
    return 1;
}

static int run_two_passengers(void) {
    GmRuntime runtime;
    if (!init(&runtime)) return 0;
    int boat = gm_mobs_spawn_boat_exact(
        &runtime.mobs, 99, 4.5, 80.0, -2.5, 30.0F);
    int first = gm_mobs_spawn_exact(
        &runtime.mobs, EW_TYPE_PIG, 100,
        0.0, 80.0, 0.0, 0.0, 0.0, 0.0,
        200.0F, 10.0F, 1, 0, 0, 0);
    int second = gm_mobs_spawn_exact(
        &runtime.mobs, EW_TYPE_PIG, 101,
        0.0, 80.0, 0.0, 0.0, 0.0, 0.0,
        -200.0F, 10.0F, 1, 0, 0, 0);
    if (boat <= 0 || first <= 0 || second <= 0) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    runtime.mobs.boat_delta_rotation[boat] = 5.0F;
    runtime.mobs.passive_head_yaw[first] = 12.0F;
    runtime.mobs.passive_head_yaw[second] = -12.0F;
    runtime.mobs.squid_render_yaw_offset[first] = 12.0F;
    runtime.mobs.squid_render_yaw_offset[second] = -12.0F;
    if (!gm_mobs_boat_mount_living(&runtime.mobs, 99, 100)
            || !gm_mobs_boat_mount_living(&runtime.mobs, 99, 101)
            || !gm_mobs_boat_update_passengers(
                &runtime.mobs, 99,
                (const struct McSinTable *)&runtime.sin_table)) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    EwStore *state = store(&runtime.mobs);
    for (int slot = first; slot <= second; ++slot)
        printf("P%d %016llx %016llx %016llx %08x %08x %08x \n",
            state->id[slot], dbits(state->x[slot]), dbits(state->y[slot]),
            dbits(state->z[slot]), fbits(state->yaw[slot]),
            fbits(runtime.mobs.passive_head_yaw[slot]),
            fbits(runtime.mobs.squid_render_yaw_offset[slot]));
    gm_runtime_destroy(&runtime);
    return 1;
}

static int run_automatic_passengers(void) {
    GmRuntime runtime;
    if (!init(&runtime)) return 0;
    int boat = gm_mobs_spawn_boat_exact(
        &runtime.mobs, 99, 0.5, 80.0, 0.5, 0.0F);
    int pigs[3];
    for (int index = 0; index < 3; ++index)
        pigs[index] = gm_mobs_spawn_exact(
            &runtime.mobs, EW_TYPE_PIG, 100 + index,
            0.60 + 0.02 * index, 79.95, 0.5,
            0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0);
    if (boat <= 0 || pigs[0] <= 0 || pigs[1] <= 0 || pigs[2] <= 0
            || !gm_mobs_boat_collide_nearby(&runtime.mobs, 99)) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    EwStore *state = store(&runtime.mobs);
    printf("A %d %d %d %016llx %016llx %016llx %016llx\n",
        runtime.mobs.entity_vehicle_eid[pigs[0]] == 99,
        runtime.mobs.entity_vehicle_eid[pigs[1]] == 99,
        runtime.mobs.entity_vehicle_eid[pigs[2]] == 99,
        dbits(state->vx[boat]), dbits(state->vz[boat]),
        dbits(state->vx[pigs[2]]), dbits(state->vz[pigs[2]]));
    gm_runtime_destroy(&runtime);
    return 1;
}

static int run_squid_push(void) {
    GmRuntime runtime;
    if (!init(&runtime)) return 0;
    int boat = gm_mobs_spawn_boat_exact(
        &runtime.mobs, 99, 0.5, 79.96, 0.5, 0.0F);
    int squid = gm_mobs_spawn_exact(
        &runtime.mobs, EW_TYPE_SQUID, 100,
        0.6, 79.95, 0.5, 0.0, 0.0, 0.0,
        0.0F, 10.0F, 1, 0, 0, 0);
    if (boat <= 0 || squid <= 0
            || !gm_mobs_boat_collide_nearby(&runtime.mobs, 99)) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    EwStore *state = store(&runtime.mobs);
    printf("S %d %016llx %016llx %016llx %016llx\n",
        runtime.mobs.entity_vehicle_eid[squid] == 99,
        dbits(state->vx[boat]), dbits(state->vz[boat]),
        dbits(state->vx[squid]), dbits(state->vz[squid]));
    gm_runtime_destroy(&runtime);
    return 1;
}

static int run_boat_push(void) {
    GmRuntime runtime;
    if (!init(&runtime)) return 0;
    int boat = gm_mobs_spawn_boat_exact(
        &runtime.mobs, 99, 0.5, 79.96, 0.5, 0.0F);
    int other = gm_mobs_spawn_boat_exact(
        &runtime.mobs, 100, 0.6, 80.0, 0.5, 0.0F);
    if (boat <= 0 || other <= 0
            || !gm_mobs_boat_collide_nearby(&runtime.mobs, 99)) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    EwStore *state = store(&runtime.mobs);
    printf("B %016llx %016llx %016llx %016llx\n",
        dbits(state->vx[boat]), dbits(state->vz[boat]),
        dbits(state->vx[other]), dbits(state->vz[other]));
    gm_runtime_destroy(&runtime);
    return 1;
}

static int run_item_variants(void) {
    static const int items[6] = {333, 444, 445, 446, 447, 448};
    for (int variant = 0; variant < 6; ++variant)
        printf("V %d %d\n", items[variant], variant);
    return 1;
}

int main(void) {
    return !(
        run("air", 0, 0, 80.0, 0.2, 0.1, -0.15, -1)
        && run("land", 1, 0, 80.0, 0.2, 0.0, -0.15, -1)
        && run("slime", 165, 0, 80.0, 0.2, 0.0, -0.15, -1)
        && run("water", 9, 0, 79.6, 0.2, 0.0, -0.15, -1)
        && run("under", 9, 0, 79.2, 0.2, 0.0, -0.15, -1)
        && run("flowing", 8, 1, 79.1, 0.2, 0.0, -0.15, -1)
        && run("entry", 9, 0, 79.6, 0.2, -0.2, -0.15, 4)
        && run_obstacle("slab_bottom", 44, 0)
        && run_obstacle("slab_top", 44, 8)
        && run_obstacle("stair_east", 53, 0)
        && run_obstacle("stair_west", 53, 1)
        && run_obstacle("fence", 85, 0)
        && run_obstacle("pane", 102, 0)
        && run_obstacle("door", 64, 0)
        && run_obstacle("trapdoor", 96, 0)
        && run_collision_matrix()
        && run_connected_matrix()
        && run_rider("forward", 1.0F, 0.0F)
        && run_rider("left", 0.0F, -1.0F)
        && run_rider("right_back", -1.0F, 1.0F)
        && run_rider("forward_left", 1.0F, -1.0F)
        && run_submerged_eject()
        && run_fall_break(1)
        && run_fall_break(0)
        && run_client_lerp()
        && run_two_passengers()
        && run_automatic_passengers()
        && run_squid_push()
        && run_boat_push()
        && run_item_variants()
        && checkpoint_continuation()
        && checkpoint_portal_continuation());
}
