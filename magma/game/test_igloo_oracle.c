#include "scattered_igloo.h"
#include "stronghold_loot.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RADIUS = 12, SIDE = 25, MIN_DY = -54, MAX_DY = 12, HEIGHT = 67 };

typedef struct { u16 blocks[SIDE * SIDE * HEIGHT]; } FixtureWorld;

static int cell_index(int x, int y, int z) {
    int dy = y - 199;
    if (x < -RADIUS || x > RADIUS || z < -RADIUS || z > RADIUS
            || dy < MIN_DY || dy > MAX_DY) return -1;
    return ((dy - MIN_DY) * SIDE + (z + RADIUS)) * SIDE + (x + RADIUS);
}

static u16 fixture_get(void *opaque, int x, int y, int z) {
    FixtureWorld *world = (FixtureWorld *)opaque;
    int index = cell_index(x, y, z);
    if (index < 0) return sd_state(0, 0);
    return world->blocks[index];
}

static void fixture_set(void *opaque, int x, int y, int z, u16 state) {
    FixtureWorld *world = (FixtureWorld *)opaque;
    int index = cell_index(x, y, z);
    if (index >= 0) world->blocks[index] = state;
}

static int fixture_contains(void *opaque, int x, int y, int z) {
    (void)opaque;
    return cell_index(x, y, z) >= 0;
}

int main(int argc, char **argv) {
    FixtureWorld *world;
    SdAccess access;
    IgIgloo igloo;
    JavaRandom random, component;
    long long component_seed, population_seed;
    static const int facings[4] = {SD_NORTH, SD_EAST, SD_SOUTH, SD_WEST};
    int component_facing;
    if (argc == 3 && !strcmp(argv[1], "--loot")) {
        TeChest chest;
        tec_init(&chest);
        shl_fill_chest(&chest, SHL_IGLOO_CHEST,
                       (i64)strtoll(argv[2], NULL, 10));
        printf("{\"values\":[");
        int nonempty = 0;
        for (int slot = 0; slot < TEC_SLOTS; ++slot) {
            const TecStack *stack = &chest.slots[slot];
            u32 fields[8] = {
                (u32)stack->item, (u32)stack->count, (u32)stack->meta,
                (u32)stack->n_enchants,
                (u32)(stack->n_enchants > 0 ? stack->enchants[0].id : 0),
                (u32)(stack->n_enchants > 0 ? stack->enchants[0].level : 0),
                (u32)(stack->n_enchants > 1 ? stack->enchants[1].id : 0),
                (u32)(stack->n_enchants > 1 ? stack->enchants[1].level : 0)
            };
            for (int field = 0; field < 8; ++field) {
                if (slot || field) putchar(',');
                printf("%u", fields[field]);
            }
            if (!tec_is_empty(stack)) ++nonempty;
        }
        printf(",%d]}\n", nonempty);
        return 0;
    }
    if (argc != 3) {
        fprintf(stderr, "usage: %s COMPONENT_SEED POPULATION_SEED\n", argv[0]);
        return 2;
    }
    component_seed = strtoll(argv[1], NULL, 10);
    population_seed = strtoll(argv[2], NULL, 10);
    world = (FixtureWorld *)malloc(sizeof *world);
    if (!world) return 2;
    for (int dy = MIN_DY; dy <= MAX_DY; ++dy)
        for (int z = -RADIUS; z <= RADIUS; ++z)
            for (int x = -RADIUS; x <= RADIUS; ++x)
                world->blocks[cell_index(x, 199 + dy, z)] =
                    sd_state(dy <= 0 ? 1 : 0, 0);
    memset(&igloo, 0, sizeof igloo);
    igloo.base_y = 199;
    access.ctx = world;
    access.get = fixture_get;
    access.set = fixture_set;
    access.contains = fixture_contains;
    jrand_set(&component, component_seed);
    component_facing = facings[jrand_int_bound(&component, 4)];
    jrand_set(&random, population_seed);
    ig_igloo_generate(&access, &igloo, &random);

    printf("{\"component_facing\":%d,\"template_rotation\":%d,"
           "\"has_basement\":%s,\"middle_count\":%d,"
           "\"base_y\":199,\"population_seed48\":%llu,\"blocks\":[",
           component_facing, igloo.rotation,
           igloo.has_basement ? "true" : "false", igloo.middle_count,
           (unsigned long long)random.seed);
    for (int dy = MIN_DY; dy <= MAX_DY; ++dy)
        for (int z = -RADIUS; z <= RADIUS; ++z)
            for (int x = -RADIUS; x <= RADIUS; ++x) {
                int index = cell_index(x, 199 + dy, z);
                if (index) putchar(',');
                printf("%u", (unsigned)world->blocks[index]);
            }
    printf("],\"chest\":");
    if (igloo.chest_placed)
        printf("{\"x\":%d,\"y\":%d,\"z\":%d,\"facing\":%d,"
               "\"loot_seed\":%lld}",
               igloo.chest_x, igloo.chest_y - igloo.base_y,
               igloo.chest_z, igloo.chest_facing,
               (long long)igloo.chest_loot_seed);
    else
        printf("null");
    printf(",\"entities\":[");
    for (int i = 0; i < igloo.entity_count; ++i) {
        const IgSpawn *spawn = &igloo.entities[i];
        if (i) putchar(',');
        printf("{\"type\":\"%s\",\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
               "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,"
               "\"health\":%.9g,\"yaw\":%.9g,\"pitch\":%.9g,"
               "\"conversion_time\":%d,\"fire\":%d,\"air\":%d,"
               "\"profession\":%d,\"persistence\":%s,"
               "\"on_ground\":%s}",
               spawn->kind == IG_ENTITY_VILLAGER
                    ? "villager" : "zombie_villager",
               spawn->x, spawn->y - igloo.base_y, spawn->z,
               spawn->vx, spawn->vy, spawn->vz,
               (double)spawn->health, (double)spawn->yaw,
               (double)spawn->pitch, spawn->conversion_time,
               (int)spawn->fire, (int)spawn->air,
               (int)spawn->profession,
               spawn->persistence ? "true" : "false",
               spawn->on_ground ? "true" : "false");
    }
    puts("]}");
    free(world);
    return 0;
}
