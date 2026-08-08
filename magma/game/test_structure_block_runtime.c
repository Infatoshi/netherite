#include "game/runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); goto fail; } } while (0)

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
        fprintf(stderr, "FAIL: %s\n", error);
        return 0;
    }
    runtime->randtick_enabled = 0;
    gm_runtime_set_pose(runtime, 8.5, 80.0, 6.5, 180.0F, 0.0F);
    return 1;
}

static GmRuntimeStructureBlock state(
        const char *name, int mode, int px, int py, int pz,
        int sx, int sy, int sz) {
    GmRuntimeStructureBlock value;
    memset(&value, 0, sizeof value);
    snprintf(value.name, sizeof value.name, "%s", name);
    snprintf(value.author, sizeof value.author, "Netherite");
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

static GmRuntimeStructureTemplate *template_named(
        GmRuntime *runtime, const char *name) {
    if (!runtime || !name) return NULL;
    for (int index = 0; index < runtime->structure_templates_cap; ++index) {
        GmRuntimeStructureTemplate *value =
            &runtime->structure_templates[index];
        if (value->active && strcmp(value->name, name) == 0)
            return value;
    }
    return NULL;
}

static int templates_equal(const GmRuntime *a, const GmRuntime *b) {
    if (!a || !b || a->structure_templates_cap
            != b->structure_templates_cap)
        return 0;
    for (int index = 0; index < a->structure_templates_cap; ++index) {
        GmRuntimeStructureTemplate left = a->structure_templates[index];
        GmRuntimeStructureTemplate right = b->structure_templates[index];
        left.tiles = right.tiles = NULL;
        left.entities = right.entities = NULL;
        if (memcmp(&left, &right, sizeof left) != 0
                || (left.tiles_cap && memcmp(
                    a->structure_templates[index].tiles,
                    b->structure_templates[index].tiles,
                    (size_t)left.tiles_cap
                        * sizeof *a->structure_templates[index].tiles) != 0))
            return 0;
        for (int entity = 0; entity < left.entities_cap; ++entity) {
            GmMobTemplateEntity le =
                a->structure_templates[index].entities[entity];
            GmMobTemplateEntity re =
                b->structure_templates[index].entities[entity];
            le.minecart_spawner_potentials = NULL;
            re.minecart_spawner_potentials = NULL;
            if (memcmp(&le, &re, sizeof le) != 0
                    || (le.minecart_spawner_potential_cap
                        && memcmp(
                            a->structure_templates[index].entities[entity]
                                .minecart_spawner_potentials,
                            b->structure_templates[index].entities[entity]
                                .minecart_spawner_potentials,
                            (size_t)le.minecart_spawner_potential_cap
                                * sizeof(GmSpawnerPotential)) != 0))
                return 0;
        }
    }
    return 1;
}

static int structure_template_capacity(void) {
    static GmRuntime runtime, restored;
    const char *checkpoint = ".tmp/structure_template_capacity.bin";
    GmRuntimeStructureBlock value;
    GmRuntimeStructureTemplate *tiles, *entities;
    int runtime_ready = 0, restored_ready = 0;
    int stage = 0;
    if (setenv("MAGMA_ITEM_SPAWN_LIMIT", "1024", 1) != 0
            || !init_flat(&runtime))
        goto fail;
    runtime_ready = 1;
    if (!init_flat(&restored)) goto fail;
    restored_ready = 1;
    stage = 1;

    if (!gm_runtime_set_block(&runtime, 100, 80, 100, 255, 0)) goto fail;
    stage = 11;
    value = state("capacity_tiles", GM_STRUCTURE_MODE_SAVE,
                  1, 0, 1, 32, 1, 9);
    for (int index = 0; index <= GM_STRUCTURE_TEMPLATE_TILES_MAX; ++index) {
        int x = 101 + index % 32;
        int z = 101 + index / 32;
        if (!gm_runtime_set_block(&runtime, x, 80, z, 25, 0)
                || !gm_runtime_note_block_set(
                    &runtime, 0, x, 80, z, index % 25, index & 1))
            goto fail;
    }
    stage = 12;
    if (!gm_runtime_structure_block_set(
            &runtime, 0, 100, 80, 100, &value)
            || !gm_runtime_structure_save(&runtime, 0, 100, 80, 100))
        goto fail;
    stage = 13;
    tiles = template_named(&runtime, "capacity_tiles");
    if (!tiles || tiles->tile_count != GM_STRUCTURE_TEMPLATE_TILES_MAX + 1
            || tiles->tiles_cap <= GM_STRUCTURE_TEMPLATE_TILES_MAX)
        goto fail;
    stage = 2;

    if (!gm_runtime_set_block(&runtime, 200, 80, 200, 255, 0)) goto fail;
    value = state("capacity_entities", GM_STRUCTURE_MODE_SAVE,
                  1, 0, 1, 32, 2, 9);
    value.ignore_entities = 0;
    for (int index = 0;
            index <= GM_STRUCTURE_TEMPLATE_ENTITIES_MAX; ++index) {
        double x = 201.25 + (double)(index % 32);
        double y = 80.1 + (double)(index & 1) * 0.5;
        double z = 201.25 + (double)(index / 32);
        if (!gm_runtime_spawn_item_fixture(
                &runtime, 20000 + index, x, y, z,
                0.0, 0.0, 0.0, 1, 1, 0, 0, 0, 1))
            goto fail;
    }
    if (!gm_runtime_structure_block_set(
            &runtime, 0, 200, 80, 200, &value)
            || !gm_runtime_structure_save(&runtime, 0, 200, 80, 200))
        goto fail;
    entities = template_named(&runtime, "capacity_entities");
    if (!entities
            || entities->entity_count
                != GM_STRUCTURE_TEMPLATE_ENTITIES_MAX + 1
            || entities->entities_cap
                <= GM_STRUCTURE_TEMPLATE_ENTITIES_MAX)
        goto fail;
    stage = 3;

    for (int index = 0;
            index <= GM_RUNTIME_STRUCTURE_TEMPLATES_MAX; ++index) {
        char name[GM_STRUCTURE_NAME_LENGTH];
        snprintf(name, sizeof name, "capacity_template_%03d", index);
        value = state(name, GM_STRUCTURE_MODE_SAVE, 0, 0, 0, 0, 0, 0);
        if (!gm_runtime_structure_block_set(
                &runtime, 0, 100, 80, 100, &value)
                || !gm_runtime_structure_save(
                    &runtime, 0, 100, 80, 100))
            goto fail;
    }
    if (gm_runtime_structure_template_count(&runtime)
            != GM_RUNTIME_STRUCTURE_TEMPLATES_MAX + 3
            || runtime.structure_templates_cap
                <= GM_RUNTIME_STRUCTURE_TEMPLATES_MAX)
        goto fail;
    stage = 31;

    value = state("capacity_controller", GM_STRUCTURE_MODE_DATA,
                  0, 1, 0, 0, 0, 0);
    for (int index = 0; index <= GM_RUNTIME_STRUCTURE_BLOCKS_MAX; ++index) {
        int x = -8 + index % 16;
        int y = 82 + index / 256;
        int z = -8 + (index / 16) % 16;
        if (!gm_runtime_set_block(&runtime, x, y, z, 255, 3)
                || !gm_runtime_set_block(&restored, x, y, z, 255, 3)
                || !gm_runtime_structure_block_set(
                    &runtime, 0, x, y, z, &value)) {
            stage = 31000 + index;
            goto fail;
        }
    }
    stage = 32;
    if (gm_runtime_structure_block_count(&runtime)
            != GM_RUNTIME_STRUCTURE_BLOCKS_MAX + 3
            || runtime.structure_blocks_cap
                <= GM_RUNTIME_STRUCTURE_BLOCKS_MAX) {
        fprintf(stderr, "Structure Block capacity diagnostic: count=%d cap=%d\n",
                gm_runtime_structure_block_count(&runtime),
                runtime.structure_blocks_cap);
        goto fail;
    }

    stage = 33;
    if (!gm_runtime_write_checkpoint(&runtime, checkpoint)
            || !gm_runtime_load_checkpoint(&restored, checkpoint)) {
        stage = 330;
        goto fail;
    }
    if (!(tiles = template_named(&restored, "capacity_tiles"))
            || tiles->tile_count != GM_STRUCTURE_TEMPLATE_TILES_MAX + 1
            || !(entities = template_named(
                &restored, "capacity_entities"))
            || entities->entity_count
                != GM_STRUCTURE_TEMPLATE_ENTITIES_MAX + 1
            || gm_runtime_structure_template_count(&restored)
                != GM_RUNTIME_STRUCTURE_TEMPLATES_MAX + 3
            || restored.structure_templates_cap
                <= GM_RUNTIME_STRUCTURE_TEMPLATES_MAX
            || gm_runtime_structure_block_count(&restored)
                != GM_RUNTIME_STRUCTURE_BLOCKS_MAX + 1
            || restored.structure_blocks_cap
                <= GM_RUNTIME_STRUCTURE_BLOCKS_MAX) {
        fprintf(stderr,
                "restored capacity diagnostic: templates=%d/%d blocks=%d/%d "
                "tiles=%d entities=%d\n",
                gm_runtime_structure_template_count(&restored),
                restored.structure_templates_cap,
                gm_runtime_structure_block_count(&restored),
                restored.structure_blocks_cap,
                tiles ? tiles->tile_count : -1,
                entities ? entities->entity_count : -1);
        goto fail;
    }
    stage = 4;
    unlink(checkpoint);
    gm_runtime_destroy(&restored);
    gm_runtime_destroy(&runtime);
    return 1;
fail:
    fprintf(stderr,
            "FAIL: growable Structure Template capacity stage %d\n", stage);
    unlink(checkpoint);
    if (restored_ready) gm_runtime_destroy(&restored);
    if (runtime_ready) gm_runtime_destroy(&runtime);
    return 0;
}

int main(int argc, char **argv) {
    /* GmRuntime owns fixed-capacity world/entity stores and is intentionally
     * large. Keep the two-fixture test out of the process stack as the live
     * state contract grows. */
    static GmRuntime source, restored;
    GmRuntimeStructureBlock value, got;
    const char *checkpoint = argc == 2
        ? argv[1] : "../.tmp/structure_block_checkpoint.bin";
    int source_ready = 0, restored_ready = 0;

    CHECK(init_flat(&source), "initialize Structure Block fixture");
    source_ready = 1;
    CHECK(init_flat(&restored), "initialize checkpoint destination");
    restored_ready = 1;

    CHECK(gm_runtime_set_block(&source, 8, 80, 8, 255, 0)
              && gm_runtime_structure_block_count(&source) == 1
              && gm_runtime_structure_block_get(&source, 0, &got),
          "placement creates the default TileEntityStructure");
    CHECK(got.mode == GM_STRUCTURE_MODE_DATA
              && got.pos_x == 0 && got.pos_y == 1 && got.pos_z == 0
              && got.size_x == 0 && got.size_y == 0 && got.size_z == 0
              && got.mirror == GM_STRUCTURE_MIRROR_NONE
              && got.rotation == GM_STRUCTURE_ROTATION_NONE
              && got.ignore_entities && !got.powered && !got.show_air
              && got.show_bounding_box && got.integrity == 1.0f
              && got.seed == 0,
          "constructor defaults match Java 1.11.2");

    value = state("bad.name?", GM_STRUCTURE_MODE_SAVE,
                  -99, 99, 2, -1, 99, 3);
    CHECK(gm_runtime_structure_block_set(
              &source, 0, 8, 80, 8, &value)
              && gm_runtime_structure_block_get(&source, 0, &got),
          "restore and clamp Structure Block state");
    CHECK(strcmp(got.name, "bad_name_") == 0
              && got.pos_x == -32 && got.pos_y == 32 && got.pos_z == 2
              && got.size_x == 0 && got.size_y == 32 && got.size_z == 3
              && gm_world_meta(source.world, 8, 80, 8)
                    == GM_STRUCTURE_MODE_SAVE,
          "NBT clamps, illegal-name replacement, and mode block state match");

    value = state("bounds", GM_STRUCTURE_MODE_SAVE, 0, 1, 0, 0, 0, 0);
    CHECK(gm_runtime_structure_block_set(
              &source, 0, 8, 80, 8, &value),
          "configure size-detection controller");
    CHECK(gm_runtime_set_block(&source, 5, 77, 5, 255, 2)
              && gm_runtime_set_block(&source, 12, 84, 13, 255, 2),
          "place bounding corners");
    value = state("bounds", GM_STRUCTURE_MODE_CORNER, 0, 1, 0, 0, 0, 0);
    CHECK(gm_runtime_structure_block_set(
              &source, 0, 5, 77, 5, &value)
              && gm_runtime_structure_block_set(
                  &source, 0, 12, 84, 13, &value)
              && gm_runtime_structure_detect_size(
                  &source, 0, 8, 80, 8)
              && gm_runtime_structure_block_get(&source, 0, &got),
          "detect matching named corner bounds");
    CHECK(got.pos_x == -2 && got.pos_y == -2 && got.pos_z == -2
              && got.size_x == 6 && got.size_y == 6 && got.size_z == 7,
          "detected position and size exclude the corner shell");

    value = state("parity_room", GM_STRUCTURE_MODE_SAVE,
                  1, 0, 1, 3, 1, 2);
    value.ignore_entities = 1;
    CHECK(gm_runtime_structure_block_set(
              &source, 0, 8, 80, 8, &value)
              && gm_runtime_set_block(&source, 9, 80, 9, 1, 1)
              && gm_runtime_set_block(&source, 10, 80, 9, 54, 2)
              && gm_runtime_chest_set_slot(
                  &source, 0, 10, 80, 9, 4, 264, 3, 0)
              && gm_runtime_set_block(&source, 11, 80, 9, 5, 2)
              && gm_runtime_set_block(&source, 9, 80, 10, 217, 0)
              && gm_runtime_set_block(&source, 10, 80, 10, 53, 0)
              && gm_runtime_set_block(&source, 11, 80, 10, 95, 11)
              && gm_runtime_structure_save(&source, 0, 8, 80, 8)
              && gm_runtime_structure_template_count(&source) == 1,
          "save full, tile, non-full, and ignored-void block categories");

    CHECK(gm_runtime_set_block(&source, 30, 80, 30, 255, 1),
          "place load controller");
    value = state("parity_room", GM_STRUCTURE_MODE_LOAD,
                  5, 0, 5, 0, 0, 0);
    value.mirror = GM_STRUCTURE_MIRROR_FRONT_BACK;
    value.rotation = GM_STRUCTURE_ROTATION_CW90;
    CHECK(gm_runtime_structure_block_set(
              &source, 0, 30, 80, 30, &value),
          "configure mirrored and rotated load");
    CHECK(!gm_runtime_structure_load(&source, 0, 30, 80, 30, 1)
              && gm_runtime_structure_block_get(&source, 3, &got)
              && got.size_x == 3 && got.size_y == 1 && got.size_z == 2,
          "GUI load first synchronizes a mismatched template size");
    CHECK(gm_runtime_set_block(&source, 35, 80, 34, 54, 5)
              && gm_runtime_chest_set_slot(
                  &source, 0, 35, 80, 34, 7, 3, 9, 0),
          "stage stale same-type tile data at the load destination");
    CHECK(gm_runtime_structure_load(&source, 0, 30, 80, 30, 1),
          "second GUI load places the template");
    CHECK(gm_world_block(source.world, 35, 80, 35) == 1
              && gm_world_meta(source.world, 35, 80, 35) == 1
              && gm_world_block(source.world, 35, 80, 34) == 54
              && gm_world_meta(source.world, 35, 80, 34) == 5
              && gm_world_block(source.world, 35, 80, 33) == 5
              && gm_world_meta(source.world, 35, 80, 33) == 2
              && gm_world_block(source.world, 34, 80, 35) == 0
              && gm_world_block(source.world, 34, 80, 34) == 53
              && gm_world_meta(source.world, 34, 80, 34) == 3
              && gm_world_block(source.world, 34, 80, 33) == 95,
          "mirror and rotation transform positions in Java template order");
    {
        const GmRuntimeChest *loaded = NULL;
        ICStack restored_slot, stale_slot;
        for (int index = 0; index < source.chests_cap; ++index)
            if (source.chests[index].active
                    && source.chests[index].wx == 35
                    && source.chests[index].wy == 80
                    && source.chests[index].wz == 34) {
                loaded = &source.chests[index];
                break;
            }
        restored_slot = loaded
            ? chest_live_get(&loaded->state, 4) : ic_empty();
        stale_slot = loaded
            ? chest_live_get(&loaded->state, 7) : ic_empty();
        CHECK(loaded
                  && restored_slot.item == 264
                  && restored_slot.count == 3
                  && isr_is_empty(&stale_slot),
              "Template tile NBT replaces stale chest data and restores slots");
    }

    value = got;
    value.pos_x = 7;
    value.pos_z = 7;
    value.mirror = GM_STRUCTURE_MIRROR_NONE;
    value.rotation = GM_STRUCTURE_ROTATION_NONE;
    value.integrity = 0.5f;
    value.seed = 12345;
    CHECK(gm_runtime_structure_block_set(
              &source, 0, 30, 80, 30, &value)
              && gm_runtime_structure_load(&source, 0, 30, 80, 30, 1),
          "seeded integrity load executes");

    {
        GmRuntimeStructureTemplate *entity_template;
        GmMobTemplateEntity loaded_xp, loaded_boat, loaded_sheep;
        GmLiveEnt *source_item = NULL, *loaded_item = NULL;
        JavaRandom expected_math, expected_generator, expected_item_random;
        float expected_hover;
        int sheep_slot, loaded_sheep_slot;
        int loaded_before;
        /* The preceding block-template case leaves a populated chest in the
         * destination volume. Retire it without drops before resetting the
         * entity constructor cursors, so this independent fixture does not
         * acquire a fifth EntityItem from chest breakBlock. */
        for (int index = 0; index < source.chests_cap; ++index)
            if (source.chests[index].active
                    && source.chests[index].wx == 35
                    && source.chests[index].wy == 80
                    && source.chests[index].wz == 34)
                for (int slot = 0; slot < CHEST_LIVE_SLOTS; ++slot)
                    chest_live_set(
                        &source.chests[index].state, slot, ic_empty());
        CHECK(gm_runtime_set_block(&source, 35, 80, 34, 0, 0),
              "retire prior template chest before entity fixture");
        value = state("entity_room", GM_STRUCTURE_MODE_SAVE,
                      6, 0, 6, 4, 4, 4);
        value.ignore_entities = 0;
        CHECK(gm_runtime_structure_block_set(
                  &source, 0, 8, 80, 8, &value)
                  && gm_runtime_spawn_mob_fixture(
                      &source, GM_MOB_SHEEP, 9101,
                      16.5, 80.0, 16.5,
                      0.125, 0.0, -0.25, 37.0F, 7.5F,
                      1, 0, 0, 0)
                  && gm_runtime_set_sheep_state(&source, 9101, 11, 1)
                  && gm_runtime_set_mob_growing_age(&source, 9101, -2400)
                  && gm_runtime_set_mob_fire_ticks(&source, 9101, 23)
                  && gm_runtime_set_mob_uuid(
                      &source, 9101, INT64_C(0x1234), INT64_C(0x5678))
                  && gm_runtime_spawn_xp_fixture(
                      &source, 15.5, 81.25, 15.5,
                      -0.1, 0.2, 0.3, 17, 9102,
                      45, 7, 9, 12)
                  && gm_runtime_spawn_item_state_fixture(
                      &source, 9105, 15.25, 80.75, 16.25,
                      0.4, -0.3, 0.2, -25.0F, 1.75F,
                      403, 1, 0, -120, -7, 3, 7000,
                      1, 1, 37, 19, 1, 0,
                      UINT64_C(0x123456789abc))
                  && gm_runtime_spawn_boat_fixture(
                      &source, 9104, 17.0, 80.0, 15.5, 0.0F)
                  && gm_runtime_spawn_mob_fixture(
                      &source, GM_MOB_SHEEP, 9103,
                      19.5, 80.0, 16.5,
                      0.0, 0.0, 0.0, 0.0F, 8.0F,
                      1, 0, 0, 0),
              "Structure save captures represented non-player entities");
        for (int slot = 0; slot < GM_LIVE_MAX; ++slot)
            if (source.entities.ents[slot].active
                    && source.entities.ents[slot].type == 0
                    && source.entities.ents[slot].eid == 9105) {
                source_item = &source.entities.ents[slot];
                break;
            }
        CHECK(source_item != NULL, "locate full EntityItem fixture");
        source_item->pitch = 12.5F;
        source_item->fall_distance = 2.25F;
        source_item->air = 123;
        source_item->portal_cooldown = 9;
        source_item->repair_cost = 4;
        source_item->custom_name = 1;
        source_item->n_enchants = 1;
        source_item->ench_id[0] = 34;
        source_item->ench_lvl[0] = 2;
        CHECK(gm_runtime_set_item_entity_uuid(
                  &source, 9105, INT64_C(0x9876), INT64_C(0x5432))
                  && gm_runtime_structure_save(&source, 0, 8, 80, 8),
              "save complete EntityItem NBT payload");
        entity_template = template_named(&source, "entity_room");
        CHECK(entity_template && entity_template->entity_count == 4
                  && entity_template->entities[0].kind
                    == GM_TEMPLATE_ENTITY_XP
                  && entity_template->entities[1].kind
                    == GM_TEMPLATE_ENTITY_ITEM
                  && entity_template->entities[2].kind
                    == GM_TEMPLATE_ENTITY_BOAT
                  && entity_template->entities[3].kind
                    == GM_TEMPLATE_ENTITY_LIVING
                  && entity_template->entities[3].type == GM_MOB_SHEEP,
              "entity query preserves Java chunk/section/list order and AABB selection");
        CHECK(entity_template->entities[0].x == 1.5
                  && entity_template->entities[0].y == 1.25
                  && entity_template->entities[0].z == 1.5
                  && entity_template->entities[1].x == 1.25
                  && entity_template->entities[1].y == 0.75
                  && entity_template->entities[1].z == 2.25
                  && entity_template->entities[2].x == 3.0
                  && entity_template->entities[2].y == 0.0
                  && entity_template->entities[2].z == 1.5
                  && entity_template->entities[3].x == 2.5
                  && entity_template->entities[3].y == 0.0
                  && entity_template->entities[3].z == 2.5,
              "saved entity Pos is an exact template-relative double");

        value.ignore_entities = 1;
        CHECK(gm_runtime_structure_block_set(
                  &source, 0, 8, 80, 8, &value)
                  && gm_runtime_structure_save(&source, 0, 8, 80, 8)
                  && entity_template->entity_count == 0,
              "ignore-entities save clears the prior Template entity list");
        value.ignore_entities = 0;
        CHECK(gm_runtime_structure_block_set(
                  &source, 0, 8, 80, 8, &value)
                  && gm_runtime_structure_save(&source, 0, 8, 80, 8)
                  && entity_template->entity_count == 4,
              "entity capture can be re-enabled without stale rows");

        value = state("entity_room", GM_STRUCTURE_MODE_LOAD,
                      5, 0, 5, 4, 4, 4);
        value.ignore_entities = 0;
        value.mirror = GM_STRUCTURE_MIRROR_FRONT_BACK;
        value.rotation = GM_STRUCTURE_ROTATION_CW90;
        jrand_set_seed48(&expected_math, UINT64_C(0x23456789abcd));
        expected_hover = (float)(
            jrand_double(&expected_math) * (MC_PI * 2.0));
        for (int draw = 0; draw < 3; ++draw)
            (void)jrand_double(&expected_math);
        jrand_set_seed48(&expected_generator, UINT64_C(0x3456789abcde));
        (void)jrand_long(&expected_generator); /* XP constructor */
        jrand_set(&expected_item_random, jrand_long(&expected_generator));
        (void)jrand_long(&expected_generator); /* boat constructor */
        (void)jrand_long(&expected_generator); /* sheep constructor */
        CHECK(gm_runtime_structure_block_set(
                  &source, 0, 30, 80, 30, &value)
                  && gm_runtime_set_entity_id_cursor(&source, 9200)
                  && gm_runtime_set_math_random_seed48(
                      &source, UINT64_C(0x23456789abcd))
                  && gm_runtime_set_entity_seed_generator_seed48(
                      &source, UINT64_C(0x3456789abcde)),
              "configure transformed entity placement");
        loaded_before = source.loaded_entity_order_count;
        int loaded_ok = gm_runtime_structure_load(
            &source, 0, 30, 80, 30, 1);
        if (!loaded_ok
                || source.loaded_entity_order_count != loaded_before + 4
                || source.next_entity_id != 9204)
        {
            fprintf(stderr,
                    "structure entity load diagnostics: ok=%d before=%d "
                    "after=%d next=%d\n",
                    loaded_ok, loaded_before,
                    source.loaded_entity_order_count, source.next_entity_id);
            for (int at = loaded_before;
                    at < source.loaded_entity_order_count; ++at)
                fprintf(stderr, " order[%d]=%d", at,
                        source.loaded_entity_order[at]);
            fputc('\n', stderr);
        }
        CHECK(loaded_ok
                  && source.loaded_entity_order_count == loaded_before + 4
                  && source.loaded_entity_order[loaded_before] == 9200
                  && source.loaded_entity_order[loaded_before + 1] == 9201
                  && source.loaded_entity_order[loaded_before + 2] == 9202
                  && source.loaded_entity_order[loaded_before + 3] == 9203
                  && source.next_entity_id == 9204,
              "load appends fresh entities in Template order with fresh EIDs");
        CHECK(gm_mobs_template_entity_capture(
                  &source.mobs, 9200, &loaded_xp)
                  && gm_mobs_template_entity_capture(
                      &source.mobs, 9202, &loaded_boat)
                  && gm_mobs_template_entity_capture(
                      &source.mobs, 9203, &loaded_sheep),
              "loaded Structure entities are represented in the live stores");
        for (int slot = 0; slot < GM_LIVE_MAX; ++slot)
            if (source.entities.ents[slot].active
                    && source.entities.ents[slot].type == 0
                    && source.entities.ents[slot].eid == 9201) {
                loaded_item = &source.entities.ents[slot];
                break;
            }
        CHECK(loaded_item != NULL
                  && loaded_item->x == 33.75
                  && loaded_item->y == 80.75
                  && loaded_item->z == 34.75
                  && loaded_item->mx == 0.4
                  && loaded_item->my == -0.3
                  && loaded_item->mz == 0.2
                  && loaded_item->yaw == 115.0F
                  && loaded_item->pitch == 12.5F
                  && loaded_item->fall_distance == 2.25F
                  && loaded_item->item == 403
                  && loaded_item->count == 1
                  && loaded_item->repair_cost == 4
                  && loaded_item->custom_name == 1
                  && loaded_item->n_enchants == 1
                  && loaded_item->ench_id[0] == 34
                  && loaded_item->ench_lvl[0] == 2
                  && loaded_item->health == 3
                  && loaded_item->age == -120
                  && loaded_item->pickup_delay == -7
                  && loaded_item->lifespan == 7000
                  && loaded_item->on_ground
                  && loaded_item->no_gravity
                  && loaded_item->fire == 19
                  && loaded_item->air == 123
                  && loaded_item->portal_cooldown == 9
                  && loaded_item->ticks_existed == 0
                  && loaded_item->first_update
                  && !loaded_item->in_water
                  && loaded_item->hover_start == expected_hover
                  && loaded_item->random_seed48
                    == expected_item_random.seed
                  && source.math_random_seed48 == expected_math.seed
                  && source.entity_seed_generator_seed48
                    == expected_generator.seed,
              "EntityItem NBT and fresh constructor cursors survive Template load");
        CHECK(loaded_xp.kind == GM_TEMPLATE_ENTITY_XP
                  && loaded_xp.x == 34.5
                  && loaded_xp.y == 81.25
                  && loaded_xp.z == 34.5
                  && loaded_xp.vx == -0.1
                  && loaded_xp.vy == 0.2
                  && loaded_xp.vz == 0.3
                  && loaded_xp.xp_value == 17
                  && loaded_xp.xp_health == 5
                  && loaded_xp.xp_age == 45
                  && loaded_xp.xp_pickup_delay == 0
                  && loaded_xp.xp_color == 0
                  && loaded_xp.xp_target_color == 0
                  && loaded_xp.box_max_x - loaded_xp.box_min_x == 0.25
                  && loaded_xp.box_max_y - loaded_xp.box_min_y == 0.25,
              "XP NBT persists while position alone receives Template transforms");
        CHECK(loaded_boat.kind == GM_TEMPLATE_ENTITY_BOAT
                  && loaded_boat.type == GM_ENTITY_BOAT
                  && loaded_boat.x == 34.5
                  && loaded_boat.y == 80.0
                  && loaded_boat.z == 33.0
                  && loaded_boat.yaw == 90.0F
                  && loaded_boat.boat_variant == 0,
              "boat Type and base entity state use the same Java transforms");
        CHECK(loaded_sheep.kind == GM_TEMPLATE_ENTITY_LIVING
                  && loaded_sheep.type == GM_MOB_SHEEP
                  && loaded_sheep.x == 33.5
                  && loaded_sheep.y == 80.0
                  && loaded_sheep.z == 33.5
                  && loaded_sheep.vx == 0.125
                  && loaded_sheep.vz == -0.25
                  && loaded_sheep.yaw == 53.0F
                  && loaded_sheep.health == 7.5F
                  && loaded_sheep.fire_ticks == 23
                  && loaded_sheep.growing_age == -2400
                  && (loaded_sheep.sheep_data & 15) == 11
                  && (loaded_sheep.sheep_data & 16) != 0,
              "living NBT, motion, and Java mirror/rotation yaw survive load");
        sheep_slot = gm_mobs_find_slot_by_eid(&source.mobs, 9101);
        loaded_sheep_slot = gm_mobs_find_slot_by_eid(&source.mobs, 9203);
        CHECK(sheep_slot > 0 && loaded_sheep_slot > 0
                  && source.mobs.entity_uuid_present[loaded_sheep_slot]
                  && (source.mobs.entity_uuid_most[loaded_sheep_slot]
                        != source.mobs.entity_uuid_most[sheep_slot]
                      || source.mobs.entity_uuid_least[loaded_sheep_slot]
                        != source.mobs.entity_uuid_least[sheep_slot]),
              "Template load replaces serialized UUIDs like UUID.randomUUID");
        CHECK(loaded_item->uuid_present
                  && (loaded_item->uuid_most != source_item->uuid_most
                      || loaded_item->uuid_least != source_item->uuid_least),
              "EntityItem also receives the Template's fresh UUID");

        value.ignore_entities = 1;
        CHECK(gm_runtime_structure_block_set(
                  &source, 0, 30, 80, 30, &value),
              "toggle load-side ignore entities");
        loaded_before = source.loaded_entity_order_count;
        CHECK(gm_runtime_structure_load(&source, 0, 30, 80, 30, 1)
                  && source.loaded_entity_order_count == loaded_before,
              "ignore-entities load places blocks without duplicating entities");
    }

    {
        static const unsigned char tile_nbt[] = {
            10, 0, 0, 3, 0, 4, 'T', 'e', 's', 't',
            0, 0, 0, 7, 0
        };
        static GmRuntimeStructureTemplate *entity_template;
        static GmRuntimePrimedTnt *loaded_tnt;
        static GmRuntimeFallingBlock *loaded_falling;
        static GmRuntimeEndCrystal *loaded_crystal;
        static GmRuntimeMinecart *source_cart;
        static GmRuntimeMinecart *loaded_cart;
        static JavaRandom expected_generator;
        static JavaRandom expected_falling;
        static JavaRandom expected_crystal;
        static JavaRandom expected_cart;
        static int tile_tag;
        static int loaded_before;
        static int expected_inner;
        tile_tag = gm_runtime_stack_tag_intern(
            &source, tile_nbt, sizeof tile_nbt);
        CHECK(tile_tag > 0,
              "create cold entity-family Structure fixture");
        value = state("entity_cold", GM_STRUCTURE_MODE_SAVE,
                      6, 20, 6, 4, 4, 4);
        value.ignore_entities = 0;
        CHECK(gm_runtime_structure_block_set(
                  &source, 0, 8, 80, 8, &value)
                  && gm_runtime_spawn_primed_tnt_fixture(
                      &source, 9250, 15.5, 100.5, 15.5,
                      0.125, -0.25, 0.375, 61)
                  && gm_runtime_spawn_falling_fixture(
                      &source, 9251, 145, 8, 37,
                      16.25, 101.0, 15.75,
                      -0.2, 0.3, 0.4, 1, 0)
                  && gm_runtime_spawn_end_crystal_fixture(
                      &source, 9252, 17.0, 101.25, 16.5,
                      12345, 0, 1, 7, 120, -9)
                  && gm_runtime_spawn_minecart_fixture(
                      &source, GM_MINECART_CHEST, 9253,
                      16.5, 100.75, 16.25,
                      0.2, -0.1, 0.3, -44.0F),
              "spawn compact Structure entity families");
        for (int slot = 0; slot < source.primed_tnt_cap; ++slot)
            if (source.primed_tnt[slot].active
                    && source.primed_tnt[slot].eid == 9250) {
                GmRuntimePrimedTnt *tnt = &source.primed_tnt[slot];
                tnt->yaw = -33.0F; tnt->pitch = 14.0F;
                tnt->fall_distance = 1.25F;
                tnt->on_ground = 1; tnt->no_gravity = 1;
                tnt->air = 122; tnt->fire_ticks = 9;
                tnt->portal_cooldown = 8;
            }
        for (int slot = 0; slot < GM_RUNTIME_FALLING_BLOCKS; ++slot)
            if (source.falling_blocks[slot].active
                    && source.falling_blocks[slot].eid == 9251) {
                GmRuntimeFallingBlock *falling =
                    &source.falling_blocks[slot];
                falling->yaw = 17.0F; falling->pitch = -4.0F;
                falling->fall_distance = 6.5F;
                falling->air = 121; falling->fire_ticks = 7;
                falling->portal_cooldown = 6;
                falling->should_drop_item = 0;
                falling->hurt_entities = 1;
                falling->fall_hurt_amount = 3.5F;
                falling->fall_hurt_max = 27;
                falling->tile_entity_tag_id = tile_tag;
            }
        for (int slot = 0; slot < GM_RUNTIME_END_CRYSTALS; ++slot)
            if (source.end_crystals[slot].active
                    && source.end_crystals[slot].eid == 9252) {
                GmRuntimeEndCrystal *crystal = &source.end_crystals[slot];
                crystal->vx = 0.01; crystal->vy = -0.02;
                crystal->vz = 0.03;
                crystal->yaw = 22.0F; crystal->pitch = -11.0F;
                crystal->fall_distance = 2.5F;
                crystal->on_ground = 1; crystal->no_gravity = 1;
                crystal->air = 120; crystal->fire_ticks = 5;
                crystal->portal_cooldown = 4;
            }
        for (int slot = 0; slot < GM_RUNTIME_MINECARTS; ++slot)
            if (source.minecarts[slot].active
                    && source.minecarts[slot].eid == 9253) {
                source_cart = &source.minecarts[slot];
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
            }
        CHECK(gm_runtime_set_transient_entity_uuid(
                  &source, 9250, INT64_C(0x1111), INT64_C(0x2222))
                  && gm_runtime_set_transient_entity_uuid(
                      &source, 9251, INT64_C(0x3333), INT64_C(0x4444))
                  && gm_runtime_set_transient_entity_uuid(
                      &source, 9252, INT64_C(0x5555), INT64_C(0x6666))
                  && source_cart
                  && gm_runtime_minecart_set_uuid(
                      &source, 9253, INT64_C(0x7777), INT64_C(0x8888))
                  && gm_runtime_minecart_set_slot(
                      &source, 9253, 4, 264, 3, 0)
                  && gm_runtime_structure_save(&source, 0, 8, 80, 8),
              "save persistent NBT for compact entity families");
        entity_template = template_named(&source, "entity_cold");
        CHECK(entity_template && entity_template->entity_count == 4
                  && entity_template->entities[0].kind
                    == GM_TEMPLATE_ENTITY_PRIMED_TNT
                  && entity_template->entities[1].kind
                    == GM_TEMPLATE_ENTITY_FALLING_BLOCK
                  && entity_template->entities[2].kind
                    == GM_TEMPLATE_ENTITY_END_CRYSTAL
                  && entity_template->entities[3].kind
                    == GM_TEMPLATE_ENTITY_MINECART,
              "Structure capture retains compact entity families in query order");

        value = state("entity_cold", GM_STRUCTURE_MODE_LOAD,
                      5, 20, 5, 4, 4, 4);
        value.ignore_entities = 0;
        value.mirror = GM_STRUCTURE_MIRROR_FRONT_BACK;
        value.rotation = GM_STRUCTURE_ROTATION_CW90;
        jrand_set_seed48(&expected_generator, UINT64_C(0x123456789abc));
        (void)jrand_long(&expected_generator);
        jrand_set(&expected_falling, jrand_long(&expected_generator));
        jrand_set(&expected_crystal, jrand_long(&expected_generator));
        expected_inner = jrand_int_bound(&expected_crystal, 100000);
        jrand_set(&expected_cart, jrand_long(&expected_generator));
        CHECK(gm_runtime_structure_block_set(
                  &source, 0, 30, 80, 30, &value)
                  && gm_runtime_set_entity_id_cursor(&source, 9300)
                  && gm_runtime_set_entity_seed_generator_seed48(
                      &source, UINT64_C(0x123456789abc)),
              "configure compact-entity Structure placement");
        loaded_before = source.loaded_entity_order_count;
        CHECK(gm_runtime_structure_load(&source, 0, 30, 80, 30, 1)
                  && source.loaded_entity_order_count == loaded_before + 4
                  && source.loaded_entity_order[loaded_before] == 9300
                  && source.loaded_entity_order[loaded_before + 1] == 9301
                  && source.loaded_entity_order[loaded_before + 2] == 9302
                  && source.loaded_entity_order[loaded_before + 3] == 9303,
              "load compact entities with fresh IDs in Template order");
        for (int slot = 0; slot < source.primed_tnt_cap; ++slot)
            if (source.primed_tnt[slot].active
                    && source.primed_tnt[slot].eid == 9300)
                loaded_tnt = &source.primed_tnt[slot];
        for (int slot = 0; slot < GM_RUNTIME_FALLING_BLOCKS; ++slot)
            if (source.falling_blocks[slot].active
                    && source.falling_blocks[slot].eid == 9301)
                loaded_falling = &source.falling_blocks[slot];
        for (int slot = 0; slot < GM_RUNTIME_END_CRYSTALS; ++slot)
            if (source.end_crystals[slot].active
                    && source.end_crystals[slot].eid == 9302)
                loaded_crystal = &source.end_crystals[slot];
        for (int slot = 0; slot < GM_RUNTIME_MINECARTS; ++slot)
            if (source.minecarts[slot].active
                    && source.minecarts[slot].eid == 9303)
                loaded_cart = &source.minecarts[slot];
        CHECK(loaded_tnt && loaded_tnt->x == 34.5
                  && loaded_tnt->y == 100.5
                  && loaded_tnt->z == 34.5
                  && loaded_tnt->vx == 0.125
                  && loaded_tnt->vy == -0.25
                  && loaded_tnt->vz == 0.375
                  && loaded_tnt->yaw == 123.0F
                  && loaded_tnt->pitch == 14.0F
                  && loaded_tnt->fuse == 61
                  && loaded_tnt->fall_distance == 1.25F
                  && loaded_tnt->on_ground && loaded_tnt->no_gravity
                  && loaded_tnt->air == 122
                  && loaded_tnt->fire_ticks == 9
                  && loaded_tnt->portal_cooldown == 8
                  && loaded_tnt->uuid_present
                  && (loaded_tnt->uuid_most != INT64_C(0x1111)
                      || loaded_tnt->uuid_least != INT64_C(0x2222)),
              "primed TNT base state, Fuse, transform, and fresh UUID persist");
        CHECK(loaded_falling && loaded_falling->x == 34.25
                  && loaded_falling->y == 101.0
                  && loaded_falling->z == 33.75
                  && loaded_falling->vx == -0.2
                  && loaded_falling->vy == 0.3
                  && loaded_falling->vz == 0.4
                  && loaded_falling->yaw == 73.0F
                  && loaded_falling->pitch == -4.0F
                  && loaded_falling->block == 145
                  && loaded_falling->meta == 8
                  && loaded_falling->fall_time == 37
                  && !loaded_falling->should_drop_item
                  && loaded_falling->hurt_entities
                  && loaded_falling->fall_hurt_amount == 3.5F
                  && loaded_falling->fall_hurt_max == 27
                  && loaded_falling->tile_entity_tag_id == tile_tag
                  && loaded_falling->random_seed48
                    == expected_falling.seed,
              "falling-block NBT and fresh constructor random persist");
        CHECK(loaded_crystal && loaded_crystal->x == 33.5
                  && loaded_crystal->y == 101.25
                  && loaded_crystal->z == 33.0
                  && loaded_crystal->vx == 0.01
                  && loaded_crystal->vy == -0.02
                  && loaded_crystal->vz == 0.03
                  && loaded_crystal->yaw == 68.0F
                  && loaded_crystal->pitch == -11.0F
                  && loaded_crystal->inner_rotation == expected_inner
                  && !loaded_crystal->show_bottom
                  && loaded_crystal->has_beam
                  && loaded_crystal->beam_x == 7
                  && loaded_crystal->beam_y == 120
                  && loaded_crystal->beam_z == -9
                  && loaded_crystal->on_ground
                  && loaded_crystal->no_gravity
                  && loaded_crystal->air == 120
                  && loaded_crystal->fire_ticks == 5
                  && loaded_crystal->portal_cooldown == 4,
              "End crystal NBT and constructor animation phase persist");
        CHECK(loaded_cart && loaded_cart->kind == GM_MINECART_CHEST
                  && loaded_cart->x == 33.75
                  && loaded_cart->y == 100.75
                  && loaded_cart->z == 33.5
                  && loaded_cart->vx == 0.2
                  && loaded_cart->vy == -0.1
                  && loaded_cart->vz == 0.3
                  && loaded_cart->yaw == 134.0F
                  && loaded_cart->pitch == 6.0F
                  && loaded_cart->fall_distance == 1.75F
                  && loaded_cart->on_ground
                  && loaded_cart->air == 119
                  && loaded_cart->fire_ticks == 3
                  && loaded_cart->portal_cooldown == 2
                  && loaded_cart->no_gravity
                  && loaded_cart->custom_display
                  && loaded_cart->display_block == 1
                  && loaded_cart->display_meta == 3
                  && loaded_cart->display_offset == 8
                  && loaded_cart->slots[4].item == 264
                  && loaded_cart->slots[4].count == 3
                  && loaded_cart->random_seed48 == expected_cart.seed
                  && loaded_cart->uuid_present
                  && (loaded_cart->uuid_most != INT64_C(0x7777)
                      || loaded_cart->uuid_least != INT64_C(0x8888)),
              "minecart base, display, inventory, RNG, transform, and UUID persist");
        CHECK(source.entity_seed_generator_seed48
                  == expected_generator.seed,
              "all compact entity constructors advance SeedHelper exactly");
    }

    CHECK(gm_runtime_set_block(&source, 45, 80, 45, 255, 0),
          "place GUI controller");
    value = state("gui_form", GM_STRUCTURE_MODE_SAVE,
                  1, 2, 3, 4, 5, 6);
    CHECK(gm_runtime_structure_block_set(
              &source, 0, 45, 80, 45, &value)
              && gm_runtime_structure_gui_open(
                  &source, 0, 45, 80, 45),
          "open GuiEditStructure with a private client form");
    CHECK(source.container == 12 && source.structure_gui.active
              && source.structure_gui.focus == GM_STRUCTURE_GUI_NAME
              && strcmp(source.structure_gui.value.name, "gui_form") == 0
              && strcmp(source.structure_gui.pos_x, "1") == 0
              && strcmp(source.structure_gui.size_z, "6") == 0
              && strcmp(source.structure_gui.integrity, "1.0") == 0,
          "GUI form initializes Java text and focus defaults");
    CHECK(gm_runtime_structure_gui_text(
              &source, "/ui", 3, 0, 0)
              && strcmp(source.structure_gui.value.name, "gui_formui") == 0
              && gm_runtime_structure_block_get(&source, 4, &got)
              && strcmp(got.name, "gui_form") == 0,
          "illegal name input is rejected and unsent text stays client-local");
    {
        int wrote = gm_runtime_write_checkpoint(&source, checkpoint);
        int loaded = wrote
            ? gm_runtime_load_checkpoint(&restored, checkpoint) : 0;
        if (!wrote || !loaded || restored.container != 12
                || memcmp(&restored.structure_gui, &source.structure_gui,
                          sizeof source.structure_gui) != 0)
            fprintf(stderr,
                    "Structure form checkpoint diagnostic: write=%d load=%d "
                    "container=%d gui_equal=%d\n",
                    wrote, loaded, restored.container,
                    memcmp(&restored.structure_gui, &source.structure_gui,
                           sizeof source.structure_gui) == 0);
        CHECK(wrote && loaded && restored.container == 12
                  && memcmp(&restored.structure_gui, &source.structure_gui,
                            sizeof source.structure_gui) == 0,
              "native checkpoint continues an open Structure form exactly");
    }
    gm_runtime_close_open_container(&restored);
    CHECK(gm_runtime_structure_gui_button(&source, 1)
              && source.container == 0
              && !source.structure_gui.active
              && gm_runtime_structure_block_get(&source, 4, &got)
              && strcmp(got.name, "gui_form") == 0,
          "Cancel discards the private form");

    CHECK(gm_runtime_structure_gui_open(&source, 0, 45, 80, 45)
              && gm_runtime_structure_gui_text(&source, "_done", 5, 0, 0)
              && gm_runtime_structure_gui_focus(
                  &source, GM_STRUCTURE_GUI_POS_X)
              && gm_runtime_structure_gui_text(&source, "99", 2, 1, 0)
              && gm_runtime_structure_gui_button(&source, 20)
              && gm_runtime_structure_gui_button(&source, 18)
              && gm_runtime_structure_gui_button(&source, 21)
              && gm_runtime_structure_gui_button(&source, 12)
              && gm_runtime_structure_gui_button(&source, 23)
              && gm_runtime_structure_gui_button(&source, 0)
              && gm_runtime_structure_block_get(&source, 4, &got),
          "edit every local control family and submit Done");
    CHECK(strcmp(got.name, "gui_form_done") == 0
              && got.pos_x == 32
              && got.mode == GM_STRUCTURE_MODE_LOAD
              && got.mirror == GM_STRUCTURE_MIRROR_LEFT_RIGHT
              && got.rotation == GM_STRUCTURE_ROTATION_CW90
              && !got.ignore_entities && !got.show_bounding_box
              && source.container == 0 && !source.structure_gui.active,
          "Done parses/clamps fields and commits toggles atomically");

    {
        static const unsigned char empty_nbt[4] = {10, 0, 0, 0};
        GmRuntimeStructureTemplate *spawner_template;
        value = state("spawner_potential_room", GM_STRUCTURE_MODE_SAVE,
                      1, 0, 1, 4, 4, 4);
        value.ignore_entities = 0;
        CHECK(gm_runtime_set_block(&source, 60, 80, 60, 255, 0)
                  && gm_runtime_structure_block_set(
                      &source, 0, 60, 80, 60, &value)
                  && gm_runtime_spawn_minecart_fixture(
                      &source, GM_MINECART_SPAWNER, 9900,
                      62.5, 80.0, 62.5, 0.0, 0.0, 0.0, 0.0F),
              "create spawner-potential Structure fixture");
        for (int index = 0; index < 17; ++index)
            CHECK(gm_runtime_minecart_add_spawner_potential(
                      &source, 9900,
                      index & 1 ? GM_MOB_SKELETON : GM_MOB_PIG,
                      index + 2, empty_nbt, sizeof empty_nbt, 1),
                  "Structure spawner potentials grow beyond former 16-row limit");
        CHECK(gm_runtime_structure_save(&source, 0, 60, 80, 60)
                  && (spawner_template = template_named(
                          &source, "spawner_potential_room"))
                  && spawner_template->entity_count == 1
                  && spawner_template->entities[0]
                      .minecart_spawner_potential_count == 18
                  && spawner_template->entities[0]
                      .minecart_spawner_potential_cap > 16,
              "Structure captures all grown spawner potentials");
    }

    CHECK(gm_runtime_write_checkpoint(&source, checkpoint)
              && gm_runtime_load_checkpoint(&restored, checkpoint)
              && restored.structure_blocks_cap
                    == source.structure_blocks_cap
              && restored.structure_templates_cap
                    == source.structure_templates_cap
              && memcmp(restored.structure_blocks, source.structure_blocks,
                    (size_t)source.structure_blocks_cap
                        * sizeof source.structure_blocks[0]) == 0
              && templates_equal(&restored, &source),
              "native checkpoint preserves tile and TemplateManager state exactly");
    {
        GmRuntimeStructureTemplate *spawner_template = template_named(
            &restored, "spawner_potential_room");
        GmRuntimeMinecart *spawned = NULL;
        value = state("spawner_potential_room", GM_STRUCTURE_MODE_LOAD,
                      1, 0, 1, 4, 4, 4);
        value.ignore_entities = 0;
        CHECK(spawner_template
                  && spawner_template->entities[0]
                      .minecart_spawner_potentials[17].weight == 18
                  && gm_runtime_set_block(&restored, 70, 80, 70, 255, 0)
                  && gm_runtime_structure_block_set(
                      &restored, 0, 70, 80, 70, &value)
                  && gm_runtime_set_entity_id_cursor(&restored, 9950)
                  && gm_runtime_structure_load(&restored, 0, 70, 80, 70, 1),
              "checkpoint restores grown Structure spawner potentials");
        for (int index = 0; index < restored.minecarts_cap; ++index)
            if (restored.minecarts[index].active
                    && restored.minecarts[index].eid == 9950)
                spawned = &restored.minecarts[index];
        CHECK(spawned && spawned->spawner_potential_count == 18
                  && spawned->spawner_potentials[17].weight == 18,
              "Structure placement deep-copies grown spawner potentials");
    }
    {
        int saved_tile_count = source.structure_templates[0].tile_count;
        source.structure_templates[0].tile_count =
            source.structure_templates[0].tiles_cap + 1;
        CHECK(!gm_runtime_write_checkpoint(&source, checkpoint),
              "checkpoint rejects an invalid Structure Template tile count");
        source.structure_templates[0].tile_count = saved_tile_count;
    }
    {
        GmRuntimeStructureTemplate *entity_template =
            template_named(&source, "entity_room");
        int saved_entity_count = entity_template
            ? entity_template->entity_count : 0;
        CHECK(entity_template != NULL,
              "entity Template remains in TemplateManager for checkpoint tests");
        entity_template->entity_count = entity_template->entities_cap + 1;
        CHECK(!gm_runtime_write_checkpoint(&source, checkpoint),
              "checkpoint rejects an invalid Structure Template entity count");
        entity_template->entity_count = saved_entity_count;
    }
    {
        int saved_container = source.container;
        source.container = 12;
        CHECK(!gm_runtime_write_checkpoint(&source, checkpoint),
              "checkpoint rejects an orphaned Structure edit container");
        source.container = saved_container;
    }

    CHECK(gm_runtime_set_block(&source, 40, 80, 40, 255, 1),
          "place redstone load controller");
    value = state("parity_room", GM_STRUCTURE_MODE_LOAD,
                  2, 0, 2, 3, 1, 2);
    CHECK(gm_runtime_structure_block_set(
              &source, 0, 40, 80, 40, &value)
              && gm_runtime_set_block(&source, 39, 80, 40, 152, 0)
              && gm_runtime_structure_block_get(&source, 6, &got)
              && got.powered
              && gm_world_block(source.world, 42, 80, 42) == 1,
          "redstone rising edge triggers LOAD and latches powered");
    CHECK(gm_runtime_set_block(&source, 39, 80, 40, 0, 0)
              && gm_runtime_structure_block_get(&source, 6, &got)
              && !got.powered,
          "redstone falling edge clears the powered latch");

    CHECK(gm_runtime_set_block(&source, 8, 80, 8, 0, 0)
              && gm_runtime_structure_block_count(&source) == 6,
          "breaking a Structure Block retires only its tile");
    CHECK(structure_template_capacity(),
          "Structure Blocks and Templates grow past former fixed limits");

    unlink(checkpoint);
    gm_runtime_destroy(&restored);
    gm_runtime_destroy(&source);
    puts("structure_block_runtime: PASS");
    return 0;
fail:
    unlink(checkpoint);
    if (restored_ready) gm_runtime_destroy(&restored);
    if (source_ready) gm_runtime_destroy(&source);
    return 1;
}
