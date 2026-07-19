/* player_survival: a SURVIVAL-PLAYER driver that COMPOSES already-verified mc-sim kernels into one
 * double-buffered tick loop over a REAL generated multi-chunk world. NOT a new subsystem - it wires
 * the verified pieces together and dumps per-tick state so CPU==CUDA can be checked bitwise.
 *
 * COMPOSED (all read-only deps; this header edits none of them):
 *   - multi-chunk gen: chunk_provider (cp_provide_chunk, vanilla LCG terrain) + te_load_primer_to_chunk
 *     into a double-buffered Chunk[] region (SPEC rule 3; same gen pattern as tick_world_multi).
 *   - player physics/collision: physics_collision_math (mc_entity_move) + the verified
 *     player_physics_world tick math (ppw_move_flying, slipperiness, friction) but over the WHOLE
 *     multi-chunk region (world-coord block query instead of one ChunkPrimer).
 *   - block props: block_props_table (mc_bpt_props) - hardness gates a break; solid flag gates
 *     collision + drop.
 *   - inventory: inventory_stack_rules (isr_add_item_stack_to_inventory / isr_decr_stack_size) -
 *     block-break drops merge in; block-place consumes from the held stack.
 *   - vitals: health + hunger tick here (hunger drains each tick; a fall's accumulated fallDistance
 *     over the collision substrate reduces health on landing).
 *
 * The tick loop is driven by a fixed DETERMINISTIC ACTION TAPE keyed on a hash seed (SPEC rule 1,
 * stateless world-coord RNG) plus schedule gates, so every wired path is exercised: a break that
 * yields a drop, a place, fall damage that lowers health, hunger drain. One env ticks serially per
 * thread; runtime randomness is order-independent hash RNG and block edits are double-buffered
 * (read 'now', write 'next', swap) so CPU and CUDA agree bitwise by construction.
 *
 * SCOPE (internal fidelity, not vanilla-bit-exact): one player AABB, stepHeight=0.6 (vanilla
 * auto-step via mc_entity_move_step, tape-verified at t417 of 20260712T055346Z), liquids
 * pass-through, drop = the block itself (item id == block id, count 1), continuous fall/hunger
 * models. No mobs/entities (attack is a recorded swing). */
#ifndef MC_PLAYER_SURVIVAL_H
#define MC_PLAYER_SURVIVAL_H

#include <math.h>
#include "tick_entities.h"          /* te_load_primer_to_chunk, Chunk, twc_copy_chunk, chunk_provider */
#include "player_physics_world.h"   /* ppw_move_flying + PPW_AI_MOVE_SPEED/PPW_JUMP_FACTOR (verified) */
#include "block_props_table.h"      /* mc_bpt_props (hardness/flags) */
#include "inventory_stack_rules.h"  /* IsrInv + stack rules; pulls items_core */
#include "mc_gamerules.h"           /* doTileDrops gate (Block.harvestBlock -> dropBlockAsItem) */

/* ---- region geometry (multi-chunk, centered on origin) ---- */
#ifndef PSV_DIM
#define PSV_DIM 3                              /* PSV_DIM x PSV_DIM chunks (3 -> 9, 48x256x48) */
#endif
#define PSV_R        (PSV_DIM / 2)
#define PSV_NCHUNKS  (PSV_DIM * PSV_DIM)

#ifndef PSV_NTICKS
#define PSV_NTICKS 128
#endif

#define PSV_MAX_BLOCKS   512
#define PSV_SPAWN_X      8.5
#define PSV_SPAWN_Y      120.0                 /* high spawn -> a real fall -> guaranteed fall damage */
#define PSV_SPAWN_Z      8.5
#define PSV_EYE_HEIGHT   1.62
#define PSV_REACH        5.0
#define PSV_RAY_DT       0.05
#define PSV_HUNGER_RATE  0.05f                 /* food drained per tick (guaranteed hunger path) */
#define PSV_STARVE_RATE  0.5f                  /* health lost per tick while starving (food<=0) */
#define PSV_FALL_SAFE    3.0f                  /* blocks of fall absorbed before damage */
#define PSV_MAX_HEALTH   20.0f
#define PSV_MAX_FOOD     20.0f
#define PSV_PURPOSE      0x50535601u           /* "PSV" action-tape hash purpose */

/* Per-tick emitted fields (fixed order; see psv_emit). */
#define PSV_FIELDS 19

/* ---- player + action state (our own struct; mc_world.h untouched) ---- */
typedef struct {
    McEntity ent;            /* pos/vel/box + collision flags (verified physics substrate) */
    float    yaw, pitch;     /* look, degrees */
    float    health;         /* 0..PSV_MAX_HEALTH */
    float    food;           /* 0..PSV_MAX_FOOD */
    float    fall_distance;  /* accumulated while airborne, consumed on landing */
    /* vanilla sprint state (EntityPlayerSP.onLivingUpdate; game layer runs the rules) */
    int      sprinting;           /* Entity flag 3: sprint speed/jump boosts active */
    int      sprint_toggle_timer; /* double-tap-W window, set to 7 on a fresh press edge */
    int      jump_factor_sprint;  /* sprint folded into jumpMovementFactor: EntityPlayer.
                                   * onLivingUpdate updates the factor AFTER super's
                                   * movement, so air accel lags the flag by one tick */
    int      jump_ticks;          /* EntityLivingBase.jumpTicks: 10-tick hold-jump
                                   * cooldown, decremented every tick */
    float    prev_move_forward;   /* last tick's movementInput.moveForward (pre-update flag2) */
    int      prev_sneak;          /* last tick's movementInput.sneak (pre-update flag1) */
    IsrInv   inv;            /* inventory (verified stack rules) */
    u32      break_events;   /* cumulative successful block breaks (drop yielded) */
    u32      place_events;   /* cumulative successful block places */
    u32      swing_events;   /* cumulative attack swings that hit nothing */
} PsvPlayer;

