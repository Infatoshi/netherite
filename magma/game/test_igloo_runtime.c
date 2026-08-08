#include "game/runtime.h"
#include "world/populate_mc.h"

#include <limits.h>
#include <stdio.h>

static int fail;
#define CHECK(c, m) do { \
    if (!(c)) { fprintf(stderr, "FAIL: %s\n", m); fail = 1; } \
} while (0)

static int claim_index(const GmRuntime *runtime,
                       const PopmcIglooResident *site) {
    for (int i = 0; i < runtime->igloo_resident_count; ++i) {
        const GmRuntimeIglooResident *resident = &runtime->igloo_residents[i];
        if (resident->x == site->x && resident->y == site->y
                && resident->z == site->z && resident->kind == site->kind)
            return i;
    }
    return -1;
}

int main(void) {
    enum { START_CX = -499, START_CZ = -171, RADIUS = 3 };
    GmConfig cfg;
    GmRuntime runtime;
    PopmcIglooResident expected[GM_RUNTIME_IGLOO_RESIDENTS];
    char err[256] = {0};
    int expected_count;

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
        START_CX * 16 + 8.5, 64.0, START_CZ * 16 + 8.5, 0.0f, 0.0f);
    expected_count = popmc_igloo_residents(
        cfg.seed, (START_CX - RADIUS) * 16, (START_CZ - RADIUS) * 16,
        (START_CX + RADIUS + 1) * 16 - 1,
        (START_CZ + RADIUS + 1) * 16 - 1,
        expected, GM_RUNTIME_IGLOO_RESIDENTS);
    CHECK(expected_count >= 2,
          "natural basement igloo exposes both resident sites");

    gm_runtime_tick(&runtime, (GmAction){.hotbar_sel = -1});
    CHECK(runtime.igloo_resident_count == expected_count,
          "production tick claims every nearby igloo resident");
    for (int i = 0; i < expected_count; ++i) {
        int claimed = claim_index(&runtime, &expected[i]);
        CHECK(claimed >= 0, "exact template entity position is claimed");
        if (claimed >= 0) {
            const GmRuntimeIglooResident *resident =
                &runtime.igloo_residents[claimed];
            int slot = gm_mobs_find_slot_by_eid(&runtime.mobs, resident->eid);
            const EwStore *store = runtime.mobs.current
                ? &runtime.mobs.b : &runtime.mobs.a;
            CHECK(slot > 0, "igloo resident enters live entity store");
            if (slot > 0) {
                int type = store->type[slot];
                CHECK(resident->vx == expected[i].vx
                          && resident->vy == expected[i].vy
                          && resident->vz == expected[i].vz
                          && resident->health == expected[i].health
                          && resident->yaw == expected[i].yaw
                          && resident->pitch == expected[i].pitch,
                      "claimed resident retains exact saved motion and pose");
                CHECK(store->vx[slot] == expected[i].vx
                          && store->vy[slot] == expected[i].vy
                          && store->vz[slot] == expected[i].vz
                          && store->health[slot] == expected[i].health
                          && store->yaw[slot] == expected[i].yaw
                          && store->on_ground[slot] == expected[i].on_ground,
                      "live entity store receives exact template state");
                CHECK(runtime.mobs.persistence_required[slot]
                              == expected[i].persistence
                          && runtime.mobs.entity_air[slot] == expected[i].air
                          && runtime.mobs.fire_ticks[slot] == expected[i].fire
                          && runtime.mobs.villager_profession[slot]
                              == expected[i].profession
                          && runtime.mobs.passive_head_yaw[slot]
                              == expected[i].yaw
                          && runtime.mobs.passive_head_pitch[slot]
                              == expected[i].pitch,
                      "live resident retains NBT persistence, air, fire, profession and head pose");
                CHECK((resident->kind == POPMC_IGLOO_RESIDENT_VILLAGER
                           && type == GM_MOB_VILLAGER
                           && runtime.mobs.villager_profession[slot]
                               == expected[i].profession)
                      || (resident->kind
                              == POPMC_IGLOO_RESIDENT_ZOMBIE_VILLAGER
                          && type == GM_MOB_ZOMBIE_VILLAGER
                          && runtime.mobs.zombie_villager_conversion_time[slot]
                              == expected[i].conversion_time),
                      "igloo resident materializes as its represented live kind");
                {
                    GmEntityView views[EW_MAX_ENTITIES];
                    int nviews = gm_mobs_fill_views(
                        &runtime.mobs, views, EW_MAX_ENTITIES);
                    int rendered = 0;
                    for (int v = 0; v < nviews; ++v)
                        if (views[v].type == type
                                && views[v].x == (float)expected[i].x
                                && views[v].y == (float)expected[i].y
                                && views[v].z == (float)expected[i].z
                                && views[v].item_id == expected[i].profession
                                && views[v].yaw == expected[i].yaw
                                && views[v].pitch == expected[i].pitch
                                && views[v].health == expected[i].health) {
                            rendered = 1;
                            break;
                        }
                    CHECK(rendered,
                          "live renderer receives resident type, profession and exact pose");
                }
            }
        }
    }

    {
        int priests = 0;
        for (int i = 0; i < runtime.village_resident_count; ++i) {
            const GmRuntimeVillageResident *resident =
                &runtime.village_residents[i];
            const GmVillagerTrade *trade = &resident->trade;
            if (resident->profession != 2) continue;
            ++priests;
            CHECK(trade->initialized && trade->career == 1
                      && trade->career_level == 1 && trade->wealth == 15
                      && trade->offer_count == 2,
                  "igloo priest retains fixed saved merchant state");
            CHECK(trade->offers[0].buy_a.item == 367
                      && trade->offers[0].buy_a.count == 36
                      && trade->offers[0].sell.item == 388
                      && trade->offers[1].buy_a.item == 266
                      && trade->offers[1].buy_a.count == 9
                      && trade->offers[1].sell.item == 388,
                  "igloo priest retains both template recipes");
        }
        CHECK(priests * 2 == expected_count,
              "one fixed priest accompanies each zombie-villager site");
    }

    {
        int before = runtime.igloo_resident_count;
        runtime.igloo_scan_x = INT_MIN;
        CHECK(gm_runtime_sync_igloo_residents(&runtime) == 0,
              "igloo resident synchronization is idempotent");
        CHECK(runtime.igloo_resident_count == before,
              "claimed igloo residents never respawn on rescan");
    }
    gm_runtime_destroy(&runtime);
    if (fail) return 1;
    printf("igloo_runtime: PASS residents=%d nbt=exact model=distinct merchant=fixed\n",
           expected_count);
    return 0;
}
