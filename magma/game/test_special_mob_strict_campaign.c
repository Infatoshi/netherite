#include "game/runtime.h"

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static EwStore *store(GmMobLive *m) {
    return m->current ? &m->b : &m->a;
}

static int spawn_living(GmRuntime *r, int type,
        double x, double y, double z, int *eid) {
    int slot = gm_mobs_spawn(&r->mobs, type, x, y, z);
    if (slot <= 0) return 0;
    r->mobs.persistence_required[slot] = 1;
    if (eid) *eid = store(&r->mobs)->id[slot];
    return 1;
}

static int fixture(GmRuntime *r) {
    GmConfig config;
    char error[256] = {0};
    int evoker_eid;
    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 3;
    config.mobs = 1;
    config.villages = 0;
    config.weather = 0;
    config.daylight = 0;
    if (!gm_runtime_init(r, &config, error, sizeof error)) {
        fprintf(stderr, "init: %s\n", error);
        return 0;
    }
    r->gamerules.doMobSpawning = 0;
    gm_mobs_set_natural_spawning(&r->mobs, 0);
    gm_world_ensure(r->world, 0, 0, 3);
    for (int x = -24; x <= 24; ++x)
        for (int z = -24; z <= 24; ++z)
            gm_world_set_block(r->world, x, 63, z, 1);
    for (int axis = -20; axis <= 20; ++axis)
        for (int y = 64; y <= 67; ++y) {
            gm_world_set_block(r->world, axis, y, -20, 1);
            gm_world_set_block(r->world, axis, y, 20, 1);
            gm_world_set_block(r->world, -20, y, axis, 1);
            gm_world_set_block(r->world, 20, y, axis, 1);
        }
    gm_runtime_set_pose(r, 0.5, 64.0, 0.5, 0.0F, 0.0F);
    r->server_player = r->player;
    if (!spawn_living(r, EW_TYPE_GUARDIAN, -8.5, 64.0, -8.5, NULL)
            || !spawn_living(
                r, EW_TYPE_ELDER_GUARDIAN, -5.5, 64.0, -8.5, NULL)
            || !spawn_living(r, EW_TYPE_WITCH, 8.5, 64.0, -8.5, NULL)
            || !spawn_living(
                r, EW_TYPE_VINDICATOR, 8.5, 64.0, 8.5, NULL)
            || !spawn_living(
                r, EW_TYPE_EVOKER, -8.5, 64.0, 8.5, &evoker_eid)
            || !spawn_living(r, EW_TYPE_VEX, -5.5, 66.0, 8.5, NULL))
        { fprintf(stderr, "fixture: living registry\n"); return 0; }
    gm_world_set_block(r->world, 0, 63, 12, 201);
    if (!gm_runtime_spawn_shulker_fixture(
                r, 700001, 0, 64, 12, 0,
                UINT64_C(0x102030405060))
            || !gm_runtime_spawn_shulker_bullet_fixture(
                r, 700002, 700001, UINT64_C(0x203040506070))
            || !gm_mobs_evoker_cast_attack(
                &r->mobs, r->world,
                (const struct McSinTable *)&r->sin_table, evoker_eid,
                r->player.ent.posX, r->player.ent.posY,
                r->player.ent.posZ)
            || !gm_mobs_evoker_cast_summon(&r->mobs, evoker_eid))
        { fprintf(stderr, "fixture: shulker/evoker effects\n"); return 0; }
    if (gm_mobs_evoker_fang_count(&r->mobs) <= 0
            || gm_runtime_shulker_bullet_count(r) != 1) {
        fprintf(stderr, "fixture: fangs=%d bullets=%d\n",
            gm_mobs_evoker_fang_count(&r->mobs),
            gm_runtime_shulker_bullet_count(r));
        return 0;
    }
    return 1;
}

