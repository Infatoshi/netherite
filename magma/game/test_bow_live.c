#include "game/runtime.h"

#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); fail = 1; } } while (0)

static void add_enchantment(ICStack *stack, int id, int level) {
    int at = stack->n_enchants++;
    stack->enchants[at].id = id;
    stack->enchants[at].level = level;
}

static int init(GmRuntime *runtime) {
    GmConfig config;
    char error[256];
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.seed = 0;
    if (!gm_runtime_init(runtime, &config, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 0;
    }
    return 1;
}

int main(void) {
    GmRuntime runtime;
    GmAction idle;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    if (!init(&runtime)) return 1;
    runtime.player.inv.current_item = 0;
    isr_set_stack(&runtime.player.inv, 0, ic_mk(261, 1, 0));
    CHECK(!gm_runtime_release_bow_now(&runtime, 20),
          "no-ammo survival bow rejects release");
    ICStack bow = ic_mk(261, 1, 0);
    add_enchantment(&bow, 51, 1);
    isr_set_stack(&runtime.player.inv, 0, bow);
    CHECK(gm_runtime_release_bow_now(&runtime, 20)
              && isr_get_stack(&runtime.player.inv, 0).meta == 1,
          "Infinity bow fires without ammunition and wears");
    memset(runtime.projectiles, 0, (size_t)runtime.projectiles_cap * sizeof *runtime.projectiles);
    bow = ic_mk(261, 1, 0);
    isr_set_stack(&runtime.player.inv, 0, bow);
    isr_set_stack(&runtime.player.inv, 1, ic_mk(262, 2, 0));
    for (int i = 0; i < runtime.projectiles_cap; ++i)
        runtime.projectiles[i].active = 1;
    CHECK(gm_runtime_release_bow_now(&runtime, 20)
              && runtime.projectiles_cap > GM_RUNTIME_PROJECTILES
              && isr_get_stack(&runtime.player.inv, 0).meta == 1
              && isr_get_stack(&runtime.player.inv, 1).count == 1,
          "full projectile hot pool grows without changing bow semantics");

    memset(runtime.projectiles, 0, (size_t)runtime.projectiles_cap * sizeof *runtime.projectiles);
    gm_runtime_set_pose(&runtime, 8.5, 5.0, 8.5, 0.0F, 0.0F);
    for (int x = 6; x <= 10; ++x)
        for (int y = 4; y <= 9; ++y)
            gm_world_set_block_meta(runtime.world, x, y, 12, 1, 0);
    CHECK(gm_runtime_set_next_arrow_random_state(
              &runtime, 0, 0, 0.0)
              && gm_runtime_release_bow_now(&runtime, 20),
          "full draw creates a seeded flight arrow");
    int arrow_slot = -1;
    for (int i = 0; i < runtime.projectiles_cap; ++i)
        if (runtime.projectiles[i].active
                && runtime.projectiles[i].type == 1) {
            arrow_slot = i;
            break;
        }
    for (int tick = 0; tick < 8 && arrow_slot >= 0
            && !runtime.projectiles[arrow_slot].arrow_in_ground; ++tick)
        gm_runtime_tick(&runtime, idle);
    CHECK(arrow_slot >= 0
              && runtime.projectiles[arrow_slot].active
              && runtime.projectiles[arrow_slot].arrow_in_ground
              && !runtime.projectiles[arrow_slot].arrow_critical
              && runtime.projectiles[arrow_slot].arrow_shake > 0,
          "live arrow ray embeds in a solid block instead of disappearing");
    if (arrow_slot >= 0) {
        GmRuntimeProjectile *arrow = &runtime.projectiles[arrow_slot];
        gm_world_set_block_meta(
            runtime.world, arrow->arrow_tile_x,
            arrow->arrow_tile_y, arrow->arrow_tile_z, 0, 0);
        gm_runtime_tick(&runtime, idle);
        CHECK(arrow->active && !arrow->arrow_in_ground
                  && arrow->age == 0,
              "removing the embedded tile releases the arrow with reset age");
    }

    memset(runtime.projectiles, 0, (size_t)runtime.projectiles_cap * sizeof *runtime.projectiles);
    gm_world_set_block_meta(runtime.world, 20, 8, 20, 1, 0);
    GmRuntimeProjectile *ground = &runtime.projectiles[0];
    ground->active = 1;
    ground->type = 1;
    ground->eid = runtime.next_entity_id++;
    ground->x = 19.5; ground->y = 8.5; ground->z = 20.5;
    ground->vx = 1.0; ground->arrow_damage = 2.0;
    ground->arrow_pickup_status = 1;
    ground->fire_ticks = -1;
    CHECK(gm_runtime_player_arrow_block_hit_now(
              &runtime, 0, 20, 8, 20, 20.0, 8.5, 20.5),
          "direct block impact seeds an embedded arrow");
    for (int tick = 0; tick < 1199; ++tick)
        gm_runtime_tick(&runtime, idle);
    CHECK(ground->active && ground->arrow_ticks_in_ground == 1199,
          "embedded arrow persists through tick 1199");
    gm_runtime_tick(&runtime, idle);
    CHECK(!ground->active && ground->arrow_ticks_in_ground == 1200,
          "embedded arrow despawns on vanilla tick 1200");

    memset(runtime.projectiles, 0, (size_t)runtime.projectiles_cap * sizeof *runtime.projectiles);
    CHECK(gm_runtime_spawn_player_arrow_state_fixture(
              &runtime, 5100, 40.5, 100.25, 40.5,
              0.75, -0.125, 0.25, 71.5F, -8.25F,
              7, 1993, 4.5, 2, 1, 1, 0, 0, 0,
              -1, -1, -1, 0, 0, UINT64_C(0x23456789ABCD),
              1, -0.375),
          "cold capsule restores a complete flying player arrow");
    GmRuntimeProjectile *resumed = &runtime.projectiles[0];
    CHECK(resumed->active && resumed->type == 1
              && resumed->eid == 5100 && resumed->age == 7
              && resumed->shooting_living && resumed->player_thrower
              && resumed->x == 40.5 && resumed->y == 100.25
              && resumed->z == 40.5 && resumed->vx == 0.75
              && resumed->vy == -0.125 && resumed->vz == 0.25
              && resumed->yaw == 71.5F && resumed->pitch == -8.25F
              && resumed->fire_ticks == 1993
              && resumed->arrow_damage == 4.5
              && resumed->arrow_knockback == 2
              && resumed->arrow_critical
              && resumed->arrow_pickup_status == 1
              && !resumed->arrow_in_ground
              && resumed->random_seed48 == UINT64_C(0x23456789ABCD)
              && resumed->random_have_gaussian
              && resumed->random_next_gaussian == -0.375,
          "flying arrow capsule preserves every tick-relevant scalar");
    gm_runtime_tick(&runtime, idle);
    CHECK(resumed->active && resumed->age == 8
              && resumed->fire_ticks == 1992
              && resumed->x == 41.25 && resumed->y == 100.125
              && resumed->z == 40.75
              && resumed->vx == 0.75 * (double)0.99F
              && resumed->vy == -0.125 * (double)0.99F
                    - 0.05000000074505806D
              && resumed->vz == 0.25 * (double)0.99F
              && resumed->random_seed48 == UINT64_C(0x23456789ABCD),
          "resumed flying arrow continues through the ordinary exact tick");

    memset(runtime.projectiles, 0, (size_t)runtime.projectiles_cap * sizeof *runtime.projectiles);
    gm_world_set_block_meta(runtime.world, 44, 100, 44, 1, 3);
    CHECK(gm_runtime_spawn_player_arrow_state_fixture(
              &runtime, 5101, 43.95, 100.5, 44.5,
              0.5, 0.0, 0.0, 0.0F, 0.0F,
              0, -1, 2.0, 0, 0, 1, 1, 3, 1199,
              44, 100, 44, 1, 3, UINT64_C(0x123456789ABC),
              0, 0.0),
          "cold capsule restores a complete embedded player arrow");
    resumed = &runtime.projectiles[0];
    gm_runtime_tick(&runtime, idle);
    CHECK(!resumed->active && resumed->arrow_in_ground
              && resumed->arrow_shake == 2
              && resumed->arrow_ticks_in_ground == 1200
              && resumed->x == 43.95 && resumed->y == 100.5
              && resumed->z == 44.5,
          "resumed embedded arrow preserves its block and despawn boundary");

    memset(runtime.projectiles, 0, (size_t)runtime.projectiles_cap * sizeof *runtime.projectiles);
    isr_init(&runtime.player.inv);
    runtime.server_player.inv = runtime.player.inv;
    gm_runtime_set_pose(&runtime, 8.5, 4.0, 8.5, 0.0F, 0.0F);
    gm_world_set_block_meta(runtime.world, 9, 4, 8, 1, 0);
    CHECK(gm_runtime_spawn_player_arrow_state_fixture(
              &runtime, 5102, 9.5, 4.25, 8.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F,
              0, -1, 2.0, 0, 0, 1, 1, 0, 0,
              9, 4, 8, 1, 0, UINT64_C(1), 0, 0.0),
          "pickup fixture restores an ordinary embedded arrow");
    resumed = &runtime.projectiles[0];
    gm_runtime_tick(&runtime, idle);
    CHECK(!resumed->active
              && isr_get_stack(&runtime.player.inv, 0).item == 262
              && isr_get_stack(&runtime.player.inv, 0).count == 1,
          "nearby embedded arrow enters a non-full survival inventory");

    memset(runtime.projectiles, 0, (size_t)runtime.projectiles_cap * sizeof *runtime.projectiles);
    isr_init(&runtime.player.inv);
    for (int i = 0; i < ISR_MAIN_SLOTS; ++i)
        isr_set_stack(&runtime.player.inv, i, ic_mk(1, 64, 0));
    runtime.server_player.inv = runtime.player.inv;
    CHECK(gm_runtime_spawn_player_arrow_state_fixture(
              &runtime, 5103, 9.5, 4.25, 8.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F,
              0, -1, 2.0, 0, 0, 1, 1, 0, 0,
              9, 4, 8, 1, 0, UINT64_C(1), 0, 0.0),
          "full-inventory pickup fixture restores");
    resumed = &runtime.projectiles[0];
    gm_runtime_tick(&runtime, idle);
    CHECK(resumed->active && resumed->arrow_ticks_in_ground == 1,
          "full survival inventory leaves an allowed arrow in the world");

    memset(runtime.projectiles, 0, (size_t)runtime.projectiles_cap * sizeof *runtime.projectiles);
    isr_init(&runtime.player.inv);
    runtime.server_player.inv = runtime.player.inv;
    CHECK(gm_runtime_spawn_player_arrow_state_fixture(
              &runtime, 5104, 9.5, 4.25, 8.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F,
              0, -1, 2.0, 0, 0, 2, 1, 0, 0,
              9, 4, 8, 1, 0, UINT64_C(1), 0, 0.0),
          "creative-only pickup fixture restores");
    resumed = &runtime.projectiles[0];
    gm_runtime_tick(&runtime, idle);
    CHECK(resumed->active,
          "survival player cannot collect a creative-only arrow");
    runtime.tape_creative = 1;
    gm_runtime_tick(&runtime, idle);
    CHECK(!resumed->active
              && isr_get_stack(&runtime.player.inv, 0).item == 0,
          "creative player collects creative-only arrow without an item");
    runtime.tape_creative = 0;

    memset(runtime.projectiles, 0, (size_t)runtime.projectiles_cap * sizeof *runtime.projectiles);
    isr_init(&runtime.player.inv);
    runtime.server_player.inv = runtime.player.inv;
    CHECK(gm_runtime_spawn_player_arrow_state_fixture(
              &runtime, 5105, 9.5, 4.25, 8.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F,
              0, -1, 2.0, 0, 0, 1, 1, 1, 0,
              9, 4, 8, 1, 0, UINT64_C(1), 0, 0.0),
          "shaking pickup fixture restores");
    resumed = &runtime.projectiles[0];
    gm_runtime_tick(&runtime, idle);
    CHECK(resumed->active && resumed->arrow_shake == 0,
          "arrow with shake one is not collected before its own tick");
    gm_runtime_tick(&runtime, idle);
    CHECK(!resumed->active,
          "arrow becomes collectible on the tick after shake reaches zero");
    gm_runtime_destroy(&runtime);

    if (!init(&runtime)) return 1;
    bow = ic_mk(261, 1, 0);
    add_enchantment(&bow, 49, 2);
    add_enchantment(&bow, 50, 1);
    isr_set_stack(&runtime.player.inv, 0, bow);
    isr_set_stack(&runtime.player.inv, 1, ic_mk(262, 2, 0));
    runtime.player.inv.current_item = 0;
    int target = gm_mobs_spawn(
        &runtime.mobs, EW_TYPE_SHEEP, 8.5, 4.0, 14.5);
    gm_runtime_set_pose(&runtime, 8.5, 4.0, 8.5, 0.0F, 6.8F);
    CHECK(target > 0 && gm_runtime_set_next_arrow_random_state(
              &runtime, 0, 0, 0.0)
              && gm_runtime_release_bow_now(&runtime, 20),
          "enchanted live arrow launches toward living target");
    for (int tick = 0; tick < 10
            && runtime.mobs.arrow_count[target] == 0; ++tick)
        gm_runtime_tick(&runtime, idle);
    EwStore *store = runtime.mobs.current
        ? &runtime.mobs.b : &runtime.mobs.a;
    CHECK(store->health[target] < 10.0F
              && runtime.mobs.arrow_count[target] == 1
              && runtime.mobs.fire_ticks[target] > 0
              && (store->vx[target] != 0.0 || store->vz[target] != 0.0),
          "release, ray flight, Flame, Punch, and living impact compose live");
    gm_runtime_destroy(&runtime);
    if (fail) return 1;
    fprintf(stderr, "bow_live: PASS\n");
    return 0;
}
