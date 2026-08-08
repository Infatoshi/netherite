#ifndef MAGMA_GAME_PARTICLES_LIVE_H
#define MAGMA_GAME_PARTICLES_LIVE_H

#include "core/types.h"
#include "mc_world.h"

#include <stdint.h>

#define GM_PARTICLES_LIVE_CAP 1024
#define GM_PARTICLE_EMITTER_CAP 16

enum {
    GM_LIVE_PARTICLE_BLOCK = 0,
    GM_LIVE_PARTICLE_EXPLOSION_NORMAL = 1,
    GM_LIVE_PARTICLE_EXPLOSION_LARGE = 2,
    GM_LIVE_PARTICLE_EXPLOSION_HUGE = 3,
    GM_LIVE_PARTICLE_WATER_BUBBLE = 4,
    GM_LIVE_PARTICLE_WATER_SPLASH = 5,
    GM_LIVE_PARTICLE_CRIT = 6,
    GM_LIVE_PARTICLE_CRIT_MAGIC = 7,
    GM_LIVE_PARTICLE_DAMAGE_INDICATOR = 8,
    GM_LIVE_PARTICLE_SWEEP_ATTACK = 9,
    GM_LIVE_PARTICLE_SPELL_MOB = 10,
    GM_LIVE_PARTICLE_SMOKE_LARGE = 11,
    GM_LIVE_PARTICLE_NOTE = 12,
    GM_LIVE_PARTICLE_SMOKE_NORMAL = 13,
    GM_LIVE_PARTICLE_HEART = 14,
    GM_LIVE_PARTICLE_SPIT = 15,
    GM_LIVE_PARTICLE_PORTAL = 16
};

typedef struct {
    int active;
    int kind;
    int newborn;
    int recorded_exact;
    int model_key;
    int texture_index;
    int texture_base;
    int age;
    int max_age;
    int on_ground;
    double prev_x, prev_y, prev_z;
    double x, y, z;
    double origin_x, origin_y, origin_z;
    double motion_x, motion_y, motion_z;
    double bb_min_x, bb_min_y, bb_min_z;
    double bb_max_x, bb_max_y, bb_max_z;
    float jitter_x, jitter_y;
    float scale;
    float original_scale;
    float gravity;
    float gray;
    float color_r, color_g, color_b;
    float lm_r, lm_g, lm_b;
    /* ParticleDigging multiplyColor base (block colorMultiplier as 0..1).
     * White (1,1,1) for untinted blocks; emit multiplies into the 0.6 gray. */
    float base_r, base_g, base_b;
} GmLiveParticle;

typedef struct {
    int active;
    int newborn;
    int kind;
    int age;
    int lifetime;
    double x, y, z;
    float width, height;
    int sky_light, block_light;
} GmLiveParticleEmitter;

typedef struct {
    GmLiveParticle particles[GM_PARTICLES_LIVE_CAP];
    GmLiveParticleEmitter emitters[GM_PARTICLE_EMITTER_CAP];
    uint64_t rng;
    uint64_t entity_rng_seed48;
    int count;
    int emitter_count;
    int gaussian_ready;
    double gaussian_value;
    int entity_gaussian_ready;
    double entity_gaussian_value;
} GmParticlesLive;

