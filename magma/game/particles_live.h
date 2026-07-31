#ifndef MAGMA_GAME_PARTICLES_LIVE_H
#define MAGMA_GAME_PARTICLES_LIVE_H

#include "core/types.h"
#include "mc_world.h"

#include <stdint.h>

#define GM_PARTICLES_LIVE_CAP 1024

typedef struct {
    int active;
    int model_key;
    int age;
    int max_age;
    int on_ground;
    double prev_x, prev_y, prev_z;
    double x, y, z;
    double motion_x, motion_y, motion_z;
    double bb_min_x, bb_min_y, bb_min_z;
    double bb_max_x, bb_max_y, bb_max_z;
    float jitter_x, jitter_y;
    float scale;
    float lm_r, lm_g, lm_b;
} GmLiveParticle;

typedef struct {
    GmLiveParticle particles[GM_PARTICLES_LIVE_CAP];
    uint64_t rng;
    int count;
} GmParticlesLive;

void gm_particles_live_init(GmParticlesLive *live, uint64_t seed);
void gm_particles_live_seed(GmParticlesLive *live, uint64_t seed);
int gm_particles_live_count(const GmParticlesLive *live);

int gm_particles_live_spawn_destroy(GmParticlesLive *live,
                                    int wx, int wy, int wz, int model_key,
                                    float lm_r, float lm_g, float lm_b);
int gm_particles_live_spawn_hit(GmParticlesLive *live,
                                int wx, int wy, int wz, int model_key, int face,
                                const float bounds[6],
                                float lm_r, float lm_g, float lm_b);

/* One ParticleManager.updateEffects tick. win is the region-local collision
 * window; pass NULL in a test that deliberately exercises free motion. */
void gm_particles_live_tick(GmParticlesLive *live, const Chunk *win,
                            int ox, int oz);

int gm_particles_live_emit(const GmParticlesLive *live, float partial_ticks,
                           float view_yaw, float view_pitch,
                           CrVertex *out, int max);

#endif
