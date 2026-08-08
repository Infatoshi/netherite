#include "game/runtime.h"
#include "tile_entity_brewing.h"

#include <stdio.h>
#include <string.h>

static int failed;
#define CHECK(condition, message) do { if (!(condition)) { \
    fprintf(stderr, "FAIL: %s\n", (message)); failed = 1; } } while (0)

/* A tipped-arrow item tag with a fixed color and one hidden, ambient Strength
 * II effect. The base PotionType remains Poison in ICStack.meta. */
static const unsigned char tipped_tag[] = {
    10, 0, 0,
    3, 0, 17,
        'C','u','s','t','o','m','P','o','t','i','o','n','C','o','l','o','r',
        0, 0x12, 0x34, 0x56,
    9, 0, 19,
        'C','u','s','t','o','m','P','o','t','i','o','n','E','f','f','e','c','t','s',
        10, 0, 0, 0, 1,
        1, 0, 2, 'I','d', 5,
        1, 0, 9, 'A','m','p','l','i','f','i','e','r', 1,
        3, 0, 8, 'D','u','r','a','t','i','o','n', 0, 0, 1, 44,
        1, 0, 7, 'A','m','b','i','e','n','t', 1,
        1, 0, 13,
            'S','h','o','w','P','a','r','t','i','c','l','e','s', 0,
        0,
    0
};

