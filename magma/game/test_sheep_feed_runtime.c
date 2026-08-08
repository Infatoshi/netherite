#include "game/runtime.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { \
    if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } \
} while (0)

static const EwStore *store(const GmMobLive *m) {
    return m->current ? &m->b : &m->a;
}

static void component_fixture_type(
        GmMobLive *m, IsrInv *inv, int type, int age, int in_love,
        int main_item, int main_count, int off_item, int off_count) {
    gm_mobs_init(m, 0);
    isr_init(inv);
    isr_set_stack(inv, 0,
        main_count > 0 ? ic_mk(main_item, main_count, 0) : ic_empty());
    isr_set_stack(inv, ISR_OFFHAND_SLOT,
        off_count > 0 ? ic_mk(off_item, off_count, 0) : ic_empty());
    CHECK(gm_mobs_spawn_exact(
              m, type, 670000, 10.0, 64.0, 10.0,
              0.0, 0.0, 0.0, 0.0F,
              type == EW_TYPE_SHEEP ? 8.0F
                  : type == EW_TYPE_CHICKEN ? 4.0F : 10.0F,
              1, 0, 0, 0) >= 0
              && gm_mobs_set_growing_age(m, 670000, age)
              && gm_mobs_set_animal_breeding_state(
                  m, 670000, in_love, 0, 0, in_love > 0),
          "component feed fixture initializes");
}

static void component_fixture(
        GmMobLive *m, IsrInv *inv, int age, int in_love,
        int main_item, int main_count, int off_item, int off_count) {
    component_fixture_type(
        m, inv, EW_TYPE_SHEEP, age, in_love,
        main_item, main_count, off_item, off_count);
}

static void check_state(
        const GmMobLive *m, int eid, int age, int in_love,
        int forced_age, int forced_timer, int bred, const char *message) {
    int got_age, got_love, got_forced, got_timer, got_bred;
    int found = gm_mobs_get_animal_breeding_state(
        m, eid, &got_age, &got_love,
        &got_forced, &got_timer, &got_bred);
    int exact = found && got_age == age && got_love == in_love
        && got_forced == forced_age && got_timer == forced_timer
        && got_bred == bred;
    if (!exact)
        fprintf(stderr,
            "feed diagnostic eid=%d found=%d got=%d/%d/%d/%d/%d "
            "expected=%d/%d/%d/%d/%d\n",
            eid, found, got_age, got_love, got_forced, got_timer, got_bred,
            age, in_love, forced_age, forced_timer, bred);
    CHECK(exact, message);
}

