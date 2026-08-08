#include "game/runtime.h"
#include "container_click.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "merchant container check failed at line %d: %s\n", \
            __LINE__, #c); exit(1); } } while (0)

static void print_stack(ICStack value) {
    printf("%d %d %d", value.item, value.count, value.meta);
}

static int total_uses(const GmRuntime *r) {
    const GmVillagerTrade *trade = &r->village_residents[0].trade;
    int total = 0;
    for (int i = 0; i < trade->offer_count; ++i)
        total += trade->offers[i].uses;
    return total;
}

static int live_item_count(const GmRuntime *r, int item) {
    int total = 0;
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        const GmLiveEnt *entity = &r->entities.ents[i];
        if (entity->active && entity->type == 0 && entity->item == item)
            total += entity->count;
    }
    return total;
}

static void row(const char *tag, GmRuntime *r) {
    printf("%s ", tag); print_stack(r->merchant_slots[0]); putchar(' ');
    print_stack(r->merchant_slots[1]); putchar(' ');
    print_stack(r->merchant_slots[2]); putchar(' ');
    print_stack(gm_player_cursor());
    printf(" %d %d\n", r->village_residents[0].trade.offers[0].uses,
           total_uses(r));
}

int main(void) {
    const int eid = 701;
    int villager_slot;
    GmConfig cfg;
    GmRuntime r;
    char err[256] = {0};
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    cfg.weather = 0;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    CHECK(gm_runtime_spawn_villager_fixture(
        &r, eid, 1.5, 5.0, 0.5, 0.0, 0.0, 0.0,
        0.0F, 20.0F, 0, 0, 0, 0, 0,
        UINT64_C(0x123456789abc), 0, 0.0));
    CHECK(gm_runtime_restore_villager_trade(&r, eid, 1, 1, 0, 0, 2));
    CHECK(gm_runtime_restore_villager_offer(&r, eid, 0, 0, 7, 1));
    CHECK(gm_runtime_restore_villager_offer_stack(
        &r, eid, 0, 0, ic_mk(388, 3, 0)));
    CHECK(gm_runtime_restore_villager_offer_stack(
        &r, eid, 0, 1, ic_empty()));
    CHECK(gm_runtime_restore_villager_offer_stack(
        &r, eid, 0, 2, ic_mk(297, 2, 0)));
    CHECK(gm_runtime_restore_villager_offer(&r, eid, 1, 0, 7, 1));
    CHECK(gm_runtime_restore_villager_offer_stack(
        &r, eid, 1, 0, ic_mk(340, 1, 0)));
    CHECK(gm_runtime_restore_villager_offer_stack(
        &r, eid, 1, 1, ic_mk(388, 5, 0)));
    CHECK(gm_runtime_restore_villager_offer_stack(
        &r, eid, 1, 2, ic_mk(264, 1, 0)));
    villager_slot = gm_mobs_find_slot_by_eid(&r.mobs, eid);
    CHECK(villager_slot > 0);
    r.mobs.growing_age[villager_slot] = -1;
    CHECK(!gm_runtime_open_villager(&r, eid));
    r.mobs.growing_age[villager_slot] = 0;
    CHECK(gm_runtime_open_villager(&r, eid));

    r.merchant_slots[0] = ic_mk(388, 5, 0);
    gm_runtime_merchant_refresh(&r);
    row("A", &r);
    CHECK(gm_container_click(&r, GMC_MERCHANT0 + 2, 0, CC_CLICK_PICKUP));
    row("B", &r);
    gm_player_cursor_set(ic_empty());

    CHECK(gm_runtime_merchant_select(&r, 1));
    r.merchant_slots[0] = ic_mk(388, 5, 0);
    gm_runtime_merchant_refresh(&r);
    r.merchant_slots[1] = ic_mk(340, 1, 0);
    gm_runtime_merchant_refresh(&r);
    row("C", &r);
    CHECK(gm_container_click(&r, GMC_MERCHANT0 + 2, 0, CC_CLICK_PICKUP));
    row("D", &r);
    gm_player_cursor_set(ic_empty());

    CHECK(gm_runtime_merchant_select(&r, 0));
    r.merchant_slots[0] = ic_mk(388, 6, 0);
    r.merchant_slots[1] = ic_empty();
    gm_runtime_merchant_refresh(&r);
    CHECK(gm_container_click(
        &r, GMC_MERCHANT0 + 2, 0, CC_CLICK_QUICK_MOVE));
    printf("E "); print_stack(r.merchant_slots[0]); putchar(' ');
    print_stack(r.merchant_slots[1]); putchar(' ');
    print_stack(r.merchant_slots[2]); putchar(' ');
    print_stack(isr_get_stack(&r.player.inv, 8));
    printf(" %d %d\n", r.village_residents[0].trade.offers[0].uses,
           total_uses(&r));

    r.merchant_slots[0] = ic_mk(388, 1, 0);
    gm_runtime_merchant_refresh(&r);
    {
        GmAction idle;
        int emeralds = 0;
        memset(&idle, 0, sizeof idle);
        gm_runtime_set_pose(&r, 20.5, 5.0, 20.5, 0.0F, 0.0F);
        gm_runtime_tick(&r, idle);
        for (int slot = 0; slot < ISR_MAIN_SLOTS; ++slot) {
            ICStack value = isr_get_stack(&r.player.inv, slot);
            if (value.item == 388) emeralds += value.count;
        }
        CHECK(r.container == 0 && r.active_villager_eid == -1);
        CHECK(emeralds == 0);
        CHECK(live_item_count(&r, 388) == 1);
    }

    gm_runtime_destroy(&r);
    return 0;
}
