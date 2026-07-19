#include <stdio.h>
#include <stdint.h>
#include "game/block_registry.h"

static int fails;
#define CHECK(C, M) do { if (!(C)) { fprintf(stderr, "FAIL: %s\n", (M)); ++fails; } } while (0)

static void test_all_generated_keys(void) {
    static const short ids[90] = {
        0,1,9,2,3,7,13,12,24,179,79,11,10,8,111,110,78,172,159,3,3,
        1,1,1,16,15,14,73,56,21,82,17,17,17,18,18,18,17,17,31,31,
        32,39,40,83,4,48,52,216,54,37,
        38,38,38,38,38,38,38,38,38,
        175,175,175,175,175,175,175,
        86,86,86,86,106,106,106,106,
        129,97,162,161,99,100,81,162,161,44,17,18,103,127,49
    };
    static const unsigned char metas[90] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,1,
        1,3,5,0,0,0,0,0,0,0,0,2,1,0,2,1,4,8,1,2,
        0,0,0,0,0,0,0,0,2,0,
        0,1,2,3,4,5,6,7,8,
        0,1,2,3,4,5,10,
        0,1,2,3,8,2,1,4,
        0,0,1,1,0,0,0,0,0,1,3,3,0,0,0
    };
    for (int key = 0; key < 90; ++key) {
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
    CHECK(gm_state_to_model_key(gm_pack_state(58, 0)) == 223, "crafting table reverse map");
    for (int m = 0; m <= 5; ++m)
        CHECK(gm_state_to_model_key(gm_pack_state(5, m)) == 224, "planks (any species) -> CBX_PLANKS");
    CHECK(gm_state_to_model_key(gm_pack_state(31, 1)) == 39, "tallgrass meta1 -> PB_TALLGRASS");
    CHECK(gm_state_to_model_key(gm_pack_state(31, 2)) == 40, "tallgrass meta2 -> PB_FERN");
    CHECK(gm_state_to_model_key(gm_pack_state(37, 0)) == 50, "dandelion reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(38, 0)) == 51, "poppy reverse map");
    CHECK(gm_state_to_model_key(gm_pack_state(38, 8)) == 59, "oxeye daisy reverse map");
    {
        uint16_t st = 0;
        CHECK(gm_model_key_to_state(223, 0, &st) == GM_MAP_EXACT, "craft table key supported");
        CHECK(gm_state_id(st) == 58 && gm_state_meta(st) == 0, "craft table key -> id 58");
        CHECK(gm_model_key_to_state(59, 0, &st) == GM_MAP_EXACT, "oxeye model key supported");
        CHECK(gm_state_id(st) == 38 && gm_state_meta(st) == 8, "oxeye key 59 -> 38:8");
        CHECK(gm_model_key_to_state(224, 3, &st) == GM_MAP_EXACT, "planks key supported");
        CHECK(gm_state_id(st) == 5 && gm_state_meta(st) == 3, "planks key 224 -> id 5, meta kept");
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
