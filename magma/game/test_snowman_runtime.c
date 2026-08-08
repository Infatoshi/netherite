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

static void component_fixture(
        GmMobLive *m, IsrInv *inv, int pumpkin, int meta,
        int unbreaking, uint64_t entity_seed) {
    gm_mobs_init(m, 0);
    isr_init(inv);
    ICStack shears = ic_mk(359, 1, meta);
    if (unbreaking) {
        shears.n_enchants = 1;
        shears.enchants[0].id = 34;
        shears.enchants[0].level = (short)unbreaking;
    }
    isr_set_stack(inv, 0, shears);
    CHECK(gm_mobs_spawn_exact(
              m, EW_TYPE_SNOWMAN, 684000,
              10.0, 64.0, 10.0, 0.0, 0.0, 0.0,
              0.0F, 4.0F, 1, 0, 0, 0) >= 0
              && gm_mobs_restore_snowman_state(m, 684000, pumpkin)
              && gm_mobs_set_entity_random_state(
                  m, 684000, entity_seed, 0, 0.0),
          "component Snow Golem fixture initializes");
}

static void test_component(void) {
    GmMobLive mobs;
    IsrInv inv;
    const uint64_t seed = UINT64_C(1);
    component_fixture(&mobs, &inv, 1, 0, 0, seed);
    CHECK(gm_mobs_shear_snowman(&mobs, 684000, &inv, 0) == 2,
          "equipped Snow Golem accepts Forge shears interaction");
    int slot = mob_slot(&mobs, 684000);
    CHECK(slot > 0 && !mobs.snowman_pumpkin[slot]
              && isr_get_stack(&inv, 0).item == 359
              && isr_get_stack(&inv, 0).meta == 1
              && mobs.entity_random[slot].random.seed == seed
              && gm_mobs_event_count(&mobs) == 0
              && gm_mobs_particle_batch_count(&mobs) == 0,
          "shearing removes pumpkin and damages tool with no drops or events");

    component_fixture(&mobs, &inv, 0, 0, 0, seed);
    CHECK(gm_mobs_shear_snowman(&mobs, 684000, &inv, 0) == 1
              && !mobs.snowman_pumpkin[mob_slot(&mobs, 684000)]
              && isr_get_stack(&inv, 0).meta == 0
              && mobs.entity_random[mob_slot(&mobs, 684000)].random.seed
                    == seed,
          "pumpkinless Snow Golem handles shears without mutation or RNG");

    component_fixture(&mobs, &inv, 1, 0, 3, seed);
    CHECK(gm_mobs_shear_snowman(&mobs, 684000, &inv, 0) == 2
              && isr_get_stack(&inv, 0).meta == 1
              && mobs.entity_random[mob_slot(&mobs, 684000)].random.seed
                    == lcg_steps(seed, 1),
          "Unbreaking durability consumes the Snow Golem entity RNG exactly");

    component_fixture(&mobs, &inv, 1, 238, 0, seed);
    CHECK(gm_mobs_shear_snowman(&mobs, 684000, &inv, 0) == 2
              && isr_get_stack(&inv, 0).count == 0,
          "terminal shears durability removes the broken tool stack");

    component_fixture(&mobs, &inv, 1, 0, 0, seed);
    isr_set_stack(&inv, 0, ic_mk(280, 1, 0));
    CHECK(gm_mobs_shear_snowman(&mobs, 684000, &inv, 0) == 0
              && mobs.snowman_pumpkin[mob_slot(&mobs, 684000)],
          "non-shears interaction passes without changing pumpkin state");
}

typedef struct {
    int pumpkin;
    int shear_pending;
    int shears_random_valid;
    int entity_count;
    int sound_count;
    int particle_count;
    ICStack tool;
    uint64_t entity_seed48;
    uint64_t shear_seed48;
} SnowmanOutcome;

static int capture_outcome(
        const GmRuntime *r, int eid, SnowmanOutcome *out) {
    int slot = mob_slot(&r->mobs, eid);
    if (slot <= 0) return 0;
    memset(out, 0, sizeof *out);
    out->pumpkin = r->mobs.snowman_pumpkin[slot];
    out->shear_pending = r->server_shear_pending;
    out->shears_random_valid = r->next_shears_random_valid;
    out->entity_count = r->entities.n_active;
    out->sound_count = gm_runtime_sound_event_count(r);
    out->particle_count = gm_runtime_particle_event_count(r);
    out->tool = isr_get_stack(&r->player.inv, 0);
    out->entity_seed48 = r->mobs.entity_random[slot].random.seed;
    out->shear_seed48 = r->next_shears_random_seed48;
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
                 "%s/snowman/generation-0000000000000001/%s",
                 root, files[i]);
        (void)remove(path);
    }
    snprintf(path, sizeof path,
             "%s/snowman/generation-0000000000000001", root);
    (void)rmdir(path);
    snprintf(path, sizeof path, "%s/snowman/current", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/snowman/write.lock", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/snowman", root);
    (void)rmdir(path);
    (void)rmdir(root);
}

