/* blaze_core.h - batched-env core for the mine-coal learned stage: one
 * MC_HD single source that reproduces the REAL env (magma_game --rl-bin
 * restored from a .bsnp snapshot) tick-for-tick, bit-for-bit, over the
 * snapshot's 64x128x64 region. CPU reference driver: blaze_cpu.c; the CUDA
 * driver (M2) compiles this same header under nvcc.
 *
 * Fidelity contract (see blaze/env/DESIGN.md Part 3): every function here is
 * a faithful port of the exact code the real env runs for the FULL
 * spawn-to-torch chain action set (forward/strafe/jump/sneak/sprint/attack/
 * use + dyaw/dpitch + hotbar + the pre-tick protocol primitives craft:N and
 * interact:1). Sources, with the per-env-ified statics:
 *   game/runtime.c   gm_runtime_tick slice + recenter() + gm_runtime_craft
 *                     + gm_runtime_use_block (table/furnace open)
 *   game/player_ctl.c gm_player_tick (dig machine, sprint, vitals, hurt-vel
 *                     shadow, rightClick timer + FIRE path: bucket /
 *                     interactable toggle / block place, eating) - statics
 *                     -> Blaze fields
 *   game/sel_box.c   gm_sel_box / gm_raycast_sel_reach (dig/use targeting)
 *   game/live_sim.c  gm_live_tick(+_player) item physics + pickup
 *   game/rl_mode.c   rl_emit_obs coal list + camera + inv_counts + BOLR,
 *                     rl_do_craft / rl_do_interact
 * blaze kernels (psv_physics_tick, pb_*, isr_*, ita_*, pv_*, ib_*, ibp_*,
 * crf_*, oc_pixel) are reused VERBATIM via include - never re-implemented.
 *
 * Deliberately NOT simulated (inert or impossible with the supported item
 * set; the snapshot baker flags offenders): fluids CA (snapshots with liquid
 * ids 8-11 are flagged), mobs/dragon/projectiles, weather/clock, portals,
 * bow/eye-of-ender (need held item 261/381 - neither is craftable in-chain).
 * Furnaces ARE simulated since the iron extension (game/furnace_live.c +
 * runtime.c furnace slots ported below; "smelt":1 primitive) - but furnace
 * state is NOT in .bsnp, so snapshots must be baked with no active furnace
 * (same quiescence contract as eat_ticks).
 *
 * Coordinate discipline: the env runs in the SAME window-local frame the
 * .bsnp stored (local pose + ox/oz origin), and replicates runtime.c's
 * recenter() on chunk crossings - double math is translation-sensitive, so
 * the frame must track the real env's exactly. Region/world reads happen at
 * world = local + origin; physics/dig reads go through a real Chunk[9]
 * window (filled from the region) so psv_* iteration order and out-of-window
 * clipping are IDENTICAL to the real env by construction.
 *
 * Allocation rule: no malloc here; all buffers are caller-allocated once at
 * create (cells/cam_cells/window/obs per env, McAABB scratch per worker). */
#ifndef BLAZE_CORE_H
#define BLAZE_CORE_H

#include <math.h>
#include <limits.h>
#include <string.h>

#include "player_survival.h"    /* Chunk, McSinTable, PsvPlayer, psv_* verbatim */
#include "player_vitals.h"      /* PvStats, pv_* verbatim */
#include "player_break.h"       /* PbInput, pb_* verbatim */
#include "items_tools_armor.h"  /* ita_* verbatim */
#include "mc_blocks.h"
#include "obs_camera.h"         /* OcRegion, oc_pixel verbatim */
#include "interact_blocks.h"    /* ib_apply (use on doors/levers/...) verbatim */
#include "item_block_place.h"   /* ibp_placed_meta (place orientation) verbatim */
#include "crafting_recipes_full.h" /* crf_build/crf_findMatching verbatim */
#include "furnace_full_tick.h"  /* fft_tick + sr_* smelt/fuel verbatim */
#include "blaze_snapshot.h"     /* RlSnapHead/RlSnapItem (.bsnp format) */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- fixed shapes (must match game/rl_mode.c) ---- */
#define CU_CAM_W    64
#define CU_CAM_H    36
#define CU_NPIX     (CU_CAM_W * CU_CAM_H)
#define CU_OBS_R    16          /* RL_OBS_R   */
#define CU_Y_DOWN   24          /* RL_Y_DOWN  */
#define CU_Y_UP     40          /* RL_Y_UP    */
#define CU_NCOAL    32          /* RL_NCOAL   */
#define CU_NBLOCKS  256         /* RL_NBLOCKS (emitted zeroed)   */
#define CU_NLOGS    64          /* RL_NLOGS   (emitted zeroed)   */
#define CU_MAX_ITEMS 48         /* GM_LIVE_MAX */
#define CU_MAX_EDITS 8          /* GM_RUNTIME_MAX_EDITS */
#define CU_COAL_SCRATCH 512     /* rl_mode coalblk[512] cap */
#define CU_COAL_CAND 1024       /* per-env geometric candidate cache cap */
#define CU_BIN_MAGIC 0x524c4f42u

/* Region geometry comes from the loaded snapshot header (rl_snapshot_write's
 * "snapshot_r" radius); the per-env buffers are sized from it at create.
 * INVARIANT: rny <= 128 (cu_recenter_fill relies on the y>=128
 * half of every window chunk staying air forever) - the loader enforces it. */
#define CU_RNY_MAX 128

/* inv_counts item ids (rl_mode.c rl_inv_ids) */
#define CU_INV_IDS {17, 5, 280, 4, 58, 270, 274, 263, 50}

/* ---- op-trace activity counters (BLAZE_OP_TRACE=1 at driver create) ----
 * Per-env u64 counters of the fundamental ops one tick performs, for the
 * zoom-in (single-env histogram) / zoom-out (cross-env activity correlation)
 * traces that decide which k_tick phases become compact vs lockstep kernels.
 * env->ops == NULL (the default) => the only hot-path cost is a predictable
 * null test; counters never feed back into sim state, so evolution stays
 * bit-identical either way. Single writer per env by construction: the tick
 * runs one thread per env, and the warp-cooperative recenter fill counts on
 * lane 0 only. NOT zeroed by reset (cumulative over a run; the driver reads
 * them out via blaze_op_trace). */
enum {
    CU_OP_WORLD_LOAD = 0,  /* cu_world_block/meta region reads */
    CU_OP_WORLD_EDIT,      /* cu_world_set_state region writes */
    CU_OP_RECENTER,        /* chunk-crossing recenters (pose shifts) */
    CU_OP_FILL_CELL,       /* window cells refilled from the region */
    CU_OP_RAYCAST,         /* cu_raycast_sel_reach calls */
    CU_OP_RAY_STEP,        /* raycast inner-loop steps */
    CU_OP_DIG_TICK,        /* dig-progress advance ticks */
    CU_OP_DIG_BREAK,       /* blocks destroyed by digging */
    CU_OP_ITEM_TICK,       /* live item entity ticks */
    CU_OP_CRAFT,           /* craft primitive attempts */
    CU_OP_INTERACT,        /* interact primitive attempts */
    CU_OP_SMELT,           /* smelt primitive attempts */
    CU_OP_FURNACE_TICK,    /* active furnace ticks */
    CU_OP_PHYS_TICK,       /* psv_physics_tick calls (player move) */
    CU_OP_COAL_CALL,       /* blaze_coal_list calls */
    CU_OP_COAL_REBUILD,    /* coal candidate cache rebuilds */
    CU_OP_COAL_SWEEP,      /* coal candidate entries swept */
    CU_OP_INV_SCAN,        /* blaze_inv_count 36-slot sweeps */
    CU_OP_SUBTICK,         /* decision sub-ticks executed */
    CU_OP_N
};
#define CU_OP(e, k) do { if ((e)->ops) (e)->ops[k]++; } while (0)
#define CU_OP_ADD(e, k, v) \
    do { if ((e)->ops) (e)->ops[k] += (unsigned long long)(v); } while (0)

/* ---- action (GmAction protocol subset; craft/interact are separate
 * pre-tick primitives - blaze_do_craft / blaze_do_interact) ---- */
typedef struct {
    float forward, strafe, dyaw, dpitch;
    int   jump, sneak, sprint, attack, use;
    int   hotbar_sel;            /* 0..8 or -1 = unchanged */
} CuAction;

/* ---- live item entity (GmLiveEnt fields, live_sim.h) ---- */
typedef struct {
    int    active;
    double x, y, z, mx, my, mz;
    int    on_ground, age, item, count, meta, pickup_delay, lifespan;
} CuItem;

/* ---- block edit (GmBlockEdit fields, game.h) ---- */
typedef struct {
    int wx, wy, wz, id, meta, drop_id, drop_count, drop_meta, harvest_tool;
} CuEdit;

/* ---- live furnace (GmRuntimeFurnace + FurnaceLive fields, runtime.h /
 * furnace_live.h). NOT part of .bsnp - bake contract: quiescent. ---- */
#define CU_MAX_FURNACES 16      /* GM_RUNTIME_FURNACES */
typedef struct {
    int active, wx, wy, wz;
    SRStack input, fuel, output;
    i32 burn_time, current_burn_time, cook_time, total_cook;
} CuFurnace;

/* ---- binary obs record: byte-layout twin of rl_mode.c RlBinObs. blocks/
 * logs are emitted ZEROED (their membership rides the real env's scan-cache
 * rebuild cadence; the verify gate excludes them - compare coal/cam/depth/
 * edge/pose/inv only). ---- */
#pragma pack(push, 1)
typedef struct {
    unsigned magic;
    long long tick;
    double x, y, z;
    float yaw, pitch;
    int dead;
    int hotbar_ids[9];
    int hotbar_counts[9];
    int hotbar_sel;
    int container;
    int inv_counts[9];
    int blocks[CU_NBLOCKS][4];
    int logs[CU_NLOGS][3];
    int coal[CU_NCOAL][3];
    unsigned short cam[CU_NPIX];
    unsigned char depth[CU_NPIX];
    unsigned char edge[CU_NPIX];
} CuBinObs;
#pragma pack(pop)

/* ---- one env. Pointers reference caller-allocated pools (create-time). */
typedef struct {
    /* large per-env buffers (pool) */
    u16   *cells;                /* region packed states (id<<4)|meta       */
    u16   *cam_cells;            /* region plain ids (oc_pixel input)       */
    Chunk *window;               /* PSV_NCHUNKS physics window (blocks only)*/
    u16   *cam;                  /* last rendered frame (want_cam=0 reuse)  */
    u8    *dep, *edg;
    const int *ore;              /* snapshot static coal list, ncoal x 3    */
    int    nore;
    const int *ore_xy;           /* snapshot CSR (ix,iy)->ore-range offsets
                                  * (rnx*rny+1; blaze_build_ore_xy), NULL =
                                  * full-scan candidate rebuild             */
    struct CuCand *coal_cand;    /* pooled CU_COAL_CAND candidate cache     */
    int   *cont;                 /* pooled container list (wx,wy,wz x n_cont;
                                  * ids 58/61/62), seeded from the snapshot at
                                  * reset and maintained by every region edit
                                  * - blaze_do_interact iterates it instead of
                                  * scanning the 33x33x65 window             */
    int    n_cont;               /* -1 = overflow: full window scan fallback */
    int    n_cand;               /* cached count; -1 = overflow (full scan) */
    int    cand_pwx, cand_pwy, cand_pwz, cand_valid;
    int    world_epoch;          /* bumped by every region cell write        */
    int    cand_epoch;           /* world_epoch the cached mined flags saw   */

    int rx0, ry0, rz0;           /* region origin (world) */
    int rnx, rny, rnz;           /* region dims (snapshot header) */
    long rvol;                   /* rnx * rny * rnz */

    /* player + vitals (window-local pose) */
    PsvPlayer pl;
    PvStats   vit;
    int ox, oz, ccx, ccz;        /* physics window origin */
    long long tick, seed;
    int dead, deaths;

    /* player_ctl.c statics, per-env-ified */
    float  dig_progress;
    int    dig_hx, dig_hy, dig_hz;   /* window-local; INT_MIN = none */
    int    dig_hitting, dig_delay, atk_prev;
    int    rc_delay, use_prev;
    int    eat_ticks, eat_item;      /* s_eat_* (NOT in .bsnp: snapshots are
                                      * taken at quiescent points, both 0) */
    int    hurt_vel_reset;
    double server_motion_x, server_motion_z;

    int container, container_wx, container_wy, container_wz;

    /* live furnaces (runtime.c furnace slots, per-env-ified) */
    CuFurnace furnaces[CU_MAX_FURNACES];
    int active_furnace;

    CuItem items[CU_MAX_ITEMS];
    int    n_items;

    /* reward/done bookkeeping (driver-level; not part of the sim gate) */
    int    base_coal;
    int    success_item;         /* +10/done=1 item id; 263 = legacy coal,
                                  * 0 = disabled (driver-set, applied at
                                  * reset; default preserves exact ppo_coal
                                  * semantics) */
    int    base_success;         /* inv count of success_item at reset */
    double prev_dist;
    int    have_prev_dist;
    int    done;                 /* 0 live, 1 coal mined, 2 out/dead */

    /* per-decision scratch (blaze_decision_ticks -> blaze_decision_finalize;
     * split so the CUDA driver can run the per-pixel camera kernel between
     * them - the last executed sub-tick's crosshair bonus reads the DECISION
     * frame's center pixel, rendered by k_obs). */
    double dec_rew_pre;          /* summed r for all but the last executed tick */
    double dec_r_last;           /* last executed tick's r, pre crosshair/+10 */
    int    dec_have_last;        /* any sub-tick executed this decision */
    int    dec_plus10;           /* +10 fired on the last executed tick */
    int    dec_attack;           /* attack head this decision */
    int    dec_cam_fresh;        /* sub-tick repeat-1 ran -> render the camera */
    int    dec_have_nc;          /* nearest-coal validity from the last tick */
    double dec_ry, dec_rp, dec_dist;

    /* op-trace counters (driver-owned pool slice, CU_OP_N u64s; NULL = off).
     * Set once at create like the other pool pointers; reset does NOT touch
     * it (counters are cumulative). Not sim state - never verified. */
    unsigned long long *ops;
} Blaze;

/* =================== region / window accessors =================== */

MC_HD static inline long cu_region_idx(const Blaze *e, int wx, int wy, int wz) {
    int ix = wx - e->rx0, iy = wy - e->ry0, iz = wz - e->rz0;
    if (ix < 0 || iy < 0 || iz < 0 || ix >= e->rnx || iy >= e->rny || iz >= e->rnz)
        return -1;
    return ((long)ix * e->rny + iy) * e->rnz + iz;
}

/* gm_world_block/gm_world_meta equivalent over the snapshot region: outside
 * the region (or y outside [0,127]) reads as air, mirroring the design's
 * out-of-region = air rule. Items/plants/obs use these (WORLD coords). */
MC_HD static inline int cu_world_block(const Blaze *e, int wx, int wy, int wz) {
    long i = cu_region_idx(e, wx, wy, wz);
    CU_OP(e, CU_OP_WORLD_LOAD);
    return i < 0 ? 0 : mc_state_id(e->cells[i]);
}
MC_HD static inline int cu_world_meta(const Blaze *e, int wx, int wy, int wz) {
    long i = cu_region_idx(e, wx, wy, wz);
    CU_OP(e, CU_OP_WORLD_LOAD);
    return i < 0 ? 0 : mc_state_meta(e->cells[i]);
}

/* window write (player_ctl.c psv_set_state: window-LOCAL coords) */
MC_HD static inline void cu_win_set_state(Chunk *chunks, int wx, int wy, int wz,
                                          int id, int meta) {
    int lx, lz, ci;
    if (wy < 0 || wy > 255) return;
    ci = psv_chunk_index(wx, wz, &lx, &lz);
    if (ci < 0) return;
    mc_set(&chunks[ci], lx, wy, lz, mc_state(id, meta & 15));
}

MC_HD static inline int cu_is_container_id(int id) {
    return id == 58 || id == 61 || id == 62;
}

/* container-list maintenance on a region cell transition. Swap-remove keeps
 * the list an exact UNORDERED mirror of the region's container cells (the
 * interact pick below is order-independent by its total order). Append past
 * the cap poisons the list (-1) until the next reset - consumers fall back
 * to the full window scan, value-identical. */
MC_HD static inline void cu_cont_edit(Blaze *e, int wx, int wy, int wz,
                                      int old_id, int new_id) {
    int oc = cu_is_container_id(old_id), nc = cu_is_container_id(new_id), c;
    if (e->n_cont < 0 || oc == nc) return;
    if (oc) {
        for (c = 0; c < e->n_cont; ++c)
            if (e->cont[c * 3 + 0] == wx && e->cont[c * 3 + 1] == wy &&
                e->cont[c * 3 + 2] == wz) {
                e->n_cont--;
                e->cont[c * 3 + 0] = e->cont[e->n_cont * 3 + 0];
                e->cont[c * 3 + 1] = e->cont[e->n_cont * 3 + 1];
                e->cont[c * 3 + 2] = e->cont[e->n_cont * 3 + 2];
                return;
            }
        return;   /* unreachable: the list mirrors the region */
    }
    if (e->n_cont >= BLAZE_SNAP_MAX_CONT) { e->n_cont = -1; return; }
    e->cont[e->n_cont * 3 + 0] = wx;
    e->cont[e->n_cont * 3 + 1] = wy;
    e->cont[e->n_cont * 3 + 2] = wz;
    e->n_cont++;
}

/* world write: region + camera ids + window mirror (the real env writes the
 * GmWorld and refills the physics window from it next tick; value-identical). */
MC_HD static inline void cu_world_set_state(Blaze *e, int wx, int wy, int wz,
                                            int id, int meta) {
    long i = cu_region_idx(e, wx, wy, wz);
    if (i >= 0) {
        cu_cont_edit(e, wx, wy, wz, mc_state_id(e->cells[i]), id);
        e->cells[i] = mc_state(id, meta);
        e->cam_cells[i] = (u16)(id & 0xFFF);
        e->world_epoch++;        /* coal candidate mined-flag invalidation */
        CU_OP(e, CU_OP_WORLD_EDIT);
    }
    cu_win_set_state(e->window, wx - e->ox, wy, wz - e->oz, id, meta);
}

