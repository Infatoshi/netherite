#include "game/native_save.h"
#include "game/runtime.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fail;
#define CHECK(C, M) do { \
    if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } \
} while (0)

static uint64_t lcg_steps(uint64_t seed, int steps) {
    while (steps-- > 0)
        seed = (seed * UINT64_C(0x5DEECE66D) + UINT64_C(0xB))
            & ((UINT64_C(1) << 48) - UINT64_C(1));
    return seed;
}

static int mob_slot(const GmMobLive *m, int eid) {
    const EwStore *s = m->current ? &m->b : &m->a;
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot)
        if (s->alive[slot] && s->id[slot] == eid)
            return slot;
    return -1;
}

typedef struct {
    int source_type, cow_type, next_entity_id;
    int next_shears_random_valid;
    uint64_t math_seed48, shears_seed48;
    ICStack tool;
    GmLiveEnt drops[5];
    GmRuntimeSoundEvent sound;
    GmRuntimeParticleEvent particle;
} MooshroomOutcome;

static int capture_outcome(
        const GmRuntime *r, int source_eid, MooshroomOutcome *out) {
    memset(out, 0, sizeof *out);
    out->source_type = gm_mobs_type_by_eid(&r->mobs, source_eid);
    out->cow_type = gm_mobs_type_by_eid(&r->mobs, source_eid + 1);
    out->next_entity_id = r->next_entity_id;
    out->next_shears_random_valid = r->next_shears_random_valid;
    out->math_seed48 = r->math_random_seed48;
    out->shears_seed48 = r->next_shears_random_seed48;
    out->tool = isr_get_stack(&r->player.inv, 0);
    if (r->entities.n_active != 5
            || gm_runtime_sound_event_count(r) != 1
            || gm_runtime_particle_event_count(r) != 1
            || !gm_runtime_sound_event_get(r, 0, &out->sound)
            || !gm_runtime_particle_event_get(r, 0, &out->particle))
        return 0;
    for (int i = 0; i < 5; ++i)
        out->drops[i] = r->entities.ents[i];
    return 1;
}

static void clean_save(const char *root) {
    char path[512];
    static const char *files[] = {
        "runtime.bin", "player_statistics.json", "manifest.bin",
        "world_dim-1.bin", "world_dim0.bin", "world_dim1.bin",
    };
    for (size_t i = 0; i < sizeof files / sizeof files[0]; ++i) {
        snprintf(path, sizeof path,
                 "%s/mooshroom/generation-0000000000000001/%s",
                 root, files[i]);
        (void)remove(path);
    }
    snprintf(path, sizeof path,
             "%s/mooshroom/generation-0000000000000001", root);
    (void)rmdir(path);
    snprintf(path, sizeof path, "%s/mooshroom/current", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/mooshroom/write.lock", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/mooshroom", root);
    (void)rmdir(path);
    (void)rmdir(root);
}

static void component_fixture(
        GmMobLive *m, GmLiveSim *drops, IsrInv *inv,
        int age, int item, int count) {
    gm_mobs_init(m, 0);
    memset(drops, 0, sizeof *drops);
    isr_init(inv);
    isr_set_stack(inv, 0, ic_mk(item, count, 0));
    CHECK(gm_mobs_spawn_exact(
              m, EW_TYPE_MOOSHROOM, 680000,
              10.0, 64.0, 10.0, 0.0, 0.0, 0.0,
              37.0F, 7.5F, 1, 0, 0, 0) >= 0
              && gm_mobs_set_growing_age(m, 680000, age)
              && gm_mobs_set_entity_random_state(m, 680000, 1, 0, 0.0),
          "component Mooshroom fixture initializes");
}

