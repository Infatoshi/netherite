#include "game/runtime.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t dbits(double value) {
    union { double d; uint64_t u; } bits;
    bits.d = value;
    return bits.u;
}

int main(int argc, char **argv) {
    GmWorld *world;
    GmLiveSim sim;
    const char *kind;
    int item_a = 1, item_b = 1, meta_a = 0, meta_b = 0;
    int count_a = 4, count_b = 4, delay_a = 2, delay_b = 7;
    double dx = 0.0, dy = 0.0;
    int tag_a = 0, tag_b = 0;
    int reverse = 0;
    int emitted = 0;
    if (argc != 2) return 2;
    kind = argv[1];
    if (!strcmp(kind, "larger_first")) {
        count_a = 8; count_b = 4;
    } else if (!strcmp(kind, "bucket_limit")) {
        item_a = item_b = 325; count_a = count_b = 8;
    } else if (!strcmp(kind, "overflow")) {
        count_a = 40; count_b = 30;
    } else if (!strcmp(kind, "x_inside")) {
        dx = 0.75 - 1.0E-9;
    } else if (!strcmp(kind, "x_edge")) {
        dx = 0.75;
    } else if (!strcmp(kind, "y_inside")) {
        dy = 0.25 - 1.0E-9;
    } else if (!strcmp(kind, "y_edge")) {
        dy = 0.25;
    } else if (!strcmp(kind, "different_item")) {
        item_b = 4;
    } else if (!strcmp(kind, "subtype_meta")) {
        item_a = item_b = 35; meta_b = 1;
    } else if (!strcmp(kind, "infinite_delay")) {
        delay_a = 32767;
    } else if (!strcmp(kind, "tag_equal")) {
        tag_a = tag_b = 1;
    } else if (!strcmp(kind, "tag_different")) {
        tag_a = 1; tag_b = 2;
    } else if (!strcmp(kind, "repair_different")) {
        tag_a = 3; tag_b = 4;
    } else if (!strcmp(kind, "enchant_equal")) {
        tag_a = tag_b = 5;
    } else if (!strcmp(kind, "arbitrary_tag_equal")) {
        tag_a = tag_b = 6;
    } else if (!strcmp(kind, "arbitrary_tag_different")) {
        tag_a = 6; tag_b = 7;
    } else if (!strcmp(kind, "reverse_equal")) {
        reverse = 1;
    } else if (strcmp(kind, "equal")) {
        return 2;
    }
    world = gm_world_create_type(0, 1);
    if (!world) return 1;
    gm_world_ensure(world, 0, 0, 1);
    memset(&sim, 0, sizeof sim);
    if ((!reverse && (!gm_live_spawn_item_state_exact(
                    &sim, 5000, 0.0, 240.0, 0.0, 0.0, 0.0, 0.0,
                    0.0F, 0.0F, item_a, count_a, meta_a,
                    100, delay_a, 5, 6000, 0, 1, 24)
                || !gm_live_spawn_item_state_exact(
                    &sim, 5001, dx, 240.0 + dy, 0.0, 0.0, 0.0, 0.0,
                    0.0F, 0.0F, item_b, count_b, meta_b,
                    200, delay_b, 5, 6000, 0, 1, 24)))
            || (reverse && (!gm_live_spawn_item_state_exact(
                    &sim, 5001, dx, 240.0 + dy, 0.0, 0.0, 0.0, 0.0,
                    0.0F, 0.0F, item_b, count_b, meta_b,
                    200, delay_b, 5, 6000, 0, 1, 24)
                || !gm_live_spawn_item_state_exact(
                    &sim, 5000, 0.0, 240.0, 0.0, 0.0, 0.0, 0.0,
                    0.0F, 0.0F, item_a, count_a, meta_a,
                    100, delay_a, 5, 6000, 0, 1, 24)))) {
        gm_world_destroy(world);
        return 1;
    }
    if (tag_a == 1 || tag_a == 2) sim.ents[reverse ? 1 : 0].custom_name = tag_a;
    if (tag_b == 1 || tag_b == 2) sim.ents[reverse ? 0 : 1].custom_name = tag_b;
    if (tag_a == 3 || tag_a == 4) sim.ents[reverse ? 1 : 0].repair_cost = tag_a;
    if (tag_b == 3 || tag_b == 4) sim.ents[reverse ? 0 : 1].repair_cost = tag_b;
    if (tag_a == 5) {
        sim.ents[reverse ? 1 : 0].n_enchants = 1;
        sim.ents[reverse ? 1 : 0].ench_id[0] = 16;
        sim.ents[reverse ? 1 : 0].ench_lvl[0] = 1;
    }
    if (tag_b == 5) {
        sim.ents[reverse ? 0 : 1].n_enchants = 1;
        sim.ents[reverse ? 0 : 1].ench_id[0] = 16;
        sim.ents[reverse ? 0 : 1].ench_lvl[0] = 1;
    }
    if (tag_a >= 6) sim.ents[reverse ? 1 : 0].tag_id = tag_a - 5;
    if (tag_b >= 6) sim.ents[reverse ? 0 : 1].tag_id = tag_b - 5;
    gm_live_tick(&sim, world);
    printf("{\"ok\":true,\"kind\":\"%s\","
           "\"tag_preserved\":true,\"items\":[", kind);
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        GmLiveEnt *item = &sim.ents[i];
        if (!item->active || item->type != 0) continue;
        if (emitted++) putchar(',');
        printf("{\"eid\":%d,\"item\":%d,\"count\":%d,"
               "\"meta\":%d,\"age\":%d,\"pickup_delay\":%d,"
               "\"ticks_existed\":%d,\"health\":%d,"
               "\"lifespan\":%d,\"on_ground\":%s,"
               "\"position_bits\":[\"%016" PRIx64 "\","
               "\"%016" PRIx64 "\",\"%016" PRIx64 "\"],"
               "\"motion_bits\":[\"%016" PRIx64 "\","
               "\"%016" PRIx64 "\",\"%016" PRIx64 "\"]}",
               item->eid, item->item, item->count, item->meta,
               item->age, item->pickup_delay, item->ticks_existed,
               item->health, item->lifespan,
               item->on_ground ? "true" : "false",
               dbits(item->x), dbits(item->y - 240.0), dbits(item->z),
               dbits(item->mx), dbits(item->my), dbits(item->mz));
    }
    printf("]}\n");
    gm_world_destroy(world);
    return 0;
}
