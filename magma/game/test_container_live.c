/* test_container_live: the ONE shipped Container.slotClick path over the full
 * inventory + grid/result/furnace slot ids (game/container_live.h), driven both
 * directly and through the authoritative gm_runtime_tick action seam.
 * Expectations are ported from Container.java / ContainerPlayer.java /
 * ContainerWorkbench.java / ContainerFurnace.java (1.11.2 oracle source). */
#include "game/runtime.h"
#include "container_click.h"
#include "inventory_stack_rules.h"
#include "tile_entity_chest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } } while (0)

static ICStack slot(const GmRuntime *r, int s) { return isr_get_stack(&r->player.inv, s); }

static void click(GmRuntime *r, int s, int button, int type) {
    GmAction a; memset(&a, 0, sizeof a);
    a.inv_click = 1; a.inv_slot = s; a.inv_button = button; a.inv_type = type;
    a.hotbar_sel = -1;
    gm_runtime_tick(r, a);
}

static int live_item_count(const GmRuntime *r, int item) {
    int n = 0;
    for (int i = 0; i < GM_LIVE_MAX; ++i)
        if (r->entities.ents[i].active && r->entities.ents[i].type == 0 &&
            r->entities.ents[i].item == item)
            n += r->entities.ents[i].count;
    return n;
}

static int live_xp_total(const GmRuntime *r) {
    int total = 0;
    for (int i = 0; i < GM_XP_ORBS; ++i)
        if (!r->mobs.xp_orbs[i].dead && r->mobs.xp_orbs[i].xpValue > 0)
            total += r->mobs.xp_orbs[i].xpValue;
    return total;
}

static int live_xp_count(const GmRuntime *r) {
    int count = 0;
    for (int i = 0; i < GM_XP_ORBS; ++i)
        if (!r->mobs.xp_orbs[i].dead && r->mobs.xp_orbs[i].xpValue > 0)
            ++count;
    return count;
}

static void test_shift_double_click(void) {
    GmConfig cfg;
    GmRuntime *r = (GmRuntime *)calloc(1, sizeof *r);
    GmPlayerView player;
    char err[256];
    int bx, by, bz, chest_total = 0;
    ICStack saved = ic_mk(1, 10, 0);
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    CHECK(r != NULL, "allocate shift-double-click runtime");
    if (!r) return;
    CHECK(gm_runtime_init(r, &cfg, err, sizeof err),
          "shift-double-click runtime initializes");
    if (fail) { free(r); return; }
    gm_runtime_view(r, &player);
    for (int s = 0; s < GMC_INV_SLOTS; ++s)
        (void)gm_runtime_set_inventory(r, s, 0, 0, 0);
    bx = (int)player.x + 1;
    bz = (int)player.z;
    by = gm_world_surface_y(r->world, bx, bz);
    CHECK(gm_runtime_set_block(r, bx, by, bz, 54, 2)
              && gm_runtime_use_block(r, bx, by, bz)
              && r->container == 3 && r->active_chest >= 0,
          "open chest for shift-double-click batch");
    chest_live_set(&r->chests[r->active_chest].state, 0, ic_mk(1, 5, 0));
    CHECK(gm_runtime_set_inventory(r, 0, 1, 10, 0)
              && gm_runtime_set_inventory(r, 1, 1, 20, 0)
              && gm_runtime_set_inventory(r, 2, 1, 7, 1),
          "seed same-inventory shift-double-click corpus");

    /* GuiContainer sends this QUICK_MOVE on the second press, then scans on
     * release using the pre-move stack saved in shiftClickedSlot. */
    click(r, 0, 0, CC_CLICK_QUICK_MOVE);
    CHECK(gm_container_shift_double_click(r, 0, saved) == 1,
          "shift-double-click issues one additional matching QUICK_MOVE");
    {
        ICStack s0 = slot(r, 0), s1 = slot(r, 1), s2 = slot(r, 2);
        CHECK(isr_is_empty(&s0) && isr_is_empty(&s1)
                  && s2.item == 1 && s2.meta == 1 && s2.count == 7,
              "shift-double-click takes exact item/meta matches only");
    }
    for (int s = 0; s < GMC_CHEST_SLOTS; ++s)
        chest_total += chest_live_get(
            &r->chests[r->active_chest].state, s).count;
    CHECK(chest_total == 35,
          "shift-double-click never rescans stacks in the target inventory");
    gm_runtime_destroy(r);
    free(r);
}

