#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "game/village_live.h"

static uint64_t hash_add(uint64_t hash, int value) {
    hash ^= (uint32_t)value;
    return hash * UINT64_C(0x100000001b3);
}

static int one(long long seed, int x, int z, int biome_type) {
    GmVillage village;
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    if (!gm_village_build(seed, x, z, biome_type, 0, &village)) return 0;
    for (int i = 0; i < village.count; ++i) {
        const GmVillagePiece *piece = &village.pieces[i];
        hash = hash_add(hash, piece->kind);
        hash = hash_add(hash, piece->component_type);
        hash = hash_add(hash, piece->facing);
        hash = hash_add(hash, piece->box.min_x);
        hash = hash_add(hash, piece->box.min_y);
        hash = hash_add(hash, piece->box.min_z);
        hash = hash_add(hash, piece->box.max_x);
        hash = hash_add(hash, piece->box.max_y);
        hash = hash_add(hash, piece->box.max_z);
        for (int j = 0; j < 4; ++j) hash = hash_add(hash, piece->extra[j]);
        if (getenv("VILLAGE_VERBOSE"))
            printf("P %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
                i, piece->kind, piece->component_type, piece->facing,
                piece->box.min_x, piece->box.min_y, piece->box.min_z,
                piece->box.max_x, piece->box.max_y, piece->box.max_z,
                piece->extra[0], piece->extra[1], piece->extra[2],
                piece->extra[3]);
    }
    printf("%lld %d %d %d %d %d %016" PRIx64 "\n",
           seed, x, z, biome_type, village.count, village.valid, hash);
    return 1;
}

int main(void) {
    return !(one(0LL, 2, 2, GM_VILLAGE_PLAINS)
        && one(1LL, -62, 114, GM_VILLAGE_DESERT)
        && one(123456789LL, 962, -654, GM_VILLAGE_SAVANNA)
        && one(-99887766LL, -1438, -1150, GM_VILLAGE_TAIGA)
        && one(49LL, 322, 706, GM_VILLAGE_PLAINS)
        && one(9876543212345LL, -318, 514, GM_VILLAGE_DESERT));
}
