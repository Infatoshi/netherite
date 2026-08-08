#include "game/runtime.h"
#include "container_click.h"
#include "tile_entity_brewing.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "container click check failed at line %d: %s\n", \
            __LINE__, #condition); \
    exit(1); \
} } while (0)

static void init_runtime(GmRuntime *r, int creative) {
    GmConfig cfg;
    char error[256] = {0};
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    cfg.weather = 0;
    CHECK(gm_runtime_init(r, &cfg, error, sizeof error));
    r->tape_creative = creative;
    gm_player_cursor_set(ic_empty());
}

static ICStack inv(const GmRuntime *r, int slot) {
    return isr_get_stack(&r->player.inv, slot);
}

static void put(GmRuntime *r, int slot, int item, int count) {
    isr_set_stack(&r->player.inv, slot, ic_mk(item, count, 0));
}

static void print_stack(ICStack stack) {
    if (cc_is_empty(&stack)) printf("0:0:0");
    else printf("%d:%d:%d", stack.item, stack.count, stack.meta);
}

static void swaps(void) {
    GmRuntime r;
    init_runtime(&r, 0);
    put(&r, 9, 1, 5); put(&r, 0, 3, 3);
    CHECK(gm_container_click(&r, 9, 0, CC_CLICK_SWAP));
    printf("S0 "); print_stack(inv(&r, 9)); printf(" ");
    print_stack(inv(&r, 0)); printf(" ");
    print_stack(gm_player_cursor()); printf("\n");
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    put(&r, 0, 310, 1);
    CHECK(gm_container_click(&r, GMC_ARMOR0 + 3, 0, CC_CLICK_SWAP));
    printf("S1 "); print_stack(inv(&r, ISR_ARMOR_HEAD)); printf(" ");
    print_stack(inv(&r, 0)); printf("\n");
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    put(&r, 0, 1, 2);
    CHECK(gm_container_click(&r, GMC_ARMOR0 + 3, 0, CC_CLICK_SWAP));
    printf("S2 "); print_stack(inv(&r, ISR_ARMOR_HEAD)); printf(" ");
    print_stack(inv(&r, 0)); printf("\n");
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    put(&r, 0, 442, 1); put(&r, ISR_OFFHAND_SLOT, 50, 2);
    CHECK(gm_container_click(&r, GMC_OFFHAND, 0, CC_CLICK_SWAP));
    printf("S3 "); print_stack(inv(&r, ISR_OFFHAND_SLOT)); printf(" ");
    print_stack(inv(&r, 0)); printf("\n");
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    put(&r, 0, 310, 3);
    CHECK(gm_container_click(&r, GMC_ARMOR0 + 3, 0, CC_CLICK_SWAP));
    printf("S4 "); print_stack(inv(&r, ISR_ARMOR_HEAD)); printf(" ");
    print_stack(inv(&r, 0)); printf("\n");
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    put(&r, 0, 310, 3); put(&r, ISR_ARMOR_HEAD, 298, 1);
    CHECK(gm_container_click(&r, GMC_ARMOR0 + 3, 0, CC_CLICK_SWAP));
    printf("S5 "); print_stack(inv(&r, ISR_ARMOR_HEAD)); printf(" ");
    print_stack(inv(&r, 0)); printf(" ");
    print_stack(inv(&r, 1)); printf("\n");
    gm_runtime_destroy(&r);
}

static void clones(void) {
    GmRuntime r;
    init_runtime(&r, 1);
    put(&r, 9, 368, 7);
    CHECK(gm_container_click(&r, 9, 2, CC_CLICK_CLONE));
    printf("C0 "); print_stack(inv(&r, 9)); printf(" ");
    print_stack(gm_player_cursor()); printf("\n");
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    put(&r, 9, 368, 7);
    CHECK(gm_container_click(&r, 9, 2, CC_CLICK_CLONE));
    printf("C1 "); print_stack(inv(&r, 9)); printf(" ");
    print_stack(gm_player_cursor()); printf("\n");
    gm_runtime_destroy(&r);
}

static void quick_moves(void) {
    GmRuntime r;
    init_runtime(&r, 0);
    put(&r, 9, 442, 2);
    CHECK(gm_container_click(&r, 9, 0, CC_CLICK_QUICK_MOVE));
    printf("Q0 "); print_stack(inv(&r, 9)); printf(" ");
    print_stack(inv(&r, ISR_OFFHAND_SLOT)); printf("\n");
    CHECK(gm_container_click(&r, GMC_OFFHAND, 0, CC_CLICK_QUICK_MOVE));
    printf("Q1 "); print_stack(inv(&r, 9)); printf(" ");
    print_stack(inv(&r, ISR_OFFHAND_SLOT)); printf("\n");
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    put(&r, 9, 86, 2);
    CHECK(gm_container_click(&r, 9, 0, CC_CLICK_QUICK_MOVE));
    printf("Q2 "); print_stack(inv(&r, 9)); printf(" ");
    print_stack(inv(&r, ISR_ARMOR_HEAD)); printf("\n");
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    put(&r, 9, 397, 2);
    CHECK(gm_container_click(&r, 9, 0, CC_CLICK_QUICK_MOVE));
    printf("Q3 "); print_stack(inv(&r, 9)); printf(" ");
    print_stack(inv(&r, ISR_ARMOR_HEAD)); printf("\n");
    gm_runtime_destroy(&r);
}

