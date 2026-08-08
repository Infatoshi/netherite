#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#include "player_survival.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include "game/particles_live.h"

#include "assets/blockmodels.h"
#include "assets/mob_atlas.h"

#include <math.h>
#include <string.h>

#define PL_DEG2RAD 0.017453292519943295769f
#define PL_COLLISION_MAX 64
/* Legacy pcl records ParticleManager.spawnEffectParticle arguments, but
 * ParticleExplosion adds an unrecorded uniform +/-0.05 velocity. With 0.9
 * drag, the worst missing displacement is 0.095 after two updates, still
 * inside the minimum 0.1 billboard half-width; after three it is 0.1355 and
 * the sprite location is no longer bounded. Keep the exact vanilla lifetime
 * and suppression state, but only draw NORMAL while its taped kinematics
 * still locate even the smallest possible sprite. */
#define PL_LEGACY_RECORDED_NORMAL_MAX_RENDER_AGE 2
/* LARGE has an unrecorded random maxAge in [6,9]. Ages 0..5 are the only
 * frames guaranteed to exist for every captured spawn, so rendering later
 * would invent survival for particles whose constructor RNG is unavailable. */
#define PL_LEGACY_RECORDED_LARGE_MAX_RENDER_AGE 5

static uint64_t pl_rng_u64(GmParticlesLive *live) {
    uint64_t x = live->rng;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    live->rng = x;
    return x * UINT64_C(2685821657736338717);
}

static float pl_rng_float(GmParticlesLive *live) {
    return (float)(pl_rng_u64(live) >> 40) * (1.0f / 16777216.0f);
}

static double pl_rng_double(GmParticlesLive *live) {
    return (double)(pl_rng_u64(live) >> 11) *
           (1.0 / 9007199254740992.0);
}

static double pl_rng_gaussian(GmParticlesLive *live) {
    if (live->gaussian_ready) {
        live->gaussian_ready = 0;
        return live->gaussian_value;
    }
    double x, y, radius;
    do {
        x = 2.0 * pl_rng_double(live) - 1.0;
        y = 2.0 * pl_rng_double(live) - 1.0;
        radius = x * x + y * y;
    } while (radius >= 1.0 || radius == 0.0);
    double scale = sqrt(-2.0 * log(radius) / radius);
    live->gaussian_value = y * scale;
    live->gaussian_ready = 1;
    return x * scale;
}

static uint32_t pl_entity_next(GmParticlesLive *live, int bits) {
    live->entity_rng_seed48 = (live->entity_rng_seed48
        * UINT64_C(0x5deece66d) + UINT64_C(0xb))
        & ((UINT64_C(1) << 48) - 1);
    return (uint32_t)(live->entity_rng_seed48 >> (48 - bits));
}

static float pl_entity_float(GmParticlesLive *live) {
    return (float)pl_entity_next(live, 24) * (1.0f / 16777216.0f);
}

static double pl_entity_double(GmParticlesLive *live) {
    uint64_t high = pl_entity_next(live, 26);
    uint64_t low = pl_entity_next(live, 27);
    return (double)((high << 27) + low)
        / (double)(UINT64_C(1) << 53);
}

static double pl_entity_gaussian(GmParticlesLive *live) {
    if (live->entity_gaussian_ready) {
        live->entity_gaussian_ready = 0;
        return live->entity_gaussian_value;
    }
    double x, y, radius;
    do {
        x = 2.0 * pl_entity_double(live) - 1.0;
        y = 2.0 * pl_entity_double(live) - 1.0;
        radius = x * x + y * y;
    } while (radius >= 1.0 || radius == 0.0);
    double scale = sqrt(-2.0 * log(radius) / radius);
    live->entity_gaussian_value = y * scale;
    live->entity_gaussian_ready = 1;
    return x * scale;
}

static float pl_mc_sin(float value) {
    int index = (int)(value * 10430.378f) & 65535;
    return (float)sin((double)index * 3.14159265358979323846 * 2.0
                      / 65536.0);
}

void gm_particles_live_seed(GmParticlesLive *live, uint64_t seed) {
    if (!live) return;
    live->rng = seed ? seed : UINT64_C(0x9e3779b97f4a7c15);
    live->gaussian_ready = 0;
    live->gaussian_value = 0.0;
    gm_particles_live_seed_entity_random(
        live, seed ^ UINT64_C(0x5deece66d), 0, 0.0);
}

void gm_particles_live_seed_entity_random(
        GmParticlesLive *live, uint64_t raw_seed48,
        int have_gaussian, double gaussian_value) {
    if (!live) return;
    live->entity_rng_seed48 = raw_seed48
        & ((UINT64_C(1) << 48) - 1);
    live->entity_gaussian_ready = have_gaussian != 0;
    live->entity_gaussian_value = gaussian_value;
}

void gm_particles_live_init(GmParticlesLive *live, uint64_t seed) {
    if (!live) return;
    memset(live, 0, sizeof *live);
    gm_particles_live_seed(live, seed);
}

int gm_particles_live_count(const GmParticlesLive *live) {
    return live ? live->count : 0;
}

static GmLiveParticle *pl_alloc(GmParticlesLive *live) {
    if (!live || live->count >= GM_PARTICLES_LIVE_CAP) return NULL;
    for (int i = 0; i < GM_PARTICLES_LIVE_CAP; ++i) {
        if (!live->particles[i].active) {
            memset(&live->particles[i], 0, sizeof live->particles[i]);
            live->particles[i].active = 1;
            live->count++;
            return &live->particles[i];
        }
    }
    return NULL;
}

static void pl_set_position(GmLiveParticle *p, double x, double y, double z,
                            float width, float height) {
    float half = width / 2.0f;
    p->x = p->prev_x = x;
    p->y = p->prev_y = y;
    p->z = p->prev_z = z;
    p->bb_min_x = x - (double)half;
    p->bb_min_y = y;
    p->bb_min_z = z - (double)half;
    p->bb_max_x = x + (double)half;
    p->bb_max_y = y + (double)height;
    p->bb_max_z = z + (double)half;
}

static void pl_set_size(GmLiveParticle *p, float width, float height) {
    p->bb_max_x = p->bb_min_x + (double)width;
    p->bb_max_y = p->bb_min_y + (double)height;
    p->bb_max_z = p->bb_min_z + (double)width;
}

static GmLiveParticle *pl_spawn(GmParticlesLive *live,
                                double x, double y, double z,
                                double speed_x, double speed_y, double speed_z,
                                int model_key,
                                float lm_r, float lm_g, float lm_b,
                                float base_r, float base_g, float base_b) {
    GmLiveParticle *p = pl_alloc(live);
    if (!p) return NULL;

    pl_set_position(p, x, y, z, 0.2f, 0.2f);
    p->jitter_x = pl_rng_float(live) * 3.0f;
    p->jitter_y = pl_rng_float(live) * 3.0f;
    p->scale = (pl_rng_float(live) * 0.5f + 0.5f) * 2.0f;
    p->max_age = (int)(4.0f / (pl_rng_float(live) * 0.9f + 0.1f));

    p->motion_x = speed_x +
        (pl_rng_double(live) * 2.0 - 1.0) * 0.4000000059604645;
    p->motion_y = speed_y +
        (pl_rng_double(live) * 2.0 - 1.0) * 0.4000000059604645;
    p->motion_z = speed_z +
        (pl_rng_double(live) * 2.0 - 1.0) * 0.4000000059604645;
    float speed = (float)(pl_rng_double(live) + pl_rng_double(live) + 1.0) * 0.15f;
    float norm = (float)sqrt(p->motion_x * p->motion_x +
                             p->motion_y * p->motion_y +
                             p->motion_z * p->motion_z);
    p->motion_x = p->motion_x / (double)norm * (double)speed *
                  0.4000000059604645;
    p->motion_y = p->motion_y / (double)norm * (double)speed *
                  0.4000000059604645 + 0.10000000149011612;
    p->motion_z = p->motion_z / (double)norm * (double)speed *
                  0.4000000059604645;

    p->model_key = model_key;
    p->scale /= 2.0f;
    p->lm_r = lm_r;
    p->lm_g = lm_g;
    p->lm_b = lm_b;
    /* ParticleDigging: particleRed starts 0.6 then *= colorMultiplier/255.
     * base_* is that multiplier (1,1,1 when untinted / Blocks.GRASS skip). */
    p->base_r = base_r;
    p->base_g = base_g;
    p->base_b = base_b;
    return p;
}

