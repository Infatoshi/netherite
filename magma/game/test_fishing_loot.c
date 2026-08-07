#include "fishing.h"

#include <stdint.h>
#include <stdio.h>

static void emit(int value) {
    printf("%08x\n", (unsigned)value);
}

int main(void) {
    JavaGaussianRandom random;
    jrand_gaussian_set_state(&random, UINT64_C(0x0fedcba98765), 0, 0.0);
    for (int i = 0; i < 48; ++i) {
        FishLoot loot = fish_generate_loot(&random, (float)(i & 3));
        emit(loot.item); emit(loot.count); emit(loot.meta);
        emit(loot.n_enchants);
        emit(loot.n_enchants > 0 ? loot.enchant_id[0] : 0);
        emit(loot.n_enchants > 0 ? loot.enchant_level[0] : 0);
        emit(loot.n_enchants > 1 ? loot.enchant_id[1] : 0);
        emit(loot.n_enchants > 1 ? loot.enchant_level[1] : 0);
    }
    return 0;
}