typedef struct {
    float forward;   /* [-1,1] */
    float strafe;    /* [-1,1] */
    float yaw;       /* degrees */
    float pitch;     /* degrees */
    int   jump;
    int   sprint;    /* resolved sprint STATE for this tick (not the key), set by the caller */
    int   sneak;     /* sneaking (edge clamp; caller pre-scales move input by 0.3) */
    int   do_break;
    int   do_place;
    int   attack;
} PsvAction;

/* ---- world-coordinate block access over the Chunk[] region ---- */
MC_HD static inline int psv_floordiv16(int v) {
    return (v >= 0) ? (v >> 4) : -(((-v) + 15) >> 4);
}

/* Resolve world (wx,wz) -> chunk index in the region + local (lx,lz). Returns -1 if outside. */
MC_HD static inline int psv_chunk_index(int wx, int wz, int *lx, int *lz) {
    int cx = psv_floordiv16(wx);
    int cz = psv_floordiv16(wz);
    if (cx < -PSV_R || cx > PSV_R || cz < -PSV_R || cz > PSV_R) return -1;
    *lx = wx - cx * 16;
    *lz = wz - cz * 16;
    return (cz + PSV_R) * PSV_DIM + (cx + PSV_R);
}

MC_HD static inline int psv_get_block(const Chunk *chunks, int wx, int wy, int wz) {
    int lx, lz, ci;
    if (wy < 0 || wy > 255) return BLK_AIR;
    ci = psv_chunk_index(wx, wz, &lx, &lz);
    if (ci < 0) return BLK_AIR;                /* outside region = pass-through air */
    return mc_state_id(mc_get(&chunks[ci], lx, wy, lz));
}

MC_HD static inline int psv_get_meta(const Chunk *chunks, int wx, int wy, int wz) {
    int lx, lz, ci;
    if (wy < 0 || wy > 255) return 0;
    ci = psv_chunk_index(wx, wz, &lx, &lz);
    if (ci < 0) return 0;
    return (int)mc_state_meta(mc_get(&chunks[ci], lx, wy, lz));
}

MC_HD static inline void psv_set_block(Chunk *chunks, int wx, int wy, int wz, int id) {
    int lx, lz, ci;
    if (wy < 0 || wy > 255) return;
    ci = psv_chunk_index(wx, wz, &lx, &lz);
    if (ci < 0) return;
    mc_set(&chunks[ci], lx, wy, lz, mc_state(id, 0));
}

/* solid (full-cube collision) per the block props table; liquids/air pass through. */
MC_HD static inline int psv_solid(int id) {
    return (mc_bpt_props(id).flags & BF_SOLID) != 0;
}
MC_HD static inline float psv_slipperiness(int id) {
    return (id == BLK_ICE) ? 0.98f : 0.6f;
}

/* Collect solid-block AABBs (world coords) intersecting the query box. */
MC_HD static inline int psv_collect_blocks(const Chunk *chunks, const McAABB *query,
                                           McAABB *blocks, int maxblocks) {
    int n = 0;
    int x0 = mc_floor(query->minX), x1 = mc_floor(query->maxX);
    int y0 = mc_floor(query->minY), y1 = mc_floor(query->maxY);
    int z0 = mc_floor(query->minZ), z1 = mc_floor(query->maxZ);
    if (y0 < 0) y0 = 0;
    if (y1 > 255) y1 = 255;
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) {
                if (!psv_solid(psv_get_block(chunks, x, y, z))) continue;
                if (n >= maxblocks) return n;
                blocks[n++] = mc_aabb_make((double)x, (double)y, (double)z,
                                           (double)x + 1.0, (double)y + 1.0, (double)z + 1.0);
            }
    return n;
}

/* ---- deterministic action tape (hash RNG + schedule gates) ---- */
MC_HD static inline PsvAction psv_action_for_tick(i64 seed, int tick, int on_ground) {
    PsvAction a;
    u64 h0 = mc_hash_seed((u64)seed, tick, 0, 0, 0, PSV_PURPOSE);
    u64 h1 = mc_hash64(h0 + 1ULL);
    u64 h2 = mc_hash64(h0 + 2ULL);
    u64 h3 = mc_hash64(h0 + 3ULL);
    u64 h4 = mc_hash64(h0 + 4ULL);
    a.forward = (float)(mc_hash_bound(h0, 3) - 1);
    a.strafe  = (float)(mc_hash_bound(h1, 3) - 1);
    a.yaw     = (float)(mc_hash_bound(h2, 24) * 15);
    a.jump    = (mc_hash_bound(h3, 7) == 0) ? 1 : 0;
    a.sprint  = 0;   /* the deterministic tape never sprints (keeps psv goldens unchanged) */
    a.sneak   = 0;   /* nor sneaks */
    a.attack  = (mc_hash_bound(h4, 5) == 0) ? 1 : 0;
    /* schedule break/place once the player is standing on terrain so the down-ray hits a real block */
    a.do_break = (on_ground && (tick % 6 == 3)) ? 1 : 0;
    a.do_place = (on_ground && (tick % 6 == 5)) ? 1 : 0;
    /* when breaking/placing, look straight down so the crosshair targets the floor block */
    a.pitch   = (a.do_break || a.do_place) ? 88.0f : 0.0f;
    return a;
}

