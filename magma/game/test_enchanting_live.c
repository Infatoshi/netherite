#include "game/runtime.h"

#include "container_click.h"

#include <stdio.h>

#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); return 1; } } while (0)

static void canonical_shelves(GmRuntime *r, int x, int y, int z)
{
    for (int dx = -2; dx <= 2; ++dx) {
        gm_runtime_set_block(r, x + dx, y, z - 2, 47, 0);
        gm_runtime_set_block(r, x + dx, y, z + 2, 47, 0);
    }
    for (int dz = -1; dz <= 1; ++dz)
        gm_runtime_set_block(r, x - 2, y, z + dz, 47, 0);
    gm_runtime_set_block(r, x + 2, y, z - 1, 47, 0);
    gm_runtime_set_block(r, x + 2, y, z + 1, 47, 0);
}

static int same_offer(const EtOffer *a, const EtOffer *b)
{
    for (int slot = 0; slot < 3; ++slot) {
        if (a->levels[slot] != b->levels[slot]
                || a->clue_id[slot] != b->clue_id[slot]
                || a->clue_lvl[slot] != b->clue_lvl[slot]
                || a->n_list[slot] != b->n_list[slot])
            return 0;
        for (int i = 0; i < a->n_list[slot]; ++i)
            if (a->lists[slot][i].id != b->lists[slot][i].id
                    || a->lists[slot][i].level
                        != b->lists[slot][i].level)
                return 0;
    }
    return 1;
}

int main(void)
{
    GmConfig cfg;
    GmRuntime r;
    char err[256];
    gm_config_defaults(&cfg);
    cfg.seed = 42;
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.enchanting = 1;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err), err);

    const int x = 8, y = 5, z = 8;
    CHECK(gm_runtime_set_block(&r, x, y, z, 116, 0), "table block");
    canonical_shelves(&r, x, y, z);
    CHECK(enchanting_live_power(r.world, x, y, z) == 15,
          "canonical bookshelf scan is 15");
    gm_runtime_set_pose(&r, 8.5, 5.75, 6.5, 180.0f, 0.0f);
    r.mobs.xp_total = 1395;
    CHECK(gm_runtime_use_block(&r, x, y, z), "open table");
    CHECK(r.container == 5 && r.enchanting.open
          && r.player_xp_level == 30, "live table and XP level");

    CHECK(gm_runtime_set_inventory(&r, 9, 276, 1, 0), "diamond sword");
    CHECK(gm_runtime_set_inventory(&r, 10, 351, 12, 4), "lapis");
    CHECK(gm_container_click(&r, 9, 0, CC_CLICK_QUICK_MOVE),
          "shift sword");
    CHECK(gm_container_click(&r, 10, 0, CC_CLICK_QUICK_MOVE),
          "shift lapis");
    CHECK(r.enchanting.slots[0].item == 276
          && r.enchanting.slots[0].count == 1
          && r.enchanting.slots[1].item == 351
          && r.enchanting.slots[1].count == 12,
          "table slots routed");
    {
        EtOffer expected;
        et_compute_offers(0, 15, ET_ITEM_DIAMOND_SWORD, &expected);
        CHECK(same_offer(&expected, &r.enchanting.offer),
              "live offers equal verified kernel");
    }
    {
        JavaRandom expected = r.mobs.player_random;
        int expected_seed = (i32)jrand_next(&expected, 32);
        CHECK(gm_container_click(
                  &r, GMC_ENCHANT_BUTTON0 + 2, 0, CC_CLICK_PICKUP),
              "third offer click");
        CHECK(r.enchanting.slots[0].item == 276
              && r.enchanting.slots[0].n_enchants > 0,
              "sword enchanted");
        CHECK(r.enchanting.slots[1].count == 9
              && r.player_xp_level == 27,
              "three lapis and levels spent");
        CHECK(r.player_xp_seed == expected_seed
              && r.enchanting.xp_seed == expected_seed,
              "EntityPlayer random reseeds offers");
    }

    gm_container_close(&r);
    r.container = 0;
    r.enchanting.open = 0;
    CHECK(gm_runtime_use_block(&r, x, y, z), "reopen table");
    enchanting_live_set_slot(
        &r.enchanting, r.world, 0, ic_mk(340, 1, 0));
    enchanting_live_set_slot(
        &r.enchanting, r.world, 1, ic_mk(351, 3, 4));
    r.player_xp_level = 30;
    CHECK(gm_runtime_enchant_click(&r, 0), "book first offer");
    CHECK(r.enchanting.slots[0].item == 403
          && r.enchanting.slots[0].n_enchants == 1,
          "book becomes StoredEnchantments book");
    CHECK(et_item_kind_from_id(261) >= 0
          && et_item_kind_from_id(346) >= 0
          && et_item_kind_from_id(298) >= 0
          && et_item_kind_from_id(317) >= 0
          && et_item_kind_from_id(260) < 0,
          "bow, fishing, armor mappings and non-enchantable rejection");

    gm_runtime_destroy(&r);
    puts("PASS enchanting live");
    return 0;
}
