#include "game/runtime.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int step;
    int fall_time;
    int dead;
    double x, y, z;
    double vx, vy, vz;
    int on_ground;
    int collided_horizontally;
    int collided_vertically;
    float fall_distance;
} FallingRow;

int main(int argc, char **argv)
{
    const char *fixture = argc >= 2 ? argv[1] : "free";
    const int wall = strcmp(fixture, "wall") == 0;
    const int free_motion = strcmp(fixture, "free") == 0;
    const int origin_x = argc >= 3 ? atoi(argv[2]) : 26;
    const int origin_z = argc >= 4 ? atoi(argv[3]) : 8;
    const int base_y = 220;
    const int min_dx = -2, max_dx = 9;
    const int min_dz = -2, max_dz = 2;
    const int min_y = base_y - 5, max_y = base_y + 1;
    const double initial_vx = wall ? 0.75 : 0.35;
    const double initial_vz = wall ? 0.0 : 0.15;
    const double constructor_y = (double)base_y
        + (double)((1.0f - 0.98f) / 2.0f);
    FallingRow rows[20];
    int row_count = 0;
    GmConfig cfg;
    GmRuntime r;
    GmAction idle;
    char err[256];

    if (!wall && !free_motion) {
        fprintf(stderr, "usage: %s free|wall [origin_x origin_z]\n", argv[0]);
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
    memset(r.falling_blocks, 0, sizeof r.falling_blocks);
    r.falling_block_count = 0;
    r.scheduled_tick_count = 0;
    r.scheduled_tick_next_order = 0;
    for (int y = min_y; y <= max_y; ++y)
        for (int z = origin_z + min_dz; z <= origin_z + max_dz; ++z)
            for (int x = origin_x + min_dx; x <= origin_x + max_dx; ++x)
                gm_world_set_block_meta(r.world, x, y, z, 0, 0);
    for (int x = origin_x + min_dx; x <= origin_x + max_dx; ++x)
        for (int z = origin_z + min_dz; z <= origin_z + max_dz; ++z)
            gm_world_set_block_meta(r.world, x, base_y - 4, z, 1, 0);
    if (wall)
        for (int y = base_y - 1; y <= base_y + 1; ++y)
            gm_world_set_block_meta(r.world, origin_x + 1, y, origin_z, 1, 0);
    gm_world_set_block_meta(r.world, origin_x, base_y, origin_z, 12, 0);
    if (!gm_runtime_set_math_random_seed48(&r, UINT64_C(0x123456789ABC))
            || !gm_runtime_set_world_random_seed48(
                &r, UINT64_C(0x23456789ABCD))
            || !gm_runtime_set_entity_id_cursor(&r, 510001)
            || !gm_runtime_schedule_tick(
                &r, origin_x + max_dx, base_y - 4, origin_z + max_dz,
                1, r.clock.total_time + 15, 0, 0)
            || !gm_runtime_spawn_falling_fixture(
                &r, 510000, 12, 0, 0,
                (double)origin_x + 0.5, constructor_y,
                (double)origin_z + 0.5,
                initial_vx, 0.0, initial_vz, 0, 0)) {
        fprintf(stderr, "fixture setup failed\n");
        gm_runtime_destroy(&r);
        return 1;
    }
    for (int step = 1; step <= 20; ++step) {
        GmRuntimeFallingBlock *falling = &r.falling_blocks[0];
        gm_runtime_tick(&r, idle);
        rows[row_count++] = (FallingRow){
            step, falling->fall_time, !falling->active,
            falling->x, falling->y, falling->z,
            falling->vx, falling->vy, falling->vz,
            falling->on_ground,
            falling->collided_horizontally,
            falling->collided_vertically,
            falling->fall_distance
        };
        if (!falling->active)
            break;
    }

    printf("{\"fixture\":\"%s\",\"origin_x\":%d,\"origin_z\":%d,"
           "\"base_y\":%d,\"initial_vx\":%.17g,\"initial_vz\":%.17g,"
           "\"rows\":[",
        fixture, origin_x, origin_z, base_y, initial_vx, initial_vz);
    for (int i = 0; i < row_count; ++i) {
        const FallingRow *row = &rows[i];
        if (i) putchar(',');
        printf("[%d,%d,%s,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,"
               "%s,%s,%s,%.9g]",
            row->step, row->fall_time, row->dead ? "true" : "false",
            row->x, row->y, row->z, row->vx, row->vy, row->vz,
            row->on_ground ? "true" : "false",
            row->collided_horizontally ? "true" : "false",
            row->collided_vertically ? "true" : "false",
            (double)row->fall_distance);
    }
    printf("],\"final_blocks\":[");
    int first = 1;
    for (int y = min_y; y <= max_y; ++y)
        for (int z = origin_z + min_dz; z <= origin_z + max_dz; ++z)
            for (int x = origin_x + min_dx; x <= origin_x + max_dx; ++x) {
                int block = gm_world_block(r.world, x, y, z);
                if (block == 0) continue;
                if (!first) putchar(',');
                first = 0;
                printf("[%d,%d,%d,%d,%d]", x, y, z, block,
                    gm_world_meta(r.world, x, y, z));
            }
    printf("],\"scheduled\":[");
    for (int i = 0; i < gm_runtime_scheduled_tick_count(&r); ++i) {
        GmRuntimeScheduledTick pending;
        if (!gm_runtime_scheduled_tick_get(&r, i, &pending)) {
            gm_runtime_destroy(&r);
            return 1;
        }
        if (i) putchar(',');
        printf("[%d,%d,%d,%d,%lld,%d,%lld]",
            pending.x, pending.y, pending.z, pending.block,
            pending.time - r.clock.total_time, pending.priority,
            pending.order);
    }
    printf("],\"fixture_entities\":[],\"source_block\":%d,"
           "\"math_seed48\":%llu,\"world_seed48\":%llu,"
           "\"next_entity_id\":%d}\n",
        gm_world_block(r.world, origin_x, base_y, origin_z),
        (unsigned long long)r.math_random_seed48,
        (unsigned long long)r.world_random_seed48,
        r.next_entity_id);
    gm_runtime_destroy(&r);
    return 0;
}
