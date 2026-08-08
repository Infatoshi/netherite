#include "game/runtime.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { \
    if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } \
} while (0)

static uint64_t java_lcg_steps(uint64_t seed, int steps)
{
    for (int i = 0; i < steps; ++i)
        seed = (seed * UINT64_C(0x5DEECE66D) + UINT64_C(0xB))
            & ((UINT64_C(1) << 48) - UINT64_C(1));
    return seed;
}

static void test_defense_math(int blast_protection, int resistance,
                              float expected_health, const char *label)
{
    GmMobLive mobs;
    PvStats vitals;
    IsrInv inventory;
    ICStack chest = ic_mk(311, 1, 0);

    gm_mobs_init(&mobs, 0);
    pv_init(&vitals);
    isr_init(&inventory);
    if (blast_protection) {
        chest.n_enchants = 1;
        chest.enchants[0].id = 3;
        chest.enchants[0].level = 4;
    }
    isr_set_stack(&inventory, ISR_ARMOR_CHEST, chest);
    if (resistance)
        mobs.player_resistance_amplifier = 0;
    CHECK(gm_mobs_attack_player_source(
              &mobs, (struct PvStats *)&vitals, &inventory,
              3.0f, 0, GM_DAMAGE_SOURCE_EXPLOSION) == 2,
          label);
    chest = isr_get_stack(&inventory, ISR_ARMOR_CHEST);
    CHECK(fabsf(vitals.health - expected_health) < 1.0e-6f
              && mobs.player_hurt_time == 10,
          label);
    CHECK(chest.item == 311 && chest.count == 1 && chest.meta == 1
              && chest.n_enchants == (blast_protection ? 1 : 0)
              && (!blast_protection
                  || (chest.enchants[0].id == 3
                      && chest.enchants[0].level == 4)),
          label);
}

static void test_item_damage_exceptions(void)
{
    GmLiveSim items;
    GmLiveExplosionTarget targets[1];
    memset(&items, 0, sizeof items);
    CHECK(gm_live_spawn_item_exact(
              &items, 1, 10.5, 83.0, 8.5,
              0.0, 0.0, 0.0, 0.0F,
              1, 1, 0, 0, 32767, 1),
          "lethal item fixture spawns");
    CHECK(gm_live_explosion_targets(&items, targets, 1) == 1
              && targets[0].eid == 1
              && targets[0].box.minX == 10.375
              && targets[0].box.maxY == 83.25,
          "item explosion target retains exact quarter-cube AABB");
    CHECK(!gm_live_apply_explosion(
              &items, targets[0].slot, 9.0F, -0.2, 0.1, 0.0)
              && items.n_active == 0,
          "lethal item explosion retires the entity");
    CHECK(gm_live_spawn_item_exact(
              &items, 2, 9.5, 83.0, 8.5,
              0.0, 0.0, 0.0, 0.0F,
              399, 1, 0, 0, 32767, 1),
          "Nether Star item fixture spawns");
    CHECK(gm_live_explosion_targets(&items, targets, 1) == 1
              && gm_live_apply_explosion(
                  &items, targets[0].slot, 9.0F, -0.2, 0.1, 0.0)
              && items.ents[targets[0].slot].health == 5
              && items.ents[targets[0].slot].mx == -0.2
              && items.ents[targets[0].slot].my == 0.1,
          "Nether Star rejects damage but retains explosion impulse");
    gm_live_destroy(&items);

    /* The old hot-table boundary is not an enumeration boundary. Keep the
     * only blast victim in cold storage so truncating to GM_LIVE_MAX would
     * make this negative control survive. */
    memset(&items, 0, sizeof items);
    for (int i = 0; i < GM_LIVE_MAX; ++i)
        CHECK(gm_live_spawn_item_exact(
                  &items, 1000 + i, 1000.0 + i, 200.0, 1000.0,
                  0.0, 0.0, 0.0, 0.0F,
                  1, 1, 0, 0, 32767, 1),
              "hot explosion-boundary fixture spawns");
    CHECK(gm_live_spawn_item_exact(
              &items, 9999, 10.5, 83.0, 8.5,
              0.0, 0.0, 0.0, 0.0F,
              1, 1, 0, 0, 32767, 1)
              && items.n_overflow == 1,
          "blast victim crosses into active cold storage");
    {
        const int capacity = gm_live_entity_slot_count(&items);
        GmLiveExplosionTarget *all = malloc(
            (size_t)capacity * sizeof *all);
        GmEntityView *views = malloc((size_t)capacity * sizeof *views);
        McAABB *boxes = malloc((size_t)capacity * sizeof *boxes);
        CHECK(all != NULL, "cold explosion snapshot allocates");
        CHECK(views != NULL && boxes != NULL,
              "cold render/collision snapshots allocate");
        if (all && views && boxes) {
            int count = gm_live_explosion_targets(
                &items, all, capacity);
            CHECK(count == GM_LIVE_MAX + 1
                      && all[count - 1].eid == 9999
                      && all[count - 1].slot == GM_LIVE_MAX,
                  "explosion snapshot includes cold target in loaded order");
            CHECK(gm_live_fill_views(&items, views, capacity)
                      == GM_LIVE_MAX + 1
                      && views[GM_LIVE_MAX].ent_id == 9999
                      && views[GM_LIVE_MAX].item_id == 1,
                  "render snapshot includes active cold target");
            CHECK(gm_live_item_boxes(&items, boxes, capacity)
                      == GM_LIVE_MAX + 1
                      && boxes[GM_LIVE_MAX].minX == 10.375
                      && boxes[GM_LIVE_MAX].maxY == 83.25,
                  "collision snapshot includes active cold target");
            CHECK(!gm_live_apply_explosion(
                      &items, all[count - 1].slot,
                      9.0F, 0.0, 0.0, 0.0)
                      && items.n_overflow == 0,
                  "lethal blast retires a cold target without touching hot rows");
        }
        free(all);
        free(views);
        free(boxes);
    }
    gm_live_destroy(&items);
}

static void test_cold_loaded_order_merge(void)
{
    GmWorld *world = gm_world_create_type(0, 1);
    CHECK(world != NULL, "cold-order world allocates");
    if (!world) return;
    gm_world_ensure(world, 0, 0, 1);
    for (int reverse = 0; reverse < 2; ++reverse) {
        GmLiveSim items;
        int order[GM_LIVE_MAX + 2];
        memset(&items, 0, sizeof items);
        for (int slot = 0; slot < GM_LIVE_MAX; ++slot) {
            CHECK(gm_live_spawn_item_state_exact(
                      &items, 1000 + slot,
                      1000.5 + slot * 2.0, 240.0, 1000.5,
                      0.0, 0.0, 0.0, 0.0F, 0.0F,
                      4, 1, 0, 0, 32767, 5, 6000, 0, 1, 0),
                  "cold-order hot fixture spawns");
            items.ents[slot].controlled_stationary = 1;
        }
        CHECK(gm_live_spawn_item_state_exact(
                  &items, 9000, 10.5, 240.0, 10.5,
                  0.0, 0.0, 0.0, 0.0F, 0.0F,
                  1, 4, 0, 100, 10, 5, 6000, 0, 1, 24)
                  && gm_live_spawn_item_state_exact(
                  &items, 9001, 10.5, 240.0, 10.5,
                  0.0, 0.0, 0.0, 0.0F, 0.0F,
                  1, 4, 0, 200, 10, 5, 6000, 0, 1, 24),
              "equal cold merge pair spawns above hot boundary");
        items.overflow[0].entity.controlled_stationary = 1;
        items.overflow[1].entity.controlled_stationary = 1;
        order[0] = GM_LIVE_MAX + reverse;
        order[1] = GM_LIVE_MAX + (reverse ^ 1);
        for (int slot = 0; slot < GM_LIVE_MAX; ++slot)
            order[slot + 2] = slot;
        gm_live_tick_player_ordered(
            &items, world, NULL, 0, 0, order, GM_LIVE_MAX + 2);
        {
            const GmLiveEnt *first = gm_live_entity_ref(
                &items, GM_LIVE_MAX);
            const GmLiveEnt *second = gm_live_entity_ref(
                &items, GM_LIVE_MAX + 1);
            int expected_survivor = reverse ? 9000 : 9001;
            const GmLiveEnt *survivor = first && first->active
                ? first : second;
            CHECK(items.n_overflow == 1 && survivor
                      && survivor->eid == expected_survivor
                      && survivor->count == 8,
                  "opposite loaded order reverses equal cold merge survivor");
        }
        gm_live_destroy(&items);
    }
    gm_world_destroy(world);
}