int gm_particles_live_spawn_water(GmParticlesLive *live, int particle_id,
                                  double x, double y, double z,
                                  double speed_x, double speed_y,
                                  double speed_z, int sky_light,
                                  int block_light) {
    if (particle_id != 4 && particle_id != 5) return 0;
    GmLiveParticle *p = pl_alloc(live);
    if (!p) return 0;

    /* Particle's shared constructor. This pool makes its otherwise wall-clock
     * private Random/Math.random entropy deterministic for repeatable play. */
    pl_set_position(p, x, y, z, 0.2f, 0.2f);
    p->jitter_x = pl_rng_float(live) * 3.0f;
    p->jitter_y = pl_rng_float(live) * 3.0f;
    p->scale = (pl_rng_float(live) * 0.5f + 0.5f) * 2.0f;
    p->max_age = (int)(4.0f / (pl_rng_float(live) * 0.9f + 0.1f));
    double base_speed_x = particle_id == 5 ? 0.0 : speed_x;
    double base_speed_y = particle_id == 5 ? 0.0 : speed_y;
    double base_speed_z = particle_id == 5 ? 0.0 : speed_z;
    p->motion_x = base_speed_x
        + (pl_rng_double(live) * 2.0 - 1.0) * 0.4000000059604645;
    p->motion_y = base_speed_y
        + (pl_rng_double(live) * 2.0 - 1.0) * 0.4000000059604645;
    p->motion_z = base_speed_z
        + (pl_rng_double(live) * 2.0 - 1.0) * 0.4000000059604645;
    float speed = (float)(pl_rng_double(live) + pl_rng_double(live) + 1.0)
        * 0.15f;
    float norm = (float)sqrt(p->motion_x * p->motion_x
        + p->motion_y * p->motion_y + p->motion_z * p->motion_z);
    p->motion_x = p->motion_x / (double)norm * (double)speed
        * 0.4000000059604645;
    p->motion_y = p->motion_y / (double)norm * (double)speed
        * 0.4000000059604645 + 0.10000000149011612;
    p->motion_z = p->motion_z / (double)norm * (double)speed
        * 0.4000000059604645;

    p->newborn = 1;
    p->lm_r = (float)sky_light;
    p->lm_g = (float)block_light;
    p->base_r = p->base_g = p->base_b = 1.0f;
    if (particle_id == 4) {
        p->kind = GM_LIVE_PARTICLE_WATER_BUBBLE;
        p->texture_index = 32;
        pl_set_size(p, 0.02f, 0.02f);
        p->scale *= pl_rng_float(live) * 0.6f + 0.2f;
        p->motion_x = speed_x * 0.20000000298023224
            + (pl_rng_double(live) * 2.0 - 1.0) * 0.019999999552965164;
        p->motion_y = speed_y * 0.20000000298023224
            + (pl_rng_double(live) * 2.0 - 1.0) * 0.019999999552965164;
        p->motion_z = speed_z * 0.20000000298023224
            + (pl_rng_double(live) * 2.0 - 1.0) * 0.019999999552965164;
        p->max_age = (int)(8.0 /
            (pl_rng_double(live) * 0.8 + 0.2));
    } else {
        /* ParticleRain constructor followed by ParticleSplash overrides. */
        p->kind = GM_LIVE_PARTICLE_WATER_SPLASH;
        p->motion_x *= 0.30000001192092896;
        p->motion_y = pl_rng_double(live) * 0.20000000298023224
            + 0.10000000149011612;
        p->motion_z *= 0.30000001192092896;
        p->texture_index = 20 + (int)(pl_rng_u64(live) & 3);
        pl_set_size(p, 0.01f, 0.01f);
        p->gravity = 0.04f;
        p->max_age = (int)(8.0 /
            (pl_rng_double(live) * 0.8 + 0.2));
        if (speed_y == 0.0 && (speed_x != 0.0 || speed_z != 0.0)) {
            p->motion_x = speed_x;
            p->motion_y = speed_y + 0.1;
            p->motion_z = speed_z;
        }
    }
    return 1;
}

int gm_particles_live_spawn_spell(GmParticlesLive *live, int particle_id,
                                  double x, double y, double z,
                                  double color_r, double color_g,
                                  double color_b, int sky_light,
                                  int block_light) {
    if (!live || particle_id != 15) return 0;
    GmLiveParticle *p = pl_alloc(live);
    if (!p) return 0;

    /* ParticleSpell substitutes two draws from its class-static Random for
     * the constructor's X/Z velocities, then invokes Particle's ordinary
     * constructor. The private JVM streams are intentionally collapsed into
     * this deterministic visual pool; simulation-visible spawn arguments are
     * carried exactly by GmRuntimeParticleEvent. */
    double speed_x = 0.5 - pl_rng_double(live);
    double speed_z = 0.5 - pl_rng_double(live);
    pl_set_position(p, x, y, z, 0.2f, 0.2f);
    p->jitter_x = pl_rng_float(live) * 3.0f;
    p->jitter_y = pl_rng_float(live) * 3.0f;
    p->scale = (pl_rng_float(live) * 0.5f + 0.5f) * 2.0f;
    p->max_age = (int)(4.0f / (pl_rng_float(live) * 0.9f + 0.1f));
    p->motion_x = speed_x
        + (pl_rng_double(live) * 2.0 - 1.0) * 0.4000000059604645;
    p->motion_y = color_g
        + (pl_rng_double(live) * 2.0 - 1.0) * 0.4000000059604645;
    p->motion_z = speed_z
        + (pl_rng_double(live) * 2.0 - 1.0) * 0.4000000059604645;
    float magnitude = (float)(
        pl_rng_double(live) + pl_rng_double(live) + 1.0) * 0.15f;
    float norm = (float)sqrt(p->motion_x * p->motion_x
        + p->motion_y * p->motion_y + p->motion_z * p->motion_z);
    if (norm != 0.0f) {
        p->motion_x = p->motion_x / (double)norm * (double)magnitude
            * 0.4000000059604645;
        p->motion_y = p->motion_y / (double)norm * (double)magnitude
            * 0.4000000059604645 + 0.10000000149011612;
        p->motion_z = p->motion_z / (double)norm * (double)magnitude
            * 0.4000000059604645;
    }
    p->motion_y *= 0.20000000298023224;
    if (color_r == 0.0 && color_b == 0.0) {
        p->motion_x *= 0.10000000149011612;
        p->motion_z *= 0.10000000149011612;
    }
    p->scale *= 0.75f;
    p->max_age = (int)(8.0 /
        (pl_rng_double(live) * 0.8 + 0.2));
    p->kind = GM_LIVE_PARTICLE_SPELL_MOB;
    p->texture_index = 128;
    p->texture_base = 128;
    p->color_r = (float)color_r;
    p->color_g = (float)color_g;
    p->color_b = (float)color_b;
    p->lm_r = (float)sky_light;
    p->lm_g = (float)block_light;
    return 1;
}

int gm_particles_live_spawn_smoke(GmParticlesLive *live, int particle_id,
                                  double x, double y, double z,
                                  double speed_x, double speed_y,
                                  double speed_z, int sky_light,
                                  int block_light) {
    if (!live || (particle_id != 11 && particle_id != 12)) return 0;
    GmLiveParticle *p = pl_alloc(live);
    if (!p) return 0;
    float variant_scale = particle_id == 12 ? 2.5f : 1.0f;

    pl_set_position(p, x, y, z, 0.2f, 0.2f);
    p->jitter_x = pl_rng_float(live) * 3.0f;
    p->jitter_y = pl_rng_float(live) * 3.0f;
    p->scale = (pl_rng_float(live) * 0.5f + 0.5f) * 2.0f;
    p->max_age = (int)(4.0f / (pl_rng_float(live) * 0.9f + 0.1f));
    p->motion_x = (pl_rng_double(live) * 2.0 - 1.0)
        * 0.4000000059604645;
    p->motion_y = (pl_rng_double(live) * 2.0 - 1.0)
        * 0.4000000059604645;
    p->motion_z = (pl_rng_double(live) * 2.0 - 1.0)
        * 0.4000000059604645;
    float magnitude = (float)(
        pl_rng_double(live) + pl_rng_double(live) + 1.0) * 0.15f;
    float norm = (float)sqrt(p->motion_x * p->motion_x
        + p->motion_y * p->motion_y + p->motion_z * p->motion_z);
    if (norm != 0.0f) {
        p->motion_x = p->motion_x / (double)norm * (double)magnitude
            * 0.4000000059604645;
        p->motion_y = p->motion_y / (double)norm * (double)magnitude
            * 0.4000000059604645 + 0.10000000149011612;
        p->motion_z = p->motion_z / (double)norm * (double)magnitude
            * 0.4000000059604645;
    }
    p->motion_x = p->motion_x * 0.10000000149011612 + speed_x;
    p->motion_y = p->motion_y * 0.10000000149011612 + speed_y;
    p->motion_z = p->motion_z * 0.10000000149011612 + speed_z;
    p->gray = (float)(pl_rng_double(live) * 0.30000001192092896);
    p->color_r = p->color_g = p->color_b = p->gray;
    p->scale *= 0.75f;
    p->scale *= variant_scale;
    p->original_scale = p->scale;
    p->max_age = (int)(8.0 /
        (pl_rng_double(live) * 0.8 + 0.2));
    p->max_age = (int)((float)p->max_age * variant_scale);
    p->kind = particle_id == 12 ? GM_LIVE_PARTICLE_SMOKE_LARGE
                                : GM_LIVE_PARTICLE_SMOKE_NORMAL;
    p->texture_index = 0;
    p->newborn = 1;
    p->lm_r = (float)sky_light;
    p->lm_g = (float)block_light;
    return 1;
}

int gm_particles_live_spawn_heart(GmParticlesLive *live, int particle_id,
                                  double x, double y, double z,
                                  int sky_light, int block_light) {
    if (!live || particle_id != 34) return 0;
    GmLiveParticle *p = pl_alloc(live);
    if (!p) return 0;

    pl_set_position(p, x, y, z, 0.2f, 0.2f);
    p->jitter_x = pl_rng_float(live) * 3.0f;
    p->jitter_y = pl_rng_float(live) * 3.0f;
    p->scale = (pl_rng_float(live) * 0.5f + 0.5f) * 2.0f;
    p->max_age = (int)(4.0f / (pl_rng_float(live) * 0.9f + 0.1f));
    p->motion_x = (pl_rng_double(live) * 2.0 - 1.0)
        * 0.4000000059604645;
    p->motion_y = (pl_rng_double(live) * 2.0 - 1.0)
        * 0.4000000059604645;
    p->motion_z = (pl_rng_double(live) * 2.0 - 1.0)
        * 0.4000000059604645;
    float magnitude = (float)(
        pl_rng_double(live) + pl_rng_double(live) + 1.0) * 0.15f;
    float norm = (float)sqrt(p->motion_x * p->motion_x
        + p->motion_y * p->motion_y + p->motion_z * p->motion_z);
    p->motion_x = p->motion_x / (double)norm * (double)magnitude
        * 0.4000000059604645;
    p->motion_y = p->motion_y / (double)norm * (double)magnitude
        * 0.4000000059604645 + 0.10000000149011612;
    p->motion_z = p->motion_z / (double)norm * (double)magnitude
        * 0.4000000059604645;
    p->motion_x *= 0.009999999776482582;
    p->motion_y = p->motion_y * 0.009999999776482582 + 0.1;
    p->motion_z *= 0.009999999776482582;
    p->scale *= 0.75f;
    p->scale *= 2.0f;
    p->original_scale = p->scale;
    p->max_age = 16;
    p->kind = GM_LIVE_PARTICLE_HEART;
    p->texture_index = 80;
    p->color_r = p->color_g = p->color_b = 1.0f;
    p->newborn = 1;
    p->lm_r = (float)sky_light;
    p->lm_g = (float)block_light;
    return 1;
}

