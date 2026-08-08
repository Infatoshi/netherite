#include "game/runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail;
#define CHECK(c, m) do { \
    if (!(c)) { fprintf(stderr, "FAIL: %s\n", m); fail = 1; } \
} while (0)

static EwStore *store(GmMobLive *m) {
    return m->current ? &m->b : &m->a;
}

static uint64_t mate_seed48(void) {
    for (long long seed = 0; seed < 1000000; ++seed) {
        JavaRandom random;
        jrand_set(&random, seed);
        uint64_t initial = random.seed;
        (void)jrand_int_bound(&random, 1000);
        if (jrand_int_bound(&random, 500) == 0) return initial;
    }
    return 0;
}

static int fixture(GmRuntime *r) {
    GmConfig config;
    char error[256] = {0};
    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 3;
    config.mobs = 1;
    config.villages = 1;
    config.weather = 0;
    config.daylight = 0;
    if (!gm_runtime_init(r, &config, error, sizeof error)) {
        fprintf(stderr, "init: %s\n", error);
        return 0;
    }
    gm_world_ensure(r->world, 0, 0, 3);
    r->gamerules.doMobSpawning = 0;
    gm_mobs_set_natural_spawning(&r->mobs, 0);
    for (int x = -32; x <= 32; ++x)
        for (int z = -32; z <= 32; ++z)
            gm_world_set_block(r->world, x, 63, z, 1);
    for (int axis = -16; axis <= 16; ++axis)
        for (int y = 64; y <= 65; ++y) {
            gm_world_set_block(r->world, axis, y, -16, 1);
            gm_world_set_block(r->world, axis, y, 16, 1);
            gm_world_set_block(r->world, -16, y, axis, 1);
            gm_world_set_block(r->world, 16, y, axis, 1);
        }
    for (int x = -12; x < 12; ++x)
        for (int z = 11; z <= 15; ++z)
            gm_world_set_block(r->world, x, 66, z, 1);
    gm_runtime_set_pose(r, 0.5, 64.0, 0.5, 0.0F, 0.0F);
    r->server_player = r->player;
    if (!gm_runtime_player_uuid_restore(
                r, UINT64_C(0x123456789abcdef0),
                UINT64_C(0x0fedcba987654321))
            || !gm_runtime_village_collection_begin(r, 1, 1)
            || !gm_runtime_village_state_restore(
                r, 0, 2, 32, 0, 1, 1, 0,
                0, 64, 0, 0, 1344, 0)
            || !gm_runtime_village_reputation_restore(
                r, 0, UINT64_C(0x123456789abcdef0),
                UINT64_C(0x0fedcba987654321), -12))
        { fprintf(stderr, "fixture: village header\n"); return 0; }
    for (int door = 0; door < 21; ++door) {
        int x = door - 10;
        gm_world_set_block_meta(r->world, x, 64, 10, 64, 0);
        gm_world_set_block_meta(r->world, x, 65, 10, 64, 8);
        if (!gm_runtime_village_door_restore(
                    r, 0, x, 64, 10, 2, 0, 1)) {
            fprintf(stderr, "fixture: door %d\n", door); return 0;
        }
    }
    int mating_eids[2] = {-1, -1};
    for (int profession = 0; profession < 2; ++profession) {
        double x = 0.5 + profession;
        double z = 0.5;
        int slot = gm_mobs_spawn_villager(
            &r->mobs, x, 64.0, z,
            profession);
        int eid;
        if (slot <= 0) { fprintf(stderr, "fixture: villager spawn %d\n",
                                 profession); return 0; }
        eid = store(&r->mobs)->id[slot];
        if (profession < 2) mating_eids[profession] = eid;
        if (!gm_mobs_set_villager_village_index(&r->mobs, eid, 0)
                || !gm_mobs_set_villager_village_state(
                    &r->mobs, eid, 1, 21, 2, 1)) {
            fprintf(stderr, "fixture: villager state %d\n", profession);
            return 0;
        }
        if (profession < 2 && !gm_mobs_set_villager_inventory_slot(
                    &r->mobs, eid, 0, ic_mk(297, 12, 0))) {
            fprintf(stderr, "fixture: villager inventory %d\n", profession);
            return 0;
        }
    }
    {
        uint64_t seed48 = mate_seed48();
        for (int index = 0; index < 2; ++index) {
            int slot = gm_mobs_find_slot_by_eid(&r->mobs, mating_eids[index]);
            if (!seed48 || slot <= 0 || !gm_mobs_set_entity_random_state(
                        &r->mobs, mating_eids[index], seed48, 0, 0.0))
                return 0;
            r->mobs.villager_ai_tick_count[slot] = 0;
            r->mobs.villager_random_tick_divider[slot] = 1000;
        }
    }
    return 1;
}

