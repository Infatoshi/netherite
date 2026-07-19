/* player_physics_full: player_physics_world harness + physics_collision_full Entity.move
 * (stepHeight, sneak step-back, web/ladder/liquid) + block_props_table slipperiness on real
 * chunk_provider terrain. Internal verify: CPU==CUDA seeds 12345/0/7, 120 lines (20 ticks x 6).
 *
 * READ-ONLY deps: physics_collision_full.h, chunk_provider.h, block_props_table.h.
 * Does NOT edit player_physics_world.h or other deps; reuses the ppw harness constants/pattern. */
#ifndef MC_PLAYER_PHYSICS_FULL_H
#define MC_PLAYER_PHYSICS_FULL_H

#include <math.h>
#include "physics_collision_full.h"
#include "chunk_provider.h"
#include "block_props_table.h"
#include "mc_math.h"
#include "mc_rng.h"

#define PPF_NUM_TICKS       20
#define PPF_MAX_BLOCKS      512
#define PPF_SPAWN_X         8.5
#define PPF_SPAWN_Y         90.0
#define PPF_SPAWN_Z         8.5
#define PPF_AI_MOVE_SPEED   0.1f
#define PPF_JUMP_FACTOR     0.02f
#define PPF_STEP_HEIGHT     0.6f
#define PPF_PURPOSE_INPUT   0x50504601u

typedef struct {
    float forward;
    float strafe;
    float yaw;
    float pitch;
    int   jump;
    int   sneak;
} PpfAction;

MC_HD static inline u16 ppf_cb_to_vanilla(u16 cb) {
    if (cb_is_stained_clay((int)cb)) return 159;
    switch (cb) {
        case CB_AIR: return 0;
        case CB_STONE: return 1;
        case CB_WATER: return 9;
        case CB_GRASS: return 2;
        case CB_DIRT: return 3;
        case CB_BEDROCK: return 7;
        case CB_GRAVEL: return 13;
        case CB_SAND: return 12;
        case CB_SANDSTONE: return 24;
        case CB_RED_SANDSTONE: return 179;
        case CB_ICE: return 79;
        case CB_LAVA: return 11;
        case CB_FLOWING_LAVA: return 10;
        case CB_FLOWING_WATER: return 8;
        case CB_WATER_LILY: return 111;
        case CB_MYCELIUM: return 110;
        case CB_SNOW_LAYER: return 78;
        case CB_HARDENED_CLAY: return 172;
        case CB_STAINED_HARDENED_CLAY: return 159;
        case CB_PODZOL: return 3;
        case CB_COARSE_DIRT: return 3;
        default: return cb;
    }
}

MC_HD static inline int ppf_get_block(const ChunkPrimer *p, int wx, int wy, int wz) {
    if (wx < 0 || wx > 15 || wz < 0 || wz > 15 || wy < 0 || wy > 255) return CB_AIR;
    return cb_get(p, wx, wy, wz);
}

/* Vanilla Block.slipperiness for ids in block_props_table KEEP set (default 0.6F). */
MC_HD static inline float ppf_bpt_slipperiness(int vanilla_id) {
    if (vanilla_id == 79 || vanilla_id == 174) return 0.98f;
    return 0.6f;
}

MC_HD static inline int ppf_collect_blocks(const ChunkPrimer *p, const McAABB *query,
                                           PcfBlock *blocks, int maxblocks) {
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
                int cb = ppf_get_block(p, x, y, z);
                if (cb == CB_AIR) continue;
                int vid = (int)ppf_cb_to_vanilla((u16)cb);
                if (vid == 0) continue;
                if (n >= maxblocks) return n;
                blocks[n++] = (PcfBlock){
                    vid, (double)x, (double)y, (double)z,
                    2 /* NORTH; chunk primer has no ladder meta */
                };
            }
    return n;
}

MC_HD static inline int ppf_entity_in_web(const ChunkPrimer *p, const McAABB *box) {
    int x0 = mc_floor(box->minX);
    int x1 = mc_floor(box->maxX);
    int y0 = mc_floor(box->minY);
    int y1 = mc_floor(box->maxY);
    int z0 = mc_floor(box->minZ);
    int z1 = mc_floor(box->maxZ);
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) {
                int cb = ppf_get_block(p, x, y, z);
                if (ppf_cb_to_vanilla((u16)cb) == 30) return 1;
            }
    return 0;
}

