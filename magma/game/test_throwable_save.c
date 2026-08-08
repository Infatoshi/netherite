#include "game/runtime.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static GmRuntimeProjectile *projectile_by_eid(
        GmRuntime *runtime, int eid, int *slot) {
    for (int index = 0; index < GM_RUNTIME_PROJECTILES; ++index)
        if (runtime->projectiles[index].active
                && runtime->projectiles[index].eid == eid) {
            if (slot) *slot = index;
            return &runtime->projectiles[index];
        }
    return NULL;
}

static int run_case(
        const char *path, int case_index, int item, int meta) {
    GmConfig config;
    GmRuntime source, restored;
    GmRuntimeProjectile *before, *after;
    ICStack stack;
    char error[256];
    int eid = 730000 + case_index;
    int source_slot = -1, restored_slot = -1;
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.seed = 0;
    if (!gm_runtime_init(&source, &config, error, sizeof error)) {
        fprintf(stderr, "source init: %s\n", error);
        return 0;
    }
    if (!gm_runtime_init(&restored, &config, error, sizeof error)) {
        fprintf(stderr, "restored init: %s\n", error);
        gm_runtime_destroy(&source);
        return 0;
    }
    gm_runtime_set_pose_state(
        &source, -150.0, 220.0, -150.0,
        37.5F, -21.25F, 0.125, -0.25, 0.375, 0, 0.0F);
    gm_runtime_set_entity_seed_generator_seed48(
        &source, (uint64_t)(18000 + case_index));
    gm_runtime_set_server_uuid_random_seed48(
        &source, (uint64_t)(28000 + case_index));
    gm_runtime_set_entity_id_cursor(&source, eid);
    stack = ic_mk(item, 5, meta);
    isr_set_stack(&source.player.inv, 0, stack);
    source.player.inv.current_item = 0;
    if (!gm_runtime_throw_player_item_now(&source, item, meta)) {
        fprintf(stderr, "case %d launch failed\n", case_index);
        goto fail;
    }
    before = projectile_by_eid(&source, eid, &source_slot);
    if (!before) goto fail;
    gm_runtime_set_pose_state(
        &source, 1000000.0, 220.0, 1000000.0,
        0.0F, 0.0F, 0.0, 0.0, 0.0, 1, 0.0F);
    for (int tick = 0; tick < 7; ++tick)
        if (!gm_runtime_tick_projectile_now(&source, source_slot))
            goto fail;
    if (!gm_runtime_write_checkpoint(&source, path)
            || !gm_runtime_load_checkpoint(&restored, path)) {
        fprintf(stderr, "case %d checkpoint failed\n", case_index);
        goto fail;
    }
    before = projectile_by_eid(&source, eid, &source_slot);
    after = projectile_by_eid(&restored, eid, &restored_slot);
    if (!before || !after || source_slot != restored_slot
            || memcmp(before, after, sizeof *before) != 0) {
        fprintf(stderr, "case %d restore mismatch\n", case_index);
        goto fail;
    }
    for (int tick = 0; tick < 11; ++tick) {
        if (!gm_runtime_tick_projectile_now(&source, source_slot)
                || !gm_runtime_tick_projectile_now(
                    &restored, restored_slot))
            goto fail;
        if (memcmp(&source.projectiles[source_slot],
                   &restored.projectiles[restored_slot],
                   sizeof source.projectiles[source_slot]) != 0) {
            fprintf(stderr,
                    "case %d continuation mismatch at tick %d\n",
                    case_index, tick + 1);
            goto fail;
        }
    }
    gm_runtime_destroy(&restored);
    gm_runtime_destroy(&source);
    return 1;
fail:
    gm_runtime_destroy(&restored);
    gm_runtime_destroy(&source);
    return 0;
}

