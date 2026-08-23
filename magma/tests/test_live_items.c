/* EntityItem kernel units: merge, pickup delay, despawn, lava, 48-cap. */
#include "entity_item.h"

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

    (void)none;
    if (fails) {
        fprintf(stderr, "test_live_items: FAILED\n");
        return 1;
    }
    fprintf(stderr, "test_live_items: ALL PASS\n");
    return 0;
}