static uint64_t double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static void test_component_feed(void) {
    GmMobLive m;
    IsrInv inv;
    GmMobEvent event;

    component_fixture(&m, &inv, 0, 0, 296, 3, 0, 0);
    CHECK(gm_mobs_feed_sheep(&m, 670000, &inv, 0, 0) == 1
              && isr_get_stack(&inv, 0).count == 2,
          "adult survival wheat is handled and consumed");
    check_state(&m, 670000, 0, 600, 0, 0, 1,
                "adult feed enters 600-tick player love state");
    CHECK(gm_mobs_event_count(&m) == 1
              && gm_mobs_event_get(&m, 0, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS
              && event.eid == 670000 && event.data == 18,
          "adult feed emits exactly status 18");

    component_fixture(&m, &inv, 0, 0, 296, 3, 0, 0);
    CHECK(gm_mobs_feed_sheep(&m, 670000, &inv, 0, 1) == 1
              && isr_get_stack(&inv, 0).count == 3,
          "adult creative wheat is handled without consumption");
    check_state(&m, 670000, 0, 600, 0, 0, 1,
                "creative feed enters the same love state");

    component_fixture(&m, &inv, 0, 100, 296, 3, 0, 0);
    CHECK(gm_mobs_feed_sheep(&m, 670000, &inv, 0, 0) == 0
              && isr_get_stack(&inv, 0).count == 3
              && gm_mobs_event_count(&m) == 0,
          "already-in-love adult passes without mutation");
    check_state(&m, 670000, 0, 100, 0, 0, 1,
                "already-in-love timer is preserved");

    component_fixture(&m, &inv, 6000, 0, 296, 3, 0, 0);
    CHECK(gm_mobs_feed_sheep(&m, 670000, &inv, 0, 0) == 0
              && isr_get_stack(&inv, 0).count == 3,
          "breeding cooldown adult passes");
    check_state(&m, 670000, 6000, 0, 0, 0, 0,
                "cooldown state is unchanged");

    component_fixture(&m, &inv, 0, 0, 280, 3, 0, 0);
    CHECK(gm_mobs_feed_sheep(&m, 670000, &inv, 0, 0) == 0
              && isr_get_stack(&inv, 0).count == 3,
          "non-wheat interaction passes");

    component_fixture(&m, &inv, -24000, 0, 296, 3, 0, 0);
    CHECK(gm_mobs_feed_sheep(&m, 670000, &inv, 0, 0) == 1
              && isr_get_stack(&inv, 0).count == 2,
          "child survival wheat is handled and consumed");
    check_state(&m, 670000, -21600, 0, 2400, 40, 0,
                "child -24000 uses exact float growth and forced-age delta");
    CHECK(gm_mobs_event_count(&m) == 0,
          "child growth emits no entity status");

    component_fixture(&m, &inv, -19, 0, 296, 3, 0, 0);
    CHECK(gm_mobs_feed_sheep(&m, 670000, &inv, 0, 0) == 1,
          "near-adult child wheat remains handled");
    check_state(&m, 670000, -19, 0, 0, 40, 0,
                "child -19 preserves age but arms forced-age timer");

    component_fixture(&m, &inv, INT_MIN, 0, 296, 3, 0, 0);
    CHECK(gm_mobs_feed_sheep(&m, 670000, &inv, 0, 0) == 1,
          "minimum-int child wheat is handled without C overflow");
    check_state(&m, 670000, INT_MIN, 0, INT_MIN, 40, 0,
                "minimum-int child follows Java wrap and clamp semantics");

    component_fixture(&m, &inv, -24000, 0, 280, 1, 296, 2);
    CHECK(gm_mobs_feed_sheep(
              &m, 670000, &inv, ISR_OFFHAND_SLOT, 1) == 1
              && isr_get_stack(&inv, ISR_OFFHAND_SLOT).count == 2,
          "creative offhand child feed does not consume wheat");
    check_state(&m, 670000, -21600, 0, 2400, 40, 0,
                "offhand child growth matches main hand");

    component_fixture(&m, &inv, 0, 50, 296, 1, 0, 0);
    CHECK(gm_mobs_damage_near(
              &m, 10.0, 64.9, 10.0, 2.0, 1.0F, NULL) == 1,
          "generic damage reaches sheep");
    check_state(&m, 670000, 0, 0, 0, 0, 0,
                "accepted damage clears love and player credit");
}

static void test_species_component_feed(void) {
    static const struct {
        int type, foods[4], food_count, rejected;
    } specs[] = {
        {EW_TYPE_COW, {296}, 1, 391},
        {EW_TYPE_PIG, {391, 392, 434}, 3, 296},
        {EW_TYPE_CHICKEN, {295, 361, 362, 435}, 4, 296},
    };
    GmMobLive m;
    IsrInv inv;
    for (size_t species = 0; species < sizeof specs / sizeof specs[0];
            ++species) {
        for (int food = 0; food < specs[species].food_count; ++food) {
            component_fixture_type(
                &m, &inv, specs[species].type, 0, 0,
                specs[species].foods[food], 2, 0, 0);
            CHECK(gm_mobs_animal_can_feed(
                      &m, 670000, specs[species].foods[food])
                      && gm_mobs_feed_animal(
                          &m, 670000, &inv, 0, 0) == 1
                      && isr_get_stack(&inv, 0).count == 1,
                  "species breeding item is accepted and consumed");
            check_state(&m, 670000, 0, 600, 0, 0, 1,
                        "species adult enters exact love state");
            CHECK(gm_mobs_event_count(&m) == 1,
                  "species adult feed emits one status event");
        }

        component_fixture_type(
            &m, &inv, specs[species].type, 0, 0,
            specs[species].rejected, 2, 0, 0);
        CHECK(!gm_mobs_animal_can_feed(
                  &m, 670000, specs[species].rejected)
                  && !gm_mobs_feed_animal(&m, 670000, &inv, 0, 0)
                  && isr_get_stack(&inv, 0).count == 2,
              "cross-species breeding item passes without mutation");

        component_fixture_type(
            &m, &inv, specs[species].type, -24000, 0,
            specs[species].foods[0], 2, 0, 0);
        CHECK(gm_mobs_feed_animal(&m, 670000, &inv, 0, 0) == 1,
              "species child breeding item is handled");
        check_state(&m, 670000, -21600, 0, 2400, 40, 0,
                    "species child uses shared exact age-up arithmetic");

        component_fixture_type(
            &m, &inv, specs[species].type, 0, 50,
            specs[species].foods[0], 1, 0, 0);
        CHECK(gm_mobs_damage_near(
                  &m, 10.0, 64.9, 10.0, 2.0, 1.0F, NULL) == 1,
              "generic damage reaches breedable species");
        check_state(&m, 670000, 0, 0, 0, 0, 0,
                    "damage clears species love and player credit");
    }
}

static void check_milk_sound(
        const GmMobLive *m, double x, double y, double z,
        int replacement, const char *message) {
    GmMobEvent event;
    CHECK(gm_mobs_event_count(m) == 1 + replacement
              && gm_mobs_event_get(m, 0, &event)
              && event.kind == GM_MOB_EVENT_SOUND && event.eid == 0
              && event.data == GM_MOB_SOUND_COW_MILK
              && event.x == x && event.y == y && event.z == z
              && event.volume == 1.0F && event.pitch == 1.0F,
          message);
    if (replacement)
        CHECK(gm_mobs_event_get(m, 1, &event)
                  && event.kind == GM_MOB_EVENT_SOUND && event.eid == 0
                  && event.data == GM_MOB_SOUND_ITEM_ARMOR_EQUIP_GENERIC
                  && event.x == x && event.y == y && event.z == z
                  && event.volume == 1.0F && event.pitch == 1.0F,
              "selected-hand milk replacement emits exact Forge equip sound");
}

static void test_component_milking(void) {
    const uint64_t math_seed = UINT64_C(0x456789abcdef);
    const uint64_t player_seed = UINT64_C(0x23456789abcd);
    GmMobLive m;
    GmLiveSim drops;
    IsrInv inv;
    McSinTable sin_table;
    uint64_t math_cursor;
    int next_eid;

    mc_sin_table_init(&sin_table);
    component_fixture_type(
        &m, &inv, EW_TYPE_COW, 0, 0, 325, 1, 296, 2);
    CHECK(gm_mobs_milk_cow(
              &m, 670000, &inv, 0, 0,
              10.0, 64.0, -2.0, 0.0F, 0.0F, (double)1.62F,
              NULL, NULL, NULL, NULL) == 1
              && isr_get_stack(&inv, 0).item == 335
              && isr_get_stack(&inv, 0).count == 1
              && isr_get_stack(&inv, ISR_OFFHAND_SLOT).count == 2,
          "single bucket becomes milk in the selected hand");
    check_milk_sound(
        &m, 10.0, 64.0, -2.0,
        1,
        "milking emits the exact player-position sound first");

    component_fixture_type(
        &m, &inv, EW_TYPE_COW, 0, 0, 325, 1, 0, 0);
    CHECK(gm_mobs_milk_cow(
              &m, 670000, &inv, 0, 1,
              0.0, 0.0, 0.0, 0.0F, 0.0F, (double)1.62F,
              NULL, NULL, NULL, NULL) == 0
              && isr_get_stack(&inv, 0).item == 325
              && gm_mobs_event_count(&m) == 0,
          "creative cow bucket passes without mutation or sound");
    component_fixture_type(
        &m, &inv, EW_TYPE_COW, -1, 0, 325, 1, 0, 0);
    CHECK(gm_mobs_milk_cow(
              &m, 670000, &inv, 0, 0,
              0.0, 0.0, 0.0, 0.0F, 0.0F, (double)1.62F,
              NULL, NULL, NULL, NULL) == 0
              && isr_get_stack(&inv, 0).item == 325
              && gm_mobs_event_count(&m) == 0,
          "child cow bucket passes without mutation or sound");

    component_fixture_type(
        &m, &inv, EW_TYPE_COW, 6000, 0, 325, 2, 0, 0);
    math_cursor = math_seed;
    next_eid = 700000;
    CHECK(gm_mobs_milk_cow(
              &m, 670000, &inv, 0, 0,
              0.0, 0.0, 0.0, 0.0F, 0.0F, (double)1.62F,
              NULL, &math_cursor, NULL, &next_eid) == 1
              && isr_get_stack(&inv, 0).item == 325
              && isr_get_stack(&inv, 0).count == 1
              && isr_get_stack(&inv, 1).item == 335
              && isr_get_stack(&inv, 1).count == 1
              && math_cursor == math_seed && next_eid == 700000,
          "cooldown adult stacked bucket inserts milk in first empty main slot");

    component_fixture_type(
        &m, &inv, EW_TYPE_COW, 0, 0, 325, 2, 0, 0);
    for (int i = 1; i < ISR_MAIN_SLOTS; ++i)
        isr_set_stack(&inv, i, ic_mk(1, 64, 0));
    memset(&drops, 0, sizeof drops);
    jrand_set_seed48(&m.player_random, player_seed);
    math_cursor = math_seed;
    next_eid = 700000;
    CHECK(gm_mobs_milk_cow(
              &m, 670000, &inv, 0, 0,
              10.0, 64.0, -2.0, 0.0F, 0.0F, (double)1.62F,
              &sin_table, &math_cursor, &drops, &next_eid) == 1
              && drops.n_active == 1 && next_eid == 700001
              && math_cursor == UINT64_C(0x7e88ed32e317)
              && m.player_random.seed == UINT64_C(0xcff53ce648a1),
          "full inventory toss advances exact EID and RNG cursors");
    {
        const GmLiveEnt *item = &drops.ents[0];
        CHECK(item->active && item->eid == 700000
                  && item->item == 335 && item->count == 1 && item->meta == 0
                  && item->age == 0 && item->pickup_delay == 40
                  && item->health == 5 && item->lifespan == 6000
                  && !item->on_ground && item->x == 10.0
                  && item->y == 65.31999999284744 && item->z == -2.0
                  && float_bits(item->hover_start) == UINT32_C(0x40482136)
                  && float_bits(item->yaw) == UINT32_C(0x438f61ba)
                  && double_bits(item->mx) == UINT64_C(0xbf4008824ba60b61)
                  && double_bits(item->my) == UINT64_C(0x3fb37f6050000000)
                  && double_bits(item->mz) == UINT64_C(0x3fd32fc4ef6aef84),
              "full inventory toss preserves exact item constructor and throw state");
    }
    check_milk_sound(
        &m, 10.0, 64.0, -2.0,
        0,
        "full inventory toss retains the pre-mutation milk sound");

    component_fixture_type(
        &m, &inv, EW_TYPE_COW, 0, 0, 325, 2, 0, 0);
    for (int i = 1; i < ISR_MAIN_SLOTS; ++i)
        isr_set_stack(&inv, i, ic_mk(1, 64, 0));
    memset(&drops, 0, sizeof drops);
    for (int i = 0; i < GM_LIVE_MAX; ++i)
        CHECK(gm_live_spawn_item_exact(
                  &drops, 710000 + i, 0.0, 64.0, 0.0,
                  0.0, 0.0, 0.0, 0.0F,
                  1, 1, 0, 0, 10, 0),
              "capacity fixture fills exact item store");
    jrand_set_seed48(&m.player_random, player_seed);
    math_cursor = math_seed;
    next_eid = 700000;
    CHECK(gm_mobs_milk_cow(
              &m, 670000, &inv, 0, 0,
              0.0, 64.0, 0.0, 0.0F, 0.0F, (double)1.62F,
              &sin_table, &math_cursor, &drops, &next_eid) == -1
              && isr_get_stack(&inv, 0).count == 2
              && math_cursor == math_seed
              && m.player_random.seed == player_seed
              && next_eid == 700000 && drops.n_active == GM_LIVE_MAX
              && gm_mobs_event_count(&m) == 0,
          "bounded full item store rejects milk toss atomically");
    isr_set_stack(&inv, 0, ic_mk(325, 1, 0));
    CHECK(gm_mobs_milk_cow(
              &m, 670000, &inv, 0, 0,
              0.0, 64.0, 0.0, 0.0F, 0.0F, (double)1.62F,
              &sin_table, &math_cursor, &drops, &next_eid) == 1
              && isr_get_stack(&inv, 0).item == 335
              && math_cursor == math_seed
              && m.player_random.seed == player_seed
              && next_eid == 700000 && drops.n_active == GM_LIVE_MAX,
          "single-bucket hand replacement does not require item capacity");
}

static void test_component_saddling(void) {
    GmMobLive m;
    IsrInv inv;
    GmMobEvent event;
    int saddled = 0;

    component_fixture_type(
        &m, &inv, EW_TYPE_PIG, 0, 0, 329, 2, 391, 2);
    CHECK(gm_mobs_saddle_pig(&m, 670000, &inv, 0, 0) == 1
              && gm_mobs_get_pig_saddled(&m, 670000, &saddled)
              && saddled && isr_get_stack(&inv, 0).count == 1
              && isr_get_stack(&inv, ISR_OFFHAND_SLOT).count == 2,
          "adult survival saddle is handled, applied, and consumed");
    CHECK(gm_mobs_event_count(&m) == 1
              && gm_mobs_event_get(&m, 0, &event)
              && event.kind == GM_MOB_EVENT_SOUND && event.eid == 0
              && event.data == GM_MOB_SOUND_PIG_SADDLE
              && event.x == 10.0 && event.y == 64.0 && event.z == 10.0
              && event.volume == 0.5F && event.pitch == 1.0F,
          "pig saddle emits exact neutral sound payload at pig position");

    component_fixture_type(
        &m, &inv, EW_TYPE_PIG, 0, 0, 329, 1, 0, 0);
    CHECK(gm_mobs_saddle_pig(&m, 670000, &inv, 0, 1) == 1
              && gm_mobs_get_pig_saddled(&m, 670000, &saddled)
              && saddled && isr_get_stack(&inv, 0).count == 1
              && gm_mobs_event_count(&m) == 1,
          "creative saddle applies and sounds without consumption");

    component_fixture_type(
        &m, &inv, EW_TYPE_PIG, -24000, 0, 329, 1, 391, 2);
    CHECK(gm_mobs_saddle_pig(&m, 670000, &inv, 0, 0) == 1
              && gm_mobs_get_pig_saddled(&m, 670000, &saddled)
              && !saddled && isr_get_stack(&inv, 0).count == 1
              && gm_mobs_event_count(&m) == 0,
          "child saddle is handled without mutation or sound");

    component_fixture_type(
        &m, &inv, EW_TYPE_PIG, 0, 0, 329, 1, 0, 0);
    CHECK(gm_mobs_set_pig_saddled(&m, 670000, 1)
              && gm_mobs_saddle_pig(&m, 670000, &inv, 0, 0) == 1
              && isr_get_stack(&inv, 0).count == 1
              && gm_mobs_event_count(&m) == 0,
          "already-saddled pig handles saddle without reapplying it");
    {
        int riding_eid = 0;
        CHECK(gm_mobs_pig_mount(&m, 670000)
                  && gm_mobs_pig_riding(&m, &riding_eid)
                  && riding_eid == 670000
                  && !gm_mobs_pig_mount(&m, 670000),
              "saddled pig accepts exactly one represented passenger");
        gm_mobs_pig_dismount(&m);
        CHECK(!gm_mobs_pig_riding(&m, NULL),
              "pig dismount clears the represented passenger association");
    }
}

static void test_component_pig_dismount(void) {
    GmConfig config;
    GmRuntime runtime;
    char error[256];
    const double pig_x = 8.5, pig_y = 220.0, pig_z = 8.5;
    const double mounted_y = pig_y + (double)0.9F * 0.75D - 0.35D;
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error),
          "pig dismount component runtime initializes");
    if (fail) return;
    for (int x = 4; x <= 12; ++x)
        for (int z = 4; z <= 12; ++z)
            for (int y = 219; y <= 224; ++y)
                gm_world_set_block(
                    runtime.world, x, y, z, y == 219 ? 1 : 0);
    gm_mobs_init(&runtime.mobs, 0);
    CHECK(gm_mobs_spawn_exact(
              &runtime.mobs, EW_TYPE_PIG, 670100,
              pig_x, pig_y, pig_z, 0.0, 0.0, 0.0,
              0.0F, 10.0F, 1, 0, 0, 0) >= 0
              && gm_mobs_set_pig_saddled(&runtime.mobs, 670100, 1)
              && gm_mobs_pig_mount(&runtime.mobs, 670100),
          "flat pig dismount fixture mounts");
    gm_runtime_set_pose(
        &runtime, pig_x, mounted_y, pig_z, 17.0F, -9.0F);
    runtime.player.ent.motionX = 0.125;
    runtime.player.ent.motionY = -0.25;
    runtime.player.ent.motionZ = 0.5;
    runtime.player.ent.onGround = 1;
    runtime.player.fall_distance = 3.25F;
    gm_mobs_pig_dismount_explicit(
        &runtime.mobs, runtime.world, NULL,
        (struct PsvPlayer *)&runtime.player, runtime.ox, runtime.oz);
    {
        McAABB expected = psv_player_box(
            7.5 - runtime.ox, mounted_y + 1.0, 8.5 - runtime.oz);
        CHECK(!gm_mobs_pig_riding(&runtime.mobs, NULL)
                  && runtime.player.ent.posX + runtime.ox == 7.5
                  && runtime.player.ent.posY == mounted_y + 1.0
                  && runtime.player.ent.posZ + runtime.oz == 8.5
                  && !memcmp(&runtime.player.ent.box, &expected, sizeof expected),
              "flat yaw-zero dismount selects exact west fallback and box");
        CHECK(runtime.player.ent.motionX == 0.125
                  && runtime.player.ent.motionY == -0.25
                  && runtime.player.ent.motionZ == 0.5
                  && runtime.player.ent.onGround == 1
                  && runtime.player.fall_distance == 3.25F
                  && runtime.player.yaw == 17.0F
                  && runtime.player.pitch == -9.0F,
              "explicit dismount preserves motion, contact, fall, and look state");
    }

    for (int x = 6; x <= 10; ++x)
        for (int z = 6; z <= 10; ++z)
            gm_world_set_block(runtime.world, x, 220, z, 1);
    gm_mobs_init(&runtime.mobs, 0);
    CHECK(gm_mobs_spawn_exact(
              &runtime.mobs, EW_TYPE_PIG, 670101,
              pig_x, pig_y, pig_z, 0.0, 0.0, 0.0,
              0.0F, 10.0F, 1, 0, 0, 0) >= 0
              && gm_mobs_set_pig_saddled(&runtime.mobs, 670101, 1)
              && gm_mobs_pig_mount(&runtime.mobs, 670101),
          "blocked pig dismount fixture mounts");
    gm_runtime_set_pose(
        &runtime, pig_x, mounted_y, pig_z, 0.0F, 0.0F);
    gm_mobs_pig_dismount_explicit(
        &runtime.mobs, runtime.world, NULL,
        (struct PsvPlayer *)&runtime.player, runtime.ox, runtime.oz);
    CHECK(runtime.player.ent.posX + runtime.ox == pig_x
              && runtime.player.ent.posY == pig_y + (double)0.9F
              && runtime.player.ent.posZ + runtime.oz == pig_z,
          "fully blocked dismount uses exact no-epsilon pig-top fallback");
    gm_runtime_destroy(&runtime);
}

