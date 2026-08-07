#include "game/runtime.h"

#include <inttypes.h>
#include <limits.h>
#include <math.h>
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

static int parse_u48(const char *text, uint64_t *out) {
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 0);
    if (!text[0] || !end || *end || value >= (UINT64_C(1) << 48))
        return 0;
    *out = (uint64_t)value;
    return 1;
}

static int parse_double(const char *text, double *out) {
    char *end = NULL;
    double value = strtod(text, &end);
    if (!text[0] || !end || *end || !isfinite(value)) return 0;
    *out = value;
    return 1;
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

static const EwStore *store(const GmMobLive *m) {
    return m->current ? &m->b : &m->a;
}

static int slot_for(const GmMobLive *m, int eid) {
    const EwStore *s = store(m);
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot)
        if (s->alive[slot] && s->id[slot] == eid) return slot;
    return -1;
}

static void print_vec3(double x, double y, double z) {
    printf("[\"%016" PRIx64 "\",\"%016" PRIx64
           "\",\"%016" PRIx64 "\"]",
        double_bits(x), double_bits(y), double_bits(z));
}

static void print_sheep(const GmMobLive *m, int slot) {
    const EwStore *s = store(m);
    printf("{\"eid\":%d,\"growing_age\":%d,\"in_love\":%d,"
           "\"fleece\":%d,\"sheared\":%s,\"ticks_existed\":%d,"
           "\"entity_age\":%d,\"living_sound_time\":%d,"
           "\"task_tick_count\":%u,\"on_ground\":%s,"
           "\"fall_distance_bits\":\"00000000\","
           "\"yaw_bits\":\"%08" PRIx32 "\","
           "\"pitch_bits\":\"00000000\",\"position_bits\":",
        s->id[slot], m->growing_age[slot], m->sheep_in_love[slot],
        m->sheep_data[slot] & 15,
        (m->sheep_data[slot] & 16) ? "true" : "false",
        m->entity_ticks_existed[slot], m->entity_age[slot],
        m->entity_living_sound_time[slot], m->sheep_ai_tick_count[slot],
        s->on_ground[slot] ? "true" : "false", float_bits(s->yaw[slot]));
    print_vec3(s->x[slot], s->y[slot], s->z[slot]);
    printf(",\"motion_bits\":");
    print_vec3(s->vx[slot], s->vy[slot], s->vz[slot]);
    printf(",\"last_tick_position_bits\":");
    print_vec3(
        m->entity_last_tick_x[slot], m->entity_last_tick_y[slot],
        m->entity_last_tick_z[slot]);
    printf(",\"previous_position_bits\":");
    print_vec3(
        m->entity_prev_x[slot], m->entity_prev_y[slot],
        m->entity_prev_z[slot]);
    printf(",\"entity_seed48\":%" PRIu64
           ",\"entity_have_next_gaussian\":%s,"
           "\"entity_next_gaussian_bits\":\"%016" PRIx64 "\"}",
        (uint64_t)m->entity_random[slot].random.seed,
        m->entity_random[slot].have_next_next_gaussian ? "true" : "false",
        double_bits(m->entity_random[slot].next_next_gaussian));
}

static int animal_type(const char *species) {
    if (!strcmp(species, "sheep")) return EW_TYPE_SHEEP;
    if (!strcmp(species, "cow")) return EW_TYPE_COW;
    if (!strcmp(species, "pig")) return EW_TYPE_PIG;
    if (!strcmp(species, "chicken")) return EW_TYPE_CHICKEN;
    return EW_TYPE_NONE;
}