static void reset_runtime(GmRuntime *r, double y, int eid, int pumpkin) {
    gm_mobs_init(&r->mobs, 0);
    memset(&r->entities, 0, sizeof r->entities);
    r->loaded_entity_order_count = 0;
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
    isr_set_stack(&r->player.inv, 0, ic_mk(359, 1, 0));
    gm_runtime_set_pose(r, 8.5, y, 8.5, 0.0F, 24.0F);
    CHECK(gm_runtime_spawn_mob_fixture(
              r, EW_TYPE_SNOWMAN, eid, 8.5, y, 10.5,
              0.0, 0.0, 0.0, 0.0F, 4.0F, 1, 0, 0, 0)
              && gm_runtime_restore_snowman_state(r, eid, pumpkin)
              && gm_mobs_set_entity_random_state(
                  &r->mobs, eid, 1, 0, 0.0)
              && gm_runtime_set_next_shears_random_seed48(
                  r, UINT64_C(0x3456789ABCDE))
              && gm_runtime_set_entity_id_cursor(r, eid + 1),
          "runtime Snow Golem fixture initializes");
}

static void test_runtime(GmRuntime *r, const GmConfig *cfg, double y) {
    GmAction use = {0}, idle = {0};
    use.hotbar_sel = idle.hotbar_sel = -1;
    use.use = use.do_place = 1;
    reset_runtime(r, y, 685000, 1);
    gm_runtime_tick(r, use);
    CHECK(r->server_shear_pending && r->server_shear_eid == 685000,
          "right click queues delayed Snow Golem shearing");
    gm_runtime_tick(r, idle);
    SnowmanOutcome immediate;
    CHECK(capture_outcome(r, 685000, &immediate)
              && !immediate.pumpkin && !immediate.shear_pending
              && !immediate.shears_random_valid
              && immediate.tool.meta == 1
              && immediate.entity_count == 0
              && immediate.sound_count == 0
              && immediate.particle_count == 0,
          "delayed shearing removes pumpkin and consumes private RNG token");

    reset_runtime(r, y, 685100, 1);
    gm_runtime_tick(r, use);
    char save_root[256], error[256];
    SnowmanOutcome uninterrupted, restored;
    (void)mkdir(".tmp", 0700);
    snprintf(save_root, sizeof save_root,
             ".tmp/snowman-runtime-%ld", (long)getpid());
    clean_save(save_root);
    CHECK(r->server_shear_pending
              && gm_native_save_write(
                  r, save_root, "snowman", error, sizeof error),
          "native save records a pending Snow Golem shear packet");
    gm_runtime_tick(r, idle);
    CHECK(capture_outcome(r, 685100, &uninterrupted),
          "capture uninterrupted Snow Golem shear outcome");
    CHECK(gm_native_save_load(
              r, cfg, save_root, "snowman", error, sizeof error),
          "native save restores pending Snow Golem shear packet");
    gm_runtime_tick(r, idle);
    CHECK(capture_outcome(r, 685100, &restored)
              && !memcmp(&restored, &uninterrupted, sizeof restored),
          "save/reload resumes exact pumpkin, tool, RNG and event outcome");
    clean_save(save_root);
}

static GmRuntimeProjectile *snowman_projectile(
        GmRuntime *r, int owner_eid) {
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
        if (r->projectiles[i].active && r->projectiles[i].type == 8
                && r->projectiles[i].shooting_living
                && r->projectiles[i].shooter_eid == owner_eid)
            return &r->projectiles[i];
    return NULL;
}

typedef struct {
    int active, eid, shooter_eid, age, ticks_existed;
    double x, y, z, vx, vy, vz;
    float yaw, pitch;
    uint64_t random_seed48;
} SnowballOutcome;