static int dimension_topology_checkpoint(const char *path) {
    GmConfig config;
    GmRuntime source, restored;
    GmRuntimeProjectile *before, *after;
    char error[256];
    int source_slot = -1, restored_slot = -1;
    int order = 0;
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.seed = 0;
    if (!gm_runtime_init(&source, &config, error, sizeof error)
            || !gm_runtime_init(&restored, &config, error, sizeof error))
        return 0;
    if (!gm_runtime_loaded_chunks_begin(&source, 25)) goto fail;
    for (int chunk_x = 4; chunk_x <= 8; ++chunk_x)
        for (int chunk_z = 4; chunk_z <= 8; ++chunk_z)
            if (!gm_runtime_loaded_chunk_set(
                    &source, order++, chunk_x, chunk_z))
                goto fail;
    if (!gm_runtime_loaded_chunks_finalize(&source)
            || !gm_runtime_ticking_chunks_begin(&source, 1)
            || !gm_runtime_ticking_chunk_set(&source, 0, 6, 6, 0x21)
            || !gm_runtime_ticking_chunks_finalize(&source)
            || !gm_runtime_pending_chunk_unloads_begin(&source, 1)
            || !gm_runtime_pending_chunk_unload_set(
                &source, 0, 8, 8, 0)
            || !gm_runtime_pending_chunk_unloads_finalize(&source)
            || !gm_runtime_spawn_throwable_state_fixture(
                &source, 739900, 7, 438, 0,
                100.5, 220.0, 100.5, 0.25, 0.1, -0.125,
                -130.0F, -33.0F, -130.0F, -33.0F,
                7, 7, 1, 0, 0, 0, 0,
                0, 0, 0, -1, -1, -1, 0,
                0, 0, 0,
                0, 0, 0, 0, 0.0, 0.0, 0,
                0, 0, 12345, 0, 0.0)
            || !gm_runtime_set_dimension(&source, -1)
            || !gm_runtime_loaded_chunks_begin(&source, 0)
            || !gm_runtime_loaded_chunks_finalize(&source)
            || !gm_runtime_ticking_chunks_begin(&source, 0)
            || !gm_runtime_ticking_chunks_finalize(&source)
            || !gm_runtime_pending_chunk_unloads_begin(&source, 0)
            || !gm_runtime_pending_chunk_unloads_finalize(&source)
            || !gm_runtime_write_checkpoint(&source, path)
            || !gm_runtime_load_checkpoint(&restored, path))
        goto fail;
    before = projectile_by_eid(&source, 739900, &source_slot);
    after = projectile_by_eid(&restored, 739900, &restored_slot);
    if (!before || !after || source_slot != restored_slot
            || !gm_runtime_tick_projectile_now(&source, source_slot)
            || !gm_runtime_tick_projectile_now(&restored, restored_slot)
            || before->age != 8 || after->age != 8
            || memcmp(before, after, sizeof *before) != 0
            || !gm_runtime_set_dimension(&source, 0)
            || !gm_runtime_set_dimension(&restored, 0)
            || !source.loaded_chunks_authoritative
            || !restored.loaded_chunks_authoritative
            || source.loaded_chunk_count != 25
            || restored.loaded_chunk_count != 25
            || source.ticking_chunk_count != 1
            || restored.ticking_chunk_count != 1
            || source.pending_chunk_unload_count != 1
            || restored.pending_chunk_unload_count != 1
            || memcmp(source.loaded_chunks, restored.loaded_chunks,
                25 * sizeof source.loaded_chunks[0]) != 0
            || memcmp(source.ticking_chunks, restored.ticking_chunks,
                sizeof source.ticking_chunks[0]) != 0
            || memcmp(source.pending_chunk_unloads,
                restored.pending_chunk_unloads,
                sizeof source.pending_chunk_unloads[0]) != 0)
        goto fail;
    gm_runtime_destroy(&restored);
    gm_runtime_destroy(&source);
    return 1;
fail:
    gm_runtime_destroy(&restored);
    gm_runtime_destroy(&source);
    return 0;
}

int main(int argc, char **argv) {
    static const int items[] = {344, 332, 384, 368, 438, 441};
    if (argc != 2) return 2;
    for (int index = 0; index < 6; ++index)
        if (!run_case(argv[1], index, items[index], 0)) {
            unlink(argv[1]);
            return 1;
        }
    if (!dimension_topology_checkpoint(argv[1])) {
        fprintf(stderr, "dimension topology checkpoint failed\n");
        unlink(argv[1]);
        return 1;
    }
    unlink(argv[1]);
    puts("throwable_save: PASS (6 projectile forks + dimension topology)");
    return 0;
}