static void test_horse_container(void) {
    GmConfig cfg;
    static GmRuntime r;
    GmPlayerView player;
    GmHorseState horse;
    char err[256];
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err),
          "horse container runtime initializes");
    if (fail) return;
    gm_runtime_view(&r, &player);

    CHECK(gm_runtime_spawn_horse_fixture(
              &r, GM_MOB_HORSE, 8800,
              player.x, player.y, player.z + 1.0, 0.0, 0.0, 0.0,
              0.0F, 20.0F, 1, 20.0, 0.225, 0.7,
              0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
              && !gm_runtime_open_horse_inventory(&r, 8800),
          "untamed horse inventory is not usable");
    CHECK(gm_runtime_spawn_horse_fixture(
              &r, GM_MOB_HORSE, 8801,
              player.x, player.y, player.z + 1.0, 0.0, 0.0, 0.0,
              0.0F, 20.0F, 1, 20.0, 0.225, 0.7,
              -10, GM_HORSE_TAME, 0, 0, 0, 0, 0, 0, 0, 0, 0)
              && !gm_runtime_open_horse_inventory(&r, 8801),
          "child horse inventory is not usable");
    CHECK(gm_runtime_spawn_horse_fixture(
              &r, GM_MOB_HORSE, 8802,
              player.x, player.y, player.z + 1.0, 0.0, 0.0, 0.0,
              0.0F, 20.0F, 1, 20.0, 0.225, 0.7,
              0, GM_HORSE_TAME, 0, 6 | (4 << 8), 0, 0, 0, 0,
              0, 0, 0)
              && gm_runtime_open_horse_inventory(&r, 8802)
              && r.container == 8 && r.active_horse_eid == 8802,
          "tame adult horse opens its live inventory");
    CHECK(gm_runtime_set_inventory(&r, 0, 329, 2, 0)
              && gm_runtime_set_inventory(&r, 1, 419, 2, 0)
              && gm_runtime_set_inventory(&r, 2, 1, 4, 0),
          "seed saddle, armor, and invalid horse-slot items");
    click(&r, 0, 0, CC_CLICK_QUICK_MOVE);
    click(&r, 1, 0, CC_CLICK_QUICK_MOVE);
    CHECK(gm_mobs_get_horse_state(&r.mobs, 8802, &horse)
              && horse.inventory[0].item == 329
              && horse.inventory[0].count == 1
              && horse.inventory[1].item == 419
              && horse.inventory[1].count == 1
              && slot(&r, 0).count == 1 && slot(&r, 1).count == 1,
          "horse QUICK_MOVE fills one saddle then one armor item");
    click(&r, 2, 0, CC_CLICK_PICKUP);
    click(&r, GMC_HORSE0, 0, CC_CLICK_PICKUP);
    CHECK(gm_mobs_get_horse_state(&r.mobs, 8802, &horse)
              && horse.inventory[0].item == 329
              && gm_player_cursor().item == 1
              && gm_player_cursor().count == 4,
          "horse saddle slot rejects a non-saddle cursor without swapping");
    click(&r, 2, 0, CC_CLICK_PICKUP);
    click(&r, GMC_HORSE0 + 1, 0, CC_CLICK_QUICK_MOVE);
    CHECK(gm_mobs_get_horse_state(&r.mobs, 8802, &horse)
              && cc_is_empty(&horse.inventory[1]),
          "horse slot QUICK_MOVE returns armor to reverse player order");

    CHECK(gm_runtime_spawn_horse_fixture(
              &r, GM_MOB_DONKEY, 8803,
              player.x, player.y, player.z + 1.0, 0.0, 0.0, 0.0,
              0.0F, 20.0F, 1, 20.0, 0.175, 0.5,
              0, GM_HORSE_TAME, 0, 0, 0, 1, 0, 0, 0, 0, 0)
              && gm_runtime_set_horse_inventory(
                  &r, 8803, 2, (ICStack){.item=1, .count=10})
              && gm_runtime_open_horse_inventory(&r, 8803),
          "tame chested donkey opens all seventeen slots");
    for (int s = 0; s < GMC_INV_SLOTS; ++s)
        (void)gm_runtime_set_inventory(&r, s, 0, 0, 0);
    gm_player_cursor_set(ic_empty());
    CHECK(gm_runtime_set_inventory(&r, 0, 1, 60, 0)
              && gm_runtime_set_inventory(&r, 1, 419, 2, 0)
              && gm_runtime_set_inventory(&r, 2, 329, 2, 0),
          "seed donkey transfer-order corpus");
    click(&r, 0, 0, CC_CLICK_QUICK_MOVE);
    click(&r, 1, 0, CC_CLICK_QUICK_MOVE);
    click(&r, 2, 0, CC_CLICK_QUICK_MOVE);
    CHECK(gm_mobs_get_horse_state(&r.mobs, 8803, &horse)
              && horse.inventory[0].item == 329
              && horse.inventory[0].count == 1
              && cc_is_empty(&horse.inventory[1])
              && horse.inventory[2].item == 1
              && horse.inventory[2].count == 64
              && horse.inventory[3].item == 1
              && horse.inventory[3].count == 6
              && horse.inventory[4].item == 419
              && horse.inventory[4].count == 2,
          "donkey QUICK_MOVE merges storage before empties and never equips armor");
    click(&r, GMC_HORSE0 + 2, 0, CC_CLICK_QUICK_MOVE);
    CHECK(gm_mobs_get_horse_state(&r.mobs, 8803, &horse)
              && cc_is_empty(&horse.inventory[2])
              && slot(&r, 8).item == 1 && slot(&r, 8).count == 64,
          "donkey storage QUICK_MOVE uses Java reverse player order");
    gm_runtime_set_pose(
        &r, player.x + 20.0, player.y, player.z, 0.0F, 0.0F);
    CHECK(r.container == 0 && r.active_horse_eid == -1
              && gm_mobs_get_horse_state(&r.mobs, 8803, &horse)
              && horse.inventory[3].item == 1
              && horse.inventory[4].item == 419,
          "walking away closes horse UI without returning persistent horse slots");
    gm_runtime_destroy(&r);
}

