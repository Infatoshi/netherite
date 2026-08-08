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

static int files_equal(const char *a, const char *b) {
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    int ca, cb, equal = fa && fb;
    if (!equal) goto done;
    do {
        ca = fgetc(fa); cb = fgetc(fb);
        if (ca != cb) { equal = 0; break; }
    } while (ca != EOF);
done:
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    return equal;
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
            int initial_level;
            int initial_offers;
            CHECK(gm_runtime_villager_offer_get(&r, eid, 0, &offer),
                  "live resident returns its first recipe");
            initial_level = r.village_residents[resident_index]
                .trade.career_level;
            initial_offers = r.village_residents[resident_index]
                .trade.offer_count;
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
            for (int tick = 0; tick < 40; ++tick)
                gm_runtime_tick(&r, (GmAction){.hotbar_sel = -1});
            CHECK(r.village_residents[resident_index].trade.career_level
                        == initial_level + 1
                      && r.village_residents[resident_index].trade.offer_count
                        > initial_offers
                      && r.village_residents[resident_index]
                             .trade.time_until_reset == 0
                      && !r.village_residents[resident_index]
                             .trade.needs_initialization,
                  "live 40-tick restock appends the next career tier");
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
                  &r, eid, 1.5, 200.0, 1.5, 0.0, 0.0, 0.0,
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

    /* An already-opened merchant restores its saved career, wealth, recipe
     * counters, and complete enchanted ItemStacks without rerolling RNG. */
    {
        const int eid = 30001;
        GmVillagerOffer offer;
        ICStack buy_a = ic_mk(340, 1, 0);
        ICStack buy_b = ic_mk(388, 17, 0);
        ICStack sell = ic_mk(403, 1, 0);
        ICStack first, second, output;
        int xp;
        sell.n_enchants = 1;
        sell.enchants[0].id = 70;
        sell.enchants[0].level = 1;
        CHECK(gm_runtime_spawn_villager_fixture(
                  &r, eid, 2.5, 65.0, 2.5, 0.0, 0.0, 0.0,
                  0.0F, 20.0F, 0, 0, 0, 1,
                  0, UINT64_C(0x123456789ABC), 0, 0.0)
                  && gm_runtime_restore_villager_trade(
                      &r, eid, 1, 4, 15, 1, 1)
                  && gm_runtime_restore_villager_offer(
                      &r, eid, 0, 3, 9, 1)
                  && gm_runtime_restore_villager_offer_stack(
                      &r, eid, 0, 0, buy_a)
                  && gm_runtime_restore_villager_offer_stack(
                      &r, eid, 0, 1, buy_b)
                  && gm_runtime_restore_villager_offer_stack(
                      &r, eid, 0, 2, sell),
              "initialized villager trade state restores");
        CHECK(gm_runtime_villager_offer_get(&r, eid, 0, &offer)
                  && offer.uses == 3 && offer.max_uses == 9
                  && offer.rewards_exp && offer.sell.n_enchants == 1
                  && offer.sell.enchants[0].id == 70,
              "restored enchanted recipe is lossless");
        first = buy_a;
        second = buy_b;
        CHECK(gm_runtime_villager_trade_execute(
                  &r, eid, 0, &first, &second, &output, &xp)
                  && output.item == 403 && output.n_enchants == 1
                  && output.enchants[0].id == 70
                  && output.enchants[0].level == 1,
              "restored recipe executes without rerolling its output");
    }

    /* Force a cold rescan. Claimed placement sites make it idempotent even
     * when a resident has wandered or later dies. */
    int before = r.village_resident_count;
    r.village_scan_x = INT_MIN;
    CHECK(gm_runtime_sync_village_residents(&r) == 0,
          "revisiting generated chunks does not respawn villagers");
    CHECK(r.village_resident_count == before,
          "resident claim ledger stays stable after rescan");

    /* Cartographer tier four runs the two world-aware structure searches.
     * Both explorer maps retain their decoration/display NBT, independent
     * MapData, and the exact continuation across a pre-restock checkpoint. */
    {
        const char *pre = ".item07-explorer-pre.bin";
        const char *final_a = ".item07-explorer-a.bin";
        const char *final_b = ".item07-explorer-b.bin";
        const int eid = 30002;
        int resident_index = r.village_resident_count;
        CHECK(gm_runtime_spawn_villager_fixture(
                  &r, eid, 8.5, 200.0, 8.5, 0.0, 0.0, 0.0,
                  0.0F, 20.0F, 0, 0, 0, 1, 0,
                  UINT64_C(0x13579BDF2468), 0, 0.0)
                  && gm_runtime_set_mob_no_ai(&r, eid, 0),
              "cartographer strict fixture restores");
        if (resident_index < r.village_resident_count) {
            GmVillagerTrade *trade =
                &r.village_residents[resident_index].trade;
            memset(trade, 0, sizeof *trade);
            trade->initialized = 1;
            trade->profession = 1;
            trade->career = 2;
            trade->career_level = 3;
            trade->time_until_reset = 1;
            trade->needs_initialization = 1;
            r.village_trade_reset_active = 1;
            CHECK(gm_runtime_write_checkpoint(&r, pre),
                  "pre-map restock checkpoint writes");
            gm_runtime_tick(&r, (GmAction){.hotbar_sel = -1});
            CHECK(trade->career_level == 4 && trade->offer_count == 2
                      && !trade->explorer_maps_pending,
                  "cartographer emits both explorer-map offers");
            for (int index = 0; index < 2; ++index) {
                const GmVillagerOffer *offer = &trade->offers[index];
                const GmRuntimeMapData *map =
                    gm_runtime_map_data_ref(&r, offer->sell.meta);
                const GmNbtBlob *tag =
                    gm_runtime_stack_tag(&r, offer->sell.tag_id);
                GmNbtBlob marker = {0}, display = {0};
                char id[8] = {0}, name[64] = {0};
                double type = -1, x = 0.0, z = 0.0, rot = 0.0;
                CHECK(offer->buy_a.item == 388
                          && offer->buy_a.count >= 12
                          && offer->buy_b.item == 345
                          && offer->buy_b.count == 1
                          && offer->sell.item == 358
                          && offer->sell.count == 1 && tag && map,
                      "explorer recipe stack identities are complete");
                CHECK(map && map->scale == 2
                          && map->has_exploration_marker
                          && map->dimension == 0 && map->tracking_position
                          && map->unlimited_tracking,
                      "explorer MapData retains vanilla setup flags");
                CHECK(tag
                          && gm_nbt_blob_extract_compound_list_element(
                              tag, "Decorations", 0, &marker)
                          && gm_nbt_blob_find_string(
                              &marker, "id", id, sizeof id)
                          && gm_nbt_blob_find_number(
                              &marker, "type", &type, NULL)
                          && gm_nbt_blob_find_number(
                              &marker, "x", &x, NULL)
                          && gm_nbt_blob_find_number(
                              &marker, "z", &z, NULL)
                          && gm_nbt_blob_find_number(
                              &marker, "rot", &rot, NULL)
                          && !strcmp(id, "+")
                          && type == (double)(8 + index)
                          && (x != 0.0 || z != 0.0) && rot == 180.0,
                      "explorer target decoration NBT is complete");
                CHECK(tag && gm_nbt_blob_extract_compound(
                              tag, "display", &display)
                          && gm_nbt_blob_find_string(
                              &display, "Name", name, sizeof name)
                          && strstr(name, "Explorer Map") != NULL,
                      "explorer display-name NBT is complete");
                gm_nbt_blob_clear(&marker);
                gm_nbt_blob_clear(&display);
            }
            CHECK(gm_runtime_write_checkpoint(&r, final_a),
                  "uninterrupted explorer-map checkpoint writes");
            CHECK(gm_runtime_load_checkpoint(&r, pre),
                  "pre-map restock checkpoint reloads");
            gm_runtime_tick(&r, (GmAction){.hotbar_sel = -1});
            CHECK(gm_runtime_write_checkpoint(&r, final_b)
                      && files_equal(final_a, final_b),
                  "explorer-map restock is byte-exact after reload");
        }
        (void)remove(pre); (void)remove(final_a); (void)remove(final_b);
    }
    gm_runtime_destroy(&r);

    /* Zombie villages use the same structure placement sites but construct
     * persistent EntityZombieVillagers. This used to silently skip every
     * resident, leaving generated zombie villages empty. */
    {
        const uint64_t seed48 = UINT64_C(0x123456789ABC);
        GmRuntime zombie;
        PopmcVillageResident sites[GM_RUNTIME_VILLAGE_RESIDENTS];
        JavaRandom expected_random = {seed48};
        /* Exhaustive region scan pins this seed-0 extreme-hills candidate.
         * Keeping the fixture literal avoids pulling the incompatible
         * standalone overworld generator types into the runtime test TU. */
        const int start_cx = -949, start_cz = -746;
        gm_config_defaults(&cfg);
        cfg.seed = 0;
        cfg.world = GM_WORLD_DEFAULT;
        cfg.villages = 1;
        cfg.mobs = 1;
        cfg.weather = 0;
        cfg.view_distance = RADIUS;
        memset(err, 0, sizeof err);
        CHECK(gm_runtime_init(&zombie, &cfg, err, sizeof err), err);
        if (!fail) {
            gm_world_ensure(zombie.world, start_cx, start_cz, RADIUS);
            int surface = gm_world_surface_y(
                zombie.world, start_cx * 16 + 8, start_cz * 16 + 8);
            gm_runtime_set_pose(
                &zombie, start_cx * 16 + 8.5, surface + 1.0,
                start_cz * 16 + 8.5, 0.0F, 0.0F);
            int site_count = popmc_village_residents(
                cfg.seed, (start_cx - RADIUS) * 16,
                (start_cz - RADIUS) * 16,
                (start_cx + RADIUS + 1) * 16 - 1,
                (start_cz + RADIUS + 1) * 16 - 1,
                sites, GM_RUNTIME_VILLAGE_RESIDENTS);
            CHECK(site_count > 0,
                  "zombie village retains generated resident sites");
            CHECK(gm_runtime_set_world_random_seed48(&zombie, seed48),
                  "zombie-village world RNG cursor installs");
            CHECK(gm_runtime_sync_village_residents(&zombie) == site_count,
                  "runtime materializes every zombie-village resident");
            for (int i = 0; i < site_count; ++i) {
                int claimed = claimed_index(&zombie, &sites[i]);
                int profession = jrand_int_bound(&expected_random, 6);
                int child = jrand_float(&expected_random) < 0.05F;
                if (child && jrand_float(&expected_random) >= 0.05F)
                    (void)jrand_float(&expected_random);
                CHECK(sites[i].zombie_infested,
                      "selected fixture contains only zombie residents");
                CHECK(claimed >= 0,
                      "zombie resident placement coordinate is claimed");
                if (claimed >= 0) {
                    int eid = zombie.village_residents[claimed].eid;
                    int slot = gm_mobs_find_slot_by_eid(&zombie.mobs, eid);
                    const EwStore *store = zombie.mobs.current
                        ? &zombie.mobs.b : &zombie.mobs.a;
                    CHECK(slot > 0 && store->type[slot]
                              == EW_TYPE_ZOMBIE_VILLAGER,
                          "zombie resident enters the live entity store");
                    CHECK(zombie.village_residents[claimed].profession
                              == profession
                              && zombie.mobs.villager_profession[slot]
                              == profession,
                          "zombie profession follows the world RNG cursor");
                    CHECK(zombie.mobs.persistence_required[slot],
                          "zombie-village residents enable persistence");
                    CHECK(gm_runtime_villager_offer_count(&zombie, eid) == 0,
                          "zombie resident is not exposed as a merchant");
                }
            }
            CHECK(zombie.world_random_seed48 == expected_random.seed,
                  "zombie professions consume the exact world RNG draws");
            gm_runtime_destroy(&zombie);
        }
    }

    if (fail) return 1;
    printf("village_runtime: PASS residents=%d merchant=live\n", expected_count);
    return 0;
}
