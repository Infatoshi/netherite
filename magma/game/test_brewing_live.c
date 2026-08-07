#include <math.h>
#include <stdio.h>
#include <string.h>

#include "container_click.h"
#include "entity_blaze_fireball.h"
#include "game/runtime.h"
#include "tile_entity_brewing.h"

static int checks, failures;
#define CHECK(C) do { \
    ++checks; \
    if (!(C)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", \
                __FILE__, __LINE__, #C); \
        ++failures; \
    } \
} while (0)

static int stack_is(ICStack s, int item, int count, int meta) {
    return s.item == item && s.count == count && s.meta == meta;
}

static void test_live_kernel(void) {
    ICStack slots[BREWING_LIVE_SLOTS];
    BrewingLiveState state;
    int drops = -1;
    brewing_live_init(slots, &state);
    CHECK(brewing_live_insert(
        slots, 0, ic_mk(TB_POTION, 1, TB_PT_WATER)) == 1);
    CHECK(brewing_live_insert(
        slots, 0, ic_mk(TB_POTION, 1, TB_PT_WATER)) == 0);
    CHECK(brewing_live_insert(
        slots, 1, ic_mk(TB_GLASS_BOTTLE, 1, 0)) == 0);
    CHECK(brewing_live_insert(
        slots, 3, ic_mk(TB_FISH, 1, 0)) == 0);
    CHECK(brewing_live_insert(
        slots, 3, ic_mk(TB_NETHER_WART, 2, 0)) == 2);
    CHECK(brewing_live_insert(
        slots, 4, ic_mk(TB_BLAZE_POWDER, 1, 0)) == 1);
    CHECK(brewing_live_comparator_strength(slots) == 3);
    CHECK((brewing_live_tick(slots, &state, &drops)
           & TB_TICK_CHANGED) != 0);
    CHECK(state.brew_time == 400 && state.fuel == 19 && drops == 0);
    CHECK(state.bottle_bits == 1);
    for (int tick = 0; tick < 400; ++tick)
        (void)brewing_live_tick(slots, &state, &drops);
    CHECK(stack_is(slots[0], TB_POTION, 1, TB_PT_AWKWARD));
    CHECK(stack_is(slots[3], TB_NETHER_WART, 1, 0));
    CHECK(state.brew_time == 0 && state.fuel == 19);

    brewing_live_init(slots, &state);
    slots[0] = ic_mk(TB_SPLASH_POTION, 1, TB_PT_AWKWARD);
    slots[3] = ic_mk(TB_DRAGON_BREATH, 2, 0);
    slots[4] = ic_mk(TB_BLAZE_POWDER, 1, 0);
    for (int tick = 0; tick < 401; ++tick)
        (void)brewing_live_tick(slots, &state, &drops);
    CHECK(stack_is(slots[0], TB_LINGERING_POTION, 1, TB_PT_AWKWARD));
    CHECK(stack_is(slots[3], TB_DRAGON_BREATH, 1, 0));
    CHECK(drops == 1);
}

static GmRuntimeStaticContainer find_stand(const GmRuntime *r) {
    GmRuntimeStaticContainer out;
    memset(&out, 0, sizeof out);
    for (int i = 0; i < gm_runtime_static_container_count(r); ++i) {
        GmRuntimeStaticContainer got;
        if (gm_runtime_static_container_get(r, i, &got)
                && got.block == 117)
            return got;
    }
    return out;
}

static GmRuntimeProjectile *find_potion(GmRuntime *r) {
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
        if (r->projectiles[i].active && r->projectiles[i].type == 6)
            return &r->projectiles[i];
    return NULL;
}

static GmRuntimeAreaEffectCloud *find_cloud(GmRuntime *r) {
    for (int i = 0; i < GM_RUNTIME_AREA_EFFECT_CLOUDS; ++i)
        if (r->area_effect_clouds[i].state.active)
            return &r->area_effect_clouds[i];
    return NULL;
}

static int top_block_y(const GmRuntime *r, int x, int z) {
    for (int y = 20; y >= 0; --y)
        if (gm_world_block(r->world, x, y, z) != 0)
            return y;
    return -1;
}

static int mob_health(const GmRuntime *r, int eid, float *health) {
    GmMobExplosionTarget targets[GM_MOB_CAPACITY];
    int count = gm_mobs_explosion_targets(
        &r->mobs, r->dimension, targets, GM_MOB_CAPACITY);
    for (int i = 0; i < count; ++i)
        if (targets[i].eid == eid) {
            if (health) *health = targets[i].health;
            return 1;
        }
    return 0;
}

static int mob_slot_health(
        const GmRuntime *r, int slot, float *health) {
    GmMobExplosionTarget targets[GM_MOB_CAPACITY];
    int count = gm_mobs_explosion_targets(
        &r->mobs, r->dimension, targets, GM_MOB_CAPACITY);
    for (int i = 0; i < count; ++i)
        if (targets[i].slot == slot) {
            if (health) *health = targets[i].health;
            return 1;
        }
    return 0;
}

static int mob_slot_by_eid(const GmRuntime *r, int eid) {
    GmMobExplosionTarget targets[GM_MOB_CAPACITY];
    int count = gm_mobs_explosion_targets(
        &r->mobs, r->dimension, targets, GM_MOB_CAPACITY);
    for (int i = 0; i < count; ++i)
        if (targets[i].eid == eid)
            return targets[i].slot;
    return -1;
}

static int mob_effect(
        const GmRuntime *r, int slot, int id, PtMobEffect *out) {
    for (int i = 0; i < gm_mobs_potion_effect_count(&r->mobs, slot); ++i) {
        PtMobEffect effect;
        if (gm_mobs_potion_effect_get(&r->mobs, slot, i, &effect)
                && effect.id == id) {
            if (out) *out = effect;
            return 1;
        }
    }
    return 0;
}

static const EwStore *mob_store(const GmRuntime *r) {
    return r->mobs.current ? &r->mobs.b : &r->mobs.a;
}

static void throw_selected_potion(
        GmRuntime *r, int item, int type, long long seed) {
    GmAction action;
    JavaGaussianRandom random;
    memset(&action, 0, sizeof action);
    action.do_place = 1;
    CHECK(gm_runtime_set_selected_slot(r, 0));
    CHECK(gm_runtime_set_inventory(r, 0, item, 1, type));
    ebf_entity_random_init(&random, seed);
    CHECK(gm_runtime_set_next_potion_random_state(
        r, random.random.seed, 0, 0.0));
    gm_runtime_tick(r, action);
    memset(&action, 0, sizeof action);
    for (int tick = 0; tick < 40 && find_potion(r); ++tick)
        gm_runtime_tick(r, action);
    CHECK(find_potion(r) == NULL);
}

static void test_potion_mob_runtime(void) {
    GmConfig cfg;
    GmRuntime r;
    char err[256];
    float health = 0.0F;
    int top, first_slot, second_slot;
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.brewing = 1;
    cfg.mobs = 1;

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    CHECK(top >= 0);
    gm_runtime_set_pose_state(
        &r, 8.5, (double)top + 1.0, 8.5, 0.0F, 20.0F,
        0.0, 0.0, 0.0, 1, 0.0F);
    CHECK(gm_runtime_spawn_mob_fixture(
        &r, EW_TYPE_COW, 501, 8.5, (double)top + 1.0, 10.5,
        0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0));
    throw_selected_potion(&r, TB_SPLASH_POTION, TB_PT_HARMING, 31001);
    CHECK(mob_health(&r, 501, &health) && health == 4.0F);
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    gm_runtime_set_pose_state(
        &r, 8.5, (double)top + 1.0, 8.5, 0.0F, 20.0F,
        0.0, 0.0, 0.0, 1, 0.0F);
    first_slot = gm_mobs_spawn(
        &r.mobs, EW_TYPE_ZOMBIE, 8.5, (double)top + 1.0, 10.5);
    CHECK(first_slot > 0);
    throw_selected_potion(&r, TB_SPLASH_POTION, TB_PT_HEALING, 31002);
    CHECK(mob_slot_health(&r, first_slot, &health) && health == 14.0F);
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    gm_runtime_set_pose_state(
        &r, 8.5, (double)top + 1.0, 8.5, 0.0F, 20.0F,
        0.0, 0.0, 0.0, 1, 0.0F);
    first_slot = gm_mobs_spawn(
        &r.mobs, EW_TYPE_ZOMBIE, 8.5, (double)top + 1.0, 10.5);
    CHECK(first_slot > 0);
    CHECK(gm_mobs_apply_explosion(
        &r.mobs, first_slot, 15.0F, 0.0, 0.0, 0.0,
        &r.entities) == 2);
    throw_selected_potion(&r, TB_SPLASH_POTION, TB_PT_HARMING, 31003);
    CHECK(mob_slot_health(&r, first_slot, &health) && health == 9.0F);
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    gm_runtime_set_pose_state(
        &r, 8.5, (double)top + 1.0, 8.5, 0.0F, 20.0F,
        0.0, 0.0, 0.0, 1, 0.0F);
    first_slot = gm_mobs_spawn(
        &r.mobs, EW_TYPE_BLAZE, 8.5, (double)top + 1.0, 10.5);
    second_slot = gm_mobs_spawn(
        &r.mobs, EW_TYPE_ENDERMAN, 10.0, (double)top + 1.0, 10.5);
    CHECK(first_slot > 0 && second_slot > 0);
    throw_selected_potion(&r, TB_SPLASH_POTION, TB_PT_WATER, 31004);
    CHECK(mob_slot_health(&r, first_slot, &health) && health == 19.0F);
    CHECK(mob_slot_health(&r, second_slot, &health) && health == 39.0F);
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    gm_runtime_set_pose_state(
        &r, 8.5, (double)top + 1.0, 8.5, 0.0F, 30.0F,
        0.0, 0.0, 0.0, 1, 0.0F);
    CHECK(gm_runtime_spawn_mob_fixture(
        &r, EW_TYPE_COW, 506, 8.5, (double)top + 1.0, 10.5,
        0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0));
    CHECK(gm_runtime_spawn_mob_fixture(
        &r, EW_TYPE_COW, 507, 10.0, (double)top + 1.0, 10.5,
        0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0));
    throw_selected_potion(
        &r, TB_LINGERING_POTION, TB_PT_HARMING, 31005);
    {
        GmAction idle;
        GmRuntimeAreaEffectCloud *cloud = find_cloud(&r);
        memset(&idle, 0, sizeof idle);
        CHECK(cloud != NULL);
        while (cloud && cloud->state.age < 10) {
            gm_runtime_tick(&r, idle);
            cloud = find_cloud(&r);
        }
        CHECK(cloud != NULL && cloud->state.age == 10);
        CHECK(mob_health(&r, 506, &health) && health == 7.0F);
        CHECK(mob_health(&r, 507, &health) && health == 7.0F);
        CHECK(cloud != NULL
              && cloud->mob_next_application[1] == 30
              && cloud->mob_next_application[2] == 30);
    }
    gm_runtime_destroy(&r);
}

static void test_splash_potion_runtime(void) {
    GmConfig cfg;
    GmRuntime r;
    GmAction throw_action, idle;
    JavaGaussianRandom expected_random;
    EbfVector expected_heading;
    GmRuntimeProjectile *potion;
    GmEntityView view;
    char err[256];
    int eid;
    memset(&throw_action, 0, sizeof throw_action);
    memset(&idle, 0, sizeof idle);
    throw_action.do_place = 1;
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.brewing = 1;
    cfg.mobs = 1;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    gm_runtime_set_pose_state(&r, 8.5, 5.0, 8.5, 0.0f, 0.0f,
                              0.0, 0.0, 0.0, 1, 0.0f);
    CHECK(gm_runtime_set_selected_slot(&r, 0));
    CHECK(gm_runtime_set_inventory(
        &r, 0, TB_SPLASH_POTION, 1, TB_PT_STRONG_SWIFTNESS));
    ebf_entity_random_init(&expected_random, 12345);
    CHECK(gm_runtime_set_next_potion_random_state(
        &r, expected_random.random.seed, 0, 0.0));
    expected_heading = ebf_throwable_heading(
        &expected_random, 0.0,
        -(double)mc_sin(&r.sin_table, -20.0F * 0.017453292F),
        (double)mc_cos(&r.sin_table, 0.0F), 0.5F, 1.0F);
    eid = r.next_entity_id;
    gm_runtime_tick(&r, throw_action);
    potion = find_potion(&r);
    CHECK(stack_is(isr_get_stack(&r.player.inv, 0), 0, 0, 0));
    CHECK(potion != NULL && potion->eid == eid
          && potion->potion_type == TB_PT_STRONG_SWIFTNESS
          && potion->age == 1);
    if (potion) {
        double spawn_y = 5.0 + PSV_EYE_HEIGHT - 0.10000000149011612;
        CHECK(potion->x == 8.5 + expected_heading.x);
        CHECK(potion->y == spawn_y + expected_heading.y);
        CHECK(potion->z == 8.5 + expected_heading.z);
        CHECK(potion->vx == expected_heading.x * (double)0.99F);
        CHECK(potion->vy == expected_heading.y * (double)0.99F
              - (double)0.05F);
        CHECK(potion->vz == expected_heading.z * (double)0.99F);
        CHECK(gm_runtime_projectile_views(&r, &view, 1) == 1);
        CHECK(view.type == GM_VIEW_BILLBOARD
              && view.item_id == TB_SPLASH_POTION
              && view.item_meta == TB_PT_STRONG_SWIFTNESS);
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    {
        int top = top_block_y(&r, 8, 8);
        CHECK(top >= 0);
        gm_runtime_set_pose_state(
            &r, 8.5, (double)top + 1.0, 8.5, 0.0f, 89.0f,
            0.0, 0.0, 0.0, 1, 0.0f);
        CHECK(gm_runtime_set_selected_slot(&r, 0));
        CHECK(gm_runtime_set_inventory(
            &r, 0, TB_SPLASH_POTION, 1, TB_PT_STRONG_SWIFTNESS));
        ebf_entity_random_init(&expected_random, 54321);
        CHECK(gm_runtime_set_next_potion_random_state(
            &r, expected_random.random.seed, 0, 0.0));
        gm_runtime_tick(&r, throw_action);
        for (int tick = 0; tick < 40 && find_potion(&r); ++tick)
            gm_runtime_tick(&r, idle);
        CHECK(find_potion(&r) == NULL);
        CHECK(stack_is(isr_get_stack(&r.player.inv, 0), 0, 0, 0));
        CHECK(r.potion_count == 1);
        CHECK(r.potions[0].id == 1 && r.potions[0].amplifier == 1);
        CHECK(r.potions[0].duration >= 21
              && r.potions[0].duration <= 1800);
        CHECK(fabs(r.player.movement_speed_multiplier
                   - 1.4000000059604645) < 1.0e-12);
        {
            int found = 0;
            for (int i = 0; i < gm_runtime_world_event_count(&r); ++i) {
                GmRuntimeWorldEvent event;
                if (gm_runtime_world_event_get(&r, i, &event)
                        && event.id == 2002 && event.data == 8171462)
                    found = 1;
            }
            CHECK(found);
        }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    {
        int top = top_block_y(&r, 8, 8);
        GmRuntimeAreaEffectCloud *cloud;
        CHECK(top >= 0);
        gm_runtime_set_pose_state(
            &r, 8.5, (double)top + 1.0, 8.5, 0.0f, 89.0f,
            0.0, 0.0, 0.0, 1, 0.0f);
        CHECK(gm_runtime_set_selected_slot(&r, 0));
        CHECK(gm_runtime_set_inventory(
            &r, 0, TB_LINGERING_POTION, 1, TB_PT_STRONG_SWIFTNESS));
        ebf_entity_random_init(&expected_random, 98765);
        CHECK(gm_runtime_set_next_potion_random_state(
            &r, expected_random.random.seed, 0, 0.0));
        gm_runtime_tick(&r, throw_action);
        potion = find_potion(&r);
        CHECK(potion != NULL && potion->potion_item == TB_LINGERING_POTION);
        for (int tick = 0; tick < 40 && find_potion(&r); ++tick)
            gm_runtime_tick(&r, idle);
        cloud = find_cloud(&r);
        CHECK(find_potion(&r) == NULL && cloud != NULL);
        CHECK(r.area_effect_cloud_count == 1);
        CHECK(r.potion_count == 0);
        while (cloud && cloud->state.age < 9) {
            gm_runtime_tick(&r, idle);
            cloud = find_cloud(&r);
        }
        CHECK(cloud != NULL && cloud->state.age == 9
              && r.potion_count == 0);
        gm_runtime_tick(&r, idle);
        cloud = find_cloud(&r);
        CHECK(cloud != NULL && cloud->state.age == 10);
        CHECK(r.potion_count == 1 && r.potions[0].id == 1
              && r.potions[0].amplifier == 1
              && r.potions[0].duration == 450);
        CHECK(cloud != NULL
              && cloud->state.radius == 3.0F - 3.0F / 600.0F - 0.5F
              && cloud->state.next_application == 30);
        for (int tick = 0; tick < 20; ++tick)
            gm_runtime_tick(&r, idle);
        cloud = find_cloud(&r);
        CHECK(cloud != NULL && cloud->state.age == 30);
        CHECK(r.potions[0].duration == 450);
        {
            float expected_radius = 3.0F;
            float radius_per_tick = -expected_radius / 600.0F;
            expected_radius += radius_per_tick;
            expected_radius += -0.5F;
            for (int age = 11; age <= 30; ++age)
                expected_radius += radius_per_tick;
            expected_radius += -0.5F;
            CHECK(cloud != NULL && cloud->state.radius == expected_radius
                  && cloud->state.next_application == 50);
        }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    {
        int top = top_block_y(&r, 8, 8);
        CHECK(top >= 0);
        CHECK(gm_runtime_set_block(&r, 8, top + 1, 8, 51, 0));
        gm_runtime_set_pose_state(
            &r, 8.5, (double)top + 1.0, 8.5, 0.0f, 89.0f,
            0.0, 0.0, 0.0, 1, 0.0f);
        CHECK(gm_runtime_set_selected_slot(&r, 0));
        CHECK(gm_runtime_set_inventory(
            &r, 0, TB_SPLASH_POTION, 1, TB_PT_WATER));
        ebf_entity_random_init(&expected_random, 13579);
        CHECK(gm_runtime_set_next_potion_random_state(
            &r, expected_random.random.seed, 0, 0.0));
        gm_runtime_tick(&r, throw_action);
        for (int tick = 0; tick < 40 && find_potion(&r); ++tick)
            gm_runtime_tick(&r, idle);
        CHECK(find_potion(&r) == NULL);
        CHECK(gm_world_block(r.world, 8, top + 1, 8) == 0);
        CHECK(find_cloud(&r) == NULL && r.potion_count == 0);
        CHECK(r.area_effect_cloud_count == 0);
        {
            int found = 0;
            for (int i = 0; i < gm_runtime_world_event_count(&r); ++i) {
                GmRuntimeWorldEvent event;
                if (gm_runtime_world_event_get(&r, i, &event)
                        && event.id == 2002 && event.data == 3694022)
                    found = 1;
            }
            CHECK(found);
        }
    }
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    {
        int top = top_block_y(&r, 8, 8);
        CHECK(top >= 0);
        gm_runtime_set_pose_state(
            &r, 8.5, (double)top + 1.0, 8.5, 0.0f, 89.0f,
            0.0, 0.0, 0.0, 1, 0.0f);
        gm_runtime_set_vitals(&r, 20.0F, 20);
        CHECK(gm_runtime_set_selected_slot(&r, 0));
        CHECK(gm_runtime_set_inventory(
            &r, 0, TB_SPLASH_POTION, 1, TB_PT_STRONG_HARMING));
        ebf_entity_random_init(&expected_random, 24680);
        CHECK(gm_runtime_set_next_potion_random_state(
            &r, expected_random.random.seed, 0, 0.0));
        gm_runtime_tick(&r, throw_action);
        for (int tick = 0; tick < 40 && find_potion(&r); ++tick)
            gm_runtime_tick(&r, idle);
        CHECK(find_potion(&r) == NULL);
        CHECK(r.vitals.health < 20.0F && r.vitals.health >= 8.0F);
        CHECK(r.potion_count == 0);
        {
            int found = 0;
            for (int i = 0; i < gm_runtime_world_event_count(&r); ++i) {
                GmRuntimeWorldEvent event;
                if (gm_runtime_world_event_get(&r, i, &event)
                        && event.id == 2007 && event.data == 4393481)
                    found = 1;
            }
            CHECK(found);
        }
    }
    gm_runtime_destroy(&r);
}

static void test_runtime_vertical_slice(void) {
    GmConfig cfg;
    GmRuntime r;
    GmAction idle;
    char err[256];
    int bx, by, bz;
    memset(&idle, 0, sizeof idle);
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.brewing = 1;
    cfg.mobs = 1;
    CHECK(gm_config_validate_runtime(&cfg, 0, 0, err, sizeof err) == 0);
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    bx = (int)floor(r.player.ent.posX) + 2;
    by = (int)floor(r.player.ent.posY);
    bz = (int)floor(r.player.ent.posZ);
    CHECK(gm_runtime_set_block(&r, bx, by, bz, 117, 0));
    CHECK(gm_runtime_brewing_set_slot(
        &r, 0, bx, by, bz, 0, TB_POTION, 1, TB_PT_WATER, 0, 0));
    CHECK(gm_runtime_brewing_set_slot(
        &r, 0, bx, by, bz, 3, TB_NETHER_WART, 1, 0, 0, 0));
    CHECK(gm_runtime_brewing_set_slot(
        &r, 0, bx, by, bz, 4, TB_BLAZE_POWDER, 1, 0, 0, 0));
    CHECK(gm_runtime_use_block(&r, bx, by, bz));
    CHECK(r.container == 4 && r.active_static_container >= 0);

    gm_runtime_tick(&r, idle);
    {
        GmRuntimeStaticContainer stand = find_stand(&r);
        CHECK(stand.active && stand.block == 117 && stand.size == 5);
        CHECK(stand.brewing.brew_time == 400 && stand.brewing.fuel == 19);
        CHECK(stack_is(stand.slots[4], 0, 0, 0));
        CHECK(gm_world_meta(r.world, bx, by, bz) == 1);
    }
    for (int tick = 0; tick < 400; ++tick) gm_runtime_tick(&r, idle);
    {
        GmRuntimeStaticContainer stand = find_stand(&r);
        CHECK(stack_is(stand.slots[0], TB_POTION, 1, TB_PT_AWKWARD));
        CHECK(stack_is(stand.slots[3], 0, 0, 0));
        CHECK(stand.brewing.brew_time == 0 && stand.brewing.fuel == 19);
    }
    {
        int found = 0;
        for (int i = 0; i < gm_runtime_world_event_count(&r); ++i) {
            GmRuntimeWorldEvent event;
            if (gm_runtime_world_event_get(&r, i, &event)
                    && event.id == 1035 && event.x == bx
                    && event.y == by && event.z == bz)
                found = 1;
        }
        CHECK(found);
    }

    CHECK(gm_container_click(&r, GMC_BREWING0, 0, CC_CLICK_PICKUP));
    CHECK(stack_is(gm_player_cursor(), TB_POTION, 1, TB_PT_AWKWARD));
    CHECK(gm_world_meta(r.world, bx, by, bz) == 1);
    gm_runtime_tick(&r, idle);
    CHECK(gm_world_meta(r.world, bx, by, bz) == 0);
    gm_player_cursor_set(ic_empty());

    CHECK(gm_runtime_brewing_set_slot(
        &r, 0, bx, by, bz, 3, TB_REDSTONE, 3, 0, 0, 7));
    CHECK(gm_runtime_set_block(&r, bx, by, bz, 0, 0));
    CHECK(gm_runtime_static_container_count(&r) == 0);
    CHECK(r.container == 0 && r.active_static_container == -1);
    {
        int count = 0;
        for (int i = 0; i < GM_LIVE_MAX; ++i)
            if (r.entities.ents[i].active
                    && r.entities.ents[i].item == TB_REDSTONE)
                count += r.entities.ents[i].count;
        CHECK(count == 3);
    }

    {
        GmAction drink;
        memset(&drink, 0, sizeof drink);
        drink.use = 1;
        CHECK(gm_runtime_set_selected_slot(&r, 0));
        CHECK(gm_runtime_set_inventory(
            &r, 0, TB_POTION, 1, TB_PT_STRONG_SWIFTNESS));
        for (int tick = 0; tick < 32; ++tick)
            gm_runtime_tick(&r, drink);
        CHECK(stack_is(
            isr_get_stack(&r.player.inv, 0), TB_GLASS_BOTTLE, 1, 0));
        CHECK(r.potion_count == 1);
        CHECK(r.potions[0].id == 1 && r.potions[0].amplifier == 1
              && r.potions[0].duration == 1800);
        CHECK(fabs(r.player.movement_speed_multiplier
                   - 1.4000000059604645) < 1.0e-12);
        gm_runtime_tick(&r, idle);
        CHECK(r.potions[0].duration == 1799);

        /* A weaker but longer effect does not replace a stronger active one:
         * PotionEffect.combine compares amplifier before duration. */
        CHECK(gm_runtime_set_inventory(
            &r, 0, TB_POTION, 1, TB_PT_LONG_SWIFTNESS));
        for (int tick = 0; tick < 32; ++tick)
            gm_runtime_tick(&r, drink);
        CHECK(r.potion_count == 1);
        CHECK(r.potions[0].id == 1 && r.potions[0].amplifier == 1
              && r.potions[0].duration == 1767);

        CHECK(gm_runtime_set_inventory(&r, 0, 335, 1, 0));
        for (int tick = 0; tick < 32; ++tick)
            gm_runtime_tick(&r, drink);
        CHECK(stack_is(isr_get_stack(&r.player.inv, 0), 325, 1, 0));
        CHECK(r.potion_count == 0);
        CHECK(r.player.movement_speed_multiplier == 1.0);
    }
    gm_runtime_destroy(&r);
}

static void test_potion_state_fixtures(void) {
    GmConfig cfg;
    GmRuntime r;
    GmAction idle;
    GmRuntimeProjectile *potion;
    GmRuntimeAreaEffectCloud *cloud;
    char err[256];
    int top;
    memset(&idle, 0, sizeof idle);
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.brewing = 1;
    cfg.mobs = 0;

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    CHECK(!gm_runtime_spawn_potion_fixture(
        &r, 700, TB_POTION, TB_PT_HARMING,
        12.5, 100.0, 8.5, 0.25, 0.5, -0.125, 7));
    CHECK(gm_runtime_spawn_potion_fixture(
        &r, 701, TB_SPLASH_POTION, TB_PT_HARMING,
        12.5, 100.0, 8.5, 0.25, 0.5, -0.125, 7));
    gm_runtime_tick(&r, idle);
    potion = find_potion(&r);
    CHECK(potion != NULL && potion->eid == 701 && potion->age == 8);
    CHECK(potion != NULL
          && potion->x == 12.75 && potion->y == 100.5
          && potion->z == 8.375);
    CHECK(potion != NULL
          && potion->vx == 0.25 * (double)0.99F
          && potion->vy == 0.5 * (double)0.99F - (double)0.05F
          && potion->vz == -0.125 * (double)0.99F);
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    CHECK(top >= 0);
    gm_runtime_set_pose_state(
        &r, 8.5, (double)top + 1.0, 8.5, 0.0F, 0.0F,
        0.0, 0.0, 0.0, 1, 0.0F);
    CHECK(!gm_runtime_spawn_area_effect_cloud_fixture(
        &r, 702, TB_PT_STRONG_SWIFTNESS, 8.5, (double)top + 1.0, 8.5,
        9, 600, 10, 20, 0.49F, -0.5F, -0.005F, 0));
    CHECK(gm_runtime_spawn_area_effect_cloud_fixture(
        &r, 703, TB_PT_STRONG_SWIFTNESS, 8.5, (double)top + 1.0, 8.5,
        9, 600, 10, 20, 3.0F, -0.5F, -0.005F, 0));
    gm_runtime_tick(&r, idle);
    cloud = find_cloud(&r);
    CHECK(cloud != NULL && cloud->eid == 703 && cloud->state.age == 10);
    CHECK(cloud != NULL
          && cloud->state.radius == 3.0F - 0.005F - 0.5F
          && cloud->state.next_application == 30);
    CHECK(r.potion_count == 1 && r.potions[0].id == 1
          && r.potions[0].amplifier == 1 && r.potions[0].duration == 450);
    gm_runtime_destroy(&r);
}

static void test_mob_status_effect_runtime(void) {
    GmConfig cfg;
    GmRuntime r;
    GmAction idle;
    PtMobEffect effect;
    char err[256];
    float health = 0.0F;
    int top, slot, undead_slot;
    memset(&idle, 0, sizeof idle);
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.brewing = 1;
    cfg.mobs = 1;

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    CHECK(gm_runtime_spawn_mob_fixture(
        &r, EW_TYPE_COW, 710, 8.5, (double)top + 1.0, 10.5,
        0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0));
    slot = mob_slot_by_eid(&r, 710);
    CHECK(slot > 0);
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 1, 0, 100));
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 1, 1, 50));
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 1, 0, 200));
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 1, 1, 80));
    CHECK(mob_effect(&r, slot, 1, &effect)
          && effect.amplifier == 1 && effect.duration == 80);
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 12, 0, 1));
    CHECK(gm_runtime_set_mob_fire_ticks(&r, 710, 1));
    gm_runtime_tick(&r, idle);
    CHECK(mob_health(&r, 710, &health) && health == 10.0F);
    CHECK(!mob_effect(&r, slot, 12, NULL));
    CHECK(mob_effect(&r, slot, 1, &effect) && effect.duration == 79);
    CHECK(gm_runtime_set_mob_fire_ticks(&r, 710, 1));
    gm_runtime_tick(&r, idle);
    CHECK(mob_health(&r, 710, &health) && health == 9.0F);
    gm_runtime_destroy(&r);

    cfg.mobs = 0;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    CHECK(gm_runtime_spawn_mob_fixture(
        &r, EW_TYPE_COW, 711, 8.5, (double)top + 1.0, 10.5,
        0.0, 0.0, 0.0, 0.0F, 5.0F, 1, 0, 0, 0));
    slot = mob_slot_by_eid(&r, 711);
    CHECK(slot > 0);
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 10, 0, 50));
    gm_runtime_tick(&r, idle);
    CHECK(mob_health(&r, 711, &health) && health == 6.0F);
    CHECK(mob_effect(&r, slot, 10, &effect) && effect.duration == 49);
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 19, 1, 12));
    gm_runtime_tick(&r, idle);
    CHECK(mob_health(&r, 711, &health) && health == 5.0F);
    CHECK(mob_effect(&r, slot, 19, &effect) && effect.duration == 11);
    undead_slot = gm_mobs_spawn(
        &r.mobs, EW_TYPE_ZOMBIE, 12.5, (double)top + 1.0, 10.5);
    CHECK(undead_slot > 0);
    CHECK(!gm_mobs_apply_potion_effect(
        &r.mobs, undead_slot, 19, 0, 900));
    CHECK(!gm_mobs_apply_potion_effect(
        &r.mobs, undead_slot, 10, 0, 900));
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    gm_runtime_set_pose_state(
        &r, 8.5, (double)top + 1.0, 8.5, 0.0F, 20.0F,
        0.0, 0.0, 0.0, 1, 0.0F);
    CHECK(gm_runtime_spawn_mob_fixture(
        &r, EW_TYPE_COW, 712, 8.5, (double)top + 1.0, 10.5,
        0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0));
    throw_selected_potion(&r, TB_SPLASH_POTION, TB_PT_POISON, 32001);
    slot = mob_slot_by_eid(&r, 712);
    CHECK(slot > 0 && mob_effect(&r, slot, 19, &effect));
    CHECK(effect.amplifier == 0
          && effect.duration > 20 && effect.duration <= 900);
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    gm_runtime_set_pose_state(
        &r, 8.5, (double)top + 1.0, 8.5, 0.0F, 30.0F,
        0.0, 0.0, 0.0, 1, 0.0F);
    CHECK(gm_runtime_spawn_mob_fixture(
        &r, EW_TYPE_COW, 713, 8.5, (double)top + 1.0, 10.5,
        0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0));
    throw_selected_potion(
        &r, TB_LINGERING_POTION, TB_PT_STRONG_POISON, 32002);
    {
        GmRuntimeAreaEffectCloud *cloud = find_cloud(&r);
        while (cloud && cloud->state.age < 10) {
            gm_runtime_tick(&r, idle);
            cloud = find_cloud(&r);
        }
        slot = mob_slot_by_eid(&r, 713);
        CHECK(cloud != NULL && cloud->state.age == 10 && slot > 0);
        CHECK(mob_effect(&r, slot, 19, &effect)
              && effect.amplifier == 1 && effect.duration == 108);
        CHECK(cloud != NULL
              && cloud->mob_next_application[slot] == 30);
    }
    gm_runtime_destroy(&r);
}