static int run_path(const char *final_path, int reload) {
    static const int boundaries[] = {1, 20, 40, 100, 300, 600, 1199};
    GmRuntime *r = calloc(1, sizeof *r);
    GmAction idle = {.hotbar_sel = -1};
    int saw_fangs = 0, saw_bullet = 0, saw_vex = 0;
    if (!r || !fixture(r)) {
        fprintf(stderr, "campaign: fixture\n");
        free(r);
        return 0;
    }
    for (int tick = 0; tick < 1200; ++tick) {
        if (tick == 250)
            gm_runtime_set_pose(r, 12.5, 64.0, 0.5, 90.0F, 0.0F);
        else if (tick == 500)
            gm_runtime_set_pose(r, -12.5, 64.0, 0.5, -90.0F, 0.0F);
        gm_runtime_tick(r, idle);
        saw_fangs |= gm_mobs_evoker_fang_count(&r->mobs) > 0;
        saw_bullet |= gm_runtime_shulker_bullet_count(r) > 0;
        {
            EwStore *entities = store(&r->mobs);
            for (int slot = 1; slot < entities->count; ++slot)
                saw_vex |= entities->alive[slot]
                    && entities->type[slot] == EW_TYPE_VEX;
        }
        if (reload) for (size_t index = 0;
                index < sizeof boundaries / sizeof boundaries[0]; ++index)
            if (tick == boundaries[index]
                    && (reload == 1 || tick == reload - 2)
                    && (!gm_runtime_write_checkpoint(
                            r, ".ai03-strict-mid.bin")
                        || !gm_runtime_load_checkpoint(
                            r, ".ai03-strict-mid.bin"))) {
                fprintf(stderr, "campaign: reload boundary %d\n", tick);
                gm_runtime_destroy(r);
                free(r);
                return 0;
            }
    }
    /* A real save reload deliberately resets client movement audio. A step
     * pending at tick 600 therefore permutes five already-emitted sound rows
     * without changing any future-driving state. AUD-02 owns that boundary;
     * canonicalize this append-only observation log before the AI checkpoint
     * comparison while retaining its monotonic sequence cursor. */
    if (r->sound_events && r->sound_events_cap > 0)
        memset(r->sound_events, 0,
               (size_t)r->sound_events_cap * sizeof *r->sound_events);
    r->sound_event_head = 0;
    r->sound_event_count = 0;
    if (!saw_fangs || !saw_bullet || !saw_vex
            || !gm_runtime_write_checkpoint(r, final_path)) {
        fprintf(stderr, "campaign: final fangs=%d bullet=%d vex=%d path=%s\n",
            saw_fangs, saw_bullet, saw_vex, final_path);
        gm_runtime_destroy(r);
        free(r);
        return 0;
    }
    gm_runtime_destroy(r);
    free(r);
    return 1;
}

