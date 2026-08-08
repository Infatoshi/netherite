#include "game/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition, message) do {                                      \
    if (!(condition)) {                                                     \
        fprintf(stderr, "FAIL: %s\n", (message));                         \
        return 0;                                                           \
    }                                                                       \
} while (0)

static int init_runtime(GmRuntime *runtime) {
    GmConfig config;
    char error[256];
    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(runtime, &config, error, sizeof error)) {
        fprintf(stderr, "FAIL: %s\n", error);
        return 0;
    }
    return 1;
}

static int same_double(double left, double right) {
    uint64_t left_bits, right_bits;
    memcpy(&left_bits, &left, sizeof left_bits);
    memcpy(&right_bits, &right, sizeof right_bits);
    return left_bits == right_bits;
}

/* Prove the item-frame cold store grows beyond historical maximum. */
static int hanging_cold_capacity(void) {
    GmRuntime runtime;
    GmRuntimeItemFrame frame;
    ICStack empty = ic_empty();
    const char *path = "test_hanging_capacity.checkpoint";
    CHECK(init_runtime(&runtime), "initialize hanging cold-capacity fixture");
    for (int index = 0; index < GM_RUNTIME_ITEM_FRAMES_MAX + 1; ++index) {
        int x = 4 + index % 16;
        int y = 20 + index / 16;
        int block_ok = gm_runtime_set_block(&runtime, x, y, 8, 1, 0);
        int frame_ok = block_ok && gm_runtime_item_frame_set_full(
            &runtime, 0, 900000 + index,
            x, y, 7, 2, empty, index & 7, index % 100,
            1.0F, (uint64_t)index, 0, 0.0,
            1000000 + index, 2000000 + index);
        if (!frame_ok) {
            fprintf(stderr,
                    "FAIL: item-frame cold store growth at index %d "
                    "(block=%d cap=%d count=%d)\n",
                    index, block_ok, runtime.item_frames_cap,
                    gm_runtime_item_frame_count(&runtime));
            gm_runtime_destroy(&runtime);
            return 0;
        }
    }
    CHECK(runtime.item_frames_cap > GM_RUNTIME_ITEM_FRAMES_MAX
              && gm_runtime_item_frame_count(&runtime)
                  == GM_RUNTIME_ITEM_FRAMES_MAX + 1
              && gm_runtime_item_frame_get(
                  &runtime, GM_RUNTIME_ITEM_FRAMES_MAX, &frame)
              && frame.eid == 900000 + GM_RUNTIME_ITEM_FRAMES_MAX
              && frame.tick_counter == GM_RUNTIME_ITEM_FRAMES_MAX % 100,
          "grown item-frame store retains terminal payload");
    CHECK(gm_runtime_write_checkpoint(&runtime, path)
              && gm_runtime_load_checkpoint(&runtime, path)
              && gm_runtime_item_frame_get(
                  &runtime, GM_RUNTIME_ITEM_FRAMES_MAX, &frame)
              && frame.eid == 900000 + GM_RUNTIME_ITEM_FRAMES_MAX
              && frame.tick_counter == GM_RUNTIME_ITEM_FRAMES_MAX % 100,
          "grown item-frame store survives checkpoint reload");
    (void)remove(path);
    gm_runtime_destroy(&runtime);
    return 1;
}

static unsigned long long double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return (unsigned long long)bits;
}

static int painting_art_width(int art) {
    static const int widths[26] = {
        16,16,16,16,16,16,16,32,32,32,32,32,16,16,
        32,32,32,32,32,32,64,64,64,64,64,64,
    };
    return art >= 0 && art < 26 ? widths[art] : 0;
}

static int painting_art_height(int art) {
    static const int heights[26] = {
        16,16,16,16,16,16,16,16,16,16,16,16,32,32,
        32,32,32,32,32,32,32,64,64,64,48,48,
    };
    return art >= 0 && art < 26 ? heights[art] : 0;
}

static int set_painting_support(
        GmRuntime *runtime, int x, int y, int z,
        int facing, int art, int block) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    static const int side_x[6] = {0, 0, -1, 1, 0, 0};
    static const int side_z[6] = {0, 0, 0, 0, 1, -1};
    int columns = painting_art_width(art) / 16;
    int rows = painting_art_height(art) / 16;
    int column_start = (columns - 1) / -2;
    int row_start = (rows - 1) / -2;
    int support_x = x - dx[facing];
    int support_z = z - dz[facing];
    for (int column = 0; column < columns; ++column)
        for (int row = 0; row < rows; ++row)
            if (!gm_runtime_set_block(
                    runtime,
                    support_x + side_x[facing] * (column + column_start),
                    y + row + row_start,
                    support_z + side_z[facing] * (column + column_start),
                    block, 0))
                return 0;
    return 1;
}

static int set_painting_support_size(
        GmRuntime *runtime, int x, int y, int z,
        int facing, int width, int height, int block) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    static const int side_x[6] = {0, 0, -1, 1, 0, 0};
    static const int side_z[6] = {0, 0, 0, 0, 1, -1};
    int columns = width / 16;
    int rows = height / 16;
    int column_start = (columns - 1) / -2;
    int row_start = (rows - 1) / -2;
    int support_x = x - dx[facing];
    int support_z = z - dz[facing];
    if (facing < 2 || facing > 5 || columns < 1 || rows < 1)
        return 0;
    for (int column = 0; column < columns; ++column)
        for (int row = 0; row < rows; ++row)
            if (!gm_runtime_set_block(
                    runtime,
                    support_x + side_x[facing]
                        * (column + column_start),
                    y + row + row_start,
                    support_z + side_z[facing]
                        * (column + column_start),
                    block, 0))
                return 0;
    return 1;
}

static int find_item(const GmRuntime *runtime, int item) {
    for (int slot = 0; slot < GM_LIVE_MAX; ++slot)
        if (runtime->entities.ents[slot].active
                && runtime->entities.ents[slot].type == 0
                && runtime->entities.ents[slot].item == item)
            return slot;
    return -1;
}