static int init_runtime(GmRuntime *runtime) {
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

static GmRuntimeProjectile *active_arrow(GmRuntime *runtime) {
    for (int index = 0; index < GM_RUNTIME_PROJECTILES; ++index)
        if (runtime->projectiles[index].active
                && (runtime->projectiles[index].type == 1
                    || runtime->projectiles[index].type == 2))
            return &runtime->projectiles[index];
    return NULL;
}

static int mob_effect(
        const GmRuntime *runtime, int slot, int id, PtMobEffect *out) {
    int count = gm_mobs_potion_effect_count(&runtime->mobs, slot);
    for (int index = 0; index < count; ++index) {
        PtMobEffect effect;
        if (gm_mobs_potion_effect_get(
                &runtime->mobs, slot, index, &effect)
                && effect.id == id) {
            if (out) *out = effect;
            return 1;
        }
    }
    return 0;
}

static int mob_effect_flags_for_id(
        const GmRuntime *runtime, int slot, int id,
        int *ambient, int *show_particles) {
    int count = gm_mobs_potion_effect_count(&runtime->mobs, slot);
    for (int index = 0; index < count; ++index) {
        PtMobEffect effect;
        if (gm_mobs_potion_effect_get(
                &runtime->mobs, slot, index, &effect)
                && effect.id == id)
            return gm_mobs_potion_effect_flags(
                &runtime->mobs, slot, index,
                ambient, show_particles);
    }
    return 0;
}

static void clear_arrows(GmRuntime *runtime) {
    memset(runtime->projectiles, 0, (size_t)runtime->projectiles_cap * sizeof *runtime->projectiles);
}

int main(void) {
    static const char checkpoint[] = "test_arrow_payload_checkpoint.bin";
    GmRuntime runtime;
    GmAction idle;
    GmRuntimeProjectile *arrow;
    PtMobEffect effect;
    unsigned char effect_flags[1] = {3};
    PtMobEffect custom_effects[1] = {{10, 240, 1}};
    int tag_id;
    int target;

    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    if (!init_runtime(&runtime)) return 1;

    tag_id = gm_runtime_stack_tag_intern(
        &runtime, tipped_tag, sizeof tipped_tag);
    CHECK(tag_id > 0, "intern a complete tipped-arrow item tag");
    runtime.player.inv.current_item = 0;
    ICStack bow = ic_mk(261, 1, 0);
    bow.n_enchants = 1;
    bow.enchants[0].id = 51;
    bow.enchants[0].level = 1;
    isr_set_stack(&runtime.player.inv, 0, bow);
    ICStack tipped = ic_mk(440, 2, TB_PT_POISON);
    tipped.tag_id = tag_id;
    isr_set_stack(&runtime.player.inv, 1, tipped);
    CHECK(gm_runtime_set_next_arrow_random_state(&runtime, 0, 0, 0.0)
              && gm_runtime_release_bow_now(&runtime, 20),
          "Infinity bow fires tagged tipped ammunition");
    arrow = active_arrow(&runtime);
    CHECK(arrow && arrow->arrow_kind == GM_ARROW_TIPPED
              && arrow->arrow_potion_type == TB_PT_POISON
              && arrow->arrow_effect_count == 1
              && arrow->arrow_effects[0].id == 5
              && arrow->arrow_effects[0].amplifier == 1
              && arrow->arrow_effects[0].duration == 300
              && arrow->arrow_effect_flags[0] == 1
              && arrow->arrow_custom_color
              && arrow->arrow_color == 0x123456
              && arrow->arrow_pickup_item == 440
              && arrow->arrow_pickup_tag_id == tag_id
              && arrow->arrow_pickup_status == 1
              && isr_get_stack(&runtime.player.inv, 1).count == 1,
          "tipped payload decodes and Infinity consumes non-ordinary ammo");

    target = gm_mobs_spawn(
        &runtime.mobs, EW_TYPE_PIG, 30.5, 80.0, 30.5);
    if (arrow) arrow->arrow_critical = 0;
    CHECK(target > 0 && arrow
              && gm_runtime_player_arrow_hit_now(
                  &runtime, (int)(arrow - runtime.projectiles), target) >= 2,
          "tagged tipped arrow lands on a living target");
    CHECK(mob_effect(&runtime, target, 19, &effect)
              && effect.amplifier == 0 && effect.duration == 112,
          "base tipped effect uses max(duration / 8, 1)");
    CHECK(mob_effect(&runtime, target, 5, &effect)
              && effect.amplifier == 1 && effect.duration == 300,
          "custom tipped effect keeps its original duration and amplifier");
    {
        int ambient = 0, show_particles = 1;
        CHECK(mob_effect_flags_for_id(
                  &runtime, target, 5, &ambient, &show_particles)
                  && ambient && !show_particles,
              "custom tipped effect retains ambient and particle flags");
    }

    clear_arrows(&runtime);
    bow = isr_get_stack(&runtime.player.inv, 0);
    bow.meta = 0;
    isr_set_stack(&runtime.player.inv, 0, bow);
    isr_set_stack(&runtime.player.inv, 1, ic_mk(439, 2, 0));
    CHECK(gm_runtime_release_bow_now(&runtime, 20),
          "Infinity bow fires spectral ammunition");
    arrow = active_arrow(&runtime);
    CHECK(arrow && arrow->arrow_kind == GM_ARROW_SPECTRAL
              && arrow->arrow_spectral_duration == 200
              && arrow->arrow_pickup_item == 439
              && arrow->arrow_pickup_status == 1
              && isr_get_stack(&runtime.player.inv, 1).count == 1,
          "spectral arrow remains consumable and pickup-allowed");
    target = gm_mobs_spawn(
        &runtime.mobs, EW_TYPE_PIG, 32.5, 80.0, 32.5);
    if (arrow) arrow->arrow_critical = 0;
    CHECK(target > 0 && arrow
              && gm_runtime_player_arrow_hit_now(
                  &runtime, (int)(arrow - runtime.projectiles), target) >= 2
              && mob_effect(&runtime, target, 24, &effect)
              && effect.amplifier == 0 && effect.duration == 200,
          "spectral impact applies the configured Glowing duration");
    {
        GmEntityView views[GM_LIVE_MAX];
        int count = gm_mobs_fill_views(&runtime.mobs, views, GM_LIVE_MAX);
        int found = 0;
        for (int i = 0; i < count; ++i)
            if (views[i].ent_id == runtime.mobs.a.id[target]
                    || views[i].ent_id == runtime.mobs.b.id[target]) {
                found = (views[i].flags & GM_ENTITY_FLAG_GLOWING) != 0;
                break;
            }
        CHECK(found,
              "active Glowing effect reaches Entity.isGlowing render state");
    }

    clear_arrows(&runtime);
    isr_init(&runtime.player.inv);
    CHECK(gm_runtime_spawn_player_arrow_state_fixture(
              &runtime, 7001, 8.5, 4.25, 8.5,
              0.0, 0.0, 0.0, 0.0F, 0.0F,
              0, -1, 2.0, 0, 0, 1, 1, 0, 0,
              8, 4, 8, 1, 0, UINT64_C(1), 0, 0.0)
              && gm_runtime_set_arrow_payload(
                  &runtime, 7001, GM_ARROW_SPECTRAL, TB_PT_EMPTY,
                  321, -1, 0, NULL, 0, NULL, 439, 0, 0)
              && gm_runtime_player_arrow_pickup_now(&runtime, 0),
          "spectral arrow enters a survival inventory");
    CHECK(isr_get_stack(&runtime.player.inv, 0).item == 439
              && isr_get_stack(&runtime.player.inv, 0).count == 1,
          "spectral pickup returns Items.SPECTRAL_ARROW");

    clear_arrows(&runtime);
    gm_runtime_set_pose(&runtime, 4.5, 4.0, 4.5, 0.0F, 0.0F);
    gm_world_set_block_meta(runtime.world, 8, 4, 8, 1, 0);
    CHECK(gm_runtime_spawn_player_arrow_state_fixture(
              &runtime, 7002, 7.95, 4.5, 8.5,
              0.5, 0.0, 0.0, 0.0F, 0.0F,
              0, -1, 2.0, 0, 0, 1, 1, 0, 0,
              8, 4, 8, 1, 0, UINT64_C(2), 0, 0.0)
              && gm_runtime_set_arrow_payload(
                  &runtime, 7002, GM_ARROW_TIPPED, TB_PT_REGENERATION,
                  200, 0xabcdef, 1,
                  custom_effects, 1, effect_flags, 440,
                  TB_PT_REGENERATION, 0),
          "restore an embedded tipped arrow with a custom effect");
    arrow = active_arrow(&runtime);
    for (int tick = 0; tick < 599; ++tick) gm_runtime_tick(&runtime, idle);
    CHECK(arrow && arrow->active && arrow->arrow_effect_count == 1
              && arrow->arrow_time_in_ground == 599,
          "custom tipped payload survives through in-ground tick 599");
    gm_runtime_tick(&runtime, idle);
    CHECK(arrow && arrow->active && arrow->arrow_kind == GM_ARROW_TIPPED
              && arrow->arrow_effect_count == 0
              && arrow->arrow_potion_type == TB_PT_EMPTY
              && arrow->arrow_color == -1
              && arrow->arrow_pickup_item == 262
              && arrow->arrow_time_in_ground == 600,
          "custom tipped payload washes out to an ordinary pickup at tick 600");

    clear_arrows(&runtime);
    CHECK(gm_runtime_spawn_player_arrow_state_fixture(
              &runtime, 7003, 49.5, 80.5, 50.5,
              1.0, 0.0, 0.0, 0.0F, 0.0F,
              0, -1, 2.0, 0, 0, 0, 0, 0, 0,
              -1, -1, -1, 0, 0, UINT64_C(3), 0, 0.0),
          "restore a living-shooter arrow fixture");
    arrow = active_arrow(&runtime);
    arrow->type = 2;
    gm_world_set_block_meta(runtime.world, 50, 80, 50, 1, 0);
    CHECK(gm_runtime_player_arrow_block_hit_now(
              &runtime, 0, 50, 80, 50, 50.0, 80.5, 50.5)
              && arrow->arrow_in_ground && arrow->arrow_shake == 7,
          "living-shooter arrow enters the exact block-impact state");

    CHECK(gm_runtime_set_arrow_payload(
              &runtime, 7003, GM_ARROW_SPECTRAL, TB_PT_EMPTY,
              417, -1, 0, NULL, 0, NULL, 439, 0, 0)
              && gm_runtime_write_checkpoint(&runtime, checkpoint)
              && gm_runtime_load_checkpoint(&runtime, checkpoint),
          "checkpoint round-trips an arrow subclass payload");
    arrow = active_arrow(&runtime);
    CHECK(arrow && arrow->eid == 7003
              && arrow->type == 2
              && arrow->arrow_kind == GM_ARROW_SPECTRAL
              && arrow->arrow_spectral_duration == 417
              && arrow->arrow_pickup_item == 439
              && arrow->arrow_in_ground,
          "checkpoint retains arrow class, payload, pickup, and ground state");
    (void)remove(checkpoint);

    gm_runtime_destroy(&runtime);

    CHECK(init_runtime(&runtime), "initialize player payload runtime");
    if (!failed) {
        gm_runtime_set_pose(&runtime, 8.5, 4.0, 8.5, 0.0F, 0.0F);
        gm_runtime_set_vitals(&runtime, 10.0F, 20);
        CHECK(gm_runtime_spawn_player_arrow_state_fixture(
                  &runtime, 7004, 8.5, 4.9, 6.5,
                  0.0, 0.0, 2.0, 0.0F, 0.0F,
                  5, -1, 2.0, 0, 0, 0, 0, 0, 0,
                  -1, -1, -1, 0, 0, UINT64_C(4), 0, 0.0)
                  && gm_runtime_set_arrow_payload(
                      &runtime, 7004, GM_ARROW_TIPPED, TB_PT_HEALING,
                      200, 0xf82423, 0, NULL, 0, NULL,
                      440, TB_PT_HEALING, 0),
              "restore a healing arrow on a player collision course");
        arrow = active_arrow(&runtime);
        if (arrow) {
            arrow->type = 2;
            arrow->player_thrower = 0;
            arrow->shooting_living = 1;
        }
        gm_runtime_tick(&runtime, idle);
        CHECK(arrow && !arrow->active
                  && runtime.potion_count == 1
                  && runtime.potions[0].id == 6
                  && runtime.potions[0].duration == 1,
              "player arrow impact queues the one-tick instant effect");
        float damaged_health = runtime.vitals.health;
        gm_runtime_tick(&runtime, idle);
        CHECK(runtime.potion_count == 0
                  && runtime.vitals.health == damaged_health + 4.0F,
              "queued instant Healing executes and expires next player tick");
    }
    gm_runtime_destroy(&runtime);
    if (failed) return 1;
    fprintf(stderr, "arrow_payload_live: PASS\n");
    return 0;
}
