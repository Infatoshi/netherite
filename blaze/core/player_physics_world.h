/* player_physics_world: player tick physics (EntityLivingBase.onLivingUpdate +
 * moveEntityWithHeading) with Entity.move collision (core/physics_collision_math.h) over a REAL
 * chunk_provider-generated ChunkPrimer for chunk (0,0). Internal verify: CPU==CUDA on seeds
 * 12345/0/7.
 *
 * READ-ONLY deps: physics_collision_math.h (mc_entity_move), chunk_provider.h (cp_provide_chunk).
 * Does NOT edit those headers; the driver copies the chunk_provider.c pattern (local primer +
 * CpScratch + McSinTable -> cp_provide_chunk).
 *
 * SCOPE v1 (internal fidelity, not vanilla-bit-exact):
 *   - One player-sized AABB (mc_pcm_player_box), stepHeight=0, no sneak step-back / web / piston.
 *   - Full-cube collision for all non-air non-liquid CB_* blocks in chunk (0,0).
 *   - Slipperiness table keyed by CB_* (default 0.6, CB_ICE 0.98).
 *   - Deterministic per-tick RL-style inputs from mc_hash_seed(tick, purpose) - order-independent.
 *   - Fixed spawn (8.5, 90.0, 8.5), PPW_NUM_TICKS ticks, dump pos+velocity each tick.
 *
 * TRIMMED (provably inactive or deferred): EntityPlayer sneak, stepHeight auto-step, liquids as
 * pass-through (no swim), sound/fire/fall side effects. */
#ifndef MC_PLAYER_PHYSICS_WORLD_H
#define MC_PLAYER_PHYSICS_WORLD_H

#include <math.h>
#include "physics_collision_math.h"
#include "chunk_provider.h"
#include "mc_math.h"
#include "mc_rng.h"

#define PPW_NUM_TICKS       20
#define PPW_MAX_BLOCKS      512
#define PPW_SPAWN_X         8.5
#define PPW_SPAWN_Y         90.0
#define PPW_SPAWN_Z         8.5
#define PPW_AI_MOVE_SPEED   0.1f
#define PPW_JUMP_FACTOR     0.02f
#define PPW_PURPOSE_INPUT   0x50505701u

typedef struct {
    float forward;   /* [-1, 1] */
    float strafe;    /* [-1, 1] */
    float yaw;       /* degrees */
    float pitch;     /* degrees (unused in v1) */
    int   jump;      /* 0 or 1 */
} PpwAction;

/* CB_* blocks that contribute a full-cube collision box (liquids/air are pass-through). */
MC_HD static inline int ppw_cb_solid(int id) {
    if (id == CB_AIR) return 0;
    if (id == CB_WATER || id == CB_FLOWING_WATER) return 0;
    if (id == CB_LAVA || id == CB_FLOWING_LAVA) return 0;
    return 1;
}

MC_HD static inline float ppw_cb_slipperiness(int id) {
    if (id == CB_ICE) return 0.98f;
    return 0.6f;
}

MC_HD static inline int ppw_get_block(const ChunkPrimer *p, int wx, int wy, int wz) {
    if (wx < 0 || wx > 15 || wz < 0 || wz > 15 || wy < 0 || wy > 255) return CB_AIR;
    return cb_get(p, wx, wy, wz);
}

/* Collect solid block AABBs in [x0..x1]x[y0..y1]x[z0..z1] intersecting query (clamped to chunk). */
MC_HD static inline int ppw_collect_blocks(const ChunkPrimer *p, const McAABB *query,
                                           McAABB *blocks, int maxblocks) {
    int n = 0;
    int x0 = mc_floor(query->minX);
    int x1 = mc_floor(query->maxX);
    int y0 = mc_floor(query->minY);
    int y1 = mc_floor(query->maxY);
    int z0 = mc_floor(query->minZ);
    int z1 = mc_floor(query->maxZ);
    if (x0 < 0) x0 = 0;
    if (x1 > 15) x1 = 15;
    if (z0 < 0) z0 = 0;
    if (z1 > 15) z1 = 15;
    if (y0 < 0) y0 = 0;
    if (y1 > 255) y1 = 255;
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) {
                int bid = ppw_get_block(p, x, y, z);
                if (!ppw_cb_solid(bid)) continue;
                if (n >= maxblocks) return n;
                blocks[n++] = mc_aabb_make((double)x, (double)y, (double)z,
                                           (double)x + 1.0, (double)y + 1.0, (double)z + 1.0);
            }
    return n;
}

