/* test_armor_live: live armor slots, CombatRules absorb, durability/breakage,
 * unblockable bypass, elytra chest ownership, and tape/set_inventory round-trip. */
#include "game/runtime.h"
#include "items_tools_armor.h"
#include "inventory_stack_rules.h"
#include "player_vitals.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } } while (0)

static int init_flat(GmRuntime *r) {
    GmConfig c; char err[256];
    gm_config_defaults(&c); c.world = GM_WORLD_SUPERFLAT; c.view_distance = 1;
    if (!gm_runtime_init(r, &c, err, sizeof err)) {
        fprintf(stderr, "init: %s\n", err); return 0;
    }
    gm_runtime_set_pose(r, 8.5, 5.0, 8.5, 0.0f, 10.0f);
    return 1;
}

static void equip_iron_set(GmRuntime *r) {
    /* iron: head 306, chest 307, legs 308, feet 309 */
    CHECK(gm_runtime_set_inventory(r, ISR_ARMOR_HEAD, 306, 1, 0), "iron head");
    CHECK(gm_runtime_set_inventory(r, ISR_ARMOR_CHEST, 307, 1, 0), "iron chest");
    CHECK(gm_runtime_set_inventory(r, ISR_ARMOR_LEGS, 308, 1, 0), "iron legs");
    CHECK(gm_runtime_set_inventory(r, ISR_ARMOR_FEET, 309, 1, 0), "iron feet");
}