static void init_component_boost(
        GmMobLive *m, IsrInv *inv, int hand_slot, int meta) {
    component_fixture_type(
        m, inv, EW_TYPE_PIG, 0, 0,
        hand_slot == 0 ? 398 : 0, hand_slot == 0 ? 1 : 0,
        hand_slot == ISR_OFFHAND_SLOT ? 398 : 0,
        hand_slot == ISR_OFFHAND_SLOT ? 1 : 0);
    isr_set_stack(inv, hand_slot, ic_mk(398, 1, meta));
    CHECK(gm_mobs_set_pig_saddled(m, 670000, 1)
              && gm_mobs_pig_mount(m, 670000)
              && gm_mobs_set_entity_random_state(
                  m, 670000, UINT64_C(0x123456789abc), 0, 0.0),
          "component pig boost fixture initializes");
}

static void test_component_boost(void) {
    GmMobLive m;
    IsrInv inv;
    int boosting, time, total;

    component_fixture_type(
        &m, &inv, EW_TYPE_PIG, 0, 0, 398, 1, 0, 0);
    CHECK(gm_mobs_set_pig_saddled(&m, 670000, 1)
              && gm_mobs_set_entity_random_state(
                  &m, 670000, UINT64_C(0x123456789abc), 0, 0.0)
              && !gm_mobs_pig_boost(&m, &inv, 0, 0)
              && isr_get_stack(&inv, 0).meta == 0,
          "carrot stick cannot boost an unmounted pig");

    init_component_boost(&m, &inv, 0, 0);
    CHECK(gm_mobs_pig_boost(&m, &inv, 0, 0)
              && isr_get_stack(&inv, 0).meta == 7
              && gm_mobs_get_pig_boost_state(
                  &m, 670000, &boosting, &time, &total)
              && boosting && time == 0 && total == 264
              && m.entity_random[m.pig_ride].random.seed
                  == UINT64_C(27500032739863),
          "survival boost consumes seven durability and exact pig RNG");
    CHECK(!gm_mobs_pig_boost(&m, &inv, 0, 0)
              && isr_get_stack(&inv, 0).meta == 7,
          "active boost rejects a repeat item use");

    init_component_boost(&m, &inv, ISR_OFFHAND_SLOT, 18);
    CHECK(gm_mobs_pig_boost(&m, &inv, ISR_OFFHAND_SLOT, 0)
              && isr_get_stack(&inv, ISR_OFFHAND_SLOT).meta == 25,
          "offhand damage-18 stick reaches exact max damage and survives");

    init_component_boost(&m, &inv, 0, 19);
    CHECK(!gm_mobs_pig_boost(&m, &inv, 0, 0)
              && isr_get_stack(&inv, 0).meta == 19
              && m.entity_random[m.pig_ride].random.seed
                  == UINT64_C(0x123456789abc),
          "damage-19 stick rejects boost without RNG or item mutation");

    init_component_boost(&m, &inv, 0, 0);
    CHECK(gm_mobs_pig_boost(&m, &inv, 0, 1)
              && isr_get_stack(&inv, 0).meta == 0,
          "creative boost preserves carrot-stick durability");
}