static void drag(
        const char *tag, int creative, int start, int add, int end) {
    GmRuntime r;
    init_runtime(&r, creative);
    gm_player_cursor_set(ic_mk(1, 10, 0));
    CHECK(gm_container_click(&r, GMC_OUTSIDE, start, CC_CLICK_QUICK_CRAFT));
    CHECK(gm_container_click(&r, 9, add, CC_CLICK_QUICK_CRAFT));
    CHECK(gm_container_click(&r, 10, add, CC_CLICK_QUICK_CRAFT));
    CHECK(gm_container_click(&r, 11, add, CC_CLICK_QUICK_CRAFT));
    CHECK(gm_container_click(&r, GMC_OUTSIDE, end, CC_CLICK_QUICK_CRAFT));
    printf("%s ", tag); print_stack(inv(&r, 9)); printf(" ");
    print_stack(inv(&r, 10)); printf(" ");
    print_stack(inv(&r, 11)); printf(" ");
    print_stack(gm_player_cursor()); printf("\n");
    gm_runtime_destroy(&r);
}

static void interrupted_drag(void) {
    GmRuntime r;
    init_runtime(&r, 0);
    gm_player_cursor_set(ic_mk(1, 10, 0));
    CHECK(gm_container_click(&r, GMC_OUTSIDE, 0, CC_CLICK_QUICK_CRAFT));
    CHECK(gm_container_click(&r, 9, 1, CC_CLICK_QUICK_CRAFT));
    CHECK(gm_container_click(&r, 10, 0, CC_CLICK_PICKUP));
    CHECK(gm_container_click(&r, GMC_OUTSIDE, 2, CC_CLICK_QUICK_CRAFT));
    printf("D3 "); print_stack(inv(&r, 9)); printf(" ");
    print_stack(inv(&r, 10)); printf(" ");
    print_stack(gm_player_cursor()); printf("\n");
    gm_runtime_destroy(&r);
}

static void drag_edges(void) {
    GmRuntime r;
    init_runtime(&r, 0);
    gm_player_cursor_set(ic_mk(1, 10, 0));
    put(&r, 9, 1, 60); put(&r, 11, 3, 7);
    CHECK(gm_container_click(&r, GMC_OUTSIDE, 0, CC_CLICK_QUICK_CRAFT));
    CHECK(gm_container_click(&r, 9, 1, CC_CLICK_QUICK_CRAFT));
    CHECK(gm_container_click(&r, 10, 1, CC_CLICK_QUICK_CRAFT));
    CHECK(gm_container_click(&r, 11, 1, CC_CLICK_QUICK_CRAFT));
    CHECK(gm_container_click(&r, GMC_OUTSIDE, 2, CC_CLICK_QUICK_CRAFT));
    printf("D4 "); print_stack(inv(&r, 9)); printf(" ");
    print_stack(inv(&r, 10)); printf(" "); print_stack(inv(&r, 11));
    printf(" "); print_stack(gm_player_cursor()); printf("\n");
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    gm_player_cursor_set(ic_mk(368, 10, 0));
    put(&r, 9, 368, 15);
    CHECK(gm_container_click(&r, GMC_OUTSIDE, 0, CC_CLICK_QUICK_CRAFT));
    CHECK(gm_container_click(&r, 9, 1, CC_CLICK_QUICK_CRAFT));
    CHECK(gm_container_click(&r, 10, 1, CC_CLICK_QUICK_CRAFT));
    CHECK(gm_container_click(&r, GMC_OUTSIDE, 2, CC_CLICK_QUICK_CRAFT));
    printf("D5 "); print_stack(inv(&r, 9)); printf(" ");
    print_stack(inv(&r, 10)); printf(" ");
    print_stack(gm_player_cursor()); printf("\n");
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    gm_player_cursor_set(ic_mk(310, 3, 0));
    CHECK(gm_container_click(&r, GMC_OUTSIDE, 0, CC_CLICK_QUICK_CRAFT));
    CHECK(gm_container_click(
        &r, GMC_ARMOR0 + 3, 1, CC_CLICK_QUICK_CRAFT));
    CHECK(gm_container_click(&r, 9, 1, CC_CLICK_QUICK_CRAFT));
    CHECK(gm_container_click(&r, GMC_OUTSIDE, 2, CC_CLICK_QUICK_CRAFT));
    printf("D6 "); print_stack(inv(&r, ISR_ARMOR_HEAD)); printf(" ");
    print_stack(inv(&r, 9)); printf(" ");
    print_stack(gm_player_cursor()); printf("\n");
    gm_runtime_destroy(&r);
}

static void pickup_all(const char *tag, int button) {
    GmRuntime r;
    init_runtime(&r, 0);
    gm_player_cursor_set(ic_mk(1, 40, 0));
    put(&r, 9, 1, 64); put(&r, 10, 1, 5);
    put(&r, 11, 1, 20); put(&r, 0, 1, 7);
    CHECK(gm_container_click(&r, 12, button, CC_CLICK_PICKUP_ALL));
    printf("%s ", tag); print_stack(inv(&r, 9)); printf(" ");
    print_stack(inv(&r, 10)); printf(" ");
    print_stack(inv(&r, 11)); printf(" ");
    print_stack(inv(&r, 0)); printf(" ");
    print_stack(gm_player_cursor()); printf("\n");
    gm_runtime_destroy(&r);
}

static void pickup_all_edges(void) {
    GmRuntime r;
    ICStack stack;
    init_runtime(&r, 0);
    stack = ic_mk(1, 60, 0); stack.tag_id = 1;
    gm_player_cursor_set(stack);
    stack = ic_mk(1, 2, 0); stack.tag_id = 1; isr_set_stack(&r.player.inv, 9, stack);
    stack.tag_id = 2; isr_set_stack(&r.player.inv, 10, stack);
    CHECK(gm_container_click(&r, 11, 0, CC_CLICK_PICKUP_ALL));
    printf("A2 "); print_stack(inv(&r, 9)); printf(" ");
    print_stack(inv(&r, 10)); printf(" ");
    print_stack(gm_player_cursor()); printf("\n");
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    gm_player_cursor_set(ic_mk(368, 10, 0));
    put(&r, 9, 368, 10);
    CHECK(gm_container_click(&r, 10, 0, CC_CLICK_PICKUP_ALL));
    printf("A3 "); print_stack(inv(&r, 9)); printf(" ");
    print_stack(gm_player_cursor()); printf("\n");
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    gm_player_cursor_set(ic_mk(368, 1, 0));
    put(&r, 9, 368, 17); put(&r, 10, 368, 2);
    CHECK(gm_container_click(&r, 11, 0, CC_CLICK_PICKUP_ALL));
    printf("A4 "); print_stack(inv(&r, 9)); printf(" ");
    print_stack(inv(&r, 10)); printf(" ");
    print_stack(gm_player_cursor()); printf("\n");
    gm_runtime_destroy(&r);
}

