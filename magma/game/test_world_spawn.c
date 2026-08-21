#include "game/world_spawn.h"

#include <stdio.h>

static int pin(long long seed, int want_x, int want_y, int want_z) {
    int x, y, z;
    if (!gm_create_spawn_position(seed, 0, 0, &x, &y, &z)) {
        fprintf(stderr, "world_spawn: seed %lld findBiomePosition failed\n", seed);
        return 1;
    }
    if (x != want_x || y != want_y || z != want_z) {
        fprintf(stderr, "world_spawn: seed %lld got %d,%d,%d want %d,%d,%d\n",
                seed, x, y, z, want_x, want_y, want_z);
        return 1;
    }
    fprintf(stderr, "world_spawn: PASS seed %lld spawn %d,%d,%d\n", seed, x, y, z);
    return 0;
}

int main(void) {
    /* oracle java/Minecraft/run/saves/qrl_<seed>/level.dat SpawnX/Y/Z */
    if (pin(1000, 168, 64, 252)) return 1;
    if (pin(0, 44, 64, 176)) return 1;
    return 0;
}