static void test_component_pig_death_drop(void) {
    GmConfig config;
    GmRuntime runtime;
    char error[256];
    const double pig_x = 8.5, pig_y = 220.0, pig_z = 8.5;
    const double mounted_y = pig_y + (double)0.9F * 0.75D - 0.35D;
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error),
          "ordinary pig-death runtime initializes");
    if (fail) return;
    for (int x = 4; x <= 12; ++x)
        for (int z = 4; z <= 12; ++z)
            for (int y = 219; y <= 224; ++y)
                gm_world_set_block(
                    runtime.world, x, y, z, y == 219 ? 1 : 0);
    gm_mobs_init(&runtime.mobs, 0);
    memset(&runtime.entities, 0, sizeof runtime.entities);
    int pig_slot = gm_mobs_spawn(
        &runtime.mobs, EW_TYPE_PIG, pig_x, pig_y, pig_z);
    int pig_eid = pig_slot > 0 ? store(&runtime.mobs)->id[pig_slot] : 0;
    CHECK(pig_slot > 0
              && gm_mobs_set_pig_saddled(&runtime.mobs, pig_eid, 1)
              && gm_mobs_pig_mount(&runtime.mobs, pig_eid),
          "ordinary saddled-pig death fixture initializes");
    gm_runtime_set_pose(
        &runtime, pig_x, mounted_y, pig_z, 17.0F, -9.0F);
    runtime.mobs.entity_recently_hit[pig_slot] = 100;
    runtime.mobs.entity_attacking_player[pig_slot] = 1;
    CHECK(gm_mobs_damage_near(
              &runtime.mobs, pig_x, pig_y + 0.45, pig_z,
              1.0, 100.0F, &runtime.entities),
          "ordinary melee lethally damages saddled pig");
    CHECK(runtime.entities.n_active == 2
              && runtime.entities.ents[0].active
              && runtime.entities.ents[0].item == 319
              && runtime.entities.ents[1].active
              && runtime.entities.ents[1].item == 329,
          "ordinary death appends saddle after pork");
    CHECK(runtime.mobs.entity_dead[pig_slot]
              && runtime.mobs.entity_death_time[pig_slot] == 0
              && runtime.mobs.pig_ride == pig_slot
              && store(&runtime.mobs)->alive[pig_slot],
          "ordinary onDeath retains the ridden pig through death time zero");
    CHECK(!gm_mobs_damage_near(
              &runtime.mobs, pig_x, pig_y + 0.45, pig_z,
              1.0, 100.0F, &runtime.entities)
              && runtime.entities.n_active == 2,
          "dying pig rejects repeat damage and duplicate drops");
    for (int tick = 1; tick <= 19; ++tick) {
        gm_mobs_tick(
            &runtime.mobs, runtime.world, NULL,
            (const struct McSinTable *)&runtime.sin_table,
            (struct PsvPlayer *)&runtime.player,
            (struct PvStats *)&runtime.vitals,
            runtime.ox, runtime.oz, runtime.dimension,
            runtime.clock.world_time, runtime.mob_griefing,
            &runtime.world_random_seed48, &runtime.math_random_seed48,
            &runtime.next_entity_id, runtime.do_mob_loot,
            &runtime.entities, 0.0F, 0.0F);
        CHECK(runtime.mobs.entity_death_time[pig_slot] == tick
                  && runtime.mobs.pig_ride == pig_slot
                  && store(&runtime.mobs)->alive[pig_slot],
              "ridden pig stays loaded and mounted through death times 1..19");
    }
    CHECK(double_bits(runtime.player.ent.posX)
                  == double_bits(pig_x - runtime.ox)
              && double_bits(runtime.player.ent.posY)
                  == double_bits(mounted_y)
              && double_bits(runtime.player.ent.posZ)
                  == double_bits(pig_z - runtime.oz),
          "dying ridden pig preserves the passenger pose through time 19");
    gm_mobs_tick(
        &runtime.mobs, runtime.world, NULL,
        (const struct McSinTable *)&runtime.sin_table,
        (struct PsvPlayer *)&runtime.player,
        (struct PvStats *)&runtime.vitals,
        runtime.ox, runtime.oz, runtime.dimension,
        runtime.clock.world_time, runtime.mob_griefing,
        &runtime.world_random_seed48, &runtime.math_random_seed48,
        &runtime.next_entity_id, runtime.do_mob_loot,
        &runtime.entities, 0.0F, 0.0F);
    CHECK(runtime.mobs.entity_death_time[pig_slot] == 20
              && runtime.mobs.pig_ride == -1
              && !store(&runtime.mobs)->alive[pig_slot]
              && store(&runtime.mobs)->type[pig_slot] == EW_TYPE_NONE,
          "death time 20 dismounts before terminal pig retirement");
    CHECK(runtime.player.ent.posY >= pig_y + (double)0.9F
              && double_bits(runtime.player.ent.box.minY)
                  == double_bits(runtime.player.ent.posY),
          "terminal pig dismount rebuilds the player box from a valid pose");
    gm_runtime_destroy(&runtime);
}