/* MathHelper.ceil(double). */
MC_HD static inline int psv_ceil(double v) {
    int i = (int)v;
    return v > (double)i ? i + 1 : i;
}

/* ---- liquid immersion (Entity.isInWater / isInLava) ----
 * Vanilla inWater: handleMaterialAcceleration over the entity box expand(0,
 * -0.4,0) (shrinks BOTH y sides) then contract(0.001) (all six sides), cells
 * [floor(min), ceil(max)). Its liquid-height test compares the surface against
 * ceil(maxY) - vacuously true for every in-range cell - so the real semantics
 * are plain cell intersection; matching that bug-for-bug is required (verified
 * against a real-game tape, water pond at spawn seed 0, tick 9631/9632). Lava:
 * isMaterialInBB over the box expand(-0.1,-0.4,-0.1), same loop shape. Flow
 * push is not applied (craster's live fluids settle; note for later). */
MC_HD static inline int psv_in_liquid(const Chunk *now, const McEntity *e, int want_water) {
    double x0, y0, z0, x1, y1, z1;
    if (want_water) {
        x0 = e->box.minX + 0.001; x1 = e->box.maxX - 0.001;
        z0 = e->box.minZ + 0.001; z1 = e->box.maxZ - 0.001;
        y0 = e->box.minY + 0.4 + 0.001; y1 = e->box.maxY - 0.4 - 0.001;
    } else {
        x0 = e->box.minX + 0.1; x1 = e->box.maxX - 0.1;
        z0 = e->box.minZ + 0.1; z1 = e->box.maxZ - 0.1;
        y0 = e->box.minY + 0.4; y1 = e->box.maxY - 0.4;
    }
    for (int bx = mc_floor(x0); bx < psv_ceil(x1); ++bx)
        for (int by = mc_floor(y0); by < psv_ceil(y1); ++by)
            for (int bz = mc_floor(z0); bz < psv_ceil(z1); ++bz) {
                int id = psv_get_block(now, bx, by, bz);
                int is_w = (id == 8 || id == 9), is_l = (id == 10 || id == 11);
                if (want_water ? is_w : is_l) return 1;
            }
    return 0;
}

/* ---- flowing-water current (BlockLiquid.getFlow + handleMaterialAcceleration) ----
 * getRenderedDepth for a water cell: LEVEL meta, falling (>=8) counts as 0;
 * -1 if the cell is not water. */
MC_HD static inline int psv_water_depth(const Chunk *now, int x, int y, int z) {
    int id = psv_get_block(now, x, y, z);
    if (id != 8 && id != 9) return -1;
    int m = psv_get_meta(now, x, y, z);
    return m >= 8 ? 0 : m;
}

/* BlockLiquid.isBlockSolid for the downward-current probe: water false,
 * ice false, otherwise the block's solid material. */
MC_HD static inline int psv_flow_side_solid(const Chunk *now, int x, int y, int z) {
    int id = psv_get_block(now, x, y, z);
    if (id == 8 || id == 9 || id == BLK_ICE) return 0;
    return psv_solid(id);
}

/* BlockLiquid.getFlow for one water cell: signed level differences toward the
 * four horizontal neighbours (falling through open air probes one below with
 * the j-(i-8) weight), the falling-water downward current, Vec3d.normalize
 * (zero below 1e-4 length). */
MC_HD static inline void psv_water_cell_flow(const Chunk *now, int bx, int by, int bz,
                                             double *fx, double *fy, double *fz) {
    static const int DX[4] = {0, -1, 0, 1}, DZ[4] = {1, 0, -1, 0}; /* S,W,N,E */
    double d0 = 0.0, d1 = 0.0, d2 = 0.0;
    int i = psv_water_depth(now, bx, by, bz);
    for (int f = 0; f < 4; ++f) {
        int nx = bx + DX[f], nz = bz + DZ[f];
        int j = psv_water_depth(now, nx, by, nz);
        if (j < 0) {
            if (!psv_solid(psv_get_block(now, nx, by, nz))) {
                j = psv_water_depth(now, nx, by - 1, nz);
                if (j >= 0) {
                    int k = j - (i - 8);
                    d0 += (double)(DX[f] * k);
                    d2 += (double)(DZ[f] * k);
                }
            }
        } else {
            int k = j - i;
            d0 += (double)(DX[f] * k);
            d2 += (double)(DZ[f] * k);
        }
    }
    if (psv_get_meta(now, bx, by, bz) >= 8) {
        for (int f = 0; f < 4; ++f) {
            int nx = bx + DX[f], nz = bz + DZ[f];
            if (psv_flow_side_solid(now, nx, by, nz) ||
                psv_flow_side_solid(now, nx, by + 1, nz)) {
                double l = sqrt(d0 * d0 + d1 * d1 + d2 * d2);
                if (l < 1.0e-4) { d0 = d1 = d2 = 0.0; }
                else { d0 /= l; d1 /= l; d2 /= l; }
                d1 += -6.0;
                break;
            }
        }
    }
    double l = sqrt(d0 * d0 + d1 * d1 + d2 * d2);
    if (l < 1.0e-4) { *fx = 0.0; *fy = 0.0; *fz = 0.0; }
    else { *fx = d0 / l; *fy = d1 / l; *fz = d2 / l; }
}

