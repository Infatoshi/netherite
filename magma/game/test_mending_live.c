#include "game/runtime.h"

#include <stdio.h>
#include <string.h>

#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); return 1; } } while (0)

static int init_flat(GmRuntime *runtime)
{
    GmConfig config;
    char error[256];
    gm_config_defaults(&config);
    config.seed = 42;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(runtime, &config, error, sizeof error)) {
        fprintf(stderr, "FAIL: %s\n", error);
        return 0;
    }
    gm_runtime_set_pose(runtime, 8.5, 5.0, 8.5, 0.0F, 0.0F);
    runtime->mobs.xp_total = 0;
    runtime->player_xp_total = 0;
    runtime->player_xp_level = 0;
    runtime->player_xp_frac = 0.0F;
    return 1;
}

static GmAction idle_action(void)
{
    GmAction action;
    memset(&action, 0, sizeof action);
    action.hotbar_sel = -1;
    return action;
}

int main(void)
{
    GmRuntime runtime;
    GmAction idle = idle_action();
    CHECK(init_flat(&runtime), "initialize XP cooldown fixture");
    CHECK(gm_runtime_spawn_xp_fixture(
              &runtime, 8.5, 5.0, 8.5, 0.0, 0.0, 0.0,
              3, 88101, 0, 0, 0, 0)
          && gm_runtime_spawn_xp_fixture(
              &runtime, 8.5, 5.0, 8.5, 0.0, 0.0, 0.0,
              5, 88102, 0, 0, 0, 0),
          "spawn ordered collocated XP orbs");
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.player_xp_total == 3
              && runtime.mobs.xp_orbs[0].dead
              && !runtime.mobs.xp_orbs[1].dead
              && runtime.mobs.player_xp_cooldown == 2,
          "first loaded orb wins collision and starts cooldown");
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.player_xp_total == 3
              && !runtime.mobs.xp_orbs[1].dead
              && runtime.mobs.player_xp_cooldown == 1,
          "one intervening tick remains pickup-blocked");
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.player_xp_total == 8
              && runtime.mobs.xp_orbs[1].dead
              && runtime.mobs.player_xp_cooldown == 2,
          "second orb is collected on the exact third boundary");
    gm_runtime_destroy(&runtime);

    CHECK(init_flat(&runtime), "initialize pickup-delay fixture");
    CHECK(gm_runtime_spawn_xp_fixture(
              &runtime, 8.5, 5.0, 8.5, 0.0, 0.0, 0.0,
              4, 88103, 0, 2, 0, 0),
          "spawn delayed XP orb");
    gm_runtime_tick(&runtime, idle);
    CHECK(!runtime.mobs.xp_orbs[0].dead
              && runtime.mobs.xp_orbs[0].delayBeforeCanPickup == 1,
          "pickup delay decrements before collision and still blocks");
    gm_runtime_tick(&runtime, idle);
    CHECK(runtime.mobs.xp_orbs[0].dead
              && runtime.player_xp_total == 4,
          "delay-zero boundary allows pickup");
    gm_runtime_destroy(&runtime);

    CHECK(init_flat(&runtime), "initialize Mending fixture");
    {
        ICStack sword = ic_mk(276, 1, 8);
        JavaRandom expected;
        sword.n_enchants = 1;
        sword.enchants[0].id = 70;
        sword.enchants[0].level = 1;
        isr_set_stack(&runtime.player.inv, 0, sword);
        jrand_set_seed48(&runtime.mobs.player_random, 9);
        jrand_set_seed48(&expected, 9);
        (void)jrand_int_bound(&expected, 1);
        CHECK(gm_runtime_spawn_xp_fixture(
                  &runtime, 8.5, 5.0, 8.5, 0.0, 0.0, 0.0,
                  5, 88104, 0, 0, 0, 0),
              "spawn Mending XP orb");
        gm_runtime_tick(&runtime, idle);
        sword = isr_get_stack(&runtime.player.inv, 0);
        CHECK(sword.meta == 0 && runtime.player_xp_total == 1
                  && runtime.mobs.xp_orbs[0].xpValue == 1,
              "Mending repairs two durability per consumed XP");
        CHECK(runtime.mobs.player_random.seed == expected.seed,
              "single Mending candidate still consumes nextInt RNG");
    }
    gm_runtime_destroy(&runtime);
    puts("PASS Mending live: equipment RNG/repair, XP cooldown/order, pickup delay");
    return 0;
}