static void print_animal(
        const GmMobLive *m, int slot, const char *species) {
    const EwStore *s = store(m);
    printf("{\"eid\":%d,\"species\":\"%s\","
           "\"growing_age\":%d,\"in_love\":%d,"
           "\"ticks_existed\":%d,\"entity_age\":%d,"
           "\"living_sound_time\":%d,\"task_tick_count\":%u,"
           "\"on_ground\":%s,\"fall_distance_bits\":\"00000000\","
           "\"yaw_bits\":\"%08" PRIx32 "\","
           "\"pitch_bits\":\"00000000\",\"position_bits\":",
        s->id[slot], species, m->growing_age[slot],
        m->sheep_in_love[slot], m->entity_ticks_existed[slot],
        m->entity_age[slot], m->entity_living_sound_time[slot],
        m->sheep_ai_tick_count[slot],
        s->on_ground[slot] ? "true" : "false", float_bits(s->yaw[slot]));
    print_vec3(s->x[slot], s->y[slot], s->z[slot]);
    printf(",\"motion_bits\":");
    print_vec3(s->vx[slot], s->vy[slot], s->vz[slot]);
    printf(",\"last_tick_position_bits\":");
    print_vec3(
        m->entity_last_tick_x[slot], m->entity_last_tick_y[slot],
        m->entity_last_tick_z[slot]);
    printf(",\"previous_position_bits\":");
    print_vec3(
        m->entity_prev_x[slot], m->entity_prev_y[slot],
        m->entity_prev_z[slot]);
    printf(",\"entity_seed48\":%" PRIu64
           ",\"entity_have_next_gaussian\":%s,"
           "\"entity_next_gaussian_bits\":\"%016" PRIx64 "\"",
        (uint64_t)m->entity_random[slot].random.seed,
        m->entity_random[slot].have_next_next_gaussian ? "true" : "false",
        double_bits(m->entity_random[slot].next_next_gaussian));
    if (s->type[slot] == EW_TYPE_SHEEP)
        printf(",\"fleece\":%d,\"sheared\":%s",
            m->sheep_data[slot] & 15,
            (m->sheep_data[slot] & 16) ? "true" : "false");
    if (s->type[slot] == EW_TYPE_CHICKEN)
        printf(",\"time_until_next_egg\":%d,"
               "\"wing_rotation_bits\":\"%08" PRIx32 "\","
               "\"dest_pos_bits\":\"%08" PRIx32 "\","
               "\"o_flap_speed_bits\":\"%08" PRIx32 "\","
               "\"o_flap_bits\":\"%08" PRIx32 "\","
               "\"wing_rot_delta_bits\":\"%08" PRIx32 "\","
               "\"chicken_jockey\":%s",
            m->chicken_time_until_next_egg[slot],
            float_bits(m->chicken_wing_rotation[slot]),
            float_bits(m->chicken_dest_pos[slot]),
            float_bits(m->chicken_old_flap_speed[slot]),
            float_bits(m->chicken_old_flap[slot]),
            float_bits(m->chicken_wing_rot_delta[slot]),
            m->chicken_jockey[slot] ? "true" : "false");
    putchar('}');
}

static int xp_slot_for(const GmMobLive *m, int eid) {
    for (int slot = 0; slot < GM_XP_ORBS; ++slot)
        if (!m->xp_orbs[slot].dead && m->xp_orbs[slot].eid == eid)
            return slot;
    return -1;
}

static void print_xp(const GmMobLive *m, int slot,
                     double spawn_x, double spawn_y, double spawn_z) {
    const McOrb *orb = &m->xp_orbs[slot];
    printf("{\"eid\":%d,\"value\":%d,\"ticks_existed\":1,"
           "\"xp_color\":%d,\"xp_orb_age\":%d,"
           "\"pickup_delay\":%d,\"health\":%d,"
           "\"on_ground\":%s,\"yaw_bits\":\"%08" PRIx32
           "\",\"position_bits\":",
        orb->eid, orb->xpValue, orb->xpColor, orb->xpOrbAge,
        orb->delayBeforeCanPickup, orb->health,
        orb->onGround ? "true" : "false", float_bits(orb->yaw));
    print_vec3(orb->posX, orb->posY, orb->posZ);
    printf(",\"motion_bits\":");
    print_vec3(orb->motionX, orb->motionY, orb->motionZ);
    printf(",\"last_tick_position_bits\":");
    print_vec3(spawn_x, spawn_y, spawn_z);
    printf(",\"previous_position_bits\":");
    print_vec3(spawn_x, spawn_y, spawn_z);
    putchar('}');
}

