#include "game/mob_live.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int mob_slot(const GmMobLive *m, int eid) {
    const EwStore *s = m->current ? &m->b : &m->a;
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot)
        if (s->alive[slot] && s->id[slot] == eid) return slot;
    return -1;
}

int main(int argc, char **argv) {
    if (argc != 12) {
        fprintf(stderr, "usage: %s SUBSTRATE X Y Z GRIEF SHEARED AGE "
                        "FLEECE ENTITY_SEED NEXT_ID UPDATES\n", argv[0]);
        return 2;
    }
    const char *substrate = argv[1];
    double x = strtod(argv[2], NULL), y = strtod(argv[3], NULL);
    double z = strtod(argv[4], NULL);
    int grief = atoi(argv[5]), sheared = atoi(argv[6]);
    int age = atoi(argv[7]), fleece = atoi(argv[8]);
    uint64_t entity_seed = strtoull(argv[9], NULL, 0);
    int eid = atoi(argv[10]), updates = atoi(argv[11]);
    if ((strcmp(substrate, "grass")
                && strcmp(substrate, "tallgrass")
                && strcmp(substrate, "tall_over_grass")
                && strcmp(substrate, "fern")
                && strcmp(substrate, "air"))
            || (grief != 0 && grief != 1)
            || (sheared != 0 && sheared != 1)
            || fleece < 0 || fleece > 15 || updates < 0 || updates > 40
            || entity_seed >= (UINT64_C(1) << 48) || eid <= 0)
        return 2;

    int bx = (int)floor(x), by = (int)floor(y), bz = (int)floor(z);
    GmWorld *world = gm_world_create(0);
    if (!world) return 1;
    gm_world_ensure(world, bx >> 4, bz >> 4, 1);
    gm_world_set_block_meta(world, bx, by, bz, 0, 0);
    gm_world_set_block_meta(world, bx, by - 1, bz, 1, 0);
    if (!strcmp(substrate, "grass")) {
        gm_world_set_block_meta(world, bx, by - 1, bz, 2, 0);
    } else if (!strcmp(substrate, "tallgrass")
            || !strcmp(substrate, "tall_over_grass")) {
        gm_world_set_block_meta(world, bx, by, bz, 31, 1);
        if (!strcmp(substrate, "tall_over_grass"))
            gm_world_set_block_meta(world, bx, by - 1, bz, 2, 0);
    } else if (!strcmp(substrate, "fern")) {
        gm_world_set_block_meta(world, bx, by, bz, 31, 2);
    }

    GmMobLive mobs;
    gm_mobs_init(&mobs, 0);
    mobs.active_dimension = 0;
    if (gm_mobs_spawn_exact(
                &mobs, EW_TYPE_SHEEP, eid, x, y, z,
                0.0, 0.0, 0.0, 0.0F, 8.0F, 1, 0, 0, 0) < 0
            || !gm_mobs_set_entity_random_state(
                &mobs, eid, entity_seed, 0, 0.0)
            || !gm_mobs_set_sheep_state(&mobs, eid, fleece, sheared)
            || !gm_mobs_set_growing_age(&mobs, eid, age)) {
        gm_world_destroy(world);
        return 1;
    }
    int should_execute = gm_mobs_sheep_graze_begin(&mobs, world, eid);
    int slot = mob_slot(&mobs, eid);
    if (slot < 0) {
        gm_world_destroy(world);
        return 1;
    }
    printf("{\"ok\":true,\"eid\":%d,"
           "\"world_x\":%d,\"world_y\":%d,\"world_z\":%d,"
           "\"should_execute\":%s,"
           "\"entity_seed48\":%" PRIu64 ",\"next_entity_id\":%d,"
           "\"rows\":[",
        eid, bx, by, bz, should_execute ? "true" : "false",
        mobs.entity_random[slot].random.seed, eid + 1);
    for (int update = 0; update <= updates; ++update) {
        if (update > 0 && should_execute)
            (void)gm_mobs_sheep_graze_update(
                &mobs, world, eid, grief);
        if (update) putchar(',');
        printf("{\"update\":%d,\"timer\":%d,"
               "\"source_block\":%d,\"source_meta\":%d,"
               "\"below_block\":%d,\"below_meta\":%d,"
               "\"sheared\":%s,\"growing_age\":%d,"
               "\"status_count\":%d,\"world_event_count\":%d}",
            update, gm_mobs_sheep_eat_timer(&mobs, eid),
            gm_world_block(world, bx, by, bz),
            gm_world_meta(world, bx, by, bz),
            gm_world_block(world, bx, by - 1, bz),
            gm_world_meta(world, bx, by - 1, bz),
            (mobs.sheep_data[slot] & 16) ? "true" : "false",
            mobs.growing_age[slot], gm_mobs_event_count(&mobs),
            mobs.sheep_world_event_pending[slot] ? 1 : 0);
    }
    fputs("],\"events\":[", stdout);
    for (int index = 0; index < gm_mobs_event_count(&mobs); ++index) {
        GmMobEvent event;
        if (!gm_mobs_event_get(&mobs, index, &event)) return 1;
        if (index) putchar(',');
        printf("{\"kind\":\"status\",\"eid\":%d,\"status\":%d}",
            event.eid, event.data);
    }
    fputs("],\"world_events\":[", stdout);
    int ex, ey, ez, event_data;
    if (gm_mobs_take_sheep_world_event(
            &mobs, &ex, &ey, &ez, &event_data))
        printf("{\"seq\":0,\"id\":2001,\"x\":%d,\"y\":%d,"
               "\"z\":%d,\"data\":%d}",
            ex, ey, ez, event_data);
    fputs("]}\n", stdout);
    gm_world_destroy(world);
    return 0;
}