int main(void) {
    GmRuntime r;
    GmAction idle; memset(&idle, 0, sizeof idle); idle.hotbar_sel = -1;

    /* ---- tape / set_inventory armor round-trip preserves metadata ---- */
    if (!init_flat(&r)) return 1;
    CHECK(gm_runtime_set_inventory(&r, ISR_ARMOR_CHEST, 307, 1, 42),
          "set_inventory accepts chest armor with meta");
    {
        ICStack c = isr_get_stack(&r.player.inv, ISR_ARMOR_CHEST);
        CHECK(c.item == 307 && c.count == 1 && c.meta == 42,
              "chest armor stack retains durability meta");
    }
    CHECK(gm_runtime_tape_inventory(&r, ISR_ARMOR_FEET, 309, 1, 7),
          "tape inventory accepts feet armor");
    CHECK(gm_runtime_tape_inventory(&r, ISR_ARMOR_LEGS, 308, 1, 8),
          "tape inventory accepts legs armor");
    CHECK(gm_runtime_tape_inventory(&r, ISR_ARMOR_CHEST, 443, 1, 10),
          "tape inventory accepts elytra chest with meta");
    CHECK(gm_runtime_tape_inventory(&r, ISR_ARMOR_HEAD, 306, 1, 3),
          "tape inventory accepts head armor");
    {
        ICStack t;
        CHECK(isr_get_stack(&r.tape_inv, ISR_ARMOR_CHEST).item == 443 &&
              isr_get_stack(&r.tape_inv, ISR_ARMOR_CHEST).meta == 10,
              "tape inv chest elytra meta round-trips");
        t = isr_get_stack(&r.tape_inv, ISR_ARMOR_FEET);
        CHECK(t.item == 309 && t.meta == 7, "tape inv feet meta round-trips");
        (void)t;
    }
    gm_runtime_destroy(&r);

    /* ---- iron full set reduces zombie melee via CombatRules ---- */
    if (!init_flat(&r)) return 1;
    equip_iron_set(&r);
    {
        ITAStack slots[4];
        for (int i = 0; i < 4; ++i) {
            ICStack s = isr_get_stack(&r.player.inv, ISR_ARMOR0 + i);
            slots[i] = ita_mk(s.item, s.meta);
        }
        int pts = ita_armor_set_points(slots);
        float expect = ita_damage_after_absorb(3.0f, (float)pts, 0.0f);
        CHECK(pts == 15, "full iron set is 15 armor points");
        CHECK(gm_mobs_spawn(&r.mobs, EW_TYPE_ZOMBIE, 8.5, 5.0, 10.5) >= 0,
              "spawn armored melee zombie");
        gm_runtime_tick(&r, idle);
        float lost = 20.0f - r.vitals.health;
        CHECK(fabsf(lost - expect) < 1e-5f,
              "iron armor reduces zombie 3.0 raw to CombatRules residual");
        /* InventoryPlayer.damageArmor: max(raw/4, 1) durability per piece */
        for (int i = 0; i < 4; ++i) {
            ICStack s = isr_get_stack(&r.player.inv, ISR_ARMOR0 + i);
            CHECK(s.item != 0 && s.meta == 1,
                  "each iron piece takes 1 durability from a 3-damage hit");
        }
    }
    gm_runtime_destroy(&r);

    /* ---- unblockable ON_FIRE / FALL leave armor and full damage ---- */
    if (!init_flat(&r)) return 1;
    equip_iron_set(&r);
    r.player_fire_ticks = 20; /* pulse this tick: ON_FIRE bypasses armor */
    gm_runtime_tick(&r, idle);
    CHECK(fabsf(r.vitals.health - 19.0f) < 1e-5f,
          "ON_FIRE deals full 1.0 through iron armor");
    for (int i = 0; i < 4; ++i) {
        ICStack s = isr_get_stack(&r.player.inv, ISR_ARMOR0 + i);
        CHECK(s.meta == 0, "ON_FIRE does not damage armor durability");
    }
    gm_runtime_destroy(&r);

    if (!init_flat(&r)) return 1;
    equip_iron_set(&r);
    {
        float before = r.vitals.health;
        /* Direct FALL path (unblockable): armor must not reduce. */
        pv_fall_damage(&r.vitals, 6.0f); /* ceil(6-3)=3 */
        r.player.health = r.vitals.health;
        CHECK(fabsf(before - r.vitals.health - 3.0f) < 1e-5f,
              "FALL deals full ceil(distance-3) through armor");
        for (int i = 0; i < 4; ++i) {
            ICStack s = isr_get_stack(&r.player.inv, ISR_ARMOR0 + i);
            CHECK(s.meta == 0, "FALL does not damage armor durability");
        }
    }
    gm_runtime_destroy(&r);

    /* ---- armor breakage removes the piece ---- */
    if (!init_flat(&r)) return 1;
    /* iron boots maxDamage = 13 * 15 = 195 */
    CHECK(gm_runtime_set_inventory(&r, ISR_ARMOR_FEET, 309, 1, 195),
          "near-broken iron boots (max 195; one more point breaks)");
    CHECK(gm_mobs_spawn(&r.mobs, EW_TYPE_ZOMBIE, 8.5, 5.0, 10.5) >= 0,
          "spawn boots-break zombie");
    gm_runtime_tick(&r, idle);
    {
        ICStack feet = isr_get_stack(&r.player.inv, ISR_ARMOR_FEET);
        CHECK(isr_is_empty(&feet), "boots break and are removed when durability exceeds max");
    }
    gm_runtime_destroy(&r);

    /* ---- elytra chest ownership + set_elytra hook ---- */
    if (!init_flat(&r)) return 1;
    gm_runtime_set_elytra(&r, 1);
    CHECK(r.player.elytra_equipped == 1, "set_elytra test hook arms flight");
    CHECK(gm_runtime_set_inventory(&r, ISR_ARMOR_CHEST, 443, 1, 0),
          "equip fresh elytra in chest");
    CHECK(r.player.elytra_equipped == 1, "elytra chest sets elytra_equipped");
    CHECK(gm_runtime_set_inventory(&r, ISR_ARMOR_CHEST, 307, 1, 0),
          "replace elytra with iron chestplate");
    CHECK(r.player.elytra_equipped == 0,
          "non-elytra chest clears elytra_equipped");
    CHECK(gm_runtime_set_inventory(&r, ISR_ARMOR_CHEST, 443, 1, 431),
          "broken-threshold elytra (meta == max-1 is unusable)");
    CHECK(r.player.elytra_equipped == 0,
          "ItemElytra unusable at meta >= maxDamage-1");
    CHECK(gm_runtime_set_inventory(&r, ISR_ARMOR_CHEST, 443, 1, 430),
          "last-usable elytra durability");
    CHECK(r.player.elytra_equipped == 1,
          "elytra usable while meta < maxDamage-1");
    gm_runtime_destroy(&r);

    /* ---- equip click validity already covered in container_live; screen ids ---- */
    CHECK(GMC_ARMOR0 + 2 == 51 && ISR_ARMOR_CHEST == 38,
          "GUI chest slot maps to isr tape slot 38");

    if (fail) { fprintf(stderr, "armor_live: FAIL\n"); return 1; }
    fprintf(stderr, "armor_live: PASS\n");
    return 0;
}
