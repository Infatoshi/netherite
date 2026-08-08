#include "game/runtime.h"
#include "game/chest_live.h"
#include "tile_entity_brewing.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); return 1; } } while (0)

static int init_flat(GmRuntime *r) {
    GmConfig cfg;
    char err[256];
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    cfg.weather = 0;
    cfg.brewing = 1;
    if (!gm_runtime_init(r, &cfg, err, sizeof err)) {
        fprintf(stderr, "FAIL: %s\n", err);
        return 0;
    }
    gm_runtime_set_pose(r, 8.5, 78.0, 8.5, 0.0f, 0.0f);
    return 1;
}

static int container_at(
        const GmRuntime *r, int x, int y, int z,
        GmRuntimeStaticContainer *out) {
    int count = gm_runtime_static_container_count(r);
    for (int i = 0; i < count; ++i) {
        GmRuntimeStaticContainer value;
        if (gm_runtime_static_container_get(r, i, &value)
                && value.wx == x && value.wy == y && value.wz == z) {
            if (out) *out = value;
            return 1;
        }
    }
    return 0;
}

static void tick(GmRuntime *r, int count) {
    GmAction action;
    memset(&action, 0, sizeof action);
    while (count-- > 0)
        gm_runtime_tick(r, action);
}

static unsigned long long dbits(double value) {
    union { double d; uint64_t u; } bits;
    bits.d = value;
    return (unsigned long long)bits.u;
}

static unsigned fbits(float value) {
    union { float f; uint32_t u; } bits;
    bits.f = value;
    return bits.u;
}

static void event_pair(const GmRuntime *r, int *first, int *second) {
    GmRuntimeWorldEvent event;
    *first = -1;
    *second = -1;
    if (gm_runtime_world_event_get(r, 0, &event)) *first = event.id;
    if (gm_runtime_world_event_get(r, 1, &event)) *second = event.id;
}

static int event_at(const GmRuntime *r, int index) {
    GmRuntimeWorldEvent event;
    return gm_runtime_world_event_get(r, index, &event) ? event.id : -1;
}

static int sound_id(const GmRuntime *r, int index) {
    GmRuntimeSoundEvent event;
    return gm_runtime_sound_event_get(r, index, &event) ? event.sound : -1;
}

static uint64_t mushroom_cuboid_hash(
        const GmRuntime *r, int x, int y, int z) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (int dy = -1; dy <= 13; ++dy)
        for (int dz = -3; dz <= 3; ++dz)
            for (int dx = -3; dx <= 3; ++dx) {
                int state = dx == -1 && dy == 0 && dz == 0 ? 0
                    : gm_world_block(r->world, x + dx, y + dy, z + dz) << 4
                        | (gm_world_meta(
                            r->world, x + dx, y + dy, z + dz) & 15);
                hash ^= (uint64_t)state;
                hash *= UINT64_C(0x100000001b3);
            }
    return hash;
}

static uint64_t grass_bonemeal_cuboid_hash(
        const GmRuntime *r, int x, int y, int z) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (int dy = 0; dy <= 1; ++dy)
        for (int dz = -7; dz <= 7; ++dz)
            for (int dx = -7; dx <= 7; ++dx) {
                int state = dx == -1 && dy == 0 && dz == 0 ? 0
                    : gm_world_block(r->world, x + dx, y + dy, z + dz) << 4
                        | (gm_world_meta(
                            r->world, x + dx, y + dy, z + dz) & 15);
                hash ^= (uint64_t)state;
                hash *= UINT64_C(0x100000001b3);
            }
    return hash;
}

static uint64_t sapling_tree_cuboid_hash(
        const GmRuntime *r, int x, int y, int z) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (int dy = -1; dy <= 34; ++dy)
        for (int dz = -15; dz <= 15; ++dz)
            for (int dx = -15; dx <= 15; ++dx) {
                int state = dx == -1 && dy == 0 && dz == 0 ? 0
                    : gm_world_block(r->world, x + dx, y + dy, z + dz) << 4
                        | (gm_world_meta(
                            r->world, x + dx, y + dy, z + dz) & 15);
                hash ^= (uint64_t)state;
                hash *= UINT64_C(0x100000001b3);
            }
    return hash;
}

static int armor_inventory_slot(int item) {
    if (item >= 298 && item <= 317)
        return ISR_ARMOR0 + 3 - ((item - 298) & 3);
    if (item == 397 || item == 86) return ISR_ARMOR_HEAD;
    return item == 442 ? ISR_OFFHAND_SLOT : ISR_ARMOR_CHEST;
}

static int armor_equipment_ordinal(int item) {
    if (item >= 298 && item <= 317)
        return 5 - ((item - 298) & 3);
    if (item == 397 || item == 86) return 5;
    return item == 442 ? 1 : 4;
}

static int furnace_at(
        const GmRuntime *r, int x, int y, int z,
        GmRuntimeFurnace *out) {
    int count = gm_runtime_furnace_count(r);
    for (int i = 0; i < count; ++i) {
        GmRuntimeFurnace value;
        if (gm_runtime_furnace_get(r, i, &value)
                && value.wx == x && value.wy == y && value.wz == z) {
            if (out) *out = value;
            return 1;
        }
    }
    return 0;
}

static int chest_at(
        const GmRuntime *r, int x, int y, int z,
        GmRuntimeChest *out) {
    int count = gm_runtime_chest_count(r);
    for (int i = 0; i < count; ++i) {
        GmRuntimeChest value;
        if (gm_runtime_chest_get(r, i, &value)
                && value.wx == x && value.wy == y && value.wz == z) {
            if (out) *out = value;
            return 1;
        }
    }
    return 0;
}