static void test_cold_loaded_order_merge_chain(void)
{
    GmWorld *world = gm_world_create_type(0, 1);
    CHECK(world != NULL, "cold chain-order world allocates");
    if (!world) return;
    gm_world_ensure(world, 0, 0, 1);
    for (int reverse = 0; reverse < 2; ++reverse) {
        GmLiveSim items;
        int order[GM_LIVE_MAX + 4];
        memset(&items, 0, sizeof items);
        for (int slot = 0; slot < GM_LIVE_MAX; ++slot) {
            CHECK(gm_live_spawn_item_state_exact(
                      &items, 1000 + slot,
                      1000.5 + slot * 2.0, 240.0, 1000.5,
                      0.0, 0.0, 0.0, 0.0F, 0.0F,
                      4, 1, 0, 0, 32767, 5, 6000, 0, 1, 0),
                  "cold chain-order hot fixture spawns");
            items.ents[slot].controlled_stationary = 1;
        }
        for (int cold = 0; cold < 4; ++cold) {
            CHECK(gm_live_spawn_item_state_exact(
                      &items, 9100 + cold, 10.5, 240.0, 10.5,
                      0.0, 0.0, 0.0, 0.0F, 0.0F,
                      1, 4, 0, 100 + cold, 10, 5, 6000, 0, 1, 24),
                  "equal cold merge chain spawns above hot boundary");
            items.overflow[cold].entity.controlled_stationary = 1;
            order[cold] = GM_LIVE_MAX
                + (reverse ? 3 - cold : cold);
        }
        for (int slot = 0; slot < GM_LIVE_MAX; ++slot)
            order[slot + 4] = slot;
        gm_live_tick_player_ordered(
            &items, world, NULL, 0, 0, order, GM_LIVE_MAX + 4);
        {
            const GmLiveEnt *survivor = NULL;
            for (int cold = 0; cold < 4; ++cold) {
                const GmLiveEnt *candidate = gm_live_entity_ref(
                    &items, GM_LIVE_MAX + cold);
                if (candidate && candidate->active) survivor = candidate;
            }
            CHECK(items.n_overflow == 1 && survivor
                      && survivor->eid == (reverse ? 9102 : 9101)
                      && survivor->count == 16,
                  "four-item chain follows supplied loaded order, not cold slots");
        }
        gm_live_destroy(&items);
    }
    gm_world_destroy(world);
}

static void test_cold_loaded_order_pickup(void)
{
    GmWorld *world = gm_world_create_type(0, 1);
    GmLiveSim items;
    PsvPlayer player;
    int order[GM_LIVE_MAX + 4];
    CHECK(world != NULL, "cold pickup-order world allocates");
    if (!world) return;
    gm_world_ensure(world, 0, 0, 1);
    memset(&items, 0, sizeof items);
    psv_player_init(&player);
    isr_init(&player.inv);
    player.ent.posX = 20.5;
    player.ent.posY = 240.0;
    player.ent.posZ = 20.5;
    for (int slot = 0; slot < GM_LIVE_MAX; ++slot) {
        CHECK(gm_live_spawn_item_state_exact(
                  &items, 1000 + slot,
                  1000.5 + slot * 2.0, 240.0, 1000.5,
                  0.0, 0.0, 0.0, 0.0F, 0.0F,
                  4, 1, 0, 0, 32767, 5, 6000, 0, 1, 0),
              "cold pickup-order hot fixture spawns");
        items.ents[slot].controlled_stationary = 1;
    }
    for (int cold = 0; cold < 4; ++cold) {
        CHECK(gm_live_spawn_item_state_exact(
                  &items, 9200 + cold, 20.5, 240.0, 20.5,
                  0.0, 0.0, 0.0, 0.0F, 0.0F,
                  1 + cold, 1, 0, 100 + cold, 0,
                  5, 6000, 0, 1, 0),
              "distinct cold pickup row spawns above hot boundary");
        items.overflow[cold].entity.controlled_stationary = 1;
        order[cold] = GM_LIVE_MAX + 3 - cold;
    }
    for (int slot = 0; slot < GM_LIVE_MAX; ++slot)
        order[slot + 4] = slot;
    gm_live_tick_player_ordered(
        &items, world, (struct PsvPlayer *)&player,
        0, 0, order, GM_LIVE_MAX + 4);
    for (int inventory_slot = 0; inventory_slot < 4; ++inventory_slot) {
        ICStack stack = isr_get_stack(&player.inv, inventory_slot);
        CHECK(stack.item == 4 - inventory_slot && stack.count == 1,
              "cold pickup inventory insertion follows supplied loaded order");
    }
    CHECK(items.n_overflow == 0,
          "loaded-order pickup consumes every eligible cold item");
    gm_live_destroy(&items);
    gm_world_destroy(world);
}