static double run_zombie_chase_effect(int potion_id, int duration) {
    GmConfig cfg;
    GmRuntime r;
    GmAction idle;
    char err[256];
    double z = NAN;
    int top, slot;
    memset(&idle, 0, sizeof idle);
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.mobs = 1;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    gm_runtime_set_pose_state(
        &r, 8.5, (double)top + 1.0, 8.5, 0.0F, 0.0F,
        0.0, 0.0, 0.0, 1, 0.0F);
    r.mobs.active_dimension = r.dimension;
    slot = gm_mobs_spawn(
        &r.mobs, EW_TYPE_ZOMBIE, 8.5, (double)top + 1.0, 14.5);
    CHECK(slot > 0);
    if (potion_id)
        CHECK(gm_mobs_apply_potion_effect(
            &r.mobs, slot, potion_id, 0, duration));
    gm_runtime_tick(&r, idle);
    if (slot > 0) z = mob_store(&r)->z[slot];
    gm_runtime_destroy(&r);
    return z;
}

static float run_zombie_melee_effect(int first_id, int second_id) {
    GmConfig cfg;
    GmRuntime r;
    GmAction idle;
    char err[256];
    float health;
    int top, slot;
    memset(&idle, 0, sizeof idle);
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.mobs = 1;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    gm_runtime_set_pose_state(
        &r, 8.5, (double)top + 1.0, 8.5, 0.0F, 0.0F,
        0.0, 0.0, 0.0, 1, 0.0F);
    r.mobs.active_dimension = r.dimension;
    slot = gm_mobs_spawn(
        &r.mobs, EW_TYPE_ZOMBIE, 8.5, (double)top + 1.0, 10.0);
    CHECK(slot > 0);
    if (first_id)
        CHECK(gm_mobs_apply_potion_effect(
            &r.mobs, slot, first_id, 0, 2));
    if (second_id)
        CHECK(gm_mobs_apply_potion_effect(
            &r.mobs, slot, second_id, 0, 2));
    gm_runtime_tick(&r, idle);
    health = r.vitals.health;
    gm_runtime_destroy(&r);
    return health;
}

