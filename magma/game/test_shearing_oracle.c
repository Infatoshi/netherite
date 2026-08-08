#include "game/mob_live.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sheep_slot(const GmMobLive *m, int eid) {
    const EwStore *s = m->current ? &m->b : &m->a;
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot)
        if (s->alive[slot] && s->id[slot] == eid)
            return slot;
    return -1;
}

static int enchantment_level(const ICStack *stack, int id) {
    for (int i = 0; i < stack->n_enchants; ++i)
        if (stack->enchants[i].id == id)
            return stack->enchants[i].level;
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 9) {
        fprintf(stderr, "usage: %s MODE X Y Z ENTITY_SEED MATH_SEED "
                        "SHEAR_SEED NEXT_ID\n", argv[0]);
        return 2;
    }
    const char *mode = argv[1];
    if (strcmp(mode, "count1") && strcmp(mode, "count3")
            && strcmp(mode, "adult_offhand")
            && strcmp(mode, "already_sheared") && strcmp(mode, "child")
            && strcmp(mode, "non_shears") && strcmp(mode, "unbreaking")
            && strcmp(mode, "capacity")) {
        fprintf(stderr, "unknown mode: %s\n", mode);
        return 2;
    }
    double x = strtod(argv[2], NULL), y = strtod(argv[3], NULL);
    double z = strtod(argv[4], NULL);
    uint64_t entity_seed = strtoull(argv[5], NULL, 0);
    uint64_t math_seed = strtoull(argv[6], NULL, 0);
    uint64_t shear_seed = strtoull(argv[7], NULL, 0);
    int next_id = atoi(argv[8]);
    const int sheep_eid = next_id;
    int hand_slot = !strcmp(mode, "adult_offhand")
        ? ISR_OFFHAND_SLOT : 0;
    int initially_sheared = !strcmp(mode, "already_sheared");
    int child = !strcmp(mode, "child");
    int held_item = !strcmp(mode, "non_shears") ? 280 : 359;
    int tool_meta = 0;
    int unbreaking = !strcmp(mode, "unbreaking") ? 3 : 0;

    GmMobLive mobs;
    GmLiveSim drops;
    IsrInv inventory;
    memset(&drops, 0, sizeof drops);
    isr_init(&inventory);
    gm_mobs_init(&mobs, 0);
    mobs.active_dimension = 0;
    int slot = gm_mobs_spawn_exact(
        &mobs, EW_TYPE_SHEEP, sheep_eid,
        x, y, z, 0.0, 0.0, 0.0, 0.0F, 8.0F, 1, 0, 0, 0);
    int random_ok = gm_mobs_set_entity_random_state(
        &mobs, sheep_eid, entity_seed, 0, 0.0);
    int sheep_ok = gm_mobs_set_sheep_state(
        &mobs, sheep_eid, 14, initially_sheared);
    int age_ok = gm_mobs_set_growing_age(
        &mobs, sheep_eid, child ? -100 : 0);
    if (slot < 0 || !random_ok || !sheep_ok || !age_ok) {
        fprintf(stderr, "fixture setup failed: slot=%d random=%d sheep=%d age=%d\n",
                slot, random_ok, sheep_ok, age_ok);
        return 1;
    }
    ICStack tool = ic_mk(held_item, 1, tool_meta);
    if (unbreaking) {
        tool.n_enchants = 1;
        tool.enchants[0].id = 34;
        tool.enchants[0].level = (short)unbreaking;
    }
    isr_set_stack(&inventory, hand_slot, tool);
    next_id = sheep_eid + 1;
    if (!strcmp(mode, "capacity")) {
        for (int i = 0; i < GM_LIVE_MAX; ++i)
            if (!gm_live_spawn_item_exact(
                    &drops, 700000 + i, x, y, z,
                    0.0, 0.0, 0.0, 0.0F,
                    1, 1, 0, 0, 10, 1))
                return 1;
    }
    int result = gm_mobs_shear_sheep(
        &mobs, sheep_eid, &inventory, hand_slot,
        &shear_seed, &math_seed, &drops, &next_id);
    slot = sheep_slot(&mobs, sheep_eid);
    if (slot < 0) return 1;
    ICStack after = isr_get_stack(&inventory, hand_slot);
    printf("{\"ok\":true,\"result_code\":%d,\"eid\":%d,"
           "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
           "\"fleece\":%d,\"sheared\":%s,\"growing_age\":%d,"
           "\"entity_seed48\":%" PRIu64 ",\"math_seed48\":%" PRIu64 ","
           "\"shear_random_constructed\":%s,"
           "\"shear_seed48\":%" PRIu64 ",\"next_entity_id\":%d,"
           "\"tool_item\":%d,\"tool_count\":%d,\"tool_meta\":%d,"
           "\"tool_unbreaking\":%d,\"drops\":[",
        result, sheep_eid, x, y, z,
        mobs.sheep_data[slot] & 15,
        (mobs.sheep_data[slot] & 16) ? "true" : "false",
        mobs.growing_age[slot], mobs.entity_random[slot].random.seed,
        math_seed, result == 2 ? "true" : "false", shear_seed, next_id,
        after.item, after.count, after.meta,
        enchantment_level(&after, 34));
    int first = 1;
    for (int eid = sheep_eid + 1; eid < next_id; ++eid) {
        const GmLiveEnt *item = NULL;
        for (int i = 0; i < GM_LIVE_MAX; ++i)
            if (drops.ents[i].active && drops.ents[i].eid == eid) {
                item = &drops.ents[i];
                break;
            }
        if (!item) continue;
        if (!first) putchar(',');
        first = 0;
        printf("{\"eid\":%d,\"item\":%d,\"count\":%d,\"meta\":%d,"
               "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
               "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,"
               "\"yaw\":%.9g,\"hover_start\":%.9g,"
               "\"age\":%d,\"pickup_delay\":%d,\"health\":%d,"
               "\"lifespan\":%d,\"on_ground\":%s,\"dead\":false}",
            item->eid, item->item, item->count, item->meta,
            item->x, item->y, item->z,
            item->mx, item->my, item->mz,
            (double)item->yaw, (double)item->hover_start,
            item->age, item->pickup_delay, item->health, item->lifespan,
            item->on_ground ? "true" : "false");
    }
    fputs("],\"events\":[", stdout);
    for (int i = 0; i < gm_mobs_event_count(&mobs); ++i) {
        GmMobEvent event;
        if (!gm_mobs_event_get(&mobs, i, &event)) return 1;
        if (i) putchar(',');
        printf("{\"kind\":\"sound\",\"eid\":%d,"
               "\"sound\":\"minecraft:entity.sheep.shear\","
               "\"category\":\"neutral\","
               "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
               "\"volume\":%.9g,\"pitch\":%.9g}",
            event.eid, event.x, event.y, event.z,
            (double)event.volume, (double)event.pitch);
    }
    fputs("]}\n", stdout);
    return 0;
}