int gm_particles_live_spawn_tame_effect(
        GmParticlesLive *live, int particle_id,
        double x, double y, double z, float width, float height,
        int sky_light, int block_light) {
    if (!live || (particle_id != 11 && particle_id != 34)
            || width <= 0.0f || height <= 0.0f)
        return 0;
    int spawned = 0;
    for (int i = 0; i < 7; ++i) {
        double vx = pl_entity_gaussian(live) * 0.02;
        double vy = pl_entity_gaussian(live) * 0.02;
        double vz = pl_entity_gaussian(live) * 0.02;
        double px = x + (double)(
            pl_entity_float(live) * width * 2.0f) - (double)width;
        double py = y + 0.5 + (double)(
            pl_entity_float(live) * height);
        double pz = z + (double)(
            pl_entity_float(live) * width * 2.0f) - (double)width;
        spawned += particle_id == 34
            ? gm_particles_live_spawn_heart(
                live, particle_id, px, py, pz, sky_light, block_light)
            : gm_particles_live_spawn_smoke(
                live, particle_id, px, py, pz,
                vx, vy, vz, sky_light, block_light);
    }
    return spawned;
}

int gm_particles_live_spawn_destroy(GmParticlesLive *live,
                                    int wx, int wy, int wz, int model_key,
                                    float lm_r, float lm_g, float lm_b,
                                    float base_r, float base_g, float base_b) {
    int spawned = 0;
    for (int ix = 0; ix < 4; ++ix) {
        for (int iy = 0; iy < 4; ++iy) {
            for (int iz = 0; iz < 4; ++iz) {
                double dx = ((double)ix + 0.5) / 4.0;
                double dy = ((double)iy + 0.5) / 4.0;
                double dz = ((double)iz + 0.5) / 4.0;
                if (pl_spawn(live, (double)wx + dx, (double)wy + dy,
                             (double)wz + dz, dx - 0.5, dy - 0.5, dz - 0.5,
                             model_key, lm_r, lm_g, lm_b,
                             base_r, base_g, base_b))
                    spawned++;
            }
        }
    }
    return spawned;
}

int gm_particles_live_spawn_hit(GmParticlesLive *live,
                                int wx, int wy, int wz, int model_key, int face,
                                const float bounds[6],
                                float lm_r, float lm_g, float lm_b,
                                float base_r, float base_g, float base_b) {
    static const float full[6] = { 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };
    const float *b = bounds ? bounds : full;
    double x = (double)wx + pl_rng_double(live) *
        ((double)b[3] - (double)b[0] - 0.20000000298023224) +
        0.10000000149011612 + (double)b[0];
    double y = (double)wy + pl_rng_double(live) *
        ((double)b[4] - (double)b[1] - 0.20000000298023224) +
        0.10000000149011612 + (double)b[1];
    double z = (double)wz + pl_rng_double(live) *
        ((double)b[5] - (double)b[2] - 0.20000000298023224) +
        0.10000000149011612 + (double)b[2];

    if (face == 0) y = (double)wy + (double)b[1] - 0.10000000149011612;
    if (face == 1) y = (double)wy + (double)b[4] + 0.10000000149011612;
    if (face == 2) z = (double)wz + (double)b[2] - 0.10000000149011612;
    if (face == 3) z = (double)wz + (double)b[5] + 0.10000000149011612;
    if (face == 4) x = (double)wx + (double)b[0] - 0.10000000149011612;
    if (face == 5) x = (double)wx + (double)b[3] + 0.10000000149011612;

    GmLiveParticle *p = pl_spawn(live, x, y, z, 0.0, 0.0, 0.0,
                                 model_key, lm_r, lm_g, lm_b,
                                 base_r, base_g, base_b);
    if (!p) return 0;
    p->motion_x *= (double)0.2f;
    p->motion_y = (p->motion_y - 0.10000000149011612) * (double)0.2f +
                  0.10000000149011612;
    p->motion_z *= (double)0.2f;
    pl_set_size(p, 0.2f * 0.6f, 0.2f * 0.6f);
    p->scale *= 0.6f;
    return 1;
}

int gm_particles_live_spawn_block(GmParticlesLive *live,
                                  double x, double y, double z,
                                  double speed_x, double speed_y,
                                  double speed_z, int model_key,
                                  float lm_r, float lm_g, float lm_b,
                                  float base_r, float base_g, float base_b) {
    GmLiveParticle *particle = pl_spawn(
        live, x, y, z, speed_x, speed_y, speed_z, model_key,
        lm_r, lm_g, lm_b, base_r, base_g, base_b);
    if (!particle) return 0;
    particle->newborn = 1;
    return 1;
}

int gm_particles_live_spawn_recorded(GmParticlesLive *live, int particle_id,
                                     double x, double y, double z,
                                     double speed_x, double speed_y,
                                     double speed_z, int sky_light,
                                     int block_light) {
    if (particle_id < 0 || particle_id > 2) return 0;
    GmLiveParticle *p = pl_alloc(live);
    if (!p) return 0;

    pl_set_position(p, x, y, z, 0.2f, 0.2f);
    p->kind = particle_id == 0 ? GM_LIVE_PARTICLE_EXPLOSION_NORMAL :
              particle_id == 1 ? GM_LIVE_PARTICLE_EXPLOSION_LARGE :
                                 GM_LIVE_PARTICLE_EXPLOSION_HUGE;
    /* World.spawnParticle queues the new Particle until updateEffects ends.
     * It renders at constructor age on the spawn tick and first updates on the
     * following client tick. */
    p->newborn = 1;
    p->lm_r = (float)sky_light;
    p->lm_g = (float)block_light;

    /* Particle's base constructor initializes these before each subclass.
     * They are overwritten where vanilla overwrites them, but consuming the
     * deterministic pool stream keeps constructor sequencing stable. */
    p->jitter_x = pl_rng_float(live) * 3.0f;
    p->jitter_y = pl_rng_float(live) * 3.0f;
    p->scale = (pl_rng_float(live) * 0.5f + 0.5f) * 2.0f;
    p->max_age = (int)(4.0f / (pl_rng_float(live) * 0.9f + 0.1f));
    (void)pl_rng_double(live); (void)pl_rng_double(live);
    (void)pl_rng_double(live); (void)pl_rng_double(live);
    (void)pl_rng_double(live); (void)pl_rng_double(live);
    (void)pl_rng_double(live);

    if (particle_id == 0) {
        /* Replay treats the captured velocity as authoritative kinematics.
         * ParticleExplosion normally adds an unrecoverable Math.random
         * +/-0.05 here; adding a second unrelated draw moves every recorded
         * particle away from its taped trajectory. Consume the constructor
         * draws to keep later deterministic attributes stable, then retain
         * the recorded values themselves. */
        (void)pl_rng_double(live); (void)pl_rng_double(live);
        (void)pl_rng_double(live);
        p->motion_x = speed_x;
        p->motion_y = speed_y;
        p->motion_z = speed_z;
        p->gray = pl_rng_float(live) * 0.3f + 0.7f;
        p->scale = pl_rng_float(live) * pl_rng_float(live) * 6.0f + 1.0f;
        p->max_age = (int)(16.0 /
            ((double)pl_rng_float(live) * 0.8 + 0.2)) + 2;
    } else if (particle_id == 1) {
        /* ParticleExplosionLarge ignores y/z speed and uses x speed only as
         * animation progress, which fixes its size for its whole lifetime. */
        p->motion_x = p->motion_y = p->motion_z = 0.0;
        p->max_age = 6 + (int)(pl_rng_u64(live) % 4);
        p->gray = pl_rng_float(live) * 0.6f + 0.4f;
        p->scale = 1.0f - (float)speed_x * 0.5f;
    } else {
        /* ParticleExplosionHuge renders nothing. Its six LARGE children per
         * update are captured as their own later pcl rows, so replay must not
         * generate a second RNG-placed set here. */
        p->motion_x = p->motion_y = p->motion_z = 0.0;
        p->max_age = 8;
    }
    return 1;
}

int gm_particles_live_spawn_spit(GmParticlesLive *live,
                                 double x, double y, double z,
                                 double speed_x, double speed_y,
                                 double speed_z, int sky_light,
                                 int block_light) {
    GmLiveParticle *p = pl_alloc(live);
    if (!p) return 0;

    /* Particle's shared constructor consumes its ordinary attributes before
     * ParticleExplosion replaces motion, scale, color, and lifetime. */
    pl_set_position(p, x, y, z, 0.2f, 0.2f);
    p->jitter_x = pl_rng_float(live) * 3.0f;
    p->jitter_y = pl_rng_float(live) * 3.0f;
    p->scale = (pl_rng_float(live) * 0.5f + 0.5f) * 2.0f;
    p->max_age = (int)(4.0f / (pl_rng_float(live) * 0.9f + 0.1f));
    (void)pl_rng_double(live); (void)pl_rng_double(live);
    (void)pl_rng_double(live); (void)pl_rng_double(live);
    (void)pl_rng_double(live); (void)pl_rng_double(live);
    (void)pl_rng_double(live);

    p->motion_x = speed_x
        + (pl_rng_double(live) * 2.0 - 1.0) * 0.05000000074505806;
    p->motion_y = speed_y
        + (pl_rng_double(live) * 2.0 - 1.0) * 0.05000000074505806;
    p->motion_z = speed_z
        + (pl_rng_double(live) * 2.0 - 1.0) * 0.05000000074505806;
    p->gray = pl_rng_float(live) * 0.3f + 0.7f;
    p->scale = pl_rng_float(live) * pl_rng_float(live) * 6.0f + 1.0f;
    p->max_age = (int)(16.0 /
        ((double)pl_rng_float(live) * 0.8 + 0.2)) + 2;
    p->kind = GM_LIVE_PARTICLE_SPIT;
    p->newborn = 1;
    p->lm_r = (float)sky_light;
    p->lm_g = (float)block_light;
    return 1;
}

