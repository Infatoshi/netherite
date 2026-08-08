#include "game/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); fail = 1; } } while (0)

static int init(GmRuntime *runtime) {
    GmConfig config;
    char error[256];
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.seed = 0;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(runtime, &config, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 0;
    }
    return 1;
}

static void seed_bottle(GmRuntime *runtime) {
    GmRuntimeProjectile *bottle = &runtime->projectiles[0];
    memset(bottle, 0, sizeof *bottle);
    bottle->active = 1;
    bottle->type = 9;
    bottle->x = 24.5;
    bottle->y = 220.0;
    bottle->z = 24.5;
}

int main(void) {
    GmRuntime runtime;
    GmRuntimeWorldEvent event;
    if (!init(&runtime)) return 1;
    gm_runtime_set_world_random_seed48(&runtime, UINT64_C(7));
    gm_runtime_set_math_random_seed48(&runtime, UINT64_C(19));
    gm_runtime_set_entity_id_cursor(&runtime, 730000);
    seed_bottle(&runtime);
    CHECK(gm_runtime_xp_bottle_impact_now(&runtime, 0) == 2,
          "XP bottle splits the seeded total into two exact orbs");
    CHECK(!runtime.projectiles[0].active
              && runtime.next_entity_id == 730002
              && runtime.world_random_seed48 == UINT64_C(33146457173913)
              && runtime.math_random_seed48 == UINT64_C(55458674866275),
          "XP bottle commits exact death, IDs, and both RNG cursors");
    CHECK(runtime.mobs.xp_orbs[0].xpValue == 7
              && runtime.mobs.xp_orbs[0].eid == 730000
              && runtime.mobs.xp_orbs[1].xpValue == 1
              && runtime.mobs.xp_orbs[1].eid == 730001,
          "XP bottle preserves Java split and loaded construction order");
    CHECK(gm_runtime_world_event_count(&runtime) == 1
              && gm_runtime_world_event_get(&runtime, 0, &event)
              && event.id == 2002 && event.x == 24
              && event.y == 220 && event.z == 24
              && event.data == 3694022,
          "XP bottle emits the exact water-potion world event");
    gm_runtime_destroy(&runtime);

    if (!init(&runtime)) return 1;
    gm_runtime_set_world_random_seed48(&runtime, UINT64_C(7));
    gm_runtime_set_math_random_seed48(&runtime, UINT64_C(19));
    gm_runtime_set_entity_id_cursor(&runtime, 730000);
    for (int i = 0; i < GM_XP_ORBS; ++i) {
        runtime.mobs.xp_orbs[i].dead = 0;
        runtime.mobs.xp_orbs[i].xpValue = 1;
    }
    seed_bottle(&runtime);
    CHECK(gm_runtime_xp_bottle_impact_now(&runtime, 0) == 2,
          "full hot XP pool grows for the complete bottle boundary");
    CHECK(!runtime.projectiles[0].active
              && runtime.mobs.xp_orb_cold_cap >= 2
              && runtime.mobs.xp_orb_cold[0].orb.xpValue == 7
              && runtime.mobs.xp_orb_cold[0].orb.eid == 730000
              && runtime.mobs.xp_orb_cold[1].orb.xpValue == 1
              && runtime.mobs.xp_orb_cold[1].orb.eid == 730001
              && runtime.next_entity_id == 730002
              && gm_runtime_world_event_count(&runtime) == 1,
          "full hot XP pool preserves split order in cold storage");
    gm_runtime_destroy(&runtime);

    if (!init(&runtime)) return 1;
    for (int i = 0; i < 512; ++i)
        CHECK(gm_mobs_spawn_xp_exact(
                  &runtime.mobs, 128.5 + i, 220.0, 128.5,
                  0.0, 0.0, 0.0, i + 1, 800000 + i,
                  i % 5999, 32767, i, -100),
              "XP cold store accepts an entry beyond loaded-order hot cap");
    {
        const char *checkpoint = "test_xp_capacity.checkpoint";
        const McOrb *last = gm_mobs_xp_orb_ref(&runtime.mobs, 511);
        int eid = 0, kind = 0, update_eid = 0;
        CHECK(last && last->eid == 800511 && last->xpValue == 512
                  && gm_mobs_loaded_order_count(&runtime.mobs) == 512
                  && gm_mobs_loaded_order_get(
                      &runtime.mobs, 511, &eid, &kind)
                  && eid == 800511 && kind == GM_MOB_LOADED_XP,
              "XP and authoritative loaded order both grow past hot caps");
        runtime.mobs.tick_update_order_count = 0;
        gm_mobs_tick_xp_from_eid(
            &runtime.mobs, runtime.world,
            (struct PsvPlayer *)&runtime.player,
            runtime.ox, runtime.oz, 800000);
        CHECK(runtime.mobs.tick_update_order_count == 512
                  && gm_mobs_tick_update_order_get(
                      &runtime.mobs, 511, &update_eid)
                  && update_eid == 800511,
              "XP update trace grows past its hot order cap");
        int wrote = gm_runtime_write_checkpoint(&runtime, checkpoint);
        int loaded = wrote
            && gm_runtime_load_checkpoint(&runtime, checkpoint);
        CHECK(wrote, "grown XP and loaded order checkpoint writes");
        CHECK(loaded, "grown XP and loaded order checkpoint reloads");
        last = gm_mobs_xp_orb_ref(&runtime.mobs, 511);
        eid = kind = update_eid = 0;
        CHECK(last && last->eid == 800511 && last->xpValue == 512
                  && gm_mobs_loaded_order_count(&runtime.mobs) == 512
                  && gm_mobs_loaded_order_get(
                      &runtime.mobs, 511, &eid, &kind)
                  && eid == 800511 && kind == GM_MOB_LOADED_XP,
              "grown XP payload and order survive checkpoint reload");
        CHECK(runtime.mobs.tick_update_order_count == 512
                  && gm_mobs_tick_update_order_get(
                      &runtime.mobs, 511, &update_eid)
                  && update_eid == 800511,
              "grown XP update trace survives checkpoint reload");
        (void)remove(checkpoint);
    }
    gm_runtime_destroy(&runtime);

    if (!init(&runtime)) return 1;
    gm_runtime_set_pose(&runtime, 8.5, 4.0, 8.5, 0.0F, 0.0F);
    gm_runtime_set_world_random_seed48(&runtime, UINT64_C(7));
    gm_runtime_set_math_random_seed48(&runtime, UINT64_C(19));
    gm_runtime_set_entity_id_cursor(&runtime, 730000);
    gm_world_set_block_meta(runtime.world, 25, 220, 24, 1, 0);
    seed_bottle(&runtime);
    runtime.projectiles[0].y = 220.5;
    runtime.projectiles[0].vx = 1.0;
    GmAction idle;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    gm_runtime_tick(&runtime, idle);
    CHECK(!runtime.projectiles[0].active
              && runtime.next_entity_id == 730002
              && runtime.mobs.xp_orbs[0].xpValue == 7
              && runtime.mobs.xp_orbs[1].xpValue == 1,
          "throwable ray impact reaches exact XP bottle boundary live");
    gm_runtime_destroy(&runtime);

    if (fail) return 1;
    fprintf(stderr, "xp_bottle_live: PASS\n");
    return 0;
}