static void print_matrix_stack(ICStack value);

static ICStack matrix_stack(int spec) {
    ICStack value;
    switch (spec) {
    case 0: return ic_empty();
    case 1: return ic_mk(1, 1, 0);
    case 2: return ic_mk(1, 5, 0);
    case 3: return ic_mk(1, 64, 0);
    case 4: return ic_mk(368, 15, 0);
    case 5: value = ic_mk(1, 5, 0); value.tag_id = 1; return value;
    case 6: return ic_mk(310, 1, 0);
    case 7: return ic_mk(1, 63, 0);
    case 8: return ic_mk(3, 3, 0);
    case 9: value = ic_mk(1, 1, 0); value.tag_id = 1; return value;
    case 10: value = ic_mk(1, 1, 0); value.tag_id = 2; return value;
    case 11: return ic_mk(1, 65, 0);
    case 12: return ic_mk(368, 16, 0);
    case 13: return ic_mk(368, 17, 0);
    case 14: return ic_mk(35, 5, 1);
    case 15: return ic_mk(35, 5, 2);
    default: return ic_mk(276, 1, 4);
    }
}

static void print_ordinary_row(int row, const GmRuntime *r)
{
    printf("O%05d", row);
    for (int slot = 0; slot <= ISR_OFFHAND_SLOT; ++slot) {
        printf(" ");
        print_matrix_stack(inv(r, slot));
    }
    printf(" ");
    print_matrix_stack(gm_player_cursor());
    for (int eid = 0; eid < r->next_entity_id; ++eid)
        for (int slot = 0; slot < GM_LIVE_MAX; ++slot) {
            const GmLiveEnt *entity = &r->entities.ents[slot];
            if (entity->active && entity->type == 0 && entity->eid == eid) {
                ICStack dropped = ic_mk(
                    entity->item, entity->count, entity->meta);
                dropped.repair_cost = entity->repair_cost;
                dropped.custom_name = entity->custom_name;
                dropped.tag_id = entity->tag_id;
                dropped.n_enchants = entity->n_enchants;
                for (int enchant = 0; enchant < entity->n_enchants;
                        ++enchant) {
                    dropped.enchants[enchant].id = entity->ench_id[enchant];
                    dropped.enchants[enchant].level =
                        entity->ench_lvl[enchant];
                }
                printf(" ");
                print_matrix_stack(dropped);
            }
        }
    printf("\n");
}

static void reset_ordinary(GmRuntime *r, int creative, int next_eid)
{
    for (int slot = 0; slot <= ISR_OFFHAND_SLOT; ++slot)
        isr_set_stack(&r->player.inv, slot, ic_empty());
    gm_player_cursor_set(ic_empty());
    memset(&r->entities, 0, sizeof r->entities);
    r->next_entity_id = next_eid;
    r->tape_creative = creative;
    r->container_drag_event = 0;
    r->container_drag_mode = 0;
    memset(r->container_drag_slots, 0, sizeof r->container_drag_slots);
}

/* Generated semantic partitions for the ordinary Slot path.  Unlike the
 * historical hand-picked rows this crosses empty, partial, full, overfull,
 * max-16, subtype, damage, and unequal-NBT stacks through every ClickType and
 * every legal button family.  Specialized Slot restrictions are exercised by
 * the actual-container rows below and by their dedicated live gates. */
