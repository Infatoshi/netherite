#include "game/runtime.h"

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

typedef struct {
    GmRuntimeScheduledTick entry;
    long long delay;
} ScheduledRow;

static int snapshot_scheduled(
        const GmRuntime *r, ScheduledRow *rows, int capacity) {
    int count = gm_runtime_scheduled_tick_count(r);
    if (count > capacity)
        return -1;
    for (int i = 0; i < count; ++i) {
        if (!gm_runtime_scheduled_tick_get(r, i, &rows[i].entry))
            return -1;
        rows[i].delay = rows[i].entry.time - r->clock.total_time;
    }
    return count;
}

static void write_scheduled(const ScheduledRow *rows, int count) {
    putchar('[');
    for (int i = 0; i < count; ++i) {
        const GmRuntimeScheduledTick *entry = &rows[i].entry;
        if (i) putchar(',');
        printf("[%d,%d,%d,%d,%lld,%d,%d]",
            entry->x, entry->y, entry->z, entry->block,
            rows[i].delay, entry->priority, i);
    }
    putchar(']');
}

int main(int argc, char **argv) {
    const char *mode = argc >= 2 ? argv[1] : "fall";
    const int supported = strcmp(mode, "supported") == 0;
    const int fall = strcmp(mode, "fall") == 0;
    const int capacity = strcmp(mode, "capacity") == 0;
    const int origin_x = argc >= 3 ? atoi(argv[2]) : 26;
    const int origin_z = argc >= 4 ? atoi(argv[3]) : 8;
    const int base_y = 220;
    const int min_y = base_y - 5, max_y = base_y + 1;
    const int min_dx = -2, max_dx = 2, min_dz = -2, max_dz = 2;
    const uint64_t math_seed = UINT64_C(0x123456789ABC);
    const uint64_t world_seed = UINT64_C(0x23456789ABCD);
    const int next_entity_id = 520000;
    FallingRow rows[20];
    ScheduledRow on_added[4], after_support_loss[4], final_scheduled[4];
    int row_count = 0, on_added_count, after_support_loss_count = 0;
    int final_scheduled_count;
    GmConfig cfg;
    GmRuntime r;
    GmAction idle;
    char err[256];

    if (!supported && !fall && !capacity) {
        fprintf(stderr, "usage: %s supported|fall|capacity [origin_x origin_z]\n",
            argv[0]);
        return 2;
    }
    gm_config_defaults(&cfg);
    cfg.view_distance = 1;
    cfg.mobs = 0;
    cfg.daylight = 0;
    cfg.weather = 0;
    cfg.render = GM_RENDER_OFF;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    if (!gm_runtime_init(&r, &cfg, err, sizeof err)) {
        fprintf(stderr, "runtime init: %s\n", err);
        return 1;
    }
    gm_runtime_set_total_time(&r, 100);
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
    gm_world_set_block_meta(r.world, origin_x, base_y - 1, origin_z, 1, 0);
    if (!gm_runtime_set_block(&r, origin_x, base_y, origin_z, 122, 0)) {
        fprintf(stderr, "dragon egg placement failed\n");
        gm_runtime_destroy(&r);
        return 1;
    }
    on_added_count = snapshot_scheduled(&r, on_added, 4);
    if (on_added_count < 0) {
        gm_runtime_destroy(&r);
        return 1;
    }
    if (fall || capacity) {
        if (!gm_runtime_set_block(
                &r, origin_x, base_y - 1, origin_z, 0, 0)) {
            fprintf(stderr, "dragon egg support removal failed\n");
            gm_runtime_destroy(&r);
            return 1;
        }
        after_support_loss_count = snapshot_scheduled(
            &r, after_support_loss, 4);
        if (after_support_loss_count < 0) {
            gm_runtime_destroy(&r);
            return 1;
        }
    }
    if (!gm_runtime_set_math_random_seed48(&r, math_seed)
            || !gm_runtime_set_world_random_seed48(&r, world_seed)
            || !gm_runtime_set_entity_id_cursor(&r, next_entity_id)) {
        fprintf(stderr, "cursor restore failed\n");
        gm_runtime_destroy(&r);
        return 1;
    }
    if (capacity) {
        for (int slot = 0; slot < GM_RUNTIME_FALLING_BLOCKS; ++slot) {
            int fixture_x = origin_x - 8 + slot;
            int fixture_z = origin_z + 8;
            gm_world_set_block_meta(
                r.world, fixture_x, 200, fixture_z, 122, 0);
            if (gm_world_block(r.world, fixture_x, 200, fixture_z) != 122
                    || !gm_runtime_spawn_falling_fixture(
                    &r, 600000 + slot, 122, 0, 0,
                    fixture_x + 0.5, 200.0, fixture_z + 0.5,
                    0.0, 0.0, 0.0, 1, 1)) {
                fprintf(stderr, "falling pool setup failed\n");
                gm_runtime_destroy(&r);
                return 1;
            }
        }
    }
    for (int tick = 0; tick < 5; ++tick)
        gm_runtime_tick(&r, idle);
    if (fall) {
        for (;;) {
            GmRuntimeFallingBlock *falling = &r.falling_blocks[0];
            if (falling->fall_time <= 0 || row_count >= 20) {
                fprintf(stderr, "falling dragon egg row boundary failed\n");
                gm_runtime_destroy(&r);
                return 1;
            }
            rows[row_count++] = (FallingRow){
                falling->fall_time, falling->fall_time, !falling->active,
                falling->x, falling->y, falling->z,
                falling->vx, falling->vy, falling->vz,
                falling->on_ground, falling->collided_horizontally,
                falling->collided_vertically, falling->fall_distance
            };
            if (!falling->active)
                break;
            gm_runtime_tick(&r, idle);
        }
    }
    final_scheduled_count = snapshot_scheduled(&r, final_scheduled, 4);
    if (final_scheduled_count < 0) {
        gm_runtime_destroy(&r);
        return 1;
    }

    printf("{\"mode\":\"%s\",\"origin_x\":%d,\"origin_z\":%d,"
           "\"base_y\":%d,\"on_added_scheduled\":",
        mode, origin_x, origin_z, base_y);
    write_scheduled(on_added, on_added_count);
    printf(",\"after_support_loss_scheduled\":");
    write_scheduled(after_support_loss, after_support_loss_count);
    printf(",\"rows\":[");
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
    printf("],\"scheduled\":");
    write_scheduled(final_scheduled, final_scheduled_count);
    printf(",\"fixture_entities\":");
    if (fall)
        printf("[[%d,true,%.17g,%.17g,%.17g]]",
            next_entity_id, r.falling_blocks[0].x,
            r.falling_blocks[0].y, r.falling_blocks[0].z);
    else
        printf("[]");
    printf(",\"source_block\":%d,"
           "\"source_meta\":%d,\"math_seed48\":%llu,"
           "\"world_seed48\":%llu,\"next_entity_id\":%d}\n",
        gm_world_block(r.world, origin_x, base_y, origin_z),
        gm_world_meta(r.world, origin_x, base_y, origin_z),
        (unsigned long long)r.math_random_seed48,
        (unsigned long long)r.world_random_seed48,
        r.next_entity_id);
    gm_runtime_destroy(&r);
    return 0;
}