/* Entity.handleWaterMovement: the psv_in_liquid water test, plus the flowing
 * current push - handleMaterialAcceleration sums each water cell's normalized
 * getFlow, normalizes the sum, and (players are isPushedByWater in survival)
 * adds 0.014 * that unit vector to motion. Runs in Entity.onUpdate, BEFORE the
 * 0.003 motion snap and travel. */
MC_HD static inline int psv_handle_water(const Chunk *now, McEntity *e) {
    double x0 = e->box.minX + 0.001, x1 = e->box.maxX - 0.001;
    double z0 = e->box.minZ + 0.001, z1 = e->box.maxZ - 0.001;
    double y0 = e->box.minY + 0.4 + 0.001, y1 = e->box.maxY - 0.4 - 0.001;
    int flag = 0;
    double sx = 0.0, sy = 0.0, sz = 0.0;
    for (int bx = mc_floor(x0); bx < psv_ceil(x1); ++bx)
        for (int by = mc_floor(y0); by < psv_ceil(y1); ++by)
            for (int bz = mc_floor(z0); bz < psv_ceil(z1); ++bz) {
                int id = psv_get_block(now, bx, by, bz);
                if (id != 8 && id != 9) continue;
                flag = 1;
                double fx, fy, fz;
                psv_water_cell_flow(now, bx, by, bz, &fx, &fy, &fz);
                sx += fx; sy += fy; sz += fz;
            }
    double l = sqrt(sx * sx + sy * sy + sz * sz);
    if (l > 0.0) {
        sx /= l; sy /= l; sz /= l;
        e->motionX += sx * 0.014;
        e->motionY += sy * 0.014;
        e->motionZ += sz * 0.014;
    }
    return flag;
}

/* Entity.isOffsetPositionInLiquid: despite the vanilla name, TRUE means the
 * offset box is FREE - no collision boxes (getCollisionBoxes empty) and no
 * liquid cells (containsAnyLiquid false, cells [floor(min), ceil(max))). The
 * water-edge hop fires only when the spot ahead is clear to hop into. */
MC_HD static inline int psv_offset_in_liquid(const Chunk *now, const McEntity *e,
                                             double dx, double dy, double dz) {
    McAABB off = e->box;
    off.minX += dx; off.maxX += dx;
    off.minY += dy; off.maxY += dy;
    off.minZ += dz; off.maxZ += dz;
    McAABB hit;
    if (psv_collect_blocks(now, &off, &hit, 1) > 0) return 0;
    for (int bx = mc_floor(off.minX); bx < psv_ceil(off.maxX); ++bx)
        for (int by = mc_floor(off.minY); by < psv_ceil(off.maxY); ++by)
            for (int bz = mc_floor(off.minZ); bz < psv_ceil(off.maxZ); ++bz) {
                int id = psv_get_block(now, bx, by, bz);
                if (id >= 8 && id <= 11) return 0;
            }
    return 1;
}