MC_HD static inline void ppf_move_flying(const McSinTable *st, McPcfEntity *e, float yaw,
                                         float strafe, float forward, float accel) {
    float f3 = strafe * strafe + forward * forward;
    if (f3 >= 1.0e-4f) {
        f3 = (float)sqrt((double)f3);
        if (f3 < 1.0f) f3 = 1.0f;
        f3 = accel / f3;
        strafe *= f3;
        forward *= f3;
        /* vanilla moveFlying converts yaw deg->rad (rotationYaw * 0.017453292F) before the LUT;
         * mc_sin/mc_cos expect radians (see player_physics_world.h ppw_move_flying). */
        float rad = yaw * 0.017453292f;
        float sinYaw = mc_sin(st, rad);
        float cosYaw = mc_cos(st, rad);
        e->motionX += (double)(strafe * cosYaw - forward * sinYaw);
        e->motionZ += (double)(forward * cosYaw + strafe * sinYaw);
    }
}

MC_HD static inline PpfAction ppf_action_for_tick(i64 seed, int tick) {
    PpfAction a;
    u64 h0 = mc_hash_seed((u64)seed, tick, 8, 90, 8, PPF_PURPOSE_INPUT);
    u64 h1 = mc_hash64(h0 + 1ULL);
    u64 h2 = mc_hash64(h0 + 2ULL);
    u64 h3 = mc_hash64(h0 + 3ULL);
    u64 h4 = mc_hash64(h0 + 4ULL);
    a.forward = (float)(mc_hash_bound(h0, 3) - 1);
    a.strafe  = (float)(mc_hash_bound(h1, 3) - 1);
    a.yaw     = (float)(mc_hash_bound(h2, 24) * 15);
    a.pitch   = 0.0f;
    a.jump    = (mc_hash_bound(h3, 7) == 0) ? 1 : 0;
    a.sneak   = (mc_hash_bound(h4, 5) == 0) ? 1 : 0;
    return a;
}

MC_HD static inline void ppf_init_entity(McPcfEntity *e) {
    pcf_init_entity(e);
    e->stepHeight = PPF_STEP_HEIGHT;
    e->isPlayer = 1;
    e->moverType = PCF_MOVER_PLAYER;
}

MC_HD static inline void ppf_player_tick(const ChunkPrimer *primer, const McSinTable *st,
                                         McPcfEntity *e, const PpfAction *act, int tick,
                                         PcfBlock *blocks) {
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
        int cb = ppf_get_block(primer, bx, by, bz);
        int vid = (int)ppf_cb_to_vanilla((u16)cb);
        /* Vanilla reads Block.slipperiness unconditionally when onGround; air and
         * every non-ice block give the 0.6 default. Gating on solidity diverged
         * when the player stood on a ledge with air under floor(posX/posZ). */
        f2 = ppf_bpt_slipperiness(vid) * 0.91f;
    }

    float f3 = 0.16277136f / (f2 * f2 * f2);
    float accel = e->onGround ? (PPF_AI_MOVE_SPEED * f3) : PPF_JUMP_FACTOR;

    ppf_move_flying(st, e, act->yaw, strafing, forward, accel);

    e->isSneaking = act->sneak ? 1 : 0;
    e->worldTime = (i64)tick;
    e->isInWeb = ppf_entity_in_web(primer, &e->box);

    McAABB query = mc_aabb_addcoord(&e->box, e->motionX, e->motionY, e->motionZ);
    int nblocks = ppf_collect_blocks(primer, &query, blocks, PPF_MAX_BLOCKS);
    pcf_entity_move(e, e->motionX, e->motionY, e->motionZ, blocks, nblocks);

    e->motionY -= 0.08;
    e->motionY *= 0.9800000190734863;
    e->motionX *= (double)f2;
    e->motionZ *= (double)f2;
}

MC_HD static inline void ppf_run(ChunkPrimer *primer, CpScratch *sc, const McSinTable *st,
                                 i64 seed, int nticks, double *out_pos_vel) {
    cp_provide_chunk(primer, sc, st, seed, 0, 0);

    McPcfEntity e;
    ppf_init_entity(&e);
    e.posX = PPF_SPAWN_X;
    e.posY = PPF_SPAWN_Y;
    e.posZ = PPF_SPAWN_Z;
    e.box = mc_pcm_player_box(e.posX, e.posY, e.posZ);

    PcfBlock blocks[PPF_MAX_BLOCKS];

    for (int t = 0; t < nticks; ++t) {
        PpfAction act = ppf_action_for_tick(seed, t);
        ppf_player_tick(primer, st, &e, &act, t, blocks);
        out_pos_vel[t * 6 + 0] = e.posX;
        out_pos_vel[t * 6 + 1] = e.posY;
        out_pos_vel[t * 6 + 2] = e.posZ;
        out_pos_vel[t * 6 + 3] = e.motionX;
        out_pos_vel[t * 6 + 4] = e.motionY;
        out_pos_vel[t * 6 + 5] = e.motionZ;
    }
}

#endif /* MC_PLAYER_PHYSICS_FULL_H */
