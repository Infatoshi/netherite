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
/* pcl records ParticleManager.spawnEffectParticle arguments, but
 * ParticleExplosion adds an unrecorded uniform +/-0.05 velocity. With 0.9
 * drag, the worst missing displacement is 0.095 after two updates, still
 * inside the minimum 0.1 billboard half-width; after three it is 0.1355 and
 * the sprite location is no longer bounded. Keep the exact vanilla lifetime
 * and suppression state, but only draw NORMAL while its taped kinematics
 * still locate even the smallest possible sprite. */
#define PL_RECORDED_NORMAL_MAX_RENDER_AGE 2
/* LARGE has an unrecorded random maxAge in [6,9]. Ages 0..5 are the only
 * frames guaranteed to exist for every captured spawn, so rendering later
 * would invent survival for particles whose constructor RNG is unavailable. */
#define PL_RECORDED_LARGE_MAX_RENDER_AGE 5

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

void gm_particles_live_seed(GmParticlesLive *live, uint64_t seed) {
    if (!live) return;
    live->rng = seed ? seed : UINT64_C(0x9e3779b97f4a7c15);
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

int gm_particles_live_suppresses_explosion(const GmParticlesLive *live) {
    if (!live) return 0;
    for (int i = 0; i < GM_PARTICLES_LIVE_CAP; ++i)
        if (live->particles[i].active &&
            live->particles[i].kind != GM_LIVE_PARTICLE_BLOCK)
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
        if (p->kind == GM_LIVE_PARTICLE_EXPLOSION_NORMAL) {
            int expired = p->age++ >= p->max_age;
            p->motion_y += 0.004;
            pl_move(p, win, ox, oz, p->motion_x, p->motion_y, p->motion_z);
            p->motion_x *= 0.8999999761581421;
            p->motion_y *= 0.8999999761581421;
            p->motion_z *= 0.8999999761581421;
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
        if (!p->active || p->kind != wanted || written + 6 > max) continue;
        if (wanted == GM_LIVE_PARTICLE_EXPLOSION_NORMAL &&
            p->age > PL_RECORDED_NORMAL_MAX_RENDER_AGE) continue;
        if (wanted == GM_LIVE_PARTICLE_EXPLOSION_LARGE &&
            p->age > PL_RECORDED_LARGE_MAX_RENDER_AGE) continue;
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