static int painting_geometry_and_lifecycle(void) {
    GmRuntime runtime;
    GmRuntimePainting painting;
    McAABB box;
    GmRuntimeSoundEvent sound;
    GmAction idle = {0};
    CHECK(init_runtime(&runtime), "initialize painting fixture");
    CHECK(runtime.paintings == NULL && runtime.paintings_cap == 0,
          "empty world does not allocate painting storage");
    CHECK(gm_runtime_set_block(&runtime, 8, 100, 9, 1, 0),
          "stage north painting support");
    CHECK(gm_runtime_painting_set(
              &runtime, 0, 7001, 8, 100, 8, 2, 0, 0)
              && runtime.paintings_cap == GM_RUNTIME_PAINTINGS_INITIAL
              && gm_runtime_painting_count(&runtime) == 1
              && gm_runtime_painting_get(&runtime, 0, &painting),
          "spawn exact 16 by 16 north painting");
    CHECK(same_double(painting.x, 8.5)
              && same_double(painting.y, 100.5)
              && same_double(painting.z, 8.96875)
              && painting.facing == 2 && painting.art == 0
              && painting.tick_counter == 0,
          "painting pose uses EntityHanging canonical geometry");
    CHECK(gm_runtime_painting_aabb(&painting, &box)
              && same_double(box.minX, 8.0)
              && same_double(box.minY, 100.0)
              && same_double(box.minZ, 8.9375)
              && same_double(box.maxX, 9.0)
              && same_double(box.maxY, 101.0)
              && same_double(box.maxZ, 9.0),
          "painting AABB uses EntityHanging canonical dimensions");
    CHECK(!gm_runtime_painting_set(
              &runtime, 0, 7002, 8, 100, 8, 2, 1, 0),
          "overlapping hanging entity is rejected");
    CHECK(gm_runtime_set_block(&runtime, 8, 100, 9, 0, 0)
              && gm_runtime_painting_count(&runtime) == 1,
          "support removal does not retire before periodic check");
    runtime.paintings[0].tick_counter = 100;
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_painting_count(&runtime) == 0
              && find_item(&runtime, 321) >= 0,
          "counter 100 support check retires and drops painting");
    CHECK(gm_runtime_sound_event_count(&runtime) > 0
              && gm_runtime_sound_event_get(
                  &runtime,
                  gm_runtime_sound_event_count(&runtime) - 1,
                  &sound)
              && sound.sound == GM_SOUND_PAINTING_BREAK
              && sound.category == GM_SOUND_CATEGORY_NEUTRAL,
          "painting break sound is represented");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int painting_footprint_and_placement(void) {
    GmRuntime runtime;
    GmRuntimePainting painting;
    CHECK(init_runtime(&runtime), "initialize painting footprint fixture");
    for (int x = 18; x <= 21; ++x)
        for (int y = 99; y <= 102; ++y)
            CHECK(gm_runtime_set_block(&runtime, x, y, 21, 1, 0),
                  "stage 64 by 64 support wall");
    CHECK(gm_runtime_painting_set(
              &runtime, 0, 7101, 20, 100, 20, 2, 21, 37)
              && gm_runtime_painting_get(&runtime, 0, &painting)
              && same_double(painting.x, 20.0)
              && same_double(painting.y, 101.0)
              && same_double(painting.z, 20.96875),
          "64 by 64 art uses exact side and vertical center offsets");
    CHECK(gm_runtime_break_painting(&runtime, 7101, 1)
              && find_item(&runtime, 321) < 0,
          "creative painting break suppresses item drop");
    isr_set_stack(&runtime.player.inv, 0, ic_mk(321, 2, 0));
    runtime.player.inv.current_item = 0;
    CHECK(gm_runtime_place_painting(
              &runtime, 20, 100, 20, 2, 0, 0)
              && gm_runtime_painting_count(&runtime) == 1
              && isr_get_stack(&runtime.player.inv, 0).count == 1,
          "survival painting item selects a fitting art and is consumed");
    CHECK(gm_runtime_painting_get(&runtime, 0, &painting)
              && painting.art >= 0 && painting.art < 26
              && painting.uuid_present,
          "placed painting owns art, identity, and canonical state");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int leash_knot_transfer_and_break(void) {
    GmRuntime runtime;
    GmRuntimeLeashKnot knot;
    GmLlamaState llama;
    McAABB box;
    GmAction idle = {0};
    CHECK(init_runtime(&runtime), "initialize leash-knot fixture");
    CHECK(gm_runtime_set_block(&runtime, 12, 100, 12, 85, 0),
          "stage oak fence");
    CHECK(gm_mobs_spawn_llama_exact(
              &runtime.mobs, 7201, 13.5, 100.0, 12.5,
              0.0, 0.0, 0.0, 0.0F, 20.0F, 1,
              20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
              0, 0, 1, -1, 0, 0, 1) > 0
              && gm_mobs_restore_llama_links(
                  &runtime.mobs, 7201, 1, 0, -1, -1, 2.1, 0),
          "stage player-leashed llama");
    CHECK(gm_runtime_set_entity_id_cursor(&runtime, 7300)
              && gm_runtime_attach_llamas_to_fence(
                  &runtime, 12, 100, 12) == 1
              && gm_runtime_leash_knot_count(&runtime) == 1
              && gm_runtime_leash_knot_get(&runtime, 0, &knot)
              && knot.eid == 7300 && knot.uuid_present
              && same_double(knot.x, 12.5)
              && same_double(knot.y, 100.5)
              && same_double(knot.z, 12.5)
              && gm_mobs_get_llama_state(&runtime.mobs, 7201, &llama)
              && llama.leashed && llama.leash_holder_kind == 3
              && llama.leash_holder_eid == 7300,
          "lead on fence creates knot and transfers player leash");
    CHECK(gm_runtime_leash_knot_aabb(&knot, &box)
              && same_double(box.minX, 12.3125)
              && same_double(box.minY, 100.375)
              && same_double(box.minZ, 12.3125)
              && same_double(box.maxX, 12.6875)
              && same_double(box.maxY, 100.875)
              && same_double(box.maxZ, 12.6875),
          "leash-knot AABB uses vanilla dimensions");
    CHECK(gm_runtime_leash_knot_interact(&runtime, 7300, 0)
              && gm_runtime_leash_knot_count(&runtime) == 0
              && gm_mobs_get_llama_state(&runtime.mobs, 7201, &llama)
              && llama.leashed && llama.leash_holder_kind == 3
              && llama.leash_holder_eid == -1,
          "empty survival knot interaction invalidates holder first");
    runtime.restored_active_mobs_enabled = 1;
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_mobs_get_llama_state(&runtime.mobs, 7201, &llama)
              && !llama.leashed && llama.leash_holder_kind == 0
              && llama.leash_holder_eid == -1
              && find_item(&runtime, 420) >= 0,
          "next EntityCreature leash update drops lead from dead knot");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int leash_knot_periodic_support(void) {
    GmRuntime runtime;
    GmAction idle = {0};
    CHECK(init_runtime(&runtime), "initialize knot support fixture");
    CHECK(gm_runtime_set_block(&runtime, 16, 100, 16, 85, 0)
              && gm_runtime_leash_knot_set(
                  &runtime, 0, 7401, 16, 100, 16, 100),
          "stage counter-100 leash knot");
    CHECK(gm_runtime_set_block(&runtime, 16, 100, 16, 0, 0)
              && gm_runtime_leash_knot_count(&runtime) == 1,
          "fence removal waits for hanging support clock");
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_leash_knot_count(&runtime) == 0,
          "counter-100 knot retires after fence disappears");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int pending_leash_reload_boundary(void) {
    GmRuntime runtime;
    GmRuntimeLeashKnot knot;
    GmLlamaState llama;
    GmAction idle = {0};
    CHECK(init_runtime(&runtime), "initialize pending leash fixture");
    CHECK(gm_runtime_set_block(&runtime, 20, 100, 20, 85, 0)
              && gm_mobs_spawn_llama_exact(
                  &runtime.mobs, 7450, 21.5, 100.0, 20.5,
                  0.0, 0.0, 0.0, 0.0F, 20.0F, 1,
                  20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
                  0, 0, 1, -1, 0, 0, 1) > 0
              && gm_runtime_set_entity_id_cursor(&runtime, 7451)
              && gm_runtime_restore_llama_leash_pending(
                  &runtime, 7450, 20, 100, 20),
          "restore pending fence-coordinate leash NBT");
    runtime.restored_active_mobs_enabled = 1;
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_leash_knot_count(&runtime) == 1
              && gm_runtime_leash_knot_get(&runtime, 0, &knot)
              && knot.eid == 7451 && knot.tick_counter == 1
              && knot.uuid_present
              && gm_mobs_get_llama_state(&runtime.mobs, 7450, &llama)
              && llama.leashed && llama.leash_holder_kind == 3
              && llama.leash_holder_eid == 7451
              && runtime.leash_knot_pending_count == 0,
          "first living tick recreates and advances unsaved leash knot");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int item_frame_lifecycle(void) {
    static const unsigned char item_tag[] = {
        10,0,0,3,0,6,'C','u','s','t','o','m',0,0,0,42,0,
    };
    GmRuntime runtime;
    GmRuntimeItemFrame frame;
    GmRuntimeSoundEvent sound;
    GmAction idle = {0};
    ICStack displayed = ic_mk(276, 1, 7);
    ICStack held;
    char path[160];
    int tag_id, name_id, drop_slot;
    (void)mkdir(".tmp", 0700);
    snprintf(path, sizeof path,
             ".tmp/test_item_frame_checkpoint.%ld.bin", (long)getpid());
    CHECK(init_runtime(&runtime), "initialize item-frame fixture");
    CHECK(gm_runtime_set_block(&runtime, 32, 100, 33, 1, 0),
          "stage north item-frame support");
    tag_id = gm_runtime_stack_tag_intern(
        &runtime, item_tag, sizeof item_tag);
    name_id = gm_runtime_item_name_intern(&runtime, "Oracle blade");
    displayed.repair_cost = 9;
    displayed.custom_name = name_id;
    displayed.tag_id = tag_id;
    displayed.n_enchants = 2;
    displayed.enchants[0].id = 16;
    displayed.enchants[0].level = 5;
    displayed.enchants[1].id = 34;
    displayed.enchants[1].level = 3;
    CHECK(tag_id > 0 && name_id > 0
              && gm_runtime_item_frame_set_full(
                  &runtime, 0, 7601, 32, 100, 32, 2,
                  displayed, 5, 73, 1.0F,
                  UINT64_C(0x123456789abc), 1, 0.25,
                  (int64_t)UINT64_C(0xf123456789abcdef),
                  (int64_t)UINT64_C(0x8123456789abcdef))
              && gm_runtime_restore_loaded_entity_order(
                  &runtime, 0, 7601)
              && gm_runtime_item_frame_get(&runtime, 0, &frame),
          "restore arbitrary tagged item-frame state");
    CHECK(frame.item == 276 && frame.count == 1 && frame.meta == 7
              && frame.rotation == 5 && frame.tick_counter == 73
              && frame.item_drop_chance == 1.0F
              && frame.random_seed48 == UINT64_C(0x123456789abc)
              && frame.random_have_gaussian
              && same_double(frame.random_gaussian, 0.25)
              && frame.uuid_present
              && (uint64_t)frame.uuid_most
                  == UINT64_C(0xf123456789abcdef)
              && (uint64_t)frame.uuid_least
                  == UINT64_C(0x8123456789abcdef)
              && frame.repair_cost == 9 && frame.custom_name == name_id
              && frame.tag_id == tag_id && frame.n_enchants == 2
              && frame.enchants[0].id == 16
              && frame.enchants[0].level == 5,
          "item-frame stack, RNG, identity, and clock round-trip exactly");
    CHECK(gm_runtime_write_checkpoint(&runtime, path)
              && gm_runtime_damage_item_frame(&runtime, 7601, 0, 0)
              && gm_runtime_load_checkpoint(&runtime, path)
              && gm_runtime_item_frame_get(&runtime, 0, &frame)
              && frame.item == 276 && frame.tag_id == tag_id
              && frame.tick_counter == 73
              && frame.random_seed48 == UINT64_C(0x123456789abc),
          "native checkpoint restores the rich item-frame payload");
    CHECK(gm_runtime_item_frame_interact(&runtime, 7601, 0, 0)
              && gm_runtime_item_frame_get(&runtime, 0, &frame)
              && frame.rotation == 6
              && gm_runtime_sound_event_get(
                  &runtime,
                  gm_runtime_sound_event_count(&runtime) - 1,
                  &sound)
              && sound.sound == GM_SOUND_ITEM_FRAME_ROTATE_ITEM,
          "interaction rotates a filled frame and emits its exact sound");
    CHECK(gm_runtime_damage_item_frame(&runtime, 7601, 0, 0)
              && gm_runtime_item_frame_get(&runtime, 0, &frame)
              && frame.item == 0 && frame.rotation == 6,
          "first non-explosion hit removes only the displayed stack");
    drop_slot = find_item(&runtime, 276);
    CHECK(drop_slot >= 0
              && runtime.entities.ents[drop_slot].count == 1
              && runtime.entities.ents[drop_slot].meta == 7
              && runtime.entities.ents[drop_slot].repair_cost == 9
              && runtime.entities.ents[drop_slot].custom_name == name_id
              && runtime.entities.ents[drop_slot].tag_id == tag_id
              && runtime.entities.ents[drop_slot].n_enchants == 2,
          "displayed-item drop preserves arbitrary stack state");
    CHECK(gm_runtime_damage_item_frame(&runtime, 7601, 0, 0)
              && gm_runtime_item_frame_count(&runtime) == 0
              && find_item(&runtime, 389) >= 0,
          "second hit breaks the empty frame and drops the frame item");

    CHECK(gm_runtime_set_entity_id_cursor(&runtime, 7700),
          "set live placement entity cursor");
    isr_set_stack(&runtime.player.inv, 0, ic_mk(389, 2, 0));
    runtime.player.inv.current_item = 0;
    CHECK(gm_runtime_place_item_frame(
              &runtime, 32, 100, 32, 2, 0, 0)
              && gm_runtime_item_frame_get(&runtime, 0, &frame)
              && frame.eid == 7700 && frame.item == 0
              && frame.tick_counter == 0 && frame.uuid_present
              && isr_get_stack(&runtime.player.inv, 0).count == 1
              && gm_runtime_sound_event_get(
                  &runtime,
                  gm_runtime_sound_event_count(&runtime) - 1,
                  &sound)
              && sound.sound == GM_SOUND_ITEM_FRAME_PLACE,
          "survival placement constructs, orders, sounds, and consumes a frame");
    held = displayed;
    held.count = 3;
    isr_set_stack(&runtime.player.inv, 0, held);
    CHECK(gm_runtime_item_frame_interact(&runtime, 7700, 0, 0)
              && gm_runtime_item_frame_get(&runtime, 0, &frame)
              && frame.item == displayed.item && frame.count == 1
              && frame.tag_id == tag_id
              && isr_get_stack(&runtime.player.inv, 0).count == 2
              && gm_runtime_sound_event_get(
                  &runtime,
                  gm_runtime_sound_event_count(&runtime) - 1,
                  &sound)
              && sound.sound == GM_SOUND_ITEM_FRAME_ADD_ITEM,
          "empty-frame interaction copies one full stack and consumes one");
    CHECK(gm_runtime_set_block(&runtime, 32, 100, 33, 0, 0)
              && gm_runtime_item_frame_count(&runtime) == 1,
          "support loss waits for the periodic hanging check");
    runtime.item_frames[0].tick_counter = 100;
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_item_frame_count(&runtime) == 0,
          "unsupported filled frame breaks completely on the periodic check");
    (void)remove(path);
    gm_runtime_destroy(&runtime);
    return 1;
}

