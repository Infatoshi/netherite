#include "game/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s\n", message); \
        return 1; \
    } \
} while (0)

static const char *sound_name(int sound) {
    switch (sound) {
    case GM_SOUND_BLOCK_WOOD_BREAK: return "minecraft:block.wood.break";
    case GM_SOUND_BLOCK_GRAVEL_BREAK: return "minecraft:block.gravel.break";
    case GM_SOUND_BLOCK_GRASS_BREAK: return "minecraft:block.grass.break";
    case GM_SOUND_BLOCK_STONE_BREAK: return "minecraft:block.stone.break";
    case GM_SOUND_BLOCK_METAL_BREAK: return "minecraft:block.metal.break";
    case GM_SOUND_BLOCK_GLASS_BREAK: return "minecraft:block.glass.break";
    case GM_SOUND_BLOCK_CLOTH_BREAK: return "minecraft:block.cloth.break";
    case GM_SOUND_BLOCK_SAND_BREAK: return "minecraft:block.sand.break";
    case GM_SOUND_BLOCK_SNOW_BREAK: return "minecraft:block.snow.break";
    case GM_SOUND_BLOCK_LADDER_BREAK: return "minecraft:block.ladder.break";
    case GM_SOUND_BLOCK_ANVIL_BREAK: return "minecraft:block.anvil.break";
    case GM_SOUND_BLOCK_SLIME_BREAK: return "minecraft:block.slime.break";
    default: return "";
    }
}

static unsigned float_bits(float value) {
    union { float f; uint32_t u; } bits;
    bits.f = value;
    return bits.u;
}

int main(void) {
    int rows = 0;
    for (int id = 0; id <= 255; ++id) {
        int sound, meta_sound;
        float volume, pitch, meta_volume, meta_pitch;
        if (!gm_runtime_block_break_sound(id, &sound, &volume, &pitch))
            continue;
        CHECK(gm_runtime_block_break_sound(
                  id | (15 << 12), &meta_sound, &meta_volume, &meta_pitch)
              && meta_sound == sound
              && float_bits(meta_volume) == float_bits(volume)
              && float_bits(meta_pitch) == float_bits(pitch),
              "legacy metadata does not alter a 1.11.2 block sound type");
        printf("B %d %s %08x %08x\n", id, sound_name(sound),
               float_bits(volume), float_bits(pitch));
        ++rows;
    }
    CHECK(rows == 235, "all registered non-air block ids are represented");

    {
        GmConfig config;
        GmRuntime runtime;
        GmRuntimeSoundEvent event;
        char err[256];
        const int states[] = {1, 2, 41, 145, 165};
        const int sounds[] = {
            GM_SOUND_BLOCK_STONE_BREAK, GM_SOUND_BLOCK_GRASS_BREAK,
            GM_SOUND_BLOCK_METAL_BREAK, GM_SOUND_BLOCK_ANVIL_BREAK,
            GM_SOUND_BLOCK_SLIME_BREAK
        };
        gm_config_defaults(&config);
        config.world = GM_WORLD_SUPERFLAT;
        config.view_distance = 1;
        config.mobs = 0;
        config.weather = 0;
        CHECK(gm_runtime_init(&runtime, &config, err, sizeof err),
              "initialize block-break sound fixture");
        for (int i = 0; i < 5; ++i)
            CHECK(gm_runtime_block_break_audio_fixture(
                      &runtime, 10 + i, 64, 20, states[i]),
                  "world event 2001 resolves a represented sound");
        CHECK(!gm_runtime_block_break_audio_fixture(
                  &runtime, 0, 64, 0, 235),
              "unregistered block id emits no fabricated sound");
        CHECK(gm_runtime_world_event_count(&runtime) == 5
              && gm_runtime_sound_event_count(&runtime) == 5,
              "world and sound rings retain the same five break events");
        for (int i = 0; i < 5; ++i) {
            CHECK(gm_runtime_sound_event_get(&runtime, i, &event)
                  && event.sound == sounds[i]
                  && event.category == GM_SOUND_CATEGORY_BLOCKS
                  && event.x == 10.5 + (double)i
                  && event.y == 64.5 && event.z == 20.5
                  && event.delay_ticks == 0,
                  "break event keeps exact family, category, center, and delay");
        }
        gm_runtime_destroy(&runtime);
    }
    fputs("block-break audio runtime fixture: PASS\n", stderr);
    return 0;
}
