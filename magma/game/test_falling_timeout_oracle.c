#include "game/runtime.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned packed_block(const GmRuntime *r, int x, int y, int z)
{
    return (unsigned)(gm_world_block(r->world, x, y, z) << 4)
        | (unsigned)gm_world_meta(r->world, x, y, z);
}

static void write_item(const GmLiveEnt *item)
{
    printf("{\"eid\":%d,\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
           "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,\"yaw\":%.9g,"
           "\"item\":%d,\"count\":%d,\"meta\":%d,\"age\":%d,"
           "\"pickup_delay\":%d}",
        item->eid, item->x, item->y, item->z,
        item->mx, item->my, item->mz, (double)item->yaw,
        item->item, item->count, item->meta, item->age,
        item->pickup_delay);
}

int main(int argc, char **argv)
{
    const char *mode = argc >= 2 ? argv[1] : "height";
    const int height = strcmp(mode, "height") == 0;
    const int low = strcmp(mode, "low") == 0;
    const int age = strcmp(mode, "age") == 0;
    const int entity_drops = argc >= 3 ? atoi(argv[2]) : 1;
    const int fall_time = age ? 600 : 100;
    const int snapshot_y = height ? 250 : (low ? 1 : 128);
    const double constructor_y = (height ? 258.0 : (low ? 1.0 : 128.0))
        + (double)((1.0f - 0.98f) / 2.0f);
    unsigned before[27];
    GmConfig cfg;
    GmRuntime r;
    GmAction idle;
    char err[256];

    if ((!height && !low && !age)
            || (entity_drops != 0 && entity_drops != 1)) {
        fprintf(stderr, "usage: %s height|low|age 0|1\n", argv[0]);
        return 2;
    }
    gm_config_defaults(&cfg);
    cfg.view_distance = 1;
    cfg.mobs = 0;
    cfg.daylight = 0;
    cfg.render = GM_RENDER_OFF;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    if (!gm_runtime_init(&r, &cfg, err, sizeof err)) {
        fprintf(stderr, "runtime init: %s\n", err);
        return 1;
    }
    memset(&r.entities, 0, sizeof r.entities);
    if (low)
        for (int clear_y = 0; clear_y <= 2; ++clear_y)
            gm_world_set_block_meta(r.world, 26, clear_y, 8, 0, 0);
    int cell = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx)
                before[cell++] = packed_block(
                    &r, 26 + dx, snapshot_y + dy, 8 + dz);
    if (!gm_runtime_set_do_entity_drops(&r, entity_drops)
            || !gm_runtime_set_entity_id_cursor(&r, 500001)
            || !gm_runtime_set_math_random_seed48(
                &r, UINT64_C(0x123456789ABC))
            || !gm_runtime_spawn_falling_fixture(
                &r, 500000, 12, 0, fall_time,
                26.5, constructor_y, 8.5, 0.0, 0.0, 0.0,
                age, 1)) {
        fprintf(stderr, "fixture setup failed\n");
        gm_runtime_destroy(&r);
        return 1;
    }
    gm_runtime_tick(&r, idle);
    GmRuntimeFallingBlock *falling = &r.falling_blocks[0];
    int unchanged = 1;
    cell = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx)
                unchanged = unchanged && before[cell++] == packed_block(
                    &r, 26 + dx, snapshot_y + dy, 8 + dz);
    if (falling->active || r.falling_block_count != 0
            || r.entities.n_active != (entity_drops ? 1 : 0)) {
        fprintf(stderr, "timeout lifecycle mismatch\n");
        gm_runtime_destroy(&r);
        return 1;
    }
    printf("{\"mode\":\"%s\",\"initial_fall_time\":%d,"
           "\"no_gravity\":%s,\"rows\":[[1,%d,true,"
           "%.17g,%.17g,%.17g,%.17g,%.17g,%.17g]],"
           "\"ticked_item\":",
        mode, fall_time, age ? "true" : "false", falling->fall_time,
        falling->x, falling->y, falling->z,
        falling->vx, falling->vy, falling->vz);
    if (entity_drops)
        write_item(&r.entities.ents[0]);
    else
        printf("null");
    printf(",\"blocks_unchanged\":%s,\"math_seed48\":%llu,"
           "\"next_entity_id\":%d}\n",
        unchanged ? "true" : "false",
        (unsigned long long)r.math_random_seed48, r.next_entity_id);
    gm_runtime_destroy(&r);
    return 0;
}
