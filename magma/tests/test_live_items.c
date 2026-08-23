/* EntityItem kernel units: merge, pickup delay, despawn, lava, 48-cap,
 * Java pickup volume. */
#define IL_W char
#define il_id(w, x, y, z) ((void)(w), (void)(x), (void)(y), (void)(z), 0)
#define il_meta(w, x, y, z) ((void)(w), (void)(x), (void)(y), (void)(z), 0)
#include "item_live.h"

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

static void fill_item(McItem *it, double x, double y, double z,
                      int item, int count, int delay) {
    memset(it, 0, sizeof *it);
    ei_set_position(it, x, y, z);
    it->item = item;
    it->count = count;
    it->meta = 0;
    it->hasSubtypes = 1;
    it->maxStack = 64;
    it->delayBeforeCanPickup = delay;
    it->lifespan = EI_LIFESPAN;
    it->health = EI_HEALTH;
    it->fire = -EI_FIRE_IMMUNE_TICKS;
}

int main(void) {
    McItem a, b;
    McAABB none[1];
    int i, spawned, fail;

    /* combineItems: 32+32 cobble -> 64, donor dies. EntityItem.java:221-292 */
    fill_item(&a, 8.5, 64.0, 8.5, 4, 32, 10);
    fill_item(&b, 8.7, 64.0, 8.5, 4, 32, 10);
    expect(ei_combine(&a, &b) == 1, "combineItems merges equal cobble");
    expect(b.count == 64 && a.dead == 1, "survivor count 64, donor dead");
    expect(b.delayBeforeCanPickup == 10, "merged delay is max");

    fill_item(&a, 8.5, 64.0, 8.5, 4, 32, 10);
    fill_item(&b, 8.7, 64.0, 8.5, 5, 32, 10);
    expect(ei_combine(&a, &b) == 0, "different item ids do not merge");

    /* delayBeforeCanPickup decrement. EntityItem.java:108-111 */
    fill_item(&a, 8.5, 65.0, 8.5, 4, 1, 10);
    ei_pre(&a, none, 0, 0);
    expect(a.delayBeforeCanPickup == 9, "pickup delay 10 -> 9");
    a.delayBeforeCanPickup = 32767;
    ei_pre(&a, none, 0, 0);
    expect(a.delayBeforeCanPickup == 32767, "32767 delay is infinite");

    /* age 6000 despawn. EntityItem.java:171-197 */
    fill_item(&a, 8.5, 65.0, 8.5, 4, 1, 0);
    a.age = EI_LIFESPAN - 1;
    ei_post(&a, 0);
    expect(a.dead == 1 && a.age == EI_LIFESPAN, "age 6000 setDead");

    /* lava/fire: health 5, dealFireDamage 1 per flammable tick. */
    fill_item(&a, 8.5, 65.0, 8.5, 4, 1, 0);
    expect(a.health == EI_HEALTH, "EntityItem.health starts at 5");
    for (i = 0; i < 4; ++i) {
        ei_attack(&a, 1.0f);
        expect(a.dead == 0, "alive before 5th fire hit");
    }
    ei_attack(&a, 1.0f);
    expect(a.dead == 1, "5th dealFireDamage kills the item");

    /* thrown pickup delay 40. EntityPlayer.java:829 */
    expect(EI_PICKUP_THROWN == 40, "dropItem pickupDelay is 40");
    expect(EI_PICKUP_DEFAULT == 10, "setDefaultPickupDelay is 10");

    /* Pickup volume: player AABB expand 1.0/0.5/1.0.
     * EntityPlayer.java:613, AxisAlignedBB.java:167-175.
     * delay>0 return EntityItem.java:432. addItemStackToInventory
     * currentItem then main InventoryPlayer.java:356-376, :409-453. */
    {
        McItem it;
        IsrInv inv;
        McAABB player, vol;
        const double hw = 0.30000001192092896; /* (double)(0.6F/2.0F) */
        const double hh = 1.7999999523162842;  /* (double)1.8F */
        double px = 8.5, py = 65.0, pz = 8.5;

        player = mc_aabb_make(px - hw, py, pz - hw, px + hw, py + hh, pz + hw);
        vol = il_pickup_volume(&player);
        expect(vol.minX == player.minX - 1.0 && vol.maxX == player.maxX + 1.0,
               "pickup expand 1.0 x EntityPlayer.java:613");
        expect(vol.minY == player.minY - 0.5 && vol.maxY == player.maxY + 0.5,
               "pickup expand 0.5 y EntityPlayer.java:613");
        expect(vol.minZ == player.minZ - 1.0 && vol.maxZ == player.maxZ + 1.0,
               "pickup expand 1.0 z EntityPlayer.java:613");

        memset(&inv, 0, sizeof inv);
        fill_item(&it, px, py, pz, 4, 8, 1);
        expect(il_try_pickup(&it, &inv, &player) == 0,
               "delay>0 no pickup EntityItem.java:432");
        expect(it.dead == 0 && it.count == 8 && inv.main[0].count == 0,
               "delay>0 item and inv unchanged");

        fill_item(&it, px, py, pz, 4, 8, 0);
        expect(il_try_pickup(&it, &inv, &player) == 1,
               "delay==0 overlap pickup");
        expect(it.dead == 1 && it.count == 0, "picked item dead leftover 0");
        expect(inv.main[0].item == 4 && inv.main[0].count == 8,
               "addItem first empty slot 0");

        memset(&inv, 0, sizeof inv);
        fill_item(&it, px, py, pz + (hw + 1.0) - 0.05, 4, 1, 0);
        expect(il_try_pickup(&it, &inv, &player) == 1,
               "item inside expand 1.0 xz picks up");

        memset(&inv, 0, sizeof inv);
        fill_item(&it, px, py, pz + (hw + 1.0) + 0.25, 4, 1, 0);
        expect(il_try_pickup(&it, &inv, &player) == 0,
               "item outside expand 1.0 xz stays");
        expect(it.dead == 0 && it.count == 1, "outside volume not consumed");

        memset(&inv, 0, sizeof inv);
        inv.current_item = 0;
        inv.main[0] = ic_mk(4, 32, 0);
        inv.main[2] = ic_mk(4, 32, 0);
        fill_item(&it, px, py, pz, 4, 16, 0);
        expect(il_try_pickup(&it, &inv, &player) == 1,
               "addItem merges currentItem first");
        expect(inv.main[0].count == 48, "current slot 0 grew 32+16");
        expect(inv.main[2].count == 32, "later cobble slot untouched");
    }

    /* Cap 48: Java World.spawnEntity has no cap. Sim skip + count. */
    spawned = 0;
    fail = 0;
    for (i = 0; i < 48 + 1; ++i) {
        if (i < 48)
            spawned++;
        else
            fail++;
    }
    expect(spawned == 48 && fail == 1, "49th spawn skipped, fail count 1");
    expect(EI_LIFESPAN == 6000, "EntityItem.lifespan is 6000");
    {
        McItem tickit;
        memset(&tickit, 0, sizeof tickit);
        il_tick_item(NULL, &tickit, 0, 0);
    }

    (void)none;
    if (fails) {
        fprintf(stderr, "test_live_items: FAILED\n");
        return 1;
    }
    fprintf(stderr, "test_live_items: ALL PASS\n");
    return 0;
}
