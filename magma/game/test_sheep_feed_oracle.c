#include "game/mob_live.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_int(const char *text, int *out) {
    char *end = NULL;
    long value = strtol(text, &end, 0);
    if (!text[0] || !end || *end || value < INT_MIN || value > INT_MAX)
        return 0;
    *out = (int)value;
    return 1;
}

static int parse_species(const char *text, int *type) {
    if (!strcmp(text, "sheep")) *type = EW_TYPE_SHEEP;
    else if (!strcmp(text, "cow")) *type = EW_TYPE_COW;
    else if (!strcmp(text, "pig")) *type = EW_TYPE_PIG;
    else if (!strcmp(text, "chicken")) *type = EW_TYPE_CHICKEN;
    else return 0;
    return 1;
}

static int interact_animal(
        GmMobLive *mobs, int type, int eid, IsrInv *inventory,
        int hand_slot, int creative) {
    int milk = gm_mobs_milk_cow(
        mobs, eid, inventory, hand_slot, creative,
        0.5, 220.0, 0.5, 0.0F, 0.0F, (double)1.62F,
        NULL, NULL, NULL, NULL);
    if (milk != 0) return 1;
    if (gm_mobs_feed_animal(
            mobs, eid, inventory, hand_slot, creative))
        return 1;
    if (type == EW_TYPE_PIG
            && isr_get_stack(inventory, hand_slot).item == 421)
        return 1;
    {
        int saddled = 0;
        if (type == EW_TYPE_PIG
                && gm_mobs_get_pig_saddled(mobs, eid, &saddled)
                && saddled && !gm_mobs_pig_riding(mobs, NULL))
            return gm_mobs_pig_mount(mobs, eid);
    }
    return gm_mobs_saddle_pig(
        mobs, eid, inventory, hand_slot, creative);
}

