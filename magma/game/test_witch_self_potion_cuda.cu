#include "entity_witch.h"

extern "C" __global__ void witch_self_potion_compile_probe(
        JavaRandom *random, EwitchSelfState *state,
        const EwitchSelfConditions *conditions,
        EwitchSelfOutcome *outcome) {
    if (blockIdx.x == 0 && threadIdx.x == 0)
        ewitch_self_potion_step(
            random, state, conditions, outcome, (EwitchRngTrace *)0);
}

extern "C" __global__ void witch_ranged_compile_probe(
        JavaRandom *random, const EwitchRangedConditions *conditions,
        EwitchRangedOutcome *outcome) {
    if (blockIdx.x == 0 && threadIdx.x == 0)
        ewitch_ranged_attack(
            random, conditions, outcome, (EwitchRngTrace *)0);
}

extern "C" __global__ void witch_loot_compile_probe(
        JavaRandom *random, int looting_level,
        EwitchLootOutcome *outcome) {
    if (blockIdx.x == 0 && threadIdx.x == 0)
        ewitch_generate_loot(random, looting_level, outcome);
}