static int mixed_hanging_loaded_order(void) {
    static const int orders[6][3] = {
        {0, 1, 2}, {0, 2, 1}, {1, 0, 2},
        {1, 2, 0}, {2, 0, 1}, {2, 1, 0},
    };
    static const int eids[3] = {8701, 8702, 8703};
    for (int row = 0; row < 6; ++row) {
        GmRuntime runtime;
        int loaded_eid = -1;
        CHECK(init_runtime(&runtime), "initialize mixed hanging order");
        CHECK(gm_runtime_set_block(&runtime, 40, 100, 41, 1, 0)
                  && gm_runtime_set_block(&runtime, 44, 100, 41, 1, 0)
                  && gm_runtime_set_block(&runtime, 48, 100, 40, 85, 0)
                  && gm_runtime_item_frame_set_full(
                      &runtime, 0, eids[0], 40, 100, 40, 2,
                      ic_empty(), 0, 100, 1.0F,
                      UINT64_C(0x123456789abc), 0, 0.0, 1, 2)
                  && gm_runtime_painting_set(
                      &runtime, 0, eids[1], 44, 100, 40, 2, 0, 100)
                  && gm_runtime_leash_knot_set(
                      &runtime, 0, eids[2], 48, 100, 40, 100),
              "stage all three hanging stores");
        for (int order = 0; order < 3; ++order)
            CHECK(gm_runtime_restore_loaded_entity_order(
                      &runtime, order, eids[orders[row][order]]),
                  "restore arbitrary mixed hanging order");
        CHECK(gm_runtime_set_block(&runtime, 40, 100, 41, 0, 0)
                  && gm_runtime_set_block(&runtime, 44, 100, 41, 0, 0)
                  && gm_runtime_set_block(&runtime, 48, 100, 40, 0, 0),
              "remove all mixed hanging supports");
        gm_runtime_tick_hanging_entities(&runtime);
        CHECK(gm_runtime_item_frame_count(&runtime) == 0
                  && gm_runtime_painting_count(&runtime) == 0
                  && gm_runtime_leash_knot_count(&runtime) == 0,
              "mixed ordered pass visits and retires all three stores");
        int expected_first = orders[row][0] == 0 ? 389
            : orders[row][0] == 1 ? 321
            : orders[row][1] == 0 ? 389 : 321;
        int expected_second = expected_first == 389 ? 321 : 389;
        int observed[2] = {0, 0}, observed_count = 0;
        for (int slot = 0; slot < GM_LIVE_MAX; ++slot) {
            const GmLiveEnt *drop = &runtime.entities.ents[slot];
            if (!drop->active || drop->type != 0) continue;
            if (observed_count < 2) observed[observed_count] = drop->item;
            ++observed_count;
        }
        CHECK(observed_count == 2 && observed[0] == expected_first
                  && observed[1] == expected_second,
              "mixed callback drop order follows loadedEntityList");
        CHECK(gm_runtime_loaded_entity_order_get(
                  &runtime, 0, &loaded_eid)
                  && loaded_eid == runtime.entities.ents[0].eid
                  && gm_runtime_loaded_entity_order_get(
                      &runtime, 1, &loaded_eid)
                  && loaded_eid == runtime.entities.ents[1].eid,
              "self-removal compacts order before callback drops append");
        gm_runtime_destroy(&runtime);
    }
    return 1;
}

