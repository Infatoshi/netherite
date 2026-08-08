#include "game/runtime.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_int(const char *text, int *out) {
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (!text[0] || !end || *end || value < INT_MIN || value > INT_MAX)
        return 0;
    *out = (int)value;
    return 1;
}

static int parse_seed(const char *text, uint64_t *out) {
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (!text[0] || !end || *end || value >= (1ULL << 48)) return 0;
    *out = (uint64_t)value;
    return 1;
}

static uint32_t float_bits(float value) {
    union { float f; uint32_t u; } bits = {value};
    return bits.u;
}

static uint64_t double_bits(double value) {
    union { double d; uint64_t u; } bits = {value};
    return bits.u;
}

static EwStore *store(GmMobLive *m) {
    return m->current ? &m->b : &m->a;
}

static PtMobEffect *effect(GmMobLive *m, int slot, int id) {
    for (int i = 0; i < m->entity_effect_count[slot]; ++i)
        if (m->entity_effects[slot][i].id == id)
            return &m->entity_effects[slot][i];
    return NULL;
}

static void print_events(const GmMobLive *m) {
    putchar('[');
    for (int i = 0; i < gm_mobs_event_count(m); ++i) {
        GmMobEvent row;
        if (!gm_mobs_event_get(m, i, &row)) exit(1);
        if (i) putchar(',');
        if (row.kind != GM_MOB_EVENT_ENTITY_STATUS) exit(1);
        printf("{\"kind\":\"status\",\"eid\":%d,\"status\":%d}",
               row.eid, row.data);
    }
    putchar(']');
}

