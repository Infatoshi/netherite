#include "game/runtime.h"
#include "game/screen.h"
#include "container_click.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "static container check failed at line %d: %s\n", \
            __LINE__, #c); exit(1); } } while (0)

static void print_stack(ICStack value) {
    printf("%d %d %d", value.item, value.count, value.meta);
}

static const GmScreenSlot *find_slot(
        const GmScreenSlot *slots, int count, int id) {
    for (int i = 0; i < count; ++i)
        if (slots[i].slot_id == id) return &slots[i];
    return NULL;
}

static void print_layout(const char *tag, int kind, int tile_slots) {
    GmScreenSlot slots[GMC_SLOT_COUNT];
    const GmScreenSlot *slot;
    int count = gm_screen_layout(kind, 176, kind == 14 ? 133 : 166,
                                 slots, GMC_SLOT_COUNT);
    printf("L %s %d", tag, count);
    for (int i = 0; i < tile_slots; ++i) {
        slot = find_slot(slots, count, GMC_CHEST0 + i);
        CHECK(slot);
        printf(" %d %d", slot->x, slot->y);
    }
    for (int i = 9; i < 36; ++i) {
        slot = find_slot(slots, count, i);
        CHECK(slot);
        printf(" %d %d", slot->x, slot->y);
    }
    for (int i = 0; i < 9; ++i) {
        slot = find_slot(slots, count, i);
        CHECK(slot);
        printf(" %d %d", slot->x, slot->y);
    }
    putchar('\n');
}

static void row(const char *tag, const GmRuntime *r, int size) {
    const GmRuntimeStaticContainer *inventory =
        &r->static_containers[r->active_static_container];
    printf("%s", tag);
    for (int slot = 0; slot < size; ++slot) {
        putchar(' ');
        print_stack(inventory->slots[slot]);
    }
    putchar(' ');
    print_stack(gm_player_cursor());
    putchar(' ');
    print_stack(isr_get_stack(&r->player.inv, 8));
    putchar('\n');
}

static void exercise(const char *tag, int block, int kind, int size) {
    GmConfig cfg;
    GmRuntime r;
    char err[256] = {0};
    const int x = 9, y = 78, z = 8;
    const char *checkpoint = getenv("NETHERITE_STATIC_CONTAINER_CHECKPOINT");
    char row_tag[3] = {tag[0], 'A', 0};
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    cfg.weather = 0;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    CHECK(checkpoint && checkpoint[0]);
    gm_runtime_set_pose(&r, 8.5, 78.0, 8.5, 0.0F, 0.0F);
    CHECK(gm_runtime_set_block(&r, x, y, z, block, 2));
    CHECK(gm_runtime_set_block(&r, x + 1, y - 1, z, 1, 0));
    CHECK(gm_runtime_load_block(&r, x + 1, y, z, 149, 1));
    CHECK(gm_runtime_use_block(&r, x, y, z));
    CHECK(r.container == kind && r.active_static_container >= 0);
    CHECK(gm_runtime_set_inventory(&r, 0, 1, 20, 0));
    CHECK(gm_container_click(&r, 0, 0, CC_CLICK_QUICK_MOVE));
    CHECK(gm_runtime_scheduled_tick_count(&r) == 1);
    row(row_tag, &r, size);
    row_tag[1] = 'B';
    CHECK(gm_container_click(&r, GMC_CHEST0, 1, CC_CLICK_PICKUP));
    row(row_tag, &r, size);
    row_tag[1] = 'C';
    CHECK(gm_container_click(&r, GMC_CHEST0 + 1, 0, CC_CLICK_PICKUP));
    row(row_tag, &r, size);
    row_tag[1] = 'D';
    CHECK(gm_container_click(&r, GMC_CHEST0, 0, CC_CLICK_PICKUP));
    CHECK(gm_container_click(&r, GMC_CHEST0 + 1, 0, CC_CLICK_PICKUP));
    row(row_tag, &r, size);
    row_tag[1] = 'E';
    CHECK(gm_container_click(
        &r, GMC_CHEST0 + 1, 0, CC_CLICK_QUICK_MOVE));
    row(row_tag, &r, size);
    CHECK(gm_container_click(&r, 8, 0, CC_CLICK_QUICK_MOVE));
    CHECK(gm_runtime_write_checkpoint(&r, checkpoint));
    CHECK(gm_runtime_static_container_set_slot(
        &r, 0, x, y, z, 0, 4, 1, 0));
    CHECK(gm_runtime_load_checkpoint(&r, checkpoint));
    CHECK(r.container == kind && r.active_static_container >= 0
        && r.static_containers[r.active_static_container].slots[0].item == 1
        && r.static_containers[r.active_static_container].slots[0].count == 20);
    gm_runtime_set_pose(&r, 40.5, 78.0, 8.5, 0.0F, 0.0F);
    CHECK(r.container == 0 && r.active_static_container == -1);
    (void)remove(checkpoint);
    gm_runtime_destroy(&r);
}

static void check_click_notifies_comparator(
        int block, int kind, int item, int count, int meta) {
    GmConfig cfg;
    GmRuntime r;
    char err[256] = {0};
    const int x = 9, y = 78, z = 8;
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    cfg.weather = 0;
    cfg.brewing = 1;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    gm_runtime_set_pose(&r, 8.5, 78.0, 8.5, 0.0F, 0.0F);
    CHECK(gm_runtime_set_block(&r, x, y, z, block, 2));
    CHECK(gm_runtime_set_block(&r, x + 1, y - 1, z, 1, 0));
    CHECK(gm_runtime_load_block(&r, x + 1, y, z, 149, 1));
    CHECK(gm_runtime_use_block(&r, x, y, z));
    CHECK(r.container == kind && r.active_static_container >= 0);
    CHECK(gm_runtime_set_inventory(&r, 0, item, count, meta));
    CHECK(gm_container_click(&r, 0, 0, CC_CLICK_QUICK_MOVE));
    CHECK(gm_runtime_scheduled_tick_count(&r) == 1);
    gm_runtime_destroy(&r);
}

int main(void) {
    print_layout("D", 13, 9);
    exercise("D", 23, 13, 9);
    print_layout("H", 14, 5);
    exercise("H", 154, 14, 5);
    check_click_notifies_comparator(158, 13, 4, 20, 0);
    check_click_notifies_comparator(117, 4, 373, 1, 0);
    check_click_notifies_comparator(219, 9, 4, 20, 0);
    return 0;
}
