/* test_hazards: live environmental damage against Java 1.11.2 constants. */
#include "game/runtime.h"
#include "player_survival.h"
#include "player_vitals.h"
#include "inventory_stack_rules.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } } while (0)

static int init_flat(GmRuntime *r) {
    GmConfig c;
    char err[256];
    gm_config_defaults(&c);
    c.world = GM_WORLD_SUPERFLAT;
    c.view_distance = 1;
    if (!gm_runtime_init(r, &c, err, sizeof err)) {
        fprintf(stderr, "init: %s\n", err);
        return 0;
    }
    gm_runtime_set_pose(r, 8.5, 5.0, 8.5, 0.0f, 0.0f);
    return 1;
}

static GmAction idle_action(void) {
    GmAction a;
    memset(&a, 0, sizeof a);
    a.hotbar_sel = -1;
    return a;
}

int main(void) {
    GmRuntime r;
    GmAction idle = idle_action();

    /* ON_FIRE 1.0 through iron, fire%20==0. Entity.java:554-557. */
    if (!init_flat(&r)) return 1;
    CHECK(gm_runtime_set_inventory(&r, ISR_ARMOR_HEAD, 306, 1, 0), "iron head");
    CHECK(gm_runtime_set_inventory(&r, ISR_ARMOR_CHEST, 307, 1, 0), "iron chest");
    CHECK(gm_runtime_set_inventory(&r, ISR_ARMOR_LEGS, 308, 1, 0), "iron legs");
    CHECK(gm_runtime_set_inventory(&r, ISR_ARMOR_FEET, 309, 1, 0), "iron feet");
    r.player_fire_ticks = 20;
    r.player.fire = 20;
    gm_runtime_tick(&r, idle);
    CHECK(fabsf(r.vitals.health - 19.0f) < 1e-5f,
          "ON_FIRE deals full 1.0 through iron");
    gm_runtime_destroy(&r);

    /* Drown: air 300, first DROWN 2.0 at tick 320. EntityLivingBase.java:297-320. */
    if (!init_flat(&r)) return 1;
    {
        int x, z, t;
        for (x = 7; x <= 9; ++x)
            for (z = 7; z <= 9; ++z) {
                gm_world_set_block(r.world, x, 5, z, 9);
                gm_world_set_block(r.world, x, 6, z, 9);
            }
        gm_runtime_set_pose(&r, 8.5, 5.0, 8.5, 0.0f, 0.0f);
        r.player.air = 300;
        for (t = 0; t < 320; ++t)
            gm_runtime_tick(&r, idle);
        CHECK(r.player.air == 0, "air resets to 0 on first drown");
        CHECK(fabsf(r.vitals.health - 18.0f) < 1e-5f,
              "first DROWN deals 2.0");
    }
    gm_runtime_destroy(&r);

    /* Lava 4.0. Entity.java:609. */
    if (!init_flat(&r)) return 1;
    gm_world_set_block(r.world, 8, 5, 8, 11);
    gm_runtime_set_pose(&r, 8.5, 5.0, 8.5, 0.0f, 0.0f);
    gm_runtime_tick(&r, idle);
    CHECK(fabsf(r.vitals.health - 16.0f) < 1e-5f, "LAVA deals 4.0");
    CHECK(r.player.fire >= 300, "lava setFire(15)");
    gm_runtime_destroy(&r);

    /* Void 4.0. EntityLivingBase.java:1647-1649. */
    if (!init_flat(&r)) return 1;
    gm_runtime_set_pose(&r, 8.5, -65.0, 8.5, 0.0f, 0.0f);
    gm_runtime_tick(&r, idle);
    CHECK(fabsf(r.vitals.health - 16.0f) < 1e-5f, "OUT_OF_WORLD deals 4.0");
    gm_runtime_destroy(&r);

    /* IN_WALL 1.0. EntityLivingBase.java:268-271. */
    if (!init_flat(&r)) return 1;
    gm_world_set_block(r.world, 8, 6, 8, 1);
    gm_runtime_set_pose(&r, 8.5, 5.0, 8.5, 0.0f, 0.0f);
    gm_runtime_tick(&r, idle);
    CHECK(fabsf(r.vitals.health - 19.0f) < 1e-5f, "IN_WALL deals 1.0");
    gm_runtime_destroy(&r);

    /* Respawn restores health/food/air/fire. PlayerList.recreatePlayerEntity. */
    if (!init_flat(&r)) return 1;
    r.vitals.health = 0.0f;
    r.player.health = 0.0f;
    r.player.air = 10;
    r.player.fire = 40;
    r.dead = 1;
    gm_runtime_respawn(&r);
    CHECK(!r.dead && r.vitals.health == 20.0f && r.vitals.foodLevel == 20,
          "respawn restores health and food");
    CHECK(r.player.air == 300 && r.player.fire == 0,
          "respawn restores air 300 and extinguishes");
    gm_runtime_destroy(&r);

    if (fail) {
        fprintf(stderr, "hazards: FAIL\n");
        return 1;
    }
    fprintf(stderr, "hazards: PASS\n");
    return 0;
}
