#include "game/mob_live.h"
#include "entity_hostile_spine.h"
#include "inventory_stack_rules.h"
#include "player_vitals.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t fbits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static uint64_t dbits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static EwStore *store(GmMobLive *m) {
    return m->current ? &m->b : &m->a;
}

static int attributes(const char *label, int type) {
    GmMobLive mobs;
    gm_mobs_init(&mobs, 0);
    int slot = gm_mobs_spawn(&mobs, type, 0.5, 1.0, 0.5);
    if (slot <= 0) return 0;
    float width, height;
    ehs_size_scaled((u8)type, 1, &width, &height);
    double speed = (double)ehs_land_speed((u8)type);
    double attack = type == EW_TYPE_ELDER_GUARDIAN ? 8.0 : 6.0;
    int duration = type == EW_TYPE_ELDER_GUARDIAN ? 60 : 80;
    printf("%s %08" PRIx32 " %08" PRIx32 " %08" PRIx32
           " %016" PRIx64 " %016" PRIx64 " %016" PRIx64
           " %016" PRIx64 " %d\n",
        label, fbits(width), fbits(height), fbits(height * 0.5F),
        dbits((double)gm_mobs_max_health(&mobs, slot)), dbits(speed),
        dbits(attack), dbits(16.0), duration);
    return 1;
}

static int beam(const char *label, int type) {
    GmMobLive mobs;
    PvStats vitals;
    IsrInv inventory;
    gm_mobs_init(&mobs, 0);
    pv_init(&vitals);
    isr_init(&inventory);
    int slot = gm_mobs_spawn(&mobs, type, 0.5, 1.0, 0.5);
    if (slot <= 0) return 0;
    int eid = store(&mobs)->id[slot];
    int target_tick = -1, damage_tick = -1;
    int seen_events = 0, last_status = 0, status_count = 0;
    for (int tick = 1; tick <= 100; ++tick) {
        float before = vitals.health;
        (void)gm_mobs_guardian_attack_step(
            &mobs, eid, (struct PvStats *)&vitals,
            (struct IsrInv *)&inventory);
        int count = gm_mobs_event_count(&mobs);
        for (; seen_events < count; ++seen_events) {
            GmMobEvent event;
            if (!gm_mobs_event_get(&mobs, seen_events, &event)) return 0;
            if (event.kind != GM_MOB_EVENT_ENTITY_STATUS) continue;
            last_status = event.data;
            ++status_count;
            if (target_tick < 0 && event.data == 21) target_tick = tick;
        }
        if (damage_tick < 0 && vitals.health != before) damage_tick = tick;
        if (damage_tick >= 0) break;
    }
    printf("%s %d %d %08" PRIx32 " %d %d\n", label,
        target_tick, damage_tick, fbits(vitals.health),
        last_status, status_count);
    return 1;
}

static int thorns(void) {
    GmMobLive mobs;
    gm_mobs_init(&mobs, 0);
    int slot = gm_mobs_spawn(
        &mobs, EW_TYPE_GUARDIAN, 0.5, 1.0, 0.5);
    if (slot <= 0) return 0;
    int eid = store(&mobs)->id[slot];
    float player = gm_mobs_guardian_thorns(
        &mobs, eid, 20.0F, 1.0F, GM_DAMAGE_SOURCE_GENERIC);
    printf("T %08" PRIx32 " %08" PRIx32 "\n",
        fbits(player), fbits(store(&mobs)->health[slot]));
    return 1;
}

static int loot(void) {
    static const uint64_t seeds[] = {
        UINT64_C(0), UINT64_C(2), UINT64_C(95), UINT64_C(402),
        (UINT64_C(1) << 48) - 1
    };
    static const int types[] = {
        EW_TYPE_GUARDIAN, EW_TYPE_ELDER_GUARDIAN
    };
    static const char *names[] = {"guardian", "elder_guardian"};
    for (int type = 0; type < 2; ++type)
        for (int seed = 0; seed < 5; ++seed)
            for (int fixture = 0; fixture < 3; ++fixture) {
                int looting = fixture == 1 ? 3 : 0;
                int killed = fixture != 2;
                uint64_t cursor = seeds[seed];
                GmHostileLootOutcome out;
                if (!gm_mobs_generate_hostile_loot(
                        types[type], 1, &cursor, looting, killed, &out))
                    return 0;
                printf("%s %" PRIu64 " %d %d %d", names[type],
                    seeds[seed], looting, killed, out.count);
                for (int item = 0; item < out.count; ++item)
                    printf(" %d:%d:%d", out.item[item],
                        out.quantity[item], out.meta[item]);
                printf(" %" PRIu64 "\n", cursor);
            }
    return 1;
}

int main(void) {
    return attributes("G", EW_TYPE_GUARDIAN)
        && beam("B", EW_TYPE_GUARDIAN)
        && attributes("E", EW_TYPE_ELDER_GUARDIAN)
        && beam("D", EW_TYPE_ELDER_GUARDIAN)
        && thorns() && loot() ? 0 : 1;
}
