/* Compare smelting_recipes.h / inventory_stack_rules.h against the
 * Java-derived table from verify/furnace_registry.py --c-header. */
#include "smelting_recipes.h"
#include "furnace_full_tick.h"
#include "inventory_stack_rules.h"
#include "items_core.h"
#include "../../out/verify/furnace_registry_expect.h"

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
    SRRecipe R[SR_NRECIPES];
    int n, i;
    char buf[128];

    n = sr_build(R);
    expect(n == FRE_NRECIPES, "sr_build count matches Java");
    expect(n == SR_NRECIPES, "SR_NRECIPES matches sr_build");
    for (i = 0; i < n && i < FRE_NRECIPES; ++i) {
        const FreRecipe *e = &FRE_RECIPES[i];
        int ok = R[i].input.item == e->in_item && R[i].input.meta == e->in_meta &&
                 R[i].output.item == e->out_item && R[i].output.count == e->out_count &&
                 R[i].output.meta == e->out_meta && R[i].xp == e->xp;
        snprintf(buf, sizeof buf, "recipe[%d] %d:%d -> %d xp=%g", i,
                 e->in_item, e->in_meta, e->out_item, (double)e->xp);
        expect(ok, buf);
    }
    for (i = 0; i < FRE_NFUELS; ++i) {
        i32 burn = sr_getItemBurnTime(sr_mk(FRE_FUELS[i].id, 1, 0));
        snprintf(buf, sizeof buf, "fuel id=%d burn=%d", FRE_FUELS[i].id,
                 FRE_FUELS[i].burn);
        expect(burn == FRE_FUELS[i].burn, buf);
    }
    expect(isr_max_stack_size(FRE_BUCKET, 0) == FRE_BUCKET_MAX,
           "empty bucket max 16 (Item.java:1566)");
    expect(isr_max_stack_size(FRE_LAVA_BUCKET, 0) == FRE_FILLED_BUCKET_MAX,
           "lava bucket max 1 (ItemBucket.java:32)");
    expect(isr_max_stack_size(FRE_WATER_BUCKET, 0) == FRE_FILLED_BUCKET_MAX,
           "water bucket max 1");
    expect(isr_max_stack_size(FRE_MILK_BUCKET, 0) == FRE_FILLED_BUCKET_MAX,
           "milk bucket max 1 (ItemBucketMilk.java:17)");

    {
        IsrInv inv;
        isr_init(&inv);
        inv.current_item = 3;
        expect(isr_get_best_hotbar_slot(&inv) == 3,
               "getBestHotbarSlot first empty is current (InventoryPlayer.java:164-171)");
        for (i = 0; i < 9; ++i) inv.main[i] = ic_mk(IC_APPLE, 1, 0);
        inv.current_item = 4;
        expect(isr_get_best_hotbar_slot(&inv) == 4,
               "full hotbar unenchanted returns current (InventoryPlayer.java:174-181)");
    }

    {
        FftFurnace f;
        memset(&f, 0, sizeof f);
        f.nrecipes = sr_build(f.recipes);
        f.slot0 = sr_mk(SR_BEEF, 1, 0);
        f.slot1 = sr_mk(SR_LAVA_BUCKET, 1, 0);
        f.slot2 = sr_empty();
        f.total_cook = TE_COOK_TICKS;
        fft_tick(&f);
        expect(f.slot1.item == SR_BUCKET && f.slot1.count == 1,
               "lava bucket fuel leaves empty bucket (TileEntityFurnace.update:232-234)");
        expect(f.burn_time == 20000, "lava burn 20000");
    }

    {
        ICStack add, held = ic_mk(IC_BUCKET, 16, 0);
        held = ic_fill_bucket(held, IC_WATER_BUCKET, &add);
        expect(held.item == IC_BUCKET && held.count == 15,
               "fillBucket shrinks empty stack");
        expect(add.item == IC_WATER_BUCKET && add.count == 1,
               "fillBucket leftover filled bucket for inventory add");
        held = ic_mk(IC_BUCKET, 1, 0);
        held = ic_fill_bucket(held, IC_LAVA_BUCKET, &add);
        expect(held.item == IC_LAVA_BUCKET && held.count == 1 &&
                   isr_is_empty(&add),
               "fillBucket count=1 returns filled in hand");
    }

    {
        IcFood c = ic_food_info(365, 0);
        expect(c.hunger == 2 && c.saturation == 0.3f && c.potion_prob == 0.3f,
               "chicken hunger/sat/potion 0.3 (Item.java:1607)");
        expect(ic_food_info(260, 0).hunger == 4, "apple hunger 4");
        expect(ic_food_info(322, 0).potion_prob < 0.0f,
               "golden apple has no ItemFood potion draw");
    }

    if (fails) {
        fprintf(stderr, "test_furnace_registry: FAIL\n");
        return 1;
    }
    printf("test_furnace_registry: PASS (%d recipes)\n", n);
    return 0;
}