int main(void) {
    GmRuntime r;
    GmRuntimeStaticContainer source, destination;
    const int x = 12, y = 78, z = 8;

    CHECK(init_flat(&r), "initialize hopper fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 154, 5),
          "place east-facing source hopper");
    CHECK(gm_runtime_set_block(&r, x + 1, y, z, 23, 2),
          "place destination dispenser");
    CHECK(gm_runtime_static_container_set_slot(
              &r, 0, x, y, z, 0, 1, 3, 0),
          "load source hopper");
    CHECK(gm_runtime_hopper_set_transfer_state(
              &r, 0, x, y, z, 0, 0),
          "arm source hopper");
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && container_at(&r, x + 1, y, z, &destination)
              && source.slots[0].count == 2
              && destination.slots[0].item == 1
              && destination.slots[0].count == 1
              && source.transfer_cooldown == 8,
          "one item transfers and starts the exact cooldown");
    printf("A 1 %d %d %d %lld\n", source.slots[0].count,
           destination.slots[0].count, source.transfer_cooldown,
           source.ticked_game_time);
    tick(&r, 7);
    CHECK(container_at(&r, x, y, z, &source)
              && source.slots[0].count == 2
              && source.transfer_cooldown == 1,
          "seven cooldown ticks do not transfer");
    printf("A 8 %d %d %d %lld\n", source.slots[0].count,
           destination.slots[0].count, source.transfer_cooldown,
           source.ticked_game_time);
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && container_at(&r, x + 1, y, z, &destination)
              && source.slots[0].count == 1
              && destination.slots[0].count == 2
              && source.transfer_cooldown == 8,
          "eighth cooldown tick transfers exactly once");
    printf("A 9 %d %d %d %lld\n", source.slots[0].count,
           destination.slots[0].count, source.transfer_cooldown,
           source.ticked_game_time);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize powered hopper fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 154, 13)
              && gm_runtime_set_block(&r, x + 1, y, z, 158, 2),
          "place powered hopper and dropper");
    /* HopperGolden's MemoryWorld.put seam installs these states without
     * delivering neighborChanged.  The public runtime placement above does
     * deliver it, so restore the deliberately synthetic disabled metadata
     * before exercising TileEntityHopper.update itself. */
    gm_world_set_block_meta(r.world, x, y, z, 154, 13);
    CHECK(gm_runtime_static_container_set_slot(
              &r, 0, x, y, z, 0, 4, 1, 0)
              && gm_runtime_hopper_set_transfer_state(
                  &r, 0, x, y, z, 0, 0),
          "load powered hopper");
    tick(&r, 2);
    CHECK(container_at(&r, x, y, z, &source)
              && container_at(&r, x + 1, y, z, &destination)
              && source.slots[0].count == 1
              && isr_is_empty(&destination.slots[0])
              && source.transfer_cooldown == 0,
          "powered hopper normalizes elapsed cooldown but stays disabled");
    printf("P 2 %d %d %d %lld\n", source.slots[0].count,
           destination.slots[0].count, source.transfer_cooldown,
           source.ticked_game_time);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize pull-chain fixture");
    CHECK(gm_runtime_set_block(&r, x, y + 1, z, 154, 0)
              && gm_runtime_set_block(&r, x, y, z, 154, 5)
              && gm_runtime_set_block(&r, x + 1, y, z, 23, 2),
          "place vertical hopper chain");
    CHECK(gm_runtime_static_container_set_slot(
              &r, 0, x, y + 1, z, 0, 5, 2, 0)
              && gm_runtime_hopper_set_transfer_state(
                  &r, 0, x, y + 1, z, 0, 0)
              && gm_runtime_hopper_set_transfer_state(
                  &r, 0, x, y, z, 0, 0),
          "load and arm hopper chain");
    tick(&r, 1);
    CHECK(container_at(&r, x, y + 1, z, &source)
              && container_at(&r, x, y, z, &destination)
              && source.slots[0].count == 1
              && destination.slots[0].item == 5
              && destination.slots[0].count == 1,
          "lower hopper pulls one item after its outgoing phase");
    printf("C 1 %d %d %d %d %lld\n", source.slots[0].count,
           destination.slots[0].count, source.transfer_cooldown,
           destination.transfer_cooldown, destination.ticked_game_time);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize dropped-item fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 154, 5)
              && gm_runtime_hopper_set_transfer_state(
                  &r, 0, x, y, z, 0, 0),
          "place and arm capture hopper");
    CHECK(gm_runtime_spawn_item_fixture(
              &r, 7001, x + 0.5, y + 1.0, z + 0.5,
              0.0, 0.0, 0.0, 264, 3, 0, 0, 20, 1),
          "spawn stationary capture stack");
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && source.slots[0].item == 264
              && source.slots[0].count == 3
              && r.entities.n_active == 0
              && source.transfer_cooldown == 8,
          "hopper captures the complete dropped stack and retires its entity");
    printf("I 1 %d %d %d\n", source.slots[0].count,
           r.entities.n_active == 0 ? 1 : 0, source.transfer_cooldown);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize brewing top-insertion fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 117, 0)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 0, 0, 0, 0)
              && gm_runtime_set_block(&r, x, y + 1, z, 154, 0)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y + 1, z, 0, TB_NETHER_WART, 1, 0)
              && gm_runtime_hopper_set_transfer_state(
                  &r, 0, x, y + 1, z, 0, 0),
          "place top hopper, stand, and ingredient");
    tick(&r, 1);
    CHECK(container_at(&r, x, y + 1, z, &source)
              && container_at(&r, x, y, z, &destination)
              && isr_is_empty(&source.slots[0])
              && destination.slots[3].item == TB_NETHER_WART
              && destination.slots[3].count == 1
              && source.transfer_cooldown == 8,
          "top face accepts only the brewing ingredient slot");
    printf("S U %d %d %d %d\n", source.slots[0].count,
           destination.slots[3].item, destination.slots[3].count,
           source.transfer_cooldown);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize brewing side-potion fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 117, 0)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 0, 0, 0, 0)
              && gm_runtime_set_block(&r, x - 1, y, z, 154, 5)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x - 1, y, z, 0, TB_POTION, 1, TB_PT_WATER)
              && gm_runtime_hopper_set_transfer_state(
                  &r, 0, x - 1, y, z, 0, 0),
          "place side hopper and water potion");
    tick(&r, 1);
    CHECK(container_at(&r, x - 1, y, z, &source)
              && container_at(&r, x, y, z, &destination)
              && isr_is_empty(&source.slots[0])
              && destination.slots[0].item == TB_POTION
              && destination.slots[0].meta == TB_PT_WATER
              && source.transfer_cooldown == 8,
          "side face accepts a potion into the first empty bottle slot");
    printf("S P %d %d %d\n", source.slots[0].count,
           destination.slots[0].item == TB_POTION
               && destination.slots[0].meta == TB_PT_WATER,
           source.transfer_cooldown);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize brewing side-fuel fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 117, 0)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 0, 0, 0, 0)
              && gm_runtime_set_block(&r, x - 1, y, z, 154, 5)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x - 1, y, z, 0, TB_BLAZE_POWDER, 1, 0)
              && gm_runtime_hopper_set_transfer_state(
                  &r, 0, x - 1, y, z, 0, 0)
              && gm_runtime_restore_tickable_tile_order(
                  &r, 0, x - 1, y, z)
              && gm_runtime_restore_tickable_tile_order(
                  &r, 1, x, y, z),
          "place side hopper and blaze powder");
    tick(&r, 1);
    CHECK(container_at(&r, x - 1, y, z, &source)
              && container_at(&r, x, y, z, &destination)
              && isr_is_empty(&source.slots[0])
              && isr_is_empty(&destination.slots[4])
              && destination.brewing.fuel == TB_FUEL_CHARGE
              && source.transfer_cooldown == 8,
          "side fuel inserts before the stand consumes one powder");
    printf("S F %d %d %d %d\n", source.slots[0].count,
           destination.slots[4].count, destination.brewing.fuel,
           source.transfer_cooldown);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize brewing bottom-output fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 117, 0)
              && gm_runtime_brewing_set_slot(
                  &r, 0, x, y, z, 0, TB_POTION, 1, TB_PT_WATER, 0, 0)
              && gm_runtime_set_block(&r, x, y - 1, z, 154, 0)
              && gm_runtime_hopper_set_transfer_state(
                  &r, 0, x, y - 1, z, 0, 0),
          "place bottom hopper and potion output");
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && container_at(&r, x, y - 1, z, &destination)
              && isr_is_empty(&source.slots[0])
              && destination.slots[0].item == TB_POTION
              && destination.slots[0].meta == TB_PT_WATER
              && destination.transfer_cooldown == 8,
          "bottom face extracts a potion output");
    printf("S O %d %d %d\n", source.slots[0].count,
           destination.slots[0].item == TB_POTION
               && destination.slots[0].meta == TB_PT_WATER,
           destination.transfer_cooldown);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize brewing blocked-reagent fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 117, 0)
              && gm_runtime_brewing_set_slot(
                  &r, 0, x, y, z, 3, TB_NETHER_WART, 1, 0, 0, 0)
              && gm_runtime_set_block(&r, x, y - 1, z, 154, 0)
              && gm_runtime_hopper_set_transfer_state(
                  &r, 0, x, y - 1, z, 0, 0),
          "place bottom hopper under ordinary reagent");
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && container_at(&r, x, y - 1, z, &destination)
              && source.slots[3].count == 1
              && isr_is_empty(&destination.slots[0])
              && destination.transfer_cooldown == 0,
          "bottom face rejects ordinary ingredient extraction");
    printf("S R %d %d %d\n", source.slots[3].count,
           destination.slots[0].count, destination.transfer_cooldown);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize brewing glass-container fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 117, 0)
              && gm_runtime_brewing_set_slot(
                  &r, 0, x, y, z, 3, TB_GLASS_BOTTLE, 1, 0, 0, 0)
              && gm_runtime_set_block(&r, x, y - 1, z, 154, 0)
              && gm_runtime_hopper_set_transfer_state(
                  &r, 0, x, y - 1, z, 0, 0),
          "place bottom hopper under ingredient container item");
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && container_at(&r, x, y - 1, z, &destination)
              && isr_is_empty(&source.slots[3])
              && destination.slots[0].item == TB_GLASS_BOTTLE
              && destination.transfer_cooldown == 8,
          "bottom face extracts the ingredient glass-bottle exception");
    printf("S G %d %d %d\n", source.slots[3].count,
           destination.slots[0].item == TB_GLASS_BOTTLE,
           destination.transfer_cooldown);
    gm_runtime_destroy(&r);

    {
        GmRuntimeFurnace furnace;
        CHECK(init_flat(&r), "initialize top-furnace hopper fixture");
        CHECK(gm_runtime_set_block(&r, x, y, z, 61, 2)
                  && gm_runtime_furnace_set_slot(
                      &r, 0, x, y, z, 0, 0, 0, 0, 0, 0, 0, 200)
                  && gm_runtime_set_block(&r, x, y + 1, z, 154, 0)
                  && gm_runtime_static_container_set_slot(
                      &r, 0, x, y + 1, z, 0, 15, 2, 0)
                  && gm_runtime_hopper_set_transfer_state(
                      &r, 0, x, y + 1, z, 0, 0),
              "place top hopper and empty furnace");
        tick(&r, 1);
        CHECK(container_at(&r, x, y + 1, z, &source)
                  && furnace_at(&r, x, y, z, &furnace)
                  && source.slots[0].count == 1
                  && furnace.state.input.item == 15
                  && furnace.state.input.count == 1,
              "top hopper inserts only into furnace input");
        printf("F U %d %d %d %d %d %d\n",
               source.slots[0].count,
               furnace.state.input.item, furnace.state.input.count,
               furnace.state.fuel.count, furnace.state.output.count,
               source.transfer_cooldown);
        gm_runtime_destroy(&r);

        CHECK(init_flat(&r), "initialize side-furnace hopper fixture");
        CHECK(gm_runtime_set_block(&r, x, y, z, 61, 2)
                  && gm_runtime_furnace_set_slot(
                      &r, 0, x, y, z, 0, 0, 0, 0, 0, 0, 0, 200)
                  && gm_runtime_set_block(&r, x - 1, y, z, 154, 5)
                  && gm_runtime_static_container_set_slot(
                      &r, 0, x - 1, y, z, 0, 263, 2, 0)
                  && gm_runtime_hopper_set_transfer_state(
                      &r, 0, x - 1, y, z, 0, 0),
              "place side hopper and empty furnace");
        tick(&r, 1);
        CHECK(container_at(&r, x - 1, y, z, &source)
                  && furnace_at(&r, x, y, z, &furnace)
                  && source.slots[0].count == 1
                  && furnace.state.fuel.item == 263
                  && furnace.state.fuel.count == 1,
              "side hopper inserts only furnace fuel");
        printf("F S %d %d %d %d\n",
               source.slots[0].count,
               furnace.state.fuel.item, furnace.state.fuel.count,
               source.transfer_cooldown);
        gm_runtime_destroy(&r);

        CHECK(init_flat(&r), "initialize bottom-furnace output fixture");
        CHECK(gm_runtime_set_block(&r, x, y + 1, z, 61, 2)
                  && gm_runtime_furnace_set_slot(
                      &r, 0, x, y + 1, z, 2, 265, 2, 0,
                      0, 0, 0, 200)
                  && gm_runtime_set_block(&r, x, y, z, 154, 5)
                  && gm_runtime_hopper_set_transfer_state(
                      &r, 0, x, y, z, 0, 0),
              "place bottom hopper under furnace output");
        tick(&r, 1);
        CHECK(container_at(&r, x, y, z, &destination)
                  && furnace_at(&r, x, y + 1, z, &furnace)
                  && furnace.state.output.count == 1
                  && destination.slots[0].item == 265
                  && destination.slots[0].count == 1,
              "bottom hopper extracts furnace output first");
        printf("F O %d %d %d %d\n",
               furnace.state.output.count,
               destination.slots[0].item, destination.slots[0].count,
               destination.transfer_cooldown);
        gm_runtime_destroy(&r);

        CHECK(init_flat(&r), "initialize bottom-furnace bucket fixture");
        CHECK(gm_runtime_set_block(&r, x, y + 1, z, 61, 2)
                  && gm_runtime_furnace_set_slot(
                      &r, 0, x, y + 1, z, 1, IC_WATER_BUCKET, 1, 0,
                      0, 0, 0, 200)
                  && gm_runtime_set_block(&r, x, y, z, 154, 5)
                  && gm_runtime_hopper_set_transfer_state(
                      &r, 0, x, y, z, 0, 0),
              "place bottom hopper under furnace water bucket");
        tick(&r, 1);
        CHECK(container_at(&r, x, y, z, &destination)
                  && furnace_at(&r, x, y + 1, z, &furnace)
                  && furnace.state.fuel.count == 0
                  && destination.slots[0].item == IC_WATER_BUCKET,
              "bottom hopper extracts furnace fuel-slot water bucket");
        printf("F W %d %d %d %d\n",
               furnace.state.fuel.count,
               destination.slots[0].item, destination.slots[0].count,
               destination.transfer_cooldown);
        gm_runtime_destroy(&r);

        CHECK(init_flat(&r), "initialize bottom-furnace rejection fixture");
        CHECK(gm_runtime_set_block(&r, x, y + 1, z, 61, 2)
                  && gm_runtime_furnace_set_slot(
                      &r, 0, x, y + 1, z, 1, 263, 1, 0,
                      0, 0, 0, 200)
                  && gm_runtime_set_block(&r, x, y, z, 154, 5)
                  && gm_runtime_hopper_set_transfer_state(
                      &r, 0, x, y, z, 0, 0),
              "place bottom hopper under ordinary furnace fuel");
        tick(&r, 1);
        CHECK(container_at(&r, x, y, z, &destination)
                  && furnace_at(&r, x, y + 1, z, &furnace)
                  && furnace.state.fuel.count == 1
                  && isr_is_empty(&destination.slots[0])
                  && destination.transfer_cooldown == 0,
              "bottom hopper rejects ordinary furnace fuel");
        printf("F R %d %d %d\n",
               furnace.state.fuel.count,
               destination.slots[0].count,
               destination.transfer_cooldown);
        gm_runtime_destroy(&r);
    }

    {
        GmRuntimeChest west, east;
        CHECK(init_flat(&r), "initialize double-chest insertion fixture");
        CHECK(gm_runtime_set_block(&r, x, y, z, 54, 2)
                  && gm_runtime_set_block(&r, x + 1, y, z, 54, 2)
                  && gm_runtime_chest_set_slot(
                      &r, 0, x, y, z, 0, 0, 0, 0)
                  && gm_runtime_chest_set_slot(
                      &r, 0, x + 1, y, z, 0, 0, 0, 0)
                  && gm_runtime_set_block(&r, x - 1, y, z, 154, 5)
                  && gm_runtime_static_container_set_slot(
                      &r, 0, x - 1, y, z, 0, 4, 2, 0)
                  && gm_runtime_hopper_set_transfer_state(
                      &r, 0, x - 1, y, z, 0, 0),
              "place west-to-east hopper and double chest");
        tick(&r, 1);
        CHECK(container_at(&r, x - 1, y, z, &source)
                  && chest_at(&r, x, y, z, &west)
                  && chest_at(&r, x + 1, y, z, &east)
                  && source.slots[0].count == 1
                  && chest_live_get(&west.state, 0).count == 1
                  && chest_live_get(&east.state, 0).count == 0,
              "double chest inserts into canonical west half first");
        printf("H I %d %d %d %d\n",
               source.slots[0].count,
               chest_live_get(&west.state, 0).count,
               chest_live_get(&east.state, 0).count,
               source.transfer_cooldown);
        gm_runtime_destroy(&r);

        CHECK(init_flat(&r), "initialize double-chest next-half fixture");
        CHECK(gm_runtime_set_block(&r, x, y, z, 54, 2)
                  && gm_runtime_set_block(&r, x + 1, y, z, 54, 2)
                  && gm_runtime_chest_set_slot(
                      &r, 0, x + 1, y, z, 0, 0, 0, 0),
              "place double chest with materialized east half");
        for (int slot = 0; slot < 27; ++slot)
            CHECK(gm_runtime_chest_set_slot(
                      &r, 0, x, y, z, slot, 1, 64, 0),
                  "fill canonical west double-chest half");
        CHECK(gm_runtime_set_block(&r, x - 1, y, z, 154, 5)
                  && gm_runtime_static_container_set_slot(
                      &r, 0, x - 1, y, z, 0, 4, 2, 0)
                  && gm_runtime_hopper_set_transfer_state(
                      &r, 0, x - 1, y, z, 0, 0),
              "load hopper beside full west half");
        tick(&r, 1);
        CHECK(container_at(&r, x - 1, y, z, &source)
                  && chest_at(&r, x, y, z, &west)
                  && chest_at(&r, x + 1, y, z, &east)
                  && chest_live_get(&west.state, 26).count == 64
                  && chest_live_get(&east.state, 0).count == 1,
              "double chest continues into east half after west fills");
        printf("H N %d %d %d %d %d\n",
               source.slots[0].count,
               chest_live_get(&west.state, 0).count,
               chest_live_get(&west.state, 26).count,
               chest_live_get(&east.state, 0).count,
               source.transfer_cooldown);
        gm_runtime_destroy(&r);

        CHECK(init_flat(&r), "initialize double-chest far-pull fixture");
        CHECK(gm_runtime_set_block(&r, x, y, z, 54, 2)
                  && gm_runtime_set_block(&r, x + 1, y, z, 54, 2)
                  && gm_runtime_chest_set_slot(
                      &r, 0, x, y, z, 0, 264, 3, 0)
                  && gm_runtime_chest_set_slot(
                      &r, 0, x + 1, y, z, 0, 0, 0, 0)
                  && gm_runtime_set_block(&r, x + 1, y - 1, z, 154, 5)
                  && gm_runtime_hopper_set_transfer_state(
                      &r, 0, x + 1, y - 1, z, 0, 0),
              "place hopper under east half with item in west half");
        tick(&r, 1);
        CHECK(container_at(&r, x + 1, y - 1, z, &destination)
                  && chest_at(&r, x, y, z, &west)
                  && chest_at(&r, x + 1, y, z, &east)
                  && chest_live_get(&west.state, 0).count == 2
                  && chest_live_get(&east.state, 0).count == 0
                  && destination.slots[0].item == 264
                  && destination.slots[0].count == 1,
              "hopper under east half pulls canonical west slot first");
        printf("H P %d %d %d %d %d\n",
               chest_live_get(&west.state, 0).count,
               chest_live_get(&east.state, 0).count,
               destination.slots[0].item,
               destination.slots[0].count,
               destination.transfer_cooldown);
        gm_runtime_destroy(&r);

        CHECK(init_flat(&r), "initialize addressed-half blocker fixture");
        CHECK(gm_runtime_set_block(&r, x, y, z, 54, 2)
                  && gm_runtime_set_block(&r, x + 1, y, z, 54, 2)
                  && gm_runtime_chest_set_slot(
                      &r, 0, x, y, z, 0, 0, 0, 0)
                  && gm_runtime_chest_set_slot(
                      &r, 0, x + 1, y, z, 0, 0, 0, 0)
                  && gm_runtime_set_block(&r, x, y + 1, z, 1, 0)
                  && gm_runtime_set_block(&r, x - 1, y, z, 154, 5)
                  && gm_runtime_static_container_set_slot(
                      &r, 0, x - 1, y, z, 0, 4, 2, 0)
                  && gm_runtime_hopper_set_transfer_state(
                      &r, 0, x - 1, y, z, 0, 0),
              "block the addressed double-chest half");
        tick(&r, 1);
        CHECK(container_at(&r, x - 1, y, z, &source)
                  && chest_at(&r, x, y, z, &west)
                  && chest_at(&r, x + 1, y, z, &east)
                  && source.slots[0].count == 1
                  && chest_live_get(&west.state, 0).count == 1
                  && chest_live_get(&east.state, 0).count == 0,
              "hopper ignores blocker above addressed chest half");
        printf("H B %d %d %d %d\n",
               source.slots[0].count,
               chest_live_get(&west.state, 0).count,
               chest_live_get(&east.state, 0).count,
               source.transfer_cooldown);
        gm_runtime_destroy(&r);

        CHECK(init_flat(&r), "initialize adjacent-half blocker fixture");
        CHECK(gm_runtime_set_block(&r, x, y, z, 54, 2)
                  && gm_runtime_set_block(&r, x + 1, y, z, 54, 2)
                  && gm_runtime_chest_set_slot(
                      &r, 0, x, y, z, 0, 0, 0, 0)
                  && gm_runtime_chest_set_slot(
                      &r, 0, x + 1, y, z, 0, 0, 0, 0)
                  && gm_runtime_set_block(&r, x + 1, y + 1, z, 1, 0)
                  && gm_runtime_set_block(&r, x - 1, y, z, 154, 5)
                  && gm_runtime_static_container_set_slot(
                      &r, 0, x - 1, y, z, 0, 4, 2, 0)
                  && gm_runtime_hopper_set_transfer_state(
                      &r, 0, x - 1, y, z, 0, 0),
              "block the adjacent double-chest half");
        tick(&r, 1);
        CHECK(container_at(&r, x - 1, y, z, &source)
                  && chest_at(&r, x, y, z, &west)
                  && chest_at(&r, x + 1, y, z, &east)
                  && source.slots[0].count == 1
                  && chest_live_get(&west.state, 0).count == 1
                  && chest_live_get(&east.state, 0).count == 0
                  && source.transfer_cooldown == 8,
              "hopper ignores blocker above adjacent chest half");
        printf("H Q %d %d %d %d\n",
               source.slots[0].count,
               chest_live_get(&west.state, 0).count,
               chest_live_get(&east.state, 0).count,
               source.transfer_cooldown);
        gm_runtime_destroy(&r);
    }

    {
        GmRuntimeChest west, east;
        CHECK(init_flat(&r), "initialize trapped-double-chest insertion fixture");
        CHECK(gm_runtime_set_block(&r, x, y, z, 146, 2)
                  && gm_runtime_set_block(&r, x + 1, y, z, 146, 2)
                  && gm_runtime_chest_set_slot(
                      &r, 0, x, y, z, 0, 0, 0, 0)
                  && gm_runtime_chest_set_slot(
                      &r, 0, x + 1, y, z, 0, 0, 0, 0)
                  && gm_runtime_set_block(&r, x - 1, y, z, 154, 5)
                  && gm_runtime_static_container_set_slot(
                      &r, 0, x - 1, y, z, 0, 4, 2, 0)
                  && gm_runtime_hopper_set_transfer_state(
                      &r, 0, x - 1, y, z, 0, 0),
              "place hopper beside trapped double chest");
        tick(&r, 1);
        CHECK(container_at(&r, x - 1, y, z, &source)
                  && chest_at(&r, x, y, z, &west)
                  && chest_at(&r, x + 1, y, z, &east)
                  && source.slots[0].count == 1
                  && chest_live_get(&west.state, 0).count == 1
                  && chest_live_get(&east.state, 0).count == 0,
              "trapped double chest inserts into canonical west half");
        printf("T I %d %d %d %d\n",
               source.slots[0].count,
               chest_live_get(&west.state, 0).count,
               chest_live_get(&east.state, 0).count,
               source.transfer_cooldown);
        gm_runtime_destroy(&r);

        CHECK(init_flat(&r), "initialize trapped-double-chest pull fixture");
        CHECK(gm_runtime_set_block(&r, x, y, z, 146, 2)
                  && gm_runtime_set_block(&r, x + 1, y, z, 146, 2)
                  && gm_runtime_chest_set_slot(
                      &r, 0, x, y, z, 0, 264, 3, 0)
                  && gm_runtime_chest_set_slot(
                      &r, 0, x + 1, y, z, 0, 0, 0, 0)
                  && gm_runtime_set_block(&r, x + 1, y - 1, z, 154, 5)
                  && gm_runtime_hopper_set_transfer_state(
                      &r, 0, x + 1, y - 1, z, 0, 0),
              "place hopper beneath east trapped-chest half");
        tick(&r, 1);
        CHECK(container_at(&r, x + 1, y - 1, z, &destination)
                  && chest_at(&r, x, y, z, &west)
                  && chest_at(&r, x + 1, y, z, &east)
                  && chest_live_get(&west.state, 0).count == 2
                  && chest_live_get(&east.state, 0).count == 0
                  && destination.slots[0].item == 264
                  && destination.slots[0].count == 1,
              "trapped double chest extracts canonical west slot first");
        printf("T P %d %d %d %d %d\n",
               chest_live_get(&west.state, 0).count,
               chest_live_get(&east.state, 0).count,
               destination.slots[0].item,
               destination.slots[0].count,
               destination.transfer_cooldown);
        gm_runtime_destroy(&r);
    }

    CHECK(init_flat(&r), "initialize shulker insertion fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 219, 0)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 0, 0, 0, 0)
              && gm_runtime_set_block(&r, x - 1, y, z, 154, 5)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x - 1, y, z, 0, 4, 2, 0)
              && gm_runtime_hopper_set_transfer_state(
                  &r, 0, x - 1, y, z, 0, 0),
          "place hopper beside empty shulker box");
    tick(&r, 1);
    CHECK(container_at(&r, x - 1, y, z, &source)
              && container_at(&r, x, y, z, &destination)
              && source.slots[0].count == 1
              && destination.slots[0].item == 4
              && destination.slots[0].count == 1,
          "shulker box accepts ordinary item insertion");
    printf("U I %d %d %d %d\n",
           source.slots[0].count,
           destination.slots[0].item, destination.slots[0].count,
           source.transfer_cooldown);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize nested-shulker rejection fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 219, 0)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 0, 0, 0, 0)
              && gm_runtime_set_block(&r, x - 1, y, z, 154, 5)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x - 1, y, z, 0, 219, 1, 0)
              && gm_runtime_hopper_set_transfer_state(
                  &r, 0, x - 1, y, z, 0, 0),
          "load shulker item beside shulker inventory");
    tick(&r, 1);
    CHECK(container_at(&r, x - 1, y, z, &source)
              && container_at(&r, x, y, z, &destination)
              && source.slots[0].count == 1
              && isr_is_empty(&destination.slots[0])
              && source.transfer_cooldown == 0,
          "shulker box rejects nested shulker item");
    printf("U R %d %d %d\n",
           source.slots[0].count,
           destination.slots[0].count,
           source.transfer_cooldown);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize shulker extraction fixture");
    CHECK(gm_runtime_set_block(&r, x, y + 1, z, 219, 0)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y + 1, z, 0, 264, 3, 0)
              && gm_runtime_set_block(&r, x, y, z, 154, 5)
              && gm_runtime_hopper_set_transfer_state(
                  &r, 0, x, y, z, 0, 0),
          "place hopper beneath filled shulker box");
    tick(&r, 1);
    CHECK(container_at(&r, x, y + 1, z, &source)
              && container_at(&r, x, y, z, &destination)
              && source.slots[0].count == 2
              && destination.slots[0].item == 264
              && destination.slots[0].count == 1,
          "hopper extracts ordinary shulker inventory item");
    printf("U P %d %d %d %d\n",
           source.slots[0].count,
           destination.slots[0].item, destination.slots[0].count,
           destination.transfer_cooldown);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize scheduled dropper fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 158, 13)
              && gm_runtime_set_block(&r, x + 1, y, z, 23, 2),
          "place triggered east dropper and destination inventory");
    CHECK(gm_runtime_static_container_set_slot(
              &r, 0, x, y, z, 0, 1, 2, 0),
          "load the one occupied dropper slot");
    CHECK(gm_runtime_schedule_tick(&r, x, y, z, 158, 1, 0, 0),
          "schedule exact dropper callback");
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && container_at(&r, x + 1, y, z, &destination)
              && source.slots[0].count == 1
              && destination.slots[0].item == 1
              && destination.slots[0].count == 1,
          "dropper inserts exactly one item into the facing inventory");
    printf("D 1 %d %d\n", source.slots[0].count,
           destination.slots[0].count);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize multi-slot dropper fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 158, 13)
              && gm_runtime_set_block(&r, x + 1, y, z, 23, 2)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 0, 1, 4, 0)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 3, 4, 4, 0)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 8, 5, 4, 0)
              && gm_runtime_set_dispenser_random_seed48(
                  &r, UINT64_C(0x13579bdf2468)),
          "load three occupied dropper slots and exact selector cursor");
    for (int run = 0; run < 7; ++run) {
        CHECK(gm_runtime_schedule_tick(
                  &r, x, y, z, 158, 1, 0, 0),
              "schedule multi-slot dropper callback");
        tick(&r, 1);
    }
    CHECK(container_at(&r, x, y, z, &source),
          "read multi-slot dropper result");
    printf("M D %d %d %d %llu\n",
           source.slots[0].count, source.slots[3].count,
           source.slots[8].count,
           (unsigned long long)r.dispenser_random_seed48);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize multi-slot dispenser fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 0, 1, 4, 0)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 3, 1, 4, 0)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 8, 1, 4, 0)
              && gm_runtime_set_dispenser_random_seed48(
                  &r, UINT64_C(0x2468ace13579)),
          "load three occupied dispenser slots and exact selector cursor");
    for (int run = 0; run < 7; ++run) {
        CHECK(gm_runtime_schedule_tick(
                  &r, x, y, z, 23, 1, 0, 0),
              "schedule multi-slot dispenser callback");
        tick(&r, 1);
    }
    CHECK(container_at(&r, x, y, z, &source),
          "read multi-slot dispenser result");
    printf("M E %d %d %d %llu\n",
           source.slots[0].count, source.slots[3].count,
           source.slots[8].count,
           (unsigned long long)r.dispenser_random_seed48);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize default dispenser fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13),
          "place triggered east dispenser");
    CHECK(gm_runtime_static_container_set_slot(
              &r, 0, x, y, z, 0, 1, 2, 0),
          "load the one occupied plain-stone dispenser slot");
    CHECK(gm_runtime_set_world_random_seed48(
              &r, (UINT64_C(123) ^ UINT64_C(0x5deece66d))
                  & ((UINT64_C(1) << 48) - 1))
              && gm_runtime_set_world_random_gaussian(&r, 0, 0.0),
          "seed exact dispenser World.rand state");
    r.math_random_seed48 = UINT64_C(0x123456789abc);
    r.next_entity_id = 9001;
    CHECK(gm_runtime_schedule_tick(&r, x, y, z, 23, 1, 0, 0),
          "schedule exact default dispenser callback");
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && source.slots[0].count == 1
              && r.entities.n_active == 1
              && gm_runtime_world_event_count(&r) == 2,
          "default dispenser ejects one live item and two world events");
    CHECK(gm_runtime_sound_event_count(&r) == 1
              && sound_id(&r, 0) == GM_SOUND_DISPENSER_DISPENSE,
          "default dispenser emits one resolved block sound");
    {
        const GmLiveEnt *entity = &r.entities.ents[0];
        printf(
            "E 1 %016llx %016llx %016llx %016llx %016llx %016llx "
            "%08x %08x %d %llu %d %016llx %d\n",
            dbits(entity->x), dbits(entity->y), dbits(entity->z),
            dbits(entity->mx), dbits(entity->my), dbits(entity->mz),
            fbits(entity->yaw), fbits(entity->hover_start),
            source.slots[0].count,
            (unsigned long long)r.world_random_seed48,
            r.world_random_have_gaussian,
            dbits(r.world_random_gaussian),
            gm_runtime_world_event_count(&r));
    }
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize TNT dispenser fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 0, 46, 2, 0),
          "load east-facing TNT dispenser");
    r.math_random_seed48 = UINT64_C(0x13579bdf2468);
    r.next_entity_id = 9101;
    CHECK(gm_runtime_schedule_tick(&r, x, y, z, 23, 1, 0, 0),
          "schedule TNT dispenser callback");
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && source.slots[0].count == 1
              && r.primed_tnt_count == 1,
          "TNT dispenser consumes one item and primes one entity");
    {
        const GmRuntimePrimedTnt *entity = NULL;
        int first, second;
        for (int i = 0; i < r.primed_tnt_cap; ++i)
            if (r.primed_tnt[i].active) entity = &r.primed_tnt[i];
        CHECK(entity != NULL, "find dispensed TNT entity");
        event_pair(&r, &first, &second);
        printf("X T %d %016llx %016llx %016llx %016llx %016llx "
               "%016llx %d %d %d\n",
               source.slots[0].count,
               dbits(entity->x), dbits(entity->y), dbits(entity->z),
               dbits(entity->vx), dbits(entity->vy), dbits(entity->vz),
               entity->fuse, first, second);
    }
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize fire-charge dispenser fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 0, 385, 2, 0)
              && gm_runtime_set_world_random_seed48(
                  &r, (UINT64_C(777) ^ UINT64_C(0x5deece66d))
                      & ((UINT64_C(1) << 48) - 1))
              && gm_runtime_set_world_random_gaussian(&r, 0, 0.0)
              && gm_runtime_set_next_fireball_random_state(
                  &r, UINT64_C(0x2468ace13579), 0, 0.0),
          "load and seed fire-charge dispenser");
    r.next_entity_id = 9201;
    CHECK(gm_runtime_schedule_tick(&r, x, y, z, 23, 1, 0, 0),
          "schedule fire-charge dispenser callback");
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && source.slots[0].count == 1,
          "fire-charge dispenser consumes one item");
    {
        const GmRuntimeProjectile *entity = NULL;
        int first, second;
        for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
            if (r.projectiles[i].active && r.projectiles[i].type == 3)
                entity = &r.projectiles[i];
        CHECK(entity != NULL, "find dispensed small fireball");
        event_pair(&r, &first, &second);
        printf("X C %d %016llx %016llx %016llx %llu %d %d %d\n",
               source.slots[0].count,
               dbits(entity->x), dbits(entity->y), dbits(entity->z),
               (unsigned long long)r.world_random_seed48,
               r.world_random_have_gaussian, first, second);
    }
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize potion dispenser fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 0, 438, 1, TB_PT_SWIFTNESS)
              && gm_runtime_set_next_potion_random_state(
                  &r, UINT64_C(0x369cf147258a), 0, 0.0),
          "load and seed splash-potion dispenser");
    r.next_entity_id = 9301;
    CHECK(gm_runtime_schedule_tick(&r, x, y, z, 23, 1, 0, 0),
          "schedule potion dispenser callback");
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && isr_is_empty(&source.slots[0]),
          "potion dispenser consumes one item");
    {
        const GmRuntimeProjectile *entity = NULL;
        int first, second;
        for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
            if (r.projectiles[i].active && r.projectiles[i].type == 6)
                entity = &r.projectiles[i];
        CHECK(entity != NULL && entity->potion_item == 438
                  && entity->potion_type == TB_PT_SWIFTNESS,
              "dispensed potion retains item and potion payload");
        event_pair(&r, &first, &second);
        printf("X P %d %d %d %d\n", source.slots[0].count,
               entity->potion_item, first, second);
    }
    gm_runtime_destroy(&r);

    {
        static const int items[3] = {344, 332, 384};
        static const int kinds[3] = {7, 8, 9};
        for (int q = 0; q < 3; ++q) {
            CHECK(init_flat(&r), "initialize throwable dispenser fixture");
            CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
                      && gm_runtime_static_container_set_slot(
                          &r, 0, x, y, z, 0, items[q], 2, 0)
                      && gm_runtime_set_next_potion_random_state(
                          &r, UINT64_C(0x4711a5c39d27) + q, 0, 0.0),
                  "load and seed throwable dispenser");
            r.next_entity_id = 9351 + q;
            CHECK(gm_runtime_schedule_tick(
                      &r, x, y, z, 23, 1, 0, 0),
                  "schedule throwable dispenser callback");
            tick(&r, 1);
            CHECK(container_at(&r, x, y, z, &source)
                      && source.slots[0].count == 1,
                  "throwable dispenser consumes one item");
            const GmRuntimeProjectile *entity = NULL;
            int first, second;
            for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
                if (r.projectiles[i].active
                        && r.projectiles[i].type == kinds[q])
                    entity = &r.projectiles[i];
            CHECK(entity != NULL && entity->potion_item == items[q],
                  "throwable dispenser retains projectile kind and item");
            event_pair(&r, &first, &second);
            printf("X Q %d %d %d %d %d\n", items[q],
                   source.slots[0].count, kinds[q], first, second);
            gm_runtime_destroy(&r);
        }
    }

    CHECK(init_flat(&r), "initialize firework dispenser fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 0, 401, 2,
                  ic_firework_meta(2, 0))
              && gm_runtime_set_next_firework_random_state(
                  &r, UINT64_C(0x48ace13579bd), 0, 0.0),
          "load and seed firework dispenser");
    r.next_entity_id = 9401;
    CHECK(gm_runtime_schedule_tick(&r, x, y, z, 23, 1, 0, 0),
          "schedule firework dispenser callback");
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && source.slots[0].count == 1
              && r.firework_count == 1,
          "firework dispenser consumes one item and launches one rocket");
    {
        const GmRuntimeFirework *entity = NULL;
        int first, second;
        for (int i = 0; i < GM_RUNTIME_FIREWORKS; ++i)
            if (r.fireworks[i].active) entity = &r.fireworks[i];
        CHECK(entity != NULL && entity->flight == 2 && entity->age == 1,
              "dispensed firework retains payload and advances once");
        event_pair(&r, &first, &second);
        printf("X F %d %d %d %d\n", source.slots[0].count,
               entity->age, first, second);
    }
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize bucket dispenser fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 0, IC_WATER_BUCKET, 1, 0),
          "load water-bucket dispenser");
    gm_runtime_set_time(&r, 1);
    r.tick = 1;
    CHECK(gm_runtime_schedule_tick(&r, x, y, z, 23, 1, 0, 0),
          "schedule bucket dispenser callback");
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && source.slots[0].item == IC_BUCKET
              && source.slots[0].count == 1
              && gm_world_block(r.world, x + 1, y, z) == 8,
          "water bucket places flowing source and becomes empty bucket");
    {
        int first, second;
        event_pair(&r, &first, &second);
        printf("X W %d %d %d %d %d\n", source.slots[0].item,
               source.slots[0].count,
               gm_world_block(r.world, x + 1, y, z), first, second);
    }
    gm_runtime_destroy(&r);

    {
        static const int target_blocks[5] = {8, 10, 1, 8, 8};
        static const int target_meta[5] = {0, 0, 0, 0, 0};
        static const int bucket_counts[5] = {1, 1, 1, 2, 2};
        static const int expected_items[5] = {
            IC_WATER_BUCKET, IC_LAVA_BUCKET, 0, IC_BUCKET, IC_BUCKET
        };
        for (int q = 0; q < 5; ++q) {
            int entity_item = 0;
            CHECK(init_flat(&r), "initialize empty-bucket dispenser fixture");
            CHECK(gm_runtime_set_block(
                      &r, x + 1, y, z, target_blocks[q], target_meta[q])
                      && gm_runtime_set_block(&r, x, y, z, 23, 13)
                      && gm_runtime_static_container_set_slot(
                          &r, 0, x, y, z, 0,
                          IC_BUCKET, bucket_counts[q], 0)
                      && gm_runtime_set_world_random_seed48(
                          &r, (UINT64_C(321) ^ UINT64_C(0x5deece66d))
                              & ((UINT64_C(1) << 48) - 1))
                      && gm_runtime_set_world_random_gaussian(&r, 0, 0.0),
                  "load empty bucket, fluid target, and exact RNG");
            if (q == 4) {
                for (int slot = 1; slot < 9; ++slot)
                    CHECK(gm_runtime_static_container_set_slot(
                              &r, 0, x, y, z, slot,
                              IC_BUCKET, 1, 0),
                          "fill remaining dispenser slots with buckets");
                CHECK(gm_runtime_set_dispenser_random_seed48(&r, 4),
                      "select stacked bucket from full dispenser");
            }
            r.math_random_seed48 = UINT64_C(0x102030405060);
            r.next_entity_id = 9601;
            CHECK(gm_runtime_schedule_tick(
                      &r, x, y, z, 23, 1, 0, 0),
                  "schedule empty-bucket dispenser callback");
            tick(&r, 1);
            CHECK(container_at(&r, x, y, z, &source),
                  "read empty-bucket dispenser result");
            for (int i = 0; i < GM_LIVE_MAX; ++i)
                if (r.entities.ents[i].active)
                    entity_item = r.entities.ents[i].item;
            if (q < 2) {
                CHECK(source.slots[0].item == expected_items[q]
                          && source.slots[0].count == 1
                          && gm_world_block(r.world, x + 1, y, z) == 0
                          && r.entities.n_active == 0,
                      "source fluid fills bucket without item ejection");
            } else if (q == 2) {
                CHECK(isr_is_empty(&source.slots[0])
                          && gm_world_block(r.world, x + 1, y, z) == 1
                          && gm_world_meta(r.world, x + 1, y, z) == 0
                          && r.entities.n_active == 1
                          && entity_item == IC_BUCKET,
                      "non-fluid target delegates to default bucket ejection");
            } else if (q == 3) {
                CHECK(source.slots[0].item == IC_BUCKET
                          && source.slots[0].count == 1
                          && source.slots[1].item == IC_WATER_BUCKET
                          && source.slots[1].count == 1
                          && gm_world_block(r.world, x + 1, y, z) == 0
                          && r.entities.n_active == 0,
                      "stacked bucket stores filled result in first empty slot");
            } else {
                CHECK(source.slots[0].item == IC_BUCKET
                          && source.slots[0].count == 1
                          && source.slots[1].item == IC_BUCKET
                          && source.slots[1].count == 1
                          && gm_world_block(r.world, x + 1, y, z) == 0
                          && r.entities.n_active == 1
                          && entity_item == IC_WATER_BUCKET,
                      "full dispenser ejects stacked bucket's filled result");
            }
            CHECK(source.slots[0].item == expected_items[q],
                  "empty-bucket selected-slot result item");
            printf("X K %d %d %d %d %d %d %d %d %d %llu %d %016llx "
                   "%d %d %d %d %d\n",
                   q, source.slots[0].item, source.slots[0].count,
                   source.slots[1].item, source.slots[1].count,
                   gm_world_block(r.world, x + 1, y, z),
                   gm_world_meta(r.world, x + 1, y, z),
                   r.entities.n_active, entity_item,
                   (unsigned long long)r.world_random_seed48,
                   r.world_random_have_gaussian,
                   dbits(r.world_random_gaussian),
                   gm_runtime_world_event_count(&r),
                   event_at(&r, 0), event_at(&r, 1),
                   event_at(&r, 2), event_at(&r, 3));
            gm_runtime_destroy(&r);
        }
    }

    {
        static const int items[6] = {4, 5, 263, 257, 351, 335};
        static const int counts[6] = {2, 2, 2, 1, 2, 1};
        static const int metas[6] = {0, 5, 1, 17, 1, 0};
        static const int targets[6] = {0, 0, 1, 0, 0, 0};
        for (int q = 0; q < 6; ++q) {
            const GmLiveEnt *entity = NULL;
            CHECK(init_flat(&r), "initialize default dispenser fixture");
            CHECK((!targets[q]
                        || gm_runtime_set_block(
                            &r, x + 1, y, z, targets[q], 0))
                      && gm_runtime_set_block(&r, x, y, z, 23, 13)
                      && gm_runtime_static_container_set_slot(
                          &r, 0, x, y, z, 0,
                          items[q], counts[q], metas[q])
                      && gm_runtime_set_world_random_seed48(
                          &r, (UINT64_C(321) ^ UINT64_C(0x5deece66d))
                              & ((UINT64_C(1) << 48) - 1))
                      && gm_runtime_set_world_random_gaussian(&r, 0, 0.0),
                  "load ordinary dispenser item and exact RNG");
            r.math_random_seed48 = UINT64_C(0x102030405060);
            r.next_entity_id = 9701 + q;
            CHECK(gm_runtime_schedule_tick(
                      &r, x, y, z, 23, 1, 0, 0),
                  "schedule default dispenser callback");
            tick(&r, 1);
            CHECK(container_at(&r, x, y, z, &source),
                  "read default dispenser result");
            for (int i = 0; i < GM_LIVE_MAX; ++i)
                if (r.entities.ents[i].active)
                    entity = &r.entities.ents[i];
            CHECK(source.slots[0].count == counts[q] - 1
                      && entity != NULL
                      && entity->item == items[q]
                      && entity->meta == metas[q]
                      && gm_world_block(r.world, x + 1, y, z) == targets[q]
                      && gm_runtime_world_event_count(&r) == 2,
                  "ordinary item delegates to exact default ejection");
            printf("V %d %d %d %d %d %llu %d %d %016llx %d %d\n",
                   q, source.slots[0].count, entity->item, entity->meta,
                   gm_world_block(r.world, x + 1, y, z),
                   (unsigned long long)r.world_random_seed48,
                   r.world_random_have_gaussian,
                   gm_runtime_world_event_count(&r),
                   dbits(r.world_random_gaussian),
                   event_at(&r, 0), event_at(&r, 1));
            gm_runtime_destroy(&r);
        }
    }

    {
        static const int minecart_items[9] = {
            328, 342, 343, 407, 408, 422, 328, 342, 422
        };
        static const int minecart_kinds[8] = {
            GM_MINECART_RIDEABLE, GM_MINECART_CHEST,
            GM_MINECART_FURNACE, GM_MINECART_TNT,
            GM_MINECART_HOPPER, GM_MINECART_COMMAND,
            GM_MINECART_RIDEABLE, GM_MINECART_CHEST
        };
        for (int q = 0; q < 9; ++q) {
            CHECK(init_flat(&r), "initialize minecart dispenser fixture");
            if (q < 6)
                CHECK(gm_runtime_set_block(&r, x + 1, y, z, 66, 1),
                      "place flat target rail");
            else if (q == 6)
                CHECK(gm_runtime_set_block(&r, x + 1, y, z, 66, 2),
                      "place ascending target rail");
            else if (q == 7)
                CHECK(gm_runtime_set_block(&r, x + 1, y - 1, z, 66, 2),
                      "place ascending lower rail");
            else
                CHECK(gm_runtime_set_block(&r, x + 1, y, z, 1, 0),
                      "place solid minecart fallback target");
            CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
                      && gm_runtime_static_container_set_slot(
                          &r, 0, x, y, z, 0,
                          minecart_items[q], 1, 0)
                      && gm_runtime_set_world_random_seed48(
                          &r, (UINT64_C(321) ^ UINT64_C(0x5deece66d))
                              & ((UINT64_C(1) << 48) - 1))
                      && gm_runtime_set_world_random_gaussian(&r, 0, 0.0),
                  "load minecart dispenser and exact RNG");
            r.math_random_seed48 = UINT64_C(0x102030405060);
            r.next_entity_id = 9801 + q;
            CHECK(gm_runtime_schedule_tick(
                      &r, x, y, z, 23, 1, 0, 0),
                  "schedule minecart dispenser callback");
            tick(&r, 1);
            CHECK(container_at(&r, x, y, z, &source)
                      && isr_is_empty(&source.slots[0]),
                  "minecart dispenser consumes one item");
            if (q < 8) {
                GmRuntimeMinecart cart;
                CHECK(gm_runtime_minecart_get(&r, 0, &cart)
                          && cart.kind == minecart_kinds[q]
                          && gm_runtime_world_event_count(&r) == 2,
                      "minecart dispenser creates exact cart kind");
                printf("Y C %d %d %d %016llx %016llx %016llx "
                       "%016llx %016llx %016llx %08x %08x %d %d %d\n",
                       q, source.slots[0].count, cart.kind,
                       dbits(cart.x), dbits(cart.y), dbits(cart.z),
                       dbits(cart.vx), dbits(cart.vy), dbits(cart.vz),
                       fbits(cart.yaw), fbits(cart.pitch),
                       gm_runtime_world_event_count(&r),
                       event_at(&r, 0), event_at(&r, 1));
            } else {
                const GmLiveEnt *entity = NULL;
                for (int i = 0; i < GM_LIVE_MAX; ++i)
                    if (r.entities.ents[i].active)
                        entity = &r.entities.ents[i];
                CHECK(entity != NULL && entity->item == 422
                          && entity->meta == 0
                          && gm_world_block(r.world, x + 1, y, z) == 1
                          && gm_runtime_world_event_count(&r) == 4,
                      "blocked minecart dispenser delegates to default");
                printf("Y D %d %d %d %d %llu %d %016llx %d "
                       "%d %d %d %d\n",
                       source.slots[0].count, entity->item, entity->meta,
                       gm_world_block(r.world, x + 1, y, z),
                       (unsigned long long)r.world_random_seed48,
                       r.world_random_have_gaussian,
                       dbits(r.world_random_gaussian),
                       gm_runtime_world_event_count(&r),
                       event_at(&r, 0), event_at(&r, 1),
                       event_at(&r, 2), event_at(&r, 3));
            }
            gm_runtime_destroy(&r);
        }
    }

    {
        for (int q = 0; q < 18; ++q) {
            int item = 219 + (q < 16 ? q : 0);
            CHECK(init_flat(&r), "initialize shulker dispenser fixture");
            if (q == 16)
                CHECK(gm_runtime_set_block(&r, x + 1, y - 1, z, 1, 0),
                      "place shulker support block");
            else if (q == 17)
                CHECK(gm_runtime_set_block(&r, x + 1, y, z, 1, 0),
                      "place blocked shulker target");
            CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
                      && gm_runtime_static_container_set_slot(
                          &r, 0, x, y, z, 0, item, 1, 0)
                      && gm_runtime_schedule_tick(
                          &r, x, y, z, 23, 1, 0, 0),
                  "load and schedule shulker dispenser");
            tick(&r, 1);
            CHECK(container_at(&r, x, y, z, &source),
                  "read shulker dispenser result");
            if (q < 17) {
                CHECK(isr_is_empty(&source.slots[0])
                          && gm_world_block(r.world, x + 1, y, z) == item
                          && gm_world_meta(r.world, x + 1, y, z)
                              == (q == 16 ? 1 : 5)
                          && container_at(
                              &r, x + 1, y, z, &destination),
                      "shulker dispenser places exact empty box");
            } else {
                CHECK(source.slots[0].item == item
                          && source.slots[0].count == 1
                          && gm_world_block(r.world, x + 1, y, z) == 1,
                      "blocked shulker dispenser preserves its item");
            }
            CHECK(gm_runtime_world_event_count(&r) == 2
                      && event_at(&r, 0) == (q == 17 ? 1001 : 1000)
                      && event_at(&r, 1) == 2000,
                  "shulker dispenser emits exact result events");
            printf("Z %d %d %d %d %d %d %d\n", q,
                   source.slots[0].count,
                   gm_world_block(r.world, x + 1, y, z),
                   gm_world_meta(r.world, x + 1, y, z),
                   gm_runtime_world_event_count(&r),
                   event_at(&r, 0), event_at(&r, 1));
            gm_runtime_destroy(&r);
        }
    }

    {
        for (int q = 0; q < 22; ++q) {
            int item = q < 20 ? 298 + q : q == 20 ? 442 : 443;
            int meta = q % 5;
            const GmLiveEnt *entity = NULL;
            CHECK(init_flat(&r), "initialize armor dispenser fixture");
            CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
                      && gm_runtime_static_container_set_slot(
                          &r, 0, x, y, z, 0, item, 1, meta)
                      && gm_runtime_set_world_random_seed48(
                          &r, (UINT64_C(321) ^ UINT64_C(0x5deece66d))
                              & ((UINT64_C(1) << 48) - 1))
                      && gm_runtime_set_world_random_gaussian(&r, 0, 0.0),
                  "load armor dispenser and exact RNG");
            r.math_random_seed48 = UINT64_C(0x102030405060);
            r.next_entity_id = 9901 + q;
            CHECK(gm_runtime_schedule_tick(
                      &r, x, y, z, 23, 1, 0, 0),
                  "schedule armor fallback callback");
            tick(&r, 1);
            CHECK(container_at(&r, x, y, z, &source),
                  "read armor dispenser result");
            for (int i = 0; i < GM_LIVE_MAX; ++i)
                if (r.entities.ents[i].active)
                    entity = &r.entities.ents[i];
            CHECK(isr_is_empty(&source.slots[0])
                      && entity != NULL && entity->item == item
                      && entity->meta == meta
                      && gm_runtime_world_event_count(&r) == 2,
                  "empty-target armor behavior delegates to default ejection");
            printf("O %d %d %d %d %llu %d %016llx %d %d %d\n",
                   q, source.slots[0].count, entity->item, entity->meta,
                   (unsigned long long)r.world_random_seed48,
                   r.world_random_have_gaussian,
                   dbits(r.world_random_gaussian),
                   gm_runtime_world_event_count(&r),
                   event_at(&r, 0), event_at(&r, 1));
            gm_runtime_destroy(&r);
        }
    }

    for (int q = 0; q < 22; ++q) {
        int item = q < 20 ? 298 + q : q == 20 ? 442 : 443;
        int meta = q % 5;
        int inventory_slot = armor_inventory_slot(item);
        ICStack worn;
        CHECK(init_flat(&r), "initialize player equipment dispenser fixture");
        gm_runtime_set_pose(&r, x + 1.5, y, z + 0.5, 0.0F, 0.0F);
        CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
                  && gm_runtime_static_container_set_slot(
                      &r, 0, x, y, z, 0, item, 1, meta)
                  && gm_runtime_set_world_random_seed48(
                      &r, (UINT64_C(321) ^ UINT64_C(0x5deece66d))
                          & ((UINT64_C(1) << 48) - 1))
                  && gm_runtime_set_world_random_gaussian(&r, 0, 0.0),
              "load player equipment dispenser and exact RNG");
        r.math_random_seed48 = UINT64_C(0x102030405060);
        r.next_entity_id = 9923 + q;
        CHECK(gm_runtime_schedule_tick(
                  &r, x, y, z, 23, 1, 0, 0),
              "schedule player equipment dispenser callback");
        tick(&r, 1);
        worn = isr_get_stack(&r.player.inv, inventory_slot);
        CHECK(container_at(&r, x, y, z, &source)
                  && isr_is_empty(&source.slots[0])
                  && worn.item == item && worn.count == 1
                  && worn.meta == meta && r.entities.n_active == 0
                  && gm_runtime_world_event_count(&r) == 2,
              "player target equips item and preserves empty-ejection bug");
        printf("Q E %d %d %d %d %d %d %d %d %llu %d %016llx "
               "%d %d %d\n",
               q, source.slots[0].count,
               armor_equipment_ordinal(item), worn.item, worn.meta,
               0, 0, 1,
               (unsigned long long)r.world_random_seed48,
               r.world_random_have_gaussian,
               dbits(r.world_random_gaussian),
               gm_runtime_world_event_count(&r),
               event_at(&r, 0), event_at(&r, 1));
        gm_runtime_destroy(&r);
    }

    {
        static const int occupied_items[6] = {
            298, 299, 300, 301, 442, 443
        };
        for (int q = 0; q < 6; ++q) {
            int item = occupied_items[q];
            int inventory_slot = armor_inventory_slot(item);
            ICStack worn, collected;
            CHECK(init_flat(&r),
                  "initialize occupied player equipment fixture");
            gm_runtime_set_pose(&r, x + 1.5, y, z + 0.5, 0.0F, 0.0F);
            isr_set_stack(&r.player.inv, inventory_slot, ic_mk(1, 1, 0));
            CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
                      && gm_runtime_static_container_set_slot(
                          &r, 0, x, y, z, 0, item, 1, q)
                      && gm_runtime_set_world_random_seed48(
                          &r, (UINT64_C(321) ^ UINT64_C(0x5deece66d))
                              & ((UINT64_C(1) << 48) - 1))
                      && gm_runtime_set_world_random_gaussian(&r, 0, 0.0),
                  "load occupied player equipment and exact RNG");
            r.math_random_seed48 = UINT64_C(0x102030405060);
            r.next_entity_id = 9945 + q;
            CHECK(gm_runtime_schedule_tick(
                      &r, x, y, z, 23, 1, 0, 0),
                  "schedule occupied player fallback callback");
            tick(&r, 1);
            worn = isr_get_stack(&r.player.inv, inventory_slot);
            collected = isr_get_stack(&r.player.inv, 0);
            CHECK(container_at(&r, x, y, z, &source)
                      && isr_is_empty(&source.slots[0])
                      && worn.item == 1 && worn.count == 1
                      && collected.item == item && collected.count == 1
                      && collected.meta == q && r.entities.n_active == 0
                      && gm_runtime_world_event_count(&r) == 2,
                  "occupied player fallback ejects then immediately collects");
            printf("Q F %d %d %d %d %d %d %d %llu %d %016llx "
                   "%d %d %d\n",
                   q, source.slots[0].count,
                   armor_equipment_ordinal(item), worn.item,
                   collected.item, collected.meta, 1,
                   (unsigned long long)r.world_random_seed48,
                   r.world_random_have_gaussian,
                   dbits(r.world_random_gaussian),
                   gm_runtime_world_event_count(&r),
                   event_at(&r, 0), event_at(&r, 1));
            gm_runtime_destroy(&r);
        }
    }

    CHECK(init_flat(&r), "initialize mob equipment fail-closed fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 0, 298, 1, 0)
              && gm_runtime_spawn_mob_fixture(
                  &r, EW_TYPE_ZOMBIE, 9951,
                  x + 1.5, y, z + 0.5,
                  0.0, 0.0, 0.0, 0.0F, 20.0F, 1, 0, 0, 0),
          "place represented mob in equipment target");
    CHECK(!gm_runtime_schedule_tick(&r, x, y, z, 23, 1, 0, 0)
              && container_at(&r, x, y, z, &source)
              && source.slots[0].item == 298
              && source.slots[0].count == 1
              && gm_runtime_world_event_count(&r) == 0,
          "reject unimplemented mob equipment mutation");
    gm_runtime_destroy(&r);

    for (int q = 0; q < 7; ++q) {
        int item = q < 6 ? 397 : 86;
        int meta = q < 6 ? q : 0;
        ICStack worn;
        CHECK(init_flat(&r), "initialize player headwear dispenser fixture");
        gm_runtime_set_pose(&r, x + 1.5, y, z + 0.5, 0.0F, 0.0F);
        CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
                  && gm_runtime_static_container_set_slot(
                      &r, 0, x, y, z, 0, item, 1, meta)
                  && gm_runtime_set_world_random_seed48(
                      &r, (UINT64_C(321) ^ UINT64_C(0x5deece66d))
                          & ((UINT64_C(1) << 48) - 1))
                  && gm_runtime_set_world_random_gaussian(&r, 0, 0.0),
              "load player headwear dispenser and exact RNG");
        r.math_random_seed48 = UINT64_C(0x102030405060);
        CHECK(gm_runtime_schedule_tick(
                  &r, x, y, z, 23, 1, 0, 0),
              "schedule player headwear callback");
        tick(&r, 1);
        worn = isr_get_stack(&r.player.inv, ISR_ARMOR_HEAD);
        CHECK(container_at(&r, x, y, z, &source)
                  && isr_is_empty(&source.slots[0])
                  && worn.item == item && worn.count == 1
                  && worn.meta == meta
                  && gm_runtime_world_event_count(&r) == 2
                  && event_at(&r, 0) == 1001,
              "singleton headwear equip retains optional failure event");
        printf("J E %d %d %d %d %d %d %llu %d %016llx %d %d %d\n",
               q, source.slots[0].count, item, meta, worn.item, worn.meta,
               (unsigned long long)r.world_random_seed48,
               r.world_random_have_gaussian,
               dbits(r.world_random_gaussian),
               gm_runtime_world_event_count(&r),
               event_at(&r, 0), event_at(&r, 1));
        gm_runtime_destroy(&r);
    }

    {
        static const int headwear[2] = {397, 86};
        static const int metadata[2] = {1, 0};
        for (int q = 0; q < 2; ++q) {
            ICStack worn;
            CHECK(init_flat(&r), "initialize stacked headwear fixture");
            gm_runtime_set_pose(&r, x + 1.5, y, z + 0.5, 0.0F, 0.0F);
            CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
                      && gm_runtime_static_container_set_slot(
                          &r, 0, x, y, z, 0,
                          headwear[q], 2, metadata[q])
                      && gm_runtime_set_world_random_seed48(
                          &r, (UINT64_C(321) ^ UINT64_C(0x5deece66d))
                              & ((UINT64_C(1) << 48) - 1))
                      && gm_runtime_set_world_random_gaussian(&r, 0, 0.0),
                  "load stacked headwear dispenser and exact RNG");
            r.math_random_seed48 = UINT64_C(0x102030405060);
            CHECK(gm_runtime_schedule_tick(
                      &r, x, y, z, 23, 1, 0, 0),
                  "schedule stacked headwear callback");
            tick(&r, 1);
            worn = isr_get_stack(&r.player.inv, ISR_ARMOR_HEAD);
            CHECK(container_at(&r, x, y, z, &source)
                      && source.slots[0].item == headwear[q]
                      && source.slots[0].count == 1
                      && source.slots[0].meta == metadata[q]
                      && worn.item == headwear[q] && worn.count == 1
                      && worn.meta == metadata[q]
                      && gm_runtime_world_event_count(&r) == 2
                      && event_at(&r, 0) == 1000,
                  "stacked headwear equip leaves one and reports success");
            printf("J S %d %d %d %d %d %d %llu %d %016llx %d %d %d\n",
                   q, source.slots[0].count, source.slots[0].item,
                   source.slots[0].meta, worn.item, worn.meta,
                   (unsigned long long)r.world_random_seed48,
                   r.world_random_have_gaussian,
                   dbits(r.world_random_gaussian),
                   gm_runtime_world_event_count(&r),
                   event_at(&r, 0), event_at(&r, 1));
            gm_runtime_destroy(&r);
        }
    }

    {
        static const int headwear[2] = {397, 86};
        static const int metadata[2] = {1, 0};
        for (int q = 0; q < 2; ++q) {
            ICStack worn;
            CHECK(init_flat(&r), "initialize empty headwear target fixture");
            CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
                      && gm_runtime_static_container_set_slot(
                          &r, 0, x, y, z, 0,
                          headwear[q], 1, metadata[q])
                      && gm_runtime_set_world_random_seed48(
                          &r, (UINT64_C(321) ^ UINT64_C(0x5deece66d))
                              & ((UINT64_C(1) << 48) - 1))
                      && gm_runtime_set_world_random_gaussian(&r, 0, 0.0),
                  "load empty headwear target and exact RNG");
            r.math_random_seed48 = UINT64_C(0x102030405060);
            CHECK(gm_runtime_schedule_tick(
                      &r, x, y, z, 23, 1, 0, 0),
                  "schedule empty headwear target callback");
            tick(&r, 1);
            CHECK(container_at(&r, x, y, z, &source)
                      && source.slots[0].item == headwear[q]
                      && source.slots[0].count == 1
                      && source.slots[0].meta == metadata[q]
                      && gm_runtime_world_event_count(&r) == 2,
                  "empty non-pattern headwear target preserves source");
            printf("J N %d %d %d %d %llu %d %016llx %d %d %d\n",
                   q, source.slots[0].count, source.slots[0].item,
                   source.slots[0].meta,
                   (unsigned long long)r.world_random_seed48,
                   r.world_random_have_gaussian,
                   dbits(r.world_random_gaussian),
                   gm_runtime_world_event_count(&r),
                   event_at(&r, 0), event_at(&r, 1));
            gm_runtime_destroy(&r);

            CHECK(init_flat(&r), "initialize occupied headwear target fixture");
            gm_runtime_set_pose(&r, x + 1.5, y, z + 0.5, 0.0F, 0.0F);
            isr_set_stack(
                &r.player.inv, ISR_ARMOR_HEAD, ic_mk(1, 1, 0));
            CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
                      && gm_runtime_static_container_set_slot(
                          &r, 0, x, y, z, 0,
                          headwear[q], 1, metadata[q])
                      && gm_runtime_set_world_random_seed48(
                          &r, (UINT64_C(321) ^ UINT64_C(0x5deece66d))
                              & ((UINT64_C(1) << 48) - 1))
                      && gm_runtime_set_world_random_gaussian(&r, 0, 0.0),
                  "load occupied headwear target and exact RNG");
            r.math_random_seed48 = UINT64_C(0x102030405060);
            CHECK(gm_runtime_schedule_tick(
                      &r, x, y, z, 23, 1, 0, 0),
                  "schedule occupied headwear target callback");
            tick(&r, 1);
            worn = isr_get_stack(&r.player.inv, ISR_ARMOR_HEAD);
            CHECK(container_at(&r, x, y, z, &source)
                      && source.slots[0].item == headwear[q]
                      && source.slots[0].count == 1
                      && source.slots[0].meta == metadata[q]
                      && worn.item == 1
                      && gm_runtime_world_event_count(&r) == 2,
                  "occupied headwear target preserves source and equipment");
            printf("J O %d %d %d %d %d %llu %d %016llx %d %d %d\n",
                   q, source.slots[0].count, source.slots[0].item,
                   source.slots[0].meta, worn.item,
                   (unsigned long long)r.world_random_seed48,
                   r.world_random_have_gaussian,
                   dbits(r.world_random_gaussian),
                   gm_runtime_world_event_count(&r),
                   event_at(&r, 0), event_at(&r, 1));
            gm_runtime_destroy(&r);
        }
    }

    {
        static const int headwear[2] = {397, 86};
        static const int metadata[2] = {1, 0};
        static const int pattern_block[2] = {88, 42};
        for (int q = 0; q < 2; ++q) {
            CHECK(init_flat(&r), "initialize summon-pattern guard fixture");
            CHECK(gm_runtime_set_block(&r, x + 1, y - 1, z,
                      pattern_block[q], 0)
                      && gm_runtime_set_block(&r, x, y, z, 23, 13)
                      && gm_runtime_static_container_set_slot(
                          &r, 0, x, y, z, 0,
                          headwear[q], 1, metadata[q]),
                  "place possible headwear summon pattern");
            CHECK(!gm_runtime_schedule_tick(
                      &r, x, y, z, 23, 1, 0, 0)
                      && container_at(&r, x, y, z, &source)
                      && source.slots[0].item == headwear[q]
                      && source.slots[0].count == 1,
                  "reject unimplemented wither/golem summon pattern");
            gm_runtime_destroy(&r);
        }
    }

    {
        static const int unsupported[1] = {383};
        for (int q = 0; q < 1; ++q) {
            CHECK(init_flat(&r), "initialize special dispenser guard");
            CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
                      && gm_runtime_static_container_set_slot(
                          &r, 0, x, y, z, 0, unsupported[q], 1, 0),
                  "load unimplemented registered dispenser behavior");
            CHECK(!gm_runtime_schedule_tick(
                      &r, x, y, z, 23, 1, 0, 0),
                  "reject unimplemented registered dispenser callback");
            CHECK(container_at(&r, x, y, z, &source)
                      && source.slots[0].item == unsupported[q]
                      && source.slots[0].count == 1
                      && r.entities.n_active == 0
                      && gm_runtime_world_event_count(&r) == 0,
                  "registered special behavior cannot fall through as default");
            gm_runtime_destroy(&r);
        }
    }

    {
        static const int crops[6] = {59, 141, 142, 207, 104, 105};
        static const int max_ages[6] = {7, 7, 7, 3, 7, 7};
        static const uint64_t high_seed = UINT64_C(0x23456789abcd);
        for (int crop_index = 0; crop_index < 6; ++crop_index) {
            const int max_age = max_ages[crop_index];
            const int ages[4] = {0, 0, max_age - 1, max_age};
            const uint64_t seeds[4] = {0, high_seed, high_seed, high_seed};
            for (int q = 0; q < 4; ++q) {
                const int mature = ages[q] == max_age;
                const int increase = crop_index == 3
                    ? (q == 0 ? 0 : 1)
                    : (q == 0 ? 2 : 4);
                const int expected_age = mature ? max_age
                    : ages[q] + increase > max_age ? max_age
                    : ages[q] + increase;
                const uint64_t expected_seed = mature ? seeds[q]
                    : q == 0 ? UINT64_C(11)
                    : UINT64_C(158853340877908);
                GmRuntimeWorldEvent event;
                CHECK(init_flat(&r),
                      "initialize crop bonemeal dispenser fixture");
                CHECK(gm_runtime_set_block(&r, x + 1, y - 1, z, 60, 7)
                          && gm_runtime_set_block(
                              &r, x + 1, y, z, crops[crop_index], ages[q])
                          && gm_runtime_set_block(&r, x, y, z, 23, 13)
                          && gm_runtime_static_container_set_slot(
                              &r, 0, x, y, z, 0, 351, 2, 15)
                          && gm_runtime_set_dispenser_random_seed48(&r, 0)
                          && gm_runtime_set_world_random_seed48(&r, seeds[q])
                          && gm_runtime_set_world_random_gaussian(&r, 0, 0.0)
                          && gm_runtime_schedule_tick(
                              &r, x, y, z, 23, 1, 0, 0),
                      "load and schedule crop bonemeal dispenser");
                tick(&r, 1);
                CHECK(container_at(&r, x, y, z, &source)
                          && source.slots[0].item == 351
                          && source.slots[0].count == (mature ? 2 : 1)
                          && source.slots[0].meta == 15
                          && gm_world_block(r.world, x + 1, y, z)
                              == crops[crop_index]
                          && gm_world_meta(r.world, x + 1, y, z)
                              == expected_age
                          && r.world_random_seed48 == expected_seed,
                      "crop bonemeal preserves exact source, age, and RNG");
                CHECK(gm_runtime_world_event_count(&r) == (mature ? 2 : 3)
                          && event_at(&r, 0) == (mature ? 1001 : 2005)
                          && event_at(&r, 1) == (mature ? 2000 : 1000)
                          && (mature || event_at(&r, 2) == 2000),
                      "crop bonemeal emits exact event sequence");
                CHECK(gm_runtime_world_event_get(
                          &r, mature ? 1 : 2, &event)
                          && event.x == x && event.y == y && event.z == z
                          && event.data == 5,
                      "crop bonemeal emits east dispenser particles");
                if (!mature)
                    CHECK(gm_runtime_world_event_get(&r, 0, &event)
                              && event.x == x + 1
                              && event.y == y && event.z == z
                              && event.data == 0,
                          "crop bonemeal emits target growth particles");
                gm_runtime_destroy(&r);
            }
        }

        {
            static const int facings[3] = {0, 2, 3};
            static const int support_dx[4] = {0, -1, 0, 1};
            static const int support_dz[4] = {1, 0, -1, 0};
            for (int facing_index = 0; facing_index < 3; ++facing_index) {
                const int facing = facings[facing_index];
                for (int age = 0; age <= 2; ++age) {
                    const int meta = (age << 2) | facing;
                    const int mature = age == 2;
                    GmRuntimeWorldEvent event;
                    CHECK(init_flat(&r),
                          "initialize cocoa bonemeal dispenser fixture");
                    CHECK(gm_runtime_set_block(
                              &r, x + 1 + support_dx[facing], y,
                              z + support_dz[facing], 17, 3)
                              && gm_runtime_set_block(
                                  &r, x + 1, y, z, 127, meta)
                              && gm_runtime_set_block(&r, x, y, z, 23, 13)
                              && gm_runtime_static_container_set_slot(
                                  &r, 0, x, y, z, 0, 351, 2, 15)
                              && gm_runtime_set_dispenser_random_seed48(&r, 0)
                              && gm_runtime_set_world_random_seed48(
                                  &r, high_seed)
                              && gm_runtime_set_world_random_gaussian(
                                  &r, 0, 0.0)
                              && gm_runtime_schedule_tick(
                                  &r, x, y, z, 23, 1, 0, 0),
                          "load and schedule cocoa bonemeal dispenser");
                    tick(&r, 1);
                    CHECK(container_at(&r, x, y, z, &source)
                              && source.slots[0].item == 351
                              && source.slots[0].count == (mature ? 2 : 1)
                              && source.slots[0].meta == 15
                              && gm_world_block(r.world, x + 1, y, z) == 127
                              && gm_world_meta(r.world, x + 1, y, z)
                                  == (mature ? meta : meta + 4)
                              && r.world_random_seed48 == high_seed,
                          "cocoa bonemeal preserves facing, source, and RNG");
                    CHECK(gm_runtime_world_event_count(&r)
                                  == (mature ? 2 : 3)
                              && event_at(&r, 0)
                                  == (mature ? 1001 : 2005)
                              && event_at(&r, 1)
                                  == (mature ? 2000 : 1000)
                              && (mature || event_at(&r, 2) == 2000),
                          "cocoa bonemeal emits exact event sequence");
                    CHECK(gm_runtime_world_event_get(
                              &r, mature ? 1 : 2, &event)
                              && event.x == x && event.y == y && event.z == z
                              && event.data == 5,
                          "cocoa bonemeal emits east dispenser particles");
                    gm_runtime_destroy(&r);
                }
            }
        }

        for (int plant_type = 0; plant_type <= 2; ++plant_type) {
            const int growable = plant_type != 0;
            GmRuntimeWorldEvent event;
            CHECK(init_flat(&r),
                  "initialize tall-grass bonemeal dispenser fixture");
            CHECK(gm_runtime_set_block(&r, x + 1, y - 1, z, 60, 7)
                      && gm_runtime_set_block(
                          &r, x + 1, y, z, 31, plant_type)
                      && gm_runtime_set_block(&r, x, y, z, 23, 13)
                      && gm_runtime_static_container_set_slot(
                          &r, 0, x, y, z, 0, 351, 2, 15)
                      && gm_runtime_set_dispenser_random_seed48(&r, 0)
                      && gm_runtime_set_world_random_seed48(&r, high_seed)
                      && gm_runtime_set_world_random_gaussian(&r, 0, 0.0)
                      && gm_runtime_schedule_tick(
                          &r, x, y, z, 23, 1, 0, 0),
                  "load and schedule tall-grass bonemeal dispenser");
            tick(&r, 1);
            CHECK(container_at(&r, x, y, z, &source)
                      && source.slots[0].item == 351
                      && source.slots[0].count == (growable ? 1 : 2)
                      && source.slots[0].meta == 15
                      && gm_world_block(r.world, x + 1, y, z)
                          == (growable ? 175 : 31)
                      && gm_world_meta(r.world, x + 1, y, z)
                          == (growable ? plant_type + 1 : 0)
                      && gm_world_block(r.world, x + 1, y + 1, z)
                          == (growable ? 175 : 0)
                      && gm_world_meta(r.world, x + 1, y + 1, z)
                          == (growable ? 10 : 0)
                      && r.world_random_seed48 == high_seed,
                  "tall-grass bonemeal preserves exact plant and RNG state");
            CHECK(gm_runtime_world_event_count(&r) == (growable ? 3 : 2)
                      && event_at(&r, 0) == (growable ? 2005 : 1001)
                      && event_at(&r, 1) == (growable ? 1000 : 2000)
                      && (!growable || event_at(&r, 2) == 2000),
                  "tall-grass bonemeal emits exact event sequence");
            CHECK(gm_runtime_world_event_get(
                      &r, growable ? 2 : 1, &event)
                      && event.x == x && event.y == y && event.z == z
                      && event.data == 5,
                  "tall-grass bonemeal emits east dispenser particles");
            gm_runtime_destroy(&r);
        }

        for (int tree_type = 0; tree_type < 6; ++tree_type) {
            const uint64_t seeds[2] = {0, high_seed};
            for (int q = 0; q < 2; ++q) {
                const int grows = q == 0;
                GmRuntimeWorldEvent event;
                CHECK(init_flat(&r),
                      "initialize sapling bonemeal dispenser fixture");
                CHECK(gm_runtime_set_block(
                          &r, x + 1, y - 1, z, 60, 7)
                          && gm_runtime_set_block(
                              &r, x + 1, y, z, 6, tree_type)
                          && gm_runtime_set_block(&r, x, y, z, 23, 13)
                          && gm_runtime_static_container_set_slot(
                              &r, 0, x, y, z, 0, 351, 2, 15)
                          && gm_runtime_set_dispenser_random_seed48(&r, 0)
                          && gm_runtime_set_world_random_seed48(
                              &r, seeds[q])
                          && gm_runtime_set_world_random_gaussian(
                              &r, 0, 0.0)
                          && gm_runtime_schedule_tick(
                              &r, x, y, z, 23, 1, 0, 0),
                      "load and schedule sapling bonemeal dispenser");
                tick(&r, 1);
                CHECK(container_at(&r, x, y, z, &source)
                          && source.slots[0].item == 351
                          && source.slots[0].count == 1
                          && source.slots[0].meta == 15
                          && gm_world_block(r.world, x + 1, y, z) == 6
                          && gm_world_meta(r.world, x + 1, y, z)
                              == tree_type + (grows ? 8 : 0)
                          && r.world_random_seed48
                              == (grows ? UINT64_C(11)
                                  : UINT64_C(158853340877908)),
                      "sapling bonemeal preserves type and exact RNG branch");
                CHECK(r.entities.n_active == 0
                          && gm_runtime_world_event_count(&r) == 3
                          && event_at(&r, 0) == 2005
                          && event_at(&r, 1) == 1000
                          && event_at(&r, 2) == 2000
                          && gm_runtime_world_event_get(&r, 2, &event)
                          && event.x == x && event.y == y && event.z == z
                          && event.data == 5,
                      "sapling bonemeal consumes and emits exact events");
                gm_runtime_destroy(&r);
            }
        }

        {
            /* Captured from the live 1.11.2 oracle over the complete bounded
             * output volume, including leaf CHECK_DECAY metadata. */
            static const int metadata[11] = {
                8, 9, 10, 11, 12, 13, 8, 9, 11, 13, 8
            };
            static const uint64_t seeds[11] = {
                0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0
            };
            static const int mega[11] = {
                0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0
            };
            static const int blocked[11] = {
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1
            };
            static const int target_block[11] = {
                17, 17, 17, 17, 162, 6, 17, 6, 6, 162, 6
            };
            static const int target_meta[11] = {
                0, 1, 2, 3, 0, 13, 0, 9, 11, 1, 8
            };
            static const uint64_t cursors[11] = {
                UINT64_C(71032958119949), UINT64_C(25979478236433),
                UINT64_C(71032958119949), UINT64_C(83935042429844),
                UINT64_C(137139456763464), UINT64_C(277363943098),
                UINT64_C(161694279895846), UINT64_C(102626409374399),
                UINT64_C(49720483695876), UINT64_C(246566694182415),
                UINT64_C(11718085204285)
            };
            static const uint64_t hashes[11] = {
                UINT64_C(0x264de0cb95f9f69d),
                UINT64_C(0x1455585c0e24290c),
                UINT64_C(0xf347515348b1311f),
                UINT64_C(0x7bfc8082de97c82b),
                UINT64_C(0xc33e0c154fec8835),
                UINT64_C(0x5a345ee6991c8e16),
                UINT64_C(0xa937e762fee22075),
                UINT64_C(0x7ade435f79987b4d),
                UINT64_C(0xb31bd3473b51b65d),
                UINT64_C(0x7ae85a83d3b4620a),
                UINT64_C(0xd41dcacfc7a0d9bd)
            };
            for (int q = 0; q < 11; ++q) {
                GmRuntimeWorldEvent event;
                int fixture_ok = 1;
                CHECK(init_flat(&r),
                      "initialize stage-one sapling bonemeal fixture");
                if (mega[q])
                    for (int dz = 0; dz <= 1; ++dz)
                        for (int dx = 0; dx <= 1; ++dx) {
                            if (dx == 0 && dz == 0) continue;
                            fixture_ok = fixture_ok && gm_runtime_set_block(
                                &r, x + 1 + dx, y - 1, z + dz, 3, 0);
                            fixture_ok = fixture_ok && gm_runtime_set_block(
                                &r, x + 1 + dx, y, z + dz,
                                6, metadata[q]);
                        }
                CHECK(fixture_ok
                          && gm_runtime_set_block(
                              &r, x + 1, y - 1, z, 3, 0)
                          && gm_runtime_set_block(
                              &r, x + 1, y, z, 6, metadata[q])
                          && (!blocked[q] || gm_runtime_set_block(
                              &r, x + 1, y + 4, z, 1, 0))
                          && gm_runtime_set_block(&r, x, y, z, 23, 13)
                          && gm_runtime_static_container_set_slot(
                              &r, 0, x, y, z, 0, 351, 2, 15)
                          && gm_runtime_set_dispenser_random_seed48(&r, 0)
                          && gm_runtime_set_world_random_seed48(
                              &r, seeds[q])
                          && gm_runtime_set_world_random_gaussian(
                              &r, 0, 0.0)
                          && gm_runtime_schedule_tick(
                              &r, x, y, z, 23, 1, 0, 0),
                      "load and schedule stage-one sapling bonemeal");
                tick(&r, 1);
                CHECK(container_at(&r, x, y, z, &source)
                          && source.slots[0].item == 351
                          && source.slots[0].count == 1
                          && source.slots[0].meta == 15
                          && gm_world_block(r.world, x + 1, y, z)
                              == target_block[q]
                          && gm_world_meta(r.world, x + 1, y, z)
                              == target_meta[q]
                          && gm_world_block(r.world, x + 1, y - 1, z) == 3
                          && gm_world_meta(r.world, x + 1, y - 1, z) == 0
                          && r.world_random_seed48 == cursors[q]
                          && sapling_tree_cuboid_hash(
                              &r, x + 1, y, z) == hashes[q],
                      "stage-one sapling matches exact Java volume and RNG");
                CHECK(r.entities.n_active == 0
                          && gm_runtime_world_event_count(&r) == 3
                          && event_at(&r, 0) == 2005
                          && event_at(&r, 1) == 1000
                          && event_at(&r, 2) == 2000
                          && gm_runtime_world_event_get(&r, 2, &event)
                          && event.x == x && event.y == y && event.z == z
                          && event.data == 5,
                      "stage-one sapling consumes and emits exact events");
                gm_runtime_destroy(&r);
            }
        }

        for (int tree_type = 0; tree_type < 6; ++tree_type) {
            const uint64_t seeds[2] = {0, high_seed};
            for (int q = 0; q < 2; ++q) {
                for (int offhand = 0; offhand <= 1; ++offhand) {
                    int slot;
                    ICStack held;
                    GmRuntimeWorldEvent event;
                    CHECK(init_flat(&r),
                          "initialize player sapling bonemeal fixture");
                    slot = offhand
                        ? ISR_OFFHAND_SLOT : r.player.inv.current_item;
                    CHECK(gm_runtime_set_block(
                              &r, x + 1, y - 1, z, 60, 7)
                              && gm_runtime_set_block(
                                  &r, x + 1, y, z, 6, tree_type)
                              && gm_runtime_set_world_random_seed48(
                                  &r, seeds[q]),
                          "load player sapling bonemeal fixture");
                    isr_set_stack(
                        &r.player.inv, slot, ic_mk(351, 2, 15));
                    CHECK(gm_runtime_player_apply_bonemeal(
                              &r, x + 1, y, z, slot, 0),
                          "apply player sapling bonemeal");
                    held = isr_get_stack(&r.player.inv, slot);
                    CHECK(held.item == 351 && held.count == 1
                              && held.meta == 15
                              && gm_world_block(r.world, x + 1, y, z) == 6
                              && gm_world_meta(r.world, x + 1, y, z)
                                  == tree_type + (q == 0 ? 8 : 0)
                              && r.world_random_seed48
                                  == (q == 0 ? UINT64_C(11)
                                      : UINT64_C(158853340877908))
                              && gm_runtime_world_event_count(&r) == 1
                              && gm_runtime_world_event_get(&r, 0, &event)
                              && event.id == 2005 && event.x == x + 1
                              && event.y == y && event.z == z,
                          "player sapling bonemeal matches hand/item/RNG/event");
                    gm_runtime_destroy(&r);
                }

                CHECK(init_flat(&r),
                      "initialize natural sapling random-tick fixture");
                CHECK(gm_runtime_set_block(
                          &r, x + 1, y - 1, z, 3, 0)
                          && gm_runtime_set_block(
                              &r, x + 1, y, z, 6, tree_type)
                          && gm_runtime_set_world_random_seed48(
                              &r, seeds[q])
                          && gm_runtime_random_tick_block(
                              &r, x + 1, y, z, 6),
                      "dispatch natural sapling random tick");
                CHECK(gm_world_block(r.world, x + 1, y, z) == 6
                          && gm_world_meta(r.world, x + 1, y, z)
                              == tree_type + (q == 0 ? 8 : 0)
                          && r.world_random_seed48
                              == (q == 0 ? UINT64_C(11)
                                  : UINT64_C(158853340877908))
                          && gm_runtime_world_event_count(&r) == 0,
                      "natural stage-zero sapling matches Java RNG/state");
                gm_runtime_destroy(&r);
            }
        }

        {
            CHECK(init_flat(&r),
                  "initialize low-light natural sapling fixture");
            CHECK(gm_runtime_set_block(&r, x + 1, y - 1, z, 3, 0)
                      && gm_runtime_set_block(&r, x + 1, y, z, 6, 0)
                      && gm_runtime_set_block(&r, x + 1, y + 1, z, 1, 0)
                      && gm_runtime_set_world_random_seed48(&r, high_seed)
                      && gm_runtime_random_tick_block(
                          &r, x + 1, y, z, 6),
                  "dispatch low-light natural sapling random tick");
            CHECK(gm_world_block(r.world, x + 1, y, z) == 6
                      && gm_world_meta(r.world, x + 1, y, z) == 0
                      && r.world_random_seed48 == high_seed
                      && gm_runtime_world_event_count(&r) == 0,
                  "low-light sapling exits before Java RNG/growth");
            gm_runtime_destroy(&r);
        }

        {
            static const int metadata[11] = {
                8, 9, 10, 11, 12, 13, 8, 8, 13, 9, 11
            };
            static const uint64_t seeds[11] = {
                0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0
            };
            static const int mega[11] = {
                0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0
            };
            static const int blocked[11] = {
                0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0
            };
            static const int target_block[11] = {
                17, 17, 17, 17, 162, 162, 17, 6, 6, 17, 17
            };
            static const int target_meta[11] = {
                0, 1, 2, 3, 0, 1, 0, 8, 13, 1, 3
            };
            static const uint64_t cursors[11] = {
                UINT64_C(71032958119949), UINT64_C(25707281917278),
                UINT64_C(71032958119949), UINT64_C(223001693843844),
                UINT64_C(137139456763464), UINT64_C(246566694182415),
                UINT64_C(161694279895846), UINT64_C(11718085204285),
                UINT64_C(277363943098), UINT64_C(25979478236433),
                UINT64_C(83935042429844)
            };
            static const uint64_t hashes[11] = {
                UINT64_C(0x264de0cb95f9f69d),
                UINT64_C(0x0098d9ef4aa1fcad),
                UINT64_C(0xf347515348b1311f),
                UINT64_C(0x0469364993f24382),
                UINT64_C(0xc33e0c154fec8835),
                UINT64_C(0x7ae85a83d3b4620a),
                UINT64_C(0xa937e762fee22075),
                UINT64_C(0xd41dcacfc7a0d9bd),
                UINT64_C(0x5a345ee6991c8e16),
                UINT64_C(0x1455585c0e24290c),
                UINT64_C(0x7bfc8082de97c82b)
            };
            for (int q = 0; q < 11; ++q) {
                for (int offhand = 0; offhand <= 1; ++offhand) {
                    int fixture_ok = 1;
                    int slot;
                    ICStack held;
                    GmRuntimeWorldEvent event;
                    CHECK(init_flat(&r),
                          "initialize player stage-one sapling fixture");
                    slot = offhand
                        ? ISR_OFFHAND_SLOT : r.player.inv.current_item;
                    if (mega[q])
                        for (int dz = 0; dz <= 1; ++dz)
                            for (int dx = 0; dx <= 1; ++dx) {
                                if (dx == 0 && dz == 0) continue;
                                fixture_ok = fixture_ok
                                    && gm_runtime_set_block(
                                        &r, x + 1 + dx, y - 1, z + dz,
                                        3, 0);
                                fixture_ok = fixture_ok
                                    && gm_runtime_set_block(
                                        &r, x + 1 + dx, y, z + dz,
                                        6, metadata[q]);
                            }
                    CHECK(fixture_ok
                              && gm_runtime_set_block(
                                  &r, x + 1, y - 1, z, 3, 0)
                              && gm_runtime_set_block(
                                  &r, x + 1, y, z, 6, metadata[q])
                              && (!blocked[q] || gm_runtime_set_block(
                                  &r, x + 1, y + 4, z, 1, 0))
                              && gm_runtime_set_world_random_seed48(
                                  &r, seeds[q]),
                          "load player stage-one sapling fixture");
                    isr_set_stack(
                        &r.player.inv, slot, ic_mk(351, 2, 15));
                    CHECK(gm_runtime_player_apply_bonemeal(
                              &r, x + 1, y, z, slot, 0),
                          "apply player stage-one sapling bonemeal");
                    held = isr_get_stack(&r.player.inv, slot);
                    CHECK(held.item == 351 && held.count == 1
                              && held.meta == 15
                              && gm_world_block(r.world, x + 1, y, z)
                                  == target_block[q]
                              && gm_world_meta(r.world, x + 1, y, z)
                                  == target_meta[q]
                              && r.world_random_seed48 == cursors[q]
                              && sapling_tree_cuboid_hash(
                                  &r, x + 1, y, z) == hashes[q]
                              && gm_runtime_world_event_count(&r) == 1
                              && gm_runtime_world_event_get(&r, 0, &event)
                              && event.id == 2005 && event.x == x + 1
                              && event.y == y && event.z == z,
                          "player stage-one sapling matches hand/volume/RNG");
                    gm_runtime_destroy(&r);
                }

                int fixture_ok = 1;
                CHECK(init_flat(&r),
                      "initialize natural stage-one sapling fixture");
                if (mega[q])
                    for (int dz = 0; dz <= 1; ++dz)
                        for (int dx = 0; dx <= 1; ++dx) {
                            if (dx == 0 && dz == 0) continue;
                            fixture_ok = fixture_ok && gm_runtime_set_block(
                                &r, x + 1 + dx, y - 1, z + dz, 3, 0);
                            fixture_ok = fixture_ok && gm_runtime_set_block(
                                &r, x + 1 + dx, y, z + dz,
                                6, metadata[q]);
                        }
                CHECK(fixture_ok
                          && gm_runtime_set_block(
                              &r, x + 1, y - 1, z, 3, 0)
                          && gm_runtime_set_block(
                              &r, x + 1, y, z, 6, metadata[q])
                          && (!blocked[q] || gm_runtime_set_block(
                              &r, x + 1, y + 4, z, 1, 0))
                          && gm_runtime_set_world_random_seed48(
                              &r, seeds[q])
                          && gm_runtime_random_tick_block(
                              &r, x + 1, y, z, 6),
                      "dispatch natural stage-one sapling random tick");
                CHECK(gm_world_block(r.world, x + 1, y, z)
                              == target_block[q]
                          && gm_world_meta(r.world, x + 1, y, z)
                              == target_meta[q]
                          && r.world_random_seed48 == cursors[q]
                          && sapling_tree_cuboid_hash(
                              &r, x + 1, y, z) == hashes[q]
                          && gm_runtime_world_event_count(&r) == 0,
                      "natural stage-one sapling matches Java volume/RNG");
                gm_runtime_destroy(&r);
            }
        }

        {
            const int target_x = 8, target_y = 78, target_z = 12;
            for (int offhand = 0; offhand <= 1; ++offhand) {
                int slot;
                ICStack held;
                GmAction action;
                GmRuntimeWorldEvent event;
                CHECK(init_flat(&r),
                      "initialize playable player-bonemeal fixture");
                slot = offhand
                    ? ISR_OFFHAND_SLOT : r.player.inv.current_item;
                CHECK(gm_runtime_set_block(
                          &r, target_x, target_y - 1, target_z, 60, 7)
                          && gm_runtime_set_block(
                              &r, target_x, target_y, target_z, 6, 0),
                      "load playable player-bonemeal target");
                tick(&r, 1);
                gm_runtime_set_pose(
                    &r, 8.5, 78.0, 8.5, 0.0F, 18.0F);
                isr_set_stack(
                    &r.player.inv, slot, ic_mk(351, 2, 15));
                CHECK(gm_runtime_set_world_random_seed48(&r, 0),
                      "seed playable player-bonemeal RNG");
                memset(&action, 0, sizeof action);
                action.do_place = 1;
                action.hotbar_sel = -1;
                gm_runtime_tick(&r, action);
                held = isr_get_stack(&r.player.inv, slot);
                CHECK(held.item == 351 && held.count == 1
                          && held.meta == 15
                          && gm_world_block(
                              r.world, target_x, target_y, target_z) == 6
                          && gm_world_meta(
                              r.world, target_x, target_y, target_z) == 8
                          && r.world_random_seed48 == UINT64_C(11)
                          && gm_runtime_world_event_count(&r) == 1
                          && gm_runtime_world_event_get(&r, 0, &event)
                          && event.id == 2005 && event.x == target_x
                          && event.y == target_y && event.z == target_z,
                      "main/offhand ray use applies exact player bonemeal");
                gm_runtime_destroy(&r);
            }
        }

        {
            static const int biomes[14] = {
                1, 1, 1, 4, 4, 4, 6, 6, 6, 132, 132, 132, 129, 134
            };
            static const uint64_t seeds[14] = {
                0, 1, UINT64_C(0x23456789abcd),
                0, 1, UINT64_C(0x23456789abcd),
                0, 1, UINT64_C(0x23456789abcd),
                0, 1, UINT64_C(0x23456789abcd), 0, 0
            };
            static const uint64_t cursors[14] = {
                UINT64_C(125628455012350), UINT64_C(10037850303051),
                UINT64_C(108432293825961),
                UINT64_C(125628455012350), UINT64_C(10037850303051),
                UINT64_C(108432293825961),
                UINT64_C(125628455012350), UINT64_C(10037850303051),
                UINT64_C(108432293825961),
                UINT64_C(125628455012350), UINT64_C(10037850303051),
                UINT64_C(108432293825961),
                UINT64_C(125628455012350), UINT64_C(125628455012350)
            };
            static const uint64_t hashes[14] = {
                UINT64_C(0xf1f6e08274a961d9),
                UINT64_C(0xfdaf4cf4a077939a),
                UINT64_C(0x6bc7e23c62a7e2a0),
                UINT64_C(0xdd5a65a9bf3363bb),
                UINT64_C(0x15e0b998fb167f7f),
                UINT64_C(0x4d1d1a081b50e173),
                UINT64_C(0x6c5dfde4a8e5e6a7),
                UINT64_C(0xbfe0b3db7e0432d7),
                UINT64_C(0xac85d496bc28b256),
                UINT64_C(0xee6fc48df8f4d3ec),
                UINT64_C(0x4ca62cae252929de),
                UINT64_C(0x5660a544a69cac4d),
                UINT64_C(0xf1f6e08274a961d9),
                UINT64_C(0x6c5dfde4a8e5e6a7)
            };
            for (int q = 0; q < 14; ++q) {
                GmRuntimeWorldEvent event;
                CHECK(init_flat(&r),
                      "initialize grass-spread bonemeal dispenser fixture");
                int platform_ok = 1;
                for (int dz = -7; dz <= 7; ++dz)
                    for (int dx = -7; dx <= 7; ++dx)
                        if ((dx != 0 || dz != 0)
                                && (dx != -1 || dz != 0))
                            platform_ok = platform_ok && gm_runtime_set_block(
                                &r, x + 1 + dx, y, z + dz, 2, 0);
                for (int dz = -7; dz <= 7; ++dz)
                    for (int dx = -7; dx <= 7; ++dx)
                        platform_ok = platform_ok && gm_world_debug_set_biome(
                            r.world, x + 1 + dx, z + dz, biomes[q]);
                CHECK(platform_ok
                          && gm_runtime_set_block(
                              &r, x + 1, y - 1, z, 3, 0)
                          && gm_runtime_set_block(&r, x + 1, y, z, 2, 0)
                          && gm_runtime_set_block(&r, x, y, z, 23, 13)
                          && gm_runtime_static_container_set_slot(
                              &r, 0, x, y, z, 0, 351, 2, 15)
                          && gm_runtime_set_dispenser_random_seed48(&r, 0)
                          && gm_runtime_set_world_random_seed48(
                              &r, seeds[q])
                          && gm_runtime_set_world_random_gaussian(
                              &r, 0, 0.0)
                          && gm_runtime_schedule_tick(
                              &r, x, y, z, 23, 1, 0, 0),
                      "load and schedule grass-spread bonemeal dispenser");
                tick(&r, 1);
                CHECK(container_at(&r, x, y, z, &source)
                          && source.slots[0].item == 351
                          && source.slots[0].count == 1
                          && source.slots[0].meta == 15
                          && gm_world_block(r.world, x + 1, y, z) == 2
                          && gm_world_biome(r.world, x + 1, z) == biomes[q]
                          && r.world_random_seed48 == cursors[q]
                          && grass_bonemeal_cuboid_hash(
                              &r, x + 1, y, z) == hashes[q],
                      "grass-spread bonemeal matches exact Java cuboid and RNG");
                CHECK(r.entities.n_active == 0
                          && gm_runtime_world_event_count(&r) == 3
                          && event_at(&r, 0) == 2005
                          && event_at(&r, 1) == 1000
                          && event_at(&r, 2) == 2000
                          && gm_runtime_world_event_get(&r, 2, &event)
                          && event.x == x && event.y == y && event.z == z
                          && event.data == 5,
                      "grass-spread bonemeal consumes and emits exact events");
                gm_runtime_destroy(&r);
            }
        }

        {
            static const uint64_t seeds[4] = {
                UINT64_C(0x23456789abcd), 0, 1, 0
            };
            static const int blocked[4] = {0, 0, 0, 1};
            static const uint64_t cursors[4] = {
                UINT64_C(158853340877908), UINT64_C(11718085204285),
                UINT64_C(245470556921330), UINT64_C(11718085204285)
            };
            static const uint64_t hashes[2][4] = {
                {
                    UINT64_C(0x3d475c67068dbbc7),
                    UINT64_C(0x31e70e22a1eac7e),
                    UINT64_C(0x5151800537ab0a6e),
                    UINT64_C(0xdc84a21564b80257)
                },
                {
                    UINT64_C(0xa91a231f76af5fd7),
                    UINT64_C(0x4371f92ebea8edca),
                    UINT64_C(0x152d4ca5ac31fa5a),
                    UINT64_C(0x09dcdd7118851947)
                }
            };
            for (int color = 0; color < 2; ++color)
                for (int q = 0; q < 4; ++q) {
                    int mushroom = 39 + color;
                    int generated = q == 1 || q == 2;
                    GmRuntimeWorldEvent event;
                    CHECK(init_flat(&r),
                          "initialize mushroom bonemeal dispenser fixture");
                    CHECK(gm_runtime_set_block(
                              &r, x + 1, y - 1, z, 3, 0)
                              && (!blocked[q] || gm_runtime_set_block(
                                  &r, x + 1, y + 4, z, 1, 0))
                              && gm_runtime_set_block(&r, x, y, z, 23, 13)
                              /* Stage the cold mushroom last. Dirt at this
                               * exposed height is not a valid bright-world
                               * support, so a later adjacent edit correctly
                               * tears it down. The fixture is testing the
                               * bonemeal callback, not that neighbor edge. */
                              && gm_runtime_set_block(
                                  &r, x + 1, y, z, mushroom, 0)
                              && gm_runtime_static_container_set_slot(
                                  &r, 0, x, y, z, 0, 351, 2, 15)
                              && gm_runtime_set_dispenser_random_seed48(&r, 0)
                              && gm_runtime_set_world_random_seed48(
                                  &r, seeds[q])
                              && gm_runtime_set_world_random_gaussian(
                                  &r, 0, 0.0)
                              && gm_runtime_schedule_tick(
                                  &r, x, y, z, 23, 1, 0, 0),
                          "load and schedule mushroom bonemeal dispenser");
                    tick(&r, 1);
                    CHECK(container_at(&r, x, y, z, &source)
                              && source.slots[0].item == 351
                              && source.slots[0].count == 1
                              && source.slots[0].meta == 15
                              && gm_world_block(r.world, x + 1, y, z)
                                  == (generated ? 99 + color : mushroom)
                              && gm_world_meta(r.world, x + 1, y, z)
                                  == (generated ? 10 : 0)
                              && r.world_random_seed48 == cursors[q]
                              && mushroom_cuboid_hash(
                                  &r, x + 1, y, z) == hashes[color][q],
                          "mushroom bonemeal matches exact Java cuboid and RNG");
                    CHECK(r.entities.n_active == 0
                              && gm_runtime_world_event_count(&r) == 3
                              && event_at(&r, 0) == 2005
                              && event_at(&r, 1) == 1000
                              && event_at(&r, 2) == 2000
                              && gm_runtime_world_event_get(&r, 2, &event)
                              && event.x == x && event.y == y && event.z == z
                              && event.data == 5,
                          "mushroom bonemeal consumes and emits exact events");
                    gm_runtime_destroy(&r);
                }
        }

        {
            static const int plant_types[4] = {0, 1, 4, 5};
            static const uint64_t math_seed =
                UINT64_C(0x3456789abcde);
            for (int q = 0; q < 4; ++q) {
              for (int upper = 0; upper <= 1; ++upper) {
                const GmLiveEnt *drop = NULL;
                GmRuntimeWorldEvent event;
                CHECK(init_flat(&r),
                      "initialize double-plant bonemeal dispenser fixture");
                CHECK(gm_runtime_set_block(
                          &r, x + 1, y - 1 - upper, z, 60, 7)
                          && gm_runtime_set_block(
                              &r, x + 1, y - upper, z,
                              175, plant_types[q])
                          && gm_runtime_set_block(
                              &r, x + 1, y + 1 - upper, z, 175, 10)
                          && gm_runtime_set_block(&r, x, y, z, 23, 13)
                          && gm_runtime_static_container_set_slot(
                              &r, 0, x, y, z, 0, 351, 2, 15)
                          && gm_runtime_set_dispenser_random_seed48(&r, 0)
                          && gm_runtime_set_world_random_seed48(
                              &r, high_seed)
                          && gm_runtime_set_world_random_gaussian(
                              &r, 0, 0.0)
                          && gm_runtime_set_math_random_seed48(
                              &r, math_seed)
                          && gm_runtime_set_entity_id_cursor(&r, 760000)
                          && gm_runtime_schedule_tick(
                              &r, x, y, z, 23, 1, 0, 0),
                      "load and schedule double-plant bonemeal dispenser");
                tick(&r, 1);
                for (int i = 0; i < GM_LIVE_MAX; ++i)
                    if (r.entities.ents[i].active) {
                        CHECK(drop == NULL,
                              "double-plant bonemeal spawns one clone");
                        drop = &r.entities.ents[i];
                    }
                CHECK(container_at(&r, x, y, z, &source)
                          && source.slots[0].item == 351
                          && source.slots[0].count == 1
                          && source.slots[0].meta == 15
                          && gm_world_block(r.world, x + 1, y, z) == 175
                          && gm_world_meta(r.world, x + 1, y, z)
                              == (upper ? 10 : plant_types[q])
                          && gm_world_block(r.world, x + 1, y + 1, z)
                              == (upper ? 0 : 175)
                          && gm_world_meta(r.world, x + 1, y + 1, z)
                              == (upper ? 0 : 10)
                          && gm_world_block(r.world, x + 1, y - 1, z)
                              == (upper ? 175 : 60)
                          && gm_world_meta(r.world, x + 1, y - 1, z)
                              == (upper ? plant_types[q] : 7),
                      "double-plant bonemeal preserves plant and source");
                CHECK(drop != NULL && drop->eid == 760000
                          && drop->item == 175 && drop->count == 1
                          && drop->meta == plant_types[q]
                          && drop->age == 1 && drop->pickup_delay == 9
                          && drop->health == 5 && drop->lifespan == 6000
                          && drop->has_hover_start && !drop->on_ground
                          && dbits(drop->x - x)
                              == UINT64_C(0x3ff92cd47d000000)
                          && dbits(drop->y - y)
                              == UINT64_C(0x3fdb177048000000)
                          && dbits(drop->z - z)
                              == UINT64_C(0x3fe1625cf0400000)
                          && dbits(drop->mx)
                              == UINT64_C(0x3fa4b4776b3da500)
                          && dbits(drop->my)
                              == UINT64_C(0x3fc41205cab6ae80)
                          && dbits(drop->mz)
                              == UINT64_C(0x3f7932d9c1302900)
                          && fbits(drop->yaw) == UINT32_C(0x4285638a)
                          && fbits(drop->hover_start)
                              == UINT32_C(0x3e4796e1),
                      "double-plant clone entity matches exact Java state");
                CHECK(r.world_random_seed48
                              == UINT64_C(161555181192494)
                          && r.math_random_seed48
                              == UINT64_C(16368555598054)
                          && r.next_entity_id == 760001,
                      "double-plant clone advances exact causal cursors");
                CHECK(gm_runtime_world_event_count(&r) == 3
                          && event_at(&r, 0) == 2005
                          && event_at(&r, 1) == 1000
                          && event_at(&r, 2) == 2000
                          && gm_runtime_world_event_get(&r, 2, &event)
                          && event.x == x && event.y == y && event.z == z
                          && event.data == 5,
                      "double-plant bonemeal emits exact events");
                gm_runtime_destroy(&r);
              }
            }
        }
    }

    {
        static const int chest_blocks[2] = {54, 146};
        static const int cover_blocks[4] = {0, 1, 44, 44};
        static const int cover_meta[4] = {0, 0, 0, 8};
        for (int chest_type = 0; chest_type < 2; ++chest_type) {
            for (int cover = 0; cover < 4; ++cover) {
                CHECK(init_flat(&r), "initialize chest obstruction fixture");
                CHECK(gm_runtime_set_block(
                          &r, x, y, z, chest_blocks[chest_type], 3),
                      "place chest obstruction target");
                if (cover_blocks[cover] != 0)
                    CHECK(gm_runtime_set_block(
                              &r, x, y + 1, z,
                              cover_blocks[cover], cover_meta[cover]),
                          "place chest cover state");
                {
                    int opened = gm_runtime_use_block(&r, x, y, z)
                        && r.container == 3;
                    CHECK(opened == (cover == 0 || cover == 3),
                          "single chest respects lower-face lid obstruction");
                    printf("K S %d %d %d\n",
                           chest_type, cover, opened);
                }
                gm_runtime_destroy(&r);
            }

            for (int blocked_half = 0; blocked_half < 2; ++blocked_half) {
                CHECK(init_flat(&r),
                      "initialize double chest obstruction fixture");
                CHECK(gm_runtime_set_block(
                          &r, x, y, z, chest_blocks[chest_type], 3)
                          && gm_runtime_set_block(
                              &r, x + 1, y, z,
                              chest_blocks[chest_type], 3)
                          && gm_runtime_set_block(
                              &r, x + blocked_half, y + 1, z, 1, 0),
                      "place covered double chest");
                {
                    int opened = gm_runtime_use_block(&r, x, y, z)
                        && r.container == 3;
                    CHECK(!opened,
                          "either covered double-chest half blocks player use");
                    printf("K D %d %d %d\n",
                           chest_type, blocked_half, opened);
                }
                gm_runtime_destroy(&r);
            }
            for (int pet_half = 0; pet_half < 2; ++pet_half) {
                CHECK(init_flat(&r),
                      "initialize sitting-ocelot chest fixture");
                CHECK(gm_runtime_set_block(
                          &r, x, y, z, chest_blocks[chest_type], 3)
                          && gm_runtime_set_block(
                              &r, x + 1, y, z,
                              chest_blocks[chest_type], 3),
                      "place ocelot-obstructed double chest");
                int pet_slot = gm_mobs_spawn(
                    &r.mobs, EW_TYPE_OCELOT,
                    x + pet_half + 0.5, y + 1.0, z + 0.5);
                CHECK(pet_slot > 0
                          && gm_mobs_set_tameable_state(
                              &r.mobs, r.mobs.a.id[pet_slot],
                              1, 1, 1, 2, 10.0F),
                      "spawn sitting ocelot over chest half");
                {
                    int opened = gm_runtime_use_block(&r, x, y, z)
                        && r.container == 3;
                    CHECK(!opened,
                          "sitting ocelot over either half blocks chest use");
                    printf("K O %d %d %d\n",
                           chest_type, pet_half, opened);
                }
                gm_runtime_destroy(&r);
            }
        }
    }

    {
        static const int boat_items[8] = {
            333, 444, 445, 446, 447, 448, 444, 448
        };
        static const int boat_types[7] = {0, 1, 2, 3, 4, 5, 1};
        for (int q = 0; q < 8; ++q) {
            CHECK(init_flat(&r), "initialize boat dispenser fixture");
            if (q < 6)
                CHECK(gm_runtime_set_block(&r, x + 1, y, z, 8, 0),
                      "place boat target water");
            else if (q == 6)
                CHECK(gm_runtime_set_block(&r, x + 1, y - 1, z, 8, 0),
                      "place lower boat target water");
            else
                CHECK(gm_runtime_set_block(&r, x + 1, y, z, 1, 0),
                      "place solid boat fallback target");
            CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
                      && gm_runtime_static_container_set_slot(
                          &r, 0, x, y, z, 0, boat_items[q], 1, 0)
                      && gm_runtime_set_world_random_seed48(
                          &r, (UINT64_C(321) ^ UINT64_C(0x5deece66d))
                              & ((UINT64_C(1) << 48) - 1))
                      && gm_runtime_set_world_random_gaussian(&r, 0, 0.0),
                  "load boat dispenser and exact RNG");
            r.math_random_seed48 = UINT64_C(0x102030405060);
            r.next_entity_id = 9501 + q;
            CHECK(gm_runtime_schedule_tick(
                      &r, x, y, z, 23, 1, 0, 0),
                  "schedule boat dispenser callback");
            tick(&r, 1);
            CHECK(container_at(&r, x, y, z, &source)
                      && isr_is_empty(&source.slots[0]),
                  "boat dispenser consumes one boat");
            if (q < 7) {
                const EwStore *store = r.mobs.current
                    ? &r.mobs.b : &r.mobs.a;
                int boat = -1;
                for (int i = 0; i < EW_MAX_ENTITIES; ++i)
                    if (store->alive[i] && store->type[i] == EW_TYPE_BOAT)
                        boat = i;
                CHECK(boat >= 0
                          && r.mobs.boat_variant[boat] == boat_types[q]
                          && gm_runtime_world_event_count(&r) == 2,
                      "find exact dispensed boat variant");
                printf("X B %d %d %d %016llx %016llx %016llx "
                       "%08x %d %d %d\n",
                       q, source.slots[0].count,
                       r.mobs.boat_variant[boat],
                       dbits(store->x[boat]), dbits(store->y[boat]),
                       dbits(store->z[boat]), fbits(store->yaw[boat]),
                       gm_runtime_world_event_count(&r),
                       event_at(&r, 0), event_at(&r, 1));
            } else {
                const GmLiveEnt *entity = NULL;
                for (int i = 0; i < GM_LIVE_MAX; ++i)
                    if (r.entities.ents[i].active)
                        entity = &r.entities.ents[i];
                CHECK(entity != NULL && entity->item == 448
                          && gm_world_block(r.world, x + 1, y, z) == 1
                          && gm_runtime_world_event_count(&r) == 4,
                      "blocked boat dispenser delegates to default");
                printf("X N %d %d %d %llu %d %016llx %d "
                       "%d %d %d %d\n",
                       source.slots[0].count, entity->item,
                       gm_world_block(r.world, x + 1, y, z),
                       (unsigned long long)r.world_random_seed48,
                       r.world_random_have_gaussian,
                       dbits(r.world_random_gaussian),
                       gm_runtime_world_event_count(&r),
                       event_at(&r, 0), event_at(&r, 1),
                       event_at(&r, 2), event_at(&r, 3));
            }
            gm_runtime_destroy(&r);
        }
    }

    {
        static const int targets[3] = {0, 1, 46};
        static const int expected[3] = {51, 1, 0};
        static const int damage[3] = {8, 7, 7};
        for (int q = 0; q < 3; ++q) {
            CHECK(init_flat(&r), "initialize flint dispenser fixture");
            CHECK(gm_runtime_set_block(&r, x + 1, y - 1, z, 1, 0)
                      && gm_runtime_set_block(
                          &r, x + 1, y, z, targets[q], 0)
                      && gm_runtime_set_block(&r, x, y, z, 23, 13)
                      && gm_runtime_static_container_set_slot(
                          &r, 0, x, y, z, 0, 259, 1, 7),
                  "load flint dispenser and target");
            CHECK(gm_runtime_schedule_tick(
                      &r, x, y, z, 23, 1, 0, 0),
                  "schedule flint dispenser callback");
            tick(&r, 1);
            CHECK(container_at(&r, x, y, z, &source)
                      && source.slots[0].count == 1
                      && source.slots[0].meta == damage[q]
                      && gm_world_block(r.world, x + 1, y, z) == expected[q],
                  "flint dispenser preserves exact target and durability");
            CHECK((q == 2 ? r.primed_tnt_count == 1
                          : r.primed_tnt_count == 0),
                  "flint TNT target primes only the TNT case");
            CHECK(gm_runtime_sound_event_count(&r) == 1
                      && sound_id(&r, 0) == (q == 1
                          ? GM_SOUND_DISPENSER_FAIL
                          : GM_SOUND_DISPENSER_DISPENSE),
                  "flint optional behavior resolves success/failure sound");
            {
                int first, second;
                event_pair(&r, &first, &second);
                printf("X L %d %d %d %d %d %d\n", q,
                       source.slots[0].count, source.slots[0].meta,
                       gm_world_block(r.world, x + 1, y, z),
                       first, second);
            }
            gm_runtime_destroy(&r);
        }
    }

    puts("hopper_live: PASS (cooldown, transfer, chain, power, item capture, "
         "brewing sidedness, multi-slot selection, empty bucket "
         "pickup/fallback)");
    return 0;
}