static int hanging_checkpoint(void) {
    GmRuntime runtime;
    GmRuntimePainting painting;
    GmRuntimeLeashKnot knot;
    GmRuntimeItemFrame frame;
    GmLlamaState llama;
    unsigned char map_colors[128 * 128];
    char path[160];
    (void)mkdir(".tmp", 0700);
    snprintf(path, sizeof path,
             ".tmp/test_hanging_checkpoint.%ld.bin", (long)getpid());
    for (int i = 0; i < 128 * 128; ++i)
        map_colors[i] = (unsigned char)((i % 35 + 1) * 4 + (i & 3));
    CHECK(init_runtime(&runtime), "initialize hanging checkpoint fixture");
    CHECK(gm_runtime_set_block(&runtime, 24, 100, 25, 1, 0)
              && gm_runtime_set_block(&runtime, 28, 100, 28, 85, 0)
              && gm_runtime_set_block(&runtime, 32, 100, 33, 1, 0)
              && gm_runtime_painting_set(
                  &runtime, 0, 7501, 24, 100, 24, 2, 0, 73)
              && gm_runtime_painting_set_uuid(
                  &runtime, 7501,
                  (int64_t)UINT64_C(0xf123456789abcdef),
                  (int64_t)UINT64_C(0x8123456789abcdef))
              && gm_runtime_leash_knot_set(
                  &runtime, 0, 7502, 28, 100, 28, 44)
              && gm_runtime_leash_knot_set_uuid(
                  &runtime, 7502,
                  (int64_t)UINT64_C(0x7123456789abcdef),
                  (int64_t)UINT64_C(0x6123456789abcdef))
              && gm_mobs_spawn_llama_exact(
                  &runtime.mobs, 7503, 29.5, 100.0, 28.5,
                  0.0, 0.0, 0.0, 0.0F, 20.0F, 1,
                  20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
                  0, 0, 1, -1, 0, 0, 1) > 0
              && gm_runtime_restore_llama_leash_knot(
                  &runtime, 7503, 7502)
              && gm_runtime_item_frame_set_full(
                  &runtime, 0, 7504, 32, 100, 32, 2,
                  ic_mk(358, 1, 17), 3, 19, 1.0F,
                  UINT64_C(0x123456789abc), 0, 0.0, 3, 4)
              && gm_runtime_item_frame_set_map_state(
                  &runtime, 7504, 47, 1, 0, 16, 0, 2,
                  1, 0, 1, 1, 8, 16, 8)
              && gm_runtime_item_frame_set_map_colors(
                  &runtime, 7504, map_colors)
              && gm_runtime_write_checkpoint(&runtime, path),
          "write painting, knot, and leash graph checkpoint");
    CHECK(gm_runtime_break_painting(&runtime, 7501, 1)
              && gm_runtime_leash_knot_interact(&runtime, 7502, 1)
              && gm_runtime_damage_item_frame(&runtime, 7504, 1, 0)
              && gm_runtime_load_checkpoint(&runtime, path),
          "reload hanging checkpoint after destructive mutations");
    CHECK(gm_runtime_painting_get(&runtime, 0, &painting)
              && painting.eid == 7501 && painting.tick_counter == 73
              && painting.uuid_present
              && (uint64_t)painting.uuid_most
                  == UINT64_C(0xf123456789abcdef)
              && (uint64_t)painting.uuid_least
                  == UINT64_C(0x8123456789abcdef)
              && gm_runtime_leash_knot_get(&runtime, 0, &knot)
              && knot.eid == 7502 && knot.tick_counter == 44
              && knot.uuid_present
              && (uint64_t)knot.uuid_most
                  == UINT64_C(0x7123456789abcdef)
              && (uint64_t)knot.uuid_least
                  == UINT64_C(0x6123456789abcdef)
              && gm_mobs_get_llama_state(&runtime.mobs, 7503, &llama)
              && llama.leashed && llama.leash_holder_kind == 3
              && llama.leash_holder_eid == 7502
              && gm_runtime_item_frame_get(&runtime, 0, &frame)
              && frame.eid == 7504 && frame.item == 358
              && frame.tracker_update_counter == 47
              && frame.map_data_present && frame.map_x_center == 16
              && frame.map_z_center == 0 && frame.map_scale == 2
              && frame.map_tracking_position
              && frame.map_decoration_present
              && frame.map_decoration_type == 1
              && frame.map_decoration_x == 8
              && frame.map_decoration_z == 16
              && frame.map_decoration_rotation == 8
              && gm_runtime_item_frame_map_colors(&runtime, 0)
              && !memcmp(gm_runtime_item_frame_map_colors(&runtime, 0),
                         map_colors, sizeof map_colors),
          "checkpoint restores hanging, map pixels, and llama-knot state exactly");
    (void)remove(path);
    gm_runtime_destroy(&runtime);
    return 1;
}

static void print_drop_rows(const GmRuntime *runtime) {
    int first = 1;
    fputs("[", stdout);
    for (int slot = 0; slot < GM_LIVE_MAX; ++slot) {
        const GmLiveEnt *item = &runtime->entities.ents[slot];
        if (!item->active || item->type != 0
                || (item->item != 321 && item->item != 420))
            continue;
        printf("%s{\"item\":%d,\"count\":%d,"
               "\"x_bits\":\"%016llx\",\"y_bits\":\"%016llx\","
               "\"z_bits\":\"%016llx\",\"pickup_delay\":%d}",
               first ? "" : ",", item->item, item->count,
               double_bits(item->x), double_bits(item->y),
               double_bits(item->z), item->pickup_delay);
        first = 0;
    }
    fputs("]", stdout);
}