static void test_mob_attribute_effect_runtime(void) {
    GmConfig cfg;
    GmRuntime r;
    GmAction idle;
    char err[256];
    double base_z = run_zombie_chase_effect(0, 0);
    double speed_z = run_zombie_chase_effect(1, 20);
    double slowness_z = run_zombie_chase_effect(2, 20);
    double expired_z = run_zombie_chase_effect(1, 1);
    CHECK(isfinite(base_z) && speed_z < base_z);
    CHECK(isfinite(slowness_z) && slowness_z > base_z);
    CHECK(expired_z == base_z);
    CHECK(run_zombie_melee_effect(5, 0) == 14.0F);
    CHECK(run_zombie_melee_effect(18, 0) == 20.0F);
    CHECK(run_zombie_melee_effect(5, 18) == 18.0F);

    memset(&idle, 0, sizeof idle);
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.mobs = 1;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    {
        int top = top_block_y(&r, 8, 8);
        gm_runtime_set_pose_state(
            &r, 8.5, (double)top + 1.0, 8.5, 0.0F, 0.0F,
            0.0, 0.0, 0.0, 1, 0.0F);
        r.mobs.active_dimension = r.dimension;
        int slot = gm_mobs_spawn(
            &r.mobs, EW_TYPE_SLIME,
            8.5, (double)top + 1.0, 14.5);
        CHECK(slot > 0);
        CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 8, 1, 2));
        gm_runtime_tick(&r, idle);
        CHECK(mob_store(&r)->vy[slot] == 0.5292000003695486);
    }
    gm_runtime_destroy(&r);
}