int gm_particles_live_spawn_recorded_state(
        GmParticlesLive *live, int particle_id,
        double prev_x, double prev_y, double prev_z,
        double x, double y, double z,
        double motion_x, double motion_y, double motion_z,
        int age, int max_age, int on_ground, float scale,
        float color_r, float color_g, float color_b,
        int texture_index, int texture_base,
        int sky_light, int block_light) {
    if ((particle_id != 0 && particle_id != 1 && particle_id != 2
             && particle_id != 11 && particle_id != 15
             && particle_id != 34 && particle_id != 48)
            || age < 0 || max_age <= 0
            || age > max_age || !isfinite(prev_x) || !isfinite(prev_y)
            || !isfinite(prev_z) || !isfinite(x) || !isfinite(y)
            || !isfinite(z) || !isfinite(motion_x) || !isfinite(motion_y)
            || !isfinite(motion_z) || !isfinite(scale)
            || !isfinite(color_r) || !isfinite(color_g)
            || !isfinite(color_b) || texture_index < 0
            || texture_index > 255 || texture_base < 0
            || texture_base > 255)
        return 0;
    GmLiveParticle *p = pl_alloc(live);
    if (!p) return 0;
    pl_set_position(p, x, y, z, 0.2f, 0.2f);
    p->prev_x = prev_x;
    p->prev_y = prev_y;
    p->prev_z = prev_z;
    p->motion_x = motion_x;
    p->motion_y = motion_y;
    p->motion_z = motion_z;
    p->kind = particle_id == 0 ? GM_LIVE_PARTICLE_EXPLOSION_NORMAL :
              particle_id == 1 ? GM_LIVE_PARTICLE_EXPLOSION_LARGE :
              particle_id == 2 ? GM_LIVE_PARTICLE_EXPLOSION_HUGE :
              particle_id == 11 ? GM_LIVE_PARTICLE_SMOKE_NORMAL :
              particle_id == 15 ? GM_LIVE_PARTICLE_SPELL_MOB :
              particle_id == 48 ? GM_LIVE_PARTICLE_SPIT :
                                  GM_LIVE_PARTICLE_HEART;
    p->age = age;
    p->max_age = max_age;
    p->on_ground = on_ground != 0;
    p->scale = scale;
    p->original_scale = scale;
    p->gray = color_r;
    p->color_r = color_r;
    p->color_g = color_g;
    p->color_b = color_b;
    p->texture_index = texture_index;
    p->texture_base = texture_base;
    p->lm_r = (float)sky_light;
    p->lm_g = (float)block_light;
    p->newborn = 1;
    p->recorded_exact = 1;
    return 1;
}

int gm_particles_live_suppresses_explosion(const GmParticlesLive *live) {
    if (!live) return 0;
    for (int i = 0; i < GM_PARTICLES_LIVE_CAP; ++i)
        if (live->particles[i].active &&
            live->particles[i].kind >= GM_LIVE_PARTICLE_EXPLOSION_NORMAL &&
            live->particles[i].kind <= GM_LIVE_PARTICLE_EXPLOSION_HUGE)
            return 1;
    return 0;
}

static void pl_offset_bb(GmLiveParticle *p, double x, double y, double z) {
    p->bb_min_x += x; p->bb_max_x += x;
    p->bb_min_y += y; p->bb_max_y += y;
    p->bb_min_z += z; p->bb_max_z += z;
}

static void pl_move(GmLiveParticle *p, const Chunk *win, int ox, int oz,
                    double x, double y, double z) {
    double orig_x = x, orig_y = y, orig_z = z;
    if (!win) {
        pl_offset_bb(p, x, y, z);
    } else {
        McAABB bb = mc_aabb_make(p->bb_min_x - (double)ox, p->bb_min_y,
                                 p->bb_min_z - (double)oz,
                                 p->bb_max_x - (double)ox, p->bb_max_y,
                                 p->bb_max_z - (double)oz);
        McAABB query = mc_aabb_addcoord(&bb, x, y, z);
        McAABB blocks[PL_COLLISION_MAX];
        int n = psv_collect_blocks(win, &query, blocks, PL_COLLISION_MAX);
        for (int i = 0; i < n; ++i)
            y = mc_aabb_calcYOffset(&blocks[i], &bb, y);
        bb = mc_aabb_offset(&bb, 0.0, y, 0.0);
        for (int i = 0; i < n; ++i)
            x = mc_aabb_calcXOffset(&blocks[i], &bb, x);
        bb = mc_aabb_offset(&bb, x, 0.0, 0.0);
        for (int i = 0; i < n; ++i)
            z = mc_aabb_calcZOffset(&blocks[i], &bb, z);
        bb = mc_aabb_offset(&bb, 0.0, 0.0, z);
        p->bb_min_x = bb.minX + (double)ox;
        p->bb_min_y = bb.minY;
        p->bb_min_z = bb.minZ + (double)oz;
        p->bb_max_x = bb.maxX + (double)ox;
        p->bb_max_y = bb.maxY;
        p->bb_max_z = bb.maxZ + (double)oz;
    }
    p->x = (p->bb_min_x + p->bb_max_x) / 2.0;
    p->y = p->bb_min_y;
    p->z = (p->bb_min_z + p->bb_max_z) / 2.0;
    p->on_ground = orig_y != y && orig_y < 0.0;
    if (orig_x != x) p->motion_x = 0.0;
    if (orig_z != z) p->motion_z = 0.0;
}

static GmLiveParticle *pl_spawn_crit(
        GmParticlesLive *live, int kind,
        double x, double y, double z,
        double speed_x, double speed_y, double speed_z,
        int sky_light, int block_light, int newborn) {
    GmLiveParticle *p = pl_alloc(live);
    if (!p) return NULL;
    pl_set_position(p, x, y, z, 0.2f, 0.2f);

    /* Particle's base constructor. Vanilla uses a private Random plus global
     * Math.random; this visual-only pool supplies a deterministic stream while
     * retaining the constructor formulas and draw classes. */
    p->jitter_x = pl_rng_float(live) * 3.0f;
    p->jitter_y = pl_rng_float(live) * 3.0f;
    p->scale = (pl_rng_float(live) * 0.5f + 0.5f) * 2.0f;
    p->max_age = (int)(4.0f / (pl_rng_float(live) * 0.9f + 0.1f));
    p->motion_x = (pl_rng_double(live) * 2.0 - 1.0)
        * 0.4000000059604645;
    p->motion_y = (pl_rng_double(live) * 2.0 - 1.0)
        * 0.4000000059604645;
    p->motion_z = (pl_rng_double(live) * 2.0 - 1.0)
        * 0.4000000059604645;
    float magnitude = (float)(pl_rng_double(live) + pl_rng_double(live) + 1.0)
        * 0.15f;
    float norm = (float)sqrt(p->motion_x * p->motion_x
        + p->motion_y * p->motion_y + p->motion_z * p->motion_z);
    if (norm != 0.0f) {
        p->motion_x = p->motion_x / (double)norm * (double)magnitude
            * 0.4000000059604645;
        p->motion_y = p->motion_y / (double)norm * (double)magnitude
            * 0.4000000059604645 + 0.10000000149011612;
        p->motion_z = p->motion_z / (double)norm * (double)magnitude
            * 0.4000000059604645;
    }

    if (kind == GM_LIVE_PARTICLE_DAMAGE_INDICATOR) speed_y += 1.0;
    p->motion_x = p->motion_x * 0.10000000149011612 + speed_x * 0.4;
    p->motion_y = p->motion_y * 0.10000000149011612 + speed_y * 0.4;
    p->motion_z = p->motion_z * 0.10000000149011612 + speed_z * 0.4;
    float color = (float)(pl_rng_double(live) * 0.30000001192092896
        + 0.6000000238418579);
    p->color_r = p->color_g = p->color_b = color;
    p->scale *= 0.75f;
    p->original_scale = p->scale;
    p->max_age = (int)(6.0 /
        (pl_rng_double(live) * 0.8 + 0.6));
    p->texture_index = 65;
    p->kind = kind;
    p->lm_r = (float)sky_light;
    p->lm_g = (float)block_light;

    /* ParticleCrit invokes onUpdate once inside its constructor. */
    p->prev_x = p->x;
    p->prev_y = p->y;
    p->prev_z = p->z;
    ++p->age;
    pl_move(p, NULL, 0, 0, p->motion_x, p->motion_y, p->motion_z);
    p->color_g = (float)((double)p->color_g * 0.96);
    p->color_b = (float)((double)p->color_b * 0.9);
    p->motion_x *= 0.699999988079071;
    p->motion_y *= 0.699999988079071;
    p->motion_z *= 0.699999988079071;
    p->motion_y -= 0.019999999552965164;

    if (kind == GM_LIVE_PARTICLE_CRIT_MAGIC) {
        p->color_r *= 0.3f;
        p->color_g *= 0.8f;
        p->texture_index = 66;
    } else if (kind == GM_LIVE_PARTICLE_DAMAGE_INDICATOR) {
        p->max_age = 20;
        p->texture_index = 67;
    }
    p->newborn = newborn;
    return p;
}

static int pl_update_emitter(GmParticlesLive *live,
                             GmLiveParticleEmitter *emitter,
                             int child_newborn) {
    int spawned = 0;
    for (int i = 0; i < 16; ++i) {
        double dx = pl_rng_float(live) * 2.0f - 1.0f;
        double dy = pl_rng_float(live) * 2.0f - 1.0f;
        double dz = pl_rng_float(live) * 2.0f - 1.0f;
        if (dx * dx + dy * dy + dz * dz > 1.0) continue;
        double x = emitter->x + dx * (double)emitter->width / 4.0;
        double y = emitter->y + (double)(emitter->height / 2.0f)
            + dy * (double)emitter->height / 4.0;
        double z = emitter->z + dz * (double)emitter->width / 4.0;
        if (pl_spawn_crit(live, emitter->kind, x, y, z,
                          dx, dy + 0.2, dz,
                          emitter->sky_light, emitter->block_light,
                          child_newborn))
            ++spawned;
    }
    ++emitter->age;
    if (emitter->age >= emitter->lifetime) {
        emitter->active = 0;
        --live->emitter_count;
    }
    return spawned;
}