static void test_end_crystal_cold_growth(void)
{
    const char *checkpoint = "test_end_crystal_capacity.checkpoint";
    GmConfig cfg;
    GmRuntime *runtime = calloc(1, sizeof *runtime);
    GmEntityView views[GM_RUNTIME_END_CRYSTALS + 1];
    GmEntityView falling_views[GM_RUNTIME_FALLING_BLOCKS + 1];
    GmEntityView firework_views[GM_RUNTIME_FIREWORKS + 1];
    GmEntityView projectile_views[GM_RUNTIME_PROJECTILES + 1];
    GmEntityView wither_views[GM_RUNTIME_WITHERS + 1];
    GmLightningView lightning_views[GM_RUNTIME_LIGHTNING + 1];
    char error[256];
    int initialized;
    CHECK(runtime != NULL, "end-crystal capacity runtime storage allocates");
    if (!runtime) return;
    gm_config_defaults(&cfg);
    cfg.mobs = 0;
    cfg.weather = 0;
    initialized = gm_runtime_init(runtime, &cfg, error, sizeof error);
    CHECK(initialized,
          "end-crystal capacity runtime initializes");
    if (!initialized) {
        gm_runtime_destroy(runtime);
        free(runtime);
        return;
    }
    for (int index = 0; index <= GM_RUNTIME_END_CRYSTALS; ++index)
        CHECK(gm_runtime_spawn_end_crystal_fixture(
                  runtime, 9300 + index,
                  1000.5 + index * 4.0, 240.0, 1000.5,
                  index, index & 1, 0, 0, 0, 0),
              "standalone end-crystal store grows beyond hot capacity");
    CHECK(runtime->end_crystal_count == GM_RUNTIME_END_CRYSTALS + 1
              && runtime->end_crystals_cap > GM_RUNTIME_END_CRYSTALS
              && runtime->loaded_entity_order_count
                  == GM_RUNTIME_END_CRYSTALS + 1
              && gm_runtime_end_crystal_views(
                  runtime, views, GM_RUNTIME_END_CRYSTALS + 1)
                  == GM_RUNTIME_END_CRYSTALS + 1
              && views[GM_RUNTIME_END_CRYSTALS].ent_id
                  == 9300 + GM_RUNTIME_END_CRYSTALS,
          "grown end-crystal store preserves payload, order, and render views");
    CHECK(gm_runtime_write_checkpoint(runtime, checkpoint)
              && gm_runtime_load_checkpoint(runtime, checkpoint),
          "grown end-crystal store checkpoint reloads");
    CHECK(runtime->end_crystal_count == GM_RUNTIME_END_CRYSTALS + 1
              && runtime->end_crystals_cap > GM_RUNTIME_END_CRYSTALS
              && runtime->end_crystals[GM_RUNTIME_END_CRYSTALS].active
              && runtime->end_crystals[GM_RUNTIME_END_CRYSTALS].eid
                  == 9300 + GM_RUNTIME_END_CRYSTALS,
          "grown end-crystal payload survives checkpoint reload");
    for (int index = 0; index <= GM_RUNTIME_FALLING_BLOCKS; ++index) {
        int x = 100 + index * 2;
        gm_world_set_block_meta(runtime->world, x, 79, 100, 1, 0);
        CHECK(gm_runtime_spawn_falling_fixture(
                  runtime, 9400 + index, 145, 0, 1,
                  x + 0.5, 80.0, 100.5,
                  0.0, 0.0, 0.0, 1, 0),
              "falling-block store grows beyond hot capacity");
    }
    CHECK(runtime->falling_block_count == GM_RUNTIME_FALLING_BLOCKS + 1
              && runtime->falling_blocks_cap > GM_RUNTIME_FALLING_BLOCKS
              && gm_runtime_falling_block_views(
                  runtime, falling_views, GM_RUNTIME_FALLING_BLOCKS + 1)
                  == GM_RUNTIME_FALLING_BLOCKS + 1
              && falling_views[GM_RUNTIME_FALLING_BLOCKS].ent_id
                  == 9400 + GM_RUNTIME_FALLING_BLOCKS,
          "grown falling-block store preserves payload and render views");
    CHECK(gm_runtime_write_checkpoint(runtime, checkpoint)
              && gm_runtime_load_checkpoint(runtime, checkpoint)
              && runtime->falling_block_count
                  == GM_RUNTIME_FALLING_BLOCKS + 1
              && runtime->falling_blocks_cap > GM_RUNTIME_FALLING_BLOCKS,
          "grown falling-block store survives checkpoint reload");
    for (int index = 0; index <= GM_RUNTIME_WORLD_EVENT_CAPACITY; ++index) {
        gm_world_set_block_meta(runtime->world, 200 + index, 80, 100, 145, 0);
        runtime->anvil.open = 1;
        runtime->anvil.wx = 200 + index;
        runtime->anvil.wy = 80;
        runtime->anvil.wz = 100;
        gm_runtime_anvil_finish(runtime, 1);
    }
    CHECK(runtime->falling_block_count == GM_RUNTIME_FALLING_BLOCKS + 1
              && gm_runtime_world_event_count(runtime)
                  == GM_RUNTIME_FALLING_BLOCKS + 1
              && runtime->world_events_cap > GM_RUNTIME_WORLD_EVENT_CAPACITY
              && runtime->world_event_dropped == 0,
          "world-event storage grows past falling-block hot capacity without loss");
    for (int index = 0; index <= GM_RUNTIME_FALLING_BLOCKS; ++index) {
        GmRuntimeWorldEvent event;
        CHECK(gm_runtime_world_event_get(runtime, index, &event)
                  && event.id == 1030 && event.seq == (uint64_t)index,
              "grown world events retain insertion order");
    }
    for (int index = 0; index <= GM_RUNTIME_FIREWORKS; ++index)
        CHECK(gm_runtime_spawn_firework_state_fixture(
                  runtime, 9500 + index,
                  300.5 + index * 2.0, 240.0, 300.5,
                  0.0, 0.05, 0.0,
                  0.0F, 0.0F, 0.0F, 0.0F,
                  0, 1000, 0, 0, 1, 1, 0, 0,
                  0, 0, 0, 0, 0,
                  1234 + index, 0, 0.0),
              "firework store grows beyond hot capacity");
    CHECK(runtime->firework_count == GM_RUNTIME_FIREWORKS + 1
              && runtime->fireworks_cap > GM_RUNTIME_FIREWORKS
              && gm_runtime_projectile_views(
                  runtime, firework_views, GM_RUNTIME_FIREWORKS + 1)
                  == GM_RUNTIME_FIREWORKS + 1
              && firework_views[GM_RUNTIME_FIREWORKS].ent_id
                  == 9500 + GM_RUNTIME_FIREWORKS,
          "grown firework store preserves payload, order, and render views");
    CHECK(gm_runtime_write_checkpoint(runtime, checkpoint)
              && gm_runtime_load_checkpoint(runtime, checkpoint)
              && runtime->firework_count == GM_RUNTIME_FIREWORKS + 1
              && runtime->fireworks_cap > GM_RUNTIME_FIREWORKS
              && runtime->fireworks[GM_RUNTIME_FIREWORKS].active
              && runtime->fireworks[GM_RUNTIME_FIREWORKS].eid
                  == 9500 + GM_RUNTIME_FIREWORKS,
          "grown firework store survives checkpoint reload");
    for (int index = 0; index < runtime->fireworks_cap; ++index)
        if (runtime->fireworks[index].active) {
            runtime->fireworks[index].age = 0;
            runtime->fireworks[index].lifetime = 0;
            runtime->fireworks[index].explosion_count = 1;
            runtime->fireworks[index].twinkle = 1;
        }
    for (int index = GM_RUNTIME_FIREWORKS + 1;
            index <= GM_RUNTIME_FIREWORK_EVENTS; ++index)
        CHECK(gm_runtime_spawn_firework_state_fixture(
                  runtime, 9500 + index,
                  300.5 + index * 2.0, 240.0, 300.5,
                  0.0, 0.05, 0.0,
                  0.0F, 0.0F, 0.0F, 0.0F,
                  0, 0, 0, 0, 1, 1, 0, 1,
                  0, 0, 0, 0, 0,
                  1234 + index, 0, 0.0),
              "dense terminal fireworks grow active storage");
    gm_runtime_tick_fireworks(runtime);
    CHECK(runtime->firework_count == 0
              && runtime->firework_event_count
                  == (GM_RUNTIME_FIREWORK_EVENTS + 1) * 2
              && runtime->firework_events_cap > GM_RUNTIME_FIREWORK_EVENTS
              && runtime->firework_event_dropped == 0,
          "firework event storage grows without dropping dense launch/explode order");
    CHECK(runtime->firework_twinkle_count
                  == GM_RUNTIME_FIREWORK_EVENTS + 1
              && runtime->firework_twinkles_cap
                  > GM_RUNTIME_FIREWORK_TWINKLES,
          "firework twinkle storage grows beyond fixed audio capacity");
    {
        GmRuntimeFireworkEvent first, last;
        CHECK(gm_runtime_firework_event_get(runtime, 0, &first)
                  && gm_runtime_firework_event_get(
                      runtime, runtime->firework_event_count - 1, &last)
                  && first.kind == GM_FIREWORK_EVENT_LAUNCH
                  && first.eid == 9500
                  && last.kind == GM_FIREWORK_EVENT_EXPLODE
                  && last.eid == 9500 + GM_RUNTIME_FIREWORK_EVENTS,
              "grown firework events retain exact insertion order");
    }
    CHECK(gm_runtime_write_checkpoint(runtime, checkpoint)
              && gm_runtime_load_checkpoint(runtime, checkpoint)
              && runtime->firework_events_cap > GM_RUNTIME_FIREWORK_EVENTS
              && runtime->firework_event_count
                  == (GM_RUNTIME_FIREWORK_EVENTS + 1) * 2
              && runtime->firework_twinkles_cap
                  > GM_RUNTIME_FIREWORK_TWINKLES
              && runtime->firework_twinkle_count
                  == GM_RUNTIME_FIREWORK_EVENTS + 1,
          "grown firework event and twinkle stores survive checkpoint reload");
    for (int index = 0; index <= GM_RUNTIME_PROJECTILES; ++index)
        CHECK(gm_runtime_spawn_arrow_fixture(
                  runtime, 9600 + index,
                  500.5 + index * 2.0, 240.0, 500.5,
                  0.0, 0.0, 0.0, 1, 0),
              "projectile store grows beyond hot capacity");
    CHECK(runtime->projectiles_cap > GM_RUNTIME_PROJECTILES
              && gm_runtime_projectile_views(
                  runtime, projectile_views, GM_RUNTIME_PROJECTILES + 1)
                  == GM_RUNTIME_PROJECTILES + 1
              && projectile_views[GM_RUNTIME_PROJECTILES].ent_id
                  == 9600 + GM_RUNTIME_PROJECTILES,
          "grown projectile store preserves payload and render views");
    CHECK(gm_runtime_write_checkpoint(runtime, checkpoint)
              && gm_runtime_load_checkpoint(runtime, checkpoint)
              && runtime->projectiles_cap > GM_RUNTIME_PROJECTILES
              && runtime->projectiles[GM_RUNTIME_PROJECTILES].active
              && runtime->projectiles[GM_RUNTIME_PROJECTILES].eid
                  == 9600 + GM_RUNTIME_PROJECTILES,
          "grown projectile store survives checkpoint reload");
    for (int index = 0; index <= GM_RUNTIME_WITHERS; ++index)
        CHECK(gm_runtime_spawn_wither_fixture(
                  runtime, 9700 + index,
                  700.5 + index * 4.0, 240.0, 700.5,
                  0.0, 0.0, 0.0, 0.0F, 0.0F, 0.0F,
                  300.0F, 0, 0, 0, 0, 0, 0,
                  2000 + index, 0, 0.0),
              "wither store grows beyond hot capacity");
    CHECK(runtime->wither_count == GM_RUNTIME_WITHERS + 1
              && runtime->withers_cap > GM_RUNTIME_WITHERS
              && gm_runtime_wither_views(
                  runtime, wither_views, GM_RUNTIME_WITHERS + 1)
                  == GM_RUNTIME_WITHERS + 1
              && wither_views[GM_RUNTIME_WITHERS].ent_id
                  == 9700 + GM_RUNTIME_WITHERS,
          "grown wither store preserves payload, order, and render views");
    CHECK(gm_runtime_write_checkpoint(runtime, checkpoint)
              && gm_runtime_load_checkpoint(runtime, checkpoint)
              && runtime->wither_count == GM_RUNTIME_WITHERS + 1
              && runtime->withers_cap > GM_RUNTIME_WITHERS
              && runtime->withers[GM_RUNTIME_WITHERS].active
              && runtime->withers[GM_RUNTIME_WITHERS].eid
                  == 9700 + GM_RUNTIME_WITHERS,
          "grown wither store survives checkpoint reload");
    for (int index = 0; index <= GM_RUNTIME_AREA_EFFECT_CLOUDS; ++index)
        CHECK(gm_runtime_spawn_area_effect_cloud_fixture(
                  runtime, 9800 + index, 0, /* TB_PT_EMPTY */
                  900.5 + index * 4.0, 240.0, 900.5,
                  0, 100, 0, 20, 3.0F, 0.0F, 0.0F, 0),
              "area-effect-cloud store grows beyond hot capacity");
    CHECK(runtime->area_effect_cloud_count
                  == GM_RUNTIME_AREA_EFFECT_CLOUDS + 1
              && runtime->area_effect_clouds_cap
                  > GM_RUNTIME_AREA_EFFECT_CLOUDS
              && runtime->area_effect_clouds[GM_RUNTIME_AREA_EFFECT_CLOUDS]
                  .state.active
              && runtime->area_effect_clouds[GM_RUNTIME_AREA_EFFECT_CLOUDS]
                  .eid == 9800 + GM_RUNTIME_AREA_EFFECT_CLOUDS,
          "grown area-effect-cloud store preserves payload and loaded order");
    CHECK(gm_runtime_write_checkpoint(runtime, checkpoint)
              && gm_runtime_load_checkpoint(runtime, checkpoint)
              && runtime->area_effect_cloud_count
                  == GM_RUNTIME_AREA_EFFECT_CLOUDS + 1
              && runtime->area_effect_clouds_cap
                  > GM_RUNTIME_AREA_EFFECT_CLOUDS
              && runtime->area_effect_clouds[GM_RUNTIME_AREA_EFFECT_CLOUDS]
                  .state.active,
          "grown area-effect-cloud store survives checkpoint reload");
    for (int index = 0; index <= GM_RUNTIME_PRIMED_TNT; ++index)
        CHECK(gm_runtime_spawn_primed_tnt_fixture(
                  runtime, 9900 + index,
                  1100.5 + index * 4.0, 240.0, 1100.5,
                  0.0, 0.0, 0.0, 120),
              "primed-TNT store grows beyond hot capacity");
    CHECK(runtime->primed_tnt_count == GM_RUNTIME_PRIMED_TNT + 1
              && runtime->primed_tnt_cap > GM_RUNTIME_PRIMED_TNT
              && runtime->primed_tnt[GM_RUNTIME_PRIMED_TNT].active
              && runtime->primed_tnt[GM_RUNTIME_PRIMED_TNT].eid
                  == 9900 + GM_RUNTIME_PRIMED_TNT
              && runtime->loaded_entity_order[
                  runtime->loaded_entity_order_count - 1]
                  == 9900 + GM_RUNTIME_PRIMED_TNT,
          "grown primed-TNT store preserves payload and loaded order");
    CHECK(gm_runtime_write_checkpoint(runtime, checkpoint)
              && gm_runtime_load_checkpoint(runtime, checkpoint)
              && runtime->primed_tnt_count == GM_RUNTIME_PRIMED_TNT + 1
              && runtime->primed_tnt_cap > GM_RUNTIME_PRIMED_TNT
              && runtime->primed_tnt[GM_RUNTIME_PRIMED_TNT].active
              && runtime->primed_tnt[GM_RUNTIME_PRIMED_TNT].fuse == 120,
          "grown primed-TNT store survives checkpoint reload");
    for (int index = 0; index <= GM_RUNTIME_LIGHTNING; ++index)
        CHECK(gm_runtime_restore_lightning(
                  runtime, runtime->dimension, 10000 + index,
                  index, 2, 3, 1, 123456 + index,
                  (uint64_t)(5000 + index),
                  1300.5 + index * 4.0, 240.0, 1300.5),
              "lightning store grows beyond hot capacity");
    CHECK(runtime->lightning_count == GM_RUNTIME_LIGHTNING + 1
              && runtime->lightning_cap > GM_RUNTIME_LIGHTNING
              && gm_runtime_lightning_views(
                  runtime, lightning_views, GM_RUNTIME_LIGHTNING + 1)
                  == GM_RUNTIME_LIGHTNING + 1
              && lightning_views[GM_RUNTIME_LIGHTNING].eid
                  == 10000 + GM_RUNTIME_LIGHTNING,
          "grown lightning store preserves payload and render views");
    CHECK(gm_runtime_write_checkpoint(runtime, checkpoint)
              && gm_runtime_load_checkpoint(runtime, checkpoint)
              && runtime->lightning_count == GM_RUNTIME_LIGHTNING + 1
              && runtime->lightning_cap > GM_RUNTIME_LIGHTNING
              && runtime->lightning[GM_RUNTIME_LIGHTNING].active
              && runtime->lightning[GM_RUNTIME_LIGHTNING].ticks_existed
                  == GM_RUNTIME_LIGHTNING,
          "grown lightning store survives checkpoint reload");
    for (int index = GM_RUNTIME_LIGHTNING + 1; index < 17; ++index)
        CHECK(gm_runtime_restore_lightning(
                  runtime, runtime->dimension, 10000 + index,
                  index, 2, 3, 1, 123456 + index,
                  (uint64_t)(5000 + index),
                  1300.5 + index * 4.0, 240.0, 1300.5),
              "dense lightning fixture grows active storage");
    {
        GmAction idle = {0};
        GmRuntimeWeatherEvent last;
        idle.hotbar_sel = -1;
        gm_runtime_tick(runtime, idle);
        CHECK(runtime->weather_event_count == 34
                  && runtime->weather_events_cap > GM_RUNTIME_WEATHER_EVENTS
                  && runtime->weather_event_dropped == 0,
              "weather-event storage grows without dropping dense lightning order");
        CHECK(gm_runtime_weather_event_get(runtime, 33, &last)
                  && last.kind == GM_WEATHER_EVENT_IMPACT
                  && last.eid == 10016 && last.seq == 33,
              "grown weather events retain exact insertion order");
    }
    CHECK(gm_runtime_write_checkpoint(runtime, checkpoint)
              && gm_runtime_load_checkpoint(runtime, checkpoint)
              && runtime->weather_event_count == 34
              && runtime->weather_events_cap > GM_RUNTIME_WEATHER_EVENTS
              && runtime->weather_event_dropped == 0,
          "grown weather-event store survives checkpoint reload");
    {
        int sound_before = gm_runtime_sound_event_count(runtime);
        int bolt_count = (GM_RUNTIME_SOUND_EVENTS + 1 - sound_before + 1) / 2;
        GmAction idle = {0};
        GmRuntimeSoundEvent last;
        if (bolt_count < 1) bolt_count = 1;
        for (int index = 0; index < bolt_count; ++index)
            CHECK(gm_runtime_restore_lightning(
                      runtime, runtime->dimension, 11000 + index,
                      index, 2, 3, 1, 223456 + index,
                      (uint64_t)(15000 + index),
                      1500.5 + index * 4.0, 240.0, 1500.5),
                  "dense sound fixture grows lightning storage");
        idle.hotbar_sel = -1;
        gm_runtime_tick(runtime, idle);
        CHECK(runtime->sound_event_count == sound_before + bolt_count * 2
                  && runtime->sound_events_cap > GM_RUNTIME_SOUND_EVENTS
                  && runtime->sound_event_dropped == 0,
              "sound-event storage grows without dropping dense audio order");
        CHECK(gm_runtime_sound_event_get(
                  runtime, runtime->sound_event_count - 1, &last)
                  && last.sound == GM_SOUND_LIGHTNING_IMPACT
                  && last.eid == 11000 + bolt_count - 1,
              "grown sound events retain exact insertion order");
    }
    CHECK(gm_runtime_write_checkpoint(runtime, checkpoint)
              && gm_runtime_load_checkpoint(runtime, checkpoint)
              && runtime->sound_events_cap > GM_RUNTIME_SOUND_EVENTS
              && runtime->sound_event_count > GM_RUNTIME_SOUND_EVENTS
              && runtime->sound_event_dropped == 0,
          "grown sound-event store survives checkpoint reload");
    {
        int particle_before = gm_runtime_particle_event_count(runtime);
        int calls = (GM_RUNTIME_PARTICLE_EVENTS + 1 - particle_before + 2) / 3;
        uint64_t random_seed48 = UINT64_C(123456789);
        GmRuntimeParticleEvent last;
        if (calls < 1) calls = 1;
        gm_world_set_block_meta(runtime->world, 1700, 240, 1700, 130, 0);
        for (int index = 0; index < calls; ++index)
            CHECK(gm_runtime_ender_chest_random_display_tick(
                      runtime, 1700, 240, 1700, &random_seed48) == 3,
                  "dense particle fixture appends every portal event");
        CHECK(runtime->particle_event_count == particle_before + calls * 3
                  && runtime->particle_events_cap > GM_RUNTIME_PARTICLE_EVENTS,
              "particle-event storage grows without dropping dense visual order");
        CHECK(gm_runtime_particle_event_get(
                  runtime, runtime->particle_event_count - 1, &last)
                  && last.kind == GM_PARTICLE_PORTAL
                  && last.x >= 1700.25 && last.x <= 1700.75
                  && last.z >= 1700.25 && last.z <= 1700.75,
              "grown particle events retain exact terminal payload");
    }
    CHECK(gm_runtime_write_checkpoint(runtime, checkpoint)
              && gm_runtime_load_checkpoint(runtime, checkpoint)
              && runtime->particle_events_cap > GM_RUNTIME_PARTICLE_EVENTS
              && runtime->particle_event_count > GM_RUNTIME_PARTICLE_EVENTS,
          "grown particle-event store survives checkpoint reload");
    (void)remove(checkpoint);
    gm_runtime_destroy(runtime);
    free(runtime);
}