static void test_mob_damage_effect_runtime(void) {
    GmConfig cfg;
    GmRuntime r;
    GmAction idle;
    PtMobEffect effect;
    char err[256];
    float health;
    int top, slot;
    memset(&idle, 0, sizeof idle);
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.mobs = 1;

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    CHECK(gm_runtime_spawn_mob_fixture(
        &r, EW_TYPE_COW, 730, 8.5, (double)top + 1.0, 10.5,
        0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0));
    slot = mob_slot_by_eid(&r, 730);
    CHECK(slot > 0);
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 11, 0, 2));
    CHECK(gm_mobs_apply_explosion(
        &r.mobs, slot, 5.0F, 0.0, 0.0, 0.0, &r.entities) == 2);
    CHECK(mob_slot_health(&r, slot, &health) && health == 6.0F);
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    CHECK(gm_runtime_spawn_mob_fixture(
        &r, EW_TYPE_COW, 731, 8.5, (double)top + 1.0, 10.5,
        0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0));
    slot = mob_slot_by_eid(&r, 731);
    CHECK(slot > 0);
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 11, 4, 2));
    CHECK(gm_mobs_apply_explosion(
        &r.mobs, slot, 5.0F, 0.0, 0.0, 0.0, &r.entities) == 2);
    CHECK(mob_slot_health(&r, slot, &health) && health == 10.0F);
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    CHECK(gm_runtime_spawn_mob_fixture(
        &r, EW_TYPE_COW, 732, 8.5, (double)top + 1.0, 10.5,
        0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0));
    slot = mob_slot_by_eid(&r, 732);
    CHECK(slot > 0);
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 11, 0, 1));
    gm_runtime_tick(&r, idle);
    CHECK(!mob_effect(&r, slot, 11, NULL));
    CHECK(gm_mobs_apply_explosion(
        &r.mobs, slot, 5.0F, 0.0, 0.0, 0.0, &r.entities) == 2);
    CHECK(mob_slot_health(&r, slot, &health) && health == 5.0F);
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    CHECK(gm_runtime_spawn_mob_fixture(
        &r, EW_TYPE_COW, 733, 8.5, (double)top + 1.0, 10.5,
        0.0, 0.0, 0.0, 0.0F, 1.0F, 1, 0, 0, 0));
    slot = mob_slot_by_eid(&r, 733);
    CHECK(slot > 0);
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 20, 0, 40));
    gm_runtime_tick(&r, idle);
    CHECK(mob_store(&r)->health[slot] == 0.0F
          && r.mobs.entity_dead[slot]);
    CHECK(mob_effect(&r, slot, 20, &effect) && effect.duration == 39);
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    CHECK(gm_runtime_spawn_mob_fixture(
        &r, EW_TYPE_COW, 734, 8.5, (double)top + 1.0, 10.5,
        0.0, 0.0, 0.0, 0.0F, 1.0F, 1, 0, 0, 0));
    slot = mob_slot_by_eid(&r, 734);
    CHECK(slot > 0);
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 11, 0, 2));
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 20, 0, 40));
    gm_runtime_tick(&r, idle);
    CHECK(mob_store(&r)->health[slot] == 0.19999998807907104F
          && !r.mobs.entity_dead[slot]);
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    r.mobs.active_dimension = r.dimension;
    slot = gm_mobs_spawn(
        &r.mobs, EW_TYPE_COW, 8.5, (double)top + 1.0, 10.5);
    CHECK(slot > 0);
    CHECK(gm_mobs_apply_explosion(
        &r.mobs, slot, 9.0F, 0.0, 0.0, 0.0, &r.entities) == 2);
    for (int tick = 0; tick < 10; ++tick) gm_runtime_tick(&r, idle);
    CHECK(mob_slot_health(&r, slot, &health) && health == 1.0F);
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 20, 0, 40));
    {
        int item_count = r.entities.n_active;
        gm_runtime_tick(&r, idle);
        CHECK(!mob_slot_health(&r, slot, NULL));
        CHECK(r.entities.n_active == item_count + 2);
    }
    gm_runtime_destroy(&r);
}

