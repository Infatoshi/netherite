#include <stdio.h>
#include <stdint.h>
#include "game/block_registry.h"

static int fails;
#define CHECK(C, M) do { if (!(C)) { fprintf(stderr, "FAIL: %s\n", (M)); ++fails; } } while (0)

static void test_all_generated_keys(void) {
    static const short ids[100] = {
        0,1,9,2,3,7,13,12,24,179,79,11,10,8,111,110,78,172,159,3,3,
        1,1,1,16,15,14,73,56,21,82,17,17,17,18,18,18,17,17,31,31,
        32,39,40,83,4,48,52,216,54,37,
        38,38,38,38,38,38,38,38,38,
        175,175,175,175,175,175,175,
        86,86,86,86,106,106,106,106,
        129,97,162,161,99,100,81,162,161,44,17,18,103,127,49,
        24,24,128,128,128,128,159,159,70,46
    };
    static const unsigned char metas[100] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,1,
        1,3,5,0,0,0,0,0,0,0,0,2,1,0,2,1,4,8,1,2,
        0,0,0,0,0,0,0,0,2,0,
        0,1,2,3,4,5,6,7,8,
        0,1,2,3,4,5,10,
        0,1,2,3,8,2,1,4,
        0,0,1,1,0,0,0,0,0,1,3,3,0,0,0,
        2,1,0,1,2,3,1,11,0,0
    };
    for (int key = 0; key < 100; ++key) {
        uint16_t state = 0xffff;
        int q = gm_model_key_to_state(key, 0, &state);
        char msg[96];
        snprintf(msg, sizeof msg, "generated key %d supported", key);
        CHECK(q != GM_MAP_UNSUPPORTED, msg);
        snprintf(msg, sizeof msg, "generated key %d vanilla id", key);
        CHECK(gm_state_id(state) == ids[key], msg);
        snprintf(msg, sizeof msg, "generated key %d vanilla meta", key);
        CHECK(gm_state_meta(state) == metas[key], msg);
    }
}

static void test_ranges_and_loss(void) {
    for (int meta = 0; meta < 16; ++meta) {
        uint16_t state = 0;
        int q = gm_model_key_to_state(120 + meta, 0, &state);
        CHECK(q == GM_MAP_EXACT, "stained clay mapping exact");
        CHECK(gm_state_id(state) == 159 && gm_state_meta(state) == meta,
              "stained clay preserves color metadata");
    }
    {
        static const int lossy[] = {2,7,8,12,13,48,79,80,88};
        for (unsigned i = 0; i < sizeof lossy / sizeof lossy[0]; ++i) {
            uint16_t state = 0;
            CHECK(gm_model_key_to_state(lossy[i], 0, &state) == GM_MAP_LOSSY,
                  "known lossy producer is labeled lossy");
        }
    }
    {
        uint16_t state = 123;
        CHECK(gm_model_key_to_state(204, 0, &state) == GM_MAP_UNSUPPORTED,
              "unknown model key rejected");
    }
}

