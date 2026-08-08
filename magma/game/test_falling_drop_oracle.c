#include "game/runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const int block = argc >= 2 ? atoi(argv[1]) : 12;
    const int support = argc >= 3 ? atoi(argv[2]) : 44;
    const int support_meta = argc >= 4 ? atoi(argv[3]) : 0;
    const int entity_drops = argc >= 5 ? atoi(argv[4]) : 1;
    const int drop_step = support == 60 || support == 208 ? 10
        : support == 88 || support == 116 ? 11
        : support == 171 ? 13
        : support == 70 || support == 72 || support == 147 || support == 148
            ? 13
        : support == 78 ? (support_meta <= 0 ? 13
            : support_meta <= 4 ? 12 : 11) : 12;
    const int x = 26, support_y = 77, z = 8;
    GmConfig cfg;
    GmRuntime r;
    GmAction idle;
    char err[256];

    if (block != 12 && block != 13) {
        fprintf(stderr, "block must be sand (12) or gravel (13)\n");
        return 2;
    }
    if (entity_drops != 0 && entity_drops != 1) {
        fprintf(stderr, "entity_drops must be 0 or 1\n");
        return 2;
    }
    if ((support != 44 && support != 60 && support != 70 && support != 72
            && support != 78 && support != 88 && support != 92
            && support != 116 && support != 147 && support != 148
            && support != 171 && support != 208)
            || support_meta < 0 || support_meta > 7
            || (support != 60 && support != 78 && support_meta != 0)) {
        fprintf(stderr,
            "support must be a promoted shaped support or pressure plate, "
            "with metadata 0..7 where supported\n");
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
    gm_runtime_set_do_entity_drops(&r, entity_drops);
    gm_runtime_set_total_time(&r, 42);
    if (((support == 60 || support == 70 || support == 72 || support == 78
                    || support == 92 || support == 147 || support == 148
                    || support == 171)
                && !gm_runtime_load_block(&r, x, support_y - 1, z, 1, 0))
            || !gm_runtime_load_block(&r, x, support_y, z, support, support_meta)
            || !gm_runtime_load_block(&r, x, support_y + 1, z, 0, 0)
            || !gm_runtime_load_block(&r, x, support_y + 2, z, 0, 0)
            || !gm_runtime_load_block(&r, x, support_y + 3, z, block, 0)
            || !gm_runtime_set_entity_id_cursor(&r, 500000)
            || !gm_runtime_set_math_random_seed48(
                &r, UINT64_C(0x123456789ABC))
            || !gm_runtime_schedule_tick(
                &r, x, support_y + 3, z, block, 45, 0, 0)) {
        fprintf(stderr, "fixture setup failed\n");
        gm_runtime_destroy(&r);
        return 1;
    }
    gm_runtime_tick(&r, idle);
    gm_runtime_tick(&r, idle);
    printf("{\"rows\":[");
    for (int step = 1; step <= drop_step; ++step) {
        GmRuntimeFallingBlock *falling = &r.falling_blocks[0];
        gm_runtime_tick(&r, idle);
        printf("%s[%d,%d,%s,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g]",
            step == 1 ? "" : ",", step, falling->fall_time,
            falling->active ? "false" : "true",
            falling->x, falling->y, falling->z,
            falling->vx, falling->vy, falling->vz);
    }
    if (r.entities.n_active != ((support == 78 && support_meta == 0)
            || !entity_drops ? 0 : 1)) {
        fprintf(stderr, "unexpected item count: %d\n", r.entities.n_active);
        gm_runtime_destroy(&r);
        return 1;
    }
    printf("],\"ticked_item\":");
    if (r.entities.n_active == 1) {
        GmLiveEnt *item = &r.entities.ents[0];
        printf("{\"eid\":%d,\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
               "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,\"yaw\":%.9g,"
               "\"item\":%d,\"count\":%d,\"meta\":%d,\"age\":%d,"
               "\"pickup_delay\":%d}",
            item->eid, item->x, item->y, item->z,
            item->mx, item->my, item->mz, (double)item->yaw,
            item->item, item->count, item->meta, item->age,
            item->pickup_delay);
    } else {
        printf("null");
    }
    printf(",\"source_block\":%d,\"source_meta\":%d,"
           "\"support_block\":%d,\"support_meta\":%d,"
           "\"math_seed48\":%llu,\"next_entity_id\":%d}\n",
        gm_world_block(r.world, x, support_y + 3, z),
        gm_world_meta(r.world, x, support_y + 3, z),
        gm_world_block(r.world, x, support_y, z),
        gm_world_meta(r.world, x, support_y, z),
        (unsigned long long)r.math_random_seed48, r.next_entity_id);
    gm_runtime_destroy(&r);
    return 0;
}