static void ordinary_corpus(void)
{
    enum { SPECS = 17 };
    GmRuntime r;
    int row = 0;
    int next_eid;
    init_runtime(&r, 0);
    next_eid = r.next_entity_id;
    for (int slot_spec = 0; slot_spec < SPECS; ++slot_spec)
        for (int cursor_spec = 0; cursor_spec < SPECS; ++cursor_spec)
            for (int button = 0; button <= 1; ++button) {
                reset_ordinary(&r, 0, next_eid);
                isr_set_stack(&r.player.inv, 9, matrix_stack(slot_spec));
                gm_player_cursor_set(matrix_stack(cursor_spec));
                CHECK(gm_container_click(
                    &r, 9, button, CC_CLICK_PICKUP));
                print_ordinary_row(row++, &r);
            }

    for (int slot_spec = 0; slot_spec < SPECS; ++slot_spec)
        for (int button = 0; button <= 1; ++button) {
            reset_ordinary(&r, 0, next_eid);
            isr_set_stack(&r.player.inv, 9, matrix_stack(slot_spec));
            CHECK(gm_container_click(
                &r, 9, button, CC_CLICK_QUICK_MOVE));
            print_ordinary_row(row++, &r);
        }

    for (int slot_spec = 0; slot_spec < SPECS; ++slot_spec)
        for (int target_spec = 0; target_spec < SPECS; ++target_spec) {
            int button = (slot_spec * SPECS + target_spec) % 9;
            reset_ordinary(&r, 0, next_eid);
            isr_set_stack(&r.player.inv, 9, matrix_stack(slot_spec));
            isr_set_stack(&r.player.inv, button, matrix_stack(target_spec));
            CHECK(gm_container_click(&r, 9, button, CC_CLICK_SWAP));
            print_ordinary_row(row++, &r);
        }

    for (int slot_spec = 0; slot_spec < SPECS; ++slot_spec)
        for (int cursor_spec = 0; cursor_spec < SPECS; ++cursor_spec)
            for (int creative = 0; creative <= 1; ++creative) {
                int button = (slot_spec + cursor_spec + creative) % 3;
                reset_ordinary(&r, creative, next_eid);
                isr_set_stack(&r.player.inv, 9, matrix_stack(slot_spec));
                gm_player_cursor_set(matrix_stack(cursor_spec));
                CHECK(gm_container_click(
                    &r, 9, button, CC_CLICK_CLONE));
                print_ordinary_row(row++, &r);
            }

    for (int slot_spec = 0; slot_spec < SPECS; ++slot_spec)
        for (int cursor_spec = 0; cursor_spec < SPECS; ++cursor_spec)
            for (int button = 0; button <= 1; ++button) {
                reset_ordinary(&r, 0, next_eid);
                isr_set_stack(&r.player.inv, 9, matrix_stack(slot_spec));
                gm_player_cursor_set(matrix_stack(cursor_spec));
                CHECK(gm_container_click(&r, 9, button, CC_CLICK_THROW));
                print_ordinary_row(row++, &r);
            }

    for (int cursor_spec = 0; cursor_spec < SPECS; ++cursor_spec)
        for (int mode = 0; mode <= 2; ++mode) {
            reset_ordinary(&r, mode == 2, next_eid);
            gm_player_cursor_set(matrix_stack(cursor_spec));
            CHECK(gm_container_click(
                &r, GMC_OUTSIDE, mode << 2, CC_CLICK_QUICK_CRAFT));
            CHECK(gm_container_click(
                &r, 9, 1 | (mode << 2), CC_CLICK_QUICK_CRAFT));
            CHECK(gm_container_click(
                &r, 10, 1 | (mode << 2), CC_CLICK_QUICK_CRAFT));
            CHECK(gm_container_click(
                &r, GMC_OUTSIDE, 2 | (mode << 2), CC_CLICK_QUICK_CRAFT));
            print_ordinary_row(row++, &r);
        }

    for (int cursor_spec = 0; cursor_spec < SPECS; ++cursor_spec)
        for (int slot_spec = 0; slot_spec < SPECS; ++slot_spec)
            for (int button = 0; button <= 1; ++button) {
                reset_ordinary(&r, 0, next_eid);
                gm_player_cursor_set(matrix_stack(cursor_spec));
                isr_set_stack(&r.player.inv, 9, matrix_stack(slot_spec));
                isr_set_stack(&r.player.inv, 10,
                    matrix_stack(cursor_spec));
                CHECK(gm_container_click(
                    &r, 11, button, CC_CLICK_PICKUP_ALL));
                print_ordinary_row(row++, &r);
            }
    CHECK(row == 2686);
    gm_runtime_destroy(&r);
}

static void print_matrix_stack(ICStack value) {
    print_stack(value);
    printf(":%d", value.tag_id);
}

static void pickup_matrix(void) {
    static const int cursor_specs[] = {0, 1, 2, 7, 8, 9, 10};
    int row = 0;
    for (int slot_spec = 0; slot_spec <= 6; ++slot_spec)
        for (size_t cursor_at = 0;
                cursor_at < sizeof cursor_specs / sizeof cursor_specs[0];
                ++cursor_at)
            for (int button = 0; button <= 1; ++button) {
                GmRuntime r;
                init_runtime(&r, 0);
                isr_set_stack(&r.player.inv, 9, matrix_stack(slot_spec));
                gm_player_cursor_set(matrix_stack(cursor_specs[cursor_at]));
                CHECK(gm_container_click(&r, 9, button, CC_CLICK_PICKUP));
                printf("M%03d ", row++);
                print_matrix_stack(inv(&r, 9)); printf(" ");
                print_matrix_stack(gm_player_cursor()); printf("\n");
                gm_runtime_destroy(&r);
            }
}

static int live_item_count(const GmRuntime *r, int item) {
    int total = 0;
    for (int index = 0; index < GM_LIVE_MAX; ++index)
        if (r->entities.ents[index].active
                && r->entities.ents[index].type == 0
                && r->entities.ents[index].item == item)
            total += r->entities.ents[index].count;
    return total;
}

static void throws_from_slot(void) {
    GmRuntime r;
    init_runtime(&r, 0);
    put(&r, 9, 1, 3);
    CHECK(gm_container_click(&r, 9, 0, CC_CLICK_THROW));
    printf("T0 "); print_stack(inv(&r, 9));
    printf(" %d\n", live_item_count(&r, 1));
    CHECK(gm_container_click(&r, 9, 1, CC_CLICK_THROW));
    printf("T1 "); print_stack(inv(&r, 9));
    printf(" %d\n", live_item_count(&r, 1));
    put(&r, 9, 1, 3);
    gm_player_cursor_set(ic_mk(3, 1, 0));
    CHECK(gm_container_click(&r, 9, 1, CC_CLICK_THROW));
    printf("T2 "); print_stack(inv(&r, 9)); printf(" ");
    print_stack(gm_player_cursor());
    printf(" %d\n", live_item_count(&r, 1));
    gm_runtime_destroy(&r);
}

static void open_fake_furnace(GmRuntime *r) {
    r->container = 2;
    r->active_furnace = 0;
    r->furnaces[0].active = 1;
    furnace_live_init(&r->furnaces[0].state);
}