int main(int argc, char **argv) {
    int age, in_love, main_item, main_count, off_item, off_count;
    int creative, eid, initial_saddled, sneaking, type;
    if (argc != 13 || !parse_species(argv[1], &type)
            || (strcmp(argv[2], "main") && strcmp(argv[2], "offhand")
                && strcmp(argv[2], "client_order"))
            || !parse_int(argv[3], &age)
            || !parse_int(argv[4], &in_love)
            || !parse_int(argv[5], &main_item)
            || !parse_int(argv[6], &main_count)
            || !parse_int(argv[7], &off_item)
            || !parse_int(argv[8], &off_count)
            || !parse_int(argv[9], &creative)
            || !parse_int(argv[10], &eid)
            || !parse_int(argv[11], &initial_saddled)
            || !parse_int(argv[12], &sneaking)
            || in_love < 0 || in_love > 600
            || main_item < 0 || main_item > 32767
            || off_item < 0 || off_item > 32767
            || main_count < 0 || main_count > 64
            || off_count < 0 || off_count > 64
            || (creative != 0 && creative != 1)
            || (initial_saddled != 0 && initial_saddled != 1)
            || (sneaking != 0 && sneaking != 1) || eid <= 0) {
        fprintf(stderr, "usage: %s sheep|cow|pig|chicken "
            "main|offhand|client_order AGE IN_LOVE "
            "MAIN_ITEM MAIN_COUNT OFF_ITEM OFF_COUNT CREATIVE EID "
            "SADDLED SNEAKING\n", argv[0]);
        return 2;
    }

    GmMobLive mobs;
    IsrInv inventory;
    gm_mobs_init(&mobs, 0);
    isr_init(&inventory);
    isr_set_stack(&inventory, 0,
        main_count > 0 ? ic_mk(main_item, main_count, 0) : ic_empty());
    isr_set_stack(&inventory, ISR_OFFHAND_SLOT,
        off_count > 0 ? ic_mk(off_item, off_count, 0) : ic_empty());
    if (gm_mobs_spawn_exact(
            &mobs, type, eid, 2.5, 220.0, 0.5,
            0.0, 0.0, 0.0, 0.0F,
            type == EW_TYPE_SHEEP ? 8.0F
                : type == EW_TYPE_CHICKEN ? 4.0F : 10.0F,
            1, 0, 0, 0) < 0
            || !gm_mobs_set_growing_age(&mobs, eid, age)
            || !gm_mobs_set_animal_breeding_state(
                &mobs, eid, in_love, 0, 0, 0))
        return 1;
    if (type == EW_TYPE_PIG
            && !gm_mobs_set_pig_saddled(&mobs, eid, initial_saddled))
        return 1;

    int results[2], result_count = 0;
    if (!strcmp(argv[2], "main") || !strcmp(argv[2], "client_order"))
        results[result_count++] = interact_animal(
            &mobs, type, eid, &inventory, 0, creative);
    if (!strcmp(argv[2], "offhand")
            || (!strcmp(argv[2], "client_order")
                && result_count == 1 && !results[0]))
        results[result_count++] = interact_animal(
            &mobs, type, eid, &inventory, ISR_OFFHAND_SLOT, creative);

    int got_age, got_love, forced_age, forced_timer, bred;
    if (!gm_mobs_get_animal_breeding_state(
            &mobs, eid, &got_age, &got_love,
            &forced_age, &forced_timer, &bred))
        return 1;
    int saddled = 0;
    if (type == EW_TYPE_PIG
            && !gm_mobs_get_pig_saddled(&mobs, eid, &saddled))
        return 1;
    int riding_eid = 0;
    int pig_being_ridden = type == EW_TYPE_PIG
        && gm_mobs_pig_riding(&mobs, &riding_eid) && riding_eid == eid;
    ICStack main_stack = isr_get_stack(&inventory, 0);
    ICStack off_stack = isr_get_stack(&inventory, ISR_OFFHAND_SLOT);
    printf("{\"ok\":true,\"species\":\"%s\",\"results\":[", argv[1]);
    for (int i = 0; i < result_count; ++i) {
        if (i) putchar(',');
        printf("\"%s\"", results[i] ? "success" : "pass");
    }
    printf("],\"eid\":%d,\"growing_age\":%d,\"in_love\":%d,"
           "\"forced_age\":%d,\"forced_age_timer\":%d,"
           "\"bred_by_player\":%s,\"saddled\":%s,"
           "\"pig_being_ridden\":%s,\"player_riding_eid\":%d,"
           "\"main_item\":%d,\"main_count\":%d,"
           "\"off_item\":%d,\"off_count\":%d,\"events\":[",
        eid, got_age, got_love, forced_age, forced_timer,
        bred ? "true" : "false", saddled ? "true" : "false",
        pig_being_ridden ? "true" : "false",
        pig_being_ridden ? riding_eid : 0,
        main_stack.count > 0 ? main_stack.item : 0,
        main_stack.count > 0 ? main_stack.count : 0,
        off_stack.count > 0 ? off_stack.item : 0,
        off_stack.count > 0 ? off_stack.count : 0);
    int event_count = gm_mobs_event_count(&mobs);
    for (int i = 0; i < event_count; ++i) {
        GmMobEvent event;
        if (!gm_mobs_event_get(&mobs, i, &event)) return 1;
        if (i) putchar(',');
        if (event.kind == GM_MOB_EVENT_ENTITY_STATUS) {
            printf("{\"kind\":\"status\",\"eid\":%d,\"status\":%d}",
                event.eid, event.data);
        } else if (event.kind == GM_MOB_EVENT_SOUND
                && (event.data == GM_MOB_SOUND_COW_MILK
                    || event.data == GM_MOB_SOUND_ITEM_ARMOR_EQUIP_GENERIC
                    || event.data == GM_MOB_SOUND_PIG_SADDLE)) {
            printf("{\"kind\":\"sound\",\"eid\":%d,"
                   "\"sound\":\"minecraft:%s\","
                   "\"category\":\"%s\","
                   "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                   "\"volume\":%.9g,\"pitch\":%.9g}",
                event.eid,
                event.data == GM_MOB_SOUND_COW_MILK
                    ? "entity.cow.milk"
                    : event.data == GM_MOB_SOUND_PIG_SADDLE
                        ? "entity.pig.saddle" : "item.armor.equip_generic",
                event.data == GM_MOB_SOUND_PIG_SADDLE
                    ? "neutral" : "player",
                event.x, event.y, event.z,
                (double)event.volume, (double)event.pitch);
        } else {
            return 1;
        }
    }
    puts("]}");
    return 0;
}