/* ---- one physics tick (verified ppw math over the multi-chunk region) ---- */
MC_HD static inline void psv_physics_tick(const Chunk *now, const McSinTable *st, PsvPlayer *pl,
                                          const PsvAction *act, McAABB *blocks) {
    McEntity *e = &pl->ent;
    float strafing = act->strafe * 0.98f;
    float forward  = act->forward * 0.98f;

    /* Entity.onEntityUpdate (handleWaterMovement) runs before onLivingUpdate's
     * motion snap, so the current push lands first and CAN be snapped. */
    int in_water = psv_handle_water(now, e);
    int in_lava  = !in_water && psv_in_liquid(now, e, 0);

    /* EntityLivingBase.onLivingUpdate: tiny motions snap to zero BEFORE jump/travel.
     * Without this, sub-0.003 dust (e.g. the LUT's sin(pi)=1.22e-16 at yaw +/-180)
     * accumulates tick over tick while the live game re-zeroes it -- a 1-ULP velocity
     * drift that eventually flips a floor()/collision boundary. */
    if (fabs(e->motionX) < 0.003) e->motionX = 0.0;
    if (fabs(e->motionY) < 0.003) e->motionY = 0.0;
    if (fabs(e->motionZ) < 0.003) e->motionZ = 0.0;

    /* EntityLivingBase.onLivingUpdate decrements jumpTicks before the jump
     * check; the swim-up branch is NOT gated by it. */
    if (pl->jump_ticks > 0) --pl->jump_ticks;

    if (act->jump && (in_water || in_lava)) {
        /* handleJumpWater/handleJumpLava: swim up */
        e->motionY += 0.03999999910593033;
    } else if (act->jump && e->onGround && pl->jump_ticks == 0) {
        pl->jump_ticks = 10;
        e->motionY = 0.41999998688697815;
        if (act->sprint) {
            /* EntityLivingBase.jump(): sprinting adds a horizontal kick along the look yaw */
            float fj = pl->yaw * 0.017453292f;
            e->motionX -= (double)(mc_sin(st, fj) * 0.2f);
            e->motionZ += (double)(mc_cos(st, fj) * 0.2f);
        }
    }

    /* EntityLivingBase.moveEntityWithHeading water/lava branches (1.11.2):
     * accel 0.02, drag 0.8 (water, motionY exactly 0.800000011920929D) or 0.5
     * (lava), sink 0.02/tick, and the climb-out kick: horizontally collided
     * with liquid at the +0.6 offset -> motionY = 0.3. */
    if (in_water || in_lava) {
        double d0 = e->posY;
        ppw_move_flying(st, e, act->yaw, strafing, forward, 0.02f);
        McAABB wquery = mc_aabb_addcoord(&e->box, e->motionX, e->motionY, e->motionZ);
        /* widen the broadphase for the auto-step retry's up-query (box.addCoord(x,+0.6,z));
         * mc_entity_move_step re-filters with the exact vanilla per-call queries. */
        if (e->box.maxY + 0.6 > wquery.maxY) wquery.maxY = e->box.maxY + 0.6;
        int wnblocks = psv_collect_blocks(now, &wquery, blocks, PSV_MAX_BLOCKS);
        mc_entity_move_step(e, e->motionX, e->motionY, e->motionZ, blocks, wnblocks, 0.6f);
        double drag = in_water ? 0.800000011920929 : 0.5;
        e->motionX *= drag;
        e->motionY *= drag;
        e->motionZ *= drag;
        e->motionY -= 0.02;
        if (e->collidedHorizontally &&
            psv_offset_in_liquid(now, e, e->motionX,
                                 e->motionY + 0.6000000238418579 - e->posY + d0, e->motionZ))
            e->motionY = 0.30000001192092896;
        pl->jump_factor_sprint = act->sprint;   /* post-movement, every tick */
        return;
    }

    float f2 = 0.91f;
    if (e->onGround) {
        int bx = mc_floor(e->posX);
        int by = mc_floor(e->box.minY) - 1;
        int bz = mc_floor(e->posZ);
        int bid = psv_get_block(now, bx, by, bz);
        /* Vanilla: slipperiness read unconditionally when onGround (air = 0.6). */
        f2 = psv_slipperiness(bid) * 0.91f;
    }

    float f3 = 0.16277136f / (f2 * f2 * f2);
    float accel;
    if (e->onGround) {
        /* getAIMoveSpeed(): MOVEMENT_SPEED attribute, base 0.10000000149011612D
         * (EntityPlayer.applyEntityAttributes), sprint modifier +0.30000001192092896D
         * op MULTIPLY_TOTAL (EntityLivingBase.SPRINTING_SPEED_BOOST), cast to float. */
        float ai = act->sprint ? (float)(0.10000000149011612 * (1.0 + 0.30000001192092896))
                               : PPW_AI_MOVE_SPEED;
        accel = ai * f3;
    } else {
        /* EntityPlayer.onLivingUpdate: jumpMovementFactor = speedInAir(0.02F), sprinting
         * adds speedInAir*0.3D in double then casts back to float. The factor is
         * recomputed AFTER super.onLivingUpdate() has already moved the entity, so
         * the movement of tick N uses the sprint flag resolved at tick N-1. */
        accel = pl->jump_factor_sprint ? (float)((double)0.02f + (double)0.02f * 0.3)
                                       : PPW_JUMP_FACTOR;
    }

    ppw_move_flying(st, e, act->yaw, strafing, forward, accel);

    /* Entity.move sneak edge clamp (1.11.2): while sneaking on the ground,
     * shave x/z toward 0 in 0.05 steps while the box offset by (x,-stepHeight,z)
     * would collide with NOTHING (i.e. the move would leave the ledge). Player
     * stepHeight = 0.6. Runs on the intended motion BEFORE the collision move.
     * Vanilla clamps only the move() ARGUMENTS (and keeps d2/d4 in sync inside
     * the loop, so motionX/Z survive the post-move zeroing): the player keeps
     * inching to the ledge lip on later ticks. Do NOT write mx/mz back into
     * e->motion*. */
    double mvx = e->motionX, mvz = e->motionZ;
    if (act->sneak && e->onGround) {
        double mx = e->motionX, mz = e->motionZ;
        McAABB sq;
        McAABB squery = mc_aabb_addcoord(&e->box, mx, -0.6, mz);
        int snb = psv_collect_blocks(now, &squery, blocks, PSV_MAX_BLOCKS);
        for (; mx != 0.0; mx = (mx < 0.05 && mx >= -0.05) ? 0.0 : (mx > 0.0 ? mx - 0.05 : mx + 0.05)) {
            sq = mc_aabb_offset(&e->box, mx, -0.6, 0.0);
            int hit = 0;
            for (int i = 0; i < snb; ++i)
                if (mc_aabb_intersects(&sq, &blocks[i])) { hit = 1; break; }
            if (hit) break;
        }
        for (; mz != 0.0; mz = (mz < 0.05 && mz >= -0.05) ? 0.0 : (mz > 0.0 ? mz - 0.05 : mz + 0.05)) {
            sq = mc_aabb_offset(&e->box, 0.0, -0.6, mz);
            int hit = 0;
            for (int i = 0; i < snb; ++i)
                if (mc_aabb_intersects(&sq, &blocks[i])) { hit = 1; break; }
            if (hit) break;
        }
        while (mx != 0.0 && mz != 0.0) {
            sq = mc_aabb_offset(&e->box, mx, -0.6, mz);
            int hit = 0;
            for (int i = 0; i < snb; ++i)
                if (mc_aabb_intersects(&sq, &blocks[i])) { hit = 1; break; }
            if (hit) break;
            mx = (mx < 0.05 && mx >= -0.05) ? 0.0 : (mx > 0.0 ? mx - 0.05 : mx + 0.05);
            mz = (mz < 0.05 && mz >= -0.05) ? 0.0 : (mz > 0.0 ? mz - 0.05 : mz + 0.05);
        }
        mvx = mx;
        mvz = mz;
    }

    McAABB query = mc_aabb_addcoord(&e->box, mvx, e->motionY, mvz);
    /* widen the broadphase for the auto-step retry's up-query (box.addCoord(x,+0.6,z));
     * mc_entity_move_step re-filters with the exact vanilla per-call queries. */
    if (e->box.maxY + 0.6 > query.maxY) query.maxY = e->box.maxY + 0.6;
    int nblocks = psv_collect_blocks(now, &query, blocks, PSV_MAX_BLOCKS);
    mc_entity_move_step(e, mvx, e->motionY, mvz, blocks, nblocks, 0.6f);

    e->motionY -= 0.08;
    e->motionY *= 0.9800000190734863;
    e->motionX *= (double)f2;
    e->motionZ *= (double)f2;

    /* EntityPlayer.onLivingUpdate refreshes jumpMovementFactor AFTER the
     * super.onLivingUpdate() movement above; next tick's air accel sees it. */
    pl->jump_factor_sprint = act->sprint;
}

