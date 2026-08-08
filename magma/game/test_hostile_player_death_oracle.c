#include "game/runtime.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int type_for_name(const char *name) {
    if (!strcmp(name, "sheep")) return EW_TYPE_SHEEP;
    if (!strcmp(name, "pig")) return EW_TYPE_PIG;
    if (!strcmp(name, "cow")) return EW_TYPE_COW;
    if (!strcmp(name, "chicken")) return EW_TYPE_CHICKEN;
    if (!strcmp(name, "zombie")) return EW_TYPE_ZOMBIE;
    if (!strcmp(name, "zombie_villager")) return EW_TYPE_ZOMBIE_VILLAGER;
    if (!strcmp(name, "pigman")) return EW_TYPE_PIGMAN;
    if (!strcmp(name, "skeleton")) return EW_TYPE_SKELETON;
    if (!strcmp(name, "wither_skeleton")) return EW_TYPE_WITHER_SKELETON;
    if (!strcmp(name, "creeper")) return EW_TYPE_CREEPER;
    if (!strcmp(name, "spider")) return EW_TYPE_SPIDER;
    if (!strcmp(name, "cave_spider")) return EW_TYPE_CAVE_SPIDER;
    if (!strcmp(name, "enderman")) return EW_TYPE_ENDERMAN;
    if (!strcmp(name, "blaze")) return EW_TYPE_BLAZE;
    if (!strcmp(name, "ghast")) return EW_TYPE_GHAST;
    if (!strcmp(name, "silverfish")) return EW_TYPE_SILVERFISH;
    if (!strcmp(name, "endermite")) return EW_TYPE_ENDERMITE;
    if (!strcmp(name, "giant")) return EW_TYPE_GIANT;
    if (!strcmp(name, "husk")) return EW_TYPE_HUSK;
    if (!strcmp(name, "stray")) return EW_TYPE_STRAY;
    if (!strcmp(name, "polar_bear")) return EW_TYPE_POLAR_BEAR;
    if (!strcmp(name, "rabbit")) return EW_TYPE_RABBIT;
    if (!strcmp(name, "villager")) return EW_TYPE_VILLAGER;
    if (!strcmp(name, "slime")) return EW_TYPE_SLIME;
    if (!strcmp(name, "magma_cube")) return EW_TYPE_MAGMA;
    return EW_TYPE_NONE;
}

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static uint64_t double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static EwStore *store(GmMobLive *m) {
    return m->current ? &m->b : &m->a;
}

int main(int argc, char **argv) {
    if (argc != 8) return 2;
    int type = type_for_name(argv[1]);
    int size = atoi(argv[2]);
    uint64_t entity_seed = strtoull(argv[3], NULL, 10);
    uint64_t math_cursor = strtoull(argv[4], NULL, 10);
    int next_id = atoi(argv[5]);
    int looting = atoi(argv[6]);
    int do_mob_loot = atoi(argv[7]) != 0;
    if (type == EW_TYPE_NONE || entity_seed >= (UINT64_C(1) << 48)
            || math_cursor >= (UINT64_C(1) << 48)
            || next_id <= 0 || looting < 0 || looting > 3
            || (size != 1 && size != 2 && size != 4)
            || ((type != EW_TYPE_SLIME && type != EW_TYPE_MAGMA)
                && size != 1))
        return 2;

    GmMobLive mobs;
    GmLiveSim drops;
    PsvPlayer player;
    McSinTable sin_table;
    gm_mobs_init(&mobs, 0);
    mobs.active_dimension = 0;
    mobs.next_id = 700;
    float width, height;
    ehs_size_scaled((u8)type, size, &width, &height);
    (void)width;
    const double target_x = 10.5;
    const double target_y = 19.28 + (double)PSV_EYE_HEIGHT
        - (double)height * 0.5;
    const double target_z = 30.5;
    int slot = gm_mobs_spawn_sized(
        &mobs, type, target_x, target_y, target_z, size);
    if (slot <= 0) return 1;
    mobs.a.health[slot] = 1.0F;
    mobs.b.health[slot] = 1.0F;
    if (!gm_mobs_set_entity_random_state(
            &mobs, store(&mobs)->id[slot], entity_seed, 0, 0.0))
        return 1;

    memset(&drops, 0, sizeof drops);
    memset(&player, 0, sizeof player);
    isr_init(&player.inv);
    player.ent.posX = target_x;
    player.ent.posY = 19.28;
    player.ent.posZ = target_z + 2.0;
    player.ent.onGround = 1;
    player.yaw = 180.0F;
    player.movement_speed_multiplier = 1.0;
    ICStack weapon = ic_mk(276, 1, 0);
    if (looting > 0) {
        weapon.n_enchants = 1;
        weapon.enchants[0].id = 21;
        weapon.enchants[0].level = looting;
    }
    isr_set_stack(&player.inv, 0, weapon);
    mc_sin_table_init(&sin_table);
    mobs.player_ticks_since_last_swing = 5;
    GmMobDeathContext context = {
        do_mob_loot, &math_cursor, &next_id
    };
    int result = gm_mobs_player_attack(
        &mobs, (const struct PsvPlayer *)&player, 0, 0,
        (const struct McSinTable *)&sin_table, &drops,
        0.0F, 1.0, 0, 0, 0, &context, 0.0F, NULL);
    if (result != 2 || !mobs.entity_dead[slot]) return 1;
    ICStack held = isr_get_stack(&player.inv, 0);
    EwStore *s = store(&mobs);

    printf("{\"ok\":true,\"target\":\"%s\","
           "\"after_bits\":\"%08" PRIx32 "\","
           "\"hurt_time\":%d,\"held_count\":%d,"
           "\"held_damage\":%d,\"drops\":[",
           argv[1], float_bits(s->health[slot]),
           mobs.entity_hurt_time[slot], held.count, held.meta);
    for (int i = 0; i < drops.n_active; ++i) {
        const GmLiveEnt *item = &drops.ents[i];
        if (i) putchar(',');
        printf("{\"eid\":%d,\"item\":%d,\"count\":%d,"
               "\"meta\":%d,\"position_bits\":["
               "\"%016" PRIx64 "\",\"%016" PRIx64 "\","
               "\"%016" PRIx64 "\"],\"motion_bits\":["
               "\"%016" PRIx64 "\",\"%016" PRIx64 "\","
               "\"%016" PRIx64 "\"],"
               "\"yaw_bits\":\"%08" PRIx32 "\","
               "\"hover_bits\":\"%08" PRIx32 "\","
               "\"pickup_delay\":%d,\"age\":%d}",
               item->eid, item->item, item->count, item->meta,
               double_bits(item->x - target_x),
               double_bits(item->y - target_y),
               double_bits(item->z - target_z),
               double_bits(item->mx), double_bits(item->my),
               double_bits(item->mz), float_bits(item->yaw),
               float_bits(item->hover_start), item->pickup_delay,
               item->age);
    }
    printf("],\"death_time\":%d,\"is_dead\":false,"
           "\"entity_seed48\":%" PRIu64 ","
           "\"math_seed48\":%" PRIu64 ","
           "\"next_entity_id\":%d,\"do_mob_loot\":%s}\n",
           mobs.entity_death_time[slot],
           mobs.entity_random[slot].random.seed, math_cursor, next_id,
           do_mob_loot ? "true" : "false");
    return 0;
}
