#ifndef NETHERITE_LIGHTNING_H
#define NETHERITE_LIGHTNING_H

#include "det.h"

struct world;

/* EntityLightningBolt belongs to World.weatherEffects, not loadedEntityList. */
struct lightning_bolt {
    double x, y, z;
    det_rng rand;
    int state, living_time, dead, ticks_existed;
    int64_t vertex;
};

void lightning_adopt(struct lightning_bolt *bolt, double x, double y, double z,
                     uint64_t rand_state, int64_t vertex, int living_time);
void lightning_constructor_fire(struct world *w, det_rng *rand, double x, double y,
                                double z, int difficulty, int do_fire_tick);
void lightning_tick(struct lightning_bolt *bolt, struct world *w, int do_fire_tick,
                    void (*strike)(void *ctx, const struct lightning_bolt *bolt), void *ctx);

#endif
