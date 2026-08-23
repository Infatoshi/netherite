/* Magma-side potion units. Shared implementation is blaze/core/potion_effects.h. */
#include "player_survival.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (cond)
        fprintf(stderr, "OK: %s\n", msg);
    else {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails = 1;
    }
}

int main(void) {
    PsvPlayer pl;
    PvStats vit;
    int t, heals;

    psv_player_init(&pl);
    pv_init(&vit);
    vit.health = 10.0f;
    pl.health = 10.0f;
    expect(psv_potion_is_ready(PSV_POT_REGENERATION, 50, 0), "regen I ready 50");
    expect(psv_potion_is_ready(PSV_POT_REGENERATION, 25, 1), "regen II ready 25");
    expect(psv_potion_is_ready(PSV_POT_POISON, 25, 0), "poison I ready 25");
    expect(psv_potion_is_ready(PSV_POT_WITHER, 40, 0), "wither I ready 40");
    psv_potion_add(&pl, PSV_POT_REGENERATION, 50, 0, 0, 1);
    psv_update_potion_effects(&pl, &vit);
    expect(vit.health == 11.0f, "regen I heal 1.0");

    heals = 0;
    psv_potion_clear(&pl);
    pv_init(&vit);
    vit.health = 1.0f;
    pl.health = 1.0f;
    psv_potion_add(&pl, PSV_POT_REGENERATION, 900, 0, 0, 1);
    for (t = 0; t < 900; ++t) {
        float before = vit.health;
        psv_update_potion_effects(&pl, &vit);
        if (vit.health > before) ++heals;
    }
    expect(heals == 18, "regen I 18 heal ticks");

    psv_potion_clear(&pl);
    pv_init(&vit);
    vit.health = 1.0f;
    pl.health = 1.0f;
    psv_potion_add(&pl, PSV_POT_POISON, 25, 0, 0, 1);
    psv_update_potion_effects(&pl, &vit);
    expect(vit.health == 1.0f, "poison at 1.0 does not kill");

    psv_potion_clear(&pl);
    pv_init(&vit);
    psv_potion_add(&pl, PSV_POT_HUNGER, 10, 0, 0, 1);
    psv_update_potion_effects(&pl, &vit);
    expect(vit.exhaustion == 0.005f, "hunger exhaustion 0.005");

    psv_potion_clear(&pl);
    psv_potion_add(&pl, PSV_POT_SPEED, 40, 0, 0, 1);
    isr_set_stack(&pl.inv, 0, ic_mk(PSV_ITEM_MILK, 1, 0));
    psv_potion_milk_finish(&pl, 0, 0);
    expect(pl.n_potions == 0, "milk clears");
    expect(isr_get_stack(&pl.inv, 0).item == PSV_ITEM_BUCKET, "milk -> bucket");

    psv_player_init(&pl);
    isr_set_stack(&pl.inv, 0, ic_mk(PSV_ITEM_SHIELD, 1, 0));
    pl.yaw = 0.0f;
    pl.use_action = PSV_USE_BLOCK;
    pl.use_max = PSV_SHIELD_USE_TICKS;
    pl.use_remaining = PSV_SHIELD_USE_TICKS - PSV_SHIELD_RAISE_TICKS;
    expect(psv_can_block_damage(&pl, PSV_HURT_PROJECTILE, pl.ent.posX,
                               pl.ent.posZ + 2.0),
           "shield blocks arrow");
    expect(psv_can_block_damage(&pl, 0, pl.ent.posX, pl.ent.posZ + 2.0),
           "shield blocks zombie");
    psv_damage_shield(&pl, 4.0f);
    expect(isr_get_stack(&pl.inv, 0).meta == 5, "shield durability 1+floor(4)");
    expect(PSV_POTION_MAX >= 27, "cap covers vanilla 27 ids");
    return fails ? 1 : 0;
}