/* Entity.moveFlying(strafe, forward, accel) using MathHelper sin/cos table. */
MC_HD static inline void ppw_move_flying(const McSinTable *st, McEntity *e, float yaw,
                                         float strafe, float forward, float accel) {
    float f3 = strafe * strafe + forward * forward;
    if (f3 >= 1.0e-4f) {
        f3 = (float)sqrt((double)f3);
        if (f3 < 1.0f) f3 = 1.0f;
        f3 = accel / f3;
        strafe *= f3;
        forward *= f3;
        /* vanilla EntityLivingBase.moveFlying: MathHelper.sin(rotationYaw * 0.017453292F).
         * mc_sin/mc_cos index the LUT with radians (factor 10430.378 = 65536/2pi), so yaw
         * (degrees) MUST be converted first -- without it forward-walk goes the wrong direction
         * (e.g. a spurious X drift at yaw=180). */
        float rad = yaw * 0.017453292f;
        float sinYaw = mc_sin(st, rad);
        float cosYaw = mc_cos(st, rad);
        e->motionX += (double)(strafe * cosYaw - forward * sinYaw);
        e->motionZ += (double)(forward * cosYaw + strafe * sinYaw);
    }
}

/* Hash-based deterministic per-tick input (order-independent, CPU==CUDA). */
MC_HD static inline PpwAction ppw_action_for_tick(i64 seed, int tick) {
    PpwAction a;
    u64 h0 = mc_hash_seed((u64)seed, tick, 8, 90, 8, PPW_PURPOSE_INPUT);
    u64 h1 = mc_hash64(h0 + 1ULL);
    u64 h2 = mc_hash64(h0 + 2ULL);
    u64 h3 = mc_hash64(h0 + 3ULL);
    a.forward = (float)(mc_hash_bound(h0, 3) - 1);
    a.strafe  = (float)(mc_hash_bound(h1, 3) - 1);
    a.yaw     = (float)(mc_hash_bound(h2, 24) * 15);
    a.pitch   = 0.0f;
    a.jump    = (mc_hash_bound(h3, 7) == 0) ? 1 : 0;
    return a;
}

/* One player physics tick: onLivingUpdate input + moveEntityWithHeading + mc_entity_move. */
MC_HD static inline void ppw_player_tick(const ChunkPrimer *primer, const McSinTable *st,
                                         McEntity *e, const PpwAction *act, McAABB *blocks) {
    float strafing = act->strafe * 0.98f;
    float forward  = act->forward * 0.98f;

    if (act->jump && e->onGround) {
        e->motionY = 0.41999998688697815;
    }

    float f2 = 0.91f;
    if (e->onGround) {
        int bx = mc_floor(e->posX);
        int by = mc_floor(e->box.minY) - 1;
        int bz = mc_floor(e->posZ);
        int bid = ppw_get_block(primer, bx, by, bz);
        /* Vanilla reads Block.slipperiness unconditionally when onGround (air = 0.6
         * default); a solidity gate diverges on ledges with air under floor(pos). */
        f2 = ppw_cb_slipperiness(bid) * 0.91f;
    }

    float f3 = 0.16277136f / (f2 * f2 * f2);
    float accel = e->onGround ? (PPW_AI_MOVE_SPEED * f3) : PPW_JUMP_FACTOR;

    ppw_move_flying(st, e, act->yaw, strafing, forward, accel);

    McAABB query = mc_aabb_addcoord(&e->box, e->motionX, e->motionY, e->motionZ);
    int nblocks = ppw_collect_blocks(primer, &query, blocks, PPW_MAX_BLOCKS);
    mc_entity_move(e, e->motionX, e->motionY, e->motionZ, blocks, nblocks);

    e->motionY -= 0.08;
    e->motionY *= 0.9800000190734863;
    e->motionX *= (double)f2;
    e->motionZ *= (double)f2;
}

/* Generate chunk (0,0) and simulate PPW_NUM_TICKS. out_pos_vel must hold nticks*6 doubles. */
MC_HD static inline void ppw_run(ChunkPrimer *primer, CpScratch *sc, const McSinTable *st,
                                 i64 seed, int nticks, double *out_pos_vel) {
    cp_provide_chunk(primer, sc, st, seed, 0, 0);

    McEntity e;
    e.posX = PPW_SPAWN_X;
    e.posY = PPW_SPAWN_Y;
    e.posZ = PPW_SPAWN_Z;
    e.box = mc_pcm_player_box(e.posX, e.posY, e.posZ);
    e.motionX = 0.0;
    e.motionY = 0.0;
    e.motionZ = 0.0;
    e.onGround = 0;
    e.collidedHorizontally = 0;
    e.collidedVertically = 0;
    e.isCollided = 0;

    McAABB blocks[PPW_MAX_BLOCKS];

    for (int t = 0; t < nticks; ++t) {
        PpwAction act = ppw_action_for_tick(seed, t);
        ppw_player_tick(primer, st, &e, &act, blocks);
        out_pos_vel[t * 6 + 0] = e.posX;
        out_pos_vel[t * 6 + 1] = e.posY;
        out_pos_vel[t * 6 + 2] = e.posZ;
        out_pos_vel[t * 6 + 3] = e.motionX;
        out_pos_vel[t * 6 + 4] = e.motionY;
        out_pos_vel[t * 6 + 5] = e.motionZ;
    }
}

#endif /* MC_PLAYER_PHYSICS_WORLD_H */