static void print_item_frame_drop_rows(const GmRuntime *runtime) {
    int first = 1;
    fputs("[", stdout);
    for (int slot = 0; slot < GM_LIVE_MAX; ++slot) {
        const GmLiveEnt *item = &runtime->entities.ents[slot];
        const char *custom_name;
        if (!item->active || item->type != 0
                || (item->item != 276 && item->item != 389))
            continue;
        custom_name = gm_runtime_item_name(runtime, item->custom_name);
        printf("%s{\"item\":%d,\"count\":%d,\"meta\":%d,"
               "\"repair_cost\":%d,\"custom_name\":\"%s\","
               "\"enchants\":[",
               first ? "" : ",", item->item, item->count, item->meta,
               item->repair_cost, custom_name ? custom_name : "");
        for (int enchant = 0; enchant < item->n_enchants; ++enchant)
            printf("%s[%d,%d]", enchant ? "," : "",
                   item->ench_id[enchant], item->ench_lvl[enchant]);
        printf("],\"x_bits\":\"%016llx\","
               "\"y_bits\":\"%016llx\","
               "\"z_bits\":\"%016llx\",\"pickup_delay\":%d}",
               double_bits(item->x), double_bits(item->y),
               double_bits(item->z), item->pickup_delay);
        first = 0;
    }
    fputs("]", stdout);
}