int main(int argc, char **argv) {
    int world_x, world_z, eid, profession, child, no_ai, weakness;
    int item, meta, count, creative, preset, accelerators;
    uint64_t entity_seed;
    if (argc != 17 || (strcmp(argv[1], "interact")
                && strcmp(argv[1], "progress") && strcmp(argv[1], "finish")
                && strcmp(argv[1], "audio"))
            || !parse_int(argv[2], &world_x)
            || !parse_int(argv[3], &world_z)
            || !parse_seed(argv[4], &entity_seed)
            || !parse_int(argv[5], &eid)
            || !parse_int(argv[6], &profession)
            || !parse_int(argv[7], &child)
            || !parse_int(argv[8], &no_ai)
            || !parse_int(argv[9], &weakness)
            || !parse_int(argv[10], &item)
            || !parse_int(argv[11], &meta)
            || !parse_int(argv[12], &count)
            || !parse_int(argv[13], &creative)
            || !parse_int(argv[14], &preset)
            || !parse_int(argv[15], &accelerators)
            || profession < 0 || profession > 5 || eid <= 0
            || (child != 0 && child != 1) || (no_ai != 0 && no_ai != 1)
            || (weakness != 0 && weakness != 1)
            || (creative != 0 && creative != 1)
            || count < 0 || count > 64 || preset < -1
            || accelerators < 0 || accelerators > 14) {
        fprintf(stderr, "invalid zombie-villager cure fixture\n");
        return 2;
    }

    GmWorld *world = gm_world_create_type(0, 1);
    GmMobLive *mobs = calloc(1, sizeof *mobs);
    if (!world || !mobs) return 1;
    gm_world_ensure(world, world_x >> 4, world_z >> 4, 1);
    gm_mobs_init(mobs, 0);
    mobs->next_id = eid;
    int slot = gm_mobs_spawn_zombie_villager(
        mobs, world_x + 0.5, 220.0, world_z + 0.5, profession);
    if (slot <= 0 || store(mobs)->id[slot] != eid
            || !gm_mobs_set_entity_random_state(
                mobs, eid, entity_seed, 0, 0.0D))
        return 1;
    mobs->growing_age[slot] = child ? -1 : 0;
    mobs->controlled_no_ai[slot] = (unsigned char)no_ai;
    if (weakness && !gm_mobs_apply_potion_effect(mobs, slot, 18, 0, 100))
        return 1;

    printf("{\"ok\":true,\"mode\":\"%s\",\"eid\":%d,"
           "\"world_x\":%d,\"world_y\":220,\"world_z\":%d,",
           argv[1], eid, world_x, world_z);
    if (!strcmp(argv[1], "audio")) {
        double x, y, z;
        float volume, pitch;
        if (!gm_mobs_zombie_villager_cure_audio(
                mobs, eid, &x, &y, &z, &volume, &pitch))
            return 1;
        printf("\"sounds\":[{\"seq\":0,"
               "\"sound\":\"minecraft:entity.zombie_villager.cure\","
               "\"category\":\"hostile\","
               "\"x_bits\":\"%016" PRIx64 "\","
               "\"y_bits\":\"%016" PRIx64 "\","
               "\"z_bits\":\"%016" PRIx64 "\","
               "\"volume_bits\":\"%08" PRIx32 "\","
               "\"pitch_bits\":\"%08" PRIx32 "\"}],"
               "\"converting\":false,\"conversion_time\":-1,"
               "\"weakness_duration\":0,\"strength_duration\":0,"
               "\"strength_amplifier\":-1,\"entity_seed48\":%" PRIu64
               ",\"events\":[]",
               double_bits(x - world_x), double_bits(y - 220.0),
               double_bits(z - world_z), float_bits(volume),
               float_bits(pitch),
               mobs->entity_server_random[slot].random.seed);
    } else if (!strcmp(argv[1], "interact")) {
        IsrInv inventory;
        isr_init(&inventory);
        if (preset >= 0) {
            IsrInv setup;
            isr_init(&setup);
            isr_set_stack(&setup, 0, ic_mk(322, 1, 0));
            if (!gm_mobs_cure_zombie_villager(
                    mobs, eid, &setup, 0, 1))
                return 1;
            mobs->zombie_villager_conversion_time[slot] = preset;
            PtMobEffect *strength = effect(mobs, slot, 5);
            if (!strength) return 1;
            strength->duration = preset;
            if (!gm_mobs_set_entity_random_state(
                    mobs, eid, entity_seed, 0, 0.0D))
                return 1;
            mobs->event_head = mobs->event_count = 0;
        }
        isr_set_stack(&inventory, 0,
            count > 0 ? ic_mk(item, count, meta) : ic_empty());
        int handled = gm_mobs_cure_zombie_villager(
            mobs, eid, &inventory, 0, creative);
        ICStack held = isr_get_stack(&inventory, 0);
        int conversion = -1;
        int converting = gm_mobs_zombie_villager_conversion_state(
            mobs, eid, &conversion);
        printf("\"handled\":%s,\"item\":%d,\"meta\":%d,"
               "\"count\":%d,",
               handled ? "true" : "false",
               held.count > 0 ? held.item : 0,
               held.count > 0 ? held.meta : 0,
               held.count > 0 ? held.count : 0);
        PtMobEffect *weak = effect(mobs, slot, 18);
        PtMobEffect *strength = effect(mobs, slot, 5);
        printf("\"converting\":%s,\"conversion_time\":%d,"
               "\"weakness_duration\":%d,\"strength_duration\":%d,"
               "\"strength_amplifier\":%d,\"entity_seed48\":%" PRIu64
               ",\"events\":",
               converting ? "true" : "false", converting ? conversion : -1,
               weak ? weak->duration : 0,
               strength ? strength->duration : 0,
               strength ? strength->amplifier : -1,
               mobs->entity_random[slot].random.seed);
        print_events(mobs);
    } else if (!strcmp(argv[1], "progress")) {
        if (strcmp(argv[16], "bed") && strcmp(argv[16], "iron_bars"))
            return 2;
        int block = !strcmp(argv[16], "bed") ? 26 : 101;
        int placed = 0;
        for (int x = world_x - 4; x < world_x + 4
                && placed < accelerators; ++x)
            for (int y = 216; y < 224 && placed < accelerators; ++y)
                for (int z = world_z - 4; z < world_z + 4
                        && placed < accelerators; ++z) {
                    gm_world_set_block(world, x, y, z, block);
                    ++placed;
                }
        int progress = gm_mobs_zombie_villager_conversion_progress(
            mobs, world, eid);
        printf("\"accelerators\":%d,\"block\":\"%s\","
               "\"progress\":%d,\"converting\":false,"
               "\"conversion_time\":-1,\"weakness_duration\":0,"
               "\"strength_duration\":0,\"strength_amplifier\":-1,"
               "\"entity_seed48\":%" PRIu64 ",\"events\":[]",
               accelerators, argv[16], progress,
               mobs->entity_random[slot].random.seed);
    } else {
        IsrInv setup;
        isr_init(&setup);
        isr_set_stack(&setup, 0, ic_mk(322, 1, 0));
        if (!weakness)
            (void)gm_mobs_apply_potion_effect(mobs, slot, 18, 0, 100);
        if (!gm_mobs_cure_zombie_villager(mobs, eid, &setup, 0, 1))
            return 1;
        mobs->zombie_villager_conversion_time[slot] = 1;
        PtMobEffect *strength = effect(mobs, slot, 5);
        if (!strength) return 1;
        strength->duration = 1;
        if (!gm_mobs_set_entity_random_state(
                mobs, eid, entity_seed, 0, 0.0D))
            return 1;
        int cursor = eid + 1;
        int new_eid = gm_mobs_zombie_villager_finish_conversion(
            mobs, eid, &cursor);
        EwStore *s = store(mobs);
        if (new_eid != eid + 1 || s->id[slot] != new_eid
                || s->type[slot] != EW_TYPE_VILLAGER)
            return 1;
        PtMobEffect *nausea = effect(mobs, slot, 9);
        int event_x, event_y, event_z;
        if (!gm_mobs_take_zombie_villager_world_event(
                mobs, &event_x, &event_y, &event_z))
            return 1;
        printf("\"old_dead\":true,\"new_eid\":%d,"
               "\"new_type\":\"villager\",\"profession\":%d,"
               "\"growing_age\":%d,\"no_ai\":%s,"
               "\"health_bits\":\"%08" PRIx32 "\","
               "\"position_bits\":[\"%016" PRIx64 "\","
               "\"%016" PRIx64 "\",\"%016" PRIx64 "\"],"
               "\"motion_bits\":[\"%016" PRIx64 "\","
               "\"%016" PRIx64 "\",\"%016" PRIx64 "\"],"
               "\"nausea_duration\":%d,\"nausea_amplifier\":%d,"
               "\"world_events\":[{\"seq\":0,\"id\":1027,"
               "\"x\":%d,\"y\":%d,\"z\":%d,\"data\":0}],"
               "\"converting\":true,\"conversion_time\":1,"
               "\"weakness_duration\":0,\"strength_duration\":1,"
               "\"strength_amplifier\":0,\"entity_seed48\":%" PRIu64
               ",\"events\":",
               new_eid, mobs->villager_profession[slot],
               mobs->growing_age[slot],
               mobs->controlled_no_ai[slot] ? "true" : "false",
               float_bits(s->health[slot]),
               double_bits(s->x[slot] - (world_x + 0.5)),
               double_bits(s->y[slot] - 220.0),
               double_bits(s->z[slot] - (world_z + 0.5)),
               double_bits(s->vx[slot]), double_bits(s->vy[slot]),
               double_bits(s->vz[slot]),
               nausea ? nausea->duration : 0,
               nausea ? nausea->amplifier : -1,
               event_x, event_y, event_z, entity_seed);
        print_events(mobs);
    }
    puts("}");
    gm_world_destroy(world);
    free(mobs);
    return 0;
}
