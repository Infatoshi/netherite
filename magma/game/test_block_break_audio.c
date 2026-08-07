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
    case GM_SOUND_BLOCK_WOOD_PLACE: return "minecraft:block.wood.place";
    case GM_SOUND_BLOCK_GRAVEL_PLACE: return "minecraft:block.gravel.place";
    case GM_SOUND_BLOCK_GRASS_PLACE: return "minecraft:block.grass.place";
    case GM_SOUND_BLOCK_STONE_PLACE: return "minecraft:block.stone.place";
    case GM_SOUND_BLOCK_METAL_PLACE: return "minecraft:block.metal.place";
    case GM_SOUND_BLOCK_GLASS_PLACE: return "minecraft:block.glass.place";
    case GM_SOUND_BLOCK_CLOTH_PLACE: return "minecraft:block.cloth.place";
    case GM_SOUND_BLOCK_SAND_PLACE: return "minecraft:block.sand.place";
    case GM_SOUND_BLOCK_SNOW_PLACE: return "minecraft:block.snow.place";
    case GM_SOUND_BLOCK_LADDER_PLACE: return "minecraft:block.ladder.place";
    case GM_SOUND_BLOCK_ANVIL_PLACE: return "minecraft:block.anvil.place";
    case GM_SOUND_BLOCK_SLIME_PLACE: return "minecraft:block.slime.place";
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
        int sound, meta_sound, place_sound, meta_place_sound;
        float volume, pitch, meta_volume, meta_pitch;
        float place_volume, place_pitch, meta_place_volume, meta_place_pitch;
        if (!gm_runtime_block_break_sound(id, &sound, &volume, &pitch))
            continue;
        CHECK(gm_runtime_block_break_sound(
                  id | (15 << 12), &meta_sound, &meta_volume, &meta_pitch)
              && meta_sound == sound
              && float_bits(meta_volume) == float_bits(volume)
              && float_bits(meta_pitch) == float_bits(pitch),
              "legacy metadata does not alter a 1.11.2 block sound type");
        CHECK(gm_runtime_block_place_sound(
                  id, &place_sound, &place_volume, &place_pitch)
              && gm_runtime_block_place_sound(
                  id | (15 << 12), &meta_place_sound,
                  &meta_place_volume, &meta_place_pitch)
              && meta_place_sound == place_sound
              && float_bits(meta_place_volume) == float_bits(place_volume)
              && float_bits(meta_place_pitch) == float_bits(place_pitch),
              "legacy metadata does not alter a 1.11.2 placement sound type");
        printf("B %d %s %08x %08x\n", id, sound_name(sound),
               float_bits(volume), float_bits(pitch));
        printf("P %d %s %08x %08x\n", id, sound_name(place_sound),
               float_bits(place_volume), float_bits(place_pitch));
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
        const int place_sounds[] = {
            GM_SOUND_BLOCK_STONE_PLACE, GM_SOUND_BLOCK_GRASS_PLACE,
            GM_SOUND_BLOCK_METAL_PLACE, GM_SOUND_BLOCK_ANVIL_PLACE,
            GM_SOUND_BLOCK_SLIME_PLACE
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
        for (int i = 0; i < 5; ++i)
            CHECK(gm_runtime_block_place_audio_fixture(
                      &runtime, 30 + i, 65, 21, states[i]),
                  "successful placement resolves a represented sound");
        CHECK(!gm_runtime_block_place_audio_fixture(
                  &runtime, 0, 65, 0, 235),
              "unregistered placement emits no fabricated sound");
        CHECK(gm_runtime_world_event_count(&runtime) == 5
              && gm_runtime_sound_event_count(&runtime) == 10,
              "placement adds sound without fabricating a world event");
        for (int i = 0; i < 5; ++i) {
            CHECK(gm_runtime_sound_event_get(&runtime, 5 + i, &event)
                  && event.sound == place_sounds[i]
                  && event.category == GM_SOUND_CATEGORY_BLOCKS
                  && event.x == 30.5 + (double)i
                  && event.y == 65.5 && event.z == 21.5
                  && event.delay_ticks == 0,
                  "place event keeps exact family, category, center, and delay");
        }
        gm_runtime_destroy(&runtime);
    }
    fputs("block break/place audio runtime fixture: PASS\n", stderr);
    return 0;
}