static void test_bowl_component(void) {
    GmMobLive m;
    GmLiveSim drops;
    IsrInv inv;
    McSinTable sin_table;
    uint64_t math_seed;
    int next_id;
    mc_sin_table_init(&sin_table);

    component_fixture(&m, &drops, &inv, 0, 281, 1);
    math_seed = UINT64_C(0x123456789ABC);
    next_id = 680001;
    CHECK(gm_mobs_bowl_mooshroom(
              &m, 680000, &inv, 0, 0,
              8.5, 64.0, 8.5, 0.0F, 0.0F, (double)1.62F,
              &sin_table, &math_seed, &drops, &next_id) == 1
              && isr_get_stack(&inv, 0).item == 282
              && isr_get_stack(&inv, 0).count == 1
              && drops.n_active == 0 && next_id == 680001
              && math_seed == UINT64_C(0x123456789ABC),
          "last bowl is replaced by one stew without RNG or EID use");

    component_fixture(&m, &drops, &inv, 0, 281, 2);
    math_seed = UINT64_C(0x123456789ABC);
    next_id = 680001;
    CHECK(gm_mobs_bowl_mooshroom(
              &m, 680000, &inv, 0, 0,
              8.5, 64.0, 8.5, 0.0F, 0.0F, (double)1.62F,
              &sin_table, &math_seed, &drops, &next_id) == 1
              && isr_get_stack(&inv, 0).item == 281
              && isr_get_stack(&inv, 0).count == 1
              && isr_get_stack(&inv, 1).item == 282
              && isr_get_stack(&inv, 1).count == 1
              && drops.n_active == 0 && next_id == 680001,
          "stacked bowls insert stew into the first empty main slot");

    component_fixture(&m, &drops, &inv, 0, 281, 2);
    for (int i = 1; i < ISR_MAIN_SLOTS; ++i)
        isr_set_stack(&inv, i, ic_mk(1, 64, 0));
    jrand_set_seed48(&m.player_random, UINT64_C(0x0FEDCBA98765));
    math_seed = UINT64_C(0x123456789ABC);
    next_id = 680001;
    CHECK(gm_mobs_bowl_mooshroom(
              &m, 680000, &inv, 0, 0,
              8.5, 64.0, 8.5, 37.25F, -21.5F, (double)1.62F,
              &sin_table, &math_seed, &drops, &next_id) == 1
              && drops.n_active == 1 && drops.ents[0].item == 282
              && drops.ents[0].eid == 680001
              && drops.ents[0].pickup_delay == 40
              && next_id == 680002
              && math_seed == lcg_steps(UINT64_C(0x123456789ABC), 8)
              && m.player_random.seed
                    == lcg_steps(UINT64_C(0x0FEDCBA98765), 4),
          "full inventory drops stew with exact constructor and toss cursors");

    component_fixture(&m, &drops, &inv, -100, 281, 1);
    math_seed = 7; next_id = 680001;
    CHECK(gm_mobs_bowl_mooshroom(
              &m, 680000, &inv, 0, 0, 0, 0, 0, 0, 0, 1.62,
              &sin_table, &math_seed, &drops, &next_id) == 0
              && isr_get_stack(&inv, 0).item == 281,
          "child Mooshroom cannot fill a bowl");
}