static void test_bed_explosion_fire(void)
{
    const uint64_t world_seed48 = UINT64_C(135120319782334);
    const uint64_t random_zero_seed48 = UINT64_C(0x5DEECE66D);
    GmConfig cfg;
    GmRuntime *r = malloc(sizeof *r);
    char err[256];

    CHECK(r != NULL, "bed-fire runtime storage allocates");
    if (!r) return;
    gm_config_defaults(&cfg);
    cfg.view_distance = 1;
    cfg.mobs = 0;
    CHECK(gm_runtime_init(r, &cfg, err, sizeof err),
          "bed-fire runtime initializes");
    if (fail) {
        free(r);
        return;
    }
    gm_world_ensure(r->world, 0, 0, 0);
    for (int x = 0; x <= 16; ++x)
        for (int y = 92; y <= 108; ++y)
            for (int z = 0; z <= 16; ++z)
                gm_world_set_block_meta(r->world, x, y, z, 0, 0);
    gm_world_set_block_meta(r->world, 8, 99, 8, 49, 0);
    gm_world_set_block_meta(r->world, 8, 100, 8, 26, 0);
    r->dimension = -1;
    gm_runtime_set_pose(r, 8.5, 100.0, 14.0, 180.0F, 0.0F);
    r->vitals.health = r->vitals.maxHealth = 200.0F;
    r->player.health = r->server_player.health = 200.0F;
    CHECK(gm_runtime_set_world_random_seed48(r, world_seed48)
              && gm_runtime_set_next_explosion_random_seed48(
                  r, random_zero_seed48),
          "bed-fire fixture restores both independent random cursors");
    CHECK(gm_runtime_use_block(r, 8, 100, 8),
          "non-Overworld bed creates a flaming size-five explosion");
    CHECK(gm_world_block(r->world, 8, 99, 8) == 49
              && gm_world_block(r->world, 8, 100, 8) == 51,
          "seed-zero explosion RNG ignites the sole eligible air cell");
    CHECK(!r->next_explosion_random_valid,
          "flaming explosion consumes the injected constructor cursor");
    CHECK(r->world_random_seed48 == java_lcg_steps(world_seed48, 1355),
          "bed rays, sound pitch, and fire scheduling consume exact World.rand");
    gm_runtime_destroy(r);
    free(r);
}