static void reset_runtime_case(
        GmRuntime *r, double y, int eid, int type,
        int main_item, int main_count, int off_item, int off_count,
        int age, int in_love, int blocked) {
    gm_mobs_init(&r->mobs, 0);
    memset(&r->entities, 0, sizeof r->entities);
    r->controlled_mobs_enabled = 0;
    r->mobs_enabled = 0;
    r->server_shear_pending = 0;
    r->server_feed_animal_pending = 0;
    r->server_pig_boost_pending = 0;
    isr_init(&r->player.inv);
    r->player.inv.current_item = 0;
    isr_set_stack(&r->player.inv, 0,
        main_count > 0 ? ic_mk(main_item, main_count, 0) : ic_empty());
    isr_set_stack(&r->player.inv, ISR_OFFHAND_SLOT,
        off_count > 0 ? ic_mk(off_item, off_count, 0) : ic_empty());
    gm_runtime_set_pose(r, 8.5, y, 8.5, 0.0F, 24.0F);
    gm_world_set_block_meta(
        r->world, 8, (int)y + 1, 9, blocked ? 1 : 0, 0);
    CHECK(gm_runtime_spawn_mob_fixture(
              r, type, eid, 8.5, y, 10.5,
              0.0, 0.0, 0.0, 0.0F,
              type == GM_MOB_SHEEP ? 8.0F
                  : type == GM_MOB_CHICKEN ? 4.0F : 10.0F,
              1, 0, 0, 0)
              && gm_mobs_set_growing_age(&r->mobs, eid, age)
              && gm_mobs_set_animal_breeding_state(
                  &r->mobs, eid, in_love, 0, 0, in_love > 0),
          "runtime feed fixture initializes");
}