static void test_shear_component(void) {
    GmMobLive m;
    GmLiveSim drops;
    IsrInv inv;
    uint64_t shear_seed, math_seed;
    int next_id;
    component_fixture(&m, &drops, &inv, 0, 359, 1);
    shear_seed = UINT64_C(0x3456789ABCDE);
    math_seed = UINT64_C(0x123456789ABC);
    next_id = 680001;
    CHECK(gm_mobs_shear_mooshroom(
              &m, 680000, &inv, 0,
              &shear_seed, &math_seed, &drops, &next_id) == 2,
          "adult Mooshroom shearing is accepted");
    int cow = mob_slot(&m, 680001);
    int exact_drops = drops.n_active == 5;
    for (int i = 0; i < 5 && exact_drops; ++i)
        exact_drops = drops.ents[i].eid == 680002 + i
            && drops.ents[i].item == 40 && drops.ents[i].count == 1
            && drops.ents[i].pickup_delay == 10;
    GmMobEvent event;
    GmMobParticleBatch particle;
    CHECK(mob_slot(&m, 680000) < 0 && cow > 0
              && gm_mobs_type_by_eid(&m, 680001) == EW_TYPE_COW
              && m.entity_pitch[cow] == 0.0F
              && isr_get_stack(&inv, 0).meta == 1
              && exact_drops && next_id == 680007
              && math_seed == lcg_steps(UINT64_C(0x123456789ABC), 46)
              && shear_seed == lcg_steps(UINT64_C(0x3456789ABCDE), 25)
              && gm_mobs_event_count(&m) == 1
              && gm_mobs_event_get(&m, 0, &event)
              && event.data == GM_MOB_SOUND_MOOSHROOM_SHEAR
              && gm_mobs_particle_batch_count(&m) == 1
              && gm_mobs_particle_batch_get(&m, 0, &particle)
              && particle.particle_id == GM_PARTICLE_EXPLOSION_LARGE
              && particle.descriptor_count == 1,
          "shearing atomically converts to cow, emits five mushrooms and exact events");

    component_fixture(&m, &drops, &inv, -100, 359, 1);
    shear_seed = 9; math_seed = 11; next_id = 680001;
    CHECK(gm_mobs_shear_mooshroom(
              &m, 680000, &inv, 0,
              &shear_seed, &math_seed, &drops, &next_id) == 1
              && mob_slot(&m, 680000) > 0 && drops.n_active == 0
              && next_id == 680001 && isr_get_stack(&inv, 0).meta == 0,
          "child shears interaction is handled without mutation");

    component_fixture(&m, &drops, &inv, 0, 359, 1);
    for (int i = 0; i < GM_LIVE_MAX - 4; ++i)
        CHECK(gm_live_spawn_item_exact(
                  &drops, 700000 + i, 0, 0, 0, 0, 0, 0, 0,
                  1, 1, 0, 0, 10, 1),
              "capacity prefill succeeds");
    drops.item_spawn_limit = GM_LIVE_MAX;
    shear_seed = 9; math_seed = 11; next_id = 680001;
    CHECK(gm_mobs_shear_mooshroom(
              &m, 680000, &inv, 0,
              &shear_seed, &math_seed, &drops, &next_id) == -1
              && mob_slot(&m, 680000) > 0 && next_id == 680001
              && shear_seed == 9 && math_seed == 11
              && gm_mobs_event_count(&m) == 0
              && gm_mobs_particle_batch_count(&m) == 0,
          "insufficient item capacity rejects conversion atomically");
}

static void reset_runtime(
        GmRuntime *r, double y, int eid, int item, int count) {
    gm_mobs_init(&r->mobs, 0);
    memset(&r->entities, 0, sizeof r->entities);
    r->controlled_mobs_enabled = 0;
    r->mobs_enabled = 0;
    r->server_shear_pending = 0;
    r->server_feed_animal_pending = 0;
    r->sound_event_head = r->sound_event_count = 0;
    r->particle_event_count = 0;
    r->sound_mob_next_seq = r->mobs.event_next_seq;
    r->particle_mob_next_seq = r->mobs.particle_batch_next_seq;
    isr_init(&r->player.inv);
    r->player.inv.current_item = 0;
    isr_set_stack(&r->player.inv, 0, ic_mk(item, count, 0));
    gm_runtime_set_pose(r, 8.5, y, 8.5, 0.0F, 24.0F);
    CHECK(gm_runtime_spawn_mob_fixture(
              r, GM_MOB_MOOSHROOM, eid, 8.5, y, 10.5,
              0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0)
              && gm_runtime_set_mob_growing_age(r, eid, 0)
              && gm_runtime_set_next_shears_random_seed48(
                  r, UINT64_C(0x3456789ABCDE))
              && gm_runtime_set_math_random_seed48(
                  r, UINT64_C(0x123456789ABC))
              && gm_runtime_set_entity_id_cursor(r, eid + 1),
          "runtime Mooshroom fixture initializes");
}

