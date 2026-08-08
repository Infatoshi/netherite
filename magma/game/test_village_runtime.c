#include "game/runtime.h"
#include "world/populate_mc.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(c, m) do { \
    if (!(c)) { fprintf(stderr, "FAIL: %s\n", m); fail = 1; } \
} while (0)

static int claimed_index(const GmRuntime *r,
                         const PopmcVillageResident *site) {
    for (int i = 0; i < r->village_resident_count; ++i)
        if (r->village_residents[i].x == site->x
                && r->village_residents[i].y == site->y
                && r->village_residents[i].z == site->z)
            return i;
    return -1;
}

int main(void) {
    /* Seed-0 normal plains village locked by test_village_live. */
    enum { START_CX = -1003, START_CZ = -754, RADIUS = 3 };
    GmConfig cfg;
    GmRuntime r;
    PopmcVillageResident expected[GM_RUNTIME_VILLAGE_RESIDENTS];
    GmEntityView views[GM_MOB_CAPACITY];
    char err[256] = {0};

    gm_config_defaults(&cfg);
    cfg.seed = 0;
    cfg.world = GM_WORLD_DEFAULT;
    cfg.villages = 1;
    cfg.mobs = 1;
    cfg.weather = 0;
    cfg.view_distance = RADIUS;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err), err);
    if (fail) return 1;

    gm_world_ensure(r.world, START_CX, START_CZ, RADIUS);
    int surface = gm_world_surface_y(
        r.world, START_CX * 16 + 8, START_CZ * 16 + 8);
    gm_runtime_set_pose(&r, START_CX * 16 + 8.5, surface + 1.0,
                        START_CZ * 16 + 8.5, 0.0f, 0.0f);
    int expected_count = popmc_village_residents(
        cfg.seed, (START_CX - RADIUS) * 16,
        (START_CZ - RADIUS) * 16,
        (START_CX + RADIUS + 1) * 16 - 1,
        (START_CZ + RADIUS + 1) * 16 - 1,
        expected, GM_RUNTIME_VILLAGE_RESIDENTS);
    CHECK(expected_count > 0, "generated resident fixture is non-empty");

    /* Exercise the production tick hook, not only the public cold helper. */
    gm_runtime_tick(&r, (GmAction){.hotbar_sel = -1});
    CHECK(r.village_resident_count == expected_count,
          "production tick materializes every cached normal-village resident");
    for (int i = 0; i < expected_count; ++i) {
        int claimed = claimed_index(&r, &expected[i]);
        CHECK(claimed >= 0, "exact generated resident coordinate is claimed");
        if (claimed >= 0)
            CHECK(r.village_residents[claimed].profession
                      == expected[i].profession,
                  "generated profession survives runtime materialization");
    }

    int view_count = gm_mobs_fill_views(&r.mobs, views, GM_MOB_CAPACITY);
    int villagers = 0, professions[6] = {0};
    for (int i = 0; i < view_count; ++i)
        if (views[i].type == GM_MOB_VILLAGER) {
            ++villagers;
            if (views[i].item_id >= 0 && views[i].item_id < 6)
                ++professions[views[i].item_id];
        }
    CHECK(villagers == expected_count,
          "all materialized residents enter the live/render entity store");
    for (int profession = 0; profession < 5; ++profession) {
        int expected_profession = 0;
        for (int i = 0; i < expected_count; ++i)
            expected_profession += expected[i].profession == profession;
        CHECK(professions[profession] == expected_profession,
              "render views preserve profession distribution");
    }

    /* The resident ledger owns lazy merchant state. Exercise an actual live
     * recipe through EntityVillager.useRecipe's sound/XP/reset boundary. */
    {
        int resident_index = -1;
        GmVillagerOffer offer;
        ICStack first, second, output;
        int xp = 0;
        int sounds_before = gm_runtime_sound_event_count(&r);
        for (int i = 0; i < r.village_resident_count; ++i)
            if (gm_runtime_villager_offer_count(
                    &r, r.village_residents[i].eid) > 0) {
                resident_index = i;
                break;
            }
        CHECK(resident_index >= 0,
              "generated residents expose a bounded initial merchant list");
        if (resident_index >= 0) {
            int eid = r.village_residents[resident_index].eid;
            CHECK(gm_runtime_villager_offer_get(&r, eid, 0, &offer),
                  "live resident returns its first recipe");
            first = offer.buy_a;
            second = offer.buy_b;
            CHECK(gm_runtime_villager_trade_execute(
                      &r, eid, 0, &first, &second, &output, &xp),
                  "live resident executes a matching recipe");
            CHECK(output.item == offer.sell.item
                      && output.count == offer.sell.count
                      && first.count == 0 && second.count == 0,
                  "live trade consumes inputs and returns exact output");
            CHECK(xp >= 8 && xp <= 11,
                  "first trade emits exact reset-boosted XP range");
            CHECK(r.village_residents[resident_index].trade.time_until_reset == 40
                      && r.village_residents[resident_index]
                             .trade.needs_initialization
                      && r.village_residents[resident_index]
                             .trade.willing_to_mate,
                  "first trade schedules restock and willingness state");
            {
                GmRuntimeSoundEvent sound;
                CHECK(gm_runtime_sound_event_count(&r) == sounds_before + 1
                          && gm_runtime_sound_event_get(
                              &r, sounds_before, &sound)
                          && sound.sound == GM_SOUND_VILLAGER_YES
                          && sound.eid == eid,
                      "live trade appends the ordered villager-yes sound");
            }
        }
    }

    /* A saved, unopened NoAI villager restores the private Entity.rand cursor.
     * The first merchant access after reload must consume exactly the same
     * career and offer draws as an uninterrupted JavaRandom continuation. */
    {
        const int eid = 30000;
        const uint64_t seed48 = UINT64_C(0x3456789ABCDE);
        JavaRandom expected_random;
        GmVillagerTrade expected_trade;
        int slot;
        jrand_set_seed48(&expected_random, seed48);
        CHECK(gm_runtime_spawn_villager_fixture(
                  &r, eid, 1.5, 65.0, 1.5, 0.0, 0.0, 0.0,
                  0.0F, 20.0F, 0, 0, 0, 0,
                  0, seed48, 1, -0.25),
              "cold unopened villager fixture restores");
        for (int tick = 0; tick < 20; ++tick) {
            (void)jrand_int_bound(&expected_random, 1000);
            gm_runtime_tick(&r, (GmAction){.hotbar_sel = -1});
        }
        slot = gm_mobs_find_slot_by_eid(&r.mobs, eid);
        CHECK(slot > 0
                  && r.mobs.entity_random[slot].random.seed
                         == expected_random.seed
                  && r.mobs.entity_living_sound_time[slot] == 20,
              "20 idle ticks preserve villager ambient RNG continuation");
        gm_villager_trade_init(&expected_trade, 0, &expected_random);
        CHECK(gm_runtime_villager_offer_count(&r, eid)
                  == expected_trade.offer_count,
              "first post-reload merchant access preserves offer count");
        CHECK(slot > 0,
              "restored villager retains its exact entity id");
        if (slot > 0) {
            GmRuntimeVillageResident *resident =
                &r.village_residents[r.village_resident_count - 1];
            CHECK(resident->eid == eid
                      && resident->trade.career == expected_trade.career
                      && resident->trade.career_level
                             == expected_trade.career_level
                      && resident->trade.offer_count
                             == expected_trade.offer_count,
                  "lazy career state continues from the saved RNG cursor");
            for (int i = 0; i < expected_trade.offer_count; ++i)
                CHECK(memcmp(&resident->trade.offers[i],
                             &expected_trade.offers[i],
                             sizeof expected_trade.offers[i]) == 0,
                      "lazy post-reload offer matches direct continuation");
            CHECK(r.mobs.entity_random[slot].random.seed
                      == expected_random.seed,
                  "lazy post-reload trade leaves the same RNG cursor");
        }
    }

    /* Force a cold rescan. Claimed placement sites make it idempotent even
     * when a resident has wandered or later dies. */
    int before = r.village_resident_count;
    r.village_scan_x = INT_MIN;
    CHECK(gm_runtime_sync_village_residents(&r) == 0,
          "revisiting generated chunks does not respawn villagers");
    CHECK(r.village_resident_count == before,
          "resident claim ledger stays stable after rescan");
    gm_runtime_destroy(&r);

    if (fail) return 1;
    printf("village_runtime: PASS residents=%d merchant=live\n", expected_count);
    return 0;
}