void gm_particles_live_init(GmParticlesLive *live, uint64_t seed);
void gm_particles_live_seed(GmParticlesLive *live, uint64_t seed);
void gm_particles_live_seed_entity_random(
    GmParticlesLive *live, uint64_t raw_seed48,
    int have_gaussian, double gaussian_value);
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
int gm_particles_live_spawn_block(GmParticlesLive *live,
                                  double x, double y, double z,
                                  double speed_x, double speed_y,
                                  double speed_z, int model_key,
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

/* V1 tape seam populated from the Particle returned by the Java factory.
 * Unlike spawn_recorded, every constructor-random field consumed by native
 * tick/render is authoritative. age/max_age are Particle age for NORMAL,
 * private life/lifeTime for LARGE, timeSinceStart/8 for HUGE, and ordinary
 * Particle age for SPELL_MOB. */
int gm_particles_live_spawn_recorded_state(
    GmParticlesLive *live, int particle_id,
    double prev_x, double prev_y, double prev_z,
    double x, double y, double z,
    double motion_x, double motion_y, double motion_z,
    int age, int max_age, int on_ground, float scale,
    float color_r, float color_g, float color_b,
    int texture_index, int texture_base,
    int sky_light, int block_light);

/* EntityLlamaSpit spawn-packet particle (id 48). The seven packet velocities
 * are authoritative inputs; ParticleExplosion's private +/-0.05 constructor
 * entropy stays in the deterministic visual pool. */
int gm_particles_live_spawn_spit(GmParticlesLive *live,
                                 double x, double y, double z,
                                 double speed_x, double speed_y,
                                 double speed_z, int sky_light,
                                 int block_light);

/* Live World.spawnParticle path for player water-entry ids 4 and 5. Spawn
 * arguments are exact runtime events; constructor-only entropy is supplied by
 * this deterministic visual pool and does not feed simulation state. */
int gm_particles_live_spawn_water(GmParticlesLive *live, int particle_id,
                                  double x, double y, double z,
                                  double speed_x, double speed_y,
                                  double speed_z, int sky_light,
                                  int block_light);

/* EntityAreaEffectCloud's SPELL_MOB (id 15) client constructor. The RGB
 * channels are the World.spawnAlwaysVisibleParticle speed arguments. */
int gm_particles_live_spawn_spell(GmParticlesLive *live, int particle_id,
                                  double x, double y, double z,
                                  double color_r, double color_g,
                                  double color_b, int sky_light,
                                  int block_light);

/* ParticleSmokeNormal ids 11/12. BlockLiquid uses the 2.5x large variant;
 * tame failure uses the ordinary variant. Constructor-private entropy stays
 * in this deterministic visual pool. */
int gm_particles_live_spawn_smoke(GmParticlesLive *live, int particle_id,
                                  double x, double y, double z,
                                  double speed_x, double speed_y,
                                  double speed_z, int sky_light,
                                  int block_light);

/* Breeding/tame HEART (id 34). The factory ignores the speed arguments, as
 * vanilla ParticleHeart passes zero motion to Particle's base constructor. */
int gm_particles_live_spawn_heart(GmParticlesLive *live, int particle_id,
                                  double x, double y, double z,
                                  int sky_light, int block_light);

/* AbstractHorse status 6/7: expand one entity status into the seven client
 * World.spawnParticle calls using a dedicated Java Random stream. */
int gm_particles_live_spawn_tame_effect(
    GmParticlesLive *live, int particle_id,
    double x, double y, double z, float width, float height,
    int sky_light, int block_light);

/* Note-block NOTE (id 23) client constructor. speed_x is note / 24. */
int gm_particles_live_spawn_note(GmParticlesLive *live, int particle_id,
                                 double x, double y, double z,
                                 double speed_x, int sky_light,
                                 int block_light);

/* PORTAL (id 24), shared by Ender Chest display ticks, Ender Pearls, and
 * Eyes of Ender. Spawn arguments are exact World.spawnParticle values; the
 * Particle-private constructor streams remain deterministic visual entropy. */
int gm_particles_live_spawn_portal(GmParticlesLive *live, int particle_id,
                                   double x, double y, double z,
                                   double speed_x, double speed_y,
                                   double speed_z, int sky_light,
                                   int block_light);

/* Combat packet/emitter seam. ids 9/10 are attached CRIT emitters (count=-1),
 * id 44 is a randomized DAMAGE_INDICATOR packet, and id 45 is SWEEP_ATTACK. */
int gm_particles_live_spawn_combat(
    GmParticlesLive *live, int particle_id, int count,
    double x, double y, double z,
    double motion_x, double motion_y, double motion_z,
    double offset_x, double offset_y, double offset_z, double speed,
    float entity_width, float entity_height,
    int sky_light, int block_light);

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

/* WATER_BUBBLE/WATER_SPLASH layer-0 billboards on particles.png. */
int gm_particles_live_emit_water(const GmParticlesLive *live,
                                 float partial_ticks, float view_yaw,
                                 float view_pitch, CrVertex *out, int max);

/* SPELL_MOB layer-0 billboards on particles.png. */
int gm_particles_live_emit_spell(const GmParticlesLive *live,
                                 float partial_ticks, float view_yaw,
                                 float view_pitch, CrVertex *out, int max);

/* SMOKE_LARGE layer-0 billboards on particles.png. */
int gm_particles_live_emit_smoke(const GmParticlesLive *live,
                                 float partial_ticks, float view_yaw,
                                 float view_pitch, CrVertex *out, int max);

/* HEART layer-0 billboards on particles.png. */
int gm_particles_live_emit_heart(const GmParticlesLive *live,
                                 float partial_ticks, float view_yaw,
                                 float view_pitch, CrVertex *out, int max);

/* PORTAL layer-0 billboards on particles.png. */
int gm_particles_live_emit_portal(const GmParticlesLive *live,
                                  float partial_ticks, float view_yaw,
                                  float view_pitch, CrVertex *out, int max);

/* Note/combat FX layer 0 (note/crit/magic/damage) or 3 (sweep.png). */
int gm_particles_live_emit_combat(const GmParticlesLive *live, int fx_layer,
                                  float partial_ticks, float view_yaw,
                                  float view_pitch, CrVertex *out, int max);

#endif