int main(void)
{
    const uint64_t blast_seed = UINT64_C(135120319782334);
    GmConfig cfg;
    GmRuntime r;
    GmAction idle;
    char err[256];

    test_end_crystal_cold_growth();
    if (fail) return 1;
    gm_config_defaults(&cfg);
    cfg.view_distance = 1;
    cfg.mobs = 0;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err),
          "TNT explosion runtime initializes");
    if (fail) return 1;

    for (int z = -8; z <= 24; ++z)
        for (int x = -8; x <= 24; ++x) {
            gm_world_set_block_meta(r.world, x, 77, z, 1, 0);
            for (int y = 78; y <= 91; ++y)
                gm_world_set_block_meta(r.world, x, y, z, 0, 0);
        }
    gm_world_set_block_meta(r.world, 9, 78, 8, 20, 0);
    r.vitals.health = 20.0f;
    r.vitals.maxHealth = 20.0f;
    r.vitals.foodLevel = 20;
    r.vitals.saturation = 5.0f;
    r.vitals.exhaustion = 0.05f;
    r.vitals.foodTimer = 0;
    r.mobs.player_hurt_time = 0;
    r.mobs.player_hurt_resistant = 0;
    r.mobs.player_last_damage = 0.0f;
    r.mobs.player_absorption = 0.0f;
    r.player.health = r.server_player.health = 20.0f;
    gm_runtime_set_pose_state(
        &r, 8.5, 78.0, 8.5, -180.0f, 0.0f,
        0.0, -0.0784000015258789, 0.0, 1, 0.0f);
    CHECK(gm_runtime_set_entity_id_cursor(&r, 9961)
              && gm_runtime_set_world_random_seed48(&r, blast_seed)
              && gm_runtime_spawn_primed_tnt_fixture(
                  &r, 9960, 13.5, 82.0, 8.5,
                  0.0, 0.0, 0.0, 1),
          "obstructed player-blast fixture spawns");

    gm_runtime_tick(&r, idle);
    CHECK(r.primed_tnt_count == 0
              && r.world_random_seed48
                  == java_lcg_steps(blast_seed, 1354),
          "obstructed blast retires with exact cursor");
    CHECK(gm_world_block(r.world, 9, 78, 8) == 20,
          "obstructed blast retains its same-seed glass occluder");
    CHECK(gm_world_block(r.world, 12, 77, 8) == 0
              && gm_world_block(r.world, 13, 77, 8) == 1
              && gm_world_block(r.world, 14, 77, 8) == 0,
          "obstructed blast matches its same-seed floor crater");
    CHECK(r.vitals.health == 16.0f
              && r.vitals.foodTimer == 1
              && fabsf(r.vitals.exhaustion - 0.15f) < 1.0e-7f
              && r.mobs.player_hurt_time == 9,
          "obstructed blast matches the damage lifecycle");
    CHECK(fabs(r.player.ent.posX - 8.404875) < 1.0e-15
              && fabs(r.player.ent.motionX
                  - (-0.05193825603276491)) < 1.0e-15
              && fabs(r.server_player.ent.motionX
                  - (-0.051960797397907814)) < 1.0e-15,
          "obstructed blast matches client and server knockback");

    gm_world_set_block_meta(r.world, 13, 82, 8, 49, 0);
    gm_world_set_block_meta(r.world, 13, 83, 8, 50, 5);
    CHECK(gm_runtime_set_world_random_seed48(&r, blast_seed)
              && gm_runtime_spawn_item_fixture(
                  &r, 9961, 23.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 1, 1, 0, 0, 32767, 1)
              && gm_runtime_spawn_primed_tnt_fixture(
                  &r, 9962, 16.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 1)
              && gm_runtime_spawn_mob_fixture(
                  &r, GM_MOB_PIG, 9963, 10.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0),
          "TNT-first open-air pig-blast fixture spawns");
    gm_runtime_tick(&r, idle);
    {
        GmMobExplosionTarget target[1];
        int count = gm_mobs_explosion_targets(
            &r.mobs, r.dimension, target, 1);
        CHECK(count == 1 && target[0].eid == 9963
                  && target[0].health == 4.0F
                  && target[0].hurt_time == 9
                  && target[0].hurt_resistant_time == 19,
              "outline-occluded TNT-first pig blast applies exact lifecycle");
        CHECK(count == 1
                  && fabs(target[0].vx
                      - (-0.16208969949315842)) < 1.0e-15
                  && fabs(target[0].vy
                      - 0.02009236855686323) < 1.0e-15
                  && target[0].vz == 0.0,
              "outline-occluded TNT-first pig blast applies exact damping");
        CHECK(gm_world_block(r.world, 13, 82, 8) == 49
                  && gm_world_block(r.world, 13, 83, 8) == 0,
              "outline occluder keeps support and loses exact torch");
        if (count == 1) {
            int slot = target[0].slot;
            r.mobs.a.alive[slot] = r.mobs.b.alive[slot] = 0;
            r.mobs.a.type[slot] = r.mobs.b.type[slot] = EW_TYPE_NONE;
            r.mobs.controlled_no_ai[slot] = 0;
            r.mobs.controlled_block_collisions[slot] = 0;
        }
        {
            const GmLiveEnt *item = NULL;
            for (int i = 0; i < GM_LIVE_MAX; ++i)
                if (r.entities.ents[i].active
                        && r.entities.ents[i].eid == 9961)
                    item = &r.entities.ents[i];
            CHECK(item && item->health == 1 && item->age == 1
                      && item->x == 23.5 && item->y == 83.0
                      && item->z == 8.5,
                  "item-first blast applies damage after its stationary tick");
            CHECK(item
                      && fabs(item->mx
                          - 0.12494932090351177) < 1.0e-15
                      && fabs(item->my
                          - 0.003413794015269717) < 1.0e-15
                      && item->mz == 0.0,
                  "item-first blast retains exact undamped impulse");
        }
    }

    for (int i = 0; i < GM_LIVE_MAX; ++i)
        if (r.entities.ents[i].active
                && r.entities.ents[i].eid == 9961) {
            r.entities.ents[i].active = 0;
            --r.entities.n_active;
        }
    gm_runtime_set_pose_state(
        &r, 0.5, 78.0, 8.5, -180.0f, 0.0f,
        0.0, -0.0784000015258789, 0.0, 1, 0.0f);
    CHECK(gm_runtime_set_world_random_seed48(&r, blast_seed)
              && gm_runtime_spawn_arrow_fixture(
                  &r, 9969, 1.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 1, 0)
              && gm_runtime_spawn_primed_tnt_fixture(
                  &r, 9970, 8.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 1)
              && gm_runtime_spawn_item_fixture(
                  &r, 9971, 1.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 1, 1, 0, 0, 32767, 1)
              && gm_runtime_spawn_boat_fixture(
                  &r, 9972, 1.5, 83.0, 8.5, 0.0F),
          "TNT-first surviving item/boat fixtures spawn");
    gm_runtime_tick(&r, idle);
    {
        const GmLiveEnt *item = NULL;
        for (int i = 0; i < GM_LIVE_MAX; ++i)
            if (r.entities.ents[i].active
                    && r.entities.ents[i].eid == 9971)
                item = &r.entities.ents[i];
        CHECK(item && item->health == 1 && item->age == 1
                  && fabs(item->x
                      - 1.3750506790964874) < 1.0e-15
                  && fabs(item->y
                      - 83.00341379401527) < 1.0e-15
                  && item->z == 8.5,
              "TNT-first item moves by the fresh impulse");
        CHECK(item
                  && fabs(item->mx
                      - (-0.12245033686866069)) < 1.0e-15
                  && fabs(item->my
                      - 0.003345518200077276) < 1.0e-15
                  && item->mz == 0.0,
              "TNT-first item applies exact same-tick damping");
    }
    {
        const GmRuntimeProjectile *arrow = NULL;
        for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
            if (r.projectiles[i].active
                    && r.projectiles[i].eid == 9969)
                arrow = &r.projectiles[i];
        CHECK(arrow && arrow->age == 1
                  && arrow->x == 1.5 && arrow->y == 83.0
                  && arrow->z == 8.5,
              "arrow-first blast retains its pre-explosion position");
        CHECK(arrow
                  && fabs(arrow->vx
                      - (-0.12499536788906258)) < 1.0e-15
                  && fabs(arrow->vy
                      - (-0.0003794502612004625)) < 1.0e-15
                  && arrow->vz == 0.0,
              "arrow-first blast applies exact raw impulse");
    }
    {
        GmMobExplosionTarget target[1];
        int count = gm_mobs_explosion_targets(
            &r.mobs, r.dimension, target, 1);
        CHECK(count == 1 && target[0].eid == 9972
                  && r.mobs.boat_damage[target[0].slot] == 39,
              "TNT-first boat survives exact damage lifecycle");
        CHECK(count == 1
                  && fabs(target[0].x
                      - 1.387742502803361) < 1.0e-15
                  && fabs(target[0].y
                      - 83.00814089616274) < 1.0e-15
                  && target[0].z == 8.5,
              "TNT-first boat moves by its damped impulse");
        CHECK(count == 1
                  && fabs(target[0].vx
                      - (-0.11225749719663904)) < 1.0e-15
                  && fabs(target[0].vy
                      - 0.008140896162744845) < 1.0e-15
                  && target[0].vz == 0.0,
              "TNT-first boat retains exact same-tick motion");
    }

    CHECK(gm_runtime_set_world_random_seed48(&r, blast_seed)
              && gm_runtime_spawn_xp_fixture(
                  &r, 1.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 1, 9980, 0, 0, 0, -100)
              && gm_runtime_spawn_primed_tnt_fixture(
                  &r, 9981, 8.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 1),
          "XP-first blast fixtures spawn");
    gm_runtime_tick(&r, idle);
    {
        const McOrb *orb = NULL;
        for (int i = 0; i < GM_XP_ORBS; ++i)
            if (!r.mobs.xp_orbs[i].dead
                    && r.mobs.xp_orbs[i].eid == 9980)
                orb = &r.mobs.xp_orbs[i];
        CHECK(orb && orb->health == 1
                  && orb->xpValue == 1 && orb->xpOrbAge == 1
                  && orb->delayBeforeCanPickup == 0
                  && orb->xpColor == 1 && orb->xpTargetColor == 0
                  && fabs(orb->posX
                      - 1.4950548948488454) < 1.0e-15
                  && fabs(orb->posY
                      - 82.949280010099) < 1.0e-15
                  && orb->posZ == 8.5,
              "XP-first blast retains exact pre-explosion physics state");
        CHECK(orb
                  && fabs(orb->motionX
                      - (-0.12902424922385383)) < 1.0e-15
                  && fabs(orb->motionY
                      - (-0.0434473581470098)) < 1.0e-15
                  && orb->motionZ == 0.0,
              "XP-first blast applies exact damage and raw impulse");
    }

    gm_runtime_set_total_time(&r, 140);
    gm_world_set_block_meta(r.world, 1, 83, 8, 12, 0);
    CHECK(gm_runtime_set_entity_id_cursor(&r, 9983)
              && gm_runtime_schedule_tick(
              &r, 1, 83, 8, 12, 142, 0,
              r.scheduled_tick_next_order)
              && gm_runtime_set_world_random_seed48(&r, blast_seed)
              && gm_runtime_spawn_primed_tnt_fixture(
                  &r, 9982, 8.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 3),
          "TNT-first falling-sand fixtures spawn");
    gm_runtime_tick(&r, idle);
    CHECK(r.falling_block_count == 0 && r.primed_tnt_count == 1,
          "falling-sand fixture retains its first pre-dispatch boundary");
    gm_runtime_tick(&r, idle);
    CHECK(r.falling_block_count == 1 && r.primed_tnt_count == 1
              && r.falling_blocks[0].fall_time == 1
              && r.falling_blocks[0].x == 1.5
              && fabs(r.falling_blocks[0].y
                  - 82.96999999135733) < 1.0e-15
              && r.falling_blocks[0].vx == 0.0
              && fabs(r.falling_blocks[0].vy
                  - (-0.03919999988675116)) < 1.0e-15,
          "falling sand dispatches after TNT's non-exploding update");
    gm_runtime_tick(&r, idle);
    CHECK(r.falling_block_count == 1 && r.primed_tnt_count == 0
              && r.falling_blocks[0].fall_time == 2
              && fabs(r.falling_blocks[0].x
                  - 1.3763911663466868) < 1.0e-15
              && fabs(r.falling_blocks[0].y
                  - 82.90807990783813) < 1.0e-15
              && r.falling_blocks[0].z == 8.5
              && fabs(r.falling_blocks[0].vx
                  - (-0.12113665933789819)) < 1.0e-15
              && fabs(r.falling_blocks[0].vy
                  - (-0.06068168302984159)) < 1.0e-15
              && r.falling_blocks[0].vz == 0.0,
          "TNT-first falling sand moves and damps the exact fresh impulse");

    CHECK(gm_runtime_set_world_random_seed48(&r, blast_seed)
              && gm_runtime_spawn_primed_tnt_fixture(
                  &r, 9990, 8.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 1)
              && gm_runtime_spawn_small_fireball_fixture(
                  &r, 9991, 1.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 0.0, 0.0, 0.0),
          "TNT-first small-fireball fixtures spawn");
    gm_runtime_tick(&r, idle);
    {
        const GmRuntimeProjectile *fireball = NULL;
        for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
            if (r.projectiles[i].active
                    && r.projectiles[i].eid == 9991)
                fireball = &r.projectiles[i];
        CHECK(r.primed_tnt_count == 0 && fireball && fireball->age == 1
                  && fabs(fireball->x
                      - 1.3750801534780397) < 1.0e-15
                  && fabs(fireball->y
                      - 83.00436104103332) < 1.0e-15
                  && fireball->z == 8.5,
              "TNT-first small fireball moves by the fresh impulse");
        CHECK(fireball
                  && fabs(fireball->vx
                      - (-0.11867385270670164)) < 1.0e-15
                  && fabs(fireball->vy
                      - 0.004142988929661038) < 1.0e-15
                  && fireball->vz == 0.0
                  && fireball->ax == 0.0
                  && fireball->ay == 0.0
                  && fireball->az == 0.0,
              "TNT-first small fireball applies exact motion damping");
        CHECK(fireball && fireball->yaw == 342.0F
                  && fireball->pitch == -0.39989015F,
              "TNT-first small fireball uses exact MathHelper rotation");
    }

    CHECK(gm_runtime_set_world_random_seed48(&r, blast_seed)
              && gm_runtime_spawn_primed_tnt_fixture(
                  &r, 9992, 8.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 1)
              && gm_runtime_spawn_primed_tnt_fixture(
                  &r, 9993, 1.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 80),
          "source-first two-TNT fixtures spawn");
    gm_runtime_tick(&r, idle);
    {
        const GmRuntimePrimedTnt *target = NULL;
        for (int i = 0; i < r.primed_tnt_cap; ++i)
            if (r.primed_tnt[i].active && r.primed_tnt[i].eid == 9993)
                target = &r.primed_tnt[i];
        CHECK(r.primed_tnt_count == 1 && target && target->fuse == 79
                  && fabs(target->x
                      - 1.3750046321109366) < 1.0e-15
                  && fabs(target->y
                      - 82.95962055063286) < 1.0e-15
                  && target->z == 8.5,
              "source-first target TNT moves by the fresh impulse");
        CHECK(target
                  && fabs(target->vx
                      - (-0.12249546291537877)) < 1.0e-15
                  && fabs(target->vy
                      - (-0.03957186114996505)) < 1.0e-15
                  && target->vz == 0.0,
              "source-first target TNT applies exact gravity and damping");
    }

    for (int i = 0; i < r.primed_tnt_cap; ++i)
        if (r.primed_tnt[i].active && r.primed_tnt[i].eid == 9993) {
            r.primed_tnt[i].active = 0;
            --r.primed_tnt_count;
        }
    CHECK(gm_runtime_set_world_random_seed48(&r, blast_seed)
              && gm_runtime_spawn_primed_tnt_fixture(
                  &r, 9994, 1.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 80)
              && gm_runtime_spawn_primed_tnt_fixture(
                  &r, 9995, 8.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 1),
          "target-first two-TNT fixtures spawn");
    gm_runtime_tick(&r, idle);
    {
        const GmRuntimePrimedTnt *target = NULL;
        for (int i = 0; i < r.primed_tnt_cap; ++i)
            if (r.primed_tnt[i].active && r.primed_tnt[i].eid == 9994)
                target = &r.primed_tnt[i];
        CHECK(r.primed_tnt_count == 1 && target && target->fuse == 79
                  && target->x == 1.5
                  && fabs(target->y
                      - 82.96000000089407) < 1.0e-15
                  && target->z == 8.5,
              "target-first target TNT retains its pre-blast position");
        CHECK(target
                  && fabs(target->vx
                      - (-0.1249617182537039)) < 1.0e-15
                  && fabs(target->vy
                      - (-0.04029341494275192)) < 1.0e-15
                  && target->vz == 0.0,
              "target-first target TNT retains the exact raw impulse");
    }

    for (int i = 0; i < r.primed_tnt_cap; ++i)
        if (r.primed_tnt[i].active) {
            r.primed_tnt[i].active = 0;
            --r.primed_tnt_count;
        }
    r.vitals.health = 20.0F;
    r.mobs.player_hurt_time = 0;
    r.mobs.player_hurt_resistant = 0;
    r.mobs.player_last_damage = 0.0F;
    r.player.health = r.server_player.health = 20.0F;
    gm_runtime_set_pose_state(
        &r, 0.5, 78.0, 8.5, -180.0F, 0.0F,
        0.0, -0.0784000015258789, 0.0, 1, 0.0F);
    CHECK(gm_runtime_set_world_random_seed48(&r, blast_seed)
              && gm_runtime_spawn_primed_tnt_fixture(
                  &r, 9996, 16.5, 88.0, 8.5,
                  0.0, 0.0, 0.0, 1)
              && gm_runtime_spawn_end_crystal_fixture(
                  &r, 9997, 9.5, 88.0, 8.5, 0, 1, 0, 0, 0, 0),
          "TNT-first End-crystal fixtures spawn");
    gm_runtime_tick(&r, idle);
    CHECK(r.primed_tnt_count == 0 && r.end_crystal_count == 0,
          "TNT-first hit retires source and End crystal");
    CHECK(r.world_random_seed48 == java_lcg_steps(blast_seed, 2708),
          "nested End-crystal blast consumes two exact explosion cursors");
    CHECK(r.vitals.health == 20.0F && r.mobs.player_hurt_time == 0
              && r.player.ent.posX == 0.5
              && r.player.ent.posY == 78.0
              && r.player.ent.posZ == 8.5,
          "distant nested End-crystal blast leaves the player unchanged");
    CHECK(gm_runtime_spawn_end_crystal_fixture(
              &r, 9998, 1.5, 83.0, 8.5, 0, 1, 0, 0, 0, 0),
          "standalone End-crystal fixture spawns");
    gm_runtime_tick(&r, idle);
    {
        GmRuntimeEndCrystal *crystal = NULL;
        for (int i = 0; i < GM_RUNTIME_END_CRYSTALS; ++i)
            if (r.end_crystals[i].active
                    && r.end_crystals[i].eid == 9998)
                crystal = &r.end_crystals[i];
        CHECK(r.end_crystal_count == 1 && crystal
                  && crystal->inner_rotation == 1
                  && crystal->show_bottom == 1
                  && crystal->x == 1.5 && crystal->y == 83.0
                  && crystal->z == 8.5,
              "ordinary standalone End-crystal tick advances render state");
    }
    CHECK(gm_runtime_set_dimension(&r, 1),
          "runtime enters the End for crystal-fire fixture");
    gm_world_ensure(r.world, 0, 0, 0);
    gm_world_set_block_meta(r.world, 9, 82, 8, 49, 0);
    gm_world_set_block_meta(r.world, 9, 83, 8, 0, 0);
    CHECK(gm_runtime_spawn_end_crystal_fixture(
              &r, 9999, 9.5, 83.0, 8.5, 0, 1, 1, 20, 99, -7),
          "End crystal-fire fixture spawns");
    gm_runtime_tick(&r, idle);
    CHECK(gm_world_block(r.world, 9, 82, 8) == 49
              && gm_world_block(r.world, 9, 83, 8) == 51,
          "End crystal creates fire above its inert support");
    {
        GmRuntimeEndCrystal *crystal = NULL;
        for (int i = 0; i < GM_RUNTIME_END_CRYSTALS; ++i)
            if (r.end_crystals[i].active
                    && r.end_crystals[i].eid == 9999)
                crystal = &r.end_crystals[i];
        CHECK(crystal && crystal->dimension == 1
                  && crystal->inner_rotation == 1
                  && crystal->has_beam == 1
                  && crystal->beam_x == 20 && crystal->beam_y == 99
                  && crystal->beam_z == -7,
              "End crystal advances while preserving its saved beam target");
    }
    {
        GmEntityView views[GM_RUNTIME_END_CRYSTALS];
        int count = gm_runtime_end_crystal_views(
            &r, views, GM_RUNTIME_END_CRYSTALS);
        CHECK(count == 1 && views[0].type == GM_ENTITY_CRYSTAL
                  && views[0].type == 31
                  && views[0].ent_id == 9999
                  && views[0].crystal_rot == 1.0F
                  && views[0].show_bottom == 1
                  && views[0].has_beam == 1
                  && views[0].beam_x == 20 && views[0].beam_y == 99
                  && views[0].beam_z == -7,
              "saved End crystal enters the live render-view stream");
    }

    /* A TNT blast marks an arena crystal, completes the crystal's nested
     * explosion, and only then notifies the dragon fight. Keep dragon and
     * player outside both blast diameters so the exact additional ten health
     * is isolated from ordinary explosion damage. */
    for (int i = 0; i < ED_NUM_CRYSTALS; ++i)
        r.dragon.state.arena.crystals[i].alive = 0;
    r.dragon.initialized = 1;
    r.dragon.state.arena.crystals[0].alive = 1;
    r.dragon.state.arena.crystals[0].x = 42.5;
    r.dragon.state.arena.crystals[0].y = 88.0;
    r.dragon.state.arena.crystals[0].z = 8.5;
    r.dragon.state.arena.dragon.alive = 1;
    r.dragon.state.arena.dragon.death_ticks = 0;
    r.dragon.state.arena.dragon.health = 100.0F;
    r.dragon.state.arena.dragon.max_health = 200.0F;
    r.dragon.state.arena.dragon.x = 11.5;
    r.dragon.state.arena.dragon.y = 88.0;
    r.dragon.state.arena.dragon.z = 8.5;
    r.dragon.state.arena.dragon.target_x = 11.5;
    r.dragon.state.arena.dragon.target_y = 88.0;
    r.dragon.state.arena.dragon.target_z = 8.5;
    r.dragon.state.arena.dragon.vx = 0.0;
    r.dragon.state.arena.dragon.vy = 0.0;
    r.dragon.state.arena.dragon.vz = 0.0;
    r.dragon.state.arena.dragon.phase = ED_PHASE_HOVER;
    r.dragon.state.arena.dragon.phase_ticks = 0;
    r.dragon.state.arena.dragon.heal_crystal_idx = 0;
    r.dragon.state.arena.player.x = r.player.ent.posX;
    r.dragon.state.arena.player.y = r.player.ent.posY;
    r.dragon.state.arena.player.z = r.player.ent.posZ;
    gm_world_ensure(r.world, 2, 0, 0);
    gm_world_ensure(r.world, 3, 0, 0);
    CHECK(gm_runtime_spawn_primed_tnt_fixture(
              &r, 10000, 42.5, 88.0, 8.5,
              0.0, 0.0, 0.0, 1),
          "arena healing-crystal TNT fixture spawns");
    gm_runtime_tick(&r, idle);
    if (r.dragon.state.arena.crystals[0].alive
            || r.dragon.state.arena.dragon.health != 90.0F)
        fprintf(stderr,
            "arena crystal result: alive=%d xyz=(%g,%g,%g) health=%g heal=%d phase=%d tnt=%d dim=%d init=%d\n",
            r.dragon.state.arena.crystals[0].alive,
            r.dragon.state.arena.crystals[0].x,
            r.dragon.state.arena.crystals[0].y,
            r.dragon.state.arena.crystals[0].z,
            r.dragon.state.arena.dragon.health,
            r.dragon.state.arena.dragon.heal_crystal_idx,
            r.dragon.state.arena.dragon.phase,r.primed_tnt_count,
            r.dimension,r.dragon.initialized);
    CHECK(!r.dragon.state.arena.crystals[0].alive
              && r.dragon.state.arena.dragon.health == 90.0F,
          "arena healing-crystal chain applies exact post-blast ten damage");

    /* Preserve arrow block-before-entity ordering while threading crystal
     * notification through the old short-circuit. */
    memset(r.end_crystals, 0,
           (size_t)r.end_crystals_cap * sizeof *r.end_crystals);
    r.end_crystal_count = 0;
    r.dragon.state.arena.crystals[0].alive = 1;
    r.dragon.state.arena.crystals[0].x = 10.5;
    r.dragon.state.arena.crystals[0].y = 88.0;
    r.dragon.state.arena.crystals[0].z = 8.5;
    r.dragon.state.arena.dragon.health = 100.0F;
    r.dragon.state.arena.dragon.x = 41.5;
    r.dragon.state.arena.dragon.y = 88.0;
    r.dragon.state.arena.dragon.z = 8.5;
    r.dragon.state.arena.dragon.target_x = 41.5;
    r.dragon.state.arena.dragon.target_y = 88.0;
    r.dragon.state.arena.dragon.target_z = 8.5;
    r.dragon.state.arena.dragon.vx = 0.0;
    r.dragon.state.arena.dragon.vy = 0.0;
    r.dragon.state.arena.dragon.vz = 0.0;
    r.dragon.state.arena.dragon.phase = ED_PHASE_HOVER;
    r.dragon.state.arena.dragon.phase_ticks = 0;
    r.dragon.state.arena.dragon.heal_crystal_idx = 0;
    gm_world_set_block_meta(r.world, 9, 88, 8, 1, 0);
    CHECK(gm_runtime_spawn_arrow_fixture(
              &r, 10001, 8.5, 88.0, 8.5,
              0.0, 0.0, 0.0, 1, 0),
          "blocked arena-crystal arrow fixture spawns");
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
        if (r.projectiles[i].active && r.projectiles[i].eid == 10001) {
            r.projectiles[i].controlled_stationary = 0;
            r.projectiles[i].vx = 2.0;
        }
    gm_runtime_tick(&r, idle);
    CHECK(r.dragon.state.arena.crystals[0].alive
              && r.dragon.state.arena.dragon.health == 100.0F,
          "solid block shields arena crystal from player arrow");
    gm_world_set_block_meta(r.world, 9, 88, 8, 0, 0);
    CHECK(gm_runtime_spawn_arrow_fixture(
              &r, 10002, 8.5, 88.0, 8.5,
              0.0, 0.0, 0.0, 1, 0),
          "unblocked arena-crystal arrow fixture spawns");
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
        if (r.projectiles[i].active && r.projectiles[i].eid == 10002) {
            r.projectiles[i].controlled_stationary = 0;
            r.projectiles[i].vx = 2.0;
        }
    gm_runtime_tick(&r, idle);
    CHECK(!r.dragon.state.arena.crystals[0].alive
              && r.dragon.state.arena.dragon.health == 90.0F,
          "unblocked player arrow explodes and notifies healing crystal");

    gm_runtime_destroy(&r);
    test_defense_math(
        0, 0, 17.816f, "diamond chestplate blast defense");
    test_defense_math(
        1, 0, 18.5148792f, "Blast Protection IV blast defense");
    test_defense_math(
        1, 1, 18.8119049f,
        "armor then Resistance then Blast Protection defense order");
    test_item_damage_exceptions();
    test_cold_loaded_order_merge();
    test_cold_loaded_order_merge_chain();
    test_cold_loaded_order_pickup();
    test_bed_explosion_fire();
    if (fail) return 1;
    puts("tnt_explosion: PASS");
    return 0;
}