static void furnace_bucket(void) {
    GmRuntime r;
    init_runtime(&r, 0);
    open_fake_furnace(&r);
    put(&r, 0, 325, 4);
    CHECK(gm_container_click(
        &r, GMC_FURNACE0 + 1, 0, CC_CLICK_SWAP));
    printf("F0 "); print_stack(furnace_live_get_ic(
        &r.furnaces[0].state, FURNACE_LIVE_SLOT_FUEL)); printf(" ");
    print_stack(inv(&r, 0)); printf("\n");
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    open_fake_furnace(&r);
    put(&r, 0, 325, 4);
    furnace_live_set_ic(&r.furnaces[0].state,
        FURNACE_LIVE_SLOT_FUEL, ic_mk(263, 2, 0));
    CHECK(gm_container_click(
        &r, GMC_FURNACE0 + 1, 0, CC_CLICK_SWAP));
    printf("F1 "); print_stack(furnace_live_get_ic(
        &r.furnaces[0].state, FURNACE_LIVE_SLOT_FUEL)); printf(" ");
    print_stack(inv(&r, 0)); printf(" ");
    print_stack(inv(&r, 1)); printf("\n");
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    open_fake_furnace(&r);
    furnace_live_set_ic(&r.furnaces[0].state,
        FURNACE_LIVE_SLOT_OUTPUT, ic_mk(265, 1, 0));
    gm_player_cursor_set(ic_mk(265, 63, 0));
    CHECK(gm_container_click(
        &r, GMC_FURNACE0 + 2, 0, CC_CLICK_PICKUP));
    printf("F2 "); print_stack(furnace_live_get_ic(
        &r.furnaces[0].state, FURNACE_LIVE_SLOT_OUTPUT)); printf(" ");
    print_stack(gm_player_cursor()); printf("\n");
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    open_fake_furnace(&r);
    furnace_live_set_ic(&r.furnaces[0].state,
        FURNACE_LIVE_SLOT_OUTPUT, ic_mk(265, 4, 0));
    CHECK(gm_container_click(
        &r, GMC_FURNACE0 + 2, 0, CC_CLICK_SWAP));
    printf("F3 "); print_stack(furnace_live_get_ic(
        &r.furnaces[0].state, FURNACE_LIVE_SLOT_OUTPUT)); printf(" ");
    print_stack(inv(&r, 0)); printf("\n");
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    open_fake_furnace(&r);
    furnace_live_set_ic(&r.furnaces[0].state,
        FURNACE_LIVE_SLOT_OUTPUT, ic_mk(265, 1, 1));
    gm_player_cursor_set(ic_mk(265, 63, 2));
    CHECK(gm_container_click(
        &r, GMC_FURNACE0 + 2, 0, CC_CLICK_PICKUP));
    printf("F4 "); print_stack(furnace_live_get_ic(
        &r.furnaces[0].state, FURNACE_LIVE_SLOT_OUTPUT)); printf(" ");
    print_stack(gm_player_cursor()); printf("\n");
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    open_fake_furnace(&r);
    furnace_live_set_ic(&r.furnaces[0].state,
        FURNACE_LIVE_SLOT_OUTPUT, ic_mk(1, 1, 1));
    gm_player_cursor_set(ic_mk(1, 63, 2));
    CHECK(gm_container_click(
        &r, GMC_FURNACE0 + 2, 0, CC_CLICK_PICKUP));
    printf("F5 "); print_stack(furnace_live_get_ic(
        &r.furnaces[0].state, FURNACE_LIVE_SLOT_OUTPUT)); printf(" ");
    print_stack(gm_player_cursor()); printf("\n");
    gm_runtime_destroy(&r);
}

static void crafting_output_metadata(void)
{
    GmRuntime r;
    init_runtime(&r, 0);
    r.container = 1;
    r.craft_grid[0] = ic_mk(5, 1, 0);
    r.craft_grid[3] = ic_mk(5, 1, 0);
    gm_player_cursor_set(ic_mk(280, 60, 2));
    CHECK(gm_container_click(&r, GMC_RESULT, 0, CC_CLICK_PICKUP));
    printf("R4 "); print_stack(gm_container_result(&r)); printf(" ");
    print_stack(gm_player_cursor()); printf(" ");
    print_stack(r.craft_grid[0]); printf(" ");
    print_stack(r.craft_grid[3]); printf("\n");
    gm_runtime_destroy(&r);
}

static void open_fake_brewing(GmRuntime *r)
{
    int x = (int)r->player.ent.posX + 2;
    int y = (int)r->player.ent.posY;
    int z = (int)r->player.ent.posZ;
    CHECK(gm_runtime_set_block(r, x, y, z, 117, 0));
    CHECK(gm_runtime_brewing_set_slot(
        r, 0, x, y, z, 0, 0, 0, 0, 0, 0));
    r->brewing_enabled = 1;
    CHECK(gm_runtime_use_block(r, x, y, z));
    r->container = 4;
    CHECK(r->active_static_container >= 0);
}

static void print_brew_stack(ICStack stack)
{
    if (cc_is_empty(&stack)) printf("0:0:0");
    else printf("%d:%d:%d", stack.item, stack.count,
        stack.meta == TB_PT_WATER ? 1
        : stack.meta == TB_PT_AWKWARD ? 2 : 3);
}

