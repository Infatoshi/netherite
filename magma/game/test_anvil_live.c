#include "game/runtime.h"

#include "container_click.h"

#include <stdio.h>

#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); return 1; } } while (0)

static int find_break_seed(void)
{
    for (int seed = 0; seed < 10000; ++seed) {
        JavaRandom random;
        jrand_set(&random, seed);
        if (jrand_float(&random) < 0.12F) return seed;
    }
    return -1;
}

int main(void)
{
    GmConfig cfg;
    GmRuntime r;
    GmRuntimeWorldEvent event;
    char err[256];
    const int x = 8, y = 5, z = 8;
    gm_config_defaults(&cfg);
    cfg.seed = 42;
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.enchanting = 1;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err), err);
    CHECK(gm_runtime_set_block(&r, x, y - 1, z, 1, 0), "anvil support");
    CHECK(gm_runtime_set_block(&r, x, y, z, 145, 0), "anvil block");
    gm_runtime_set_pose(&r, 8.5, 5.0, 6.5, 180.0f, 0.0f);
    CHECK(gm_runtime_use_block(&r, x, y, z), "open live anvil");
    CHECK(r.container == 6 && r.anvil.open, "anvil container state");

    {
        ICStack sword = ic_mk(276, 1, 1000);
        CHECK(gm_runtime_set_inventory_stack(&r, 9, sword), "worn sword");
        CHECK(gm_runtime_set_inventory(&r, 10, 264, 1, 0), "diamond material");
        r.player_xp_level = 30;
        CHECK(gm_container_click(&r, 9, 0, CC_CLICK_QUICK_MOVE), "shift sword");
        CHECK(gm_container_click(&r, 10, 0, CC_CLICK_QUICK_MOVE), "shift diamond");
        CHECK(r.anvil.slots[0].item == 276 && r.anvil.slots[1].item == 264
              && r.anvil.slots[2].item == 276 && r.anvil.slots[2].meta == 610
              && r.anvil.slots[2].repair_cost == 1
              && r.anvil.maximum_cost == 1 && r.anvil.material_cost == 1,
              "material repair preview");
        {
            int seed = find_break_seed();
            CHECK(seed >= 0, "degradation seed exists");
            jrand_set(&r.mobs.player_random, seed);
        }
        CHECK(gm_container_click(
                  &r, GMC_ANVIL0 + 2, 0, CC_CLICK_PICKUP),
              "take repaired output");
        {
            ICStack cursor = gm_player_cursor();
            CHECK(cursor.item == 276 && cursor.meta == 610
                  && cursor.repair_cost == 1, "cursor receives repaired stack");
        }
        CHECK(r.player_xp_level == 29 && isr_is_empty(&r.anvil.slots[0])
              && isr_is_empty(&r.anvil.slots[1])
              && r.anvil.maximum_cost == 0, "take consumes inputs and one level");
        CHECK(gm_world_block(r.world, x, y, z) == 145
              && gm_world_meta(r.world, x, y, z) == 4,
              "12 percent branch damages anvil one tier");
        CHECK(gm_runtime_world_event_count(&r) == 1
              && gm_runtime_world_event_get(&r, 0, &event)
              && event.id == 1030 && event.x == x && event.y == y && event.z == z,
              "anvil use event is ordered at the live block");
        gm_player_cursor_set(ic_empty());
    }

    /* Rename a stack, retain 63 inputs, and expose Java's post-take preview
     * with maximumCost reset to zero. */
    CHECK(gm_runtime_set_inventory(&r, 11, 1, 64, 0), "rename stack");
    CHECK(gm_container_click(&r, 11, 0, CC_CLICK_QUICK_MOVE), "shift rename input");
    CHECK(gm_runtime_anvil_set_name(&r, "Polished"), "set repaired name");
    {
        int name = gm_runtime_item_name_intern(&r, "Polished");
        CHECK(name > 0 && r.anvil.maximum_cost == 1
              && r.anvil.slots[2].item == 1
              && r.anvil.slots[2].count == 1
              && r.anvil.slots[2].custom_name == name,
              "rename preview and interned identity");
        r.player_xp_level = 5;
        jrand_set(&r.mobs.player_random, 0); /* nextFloat > 0.12 */
        CHECK(gm_container_click(
                  &r, GMC_ANVIL0 + 2, 0, CC_CLICK_PICKUP),
              "take renamed output");
        {
            ICStack cursor = gm_player_cursor();
            CHECK(cursor.item == 1 && cursor.count == 1
                  && cursor.custom_name == name,
                  "renamed output retains custom-name id");
        }
        CHECK(r.player_xp_level == 4 && r.anvil.slots[0].count == 63
              && r.anvil.slots[2].item == 1
              && r.anvil.maximum_cost == 0,
              "stack rename consumes one and resets cost after callback preview");
        CHECK(gm_world_meta(r.world, x, y, z) == 4
              && gm_runtime_world_event_count(&r) == 2
              && gm_runtime_world_event_get(&r, 1, &event)
              && event.id == 1030,
              "non-degrading repair still emits use event");
    }

    gm_container_close(&r);
    r.container = 0;
    r.anvil.open = 0;
    {
        int inventory_plain = 0;
        int dropped_plain = 0;
        int dropped_named = 0;
        for (int slot = 0; slot < 36; ++slot) {
            ICStack value = isr_get_stack(&r.player.inv, slot);
            if (value.item == 1 && value.custom_name == 0)
                inventory_plain += value.count;
        }
        for (int slot = 0; slot < GM_LIVE_MAX; ++slot) {
            const GmLiveEnt *entity = &r.entities.ents[slot];
            if (!entity->active || entity->type != 0 || entity->item != 1)
                continue;
            if (entity->custom_name == 0) dropped_plain += entity->count;
            else dropped_named += entity->count;
        }
        CHECK(inventory_plain == 0 && dropped_plain == 63
              && dropped_named == 1,
              "closing drops cursor then remaining input without reinsertion");
    }

    gm_runtime_destroy(&r);
    puts("PASS anvil live: repair/combine/rename/xp/degradation/events/container");
    return 0;
}