int gm_particles_live_spawn_combat(
        GmParticlesLive *live, int particle_id, int count,
        double x, double y, double z,
        double motion_x, double motion_y, double motion_z,
        double offset_x, double offset_y, double offset_z, double speed,
        float entity_width, float entity_height,
        int sky_light, int block_light) {
    if (!live) return 0;
    if ((particle_id == 9 || particle_id == 10) && count == -1) {
        for (int i = 0; i < GM_PARTICLE_EMITTER_CAP; ++i) {
            GmLiveParticleEmitter *emitter = &live->emitters[i];
            if (emitter->active) continue;
            *emitter = (GmLiveParticleEmitter){
                1, 1,
                particle_id == 9 ? GM_LIVE_PARTICLE_CRIT
                                 : GM_LIVE_PARTICLE_CRIT_MAGIC,
                0, 3, x, y, z, entity_width, entity_height,
                sky_light, block_light
            };
            ++live->emitter_count;
            return pl_update_emitter(live, emitter, 1);
        }
        return 0;
    }
    if (particle_id == 44 && count > 0) {
        int spawned = 0;
        for (int i = 0; i < count; ++i) {
            double px = x + pl_rng_gaussian(live) * offset_x;
            double py = y + pl_rng_gaussian(live) * offset_y;
            double pz = z + pl_rng_gaussian(live) * offset_z;
            double vx = pl_rng_gaussian(live) * speed;
            double vy = pl_rng_gaussian(live) * speed;
            double vz = pl_rng_gaussian(live) * speed;
            if (pl_spawn_crit(live, GM_LIVE_PARTICLE_DAMAGE_INDICATOR,
                              px, py, pz, vx, vy, vz,
                              sky_light, block_light, 1))
                ++spawned;
        }
        return spawned;
    }
    if (particle_id == 45 && count == 0) {
        GmLiveParticle *p = pl_alloc(live);
        if (!p) return 0;
        pl_set_position(p, x, y, z, 0.2f, 0.2f);
        p->jitter_x = pl_rng_float(live) * 3.0f;
        p->jitter_y = pl_rng_float(live) * 3.0f;
        p->scale = (pl_rng_float(live) * 0.5f + 0.5f) * 2.0f;
        p->max_age = (int)(4.0f / (pl_rng_float(live) * 0.9f + 0.1f));
        (void)pl_rng_double(live); (void)pl_rng_double(live);
        (void)pl_rng_double(live); (void)pl_rng_double(live);
        (void)pl_rng_double(live); (void)pl_rng_double(live);
        (void)pl_rng_double(live);
        p->kind = GM_LIVE_PARTICLE_SWEEP_ATTACK;
        p->max_age = 4;
        p->gray = pl_rng_float(live) * 0.6f + 0.4f;
        p->scale = 1.0f - (float)motion_x * 0.5f;
        p->motion_x = motion_x;
        p->motion_y = motion_y;
        p->motion_z = motion_z;
        p->newborn = 1;
        (void)offset_x; (void)offset_y; (void)offset_z; (void)speed;
        return 1;
    }
    return 0;
}

int gm_particles_live_spawn_note(
        GmParticlesLive *live, int particle_id,
        double x, double y, double z, double speed_x,
        int sky_light, int block_light) {
    if (!live || particle_id != 23) return 0;
    GmLiveParticle *p = pl_alloc(live);
    if (!p) return 0;
    pl_set_position(p, x, y, z, 0.2f, 0.2f);
    p->jitter_x = pl_rng_float(live) * 3.0f;
    p->jitter_y = pl_rng_float(live) * 3.0f;
    p->scale = (pl_rng_float(live) * 0.5f + 0.5f) * 2.0f;
    p->max_age = (int)(4.0f / (pl_rng_float(live) * 0.9f + 0.1f));
    p->motion_x = (pl_rng_double(live) * 2.0 - 1.0)
        * 0.4000000059604645;
    p->motion_y = (pl_rng_double(live) * 2.0 - 1.0)
        * 0.4000000059604645;
    p->motion_z = (pl_rng_double(live) * 2.0 - 1.0)
        * 0.4000000059604645;
    float magnitude = (float)(pl_rng_double(live) + pl_rng_double(live) + 1.0)
        * 0.15f;
    float norm = (float)sqrt(p->motion_x * p->motion_x
        + p->motion_y * p->motion_y + p->motion_z * p->motion_z);
    if (norm != 0.0f) {
        p->motion_x = p->motion_x / (double)norm * (double)magnitude
            * 0.4000000059604645;
        p->motion_y = p->motion_y / (double)norm * (double)magnitude
            * 0.4000000059604645 + 0.10000000149011612;
        p->motion_z = p->motion_z / (double)norm * (double)magnitude
            * 0.4000000059604645;
    }
    p->motion_x *= 0.009999999776482582;
    p->motion_y *= 0.009999999776482582;
    p->motion_z *= 0.009999999776482582;
    p->motion_y += 0.2;
    float note = (float)speed_x;
    p->color_r = pl_mc_sin((note + 0.0f) * 6.2831855f) * 0.65f + 0.35f;
    p->color_g = pl_mc_sin((note + 0.33333334f) * 6.2831855f)
        * 0.65f + 0.35f;
    p->color_b = pl_mc_sin((note + 0.6666667f) * 6.2831855f)
        * 0.65f + 0.35f;
    p->scale *= 0.75f;
    p->scale *= 2.0f;
    p->original_scale = p->scale;
    p->max_age = 6;
    p->texture_index = 64;
    p->kind = GM_LIVE_PARTICLE_NOTE;
    p->lm_r = (float)sky_light;
    p->lm_g = (float)block_light;
    p->newborn = 1;
    return 1;
}

int gm_particles_live_spawn_portal(
        GmParticlesLive *live, int particle_id,
        double x, double y, double z,
        double speed_x, double speed_y, double speed_z,
        int sky_light, int block_light) {
    if (!live || particle_id != 24) return 0;
    GmLiveParticle *p = pl_alloc(live);
    if (!p) return 0;

    /* Particle's constructor consumes four private Random floats and five
     * Math.random doubles before ParticlePortal replaces its motion. */
    pl_set_position(p, x, y, z, 0.2f, 0.2f);
    p->jitter_x = pl_rng_float(live) * 3.0f;
    p->jitter_y = pl_rng_float(live) * 3.0f;
    p->scale = (pl_rng_float(live) * 0.5f + 0.5f) * 2.0f;
    p->max_age = (int)(4.0f / (pl_rng_float(live) * 0.9f + 0.1f));
    (void)pl_rng_double(live);
    (void)pl_rng_double(live);
    (void)pl_rng_double(live);
    (void)pl_rng_double(live);
    (void)pl_rng_double(live);

    p->kind = GM_LIVE_PARTICLE_PORTAL;
    p->motion_x = speed_x;
    p->motion_y = speed_y;
    p->motion_z = speed_z;
    p->origin_x = x;
    p->origin_y = y;
    p->origin_z = z;
    float color = pl_rng_float(live) * 0.6f + 0.4f;
    p->scale = pl_rng_float(live) * 0.2f + 0.5f;
    p->original_scale = p->scale;
    p->color_r = color * 0.9f;
    p->color_g = color * 0.3f;
    p->color_b = color;
    p->max_age = (int)(pl_rng_double(live) * 10.0) + 40;
    p->texture_index = (int)(pl_rng_double(live) * 8.0);
    p->lm_r = (float)sky_light;
    p->lm_g = (float)block_light;
    p->newborn = 1;
    return 1;
}

