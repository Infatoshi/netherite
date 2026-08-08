#include "game/dragon_live.h"

#include <stdint.h>
#include <stdio.h>

static uint64_t add(uint64_t hash, int value) {
    hash ^= (uint32_t)value;
    return hash * UINT64_C(0x100000001b3);
}

static uint64_t double_bits(double value) {
    union { double d; uint64_t u; } bits;
    bits.d = value;
    return bits.u;
}

static void descriptors(long long seed) {
    for (int i = 0; i < ED_NUM_CRYSTALS; ++i) {
        int x, z, radius, height, guarded;
        ed_pillar_spec((u64)seed, i, &x, &z, &radius, &height, &guarded);
        printf("D %lld %d %d %d %d %d %d\n",
               seed, i, x, z, radius, height, guarded);
    }
}

int main(void) {
    static const long long seeds[] = {0, 1, 123456789, -99887766};
    GmWorld *world;
    GmDragonLive dragon;
    for (unsigned i = 0; i < sizeof seeds / sizeof seeds[0]; ++i)
        descriptors(seeds[i]);
    world = gm_world_create_type(0, 3);
    if (!world) return 1;
    gm_world_ensure(world, 0, 0, 4);
    gm_dragon_init(&dragon, world, 0);
    for (int i = 0; i < ED_NUM_CRYSTALS; ++i) {
        int cx, cz, radius, height, guarded;
        int obsidian = 0, bars = 0, bedrock = 0;
        uint64_t hash = UINT64_C(0xcbf29ce484222325);
        const EdCrystal *crystal = &dragon.state.arena.crystals[i];
        ed_pillar_spec(0, i, &cx, &cz, &radius, &height, &guarded);
        for (int x = cx - radius; x <= cx + radius; ++x)
            for (int y = 0; y <= height + 10; ++y)
                for (int z = cz - radius; z <= cz + radius; ++z) {
                    int id = gm_world_block(world, x, y, z);
                    if (id == 49) ++obsidian;
                    else if (id == 101) ++bars;
                    else if (id == 7) ++bedrock;
                    else continue;
                    hash = add(hash, x);
                    hash = add(hash, y);
                    hash = add(hash, z);
                    hash = add(hash, id);
                    hash = add(hash, gm_world_meta(world, x, y, z));
                }
        printf("S %d %d %d %d %016llx %016llx %016llx %016llx 0 0 0 0 1\n",
               i, obsidian, bars, bedrock,
               (unsigned long long)hash,
               (unsigned long long)double_bits(crystal->x),
               (unsigned long long)double_bits(crystal->y),
               (unsigned long long)double_bits(crystal->z));
        (void)guarded;
    }
    gm_world_destroy(world);
    return 0;
}
