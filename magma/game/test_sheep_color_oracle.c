#include "game/mob_live.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sheep_slot(const GmMobLive *m, int eid) {
    const EwStore *s = m->current ? &m->b : &m->a;
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot)
        if (s->alive[slot] && s->id[slot] == eid) return slot;
    return -1;
}

int main(int argc, char **argv) {
    if (argc != 4
            || (strcmp(argv[1], "direct")
                && strcmp(argv[1], "initial_spawn"))) {
        fprintf(stderr, "usage: %s direct|initial_spawn SEED48 SHEARED\n",
            argv[0]);
        return 2;
    }
    uint64_t seed48 = strtoull(argv[2], NULL, 0);
    int sheared = atoi(argv[3]);
    if (seed48 >= (UINT64_C(1) << 48)
            || (sheared != 0 && sheared != 1))
        return 2;

    JavaRandom world_random;
    jrand_set_seed48(&world_random, seed48);
    int fleece;
    int final_sheared = 0;
    if (!strcmp(argv[1], "direct")) {
        fleece = gm_mobs_random_sheep_color(&world_random);
    } else {
        const int eid = 660000;
        GmMobLive mobs;
        gm_mobs_init(&mobs, 0);
        if (gm_mobs_spawn_exact(
                &mobs, EW_TYPE_SHEEP, eid, 0.5, 64.0, 0.5,
                0.0, 0.0, 0.0, 0.0F, 8.0F, 1, 0, 0, 0) < 0
                || !gm_mobs_set_sheep_state(&mobs, eid, 14, sheared)
                || !gm_mobs_sheep_on_initial_spawn(
                    &mobs, eid, &world_random))
            return 1;
        int slot = sheep_slot(&mobs, eid);
        if (slot < 0) return 1;
        fleece = mobs.sheep_data[slot] & 15;
        final_sheared = (mobs.sheep_data[slot] & 16) != 0;
    }

    printf("{\"ok\":true,\"mode\":\"%s\",\"fleece\":%d,"
           "\"sheared\":%s,\"world_seed48\":%" PRIu64 "}\n",
        argv[1], fleece, final_sheared ? "true" : "false",
        world_random.seed);
    return 0;
}
