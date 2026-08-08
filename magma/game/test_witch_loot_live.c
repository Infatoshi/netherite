#include "entity_witch.h"
#include "game/runtime.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s\n", message); \
        fail = 1; \
    } \
} while (0)

static EwStore *store(GmMobLive *m) {
    return m->current ? &m->b : &m->a;
}

static void expected_feedback_and_loot(
        uint64_t seed48, int looting, JavaRandom *random,
        EwitchLootOutcome *loot) {
    *random = (JavaRandom){seed48};
    (void)jrand_double(random);
    (void)jrand_double(random);
    (void)jrand_float(random);
    (void)jrand_float(random);
    ewitch_generate_loot(random, looting, loot);
}

static int exact_item(
        const GmLiveEnt *item, int eid, int expected_item,
        int count, double x, double y, double z, JavaRandom *math) {
    float hover = (float)(jrand_double(math) * (MC_PI * 2.0));
    float yaw = (float)(jrand_double(math) * 360.0);
    double motion_x = (double)(float)(
        jrand_double(math) * 0.20000000298023224
            - 0.10000000149011612);
    double motion_z = (double)(float)(
        jrand_double(math) * 0.20000000298023224
            - 0.10000000149011612);
    return item->active && item->type == 0 && item->eid == eid
        && item->item == expected_item && item->count == count
        && item->meta == 0 && item->x == x && item->y == y && item->z == z
        && item->mx == motion_x && item->my == 0.20000000298023224
        && item->mz == motion_z && item->yaw == yaw
        && item->has_hover_start && item->hover_start == hover
        && item->age == 0 && item->pickup_delay == 10
        && item->health == 5 && item->lifespan == 6000;
}

static int spawn_witch_fixture(
        GmMobLive *m, int eid, float health, uint64_t seed48) {
    gm_mobs_init(m, 0);
    m->next_id = eid;
    int slot = gm_mobs_spawn_witch(m, 10.5, 20.0, 30.5);
    if (slot <= 0 || store(m)->id[slot] != eid) return -1;
    store(m)->health[slot] = health;
    (m->current ? &m->a : &m->b)->health[slot] = health;
    if (!gm_mobs_set_entity_random_state(m, eid, seed48, 0, 0.0))
        return -1;
    return slot;
}