static void test_collision_barrier(void) {
    uint16_t plant = 0;
    CHECK(gm_model_key_to_state(61, 0, &plant) == GM_MAP_EXACT,
          "PB 61 is a known double-plant key");
    CHECK(gm_state_id(plant) == 175 && gm_state_meta(plant) == 1,
          "PB 61 is not interpreted as furnace");

    static const int vanilla_ids[] = {59, 60, 61, 64};
    static const int vanilla_meta[] = {4, 7, 3, 4};
    for (int i = 0; i < 4; ++i) {
        uint16_t state = gm_pack_state(vanilla_ids[i], vanilla_meta[i]);
        int key = gm_state_to_model_key(state);
        CHECK(key == GM_MODEL_FALLBACK, "unsupported player block gets explicit model fallback");
        CHECK(key != vanilla_ids[i], "vanilla id never leaks into PB model namespace");
        CHECK(gm_state_id(state) == vanilla_ids[i] && gm_state_meta(state) == vanilla_meta[i],
              "canonical state remains exact despite visual fallback");
    }

    CHECK(gm_state_to_model_key(gm_pack_state(9, 0)) == 2, "water reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(17, 0)) == 31, "oak log reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(49, 0)) == 89, "obsidian reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(24, 1)) == 91, "chiseled sandstone reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(24, 2)) == 90, "smooth sandstone reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(128, 3)) == 95, "north sandstone stairs reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(159, 1)) == 96, "orange clay reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(159, 11)) == 97, "blue clay reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(70, 0)) == 98, "stone pressure plate reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(46, 0)) == 236, "TNT reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(58, 0)) == 223, "crafting table reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(66, 0)) == 235, "rail reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(102, 0)) == 253,
          "glass pane reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(67, 6)) == 254,
          "cobblestone stairs reverse map");
    for (int meta = 0; meta < 16; ++meta) {
        uint16_t st = 0;
        CHECK(gm_state_to_model_key(gm_pack_state(96, meta)) ==
                  GM_MODEL_TRAPDOOR,
              "trapdoor reverse map");
        CHECK(gm_model_key_to_state(GM_MODEL_TRAPDOOR, meta, &st) ==
                  GM_MAP_EXACT,
              "trapdoor model key supported");
        CHECK(gm_state_id(st) == 96 && gm_state_meta(st) == meta,
              "trapdoor model key preserves metadata");
    }
    CHECK(gm_state_to_model_key(gm_pack_state(65, 2)) == GM_MODEL_LADDER,
          "ladder reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(98, 0)) == GM_MODEL_STONEBRICK,
          "stonebrick reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(112, 0)) == 228, "nether brick reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(165, 0)) == 229, "slime reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(30, 0)) == 230, "web reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(174, 0)) == 231, "packed ice reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(113, 0)) == 232, "nether fence reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(139, 0)) == 233, "cobble wall reverse map");
    {
        int ep = gm_state_to_model_key(gm_pack_state(119, 0));
        CHECK(ep != GM_MODEL_FALLBACK, "active end portal 119 is not model fallback");
        CHECK(ep != 1 && ep != 4095, "active end portal 119 is not stone/fallback key");
        CHECK(ep == 234, "active end portal 119 -> CBX_END_PORTAL 234");
        uint16_t st = 0;
        CHECK(gm_model_key_to_state(234, 0, &st) == GM_MAP_EXACT,
              "end portal key 234 supported");
        CHECK(gm_state_id(st) == 119 && gm_state_meta(st) == 0,
              "end portal key 234 -> vanilla id 119");
    }
    CHECK(gm_state_to_model_key(gm_pack_state(1, 2)) == 225, "polished granite reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(1, 4)) == 226, "polished diorite reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(1, 6)) == 227, "polished andesite reverse map");
    for (int m = 0; m <= 5; ++m)
        CHECK(gm_state_to_model_key(gm_pack_state(5, m)) == 224, "planks (any species) -> CBX_PLANKS");
    CHECK(gm_state_to_model_key(gm_pack_state(31, 1)) == 39, "tallgrass meta1 -> PB_TALLGRASS");
    CHECK(gm_state_to_model_key(gm_pack_state(31, 2)) == 40, "tallgrass meta2 -> PB_FERN");
    CHECK(gm_state_to_model_key(gm_pack_state(37, 0)) == 50, "dandelion reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(38, 0)) == 51, "poppy reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(38, 8)) == 59, "oxeye daisy reverse map");
    for (int meta = 0; meta < 16; ++meta) {
        uint16_t st = 0;
        int key = gm_state_to_model_key(gm_pack_state(44, meta));
        int want = (meta & 8 ? GM_MODEL_STONE_SLAB_TOP_BASE
                             : GM_MODEL_STONE_SLAB_BOTTOM_BASE) + (meta & 7);
        CHECK(key == want, "stone slab model key preserves variant and half");
        CHECK(gm_model_key_to_state(key, 0, &st) == GM_MAP_EXACT,
              "stone slab model key maps exactly");
        CHECK(gm_state_id(st) == 44 && gm_state_meta(st) == meta,
              "stone slab model key round-trips canonical state");
    }
    CHECK(gm_state_to_model_key(gm_pack_state(199, 0)) == 273,
          "chorus plant reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(200, 2)) == 274,
          "living chorus flower reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(200, 5)) == 275,
          "dead chorus flower reverse map");
    {
        uint16_t st = 0;
        CHECK(gm_model_key_to_state(223, 0, &st) == GM_MAP_EXACT, "craft table key supported");
        CHECK(gm_state_id(st) == 58 && gm_state_meta(st) == 0, "craft table key -> id 58");
        CHECK(gm_model_key_to_state(59, 0, &st) == GM_MAP_EXACT, "oxeye model key supported");
        CHECK(gm_state_id(st) == 38 && gm_state_meta(st) == 8, "oxeye key 59 -> 38:8");
        CHECK(gm_model_key_to_state(224, 3, &st) == GM_MAP_EXACT, "planks key supported");
        CHECK(gm_state_id(st) == 5 && gm_state_meta(st) == 3, "planks key 224 -> id 5, meta kept");
        CHECK(gm_model_key_to_state(225, 0, &st) == GM_MAP_EXACT,
              "polished granite key supported");
        CHECK(gm_state_id(st) == 1 && gm_state_meta(st) == 2,
              "polished granite key -> id 1 meta 2");
        CHECK(gm_model_key_to_state(226, 0, &st) == GM_MAP_EXACT,
              "polished diorite key supported");
        CHECK(gm_state_id(st) == 1 && gm_state_meta(st) == 4,
              "polished diorite key -> id 1 meta 4");
        CHECK(gm_model_key_to_state(227, 0, &st) == GM_MAP_EXACT,
              "polished andesite key supported");
        CHECK(gm_state_id(st) == 1 && gm_state_meta(st) == 6,
              "polished andesite key -> id 1 meta 6");
        CHECK(gm_model_key_to_state(235, 1, &st) == GM_MAP_EXACT,
              "rail key supported");
        CHECK(gm_state_id(st) == 66 && gm_state_meta(st) == 1,
              "rail key -> id 66, meta kept");
        CHECK(gm_model_key_to_state(253, 0, &st) == GM_MAP_EXACT,
              "glass pane key supported");
        CHECK(gm_state_id(st) == 102 && gm_state_meta(st) == 0,
              "glass pane key -> id 102");
        CHECK(gm_model_key_to_state(254, 6, &st) == GM_MAP_EXACT,
              "cobblestone stairs key supported");
        CHECK(gm_state_id(st) == 67 && gm_state_meta(st) == 6,
              "cobblestone stairs key -> id 67, meta kept");
        CHECK(gm_model_key_to_state(GM_MODEL_LADDER, 2, &st) == GM_MAP_EXACT,
              "ladder key supported");
        CHECK(gm_state_id(st) == 65 && gm_state_meta(st) == 2,
              "ladder key -> id 65, meta kept");
        CHECK(gm_model_key_to_state(GM_MODEL_STONEBRICK, 0, &st) == GM_MAP_EXACT,
              "stonebrick key supported");
        CHECK(gm_state_id(st) == 98 && gm_state_meta(st) == 0,
              "stonebrick key -> id 98");
        CHECK(gm_model_key_to_state(273, 0, &st) == GM_MAP_EXACT
              && gm_state_id(st) == 199 && gm_state_meta(st) == 0,
              "chorus plant key -> id 199");
        CHECK(gm_model_key_to_state(275, 0, &st) == GM_MAP_EXACT
              && gm_state_id(st) == 200 && gm_state_meta(st) == 5,
              "dead chorus flower key -> id 200 meta 5");
    }
}

int main(void) {
    test_all_generated_keys();
    test_ranges_and_loss();
    test_collision_barrier();
    if (fails) {
        fprintf(stderr, "%d block-registry check(s) failed\n", fails);
        return 1;
    }
    puts("block_registry: PASS");
    return 0;
}