int main(void) {
    GmConfig cfg;
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    static GmRuntime r;
    int furnace_x = 0, furnace_y = 0, furnace_z = 0;
    char err[256];
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err), "runtime initializes");
    if (fail) return 1;

    /* ---- PICKUP across hotbar and main inventory ---- */
    CHECK(gm_runtime_set_inventory(&r, 0, 1, 10, 0), "seed stone in hotbar 0");
    click(&r, 0, 0, CC_CLICK_PICKUP);
    { ICStack c = gm_player_cursor(), s0 = slot(&r, 0);
      CHECK(c.item == 1 && c.count == 10 && isr_is_empty(&s0),
            "PICKUP lifts the full hotbar stack onto the cursor"); }
    click(&r, 20, 0, CC_CLICK_PICKUP);
    { ICStack s20 = slot(&r, 20), c = gm_player_cursor();
      CHECK(s20.item == 1 && s20.count == 10 && isr_is_empty(&c),
            "PICKUP places the cursor into a MAIN inventory slot (20)"); }
    click(&r, 20, 1, CC_CLICK_PICKUP);
    { ICStack c = gm_player_cursor(), s20 = slot(&r, 20);
      CHECK(c.count == 5 && s20.count == 5, "right PICKUP takes the rounded-up half"); }
    click(&r, 21, 1, CC_CLICK_PICKUP);
    { ICStack s21 = slot(&r, 21), c = gm_player_cursor();
      CHECK(s21.count == 1 && c.count == 4, "right PICKUP places exactly one"); }
    click(&r, 20, 0, CC_CLICK_PICKUP);
    { ICStack s20 = slot(&r, 20), c = gm_player_cursor();
      CHECK(s20.count == 9 && isr_is_empty(&c), "left PICKUP merges the cursor remainder"); }

    /* ---- QUICK_MOVE vanilla ordering: hotbar -> first main slot, and back ---- */
    CHECK(gm_runtime_set_inventory(&r, 3, 3, 7, 0), "seed dirt in hotbar 3");
    click(&r, 3, 0, CC_CLICK_QUICK_MOVE);
    { ICStack s9 = slot(&r, 9), s3 = slot(&r, 3);
      CHECK(s9.item == 3 && s9.count == 7 && isr_is_empty(&s3),
            "QUICK_MOVE from the hotbar fills the FIRST MAIN slot (9), not another hotbar slot"); }
    click(&r, 9, 0, CC_CLICK_QUICK_MOVE);
    { ICStack s0 = slot(&r, 0), s9 = slot(&r, 9);
      CHECK(s0.item == 3 && s0.count == 7 && isr_is_empty(&s9),
            "QUICK_MOVE from main fills the first hotbar slot"); }

    /* ---- THROW spawns a REAL item entity ---- */
    { GmAction a; memset(&a, 0, sizeof a); a.hotbar_sel = -1;
      int before = live_item_count(&r, 3);
      click(&r, 0, 0, CC_CLICK_THROW);
      ICStack s0 = slot(&r, 0);
      CHECK(s0.count == 6, "THROW drops one from the slot");
      CHECK(live_item_count(&r, 3) == before + 1, "THROW spawned a live dirt item entity");
      click(&r, 0, 1, CC_CLICK_THROW);
      s0 = slot(&r, 0);
      CHECK(isr_is_empty(&s0), "ctrl-THROW drops the whole stack");
      (void)a; }

    /* ---- player 2x2 grid: gating + a real craft ---- */
    CHECK(!gm_container_click(&r, GMC_GRID0 + 2, 0, CC_CLICK_PICKUP),
          "3x3-only grid cell is rejected on the player screen");
    CHECK(gm_runtime_set_inventory(&r, 0, 17, 2, 0), "seed two logs");
    click(&r, 0, 0, CC_CLICK_PICKUP);                 /* cursor: 2 logs */
    click(&r, GMC_GRID0, 1, CC_CLICK_PICKUP);         /* place ONE log in cell 0 */
    { ICStack res = gm_container_result(&r);
      CHECK(res.item == 5 && res.count == 4, "one log in the 2x2 grid previews 4 planks"); }
    click(&r, GMC_OUTSIDE, 0, CC_CLICK_PICKUP);       /* drop the spare log */
    click(&r, GMC_RESULT, 0, CC_CLICK_PICKUP);
    { ICStack c = gm_player_cursor();
      CHECK(c.item == 5 && c.count == 4, "taking the result yields 4 planks on the cursor");
      CHECK(cc_is_empty(&r.craft_grid[0]) || r.craft_grid[0].count == 0,
            "taking the result consumed the grid log"); }
    click(&r, 1, 0, CC_CLICK_PICKUP);                 /* park planks in hotbar 1 */

    /* ---- QUICK_MOVE on the result crafts ALL and lands hotbar-last-first ---- */
    CHECK(gm_runtime_set_inventory(&r, 0, 17, 3, 0), "seed three logs");
    click(&r, 0, 0, CC_CLICK_PICKUP);
    click(&r, GMC_GRID0, 0, CC_CLICK_PICKUP);         /* all 3 logs into cell 0 */
    click(&r, GMC_RESULT, 0, CC_CLICK_QUICK_MOVE);
    { ICStack s1 = slot(&r, 1);
      CHECK(s1.item == 5 && s1.count == 16,
            "shift-crafting 3 logs stacks 12 planks onto the parked 4 (vanilla merge-match first)");
      CHECK(cc_is_empty(&r.craft_grid[0]), "shift-craft drained the grid"); }

    /* ---- crafting table opens the full 3x3 ---- */
    { GmPlayerView v; gm_runtime_view(&r, &v);
      int bx = (int)v.x + 1, by = (int)v.y, bz = (int)v.z;
      CHECK(gm_runtime_set_block(&r, bx, by + 1, bz, 58, 0), "place a crafting table");
      CHECK(gm_runtime_use_block(&r, bx, by + 1, bz), "open the crafting table");
      CHECK(r.container == 1, "container is the table");
      CHECK(gm_container_click(&r, GMC_GRID0 + 2, 0, CC_CLICK_PICKUP),
            "3x3 grid cell is usable at a table"); }

    /* SlotCrafting must replace consumed milk buckets in their original
     * cells, rather than deleting them or routing them through inventory. */
    gm_player_cursor_set(ic_empty());
    for (int i = 0; i < 9; ++i) r.craft_grid[i] = ic_empty();
    r.craft_grid[0] = ic_mk(335, 1, 0);
    r.craft_grid[1] = ic_mk(335, 1, 0);
    r.craft_grid[2] = ic_mk(335, 1, 0);
    r.craft_grid[3] = ic_mk(353, 1, 0);
    r.craft_grid[4] = ic_mk(344, 1, 0);
    r.craft_grid[5] = ic_mk(353, 1, 0);
    r.craft_grid[6] = ic_mk(296, 1, 0);
    r.craft_grid[7] = ic_mk(296, 1, 0);
    r.craft_grid[8] = ic_mk(296, 1, 0);
    CHECK(gm_container_result(&r).item == 354, "cake recipe previews");
    click(&r, GMC_RESULT, 0, CC_CLICK_PICKUP);
    CHECK(gm_player_cursor().item == 354
              && r.craft_grid[0].item == 325
              && r.craft_grid[1].item == 325
              && r.craft_grid[2].item == 325,
          "cake take returns all three buckets to their crafting cells");
    for (int i = 3; i < 9; ++i)
        CHECK(cc_is_empty(&r.craft_grid[i]),
              "cake take consumes every non-container ingredient");
    gm_player_cursor_set(ic_empty());
    for (int i = 0; i < 9; ++i) r.craft_grid[i] = ic_empty();

    /* ---- close-toss: temporary crafting contents drop on container close ---- */
    click(&r, 1, 0, CC_CLICK_PICKUP);                 /* cursor: crafted planks */
    click(&r, GMC_GRID0 + 2, 0, CC_CLICK_PICKUP);     /* leave them in the 3x3 cell */
    { GmPlayerView v; gm_runtime_view(&r, &v);
      gm_runtime_set_pose(&r, v.x + 20.0, v.y, v.z, 0.0f, 0.0f); /* force-close */
      CHECK(r.container == 0, "walking away closes the container");
      int planks = 0;
      for (int s = 0; s < GMC_INV_SLOTS; ++s) { ICStack t = slot(&r, s); if (t.item == 5) planks += t.count; }
      CHECK(planks == 0 && live_item_count(&r, 5) == 16,
            "grid planks are tossed, not silently returned, on close");
      for (int i = 0; i < 9; ++i) CHECK(cc_is_empty(&r.craft_grid[i]), "grid empty after close"); }

    /* ---- furnace: shift-routing, click insert/extract, real smelt ---- */
    { GmPlayerView v; gm_runtime_view(&r, &v);
      int bx = (int)v.x + 1, by = (int)v.y, bz = (int)v.z;
      furnace_x = bx; furnace_y = by + 1; furnace_z = bz;
      CHECK(gm_runtime_set_block(&r, bx, by + 1, bz, 61, 0), "place a furnace");
      CHECK(gm_runtime_use_block(&r, bx, by + 1, bz), "open the furnace");
      CHECK(r.container == 2 && r.active_furnace >= 0, "furnace container active"); }
    CHECK(gm_runtime_set_inventory(&r, 0, 15, 2, 0), "seed two iron ore");
    CHECK(gm_runtime_set_inventory(&r, 1, 263, 4, 0), "seed four coal");
    click(&r, 0, 0, CC_CLICK_QUICK_MOVE);
    click(&r, 1, 0, CC_CLICK_QUICK_MOVE);
    { const FurnaceLive *f = &r.furnaces[r.active_furnace].state;
      CHECK(f->input.item == 15 && f->input.count == 2,
            "QUICK_MOVE routes smeltable ore into the furnace INPUT");
      /* the same runtime tick already ignited the burn, consuming one coal */
      CHECK(f->fuel.item == 263 && f->fuel.count == 3,
            "QUICK_MOVE routes coal into the furnace FUEL (one consumed igniting)"); }
    CHECK(!gm_container_click(&r, GMC_GRID0, 0, CC_CLICK_PICKUP),
          "craft grid is not reachable while a furnace is open");
    for (int t = 0; t < 450 && r.container == 2; ++t) {
        GmAction a; memset(&a, 0, sizeof a); a.hotbar_sel = -1;
        gm_runtime_tick(&r, a);
    }
    CHECK(r.container == 2, "player stayed in furnace range while smelting");
    { const FurnaceLive *f = &r.furnaces[r.active_furnace].state;
      CHECK(f->output.item == 265 && f->output.count == 2, "both ore smelted to iron ingots"); }
    {
      int xp_before = live_xp_total(&r);
      int orbs_before = live_xp_count(&r);
      int next_eid = r.next_entity_id;
      r.math_random_seed48 = UINT64_C(0x23456789abcd);
      CHECK(gm_container_click(
                &r, GMC_FURNACE0 + 2, 0, CC_CLICK_PICKUP),
            "direct output PICKUP is accepted");
      CHECK(live_xp_total(&r) == xp_before + 1
                && live_xp_count(&r) == orbs_before + 1
                && r.next_entity_id == next_eid + 1
                && r.math_random_seed48 == UINT64_C(0x932033aed0f7),
            "output PICKUP applies Java fractional XP rounding and constructs one orb");
    }
    { ICStack c = gm_player_cursor();
      CHECK(c.item == 265 && c.count == 2, "PICKUP takes the smelted ingots onto the cursor"); }
    {
      int xp_before = live_xp_total(&r);
      int orbs_before = live_xp_count(&r);
      r.math_random_seed48 = UINT64_C(0x123456789abc);
      CHECK(gm_runtime_furnace_output_taken(
                &r, (ICStack){.item=265, .count=2}, 2) == 2
                && live_xp_total(&r) == xp_before + 2
                && live_xp_count(&r) == orbs_before + 2
                && r.math_random_seed48 == UINT64_C(0x7fbf5cc324e6),
            "fractional output XP takes the upper branch and preserves orb split order");
      xp_before = live_xp_total(&r);
      r.math_random_seed48 = UINT64_C(0xabcdef012345);
      CHECK(gm_runtime_furnace_output_taken(
                &r, (ICStack){.item=4, .count=8}, 8) == 0
                && live_xp_total(&r) == xp_before
                && r.math_random_seed48 == UINT64_C(0xabcdef012345),
            "an output with no recipe XP consumes no Math.random values");
    }
    click(&r, 5, 0, CC_CLICK_PICKUP);
    { ICStack s5 = slot(&r, 5);
      CHECK(s5.item == 265 && s5.count == 2, "ingots parked in hotbar 5"); }
    /* fuel slot validity: iron ingots are NOT fuel */
    click(&r, 5, 0, CC_CLICK_PICKUP);                 /* cursor: 2 ingots */
    click(&r, GMC_FURNACE0 + 1, 0, CC_CLICK_PICKUP);
    { ICStack c = gm_player_cursor();
      const FurnaceLive *f = &r.furnaces[r.active_furnace].state;
      CHECK(c.item == 265 && c.count == 2 && f->fuel.item == 263,
            "non-fuel is rejected by the fuel slot (cursor unchanged)"); }
    click(&r, 5, 0, CC_CLICK_PICKUP);                 /* park them back */

    /* BlockFurnace.breakBlock delegates every complete ItemStack to
     * InventoryHelper. The compact kernel triplet alone would lose names,
     * repair cost, and enchantments here. */
    {
      int name = gm_runtime_item_name_intern(&r, "Furnace Blade");
      ICStack rich = ic_mk(276, 1, 42);
      CHECK(gm_runtime_furnace_set_custom_name(
                &r, 0, furnace_x, furnace_y, furnace_z,
                "Smelter Prime")
                && !strcmp(gm_runtime_item_name(
                    &r, r.furnaces[r.active_furnace].custom_name),
                    "Smelter Prime"),
            "furnace custom name is retained by the live tile");
      rich.repair_cost = 7;
      rich.custom_name = name;
      rich.n_enchants = 1;
      rich.enchants[0].id = 16;
      rich.enchants[0].level = 3;
      furnace_live_set_ic(
          &r.furnaces[r.active_furnace].state,
          FURNACE_LIVE_SLOT_INPUT, rich);
      CHECK(name > 0
                && gm_runtime_set_block(
                    &r, furnace_x, furnace_y, furnace_z, 0, 0),
            "breaking a furnace retires its tile and drops its slots");
      const GmLiveEnt *drop = NULL;
      for (int i = 0; i < GM_LIVE_MAX; ++i)
          if (r.entities.ents[i].active
                  && r.entities.ents[i].item == 276) {
              drop = &r.entities.ents[i];
              break;
          }
      CHECK(drop && drop->count == 1 && drop->meta == 42
                && drop->repair_cost == 7 && drop->custom_name == name
                && drop->n_enchants == 1
                && drop->ench_id[0] == 16 && drop->ench_lvl[0] == 3
                && r.container == 0 && gm_runtime_furnace_count(&r) == 0,
            "furnace break preserves the complete rich stack payload");
    }

    /* ---- armor slots: validity, stack limit 1, QUICK_MOVE equip ---- */
    gm_container_close(&r);
    r.container = 0;
    r.active_furnace = -1;
    CHECK(gm_runtime_set_inventory(&r, 0, 306, 1, 0), "seed iron helmet");
    click(&r, 0, 0, CC_CLICK_PICKUP);
    click(&r, GMC_ARMOR0 + 0, 0, CC_CLICK_PICKUP); /* feet rejects helmet */
    { ICStack c = gm_player_cursor();
      ICStack feet = isr_get_stack(&r.player.inv, ISR_ARMOR_FEET);
      CHECK(c.item == 306 && isr_is_empty(&feet),
            "armor slot rejects wrong-slot armor (helmet into feet)"); }
    click(&r, GMC_ARMOR0 + 3, 0, CC_CLICK_PICKUP); /* head accepts helmet */
    { ICStack head = isr_get_stack(&r.player.inv, ISR_ARMOR_HEAD);
      ICStack c = gm_player_cursor();
      CHECK(head.item == 306 && head.count == 1 && isr_is_empty(&c),
            "PICKUP equips iron helmet into head (GMC_ARMOR0+3 / isr 39)"); }
    CHECK(gm_runtime_set_inventory(&r, 1, 1, 16, 0), "seed stone stack");
    click(&r, 1, 0, CC_CLICK_PICKUP);
    click(&r, GMC_ARMOR0 + 1, 0, CC_CLICK_PICKUP);
    { ICStack c = gm_player_cursor();
      ICStack legs = isr_get_stack(&r.player.inv, ISR_ARMOR_LEGS);
      CHECK(c.item == 1 && c.count == 16 && isr_is_empty(&legs),
            "armor slot rejects non-armor (stone into legs)"); }
    click(&r, 1, 0, CC_CLICK_PICKUP); /* park stone */
    CHECK(gm_runtime_set_inventory(&r, 2, 307, 1, 0), "seed iron chestplate");
    click(&r, 2, 0, CC_CLICK_QUICK_MOVE);
    { ICStack chest = isr_get_stack(&r.player.inv, ISR_ARMOR_CHEST);
      ICStack s2 = slot(&r, 2);
      CHECK(chest.item == 307 && isr_is_empty(&s2),
            "QUICK_MOVE equips iron chestplate into chest (isr 38)"); }
    /* chest occupied: vanilla skips armor equip and does main/hotbar instead */
    CHECK(gm_runtime_set_inventory(&r, 3, 443, 1, 0), "seed elytra");
    click(&r, 3, 0, CC_CLICK_QUICK_MOVE);
    { ICStack chest = isr_get_stack(&r.player.inv, ISR_ARMOR_CHEST);
      ICStack s3 = slot(&r, 3);
      ICStack s9 = slot(&r, 9);
      CHECK(chest.item == 307 && isr_is_empty(&s3) && s9.item == 443,
            "QUICK_MOVE with full chest slot falls through to main inventory"); }
    click(&r, GMC_ARMOR0 + 2, 0, CC_CLICK_PICKUP); /* take chestplate to cursor */
    click(&r, 4, 0, CC_CLICK_PICKUP);               /* park chestplate */
    click(&r, 9, 0, CC_CLICK_QUICK_MOVE);           /* equip elytra from main */
    { ICStack chest = isr_get_stack(&r.player.inv, ISR_ARMOR_CHEST);
      CHECK(chest.item == 443 && r.player.elytra_equipped == 1,
            "QUICK_MOVE equips elytra into empty chest and arms flight"); }

    /* ---- single chest: open, transfer, persist across close/reopen ---- */
    { GmPlayerView v; gm_runtime_view(&r, &v);
      /* close furnace by walking away, then tick so the new column is loaded */
      gm_runtime_set_pose(&r, v.x + 20.0, v.y, v.z, 0.0f, 0.0f);
      { GmAction idle; memset(&idle, 0, sizeof idle); idle.hotbar_sel = -1;
        gm_runtime_tick(&r, idle); }
      gm_runtime_view(&r, &v);
      /* clear inv so transfers have room and no leftover cursor */
      for (int s = 0; s < GMC_INV_SLOTS; ++s)
          (void)gm_runtime_set_inventory(&r, s, 0, 0, 0);
      gm_player_cursor_set(ic_empty());
      int bx = (int)v.x + 1, by = (int)v.y, bz = (int)v.z;
      int ground = gm_world_surface_y(r.world, bx, bz);
      if (ground < 1) ground = by;
      CHECK(gm_runtime_set_block(&r, bx, ground, bz, 54, 2), "place a chest");
      CHECK(gm_world_block(r.world, bx, ground, bz) == 54, "chest block present");
      CHECK(gm_runtime_use_block(&r, bx, ground, bz), "open the chest");
      CHECK(r.container == 3 && r.active_chest >= 0, "chest container active");
      CHECK(gm_container_click(&r, GMC_CHEST0, 0, CC_CLICK_PICKUP),
            "chest slot 0 is usable");
      CHECK(!gm_container_click(&r, GMC_GRID0, 0, CC_CLICK_PICKUP),
            "craft grid is not reachable while a chest is open");
      CHECK(gm_runtime_set_inventory(&r, 0, 4, 16, 0), "seed cobble for chest");
      click(&r, 0, 0, CC_CLICK_PICKUP);
      click(&r, GMC_CHEST0 + 3, 0, CC_CLICK_PICKUP);
      { ICStack c = gm_player_cursor();
        CHECK(isr_is_empty(&c), "cobble placed into chest slot 3");
        ICStack ch = chest_live_get(&r.chests[r.active_chest].state, 3);
        CHECK(ch.item == 4 && ch.count == 16, "chest holds 16 cobble"); }
      click(&r, GMC_CHEST0 + 3, 0, CC_CLICK_QUICK_MOVE);
      { int cobble = 0;
        for (int s = 0; s < GMC_INV_SLOTS; ++s) {
            ICStack t = slot(&r, s); if (t.item == 4) cobble += t.count;
        }
        CHECK(cobble == 16, "QUICK_MOVE returns chest stack to inv");
        ICStack ch = chest_live_get(&r.chests[r.active_chest].state, 3);
        CHECK(isr_is_empty(&ch), "chest slot emptied by QUICK_MOVE"); }
      /* put one back and close by walking away */
      {
          ICStack cur = gm_player_cursor();
          if (isr_is_empty(&cur)) {
              for (int s = 0; s < GMC_INV_SLOTS; ++s) {
                  ICStack t = slot(&r, s);
                  if (t.item == 4 && t.count > 0) {
                      click(&r, s, 0, CC_CLICK_PICKUP);
                      break;
                  }
              }
          }
      }
      click(&r, GMC_CHEST0, 1, CC_CLICK_PICKUP); /* place 1 cobble */
      click(&r, 1, 0, CC_CLICK_PICKUP); /* park remainder */
      gm_runtime_set_pose(&r, v.x + 20.0, v.y, v.z, 0.0f, 0.0f);
      CHECK(r.container == 0, "walking away closes the chest");
      /* walk back into range and reopen */
      gm_runtime_set_pose(&r, bx + 0.5, (double)ground, bz + 0.5, 0.0f, 0.0f);
      CHECK(gm_runtime_use_block(&r, bx, ground, bz), "reopen the chest");
      CHECK(r.container == 3, "chest reopened");
      { ICStack ch = chest_live_get(&r.chests[r.active_chest].state, 0);
        CHECK(ch.item == 4 && ch.count == 1, "chest contents persist after close/reopen"); }

      /* InventoryLargeChest: clicking the east half still exposes west first,
       * then east through the appended 27 slot ids. Both viewer counts close
       * together and the second-half item path is live, not display-only. */
      CHECK(gm_runtime_set_block(&r, bx + 1, ground, bz, 54, 2),
            "place east half of large chest");
      CHECK(gm_runtime_chest_set_slot(
                &r, 0, bx + 1, ground, bz, 0, 297, 2, 0),
            "seed east large-chest half");
      CHECK(gm_runtime_use_block(&r, bx + 1, ground, bz),
            "open large chest through east half");
      CHECK(r.container == 10 && r.active_chest >= 0
                && r.active_chest_pair >= 0,
            "large chest uses 54-slot container");
      CHECK(r.chests[r.active_chest].wx == bx
                && r.chests[r.active_chest_pair].wx == bx + 1,
            "large chest preserves west-first InventoryLargeChest order");
      int large_first = r.active_chest;
      int large_second = r.active_chest_pair;
      click(&r, GMC_CHEST_EXTRA0, 0, CC_CLICK_PICKUP);
      { ICStack cur = gm_player_cursor();
        CHECK(cur.item == 297 && cur.count == 2,
              "large-chest second-half slot reaches east TE"); }
      click(&r, GMC_CHEST_EXTRA0 + 1, 0, CC_CLICK_PICKUP);
      { ICStack cur = gm_player_cursor();
        CHECK(isr_is_empty(&cur),
              "large-chest second-half deposit consumes cursor"); }
      CHECK(chest_live_get(
                &r.chests[r.active_chest_pair].state, 1).item == 297,
            "large-chest second-half deposit persists in east TE");
      gm_runtime_set_pose(&r, v.x + 20.0, v.y, v.z, 0.0f, 0.0f);
      CHECK(r.container == 0
                && r.chests[large_first].state.te.num_players_using == 0
                && r.chests[large_second].state.te.num_players_using == 0,
            "large chest closes both half viewer counts");
    }

    /* ---- multi-enchant book: take / deposit / shift / drop / pickup ---- */
    {
        ICStack book = ic_mk(403, 1, 0);
        ICStack book_b = ic_mk(403, 1, 0);
        book.n_enchants = 2;
        book.enchants[0].id = 16; book.enchants[0].level = 3; /* Sharpness III */
        book.enchants[1].id = 34; book.enchants[1].level = 1; /* Unbreaking I */
        book_b.n_enchants = 1;
        book_b.enchants[0].id = 16; book_b.enchants[0].level = 5; /* Sharpness V */

        /* Clear inv/cursor so round-trips are unambiguous. */
        for (int s = 0; s < GMC_INV_SLOTS; ++s)
            (void)gm_runtime_set_inventory(&r, s, 0, 0, 0);
        gm_player_cursor_set(ic_empty());

        GmPlayerView v; gm_runtime_view(&r, &v);
        int bx = (int)v.x + 1, by = (int)v.y, bz = (int)v.z;
        int ground = gm_world_surface_y(r.world, bx, bz);
        if (ground < 1) ground = by;
        /* Walk near, place chest, seed multi-enchant book in slot 0. */
        gm_runtime_set_pose(&r, bx + 0.5, (double)ground, bz + 0.5, 0.0f, 0.0f);
        CHECK(gm_runtime_set_block(&r, bx, ground, bz, 54, 2), "place chest for enchant RT");
        CHECK(gm_runtime_use_block(&r, bx, ground, bz), "open chest for enchant RT");
        CHECK((r.container == 3 || r.container == 10)
                  && r.active_chest >= 0,
              "chest open for enchant RT");
        chest_live_set(&r.chests[r.active_chest].state, 0, book);

        /* TAKE: PICKUP from chest -> cursor keeps StoredEnchantments. */
        click(&r, GMC_CHEST0, 0, CC_CLICK_PICKUP);
        {
            ICStack c = gm_player_cursor();
            CHECK(c.item == 403 && c.count == 1 && c.n_enchants == 2 &&
                  c.enchants[0].id == 16 && c.enchants[0].level == 3 &&
                  c.enchants[1].id == 34 && c.enchants[1].level == 1,
                  "PICKUP take multi-enchant book onto cursor");
            ICStack ch = chest_live_get(&r.chests[r.active_chest].state, 0);
            CHECK(isr_is_empty(&ch), "chest slot empty after take");
        }

        /* DEPOSIT: place into chest slot 2, payload intact. */
        click(&r, GMC_CHEST0 + 2, 0, CC_CLICK_PICKUP);
        {
            ICStack c = gm_player_cursor();
            ICStack ch = chest_live_get(&r.chests[r.active_chest].state, 2);
            CHECK(isr_is_empty(&c), "cursor empty after deposit");
            CHECK(ch.item == 403 && ch.n_enchants == 2 &&
                  ch.enchants[0].id == 16 && ch.enchants[1].id == 34,
                  "deposit retains multi-enchant StoredEnchantments");
        }

        /* SHIFT: quick-move chest -> inv keeps payload. */
        click(&r, GMC_CHEST0 + 2, 0, CC_CLICK_QUICK_MOVE);
        {
            ICStack ch = chest_live_get(&r.chests[r.active_chest].state, 2);
            CHECK(isr_is_empty(&ch), "shift emptied chest slot");
            int found = 0;
            for (int s = 0; s < GMC_INV_SLOTS; ++s) {
                ICStack t = slot(&r, s);
                if (t.item == 403 && t.n_enchants == 2 &&
                    t.enchants[0].id == 16 && t.enchants[0].level == 3 &&
                    t.enchants[1].id == 34 && t.enchants[1].level == 1) {
                    found = 1;
                    break;
                }
            }
            CHECK(found, "QUICK_MOVE inv stack keeps multi-enchant payload");
        }

        /* Shift inv -> chest and back once more for deposit path. */
        {
            int inv_slot = -1;
            for (int s = 0; s < GMC_INV_SLOTS; ++s) {
                ICStack t = slot(&r, s);
                if (t.item == 403) { inv_slot = s; break; }
            }
            CHECK(inv_slot >= 0, "book still in inventory before reverse shift");
            click(&r, inv_slot, 0, CC_CLICK_QUICK_MOVE);
            {
                ICStack ch = chest_live_get(&r.chests[r.active_chest].state, 0);
                CHECK(ch.item == 403 && ch.n_enchants == 2,
                      "shift-deposit into chest retains enchants");
            }
        }

        /* Stackability: 1.11.2 enchanted books max stack 1 — equal tags do NOT merge.
         * Mismatch swaps (no silent strip). */
        {
            CHECK(isr_max_stack_size(403, 0) == 1 &&
                  cc_max_stack_size(403, 0) == 1 &&
                  tec_max_stack_size(403) == 1,
                  "enchanted book max stack is 1 in isr/cc/tec");
            ICStack twin = book; /* same multi-enchant list */
            /* Slot 0 currently has multi book count 1. Put twin in slot 4. */
            chest_live_set(&r.chests[r.active_chest].state, 4, twin);
            click(&r, GMC_CHEST0 + 4, 0, CC_CLICK_PICKUP); /* twin on cursor */
            click(&r, GMC_CHEST0, 0, CC_CLICK_PICKUP);     /* try merge onto matching multi */
            {
                ICStack c = gm_player_cursor();
                ICStack ch = chest_live_get(&r.chests[r.active_chest].state, 0);
                /* Max stack 1: no merge; vanilla swaps equal unstackables. */
                CHECK(c.item == 403 && c.count == 1 && c.n_enchants == 2 &&
                      ch.item == 403 && ch.count == 1 && ch.n_enchants == 2,
                      "equal-tag enchanted books do not merge (max stack 1)");
            }
            /* Cursor still holds multi; place into empty slot for mismatch setup. */
            click(&r, GMC_CHEST0 + 4, 0, CC_CLICK_PICKUP); /* deposit multi into 4 */
            chest_live_set(&r.chests[r.active_chest].state, 5, book_b);
            click(&r, GMC_CHEST0 + 5, 0, CC_CLICK_PICKUP); /* Sharpness V on cursor */
            {
                ICStack c = gm_player_cursor();
                CHECK(c.item == 403 && c.n_enchants == 1 && c.enchants[0].level == 5,
                      "Sharpness V book on cursor before mismatch click");
            }
            /* Slot 0 has multi; click -> swap with V. */
            click(&r, GMC_CHEST0, 0, CC_CLICK_PICKUP);
            {
                ICStack c = gm_player_cursor();
                ICStack ch = chest_live_get(&r.chests[r.active_chest].state, 0);
                CHECK(c.item == 403 && c.n_enchants == 2,
                      "mismatched enchants swap (cursor gets multi)");
                CHECK(ch.item == 403 && ch.n_enchants == 1 && ch.enchants[0].level == 5,
                      "mismatched enchants swap (chest gets Sharpness V)");
            }
            /* multi book on cursor for drop test below */
        }

        /* DROP: outside PICKUP spawns EntityItem with full payload. */
        {
            int before_ents = 0;
            for (int i = 0; i < GM_LIVE_MAX; ++i)
                if (r.entities.ents[i].active && r.entities.ents[i].item == 403)
                    before_ents++;
            click(&r, GMC_OUTSIDE, 0, CC_CLICK_PICKUP);
            {
                ICStack c = gm_player_cursor();
                CHECK(isr_is_empty(&c), "cursor cleared after outside drop");
            }
            int found_drop = 0;
            for (int i = 0; i < GM_LIVE_MAX; ++i) {
                const GmLiveEnt *e = &r.entities.ents[i];
                if (!e->active || e->item != 403) continue;
                if (e->n_enchants == 2 &&
                    e->ench_id[0] == 16 && e->ench_lvl[0] == 3 &&
                    e->ench_id[1] == 34 && e->ench_lvl[1] == 1) {
                    found_drop = 1;
                    break;
                }
            }
            CHECK(found_drop, "outside drop EntityItem keeps multi-enchant payload");
            (void)before_ents;
        }

        /* PICKUP ground item: walk-on + tick clears delay then merges into inv. */
        {
            /* Force pickup delay to 0 so the next tick collects. */
            for (int i = 0; i < GM_LIVE_MAX; ++i) {
                GmLiveEnt *e = &r.entities.ents[i];
                if (e->active && e->item == 403 && e->n_enchants == 2)
                    e->pickup_delay = 0;
            }
            for (int s = 0; s < GMC_INV_SLOTS; ++s)
                (void)gm_runtime_set_inventory(&r, s, 0, 0, 0);
            /* Position player on the drop and tick. */
            {
                GmAction idle; memset(&idle, 0, sizeof idle); idle.hotbar_sel = -1;
                for (int t = 0; t < 5; ++t) gm_runtime_tick(&r, idle);
            }
            int found_inv = 0;
            for (int s = 0; s < GMC_INV_SLOTS; ++s) {
                ICStack t = slot(&r, s);
                if (t.item == 403 && t.n_enchants == 2 &&
                    t.enchants[0].id == 16 && t.enchants[0].level == 3 &&
                    t.enchants[1].id == 34 && t.enchants[1].level == 1) {
                    found_inv = 1;
                    break;
                }
            }
            CHECK(found_inv, "ground pickup restores multi-enchant book into inventory");
        }

        /* THROW from inv slot also retains payload on EntityItem. */
        {
            int inv_slot = -1;
            for (int s = 0; s < GMC_INV_SLOTS; ++s) {
                ICStack t = slot(&r, s);
                if (t.item == 403 && t.n_enchants == 2) { inv_slot = s; break; }
            }
            CHECK(inv_slot >= 0, "book present for THROW test");
            /* Clear other 403 ents so we can find the throw. */
            for (int i = 0; i < GM_LIVE_MAX; ++i)
                if (r.entities.ents[i].active && r.entities.ents[i].item == 403)
                    r.entities.ents[i].active = 0;
            click(&r, inv_slot, 0, CC_CLICK_THROW);
            int found_throw = 0;
            for (int i = 0; i < GM_LIVE_MAX; ++i) {
                const GmLiveEnt *e = &r.entities.ents[i];
                if (!e->active || e->item != 403) continue;
                if (e->n_enchants == 2 && e->ench_lvl[0] == 3 &&
                    e->ench_id[1] == 34) {
                    found_throw = 1;
                    break;
                }
            }
            CHECK(found_throw, "THROW EntityItem keeps multi-enchant payload");
        }
    }

    /* ---- Curse of Binding: SlotArmor.canTakeStack on every removal path ---- */
    {
        GmRuntime b;
        ICStack boots = ic_mk(313, 1, 7);
        int before;
        CHECK(gm_runtime_init(&b, &cfg, err, sizeof err),
              "binding runtime initializes");
        boots.n_enchants = 1;
        boots.enchants[0].id = 10;
        boots.enchants[0].level = 1;
        isr_set_stack(&b.player.inv, ISR_ARMOR_FEET, boots);
        gm_player_cursor_set(ic_empty());
        CHECK(gm_container_click(
                  &b, GMC_ARMOR0, 0, CC_CLICK_PICKUP),
              "binding pickup is a valid unchanged click");
        {
            ICStack cursor = gm_player_cursor();
            CHECK(isr_get_stack(&b.player.inv, ISR_ARMOR_FEET).item == 313
                      && isr_is_empty(&cursor),
                  "binding blocks survival pickup");
        }
        CHECK(gm_container_click(
                  &b, GMC_ARMOR0, 0, CC_CLICK_QUICK_MOVE),
              "binding quick move is a valid unchanged click");
        CHECK(isr_get_stack(&b.player.inv, ISR_ARMOR_FEET).item == 313,
              "binding blocks survival quick move");
        before = live_item_count(&b, 313);
        CHECK(gm_container_click(
                  &b, GMC_ARMOR0, 1, CC_CLICK_THROW),
              "binding throw is a valid unchanged click");
        CHECK(isr_get_stack(&b.player.inv, ISR_ARMOR_FEET).item == 313
                  && live_item_count(&b, 313) == before,
              "binding blocks survival throw");
        b.tape_creative = 1;
        CHECK(gm_container_click(
                  &b, GMC_ARMOR0, 0, CC_CLICK_PICKUP),
              "creative binding pickup is valid");
        {
            ICStack cursor = gm_player_cursor();
            CHECK(isr_is_empty(
                      &b.player.inv.armor[ISR_ARMOR_FEET - ISR_ARMOR0])
                      && cursor.item == 313
                      && ic_enchantment_level(&cursor, 10) == 1,
                  "creative can remove Binding armor with NBT intact");
        }
        gm_player_cursor_set(ic_empty());
        gm_runtime_destroy(&b);
    }

    test_shift_double_click();
    test_horse_container();

    if (fail) { fprintf(stderr, "container_live: FAIL\n"); return 1; }
    fprintf(stderr, "container_live: PASS\n");
    return 0;
}
