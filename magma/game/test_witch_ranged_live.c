#include "entity_blaze_fireball.h"
#include "entity_witch.h"
#include "game/runtime.h"
#include "tile_entity_brewing.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int checks, failures;
#define CHECK(C) do { \
    ++checks; \
    if (!(C)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", \
                __FILE__, __LINE__, #C); \
        ++failures; \
    } \
} while (0)

typedef struct {
    double offset;
    float health;
    unsigned int flags;
    uint64_t seed48;
    int potion;
} RangedCase;

static EwStore *store(GmMobLive *m) {
    return m->current ? &m->b : &m->a;
}

static void set_attack_time(GmMobLive *m, int slot, int value) {
    m->a.attack_time[slot] = value;
    m->b.attack_time[slot] = value;
}

static int init_runtime(GmRuntime *r) {
    GmConfig cfg;
    char error[256] = {0};
    gm_config_defaults(&cfg);
    cfg.seed = 0;
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 1;
    cfg.weather = 0;
    if (!gm_runtime_init(r, &cfg, error, sizeof error)) {
        fprintf(stderr, "runtime init: %s\n", error);
        return 0;
    }
    gm_runtime_set_pose_state(
        r, 8.5, 5.0, 8.5, 0.0F, 0.0F,
        0.0, 0.0, 0.0, 1, 0.0F);
    return 1;
}

static void expected_attack(
        JavaRandom *random, double witch_x, double offset, float health,
        unsigned int flags, EwitchRangedOutcome *outcome) {
    /* EntityLiving's initial ambient roll precedes Witch living AI. */
    (void)jrand_int_bound(random, 1000);
    EwitchSelfState self = {0};
    EwitchSelfConditions self_conditions = {
        0, 0, 0, 0, 0, 1, offset * offset, 26.0F, 26.0F
    };
    EwitchSelfOutcome self_outcome;
    ewitch_self_potion_step(
        random, &self, &self_conditions, &self_outcome, NULL);
    EwitchRangedConditions ranged = {
        self.drinking,
        (flags & GM_PLAYER_POTION_SLOWNESS) != 0,
        (flags & GM_PLAYER_POTION_POISON) != 0,
        (flags & GM_PLAYER_POTION_WEAKNESS) != 0,
        witch_x, 5.0, 8.5,
        8.5, 5.0, 8.5,
        0.0, 0.0, 1.62F, health
    };
    ewitch_ranged_attack(random, &ranged, outcome, NULL);
}

static void test_selection_and_queue(void) {
    static const RangedCase cases[] = {
        {9.0, 7.0F, 0, 5588, EWITCH_THROW_SLOWNESS},
        {9.0, 20.0F, GM_PLAYER_POTION_SLOWNESS, 5588,
            EWITCH_THROW_POISON},
        {5.0, 20.0F, 0, 5588, EWITCH_THROW_POISON},
        {5.0, 20.0F, GM_PLAYER_POTION_POISON, 5588,
            EWITCH_THROW_HARMING},
        {2.0, 7.0F, 0, 0, EWITCH_THROW_WEAKNESS},
        {2.0, 7.0F, 0, 5588, EWITCH_THROW_HARMING},
        {2.0, 7.0F, GM_PLAYER_POTION_WEAKNESS, 0,
            EWITCH_THROW_HARMING},
    };
    for (int c = 0; c < (int)(sizeof cases / sizeof cases[0]); ++c) {
        GmRuntime r;
        CHECK(init_runtime(&r));
        if (failures) return;
        double witch_x = 8.5 - cases[c].offset;
        int slot = gm_mobs_spawn_witch(&r.mobs, witch_x, 5.0, 8.5);
        int eid = store(&r.mobs)->id[slot];
        JavaRandom expected;
        EwitchRangedOutcome outcome;
        GmWitchShot shot;
        jrand_set_seed48(&expected, cases[c].seed48);
        expected_attack(
            &expected, witch_x, cases[c].offset, cases[c].health,
            cases[c].flags, &outcome);
        r.vitals.health = cases[c].health;
        r.player.health = cases[c].health;
        gm_mobs_set_player_potion_flags(&r.mobs, cases[c].flags);
        set_attack_time(&r.mobs, slot, 1);
        CHECK(gm_mobs_set_entity_random_state(
            &r.mobs, eid, cases[c].seed48, 0, 0.0));
        gm_mobs_tick(
            &r.mobs, r.world, NULL,
            (const struct McSinTable *)&r.sin_table,
            (struct PsvPlayer *)&r.player,
            (struct PvStats *)&r.vitals, r.ox, r.oz, r.dimension,
            r.clock.world_time, &r.clock, r.mob_griefing,
            &r.world_random_seed48, &r.math_random_seed48,
            &r.next_entity_id, r.do_mob_loot, &r.entities, 0.0F, 0.0F);
        CHECK(gm_mobs_take_witch_shot(&r.mobs, slot, &shot));
        CHECK(outcome.thrown && outcome.potion == cases[c].potion);
        CHECK(shot.potion == cases[c].potion
            && shot.x == outcome.spawn_x && shot.y == outcome.spawn_y
            && shot.z == outcome.spawn_z
            && shot.aim_x == outcome.aim_x
            && shot.aim_y == outcome.aim_y
            && shot.aim_z == outcome.aim_z
            && shot.sound_pitch == outcome.sound_pitch);
        CHECK(r.mobs.entity_random[slot].random.seed == expected.seed);
        CHECK(store(&r.mobs)->attack_time[slot] == 60);
        gm_runtime_destroy(&r);
    }
}

static GmRuntimeProjectile *find_potion(GmRuntime *r) {
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
        if (r->projectiles[i].active && r->projectiles[i].type == 6)
            return &r->projectiles[i];
    return NULL;
}

static void test_runtime_projectile(void) {
    GmRuntime r;
    JavaRandom witch_random;
    JavaGaussianRandom projectile_random;
    EwitchRangedOutcome outcome;
    EbfVector heading;
    CHECK(init_runtime(&r));
    if (failures) return;
    int slot = gm_mobs_spawn_witch(&r.mobs, 3.5, 5.0, 8.5);
    int witch_eid = store(&r.mobs)->id[slot];
    int projectile_eid = r.next_entity_id > r.mobs.next_id
        ? r.next_entity_id : r.mobs.next_id;
    set_attack_time(&r.mobs, slot, 1);
    CHECK(gm_mobs_set_entity_random_state(
        &r.mobs, witch_eid, UINT64_C(5588), 0, 0.0));
    jrand_set_seed48(&witch_random, UINT64_C(5588));
    expected_attack(&witch_random, 3.5, 5.0, 20.0F, 0, &outcome);
    jrand_gaussian_set_state(
        &projectile_random, UINT64_C(12345), 0, 0.0);
    CHECK(gm_runtime_set_next_potion_random_state(
        &r, UINT64_C(12345), 0, 0.0));
    heading = ebf_throwable_heading(
        &projectile_random, outcome.aim_x, outcome.aim_y,
        outcome.aim_z, 0.75F, 8.0F);
    gm_runtime_tick(&r, (GmAction){.hotbar_sel = -1});
    GmRuntimeProjectile *potion = find_potion(&r);
    CHECK(potion && potion->eid == projectile_eid
        && potion->shooter_eid == witch_eid && potion->shooting_living
        && !potion->player_thrower && potion->potion_item == TB_SPLASH_POTION
        && potion->potion_type == TB_PT_POISON && potion->age == 1);
    if (potion) {
        CHECK(potion->x == outcome.spawn_x + heading.x
            && potion->y == outcome.spawn_y + heading.y
            && potion->z == outcome.spawn_z + heading.z);
        CHECK(potion->vx == heading.x * (double)0.99F
            && potion->vy == heading.y * (double)0.99F - (double)0.05F
            && potion->vz == heading.z * (double)0.99F);
    }
    int found_sound = 0;
    for (int i = 0; i < gm_runtime_sound_event_count(&r); ++i) {
        GmRuntimeSoundEvent sound;
        if (gm_runtime_sound_event_get(&r, i, &sound)
                && sound.sound == GM_SOUND_WITCH_THROW
                && sound.category == GM_SOUND_CATEGORY_HOSTILE
                && sound.eid == witch_eid && sound.volume == 1.0F
                && sound.pitch == outcome.sound_pitch)
            found_sound = 1;
    }
    CHECK(found_sound);
    gm_runtime_destroy(&r);
}

static void test_drinking_and_cooldown(void) {
    GmRuntime r;
    GmWitchShot shot;
    CHECK(init_runtime(&r));
    if (failures) return;
    int slot = gm_mobs_spawn_witch(&r.mobs, 3.5, 5.0, 8.5);
    int eid = store(&r.mobs)->id[slot];
    CHECK(store(&r.mobs)->attack_time[slot] == -1);
    gm_mobs_tick(
        &r.mobs, r.world, NULL,
        (const struct McSinTable *)&r.sin_table,
        (struct PsvPlayer *)&r.player,
        (struct PvStats *)&r.vitals, r.ox, r.oz, r.dimension,
        r.clock.world_time, &r.clock, r.mob_griefing,
        &r.world_random_seed48, &r.math_random_seed48,
        &r.next_entity_id, r.do_mob_loot, &r.entities, 0.0F, 0.0F);
    CHECK(store(&r.mobs)->attack_time[slot] == 60
        && !gm_mobs_take_witch_shot(&r.mobs, slot, &shot));
    set_attack_time(&r.mobs, slot, 1);
    r.mobs.witch_drinking[slot] = 1;
    r.mobs.witch_attack_timer[slot] = 20;
    CHECK(gm_mobs_set_entity_random_state(
        &r.mobs, eid, UINT64_C(5588), 0, 0.0));
    gm_mobs_tick(
        &r.mobs, r.world, NULL,
        (const struct McSinTable *)&r.sin_table,
        (struct PsvPlayer *)&r.player,
        (struct PvStats *)&r.vitals, r.ox, r.oz, r.dimension,
        r.clock.world_time, &r.clock, r.mob_griefing,
        &r.world_random_seed48, &r.math_random_seed48,
        &r.next_entity_id, r.do_mob_loot, &r.entities, 0.0F, 0.0F);
    CHECK(store(&r.mobs)->attack_time[slot] == 60
        && r.mobs.witch_attack_timer[slot] == 19
        && !gm_mobs_take_witch_shot(&r.mobs, slot, &shot));
    gm_runtime_destroy(&r);
}

static void test_visibility_wait(void) {
    GmRuntime r;
    GmWitchShot shot;
    CHECK(init_runtime(&r));
    if (failures) return;
    int slot = gm_mobs_spawn_witch(&r.mobs, 3.5, 5.0, 8.5);
    int eid = store(&r.mobs)->id[slot];
    set_attack_time(&r.mobs, slot, 1);
    CHECK(gm_mobs_set_entity_random_state(
        &r.mobs, eid, UINT64_C(5588), 0, 0.0));
    gm_world_set_block(r.world, 6, 6, 8, 1);
    gm_mobs_tick(
        &r.mobs, r.world, NULL,
        (const struct McSinTable *)&r.sin_table,
        (struct PsvPlayer *)&r.player,
        (struct PvStats *)&r.vitals, r.ox, r.oz, r.dimension,
        r.clock.world_time, &r.clock, r.mob_griefing,
        &r.world_random_seed48, &r.math_random_seed48,
        &r.next_entity_id, r.do_mob_loot, &r.entities, 0.0F, 0.0F);
    CHECK(store(&r.mobs)->attack_time[slot] == 0
        && r.mobs.witch_see_time[slot] == 0
        && !gm_mobs_take_witch_shot(&r.mobs, slot, &shot));
    gm_world_set_block(r.world, 6, 6, 8, 0);
    gm_mobs_tick(
        &r.mobs, r.world, NULL,
        (const struct McSinTable *)&r.sin_table,
        (struct PsvPlayer *)&r.player,
        (struct PvStats *)&r.vitals, r.ox, r.oz, r.dimension,
        r.clock.world_time, &r.clock, r.mob_griefing,
        &r.world_random_seed48, &r.math_random_seed48,
        &r.next_entity_id, r.do_mob_loot, &r.entities, 0.0F, 0.0F);
    CHECK(store(&r.mobs)->attack_time[slot] == 60
        && r.mobs.witch_see_time[slot] == 1
        && gm_mobs_take_witch_shot(&r.mobs, slot, &shot));
    gm_runtime_destroy(&r);
}

int main(void) {
    test_selection_and_queue();
    test_runtime_projectile();
    test_drinking_and_cooldown();
    test_visibility_wait();
    if (failures) return 1;
    printf("witch_ranged_live: PASS (%d checks)\n", checks);
    return 0;
}