static void test_mob_health_and_levitation_effect_runtime(void) {
    GmConfig cfg;
    GmRuntime r;
    GmAction idle;
    char err[256];
    float health;
    int top, slot;
    memset(&idle, 0, sizeof idle);
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.mobs = 1;

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    r.mobs.active_dimension = r.dimension;
    slot = gm_mobs_spawn(
        &r.mobs, EW_TYPE_COW, 8.5, (double)top + 1.0, 10.5);
    CHECK(slot > 0);
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 25, 0, 2));
    gm_runtime_tick(&r, idle);
    CHECK(mob_store(&r)->vy[slot] == 0.009800000190734865);
    gm_runtime_tick(&r, idle);
    CHECK(!mob_effect(&r, slot, 25, NULL));
    CHECK(mob_store(&r)->vy[slot]
          == (0.009800000190734865 - 0.08)
              * 0.9800000190734863);
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    CHECK(gm_runtime_spawn_mob_fixture(
        &r, EW_TYPE_COW, 740, 8.5, (double)top + 1.0, 10.5,
        0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0));
    slot = mob_slot_by_eid(&r, 740);
    CHECK(slot > 0);
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 21, 0, 1));
    CHECK(gm_mobs_max_health(&r.mobs, slot) == 14.0F);
    CHECK(gm_mobs_apply_instant_potion(
        &r.mobs, slot, 6, 0, 1.0, &r.entities) == 2);
    CHECK(mob_slot_health(&r, slot, &health) && health == 14.0F);
    gm_runtime_tick(&r, idle);
    CHECK(!mob_effect(&r, slot, 21, NULL));
    CHECK(gm_mobs_max_health(&r.mobs, slot) == 10.0F);
    CHECK(mob_slot_health(&r, slot, &health) && health == 10.0F);
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    CHECK(gm_runtime_spawn_mob_fixture(
        &r, EW_TYPE_COW, 741, 8.5, (double)top + 1.0, 10.5,
        0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0));
    slot = mob_slot_by_eid(&r, 741);
    CHECK(slot > 0);
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 22, 0, 2));
    CHECK(gm_mobs_absorption(&r.mobs, slot) == 4.0F);
    CHECK(gm_mobs_apply_explosion(
        &r.mobs, slot, 6.0F, 0.0, 0.0, 0.0, &r.entities) == 2);
    CHECK(gm_mobs_absorption(&r.mobs, slot) == 0.0F);
    CHECK(mob_slot_health(&r, slot, &health) && health == 8.0F);
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    CHECK(gm_runtime_spawn_mob_fixture(
        &r, EW_TYPE_COW, 742, 8.5, (double)top + 1.0, 10.5,
        0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0));
    slot = mob_slot_by_eid(&r, 742);
    CHECK(slot > 0);
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 22, 0, 10));
    CHECK(gm_mobs_apply_explosion(
        &r.mobs, slot, 3.0F, 0.0, 0.0, 0.0, &r.entities) == 2);
    CHECK(mob_slot_health(&r, slot, &health) && health == 10.0F);
    CHECK(gm_mobs_absorption(&r.mobs, slot) == 1.0F);
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 22, 0, 20));
    CHECK(gm_mobs_absorption(&r.mobs, slot) == 4.0F);
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 22, 1, 10));
    CHECK(gm_mobs_absorption(&r.mobs, slot) == 8.0F);
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    CHECK(gm_runtime_spawn_mob_fixture(
        &r, EW_TYPE_COW, 744, 8.5, (double)top + 1.0, 10.5,
        0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0));
    slot = mob_slot_by_eid(&r, 744);
    CHECK(slot > 0);
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 22, 1, 10));
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 11, 0, 2));
    CHECK(gm_mobs_apply_explosion(
        &r.mobs, slot, 10.0F, 0.0, 0.0, 0.0, &r.entities) == 2);
    CHECK(gm_mobs_absorption(&r.mobs, slot) == 0.0F);
    CHECK(mob_slot_health(&r, slot, &health) && health == 10.0F);
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 8);
    CHECK(gm_runtime_spawn_mob_fixture(
        &r, EW_TYPE_COW, 743, 8.5, (double)top + 1.0, 10.5,
        0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0));
    slot = mob_slot_by_eid(&r, 743);
    CHECK(slot > 0);
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 22, 0, 1));
    gm_runtime_tick(&r, idle);
    CHECK(!mob_effect(&r, slot, 22, NULL));
    CHECK(gm_mobs_absorption(&r.mobs, slot) == 0.0F);
    gm_runtime_destroy(&r);
}

