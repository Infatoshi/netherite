#include "game/mob_live.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int type_for_name(const char *name) {
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
    if (!strcmp(name, "slime")) return EW_TYPE_SLIME;
    if (!strcmp(name, "magma_cube")) return EW_TYPE_MAGMA;
    if (!strcmp(name, "silverfish")) return EW_TYPE_SILVERFISH;
    if (!strcmp(name, "endermite")) return EW_TYPE_ENDERMITE;
    if (!strcmp(name, "giant")) return EW_TYPE_GIANT;
    if (!strcmp(name, "husk")) return EW_TYPE_HUSK;
    if (!strcmp(name, "stray")) return EW_TYPE_STRAY;
    if (!strcmp(name, "polar_bear")) return EW_TYPE_POLAR_BEAR;
    if (!strcmp(name, "rabbit")) return EW_TYPE_RABBIT;
    if (!strcmp(name, "villager")) return EW_TYPE_VILLAGER;
    if (!strcmp(name, "vindicator")) return EW_TYPE_VINDICATOR;
    if (!strcmp(name, "evoker")) return EW_TYPE_EVOKER;
    if (!strcmp(name, "snowman")) return EW_TYPE_SNOWMAN;
    return EW_TYPE_NONE;
}

int main(int argc, char **argv) {
    if (argc != 6) return 2;
    int type = type_for_name(argv[1]);
    int size = atoi(argv[2]);
    int looting = atoi(argv[3]);
    int killed_by_player = atoi(argv[4]);
    uint64_t seed = strtoull(argv[5], NULL, 10);
    GmHostileLootOutcome loot;
    if (type == EW_TYPE_NONE || !gm_mobs_generate_hostile_loot(
            type, size, &seed, looting, killed_by_player, &loot))
        return 1;
    printf("{\"ok\":true,\"target\":\"%s\",\"size\":%d,"
           "\"looting\":%d,\"killed_by_player\":%s,\"items\":[",
           argv[1], size, looting, killed_by_player ? "true" : "false");
    for (int i = 0; i < loot.count; ++i) {
        if (i) putchar(',');
        printf("{\"item\":%d,\"count\":%d,\"meta\":%d",
               loot.item[i], loot.quantity[i], loot.meta[i]);
        if (loot.potion_type[i] == GM_HOSTILE_LOOT_POTION_SLOWNESS)
            printf(",\"tag\":\"{Potion:\\\"minecraft:slowness\\\"}\"");
        putchar('}');
    }
    printf("],\"entity_seed48\":%" PRIu64 "}\n", seed);
    return 0;
}
