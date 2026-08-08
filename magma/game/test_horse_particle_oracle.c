#include "game/particles_live.h"

#include <stdint.h>
#include <stdio.h>

static uint64_t dbits(double value) {
    union { double d; uint64_t u; } bits = {value};
    return bits.u;
}

static int run(const char *name, uint64_t seed48, int particle_id) {
    GmParticlesLive live;
    gm_particles_live_init(&live, UINT64_C(0x68707274636c));
    gm_particles_live_seed_entity_random(&live, seed48, 0, 0.0);
    if (gm_particles_live_spawn_tame_effect(
            &live, particle_id, 8.25, 65.2, 8.75,
            1.3964844F, 1.6F, 15, 0) != 7)
        return 0;
    for (int i = 0; i < 7; ++i) {
        GmLiveParticle *particle = &live.particles[i];
        printf("%s %d %d %016llx %016llx %016llx\n",
            name, i, particle_id,
            (unsigned long long)dbits(particle->x),
            (unsigned long long)dbits(particle->y),
            (unsigned long long)dbits(particle->z));
    }
    printf("%s seed %012llx\n", name,
        (unsigned long long)live.entity_rng_seed48);
    return 1;
}

int main(void) {
    return run("H", 0, 34) && run("S", 1, 11) ? 0 : 1;
}