void gm_particles_live_tick(GmParticlesLive *live, const Chunk *win,
                            int ox, int oz) {
    if (!live) return;
    for (int i = 0; i < GM_PARTICLES_LIVE_CAP; ++i) {
        GmLiveParticle *p = &live->particles[i];
        if (!p->active) continue;
        if (p->newborn) {
            p->newborn = 0;
            continue;
        }
        if (p->kind == GM_LIVE_PARTICLE_PORTAL) {
            p->prev_x = p->x;
            p->prev_y = p->y;
            p->prev_z = p->z;
            float progress = (float)p->age / (float)p->max_age;
            float curve = 1.0f - (-progress + progress * progress * 2.0f);
            p->x = p->origin_x + p->motion_x * (double)curve;
            p->y = p->origin_y + p->motion_y * (double)curve
                + (double)(1.0f - progress);
            p->z = p->origin_z + p->motion_z * (double)curve;
            if (p->age++ >= p->max_age) {
                p->active = 0;
                --live->count;
            }
            continue;
        }
        if (p->kind == GM_LIVE_PARTICLE_WATER_BUBBLE) {
            p->prev_x = p->x;
            p->prev_y = p->y;
            p->prev_z = p->z;
            ++p->age;
            p->motion_y += 0.002;
            pl_move(p, win, ox, oz, p->motion_x, p->motion_y, p->motion_z);
            p->motion_x *= 0.8500000238418579;
            p->motion_y *= 0.8500000238418579;
            p->motion_z *= 0.8500000238418579;
            int expired = p->max_age-- <= 0;
            if (win) {
                int id = psv_get_block(win, (int)floor(p->x) - ox,
                                       (int)floor(p->y),
                                       (int)floor(p->z) - oz);
                if (id != 8 && id != 9) expired = 1;
            }
            if (expired) {
                p->active = 0;
                live->count--;
            }
            continue;
        }
        if (p->kind == GM_LIVE_PARTICLE_WATER_SPLASH) {
            p->prev_x = p->x;
            p->prev_y = p->y;
            p->prev_z = p->z;
            ++p->age;
            p->motion_y -= (double)p->gravity;
            pl_move(p, win, ox, oz, p->motion_x, p->motion_y, p->motion_z);
            p->motion_x *= 0.9800000190734863;
            p->motion_y *= 0.9800000190734863;
            p->motion_z *= 0.9800000190734863;
            int expired = p->max_age-- <= 0;
            if (p->on_ground) {
                if (pl_rng_double(live) < 0.5) expired = 1;
                p->motion_x *= 0.699999988079071;
                p->motion_z *= 0.699999988079071;
            }
            if (win) {
                int bx = (int)floor(p->x) - ox;
                int by = (int)floor(p->y);
                int bz = (int)floor(p->z) - oz;
                int id = psv_get_block(win, bx, by, bz);
                if (id == 8 || id == 9 || id == 10 || id == 11
                        || psv_solid(id)) {
                    double height = 1.0;
                    if (id >= 8 && id <= 11) {
                        int meta = psv_get_meta(win, bx, by, bz);
                        if (meta >= 8) meta = 0;
                        height = 1.0 - (double)((float)(meta + 1) / 9.0f);
                    }
                    if (p->y < (double)by + height) expired = 1;
                }
            }
            if (expired) {
                p->active = 0;
                live->count--;
            }
            continue;
        }
        if (p->kind == GM_LIVE_PARTICLE_SPELL_MOB) {
            p->prev_x = p->x;
            p->prev_y = p->y;
            p->prev_z = p->z;
            int expired = p->age++ >= p->max_age;
            p->texture_index = p->texture_base + 7
                - p->age * 8 / p->max_age;
            p->motion_y += 0.004;
            pl_move(p, win, ox, oz,
                    p->motion_x, p->motion_y, p->motion_z);
            if (p->y == p->prev_y) {
                p->motion_x *= 1.1;
                p->motion_z *= 1.1;
            }
            p->motion_x *= 0.9599999785423279;
            p->motion_y *= 0.9599999785423279;
            p->motion_z *= 0.9599999785423279;
            if (p->on_ground) {
                p->motion_x *= 0.699999988079071;
                p->motion_z *= 0.699999988079071;
            }
            if (expired) {
                p->active = 0;
                --live->count;
            }
            continue;
        }
        if (p->kind == GM_LIVE_PARTICLE_SMOKE_LARGE
                || p->kind == GM_LIVE_PARTICLE_SMOKE_NORMAL) {
            p->prev_x = p->x;
            p->prev_y = p->y;
            p->prev_z = p->z;
            int expired = p->age++ >= p->max_age;
            p->texture_index = 7 - p->age * 8 / p->max_age;
            p->motion_y += 0.004;
            pl_move(p, win, ox, oz,
                    p->motion_x, p->motion_y, p->motion_z);
            if (p->y == p->prev_y) {
                p->motion_x *= 1.1;
                p->motion_z *= 1.1;
            }
            p->motion_x *= 0.9599999785423279;
            p->motion_y *= 0.9599999785423279;
            p->motion_z *= 0.9599999785423279;
            if (p->on_ground) {
                p->motion_x *= 0.699999988079071;
                p->motion_z *= 0.699999988079071;
            }
            if (expired) {
                p->active = 0;
                --live->count;
            }
            continue;
        }
        if (p->kind == GM_LIVE_PARTICLE_HEART) {
            p->prev_x = p->x;
            p->prev_y = p->y;
            p->prev_z = p->z;
            int expired = p->age++ >= p->max_age;
            pl_move(p, win, ox, oz,
                    p->motion_x, p->motion_y, p->motion_z);
            if (p->y == p->prev_y) {
                p->motion_x *= 1.1;
                p->motion_z *= 1.1;
            }
            p->motion_x *= 0.8600000143051147;
            p->motion_y *= 0.8600000143051147;
            p->motion_z *= 0.8600000143051147;
            if (p->on_ground) {
                p->motion_x *= 0.699999988079071;
                p->motion_z *= 0.699999988079071;
            }
            if (expired) {
                p->active = 0;
                --live->count;
            }
            continue;
        }
        if (p->kind == GM_LIVE_PARTICLE_NOTE) {
            p->prev_x = p->x;
            p->prev_y = p->y;
            p->prev_z = p->z;
            int expired = p->age++ >= p->max_age;
            pl_move(p, win, ox, oz,
                    p->motion_x, p->motion_y, p->motion_z);
            if (p->y == p->prev_y) {
                p->motion_x *= 1.1;
                p->motion_z *= 1.1;
            }
            p->motion_x *= 0.6600000262260437;
            p->motion_y *= 0.6600000262260437;
            p->motion_z *= 0.6600000262260437;
            if (p->on_ground) {
                p->motion_x *= 0.699999988079071;
                p->motion_z *= 0.699999988079071;
            }
            if (expired) {
                p->active = 0;
                --live->count;
            }
            continue;
        }
        p->prev_x = p->x;
        p->prev_y = p->y;
        p->prev_z = p->z;
        if (p->kind == GM_LIVE_PARTICLE_EXPLOSION_LARGE ||
            p->kind == GM_LIVE_PARTICLE_EXPLOSION_HUGE) {
            if (++p->age == p->max_age) {
                p->active = 0;
                live->count--;
            }
            continue;
        }
        if (p->kind == GM_LIVE_PARTICLE_EXPLOSION_NORMAL
                || p->kind == GM_LIVE_PARTICLE_SPIT) {
            int expired = p->age++ >= p->max_age;
            p->motion_y += 0.004;
            pl_move(p, win, ox, oz, p->motion_x, p->motion_y, p->motion_z);
            p->motion_x *= 0.8999999761581421;
            p->motion_y *= 0.8999999761581421;
            p->motion_z *= 0.8999999761581421;
            if (p->kind == GM_LIVE_PARTICLE_SPIT)
                p->motion_y -= 0.004 + 0.04 * 0.5;
            if (p->on_ground) {
                p->motion_x *= 0.699999988079071;
                p->motion_z *= 0.699999988079071;
            }
            if (expired) {
                p->active = 0;
                live->count--;
            }
            continue;
        }
        if (p->kind == GM_LIVE_PARTICLE_SWEEP_ATTACK) {
            if (++p->age == 4) {
                p->active = 0;
                --live->count;
            }
            continue;
        }
        if (p->kind == GM_LIVE_PARTICLE_CRIT
                || p->kind == GM_LIVE_PARTICLE_CRIT_MAGIC
                || p->kind == GM_LIVE_PARTICLE_DAMAGE_INDICATOR) {
            int expired = p->age++ >= p->max_age;
            pl_move(p, win, ox, oz, p->motion_x, p->motion_y, p->motion_z);
            p->color_g = (float)((double)p->color_g * 0.96);
            p->color_b = (float)((double)p->color_b * 0.9);
            p->motion_x *= 0.699999988079071;
            p->motion_y *= 0.699999988079071;
            p->motion_z *= 0.699999988079071;
            p->motion_y -= 0.019999999552965164;
            if (p->on_ground) {
                p->motion_x *= 0.699999988079071;
                p->motion_z *= 0.699999988079071;
            }
            if (expired) {
                p->active = 0;
                --live->count;
            }
            continue;
        }
        int expired = p->age++ >= p->max_age;
        p->motion_y -= 0.04 * (double)1.0f;
        pl_move(p, win, ox, oz, p->motion_x, p->motion_y, p->motion_z);
        p->motion_x *= 0.9800000190734863;
        p->motion_y *= 0.9800000190734863;
        p->motion_z *= 0.9800000190734863;
        if (p->on_ground) {
            p->motion_x *= 0.699999988079071;
            p->motion_z *= 0.699999988079071;
        }
        if (expired) {
            p->active = 0;
            live->count--;
        }
    }
    for (int i = 0; i < GM_PARTICLE_EMITTER_CAP; ++i) {
        GmLiveParticleEmitter *emitter = &live->emitters[i];
        if (!emitter->active) continue;
        if (emitter->newborn) {
            emitter->newborn = 0;
            continue;
        }
        (void)pl_update_emitter(live, emitter, 0);
    }
}

static int pl_emit_billboard(double x, double y, double z, float half,
                             float u0, float v0, float u1, float v1,
                             CrRgba tint, float cy, float sy, float cp, float sp,
                             CrVertex *out, int max) {
    if (max < 6) return 0;
    static const float corners[4][2] = {
        { -1.0f, -1.0f }, { 1.0f, -1.0f },
        { 1.0f, 1.0f }, { -1.0f, 1.0f }
    };
    static const int tris[6] = { 0, 1, 2, 0, 2, 3 };
    float us[4] = { u0, u1, u1, u0 };
    float vs[4] = { v1, v1, v0, v0 };
    CrVertex quad[4];
    for (int i = 0; i < 4; ++i) {
        float px = corners[i][0] * half;
        float py = corners[i][1] * half;
        float pz = 0.0f;
        float ty = py * cp - pz * sp;
        float tz = py * sp + pz * cp;
        py = ty; pz = tz;
        float tx = px * cy + pz * sy;
        tz = -px * sy + pz * cy;
        quad[i].pos.x = (float)x + tx;
        quad[i].pos.y = (float)y + py;
        quad[i].pos.z = (float)z + tz;
        quad[i].uv.x = us[i];
        quad[i].uv.y = vs[i];
        quad[i].light = 1.0f;
        quad[i].blk = 15.0f;
        quad[i].tint = tint;
        quad[i].ao = 1.0f;
    }
    for (int i = 0; i < 6; ++i) out[i] = quad[tris[i]];
    return 6;
}