/* ---- crosshair raycast (fixed-step DDA over 'now') ---- *
 * Returns: 1 = solid hit AND a preceding air cell was found (place target valid),
 *          0 = solid hit but the very first cell was already solid (no place target),
 *         -1 = no solid within reach. hit cell -> (hx,hy,hz); place cell -> (ax,ay,az). */
MC_HD static inline int psv_raycast(const Chunk *now, const McSinTable *st, const PsvPlayer *pl,
                                    int *hx, int *hy, int *hz, int *ax, int *ay, int *az) {
    /* vanilla getVectorForRotation(pitch, yaw) with the MathHelper sin table */
    float f  = mc_cos(st, -pl->yaw * 0.017453292f - 3.1415927f);
    float f1 = mc_sin(st, -pl->yaw * 0.017453292f - 3.1415927f);
    float f2 = -mc_cos(st, -pl->pitch * 0.017453292f);
    float f3 = mc_sin(st, -pl->pitch * 0.017453292f);
    double dx = (double)(f1 * f2);
    double dy = (double)f3;
    double dz = (double)(f * f2);

    double ex = pl->ent.posX;
    double ey = pl->ent.posY + PSV_EYE_HEIGHT;
    double ez = pl->ent.posZ;

    int lastx = mc_floor(ex), lasty = mc_floor(ey), lastz = mc_floor(ez);
    int have_air = 0;
    double t;
    for (t = PSV_RAY_DT; t <= PSV_REACH; t += PSV_RAY_DT) {
        int bx = mc_floor(ex + dx * t);
        int by = mc_floor(ey + dy * t);
        int bz = mc_floor(ez + dz * t);
        if (bx == lastx && by == lasty && bz == lastz) continue;   /* same cell */
        if (psv_solid(psv_get_block(now, bx, by, bz))) {
            *hx = bx; *hy = by; *hz = bz;
            *ax = lastx; *ay = lasty; *az = lastz;   /* last empty cell before the hit = place spot */
            return have_air;
        }
        lastx = bx; lasty = by; lastz = bz;
        have_air = 1;
    }
    return -1;   /* no hit within reach */
}

/* ---- vitals: fall damage on landing + continuous hunger drain ---- */
MC_HD static inline void psv_vitals_tick(PsvPlayer *pl, int was_air, double prev_min_y) {
    McEntity *e = &pl->ent;
    /* accumulate fall distance while airborne (downward movement only) */
    if (!e->onGround) {
        double dropped = prev_min_y - e->box.minY;
        if (dropped > 0.0) pl->fall_distance += (float)dropped;
    } else if (was_air && pl->fall_distance > PSV_FALL_SAFE) {
        pl->health -= (pl->fall_distance - PSV_FALL_SAFE);   /* damage lowers health */
        if (pl->health < 0.0f) pl->health = 0.0f;
    }
    if (e->onGround) pl->fall_distance = 0.0f;

    /* hunger drains every tick; starvation eats health once food is gone */
    pl->food -= PSV_HUNGER_RATE;
    if (pl->food < 0.0f) { pl->food = 0.0f; pl->health -= PSV_STARVE_RATE; }
    if (pl->health < 0.0f) pl->health = 0.0f;
}