int main(void) {
    const uint64_t entity_seed48 = 0;
    const uint64_t math_seed48 = UINT64_C(67890);
    GmMobLive mobs;
    GmLiveSim drops;
    JavaRandom expected_random, expected_math;
    EwitchLootOutcome expected_loot;
    GmMobEvent event;
    uint64_t math_cursor = math_seed48;
    int next_id = 1000;
    int slot = spawn_witch_fixture(&mobs, 700, 1.0F, entity_seed48);
    GmMobDeathContext context = {1, &math_cursor, &next_id};
    memset(&drops, 0, sizeof drops);
    expected_feedback_and_loot(
        entity_seed48, 2, &expected_random, &expected_loot);
    expected_math = (JavaRandom){math_seed48};

    CHECK(slot > 0 && expected_loot.count == 3,
          "Witch live fixture has a three-stack exact loot row");
    CHECK(gm_mobs_player_damage_witch_exact(
              &mobs, 700, 9.5, 30.5, 2.0F, 2, &drops, &context) == 2,
          "lethal player Witch damage accepts");
    CHECK(store(&mobs)->alive[slot] && mobs.entity_dead[slot]
              && store(&mobs)->health[slot] == 0.0F
              && mobs.entity_death_time[slot] == 0,
          "Witch enters the Java death-update window after loot");
    CHECK(drops.n_active == expected_loot.count,
          "all nonempty Witch loot stacks become separate EntityItems");
    for (int i = 0; i < expected_loot.count; ++i)
        CHECK(exact_item(
                  &drops.ents[i], 1000 + i, expected_loot.item[i],
                  expected_loot.quantity[i], 10.5, 20.0, 30.5,
                  &expected_math),
              "Witch live drop preserves exact item constructor state");
    if (mobs.entity_random[slot].random.seed != expected_random.seed
            || math_cursor != expected_math.seed
            || next_id != 1000 + expected_loot.count
            || mobs.next_id != next_id || mobs.next_orb_id != next_id)
        fprintf(stderr, "cursor got entity=%llu math=%llu next=%d "
                "mob_next=%d orb_next=%d expected entity=%llu math=%llu "
                "next=%d\n",
            (unsigned long long)mobs.entity_random[slot].random.seed,
            (unsigned long long)math_cursor, next_id,
            mobs.next_id, mobs.next_orb_id,
            (unsigned long long)expected_random.seed,
            (unsigned long long)expected_math.seed,
            1000 + expected_loot.count);
    CHECK(mobs.entity_random[slot].random.seed == expected_random.seed
              && math_cursor == expected_math.seed
              && next_id == 1000 + expected_loot.count
              && mobs.next_id == next_id && mobs.next_orb_id == next_id,
          "Witch, Math, and entity-ID cursors commit exact live values");
    CHECK(gm_mobs_event_count(&mobs) == 3
              && gm_mobs_event_get(&mobs, 0, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS
              && event.eid == 700 && event.data == 2
              && gm_mobs_event_get(&mobs, 1, &event)
              && event.kind == GM_MOB_EVENT_SOUND
              && event.data == GM_MOB_SOUND_WITCH_DEATH
              && gm_mobs_event_get(&mobs, 2, &event)
              && event.kind == GM_MOB_EVENT_ENTITY_STATUS
              && event.data == 3,
          "Witch death orders hurt status, death sound, and death status");

    /* The product player attack extracts Looting from the held stack and
     * enters the same exact Witch boundary before its attack tail. */
    {
        PsvPlayer player;
        McSinTable sin_table;
        ICStack weapon = ic_mk(280, 1, 0);
        uint64_t product_math = math_seed48;
        int product_next = 1000;
        int product_slot = spawn_witch_fixture(
            &mobs, 703, 1.0F, entity_seed48);
        GmMobDeathContext product_context = {
            1, &product_math, &product_next
        };
        JavaRandom product_expected;
        EwitchLootOutcome product_loot;
        memset(&player, 0, sizeof player);
        isr_init(&player.inv);
        player.ent.posX = 10.5;
        player.ent.posY = 19.28;
        player.ent.posZ = 32.5;
        player.ent.onGround = 1;
        player.yaw = 180.0F;
        player.movement_speed_multiplier = 1.0;
        weapon.n_enchants = 1;
        weapon.enchants[0].id = 21;
        weapon.enchants[0].level = 2;
        isr_set_stack(&player.inv, 0, weapon);
        mc_sin_table_init(&sin_table);
        memset(&drops, 0, sizeof drops);
        expected_feedback_and_loot(
            entity_seed48, 2, &product_expected, &product_loot);
        mobs.player_ticks_since_last_swing = 5;
        CHECK(product_slot > 0 && gm_mobs_player_attack(
                  &mobs, (const struct PsvPlayer *)&player, 0, 0,
                  (const struct McSinTable *)&sin_table, &drops,
                  0.0F, 1.0, 0, 0, 0, &product_context,
                  0.0F, NULL) == 2,
              "product player attack accepts a lethal Witch hit");
        CHECK(mobs.entity_dead[product_slot]
                  && drops.n_active == product_loot.count
                  && product_loot.count == 3
                  && mobs.entity_random[product_slot].random.seed
                      == product_expected.seed
                  && product_next == 1000 + product_loot.count,
              "product player attack applies held Looting to exact Witch loot");
    }

    /* A false doMobLoot gamerule skips both table RNG and EntityItem Math. */
    {
        uint64_t no_loot_math = UINT64_C(12345);
        int no_loot_next = 200;
        int no_loot_slot = spawn_witch_fixture(
            &mobs, 701, 1.0F, UINT64_C(9));
        GmMobDeathContext no_loot = {
            0, &no_loot_math, &no_loot_next
        };
        JavaRandom feedback = {UINT64_C(9)};
        memset(&drops, 0, sizeof drops);
        (void)jrand_double(&feedback);
        (void)jrand_double(&feedback);
        (void)jrand_float(&feedback);
        (void)jrand_float(&feedback);
        CHECK(no_loot_slot > 0 && gm_mobs_player_damage_witch_exact(
                  &mobs, 701, 9.5, 30.5, 2.0F, 3,
                  &drops, &no_loot) == 2,
              "disabled Witch loot still accepts lethal damage");
        CHECK(drops.n_active == 0
                  && mobs.entity_random[no_loot_slot].random.seed
                      == feedback.seed
                  && no_loot_math == UINT64_C(12345)
                  && no_loot_next == 200,
              "disabled Witch loot consumes no table or constructor RNG");
    }

    /* Fixed live-item capacity rejects the whole damage boundary atomically. */
    {
        uint64_t full_math = math_seed48;
        int full_next = 300;
        int full_slot = spawn_witch_fixture(
            &mobs, 702, 1.0F, entity_seed48);
        GmMobDeathContext full_context = {1, &full_math, &full_next};
        memset(&drops, 0, sizeof drops);
        for (int i = 0; i < GM_LIVE_MAX; ++i)
            drops.ents[i].active = 1;
        drops.n_active = GM_LIVE_MAX;
        drops.item_spawn_limit = GM_LIVE_MAX;
        CHECK(full_slot > 0 && gm_mobs_player_damage_witch_exact(
                  &mobs, 702, 9.5, 30.5, 2.0F, 2,
                  &drops, &full_context) == 1,
              "full item store reports a rejected exact Witch hit");
        CHECK(store(&mobs)->health[full_slot] == 1.0F
                  && !mobs.entity_dead[full_slot]
                  && mobs.entity_random[full_slot].random.seed
                      == entity_seed48
                  && full_math == math_seed48 && full_next == 300
                  && gm_mobs_event_count(&mobs) == 0,
              "capacity rejection leaves the damage boundary atomic");
    }

    if (fail) return 1;
    puts("witch_loot_live: PASS exact=3 product=1 gamerule=1 "
         "capacity_atomic=1");
    return 0;
}
