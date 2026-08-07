#include "explosion.h"

#include <stdio.h>

static void run_case(int flaming)
{
    const u64 world_seed48 = UINT64_C(135120319782334);
    const u64 explosion_seed48 = UINT64_C(0x5DEECE66D);
    u16 grid[EX_VOL];
    u8 hit[EX_VOL], affected[EX_VOL];
    JavaRandom world_random, explosion_random;
    int fire_count = 0;

    ex_fill(grid, mc_state(BLK_AIR, 0));
    ex_set(grid, 8, 7, 8, mc_state(BLK_OBSIDIAN, 0));
    jrand_set_seed48(&world_random, world_seed48);
    ex_do_explosion_blocks_random_affected(
        grid, 8.5, 8.5, 8.5, 5.0F,
        hit, affected, &world_random);
    (void)jrand_float(&world_random);
    (void)jrand_float(&world_random);
    for (int x = 0; x < EX_DIM; ++x)
        for (int y = 0; y < EX_DIM; ++y)
            for (int z = 0; z < EX_DIM; ++z)
                if (hit[ex_idx(x, y, z)])
                    ex_set(grid, x, y, z, mc_state(BLK_AIR, 0));

    jrand_set_seed48(&explosion_random, explosion_seed48);
    if (flaming)
        for (int x = 0; x < EX_DIM; ++x)
            for (int y = 0; y < EX_DIM; ++y)
                for (int z = 0; z < EX_DIM; ++z)
                    if (affected[ex_idx(x, y, z)]
                            && ex_is_air(ex_get(grid, x, y, z))
                            && mc_state_id(ex_get(grid, x, y - 1, z))
                                == BLK_OBSIDIAN
                            && jrand_int_bound(&explosion_random, 3) == 0) {
                        ex_set(grid, x, y, z, mc_state(51, 0));
                        (void)jrand_int_bound(&world_random, 10);
                        ++fire_count;
                    }
    printf("%d %d %d %llu %llu\n",
        affected[ex_idx(8, 8, 8)] ? 1 : 0,
        mc_state_id(ex_get(grid, 8, 8, 8)), fire_count,
        (unsigned long long)world_random.seed,
        (unsigned long long)explosion_random.seed);
}

int main(void)
{
    run_case(1);
    run_case(0);
    return 0;
}