static int tail_fixture(GmRuntime *r) {
    for (int profession = 2; profession < 6; ++profession) {
        double x = profession == 2 ? 0.5
            : -10.5 + (profession - 3) * 2.0;
        double z = profession == 2 ? 9.0 : 0.5;
        int slot = gm_mobs_spawn_villager(
            &r->mobs, x, 64.0, z, profession);
        int eid;
        if (slot <= 0) return 0;
        eid = store(&r->mobs)->id[slot];
        if (!gm_mobs_set_villager_village_index(&r->mobs, eid, 0)
                || !gm_mobs_set_villager_village_state(
                    &r->mobs, eid, 1, 21, 7, 1)) return 0;
        if (profession >= 3 && !gm_runtime_set_mob_no_ai(r, eid, 1))
            return 0;
        if (profession == 2)
            r->mobs.villager_random_tick_divider[slot] = 0;
    }
    {
        int slot = gm_mobs_spawn_iron_golem(&r->mobs, 0.5, 64.0, 4.5, 0);
        int eid;
        if (slot <= 0) { fprintf(stderr, "fixture: golem spawn\n"); return 0; }
        eid = store(&r->mobs)->id[slot];
        if (!gm_mobs_set_iron_golem_village_context(
                    &r->mobs, eid, 0, -1, 1)) {
            fprintf(stderr, "fixture: golem context\n"); return 0;
        }
    }
    {
        int slot = gm_mobs_spawn_zombie_villager(
            &r->mobs, 8.5, 64.0, 8.5, 3);
        int eid;
        ICStack apple;
        if (slot <= 0) { fprintf(stderr, "fixture: zombie spawn\n"); return 0; }
        eid = store(&r->mobs)->id[slot];
        if (!gm_mobs_set_entity_random_state(
                    &r->mobs, eid, 0, 0, 0.0)
                || !gm_mobs_apply_potion_effect(
                    &r->mobs, slot, 18, 0, 100)) {
            fprintf(stderr, "fixture: zombie weakness\n"); return 0;
        }
        isr_init(&r->player.inv);
        r->player.inv.current_item = 0;
        isr_set_stack(&r->player.inv, 0, ic_mk(322, 1, 0));
        apple = isr_get_stack(&r->player.inv, 0);
        if (!gm_mobs_zombie_villager_can_cure(&r->mobs, eid, &apple)
                || !gm_mobs_cure_zombie_villager(
                    &r->mobs, eid, &r->player.inv, 0, 0)) {
            fprintf(stderr, "fixture: zombie cure\n"); return 0;
        }
        slot = gm_mobs_find_slot_by_eid(&r->mobs, eid);
        if (slot <= 0) { fprintf(stderr, "fixture: zombie refind\n"); return 0; }
        r->mobs.zombie_villager_conversion_time[slot] = 600;
    }
    return 1;
}

static int run_path(const char *final_path, int reload) {
    static const int boundaries[] = {1, 40, 299, 300, 599, 600, 1199};
    const char *mid = ".ai02-strict-mid.bin";
    GmRuntime *r = calloc(1, sizeof *r);
    GmAction idle = {.hotbar_sel = -1};
    int birth_observed = 0;
    if (!r || !fixture(r)) {
        free(r);
        return 0;
    }
    for (int tick = 0; tick < 1200; ++tick) {
        gm_runtime_tick(r, idle);
        {
            EwStore *entities = store(&r->mobs);
            int villagers = 0;
            for (int slot = 1; slot < entities->count; ++slot)
                villagers += entities->alive[slot]
                    && entities->type[slot] == EW_TYPE_VILLAGER;
            if (tick <= 299 && villagers > 2) birth_observed = 1;
        }
        if (tick == 299 && !tail_fixture(r)) {
            fprintf(stderr, "campaign: tail fixture\n");
            gm_runtime_destroy(r);
            free(r);
            return 0;
        }
        if (reload) for (size_t index = 0;
                index < sizeof boundaries / sizeof boundaries[0]; ++index)
            if (tick == boundaries[index]
                    && (!gm_runtime_write_checkpoint(r, mid)
                        || !gm_runtime_load_checkpoint(r, mid))) {
                fprintf(stderr, "campaign: reload boundary %d\n", tick);
                gm_runtime_destroy(r);
                free(r);
                return 0;
            }
    }
    {
        EwStore *entities = store(&r->mobs);
        int villagers = 0, golems = 0, converting = 0;
        for (int slot = 1; slot < entities->count; ++slot) {
            if (!entities->alive[slot]) continue;
            villagers += entities->type[slot] == EW_TYPE_VILLAGER;
            golems += entities->type[slot] == EW_TYPE_IRON_GOLEM;
            if (entities->type[slot] == EW_TYPE_ZOMBIE_VILLAGER) {
                ++converting;
                fprintf(stderr,
                    "campaign: surviving zombie slot=%d eid=%d conversion=%d "
                    "noai=%d next_id=%d\n", slot, entities->id[slot],
                    r->mobs.zombie_villager_conversion_time[slot],
                    r->mobs.controlled_no_ai[slot], r->next_entity_id);
            }
        }
        if (!birth_observed || villagers < 6 || golems < 1 || converting != 0
                || r->village_state_count != 1
                || r->village_states[0].door_count != 21) {
            fprintf(stderr,
                "campaign: final villagers=%d golems=%d zombie_villagers=%d "
                "villages=%d doors=%d birth=%d\n", villagers, golems,
                converting,
                r->village_state_count,
                r->village_state_count ? r->village_states[0].door_count : -1,
                birth_observed);
            gm_runtime_destroy(r);
            free(r);
            return 0;
        }
    }
    if (!gm_runtime_write_checkpoint(r, final_path)) {
        fprintf(stderr, "campaign: final checkpoint %s\n", final_path);
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

int main(void) {
    const char *continuous = ".ai02-strict-continuous.bin";
    const char *reloaded = ".ai02-strict-reloaded.bin";
    CHECK(run_path(continuous, 0), "run uninterrupted 1,200-tick village");
    CHECK(run_path(reloaded, 1), "run seven-boundary reloaded village");
    CHECK(files_equal(continuous, reloaded),
          "uninterrupted and repeatedly reloaded checkpoints are byte exact");
    remove(".ai02-strict-mid.bin");
    remove(".ai02-strict-mid.bin.tmp");
    remove(continuous);
    remove(reloaded);
    if (fail) return 1;
    puts("village_strict_campaign: PASS 1200 ticks, 6 professions, mating, "
         "golem, reputation, zombie cure, 21 doors, 7 reload boundaries");
    return 0;
}
