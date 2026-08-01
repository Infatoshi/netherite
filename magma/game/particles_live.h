#ifndef MAGMA_GAME_PARTICLES_LIVE_H
#define MAGMA_GAME_PARTICLES_LIVE_H

#include "core/types.h"
#include "mc_world.h"

#include <stdint.h>

#define GM_PARTICLES_LIVE_CAP 1024

enum {
    GM_LIVE_PARTICLE_BLOCK = 0,
    GM_LIVE_PARTICLE_EXPLOSION_NORMAL = 1,
    GM_LIVE_PARTICLE_EXPLOSION_LARGE = 2,
    GM_LIVE_PARTICLE_EXPLOSION_HUGE = 3
};

typedef struct {
    int active;
    int kind;
    int newborn;
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
    float gray;
    float lm_r, lm_g, lm_b;
    /* ParticleDigging multiplyColor base (block colorMultiplier as 0..1).
     * White (1,1,1) for untinted blocks; emit multiplies into the 0.6 gray. */
    float base_r, base_g, base_b;
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
                                    float lm_r, float lm_g, float lm_b,
                                    float base_r, float base_g, float base_b);
int gm_particles_live_spawn_hit(GmParticlesLive *live,
                                int wx, int wy, int wz, int model_key, int face,
                                const float bounds[6],
                                float lm_r, float lm_g, float lm_b,
                                float base_r, float base_g, float base_b);

/* Tape-replay constructor seam for 1.11.2 EnumParticleTypes ids 0..2.
 * Positions and speed arguments are the recorded World.spawnParticle call;
 * constructor-only random attributes remain on this pool's deterministic RNG. */
int gm_particles_live_spawn_recorded(GmParticlesLive *live, int particle_id,
                                     double x, double y, double z,
                                     double speed_x, double speed_y,
                                     double speed_z, int sky_light,
                                     int block_light);

/* True while a recorded explosion-class particle from a replay event is still
 * alive. The renderer uses this to suppress its stateless RNG reconstruction. */
int gm_particles_live_suppresses_explosion(const GmParticlesLive *live);

/* One ParticleManager.updateEffects tick. win is the region-local collision
 * window; pass NULL in a test that deliberately exercises free motion. */
void gm_particles_live_tick(GmParticlesLive *live, const Chunk *win,
                            int ox, int oz);

int gm_particles_live_emit(const GmParticlesLive *live, float partial_ticks,
                           float view_yaw, float view_pitch,
                           CrVertex *out, int max);

/* Recorded explosion render layers: 0 = ParticleExplosion on particles.png,
 * 3 = ParticleExplosionLarge on explosion.png. HUGE is an invisible emitter;
 * its separately recorded LARGE children are spawned by later script rows. */
int gm_particles_live_emit_recorded(const GmParticlesLive *live, int fx_layer,
                                    float partial_ticks, float view_yaw,
                                    float view_pitch, CrVertex *out, int max);

#endif