/* ---- one whole tick: physics -> raycast break/place -> vitals (double-buffered) ----
 * gr threads GameRules: doTileDrops gates the broken-block item drop (Block.harvestBlock ->
 * dropBlockAsItem). The block is still removed regardless; only the drop into inventory is
 * gated. Default rules (doTileDrops=1) are bit-identical to prior behavior. */
MC_HD static inline void psv_tick_gr(Chunk *now, Chunk *next, const McSinTable *st, PsvPlayer *pl,
                                     i64 seed, int tick, McAABB *blocks, const McGameRules *gr) {
    int i;
    for (i = 0; i < PSV_NCHUNKS; ++i) twc_copy_chunk(&next[i], &now[i]);

    PsvAction act = psv_action_for_tick(seed, tick, pl->ent.onGround);
    pl->yaw = act.yaw;
    pl->pitch = act.pitch;

    int was_air = !pl->ent.onGround;
    double prev_min_y = pl->ent.box.minY;

    psv_physics_tick(now, st, pl, &act, blocks);

    /* block break: drop the broken block into the inventory (verified stack rules) */
    if (act.do_break) {
        int hx, hy, hz, ax, ay, az;
        int r = psv_raycast(now, st, pl, &hx, &hy, &hz, &ax, &ay, &az);
        if (r >= 0) {
            int bid = psv_get_block(now, hx, hy, hz);
            BptProps bp = mc_bpt_props(bid);
            if (psv_solid(bid) && bp.hardness >= 0.0f) {
                psv_set_block(next, hx, hy, hz, BLK_AIR);
                if (gr->doTileDrops) {              /* Block.harvestBlock -> dropBlockAsItem */
                    ICStack drop = ic_mk(bid, 1, 0);
                    isr_add_item_stack_to_inventory(&pl->inv, &drop);
                }
                pl->break_events++;
            }
        }
    }

    /* block place: consume from the held stack, set the world block */
    if (act.do_place) {
        int hx, hy, hz, ax, ay, az;
        int r = psv_raycast(now, st, pl, &hx, &hy, &hz, &ax, &ay, &az);
        if (r == 1) {   /* a real air cell was found before the hit */
            ICStack held = isr_get_stack(&pl->inv, pl->inv.current_item);
            if (!isr_is_empty(&held) && psv_get_block(now, ax, ay, az) == BLK_AIR) {
                ICStack used = isr_decr_stack_size(&pl->inv, pl->inv.current_item, 1);
                if (!isr_is_empty(&used)) {
                    psv_set_block(next, ax, ay, az, used.item);
                    pl->place_events++;
                }
            }
        }
    }

    if (act.attack) pl->swing_events++;

    psv_vitals_tick(pl, was_air, prev_min_y);
}

/* Default-rules wrapper (doTileDrops=1): bit-identical to the prior psv_tick. */
MC_HD static inline void psv_tick(Chunk *now, Chunk *next, const McSinTable *st, PsvPlayer *pl,
                                  i64 seed, int tick, McAABB *blocks) {
    McGameRules gr = mc_gamerules_default();
    psv_tick_gr(now, next, st, pl, seed, tick, blocks, &gr);
}

/* ---- region hash (captures break/place edits) ---- */
MC_HD static inline u64 psv_blocks_hash(const Chunk *chunks) {
    u64 h = 0xcbf29ce484222325ULL;
    int ci, i;
    for (ci = 0; ci < PSV_NCHUNKS; ++ci)
        for (i = 0; i < MC_CHUNK_VOL; ++i) { h ^= (u64)chunks[ci].blocks[i]; h *= 0x100000001b3ULL; }
    return h;
}

MC_HD static inline u64 psv_f2u(float v) { union { float f; u32 u; } t; t.f = v; return (u64)t.u; }
MC_HD static inline u64 psv_d2u(double v) { union { double d; u64 u; } t; t.d = v; return t.u; }

/* Emit PSV_FIELDS fixed-order u64 lines for this tick's state. */
MC_HD static inline void psv_emit(const Chunk *now, const PsvPlayer *pl, u64 *o) {
    ICStack held = isr_get_stack(&pl->inv, pl->inv.current_item);
    o[0]  = psv_d2u(pl->ent.posX);
    o[1]  = psv_d2u(pl->ent.posY);
    o[2]  = psv_d2u(pl->ent.posZ);
    o[3]  = psv_d2u(pl->ent.motionX);
    o[4]  = psv_d2u(pl->ent.motionY);
    o[5]  = psv_d2u(pl->ent.motionZ);
    o[6]  = psv_f2u(pl->health);
    o[7]  = psv_f2u(pl->food);
    o[8]  = psv_f2u(pl->fall_distance);
    o[9]  = (u64)(u32)pl->ent.onGround;
    o[10] = psv_f2u(pl->yaw);
    o[11] = psv_f2u(pl->pitch);
    o[12] = (u64)(u32)isr_hotbar_total(&pl->inv);
    o[13] = (u64)(u32)isr_main_total(&pl->inv);
    o[14] = (u64)(u32)held.count;
    o[15] = (u64)(u32)held.item;
    o[16] = psv_blocks_hash(now);
    o[17] = (u64)pl->break_events;
    o[18] = (u64)pl->place_events;
}