static void test_runtime(GmRuntime *r, const GmConfig *cfg, double y) {
    GmAction use = {0}, idle = {0};
    use.hotbar_sel = idle.hotbar_sel = -1;
    use.use = use.do_place = 1;

    reset_runtime(r, y, 681000, 359, 1);
    gm_runtime_tick(r, use);
    CHECK(r->server_shear_pending && r->server_shear_eid == 681000
              && gm_mobs_type_by_eid(&r->mobs, 681000)
                    == EW_TYPE_MOOSHROOM,
          "right click queues delayed Mooshroom shearing");
    gm_runtime_tick(r, idle);
    GmRuntimeSoundEvent sound;
    GmRuntimeParticleEvent particle;
    CHECK(!r->server_shear_pending
              && gm_mobs_type_by_eid(&r->mobs, 681000) == EW_TYPE_NONE
              && gm_mobs_type_by_eid(&r->mobs, 681001) == EW_TYPE_COW
              && r->entities.n_active == 5
              && gm_runtime_sound_event_count(r) == 1
              && gm_runtime_sound_event_get(r, 0, &sound)
              && sound.sound == GM_SOUND_MOOSHROOM_SHEAR
              && gm_runtime_particle_event_count(r) == 1
              && gm_runtime_particle_event_get(r, 0, &particle)
              && particle.kind == GM_PARTICLE_EXPLOSION_LARGE
              && particle.count == 1,
          "delayed shearing drains exact cow/drop/sound/particle transition");

    reset_runtime(r, y, 681100, 281, 1);
    gm_runtime_tick(r, use);
    CHECK(r->server_feed_animal_pending == 10,
          "right click queues delayed bowl interaction");
    gm_runtime_tick(r, idle);
    CHECK(!r->server_feed_animal_pending
              && isr_get_stack(&r->player.inv, 0).item == 282,
          "delayed bowl interaction replaces the held bowl with stew");

    reset_runtime(r, y, 681200, 325, 1);
    gm_runtime_tick(r, use);
    CHECK(r->server_feed_animal_pending == 2,
          "Mooshroom inherits delayed cow milking interaction");
    gm_runtime_tick(r, idle);
    CHECK(isr_get_stack(&r->player.inv, 0).item == 335,
          "Mooshroom bucket interaction produces milk");

    reset_runtime(r, y, 681300, 359, 1);
    gm_runtime_tick(r, use);
    char save_root[256], error[256];
    MooshroomOutcome uninterrupted, restored;
    (void)mkdir(".tmp", 0700);
    snprintf(save_root, sizeof save_root,
             ".tmp/mooshroom-runtime-%ld", (long)getpid());
    clean_save(save_root);
    CHECK(r->server_shear_pending
              && gm_native_save_write(
                  r, save_root, "mooshroom", error, sizeof error),
          "native save records a pending Mooshroom shear packet");
    gm_runtime_tick(r, idle);
    CHECK(capture_outcome(r, 681300, &uninterrupted),
          "capture uninterrupted post-shear outcome");
    CHECK(gm_native_save_load(
              r, cfg, save_root, "mooshroom", error, sizeof error),
          "native save restores pending Mooshroom shear packet");
    gm_runtime_tick(r, idle);
    CHECK(capture_outcome(r, 681300, &restored)
              && !memcmp(&restored, &uninterrupted, sizeof restored),
          "save/reload resumes exact conversion, RNG, items, sound, particle");
    clean_save(save_root);
}

int main(void) {
    GmConfig cfg;
    GmRuntime r;
    char err[256];
    test_bowl_component();
    test_shear_component();
    gm_config_defaults(&cfg);
    cfg.view_distance = 1;
    cfg.mobs = 0;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err),
          "Mooshroom runtime initializes");
    if (fail) return 1;
    double y = (double)gm_world_surface_y(r.world, 8, 8) + 1.0;
    test_runtime(&r, &cfg, y);
    gm_runtime_destroy(&r);
    if (fail) return 1;
    puts("PASS Mooshroom runtime: bowl, milk, shear/cow conversion, RNG, save");
    return 0;
}
