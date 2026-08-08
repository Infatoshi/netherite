#include "game/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int init_flat(GmRuntime *runtime) {
    GmConfig config;
    char error[256];
    gm_config_defaults(&config);
    config.seed = 42;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(runtime, &config, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 0;
    }
    runtime->randtick_enabled = 0;
    return 1;
}

static GmRuntimeStructureBlock state(
        const char *name, int mode, int px, int py, int pz,
        int sx, int sy, int sz) {
    GmRuntimeStructureBlock value;
    memset(&value, 0, sizeof value);
    snprintf(value.name, sizeof value.name, "%s", name);
    value.pos_x = px;
    value.pos_y = py;
    value.pos_z = pz;
    value.size_x = sx;
    value.size_y = sy;
    value.size_z = sz;
    value.mode = mode;
    value.ignore_entities = 1;
    value.show_bounding_box = 1;
    value.integrity = 1.0f;
    return value;
}

static int structure_at(
        const GmRuntime *runtime, int x, int y, int z,
        GmRuntimeStructureBlock *out) {
    int count = gm_runtime_structure_block_count(runtime);
    for (int index = 0; index < count; ++index) {
        GmRuntimeStructureBlock value;
        if (gm_runtime_structure_block_get(runtime, index, &value)
                && value.wx == x && value.wy == y && value.wz == z) {
            if (out) *out = value;
            return 1;
        }
    }
    return 0;
}

static int packed_at(const GmRuntime *runtime, int x, int y, int z) {
    return gm_world_block(runtime->world, x, y, z) * 16
        + gm_world_meta(runtime->world, x, y, z);
}

static GmRuntimeStructureTemplate *template_named(
        GmRuntime *runtime, const char *name) {
    for (int index = 0; index < runtime->structure_templates_cap; ++index) {
        GmRuntimeStructureTemplate *value =
            &runtime->structure_templates[index];
        if (value->active && strcmp(value->name, name) == 0)
            return value;
    }
    return NULL;
}

static unsigned long long double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return (unsigned long long)bits;
}

static unsigned int float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return (unsigned int)bits;
}

static int runtime_string_equals(
        const GmRuntime *runtime, int id, const char *expected) {
    const unsigned char *data;
    size_t length;
    size_t expected_length = strlen(expected);
    return id > 0
        && gm_runtime_armor_stand_string(runtime, id, &data, &length)
        && length == expected_length
        && memcmp(data, expected, length) == 0;
}

static int configure_boat(
        GmRuntime *runtime, int eid, int variant,
        double vx, double vy, double vz) {
    int slot = gm_mobs_find_slot_by_eid(&runtime->mobs, eid);
    EwStore *now = runtime->mobs.current
        ? &runtime->mobs.b : &runtime->mobs.a;
    EwStore *next = runtime->mobs.current
        ? &runtime->mobs.a : &runtime->mobs.b;
    if (slot <= 0 || variant < 0 || variant > 5) return 0;
    now->vx[slot] = vx;
    now->vy[slot] = vy;
    now->vz[slot] = vz;
    runtime->mobs.boat_variant[slot] = (unsigned char)variant;
    ew_store_copy_slot(next, now, slot);
    return 1;
}