static void brewing_takes(void)
{
    for (int mode = 0; mode < 5; ++mode) {
        GmRuntime r;
        init_runtime(&r, 0);
        open_fake_brewing(&r);
        r.static_containers[r.active_static_container].slots[0] = ic_mk(
            TB_POTION, 1, mode == 1 ? TB_PT_WATER : TB_PT_AWKWARD);
        CHECK(gm_container_click(&r, GMC_BREWING0, 0,
            mode <= 1 ? CC_CLICK_PICKUP
            : mode == 2 ? CC_CLICK_THROW
            : mode == 3 ? CC_CLICK_SWAP : CC_CLICK_QUICK_MOVE));
        printf("B%d ", mode);
        print_brew_stack(
            r.static_containers[r.active_static_container].slots[0]);
        printf(" "); print_brew_stack(gm_player_cursor());
        printf(" %d\n", gm_runtime_brew_event_count(&r));
        gm_runtime_destroy(&r);
    }

    {
        GmRuntime r;
        init_runtime(&r, 0);
        open_fake_brewing(&r);
        r.static_containers[r.active_static_container].slots[0] =
            ic_mk(TB_POTION, 1, TB_PT_AWKWARD);
        gm_player_cursor_set(ic_mk(TB_POTION, 1, TB_PT_WATER));
        CHECK(gm_container_click(
            &r, GMC_BREWING0, 0, CC_CLICK_PICKUP));
        printf("B5 "); print_brew_stack(
            r.static_containers[r.active_static_container].slots[0]);
        printf(" "); print_brew_stack(gm_player_cursor());
        printf(" %d\n", gm_runtime_brew_event_count(&r));
        gm_runtime_destroy(&r);
    }
}

static void brewing_event_persistence(void)
{
    static const char statistics[] =
        "{\"achievement.blazeRod\":1,\"achievement.potion\":2,"
        "\"oracle.unknown\":17}";
    const char *tmpdir = getenv("TMPDIR");
    char checkpoint[512], stats_path[512], text[512];
    GmRuntime r;
    GmRuntimeBrewEvent event;
    FILE *stream;
    size_t length;
    if (!tmpdir || !*tmpdir) tmpdir = ".tmp";
    CHECK(snprintf(checkpoint, sizeof checkpoint,
        "%s/container-brew.%ld.bin", tmpdir, (long)getpid())
        < (int)sizeof checkpoint);
    CHECK(snprintf(stats_path, sizeof stats_path,
        "%s/container-brew.%ld.json", tmpdir, (long)getpid())
        < (int)sizeof stats_path);

    init_runtime(&r, 0);
    CHECK(gm_runtime_restore_player_statistics(
        &r, statistics, sizeof statistics - 1, 0, 0));
    CHECK(r.stat_achievement_blaze_rod == 1
        && r.stat_achievement_blaze_rod_present
        && r.stat_achievement_potion == 2
        && r.stat_achievement_potion_present);
    CHECK(gm_runtime_brewed_potion_taken(
        &r, ic_mk(TB_POTION, 1, TB_PT_AWKWARD)));
    CHECK(r.stat_achievement_potion == 3
        && gm_runtime_brew_event_count(&r) == 1
        && gm_runtime_brew_event_get(&r, 0, &event)
        && event.stack.item == TB_POTION
        && event.stack.meta == TB_PT_AWKWARD
        && event.achievement_awarded);
    CHECK(gm_runtime_write_player_statistics(&r, stats_path));
    stream = fopen(stats_path, "rb");
    CHECK(stream != NULL);
    length = fread(text, 1, sizeof text - 1, stream);
    CHECK(!ferror(stream) && fclose(stream) == 0);
    text[length] = '\0';
    CHECK(strstr(text, "\"achievement.blazeRod\":1") != NULL
        && strstr(text, "\"achievement.potion\":3") != NULL
        && strstr(text, "\"oracle.unknown\":17") != NULL);

    CHECK(gm_runtime_write_checkpoint(&r, checkpoint));
    r.stat_achievement_blaze_rod = 0;
    r.stat_achievement_potion = 0;
    r.brew_event_count = 0;
    CHECK(gm_runtime_load_checkpoint(&r, checkpoint));
    CHECK(r.stat_achievement_blaze_rod == 1
        && r.stat_achievement_potion == 3
        && gm_runtime_brew_event_count(&r) == 1
        && gm_runtime_brew_event_get(&r, 0, &event)
        && event.achievement_awarded);
    gm_runtime_destroy(&r);
    CHECK(remove(checkpoint) == 0);
    CHECK(remove(stats_path) == 0);
}

static void enchanting_transfer_tags(void) {
    GmRuntime r;
    ICStack value;
    init_runtime(&r, 0);
    r.container = 5;
    r.enchanting.open = 1;
    value = ic_mk(1, 2, 0); value.tag_id = 1;
    isr_set_stack(&r.player.inv, 9, value);
    CHECK(gm_container_click(&r, 9, 0, CC_CLICK_QUICK_MOVE));
    printf("E0 "); print_stack(r.enchanting.slots[0]);
    printf(" %s ", r.enchanting.slots[0].tag_id ? "true" : "false");
    print_stack(inv(&r, 9));
    printf(" %s\n", inv(&r, 9).tag_id ? "true" : "false");
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    r.container = 5;
    r.enchanting.open = 1;
    value = ic_mk(1, 1, 0); value.tag_id = 1;
    isr_set_stack(&r.player.inv, 9, value);
    CHECK(gm_container_click(&r, 9, 0, CC_CLICK_QUICK_MOVE));
    printf("E1 "); print_stack(r.enchanting.slots[0]);
    printf(" %s ", r.enchanting.slots[0].tag_id ? "true" : "false");
    print_stack(inv(&r, 9)); printf("\n");
    gm_runtime_destroy(&r);
}

static void special_crafting_takes(void) {
    for (int mode = 0; mode < 3; ++mode) {
        GmRuntime r;
        init_runtime(&r, 0);
        r.container = 1;
        r.craft_grid[0] = ic_mk(299, 1, 0);
        r.craft_grid[1] = ic_mk(351, 1, 11);
        CHECK(gm_container_result(&r).item == 299);
        CHECK(gm_container_click(
            &r, GMC_RESULT, 0,
            mode == 0 ? CC_CLICK_PICKUP
                : mode == 1 ? CC_CLICK_QUICK_MOVE : CC_CLICK_THROW));
        printf("T%d ", mode); print_stack(gm_container_result(&r));
        printf(" "); print_stack(r.craft_grid[0]);
        printf(" "); print_stack(r.craft_grid[1]);
        printf(" "); print_stack(gm_player_cursor());
        printf(" %d\n", live_item_count(&r, 299));
        gm_runtime_destroy(&r);
    }
}

