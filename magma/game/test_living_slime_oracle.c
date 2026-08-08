#include "living_base.h"
#include "mc_rng.h"

#include <inttypes.h>
#include <limits.h>
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
    if (argc != 7) return 2;
    float width;
    float height;
    if (!strcmp(argv[1], "zombie")
            || !strcmp(argv[1], "zombie_villager")
            || !strcmp(argv[1], "pigman") || !strcmp(argv[1], "witch")) {
        width = 0.6F;
        height = 1.95F;
    } else if (!strcmp(argv[1], "skeleton")) {
        width = 0.6F;
        height = 1.99F;
    } else if (!strcmp(argv[1], "wither_skeleton")) {
        width = 0.7F;
        height = 2.4F;
    } else if (!strcmp(argv[1], "creeper")) {
        width = 0.6F;
        height = 1.7F;
    } else if (!strcmp(argv[1], "spider")) {
        width = 1.4F;
        height = 0.9F;
    } else if (!strcmp(argv[1], "cave_spider")) {
        width = 0.7F;
        height = 0.5F;
    } else if (!strcmp(argv[1], "enderman")) {
        width = 0.6F;
        height = 2.9F;
    } else if (!strcmp(argv[1], "silverfish")) {
        width = 0.4F;
        height = 0.3F;
    } else if (!strcmp(argv[1], "sheep") || !strcmp(argv[1], "pig")) {
        width = 0.9F;
        height = 1.3F;
    } else if (!strcmp(argv[1], "cow")) {
        width = 0.9F;
        height = 1.4F;
    } else if (!strcmp(argv[1], "villager")) {
        width = 0.6F;
        height = 1.95F;
    } else {
        return 2;
    }
    int walking = !strcmp(argv[2], "walk");
    int big_stone = !strcmp(argv[2], "stone_big");
    int hay = !strcmp(argv[2], "hay");
    int jump_stone = !strcmp(argv[2], "stone_jump");
    int drown = !strcmp(argv[1], "enderman")
        && !strcmp(argv[2], "drown");
    int ordinary_landing = !strcmp(argv[2], "stone")
        || big_stone || hay || jump_stone;
    if (!walking && !ordinary_landing && !drown
            && strcmp(argv[2], "bounce")) return 2;
    int hostile_landing = !strcmp(argv[1], "zombie")
        || !strcmp(argv[1], "zombie_villager")
        || !strcmp(argv[1], "skeleton")
        || !strcmp(argv[1], "wither_skeleton")
        || !strcmp(argv[1], "creeper") || !strcmp(argv[1], "spider")
        || !strcmp(argv[1], "cave_spider")
        || !strcmp(argv[1], "pigman")
        || !strcmp(argv[1], "silverfish")
        || !strcmp(argv[1], "enderman");
    int generic_landing = !strcmp(argv[1], "sheep")
        || !strcmp(argv[1], "pig") || !strcmp(argv[1], "cow")
        || !strcmp(argv[1], "villager");
    if (ordinary_landing && !hostile_landing && !generic_landing)
        return 2;
    if (!ordinary_landing && !drown && strcmp(argv[1], "zombie")
            && strcmp(argv[1], "sheep") && strcmp(argv[1], "witch"))
        return 2;
    char *end = NULL;
    long parsed_x = strtol(argv[3], &end, 10);
    if (!end || *end || parsed_x < INT_MIN || parsed_x > INT_MAX) return 2;
    int bx = (int)parsed_x;
    long parsed_z = strtol(argv[4], &end, 10);
    if (!end || *end || parsed_z < INT_MIN || parsed_z > INT_MAX) return 2;
    int bz = (int)parsed_z;
    unsigned long long parsed_entity_seed = strtoull(argv[5], &end, 10);
    if (!end || *end || parsed_entity_seed >= (1ULL << 48)) return 2;
    unsigned long long parsed_math_seed = strtoull(argv[6], &end, 10);
    if (!end || *end || parsed_math_seed >= (1ULL << 48)) return 2;

    McSinTable sin_table;
    mc_sin_table_init(&sin_table);
    EbLiving living;
    double base_x = (double)bx + 0.5;
    double base_z = (double)bz + 0.5;
    elb_init(&living, width, height, base_x, 220.0, base_z);
    living.base.phys.motionX = walking ? 0.2 : 0.0;
    living.base.phys.motionY = drown ? 0.0 : walking ? -0.05 : -0.1;
    living.base.fallDistance = ordinary_landing
        ? (big_stone || hay ? 8.0F : 5.0F) : 8.0F;
    int support = ordinary_landing ? (hay ? 170 : 1) : 165;
    PcfBlock blocks[] = {{support, (double)bx, 219.0, (double)bz, 0}};
    int contacts[][4] = {{bx, 219, bz, support}};
    if (!drown)
        elb_move_with_heading_contacts(
            &living, 0.0F, 0.0F, 0.6F,
            blocks, 1, contacts, 1, &sin_table);

    if (ordinary_landing || drown) {
        JavaRandom entity_random = {(uint64_t)parsed_entity_seed};
        JavaRandom math_random = {(uint64_t)parsed_math_seed};
        int damage = jump_stone ? 0 : big_stone ? 5 : hay || drown ? 1 : 2;
        float pitch = 0.0F;
        int teleported = 0;
        double teleport_x = 0.0D;
        double teleport_y = 0.0D;
        double teleport_z = 0.0D;
        if (damage > 0) {
            if (!drown)
                (void)jrand_double(&entity_random);
            (void)jrand_double(&math_random);
            float first = jrand_float(&entity_random);
            float second = jrand_float(&entity_random);
            pitch = (first - second) * 0.2F + 1.0F;
            if (!strcmp(argv[1], "enderman")
                    && jrand_int_bound(&entity_random, 10) != 0) {
                teleport_x = living.base.phys.posX
                    + (jrand_double(&entity_random) - 0.5D) * 64.0D
                    - base_x;
                (void)jrand_int_bound(&entity_random, 64);
                teleport_y = -216.0D;
                teleport_z = living.base.phys.posZ
                    + (jrand_double(&entity_random) - 0.5D) * 64.0D
                    - base_z;
                for (int particle = 0; particle < 128; ++particle) {
                    (void)jrand_float(&entity_random);
                    (void)jrand_float(&entity_random);
                    (void)jrand_float(&entity_random);
                    (void)jrand_double(&entity_random);
                    (void)jrand_double(&entity_random);
                    (void)jrand_double(&entity_random);
                }
                teleported = 1;
            }
        }
        const char *hurt = !strcmp(argv[1], "skeleton")
            ? "minecraft:entity.skeleton.hurt"
            : !strcmp(argv[1], "zombie_villager")
                ? "minecraft:entity.zombie_villager.hurt"
            : !strcmp(argv[1], "wither_skeleton")
                ? "minecraft:entity.wither_skeleton.hurt"
            : !strcmp(argv[1], "creeper")
                ? "minecraft:entity.creeper.hurt"
            : !strcmp(argv[1], "spider")
                    || !strcmp(argv[1], "cave_spider")
                ? "minecraft:entity.spider.hurt"
            : !strcmp(argv[1], "pigman")
                ? "minecraft:entity.zombie_pig.hurt"
            : !strcmp(argv[1], "silverfish")
                ? "minecraft:entity.silverfish.hurt"
            : !strcmp(argv[1], "enderman")
                ? "minecraft:entity.endermen.hurt"
            : !strcmp(argv[1], "sheep")
                ? "minecraft:entity.sheep.hurt"
            : !strcmp(argv[1], "pig")
                ? "minecraft:entity.pig.hurt"
            : !strcmp(argv[1], "cow")
                ? "minecraft:entity.cow.hurt"
            : !strcmp(argv[1], "villager")
                ? "minecraft:entity.villager.hurt"
            : "minecraft:entity.zombie.hurt";
        float max_health = !strcmp(argv[1], "spider") ? 16.0F
            : !strcmp(argv[1], "cave_spider") ? 12.0F
            : !strcmp(argv[1], "enderman") ? 40.0F
            : !strcmp(argv[1], "sheep") ? 8.0F
            : !strcmp(argv[1], "pig") || !strcmp(argv[1], "cow")
                ? 10.0F
            : !strcmp(argv[1], "silverfish") ? 8.0F : 20.0F;
        const char *fall = hostile_landing
            ? damage > 4 ? "minecraft:entity.hostile.big_fall"
                         : "minecraft:entity.hostile.small_fall"
            : damage > 4 ? "minecraft:entity.generic.big_fall"
                         : "minecraft:entity.generic.small_fall";
        const char *support_sound = hay
            ? "minecraft:block.grass.fall"
            : "minecraft:block.stone.fall";
        float hurt_volume = !strcmp(argv[1], "cow") ? 0.4F : 1.0F;
        printf("{\"ok\":true,\"type\":\"%s\",\"scenario\":\"%s\","
               "\"base_x\":%d,\"base_z\":%d,"
               "\"position_bits\":[\"%016" PRIx64 "\",\"%016" PRIx64
               "\",\"%016" PRIx64 "\"],\"motion_bits\":[\"%016" PRIx64
               "\",\"%016" PRIx64 "\",\"%016" PRIx64 "\"],"
               "\"on_ground\":%s,\"collided_horizontal\":%s,"
               "\"collided_vertical\":%s,\"collided\":%s,"
               "\"fall_distance_bits\":\"%08" PRIx32 "\","
               "\"health_bits\":\"%08" PRIx32 "\",\"hurt_time\":%d,"
               "\"hurt_resistant_time\":%d,\"event_order\":[",
               argv[1], argv[2], bx, bz,
               double_bits(living.base.phys.posX - base_x + teleport_x),
               double_bits(living.base.phys.posY - 220.0 + teleport_y),
               double_bits(living.base.phys.posZ - base_z + teleport_z),
               double_bits(living.base.phys.motionX),
               double_bits(living.base.phys.motionY),
               double_bits(living.base.phys.motionZ),
               living.base.phys.onGround ? "true" : "false",
               living.base.phys.collidedHorizontally ? "true" : "false",
               living.base.phys.collidedVertically ? "true" : "false",
               living.base.phys.isCollided ? "true" : "false",
               float_bits(living.base.fallDistance),
               float_bits(max_health - (float)damage),
               damage > 0 ? 10 : 0, damage > 0 ? 20 : 0);
        if (damage > 0) {
            if (!drown)
                printf("\"%s:3f800000:3f800000\",", fall);
            printf("\"status:2\",\"%s:%08" PRIx32 ":%08" PRIx32 "\"",
                   hurt, float_bits(hurt_volume), float_bits(pitch));
            if (teleported) {
                printf(",\"minecraft:entity.endermen.teleport:"
                       "3f800000:3f800000\"");
                if (!drown)
                    printf(hay
                           ? ",\"minecraft:block.grass.fall:3f000000:3f400000\","
                             "\"minecraft:block.grass.step:3e19999a:3f800000\""
                           : ",\"minecraft:block.grass.fall:3f000000:3f400000\","
                             "\"minecraft:block.stone.step:3e19999a:3f800000\"");
            }
            else if (!drown)
                printf(",\"%s:3f000000:3f400000\"", support_sound);
        }
        printf("],\"entity_seed48\":%" PRIu64
               ",\"math_seed48\":%" PRIu64
               ",\"jump_duration\":%d,\"jump_amplifier\":%d%s,"
               "\"landing_particles\":[",
               entity_random.seed, math_random.seed,
               jump_stone ? 20 : 0, jump_stone ? 1 : -1,
               !strcmp(argv[1], "creeper")
                   ? big_stone || hay
                       ? ",\"creeper_fuse\":12"
                       : ",\"creeper_fuse\":7"
                   : "");
        if (!drown)
            printf("{\"id\":38,\"count\":%d,"
               "\"long_distance\":false,\"descriptor_bits\":["
               "\"%016" PRIx64 "\",\"%016" PRIx64 "\","
               "\"%016" PRIx64 "\",\"0000000000000000\","
               "\"0000000000000000\",\"0000000000000000\","
               "\"3fc3333340000000\"],\"parameters\":[%d]}],"
               "\"support_block\":%d,\"support_meta\":0}\n",
               big_stone || hay ? 80 : 50,
               double_bits(teleported ? -teleport_x : 0.0D),
               double_bits(teleported ? -teleport_y : 0.0D),
               double_bits(teleported ? -teleport_z : 0.0D),
               support, support);
        else
            printf("],\"support_block\":%d,\"support_meta\":0}\n",
                   support);
        return 0;
    }

    printf("{\"ok\":true,\"type\":\"%s\",\"scenario\":\"%s\","
           "\"base_x\":%d,\"base_z\":%d,"
           "\"position_bits\":[\"%016" PRIx64 "\",\"%016" PRIx64
           "\",\"%016" PRIx64 "\"],\"motion_bits\":[\"%016" PRIx64
           "\",\"%016" PRIx64 "\",\"%016" PRIx64 "\"],"
           "\"on_ground\":%s,\"collided_horizontal\":%s,"
           "\"collided_vertical\":%s,\"collided\":%s,"
           "\"fall_distance_bits\":\"%08" PRIx32 "\","
           "\"landing_particles\":[{\"id\":38,\"count\":80,"
           "\"long_distance\":false,\"descriptor_bits\":["
           "\"0000000000000000\",\"0000000000000000\","
           "\"0000000000000000\",\"0000000000000000\","
           "\"0000000000000000\",\"0000000000000000\","
           "\"3fc3333340000000\"],\"parameters\":[165]}],"
           "\"support_block\":165,\"support_meta\":0}\n",
           argv[1], argv[2], bx, bz,
           double_bits(living.base.phys.posX - base_x),
           double_bits(living.base.phys.posY - 220.0),
           double_bits(living.base.phys.posZ - base_z),
           double_bits(living.base.phys.motionX),
           double_bits(living.base.phys.motionY),
           double_bits(living.base.phys.motionZ),
           living.base.phys.onGround ? "true" : "false",
           living.base.phys.collidedHorizontally ? "true" : "false",
           living.base.phys.collidedVertically ? "true" : "false",
           living.base.phys.isCollided ? "true" : "false",
           float_bits(living.base.fallDistance));
    return 0;
}
