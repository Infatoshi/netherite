#include "game/mob_live.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int main(int argc, char **argv) {
    GmMobLive mobs;
    GmSnowmanShot shot;
    GmMobEvent event;
    uint64_t owner_seed, entity_seed, uuid_seed;
    int next_id;
    double owner_x, owner_y, owner_z, target_x, target_y, target_z;
    if (argc != 11) return 2;
    owner_x = strtod(argv[1], NULL);
    owner_y = strtod(argv[2], NULL);
    owner_z = strtod(argv[3], NULL);
    target_x = strtod(argv[4], NULL);
    target_y = strtod(argv[5], NULL);
    target_z = strtod(argv[6], NULL);
    owner_seed = strtoull(argv[7], NULL, 10);
    entity_seed = strtoull(argv[8], NULL, 10);
    uuid_seed = strtoull(argv[9], NULL, 10);
    next_id = atoi(argv[10]);
    if (owner_seed >= (UINT64_C(1) << 48)
            || entity_seed >= (UINT64_C(1) << 48)
            || uuid_seed >= (UINT64_C(1) << 48)
            || next_id <= 0 || next_id == INT32_MAX)
        return 2;
    gm_mobs_init(&mobs, 0);
    if (gm_mobs_spawn_exact(
                &mobs, EW_TYPE_SNOWMAN, 100,
                owner_x, owner_y, owner_z,
                0.0, 0.0, 0.0, 0.0F, 4.0F, 0, 0, 0, 0) <= 0
            || gm_mobs_spawn_exact(
                &mobs, EW_TYPE_ZOMBIE, 101,
                target_x, target_y, target_z,
                0.0, 0.0, 0.0, 0.0F, 20.0F, 0, 0, 0, 0) <= 0
            || !gm_mobs_set_entity_random_state(
                &mobs, 100, owner_seed, 0, 0.0)
            || !gm_mobs_snowman_attack_exact(
                &mobs, 100, 101, &entity_seed, &uuid_seed,
                &next_id, &shot)
            || gm_mobs_event_count(&mobs) != 1
            || !gm_mobs_event_get(&mobs, 0, &event))
        return 1;
    int owner_slot = gm_mobs_find_slot_by_eid(&mobs, 100);
    printf("{\"ok\":true,\"eid\":%d,\"owner_eid\":%d"
           ",\"position_bits\":[\"%016" PRIx64 "\",\"%016" PRIx64
           "\",\"%016" PRIx64 "\"]"
           ",\"motion_bits\":[\"%016" PRIx64 "\",\"%016" PRIx64
           "\",\"%016" PRIx64 "\"]"
           ",\"rotation_bits\":[\"%08" PRIx32 "\",\"%08" PRIx32
           "\",\"%08" PRIx32 "\",\"%08" PRIx32 "\"]"
           ",\"seed48\":%" PRIu64
           ",\"have_gaussian\":%s"
           ",\"next_gaussian_bits\":\"%016" PRIx64 "\""
           ",\"uuid_most\":%" PRId64 ",\"uuid_least\":%" PRId64
           ",\"owner_seed48\":%" PRIu64
           ",\"entity_seed48\":%" PRIu64
           ",\"server_uuid_seed48\":%" PRIu64
           ",\"next_entity_id\":%d"
           ",\"sound\":%d,\"sound_volume_bits\":\"%08" PRIx32 "\""
           ",\"sound_pitch_bits\":\"%08" PRIx32 "\"}\n",
           shot.eid, shot.owner_eid,
           double_bits(shot.x), double_bits(shot.y), double_bits(shot.z),
           double_bits(shot.vx), double_bits(shot.vy), double_bits(shot.vz),
           float_bits(shot.yaw), float_bits(shot.pitch),
           float_bits(shot.yaw), float_bits(shot.pitch),
           shot.random.random.seed,
           shot.random.have_next_next_gaussian ? "true" : "false",
           double_bits(shot.random.next_next_gaussian),
           shot.uuid_most, shot.uuid_least,
           mobs.entity_random[owner_slot].random.seed,
           entity_seed, uuid_seed, next_id,
           event.data, float_bits(event.volume), float_bits(event.pitch));
    return 0;
}