static void empty_map_use(void) {
    for (int count = 1; count <= 2; ++count) {
        GmRuntime r;
        GmAction action;
        const GmRuntimeMapData *map;
        ICStack filled;
        init_runtime(&r, 0);
        memset(&action, 0, sizeof action);
        r.player.inv.current_item = 0;
        r.player.ent.posX = 0.0;
        r.player.ent.posZ = 0.0;
        put(&r, 0, 395, count);
        action.do_place = 1;
        gm_runtime_tick(&r, action);
        filled = count == 1 ? inv(&r, 0) : inv(&r, 1);
        map = gm_runtime_map_data_ref(&r, filled.meta);
        CHECK(filled.item == 358 && map != NULL);
        printf("Q%d ", count - 1); print_stack(inv(&r, 0));
        printf(" "); print_stack(inv(&r, 1));
        printf(" %d %d %d %d %s %s\n",
            filled.meta, map->x_center, map->z_center, map->dimension,
            map->tracking_position ? "true" : "false",
            map->unlimited_tracking ? "true" : "false");
        gm_runtime_destroy(&r);
    }
}

static void result_edges(void) {
    GmRuntime r;
    init_runtime(&r, 0);
    r.craft_grid[0] = ic_mk(17, 1, 0);
    CHECK(gm_container_click(&r, GMC_RESULT, 0, CC_CLICK_THROW));
    printf("R0 "); print_stack(r.craft_grid[0]); printf(" ");
    print_stack(gm_container_result(&r));
    printf(" %d\n", live_item_count(&r, 5));
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    r.craft_grid[0] = ic_mk(17, 1, 0);
    CHECK(gm_container_click(&r, GMC_RESULT, 1, CC_CLICK_THROW));
    printf("R1 "); print_stack(r.craft_grid[0]); printf(" ");
    print_stack(gm_container_result(&r));
    printf(" %d\n", live_item_count(&r, 5));
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    r.container = 6;
    gm_anvil_live_open(&r.anvil, 0, 64, 0);
    r.player_xp_level = 30;
    r.anvil.slots[0] = ic_mk(421, 2, 0);
    r.anvil.slots[0].custom_name = 0;
    gm_anvil_live_set_name(
        &r.anvil, gm_runtime_item_name_intern(&r, "oracle"), 0);
    {
        ICStack cursor = r.anvil.slots[2];
        CHECK(cursor.item == 421 && cursor.count == 2
            && r.anvil.maximum_cost == 1);
        cursor.count = 63;
        gm_player_cursor_set(cursor);
    }
    CHECK(gm_container_click(&r, 9, 1, CC_CLICK_PICKUP_ALL));
    printf("R2 "); print_stack(r.anvil.slots[0]); printf(" ");
    print_stack(r.anvil.slots[2]); printf(" ");
    print_stack(gm_player_cursor()); printf(" %d\n", r.player_xp_level);
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    {
        const int eid = 701;
        int villager_slot;
        CHECK(gm_runtime_spawn_villager_fixture(
            &r, eid, 1.5, 5.0, 0.5, 0.0, 0.0, 0.0,
            0.0F, 20.0F, 0, 0, 0, 0, 0,
            UINT64_C(0x123456789abc), 0, 0.0));
        CHECK(gm_runtime_restore_villager_trade(&r, eid, 1, 1, 0, 0, 1));
        CHECK(gm_runtime_restore_villager_offer(&r, eid, 0, 0, 7, 1));
        CHECK(gm_runtime_restore_villager_offer_stack(
            &r, eid, 0, 0, ic_mk(388, 3, 0)));
        CHECK(gm_runtime_restore_villager_offer_stack(
            &r, eid, 0, 1, ic_empty()));
        CHECK(gm_runtime_restore_villager_offer_stack(
            &r, eid, 0, 2, ic_mk(297, 2, 0)));
        villager_slot = gm_mobs_find_slot_by_eid(&r.mobs, eid);
        CHECK(villager_slot > 0);
        r.mobs.growing_age[villager_slot] = 0;
        CHECK(gm_runtime_open_villager(&r, eid));
        r.merchant_slots[0] = ic_mk(388, 3, 0);
        gm_runtime_merchant_refresh(&r);
        gm_player_cursor_set(ic_mk(297, 63, 0));
        CHECK(gm_container_click(&r, 9, 0, CC_CLICK_PICKUP_ALL));
        printf("R3 "); print_stack(r.merchant_slots[0]); printf(" ");
        print_stack(r.merchant_slots[2]); printf(" ");
        print_stack(gm_player_cursor());
        printf(" %d\n",
            r.village_residents[0].trade.offers[0].uses);
    }
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    {
        const int eid = 702;
        int villager_slot;
        CHECK(gm_runtime_spawn_villager_fixture(
            &r, eid, 1.5, 5.0, 0.5, 0.0, 0.0, 0.0,
            0.0F, 20.0F, 0, 0, 0, 0, 0,
            UINT64_C(0x123456789abd), 0, 0.0));
        CHECK(gm_runtime_restore_villager_trade(
            &r, eid, 1, 1, 0, 0, 1));
        CHECK(gm_runtime_restore_villager_offer(&r, eid, 0, 0, 7, 1));
        CHECK(gm_runtime_restore_villager_offer_stack(
            &r, eid, 0, 0, ic_mk(388, 3, 0)));
        CHECK(gm_runtime_restore_villager_offer_stack(
            &r, eid, 0, 1, ic_empty()));
        CHECK(gm_runtime_restore_villager_offer_stack(
            &r, eid, 0, 2, ic_mk(297, 2, 0)));
        villager_slot = gm_mobs_find_slot_by_eid(&r.mobs, eid);
        CHECK(villager_slot > 0);
        r.mobs.growing_age[villager_slot] = 0;
        CHECK(gm_runtime_open_villager(&r, eid));
        r.merchant_slots[0] = ic_mk(388, 3, 0);
        gm_runtime_merchant_refresh(&r);
        gm_player_cursor_set(ic_mk(297, 62, 2));
        CHECK(gm_container_click(
            &r, GMC_MERCHANT0 + 2, 0, CC_CLICK_PICKUP));
        printf("R5 "); print_stack(r.merchant_slots[0]); printf(" ");
        print_stack(r.merchant_slots[2]); printf(" ");
        print_stack(gm_player_cursor());
        printf(" %d\n", r.village_residents[0].trade.offers[0].uses);
    }
    gm_runtime_destroy(&r);
}