static void run_runtime_feed_case(
        GmRuntime *r, double y, int eid, int type, int item, int offhand) {
    GmAction use = {0}, idle = {0};
    use.hotbar_sel = idle.hotbar_sel = -1;
    use.use = use.do_place = 1;
    reset_runtime_case(
        r, y, eid, type, offhand ? 280 : item, 2,
        offhand ? item : 0, offhand ? 2 : 0, 0, 0, 0);
    gm_runtime_tick(r, use);
    CHECK(r->server_feed_animal_pending
              && r->server_feed_animal_eid == eid
              && r->server_feed_animal_hand == offhand,
          "entity use queues one delayed feed packet with exact hand");
    check_state(&r->mobs, eid, 0, 0, 0, 0, 0,
                "queued feed has not mutated server sheep early");
    gm_runtime_tick(r, idle);
    ICStack wheat = isr_get_stack(
        &r->player.inv, offhand ? ISR_OFFHAND_SLOT : 0);
    CHECK(!r->server_feed_animal_pending
              && wheat.item == item && wheat.count == 1,
          "delayed server feed consumes selected-hand breeding item");
    check_state(&r->mobs, eid, 0, 599, 0, 0, 1,
                "delayed runtime feed enters and ages love mode once");
    r->mobs_enabled = 1;
    gm_runtime_tick(r, idle);
    check_state(&r->mobs, eid, 0, 598, 0, 0, 1,
                "ordinary species tick ages the love timer once");
    r->mobs_enabled = 0;
}

static const GmLiveEnt *find_live_eid(const GmLiveSim *live, int eid) {
    for (int i = 0; i < GM_LIVE_MAX; ++i)
        if (live->ents[i].active && live->ents[i].eid == eid)
            return &live->ents[i];
    return NULL;
}