int main(void) {
    GmRuntime runtime;
    GmRuntimeStructureBlock value, defaults, detected, loader, redstone;
    const int x = 8, y = 200, z = 8;
    int first_load, second_load, integrity_load;
    int loaded_chest_item = 0, loaded_chest_count = 0;
    GmMobTemplateEntity loaded_sheep, loaded_xp, loaded_boat;
    GmLiveEnt *source_item = NULL, *loaded_item = NULL;
    GmRuntimePrimedTnt *source_tnt = NULL, *loaded_tnt = NULL;
    GmRuntimeFallingBlock *source_falling = NULL, *loaded_falling = NULL;
    GmRuntimeEndCrystal *source_crystal = NULL, *loaded_crystal = NULL;
    GmRuntimeMinecart *source_cart = NULL, *loaded_cart = NULL;
    GmRuntimeStructureTemplate *saved_template;
    int item_name;
    int loaded_sheep_slot, source_sheep_slot;
    int loaded_sheep_fresh_uuid;
    int loaded_falling_tile_test = 0;
    int cart_variants_saved = 0, cart_variants_loaded = 0;
    uint64_t structure_entity_seed48_after = 0;
    GmRuntimeMinecart loaded_cart_variants[7];
    int loaded_cart_variant_present[7] = {0};
    if (!init_flat(&runtime)) return 1;
    item_name = gm_runtime_item_name_intern(&runtime, "Oracle item");
    if (item_name <= 0) goto fail;
    if (!gm_runtime_set_block(&runtime, x, y, z, 255, 0)
            || !structure_at(&runtime, x, y, z, &defaults))
        goto fail;
    value = defaults;
    snprintf(value.name, sizeof value.name, "bad.name?");
    if (!gm_runtime_structure_block_set(&runtime, 0, x, y, z, &value)
            || !structure_at(&runtime, x, y, z, &value))
        goto fail;
    char sanitized[GM_STRUCTURE_NAME_LENGTH];
    snprintf(sanitized, sizeof sanitized, "%s", value.name);

    value = state("bounds", GM_STRUCTURE_MODE_SAVE, 0, 1, 0, 0, 0, 0);
    if (!gm_runtime_structure_block_set(&runtime, 0, x, y, z, &value)
            || !gm_runtime_set_block(&runtime, x - 3, y - 3, z - 3, 255, 2)
            || !gm_runtime_set_block(&runtime, x + 4, y + 4, z + 5, 255, 2))
        goto fail;
    value = state("bounds", GM_STRUCTURE_MODE_CORNER, 0, 1, 0, 0, 0, 0);
    if (!gm_runtime_structure_block_set(
                &runtime, 0, x - 3, y - 3, z - 3, &value)
            || !gm_runtime_structure_block_set(
                &runtime, 0, x + 4, y + 4, z + 5, &value)
            || !gm_runtime_structure_detect_size(&runtime, 0, x, y, z)
            || !structure_at(&runtime, x, y, z, &detected))
        goto fail;

    value = state("qrl_structure_oracle", GM_STRUCTURE_MODE_SAVE,
                  1, 0, 1, 3, 1, 2);
    value.ignore_entities = 0;
    if (!gm_runtime_structure_block_set(&runtime, 0, x, y, z, &value)
            || !gm_runtime_set_block(&runtime, x + 1, y, z + 1, 1, 1)
            || !gm_runtime_set_block(&runtime, x + 2, y, z + 1, 54, 2)
            || !gm_runtime_chest_set_slot(
                &runtime, 0, x + 2, y, z + 1, 4, 264, 3, 0)
            || !gm_runtime_set_block(&runtime, x + 3, y, z + 1, 5, 2)
            || !gm_runtime_set_block(&runtime, x + 1, y, z + 2, 217, 0)
            || !gm_runtime_set_block(&runtime, x + 2, y, z + 2, 53, 0)
            || !gm_runtime_set_block(&runtime, x + 3, y, z + 2, 95, 11)
            || !gm_runtime_spawn_mob_fixture(
                &runtime, GM_MOB_SHEEP, 9101,
                (double)x + 1.5, (double)y, (double)z + 1.5,
                0.125, 0.0, -0.25, 37.0F, 7.5F, 1, 0, 0, 0)
            || !gm_runtime_set_sheep_state(&runtime, 9101, 11, 1)
            || !gm_runtime_set_mob_growing_age(&runtime, 9101, -2400)
            || !gm_runtime_set_mob_fire_ticks(&runtime, 9101, 40)
            || !gm_runtime_set_mob_uuid(
                &runtime, 9101, INT64_C(0x1234), INT64_C(0x5678))
            || !gm_runtime_spawn_xp_fixture(
                &runtime, (double)x + 2.5, (double)y + 0.25,
                (double)z + 1.5, -0.1, 0.2, 0.3,
                17, 9102, 45, 7, 9, 12)
            || !gm_runtime_spawn_item_state_fixture(
                &runtime, 9104, (double)x + 2.25,
                (double)y + 0.5, (double)z + 1.5,
                0.4, -0.3, 0.2, -25.0F, 1.75F,
                276, 1, 17, -120, -7, 3, 7000,
                1, 1, 37, 40, 1, 0,
                UINT64_C(0x123456789abc))
            || !gm_runtime_spawn_boat_fixture(
                &runtime, 9103, (double)x + 2.0, (double)y,
                (double)z + 1.75, -30.0F)
            || !configure_boat(
                &runtime, 9103, 5, 0.05, 0.1, -0.15)
            || !gm_runtime_spawn_primed_tnt_fixture(
                &runtime, 9105, (double)x + 1.25, (double)y,
                (double)z + 1.25, 0.125, -0.25, 0.375, 61)
            || !gm_runtime_spawn_falling_fixture(
                &runtime, 9106, 145, 8, 37,
                (double)x + 1.75, (double)y + 0.1,
                (double)z + 1.75, -0.2, 0.3, 0.4, 1, 0)
            || !gm_runtime_spawn_end_crystal_fixture(
                &runtime, 9107, (double)x + 2.5, (double)y,
                (double)z + 1.5, 12345, 0, 1, 7, 120, -9)
            || !gm_runtime_spawn_minecart_fixture(
                &runtime, GM_MINECART_CHEST, 9108,
                (double)x + 2.75, (double)y + 0.2,
                (double)z + 1.25, 0.2, -0.1, 0.3, -44.0F))
        goto fail;
    for (int slot = 0; slot < GM_LIVE_MAX; ++slot)
        if (runtime.entities.ents[slot].active
                && runtime.entities.ents[slot].type == 0
                && runtime.entities.ents[slot].eid == 9104) {
            source_item = &runtime.entities.ents[slot];
            break;
        }
    if (!source_item) goto fail;
    source_item->pitch = 12.5F;
    source_item->fall_distance = 2.25F;
    source_item->air = 123;
    source_item->portal_cooldown = 9;
    source_item->repair_cost = 4;
    source_item->custom_name = item_name;
    source_item->n_enchants = 1;
    source_item->ench_id[0] = 34;
    source_item->ench_lvl[0] = 2;
    for (int slot = 0; slot < runtime.primed_tnt_cap; ++slot)
        if (runtime.primed_tnt[slot].active
                && runtime.primed_tnt[slot].eid == 9105)
            source_tnt = &runtime.primed_tnt[slot];
    for (int slot = 0; slot < GM_RUNTIME_FALLING_BLOCKS; ++slot)
        if (runtime.falling_blocks[slot].active
                && runtime.falling_blocks[slot].eid == 9106)
            source_falling = &runtime.falling_blocks[slot];
    for (int slot = 0; slot < GM_RUNTIME_END_CRYSTALS; ++slot)
        if (runtime.end_crystals[slot].active
                && runtime.end_crystals[slot].eid == 9107)
            source_crystal = &runtime.end_crystals[slot];
    for (int slot = 0; slot < GM_RUNTIME_MINECARTS; ++slot)
        if (runtime.minecarts[slot].active
                && runtime.minecarts[slot].eid == 9108)
            source_cart = &runtime.minecarts[slot];
    if (!source_tnt || !source_falling || !source_crystal || !source_cart)
        goto fail;
    source_tnt->yaw = -33.0F; source_tnt->pitch = 14.0F;
    source_tnt->fall_distance = 1.25F;
    source_tnt->on_ground = 1; source_tnt->no_gravity = 1;
    source_tnt->air = 122; source_tnt->fire_ticks = 9;
    source_tnt->portal_cooldown = 8;
    source_falling->yaw = 17.0F; source_falling->pitch = -4.0F;
    source_falling->fall_distance = 6.5F;
    source_falling->air = 121; source_falling->fire_ticks = 7;
    source_falling->portal_cooldown = 6;
    source_falling->should_drop_item = 0;
    source_falling->hurt_entities = 1;
    source_falling->fall_hurt_amount = 3.5F;
    source_falling->fall_hurt_max = 27;
    {
        static const unsigned char tile_nbt[] = {
            10, 0, 0, 3, 0, 4, 'T', 'e', 's', 't',
            0, 0, 0, 7, 0
        };
        source_falling->tile_entity_tag_id = gm_runtime_stack_tag_intern(
            &runtime, tile_nbt, sizeof tile_nbt);
    }
    source_crystal->vx = 0.01; source_crystal->vy = -0.02;
    source_crystal->vz = 0.03;
    source_crystal->yaw = 22.0F; source_crystal->pitch = -11.0F;
    source_crystal->fall_distance = 2.5F;
    source_crystal->on_ground = 1; source_crystal->no_gravity = 1;
    source_crystal->air = 120; source_crystal->fire_ticks = 5;
    source_crystal->portal_cooldown = 4;
    source_cart->pitch = 6.0F;
    source_cart->fall_distance = 1.75F;
    source_cart->on_ground = 1;
    source_cart->air = 119;
    source_cart->fire_ticks = 3;
    source_cart->portal_cooldown = 2;
    source_cart->no_gravity = 1;
    source_cart->custom_display = 1;
    source_cart->display_block = 1;
    source_cart->display_meta = 3;
    source_cart->display_offset = 8;
    source_cart->reverse = 1;
    source_cart->rolling_amplitude = 7;
    source_cart->rolling_direction = -1;
    source_cart->damage = 12.0F;
    if (!gm_runtime_set_item_entity_uuid(
            &runtime, 9104, INT64_C(0x9876), INT64_C(0x5432))
            || source_falling->tile_entity_tag_id <= 0
            || !gm_runtime_set_transient_entity_uuid(
                &runtime, 9105, INT64_C(0x1111), INT64_C(0x2222))
            || !gm_runtime_set_transient_entity_uuid(
                &runtime, 9106, INT64_C(0x3333), INT64_C(0x4444))
            || !gm_runtime_set_transient_entity_uuid(
                &runtime, 9107, INT64_C(0x5555), INT64_C(0x6666))
            || !gm_runtime_minecart_set_uuid(
                &runtime, 9108, INT64_C(0x7777), INT64_C(0x8888))
            || !gm_runtime_minecart_set_slot(
                &runtime, 9108, 4, 264, 3, 0)
            || !gm_runtime_structure_save(&runtime, 0, x, y, z))
        goto fail;
    for (int slot = 0; slot < GM_XP_ORBS; ++slot)
        if (!runtime.mobs.xp_orbs[slot].dead
                && runtime.mobs.xp_orbs[slot].eid == 9102)
            runtime.mobs.xp_orbs[slot].yaw = 17.0F;
    /* Re-save after assigning the source orb's serialized Rotation. */
    if (!gm_runtime_structure_save(&runtime, 0, x, y, z)) goto fail;
    saved_template = template_named(&runtime, "qrl_structure_oracle");
    if (!saved_template || saved_template->entity_count != 8)
        goto fail;

    if (!gm_runtime_set_block(&runtime, x + 22, y, z + 22, 255, 1))
        goto fail;
    value = state("qrl_structure_oracle", GM_STRUCTURE_MODE_LOAD,
                  5, 0, 5, 0, 0, 0);
    value.mirror = GM_STRUCTURE_MIRROR_FRONT_BACK;
    value.rotation = GM_STRUCTURE_ROTATION_CW90;
    value.ignore_entities = 0;
    if (!gm_runtime_structure_block_set(
            &runtime, 0, x + 22, y, z + 22, &value)
            || !gm_runtime_set_entity_id_cursor(&runtime, 9200))
        goto fail;
    first_load = gm_runtime_structure_load(
        &runtime, 0, x + 22, y, z + 22, 1);
    if (!structure_at(&runtime, x + 22, y, z + 22, &loader))
        goto fail;
    if (!gm_runtime_set_math_random_seed48(
            &runtime, UINT64_C(0x23456789abcd))
            || !gm_runtime_set_entity_seed_generator_seed48(
                &runtime, UINT64_C(0x3456789abcde))
            || !gm_runtime_set_server_uuid_random_seed48(
                &runtime, UINT64_C(0x456789abcdef)))
        goto fail;
    second_load = gm_runtime_structure_load(
        &runtime, 0, x + 22, y, z + 22, 1);
    if (!second_load) goto fail;
    structure_entity_seed48_after =
        runtime.entity_seed_generator_seed48;
    if (!gm_mobs_template_entity_capture(
                &runtime.mobs, 9200, &loaded_sheep)
            || !gm_mobs_template_entity_capture(
                &runtime.mobs, 9201, &loaded_xp)
            || !gm_mobs_template_entity_capture(
                &runtime.mobs, 9203, &loaded_boat))
        goto fail;
    for (int slot = 0; slot < GM_LIVE_MAX; ++slot)
        if (runtime.entities.ents[slot].active
                && runtime.entities.ents[slot].type == 0
                && runtime.entities.ents[slot].eid == 9202) {
            loaded_item = &runtime.entities.ents[slot];
            break;
        }
    for (int slot = 0; slot < runtime.primed_tnt_cap; ++slot)
        if (runtime.primed_tnt[slot].active
                && runtime.primed_tnt[slot].eid == 9204)
            loaded_tnt = &runtime.primed_tnt[slot];
    for (int slot = 0; slot < GM_RUNTIME_FALLING_BLOCKS; ++slot)
        if (runtime.falling_blocks[slot].active
                && runtime.falling_blocks[slot].eid == 9205)
            loaded_falling = &runtime.falling_blocks[slot];
    for (int slot = 0; slot < GM_RUNTIME_END_CRYSTALS; ++slot)
        if (runtime.end_crystals[slot].active
                && runtime.end_crystals[slot].eid == 9206)
            loaded_crystal = &runtime.end_crystals[slot];
    for (int slot = 0; slot < GM_RUNTIME_MINECARTS; ++slot)
        if (runtime.minecarts[slot].active
                && runtime.minecarts[slot].eid == 9207)
            loaded_cart = &runtime.minecarts[slot];
    if (!loaded_item || !loaded_tnt || !loaded_falling || !loaded_crystal
            || !loaded_cart)
        goto fail;
    {
        double value_number = 0.0;
        const GmNbtBlob *tag = gm_runtime_stack_tag(
            &runtime, loaded_falling->tile_entity_tag_id);
        if (!tag || !gm_nbt_blob_find_number(
                tag, "Test", &value_number, NULL))
            goto fail;
        loaded_falling_tile_test = (int)value_number;
    }
    source_sheep_slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 9101);
    loaded_sheep_slot = gm_mobs_find_slot_by_eid(&runtime.mobs, 9200);
    if (source_sheep_slot <= 0 || loaded_sheep_slot <= 0
            || !runtime.mobs.entity_uuid_present[loaded_sheep_slot])
        goto fail;
    loaded_sheep_fresh_uuid =
        runtime.mobs.entity_uuid_most[loaded_sheep_slot]
            != runtime.mobs.entity_uuid_most[source_sheep_slot]
        || runtime.mobs.entity_uuid_least[loaded_sheep_slot]
            != runtime.mobs.entity_uuid_least[source_sheep_slot];
    for (int index = 0; index < runtime.chests_cap; ++index) {
        const GmRuntimeChest *chest = &runtime.chests[index];
        if (chest->active && chest->wx == x + 27 && chest->wy == y
                && chest->wz == z + 26) {
            ICStack stack = chest_live_get(&chest->state, 4);
            loaded_chest_item = stack.item;
            loaded_chest_count = stack.count;
            break;
        }
    }

    {
        const int controller_x = x + 50;
        const int controller_z = z + 50;
        const int load_x = controller_x + 12;
        const int load_z = controller_z + 12;
        GmRuntimeStructureTemplate *variant_template;
        value = state("qrl_minecart_variants", GM_STRUCTURE_MODE_SAVE,
                      1, 0, 1, 4, 1, 2);
        value.ignore_entities = 0;
        if (!gm_runtime_set_block(
                    &runtime, controller_x, y, controller_z, 255, 0)
                || !gm_runtime_structure_block_set(
                    &runtime, 0, controller_x, y, controller_z, &value))
            goto fail;
        for (int kind = 0; kind < 7; ++kind) {
            int eid = 9300 + kind;
            if (!gm_runtime_spawn_minecart_fixture(
                    &runtime, kind, eid,
                    (double)controller_x + 1.25 + (double)kind * 0.45,
                    (double)y + 0.2, (double)controller_z + 1.5,
                    0.0, 0.0, 0.0, 0.0F)
                    || !gm_runtime_minecart_set_uuid(
                        &runtime, eid,
                        INT64_C(0x9000) + kind,
                        INT64_C(0xa000) + kind))
                goto fail;
        }
        if (!gm_runtime_minecart_set_slot(
                    &runtime, 9301, 4, 264, 3, 0)
                || !gm_runtime_minecart_set_state(
                    &runtime, 9302, 1234, 0.25, -0.5, -1, 1, -1)
                || !gm_runtime_minecart_set_state(
                    &runtime, 9303, 0, 0.0, 0.0, 47, 1, -1)
                || !gm_runtime_minecart_set_state(
                    &runtime, 9305, 0, 0.0, 0.0, -1, 0, 3)
                || !gm_runtime_minecart_set_slot(
                    &runtime, 9305, 2, 388, 4, 0)
                || !gm_runtime_minecart_set_command_state(
                    &runtime, 9306, "Searge", "cart-oracle", 7, 1,
                    "{\"text\":\"seed\"}"))
            goto fail;
        for (int slot = 0; slot < GM_RUNTIME_MINECARTS; ++slot)
            if (runtime.minecarts[slot].active
                    && runtime.minecarts[slot].eid == 9306) {
                runtime.minecarts[slot].custom_display = 1;
                runtime.minecarts[slot].display_block = 1;
                runtime.minecarts[slot].display_meta = 4;
                runtime.minecarts[slot].display_offset = 9;
            }
        cart_variants_saved = gm_runtime_structure_save(
            &runtime, 0, controller_x, y, controller_z);
        variant_template = template_named(
            &runtime, "qrl_minecart_variants");
        if (!cart_variants_saved || !variant_template
                || variant_template->entity_count != 7
                || !gm_runtime_set_block(
                    &runtime, load_x, y, load_z, 255, 1))
            goto fail;
        value = state("qrl_minecart_variants", GM_STRUCTURE_MODE_LOAD,
                      1, 0, 1, 4, 1, 2);
        value.ignore_entities = 0;
        if (!gm_runtime_structure_block_set(
                    &runtime, 0, load_x, y, load_z, &value)
                || !gm_runtime_set_entity_id_cursor(&runtime, 9400)
                || !gm_runtime_set_entity_seed_generator_seed48(
                    &runtime, UINT64_C(0x13579bdf2468))
                || !gm_runtime_set_server_uuid_random_seed48(
                    &runtime, UINT64_C(0x2468ace13579)))
            goto fail;
        cart_variants_loaded = gm_runtime_structure_load(
            &runtime, 0, load_x, y, load_z, 1);
        if (!cart_variants_loaded) goto fail;
        for (int slot = 0; slot < GM_RUNTIME_MINECARTS; ++slot) {
            const GmRuntimeMinecart *candidate = &runtime.minecarts[slot];
            if (!candidate->active || candidate->eid < 9400
                    || candidate->eid >= 9407)
                continue;
            int kind = candidate->eid - 9400;
            loaded_cart_variants[kind] = *candidate;
            loaded_cart_variant_present[kind] = 1;
        }
        for (int kind = 0; kind < 7; ++kind)
            if (!loaded_cart_variant_present[kind]
                    || loaded_cart_variants[kind].kind != kind
                    || !loaded_cart_variants[kind].uuid_present)
                goto fail;
        if (!runtime_string_equals(
                    &runtime, loaded_cart_variants[6].command_tag_id,
                    "Searge")
                || !runtime_string_equals(
                    &runtime, loaded_cart_variants[6].command_name_tag_id,
                    "cart-oracle")
                || !runtime_string_equals(
                    &runtime,
                    loaded_cart_variants[6].command_last_output_tag_id,
                    "{\"text\":\"seed\"}")
                || loaded_cart_variants[6].command_success_count != 7
                || loaded_cart_variants[6].command_track_output != 1
                || loaded_cart_variants[6].ticks_existed != 0
                || loaded_cart_variants[6].command_activator_cooldown != 0)
            goto fail;
    }

    value = loader;
    value.pos_x = 7;
    value.pos_z = 7;
    value.mirror = GM_STRUCTURE_MIRROR_NONE;
    value.rotation = GM_STRUCTURE_ROTATION_NONE;
    value.ignore_entities = 1;
    value.integrity = 0.5f;
    value.seed = 12345;
    if (!gm_runtime_structure_block_set(
            &runtime, 0, x + 22, y, z + 22, &value))
        goto fail;
    for (int rz = 0; rz < 2; ++rz)
        for (int rx = 0; rx < 3; ++rx)
            if (!gm_runtime_set_block(
                    &runtime, x + 29 + rx, y, z + 29 + rz, 0, 0))
                goto fail;
    integrity_load = gm_runtime_structure_load(
        &runtime, 0, x + 22, y, z + 22, 1);

    if (!gm_runtime_set_block(&runtime, x + 32, y, z + 32, 255, 1))
        goto fail;
    value = state("qrl_structure_oracle", GM_STRUCTURE_MODE_LOAD,
                  2, 0, 2, 3, 1, 2);
    if (!gm_runtime_structure_block_set(
                &runtime, 0, x + 32, y, z + 32, &value)
            || !gm_runtime_set_block(
                &runtime, x + 31, y, z + 32, 152, 0)
            || !structure_at(
                &runtime, x + 32, y, z + 32, &redstone))
        goto fail;
    int rising_powered = redstone.powered;
    int rising_state = packed_at(&runtime, x + 34, y, z + 34);
    if (!gm_runtime_set_block(
                &runtime, x + 31, y, z + 32, 0, 0)
            || !structure_at(
                &runtime, x + 32, y, z + 32, &redstone))
        goto fail;

    printf("{\"ok\":true,\"default_name\":\"%s\","
           "\"default_author\":\"%s\",\"default_metadata\":\"%s\","
           "\"default_position\":[%d,%d,%d],"
           "\"default_size\":[%d,%d,%d],"
           "\"default_mode\":\"DATA\",\"default_mirror\":\"NONE\","
           "\"default_rotation\":\"NONE\","
           "\"default_ignore_entities\":%s,\"default_powered\":%s,"
           "\"default_show_air\":%s,\"default_show_box\":%s,"
           "\"default_integrity_bits\":\"3f800000\","
           "\"default_seed\":%lld,\"sanitized_name\":\"%s\","
           "\"detected\":true,\"detected_position\":[%d,%d,%d],"
           "\"detected_size\":[%d,%d,%d],\"saved\":true,"
           "\"saved_entity_count\":%d,"
           "\"saved_entity_kinds\":[\"sheep\",\"xp\",\"item\",\"boat\","
           "\"tnt\",\"falling_block\",\"end_crystal\",\"minecart\"],"
           "\"saved_entity_pos_bits\":["
           "[\"%016llx\",\"%016llx\",\"%016llx\"],"
           "[\"%016llx\",\"%016llx\",\"%016llx\"],"
           "[\"%016llx\",\"%016llx\",\"%016llx\"],"
           "[\"%016llx\",\"%016llx\",\"%016llx\"],"
           "[\"%016llx\",\"%016llx\",\"%016llx\"],"
           "[\"%016llx\",\"%016llx\",\"%016llx\"],"
           "[\"%016llx\",\"%016llx\",\"%016llx\"],"
           "[\"%016llx\",\"%016llx\",\"%016llx\"]],"
           "\"first_load\":%s,\"synchronized_size\":[%d,%d,%d],"
           "\"second_load\":%s,\"loaded_states\":[",
           defaults.name, defaults.author, defaults.metadata,
           defaults.pos_x, defaults.pos_y, defaults.pos_z,
           defaults.size_x, defaults.size_y, defaults.size_z,
           defaults.ignore_entities ? "true" : "false",
           defaults.powered ? "true" : "false",
           defaults.show_air ? "true" : "false",
           defaults.show_bounding_box ? "true" : "false",
           defaults.seed, sanitized,
           detected.pos_x, detected.pos_y, detected.pos_z,
           detected.size_x, detected.size_y, detected.size_z,
           saved_template->entity_count,
           double_bits(saved_template->entities[0].x),
           double_bits(saved_template->entities[0].y),
           double_bits(saved_template->entities[0].z),
           double_bits(saved_template->entities[1].x),
           double_bits(saved_template->entities[1].y),
           double_bits(saved_template->entities[1].z),
           double_bits(saved_template->entities[2].x),
           double_bits(saved_template->entities[2].y),
           double_bits(saved_template->entities[2].z),
           double_bits(saved_template->entities[3].x),
           double_bits(saved_template->entities[3].y),
           double_bits(saved_template->entities[3].z),
           double_bits(saved_template->entities[4].x),
           double_bits(saved_template->entities[4].y),
           double_bits(saved_template->entities[4].z),
           double_bits(saved_template->entities[5].x),
           double_bits(saved_template->entities[5].y),
           double_bits(saved_template->entities[5].z),
           double_bits(saved_template->entities[6].x),
           double_bits(saved_template->entities[6].y),
           double_bits(saved_template->entities[6].z),
           double_bits(saved_template->entities[7].x),
           double_bits(saved_template->entities[7].y),
           double_bits(saved_template->entities[7].z),
           first_load ? "true" : "false",
           loader.size_x, loader.size_y, loader.size_z,
           second_load ? "true" : "false");
    static const int loaded_offsets[6][2] = {
        {5,5}, {5,4}, {5,3}, {4,5}, {4,4}, {4,3}
    };
    for (int index = 0; index < 6; ++index) {
        if (index) putchar(',');
        printf("%d", packed_at(
            &runtime, x + 22 + loaded_offsets[index][0], y,
            z + 22 + loaded_offsets[index][1]));
    }
    printf("],\"loaded_chest_item\":%d,\"loaded_chest_count\":%d,"
           "\"loaded_sheep_bits\":["
           "\"%016llx\",\"%016llx\",\"%016llx\","
           "\"%016llx\",\"%016llx\",\"%016llx\","
           "\"%08x\",\"%08x\"],"
           "\"loaded_sheep_fire\":%d,\"loaded_sheep_age\":%d,"
           "\"loaded_sheep_color\":%d,\"loaded_sheep_sheared\":%s,"
           "\"loaded_sheep_no_ai\":%s,"
           "\"loaded_sheep_fresh_uuid\":%s,"
           "\"loaded_xp_bits\":["
           "\"%016llx\",\"%016llx\",\"%016llx\","
           "\"%016llx\",\"%016llx\",\"%016llx\","
           "\"%08x\",\"%016llx\",\"%016llx\"],"
           "\"loaded_xp_health\":%d,\"loaded_xp_age\":%d,"
           "\"loaded_xp_value\":%d,\"loaded_xp_pickup\":%d,"
           "\"loaded_xp_color\":%d,\"loaded_xp_target_color\":%d,"
           "\"loaded_xp_fresh_uuid\":true,"
           "\"loaded_item_bits\":["
           "\"%016llx\",\"%016llx\",\"%016llx\","
           "\"%016llx\",\"%016llx\",\"%016llx\","
           "\"%08x\",\"%08x\",\"%08x\",\"%08x\"],"
           "\"loaded_item_health\":%d,\"loaded_item_age\":%d,"
           "\"loaded_item_pickup\":%d,\"loaded_item_lifespan\":%d,"
           "\"loaded_item_on_ground\":%s,"
           "\"loaded_item_no_gravity\":%s,"
           "\"loaded_item_fire\":%d,\"loaded_item_air\":%d,"
           "\"loaded_item_portal_cooldown\":%d,"
           "\"loaded_item_ticks_existed\":%d,"
           "\"loaded_item_item\":%d,\"loaded_item_count\":%d,"
           "\"loaded_item_meta\":%d,"
           "\"loaded_item_repair_cost\":%d,"
           "\"loaded_item_custom_name\":\"%s\","
           "\"loaded_item_enchant_level\":%d,"
           "\"loaded_item_fresh_uuid\":%s,"
           "\"loaded_item_entity_seed48\":%llu,"
           "\"structure_math_seed48_after\":%llu,"
           "\"structure_entity_seed48_after\":%llu,"
           "\"loaded_boat_bits\":["
           "\"%016llx\",\"%016llx\",\"%016llx\","
           "\"%016llx\",\"%016llx\",\"%016llx\","
           "\"%08x\",\"%016llx\",\"%016llx\"],"
           "\"loaded_boat_type\":%d,"
           "\"loaded_boat_fresh_uuid\":true,"
           "\"loaded_tnt_bits\":["
           "\"%016llx\",\"%016llx\",\"%016llx\","
           "\"%016llx\",\"%016llx\",\"%016llx\","
           "\"%08x\",\"%08x\",\"%08x\"],"
           "\"loaded_tnt_fuse\":%d,\"loaded_tnt_on_ground\":%s,"
           "\"loaded_tnt_no_gravity\":%s,"
           "\"loaded_tnt_fire\":%d,\"loaded_tnt_air\":%d,"
           "\"loaded_tnt_portal_cooldown\":%d,"
           "\"loaded_tnt_seed48\":%llu,"
           "\"loaded_tnt_fresh_uuid\":%s,"
           "\"loaded_falling_bits\":["
           "\"%016llx\",\"%016llx\",\"%016llx\","
           "\"%016llx\",\"%016llx\",\"%016llx\","
           "\"%08x\",\"%08x\",\"%08x\"],"
           "\"loaded_falling_block\":%d,\"loaded_falling_meta\":%d,"
           "\"loaded_falling_time\":%d,"
           "\"loaded_falling_drop_item\":%s,"
           "\"loaded_falling_hurt_entities\":%s,"
           "\"loaded_falling_hurt_amount_bits\":\"%08x\","
           "\"loaded_falling_hurt_max\":%d,"
           "\"loaded_falling_tile_test\":%d,"
           "\"loaded_falling_no_gravity\":%s,"
           "\"loaded_falling_on_ground\":%s,"
           "\"loaded_falling_fire\":%d,\"loaded_falling_air\":%d,"
           "\"loaded_falling_portal_cooldown\":%d,"
           "\"loaded_falling_seed48\":%llu,"
           "\"loaded_falling_fresh_uuid\":%s,"
           "\"loaded_crystal_bits\":["
           "\"%016llx\",\"%016llx\",\"%016llx\","
           "\"%016llx\",\"%016llx\",\"%016llx\","
           "\"%08x\",\"%08x\",\"%08x\"],"
           "\"loaded_crystal_inner_rotation\":%d,"
           "\"loaded_crystal_show_bottom\":%s,"
           "\"loaded_crystal_has_beam\":%s,"
           "\"loaded_crystal_beam_x\":%d,"
           "\"loaded_crystal_beam_y\":%d,"
           "\"loaded_crystal_beam_z\":%d,"
           "\"loaded_crystal_no_gravity\":%s,"
           "\"loaded_crystal_on_ground\":%s,"
           "\"loaded_crystal_fire\":%d,\"loaded_crystal_air\":%d,"
           "\"loaded_crystal_portal_cooldown\":%d,"
           "\"loaded_crystal_seed48\":%llu,"
           "\"loaded_crystal_fresh_uuid\":%s,"
           "\"loaded_minecart_bits\":["
           "\"%016llx\",\"%016llx\",\"%016llx\","
           "\"%016llx\",\"%016llx\",\"%016llx\","
           "\"%08x\",\"%08x\",\"%08x\"],"
           "\"loaded_minecart_type\":%d,"
           "\"loaded_minecart_on_ground\":%s,"
           "\"loaded_minecart_no_gravity\":%s,"
           "\"loaded_minecart_fire\":%d,"
           "\"loaded_minecart_air\":%d,"
           "\"loaded_minecart_portal_cooldown\":%d,"
           "\"loaded_minecart_custom_display\":%s,"
           "\"loaded_minecart_display_block\":%d,"
           "\"loaded_minecart_display_meta\":%d,"
           "\"loaded_minecart_display_offset\":%d,"
           "\"loaded_minecart_item\":%d,"
           "\"loaded_minecart_count\":%d,"
           "\"loaded_minecart_seed48\":%llu,"
           "\"loaded_minecart_fresh_uuid\":%s,"
           "\"loaded_minecart_reverse\":%s,"
           "\"loaded_minecart_rolling_amplitude\":%d,"
           "\"loaded_minecart_rolling_direction\":%d,"
           "\"loaded_minecart_damage_bits\":\"%08x\","
           "\"minecart_variants_saved\":%s,"
           "\"minecart_variants_loaded\":%s,"
           "\"minecart_variant_kinds\":[0,1,2,3,4,5,6],"
           "\"minecart_chest_payload\":[%d,%d],"
           "\"minecart_furnace_payload\":[%d,\"%016llx\",\"%016llx\"],"
           "\"minecart_tnt_payload\":%d,"
           "\"minecart_hopper_payload\":[%s,%d,%d,%d],"
           "\"minecart_command_display\":[%s,%d,%d,%d],"
           "\"minecart_variant_fresh_uuids\":%s,"
           "\"integrity_load\":%s,\"integrity_states\":[",
           loaded_chest_item, loaded_chest_count,
           double_bits(loaded_sheep.x - (double)(x + 22)),
           double_bits(loaded_sheep.y - (double)y),
           double_bits(loaded_sheep.z - (double)(z + 22)),
           double_bits(loaded_sheep.vx),
           double_bits(loaded_sheep.vy), double_bits(loaded_sheep.vz),
           float_bits(loaded_sheep.yaw), float_bits(loaded_sheep.health),
           loaded_sheep.fire_ticks, loaded_sheep.growing_age,
           loaded_sheep.sheep_data & 15,
           (loaded_sheep.sheep_data & 16) ? "true" : "false",
           loaded_sheep.no_ai ? "true" : "false",
           loaded_sheep_fresh_uuid ? "true" : "false",
           double_bits(loaded_xp.x - (double)(x + 22)),
           double_bits(loaded_xp.y - (double)y),
           double_bits(loaded_xp.z - (double)(z + 22)),
           double_bits(loaded_xp.vx),
           double_bits(loaded_xp.vy), double_bits(loaded_xp.vz),
           float_bits(loaded_xp.yaw),
           double_bits(loaded_xp.box_max_x - loaded_xp.box_min_x),
           double_bits(loaded_xp.box_max_y - loaded_xp.box_min_y),
           loaded_xp.xp_health, loaded_xp.xp_age, loaded_xp.xp_value,
           loaded_xp.xp_pickup_delay, loaded_xp.xp_color,
           loaded_xp.xp_target_color,
           double_bits(loaded_item->x - (double)(x + 22)),
           double_bits(loaded_item->y - (double)y),
           double_bits(loaded_item->z - (double)(z + 22)),
           double_bits(loaded_item->mx),
           double_bits(loaded_item->my),
           double_bits(loaded_item->mz),
           float_bits(loaded_item->yaw),
           float_bits(loaded_item->pitch),
           float_bits(loaded_item->fall_distance),
           float_bits(loaded_item->hover_start),
           loaded_item->health, loaded_item->age,
           loaded_item->pickup_delay, loaded_item->lifespan,
           loaded_item->on_ground ? "true" : "false",
           loaded_item->no_gravity ? "true" : "false",
           loaded_item->fire, loaded_item->air,
           loaded_item->portal_cooldown, loaded_item->ticks_existed,
           loaded_item->item, loaded_item->count, loaded_item->meta,
           loaded_item->repair_cost,
           gm_runtime_item_name(&runtime, loaded_item->custom_name),
           loaded_item->n_enchants > 0
                && loaded_item->ench_id[0] == 34
               ? loaded_item->ench_lvl[0] : 0,
           loaded_item->uuid_present
                && (loaded_item->uuid_most != source_item->uuid_most
                    || loaded_item->uuid_least != source_item->uuid_least)
               ? "true" : "false",
           (unsigned long long)loaded_item->random_seed48,
           (unsigned long long)runtime.math_random_seed48,
           (unsigned long long)structure_entity_seed48_after,
           double_bits(loaded_boat.x - (double)(x + 22)),
           double_bits(loaded_boat.y - (double)y),
           double_bits(loaded_boat.z - (double)(z + 22)),
           double_bits(loaded_boat.vx), double_bits(loaded_boat.vy),
           double_bits(loaded_boat.vz), float_bits(loaded_boat.yaw),
           double_bits(loaded_boat.box_max_x - loaded_boat.box_min_x),
           double_bits(loaded_boat.box_max_y - loaded_boat.box_min_y),
           loaded_boat.boat_variant,
           double_bits(loaded_tnt->x - (double)(x + 22)),
           double_bits(loaded_tnt->y - (double)y),
           double_bits(loaded_tnt->z - (double)(z + 22)),
           double_bits(loaded_tnt->vx), double_bits(loaded_tnt->vy),
           double_bits(loaded_tnt->vz), float_bits(loaded_tnt->yaw),
           float_bits(loaded_tnt->pitch),
           float_bits(loaded_tnt->fall_distance), loaded_tnt->fuse,
           loaded_tnt->on_ground ? "true" : "false",
           loaded_tnt->no_gravity ? "true" : "false",
           loaded_tnt->fire_ticks, loaded_tnt->air,
           loaded_tnt->portal_cooldown,
           (unsigned long long)loaded_tnt->random_seed48,
           loaded_tnt->uuid_present
                && (loaded_tnt->uuid_most != source_tnt->uuid_most
                    || loaded_tnt->uuid_least != source_tnt->uuid_least)
               ? "true" : "false",
           double_bits(loaded_falling->x - (double)(x + 22)),
           double_bits(loaded_falling->y - (double)y),
           double_bits(loaded_falling->z - (double)(z + 22)),
           double_bits(loaded_falling->vx),
           double_bits(loaded_falling->vy),
           double_bits(loaded_falling->vz),
           float_bits(loaded_falling->yaw),
           float_bits(loaded_falling->pitch),
           float_bits(loaded_falling->fall_distance),
           loaded_falling->block, loaded_falling->meta,
           loaded_falling->fall_time,
           loaded_falling->should_drop_item ? "true" : "false",
           loaded_falling->hurt_entities ? "true" : "false",
           float_bits(loaded_falling->fall_hurt_amount),
           loaded_falling->fall_hurt_max, loaded_falling_tile_test,
           loaded_falling->no_gravity ? "true" : "false",
           loaded_falling->on_ground ? "true" : "false",
           loaded_falling->fire_ticks, loaded_falling->air,
           loaded_falling->portal_cooldown,
           (unsigned long long)loaded_falling->random_seed48,
           loaded_falling->uuid_present
                && (loaded_falling->uuid_most != source_falling->uuid_most
                    || loaded_falling->uuid_least
                        != source_falling->uuid_least)
               ? "true" : "false",
           double_bits(loaded_crystal->x - (double)(x + 22)),
           double_bits(loaded_crystal->y - (double)y),
           double_bits(loaded_crystal->z - (double)(z + 22)),
           double_bits(loaded_crystal->vx),
           double_bits(loaded_crystal->vy),
           double_bits(loaded_crystal->vz),
           float_bits(loaded_crystal->yaw),
           float_bits(loaded_crystal->pitch),
           float_bits(loaded_crystal->fall_distance),
           loaded_crystal->inner_rotation,
           loaded_crystal->show_bottom ? "true" : "false",
           loaded_crystal->has_beam ? "true" : "false",
           loaded_crystal->beam_x, loaded_crystal->beam_y,
           loaded_crystal->beam_z,
           loaded_crystal->no_gravity ? "true" : "false",
           loaded_crystal->on_ground ? "true" : "false",
           loaded_crystal->fire_ticks, loaded_crystal->air,
           loaded_crystal->portal_cooldown,
           (unsigned long long)loaded_crystal->random_seed48,
           loaded_crystal->uuid_present
                && (loaded_crystal->uuid_most != source_crystal->uuid_most
                    || loaded_crystal->uuid_least
                        != source_crystal->uuid_least)
               ? "true" : "false",
           double_bits(loaded_cart->x - (double)(x + 22)),
           double_bits(loaded_cart->y - (double)y),
           double_bits(loaded_cart->z - (double)(z + 22)),
           double_bits(loaded_cart->vx),
           double_bits(loaded_cart->vy),
           double_bits(loaded_cart->vz),
           float_bits(loaded_cart->yaw),
           float_bits(loaded_cart->pitch),
           float_bits(loaded_cart->fall_distance),
           loaded_cart->kind,
           loaded_cart->on_ground ? "true" : "false",
           loaded_cart->no_gravity ? "true" : "false",
           loaded_cart->fire_ticks,
           loaded_cart->air,
           loaded_cart->portal_cooldown,
           loaded_cart->custom_display ? "true" : "false",
           loaded_cart->display_block,
           loaded_cart->display_meta,
           loaded_cart->display_offset,
           loaded_cart->slots[4].item,
           loaded_cart->slots[4].count,
           (unsigned long long)loaded_cart->random_seed48,
           loaded_cart->uuid_present
                && (loaded_cart->uuid_most != source_cart->uuid_most
                    || loaded_cart->uuid_least != source_cart->uuid_least)
               ? "true" : "false",
           loaded_cart->reverse ? "true" : "false",
           loaded_cart->rolling_amplitude,
           loaded_cart->rolling_direction,
           float_bits(loaded_cart->damage),
           cart_variants_saved ? "true" : "false",
           cart_variants_loaded ? "true" : "false",
           loaded_cart_variants[1].slots[4].item,
           loaded_cart_variants[1].slots[4].count,
           loaded_cart_variants[2].fuel,
           double_bits(loaded_cart_variants[2].push_x),
           double_bits(loaded_cart_variants[2].push_z),
           loaded_cart_variants[3].tnt_fuse,
           loaded_cart_variants[5].hopper_enabled ? "true" : "false",
           loaded_cart_variants[5].transfer_cooldown,
           loaded_cart_variants[5].slots[2].item,
           loaded_cart_variants[5].slots[2].count,
           loaded_cart_variants[6].custom_display ? "true" : "false",
           loaded_cart_variants[6].display_block,
           loaded_cart_variants[6].display_meta,
           loaded_cart_variants[6].display_offset,
           loaded_cart_variants[0].uuid_most != INT64_C(0x9000)
                && loaded_cart_variants[1].uuid_most != INT64_C(0x9001)
                && loaded_cart_variants[2].uuid_most != INT64_C(0x9002)
                && loaded_cart_variants[3].uuid_most != INT64_C(0x9003)
                && loaded_cart_variants[4].uuid_most != INT64_C(0x9004)
                && loaded_cart_variants[5].uuid_most != INT64_C(0x9005)
                && loaded_cart_variants[6].uuid_most != INT64_C(0x9006)
               ? "true" : "false",
           integrity_load ? "true" : "false");
    for (int rz = 0, index = 0; rz < 2; ++rz)
        for (int rx = 0; rx < 3; ++rx, ++index) {
            if (index) putchar(',');
            printf("%d", packed_at(
                &runtime, x + 29 + rx, y, z + 29 + rz));
        }
    printf("],\"rising_powered\":%s,\"rising_state\":%d,"
           "\"falling_powered\":%s}\n",
           rising_powered ? "true" : "false", rising_state,
           redstone.powered ? "true" : "false");
    gm_runtime_destroy(&runtime);
    return 0;
fail:
    gm_runtime_destroy(&runtime);
    return 1;
}
