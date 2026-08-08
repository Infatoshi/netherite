#include "game/runtime.h"
#include "world/populate_mc.h"

#include <limits.h>
#include <stdio.h>

static int fail;
#define CHECK(c, m) do { \
    if (!(c)) { fprintf(stderr, "FAIL: %s\n", m); fail = 1; } \
} while (0)

static int claim_index(const GmRuntime *runtime,
                       const PopmcSwampWitch *site) {
    for (int i = 0; i < runtime->swamp_witch_count; ++i)
        if (runtime->swamp_witches[i].x == site->x
                && runtime->swamp_witches[i].y == site->y
                && runtime->swamp_witches[i].z == site->z)
            return i;
    return -1;
}

int main(void) {
    enum { START_CX = -447, START_CZ = 76, RADIUS = 3 };
    GmConfig cfg;
    GmRuntime runtime;
    PopmcSwampWitch expected[GM_RUNTIME_SWAMP_WITCHES];
    char err[256] = {0};

    gm_config_defaults(&cfg);
    cfg.seed = 0;
    cfg.world = GM_WORLD_DEFAULT;
    cfg.villages = 0;
    cfg.mobs = 1;
    cfg.weather = 0;
    cfg.view_distance = RADIUS;
    CHECK(gm_runtime_init(&runtime, &cfg, err, sizeof err), err);
    if (fail) return 1;

    gm_world_ensure(runtime.world, START_CX, START_CZ, RADIUS);
    gm_runtime_set_pose(&runtime,
        START_CX * 16 + 8.5, 64.0, START_CZ * 16 + 8.5, 0.0F, 0.0F);
    int count = popmc_swamp_witches(
        cfg.seed, (START_CX - RADIUS) * 16, (START_CZ - RADIUS) * 16,
        (START_CX + RADIUS + 1) * 16 - 1,
        (START_CZ + RADIUS + 1) * 16 - 1,
        expected, GM_RUNTIME_SWAMP_WITCHES);
    CHECK(count >= 1, "natural seed-0 swamp hut exposes a witch site");
    CHECK(gm_runtime_sync_swamp_witches(&runtime) == count,
          "cold synchronization materializes each nearby witch once");
    CHECK(runtime.swamp_witch_count == count,
          "runtime retains every claimed witch site");

    for (int i = 0; i < count; ++i) {
        int claimed = claim_index(&runtime, &expected[i]);
        CHECK(claimed >= 0, "exact centered structure spawn position is claimed");
        if (claimed < 0) continue;
        int eid = runtime.swamp_witches[claimed].eid;
        int slot = gm_mobs_find_slot_by_eid(&runtime.mobs, eid);
        const EwStore *store = runtime.mobs.current
            ? &runtime.mobs.b : &runtime.mobs.a;
        CHECK(slot > 0, "hut witch enters the live entity store");
        if (slot <= 0) continue;
        CHECK(store->type[slot] == GM_MOB_WITCH
                  && store->x[slot] == expected[i].x
                  && store->y[slot] == expected[i].y
                  && store->z[slot] == expected[i].z,
              "hut witch uses its distinct live type and exact position");
        CHECK(store->vx[slot] == expected[i].vx
                  && store->vy[slot] == expected[i].vy
                  && store->vz[slot] == expected[i].vz
                  && store->health[slot] == expected[i].health
                  && store->yaw[slot] == expected[i].yaw
                  && store->on_ground[slot] == expected[i].on_ground,
              "hut witch retains its pre-first-tick base state");
        CHECK(runtime.mobs.persistence_required[slot]
                      == expected[i].persistence
                  && runtime.mobs.fire_ticks[slot] == expected[i].fire
                  && runtime.mobs.entity_air[slot] == expected[i].air,
              "hut witch retains persistence, fire, and air state");
        {
            GmEntityView views[EW_MAX_ENTITIES];
            int nviews = gm_mobs_fill_views(
                &runtime.mobs, views, EW_MAX_ENTITIES);
            int found = 0;
            for (int v = 0; v < nviews; ++v)
                if (views[v].type == GM_MOB_WITCH
                        && views[v].x == (float)expected[i].x
                        && views[v].y == (float)expected[i].y
                        && views[v].z == (float)expected[i].z
                        && views[v].health == expected[i].health) {
                    found = 1;
                    break;
                }
            CHECK(found, "hut witch reaches the existing witch renderer");
        }
    }

    {
        int before = runtime.swamp_witch_count;
        runtime.swamp_witch_scan_x = INT_MIN;
        CHECK(gm_runtime_sync_swamp_witches(&runtime) == 0,
              "claimed hut witches do not respawn on a cold rescan");
        CHECK(runtime.swamp_witch_count == before,
              "witch claim state survives rescans independently of live slots");
    }

    gm_runtime_destroy(&runtime);
    if (fail) return 1;
    printf("swamp_witch_runtime: PASS witches=%d state=exact claim=once\n",
           count);
    return 0;
}