/* ---- gen the multi-chunk region into both buffers (SPEC rule 3 double buffer) ---- */
MC_HD static inline void psv_gen(Chunk *a, Chunk *b, ChunkPrimer *primer, CpScratch *sc,
                                 const McSinTable *st, i64 seed) {
    int i;
    for (i = 0; i < PSV_NCHUNKS; ++i) {
        int cx = (i % PSV_DIM) - PSV_R;
        int cz = (i / PSV_DIM) - PSV_R;
        cp_provide_chunk(primer, sc, st, seed, cx, cz);
        te_load_primer_to_chunk(&a[i], primer);
        a[i].cx = cx;
        a[i].cz = cz;
    }
    for (i = 0; i < PSV_NCHUNKS; ++i) twc_copy_chunk(&b[i], &a[i]);
}

/* VANILLA player box, bit-exact: Entity.setSize(0.6F, 1.8F) + setPosition uses
 * half-width (double)(0.6F/2.0F) = 0.30000001192092896 and height (double)1.8F =
 * 1.7999999523162842 -- the FLOAT literals widened to double, NOT 0.3/1.8. The live
 * game rests against a wall at blockface + 0.30000001192092896; with an exact-0.3 box
 * every post-collision position is off by 1.19e-8 vs the real game. mc_pcm_player_box
 * (exact 0.3/1.8) is left untouched: the baked collision scenarios mirror Golden.java's
 * plain-double literals. */
MC_HD static inline McAABB psv_player_box(double px, double py, double pz) {
    const double hw = 0.30000001192092896;   /* (double)(0.6F / 2.0F) */
    const double hh = 1.7999999523162842;    /* (double)1.8F          */
    return mc_aabb_make(px - hw, py, pz - hw, px + hw, py + hh, pz + hw);
}

MC_HD static inline void psv_player_init(PsvPlayer *pl) {
    McEntity *e = &pl->ent;
    e->posX = PSV_SPAWN_X; e->posY = PSV_SPAWN_Y; e->posZ = PSV_SPAWN_Z;
    e->box = psv_player_box(e->posX, e->posY, e->posZ);
    e->motionX = e->motionY = e->motionZ = 0.0;
    e->onGround = 0; e->collidedHorizontally = 0; e->collidedVertically = 0; e->isCollided = 0;
    pl->yaw = 0.0f; pl->pitch = 0.0f;
    pl->health = PSV_MAX_HEALTH;
    pl->food = PSV_MAX_FOOD;
    pl->fall_distance = 0.0f;
    pl->sprinting = 0; pl->sprint_toggle_timer = 0; pl->jump_factor_sprint = 0;
    pl->jump_ticks = 0;
    pl->prev_move_forward = 0.0f; pl->prev_sneak = 0;
    pl->break_events = pl->place_events = pl->swing_events = 0;
    isr_init(&pl->inv);
    pl->inv.current_item = 0;
    pl->inv.main[0] = ic_mk(BLK_COBBLESTONE, 64, 0);   /* held blocks so place always has stock */
}

/* Full run under explicit GameRules; returns the final player state via *out_pl (may be NULL).
 * out (may be NULL) receives PSV_FIELDS u64 per tick as in psv_run. */
MC_HD static inline void psv_run_gr(Chunk *a, Chunk *b, ChunkPrimer *primer, CpScratch *sc,
                                    const McSinTable *st, i64 seed, int nticks,
                                    const McGameRules *gr, PsvPlayer *out_pl, u64 *out) {
    PsvPlayer pl;
    McAABB blocks[PSV_MAX_BLOCKS];
    int cur = 0, t;

    psv_gen(a, b, primer, sc, st, seed);
    psv_player_init(&pl);

    for (t = 0; t < nticks; ++t) {
        Chunk *now = cur ? b : a;
        Chunk *next = cur ? a : b;
        psv_tick_gr(now, next, st, &pl, seed, t, blocks, gr);
        cur ^= 1;
        {
            Chunk *cur_now = cur ? b : a;   /* post-swap buffer holds this tick's edits */
            if (out) psv_emit(cur_now, &pl, &out[(size_t)t * PSV_FIELDS]);
        }
    }
    if (out_pl) *out_pl = pl;
}

/* Full run: gen region, spawn player, tick PSV_NTICKS, emit PSV_FIELDS u64 per tick.
 * Default rules (doTileDrops=1): bit-identical to the prior psv_run. */
MC_HD static inline void psv_run(Chunk *a, Chunk *b, ChunkPrimer *primer, CpScratch *sc,
                                 const McSinTable *st, i64 seed, int nticks, u64 *out) {
    McGameRules gr = mc_gamerules_default();
    psv_run_gr(a, b, primer, sc, st, seed, nticks, &gr, 0, out);
}

#endif /* MC_PLAYER_SURVIVAL_H */