static int capture_snowball(
        GmRuntime *r, int owner_eid, SnowballOutcome *out) {
    GmRuntimeProjectile *p = snowman_projectile(r, owner_eid);
    if (!p) return 0;
    *out = (SnowballOutcome){
        p->active, p->eid, p->shooter_eid, p->age, p->ticks_existed,
        p->x, p->y, p->z, p->vx, p->vy, p->vz,
        p->yaw, p->pitch, p->random_seed48,
    };
    return 1;
}

static void test_active_ranged(
        GmRuntime *r, const GmConfig *cfg, double y) {
    GmAction idle = {.hotbar_sel = -1};
    const int snowman_eid = 686000;
    const int zombie_eid = 686001;
    reset_runtime(r, y, snowman_eid, 1);
    int floor_y = (int)y - 1;
    for (int x = 7; x <= 9; ++x)
        for (int z = 7; z <= 30; ++z) {
            gm_world_set_block_meta(r->world, x, floor_y, z, 1, 0);
            for (int clear_y = floor_y + 1;
                    clear_y <= floor_y + 4; ++clear_y)
                gm_world_set_block_meta(
                    r->world, x, clear_y, z, 0, 0);
        }
    int snowman = mob_slot(&r->mobs, snowman_eid);
    CHECK(snowman > 0 && gm_mobs_set_no_ai(&r->mobs, snowman_eid, 0)
              && gm_runtime_spawn_mob_fixture(
                  r, EW_TYPE_ZOMBIE, zombie_eid,
                  8.5, y, 26.4, 0.0, 0.0, 0.0,
                  0.0F, 20.0F, 1, 0, 0, 0),
          "active ranged target fixture initializes");
    CHECK(gm_runtime_set_entity_id_cursor(r, zombie_eid + 1),
          "active ranged fixture advances the entity ID cursor");
    r->restored_active_mobs_enabled = 1;
    r->mobs.snowman_target_eid[snowman] = zombie_eid;
    r->mobs.snowman_target_task_tick[snowman] = 1;
    r->mobs.snowman_ranged_attack_time[snowman] = -1;
    for (int tick = 0; tick < 20; ++tick)
        gm_runtime_tick(r, idle);
    CHECK(!snowman_projectile(r, snowman_eid),
          "Snow Golem waits the exact 20-tick first-shot interval");
    gm_runtime_tick(r, idle);
    GmRuntimeProjectile *projectile = snowman_projectile(r, snowman_eid);
    CHECK(projectile && projectile->eid == zombie_eid + 1
              && projectile->shooter_eid == snowman_eid
              && projectile->shooting_living
              && projectile->uuid_present
              && projectile->potion_item == 332,
          "active Snow Golem creates an owned snowball on tick 21");
    CHECK(r->mobs.snowman_shot_count == 0
              && r->mobs.snowman_ranged_attack_time[snowman] == 20,
          "runtime drains the launch queue and resets ranged cooldown");

    char save_root[256], error[256];
    SnowballOutcome uninterrupted, restored;
    snprintf(save_root, sizeof save_root,
             ".tmp/snowman-runtime-%ld", (long)getpid());
    clean_save(save_root);
    CHECK(gm_native_save_write(
              r, save_root, "snowman", error, sizeof error),
          "native save records the in-flight Snow Golem snowball");
    gm_runtime_tick(r, idle);
    CHECK(capture_snowball(r, snowman_eid, &uninterrupted),
          "capture uninterrupted owned-snowball continuation");
    CHECK(gm_native_save_load(
              r, cfg, save_root, "snowman", error, sizeof error),
          "native save restores the in-flight Snow Golem snowball");
    gm_runtime_tick(r, idle);
    CHECK(capture_snowball(r, snowman_eid, &restored)
              && !memcmp(&restored, &uninterrupted, sizeof restored),
          "save/reload preserves exact owned-snowball continuation");
    clean_save(save_root);
}

int main(void) {
    GmConfig cfg;
    GmRuntime runtime;
    char error[256];
    test_component();
    gm_config_defaults(&cfg);
    cfg.view_distance = 1;
    cfg.mobs = 0;
    CHECK(gm_runtime_init(&runtime, &cfg, error, sizeof error),
          "Snow Golem runtime initializes");
    if (fail) return 1;
    double y = (double)gm_world_surface_y(runtime.world, 8, 8) + 1.0;
    test_runtime(&runtime, &cfg, y);
    test_active_ranged(&runtime, &cfg, y);
    gm_runtime_destroy(&runtime);
    if (fail) return 1;
    puts("PASS Snow Golem runtime: shear, ranged AI, owned snowballs and save continuation");
    return 0;
}