static float pl_clamp01(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

int gm_particles_live_emit(const GmParticlesLive *live, float partial_ticks,
                           float view_yaw, float view_pitch,
                           CrVertex *out, int max) {
    if (!live || !out || max < 6) return 0;
    float yr = (180.0f - view_yaw) * PL_DEG2RAD;
    float pr = -view_pitch * PL_DEG2RAD;
    float cy = cosf(yr), sy = sinf(yr);
    float cp = cosf(pr), sp = sinf(pr);
    int written = 0;
    for (int i = 0; i < GM_PARTICLES_LIVE_CAP; ++i) {
        const GmLiveParticle *p = &live->particles[i];
        if (!p->active || p->kind != GM_LIVE_PARTICLE_BLOCK ||
            written + 6 > max) continue;
        float bu0, bv0, bu1, bv1;
        bm_sprite_uv(bm_particle_sprite(p->model_key),
                     &bu0, &bv0, &bu1, &bv1);
        float du = bu1 - bu0, dv = bv1 - bv0;
        float u0 = bu0 + (p->jitter_x / 4.0f) * du;
        float u1 = bu0 + ((p->jitter_x + 1.0f) / 4.0f) * du;
        float v0 = bv0 + (p->jitter_y / 4.0f) * dv;
        float v1 = bv0 + ((p->jitter_y + 1.0f) / 4.0f) * dv;
        double x = p->prev_x + (p->x - p->prev_x) * (double)partial_ticks;
        double y = p->prev_y + (p->y - p->prev_y) * (double)partial_ticks;
        double z = p->prev_z + (p->z - p->prev_z) * (double)partial_ticks;
        /* particleRed = 0.6 * colorMul; VertexBuffer.color * lightmap. */
        CrRgba tint = {
            (u8)(0.6f * 255.0f * pl_clamp01(p->base_r) * pl_clamp01(p->lm_r) + 0.5f),
            (u8)(0.6f * 255.0f * pl_clamp01(p->base_g) * pl_clamp01(p->lm_g) + 0.5f),
            (u8)(0.6f * 255.0f * pl_clamp01(p->base_b) * pl_clamp01(p->lm_b) + 0.5f),
            255
        };
        written += pl_emit_billboard(x, y, z, 0.1f * p->scale,
                                     u0, v0, u1, v1, tint,
                                     cy, sy, cp, sp,
                                     out + written, max - written);
    }
    return written;
}

static void pl_recorded_uv(int kind, int frame, float *u0, float *v0,
                           float *u1, float *v1) {
    const CrMobSprite *sp = &CR_MOB_SPRITES[
        kind == GM_LIVE_PARTICLE_EXPLOSION_NORMAL ?
        CR_MOB_PARTICLES : CR_MOB_EXPLOSION];
    float aw = (float)CR_MOB_ATLAS_W, ah = (float)CR_MOB_ATLAS_H;
    float bx = (float)sp->x0 / aw, by = (float)sp->y0 / ah;
    float su = (float)sp->w / aw, sv = (float)sp->h / ah;
    if (kind == GM_LIVE_PARTICLE_EXPLOSION_NORMAL) {
        int ix = frame % 16, iy = frame / 16;
        *u0 = bx + ((float)ix / 16.0f) * su;
        *u1 = bx + ((float)ix / 16.0f + 0.0624375f) * su;
        *v0 = by + ((float)iy / 16.0f) * sv;
        *v1 = by + ((float)iy / 16.0f + 0.0624375f) * sv;
    } else {
        if (frame < 0) frame = 0;
        if (frame > 15) frame = 15;
        float fu = (float)(frame % 4) / 4.0f;
        float fv = (float)(frame / 4) / 4.0f;
        *u0 = bx + fu * su;
        *u1 = bx + (fu + 0.24975f) * su;
        *v0 = by + fv * sv;
        *v1 = by + (fv + 0.24975f) * sv;
    }
}

int gm_particles_live_emit_water(const GmParticlesLive *live,
                                 float partial_ticks, float view_yaw,
                                 float view_pitch, CrVertex *out, int max) {
    if (!live || !out || max < 6) return 0;
    float yr = (180.0f - view_yaw) * PL_DEG2RAD;
    float pr = -view_pitch * PL_DEG2RAD;
    float cy = cosf(yr), sy = sinf(yr);
    float cp = cosf(pr), sp = sinf(pr);
    int written = 0;
    for (int i = 0; i < GM_PARTICLES_LIVE_CAP; ++i) {
        const GmLiveParticle *p = &live->particles[i];
        if (!p->active || (p->kind != GM_LIVE_PARTICLE_WATER_BUBBLE
                && p->kind != GM_LIVE_PARTICLE_WATER_SPLASH)
                || written + 6 > max)
            continue;
        float u0, v0, u1, v1;
        pl_recorded_uv(GM_LIVE_PARTICLE_EXPLOSION_NORMAL,
                       p->texture_index, &u0, &v0, &u1, &v1);
        double x = p->prev_x + (p->x - p->prev_x) * (double)partial_ticks;
        double y = p->prev_y + (p->y - p->prev_y) * (double)partial_ticks;
        double z = p->prev_z + (p->z - p->prev_z) * (double)partial_ticks;
        int start = written;
        CrRgba tint = {255, 255, 255, 255};
        written += pl_emit_billboard(x, y, z, 0.1f * p->scale,
                                     u0, v0, u1, v1, tint,
                                     cy, sy, cp, sp,
                                     out + written, max - written);
        for (int v = start; v < written; ++v) {
            out[v].light = p->lm_r;
            out[v].blk = p->lm_g;
        }
    }
    return written;
}

int gm_particles_live_emit_spell(const GmParticlesLive *live,
                                 float partial_ticks, float view_yaw,
                                 float view_pitch, CrVertex *out, int max) {
    if (!live || !out || max < 6) return 0;
    float yr = (180.0f - view_yaw) * PL_DEG2RAD;
    float pr = -view_pitch * PL_DEG2RAD;
    float cy = cosf(yr), sy = sinf(yr);
    float cp = cosf(pr), sp = sinf(pr);
    int written = 0;
    for (int i = 0; i < GM_PARTICLES_LIVE_CAP; ++i) {
        const GmLiveParticle *p = &live->particles[i];
        if (!p->active || p->kind != GM_LIVE_PARTICLE_SPELL_MOB
                || written + 6 > max)
            continue;
        float u0, v0, u1, v1;
        pl_recorded_uv(GM_LIVE_PARTICLE_EXPLOSION_NORMAL,
                       p->texture_index, &u0, &v0, &u1, &v1);
        double x = p->prev_x + (p->x - p->prev_x) * (double)partial_ticks;
        double y = p->prev_y + (p->y - p->prev_y) * (double)partial_ticks;
        double z = p->prev_z + (p->z - p->prev_z) * (double)partial_ticks;
        CrRgba tint = {
            (u8)(pl_clamp01(p->color_r) * 255.0f + 0.5f),
            (u8)(pl_clamp01(p->color_g) * 255.0f + 0.5f),
            (u8)(pl_clamp01(p->color_b) * 255.0f + 0.5f),
            255
        };
        int start = written;
        written += pl_emit_billboard(
            x, y, z, 0.1f * p->scale,
            u0, v0, u1, v1, tint, cy, sy, cp, sp,
            out + written, max - written);
        for (int v = start; v < written; ++v) {
            out[v].light = p->lm_r;
            out[v].blk = p->lm_g;
        }
    }
    return written;
}

int gm_particles_live_emit_smoke(const GmParticlesLive *live,
                                 float partial_ticks, float view_yaw,
                                 float view_pitch, CrVertex *out, int max) {
    if (!live || !out || max < 6) return 0;
    float yr = (180.0f - view_yaw) * PL_DEG2RAD;
    float pr = -view_pitch * PL_DEG2RAD;
    float cy = cosf(yr), sy = sinf(yr);
    float cp = cosf(pr), sp = sinf(pr);
    int written = 0;
    for (int i = 0; i < GM_PARTICLES_LIVE_CAP; ++i) {
        const GmLiveParticle *p = &live->particles[i];
        if (!p->active || (p->kind != GM_LIVE_PARTICLE_SMOKE_LARGE
                && p->kind != GM_LIVE_PARTICLE_SMOKE_NORMAL)
                || written + 6 > max)
            continue;
        float u0, v0, u1, v1;
        pl_recorded_uv(GM_LIVE_PARTICLE_EXPLOSION_NORMAL,
                       p->texture_index, &u0, &v0, &u1, &v1);
        double x = p->prev_x + (p->x - p->prev_x) * (double)partial_ticks;
        double y = p->prev_y + (p->y - p->prev_y) * (double)partial_ticks;
        double z = p->prev_z + (p->z - p->prev_z) * (double)partial_ticks;
        float progress = ((float)p->age + partial_ticks)
            / (float)p->max_age * 32.0f;
        if (progress < 0.0f) progress = 0.0f;
        if (progress > 1.0f) progress = 1.0f;
        float scale = p->original_scale * progress;
        /* VertexBuffer.color(float...) truncates each primary channel before
         * storing UBYTE vertex data. Rounding here makes half of the grayscale
         * smoke texels one value too bright. */
        CrRgba tint = {
            (u8)(pl_clamp01(p->color_r) * 255.0f),
            (u8)(pl_clamp01(p->color_g) * 255.0f),
            (u8)(pl_clamp01(p->color_b) * 255.0f),
            255
        };
        int start = written;
        written += pl_emit_billboard(
            x, y, z, 0.1f * scale, u0, v0, u1, v1, tint,
            cy, sy, cp, sp, out + written, max - written);
        for (int v = start; v < written; ++v) {
            out[v].light = p->lm_r;
            out[v].blk = p->lm_g;
        }
    }
    return written;
}

int gm_particles_live_emit_heart(const GmParticlesLive *live,
                                 float partial_ticks, float view_yaw,
                                 float view_pitch, CrVertex *out, int max) {
    if (!live || !out || max < 6) return 0;
    float yr = (180.0f - view_yaw) * PL_DEG2RAD;
    float pr = -view_pitch * PL_DEG2RAD;
    float cy = cosf(yr), sy = sinf(yr);
    float cp = cosf(pr), sp = sinf(pr);
    int written = 0;
    for (int i = 0; i < GM_PARTICLES_LIVE_CAP; ++i) {
        const GmLiveParticle *p = &live->particles[i];
        if (!p->active || p->kind != GM_LIVE_PARTICLE_HEART
                || written + 6 > max)
            continue;
        float u0, v0, u1, v1;
        pl_recorded_uv(GM_LIVE_PARTICLE_EXPLOSION_NORMAL,
                       p->texture_index, &u0, &v0, &u1, &v1);
        double x = p->prev_x + (p->x - p->prev_x) * (double)partial_ticks;
        double y = p->prev_y + (p->y - p->prev_y) * (double)partial_ticks;
        double z = p->prev_z + (p->z - p->prev_z) * (double)partial_ticks;
        float progress = ((float)p->age + partial_ticks)
            / (float)p->max_age * 32.0f;
        float scale = p->original_scale * pl_clamp01(progress);
        int start = written;
        written += pl_emit_billboard(
            x, y, z, 0.1f * scale, u0, v0, u1, v1,
            (CrRgba){255, 255, 255, 255}, cy, sy, cp, sp,
            out + written, max - written);
        for (int v = start; v < written; ++v) {
            out[v].light = p->lm_r;
            out[v].blk = p->lm_g;
        }
    }
    return written;
}

int gm_particles_live_emit_portal(const GmParticlesLive *live,
                                  float partial_ticks, float view_yaw,
                                  float view_pitch, CrVertex *out, int max) {
    if (!live || !out || max < 6) return 0;
    float yr = (180.0f - view_yaw) * PL_DEG2RAD;
    float pr = -view_pitch * PL_DEG2RAD;
    float cy = cosf(yr), sy = sinf(yr);
    float cp = cosf(pr), sp = sinf(pr);
    int written = 0;
    for (int i = 0; i < GM_PARTICLES_LIVE_CAP; ++i) {
        const GmLiveParticle *p = &live->particles[i];
        if (!p->active || p->kind != GM_LIVE_PARTICLE_PORTAL
                || written + 6 > max)
            continue;
        float u0, v0, u1, v1;
        pl_recorded_uv(GM_LIVE_PARTICLE_EXPLOSION_NORMAL,
                       p->texture_index, &u0, &v0, &u1, &v1);
        double x = p->prev_x + (p->x - p->prev_x) * (double)partial_ticks;
        double y = p->prev_y + (p->y - p->prev_y) * (double)partial_ticks;
        double z = p->prev_z + (p->z - p->prev_z) * (double)partial_ticks;
        float phase = ((float)p->age + partial_ticks) / (float)p->max_age;
        float scale = p->original_scale * (1.0f - (1.0f - phase)
            * (1.0f - phase));
        CrRgba tint = {
            (u8)(pl_clamp01(p->color_r) * 255.0f),
            (u8)(pl_clamp01(p->color_g) * 255.0f),
            (u8)(pl_clamp01(p->color_b) * 255.0f),
            255
        };
        int start = written;
        written += pl_emit_billboard(
            x, y, z, 0.1f * scale, u0, v0, u1, v1, tint,
            cy, sy, cp, sp, out + written, max - written);
        float brightness_phase = (float)p->age / (float)p->max_age;
        brightness_phase *= brightness_phase;
        brightness_phase *= brightness_phase;
        float block_light = p->lm_g + brightness_phase * 15.0f;
        if (block_light > 15.0f) block_light = 15.0f;
        for (int v = start; v < written; ++v) {
            out[v].light = p->lm_r;
            out[v].blk = block_light;
        }
    }
    return written;
}

static int pl_emit_sweep(const GmLiveParticle *p, int frame,
                         float view_yaw, float view_pitch,
                         CrVertex *out, int max) {
    if (max < 6 || frame < 0 || frame > 7) return 0;
    const CrMobSprite *sprite = &CR_MOB_SPRITES[CR_MOB_SWEEP];
    float aw = (float)CR_MOB_ATLAS_W, ah = (float)CR_MOB_ATLAS_H;
    float span_u = (float)sprite->w / aw;
    float span_v = (float)sprite->h / ah;
    float base_u = (float)sprite->x0 / aw;
    float base_v = (float)sprite->y0 / ah;
    float u0 = base_u + ((float)(frame % 4) / 4.0f) * span_u;
    float u1 = base_u + ((float)(frame % 4) / 4.0f + 0.24975f)
        * span_u;
    float v0 = base_v + ((float)(frame / 2) / 2.0f) * span_v;
    float v1 = base_v + ((float)(frame / 2) / 2.0f + 0.4995f)
        * span_v;

    float yaw = view_yaw * PL_DEG2RAD;
    float pitch = view_pitch * PL_DEG2RAD;
    float rotation_x = cosf(yaw);
    float rotation_z = sinf(yaw);
    float rotation_yz = -rotation_z * sinf(pitch);
    float rotation_xy = rotation_x * sinf(pitch);
    float rotation_xz = cosf(pitch);
    float size = p->scale;
    float px = (float)p->x, py = (float)p->y, pz = (float)p->z;
    float positions[4][3] = {
        {px - rotation_x * size - rotation_xy * size,
         py - rotation_z * size * 0.5f,
         pz - rotation_yz * size - rotation_xz * size},
        {px - rotation_x * size + rotation_xy * size,
         py + rotation_z * size * 0.5f,
         pz - rotation_yz * size + rotation_xz * size},
        {px + rotation_x * size + rotation_xy * size,
         py + rotation_z * size * 0.5f,
         pz + rotation_yz * size + rotation_xz * size},
        {px + rotation_x * size - rotation_xy * size,
         py - rotation_z * size * 0.5f,
         pz + rotation_yz * size - rotation_xz * size}
    };
    float us[4] = {u1, u1, u0, u0};
    float vs[4] = {v1, v0, v0, v1};
    static const int indices[6] = {0, 1, 2, 0, 2, 3};
    u8 gray = (u8)(pl_clamp01(p->gray) * 255.0f + 0.5f);
    for (int i = 0; i < 6; ++i) {
        int q = indices[i];
        out[i].pos.x = positions[q][0];
        out[i].pos.y = positions[q][1];
        out[i].pos.z = positions[q][2];
        out[i].uv.x = us[q];
        out[i].uv.y = vs[q];
        out[i].light = 0.0f;
        out[i].blk = 15.0f;
        out[i].tint = (CrRgba){gray, gray, gray, 255};
        out[i].ao = 1.0f;
    }
    return 6;
}

int gm_particles_live_emit_combat(const GmParticlesLive *live, int fx_layer,
                                  float partial_ticks, float view_yaw,
                                  float view_pitch, CrVertex *out, int max) {
    if (!live || !out || max < 6 || (fx_layer != 0 && fx_layer != 3))
        return 0;
    float yr = (180.0f - view_yaw) * PL_DEG2RAD;
    float pr = -view_pitch * PL_DEG2RAD;
    float cy = cosf(yr), sy = sinf(yr);
    float cp = cosf(pr), sp = sinf(pr);
    int written = 0;
    for (int i = 0; i < GM_PARTICLES_LIVE_CAP; ++i) {
        const GmLiveParticle *p = &live->particles[i];
        if (!p->active || written + 6 > max) continue;
        if (p->kind == GM_LIVE_PARTICLE_SWEEP_ATTACK) {
            if (fx_layer != 3) continue;
            int frame = (int)(((float)p->age + partial_ticks) * 3.0f / 4.0f);
            written += pl_emit_sweep(
                p, frame, view_yaw, view_pitch,
                out + written, max - written);
            continue;
        }
        if (fx_layer != 0 || (p->kind != GM_LIVE_PARTICLE_NOTE
                && p->kind != GM_LIVE_PARTICLE_CRIT
                && p->kind != GM_LIVE_PARTICLE_CRIT_MAGIC
                && p->kind != GM_LIVE_PARTICLE_DAMAGE_INDICATOR))
            continue;
        float u0, v0, u1, v1;
        pl_recorded_uv(GM_LIVE_PARTICLE_EXPLOSION_NORMAL,
                       p->texture_index, &u0, &v0, &u1, &v1);
        double x = p->prev_x + (p->x - p->prev_x) * (double)partial_ticks;
        double y = p->prev_y + (p->y - p->prev_y) * (double)partial_ticks;
        double z = p->prev_z + (p->z - p->prev_z) * (double)partial_ticks;
        float life_scale = ((float)p->age + partial_ticks)
            / (float)p->max_age * 32.0f;
        life_scale = pl_clamp01(life_scale);
        CrRgba tint = {
            (u8)(pl_clamp01(p->color_r) * 255.0f + 0.5f),
            (u8)(pl_clamp01(p->color_g) * 255.0f + 0.5f),
            (u8)(pl_clamp01(p->color_b) * 255.0f + 0.5f),
            255
        };
        int start = written;
        written += pl_emit_billboard(
            x, y, z, 0.1f * p->original_scale * life_scale,
            u0, v0, u1, v1, tint, cy, sy, cp, sp,
            out + written, max - written);
        for (int v = start; v < written; ++v) {
            out[v].light = p->lm_r;
            out[v].blk = p->lm_g;
        }
    }
    return written;
}

int gm_particles_live_emit_recorded(const GmParticlesLive *live, int fx_layer,
                                    float partial_ticks, float view_yaw,
                                    float view_pitch, CrVertex *out, int max) {
    if (!live || !out || max < 6 || (fx_layer != 0 && fx_layer != 3)) return 0;
    int wanted = fx_layer == 0 ? GM_LIVE_PARTICLE_EXPLOSION_NORMAL :
                                 GM_LIVE_PARTICLE_EXPLOSION_LARGE;
    float yr = (180.0f - view_yaw) * PL_DEG2RAD;
    float pr = -view_pitch * PL_DEG2RAD;
    float cy = cosf(yr), sy = sinf(yr);
    float cp = cosf(pr), sp = sinf(pr);
    int written = 0;
    for (int i = 0; i < GM_PARTICLES_LIVE_CAP; ++i) {
        const GmLiveParticle *p = &live->particles[i];
        if (!p->active
                || (p->kind != wanted
                    && !(wanted == GM_LIVE_PARTICLE_EXPLOSION_NORMAL
                        && p->kind == GM_LIVE_PARTICLE_SPIT))
                || written + 6 > max) continue;
        if (!p->recorded_exact &&
            wanted == GM_LIVE_PARTICLE_EXPLOSION_NORMAL
            && p->kind != GM_LIVE_PARTICLE_SPIT &&
            p->age > PL_LEGACY_RECORDED_NORMAL_MAX_RENDER_AGE) continue;
        if (!p->recorded_exact &&
            wanted == GM_LIVE_PARTICLE_EXPLOSION_LARGE &&
            p->age > PL_LEGACY_RECORDED_LARGE_MAX_RENDER_AGE) continue;
        int frame;
        float half;
        if (wanted == GM_LIVE_PARTICLE_EXPLOSION_NORMAL) {
            frame = p->age == 0 ? 0 : 7 - p->age * 8 / p->max_age;
            if (frame < 0) frame = 0;
            half = 0.1f * p->scale;
        } else {
            frame = (int)(((float)p->age + partial_ticks) * 15.0f /
                          (float)p->max_age);
            half = 2.0f * p->scale;
        }
        float u0, v0, u1, v1;
        pl_recorded_uv(wanted, frame, &u0, &v0, &u1, &v1);
        double x = p->prev_x + (p->x - p->prev_x) * (double)partial_ticks;
        double y = p->prev_y + (p->y - p->prev_y) * (double)partial_ticks;
        double z = p->prev_z + (p->z - p->prev_z) * (double)partial_ticks;
        u8 g = (u8)(pl_clamp01(p->gray) * 255.0f + 0.5f);
        CrRgba tint = { g, g, g, 255 };
        int start = written;
        written += pl_emit_billboard(x, y, z, half, u0, v0, u1, v1, tint,
                                     cy, sy, cp, sp,
                                     out + written, max - written);
        if (wanted == GM_LIVE_PARTICLE_EXPLOSION_NORMAL)
            for (int v = start; v < written; ++v) {
                out[v].light = p->lm_r;
                out[v].blk = p->lm_g;
            }
    }
    return written;
}