static void print_drop_sequence(const char *tag, const GmRuntime *r) {
    printf("%s", tag);
    for (int eid = 0; eid < r->next_entity_id; ++eid)
        for (int slot = 0; slot < GM_LIVE_MAX; ++slot) {
            const GmLiveEnt *entity = &r->entities.ents[slot];
            if (entity->active && entity->type == 0 && entity->eid == eid) {
                printf(" "); print_stack(ic_mk(
                    entity->item, entity->count, entity->meta));
            }
        }
    printf("\n");
}

static void close_sequences(void) {
    GmRuntime r;
    init_runtime(&r, 0);
    gm_player_cursor_set(ic_mk(3, 2, 0));
    r.craft_grid[0] = ic_mk(17, 1, 0);
    r.craft_grid[1] = ic_mk(1, 3, 0);
    gm_container_close(&r);
    print_drop_sequence("L0", &r);
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    r.container = 5;
    r.enchanting.open = 1;
    gm_player_cursor_set(ic_mk(3, 2, 0));
    r.enchanting.slots[0] = ic_mk(276, 1, 0);
    r.enchanting.slots[1] = ic_mk(351, 4, 0);
    gm_container_close(&r);
    print_drop_sequence("L1", &r);
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    r.container = 6;
    gm_anvil_live_open(&r.anvil, 0, 64, 0);
    gm_player_cursor_set(ic_mk(3, 2, 0));
    r.anvil.slots[0] = ic_mk(267, 1, 0);
    r.anvil.slots[1] = ic_mk(265, 3, 0);
    gm_container_close(&r);
    print_drop_sequence("L2", &r);
    gm_runtime_destroy(&r);

    init_runtime(&r, 0);
    r.container = 7;
    gm_player_cursor_set(ic_mk(3, 2, 0));
    r.merchant_slots[0] = ic_mk(388, 3, 0);
    r.merchant_slots[1] = ic_mk(4, 1, 0);
    gm_container_close(&r);
    print_drop_sequence("L3", &r);
    gm_runtime_destroy(&r);
}

static void checkpoint_mid_drag(void) {
    GmRuntime r;
    const char *tmpdir = getenv("TMPDIR");
    char path[512];
    if (!tmpdir || !*tmpdir) tmpdir = ".tmp";
    CHECK(snprintf(path, sizeof path, "%s/container-drag.%ld.bin",
        tmpdir, (long)getpid()) < (int)sizeof path);
    init_runtime(&r, 0);
    gm_player_cursor_set(ic_mk(1, 10, 0));
    CHECK(gm_container_click(&r, GMC_OUTSIDE, 0, CC_CLICK_QUICK_CRAFT));
    CHECK(gm_container_click(&r, 9, 1, CC_CLICK_QUICK_CRAFT));
    CHECK(gm_container_click(&r, 10, 1, CC_CLICK_QUICK_CRAFT));
    CHECK(gm_runtime_write_checkpoint(&r, path));
    r.container_drag_event = 0;
    r.container_drag_mode = 0;
    for (int slot = 0; slot < GMC_SLOT_COUNT; ++slot)
        r.container_drag_slots[slot] = 0;
    put(&r, 9, 3, 17);
    gm_player_cursor_set(ic_empty());
    CHECK(gm_runtime_load_checkpoint(&r, path));
    CHECK(r.container_drag_event == 1 && r.container_drag_mode == 0
        && r.container_drag_slots[9] && r.container_drag_slots[10]
        && !r.container_drag_slots[11]
        && gm_player_cursor().item == 1
        && gm_player_cursor().count == 10
        && inv(&r, 9).item == 0);
    CHECK(gm_container_click(&r, 11, 1, CC_CLICK_QUICK_CRAFT));
    CHECK(gm_container_click(&r, GMC_OUTSIDE, 2, CC_CLICK_QUICK_CRAFT));
    CHECK(inv(&r, 9).item == 1 && inv(&r, 9).count == 3
        && inv(&r, 10).count == 3 && inv(&r, 11).count == 3
        && gm_player_cursor().count == 1);
    gm_runtime_destroy(&r);
    CHECK(remove(path) == 0);
}

int main(void) {
    swaps();
    clones();
    quick_moves();
    drag("D0", 0, 0, 1, 2);
    drag("D1", 0, 4, 5, 6);
    drag("D2", 1, 8, 9, 10);
    interrupted_drag();
    drag_edges();
    pickup_all("A0", 0);
    pickup_all("A1", 1);
    pickup_all_edges();
    pickup_matrix();
    ordinary_corpus();
    throws_from_slot();
    furnace_bucket();
    brewing_takes();
    brewing_event_persistence();
    enchanting_transfer_tags();
    result_edges();
    crafting_output_metadata();
    special_crafting_takes();
    empty_map_use();
    close_sequences();
    checkpoint_mid_drag();
    return 0;
}