static void test_mob_water_breathing_and_drowning_runtime(void) {
    GmConfig cfg;
    GmRuntime r;
    GmAction idle;
    JavaRandom expected;
    char err[256];
    float health;
    int top, slot, eye_y;
    memset(&idle, 0, sizeof idle);
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.mobs = 1;

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 10);
    eye_y = top + 2;
    for (int x = -16; x <= 32; ++x)
        for (int z = -16; z <= 32; ++z)
            gm_world_load_block_meta(r.world, x, eye_y, z, 9, 0);
    CHECK(gm_runtime_spawn_mob_fixture(
        &r, EW_TYPE_COW, 750, 8.5, (double)top + 1.0, 10.5,
        0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0));
    slot = mob_slot_by_eid(&r, 750);
    CHECK(slot > 0);
    CHECK(gm_mobs_set_entity_random_state(
        &r.mobs, 750, UINT64_C(0x123456789abc), 0, 0.0));
    for (int tick = 0; tick < 319; ++tick)
        gm_runtime_tick(&r, idle);
    CHECK(gm_mobs_air(&r.mobs, slot) == -19);
    CHECK(mob_slot_health(&r, slot, &health) && health == 10.0F);
    jrand_set_seed48(&expected, UINT64_C(0x123456789abc));
    {
        int living_sound_time = 0;
        for (int tick = 0; tick < 320; ++tick) {
            if (jrand_int_bound(&expected, 1000) < living_sound_time++) {
                living_sound_time = -120;
                (void)jrand_float(&expected);
                (void)jrand_float(&expected);
            }
        }
    }
    for (int draw = 0; draw < 48; ++draw) (void)jrand_float(&expected);
    gm_runtime_tick(&r, idle);
    CHECK(gm_mobs_air(&r.mobs, slot) == 0);
    CHECK(mob_slot_health(&r, slot, &health) && health == 8.0F);
    CHECK(r.mobs.entity_hurt_time[slot] == 9);
    CHECK(r.mobs.entity_hurt_resistant[slot] == 19);
    CHECK(r.mobs.entity_random[slot].random.seed == expected.seed);
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 10);
    gm_world_set_block_meta(r.world, 8, top + 2, 10, 9, 0);
    CHECK(gm_runtime_spawn_mob_fixture(
        &r, EW_TYPE_COW, 751, 8.5, (double)top + 1.0, 10.5,
        0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0));
    slot = mob_slot_by_eid(&r, 751);
    CHECK(slot > 0);
    CHECK(gm_runtime_set_mob_air(&r, 751, -19));
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 13, 0, 1));
    gm_runtime_tick(&r, idle);
    CHECK(gm_mobs_air(&r.mobs, slot) == -19);
    CHECK(!mob_effect(&r, slot, 13, NULL));
    CHECK(mob_slot_health(&r, slot, &health) && health == 10.0F);
    gm_runtime_tick(&r, idle);
    CHECK(gm_mobs_air(&r.mobs, slot) == 0);
    CHECK(mob_slot_health(&r, slot, &health) && health == 8.0F);
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 10);
    CHECK(gm_runtime_spawn_mob_fixture(
        &r, EW_TYPE_COW, 752, 8.5, (double)top + 1.0, 10.5,
        0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0));
    slot = mob_slot_by_eid(&r, 752);
    CHECK(slot > 0);
    CHECK(gm_runtime_set_mob_air(&r, 752, -7));
    gm_runtime_tick(&r, idle);
    CHECK(gm_mobs_air(&r.mobs, slot) == 300);
    gm_runtime_destroy(&r);

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 10);
    gm_world_set_block_meta(r.world, 8, top + 2, 10, 8, 0);
    CHECK(gm_runtime_spawn_mob_fixture(
        &r, EW_TYPE_COW, 753, 8.5, (double)top + 1.0, 10.5,
        0.0, 0.0, 0.0, 0.0F, 2.0F, 1, 0, 0, 0));
    slot = mob_slot_by_eid(&r, 753);
    CHECK(slot > 0);
    CHECK(gm_runtime_set_mob_air(&r, 753, -19));
    gm_runtime_tick(&r, idle);
    CHECK(mob_store(&r)->health[slot] == 0.0F);
    CHECK(r.mobs.entity_dead[slot]);
    CHECK(r.mobs.entity_death_time[slot] == 1);
    gm_runtime_destroy(&r);
}

