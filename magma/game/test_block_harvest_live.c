#include "game/runtime.h"
#include "items_tools_armor.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s\n", message); \
        ++failures; \
    } \
} while (0)

int main(void)
{
    GmConfig config;
    GmRuntime runtime;
    char error[256];
    gm_config_defaults(&config);
    config.seed = 42;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "FAIL: runtime init: %s\n", error);
        return 1;
    }
    runtime.randtick_enabled = 0;
    gm_runtime_set_pose_state(
        &runtime, 8.5, 10.0, 8.5, 0.0F, 89.0F,
        0.0, 0.0, 0.0, 1, 0.0F);
    gm_world_set_block_meta(runtime.world, 8, 9, 8, 56, 0);
    gm_world_set_block_meta(runtime.world, 8, 10, 8, 0, 0);
    gm_world_set_block_meta(runtime.world, 8, 11, 8, 0, 0);

    ICStack pick = ic_mk(278, 1, 100);
    pick.n_enchants = 2;
    pick.enchants[0].id = 34;
    pick.enchants[0].level = 3;
    pick.enchants[1].id = 35;
    pick.enchants[1].level = 3;
    isr_set_stack(&runtime.player.inv, 0, pick);
    runtime.player.inv.current_item = 0;
    jrand_set_seed48(&runtime.mobs.player_random, 7);
    runtime.world_random_seed48 = 0;
    runtime.block_random_seed48 = 0;
    GmRuntime xp_probe;
    memset(&xp_probe, 0, sizeof xp_probe);
    xp_probe.world_random_seed48 = runtime.world_random_seed48;
    xp_probe.block_random_seed48 = runtime.block_random_seed48;
    int expected_xp = 0;
    CHECK(gm_runtime_harvest_xp_result(
              &xp_probe, 56, 278, 0, 3, &expected_xp),
          "diamond XP fixture is represented");
    JavaRandom expected_player = runtime.mobs.player_random;
    ITAStack expected_tool = ita_mk(278, 100);
    expected_tool.unbreaking = 3;
    (void)ita_attempt_damage(&expected_tool, 1, &expected_player);

    GmAction dig;
    memset(&dig, 0, sizeof dig);
    dig.attack = 1;
    dig.hotbar_sel = -1;
    gm_player_dig_reset();
    int broke_at = -1;
    for (int tick = 0; tick < 40; ++tick) {
        gm_runtime_tick(&runtime, dig);
        if (gm_world_block(runtime.world, 8, 9, 8) == 0) {
            broke_at = tick;
            break;
        }
    }
    CHECK(broke_at >= 0, "live progressive dig breaks diamond ore");
    ICStack after = isr_get_stack(&runtime.player.inv, 0);
    CHECK(after.item == 278 && after.meta == expected_tool.damage,
          "live authoritative break applies exact Unbreaking durability");
    CHECK(runtime.mobs.player_random.seed == expected_player.seed,
          "live authoritative break advances exact player RNG");
    int diamonds = 0;
    for (int i = 0; i < GM_LIVE_MAX; ++i)
        if (runtime.entities.ents[i].active
                && runtime.entities.ents[i].item == 264
                && runtime.entities.ents[i].count == 1)
            ++diamonds;
    CHECK(diamonds >= 1 && diamonds <= 4,
          "live Fortune III break emits separate diamond EntityItems");
    CHECK(runtime.entities.n_active == diamonds,
          "live harvest does not retain the old single-drop approximation");
    int live_xp = runtime.mobs.xp_total;
    for (int i = 0; i < GM_XP_ORBS; ++i)
        if (!runtime.mobs.xp_orbs[i].dead
                && runtime.mobs.xp_orbs[i].xpValue > 0)
            live_xp += runtime.mobs.xp_orbs[i].xpValue;
    CHECK(live_xp == expected_xp,
          "live ore break preserves the exact pre-drop XP amount");

    gm_runtime_destroy(&runtime);
    if (failures) return 1;
    puts("PASS block harvest live: Fortune drops + exact Unbreaking wear");
    return 0;
}