static int painting_oracle(
        int x, int y, int z, int facing, int art,
        int tick_counter, int remove_support) {
    GmRuntime runtime;
    GmRuntimePainting painting;
    McAABB box;
    int active_after;
    if (!init_runtime(&runtime)
            || !set_painting_support(
                &runtime, x, y, z, facing, art, 1)
            || !gm_runtime_painting_set(
                &runtime, 0, 8001, x, y, z,
                facing, art, tick_counter)
            || !gm_runtime_painting_get(&runtime, 0, &painting)
            || !gm_runtime_painting_aabb(&painting, &box)) {
        fprintf(stderr, "FAIL: painting oracle fixture\n");
        return 0;
    }
    printf("{\"valid_before\":true,\"art\":%d,\"width\":%d,"
           "\"height\":%d,\"facing\":%d,"
           "\"x_bits\":\"%016llx\",\"y_bits\":\"%016llx\","
           "\"z_bits\":\"%016llx\","
           "\"min_x_bits\":\"%016llx\","
           "\"min_y_bits\":\"%016llx\","
           "\"min_z_bits\":\"%016llx\","
           "\"max_x_bits\":\"%016llx\","
           "\"max_y_bits\":\"%016llx\","
           "\"max_z_bits\":\"%016llx\",",
           art, painting_art_width(art), painting_art_height(art), facing,
           double_bits(painting.x), double_bits(painting.y),
           double_bits(painting.z),
           double_bits(box.minX), double_bits(box.minY),
           double_bits(box.minZ), double_bits(box.maxX),
           double_bits(box.maxY), double_bits(box.maxZ));
    if (remove_support) {
        if (!set_painting_support(
                &runtime, x, y, z, facing, art, 0)) {
            gm_runtime_destroy(&runtime);
            return 0;
        }
        gm_runtime_tick_hanging_entities(&runtime);
    }
    active_after = gm_runtime_painting_get(&runtime, 0, &painting);
    printf("\"dead_after\":%s,\"tick_counter_after\":%d,\"drops\":",
           active_after ? "false" : "true",
           active_after ? painting.tick_counter : 0);
    print_drop_rows(&runtime);
    puts("}");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int painting_constructor_oracle(
        int x, int y, int z, int facing, int support_width,
        int support_height, uint64_t generator_seed48) {
    GmRuntime runtime;
    GmRuntimePainting painting;
    McAABB box;
    JavaRandom generator;
    JavaRandom entity_random;
    int valid_count = 0;
    if (!init_runtime(&runtime)
            || support_width < 16 || support_width > 64
            || support_height < 16 || support_height > 64
            || support_width % 16 != 0 || support_height % 16 != 0
            || !set_painting_support_size(
                &runtime, x, y, z, facing,
                support_width, support_height, 1))
        return 0;
    for (int art = 0; art < 26; ++art)
        if (painting_art_width(art) <= support_width
                && painting_art_height(art) <= support_height)
            ++valid_count;
    isr_set_stack(&runtime.player.inv, 0, ic_mk(321, 1, 0));
    runtime.player.inv.current_item = 0;
    if (!gm_runtime_set_entity_seed_generator_seed48(
                &runtime, generator_seed48)
            || !gm_runtime_place_painting(
                &runtime, x, y, z, facing, 0, 0)
            || !gm_runtime_painting_get(&runtime, 0, &painting)
            || !gm_runtime_painting_aabb(&painting, &box)) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    jrand_set_seed48(&generator, generator_seed48);
    jrand_set(&entity_random, jrand_long(&generator));
    (void)jrand_int_bound(&entity_random, valid_count);
    printf("{\"valid_before\":true,\"art\":%d,\"width\":%d,"
           "\"height\":%d,\"facing\":%d,"
           "\"x_bits\":\"%016llx\",\"y_bits\":\"%016llx\","
           "\"z_bits\":\"%016llx\","
           "\"min_x_bits\":\"%016llx\","
           "\"min_y_bits\":\"%016llx\","
           "\"min_z_bits\":\"%016llx\","
           "\"max_x_bits\":\"%016llx\","
           "\"max_y_bits\":\"%016llx\","
           "\"max_z_bits\":\"%016llx\","
           "\"dead_after\":false,\"tick_counter_after\":0,"
           "\"entity_seed_generator_seed48_after\":%llu,"
           "\"entity_seed48_after\":%llu,\"drops\":[]}",
           painting.art, painting_art_width(painting.art),
           painting_art_height(painting.art), facing,
           double_bits(painting.x), double_bits(painting.y),
           double_bits(painting.z), double_bits(box.minX),
           double_bits(box.minY), double_bits(box.minZ),
           double_bits(box.maxX), double_bits(box.maxY),
           double_bits(box.maxZ),
           (unsigned long long)runtime.entity_seed_generator_seed48,
           (unsigned long long)entity_random.seed);
    putchar('\n');
    gm_runtime_destroy(&runtime);
    return 1;
}

static int mixed_order_oracle(
        int x, int y, int z, int first, int second, int third) {
    static const int eids[3] = {8801, 8802, 8803};
    int order[3] = {first, second, third};
    int seen[3] = {0, 0, 0};
    GmRuntime runtime;
    for (int index = 0; index < 3; ++index)
        if (order[index] < 0 || order[index] > 2 || seen[order[index]]++)
            return 0;
    if (!init_runtime(&runtime)
            || !gm_runtime_set_block(&runtime, x, y, z + 1, 1, 0)
            || !gm_runtime_set_block(&runtime, x + 4, y, z + 1, 1, 0)
            || !gm_runtime_set_block(&runtime, x + 8, y, z, 85, 0)
            || !gm_runtime_item_frame_set_full(
                &runtime, 0, eids[0], x, y, z, 2, ic_empty(),
                0, 100, 1.0F, UINT64_C(0x123456789abc),
                0, 0.0, 1, 2)
            || !gm_runtime_painting_set(
                &runtime, 0, eids[1], x + 4, y, z, 2, 0, 100)
            || !gm_runtime_leash_knot_set(
                &runtime, 0, eids[2], x + 8, y, z, 100))
        return 0;
    for (int index = 0; index < 3; ++index)
        if (!gm_runtime_restore_loaded_entity_order(
                &runtime, index, eids[order[index]])) {
            gm_runtime_destroy(&runtime);
            return 0;
        }
    gm_runtime_set_block(&runtime, x, y, z + 1, 0, 0);
    gm_runtime_set_block(&runtime, x + 4, y, z + 1, 0, 0);
    gm_runtime_set_block(&runtime, x + 8, y, z, 0, 0);
    gm_runtime_tick_hanging_entities(&runtime);
    printf("{\"loaded_kinds\":[%d,%d,%d],"
           "\"frame_dead\":%s,\"painting_dead\":%s,"
           "\"knot_dead\":%s,\"drops\":[",
           first, second, third,
           gm_runtime_item_frame_count(&runtime) ? "false" : "true",
           gm_runtime_painting_count(&runtime) ? "false" : "true",
           gm_runtime_leash_knot_count(&runtime) ? "false" : "true");
    int written = 0;
    for (int slot = 0; slot < GM_LIVE_MAX; ++slot) {
        const GmLiveEnt *drop = &runtime.entities.ents[slot];
        if (!drop->active || drop->type != 0) continue;
        printf("%s{\"item\":%d,\"count\":%d}",
               written++ ? "," : "", drop->item, drop->count);
    }
    puts("]}");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int hanging_damage_oracle(
        int x, int y, int z, int entity_type, int source, int creative) {
    GmRuntime runtime;
    int attacker_creative = creative && (source == 1 || source == 2);
    int dead = 0, item_after = 0;
    if (entity_type < 0 || entity_type > 3 || source < 0 || source > 7
            || (creative != 0 && creative != 1)
            || !init_runtime(&runtime))
        return 0;
    if (entity_type <= 1) {
        ICStack displayed = entity_type == 1 ? ic_mk(276, 1, 7) : ic_empty();
        if (!gm_runtime_set_block(&runtime, x, y, z + 1, 1, 0)
                || !gm_runtime_item_frame_set_full(
                    &runtime, 0, 8901, x, y, z, 2, displayed,
                    3, 7, 1.0F, UINT64_C(0x123456789abc),
                    0, 0.0, 1, 2)
                || !gm_runtime_damage_item_frame(
                    &runtime, 8901, attacker_creative, source == 4)) {
            gm_runtime_destroy(&runtime);
            return 0;
        }
        GmRuntimeItemFrame frame;
        if (gm_runtime_item_frame_get(&runtime, 0, &frame))
            item_after = frame.item;
        else
            dead = 1;
    } else if (entity_type == 2) {
        if (!gm_runtime_set_block(&runtime, x, y, z + 1, 1, 0)
                || !gm_runtime_painting_set(
                    &runtime, 0, 8902, x, y, z, 2, 0, 7)
                || !gm_runtime_break_painting(
                    &runtime, 8902, attacker_creative)) {
            gm_runtime_destroy(&runtime);
            return 0;
        }
        dead = gm_runtime_painting_count(&runtime) == 0;
    } else {
        if (!gm_runtime_set_block(&runtime, x, y, z, 85, 0)
                || !gm_runtime_leash_knot_set(
                    &runtime, 0, 8903, x, y, z, 7)
                || !gm_runtime_damage_leash_knot(
                    &runtime, 8903, attacker_creative)) {
            gm_runtime_destroy(&runtime);
            return 0;
        }
        dead = gm_runtime_leash_knot_count(&runtime) == 0;
    }
    printf("{\"dead_after\":%s,\"item_after\":%d,\"drops\":[",
           dead ? "true" : "false", item_after);
    int written = 0;
    for (int slot = 0; slot < GM_LIVE_MAX; ++slot) {
        const GmLiveEnt *drop = &runtime.entities.ents[slot];
        if (!drop->active || drop->type != 0) continue;
        printf("%s{\"item\":%d,\"count\":%d}",
               written++ ? "," : "", drop->item, drop->count);
    }
    puts("]}");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int item_frame_map_oracle(
        int x, int y, int z, int facing, int scale,
        int center_x, int center_z, int tracking,
        int operation, double drop_chance) {
    GmRuntime runtime;
    GmRuntimeItemFrame frame;
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int active;
    if (facing < 2 || facing > 5 || scale < 0 || scale > 4
            || (tracking != 0 && tracking != 1)
            || operation < 0 || operation > 3
            || (drop_chance != 0.0 && drop_chance != 1.0)
            || !init_runtime(&runtime)
            || !gm_runtime_set_block(
                &runtime, x - dx[facing], y, z - dz[facing], 1, 0)
            || !gm_runtime_item_frame_set_full(
                &runtime, 0, 8951, x, y, z, facing,
                ic_mk(358, 1, 30000), 0, 0, (float)drop_chance,
                UINT64_C(0x123456789abc), 0, 0.0, 1, 2)
            || !gm_runtime_item_frame_set_map_state(
                &runtime, 8951, 0, 1, 0,
                center_x, center_z, scale, tracking, 0,
                0, 0, 0, 0, 0)
            || !gm_runtime_item_frame_tracker_tick(&runtime, 8951)) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    if (operation == 1
            && !gm_runtime_damage_item_frame(&runtime, 8951, 0, 0)) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    if (operation == 2
            && !gm_runtime_damage_item_frame(&runtime, 8951, 1, 0)) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    if (operation == 3
            && !gm_runtime_item_frame_interact(&runtime, 8951, 0, 0)) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    active = gm_runtime_item_frame_get(&runtime, 0, &frame);
    printf("{\"dead_after\":%s,\"item_after\":%d,"
           "\"rotation_after\":%d,\"decoration_present\":%s,"
           "\"decoration_type\":%d,\"decoration_x\":%d,"
           "\"decoration_z\":%d,\"decoration_rotation\":%d,"
           "\"drops\":[",
           active ? "false" : "true", active ? frame.item : 0,
           active ? frame.rotation : 0,
           active && frame.map_decoration_present ? "true" : "false",
           active ? frame.map_decoration_type : 0,
           active ? frame.map_decoration_x : 0,
           active ? frame.map_decoration_z : 0,
           active ? frame.map_decoration_rotation : 0);
    int written = 0;
    for (int slot = 0; slot < GM_LIVE_MAX; ++slot) {
        const GmLiveEnt *drop = &runtime.entities.ents[slot];
        if (!drop->active || drop->type != 0
                || (drop->item != 358 && drop->item != 389))
            continue;
        printf("%s{\"item\":%d,\"count\":%d}",
               written++ ? "," : "", drop->item, drop->count);
    }
    puts("]}");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int knot_oracle(
        int x, int y, int z, int attach, int interact, int llama_tick) {
    GmRuntime runtime;
    GmRuntimeLeashKnot knot;
    GmLlamaState llama;
    McAABB box;
    GmAction idle = {0};
    int llama_eid = 8101;
    int active_after;
    if (!init_runtime(&runtime)
            || !gm_runtime_set_block(&runtime, x, y, z, 85, 0)) {
        fprintf(stderr, "FAIL: knot oracle fixture\n");
        return 0;
    }
    if (attach) {
        if (gm_mobs_spawn_llama_exact(
                &runtime.mobs, llama_eid,
                x + 1.5, y, z + 0.5,
                0.0, 0.0, 0.0, 0.0F, 20.0F, 1,
                20.0, 0.175, 0.5, 0, GM_HORSE_TAME,
                0, 0, 1, -1, 0, 0, 1) <= 0
                || !gm_mobs_restore_llama_links(
                    &runtime.mobs, llama_eid, 1, 0,
                    -1, -1, 2.1, 0)
                || !gm_runtime_set_entity_id_cursor(&runtime, 8201)
                || gm_runtime_attach_llamas_to_fence(
                    &runtime, x, y, z) != 1) {
            gm_runtime_destroy(&runtime);
            return 0;
        }
    } else if (!gm_runtime_leash_knot_set(
            &runtime, 0, 8201, x, y, z, 0)) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    if (!gm_runtime_leash_knot_get(&runtime, 0, &knot)
            || !gm_runtime_leash_knot_aabb(&knot, &box)) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    printf("{\"valid_before\":true,"
           "\"x_bits\":\"%016llx\",\"y_bits\":\"%016llx\","
           "\"z_bits\":\"%016llx\","
           "\"min_x_bits\":\"%016llx\","
           "\"min_y_bits\":\"%016llx\","
           "\"min_z_bits\":\"%016llx\","
           "\"max_x_bits\":\"%016llx\","
           "\"max_y_bits\":\"%016llx\","
           "\"max_z_bits\":\"%016llx\",",
           double_bits(knot.x), double_bits(knot.y), double_bits(knot.z),
           double_bits(box.minX), double_bits(box.minY),
           double_bits(box.minZ), double_bits(box.maxX),
           double_bits(box.maxY), double_bits(box.maxZ));
    if (attach) {
        gm_mobs_get_llama_state(&runtime.mobs, llama_eid, &llama);
        printf("\"holder_before\":\"%s\",",
               llama.leashed && llama.leash_holder_kind == 3
                   ? "knot" : "other");
    }
    if (interact)
        gm_runtime_leash_knot_interact(&runtime, knot.eid, 0);
    if (llama_tick) {
        runtime.restored_active_mobs_enabled = 1;
        gm_runtime_tick(&runtime, idle);
    }
    active_after = gm_runtime_leash_knot_get(&runtime, 0, &knot);
    printf("\"dead_after\":%s,", active_after ? "false" : "true");
    if (attach) {
        gm_mobs_get_llama_state(&runtime.mobs, llama_eid, &llama);
        printf("\"llama_leashed_after\":%s,\"holder_after\":\"%s\",",
               llama.leashed ? "true" : "false",
               !llama.leashed ? "none"
                   : llama.leash_holder_kind == 3 ? "knot" : "other");
    }
    fputs("\"drops\":", stdout);
    print_drop_rows(&runtime);
    puts("}");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int living_leash_oracle(
        int x, int y, int z, int class_index, int operation) {
    static const int types[16] = {
        EW_TYPE_PIG, EW_TYPE_SHEEP, EW_TYPE_COW, EW_TYPE_CHICKEN,
        EW_TYPE_SQUID, EW_TYPE_WOLF, EW_TYPE_MOOSHROOM,
        EW_TYPE_SNOWMAN, EW_TYPE_OCELOT, EW_TYPE_IRON_GOLEM,
        EW_TYPE_HORSE, EW_TYPE_DONKEY, EW_TYPE_MULE, EW_TYPE_RABBIT,
        EW_TYPE_POLAR_BEAR, EW_TYPE_LLAMA,
    };
    GmRuntime runtime;
    GmLivingLeashState leash;
    const EwStore *store;
    int type, eid = 9001, slot, handled = 0, can_before;
    double distance = operation == 3 ? 11.0 : 7.0;
    if (class_index < 0 || class_index >= 16
            || operation < 0 || operation > 6
            || !init_runtime(&runtime))
        return 0;
    type = types[class_index];
    if (!gm_runtime_set_player_entity_id(&runtime, 9000)
            || gm_mobs_spawn_exact(
                &runtime.mobs, type, eid,
                x + 0.5, y, z + 0.5, 0.0, 0.0, 0.0,
                0.0F, 1.0F, 1, 0, 0, 0) <= 0) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    slot = gm_mobs_find_slot_by_eid(&runtime.mobs, eid);
    if (slot <= 0) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    if (operation == 6 && type == EW_TYPE_WOLF)
        (void)gm_mobs_set_wolf_angry(&runtime.mobs, eid, 1);
    can_before = gm_mobs_living_can_be_leashed(&runtime.mobs, eid);
    if (operation == 0 || operation == 1 || operation == 6) {
        isr_set_stack(&runtime.player.inv, 0, ic_mk(420, 3, 0));
        handled = gm_runtime_living_leash_interact(&runtime, eid, 0);
        if (operation == 1) {
            if (!gm_runtime_set_block(&runtime, x + 2, y, z, 85, 0)) {
                gm_runtime_destroy(&runtime);
                return 0;
            }
            handled = handled
                && gm_runtime_attach_living_to_fence(
                    &runtime, x + 2, y, z) == 1;
        }
    } else {
        if (operation == 4
                && (type == EW_TYPE_WOLF || type == EW_TYPE_OCELOT))
            runtime.mobs.tameable_sitting[slot] = 1;
        if (operation == 5 && gm_mobs_horse_type(type))
            runtime.mobs.horse_status[slot] |= GM_HORSE_EATING;
        if (!gm_mobs_living_set_leash_knot(
                &runtime.mobs, eid, 9100,
                x + 0.5 + distance, y, z + 0.5)) {
            gm_runtime_destroy(&runtime);
            return 0;
        }
        runtime.mobs.active_entity_seed_generator_seed48 =
            &runtime.entity_seed_generator_seed48;
        runtime.mobs.active_server_uuid_random_seed48 =
            &runtime.server_uuid_random_seed48;
        handled = gm_mobs_living_leash_step(
            &runtime.mobs, eid, 0, 0.0, 0.0, 0.0, 20.0F,
            &runtime.math_random_seed48, &runtime.next_entity_id,
            &runtime.entities);
        runtime.mobs.active_entity_seed_generator_seed48 = NULL;
        runtime.mobs.active_server_uuid_random_seed48 = NULL;
    }
    store = runtime.mobs.current ? &runtime.mobs.b : &runtime.mobs.a;
    if (!gm_mobs_get_living_leash_state(&runtime.mobs, eid, &leash)) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    printf("{\"class_index\":%d,\"can_leash_before\":%s,"
           "\"handled\":%s,\"leashed_after\":%s,"
           "\"holder_after\":\"%s\",\"held_count_after\":%d,"
           "\"motion_x_bits\":\"%016llx\","
           "\"motion_y_bits\":\"%016llx\","
           "\"motion_z_bits\":\"%016llx\","
           "\"eating_after\":%s,\"drops\":[",
           class_index, can_before ? "true" : "false",
           handled ? "true" : "false",
           leash.leashed ? "true" : "false",
           !leash.leashed ? "none"
               : leash.holder_kind == 1 ? "player"
               : "anchor",
           isr_get_stack(&runtime.player.inv, 0).count,
           double_bits(store->vx[slot]), double_bits(store->vy[slot]),
           double_bits(store->vz[slot]),
           gm_mobs_horse_type(type)
                   && (runtime.mobs.horse_status[slot] & GM_HORSE_EATING)
               ? "true" : "false");
    int written = 0;
    for (int item_slot = 0; item_slot < GM_LIVE_MAX; ++item_slot) {
        const GmLiveEnt *drop = &runtime.entities.ents[item_slot];
        if (!drop->active || drop->type != 0 || drop->item != 420)
            continue;
        printf("%s{\"item\":420,\"count\":%d}",
               written++ ? "," : "", drop->count);
    }
    puts("]}");
    gm_runtime_destroy(&runtime);
    return 1;
}

static int item_frame_oracle(
        int x, int y, int z, int facing, int item, int rotation,
        int tick_counter, int remove_support, int hits, int creative,
        int explosion, float drop_chance, uint64_t entity_seed48,
        uint64_t math_seed48, int interactions) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    static const unsigned char item_tag[] = {
        10,0,0,3,0,6,'C','u','s','t','o','m',0,0,0,42,0,
    };
    GmRuntime runtime;
    GmRuntimeItemFrame frame;
    McAABB box;
    ICStack stack = item ? ic_mk(item, 1, 7) : ic_empty();
    int active_after, tag_id = 0, name_id = 0;
    if (!init_runtime(&runtime)
            || facing < 2 || facing > 5
            || !gm_runtime_set_block(
                &runtime, x - dx[facing], y, z - dz[facing], 1, 0))
        return 0;
    if (item) {
        tag_id = gm_runtime_stack_tag_intern(
            &runtime, item_tag, sizeof item_tag);
        name_id = gm_runtime_item_name_intern(&runtime, "Oracle blade");
        stack.repair_cost = 9;
        stack.custom_name = name_id;
        stack.tag_id = tag_id;
        stack.n_enchants = 2;
        stack.enchants[0].id = 16;
        stack.enchants[0].level = 5;
        stack.enchants[1].id = 34;
        stack.enchants[1].level = 3;
    }
    if ((item && (tag_id <= 0 || name_id <= 0))
            || !gm_runtime_item_frame_set_full(
                &runtime, 0, 8001, x, y, z, facing, stack,
                rotation, tick_counter, drop_chance,
                entity_seed48, 0, 0.0, 1, 2)
            || !gm_runtime_item_frame_get(&runtime, 0, &frame)
            || !gm_runtime_item_frame_aabb(&frame, &box)
            || !gm_runtime_set_math_random_seed48(&runtime, math_seed48)) {
        gm_runtime_destroy(&runtime);
        return 0;
    }
    printf("{\"valid_before\":true,"
           "\"x_bits\":\"%016llx\",\"y_bits\":\"%016llx\","
           "\"z_bits\":\"%016llx\","
           "\"min_x_bits\":\"%016llx\","
           "\"min_y_bits\":\"%016llx\","
           "\"min_z_bits\":\"%016llx\","
           "\"max_x_bits\":\"%016llx\","
           "\"max_y_bits\":\"%016llx\","
           "\"max_z_bits\":\"%016llx\",",
           double_bits(frame.x), double_bits(frame.y),
           double_bits(frame.z), double_bits(box.minX),
           double_bits(box.minY), double_bits(box.minZ),
           double_bits(box.maxX), double_bits(box.maxY),
           double_bits(box.maxZ));
    if (remove_support) {
        gm_runtime_set_block(
            &runtime, x - dx[facing], y, z - dz[facing], 0, 0);
        gm_runtime_tick_hanging_entities(&runtime);
    }
    for (int hit = 0; hit < hits; ++hit) {
        if (!gm_runtime_item_frame_get(&runtime, 0, &frame)) break;
        gm_runtime_damage_item_frame(
            &runtime, frame.eid, creative, explosion);
    }
    if (interactions > 0) {
        ICStack held = stack;
        if (isr_is_empty(&held)) {
            held = ic_mk(276, 3, 7);
            tag_id = gm_runtime_stack_tag_intern(
                &runtime, item_tag, sizeof item_tag);
            name_id = gm_runtime_item_name_intern(
                &runtime, "Oracle blade");
            held.repair_cost = 9;
            held.custom_name = name_id;
            held.tag_id = tag_id;
            held.n_enchants = 2;
            held.enchants[0].id = 16;
            held.enchants[0].level = 5;
            held.enchants[1].id = 34;
            held.enchants[1].level = 3;
        } else {
            held.count = 3;
        }
        isr_set_stack(&runtime.player.inv, 0, held);
        for (int interaction = 0; interaction < interactions; ++interaction)
            if (gm_runtime_item_frame_get(&runtime, 0, &frame))
                gm_runtime_item_frame_interact(
                    &runtime, frame.eid, 0, creative);
    }
    active_after = gm_runtime_item_frame_get(&runtime, 0, &frame);
    printf("\"dead_after\":%s,\"item_after\":%d,"
           "\"count_after\":%d,\"meta_after\":%d,"
           "\"rotation_after\":%d,\"tick_counter_after\":%d,"
           "\"entity_seed48_after\":%llu,\"held_count_after\":%d,"
           "\"math_seed48_after\":%llu,\"drops\":",
           active_after ? "false" : "true",
           active_after ? frame.item : 0,
           active_after ? frame.count : 0,
           active_after ? frame.meta : 0,
           active_after ? frame.rotation : 0,
           active_after ? frame.tick_counter : 0,
           active_after ? frame.random_seed48 : UINT64_C(0),
           interactions > 0
               ? isr_get_stack(&runtime.player.inv, 0).count : 0,
           (unsigned long long)runtime.math_random_seed48);
    print_item_frame_drop_rows(&runtime);
    puts("}");
    gm_runtime_destroy(&runtime);
    return 1;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--oracle") == 0) {
        if (argc == 10 && strcmp(argv[2], "painting") == 0)
            return painting_oracle(
                atoi(argv[3]), atoi(argv[4]), atoi(argv[5]),
                atoi(argv[6]), atoi(argv[7]), atoi(argv[8]),
                atoi(argv[9])) ? 0 : 1;
        if (argc == 10 && strcmp(argv[2], "painting_constructor") == 0)
            return painting_constructor_oracle(
                atoi(argv[3]), atoi(argv[4]), atoi(argv[5]),
                atoi(argv[6]), atoi(argv[7]), atoi(argv[8]),
                (uint64_t)strtoull(argv[9], NULL, 10)) ? 0 : 1;
        if (argc == 9 && strcmp(argv[2], "mixed_order") == 0)
            return mixed_order_oracle(
                atoi(argv[3]), atoi(argv[4]), atoi(argv[5]),
                atoi(argv[6]), atoi(argv[7]), atoi(argv[8])) ? 0 : 1;
        if (argc == 9 && strcmp(argv[2], "damage") == 0)
            return hanging_damage_oracle(
                atoi(argv[3]), atoi(argv[4]), atoi(argv[5]),
                atoi(argv[6]), atoi(argv[7]), atoi(argv[8])) ? 0 : 1;
        if (argc == 13 && strcmp(argv[2], "map") == 0)
            return item_frame_map_oracle(
                atoi(argv[3]), atoi(argv[4]), atoi(argv[5]),
                atoi(argv[6]), atoi(argv[7]), atoi(argv[8]),
                atoi(argv[9]), atoi(argv[10]), atoi(argv[11]),
                strtod(argv[12], NULL)) ? 0 : 1;
        if (argc == 9 && strcmp(argv[2], "knot") == 0)
            return knot_oracle(
                atoi(argv[3]), atoi(argv[4]), atoi(argv[5]),
                atoi(argv[6]), atoi(argv[7]), atoi(argv[8])) ? 0 : 1;
        if (argc == 8 && strcmp(argv[2], "living_leash") == 0)
            return living_leash_oracle(
                atoi(argv[3]), atoi(argv[4]), atoi(argv[5]),
                atoi(argv[6]), atoi(argv[7])) ? 0 : 1;
        if (argc == 18 && strcmp(argv[2], "frame") == 0)
            return item_frame_oracle(
                atoi(argv[3]), atoi(argv[4]), atoi(argv[5]),
                atoi(argv[6]), atoi(argv[7]), atoi(argv[8]),
                atoi(argv[9]), atoi(argv[10]), atoi(argv[11]),
                atoi(argv[12]), atoi(argv[13]),
                (float)strtod(argv[14], NULL),
                (uint64_t)strtoull(argv[15], NULL, 10),
                (uint64_t)strtoull(argv[16], NULL, 10),
                atoi(argv[17])) ? 0 : 1;
        fprintf(stderr,
                "usage: %s --oracle painting X Y Z FACING ART COUNTER REMOVE\n"
                "       %s --oracle painting_constructor X Y Z FACING "
                "SUPPORT_W SUPPORT_H GENERATOR_SEED\n"
                "       %s --oracle mixed_order X Y Z FIRST SECOND THIRD\n"
                "       %s --oracle damage X Y Z ENTITY SOURCE CREATIVE\n"
                "       %s --oracle map X Y Z FACING SCALE CENTER_X "
                "CENTER_Z TRACKING OP DROP_CHANCE\n"
                "       %s --oracle knot X Y Z ATTACH INTERACT LLAMA_TICK\n"
                "       %s --oracle living_leash X Y Z CLASS OP\n"
                "       %s --oracle frame X Y Z FACING ITEM ROT COUNTER "
                "REMOVE HITS CREATIVE EXPLOSION CHANCE ENTITY_SEED "
                "MATH_SEED INTERACTIONS\n",
                argv[0], argv[0], argv[0], argv[0], argv[0], argv[0],
                argv[0], argv[0]);
        return 2;
    }
    if (!painting_geometry_and_lifecycle()) return 1;
    if (!painting_footprint_and_placement()) return 1;
    if (!leash_knot_transfer_and_break()) return 1;
    if (!leash_knot_periodic_support()) return 1;
    if (!pending_leash_reload_boundary()) return 1;
    if (!item_frame_lifecycle()) return 1;
    if (!mixed_hanging_loaded_order()) return 1;
    if (!hanging_checkpoint()) return 1;
    if (!hanging_cold_capacity()) return 1;
    puts("hanging runtime: PASS");
    return 0;
}