static void test_mob_invisibility_render_state(void) {
    GmConfig cfg;
    GmRuntime r;
    GmAction idle;
    GmEntityView views[GM_MOB_CAPACITY];
    char err[256];
    int top, slot, count;
    memset(&idle, 0, sizeof idle);
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.mobs = 1;

    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err));
    top = top_block_y(&r, 8, 10);
    CHECK(gm_runtime_spawn_mob_fixture(
        &r, EW_TYPE_COW, 760, 8.5, (double)top + 1.0, 10.5,
        0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0));
    slot = mob_slot_by_eid(&r, 760);
    CHECK(slot > 0);
    CHECK(gm_mobs_apply_potion_effect(&r.mobs, slot, 14, 0, 1));
    count = gm_mobs_fill_views(&r.mobs, views, GM_MOB_CAPACITY);
    CHECK(count == 1 && views[0].type == EW_TYPE_COW
          && (views[0].flags & 4) != 0);
    gm_runtime_tick(&r, idle);
    CHECK(!mob_effect(&r, slot, 14, NULL));
    count = gm_mobs_fill_views(&r.mobs, views, GM_MOB_CAPACITY);
    CHECK(count == 1 && views[0].type == EW_TYPE_COW
          && (views[0].flags & 4) == 0);
    gm_runtime_destroy(&r);
}

int main(void) {
    test_live_kernel();
    test_runtime_vertical_slice();
    test_splash_potion_runtime();
    test_potion_mob_runtime();
    test_potion_state_fixtures();
    test_mob_status_effect_runtime();
    test_mob_attribute_effect_runtime();
    test_mob_damage_effect_runtime();
    test_mob_health_and_levitation_effect_runtime();
    test_mob_water_breathing_and_drowning_runtime();
    test_mob_invisibility_render_state();
    if (failures) {
        fprintf(stderr, "brewing_live: FAIL (%d/%d checks failed)\n",
                failures, checks);
        return 1;
    }
    printf("brewing_live: PASS (%d checks)\n", checks);
    return 0;
}
