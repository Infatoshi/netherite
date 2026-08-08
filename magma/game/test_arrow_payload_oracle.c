#include "game/runtime.h"
#include "tile_entity_brewing.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    PtMobEffect effect;
    int ambient;
    int show_particles;
} EffectRow;

static int effect_color(int id) {
    static const int colors[28] = {
        0, 8171462, 5926017, 14270531, 4866583, 9643043, 16262179,
        4393481, 2293580, 5578058, 13458603, 10044730, 14981690,
        3035801, 8356754, 2039587, 2039713, 5797459, 4738376,
        5149489, 3484199, 16284963, 2445989, 16262179, 9740385,
        13565951, 3381504, 12624973
    };
    return id >= 0 && id < 28 ? colors[id] : 0;
}

static int potion_effect(
        int type, int *id, int *amplifier, int *duration) {
    *id = 0; *amplifier = 0; *duration = 0;
    switch (type) {
    case TB_PT_NIGHT_VISION:*id=16;*duration=3600;break;
    case TB_PT_LONG_NIGHT_VISION:*id=16;*duration=9600;break;
    case TB_PT_INVISIBILITY:*id=14;*duration=3600;break;
    case TB_PT_LONG_INVISIBILITY:*id=14;*duration=9600;break;
    case TB_PT_LEAPING:*id=8;*duration=3600;break;
    case TB_PT_LONG_LEAPING:*id=8;*duration=9600;break;
    case TB_PT_STRONG_LEAPING:*id=8;*duration=1800;*amplifier=1;break;
    case TB_PT_FIRE_RESISTANCE:*id=12;*duration=3600;break;
    case TB_PT_LONG_FIRE_RESISTANCE:*id=12;*duration=9600;break;
    case TB_PT_SWIFTNESS:*id=1;*duration=3600;break;
    case TB_PT_LONG_SWIFTNESS:*id=1;*duration=9600;break;
    case TB_PT_STRONG_SWIFTNESS:*id=1;*duration=1800;*amplifier=1;break;
    case TB_PT_SLOWNESS:*id=2;*duration=1800;break;
    case TB_PT_LONG_SLOWNESS:*id=2;*duration=4800;break;
    case TB_PT_WATER_BREATHING:*id=13;*duration=3600;break;
    case TB_PT_LONG_WATER_BREATHING:*id=13;*duration=9600;break;
    case TB_PT_HEALING:*id=6;*duration=1;break;
    case TB_PT_STRONG_HEALING:*id=6;*duration=1;*amplifier=1;break;
    case TB_PT_HARMING:*id=7;*duration=1;break;
    case TB_PT_STRONG_HARMING:*id=7;*duration=1;*amplifier=1;break;
    case TB_PT_POISON:*id=19;*duration=900;break;
    case TB_PT_LONG_POISON:*id=19;*duration=1800;break;
    case TB_PT_STRONG_POISON:*id=19;*duration=432;*amplifier=1;break;
    case TB_PT_REGENERATION:*id=10;*duration=900;break;
    case TB_PT_LONG_REGENERATION:*id=10;*duration=1800;break;
    case TB_PT_STRONG_REGENERATION:*id=10;*duration=450;*amplifier=1;break;
    case TB_PT_STRENGTH:*id=5;*duration=3600;break;
    case TB_PT_LONG_STRENGTH:*id=5;*duration=9600;break;
    case TB_PT_STRONG_STRENGTH:*id=5;*duration=1800;*amplifier=1;break;
    case TB_PT_WEAKNESS:*id=18;*duration=1800;break;
    case TB_PT_LONG_WEAKNESS:*id=18;*duration=4800;break;
    case TB_PT_LUCK:*id=26;*duration=6000;break;
    default:return 0;
    }
    return 1;
}

static int automatic_color(
        int potion_type, int custom_id, int custom_amplifier,
        int custom_flags) {
    float red = 0.0F, green = 0.0F, blue = 0.0F;
    int total = 0, id, amplifier, duration;
    if (potion_effect(potion_type, &id, &amplifier, &duration)) {
        int color = effect_color(id), scale = amplifier + 1;
        red += (float)(scale * (color >> 16 & 255)) / 255.0F;
        green += (float)(scale * (color >> 8 & 255)) / 255.0F;
        blue += (float)(scale * (color & 255)) / 255.0F;
        total += scale;
    }
    if (custom_id > 0 && (custom_flags & 2)) {
        int color = effect_color(custom_id), scale = custom_amplifier + 1;
        red += (float)(scale * (color >> 16 & 255)) / 255.0F;
        green += (float)(scale * (color >> 8 & 255)) / 255.0F;
        blue += (float)(scale * (color & 255)) / 255.0F;
        total += scale;
    }
    if (total == 0)
        return potion_type == TB_PT_EMPTY && custom_id == 0
            ? 3694022 : 0;
    return ((int)(red / (float)total * 255.0F) << 16)
        | ((int)(green / (float)total * 255.0F) << 8)
        | (int)(blue / (float)total * 255.0F);
}

static int compare_effect_rows(const void *left, const void *right) {
    const EffectRow *a = left, *b = right;
    return a->effect.id - b->effect.id;
}