static void print_item(const GmLiveEnt *item) {
    printf("{\"eid\":%d,\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
           "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,"
           "\"item\":%d,\"count\":%d,\"meta\":%d,"
           "\"age\":%d,\"pickup_delay\":%d,\"health\":%d,"
           "\"lifespan\":%d,\"yaw_bits\":\"%08" PRIx32 "\","
           "\"hover_start_bits\":\"%08" PRIx32 "\","
           "\"on_ground\":%s,\"is_dead\":false}",
        item->eid, item->x, item->y, item->z,
        item->mx, item->my, item->mz,
        item->item, item->count, item->meta, item->age,
        item->pickup_delay, item->health, item->lifespan,
        float_bits(item->yaw), float_bits(item->hover_start),
        item->on_ground ? "true" : "false");
}

static int print_egg_items(const GmLiveSim *items) {
    int count = 0;
    for (int slot = 0; slot < GM_LIVE_MAX; ++slot) {
        const GmLiveEnt *item = &items->ents[slot];
        if (!item->active || item->type != 0 || item->item != 344)
            continue;
        if (count++) putchar(',');
        print_item(item);
    }
    return count;
}

static void print_animal_events(const GmMobLive *m) {
    int count = 0;
    for (int index = 0; index < gm_mobs_event_count(m); ++index) {
        GmMobEvent event;
        if (!gm_mobs_event_get(m, index, &event)
                || event.kind != GM_MOB_EVENT_SOUND
                || event.data != GM_MOB_SOUND_CHICKEN_EGG)
            continue;
        if (count++) putchar(',');
        printf("{\"kind\":\"sound\",\"eid\":%d,"
               "\"sound\":\"minecraft:entity.chicken.egg\","
               "\"category\":\"neutral\","
               "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
               "\"volume_bits\":\"%08" PRIx32 "\","
               "\"pitch_bits\":\"%08" PRIx32 "\"}",
            event.eid, event.x, event.y, event.z,
            float_bits(event.volume), float_bits(event.pitch));
    }
}