/* The PSV 3x3-chunk physics window layout is exactly what
 * gm_world_fill_window produces (chunk (dz+1)*3+(dx+1) holds world chunk
 * (ccx+dx, ccz+dz)); cells outside the region read air. Blocks only - the
 * physics never reads light/biome. Fills go through cu_fill_chunk_lanes. */

/* lane barrier for the cooperative recenter fill: a no-op on the CPU / in
 * serial (nlanes==1) callers; a full-warp __syncwarp on CUDA (the k_tick
 * cooperative path keeps every lane of the warp alive by construction). */
MC_HD static inline void cu_sync_lanes(int nlanes) {
#if defined(__CUDA_ARCH__)
    if (nlanes > 1) __syncwarp();
#else
    (void)nlanes;
#endif
}

/* fill ONE window chunk from the region, y 0..ylim-1, cells strided over
 * `nlanes` cooperating lanes (lane, lane+nlanes, ...). lz varies fastest so
 * adjacent lanes read adjacent region cells (region z is the contiguous
 * axis). lane 0 stamps the chunk coords. Serial (0,1) call = the old
 * cu_fill_chunk_lo / cu_fill_window per-chunk body, value-identical (the
 * writes are disjoint, so iteration order cannot matter). */
MC_HD static inline void cu_fill_chunk_lanes(Blaze *e, Chunk *ch, int cx, int cz,
                                             int ylim, int lane, int nlanes) {
    int c, ncell = MC_CX * MC_CZ * ylim;
    int bx = cx * 16, bz = cz * 16;
    if (lane == 0) { ch->cx = cx; ch->cz = cz; CU_OP_ADD(e, CU_OP_FILL_CELL, ncell); }
    for (c = lane; c < ncell; c += nlanes) {
        int lz = c % MC_CZ, lx = (c / MC_CZ) % MC_CX, y = c / (MC_CZ * MC_CX);
        long ri = cu_region_idx(e, bx + lx, y, bz + lz);
        ch->blocks[mc_idx(lx, y, lz)] = ri < 0 ? 0 : e->cells[ri];
    }
}

/* runtime.c recenter(), pose half: shift the local frame when the player
 * crosses a chunk boundary. Returns 1 (with the chunk shift in odcx/odcz)
 * when a crossing happened - the window must then be refilled with
 * cu_recenter_fill. The dig target (dig_hx/hy/hz) is window-local and is NOT
 * shifted - the real env's player_ctl statics aren't either (the next dig
 * tick sees a "target change" and re-acquires; faithful, if quirky). */
MC_HD static inline int cu_recenter_pose(Blaze *e, int *odcx, int *odcz) {
    double wx = e->pl.ent.posX + e->ox;
    double wz = e->pl.ent.posZ + e->oz;
    int nccx = psv_floordiv16(mc_floor(wx));
    int nccz = psv_floordiv16(mc_floor(wz));
    double dx, dz;
    int dcx, dcz;
    if (nccx == e->ccx && nccz == e->ccz) return 0;
    dcx = nccx - e->ccx;
    dcz = nccz - e->ccz;
    dx = (double)(dcx * 16);
    dz = (double)(dcz * 16);
    e->ccx = nccx; e->ccz = nccz;
    e->ox = nccx * 16; e->oz = nccz * 16;
    e->pl.ent.posX -= dx; e->pl.ent.posZ -= dz;
    e->pl.ent.box.minX -= dx; e->pl.ent.box.maxX -= dx;
    e->pl.ent.box.minZ -= dz; e->pl.ent.box.maxZ -= dz;
    *odcx = dcx;
    *odcz = dcz;
    CU_OP(e, CU_OP_RECENTER);
    return 1;
}

/* runtime.c recenter(), fill half - value-identical to a full window fill
 * (window blocks are a pure function of the region + chunk coords): retained
 * chunks move between slots by copying the y<128 block half (64 KB; y>=128
 * is air forever by the CU_RNY_MAX invariant), only the newly exposed chunks
 * are filled from the region. Destination slots are walked in
 * shift-direction order so each retained source slot is read before it is
 * overwritten; with nlanes cooperating lanes each slot's copy/fill is
 * lane-strided and a lane barrier between slots preserves that order.
 * ccx/ccz/dcx/dcz are passed by value so cooperating lanes never read
 * another lane's freshly-written env fields (only the untouched region
 * geometry + pool pointers). Serial reference call: (e->ccx, e->ccz, dcx,
 * dcz, 0, 1). */
MC_HD static inline void cu_recenter_fill(Blaze *e, int nccx, int nccz,
                                          int dcx, int dcz,
                                          int lane, int nlanes) {
    int xs, xe, xstep, zs, ze, zstep, dxl, dzl;
    if (dcx < -(PSV_DIM - 1) || dcx > PSV_DIM - 1 ||
        dcz < -(PSV_DIM - 1) || dcz > PSV_DIM - 1) {
        /* no chunk survives the shift: full 0..255 fill (== cu_fill_window) */
        for (dzl = 0; dzl < PSV_DIM; ++dzl)
            for (dxl = 0; dxl < PSV_DIM; ++dxl)
                cu_fill_chunk_lanes(e, &e->window[dzl * PSV_DIM + dxl],
                                    nccx + dxl - PSV_R, nccz + dzl - PSV_R,
                                    MC_CY, lane, nlanes);
        return;
    }
    if (dcx >= 0) { xs = 0; xe = PSV_DIM; xstep = 1; }
    else          { xs = PSV_DIM - 1; xe = -1; xstep = -1; }
    if (dcz >= 0) { zs = 0; ze = PSV_DIM; zstep = 1; }
    else          { zs = PSV_DIM - 1; ze = -1; zstep = -1; }
    for (dzl = zs; dzl != ze; dzl += zstep)
        for (dxl = xs; dxl != xe; dxl += xstep) {
            /* new slot (dxl,dzl) holds world chunk (nccx+dxl-R, nccz+dzl-R);
             * under the OLD center that chunk sat in slot (dxl+dcx, dzl+dcz) */
            int sxl = dxl + dcx, szl = dzl + dcz;
            Chunk *dst = &e->window[dzl * PSV_DIM + dxl];
            if (sxl >= 0 && sxl < PSV_DIM && szl >= 0 && szl < PSV_DIM) {
                const Chunk *src = &e->window[szl * PSV_DIM + sxl];
                unsigned long long *d = (unsigned long long *)dst->blocks;
                const unsigned long long *s =
                    (const unsigned long long *)src->blocks;
                int q;
                for (q = lane; q < CU_RNY_MAX * MC_CX * MC_CZ * 2 / 8;
                     q += nlanes)
                    d[q] = s[q];
                if (lane == 0) {
                    dst->cx = nccx + dxl - PSV_R;
                    dst->cz = nccz + dzl - PSV_R;
                }
            } else {
                cu_fill_chunk_lanes(e, dst, nccx + dxl - PSV_R,
                                    nccz + dzl - PSV_R, CU_RNY_MAX,
                                    lane, nlanes);
            }
            cu_sync_lanes(nlanes);
        }
}

/* serial recenter (CPU driver + single-thread verify kernels): pose + fill.
 * Identical statements and order to the pre-split cu_recenter. */
MC_HD static inline void cu_recenter(Blaze *e) {
    int dcx, dcz;
    if (cu_recenter_pose(e, &dcx, &dcz))
        cu_recenter_fill(e, e->ccx, e->ccz, dcx, dcz, 0, 1);
}

/* =================== selection raycast (game/sel_box.c port) =============== */

typedef struct {
    int id, meta;
    int nid[4];          /* N,S,W,E neighbour ids (z-1, z+1, x-1, x+1) */
    int below_meta, above_meta;
} CuSelIn;

MC_HD static inline void cu_set6(float b[6], float x0, float y0, float z0,
                                 float x1, float y1, float z1) {
    b[0] = x0; b[1] = y0; b[2] = z0; b[3] = x1; b[4] = y1; b[5] = z1;
}

MC_HD static inline int cu_is_fence(int id)  { return id == 85 || id == 113 || (id >= 188 && id <= 192); }
MC_HD static inline int cu_is_gate(int id)   { return id == 107 || (id >= 183 && id <= 187); }
MC_HD static inline int cu_is_pane(int id)   { return id == 101 || id == 102 || id == 160; }
MC_HD static inline int cu_is_glassy(int id) { return id == 20 || id == 95; }
MC_HD static inline int cu_is_solid_cube(int id) { return (mc_bpt_props(id).flags & BF_SOLID) != 0; }
MC_HD static inline int cu_is_rs_component(int id) {
    return id == 55 || id == 75 || id == 76 || id == 93 || id == 94 ||
           id == 149 || id == 150 || id == 69 || id == 77 || id == 143 ||
           id == 70 || id == 72 || id == 147 || id == 148 || id == 151 ||
           id == 178 || id == 152;
}

MC_HD static inline void cu_torch_box(int meta, float b[6]) {
    switch (meta) {
    case 1: cu_set6(b, 0.0f, 0.2f, 0.35f, 0.3f, 0.8f, 0.65f); break;
    case 2: cu_set6(b, 0.7f, 0.2f, 0.35f, 1.0f, 0.8f, 0.65f); break;
    case 3: cu_set6(b, 0.35f, 0.2f, 0.0f, 0.65f, 0.8f, 0.3f); break;
    case 4: cu_set6(b, 0.35f, 0.2f, 0.7f, 0.65f, 0.8f, 1.0f); break;
    default: cu_set6(b, 0.4f, 0.0f, 0.4f, 0.6f, 0.6f, 0.6f); break;
    }
}

/* verbatim gm_sel_box (sel_box.c:33) */
MC_HD static inline void cu_sel_box(const CuSelIn *in, float b[6]) {
    int id = in->id, meta = in->meta;
    cu_set6(b, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f);
    switch (id) {
    case 29: case 33:
        if (meta & 8) {
            switch (meta & 7) {
            case 0: cu_set6(b, 0.f, 0.25f, 0.f, 1.f, 1.f, 1.f); break;
            case 1: cu_set6(b, 0.f, 0.f, 0.f, 1.f, 0.75f, 1.f); break;
            case 2: cu_set6(b, 0.f, 0.f, 0.25f, 1.f, 1.f, 1.f); break;
            case 3: cu_set6(b, 0.f, 0.f, 0.f, 1.f, 1.f, 0.75f); break;
            case 4: cu_set6(b, 0.25f, 0.f, 0.f, 1.f, 1.f, 1.f); break;
            case 5: cu_set6(b, 0.f, 0.f, 0.f, 0.75f, 1.f, 1.f); break;
            default: break;
            }
        }
        break;
    case 6:  case 31: case 32:
        cu_set6(b, 0.1f, 0.f, 0.1f, 0.9f, 0.8f, 0.9f); break;
    case 37: case 38:
        cu_set6(b, 0.3f, 0.f, 0.3f, 0.7f, 0.6f, 0.7f); break;
    case 39: case 40:
        cu_set6(b, 0.3f, 0.f, 0.3f, 0.7f, 0.4f, 0.7f); break;
    case 59: case 141: case 142:
        cu_set6(b, 0.f, 0.f, 0.f, 1.f, (float)((meta & 7) + 1) * 0.125f, 1.f); break;
    case 104: case 105:
        cu_set6(b, 0.375f, 0.f, 0.375f, 0.625f, (float)((meta & 7) + 1) * 0.125f, 0.625f); break;
    case 115:
    {
        const float wart_h[4] = { 0.3125f, 0.5f, 0.6875f, 0.875f };
        cu_set6(b, 0.f, 0.f, 0.f, 1.f, wart_h[meta & 3], 1.f); break;
    }
    case 83:  cu_set6(b, 0.125f, 0.f, 0.125f, 0.875f, 1.f, 0.875f); break;
    case 81:  cu_set6(b, 0.0625f, 0.f, 0.0625f, 0.9375f, 1.f, 0.9375f); break;
    case 111: cu_set6(b, 0.0625f, 0.f, 0.0625f, 0.9375f, 0.09375f, 0.9375f); break;
    case 44: case 126: case 182:
        if (meta & 8) cu_set6(b, 0.f, 0.5f, 0.f, 1.f, 1.f, 1.f);
        else          cu_set6(b, 0.f, 0.f, 0.f, 1.f, 0.5f, 1.f);
        break;
    case 78:
        cu_set6(b, 0.f, 0.f, 0.f, 1.f, (float)((meta & 7) + 1) * 0.125f, 1.f); break;
    case 60:  cu_set6(b, 0.f, 0.f, 0.f, 1.f, 0.9375f, 1.f); break;
    case 88:  cu_set6(b, 0.f, 0.f, 0.f, 1.f, 0.875f, 1.f); break;
    case 171: cu_set6(b, 0.f, 0.f, 0.f, 1.f, 0.0625f, 1.f); break;
    case 26:  cu_set6(b, 0.f, 0.f, 0.f, 1.f, 0.5625f, 1.f); break;
    case 116: cu_set6(b, 0.f, 0.f, 0.f, 1.f, 0.75f, 1.f); break;
    case 151: case 178: cu_set6(b, 0.f, 0.f, 0.f, 1.f, 0.375f, 1.f); break;
    case 93: case 94: case 149: case 150:
        cu_set6(b, 0.f, 0.f, 0.f, 1.f, 0.125f, 1.f); break;
    case 117:
        cu_set6(b, 0.f, 0.f, 0.f, 1.f, 0.125f, 1.f); break;
    case 120: cu_set6(b, 0.f, 0.f, 0.f, 1.f, 0.8125f, 1.f); break;
    case 27: case 28: case 66: case 157:
        cu_set6(b, 0.f, 0.f, 0.f, 1.f, 0.125f, 1.f); break;
    case 70: case 72: case 147: case 148:
        cu_set6(b, 0.0625f, 0.f, 0.0625f, 0.9375f, meta ? 0.03125f : 0.0625f, 0.9375f); break;
    case 132:
        if (meta & 4) cu_set6(b, 0.f, 0.0625f, 0.f, 1.f, 0.15625f, 1.f);
        else          cu_set6(b, 0.f, 0.f, 0.f, 1.f, 0.5f, 1.f);
        break;
    case 92:
        cu_set6(b, 0.0625f + (float)(meta & 7) * 0.125f, 0.f, 0.0625f,
                0.9375f, 0.5f, 0.9375f); break;
    case 50: case 75: case 76: cu_torch_box(meta, b); break;
    case 65:
        switch (meta) {
        case 2: cu_set6(b, 0.f, 0.f, 0.8125f, 1.f, 1.f, 1.f); break;
        case 3: cu_set6(b, 0.f, 0.f, 0.f, 1.f, 1.f, 0.1875f); break;
        case 4: cu_set6(b, 0.8125f, 0.f, 0.f, 1.f, 1.f, 1.f); break;
        default: cu_set6(b, 0.f, 0.f, 0.f, 0.1875f, 1.f, 1.f); break;
        } break;
    case 68:
        switch (meta) {
        case 2: cu_set6(b, 0.f, 0.28125f, 0.875f, 1.f, 0.78125f, 1.f); break;
        case 3: cu_set6(b, 0.f, 0.28125f, 0.f, 1.f, 0.78125f, 0.125f); break;
        case 4: cu_set6(b, 0.875f, 0.28125f, 0.f, 1.f, 0.78125f, 1.f); break;
        default: cu_set6(b, 0.f, 0.28125f, 0.f, 0.125f, 0.78125f, 1.f); break;
        } break;
    case 63: cu_set6(b, 0.25f, 0.f, 0.25f, 0.75f, 1.f, 0.75f); break;
    case 69:
        switch (meta & 7) {
        case 1: cu_set6(b, 0.f, 0.2f, 0.3125f, 0.375f, 0.8f, 0.6875f); break;
        case 2: cu_set6(b, 0.625f, 0.2f, 0.3125f, 1.f, 0.8f, 0.6875f); break;
        case 3: cu_set6(b, 0.3125f, 0.2f, 0.f, 0.6875f, 0.8f, 0.375f); break;
        case 4: cu_set6(b, 0.3125f, 0.2f, 0.625f, 0.6875f, 0.8f, 1.f); break;
        case 5: case 6: cu_set6(b, 0.25f, 0.f, 0.25f, 0.75f, 0.6f, 0.75f); break;
        default: cu_set6(b, 0.25f, 0.4f, 0.25f, 0.75f, 1.f, 0.75f); break;
        } break;
    case 77: case 143:
        switch (meta & 7) {
        case 0: cu_set6(b, 0.3125f, 0.875f, 0.375f, 0.6875f, 1.f, 0.625f); break;
        case 1: cu_set6(b, 0.f, 0.375f, 0.3125f, 0.125f, 0.625f, 0.6875f); break;
        case 2: cu_set6(b, 0.875f, 0.375f, 0.3125f, 1.f, 0.625f, 0.6875f); break;
        case 3: cu_set6(b, 0.3125f, 0.375f, 0.f, 0.6875f, 0.625f, 0.125f); break;
        case 4: cu_set6(b, 0.3125f, 0.375f, 0.875f, 0.6875f, 0.625f, 1.f); break;
        default: cu_set6(b, 0.3125f, 0.f, 0.375f, 0.6875f, 0.125f, 0.625f); break;
        } break;
    case 106:
        if (meta == 0) { cu_set6(b, 0.f, 0.9375f, 0.f, 1.f, 1.f, 1.f); break; }
        {
            float x0 = 1.f, z0 = 1.f, x1 = 0.f, z1 = 0.f;
            if (meta & 1) { x0 = 0.f; x1 = 1.f; if (z0 > 0.9375f) z0 = 0.9375f; z1 = 1.f; }
            if (meta & 2) { z0 = 0.f; z1 = 1.f; x0 = 0.f; if (x1 < 0.0625f) x1 = 0.0625f; }
            if (meta & 4) { x0 = 0.f; x1 = 1.f; z0 = 0.f; if (z1 < 0.0625f) z1 = 0.0625f; }
            if (meta & 8) { z0 = 0.f; z1 = 1.f; if (x0 > 0.9375f) x0 = 0.9375f; x1 = 1.f; }
            cu_set6(b, x0, 0.f, z0, x1, 1.f, z1);
        } break;
    case 144:
        switch (meta & 7) {
        case 2: cu_set6(b, 0.25f, 0.25f, 0.5f, 0.75f, 0.75f, 1.f); break;
        case 3: cu_set6(b, 0.25f, 0.25f, 0.f, 0.75f, 0.75f, 0.5f); break;
        case 4: cu_set6(b, 0.5f, 0.25f, 0.25f, 1.f, 0.75f, 0.75f); break;
        case 5: cu_set6(b, 0.f, 0.25f, 0.25f, 0.5f, 0.75f, 0.75f); break;
        default: cu_set6(b, 0.25f, 0.f, 0.25f, 0.75f, 0.5f, 0.75f); break;
        } break;
    case 140: cu_set6(b, 0.3125f, 0.f, 0.3125f, 0.6875f, 0.375f, 0.6875f); break;
    case 198:
        switch (meta & 7) {
        case 2: case 3: cu_set6(b, 0.375f, 0.375f, 0.f, 0.625f, 0.625f, 1.f); break;
        case 4: case 5: cu_set6(b, 0.f, 0.375f, 0.375f, 1.f, 0.625f, 0.625f); break;
        default: cu_set6(b, 0.375f, 0.f, 0.375f, 0.625f, 1.f, 0.625f); break;
        } break;
    case 54: case 130: case 146:
        cu_set6(b, 0.0625f, 0.f, 0.0625f, 0.9375f, 0.875f, 0.9375f); break;
    case 96: case 167:
        if (meta & 4) {
            switch (meta & 3) {
            case 0: cu_set6(b, 0.f, 0.f, 0.8125f, 1.f, 1.f, 1.f); break;
            case 1: cu_set6(b, 0.f, 0.f, 0.f, 1.f, 1.f, 0.1875f); break;
            case 2: cu_set6(b, 0.8125f, 0.f, 0.f, 1.f, 1.f, 1.f); break;
            default: cu_set6(b, 0.f, 0.f, 0.f, 0.1875f, 1.f, 1.f); break;
            }
        } else if (meta & 8) cu_set6(b, 0.f, 0.8125f, 0.f, 1.f, 1.f, 1.f);
        else                 cu_set6(b, 0.f, 0.f, 0.f, 1.f, 0.1875f, 1.f);
        break;
    case 64: case 71: case 193: case 194: case 195: case 196: case 197:
    {
        int upper  = (meta & 8) != 0;
        int lm     = upper ? in->below_meta : meta;
        int hm     = upper ? meta : in->above_meta;
        int facing = lm & 3;
        int open   = (lm & 4) != 0;
        int right  = (hm & 1) != 0;
        static const float FB[4][6] = {
            { 0.f, 0.f, 0.f, 0.1875f, 1.f, 1.f },
            { 0.f, 0.f, 0.f, 1.f, 1.f, 0.1875f },
            { 0.8125f, 0.f, 0.f, 1.f, 1.f, 1.f },
            { 0.f, 0.f, 0.8125f, 1.f, 1.f, 1.f },
        };
        int box;
        if (!open) box = facing;
        else switch (facing) {
        case 0:  box = right ? 3 : 1; break;
        case 1:  box = right ? 0 : 2; break;
        case 2:  box = right ? 1 : 3; break;
        default: box = right ? 2 : 0; break;
        }
        cu_set6(b, FB[box][0], FB[box][1], FB[box][2], FB[box][3], FB[box][4], FB[box][5]);
        break;
    }
    case 85: case 113: case 188: case 189: case 190: case 191: case 192:
    case 101: case 102: case 160:
    {
        int fence = cu_is_fence(id);
        float lo = fence ? 0.375f : 0.4375f, hi = fence ? 0.625f : 0.5625f;
        float x0 = lo, x1 = hi, z0 = lo, z1 = hi;
        int d;
        for (d = 0; d < 4; ++d) {
            int nb = in->nid[d];
            int conn = fence ? (cu_is_fence(nb) || cu_is_gate(nb) || cu_is_solid_cube(nb))
                             : (cu_is_pane(nb) || cu_is_glassy(nb) || cu_is_solid_cube(nb));
            if (!conn) continue;
            if (d == 0) z0 = 0.f; else if (d == 1) z1 = 1.f;
            else if (d == 2) x0 = 0.f; else x1 = 1.f;
        }
        cu_set6(b, x0, 0.f, z0, x1, 1.f, z1);
        break;
    }
    case 107: case 183: case 184: case 185: case 186: case 187:
        if ((meta & 1) == 0) cu_set6(b, 0.f, 0.f, 0.375f, 1.f, 1.f, 0.625f);
        else                 cu_set6(b, 0.375f, 0.f, 0.f, 0.625f, 1.f, 1.f);
        break;
    case 55:
    {
        float x0 = 0.1875f, x1 = 0.8125f, z0 = 0.1875f, z1 = 0.8125f;
        int d;
        for (d = 0; d < 4; ++d) {
            if (!cu_is_rs_component(in->nid[d])) continue;
            if (d == 0) z0 = 0.f; else if (d == 1) z1 = 1.f;
            else if (d == 2) x0 = 0.f; else x1 = 1.f;
        }
        cu_set6(b, x0, 0.f, z0, x1, 0.0625f, z1);
        break;
    }
    default: break;
    }
}