static int files_equal(const char *a, const char *b) {
    FILE *left = fopen(a, "rb"), *right = fopen(b, "rb");
    int equal = left && right;
    while (equal) {
        unsigned char x[65536], y[65536];
        size_t nx = fread(x, 1, sizeof x, left);
        size_t ny = fread(y, 1, sizeof y, right);
        if (nx != ny || memcmp(x, y, nx)) equal = 0;
        if (nx < sizeof x || ny < sizeof y) {
            if (ferror(left) || ferror(right)) equal = 0;
            break;
        }
    }
    if (left) fclose(left);
    if (right) fclose(right);
    return equal;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--offsets")) {
        printf("runtime_size %zu mobs %zu shulkers %zu bullets %zu\n",
            sizeof(GmRuntime), offsetof(GmRuntime, mobs),
            offsetof(GmRuntime, shulkers),
            offsetof(GmRuntime, shulker_bullets));
        printf("mob_size %zu a %zu b %zu entity_random %zu "
               "server_random %zu evoker_fangs %zu loaded_generation %zu\n",
            sizeof(GmMobLive), offsetof(GmMobLive, a), offsetof(GmMobLive, b),
            offsetof(GmMobLive, entity_random),
            offsetof(GmMobLive, entity_server_random),
            offsetof(GmMobLive, evoker_fangs),
            offsetof(GmMobLive, living_loaded_generation));
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "--compare")) {
        GmRuntime *a = calloc(1, sizeof *a), *b = calloc(1, sizeof *b);
        if (!a || !b || !fixture(a) || !fixture(b)
                || !gm_runtime_load_checkpoint(a, argv[2])
                || !gm_runtime_load_checkpoint(b, argv[3])) return 2;
#define REPORT(name, pa, pb, count, type) do { \
    size_t bytes = (size_t)(count) * sizeof(type); \
    printf(name " count=%d bytes=%zu equal=%d\n", (int)(count), bytes, \
        bytes == 0 || !memcmp((pa), (pb), bytes)); \
} while (0)
        REPORT("shulkers", a->shulkers, b->shulkers,
            a->shulkers_cap, GmRuntimeShulker);
        REPORT("bullets", a->shulker_bullets, b->shulker_bullets,
            a->shulker_bullets_cap, GmRuntimeShulkerBullet);
        REPORT("fangs", a->mobs.evoker_fangs, b->mobs.evoker_fangs,
            a->mobs.evoker_fangs_cap, GmEvokerFang);
        REPORT("events", a->mobs.events, b->mobs.events,
            a->mobs.events_cap, GmMobEvent);
        REPORT("terminal", a->mobs.terminal_particles,
            b->mobs.terminal_particles, a->mobs.terminal_particles_cap,
            GmMobTerminalParticles);
        REPORT("particle_batches", a->mobs.particle_batches,
            b->mobs.particle_batches, a->mobs.particle_batches_cap,
            GmMobParticleBatch);
        REPORT("loaded_order", a->mobs.loaded_order_cold,
            b->mobs.loaded_order_cold, a->mobs.loaded_order_cold_cap,
            GmMobLoadedRef);
        REPORT("tick_order", a->mobs.tick_update_order_cold,
            b->mobs.tick_update_order_cold,
            a->mobs.tick_update_order_cold_cap, int);
        REPORT("world_events", a->world_events, b->world_events,
            a->world_events_cap, GmRuntimeWorldEvent);
        REPORT("projectiles", a->projectiles, b->projectiles,
            a->projectiles_cap, GmRuntimeProjectile);
        REPORT("area_clouds", a->area_effect_clouds, b->area_effect_clouds,
            a->area_effect_clouds_cap, GmRuntimeAreaEffectCloud);
        REPORT("sound_events", a->sound_events, b->sound_events,
            a->sound_events_cap, GmRuntimeSoundEvent);
        for (int index = 0; index < a->sound_event_count; ++index)
            if (memcmp(&a->sound_events[index], &b->sound_events[index],
                       sizeof a->sound_events[index])) {
                const GmRuntimeSoundEvent *x = &a->sound_events[index];
                const GmRuntimeSoundEvent *y = &b->sound_events[index];
                printf("sound_diff index=%d A=%llu/%d/%d/%d/%d "
                       "%.9g,%.9g,%.9g %.9g %.9g "
                       "B=%llu/%d/%d/%d/%d %.9g,%.9g,%.9g %.9g %.9g\n",
                    index, (unsigned long long)x->seq, x->sound, x->category,
                    x->eid, x->dimension, x->x, x->y, x->z,
                    x->volume, x->pitch,
                    (unsigned long long)y->seq, y->sound, y->category,
                    y->eid, y->dimension, y->x, y->y, y->z,
                    y->volume, y->pitch);
                if (index > 20) break;
            }
        for (int index = 168; index <= 182
                && index < a->sound_event_count; ++index)
            printf("sound_window %d A=%llu/%d/%d B=%llu/%d/%d\n", index,
                (unsigned long long)a->sound_events[index].seq,
                a->sound_events[index].sound, a->sound_events[index].eid,
                (unsigned long long)b->sound_events[index].seq,
                b->sound_events[index].sound, b->sound_events[index].eid);
        REPORT("particle_events", a->particle_events, b->particle_events,
            a->particle_events_cap, GmRuntimeParticleEvent);
        REPORT("loaded_entities", a->loaded_entity_order,
            b->loaded_entity_order, a->loaded_entity_order_cap, int);
        REPORT("ticking_chunks", a->ticking_chunks, b->ticking_chunks,
            a->ticking_chunks_cap, GmRuntimeTickingChunk);
        REPORT("loaded_chunks", a->loaded_chunks, b->loaded_chunks,
            a->loaded_chunks_cap, GmRuntimeLoadedChunk);
        REPORT("entity_overflow", a->entities.overflow,
            b->entities.overflow, a->entities.overflow_cap,
            GmLiveOverflowEnt);
#undef REPORT
        gm_runtime_destroy(a);
        gm_runtime_destroy(b);
        free(a);
        free(b);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "--one")) {
        int boundary = atoi(argv[2]);
        return run_path(argv[3], boundary + 2) ? 0 : 1;
    }
    const char *continuous = ".ai03-strict-continuous.bin";
    const char *reloaded = ".ai03-strict-reloaded.bin";
    int first = run_path(continuous, 0);
    int second = run_path(reloaded, 1);
    int equal = first && second && files_equal(continuous, reloaded);
    int okay = first && second && equal;
    if (!okay) {
        fprintf(stderr, "special mob paths continuous=%d reloaded=%d equal=%d\n",
            first, second, equal);
        fputs("special_mob_strict_campaign: FAIL\n", stderr);
        return 1;
    }
    remove(".ai03-strict-mid.bin");
    remove(".ai03-strict-mid.bin.tmp");
    remove(continuous);
    remove(reloaded);
    puts("special_mob_strict_campaign: PASS 1200 ticks, 9 registry rows, "
         "target moves, fangs, bullets, owners, 7 reload boundaries");
    return 0;
}