int main(int argc, char **argv) {
    int generic = argc == 35;
    int shift = generic ? 1 : 0;
    const char *species = generic ? argv[1] : "sheep";
    int type = animal_type(species);
    int first_color, second_color, next_entity_id, do_mob_loot, grounded;
    int pair_count, third_color, fourth_color;
    int preexisting_xp, preexisting_xp_expires;
    int preexisting_living_expires;
    int first_egg_timer = 0, second_egg_timer = 0;
    int third_egg_timer = 0, fourth_egg_timer = 0;
    int child_egg_timer = 0, second_child_egg_timer = 0;
    double distance, z_offset, x, y, z, pair_gap;
    double second_distance, second_z_offset;
    uint64_t world_seed48, first_seed48, second_seed48;
    uint64_t child_seed48, math_seed48;
    uint64_t third_seed48, fourth_seed48, second_child_seed48;
    if ((argc != 28 && argc != 35) || type == EW_TYPE_NONE
            || !parse_int(argv[1 + shift], &first_color)
            || !parse_int(argv[2 + shift], &second_color)
            || !parse_double(argv[3 + shift], &distance)
            || !parse_double(argv[4 + shift], &z_offset)
            || !parse_double(argv[5 + shift], &x)
            || !parse_double(argv[6 + shift], &y)
            || !parse_double(argv[7 + shift], &z)
            || !parse_u48(argv[8 + shift], &world_seed48)
            || !parse_u48(argv[9 + shift], &first_seed48)
            || !parse_u48(argv[10 + shift], &second_seed48)
            || !parse_u48(argv[11 + shift], &child_seed48)
            || !parse_u48(argv[12 + shift], &math_seed48)
            || !parse_int(argv[13 + shift], &next_entity_id)
            || !parse_int(argv[14 + shift], &do_mob_loot)
            || !parse_int(argv[15 + shift], &grounded)
            || !parse_int(argv[16 + shift], &pair_count)
            || !parse_double(argv[17 + shift], &pair_gap)
            || !parse_int(argv[18 + shift], &third_color)
            || !parse_int(argv[19 + shift], &fourth_color)
            || !parse_double(argv[20 + shift], &second_distance)
            || !parse_double(argv[21 + shift], &second_z_offset)
            || !parse_u48(argv[22 + shift], &third_seed48)
            || !parse_u48(argv[23 + shift], &fourth_seed48)
            || !parse_u48(argv[24 + shift], &second_child_seed48)
            || !parse_int(argv[25 + shift], &preexisting_xp)
            || !parse_int(argv[26 + shift], &preexisting_xp_expires)
            || !parse_int(argv[27 + shift], &preexisting_living_expires)
            || (generic && (!parse_int(argv[29], &first_egg_timer)
                || !parse_int(argv[30], &second_egg_timer)
                || !parse_int(argv[31], &third_egg_timer)
                || !parse_int(argv[32], &fourth_egg_timer)
                || !parse_int(argv[33], &child_egg_timer)
                || !parse_int(argv[34], &second_child_egg_timer)))
            || first_color < 0 || first_color > 15
            || second_color < 0 || second_color > 15
            || distance < 0.0
            || distance * distance + z_offset * z_offset >= 9.0
            || pair_count < 1 || pair_count > 2
            || pair_gap < 9.0 || pair_gap > 24.0
            || third_color < 0 || third_color > 15
            || fourth_color < 0 || fourth_color > 15
            || second_distance < 0.0
            || second_distance * second_distance
                + second_z_offset * second_z_offset >= 9.0
            || y < 32.0 || y > 250.0
            || next_entity_id <= 0 || next_entity_id >= INT_MAX - 8
            || (do_mob_loot != 0 && do_mob_loot != 1)
            || (grounded != 0 && grounded != 1)
            || (generic && type == EW_TYPE_CHICKEN
                && (first_egg_timer <= 0 || second_egg_timer <= 0
                    || (pair_count == 2
                        && (third_egg_timer <= 0 || fourth_egg_timer <= 0))
                    || child_egg_timer <= 0
                    || (pair_count == 2 && second_child_egg_timer <= 0)
                    || (pair_count == 2
                        && (first_egg_timer <= 1 || second_egg_timer <= 1
                            || third_egg_timer <= 1
                            || fourth_egg_timer <= 1))))) {
        fprintf(stderr, "usage: %s [SPECIES] FIRST_COLOR SECOND_COLOR DISTANCE "
            "Z_OFFSET X Y Z "
            "WORLD_SEED48 FIRST_SEED48 SECOND_SEED48 CHILD_SEED48 "
            "MATH_SEED48 NEXT_ENTITY_ID DO_MOB_LOOT GROUNDED "
            "PAIR_COUNT PAIR_GAP THIRD_COLOR FOURTH_COLOR "
            "SECOND_DISTANCE SECOND_Z_OFFSET THIRD_SEED48 FOURTH_SEED48 "
            "SECOND_CHILD_SEED48 PREEXISTING_XP PREEXISTING_XP_EXPIRES "
            "PREEXISTING_LIVING_EXPIRES [FIRST_EGG SECOND_EGG "
            "THIRD_EGG FOURTH_EGG CHILD_EGG SECOND_CHILD_EGG]\n",
            argv[0]);
        return 2;
    }
    if ((preexisting_xp != 0 && preexisting_xp != 1)
            || (preexisting_xp_expires != 0
                && preexisting_xp_expires != 1)
            || (preexisting_xp_expires && !preexisting_xp))
        return 2;
    if (preexisting_living_expires != 0
            && preexisting_living_expires != 1)
        return 2;

    GmConfig config;
    GmRuntime runtime;
    GmAction idle;
    char error[256];
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "runtime init: %s\n", error);
        return 1;
    }
    gm_runtime_set_pose(&runtime, x, 64.0, z, 0.0F, 0.0F);
    if (grounded) {
        int platform_y = mc_floor(y) - 1;
        double max_x = pair_count == 2
            ? fmax(x + distance, x + pair_gap + second_distance)
            : x + distance;
        double min_z = pair_count == 2
            ? fmin(fmin(z, z + z_offset), z + second_z_offset)
            : fmin(z, z + z_offset);
        double max_z = pair_count == 2
            ? fmax(fmax(z, z + z_offset), z + second_z_offset)
            : fmax(z, z + z_offset);
        for (int block_x = mc_floor(x) - 2;
                block_x <= mc_floor(max_x) + 2; ++block_x)
            for (int block_z = mc_floor(min_z) - 2;
                    block_z <= mc_floor(max_z) + 2;
                    ++block_z)
                gm_world_set_block(runtime.world,
                    block_x, platform_y, block_z, 1);
    }
    gm_mobs_init(&runtime.mobs, 0);
    runtime.mobs.active_dimension = runtime.dimension;
    int parent_base_id = next_entity_id + preexisting_xp
        + preexisting_living_expires;
    if (preexisting_xp && !gm_mobs_spawn_xp_exact(
            &runtime.mobs, x - 4.0, y, z, 0.0, 0.0, 0.0,
            5, next_entity_id, preexisting_xp_expires ? 5999 : 10,
            5, 20, 0)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    int old_living_slot = -1;
    if (preexisting_living_expires) {
        int old_living_eid = next_entity_id + preexisting_xp;
        old_living_slot = gm_mobs_spawn_exact(
            &runtime.mobs, EW_TYPE_COW, old_living_eid,
            x - 16.0, y, z, 0.0, 0.0, 0.0,
            0.0F, 0.0F, 1, 0, 19, 0);
        if (old_living_slot < 0) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
    }
    runtime.mobs.next_id = parent_base_id;
    int first = gm_mobs_spawn(
        &runtime.mobs, type, x, y, z);
    int second = gm_mobs_spawn(
        &runtime.mobs, type, x + distance, y, z + z_offset);
    int third = -1, fourth = -1;
    if (pair_count == 2) {
        third = gm_mobs_spawn(
            &runtime.mobs, type, x + pair_gap, y, z);
        fourth = gm_mobs_spawn(
            &runtime.mobs, type,
            x + pair_gap + second_distance, y, z + second_z_offset);
    }
    int first_eid = parent_base_id;
    int second_eid = parent_base_id + 1;
    int third_eid = parent_base_id + 2;
    int fourth_eid = parent_base_id + 3;
    runtime.next_entity_id = parent_base_id + pair_count * 2;
    runtime.world_random_seed48 = world_seed48;
    runtime.math_random_seed48 = math_seed48;
    runtime.mobs_enabled = 1;
    runtime.controlled_mobs_enabled = 0;
    runtime.do_mob_loot = do_mob_loot;
    runtime.mobs.tick = 1;
    if (first < 0 || second < 0
            || (pair_count == 2 && (third < 0 || fourth < 0))
            || (type == EW_TYPE_SHEEP
                && (!gm_mobs_set_sheep_state(
                        &runtime.mobs, first_eid, first_color, 0)
                    || !gm_mobs_set_sheep_state(
                        &runtime.mobs, second_eid, second_color, 0)))
            || !gm_mobs_set_animal_breeding_state(
                &runtime.mobs, first_eid, 600, 0, 0, 0)
            || !gm_mobs_set_animal_breeding_state(
                &runtime.mobs, second_eid, 600, 0, 0, 0)
            || !gm_mobs_set_entity_random_state(
                &runtime.mobs, first_eid, first_seed48, 0, 0.0)
            || !gm_mobs_set_entity_random_state(
                &runtime.mobs, second_eid, second_seed48, 0, 0.0)
            || (type == EW_TYPE_CHICKEN
                && (!gm_mobs_set_chicken_state(
                        &runtime.mobs, first_eid, first_egg_timer,
                        0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0)
                    || !gm_mobs_set_chicken_state(
                        &runtime.mobs, second_eid, second_egg_timer,
                        0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0)))
            || !gm_mobs_set_next_animal_child_state(
                &runtime.mobs, child_seed48, 0, 0.0, child_egg_timer)
            || (pair_count == 2
                && ((type == EW_TYPE_SHEEP
                        && (!gm_mobs_set_sheep_state(
                                &runtime.mobs, third_eid, third_color, 0)
                            || !gm_mobs_set_sheep_state(
                                &runtime.mobs, fourth_eid, fourth_color, 0)))
                    || !gm_mobs_set_animal_breeding_state(
                        &runtime.mobs, third_eid, 600, 0, 0, 0)
                    || !gm_mobs_set_animal_breeding_state(
                        &runtime.mobs, fourth_eid, 600, 0, 0, 0)
                    || !gm_mobs_set_entity_random_state(
                        &runtime.mobs, third_eid, third_seed48, 0, 0.0)
                    || !gm_mobs_set_entity_random_state(
                        &runtime.mobs, fourth_eid, fourth_seed48, 0, 0.0)
                    || (type == EW_TYPE_CHICKEN
                        && (!gm_mobs_set_chicken_state(
                                &runtime.mobs, third_eid, third_egg_timer,
                                0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0)
                            || !gm_mobs_set_chicken_state(
                                &runtime.mobs, fourth_eid, fourth_egg_timer,
                                0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0)))
                    || !gm_mobs_queue_animal_child_state(
                        &runtime.mobs, second_child_seed48, 0, 0.0,
                        second_child_egg_timer)))) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    /* Entity.setLocationAndAngles does not make the Java fixture grounded. */
    runtime.mobs.a.on_ground[first] = (unsigned char)grounded;
    runtime.mobs.a.on_ground[second] = (unsigned char)grounded;
    runtime.mobs.b.on_ground[first] = (unsigned char)grounded;
    runtime.mobs.b.on_ground[second] = (unsigned char)grounded;
    if (pair_count == 2) {
        runtime.mobs.a.on_ground[third] = (unsigned char)grounded;
        runtime.mobs.a.on_ground[fourth] = (unsigned char)grounded;
        runtime.mobs.b.on_ground[third] = (unsigned char)grounded;
        runtime.mobs.b.on_ground[fourth] = (unsigned char)grounded;
    }
    runtime.mobs.sheep_mate_active[first] = 1;
    runtime.mobs.sheep_mate_slot[first] = second;
    runtime.mobs.sheep_mate_delay[first] = 59;
    if (pair_count == 2) {
        runtime.mobs.sheep_mate_active[third] = 1;
        runtime.mobs.sheep_mate_slot[third] = fourth;
        runtime.mobs.sheep_mate_delay[third] = 59;
    }
    gm_runtime_tick(&runtime, idle);

    int child_eid = parent_base_id + pair_count * 2;
    int first_xp_eid = do_mob_loot ? child_eid + 1 : -1;
    int second_child_eid = pair_count == 2
        ? child_eid + 1 + do_mob_loot : -1;
    int second_xp_eid = pair_count == 2 && do_mob_loot
        ? second_child_eid + 1 : -1;
    int child = slot_for(&runtime.mobs, child_eid);
    int second_child = pair_count == 2
        ? slot_for(&runtime.mobs, second_child_eid) : -1;
    int first_xp = do_mob_loot
        ? xp_slot_for(&runtime.mobs, first_xp_eid) : -1;
    int second_xp = pair_count == 2 && do_mob_loot
        ? xp_slot_for(&runtime.mobs, second_xp_eid) : -1;
    int old_xp = preexisting_xp && !preexisting_xp_expires
        ? xp_slot_for(&runtime.mobs, next_entity_id) : -1;
    if (child < 0 || (pair_count == 2 && second_child < 0)
            || (preexisting_living_expires
                && child != old_living_slot)
            || (preexisting_xp && !preexisting_xp_expires && old_xp < 0)
            || (do_mob_loot && first_xp < 0)
            || (pair_count == 2 && do_mob_loot && second_xp < 0)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    printf("{\"ok\":true,\"delay_hook_count\":%d,"
           "\"birth_pin_count\":%d,\"child_pin_count\":%d,"
           "\"update_order\":[",
        pair_count, pair_count, pair_count);
    for (int index = 0;
            index < runtime.mobs.tick_update_order_count; ++index) {
        if (index) putchar(',');
        printf("%d", runtime.mobs.tick_update_order[index]);
    }
    int update_count = runtime.mobs.tick_update_order_count;
    if (generic) for (int slot = 0; slot < GM_LIVE_MAX; ++slot) {
        const GmLiveEnt *item = &runtime.entities.ents[slot];
        if (!item->active || item->type != 0 || item->item != 344) continue;
        if (update_count++) putchar(',');
        printf("%d", item->eid);
    }
    printf("],\"entity_order\":[");
    int entity_count = gm_mobs_loaded_order_count(&runtime.mobs);
    for (int index = 0; index < entity_count; ++index) {
        int eid;
        if (!gm_mobs_loaded_order_get(
                &runtime.mobs, index, &eid, NULL)) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
        if (index) putchar(',');
        printf("%d", eid);
    }
    if (generic) for (int slot = 0; slot < GM_LIVE_MAX; ++slot) {
        const GmLiveEnt *item = &runtime.entities.ents[slot];
        if (!item->active || item->type != 0 || item->item != 344) continue;
        if (entity_count++) putchar(',');
        printf("%d", item->eid);
    }
    if (generic)
        printf("],\"species\":\"%s\",\"animals\":[", species);
    else
        printf("],\"sheep\":[");
    if (generic)
        print_animal(&runtime.mobs, first, species);
    else
        print_sheep(&runtime.mobs, first);
    putchar(',');
    if (generic)
        print_animal(&runtime.mobs, second, species);
    else
        print_sheep(&runtime.mobs, second);
    if (pair_count == 2) {
        putchar(',');
        if (generic)
            print_animal(&runtime.mobs, third, species);
        else
            print_sheep(&runtime.mobs, third);
        putchar(',');
        if (generic)
            print_animal(&runtime.mobs, fourth, species);
        else
            print_sheep(&runtime.mobs, fourth);
    }
    putchar(',');
    if (generic)
        print_animal(&runtime.mobs, child, species);
    else
        print_sheep(&runtime.mobs, child);
    if (pair_count == 2) {
        putchar(',');
        if (generic)
            print_animal(&runtime.mobs, second_child, species);
        else
            print_sheep(&runtime.mobs, second_child);
    }
    printf("],\"xp_orbs\":[");
    if (preexisting_xp && !preexisting_xp_expires)
        print_xp(&runtime.mobs, old_xp, x - 4.0, y, z);
    if (do_mob_loot) {
        if (preexisting_xp && !preexisting_xp_expires) putchar(',');
        print_xp(&runtime.mobs, first_xp, x, y, z);
        if (pair_count == 2) {
            putchar(',');
            print_xp(&runtime.mobs, second_xp, x + pair_gap, y, z);
        }
    }
    if (generic) {
        printf("],\"items\":[");
        (void)print_egg_items(&runtime.entities);
        printf("],\"events\":[");
        print_animal_events(&runtime.mobs);
        printf("],\"particles\":[");
    } else {
        printf("],\"particles\":[");
    }
    int particle_count = 0;
    uint64_t sequence = 0;
    for (int batch_index = 0;
            batch_index < gm_mobs_particle_batch_count(&runtime.mobs);
            ++batch_index) {
        GmMobParticleBatch batch;
        if (!gm_mobs_particle_batch_get(
                &runtime.mobs, batch_index, &batch)) {
            gm_runtime_destroy(&runtime);
            return 1;
        }
        for (int index = 0; index < batch.count; ++index) {
            const GmTerminalParticle *particle = &batch.particles[index];
            if (particle_count++) putchar(',');
            printf("{\"seq\":%" PRIu64 ",\"id\":%d,"
                   "\"ignore_range\":false,\"parameters\":[],"
                   "\"payload_bits\":",
                sequence++, batch.particle_id);
            printf("[\"%016" PRIx64 "\",\"%016" PRIx64
                   "\",\"%016" PRIx64 "\",\"%016" PRIx64
                   "\",\"%016" PRIx64 "\",\"%016" PRIx64 "\"]}",
                double_bits(particle->x), double_bits(particle->y),
                double_bits(particle->z), double_bits(particle->vx),
                double_bits(particle->vy), double_bits(particle->vz));
        }
    }
    printf("],\"world_seed48\":%" PRIu64
           ",\"math_seed48\":%" PRIu64
           ",\"next_entity_id\":%d}\n",
        runtime.world_random_seed48, runtime.math_random_seed48,
        runtime.next_entity_id);
    gm_runtime_destroy(&runtime);
    return 0;
}
