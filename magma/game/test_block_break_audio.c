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
    case GM_SOUND_BLOCK_WOOD_HIT: return "minecraft:block.wood.hit";
    case GM_SOUND_BLOCK_GRAVEL_HIT: return "minecraft:block.gravel.hit";
    case GM_SOUND_BLOCK_GRASS_HIT: return "minecraft:block.grass.hit";
    case GM_SOUND_BLOCK_STONE_HIT: return "minecraft:block.stone.hit";
    case GM_SOUND_BLOCK_METAL_HIT: return "minecraft:block.metal.hit";
    case GM_SOUND_BLOCK_GLASS_HIT: return "minecraft:block.glass.hit";
    case GM_SOUND_BLOCK_CLOTH_HIT: return "minecraft:block.cloth.hit";
    case GM_SOUND_BLOCK_SAND_HIT: return "minecraft:block.sand.hit";
    case GM_SOUND_BLOCK_SNOW_HIT: return "minecraft:block.snow.hit";
    case GM_SOUND_BLOCK_LADDER_HIT: return "minecraft:block.ladder.hit";
    case GM_SOUND_BLOCK_ANVIL_HIT: return "minecraft:block.anvil.hit";
    case GM_SOUND_BLOCK_SLIME_HIT: return "minecraft:block.slime.hit";
    case GM_SOUND_BLOCK_WOOD_FALL: return "minecraft:block.wood.fall";
    case GM_SOUND_BLOCK_GRAVEL_FALL: return "minecraft:block.gravel.fall";
    case GM_SOUND_BLOCK_GRASS_FALL: return "minecraft:block.grass.fall";
    case GM_SOUND_BLOCK_STONE_FALL: return "minecraft:block.stone.fall";
    case GM_SOUND_BLOCK_METAL_FALL: return "minecraft:block.metal.fall";
    case GM_SOUND_BLOCK_GLASS_FALL: return "minecraft:block.glass.fall";
    case GM_SOUND_BLOCK_CLOTH_FALL: return "minecraft:block.cloth.fall";
    case GM_SOUND_BLOCK_SAND_FALL: return "minecraft:block.sand.fall";
    case GM_SOUND_BLOCK_SNOW_FALL: return "minecraft:block.snow.fall";
    case GM_SOUND_BLOCK_LADDER_FALL: return "minecraft:block.ladder.fall";
    case GM_SOUND_BLOCK_ANVIL_FALL: return "minecraft:block.anvil.fall";
    case GM_SOUND_BLOCK_SLIME_FALL: return "minecraft:block.slime.fall";
    case GM_SOUND_BLOCK_WOOD_STEP: return "minecraft:block.wood.step";
    case GM_SOUND_BLOCK_GRAVEL_STEP: return "minecraft:block.gravel.step";
    case GM_SOUND_BLOCK_GRASS_STEP: return "minecraft:block.grass.step";
    case GM_SOUND_BLOCK_STONE_STEP: return "minecraft:block.stone.step";
    case GM_SOUND_BLOCK_METAL_STEP: return "minecraft:block.metal.step";
    case GM_SOUND_BLOCK_GLASS_STEP: return "minecraft:block.glass.step";
    case GM_SOUND_BLOCK_CLOTH_STEP: return "minecraft:block.cloth.step";
    case GM_SOUND_BLOCK_SAND_STEP: return "minecraft:block.sand.step";
    case GM_SOUND_BLOCK_SNOW_STEP: return "minecraft:block.snow.step";
    case GM_SOUND_BLOCK_LADDER_STEP: return "minecraft:block.ladder.step";
    case GM_SOUND_BLOCK_ANVIL_STEP: return "minecraft:block.anvil.step";
    case GM_SOUND_BLOCK_SLIME_STEP: return "minecraft:block.slime.step";
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
        int hit_sound, meta_hit_sound;
        int fall_sound, meta_fall_sound;
        int step_sound, meta_step_sound;
        float volume, pitch, meta_volume, meta_pitch;
        float place_volume, place_pitch, meta_place_volume, meta_place_pitch;
        float hit_volume, hit_pitch, meta_hit_volume, meta_hit_pitch;
        float fall_volume, fall_pitch, meta_fall_volume, meta_fall_pitch;
        float step_volume, step_pitch, meta_step_volume, meta_step_pitch;
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
        CHECK(gm_runtime_block_hit_sound(
                  id, &hit_sound, &hit_volume, &hit_pitch)
              && gm_runtime_block_hit_sound(
                  id | (15 << 12), &meta_hit_sound,
                  &meta_hit_volume, &meta_hit_pitch)
              && meta_hit_sound == hit_sound
              && float_bits(meta_hit_volume) == float_bits(hit_volume)
              && float_bits(meta_hit_pitch) == float_bits(hit_pitch),
              "legacy metadata does not alter a 1.11.2 hit sound type");
        CHECK(gm_runtime_block_fall_sound(
                  id, &fall_sound, &fall_volume, &fall_pitch)
              && gm_runtime_block_fall_sound(
                  id | (15 << 12), &meta_fall_sound,
                  &meta_fall_volume, &meta_fall_pitch)
              && meta_fall_sound == fall_sound
              && float_bits(meta_fall_volume) == float_bits(fall_volume)
              && float_bits(meta_fall_pitch) == float_bits(fall_pitch),
              "legacy metadata does not alter a 1.11.2 fall sound type");
        CHECK(gm_runtime_block_step_sound(
                  id, &step_sound, &step_volume, &step_pitch)
              && gm_runtime_block_step_sound(
                  id | (15 << 12), &meta_step_sound,
                  &meta_step_volume, &meta_step_pitch)
              && meta_step_sound == step_sound
              && float_bits(meta_step_volume) == float_bits(step_volume)
              && float_bits(meta_step_pitch) == float_bits(step_pitch),
              "legacy metadata does not alter a 1.11.2 step sound type");
        printf("B %d %s %08x %08x\n", id, sound_name(sound),
               float_bits(volume), float_bits(pitch));
        printf("P %d %s %08x %08x\n", id, sound_name(place_sound),
               float_bits(place_volume), float_bits(place_pitch));
        printf("H %d %s %08x %08x\n", id, sound_name(hit_sound),
               float_bits(hit_volume), float_bits(hit_pitch));
        printf("F %d %s %08x %08x\n", id, sound_name(fall_sound),
               float_bits(fall_volume), float_bits(fall_pitch));
        printf("S %d %s %08x %08x\n", id, sound_name(step_sound),
               float_bits(step_volume), float_bits(step_pitch));
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
    fputs("block break/place/hit/fall/step audio runtime fixture: PASS\n", stderr);
    return 0;
}