int main(int argc, char **argv) {
    if (argc != 11) return 2;
    int kind = atoi(argv[1]);
    int potion_type = atoi(argv[2]);
    int spectral_duration = atoi(argv[3]);
    int custom_id = atoi(argv[4]);
    int custom_amplifier = atoi(argv[5]);
    int custom_duration = atoi(argv[6]);
    int custom_flags = atoi(argv[7]);
    int fixed_color = atoi(argv[8]);
    int target_type = atoi(argv[9]);
    int ground_ticks = atoi(argv[10]);
    if ((kind != GM_ARROW_TIPPED && kind != GM_ARROW_SPECTRAL)
            || potion_type < TB_PT_EMPTY || potion_type >= TB_PT_COUNT
            || custom_id < 0 || custom_id > 27
            || custom_amplifier < 0 || custom_amplifier > 255
            || (custom_id > 0 && custom_duration <= 0)
            || custom_flags < 0 || custom_flags > 3
            || target_type < -1 || target_type > 1
            || (ground_ticks != -1 && ground_ticks != 599
                && ground_ticks != 600))
        return 2;

    GmConfig config;
    GmRuntime runtime;
    GmAction idle;
    char error[256];
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.seed = 0;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    gm_runtime_set_pose(&runtime, 4.5, 4.0, 4.5, 0.0F, 0.0F);
    gm_world_set_block_meta(runtime.world, 8, 4, 8, 1, 0);
    if (!gm_runtime_spawn_player_arrow_state_fixture(
            &runtime, 7300, 7.95, 4.5, 8.5,
            0.1, 0.0, 0.0, 0.0F, 0.0F,
            0, -1, 1.0, 0, 0, 1,
            ground_ticks >= 0, 0, 0,
            ground_ticks >= 0 ? 8 : -1,
            ground_ticks >= 0 ? 4 : -1,
            ground_ticks >= 0 ? 8 : -1,
            ground_ticks >= 0 ? 1 : 0, 0,
            UINT64_C(1), 0, 0.0)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    GmRuntimeProjectile *arrow = &runtime.projectiles[0];
    PtMobEffect custom;
    unsigned char flags;
    int color = fixed_color >= 0 ? fixed_color
        : automatic_color(
            potion_type, custom_id, custom_amplifier, custom_flags);
    int pickup_item = kind == GM_ARROW_SPECTRAL ? 439
        : potion_type != TB_PT_EMPTY || custom_id > 0 ? 440 : 262;
    int pickup_meta = pickup_item == 440
        ? (potion_type == TB_PT_EMPTY ? TB_PT_WATER : potion_type) : 0;
    custom = (PtMobEffect){custom_id, custom_duration, custom_amplifier};
    flags = (unsigned char)custom_flags;
    if (!gm_runtime_set_arrow_payload(
            &runtime, 7300, kind, potion_type, spectral_duration,
            kind == GM_ARROW_TIPPED ? color : -1,
            kind == GM_ARROW_TIPPED && fixed_color >= 0,
            custom_id > 0 ? &custom : NULL, custom_id > 0,
            custom_id > 0 ? &flags : NULL,
            pickup_item, pickup_meta, 0)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }

    int target = -1;
    if (ground_ticks >= 0) {
        arrow->arrow_time_in_ground = ground_ticks - 1;
        gm_runtime_tick(&runtime, idle);
    } else if (target_type >= 0) {
        target = gm_mobs_spawn(
            &runtime.mobs,
            target_type == 0 ? EW_TYPE_PIG : EW_TYPE_ZOMBIE,
            20.5, 4.0, 20.5);
        if (target <= 0
                || gm_runtime_player_arrow_hit_now(
                    &runtime, 0, target) < 2) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
    }

    EffectRow rows[GM_MOB_EFFECT_CAPACITY];
    int row_count = target > 0
        ? gm_mobs_potion_effect_count(&runtime.mobs, target) : 0;
    for (int index = 0; index < row_count; ++index) {
        gm_mobs_potion_effect_get(
            &runtime.mobs, target, index, &rows[index].effect);
        gm_mobs_potion_effect_flags(
            &runtime.mobs, target, index,
            &rows[index].ambient, &rows[index].show_particles);
    }
    qsort(rows, (size_t)row_count, sizeof rows[0], compare_effect_rows);
    printf("{\"ok\":true,\"pickup_item\":%d,\"pickup_potion\":%d,"
           "\"pickup_effect_count\":%d,\"pickup_color\":%d,"
           "\"arrow_color\":%d,\"effects\":[",
           arrow->arrow_pickup_item, arrow->arrow_pickup_item == 440
                ? arrow->arrow_pickup_meta : TB_PT_EMPTY,
           arrow->arrow_pickup_item == 440
                ? arrow->arrow_effect_count : 0,
           arrow->arrow_pickup_item == 440 && arrow->arrow_custom_color
                ? arrow->arrow_color : -1,
           arrow->arrow_kind == GM_ARROW_TIPPED
                ? arrow->arrow_color : -1);
    for (int index = 0; index < row_count; ++index)
        printf("%s{\"id\":%d,\"amp\":%d,\"dur\":%d,"
               "\"ambient\":%s,\"show_particles\":%s}",
               index ? "," : "", rows[index].effect.id,
               rows[index].effect.amplifier,
               rows[index].effect.duration,
               rows[index].ambient ? "true" : "false",
               rows[index].show_particles ? "true" : "false");
    puts("]}");
    gm_runtime_destroy(&runtime);
    return 0;
}