MC_HD static inline void cu_sel_box_at(const Chunk *w, int x, int y, int z, float b[6]) {
    CuSelIn in;
    in.id   = psv_get_block(w, x, y, z);
    in.meta = psv_get_meta(w, x, y, z);
    in.nid[0] = psv_get_block(w, x, y, z - 1);
    in.nid[1] = psv_get_block(w, x, y, z + 1);
    in.nid[2] = psv_get_block(w, x - 1, y, z);
    in.nid[3] = psv_get_block(w, x + 1, y, z);
    in.below_meta = psv_get_meta(w, x, y - 1, z);
    in.above_meta = psv_get_meta(w, x, y + 1, z);
    cu_sel_box(&in, b);
}

/* verbatim ray_box_hit (sel_box.c) */
MC_HD static inline double cu_ray_box_hit(double ex, double ey, double ez,
                                          double dx, double dy, double dz,
                                          double x0, double y0, double z0,
                                          double x1, double y1, double z1,
                                          int *nx, int *ny, int *nz) {
    double tmin = -1e30, tmax = 1e30;
    const double o[3] = { ex, ey, ez }, d[3] = { dx, dy, dz };
    const double lo[3] = { x0, y0, z0 }, hi[3] = { x1, y1, z1 };
    int enter_axis = -1, enter_sign = 0;
    int exit_axis = -1, exit_sign = 0;
    int i, axis, sign;
    double t;
    for (i = 0; i < 3; ++i) {
        if (d[i] > -1e-12 && d[i] < 1e-12) {
            if (o[i] < lo[i] || o[i] > hi[i]) return -1.0;
            continue;
        }
        {
            double t0 = (lo[i] - o[i]) / d[i], t1 = (hi[i] - o[i]) / d[i];
            int n0 = -1, n1 = 1;
            if (t0 > t1) {
                double tt = t0; t0 = t1; t1 = tt;
                { int nt = n0; n0 = n1; n1 = nt; }
            }
            if (t0 > tmin) {
                tmin = t0;
                enter_axis = i;
                enter_sign = n0;
            }
            if (t1 < tmax) {
                tmax = t1;
                exit_axis = i;
                exit_sign = n1;
            }
            if (tmin > tmax) return -1.0;
        }
    }
    axis = enter_axis; sign = enter_sign; t = tmin;
    if (t < 0.0) {
        t = tmax;
        axis = exit_axis;
        sign = exit_sign;
    }
    if (t < 0.0 || axis < 0) return -1.0;
    *nx = axis == 0 ? sign : 0;
    *ny = axis == 1 ? sign : 0;
    *nz = axis == 2 ? sign : 0;
    return t;
}

MC_HD static inline int cu_ray_transparent(int id) {
    return id == 0 || (id >= 8 && id <= 11) || id == 51;
}

/* verbatim gm_raycast_sel_reach (sel_box.c); `ops` is the caller env's
 * op-trace slice (NULL = off) - the only non-verbatim addition, inert. */
MC_HD static inline int cu_raycast_sel_reach(const Chunk *now, const McSinTable *st,
                                             const PsvPlayer *pl, double reach,
                                             int *hx, int *hy, int *hz,
                                             int *ax, int *ay, int *az,
                                             unsigned long long *ops) {
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
    if (ops) ops[CU_OP_RAYCAST]++;
    for (t = PSV_RAY_DT; t <= reach; t += PSV_RAY_DT) {
        int bx = mc_floor(ex + dx * t);
        int by = mc_floor(ey + dy * t);
        int bz = mc_floor(ez + dz * t);
        int id;
        if (ops) ops[CU_OP_RAY_STEP]++;
        if (bx == lastx && by == lasty && bz == lastz) continue;
        id = psv_get_block(now, bx, by, bz);
        if (!cu_ray_transparent(id)) {
            float b[6];
            double th;
            int nx, ny, nz;
            cu_sel_box_at(now, bx, by, bz, b);
            th = cu_ray_box_hit(ex, ey, ez, dx, dy, dz,
                                bx + (double)b[0], by + (double)b[1], bz + (double)b[2],
                                bx + (double)b[3], by + (double)b[4], bz + (double)b[5],
                                &nx, &ny, &nz);
            if (th >= 0.0 && th <= reach) {
                *hx = bx; *hy = by; *hz = bz;
                *ax = bx + nx; *ay = by + ny; *az = bz + nz;
                return have_air;
            }
        }
        lastx = bx; lasty = by; lastz = bz;
        have_air = 1;
    }
    return -1;
}

/* =================== player tick (game/player_ctl.c port) ================= */

/* verbatim ib_is_interactable (player_ctl.c:108) */
MC_HD static inline int cu_ib_is_interactable(int id) {
    return id == IB_WOODEN_DOOR || id == IB_IRON_DOOR || id == IB_LEVER
        || id == IB_STONE_PLATE || id == IB_WOODEN_PLATE || id == IB_STONE_BUTTON
        || id == IB_TRAPDOOR || id == IB_FENCE_GATE || id == IB_WOODEN_BUTTON
        || id == IB_LIGHT_PLATE || id == IB_HEAVY_PLATE || id == IB_IRON_TRAPDOOR;
}

/* verbatim face_from_adj (player_ctl.c:116) */
MC_HD static inline int cu_face_from_adj(int hx, int hy, int hz,
                                         int ax, int ay, int az) {
    if (ax < hx) return IBP_WEST;
    if (ax > hx) return IBP_EAST;
    if (ay < hy) return IBP_DOWN;
    if (ay > hy) return IBP_UP;
    if (az < hz) return IBP_NORTH;
    if (az > hz) return IBP_SOUTH;
    return IBP_NORTH;
}

/* verbatim yaw_to_quad (player_ctl.c:126) */
MC_HD static inline int cu_yaw_to_quad(float yaw_deg) {
    double y = fmod((double)yaw_deg, 360.0);
    if (y < 0.0) y += 360.0;
    return ((int)floor(y * 4.0 / 360.0 + 0.5)) & 3;
}

/* verbatim psv_replaceable (player_ctl.c:135) */
MC_HD static inline int cu_replaceable(int id) {
    return id == 0 || (id >= 8 && id <= 11) || id == 31 || id == 32 ||
           id == 51 || id == 78;
}

/* verbatim bucket_raycast (player_ctl.c:153) */
MC_HD static inline int cu_bucket_raycast(const Chunk *w, const McSinTable *st,
                                          const PsvPlayer *pl,
                                          int *hx, int *hy, int *hz) {
    float f  = mc_cos(st, -pl->yaw * 0.017453292f - 3.1415927f);
    float f1 = mc_sin(st, -pl->yaw * 0.017453292f - 3.1415927f);
    float f2 = -mc_cos(st, -pl->pitch * 0.017453292f);
    float f3 = mc_sin(st, -pl->pitch * 0.017453292f);
    double dx = (double)(f1 * f2), dy = (double)f3, dz = (double)(f * f2);
    double ex = pl->ent.posX, ey = pl->ent.posY + PSV_EYE_HEIGHT, ez = pl->ent.posZ;
    int lx = mc_floor(ex), ly = mc_floor(ey), lz = mc_floor(ez);
    double t;
    for (t = PSV_RAY_DT; t <= PSV_REACH; t += PSV_RAY_DT) {
        int x = mc_floor(ex + dx * t), y = mc_floor(ey + dy * t),
            z = mc_floor(ez + dz * t);
        int id, meta;
        if (x == lx && y == ly && z == lz) continue;
        lx = x; ly = y; lz = z;
        id = psv_get_block(w, x, y, z);
        meta = psv_get_meta(w, x, y, z);
        if ((id == 8 || id == 9 || id == 10 || id == 11) && meta == 0) {
            *hx = x; *hy = y; *hz = z;
            return id;
        }
        if (psv_solid(id)) return 0;
    }
    return 0;
}

/* verbatim eye_in_water (player_ctl.c:143) */
MC_HD static inline int cu_eye_in_water(const Chunk *w, const PsvPlayer *pl) {
    double eye_y = pl->ent.posY + PSV_EYE_HEIGHT;
    int x = mc_floor(pl->ent.posX), y = mc_floor(eye_y), z = mc_floor(pl->ent.posZ);
    int id = psv_get_block(w, x, y, z);
    int m;
    if (id != 8 && id != 9) return 0;
    m = psv_get_meta(w, x, y, z);
    if (m >= 8) m = 0;
    return eye_y < (double)(y + 1) - (double)(m + 1) / 9.0;
}

/* verbatim gm_vitals_apply (player_ctl.c:57) */
MC_HD static inline void cu_vitals_apply(PvStats *vit, PsvPlayer *pl, CuAction act,
                                         int was_air, double prev_min_y,
                                         double dx, double dy, double dz,
                                         int in_water_pre, int eye_water_post,
                                         int land_jump) {
    McEntity *e = &pl->ent;
    if (eye_water_post) {
        int i = (int)floorf(sqrtf((float)(dx * dx + dy * dy + dz * dz)) * 100.0f + 0.5f);
        if (i > 0) pv_add_exhaustion(vit, 0.01f * (float)i * 0.01f);
    } else if (in_water_pre) {
        int j = (int)floorf(sqrtf((float)(dx * dx + dz * dz)) * 100.0f + 0.5f);
        if (j > 0) pv_add_exhaustion(vit, 0.01f * (float)j * 0.01f);
    } else if (e->onGround && act.sprint) {
        int k = (int)floorf(sqrtf((float)(dx * dx + dz * dz)) * 100.0f + 0.5f);
        if (k > 0) pv_add_exhaustion(vit, 0.1f * (float)k * 0.01f);
    }
    if (land_jump) pv_add_exhaustion(vit, act.sprint ? 0.2f : 0.05f);

    if (!e->onGround) {
        double dropped = prev_min_y - e->box.minY;
        if (dropped > 0.0) pl->fall_distance += (float)dropped;
    } else if (was_air && pl->fall_distance > 0.0f) {
        pv_fall_damage(vit, pl->fall_distance);
    }
    if (e->onGround) pl->fall_distance = 0.0f;

    pv_on_update(vit);
    pl->health = vit->health;
    pl->food   = (float)vit->foodLevel;
}

/* verbatim harvest_drop (player_ctl.c:188), incl. the gravel positional hash */
MC_HD static inline void cu_harvest_drop(int block_id, int block_meta, int tool_id,
                                         int wx, int wy, int wz,
                                         int *item, int *count, int *meta) {
    *item = 0; *count = 0; *meta = 0;
    if (!pb_can_harvest(tool_id, block_id)) return;
    switch (block_id) {
        case 1:  *item = 4;  *count = 1; break;
        case 2:  *item = 3;  *count = 1; break;
        case 3:  *item = 3;  *count = 1; *meta = block_meta & 3; break;
        case 4:  *item = 4;  *count = 1; break;
        case 12: *item = 12; *count = 1; *meta = block_meta & 1; break;
        case 13: {
            u32 h = (u32)wx * 73428767u ^ (u32)wy * 912931u ^ (u32)wz * 19349663u;
            h ^= h >> 13; h *= 0x85ebca6bu; h ^= h >> 16;
            *item = (h % 10u) == 0u ? 318 : 13; *count = 1; break;
        }
        case 14: *item = 14; *count = 1; break;
        case 15: *item = 15; *count = 1; break;
        case 16: *item = 263; *count = 1; break;
        case 17: *item = 17; *count = 1; *meta = block_meta & 3; break;
        case 49: *item = 49; *count = 1; break;
        case 56: *item = 264; *count = 1; break;
        case 132: *item = 287; *count = 1; break;
        default: break;
    }
}

MC_HD static inline void cu_emit_edit(CuEdit *edits, int *ne, int max_edits,
                                      int ox, int oy, int oz,
                                      int lx, int ly, int lz, int id, int meta,
                                      int drop_id, int drop_count, int drop_meta) {
    if (*ne >= max_edits) return;
    edits[*ne].wx = ox + lx;
    edits[*ne].wy = oy + ly;
    edits[*ne].wz = oz + lz;
    edits[*ne].id = id;
    edits[*ne].meta = meta & 15;
    edits[*ne].drop_id = drop_id;
    edits[*ne].drop_count = drop_count;
    edits[*ne].drop_meta = drop_meta & 15;
    edits[*ne].harvest_tool = 0;
    (*ne)++;
}

/* verbatim dig_destroy (player_ctl.c:214) */
MC_HD static inline void cu_dig_destroy(Chunk *window, PsvPlayer *pl, PvStats *vit,
                                        int hx, int hy, int hz,
                                        int bid, int bmeta, int ox, int oy, int oz,
                                        CuEdit *edits, int *ne, int max_edits) {
    ICStack held = isr_get_stack(&pl->inv, pl->inv.current_item);
    int drop_id, drop_count, drop_meta;
    cu_harvest_drop(bid, bmeta, held.item, hx + ox, hy + oy, hz + oz,
                    &drop_id, &drop_count, &drop_meta);
    if (pb_can_harvest(held.item, bid))
        pv_add_exhaustion(vit, 0.005f);
    if (!isr_is_empty(&held)) {
        ITAStack tool = ita_mk(held.item, held.meta);
        int max_damage;
        ita_on_block_destroyed(&tool, bid);
        max_damage = ita_stack_max_damage(&tool);
        if (max_damage > 0) {
            if (tool.damage > max_damage)
                (void)isr_decr_stack_size(&pl->inv, pl->inv.current_item, 1);
            else {
                held.meta = tool.damage;
                isr_set_stack(&pl->inv, pl->inv.current_item, held);
            }
        }
    }
    cu_win_set_state(window, hx, hy, hz, BLK_AIR, 0);
    pl->break_events++;
    {
        int edit_index = *ne;
        cu_emit_edit(edits, ne, max_edits, ox, oy, oz, hx, hy, hz, 0, 0,
                     drop_id, drop_count, drop_meta);
        if (*ne > edit_index)
            edits[edit_index].harvest_tool = held.item;
    }
}