static void test_runtime_feed(void) {
    GmConfig cfg;
    GmRuntime r;
    GmAction use = {0};
    char err[256];
    gm_config_defaults(&cfg);
    cfg.view_distance = 1;
    cfg.mobs = 0;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err),
          "feed runtime initializes");
    if (fail) return;
    double y = (double)gm_world_surface_y(r.world, 8, 8) + 1.0;
    run_runtime_feed_case(&r, y, 671000, GM_MOB_SHEEP, 296, 0);
    run_runtime_feed_case(&r, y, 671100, GM_MOB_SHEEP, 296, 1);
    run_runtime_feed_case(&r, y, 671110, GM_MOB_COW, 296, 0);
    run_runtime_feed_case(&r, y, 671120, GM_MOB_PIG, 391, 1);
    run_runtime_feed_case(&r, y, 671130, GM_MOB_CHICKEN, 295, 0);

    use.hotbar_sel = -1;
    use.use = use.do_place = 1;
    reset_runtime_case(
        &r, y, 671200, GM_MOB_SHEEP, 296, 2, 359, 1, 0, 0, 0);
    gm_runtime_tick(&r, use);
    CHECK(r.server_feed_animal_pending && !r.server_shear_pending
              && r.server_feed_animal_hand == 0,
          "handled main-hand wheat precedes offhand shears");

    reset_runtime_case(
        &r, y, 671300, GM_MOB_SHEEP, 296, 2, 359, 1, 0, 50, 0);
    gm_runtime_tick(&r, use);
    CHECK(!r.server_feed_animal_pending && r.server_shear_pending
              && r.server_shear_hand == 1,
          "ineligible main-hand wheat passes to offhand shears");

    reset_runtime_case(
        &r, y, 671320, GM_MOB_COW, 325, 1, 296, 2, 0, 0, 0);
    gm_runtime_tick(&r, use);
    CHECK(r.server_feed_animal_pending == 2
              && r.server_feed_animal_hand == 0,
          "adult cow main-hand bucket queues milk before offhand wheat");
    gm_runtime_tick(&r, (GmAction){.hotbar_sel = -1});
    check_state(&r.mobs, 671320, 0, 0, 0, 0, 0,
                "milking does not mutate breeding state");
    CHECK(isr_get_stack(&r.player.inv, 0).item == 335
              && isr_get_stack(&r.player.inv, 0).count == 1,
          "delayed main-hand milking replaces the bucket");
    CHECK(isr_get_stack(&r.player.inv, ISR_OFFHAND_SLOT).count == 2,
          "main-hand milking preserves offhand wheat");

    reset_runtime_case(
        &r, y, 671325, GM_MOB_COW, 280, 1, 325, 1, 0, 0, 0);
    gm_runtime_tick(&r, use);
    CHECK(r.server_feed_animal_pending == 2
              && r.server_feed_animal_hand == 1,
          "main-hand pass queues adult cow offhand milk");
    gm_runtime_tick(&r, (GmAction){.hotbar_sel = -1});
    CHECK(isr_get_stack(&r.player.inv, ISR_OFFHAND_SLOT).item == 335
              && isr_get_stack(&r.player.inv, ISR_OFFHAND_SLOT).count == 1,
          "delayed offhand milking replaces the bucket");

    reset_runtime_case(
        &r, y, 671327, GM_MOB_COW, 296, 2, 325, 1, 0, 0, 0);
    gm_runtime_tick(&r, use);
    CHECK(r.server_feed_animal_pending == 1
              && r.server_feed_animal_hand == 0,
          "main-hand cow feed precedes offhand milk");

    reset_runtime_case(
        &r, y, 671328, GM_MOB_COW, 325, 2, 0, 0, 0, 0, 0);
    for (int i = 1; i < ISR_MAIN_SLOTS; ++i)
        isr_set_stack(&r.player.inv, i, ic_mk(1, 64, 0));
    gm_runtime_tick(&r, use);
    CHECK(r.server_feed_animal_pending == 2,
          "full-inventory cow milk is delayed through server packet routing");
    r.math_random_seed48 = UINT64_C(0x456789abcdef);
    jrand_set_seed48(&r.mobs.player_random, UINT64_C(0x23456789abcd));
    r.next_entity_id = 720000;
    gm_runtime_tick(&r, (GmAction){.hotbar_sel = -1});
    {
        const GmLiveEnt *milk = find_live_eid(&r.entities, 720000);
        CHECK(milk && milk->item == 335 && milk->count == 1
                  && milk->age == 1 && milk->pickup_delay == 39
                  && r.next_entity_id == 720001
                  && r.math_random_seed48 == UINT64_C(0x7e88ed32e317)
                  && r.mobs.player_random.seed == UINT64_C(0xcff53ce648a1),
              "dropped milk receives exact RNG/EID state and same-boundary item tick");
    }

    reset_runtime_case(
        &r, y, 671330, GM_MOB_PIG, 329, 1, 391, 2, 0, 0, 0);
    gm_runtime_tick(&r, use);
    CHECK(r.server_feed_animal_pending == 3
              && r.server_feed_animal_hand == 0,
          "pig main-hand saddle queues before offhand carrot");
    {
        int saddled = 0;
        CHECK(gm_mobs_get_pig_saddled(&r.mobs, 671330, &saddled)
                  && !saddled && isr_get_stack(&r.player.inv, 0).count == 1,
              "queued pig saddle does not mutate the server early");
    }
    gm_runtime_tick(&r, (GmAction){.hotbar_sel = -1});
    check_state(&r.mobs, 671330, 0, 0, 0, 0, 0,
                "saddling preserves breeding state");
    {
        int saddled = 0;
        CHECK(gm_mobs_get_pig_saddled(&r.mobs, 671330, &saddled)
                  && saddled && isr_get_stack(&r.player.inv, 0).count == 0
                  && isr_get_stack(&r.player.inv, ISR_OFFHAND_SLOT).count == 2,
              "delayed saddle applies and preserves offhand carrot");
    }

    reset_runtime_case(
        &r, y, 671332, GM_MOB_PIG, 280, 1, 329, 1, 0, 0, 0);
    gm_runtime_tick(&r, use);
    CHECK(r.server_feed_animal_pending == 3
              && r.server_feed_animal_hand == 1,
          "main-hand pass queues an offhand pig saddle");
    gm_runtime_tick(&r, (GmAction){.hotbar_sel = -1});
    {
        int saddled = 0;
        CHECK(gm_mobs_get_pig_saddled(&r.mobs, 671332, &saddled)
                  && saddled
                  && isr_get_stack(
                      &r.player.inv, ISR_OFFHAND_SLOT).count == 0,
              "delayed offhand saddle applies to the pig");
    }

    reset_runtime_case(
        &r, y, 671333, GM_MOB_PIG, 329, 1, 391, 2, -24000, 0, 0);
    gm_runtime_set_pose(&r, 8.5, y, 8.5, 0.0F, 32.0F);
    gm_runtime_tick(&r, use);
    CHECK(r.server_feed_animal_pending == 3,
          "child pig saddle still handles and preempts offhand feed");
    gm_runtime_tick(&r, (GmAction){.hotbar_sel = -1});
    {
        int saddled = 0;
        CHECK(gm_mobs_get_pig_saddled(&r.mobs, 671333, &saddled)
                  && !saddled && isr_get_stack(&r.player.inv, 0).count == 1
                  && isr_get_stack(
                      &r.player.inv, ISR_OFFHAND_SLOT).count == 2,
              "child saddle causes no mutation after delayed handling");
    }

    reset_runtime_case(
        &r, y, 671335, GM_MOB_PIG, 421, 1, 391, 2, 0, 0, 0);
    gm_runtime_tick(&r, use);
    CHECK(!r.server_feed_animal_pending,
          "plain pig name tag handles and preempts offhand carrot");
    gm_runtime_tick(&r, (GmAction){.hotbar_sel = -1});
    check_state(&r.mobs, 671335, 0, 0, 0, 0, 0,
                "plain pig name tag leaves breeding state unchanged");
    CHECK(isr_get_stack(&r.player.inv, ISR_OFFHAND_SLOT).count == 2,
          "plain pig name tag preserves offhand carrot");

    reset_runtime_case(
        &r, y, 671336, GM_MOB_PIG, 0, 0, 391, 2, 0, 0, 0);
    CHECK(gm_mobs_set_pig_saddled(&r.mobs, 671336, 1),
          "runtime mount fixture starts saddled");
    gm_runtime_tick(&r, use);
    CHECK(r.server_feed_animal_pending == 4
              && !gm_mobs_pig_riding(&r.mobs, NULL),
          "empty main hand queues pig mount without early association");
    gm_runtime_tick(&r, (GmAction){.hotbar_sel = -1});
    {
        int riding_eid = 0;
        CHECK(gm_mobs_pig_riding(&r.mobs, &riding_eid)
                  && riding_eid == 671336
                  && isr_get_stack(
                      &r.player.inv, ISR_OFFHAND_SLOT).count == 2,
              "delayed pig mount associates player and preempts offhand");
    }
    {
        int boosting, boost_time, boost_total;
        r.mobs.controlled_no_ai[r.mobs.pig_ride] = 0;
        r.mobs_enabled = 1;
        double before_z = store(&r.mobs)->z[r.mobs.pig_ride];
        isr_set_stack(&r.player.inv, 0, ic_mk(398, 1, 0));
        gm_runtime_tick(
            &r, (GmAction){.hotbar_sel = -1, .do_place = 1});
        CHECK(r.server_pig_boost_pending
                  && r.server_pig_boost_hand == 0
                  && isr_get_stack(&r.player.inv, 0).meta == 0
                  && gm_mobs_get_pig_boost_state(
                      &r.mobs, 671336,
                      &boosting, &boost_time, &boost_total)
                  && !boosting && boost_time == 0 && boost_total == 0,
              "mounted main-hand boost queues without early mutation");
        gm_runtime_tick(&r, (GmAction){.hotbar_sel = -1});
        CHECK(!r.server_pig_boost_pending,
              "processed pig boost clears the delayed packet");
        CHECK(isr_get_stack(&r.player.inv, 0).meta == 7,
              "processed pig boost consumes seven durability");
        CHECK(gm_mobs_get_pig_boost_state(
                  &r.mobs, 671336, &boosting, &boost_time, &boost_total)
                  && boosting && boost_time == 1
                  && boost_total >= 140 && boost_total <= 980,
              "processed pig boost advances its first travel sample");
        CHECK(store(&r.mobs)->z[r.mobs.pig_ride] > before_z,
              "carrot-stick steering drives the ordinary ridden pig forward");
    }
    gm_runtime_tick(
        &r, (GmAction){.hotbar_sel = -1, .sneak = 1});
    CHECK(!gm_mobs_pig_riding(&r.mobs, NULL),
          "sneak dismount clears pig passenger association");

    reset_runtime_case(
        &r, y, 671341, GM_MOB_PIG, 0, 0, 398, 1, 0, 0, 0);
    CHECK(gm_mobs_set_pig_saddled(&r.mobs, 671341, 1)
              && gm_mobs_pig_mount(&r.mobs, 671341),
          "offhand boost fixture starts mounted");
    r.mobs.controlled_no_ai[r.mobs.pig_ride] = 0;
    r.mobs_enabled = 1;
    gm_runtime_tick(
        &r, (GmAction){.hotbar_sel = -1, .do_place = 1});
    CHECK(r.server_pig_boost_pending && r.server_pig_boost_hand == 1,
          "empty main hand queues mounted offhand carrot-stick use");
    gm_runtime_tick(&r, (GmAction){.hotbar_sel = -1});
    {
        int boosting, boost_time, boost_total;
        CHECK(isr_get_stack(
                  &r.player.inv, ISR_OFFHAND_SLOT).meta == 7
                  && gm_mobs_get_pig_boost_state(
                      &r.mobs, 671341,
                      &boosting, &boost_time, &boost_total)
                  && boosting && boost_time == 1,
              "delayed offhand boost mutates the selected hand only");
    }

    reset_runtime_case(
        &r, y, 671337, GM_MOB_PIG, 391, 2, 0, 0, 0, 0, 0);
    CHECK(gm_mobs_set_pig_saddled(&r.mobs, 671337, 1),
          "saddled-feed precedence fixture initializes");
    gm_runtime_tick(&r, use);
    CHECK(r.server_feed_animal_pending == 1,
          "eligible carrot feeds a saddled pig before mounting");

    reset_runtime_case(
        &r, y, 671338, GM_MOB_PIG, 391, 2, 0, 0, 0, 100, 0);
    CHECK(gm_mobs_set_pig_saddled(&r.mobs, 671338, 1),
          "saddled-mount fallback fixture initializes");
    gm_runtime_tick(&r, use);
    CHECK(r.server_feed_animal_pending == 4,
          "ineligible carrot falls through to saddled-pig mount");
    gm_runtime_tick(&r, (GmAction){.hotbar_sel = -1});
    CHECK(gm_mobs_pig_riding(&r.mobs, NULL)
              && isr_get_stack(&r.player.inv, 0).count == 2,
          "mount fallback preserves the ineligible carrot");

    reset_runtime_case(
        &r, y, 671339, GM_MOB_PIG, 421, 1, 0, 0, 0, 0, 0);
    CHECK(gm_mobs_set_pig_saddled(&r.mobs, 671339, 1),
          "saddled-name-tag fixture initializes");
    gm_runtime_tick(&r, use);
    CHECK(!r.server_feed_animal_pending
              && !gm_mobs_pig_riding(&r.mobs, NULL),
          "pig name tag handles before the saddled mount branch");

    reset_runtime_case(
        &r, y, 671340, GM_MOB_COW, 325, 1, 296, 2, -24000, 0, 0);
    gm_runtime_tick(&r, use);
    CHECK(r.server_feed_animal_pending && r.server_feed_animal_hand == 1,
          "child cow bucket passes to offhand wheat feed");
    gm_runtime_tick(&r, (GmAction){.hotbar_sel = -1});
    check_state(&r.mobs, 671340, -21618, 0, 2380, 40, 0,
                "child cow offhand feed applies exact growth");

    reset_runtime_case(
        &r, y, 671400, GM_MOB_SHEEP, 296, 2, 0, 0, 0, 0, 1);
    gm_runtime_tick(&r, use);
    CHECK(!r.server_feed_animal_pending,
          "nearer solid selection box occludes sheep feeding");
    reset_runtime_case(
        &r, y, 671410, GM_MOB_COW, 325, 1, 0, 0, 0, 0, 1);
    gm_runtime_tick(&r, use);
    CHECK(!r.server_feed_animal_pending,
          "nearer solid selection box occludes cow milking");
    reset_runtime_case(
        &r, y, 671420, GM_MOB_PIG, 329, 1, 0, 0, 0, 0, 1);
    gm_runtime_tick(&r, use);
    CHECK(!r.server_feed_animal_pending,
          "nearer solid selection box occludes pig saddling");
    reset_runtime_case(
        &r, y, 671430, GM_MOB_PIG, 0, 0, 0, 0, 0, 0, 1);
    CHECK(gm_mobs_set_pig_saddled(&r.mobs, 671430, 1),
          "occluded mount fixture starts saddled");
    gm_runtime_tick(&r, use);
    CHECK(!r.server_feed_animal_pending,
          "nearer solid selection box occludes pig mounting");
    gm_runtime_destroy(&r);
}

int main(void) {
    test_component_feed();
    test_species_component_feed();
    test_component_milking();
    test_component_saddling();
    test_component_pig_dismount();
    test_component_boost();
    test_component_pig_death_drop();
    test_runtime_feed();
    if (fail) return 1;
    puts("PASS animal interactions: feed, milk, saddle/mount/boost/dismount, delayed hands, RNG/items, occlusion");
    return 0;
}
