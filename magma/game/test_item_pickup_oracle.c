#include "game/runtime.h"

#include <stdio.h>
#include <string.h>

static void fill_main(IsrInv *inv, int item, int count, int meta) {
    for (int i = 0; i < ISR_MAIN_SLOTS; ++i)
        isr_set_stack(inv, i, ic_mk(item, count, meta));
}

int main(int argc, char **argv) {
    const char *kind;
    GmLiveSim sim;
    PsvPlayer player;
    GmLiveEnt *dropped;
    int item = 1, count = 64, meta = 0, transferred;
    if (argc != 2) return 2;
    kind = argv[1];
    memset(&sim, 0, sizeof sim);
    psv_player_init(&player);
    isr_init(&player.inv);
    if (!strcmp(kind, "partial")) {
        isr_set_stack(&player.inv, 0, ic_mk(1, 60, 0));
        count = 10;
    } else if (!strcmp(kind, "partial_full")) {
        fill_main(&player.inv, 4, 64, 0);
        isr_set_stack(&player.inv, 0, ic_mk(1, 60, 0));
        count = 10;
    } else if (!strcmp(kind, "full")) {
        fill_main(&player.inv, 4, 64, 0);
        count = 10;
    } else if (!strcmp(kind, "bucket")) {
        item = 325; count = 2;
        isr_set_stack(&player.inv, 0, ic_mk(325, 15, 0));
    } else if (!strcmp(kind, "damaged")
            || !strcmp(kind, "damaged_full")) {
        item = 276; count = 1; meta = 5;
        if (!strcmp(kind, "damaged_full"))
            fill_main(&player.inv, 4, 64, 0);
    } else if (!strcmp(kind, "subtype")) {
        item = 35; count = 5; meta = 2;
        isr_set_stack(&player.inv, 0, ic_mk(35, 10, 1));
    } else if (!strcmp(kind, "nonsubtype")) {
        count = 5; meta = 2;
        isr_set_stack(&player.inv, 0, ic_mk(1, 10, 1));
    } else if (!strcmp(kind, "offhand")) {
        fill_main(&player.inv, 4, 64, 0);
        isr_set_stack(&player.inv, 1, ic_mk(1, 10, 0));
        isr_set_stack(&player.inv, ISR_OFFHAND_SLOT, ic_mk(1, 60, 0));
        count = 10;
    } else if (!strcmp(kind, "selected")) {
        fill_main(&player.inv, 4, 64, 0);
        isr_set_stack(&player.inv, 5, ic_mk(1, 60, 0));
        isr_set_stack(&player.inv, ISR_OFFHAND_SLOT, ic_mk(1, 60, 0));
        player.inv.current_item = 5;
        count = 10;
    } else if (!strcmp(kind, "shulker")) {
        item = 219; count = 1;
        isr_set_stack(&player.inv, 0, ic_mk(219, 1, 0));
    } else if (!strcmp(kind, "arbitrary_tag")) {
        count = 10;
    } else if (strcmp(kind, "empty")) {
        return 2;
    }
    if (!gm_live_spawn_item_state_exact(
            &sim, 5000, 0.0, 240.0, 0.0, 0.0, 0.0, 0.0,
            0.0F, 0.0F, item, count, meta, 0, 0,
            5, 6000, 0, 1, 0))
        return 1;
    dropped = &sim.ents[0];
    if (!strcmp(kind, "arbitrary_tag")) dropped->tag_id = 1;
    transferred = gm_live_item_collide_player_exact(
        &sim, 5000, (struct PsvPlayer *)&player);
    printf("{\"ok\":true,\"kind\":\"%s\","
           "\"item_alive\":%s,\"item_count\":%d,"
           "\"transferred\":%d,\"selected\":%d,"
           "\"tag_preserved\":%s,\"main\":[",
           kind, dropped->active ? "true" : "false",
           dropped->active ? dropped->count : 0,
           transferred, player.inv.current_item,
           strcmp(kind, "arbitrary_tag")
               || isr_get_stack(&player.inv, 0).tag_id == 1
               ? "true" : "false");
    for (int i = 0; i < ISR_MAIN_SLOTS; ++i) {
        ICStack stack = isr_get_stack(&player.inv, i);
        printf("%s[%d,%d,%d]", i ? "," : "",
               stack.item, stack.count, stack.meta);
    }
    {
        ICStack offhand = isr_get_stack(&player.inv, ISR_OFFHAND_SLOT);
        printf("],\"offhand\":[%d,%d,%d]}\n",
               offhand.item, offhand.count, offhand.meta);
    }
    return 0;
}