/* gm_player_tick (player_ctl.c:241) for the learned-stage action subset.
 * Ported exactly, statics -> Blaze fields. Dropped as inert for this action
 * space (act.use is rejected upstream): FOV/bow render state, rightClickMouse
 * FIRE + place/interact/bucket, eating. The rc_delay timer and use edge ARE
 * kept ticking (snapshot state must evolve identically). */
MC_HD static inline void blaze_player_tick(Blaze *env, const McSinTable *st,
                                           CuAction act, CuEdit *edits, int *nedits,
                                           int max_edits, McAABB *blocks) {
    Chunk *window = env->window;
    PsvPlayer *pl = &env->pl;
    PvStats *vit = &env->vit;
    int ox = env->ox, oy = 0, oz = env->oz;
    int ne = 0;
    PsvAction a;
    int use_gate_hitting, was_air, land_jump, water_pre, lava_pre;
    double prev_min_y, pre_x, pre_y, pre_z;

    if (act.hotbar_sel >= 0 && act.hotbar_sel <= 8)
        pl->inv.current_item = act.hotbar_sel;

    a.forward = act.forward;
    a.strafe  = -act.strafe;
    if (act.sneak) {
        a.forward = (float)((double)a.forward * 0.3);
        a.strafe  = (float)((double)a.strafe  * 0.3);
    }
    a.sneak   = act.sneak;
    a.jump    = act.jump;

    pl->yaw   += act.dyaw;
    pl->pitch += act.dpitch;
    if (pl->pitch >  89.0f) pl->pitch =  89.0f;
    if (pl->pitch < -89.0f) pl->pitch = -89.0f;
    a.yaw   = pl->yaw;
    a.pitch = pl->pitch;

    a.do_break = 0;
    a.do_place = 0;
    a.attack   = act.attack;

    /* vanilla sprint state machine (player_ctl.c:287) */
    {
        int   flag1, flag2, flag4;
        float mf;
        if (pl->sprint_toggle_timer > 0) pl->sprint_toggle_timer--;
        flag1 = pl->prev_sneak;
        flag2 = pl->prev_move_forward >= 0.8f;
        mf = act.sneak ? (float)((double)act.forward * 0.3) : act.forward;
        flag4 = pl->food > 6.0f;
        if (pl->ent.onGround && !flag1 && !flag2 && mf >= 0.8f && !pl->sprinting && flag4) {
            if (pl->sprint_toggle_timer <= 0 && !act.sprint)
                pl->sprint_toggle_timer = 7;
            else
                pl->sprinting = 1;
        }
        if (!pl->sprinting && mf >= 0.8f && flag4 && act.sprint) pl->sprinting = 1;
        if (pl->sprinting && (mf < 0.8f || pl->ent.collidedHorizontally || !flag4))
            pl->sprinting = 0;
        pl->prev_move_forward = mf;
        pl->prev_sneak = act.sneak;
        act.sprint = pl->sprinting;
    }
    a.sprint = act.sprint;

    /* FOV/bow hand state (player_ctl.c:316-337): render-only, no physics
     * effect, not in any emitted obs field - skipped. */

    was_air    = !pl->ent.onGround;
    prev_min_y =  pl->ent.box.minY;
    pre_x      =  pl->ent.posX;
    pre_y      =  pl->ent.posY;
    pre_z      =  pl->ent.posZ;

    /* hurt-velocity server-motion shadow (player_ctl.c:356) */
    if (env->hurt_vel_reset) {
        pl->ent.motionX = (double)(int)(env->server_motion_x * 8000.0) / 8000.0;
        pl->ent.motionZ = (double)(int)(env->server_motion_z * 8000.0) / 8000.0;
        env->hurt_vel_reset = 0;
    }

    /* progressive dig BEFORE the move (player_ctl.c:364-459) */
    use_gate_hitting = env->dig_hitting;
    if (act.attack) {
        int press = !env->atk_prev;
        int hx, hy, hz, ax, ay, az;
        int r = cu_raycast_sel_reach(window, st, pl, PSV_REACH,
                                     &hx, &hy, &hz, &ax, &ay, &az, env->ops);
        if (r >= 0) {
            int bid = psv_get_block(window, hx, hy, hz);
            BptProps bp = mc_bpt_props(bid);
            if (bid != BLK_AIR && bp.hardness >= 0.0f) {
                ICStack held = isr_get_stack(&pl->inv, pl->inv.current_item);
                PbInput pin;
                float rel;
                pin.block_id = bid;
                pin.block_meta = psv_get_meta(window, hx, hy, hz);
                pin.tool_id = held.item;
                pin.tool_meta = held.meta;
                pin.efficiency = 0;
                pin.haste_amp = -1;
                pin.fatigue_amp = -1;
                pin.in_water = cu_eye_in_water(window, pl);
                pin.aqua_affinity = 0;
                pin.on_ground = pl->ent.onGround;
                pin.creative = 0;
                rel = pb_relative_hardness(&pin);
                if (press &&
                    (!env->dig_hitting || hx != env->dig_hx || hy != env->dig_hy ||
                     hz != env->dig_hz)) {
                    if (rel >= 1.0f) {
                        CU_OP(env, CU_OP_DIG_BREAK);
                        cu_dig_destroy(window, pl, vit, hx, hy, hz, bid, pin.block_meta,
                                       ox, oy, oz, edits, &ne, max_edits);
                        bid = BLK_AIR;
                    } else {
                        env->dig_hitting = 1;
                        env->dig_hx = hx; env->dig_hy = hy; env->dig_hz = hz;
                        env->dig_progress = 0.0f;
                        use_gate_hitting = 1;
                    }
                }
                if (bid != BLK_AIR) {
                    if (env->dig_delay > 0) {
                        --env->dig_delay;
                    } else if (hx == env->dig_hx && hy == env->dig_hy &&
                               hz == env->dig_hz) {
                        env->dig_progress += rel;
                        CU_OP(env, CU_OP_DIG_TICK);
                        if (env->dig_progress >= 1.0f) {
                            env->dig_hitting = 0;
                            CU_OP(env, CU_OP_DIG_BREAK);
                            cu_dig_destroy(window, pl, vit, hx, hy, hz, bid, pin.block_meta,
                                           ox, oy, oz, edits, &ne, max_edits);
                            env->dig_progress = 0.0f;
                            env->dig_delay = 5;
                        }
                    } else {
                        if (rel >= 1.0f) {
                            CU_OP(env, CU_OP_DIG_BREAK);
                            cu_dig_destroy(window, pl, vit, hx, hy, hz, bid, pin.block_meta,
                                           ox, oy, oz, edits, &ne, max_edits);
                        } else {
                            env->dig_hitting = 1;
                            env->dig_hx = hx; env->dig_hy = hy; env->dig_hz = hz;
                            env->dig_progress = 0.0f;
                        }
                    }
                }
            } else {
                env->dig_hitting = 0;
                env->dig_hx = INT_MIN;
                env->dig_progress = 0.0f;
            }
        } else {
            env->dig_hitting = 0;
            env->dig_hx = INT_MIN;
            env->dig_progress = 0.0f;
        }
    } else {
        env->dig_hitting = 0;
        env->dig_hx = INT_MIN;
        env->dig_progress = 0.0f;
    }
    env->atk_prev = act.attack;

    /* rightClickMouse timer/edge (player_ctl.c:472-479) + FIRE path
     * (player_ctl.c:481-568): bucket scoop, interactable toggle, and block
     * place with the mayPlace player-bb gate - ported exactly for the full
     * chain's "use":1 (table placement). Scripted do_place does not exist
     * here (protocol actions only). */
    if (env->rc_delay > 0) --env->rc_delay;
    {
        int use_fire = act.use && (!env->use_prev || env->rc_delay == 0) &&
                       !use_gate_hitting;
        if (use_fire) env->rc_delay = 4;
        env->use_prev = act.use;
        if (use_fire) {
            ICStack held0 = isr_get_stack(&pl->inv, pl->inv.current_item);
            int done_use = 0;
            if (held0.item == 325) {
                int bx, by, bz;
                int bid = cu_bucket_raycast(window, st, pl, &bx, &by, &bz);
                if (bid) {
                    cu_win_set_state(window, bx, by, bz, 0, 0);
                    isr_set_stack(&pl->inv, pl->inv.current_item,
                                  ic_mk(bid == 8 || bid == 9 ? 326 : 327, 1, 0));
                    cu_emit_edit(edits, &ne, max_edits, ox, oy, oz, bx, by, bz,
                                 0, 0, 0, 0, 0);
                    done_use = 1;
                }
            }
            if (!done_use) {
                int hx, hy, hz, ax, ay, az;
                int r = cu_raycast_sel_reach(window, st, pl, PSV_REACH,
                                             &hx, &hy, &hz, &ax, &ay, &az,
                                             env->ops);
                if (r >= 0) {
                    int hit_id = psv_get_block(window, hx, hy, hz);
                    int hit_meta = psv_get_meta(window, hx, hy, hz);
                    if (cu_ib_is_interactable(hit_id)) {
                        IbCase ic;
                        IbResult ir;
                        ic.block_id = hit_id;
                        ic.meta_in = hit_meta;
                        ic.action = IB_ACT_CLICK;
                        ic.arg0 = cu_yaw_to_quad(pl->yaw);
                        ir = ib_apply(&ic);
                        if (ir.accepted) {
                            cu_win_set_state(window, hx, hy, hz, hit_id,
                                             ir.meta_out);
                            cu_emit_edit(edits, &ne, max_edits, ox, oy, oz,
                                         hx, hy, hz, hit_id, ir.meta_out,
                                         0, 0, 0);
                        }
                    } else if (r == 1) {
                        /* place against hit face into the air cell */
                        ICStack held = isr_get_stack(&pl->inv,
                                                     pl->inv.current_item);
                        if ((held.item == 326 || held.item == 327) &&
                            cu_replaceable(psv_get_block(window, ax, ay, az))) {
                            int fluid = held.item == 326 ? 8 : 10;
                            cu_win_set_state(window, ax, ay, az, fluid, 0);
                            isr_set_stack(&pl->inv, pl->inv.current_item,
                                          ic_mk(325, 1, 0));
                            cu_emit_edit(edits, &ne, max_edits, ox, oy, oz,
                                         ax, ay, az, fluid, 0, 0, 0, 0);
                        } else if (held.item == 355 &&
                                   psv_get_block(window, ax, ay, az) == BLK_AIR) {
                            /* bed: two-cell place (player_ctl.c:518) */
                            static const int bdx[4] = {0, -1, 0, 1};
                            static const int bdz[4] = {1, 0, -1, 0};
                            int q = cu_yaw_to_quad(pl->yaw);
                            int hx2 = ax + bdx[q], hz2 = az + bdz[q];
                            if (psv_get_block(window, hx2, ay, hz2) == BLK_AIR) {
                                cu_win_set_state(window, ax, ay, az, 26, q);
                                cu_win_set_state(window, hx2, ay, hz2, 26, q | 8);
                                (void)isr_decr_stack_size(&pl->inv,
                                                          pl->inv.current_item, 1);
                                cu_emit_edit(edits, &ne, max_edits, ox, oy, oz,
                                             ax, ay, az, 26, q, 0, 0, 0);
                                cu_emit_edit(edits, &ne, max_edits, ox, oy, oz,
                                             hx2, ay, hz2, 26, q | 8, 0, 0, 0);
                            }
                        } else if (!isr_is_empty(&held) &&
                                   psv_get_block(window, ax, ay, az) == BLK_AIR &&
                                   held.item == 259) {
                            /* flint&steel: fire edit (portal ignite is a
                             * runtime-side follower - see blaze_runtime_tick) */
                            cu_win_set_state(window, ax, ay, az, 51, 0);
                            held.meta++;
                            if (held.meta > 64)
                                (void)isr_decr_stack_size(&pl->inv,
                                                          pl->inv.current_item, 1);
                            else
                                isr_set_stack(&pl->inv, pl->inv.current_item,
                                              held);
                            cu_emit_edit(edits, &ne, max_edits, ox, oy, oz,
                                         ax, ay, az, 51, 0, 0, 0, 0);
                        } else if (!isr_is_empty(&held) &&
                                   cu_replaceable(psv_get_block(window, ax, ay,
                                                                az))) {
                            int place_id = held.item;
                            /* World.mayPlace entity-collision gate (strict
                             * AABB intersects, PRE-move pose) */
                            if (psv_solid(place_id)) {
                                const McAABB *pb = &pl->ent.box;
                                if (pb->minX < (double)ax + 1.0 &&
                                    pb->maxX > (double)ax &&
                                    pb->minY < (double)ay + 1.0 &&
                                    pb->maxY > (double)ay &&
                                    pb->minZ < (double)az + 1.0 &&
                                    pb->maxZ > (double)az)
                                    place_id = 0;
                            }
                            /* block ids only (1..255); tools stay tools */
                            if (place_id > 0 && place_id < 256 &&
                                !ita_is_pickaxe(place_id)) {
                                int face = cu_face_from_adj(hx, hy, hz,
                                                            ax, ay, az);
                                int yq = cu_yaw_to_quad(pl->yaw);
                                int pmeta = ibp_placed_meta(place_id, face, yq,
                                                            act.sneak,
                                                            held.meta) & 15;
                                ICStack used = isr_decr_stack_size(
                                    &pl->inv, pl->inv.current_item, 1);
                                if (!isr_is_empty(&used)) {
                                    cu_win_set_state(window, ax, ay, az,
                                                     place_id, pmeta);
                                    pl->place_events++;
                                    cu_emit_edit(edits, &ne, max_edits,
                                                 ox, oy, oz, ax, ay, az,
                                                 place_id, pmeta, 0, 0, 0);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    (void)use_gate_hitting;

    /* water fall-distance zero + land jump server-motion kick (player_ctl.c:579) */
    water_pre = psv_in_liquid(window, &pl->ent, 1);
    lava_pre  = water_pre ? 0 : psv_in_liquid(window, &pl->ent, 0);
    if (water_pre) pl->fall_distance = 0.0f;
    land_jump = act.jump && !was_air && !water_pre && !lava_pre;
    if (land_jump && act.sprint) {
        float fj = pl->yaw * 0.017453292f;
        env->server_motion_x -= (double)(mc_sin(st, fj) * 0.2f);
        env->server_motion_z += (double)(mc_cos(st, fj) * 0.2f);
    }

    CU_OP(env, CU_OP_PHYS_TICK);
    psv_physics_tick(window, st, pl, &a, blocks);

    /* eating (player_ctl.c:592-611): ported verbatim (food items are
     * unobtainable in the chain, but "use" is now a live action - keep the
     * statics evolving identically for any inventory). */
    {
        ICStack food = isr_get_stack(&pl->inv, pl->inv.current_item);
        int hunger = 0;
        float sat = 0.0f;
        switch (food.item) {
        case 260: hunger = 4; sat = 0.3f; break;
        case 297: hunger = 5; sat = 0.6f; break;
        case 319: case 363: hunger = 3; sat = 0.3f; break;
        case 320: case 364: hunger = 8; sat = 0.8f; break;
        case 365: case 423: hunger = 2; sat = 0.3f; break;
        case 366: case 424: hunger = 6; sat = 0.6f; break;
        default: break;
        }
        if (act.use && hunger && vit->foodLevel < 20) {
            if (env->eat_item != food.item) {
                env->eat_item = food.item;
                env->eat_ticks = 0;
            }
            if (++env->eat_ticks >= 32) {
                (void)isr_decr_stack_size(&pl->inv, pl->inv.current_item, 1);
                vit->foodLevel += hunger;
                if (vit->foodLevel > 20) vit->foodLevel = 20;
                vit->saturation += (float)hunger * sat * 2.0f;
                if (vit->saturation > (float)vit->foodLevel)
                    vit->saturation = (float)vit->foodLevel;
                pl->food = (float)vit->foodLevel;
                env->eat_ticks = 0;
                env->eat_item = 0;
            }
        } else {
            env->eat_ticks = 0;
            env->eat_item = 0;
        }
    }

    if (a.attack) pl->swing_events++;

    {
        double dx = pl->ent.posX - pre_x, dy = pl->ent.posY - pre_y,
               dz = pl->ent.posZ - pre_z;
        float hp_before = vit->health;
        cu_vitals_apply(vit, pl, act, was_air, prev_min_y, dx, dy, dz,
                        water_pre, cu_eye_in_water(window, pl), land_jump);
        if (vit->health < hp_before) {
            env->hurt_vel_reset = 1;
        } else {
            float drag = 0.91f;
            if (pl->ent.onGround) {
                int bx = mc_floor(pl->ent.posX), by = mc_floor(pl->ent.posY) - 1;
                int bz = mc_floor(pl->ent.posZ);
                drag *= psv_slipperiness(psv_get_block(window, bx, by, bz));
            }
            env->server_motion_x *= (double)drag;
            env->server_motion_z *= (double)drag;
            if (fabs(env->server_motion_x) < 0.003) env->server_motion_x = 0.0;
            if (fabs(env->server_motion_z) < 0.003) env->server_motion_z = 0.0;
        }
    }

    *nedits = ne;
}

/* =================== live items (game/live_sim.c port) ==================== */

/* verbatim gm_live_spawn_item (live_sim.c:43) */
MC_HD static inline int cu_spawn_item(Blaze *env, double x, double y, double z,
                                      int item, int count, int meta,
                                      int pickup_delay) {
    int i;
    if (item <= 0 || count <= 0) return 0;
    for (i = 0; i < CU_MAX_ITEMS; ++i) {
        CuItem *e = &env->items[i];
        if (e->active) continue;
        e->active = 1;
        e->x = x; e->y = y; e->z = z;
        e->mx = 0.0; e->my = 0.0; e->mz = 0.0;
        e->on_ground = 0; e->age = 0;
        e->item = item; e->count = count; e->meta = meta & 15;
        e->pickup_delay = pickup_delay < 0 ? 0 : pickup_delay;
        e->lifespan = 6000;
        env->n_items++;
        return 1;
    }
    return 0;
}

/* verbatim solid_at (live_sim.c:61) - note: ANY non-air non-liquid id */
MC_HD static inline int cu_item_solid_at(const Blaze *env, int x, int y, int z) {
    int id = cu_world_block(env, x, y, z);
    return id != 0 && id != 8 && id != 9 && id != 10 && id != 11;
}

/* verbatim gm_live_tick item slice + gm_live_tick_player pickup
 * (live_sim.c:66-148; the wheat plot is inactive in rl mode - gm_runtime_init
 * memsets r->entities, so plant_active==0 always). */
MC_HD static inline void cu_live_tick_player(Blaze *env) {
    int i;
    double px, py, pz;
    for (i = 0; i < CU_MAX_ITEMS; ++i) {
        CuItem *e = &env->items[i];
        int by, bx, bz, under;
        float slip, f;
        if (!e->active) continue;
        CU_OP(env, CU_OP_ITEM_TICK);
        if (e->pickup_delay > 0) e->pickup_delay--;
        e->my -= 0.03999999910593033;
        e->x += e->mx;
        e->y += e->my;
        e->z += e->mz;
        by = (int)floor(e->y);
        bx = (int)floor(e->x);
        bz = (int)floor(e->z);
        if (cu_item_solid_at(env, bx, by, bz)) {
            e->y = (double)(by + 1);
            e->my = 0.0;
            e->on_ground = 1;
        } else if (cu_item_solid_at(env, bx, by - 1, bz) && e->y - floor(e->y) < 0.01) {
            e->on_ground = 1;
            e->my = 0.0;
        } else {
            e->on_ground = 0;
        }
        slip = 0.6f;
        under = cu_world_block(env, bx, by - 1, bz);
        if (under == BLK_ICE || under == 174 || under == 212) slip = 0.98f;
        f = e->on_ground ? (slip * 0.98f) : 0.98f;
        e->mx *= (double)f;
        e->mz *= (double)f;
        e->my *= 0.9800000190734863;
        if (e->on_ground) e->my *= -0.5;
        e->age++;
        if (e->lifespan > 0 && e->age >= e->lifespan) {
            e->active = 0;
            if (env->n_items > 0) env->n_items--;
        }
    }
    px = env->pl.ent.posX + (double)env->ox;
    py = env->pl.ent.posY;
    pz = env->pl.ent.posZ + (double)env->oz;
    for (i = 0; i < CU_MAX_ITEMS; ++i) {
        CuItem *e = &env->items[i];
        ICStack incoming;
        if (!e->active || e->pickup_delay > 0) continue;
        if (fabs(e->x - px) > 1.0 || fabs(e->z - pz) > 1.0 ||
            e->y < py - 0.25 || e->y > py + 2.8) continue;
        incoming = ic_mk(e->item, e->count, e->meta);
        isr_add_item_stack_to_inventory(&env->pl.inv, &incoming);
        e->count = incoming.count;
        if (e->count <= 0) {
            e->active = 0;
            if (env->n_items > 0) env->n_items--;
        }
    }
}

/* =================== plant cascade (game/runtime.c port) ================== */

/* verbatim plant_support_ok / plant_drop_item / break_unsupported_plants
 * (runtime.c:67-105) over the region. Underground digs never trigger it, but
 * the port keeps surface-adjacent snapshots exact. */
MC_HD static inline int cu_plant_support_ok(int id, int meta, int below) {
    switch (id) {
    case 6: case 31: case 37: case 38: return below == 2 || below == 3 || below == 60;
    case 32:  return below == 3 || below == 12 || below == 159 || below == 172;
    case 83:  return below == 83 || below == 2 || below == 3 || below == 12;
    case 175: return meta >= 8 ? below == 175 : (below == 2 || below == 3);
    case 111: return below == 8 || below == 9;
    case 81:  return below == 12 || below == 81;
    default:  return 1;
    }
}

MC_HD static inline int cu_plant_drop_item(int id, int meta) {
    switch (id) {
    case 6: case 37: case 38: return id;
    case 83:  return 338;
    case 81:  return 81;
    case 111: return 111;
    case 175: return meta >= 8 ? 0 : 0;
    default:  return 0;
    }
}

MC_HD static inline void cu_break_unsupported_plants(Blaze *env, int wx, int wy, int wz) {
    int y;
    for (y = wy + 1; y < 255; ++y) {
        int id = cu_world_block(env, wx, y, wz);
        int meta = cu_world_meta(env, wx, y, wz);
        int below = cu_world_block(env, wx, y - 1, wz);
        int drop;
        if (cu_plant_support_ok(id, meta, below)) break;
        cu_world_set_state(env, wx, y, wz, 0, 0);
        drop = cu_plant_drop_item(id, meta);
        if (drop > 0)
            cu_spawn_item(env, wx + 0.5, y + 0.5, wz + 0.5, drop, 1, 0, 10);
    }
}

/* =================== live furnaces (game/furnace_live.c port) ============= */

/* verbatim furnace_live_init */
MC_HD static inline void cu_furnace_init(CuFurnace *f) {
    f->input = sr_empty();
    f->fuel = sr_empty();
    f->output = sr_empty();
    f->burn_time = 0;
    f->current_burn_time = 0;
    f->cook_time = 0;
    f->total_cook = fft_get_cook_time(&f->input);
}

/* verbatim furnace_live_stack_limit */
MC_HD static inline int cu_furnace_stack_limit(SRStack stack) {
    return stack.item == SR_LAVA_BUCKET ? 1 : FFT_STACK_LIMIT;
}

/* verbatim furnace_live_insert (slot 0 input, 1 fuel; output insert-blocked) */
MC_HD static inline int cu_furnace_insert(CuFurnace *f, int slot, SRStack stack) {
    SRStack *dst;
    int limit, moved;
    if (slot == 2 || sr_isEmpty(stack)) return 0;
    dst = slot == 0 ? &f->input : slot == 1 ? &f->fuel : NULL;
    if (dst == NULL) return 0;
    if (slot == 1 && sr_getItemBurnTime(stack) <= 0) return 0;
    limit = cu_furnace_stack_limit(stack);
    if (!sr_isEmpty(*dst)) {
        if (dst->item != stack.item || dst->meta != stack.meta) return 0;
        limit -= dst->count;
    }
    if (limit <= 0) return 0;
    moved = stack.count;
    if (moved > limit) moved = limit;
    if (sr_isEmpty(*dst)) *dst = sr_mk(stack.item, moved, stack.meta);
    else dst->count += moved;
    return moved;
}

/* verbatim furnace_live_extract */
MC_HD static inline SRStack cu_furnace_extract(CuFurnace *f, int slot,
                                               int amount) {
    SRStack *src = slot == 0 ? &f->input : slot == 1 ? &f->fuel :
                   slot == 2 ? &f->output : NULL;
    SRStack result;
    int taken;
    if (src == NULL || amount <= 0 || sr_isEmpty(*src)) return sr_empty();
    taken = amount;
    if (taken > src->count) taken = src->count;
    result = sr_mk(src->item, taken, src->meta);
    src->count -= taken;
    if (src->count <= 0) *src = sr_empty();
    return result;
}

/* verbatim furnace_live_tick (FftFurnace kernel round-trip; sr_build per
 * tick like the real env - the table is a pure constant so this is
 * value-identical however it is produced) */
MC_HD static inline void cu_furnace_tick(CuFurnace *f) {
    FftFurnace kernel;
    kernel.slot0 = f->input;
    kernel.slot1 = f->fuel;
    kernel.slot2 = f->output;
    kernel.burn_time = f->burn_time;
    kernel.current_burn_time = f->current_burn_time;
    kernel.cook_time = f->cook_time;
    kernel.total_cook = f->total_cook;
    kernel.nrecipes = sr_build(kernel.recipes);
    fft_tick(&kernel);
    f->input = kernel.slot0;
    f->fuel = kernel.slot1;
    f->output = kernel.slot2;
    f->burn_time = kernel.burn_time;
    f->current_burn_time = kernel.current_burn_time;
    f->cook_time = kernel.cook_time;
    f->total_cook = kernel.total_cook;
}

/* =================== discrete protocol primitives (rl_mode.c) ============= */

/* rl_mode.c rl_crafts + rl_do_craft + runtime.c gm_runtime_craft, merged and
 * ported exactly: per required item id find ONE inventory slot holding
 * enough, feed it to the grid, full CraftingManager match (crf_findMatching
 * verbatim), silent failure, 3x3 recipes gated on an open table
 * (env->container == 1). `recipes` = crf_build output (built once at create
 * on the host and treated as a constant table; the real env rebuilds the
 * identical table per call). Returns 1 on success. */
MC_HD static inline int blaze_do_craft(Blaze *env, int which,
                                       const CRRecipe *recipes, int nrecipes) {
    /* rl_crafts (rl_mode.c:65): 3x3 row-major cells, 2x2 uses 0,1,3,4 */
    static const int cu_craft_w[8] = {2, 2, 2, 3, 3, 2, 3, 3};
    static const int cu_craft_cell[8][9] = {
        /* 0 planks  */ {17, 0, 0, 0, 0, 0, 0, 0, 0},
        /* 1 sticks  */ {5, 0, 0, 5, 0, 0, 0, 0, 0},
        /* 2 table   */ {5, 5, 0, 5, 5, 0, 0, 0, 0},
        /* 3 w.pick  */ {5, 5, 5, 0, 280, 0, 0, 280, 0},
        /* 4 s.pick  */ {4, 4, 4, 0, 280, 0, 0, 280, 0},
        /* 5 torch   */ {263, 0, 0, 280, 0, 0, 0, 0, 0},
        /* 6 furnace */ {4, 4, 4, 4, 0, 4, 4, 4, 4},
        /* 7 i.pick  */ {265, 265, 265, 0, 280, 0, 0, 280, 0},
    };
    int slots[9], need_id[9], need_n[9], nids = 0, i, j, width, slot;
    CRStack grid[9], result;
    int use[ISR_MAIN_SLOTS];
    IsrInv next;
    ICStack output;

    CU_OP(env, CU_OP_CRAFT);
    if (which < 0 || which >= 8) return 0;
    width = cu_craft_w[which];
    for (i = 0; i < 9; ++i) slots[i] = -1;
    for (i = 0; i < 9; ++i) {
        if (!cu_craft_cell[which][i]) continue;
        for (j = 0; j < nids && need_id[j] != cu_craft_cell[which][i]; ++j) {}
        if (j == nids) {
            need_id[nids] = cu_craft_cell[which][i];
            need_n[nids] = 0;
            ++nids;
        }
        ++need_n[j];
    }
    for (j = 0; j < nids; ++j) {
        slot = -1;
        for (i = 0; i < ISR_MAIN_SLOTS; ++i) {
            ICStack s = isr_get_stack(&env->pl.inv, i);
            if (!isr_is_empty(&s) && s.item == need_id[j] &&
                s.count >= need_n[j]) { slot = i; break; }
        }
        if (slot < 0) return 0;
        for (i = 0; i < 9; ++i)
            if (cu_craft_cell[which][i] == need_id[j]) slots[i] = slot;
    }

    /* gm_runtime_craft (runtime.c:850) */
    if (width == 3 && env->container != 1) return 0;
    for (i = 0; i < ISR_MAIN_SLOTS; ++i) use[i] = 0;
    for (i = 0; i < 9; ++i) {
        ICStack s;
        grid[i] = crf_empty();
        slot = slots[i];
        if (slot < 0) continue;
        if (slot >= ISR_MAIN_SLOTS) return 0;
        if (width == 2 && (i % 3 >= 2 || i / 3 >= 2)) return 0;
        s = isr_get_stack(&env->pl.inv, slot);
        if (isr_is_empty(&s)) return 0;
        use[slot]++;
        if (use[slot] > s.count) return 0;
        grid[i] = crf_mk(s.item, 1, s.meta);
    }
    result = crf_findMatching(recipes, nrecipes, grid);
    if (crf_isEmpty(result) || result.item == (i32)0xffffffff) return 0;
    next = env->pl.inv;
    for (slot = 0; slot < ISR_MAIN_SLOTS; ++slot)
        if (use[slot]) (void)isr_decr_stack_size(&next, slot, use[slot]);
    output = ic_mk(result.item, result.count, result.meta);
    (void)isr_add_item_stack_to_inventory(&next, &output);
    if (!isr_is_empty(&output)) return 0;
    env->pl.inv = next;
    return 1;
}

/* rl_mode.c rl_do_interact + runtime.c gm_runtime_use_block table/furnace
 * branches. The real env picks the nearest container id (58/61/62) from its
 * scan cache: those are "always" scan ids, so membership is exactly presence
 * in the scan window around the player block, cache order is the scan order
 * (dx, then dz, then y DESCENDING), the d2 values were recomputed from the
 * current pose at the previous emit, and selection is strict-< (first in
 * scan order wins ties). A fresh scan in the identical order reproduces the
 * pick byte-for-byte. Furnace opens (61/62) mirror gm_runtime_use_block's
 * slot allocation: reuse the active furnace bound to that block position,
 * else claim a free slot (init to empty) - runtime.c:895. */
MC_HD static inline int blaze_do_interact(Blaze *env) {
    float fx = (float)(env->pl.ent.posX + (double)env->ox);
    float fy = (float)(env->pl.ent.posY);
    float fz = (float)(env->pl.ent.posZ + (double)env->oz);
    int pwx = (int)floor((double)fx);
    int pwy = (int)floor((double)fy);
    int pwz = (int)floor((double)fz);
    int y0 = pwy - CU_Y_DOWN, y1 = pwy + CU_Y_UP;
    double bd = 36.0;   /* gm_runtime_use_block reach check, squared */
    int bx = 0, by = 0, bz = 0, best = 0, dx, dz, y;
    CU_OP(env, CU_OP_INTERACT);
    if (y0 < 0)   y0 = 0;
    if (y1 > 255) y1 = 255;
    if (env->n_cont >= 0) {
        /* container-list pick, value-identical to the window scan below: the
         * scan accepts on STRICT d2 < best, so ties keep the EARLIEST cell in
         * scan order (x asc, z asc, y DESC) - the same cell as the lex min of
         * (d2, wx, wz, -wy) over the (unordered) list. d2 is computed with
         * the identical expression, so its bits match per cell. */
        int c;
        for (c = 0; c < env->n_cont; ++c) {
            int wx = env->cont[c * 3 + 0];
            int wy = env->cont[c * 3 + 1];
            int wz = env->cont[c * 3 + 2];
            double ddx, ddy, ddz, d2;
            if (wx < pwx - CU_OBS_R || wx > pwx + CU_OBS_R ||
                wz < pwz - CU_OBS_R || wz > pwz + CU_OBS_R ||
                wy < y0 || wy > y1) continue;
            ddx = wx + 0.5 - (double)fx;
            ddy = wy + 0.5 - (double)fy;
            ddz = wz + 0.5 - (double)fz;
            d2 = ddx * ddx + ddy * ddy + ddz * ddz;
            if (d2 < bd ||
                (best && d2 == bd &&
                 (wx < bx || (wx == bx && (wz < bz ||
                  (wz == bz && wy > by)))))) {
                bd = d2; bx = wx; by = wy; bz = wz; best = 1;
            }
        }
    } else {
        for (dx = -CU_OBS_R; dx <= CU_OBS_R; ++dx)
            for (dz = -CU_OBS_R; dz <= CU_OBS_R; ++dz) {
                int wx = pwx + dx, wz = pwz + dz;
                for (y = y1; y >= y0; --y) {
                    int id = cu_world_block(env, wx, y, wz);
                    double ddx, ddy, ddz, d2;
                    if (id != 58 && id != 61 && id != 62) continue;
                    ddx = wx + 0.5 - (double)fx;
                    ddy = y + 0.5 - (double)fy;
                    ddz = wz + 0.5 - (double)fz;
                    d2 = ddx * ddx + ddy * ddy + ddz * ddz;
                    if (d2 < bd) { bd = d2; bx = wx; by = y; bz = wz;
                                   best = 1; }
                }
            }
    }
    if (!best) return 0;
    /* gm_runtime_use_block (runtime.c:882): the eye-height distance recheck.
     * The real check adds float view y + float eye height BEFORE the double
     * subtraction - replicate the float add. */
    {
        float eyef = fy + (float)PSV_EYE_HEIGHT;
        double dx2 = (bx + 0.5) - (double)fx;
        double dy2 = (by + 0.5) - (double)eyef;
        double dz2 = (bz + 0.5) - (double)fz;
        int id;
        if (dx2 * dx2 + dy2 * dy2 + dz2 * dz2 > 36.0) return 0;
        id = cu_world_block(env, bx, by, bz);
        if (id == 58) {
            /* gm_container_close: no-op here (blaze carries no craft grid or
             * cursor; the craft primitive never leaves either non-empty) */
            env->container = 1;
            env->container_wx = bx; env->container_wy = by;
            env->container_wz = bz;
            return 1;
        }
        if (id == 61 || id == 62) {
            int fi, free_slot = -1;
            for (fi = 0; fi < CU_MAX_FURNACES; ++fi) {
                CuFurnace *f = &env->furnaces[fi];
                if (f->active && f->wx == bx && f->wy == by && f->wz == bz) {
                    env->container = 2; env->active_furnace = fi;
                    env->container_wx = bx; env->container_wy = by;
                    env->container_wz = bz;
                    return 1;
                }
                if (!f->active && free_slot < 0) free_slot = fi;
            }
            if (free_slot < 0) return 0;
            {
                CuFurnace *f = &env->furnaces[free_slot];
                f->active = 1; f->wx = bx; f->wy = by; f->wz = bz;
                cu_furnace_init(f);
            }
            env->container = 2; env->active_furnace = free_slot;
            env->container_wx = bx; env->container_wy = by;
            env->container_wz = bz;
            return 1;
        }
        return 0;
    }
}

/* rl_mode.c rl_do_smelt + runtime.c gm_runtime_furnace_insert/extract,
 * merged and ported exactly: with the furnace open (container == 2), pull
 * the whole output slot into the inventory (partial-move semantics: only
 * what the inventory accepts leaves the furnace), top the input slot up
 * with iron ore (first inventory slot holding any), and when the furnace
 * fuel slot is empty feed it ONE coal. Returns 1 when anything moved. */
MC_HD static inline int blaze_do_smelt(Blaze *env) {
    int did = 0, i;
    CuFurnace *f;
    CU_OP(env, CU_OP_SMELT);
    if (env->container != 2 || env->active_furnace < 0) return 0;
    f = &env->furnaces[env->active_furnace];
    /* gm_runtime_furnace_extract(r, 2, 64) */
    if (!sr_isEmpty(f->output)) {
        int n = f->output.count < 64 ? f->output.count : 64;
        IsrInv next = env->pl.inv;
        ICStack out = ic_mk(f->output.item, n, f->output.meta);
        int moved;
        (void)isr_add_item_stack_to_inventory(&next, &out);
        moved = n - out.count;
        if (moved > 0) {
            (void)cu_furnace_extract(f, 2, moved);
            env->pl.inv = next;
            did = 1;
        }
    }
    /* gm_runtime_furnace_insert(r, 0, slot, count) - first iron-ore slot */
    for (i = 0; i < ISR_MAIN_SLOTS; ++i) {
        ICStack s = isr_get_stack(&env->pl.inv, i);
        int moved;
        if (isr_is_empty(&s) || s.item != 15) continue;
        moved = cu_furnace_insert(f, 0, sr_mk(s.item, s.count, s.meta));
        if (moved > 0) {
            (void)isr_decr_stack_size(&env->pl.inv, i, moved);
            did = 1;
        }
        break;
    }
    /* fuel: one coal, only when the fuel slot is empty (coal-frugal) */
    if (sr_isEmpty(f->fuel)) {
        for (i = 0; i < ISR_MAIN_SLOTS; ++i) {
            ICStack s = isr_get_stack(&env->pl.inv, i);
            int moved;
            if (isr_is_empty(&s) || s.item != 263) continue;
            moved = cu_furnace_insert(f, 1, sr_mk(s.item, 1, s.meta));
            if (moved > 0) {
                (void)isr_decr_stack_size(&env->pl.inv, i, moved);
                did = 1;
            }
            break;
        }
    }
    return did;
}

/* =================== one whole game tick (gm_runtime_tick slice) ========== */

/* tick body WITHOUT the recenter: the caller recenters first (serially via
 * blaze_runtime_tick below, or warp-cooperatively in the CUDA k_tick). */
MC_HD static inline void blaze_runtime_tick_nr(Blaze *env, const McSinTable *st,
                                               CuAction act, McAABB *blocks) {
    CuEdit edits[CU_MAX_EDITS];
    int n = 0, i;

    if (env->dead) return;                       /* r->dead || r->won gate */

    /* bow draw / eye-of-ender (runtime.c:264-269): need held item 261/381,
     * neither craftable with the chain's item set - unreachable. The
     * runtime-level do_place-table open (runtime.c:299-307) fires only on
     * caller-set action.do_place, which the RL protocol never sets (rl_mode
     * memsets the action; use goes through player_ctl's rightClickMouse). */

    /* open-container distance check (runtime.c:270-281): live for the full
     * chain (interact opens the table; walking >6 blocks or breaking it
     * closes). gm_container_close is a no-op on an empty grid/cursor
     * (neither is snapshot state and the craft primitive never fills them). */
    if (env->container) {
        float cvx = (float)(env->pl.ent.posX + (double)env->ox);
        float cvy = (float)(env->pl.ent.posY);
        float cvz = (float)(env->pl.ent.posZ + (double)env->oz);
        double dx = (env->container_wx + 0.5) - cvx;
        double dy = (env->container_wy + 0.5) - (cvy + (float)PSV_EYE_HEIGHT);
        double dz = (env->container_wz + 0.5) - cvz;
        int id = cu_world_block(env, env->container_wx, env->container_wy,
                                env->container_wz);
        int valid = env->container == 1 ? id == 58 : (id == 61 || id == 62);
        if (!valid || dx * dx + dy * dy + dz * dz > 36.0) {
            env->container = 0;
            env->active_furnace = -1;   /* runtime.c:279 */
        }
    }

    /* dragon/mob player-attack (runtime.c:308-312): dimension 0 + mobs off
     * (--mobs off, store empty) -> both return 0. Window refill (runtime.c:
     * 290-298): blaze's window is updated in place on every edit,
     * value-identical to the gen-triggered refill. */

    blaze_player_tick(env, st, act, edits, &n, CU_MAX_EDITS, blocks);

    /* ghost pushers (runtime.c:328-354): tape-replay only, nghosts==0. */

    for (i = 0; i < n; ++i) {
        cu_world_set_state(env, edits[i].wx, edits[i].wy, edits[i].wz,
                           edits[i].id, edits[i].meta);
        /* gm_fluid_mark: fluids CA not simulated; snapshots with liquids in
         * the region are flagged at bake time and excluded. */
        cu_break_unsupported_plants(env, edits[i].wx, edits[i].wy, edits[i].wz);
        /* water/lava placement interactions (runtime.c:361-372), ported for
         * completeness - edit ids 8/10 need a filled bucket; buckets are not
         * in rl_crafts, so still unobtainable. The nether water-vaporize branch
         * (dimension == -1) and the fire -> portal ignite follower
         * (runtime.c:373, needs flint&steel + the portal machinery) are NOT
         * ported: dimension is always 0 here and id 51 edits are unreachable. */
        if (edits[i].id == 8 || edits[i].id == 10) {
            static const int adx[6] = {1, -1, 0, 0, 0, 0};
            static const int ady[6] = {0, 0, 1, -1, 0, 0};
            static const int adz[6] = {0, 0, 0, 0, 1, -1};
            int q;
            for (q = 0; q < 6; ++q) {
                int x = edits[i].wx + adx[q], y = edits[i].wy + ady[q],
                    z = edits[i].wz + adz[q];
                int id2 = cu_world_block(env, x, y, z);
                if (edits[i].id == 8 && (id2 == 10 || id2 == 11))
                    cu_world_set_state(env, x, y, z,
                                       cu_world_meta(env, x, y, z) == 0 ? 49 : 4,
                                       0);
                else if (edits[i].id == 10 && (id2 == 8 || id2 == 9))
                    cu_world_set_state(env, edits[i].wx, edits[i].wy,
                                       edits[i].wz, 49, 0);
            }
        }
        if (edits[i].drop_id > 0)
            cu_spawn_item(env, edits[i].wx + 0.5, edits[i].wy + 0.5,
                          edits[i].wz + 0.5, edits[i].drop_id,
                          edits[i].drop_count, edits[i].drop_meta, 10);
    }

    /* world clock / weather (no obs field), fluid CA, mobs, dragon,
     * projectiles: inert in the learned stage - skipped. */

    cu_live_tick_player(env);

    /* live furnace tick + 61<->62 lit block flip (runtime.c:398) */
    for (i = 0; i < CU_MAX_FURNACES; ++i) if (env->furnaces[i].active) {
        CuFurnace *f = &env->furnaces[i];
        int was_lit = f->burn_time > 0, lit;
        CU_OP(env, CU_OP_FURNACE_TICK);
        cu_furnace_tick(f);
        lit = f->burn_time > 0;
        if (lit != was_lit) {
            int id = cu_world_block(env, f->wx, f->wy, f->wz);
            if (id == 61 || id == 62)
                cu_world_set_state(env, f->wx, f->wy, f->wz, lit ? 62 : 61,
                                   cu_world_meta(env, f->wx, f->wy, f->wz));
        }
    }

    if (env->vit.health <= 0.0f) {
        env->dead = 1;
        env->deaths++;
    }

    /* portal contact checks (runtime.c:414-459): blocks 90/119 cannot occur
     * in an overworld training region - skipped. */

    env->tick++;
}

/* one whole game tick, serial reference (CPU driver, verify kernels):
 * recenter + tick body. Sequencing identical to the pre-split function
 * (dead gate first, then recenter, then the body's own dead gate). */
MC_HD static inline void blaze_runtime_tick(Blaze *env, const McSinTable *st,
                                            CuAction act, McAABB *blocks) {
    if (!env->dead) cu_recenter(env);
    blaze_runtime_tick_nr(env, st, act, blocks);
}

/* =================== observation (rl_mode.c rl_emit_obs port) ============= */

/* coal-list entry ordering: rl_mode.c rl_block_lt TOTAL order (d2, x, y, z) */
typedef struct { double d2; int x, y, z; } CuCoalEnt;

/* cached coal candidate: world coords + precomputed region cell index. The
 * mined status lives in CU_CAND_MINED of .ri, refreshed only when the env's
 * world_epoch moved (a region cell was written) - on the common tick the
 * whole coal pass is one sequential 16 B/candidate sweep, no cells loads. */
typedef struct CuCand { int x, y, z, ri; } CuCand;
#define CU_CAND_MINED 0x40000000

MC_HD static inline int cu_coal_lt(const CuCoalEnt *a, const CuCoalEnt *b) {
    if (a->d2 != b->d2) return a->d2 < b->d2;
    if (a->x != b->x) return a->x < b->x;
    if (a->y != b->y) return a->y < b->y;
    return a->z < b->z;
}

/* Coal list: static ore set (snapshot region scan) minus mined (region cell
 * no longer coal), membership = the rl_mode scan window around the player
 * BLOCK (float view pose, +-16 xz, -24/+40 y clamped to [0,255]; coal is an
 * "always" id so depth never hides it), sorted by (d2,x,y,z) with d2 from
 * the FLOAT view pose (feet y, +0.5 block centers) exactly as rl_emit_obs
 * computes it. Returns the emitted count (<= CU_NCOAL).
 *
 * Implementation (semantics identical to rl_mode's append-all + full sort;
 * verified by the M1/M2 bitwise gates):
 * - bounded top-CU_NCOAL selection: under the STRICT total order (d2, x, y,
 *   z - block coords are unique, so no ties) the kept top-32 is IDENTICAL
 *   to full-sort + truncate, including the 512-accepted scan cap (counted,
 *   break before the 513th append, exactly like the scratch-full break).
 *   Keeps the hot array at 32 entries in thread-local storage - the
 *   512-entry variant in pooled global memory made k_tick latency-bound.
 * - geometric candidate cache: window membership (bounds tests) depends
 *   only on the player BLOCK coords, so the ores passing them are cached per
 *   env (in ore order, coords + region index + mined flag) and rebuilt only
 *   when (pwx,pwy,pwz) changes; the accept cap + selection run per call over
 *   the cached order - byte-identical accept sequence. The mined flags are
 *   re-read from the region only when world_epoch moved (some cell was
 *   written; digging is rare next to the per-sub-tick call rate). Ores whose
 *   region index is invalid (cannot happen for snapshot ores) are permanently
 *   non-coal and never accepted, so they are simply not cached. Snapshots
 *   carry 2-3.4k ores; the full scan every sub-tick dominated k_tick.
 *   n_cand == -1 means the window held > CU_COAL_CAND candidates (never
 *   seen; snapshots peak ~500): fall back to the full scan. */
/* Candidate-cache maintenance, split out of blaze_coal_list so the CUDA
 * warp-cooperative sweep can run it on one lane and then fan the (unchanged)
 * sweep out across the warp. Rebuild on player-block move, mined-flag
 * refresh on world_epoch move; statement-identical to the pre-split code. */
MC_HD static inline void blaze_coal_cache_sync(Blaze *env, int pwx, int pwy,
                                               int pwz, int y0, int y1) {
    int i, c;
    if (!env->cand_valid || env->cand_pwx != pwx || env->cand_pwy != pwy ||
        env->cand_pwz != pwz) {
        int m = 0;
        CU_OP(env, CU_OP_COAL_REBUILD);
        if (env->ore_xy) {
            /* spatially bucketed rebuild: the ore list is strictly writer-
             * ordered (lex region x,y,z; verified by blaze_build_ore_xy), so
             * each (ix, iy-range) is one CONTIGUOUS ore-array span in the
             * original list order. Walking ix ascending over the window's
             * clamped x/y ranges therefore reproduces the full scan's
             * ascending ore-index accept sequence byte-for-byte - only the z
             * test (and the ri guard) remains per entry. Ores outside the
             * region cannot exist here (build rejects them -> ore_xy NULL). */
            int ix0 = pwx - CU_OBS_R - env->rx0;
            int ix1 = pwx + CU_OBS_R - env->rx0;
            int iy0 = y0 - env->ry0, iy1 = y1 - env->ry0;
            int ix;
            if (ix0 < 0) ix0 = 0;
            if (ix1 > env->rnx - 1) ix1 = env->rnx - 1;
            if (iy0 < 0) iy0 = 0;
            if (iy1 > env->rny - 1) iy1 = env->rny - 1;
            for (ix = ix0; ix <= ix1 && iy0 <= iy1 && m >= 0; ++ix) {
                int b1 = env->ore_xy[ix * env->rny + iy1 + 1];
                for (i = env->ore_xy[ix * env->rny + iy0]; i < b1; ++i) {
                    int wx = env->ore[i * 3 + 0];
                    int wy = env->ore[i * 3 + 1];
                    int wz = env->ore[i * 3 + 2];
                    long ri;
                    if (wz < pwz - CU_OBS_R || wz > pwz + CU_OBS_R) continue;
                    ri = cu_region_idx(env, wx, wy, wz);
                    if (ri < 0) continue;
                    if (m >= CU_COAL_CAND) { m = -1; break; }
                    env->coal_cand[m].x = wx;
                    env->coal_cand[m].y = wy;
                    env->coal_cand[m].z = wz;
                    env->coal_cand[m].ri =
                        mc_state_id(env->cells[ri]) == BLK_COAL_ORE
                            ? (int)ri : ((int)ri | CU_CAND_MINED);
                    ++m;
                }
            }
        } else {
            for (i = 0; i < env->nore; ++i) {
                int wx = env->ore[i * 3 + 0];
                int wy = env->ore[i * 3 + 1];
                int wz = env->ore[i * 3 + 2];
                long ri;
                if (wx < pwx - CU_OBS_R || wx > pwx + CU_OBS_R) continue;
                if (wz < pwz - CU_OBS_R || wz > pwz + CU_OBS_R) continue;
                if (wy < y0 || wy > y1) continue;
                ri = cu_region_idx(env, wx, wy, wz);
                if (ri < 0) continue;     /* permanently non-coal: never cache */
                if (m >= CU_COAL_CAND) { m = -1; break; }
                env->coal_cand[m].x = wx;
                env->coal_cand[m].y = wy;
                env->coal_cand[m].z = wz;
                env->coal_cand[m].ri =
                    mc_state_id(env->cells[ri]) == BLK_COAL_ORE
                        ? (int)ri : ((int)ri | CU_CAND_MINED);
                ++m;
            }
        }
        env->n_cand = m;
        env->cand_pwx = pwx; env->cand_pwy = pwy; env->cand_pwz = pwz;
        env->cand_valid = 1;
        env->cand_epoch = env->world_epoch;
    } else if (env->n_cand >= 0 && env->cand_epoch != env->world_epoch) {
        for (c = 0; c < env->n_cand; ++c) {   /* refresh mined flags only */
            int ri = env->coal_cand[c].ri & ~CU_CAND_MINED;
            env->coal_cand[c].ri =
                mc_state_id(env->cells[ri]) == BLK_COAL_ORE
                    ? ri : (ri | CU_CAND_MINED);
        }
        env->cand_epoch = env->world_epoch;
    }
}

/* Serial sweep + top-32 selection over the SYNCED candidate cache (or the
 * full ore list on cache overflow). Split from blaze_coal_list so the CUDA
 * warp path can reuse it as its exact fallback without re-counting the
 * CU_OP_COAL_CALL of the composed function. */
MC_HD static inline int blaze_coal_sweep(Blaze *env, float fx, float fy,
                                         float fz, int pwx, int pwz,
                                         int y0, int y1,
                                         int out[CU_NCOAL][3]) {
    CuCoalEnt best[CU_NCOAL];
    int nbest = 0, nl = 0, i, j, k, c, ncand;
    ncand = env->n_cand >= 0 ? env->n_cand : env->nore;
    CU_OP_ADD(env, CU_OP_COAL_SWEEP, ncand);
    for (c = 0; c < ncand; ++c) {
        int wx, wy, wz;
        double ddx, ddy, ddz;
        CuCoalEnt v;
        if (env->n_cand >= 0) {
            CuCand cd = env->coal_cand[c];
            wx = cd.x; wy = cd.y; wz = cd.z;
            /* mined check == cu_world_block, via the epoch-fresh flag */
            if (cd.ri & CU_CAND_MINED) continue;
        } else {                 /* overflow fallback: full geometric test */
            wx = env->ore[c * 3 + 0];
            wy = env->ore[c * 3 + 1];
            wz = env->ore[c * 3 + 2];
            if (wx < pwx - CU_OBS_R || wx > pwx + CU_OBS_R) continue;
            if (wz < pwz - CU_OBS_R || wz > pwz + CU_OBS_R) continue;
            if (wy < y0 || wy > y1) continue;
            if (cu_world_block(env, wx, wy, wz) != BLK_COAL_ORE) continue;
        }
        if (nl >= CU_COAL_SCRATCH) break;
        ++nl;
        ddx = wx + 0.5 - (double)fx;
        ddy = wy + 0.5 - (double)fy;
        ddz = wz + 0.5 - (double)fz;
        v.d2 = ddx * ddx + ddy * ddy + ddz * ddz;
        v.x = wx; v.y = wy; v.z = wz;
        if (nbest == CU_NCOAL && !cu_coal_lt(&v, &best[CU_NCOAL - 1]))
            continue;
        j = (nbest < CU_NCOAL ? nbest : CU_NCOAL - 1) - 1;
        for (; j >= 0 && cu_coal_lt(&v, &best[j]); --j) best[j + 1] = best[j];
        best[j + 1] = v;
        if (nbest < CU_NCOAL) ++nbest;
    }
    k = nl < CU_NCOAL ? nl : CU_NCOAL;
    for (i = 0; i < k; ++i) {
        out[i][0] = best[i].x; out[i][1] = best[i].y; out[i][2] = best[i].z;
    }
    for (; i < CU_NCOAL; ++i) { out[i][0] = out[i][1] = out[i][2] = 0; }
    return k;
}

MC_HD static inline int blaze_coal_list(Blaze *env, int out[CU_NCOAL][3]) {
    float fx = (float)(env->pl.ent.posX + (double)env->ox);
    float fy = (float)(env->pl.ent.posY);
    float fz = (float)(env->pl.ent.posZ + (double)env->oz);
    int pwx = (int)floor((double)fx);
    int pwy = (int)floor((double)fy);
    int pwz = (int)floor((double)fz);
    int y0 = pwy - CU_Y_DOWN, y1 = pwy + CU_Y_UP;
    CU_OP(env, CU_OP_COAL_CALL);
    if (y0 < 0)   y0 = 0;
    if (y1 > 255) y1 = 255;
    blaze_coal_cache_sync(env, pwx, pwy, pwz, y0, y1);
    return blaze_coal_sweep(env, fx, fy, fz, pwx, pwz, y0, y1, out);
}

/* total count of item id across the 36 main slots (rl_mode.c rl_inv_count) */
MC_HD static inline int blaze_inv_count(const Blaze *env, int item) {
    int i, n = 0;
    CU_OP(env, CU_OP_INV_SCAN);
    for (i = 0; i < ISR_MAIN_SLOTS; ++i) {
        ICStack s = isr_get_stack(&env->pl.inv, i);
        if (!isr_is_empty(&s) && s.item == item) n += s.count;
    }
    return n;
}

/* Camera frame into the env's cam/dep/edg buffers (oc_pixel over the region
 * id tensor; eye = exact double pose + origin, matching rl_emit_obs). The
 * real env's camera region mirrors the LIVE world over the full ray reach;
 * blaze rays that leave the 64x128x64 snapshot region read air - the design
 * bounds episodes to the region so this never fires in-distribution.
 * Single-pixel form so the CUDA k_obs (one thread per pixel) and the serial
 * CPU loop share the exact ray source. */
MC_HD static inline void blaze_render_cam_pixel(Blaze *env, const McSinTable *st,
                                                int pix) {
    OcRegion reg;
    double ex = env->pl.ent.posX + (double)env->ox;
    double ey = env->pl.ent.posY + PSV_EYE_HEIGHT;
    double ez = env->pl.ent.posZ + (double)env->oz;
    reg.cells = env->cam_cells;
    reg.x0 = env->rx0; reg.y0 = env->ry0; reg.z0 = env->rz0;
    reg.nx = env->rnx; reg.ny = env->rny; reg.nz = env->rnz;
    oc_pixel(&reg, st, ex, ey, ez, env->pl.yaw, env->pl.pitch,
             pix % CU_CAM_W, pix / CU_CAM_W,
             &env->cam[pix], &env->dep[pix], &env->edg[pix]);
}

MC_HD static inline void blaze_render_cam(Blaze *env, const McSinTable *st) {
    int pix;
    for (pix = 0; pix < CU_NPIX; ++pix)
        blaze_render_cam_pixel(env, st, pix);
}

/* Fill a BOLR-layout record. want_cam=0 re-emits the previous frame buffers
 * (rl_mode "cam":0 semantics). blocks/logs stay zeroed (excluded from the
 * fidelity gate; see CuBinObs note). */
MC_HD static inline void blaze_emit_bolr(Blaze *env, const McSinTable *st,
                                         CuBinObs *o, int want_cam) {
    static const int inv_ids[9] = CU_INV_IDS;
    int i;
    memset(o, 0, sizeof *o);
    o->magic = CU_BIN_MAGIC;
    o->tick = env->tick;
    o->x = (double)(float)(env->pl.ent.posX + (double)env->ox);
    o->y = (double)(float)(env->pl.ent.posY);
    o->z = (double)(float)(env->pl.ent.posZ + (double)env->oz);
    o->yaw = env->pl.yaw;
    o->pitch = env->pl.pitch;
    o->dead = env->dead;
    {
        int sel = env->pl.inv.current_item;
        if (sel < 0) sel = 0;
        if (sel > 8) sel = 8;
        o->hotbar_sel = sel;
    }
    for (i = 0; i < 9; ++i) {
        ICStack s = isr_get_stack(&env->pl.inv, i);
        o->hotbar_ids[i] = s.item;
        o->hotbar_counts[i] = s.count;
        o->inv_counts[i] = blaze_inv_count(env, inv_ids[i]);
    }
    o->container = env->container;
    (void)blaze_coal_list(env, o->coal);
    if (want_cam)
        blaze_render_cam(env, st);
    memcpy(o->cam, env->cam, sizeof o->cam);
    memcpy(o->depth, env->dep, sizeof o->depth);
    memcpy(o->edge, env->edg, sizeof o->edge);
}

/* =================== decision step: reward + scalars (ppo_coal.py) ======== */

/* Trainer-side semantics of ppo_coal.py (act_dict, nearest_coal, reward),
 * ported exactly in libm doubles. NOT part of the sim-fidelity gate vs the
 * real game; CPU and CUDA share THIS source and are gated against each other
 * (bitwise target, <=1e-12 relative fallback for the device-libm calls).
 *
 * Split into ticks + finalize so the CUDA driver can run the per-pixel
 * camera kernel between them: the crosshair +0.03 bonus for the LAST
 * executed sub-tick reads the decision frame's center pixel (fresh when
 * sub-tick repeat-1 ran, else the persisted previous frame), so it is
 * applied in finalize, after k_obs. The double accumulation order equals a
 * straight per-tick `r_total += r` sum, so splitting changes no bits. */

#define CU_DEC_PI 3.14159265358979323846

/* act_dict yaw/pitch heads (ppo_coal.py:51-54) */
MC_HD static inline float cu_yaw_step(int i) {
    return i == 0 ? -15.0f : (i == 1 ? 0.0f : 15.0f);
}
MC_HD static inline float cu_pitch_step(int i) {
    return i == 0 ? -10.0f : (i == 1 ? 0.0f : 10.0f);
}

MC_HD static inline double blaze_wrap180(double a) {
    a = fmod(a + 180.0, 360.0);
    if (a < 0.0) a += 360.0;
    return a - 180.0;
}

/* nearest_coal over an emitted coal list: (rel_yaw, rel_pitch, dist); returns
 * 0 when the list is empty. Matches ppo_coal.nearest_coal incl. the
 * first-strictly-lower min and the [0,0,0] terminator. */
MC_HD static inline int blaze_nearest_coal(const int coal[CU_NCOAL][3],
                                           double x, double y, double z,
                                           double yaw, double pitch,
                                           double *ry, double *rp,
                                           double *dist) {
    double ex = x, ey = y + 1.62, ez = z;
    double bd = 0.0;
    int have = 0, i;
    for (i = 0; i < CU_NCOAL; ++i) {
        double dx, dy, dz, d;
        if (coal[i][0] == 0 && coal[i][1] == 0 && coal[i][2] == 0) break;
        dx = coal[i][0] + 0.5 - ex;
        dy = coal[i][1] + 0.5 - ey;
        dz = coal[i][2] + 0.5 - ez;
        d = sqrt(dx * dx + dy * dy + dz * dz);
        if (!have || d < bd) {
            double dd = d > 1e-9 ? d : 1e-9;
            *ry = blaze_wrap180(atan2(-dx, dz) * (180.0 / CU_DEC_PI) - yaw);
            *rp = -asin(dy / dd) * (180.0 / CU_DEC_PI) - pitch;
            *dist = d;
            bd = d;
            have = 1;
        }
    }
    return have;
}

/* One trainer decision for one env: `repeat` game ticks with dyaw/dpitch
 * applied on sub-tick 0 only. a[13] = the FULL raw action vector, the same
 * layout as blaze_tick_raw: {forward, strafe, dyaw(deg), dpitch(deg), jump,
 * sneak, sprint, attack, use, hotbar(0..8 or -1), craft(rl_crafts index or
 * -1), interact(0/1), smelt(0/1)}. forward/strafe may be fractional.
 * craft/interact/smelt are PRE-tick protocol primitives: applied ONCE,
 * before sub-tick 0, in rl_mode order. The legacy 5-head trainer
 * actions are expanded to this layout by the Python wrapper (blaze.py) with
 * the exact old numeric decode, so trainer semantics are bit-identical.
 * When render_cam_inline, sub-tick repeat-1 renders the camera in-thread
 * (CPU path); the CUDA path passes 0 and k_obs renders envs with
 * dec_cam_fresh.
 * Reward terms per sub-tick: -0.005, 0.5*(prev_dist-dist) shaping, +0.03
 * attack-with-crosshair-on-coal (persisted center pixel; deferred to
 * finalize for the last executed sub-tick), +10 & done on coal-count
 * increase. blocks is per-worker pooled McAABB[PSV_MAX_BLOCKS] scratch;
 * recipes/nrecipes is the driver's crf_build table (craft primitive). */
/* atk_gate (OPT-IN training-reward mode, default 0.0 = off = exact ppo_coal
 * semantics): when > 0, the +0.03 crosshair-attack bonus additionally
 * requires the nearest coal to be within atk_gate blocks (chain_probe mines
 * only within 3.2 - farther drops strand out of pickup range, and the
 * ungated bonus was teaching mine-on-sight from beyond it). */
/* Decision prologue: dec_* reset, done gate, pre-tick protocol primitives,
 * prev_dist init. Returns 0 when the env is done (idle until reset), 1 when
 * the sub-tick loop should run. */
MC_HD static inline int blaze_decision_begin(Blaze *e, const McSinTable *st,
                                             const double *a,
                                             const CRRecipe *recipes,
                                             int nrecipes) {
    (void)st;
    e->dec_rew_pre = 0.0;
    e->dec_r_last = 0.0;
    e->dec_have_last = 0;
    e->dec_plus10 = 0;
    e->dec_attack = (int)a[7];
    e->dec_cam_fresh = 0;
    e->dec_have_nc = 0;
    e->dec_ry = e->dec_rp = e->dec_dist = 0.0;
    if (e->done) return 0;           /* done envs idle until reset */

    /* pre-tick protocol primitives, once per decision (rl_mode order:
     * craft, then interact, then smelt) */
    if ((int)a[10] >= 0)
        (void)blaze_do_craft(e, (int)a[10], recipes, nrecipes);
    if ((int)a[11])
        (void)blaze_do_interact(e);
    if ((int)a[12])
        (void)blaze_do_smelt(e);

    if (!e->have_prev_dist) {
        int coal_now[CU_NCOAL][3];
        double ry = 0.0, rp = 0.0;
        (void)blaze_coal_list(e, coal_now);
        e->have_prev_dist = blaze_nearest_coal(
            coal_now,
            (double)(float)(e->pl.ent.posX + (double)e->ox),
            (double)(float)e->pl.ent.posY,
            (double)(float)(e->pl.ent.posZ + (double)e->oz),
            (double)e->pl.yaw, (double)e->pl.pitch, &ry, &rp, &e->prev_dist);
    }
    return 1;
}

/* Physics half of one sub-tick (action decode + game tick + cam-fresh mark);
 * emits the FLOAT view pose the coal pass consumes. Split from the reward
 * half so the CUDA warp path can run the coal sweep warp-cooperatively
 * between the two; blaze_decision_subtick composes them statement-for-
 * statement identically to the pre-split function. */
MC_HD static inline void blaze_subtick_phys(Blaze *e, const McSinTable *st,
                                            const double *a, int rep,
                                            int repeat, McAABB *blocks,
                                            int render_cam_inline,
                                            double *fx, double *fy,
                                            double *fz) {
    CuAction act;
    CU_OP(e, CU_OP_SUBTICK);
    memset(&act, 0, sizeof act);
    act.forward = (float)a[0];
    act.strafe = (float)a[1];
    act.dyaw = rep == 0 ? (float)a[2] : 0.0f;
    act.dpitch = rep == 0 ? (float)a[3] : 0.0f;
    act.jump = (int)a[4];
    act.sneak = (int)a[5];
    act.sprint = (int)a[6];
    act.attack = (int)a[7];
    act.use = (int)a[8];
    act.hotbar_sel = (int)a[9];
    blaze_runtime_tick_nr(e, st, act, blocks);
    if (rep == repeat - 1) {
        e->dec_cam_fresh = 1;
        if (render_cam_inline) blaze_render_cam(e, st);
    }
    *fx = (double)(float)(e->pl.ent.posX + (double)e->ox);
    *fy = (double)(float)e->pl.ent.posY;
    *fz = (double)(float)(e->pl.ent.posZ + (double)e->oz);
}

/* Reward half of one sub-tick: shaping from the coal result, success/done
 * gates, envelope guard, dec_* accumulation. */
MC_HD static inline void blaze_subtick_post(Blaze *e, int rep, int repeat,
                                            double atk_gate, int have_nc,
                                            double ry, double rp,
                                            double dist) {
    double r = -0.005;
    int plus10 = 0, is_last;
    {
        if (have_nc && e->have_prev_dist)
            r += 0.5 * (e->prev_dist - dist);
        if (have_nc) { e->prev_dist = dist; e->have_prev_dist = 1; }
        if (e->success_item > 0 &&
            blaze_inv_count(e, e->success_item) > e->base_success) {
            plus10 = 1;
            e->done = 1;
        }
        if (e->dead) e->done = 2;
        {   /* region-envelope guard: outside the snapshot region the world
             * reads air - end the episode before fidelity can degrade
             * (DESIGN Part 4). */
            double wx = e->pl.ent.posX + (double)e->ox;
            double wz = e->pl.ent.posZ + (double)e->oz;
            if (wx < e->rx0 + 2 || wx > e->rx0 + e->rnx - 2 ||
                wz < e->rz0 + 2 || wz > e->rz0 + e->rnz - 2)
                e->done = 2;
        }

        is_last = (rep == repeat - 1) || e->done;
        if (is_last) {
            e->dec_have_last = 1;
            e->dec_r_last = r;
            e->dec_plus10 = plus10;
            e->dec_have_nc = have_nc;
            e->dec_ry = ry; e->dec_rp = rp; e->dec_dist = dist;
        } else {
            /* not the last executed tick: the crosshair check reads the
             * persisted frame NOW (it is only re-rendered on the last
             * sub-tick); addition order 0.03 then 10.0 matches ppo_coal. */
            if (e->dec_attack && e->cam[18 * CU_CAM_W + 32] == BLK_COAL_ORE &&
                (atk_gate <= 0.0 || (have_nc && dist <= atk_gate)))
                r += 0.03;
            if (plus10) r += 10.0;
            e->dec_rew_pre += r;
        }
    }
}

/* One decision sub-tick WITHOUT the recenter - the caller recenters first
 * (serially in blaze_decision_ticks, warp-cooperatively in the CUDA k_tick).
 * Caller contract: only while !e->done, after blaze_decision_begin ran. */
MC_HD static inline void blaze_decision_subtick(Blaze *e, const McSinTable *st,
                                                const double *a, int rep,
                                                int repeat, McAABB *blocks,
                                                int render_cam_inline,
                                                double atk_gate) {
    int coal_now[CU_NCOAL][3];
    double ry = 0.0, rp = 0.0, dist = 0.0, fx, fy, fz;
    int have_nc;
    blaze_subtick_phys(e, st, a, rep, repeat, blocks, render_cam_inline,
                       &fx, &fy, &fz);
    (void)blaze_coal_list(e, coal_now);
    have_nc = blaze_nearest_coal(coal_now, fx, fy, fz,
                                 (double)e->pl.yaw, (double)e->pl.pitch,
                                 &ry, &rp, &dist);
    blaze_subtick_post(e, rep, repeat, atk_gate, have_nc, ry, rp, dist);
}

/* Whole decision, serial reference (CPU driver): begin + recenter + sub-tick
 * per rep. Statement-for-statement the pre-split function (the recenter that
 * used to open blaze_runtime_tick now sits between the done and dead gates
 * at the same sequence point). */
MC_HD static inline void blaze_decision_ticks(Blaze *e, const McSinTable *st,
                                              const double *a, int repeat,
                                              McAABB *blocks,
                                              int render_cam_inline,
                                              double atk_gate,
                                              const CRRecipe *recipes,
                                              int nrecipes) {
    int rep;
    if (!blaze_decision_begin(e, st, a, recipes, nrecipes)) return;
    for (rep = 0; rep < repeat && !e->done; ++rep) {
        if (!e->dead) cu_recenter(e);
        blaze_decision_subtick(e, st, a, rep, repeat, blocks,
                               render_cam_inline, atk_gate);
    }
}

/* Finish the decision AFTER the camera frame is current: apply the deferred
 * crosshair/+10 terms and write reward, the 6 scalars (ppo_coal.planes),
 * done and pose. Any output may be NULL. */
MC_HD static inline void blaze_decision_finalize(Blaze *e, const McSinTable *st,
                                                 float *scal, float *rew,
                                                 unsigned char *done,
                                                 float *pose,
                                                 double atk_gate) {
    double ry = e->dec_ry, rp = e->dec_rp, dist = e->dec_dist;
    int have_nc = e->dec_have_nc;
    (void)st;
    if (rew) {
        double r_total = e->dec_rew_pre;
        if (e->dec_have_last) {
            double r = e->dec_r_last;
            if (e->dec_attack && e->cam[18 * CU_CAM_W + 32] == BLK_COAL_ORE &&
                (atk_gate <= 0.0 ||
                 (e->dec_have_nc && e->dec_dist <= atk_gate)))
                r += 0.03;
            if (e->dec_plus10) r += 10.0;
            r_total += r;
        }
        *rew = (float)r_total;
    }
    if (scal) {
        double pr = (double)e->pl.pitch * (CU_DEC_PI / 180.0);
        if (!have_nc) {
            int coal_now[CU_NCOAL][3];
            (void)blaze_coal_list(e, coal_now);
            have_nc = blaze_nearest_coal(
                coal_now,
                (double)(float)(e->pl.ent.posX + (double)e->ox),
                (double)(float)e->pl.ent.posY,
                (double)(float)(e->pl.ent.posZ + (double)e->oz),
                (double)e->pl.yaw, (double)e->pl.pitch, &ry, &rp, &dist);
        }
        if (!have_nc) {
            scal[0] = 0.0f; scal[1] = 0.0f; scal[2] = 0.0f; scal[3] = 1.0f;
        } else {
            scal[0] = (float)sin(ry * (CU_DEC_PI / 180.0));
            scal[1] = (float)cos(ry * (CU_DEC_PI / 180.0));
            scal[2] = (float)(rp / 90.0);
            scal[3] = (float)((dist < 24.0 ? dist : 24.0) / 24.0);
        }
        scal[4] = (float)sin(pr);
        scal[5] = (float)cos(pr);
    }
    if (done) *done = (unsigned char)e->done;
    if (pose) {
        pose[0] = (float)(e->pl.ent.posX + (double)e->ox);
        pose[1] = (float)e->pl.ent.posY;
        pose[2] = (float)(e->pl.ent.posZ + (double)e->oz);
        pose[3] = e->pl.yaw;
        pose[4] = e->pl.pitch;
    }
}

/* =================== trainer status vector (chain milestones) ============= */

/* CU_STATUS_K int32 values per env: the 9 rl_inv_ids counts (log, planks,
 * stick, cobble, table, w.pick, s.pick, coal, torch), hotbar_sel, the held
 * item id (0 when the slot is empty), the container state, the live dig
 * progress in permille (0 when not hitting a block), then the iron-chain
 * counts: furnace item (61), iron ore (15), iron ingot (265), iron pickaxe
 * (257). Everything a milestone-chain trainer needs that the float outputs
 * do not carry. Consumed by the blaze_step_full drivers; NOT part of the
 * sim-fidelity gate (pure readout of gated state; the gated BOLR
 * inv_counts stays the 9-id CU_INV_IDS layout). */
#define CU_STATUS_K 17
MC_HD static inline void blaze_fill_status(const Blaze *e, int *out) {
    static const int inv_ids[9] = CU_INV_IDS;
    int i, sel = e->pl.inv.current_item;
    for (i = 0; i < 9; ++i) out[i] = blaze_inv_count(e, inv_ids[i]);
    if (sel < 0) sel = 0;
    if (sel > 8) sel = 8;
    out[9] = sel;
    {
        ICStack s = isr_get_stack(&e->pl.inv, sel);
        out[10] = isr_is_empty(&s) ? 0 : s.item;
    }
    out[11] = e->container;
    out[12] = e->dig_hitting ? (int)(e->dig_progress * 1000.0f) : 0;
    out[13] = blaze_inv_count(e, 61);
    out[14] = blaze_inv_count(e, 15);
    out[15] = blaze_inv_count(e, 265);
    out[16] = blaze_inv_count(e, 257);
}

/* =================== live env -> snapshot capture ========================= */

/* Inverse of blaze_reset_scalar: serialize a live env's scalar state into an
 * RlSnapHead + item list (the region cells are copied separately by the
 * driver). Round-trip contract: blaze_reset_scalar(blaze_capture_head(e))
 * restores the exact double bits of pose/box/motion and every player_ctl
 * static that lives in the head. eat_ticks/eat_item and the live FURNACE
 * states are NOT representable in the head (bake contract: quiescent) -
 * envs holding food mid-bite or running a furnace lose that state on
 * capture; curriculum captures must be taken with no active furnace.
 * world_dirty is 0 (blaze has no scan cache). Returns n_items written. */
MC_HD static inline int blaze_capture_head(const Blaze *e, RlSnapHead *h,
                                           RlSnapItem *items) {
    int i, n = 0;
    memset(h, 0, sizeof *h);
    h->magic[0] = 'B'; h->magic[1] = 'S'; h->magic[2] = 'N';
    h->magic[3] = 'P';
    h->version = 1;
    h->seed = e->seed;
    h->tick = e->tick;
    h->ox = e->ox; h->oz = e->oz;
    h->px = e->pl.ent.posX; h->py = e->pl.ent.posY; h->pz = e->pl.ent.posZ;
    h->box[0] = e->pl.ent.box.minX; h->box[1] = e->pl.ent.box.minY;
    h->box[2] = e->pl.ent.box.minZ; h->box[3] = e->pl.ent.box.maxX;
    h->box[4] = e->pl.ent.box.maxY; h->box[5] = e->pl.ent.box.maxZ;
    h->yaw = e->pl.yaw; h->pitch = e->pl.pitch;
    h->mx = e->pl.ent.motionX; h->my = e->pl.ent.motionY;
    h->mz = e->pl.ent.motionZ;
    h->on_ground = e->pl.ent.onGround;
    h->collided_h = e->pl.ent.collidedHorizontally;
    h->collided_v = e->pl.ent.collidedVertically;
    h->is_collided = e->pl.ent.isCollided;
    h->fall_distance = e->pl.fall_distance;
    h->sprinting = e->pl.sprinting;
    h->sprint_toggle_timer = e->pl.sprint_toggle_timer;
    h->jump_factor_sprint = e->pl.jump_factor_sprint;
    h->jump_ticks = e->pl.jump_ticks;
    h->prev_move_forward = e->pl.prev_move_forward;
    h->prev_sneak = e->pl.prev_sneak;
    h->health = e->vit.health;
    h->food = e->vit.foodLevel;
    h->saturation = e->vit.saturation;
    h->exhaustion = e->vit.exhaustion;
    h->food_timer = e->vit.foodTimer;
    h->dig_progress = e->dig_progress;
    h->dig_hx = e->dig_hx; h->dig_hy = e->dig_hy; h->dig_hz = e->dig_hz;
    h->dig_hitting = e->dig_hitting; h->dig_delay = e->dig_delay;
    h->atk_prev = e->atk_prev; h->rc_delay = e->rc_delay;
    h->use_prev = e->use_prev;
    h->hurt_vel_reset = e->hurt_vel_reset;
    h->server_motion_x = e->server_motion_x;
    h->server_motion_z = e->server_motion_z;
    h->container = e->container;
    h->container_wx = e->container_wx; h->container_wy = e->container_wy;
    h->container_wz = e->container_wz;
    h->world_dirty = 0;
    h->hotbar_sel = e->pl.inv.current_item;
    for (i = 0; i < 37; ++i) {
        ICStack s = isr_get_stack(&e->pl.inv,
                                  i < 36 ? i : ISR_OFFHAND_SLOT);
        if (isr_is_empty(&s)) {
            h->inv[i][0] = h->inv[i][1] = h->inv[i][2] = 0;
        } else {
            h->inv[i][0] = s.item; h->inv[i][1] = s.count;
            h->inv[i][2] = s.meta;
        }
    }
    for (i = 0; i < CU_MAX_ITEMS && n < BLAZE_SNAP_MAX_ITEMS; ++i) {
        const CuItem *it = &e->items[i];
        if (!it->active) continue;
        items[n].x = it->x; items[n].y = it->y; items[n].z = it->z;
        items[n].mx = it->mx; items[n].my = it->my; items[n].mz = it->mz;
        items[n].item = it->item; items[n].count = it->count;
        items[n].meta = it->meta; items[n].age = it->age;
        items[n].pickup_delay = it->pickup_delay;
        items[n].lifespan = it->lifespan;
        items[n].on_ground = it->on_ground;
        ++n;
    }
    h->n_items = (unsigned)n;
    h->rx0 = e->rx0; h->ry0 = e->ry0; h->rz0 = e->rz0;
    h->rnx = e->rnx; h->rny = e->rny; h->rnz = e->rnz;
    return n;
}

/* Raw sim state for divergence bisecting (blaze_debug_state ABI): doubles
 * {posX+ox, posY, posZ+oz (world, EXACT), motionX/Y/Z, yaw, pitch, onGround,
 *  fall_distance, sprinting, sprint_toggle_timer, dig_progress, dig_hitting,
 *  dig_delay, health, food, exhaustion, server_motion_x, server_motion_z,
 *  tick} (21 values). Reads scalar fields only, safe on a host-side copy of
 * a device Blaze. */
MC_HD static inline int blaze_debug_fill(const Blaze *e, double *out) {
    int k = 0;
    out[k++] = e->pl.ent.posX + (double)e->ox;
    out[k++] = e->pl.ent.posY;
    out[k++] = e->pl.ent.posZ + (double)e->oz;
    out[k++] = e->pl.ent.motionX;
    out[k++] = e->pl.ent.motionY;
    out[k++] = e->pl.ent.motionZ;
    out[k++] = (double)e->pl.yaw;
    out[k++] = (double)e->pl.pitch;
    out[k++] = (double)e->pl.ent.onGround;
    out[k++] = (double)e->pl.fall_distance;
    out[k++] = (double)e->pl.sprinting;
    out[k++] = (double)e->pl.sprint_toggle_timer;
    out[k++] = (double)e->dig_progress;
    out[k++] = (double)e->dig_hitting;
    out[k++] = (double)e->dig_delay;
    out[k++] = (double)e->vit.health;
    out[k++] = (double)e->vit.foodLevel;
    out[k++] = (double)e->vit.exhaustion;
    out[k++] = e->server_motion_x;
    out[k++] = e->server_motion_z;
    out[k++] = (double)e->tick;
    return k;
}

/* =================== snapshot -> env state (rl_snapshot_load port) ======== */

/* Reset is split into a SCALAR phase (every per-env field; one thread per
 * env on CUDA) and a BULK phase (region copy + window fill + frame clear;
 * one thread per cell on CUDA - the single-threaded ~3.7 MB restore made a
 * masked k_reset cost >100 ms). blaze_reset_from_snapshot below runs both
 * serially and stays the single-source reference (CPU driver + any caller
 * that wants the whole thing).
 *
 * Bulk cell index space (cu_reset_bulk_count(env) per env; the env's scalar
 * phase must have set rnx/rny/rnz/rvol first):
 *   [0, rvol)               region cells -> cells + cam_cells
 *   [rvol, +9*chunk_vol)    physics window blocks, computed from cells_src
 *                           directly (identical values; keeps the two ranges
 *                           order-independent)
 *   last CU_NPIX            frame buffer clear (cam 0 / dep 255 / edg 0)
 * The scalar phase must run first (bulk reads rx0/ccx/dims/pointer fields). */
MC_HD static inline long cu_reset_bulk_count(const Blaze *env) {
    return env->rvol + (long)PSV_NCHUNKS * MC_CHUNK_VOL + CU_NPIX;
}

MC_HD static inline void blaze_reset_bulk(Blaze *env, const u16 *cells_src,
                                          long idx) {
    if (idx < env->rvol) {
        u16 s = cells_src[idx];
        env->cells[idx] = s;
        env->cam_cells[idx] = (u16)(s >> 4);
    } else if (idx < env->rvol + (long)PSV_NCHUNKS * MC_CHUNK_VOL) {
        long w = idx - env->rvol;
        int ci = (int)(w / MC_CHUNK_VOL), cell = (int)(w % MC_CHUNK_VOL);
        /* mc_idx layout: cell = (y*MC_CZ + z)*MC_CX + x */
        int lx = cell % MC_CX, lz = (cell / MC_CX) % MC_CZ;
        int y = cell / (MC_CX * MC_CZ);
        int dx = ci % PSV_DIM - PSV_R, dz = ci / PSV_DIM - PSV_R;
        int wx = (env->ccx + dx) * 16 + lx, wz = (env->ccz + dz) * 16 + lz;
        long ri = cu_region_idx(env, wx, y, wz);
        env->window[ci].blocks[cell] = ri < 0 ? 0 : cells_src[ri];
    } else {
        long pix = idx - env->rvol - (long)PSV_NCHUNKS * MC_CHUNK_VOL;
        env->cam[pix] = 0; env->dep[pix] = 255; env->edg[pix] = 0;
    }
}

/* Scalar phase: every per-env field. Mirrors rl_snapshot_load (rl_mode.c)
 * field-for-field; world_dirty is ignored (blaze has no scan cache - coal
 * membership is a pure function of world + player block). cells/cam_cells/
 * window/cam bufs must already point into the per-env pool; ore/nore is the
 * snapshot's static coal list. */
MC_HD static inline void blaze_reset_scalar(Blaze *env, const RlSnapHead *h,
                                            const RlSnapItem *items,
                                            const int *ore, int nore,
                                            const int *ore_xy,
                                            const int *cont, int ncont,
                                            int success_item) {
    int i, dx, dz;
    unsigned u;

    env->rx0 = h->rx0; env->ry0 = h->ry0; env->rz0 = h->rz0;
    env->rnx = h->rnx; env->rny = h->rny; env->rnz = h->rnz;
    env->rvol = (long)h->rnx * h->rny * h->rnz;
    env->ore = ore;
    env->nore = nore;
    env->ore_xy = ore_xy;
    env->n_cont = ncont;
    if (ncont > 0)
        for (i = 0; i < ncont * 3; ++i) env->cont[i] = cont[i];

    env->seed = h->seed;
    env->tick = h->tick;
    env->dead = 0;
    env->deaths = 0;

    env->ccx = psv_floordiv16(h->ox); env->ccz = psv_floordiv16(h->oz);
    env->ox = h->ox; env->oz = h->oz;

    psv_player_init(&env->pl);
    isr_init(&env->pl.inv);
    env->pl.ent.posX = h->px; env->pl.ent.posY = h->py; env->pl.ent.posZ = h->pz;
    env->pl.ent.box.minX = h->box[0]; env->pl.ent.box.minY = h->box[1];
    env->pl.ent.box.minZ = h->box[2]; env->pl.ent.box.maxX = h->box[3];
    env->pl.ent.box.maxY = h->box[4]; env->pl.ent.box.maxZ = h->box[5];
    env->pl.yaw = h->yaw; env->pl.pitch = h->pitch;
    env->pl.ent.motionX = h->mx; env->pl.ent.motionY = h->my;
    env->pl.ent.motionZ = h->mz;
    env->pl.ent.onGround = h->on_ground;
    env->pl.ent.collidedHorizontally = h->collided_h;
    env->pl.ent.collidedVertically = h->collided_v;
    env->pl.ent.isCollided = h->is_collided;
    env->pl.fall_distance = h->fall_distance;
    env->pl.sprinting = h->sprinting;
    env->pl.sprint_toggle_timer = h->sprint_toggle_timer;
    env->pl.jump_factor_sprint = h->jump_factor_sprint;
    env->pl.jump_ticks = h->jump_ticks;
    env->pl.prev_move_forward = h->prev_move_forward;
    env->pl.prev_sneak = h->prev_sneak;

    env->vit.health = h->health; env->vit.foodLevel = h->food;
    env->vit.saturation = h->saturation; env->vit.exhaustion = h->exhaustion;
    env->vit.foodTimer = h->food_timer;
    /* .bsnp predates effect-aware max-health state. Blaze snapshots are
     * quiescent/default-attribute by contract, so restore the 1.11.2 player
     * base instead of retaining calloc's zero (which makes the first
     * pv_set_health clamp lethal and forces expensive masked resets). */
    env->vit.maxHealth = PV_MAX_HEALTH;
    env->pl.health = h->health; env->pl.food = (float)h->food;

    env->dig_progress = h->dig_progress;
    env->dig_hx = h->dig_hx; env->dig_hy = h->dig_hy; env->dig_hz = h->dig_hz;
    env->dig_hitting = h->dig_hitting; env->dig_delay = h->dig_delay;
    env->atk_prev = h->atk_prev; env->rc_delay = h->rc_delay;
    env->use_prev = h->use_prev; env->hurt_vel_reset = h->hurt_vel_reset;
    env->eat_ticks = 0; env->eat_item = 0;   /* quiescent by bake contract */
    env->server_motion_x = h->server_motion_x;
    env->server_motion_z = h->server_motion_z;

    env->container = h->container;
    env->container_wx = h->container_wx; env->container_wy = h->container_wy;
    env->container_wz = h->container_wz;

    /* furnace state is NOT in .bsnp (bake contract: quiescent, no active
     * furnace) - the real env's --snapshot-in likewise restores with
     * active_furnace == -1 and every furnace slot empty. */
    for (i = 0; i < CU_MAX_FURNACES; ++i) {
        env->furnaces[i].active = 0;
        env->furnaces[i].wx = env->furnaces[i].wy = env->furnaces[i].wz = 0;
        cu_furnace_init(&env->furnaces[i]);
    }
    env->active_furnace = -1;

    env->pl.inv.current_item = h->hotbar_sel;
    for (i = 0; i < 37; ++i)
        isr_set_stack(&env->pl.inv, i < 36 ? i : ISR_OFFHAND_SLOT,
                      h->inv[i][1] == 0 ? ic_empty()
                                        : ic_mk(h->inv[i][0], h->inv[i][1],
                                                h->inv[i][2]));

    for (i = 0; i < CU_MAX_ITEMS; ++i) env->items[i].active = 0;
    env->n_items = 0;
    for (u = 0; u < h->n_items && u < CU_MAX_ITEMS; ++u) {
        CuItem *e = &env->items[u];
        e->active = 1;
        e->x = items[u].x; e->y = items[u].y; e->z = items[u].z;
        e->mx = items[u].mx; e->my = items[u].my; e->mz = items[u].mz;
        e->item = items[u].item; e->count = items[u].count; e->meta = items[u].meta;
        e->age = items[u].age; e->pickup_delay = items[u].pickup_delay;
        e->lifespan = items[u].lifespan; e->on_ground = items[u].on_ground;
        env->n_items++;
    }

    /* window chunk coords; the block contents come from the bulk phase */
    for (dz = -PSV_R; dz <= PSV_R; ++dz)
        for (dx = -PSV_R; dx <= PSV_R; ++dx) {
            Chunk *ch = &env->window[(dz + PSV_R) * PSV_DIM + (dx + PSV_R)];
            ch->cx = env->ccx + dx; ch->cz = env->ccz + dz;
        }

    env->base_coal = blaze_inv_count(env, 263);
    env->success_item = success_item;
    env->base_success =
        success_item > 0 ? blaze_inv_count(env, success_item) : 0;
    env->have_prev_dist = 0;
    env->prev_dist = 0.0;
    env->done = 0;
    env->dec_rew_pre = 0.0;
    env->dec_r_last = 0.0;
    env->dec_have_last = 0;
    env->dec_plus10 = 0;
    env->dec_attack = 0;
    env->dec_cam_fresh = 0;
    env->dec_have_nc = 0;
    env->dec_ry = env->dec_rp = env->dec_dist = 0.0;
    env->cand_valid = 0;
    env->n_cand = 0;
    env->cand_pwx = env->cand_pwy = env->cand_pwz = 0;
    env->world_epoch = 0;
    env->cand_epoch = 0;
}

/* Whole reset, serial (CPU driver / reference): scalar phase then every bulk
 * cell. Value-identical to the pre-split single function. */
MC_HD static inline void blaze_reset_from_snapshot(Blaze *env, const RlSnapHead *h,
                                                   const RlSnapItem *items,
                                                   const u16 *cells_src,
                                                   const int *ore, int nore,
                                                   const int *ore_xy,
                                                   const int *cont, int ncont,
                                                   int success_item) {
    long i, nbulk;
    blaze_reset_scalar(env, h, items, ore, nore, ore_xy, cont, ncont,
                       success_item);
    nbulk = cu_reset_bulk_count(env);
    for (i = 0; i < nbulk; ++i) blaze_reset_bulk(env, cells_src, i);
}

#ifdef __cplusplus
}
#endif
#endif /* BLAZE_CORE_H */
