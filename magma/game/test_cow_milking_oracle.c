#include "game/mob_live.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_seed(const char *text, uint64_t *out) {
    char *end = NULL;
    unsigned long long value;
    errno = 0;
    value = strtoull(text, &end, 0);
    if (errno || !text[0] || !end || *end || value >= (UINT64_C(1) << 48))
        return 0;
    *out = (uint64_t)value;
    return 1;
}

static int parse_int(const char *text, int *out) {
    char *end = NULL;
    long value;
    errno = 0;
    value = strtol(text, &end, 0);
    if (errno || !text[0] || !end || *end
            || value < INT_MIN || value > INT_MAX)
        return 0;
    *out = (int)value;
    return 1;
}

static int parse_float(const char *text, float *out) {
    char *end = NULL;
    float value;
    errno = 0;
    value = strtof(text, &end);
    if (errno || !text[0] || !end || *end || value != value)
        return 0;
    *out = value;
    return 1;
}

static int parse_double(const char *text, double *out) {
    char *end = NULL;
    double value;
    errno = 0;
    value = strtod(text, &end);
    if (errno || !text[0] || !end || *end || value != value)
        return 0;
    *out = value;
    return 1;
}

static void emit_event(const GmMobEvent *event) {
    const char *sound;
    if (event->kind != GM_MOB_EVENT_SOUND) exit(1);
    if (event->data == GM_MOB_SOUND_COW_MILK)
        sound = "entity.cow.milk";
    else if (event->data == GM_MOB_SOUND_ITEM_ARMOR_EQUIP_GENERIC)
        sound = "item.armor.equip_generic";
    else
        exit(1);
    printf("{\"kind\":\"sound\",\"eid\":%d,"
           "\"sound\":\"minecraft:%s\",\"category\":\"player\","
           "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
           "\"volume\":%.9g,\"pitch\":%.9g}",
        event->eid, sound, event->x, event->y, event->z,
        (double)event->volume, (double)event->pitch);
}

static void emit_drop(const GmLiveEnt *item) {
    printf("{\"eid\":%d,\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
           "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,"
           "\"yaw\":%.9g,\"item\":%d,\"count\":%d,\"meta\":%d,"
           "\"age\":%d,\"pickup_delay\":%d,\"health\":%d,"
           "\"lifespan\":%d,\"hover_start\":%.9g,"
           "\"on_ground\":%s,\"is_dead\":false}",
        item->eid, item->x, item->y, item->z,
        item->mx, item->my, item->mz, (double)item->yaw,
        item->item, item->count, item->meta, item->age,
        item->pickup_delay, item->health, item->lifespan,
        (double)item->hover_start, item->on_ground ? "true" : "false");
}

int main(int argc, char **argv) {
    const char *mode;
    uint64_t math_seed, player_seed;
    int cow_eid;
    float yaw, pitch;
    double player_x, player_z;
    if (argc != 9 || ((mode = argv[1]), strcmp(mode, "insert")
                && strcmp(mode, "drop") && strcmp(mode, "replace_full"))
            || !parse_seed(argv[2], &math_seed)
            || !parse_seed(argv[3], &player_seed)
            || !parse_int(argv[4], &cow_eid) || cow_eid <= 0
            || cow_eid >= INT_MAX - 2
            || !parse_float(argv[5], &yaw)
            || !parse_float(argv[6], &pitch)
            || !parse_double(argv[7], &player_x)
            || !parse_double(argv[8], &player_z)
            || pitch < -90.0F || pitch > 90.0F) {
        fprintf(stderr, "usage: %s insert|drop|replace_full "
            "MATH_SEED48 PLAYER_SEED48 COW_EID YAW PITCH PLAYER_X PLAYER_Z\n",
            argv[0]);
        return 2;
    }

    GmMobLive mobs;
    GmLiveSim drops;
    IsrInv inventory;
    McSinTable sin_table;
    int next_eid = cow_eid + 1;
    gm_mobs_init(&mobs, 0);
    memset(&drops, 0, sizeof drops);
    isr_init(&inventory);
    mc_sin_table_init(&sin_table);
    if (strcmp(mode, "insert"))
        for (int i = 1; i < ISR_MAIN_SLOTS; ++i)
            isr_set_stack(&inventory, i, ic_mk(1, 64, 0));
    isr_set_stack(&inventory, 0,
        ic_mk(325, !strcmp(mode, "replace_full") ? 1 : 2, 0));
    if (gm_mobs_spawn_exact(
            &mobs, EW_TYPE_COW, cow_eid,
            2.5, 220.0, 0.5, 0.0, 0.0, 0.0, 0.0F,
            10.0F, 1, 0, 0, 0) < 0
            || !gm_mobs_set_growing_age(&mobs, cow_eid, 0))
        return 1;
    jrand_set_seed48(&mobs.player_random, player_seed);
    int result = gm_mobs_milk_cow(
        &mobs, cow_eid, &inventory, 0, 0,
        player_x, 220.0, player_z, yaw, pitch, (double)1.62F,
        &sin_table, &math_seed, &drops, &next_eid);
    if (result != 1) return 1;

    printf("{\"ok\":true,\"mode\":\"%s\",\"result\":\"success\","
           "\"cow_eid\":%d,\"player_x\":%.17g,\"player_y\":220,"
           "\"player_z\":%.17g,\"inventory\":[",
        mode, cow_eid, player_x, player_z);
    for (int i = 0; i < ISR_MAIN_SLOTS; ++i) {
        ICStack stack = isr_get_stack(&inventory, i);
        if (i) putchar(',');
        printf("[%d,%d,%d]", stack.count > 0 ? stack.item : 0,
            stack.count > 0 ? stack.count : 0,
            stack.count > 0 ? stack.meta : 0);
    }
    ICStack off = isr_get_stack(&inventory, ISR_OFFHAND_SLOT);
    printf("],\"off_item\":%d,\"off_count\":%d,\"events\":[",
        off.count > 0 ? off.item : 0, off.count > 0 ? off.count : 0);
    for (int i = 0; i < gm_mobs_event_count(&mobs); ++i) {
        GmMobEvent event;
        if (!gm_mobs_event_get(&mobs, i, &event)) return 1;
        if (i) putchar(',');
        emit_event(&event);
    }
    printf("],\"drops\":[");
    int emitted = 0;
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        if (!drops.ents[i].active) continue;
        if (emitted++) putchar(',');
        emit_drop(&drops.ents[i]);
    }
    printf("],\"math_seed48\":%llu,\"player_seed48\":%llu,"
           "\"next_entity_id\":%d}\n",
        (unsigned long long)math_seed,
        (unsigned long long)mobs.player_random.seed, next_eid);
    return 0;
}
