/* blaze_cuda.cu - CUDA driver over blaze_core.h, exporting the SAME C ABI as
 * blaze_cpu.c so Python picks a .so at load time. One env per thread for the
 * tick, one thread per pixel for the camera (DESIGN Part 2.3):
 *   k_reset_scalar/_bulk : snapshot restore from the device-resident cache
 *             (host-compacted env list; bulk = 1 thread/cell)
 *   k_tick  : blaze_decision_begin + `repeat` blaze_decision_subtick per env
 *             thread (dyaw/dpitch on sub-tick 0 only; craft/interact
 *             pre-tick on sub-tick 0), full 12-double raw action rows;
 *             physics-window recenter refills run WARP-COOPERATIVELY (see
 *             the kernel comment; BLAZE_LEGACY_RECENTER=1 selects the old
 *             serial-recenter kernel at create for A/B)
 *   k_obs   : blaze_render_cam_pixel for envs whose decision frame is fresh,
 *             then copies the persisted frame into the caller's tensors
 *   k_final : blaze_decision_finalize - deferred crosshair/+10 reward terms
 *             (read the k_obs frame's center pixel), 6 scalars, done, pose
 * Envs are fully independent - no shared device state beyond the read-only
 * sin table, recipe table and snapshot cache.
 *
 * Action ABI (mirrors blaze_cpu.c): blaze_step takes double actions[n][12] =
 * the FULL raw action vector in blaze_tick_raw order {forward,strafe,dyaw,
 * dpitch,jump,sneak,sprint,attack,use,hotbar(-1),craft(-1),interact}.
 * craft/interact are pre-tick primitives, applied once before sub-tick 0 in
 * the SAME thread and order as the CPU driver. Legacy 5-head trainer actions
 * are expanded to this layout in blaze.py (bit-identical decode).
 *
 * Pointer convention: blaze_step's actions and all outputs are DEVICE
 * pointers on the .so's device (torch cuda tensors' .data_ptr()); everything
 * else (paths, snap_idx, reset mask, verify-helper buffers) is host memory.
 *
 * Region dims are DYNAMIC (snapshot header, e.g. fresh-spawn t0 snapshots
 * are 128x128x128): the region-sized pools (cells/cam_cells, n * rvol each)
 * are allocated ONCE at the FIRST blaze_load_snapshots (dims unknown at
 * create; every loaded snapshot must share them - the loader enforces it).
 *
 * Allocation rule (blaze discipline): every device allocation happens in
 * blaze_create/blaze_load_snapshots; kernels only mutate bytes. The per-env
 * Chunk[9] window and McAABB[512] scratch live in global pools indexed by
 * env id - NEVER on the device stack. t0 (128^3) snapshots cost ~9.3 MB/env
 * (cells 4M + cam_cells 4M + window 1.2M + scratch); N=8192 ~ 76 GB.
 * blaze_create/blaze_load_snapshots fail gracefully (NULL / -1) with a GB
 * estimate if any cudaMalloc fails.
 *
 * Build: nvcc -O2 --fmad=false (blaze determinism flags; NEVER fast-math),
 * Makefile target blaze_cuda_so. Optional kernel timing: set BLAZE_KTIME=1
 * before create; per-kernel totals print to stderr at destroy. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cuda_runtime.h>

#include "blaze_core.h"

#define BLAZE_MAX_SNAPS 128
#define BLAZE_ACT_HEADS 13
#define CU_TPB 128
/* k_tick is latency-bound (1 env/thread, big serial per-thread chains) and
 * small-grid: at N=4096 TPB=128 makes only 32 blocks, parking everything on
 * 32 of the GPU's SMs. TPB=32 spreads the same warps across 4x the SMs
 * (more L1/registers per thread, no scheduling downside at these sizes). */
#ifndef CU_TICK_TPB
#define CU_TICK_TPB 32
#endif
/* k_tick's cooperative recenter uses full-mask warp collectives: every warp
 * must be fully populated (no thread ever returns early inside k_tick). */
static_assert(CU_TICK_TPB % 32 == 0, "k_tick needs full warps");

/* device-resident snapshot cache entry (head/items by value, region/coal as
 * device pointers) */
typedef struct {
    RlSnapHead head;
    RlSnapItem items[BLAZE_SNAP_MAX_ITEMS];
    const u16 *cells;            /* device, head.rnx*rny*rnz packed states */
    const int *coal;             /* device, ncoal x 3 */
    int ncoal;
    const int *xy_off;           /* device, rnx*rny+1 CSR (ix,iy)->coal-range
                                  * offsets (blaze_build_ore_xy); NULL =
                                  * full-scan candidate rebuild */
    const int *cont;             /* device, ncont x 3 container cells (58/61/
                                  * 62); interact-candidate seed */
    int ncont;                   /* -1 = overflow: full window scan fallback */
} CuSnapDev;

typedef struct {
    int n, device;
    cudaStream_t stream;
    Blaze *d_envs;
    Blaze *h_envs;               /* host staging mirror (pool pointers) */
    McSinTable *d_st;
    int *d_assign;
    int *h_assign;
    int *d_active;               /* compacted resetting-env index list */
    int *h_active;
    /* pooled per-env buffers. Region-sized pools (d_cells/d_camcells) are
     * allocated lazily at the FIRST snapshot load - dims come from the
     * snapshot header. Still init-time-only: nothing allocates in a tick
     * path. */
    int rnx, rny, rnz;           /* 0 until the first snapshot is loaded */
    long rvol;
    u16 *d_cells, *d_camcells, *d_cam;
    u8 *d_dep, *d_edg;
    Chunk *d_window;
    CuCand *d_cand;
    int *d_cont;                 /* per-env BLAZE_SNAP_MAX_CONT container cells */
    McAABB *d_aabb;
    CRRecipe *d_recipes;         /* crf_build once at create, uploaded once */
    int nrecipes;
    /* snapshot cache */
    CuSnapDev h_snaps[BLAZE_MAX_SNAPS];   /* mirrors d_snaps; .cells/.coal are
                                           * device pointers */
    CuSnapDev *d_snaps;
    int nsnaps;
    int has_liquid[BLAZE_MAX_SNAPS];
    /* verify-helper scratch */
    CuBinObs *d_obs;
    double atk_gate;  /* opt-in +0.03 gate; 0 = off (exact ppo_coal) */
    int success_item; /* +10/done=1 item id; 263 default (exact ppo_coal),
                       * 0 = never. Applied to envs at their next reset. */
    int legacy_recenter;  /* BLAZE_LEGACY_RECENTER=1 at create: A/B fallback
                           * to the serial-recenter k_tick_legacy (host-side
                           * launch pick; zero tick cost) */
    int warp_tick;        /* BLAZE_WARP_TICK (default 1): one env per WARP -
                           * 32x resident warps for the latency-bound serial
                           * chains + warp-parallel coal sweep. 0 = flat
                           * one-env-per-thread k_tick. */
    /* optional kernel timing (BLAZE_KTIME=1) */
    int ktime;
    cudaEvent_t ev[4];
    double ms_tick, ms_obs, ms_final;
    long nsteps;
    /* optional k_tick stage cycle counters (BLAZE_STAGE_TIME=1):
     * [0] decision_begin  [1] recenter (pose+coop fill)  [2] decision_subtick
     * sum of per-thread clock64 deltas; relative share of work, not wall. */
    int stage_time;
    unsigned long long *d_stage_cycles; /* device, 3 counters */
    unsigned long long h_stage_cycles[3];
    /* optional op-trace activity counters (BLAZE_OP_TRACE=1): device pool of
     * n * CU_OP_N u64s, sliced into every env's ->ops at create. */
    int op_trace;
    unsigned long long *d_ops;
} CuVecCu;

extern "C" {
void *blaze_create(int device, int n);
void blaze_destroy(void *vh);
int blaze_load_snapshots(void *vh, const char *const *paths, int count,
                         char *err, int err_cap);
int blaze_snapshot_has_liquid(void *vh, int snap);
int blaze_assign(void *vh, const int *snap_idx);
int blaze_set_reward_gate(void *vh, double dist_gate);
int blaze_set_success_item(void *vh, int item);
int blaze_reset(void *vh, const unsigned char *mask);
int blaze_step(void *vh, const double *actions, int repeat,
               unsigned short *cam, unsigned char *depth, unsigned char *edge,
               float *scal, float *rew, unsigned char *done, float *pose);
int blaze_step_full(void *vh, const double *actions, int repeat,
                    unsigned short *cam, unsigned char *depth,
                    unsigned char *edge, float *scal, float *rew,
                    unsigned char *done, float *pose, int *status);
int blaze_capture(void *vh, int env, int slot);
int blaze_obs_size(void);
int blaze_emit(void *vh, int env, int want_cam, void *out);
int blaze_tick_raw(void *vh, int env, const double a[13], int want_cam,
                   void *out);
int blaze_debug_state(void *vh, int env, double *out, int cap);
int blaze_op_count(void);
int blaze_op_trace(void *vh, unsigned long long *out);
}

/* =================== kernels =================== */

/* reset phase 1: one thread per RESETTING env (host-compacted active list) */
__global__ void k_reset_scalar(Blaze *envs, const int *active, int nactive,
                               const CuSnapDev *snaps, const int *assign,
                               int success_item) {
    int gi = blockIdx.x * blockDim.x + threadIdx.x;
    if (gi >= nactive) return;
    int i = active[gi];
    const CuSnapDev *s = &snaps[assign[i]];
    blaze_reset_scalar(&envs[i], &s->head, s->items, s->coal, s->ncoal,
                       s->xy_off, s->cont, s->ncont, success_item);
}

/* reset phase 2: one thread per bulk cell (region copy + window fill +
 * frame clear) - the single-threaded multi-MB restore made masked resets
 * cost >100 ms. bulk = cu_reset_bulk_count for the (shared) region dims,
 * computed host-side after the scalar phase set the env dims. */
__global__ void k_reset_bulk(Blaze *envs, const int *active,
                             long long nactive, long long bulk,
                             const CuSnapDev *snaps, const int *assign) {
    long long gi = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (gi >= nactive * bulk) return;
    int i = active[gi / bulk];
    blaze_reset_bulk(&envs[i], snaps[assign[i]].cells, (long)(gi % bulk));
}

/* Cooperative decision kernel: one env per thread for the tick logic, but
 * physics-window recenter refills run WARP-COOPERATIVELY. The serial
 * per-thread refill (3 chunk gathers + 6 flat copies, ~150k memory ops) was
 * k_tick's dominant cost on open-surface snapshots: one crossing lane
 * stalled its whole warp for the full serial chain (~1 crossing per warp per
 * sub-tick at t0 walk rates). Here every lane of the warp strides over the
 * crossing env's copy/fill cells instead (coalesced, ~32x less warp-stall).
 *
 * Uniform-control-flow contract: NO thread returns early (tail lanes with
 * i >= n and done envs stay in the rep loop as helpers), so the full-mask
 * __ballot_sync/__shfl_sync/__syncwarp collectives are always valid. The
 * refill inputs (env index, post-pose ccx/ccz, shift) are broadcast by
 * value; __syncwarp() orders the owner lane's pose/window writes against
 * the helpers' reads. State evolution is bit-identical to the serial
 * blaze_decision_ticks: same recenter sequence point, same fill values
 * (window bytes are a pure function of region + chunk coords). */
/* Opaque clock with memory clobber so stage timers cannot reorder past work. */
__device__ __forceinline__ unsigned long long cu_clk64(void) {
    unsigned long long t;
    asm volatile("mov.u64 %0, %%clock64;" : "=l"(t) :: "memory");
    return t;
}

__global__ void k_tick(Blaze *envs, int n, const McSinTable *st,
                       const double *actions, int repeat, McAABB *aabb_pool,
                       const CRRecipe *recipes, int nrecipes,
                       double atk_gate, unsigned long long *stage_cycles) {
    const unsigned FULL = 0xffffffffu;
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    int lane = (int)(threadIdx.x & 31u);
    int valid = i < n;
    Blaze *e = &envs[valid ? i : 0];
    int exec = 0;
    unsigned long long t0 = 0, t1 = 0;
    unsigned long long c_begin = 0, c_re = 0, c_sub = 0;
    if (stage_cycles) t0 = cu_clk64();
    if (valid)
        exec = blaze_decision_begin(e, st,
                                    actions + (size_t)i * BLAZE_ACT_HEADS,
                                    recipes, nrecipes);
    if (stage_cycles) {
        t1 = cu_clk64();
        c_begin = t1 - t0;
        t0 = t1;
    }
    for (int rep = 0; rep < repeat; ++rep) {
        int dcx = 0, dcz = 0, ccx = 0, ccz = 0;
        int need = 0;
        if (exec && !e->dead && cu_recenter_pose(e, &dcx, &dcz)) {
            need = 1;
            ccx = e->ccx;
            ccz = e->ccz;
        }
        unsigned m = __ballot_sync(FULL, need);
        if (m) {
            __syncwarp();            /* owner pose writes -> helper reads */
            do {
                int src = __ffs((int)m) - 1;
                m &= m - 1;
                int ei = __shfl_sync(FULL, i, src);
                int tccx = __shfl_sync(FULL, ccx, src);
                int tccz = __shfl_sync(FULL, ccz, src);
                int tdcx = __shfl_sync(FULL, dcx, src);
                int tdcz = __shfl_sync(FULL, dcz, src);
                cu_recenter_fill(&envs[ei], tccx, tccz, tdcx, tdcz, lane, 32);
            } while (m);
            __syncwarp();            /* helper fill writes -> owner reads */
        }
        if (stage_cycles) {
            t1 = cu_clk64();
            c_re += t1 - t0;
            t0 = t1;
        }
        if (exec) {
            blaze_decision_subtick(e, st,
                                   actions + (size_t)i * BLAZE_ACT_HEADS,
                                   rep, repeat,
                                   aabb_pool + (size_t)i * PSV_MAX_BLOCKS,
                                   0, atk_gate);
            if (e->done) exec = 0;
        }
        if (stage_cycles) {
            t1 = cu_clk64();
            c_sub += t1 - t0;
            t0 = t1;
        }
    }
    if (stage_cycles) {
        atomicAdd((unsigned long long *)&stage_cycles[0], c_begin);
        atomicAdd((unsigned long long *)&stage_cycles[1], c_re);
        atomicAdd((unsigned long long *)&stage_cycles[2], c_sub);
    }
}

/* ---- warp-per-env tick (BLAZE_WARP_TICK, default ON) ----
 *
 * k_tick runs ONE thread per env: at N=8192 that is 256 warps over ~188 SMs
 * (~1.4/SM) and the whole kernel is dependent-instruction latency (measured:
 * neither FP64 cuts nor coalescing moved the 15 ms). Here each env owns a
 * FULL WARP (8192 resident warps): lane 0 runs the serial physics/reward
 * statements unchanged, the recenter refill reuses the existing cooperative
 * fill over the env's own warp, and the per-sub-tick coal pass fans the
 * candidate sweep across all 32 lanes.
 *
 * Warp-parallel coal selection is BIT-EXACT to the serial sweep because the
 * kept top-32 is a pure function of the eligible set: the order (d2, x, y,
 * z) is a strict total order (block coords unique), every candidate's d2 is
 * computed independently with the same expression, and when the total accept
 * count fits the 512 scratch cap the eligible set is exactly "all non-mined
 * cached candidates" regardless of scan order. The two order-dependent cases
 * fall back to the serial lane-0 sweep: candidate-cache overflow (n_cand<0)
 * and accept-count > CU_COAL_SCRATCH (cap truncation depends on scan order).
 * Nearest-coal takes the argmin by (d, list rank) - identical to the serial
 * first-strictly-lower scan - and only the winning lane evaluates the
 * atan2/asin path, matching the serial code's last-improvement values. */
__device__ __forceinline__ int cu_k2(const Blaze *e, int wx, int wy, int wz) {
    /* region-local pack, monotonic in world (x,y,z) lex order */
    return ((wx - e->rx0) << 16) | ((wy - e->ry0) << 8) | (wz - e->rz0);
}

__device__ void cu_coal_warp(Blaze *env, int lane,
                             int *have_out, double *ry_out, double *rp_out,
                             double *dist_out) {
    const unsigned FULL = 0xffffffffu;
    float fx = (float)(env->pl.ent.posX + (double)env->ox);
    float fy = (float)(env->pl.ent.posY);
    float fz = (float)(env->pl.ent.posZ + (double)env->oz);
    int pwx = (int)floor((double)fx);
    int pwy = (int)floor((double)fy);
    int pwz = (int)floor((double)fz);
    int y0 = pwy - CU_Y_DOWN, y1 = pwy + CU_Y_UP;
    int ncand, c, r, k, total, myacc = 0, nloc = 0, head = 0, fell = 0;
    struct { double d2; int k2, x, y, z; } loc[(CU_COAL_CAND + 31) / 32], own;
    if (y0 < 0)   y0 = 0;
    if (y1 > 255) y1 = 255;
    if (lane == 0) {
        CU_OP(env, CU_OP_COAL_CALL);
        blaze_coal_cache_sync(env, pwx, pwy, pwz, y0, y1);
    }
    __syncwarp();
    ncand = env->n_cand;
    own.d2 = 0.0; own.x = own.y = own.z = 0;
    if (ncand >= 0) {
        for (c = lane; c < ncand; c += 32) {
            CuCand cd = env->coal_cand[c];
            double ddx, ddy, ddz, d2;
            int k2, j;
            if (cd.ri & CU_CAND_MINED) continue;
            ++myacc;
            ddx = cd.x + 0.5 - (double)fx;
            ddy = cd.y + 0.5 - (double)fy;
            ddz = cd.z + 0.5 - (double)fz;
            d2 = ddx * ddx + ddy * ddy + ddz * ddz;
            k2 = cu_k2(env, cd.x, cd.y, cd.z);
            j = nloc - 1;
            for (; j >= 0 && (d2 < loc[j].d2 ||
                              (d2 == loc[j].d2 && k2 < loc[j].k2)); --j)
                loc[j + 1] = loc[j];
            loc[j + 1].d2 = d2; loc[j + 1].k2 = k2;
            loc[j + 1].x = cd.x; loc[j + 1].y = cd.y; loc[j + 1].z = cd.z;
            ++nloc;
        }
        total = myacc;
        for (c = 16; c; c >>= 1)
            total += __shfl_down_sync(FULL, total, c);
        total = __shfl_sync(FULL, total, 0);
        if (total > CU_COAL_SCRATCH)
            fell = 1;             /* cap truncation is scan-order dependent */
        else if (lane == 0)
            CU_OP_ADD(env, CU_OP_COAL_SWEEP, ncand);
    } else {
        fell = 1;                 /* candidate-cache overflow */
        total = 0;
    }
    if (fell) {
        int have = 0;
        double ry = 0.0, rp = 0.0, dist = 0.0;
        if (lane == 0) {
            int coal_now[CU_NCOAL][3];
            /* serial sweep over the already-synced cache (no second
             * CU_OP_COAL_CALL: blaze_coal_list would re-count it) */
            (void)blaze_coal_sweep(env, fx, fy, fz, pwx, pwz, y0, y1,
                                   coal_now);
            have = blaze_nearest_coal(coal_now, (double)fx, (double)fy,
                                      (double)fz, (double)env->pl.yaw,
                                      (double)env->pl.pitch, &ry, &rp, &dist);
        }
        *have_out = __shfl_sync(FULL, have, 0);
        *ry_out = __shfl_sync(FULL, ry, 0);
        *rp_out = __shfl_sync(FULL, rp, 0);
        *dist_out = __shfl_sync(FULL, dist, 0);
        return;
    }
    k = total < CU_NCOAL ? total : CU_NCOAL;
    for (r = 0; r < k; ++r) {
        double hd2 = head < nloc ? loc[head].d2 : 1e300;
        int hk2 = head < nloc ? loc[head].k2 : 0x7fffffff;
        double bd2 = hd2;
        int bk2 = hk2, bl = lane, win;
        for (c = 16; c; c >>= 1) {
            double od2 = __shfl_down_sync(FULL, bd2, c);
            int ok2 = __shfl_down_sync(FULL, bk2, c);
            int ol = __shfl_down_sync(FULL, bl, c);
            if (od2 < bd2 || (od2 == bd2 && ok2 < bk2)) {
                bd2 = od2; bk2 = ok2; bl = ol;
            }
        }
        win = __shfl_sync(FULL, bl, 0);
        {
            int wx = __shfl_sync(FULL, head < nloc ? loc[head].x : 0, win);
            int wy = __shfl_sync(FULL, head < nloc ? loc[head].y : 0, win);
            int wz = __shfl_sync(FULL, head < nloc ? loc[head].z : 0, win);
            if (lane == r) { own.x = wx; own.y = wy; own.z = wz; }
        }
        if (lane == win) ++head;
    }
    {   /* nearest: argmin by (d, rank), winner evaluates the angles */
        double ex = (double)fx, ey = (double)fy + 1.62, ez = (double)fz;
        double dx = 0.0, dy = 0.0, dz = 0.0, d = 1e300;
        double ry = 0.0, rp = 0.0;
        int bl = lane, win;
        if (lane < k) {
            dx = own.x + 0.5 - ex;
            dy = own.y + 0.5 - ey;
            dz = own.z + 0.5 - ez;
            d = sqrt(dx * dx + dy * dy + dz * dz);
        }
        double bd = d;
        for (c = 16; c; c >>= 1) {
            double od = __shfl_down_sync(FULL, bd, c);
            int ol = __shfl_down_sync(FULL, bl, c);
            if (od < bd || (od == bd && ol < bl)) { bd = od; bl = ol; }
        }
        win = __shfl_sync(FULL, bl, 0);
        if (lane == win && k > 0) {
            double dd = d > 1e-9 ? d : 1e-9;
            ry = blaze_wrap180(atan2(-dx, dz) * (180.0 / CU_DEC_PI) -
                               (double)env->pl.yaw);
            rp = -asin(dy / dd) * (180.0 / CU_DEC_PI) -
                 (double)env->pl.pitch;
        }
        *have_out = k > 0;
        *ry_out = __shfl_sync(FULL, ry, win);
        *rp_out = __shfl_sync(FULL, rp, win);
        *dist_out = __shfl_sync(FULL, k > 0 ? bd : 0.0, 0);
    }
}

__global__ void k_tick_warp(Blaze *envs, int n, const McSinTable *st,
                            const double *actions, int repeat,
                            McAABB *aabb_pool, const CRRecipe *recipes,
                            int nrecipes, double atk_gate,
                            unsigned long long *stage_cycles) {
    const unsigned FULL = 0xffffffffu;
    int w = (int)((blockIdx.x * (unsigned)blockDim.x + threadIdx.x) >> 5);
    int lane = (int)(threadIdx.x & 31u);
    if (w >= n) return;                    /* whole tail warp exits together */
    Blaze *e = &envs[w];
    const double *a = actions + (size_t)w * BLAZE_ACT_HEADS;
    int exec = 0;
    unsigned long long t0 = 0, t1 = 0;
    unsigned long long c_begin = 0, c_re = 0, c_sub = 0;
    if (stage_cycles && lane == 0) t0 = cu_clk64();
    if (lane == 0)
        exec = blaze_decision_begin(e, st, a, recipes, nrecipes);
    exec = __shfl_sync(FULL, exec, 0);
    if (stage_cycles && lane == 0) {
        t1 = cu_clk64();
        c_begin = t1 - t0;
        t0 = t1;
    }
    for (int rep = 0; rep < repeat; ++rep) {
        int need = 0, ccx = 0, ccz = 0, dcx = 0, dcz = 0;
        if (lane == 0 && exec && !e->dead &&
            cu_recenter_pose(e, &dcx, &dcz)) {
            need = 1;
            ccx = e->ccx;
            ccz = e->ccz;
        }
        need = __shfl_sync(FULL, need, 0);
        if (need) {
            ccx = __shfl_sync(FULL, ccx, 0);
            ccz = __shfl_sync(FULL, ccz, 0);
            dcx = __shfl_sync(FULL, dcx, 0);
            dcz = __shfl_sync(FULL, dcz, 0);
            __syncwarp();        /* owner pose writes -> helper reads */
            cu_recenter_fill(e, ccx, ccz, dcx, dcz, lane, 32);
            __syncwarp();        /* helper fill writes -> owner reads */
        }
        if (stage_cycles && lane == 0) {
            t1 = cu_clk64();
            c_re += t1 - t0;
            t0 = t1;
        }
        if (exec) {
            double fx = 0.0, fy = 0.0, fz = 0.0;
            double ry = 0.0, rp = 0.0, dist = 0.0;
            int have_nc = 0;
            if (lane == 0)
                blaze_subtick_phys(e, st, a, rep, repeat,
                                   aabb_pool + (size_t)w * PSV_MAX_BLOCKS,
                                   0, &fx, &fy, &fz);
            __syncwarp();        /* lane-0 world/pose writes -> lane reads */
            cu_coal_warp(e, lane, &have_nc, &ry, &rp, &dist);
            if (lane == 0) {
                blaze_subtick_post(e, rep, repeat, atk_gate, have_nc,
                                   ry, rp, dist);
                if (e->done) exec = 0;
            }
            exec = __shfl_sync(FULL, exec, 0);
        }
        if (stage_cycles && lane == 0) {
            t1 = cu_clk64();
            c_sub += t1 - t0;
            t0 = t1;
        }
    }
    if (stage_cycles && lane == 0) {
        atomicAdd((unsigned long long *)&stage_cycles[0], c_begin);
        atomicAdd((unsigned long long *)&stage_cycles[1], c_re);
        atomicAdd((unsigned long long *)&stage_cycles[2], c_sub);
    }
}

/* Pre-cooperative kernel, selectable at CREATE time (BLAZE_LEGACY_RECENTER=1)
 * for A/B benching; zero cost on the default path (host-side launch pick). */
__global__ void k_tick_legacy(Blaze *envs, int n, const McSinTable *st,
                              const double *actions, int repeat,
                              McAABB *aabb_pool, const CRRecipe *recipes,
                              int nrecipes, double atk_gate) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    blaze_decision_ticks(&envs[i], st,
                         actions + (size_t)i * BLAZE_ACT_HEADS, repeat,
                         aabb_pool + (size_t)i * PSV_MAX_BLOCKS, 0, atk_gate,
                         recipes, nrecipes);
}

__global__ void k_obs(Blaze *envs, int n, const McSinTable *st,
                      unsigned short *cam, unsigned char *depth,
                      unsigned char *edge) {
    int gi = blockIdx.x * blockDim.x + threadIdx.x;
    int i = gi / CU_NPIX, pix = gi % CU_NPIX;
    if (i >= n) return;
    Blaze *e = &envs[i];
    if (e->dec_cam_fresh)
        blaze_render_cam_pixel(e, st, pix);
    if (cam)   cam[(size_t)i * CU_NPIX + pix] = e->cam[pix];
    if (depth) depth[(size_t)i * CU_NPIX + pix] = e->dep[pix];
    if (edge)  edge[(size_t)i * CU_NPIX + pix] = e->edg[pix];
}

__global__ void k_final(Blaze *envs, int n, const McSinTable *st,
                        float *scal, float *rew,
                        unsigned char *done, float *pose, double atk_gate,
                        int *status) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    blaze_decision_finalize(&envs[i], st,
                            scal ? scal + (size_t)i * 6 : NULL,
                            rew ? rew + i : NULL,
                            done ? done + i : NULL,
                            pose ? pose + (size_t)i * 5 : NULL,
                            atk_gate);
    if (status) blaze_fill_status(&envs[i], status + (size_t)i * CU_STATUS_K);
}

/* Verify-helper camera path.  The regular batched path already assigns one
 * thread per pixel in k_obs; do the same here instead of making k_emit's one
 * record thread raycast the whole frame serially. */
__global__ void k_emit_cam(Blaze *envs, int env, const McSinTable *st) {
    int pix = blockIdx.x * blockDim.x + threadIdx.x;
    if (pix >= CU_NPIX) return;
    blaze_render_cam_pixel(&envs[env], st, pix);
}

/* Record assembly stays single-threaded and runs after k_emit_cam in the
 * same stream.  want_cam=0 copies the frame that was just produced (or the
 * persisted prior frame for the protocol's cam:0 case). */
__global__ void k_emit(Blaze *envs, int env, const McSinTable *st,
                       CuBinObs *out) {
    if (threadIdx.x || blockIdx.x) return;
    blaze_emit_bolr(&envs[env], st, out, 0);
}

typedef struct { double a[13]; } CuRawAct;

/* raw tick for env `env`, or for ALL n envs when env == -1 (one thread per
 * env; the chain gate's 64-identical-lanes stepper). Mirrors the CPU
 * driver's blaze_tick_raw: craft, then interact, then smelt, then the tick
 * - same thread, same order. */
__global__ void k_tick_raw(Blaze *envs, int env, int n, const McSinTable *st,
                           CuRawAct ra, int want_cam, CuBinObs *out,
                           McAABB *aabb_pool, const CRRecipe *recipes,
                           int nrecipes) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (env >= 0) {
        if (i) return;
        i = env;
    } else if (i >= n) {
        return;
    }
    CuAction act;
    memset(&act, 0, sizeof act);
    act.forward = (float)ra.a[0];
    act.strafe = (float)ra.a[1];
    act.dyaw = (float)ra.a[2];
    act.dpitch = (float)ra.a[3];
    act.jump = (int)ra.a[4];
    act.sneak = (int)ra.a[5];
    act.sprint = (int)ra.a[6];
    act.attack = (int)ra.a[7];
    act.use = (int)ra.a[8];
    act.hotbar_sel = (int)ra.a[9];
    if ((int)ra.a[10] >= 0)
        (void)blaze_do_craft(&envs[i], (int)ra.a[10], recipes, nrecipes);
    if ((int)ra.a[11])
        (void)blaze_do_interact(&envs[i]);
    if ((int)ra.a[12])
        (void)blaze_do_smelt(&envs[i]);
    blaze_runtime_tick(&envs[i], st, act,
                       aabb_pool + (size_t)i * PSV_MAX_BLOCKS);
    if (out && env >= 0)
        blaze_emit_bolr(&envs[i], st, out, want_cam);
}

/* =================== host driver =================== */

static int cu_ck(cudaError_t e, const char *what) {
    if (e == cudaSuccess) return 0;
    fprintf(stderr, "blaze_cuda: %s: %s\n", what, cudaGetErrorString(e));
    return -1;
}

void *blaze_create(int device, int n) {
    CuVecCu *v;
    int i;
    if (n <= 0) return NULL;
    if (cu_ck(cudaSetDevice(device), "cudaSetDevice")) return NULL;
    v = (CuVecCu *)calloc(1, sizeof *v);
    if (!v) return NULL;
    v->n = n;
    v->device = device;
    v->success_item = 263;
    v->ktime = getenv("BLAZE_KTIME") && atoi(getenv("BLAZE_KTIME"));
    v->stage_time = getenv("BLAZE_STAGE_TIME") &&
                    atoi(getenv("BLAZE_STAGE_TIME"));
    v->legacy_recenter = getenv("BLAZE_LEGACY_RECENTER") &&
                         atoi(getenv("BLAZE_LEGACY_RECENTER"));
    v->warp_tick = getenv("BLAZE_WARP_TICK")
                       ? atoi(getenv("BLAZE_WARP_TICK")) : 1;
    v->op_trace = getenv("BLAZE_OP_TRACE") && atoi(getenv("BLAZE_OP_TRACE"));
    v->h_assign = (int *)calloc((size_t)n, sizeof *v->h_assign);
    v->h_active = (int *)calloc((size_t)n, sizeof *v->h_active);
    v->h_envs = (Blaze *)calloc((size_t)n, sizeof *v->h_envs);
    if (!v->h_assign || !v->h_active || !v->h_envs) {
        free(v->h_assign); free(v->h_active); free(v->h_envs); free(v);
        return NULL;
    }
    for (i = 0; i < n; ++i) v->h_assign[i] = -1;

    if (cudaStreamCreate(&v->stream) != cudaSuccess ||
        cudaMalloc(&v->d_envs, (size_t)n * sizeof(Blaze)) != cudaSuccess ||
        cudaMalloc(&v->d_st, sizeof(McSinTable)) != cudaSuccess ||
        cudaMalloc(&v->d_assign, (size_t)n * sizeof(int)) != cudaSuccess ||
        cudaMalloc(&v->d_active, (size_t)n * sizeof(int)) != cudaSuccess ||
        cudaMalloc(&v->d_cam,
                   (size_t)n * CU_NPIX * sizeof(u16)) != cudaSuccess ||
        cudaMalloc(&v->d_dep, (size_t)n * CU_NPIX) != cudaSuccess ||
        cudaMalloc(&v->d_edg, (size_t)n * CU_NPIX) != cudaSuccess ||
        cudaMalloc(&v->d_window,
                   (size_t)n * PSV_NCHUNKS * sizeof(Chunk)) != cudaSuccess ||
        cudaMalloc(&v->d_cand,
                   (size_t)n * CU_COAL_CAND * sizeof(CuCand)) != cudaSuccess ||
        cudaMalloc(&v->d_cont,
                   (size_t)n * BLAZE_SNAP_MAX_CONT * 3 *
                       sizeof(int)) != cudaSuccess ||
        cudaMalloc(&v->d_aabb,
                   (size_t)n * PSV_MAX_BLOCKS * sizeof(McAABB)) != cudaSuccess ||
        cudaMalloc(&v->d_recipes,
                   (size_t)CRF_NRECIPES * sizeof(CRRecipe)) != cudaSuccess ||
        cudaMalloc(&v->d_snaps,
                   (size_t)BLAZE_MAX_SNAPS * sizeof(CuSnapDev)) != cudaSuccess ||
        cudaMalloc(&v->d_obs, sizeof(CuBinObs)) != cudaSuccess) {
        fprintf(stderr, "blaze_cuda: cudaMalloc failed for n=%d fixed pools "
                        "(~%.1f GB; region pools come later at snapshot "
                        "load)\n",
                n, (double)n * ((double)PSV_NCHUNKS * sizeof(Chunk) +
                                PSV_MAX_BLOCKS * sizeof(McAABB) +
                                CU_COAL_CAND * sizeof(CuCand) +
                                sizeof(Blaze)) / 1e9);
        blaze_destroy(v);
        return NULL;
    }

    {   /* upload the LUT sin table + the crf recipe table (built once) */
        McSinTable *h_st = (McSinTable *)malloc(sizeof *h_st);
        CRRecipe *h_rec =
            (CRRecipe *)malloc((size_t)CRF_NRECIPES * sizeof *h_rec);
        if (!h_st || !h_rec) {
            free(h_st); free(h_rec); blaze_destroy(v);
            return NULL;
        }
        mc_sin_table_init(h_st);
        v->nrecipes = crf_build(h_rec);
        if (cu_ck(cudaMemcpy(v->d_st, h_st, sizeof *h_st,
                             cudaMemcpyHostToDevice), "st upload") ||
            cu_ck(cudaMemcpy(v->d_recipes, h_rec,
                             (size_t)v->nrecipes * sizeof *h_rec,
                             cudaMemcpyHostToDevice), "recipes upload")) {
            free(h_st); free(h_rec); blaze_destroy(v);
            return NULL;
        }
        free(h_st);
        free(h_rec);
    }

    if (v->op_trace) {
        size_t nb = (size_t)n * CU_OP_N * sizeof(unsigned long long);
        if (cudaMalloc(&v->d_ops, nb) != cudaSuccess) {
            fprintf(stderr, "blaze_cuda: op-trace counter alloc failed\n");
            blaze_destroy(v);
            return NULL;
        }
        cudaMemset(v->d_ops, 0, nb);
    }

    /* stage env structs host-side with the create-time pool pointers;
     * cells/cam_cells stay NULL until the first snapshot load sizes the
     * region pools. Uploaded (again) there. */
    for (i = 0; i < n; ++i) {
        Blaze *e = &v->h_envs[i];
        e->cam = v->d_cam + (size_t)i * CU_NPIX;
        e->dep = v->d_dep + (size_t)i * CU_NPIX;
        e->edg = v->d_edg + (size_t)i * CU_NPIX;
        e->window = v->d_window + (size_t)i * PSV_NCHUNKS;
        e->coal_cand = v->d_cand + (size_t)i * CU_COAL_CAND;
        e->cont = v->d_cont + (size_t)i * BLAZE_SNAP_MAX_CONT * 3;
        e->ops = v->d_ops ? v->d_ops + (size_t)i * CU_OP_N : NULL;
    }
    if (cu_ck(cudaMemcpy(v->d_envs, v->h_envs, (size_t)n * sizeof(Blaze),
                         cudaMemcpyHostToDevice), "env upload")) {
        blaze_destroy(v);
        return NULL;
    }

    if (v->ktime)
        for (i = 0; i < 4; ++i) cudaEventCreate(&v->ev[i]);
    if (v->stage_time) {
        if (cudaMalloc(&v->d_stage_cycles, 3 * sizeof(unsigned long long)) !=
            cudaSuccess) {
            fprintf(stderr, "blaze_cuda: stage_time counter alloc failed\n");
            blaze_destroy(v);
            return NULL;
        }
        cudaMemset(v->d_stage_cycles, 0, 3 * sizeof(unsigned long long));
    }
    return v;
}

void blaze_destroy(void *vh) {
    CuVecCu *v = (CuVecCu *)vh;
    int i;
    if (!v) return;
    cudaSetDevice(v->device);
    if (v->ktime && v->nsteps) {
        fprintf(stderr,
                "blaze_cuda ktime: %ld steps  k_tick %.1f ms (%.3f ms/step)  "
                "k_obs %.1f ms (%.3f ms/step)  k_final %.1f ms (%.3f ms/step)\n",
                v->nsteps, v->ms_tick, v->ms_tick / v->nsteps,
                v->ms_obs, v->ms_obs / v->nsteps,
                v->ms_final, v->ms_final / v->nsteps);
        for (i = 0; i < 4; ++i) cudaEventDestroy(v->ev[i]);
    }
    if (v->stage_time && v->d_stage_cycles) {
        unsigned long long sum;
        cudaMemcpy(v->h_stage_cycles, v->d_stage_cycles,
                   3 * sizeof(unsigned long long), cudaMemcpyDeviceToHost);
        sum = v->h_stage_cycles[0] + v->h_stage_cycles[1] +
              v->h_stage_cycles[2];
        if (sum) {
            fprintf(stderr,
                    "blaze_cuda stage_time (thread-cycle share of k_tick):  "
                    "begin %.1f%%  recenter %.1f%%  subtick %.1f%%  "
                    "(cycles begin=%llu re=%llu sub=%llu)\n",
                    100.0 * (double)v->h_stage_cycles[0] / (double)sum,
                    100.0 * (double)v->h_stage_cycles[1] / (double)sum,
                    100.0 * (double)v->h_stage_cycles[2] / (double)sum,
                    (unsigned long long)v->h_stage_cycles[0],
                    (unsigned long long)v->h_stage_cycles[1],
                    (unsigned long long)v->h_stage_cycles[2]);
        }
        cudaFree(v->d_stage_cycles);
        v->d_stage_cycles = NULL;
    }
    for (i = 0; i < v->nsnaps; ++i) {
        cudaFree((void *)v->h_snaps[i].cells);
        cudaFree((void *)v->h_snaps[i].coal);
        cudaFree((void *)v->h_snaps[i].xy_off);
        cudaFree((void *)v->h_snaps[i].cont);
    }
    cudaFree(v->d_ops);
    cudaFree(v->d_obs);
    cudaFree(v->d_snaps);
    cudaFree(v->d_recipes);
    cudaFree(v->d_aabb);
    cudaFree(v->d_cont);
    cudaFree(v->d_cand);
    cudaFree(v->d_window);
    cudaFree(v->d_edg);
    cudaFree(v->d_dep);
    cudaFree(v->d_cam);
    cudaFree(v->d_camcells);
    cudaFree(v->d_cells);
    cudaFree(v->d_active);
    cudaFree(v->d_assign);
    cudaFree(v->d_st);
    cudaFree(v->d_envs);
    if (v->stream) cudaStreamDestroy(v->stream);
    free(v->h_envs);
    free(v->h_assign);
    free(v->h_active);
    free(v);
}

/* Size the region pools (n * rvol cells + cam_cells) from the first-loaded
 * snapshot's dims, patch the staged env structs' pointers and re-upload
 * them. Init-time only; all further snapshots must match the dims. */
static int cu_alloc_region_pools(CuVecCu *v, int rnx, int rny, int rnz,
                                 char *err, int err_cap) {
    int i;
    long rvol = (long)rnx * rny * rnz;
    double gb = 2.0 * (double)v->n * rvol * sizeof(u16) / 1e9;
    if (cudaMalloc(&v->d_cells,
                   (size_t)v->n * rvol * sizeof(u16)) != cudaSuccess ||
        cudaMalloc(&v->d_camcells,
                   (size_t)v->n * rvol * sizeof(u16)) != cudaSuccess) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap,
                     "region pool cudaMalloc failed (%dx%dx%d x %d envs = "
                     "%.1f GB)", rnx, rny, rnz, v->n, gb);
        cudaFree(v->d_camcells); v->d_camcells = NULL;
        cudaFree(v->d_cells); v->d_cells = NULL;
        return 0;
    }
    v->rnx = rnx; v->rny = rny; v->rnz = rnz;
    v->rvol = rvol;
    for (i = 0; i < v->n; ++i) {
        v->h_envs[i].cells = v->d_cells + (size_t)i * rvol;
        v->h_envs[i].cam_cells = v->d_camcells + (size_t)i * rvol;
    }
    if (cudaMemcpy(v->d_envs, v->h_envs, (size_t)v->n * sizeof(Blaze),
                   cudaMemcpyHostToDevice) != cudaSuccess) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "env re-upload failed");
        return 0;
    }
    return 1;
}

int blaze_load_snapshots(void *vh, const char *const *paths, int count,
                         char *err, int err_cap) {
    CuVecCu *v = (CuVecCu *)vh;
    int i;
    if (!v || count < 0 || v->nsnaps + count > BLAZE_MAX_SNAPS) return -1;
    cudaSetDevice(v->device);
    for (i = 0; i < count; ++i) {
        CuSnapshot s;
        CuSnapDev *d = &v->h_snaps[v->nsnaps];
        const RlSnapHead *h;
        long svol;
        u16 *d_cells = NULL;
        int *d_coal = NULL;
        if (!blaze_snapshot_load(paths[i], &s, err, err_cap)) return -1;
        h = &s.head;
        if (h->rny > CU_RNY_MAX) {   /* window y>=128 air invariant */
            if (err && err_cap > 0)
                snprintf(err, (size_t)err_cap, "region rny %d > %d: %s",
                         h->rny, CU_RNY_MAX, paths[i]);
            blaze_snapshot_free(&s);
            return -1;
        }
        if (v->rvol == 0) {
            if (!cu_alloc_region_pools(v, h->rnx, h->rny, h->rnz,
                                       err, err_cap)) {
                blaze_snapshot_free(&s);
                return -1;
            }
        } else if (h->rnx != v->rnx || h->rny != v->rny || h->rnz != v->rnz) {
            if (err && err_cap > 0)
                snprintf(err, (size_t)err_cap,
                         "region dims %dx%dx%d != pool %dx%dx%d: %s",
                         h->rnx, h->rny, h->rnz, v->rnx, v->rny, v->rnz,
                         paths[i]);
            blaze_snapshot_free(&s);
            return -1;
        }
        svol = (long)h->rnx * h->rny * h->rnz;
        {
        int *d_xy = NULL;
        int *d_cn = NULL;
        size_t xy_nb = ((size_t)h->rnx * h->rny + 1) * sizeof(int);
        if (cudaMalloc(&d_cells, (size_t)svol * sizeof(u16)) !=
                cudaSuccess ||
            cudaMemcpy(d_cells, s.cells, (size_t)svol * sizeof(u16),
                       cudaMemcpyHostToDevice) != cudaSuccess ||
            (s.ncoal &&
             (cudaMalloc(&d_coal, (size_t)s.ncoal * 3 * sizeof(int)) !=
                  cudaSuccess ||
              cudaMemcpy(d_coal, s.coal, (size_t)s.ncoal * 3 * sizeof(int),
                         cudaMemcpyHostToDevice) != cudaSuccess)) ||
            (s.xy_off &&
             (cudaMalloc(&d_xy, xy_nb) != cudaSuccess ||
              cudaMemcpy(d_xy, s.xy_off, xy_nb,
                         cudaMemcpyHostToDevice) != cudaSuccess)) ||
            (s.ncont > 0 &&
             (cudaMalloc(&d_cn, (size_t)s.ncont * 3 * sizeof(int)) !=
                  cudaSuccess ||
              cudaMemcpy(d_cn, s.cont, (size_t)s.ncont * 3 * sizeof(int),
                         cudaMemcpyHostToDevice) != cudaSuccess))) {
            if (err && err_cap > 0)
                snprintf(err, (size_t)err_cap, "device upload failed: %s",
                         paths[i]);
            cudaFree(d_cells);
            cudaFree(d_coal);
            cudaFree(d_xy);
            cudaFree(d_cn);
            blaze_snapshot_free(&s);
            return -1;
        }
        memset(d, 0, sizeof *d);
        d->head = s.head;
        memcpy(d->items, s.items, sizeof d->items);
        d->cells = d_cells;
        d->coal = d_coal;
        d->ncoal = (int)s.ncoal;
        d->xy_off = d_xy;
        d->cont = d_cn;
        d->ncont = s.ncont;
        }
        v->has_liquid[v->nsnaps] = s.has_liquid;
        blaze_snapshot_free(&s);
        v->nsnaps++;
    }
    if (cu_ck(cudaMemcpy(v->d_snaps, v->h_snaps,
                         (size_t)v->nsnaps * sizeof(CuSnapDev),
                         cudaMemcpyHostToDevice), "snap table upload"))
        return -1;
    return v->nsnaps;
}

int blaze_snapshot_has_liquid(void *vh, int snap) {
    CuVecCu *v = (CuVecCu *)vh;
    if (!v || snap < 0 || snap >= v->nsnaps) return -1;
    return v->has_liquid[snap];
}

/* OPT-IN training-reward mode: gate the +0.03 crosshair-attack bonus on
 * nearest-coal dist <= dist_gate. dist_gate <= 0 restores the default
 * (exact bitwise-gated ppo_coal semantics). */
int blaze_set_reward_gate(void *vh, double dist_gate) {
    CuVecCu *v = (CuVecCu *)vh;
    if (!v) return -1;
    v->atk_gate = dist_gate;
    return 0;
}

/* OPT-IN chain-training mode: which inventory item id fires the in-kernel
 * +10/done=1 on count increase vs its at-reset baseline. 263 (default) =
 * exact legacy mine-coal semantics; 50 = torches (full chain); 0 = never.
 * Applies to envs at their NEXT reset. */
int blaze_set_success_item(void *vh, int item) {
    CuVecCu *v = (CuVecCu *)vh;
    if (!v || item < 0) return -1;
    v->success_item = item;
    return 0;
}

int blaze_assign(void *vh, const int *snap_idx) {
    CuVecCu *v = (CuVecCu *)vh;
    int i;
    if (!v || !snap_idx) return -1;
    for (i = 0; i < v->n; ++i)
        if (snap_idx[i] < 0 || snap_idx[i] >= v->nsnaps) return -1;
    memcpy(v->h_assign, snap_idx, (size_t)v->n * sizeof(int));
    cudaSetDevice(v->device);
    return cu_ck(cudaMemcpy(v->d_assign, snap_idx,
                            (size_t)v->n * sizeof(int),
                            cudaMemcpyHostToDevice), "assign upload");
}

int blaze_reset(void *vh, const unsigned char *mask) {
    CuVecCu *v = (CuVecCu *)vh;
    int i, nact = 0;
    long long bulk, bulk_blocks;
    if (!v || v->rvol == 0) return -1;   /* pools exist after first load */
    for (i = 0; i < v->n; ++i) {   /* compact the resetting envs host-side */
        if (mask && !mask[i]) continue;
        if (v->h_assign[i] < 0) return -1;
        v->h_active[nact++] = i;
    }
    if (!nact) return 0;
    cudaSetDevice(v->device);
    if (cu_ck(cudaMemcpyAsync(v->d_active, v->h_active,
                              (size_t)nact * sizeof(int),
                              cudaMemcpyHostToDevice, v->stream),
              "active upload"))
        return -1;
    k_reset_scalar<<<(nact + CU_TPB - 1) / CU_TPB, CU_TPB, 0, v->stream>>>(
        v->d_envs, v->d_active, nact, v->d_snaps, v->d_assign,
        v->success_item);
    /* all snapshots share the pool dims, so the bulk count is uniform */
    bulk = v->rvol + (long long)PSV_NCHUNKS * MC_CHUNK_VOL + CU_NPIX;
    bulk_blocks = ((long long)nact * bulk + CU_TPB - 1) / CU_TPB;
    k_reset_bulk<<<(unsigned)bulk_blocks, CU_TPB, 0, v->stream>>>(
        v->d_envs, v->d_active, nact, bulk, v->d_snaps, v->d_assign);
    return cu_ck(cudaStreamSynchronize(v->stream), "k_reset");
}

/* blaze_step + an optional int32[n][CU_STATUS_K] status readout (device
 * pointer; the 9 rl_inv_ids counts, hotbar_sel, held item id, container).
 * status == NULL is the legacy blaze_step. */
int blaze_step_full(void *vh, const double *actions, int repeat,
                    unsigned short *cam, unsigned char *depth,
                    unsigned char *edge, float *scal, float *rew,
                    unsigned char *done, float *pose, int *status) {
    CuVecCu *v = (CuVecCu *)vh;
    int eblocks, pblocks;
    if (!v || !actions || repeat < 1) return -1;
    cudaSetDevice(v->device);
    eblocks = (v->n + CU_TPB - 1) / CU_TPB;
    pblocks = (int)(((size_t)v->n * CU_NPIX + CU_TPB - 1) / CU_TPB);
    if (v->ktime) cudaEventRecord(v->ev[0], v->stream);
    if (v->legacy_recenter)
        k_tick_legacy<<<(v->n + CU_TICK_TPB - 1) / CU_TICK_TPB, CU_TICK_TPB,
                        0, v->stream>>>(v->d_envs, v->n, v->d_st, actions,
                                        repeat, v->d_aabb, v->d_recipes,
                                        v->nrecipes, v->atk_gate);
    else if (v->warp_tick)
        k_tick_warp<<<(unsigned)(((size_t)v->n * 32 + 127) / 128), 128, 0,
                      v->stream>>>(v->d_envs, v->n, v->d_st, actions,
                                   repeat, v->d_aabb, v->d_recipes,
                                   v->nrecipes, v->atk_gate,
                                   v->stage_time ? v->d_stage_cycles : NULL);
    else
        k_tick<<<(v->n + CU_TICK_TPB - 1) / CU_TICK_TPB, CU_TICK_TPB, 0,
                 v->stream>>>(v->d_envs, v->n, v->d_st, actions, repeat,
                              v->d_aabb, v->d_recipes, v->nrecipes,
                              v->atk_gate,
                              v->stage_time ? v->d_stage_cycles : NULL);
    if (v->ktime) cudaEventRecord(v->ev[1], v->stream);
    k_obs<<<pblocks, CU_TPB, 0, v->stream>>>(v->d_envs, v->n, v->d_st,
                                             cam, depth, edge);
    if (v->ktime) cudaEventRecord(v->ev[2], v->stream);
    k_final<<<eblocks, CU_TPB, 0, v->stream>>>(v->d_envs, v->n, v->d_st,
                                               scal, rew, done, pose,
                                               v->atk_gate, status);
    if (v->ktime) cudaEventRecord(v->ev[3], v->stream);
    if (cu_ck(cudaStreamSynchronize(v->stream), "blaze_step")) return -1;
    if (v->ktime) {
        float ms;
        cudaEventElapsedTime(&ms, v->ev[0], v->ev[1]); v->ms_tick += ms;
        cudaEventElapsedTime(&ms, v->ev[1], v->ev[2]); v->ms_obs += ms;
        cudaEventElapsedTime(&ms, v->ev[2], v->ev[3]); v->ms_final += ms;
        v->nsteps++;
    }
    return 0;
}

int blaze_step(void *vh, const double *actions, int repeat,
               unsigned short *cam, unsigned char *depth, unsigned char *edge,
               float *scal, float *rew, unsigned char *done, float *pose) {
    return blaze_step_full(vh, actions, repeat, cam, depth, edge, scal, rew,
                           done, pose, NULL);
}

/* Capture a LIVE env's full state into snapshot slot `slot` (self-generated
 * start-state curriculum). slot overwrites an existing snapshot or appends
 * at nsnaps (dense growth). The slot inherits the env's current region cells
 * (post-edit world), its static ore list and the source snapshot's liquid
 * flag. Rare host call; the per-slot cudaMallocs happen once per slot (all
 * snapshots share the region dims) except the coal list, which reallocs only
 * when its length changes. CAUTION: overwriting a slot frees/rewrites the
 * device coal buffer that envs previously reset FROM this slot still point
 * at - keep a fixed (seed,stage)->slot discipline (same seed => identical
 * ore list, so the rewrite is content-identical) or reset those envs first. */
int blaze_capture(void *vh, int env, int slot) {
    CuVecCu *v = (CuVecCu *)vh;
    Blaze he;
    CuSnapDev *d;
    if (!v || env < 0 || env >= v->n || slot < 0 ||
        slot >= BLAZE_MAX_SNAPS || slot > v->nsnaps || v->rvol == 0)
        return -1;
    if (v->h_assign[env] < 0) return -1;
    cudaSetDevice(v->device);
    if (cu_ck(cudaMemcpy(&he, v->d_envs + env, sizeof he,
                         cudaMemcpyDeviceToHost), "capture env readback"))
        return -1;
    d = &v->h_snaps[slot];
    if (slot == v->nsnaps) {
        memset(d, 0, sizeof *d);
        v->nsnaps++;
    }
    (void)blaze_capture_head(&he, &d->head, d->items);
    if (!d->cells) {
        u16 *cells = NULL;
        if (cu_ck(cudaMalloc(&cells, (size_t)v->rvol * sizeof(u16)),
                  "capture cells alloc"))
            return -1;
        d->cells = cells;
    }
    if (cu_ck(cudaMemcpy((void *)d->cells, v->d_cells + (size_t)env * v->rvol,
                         (size_t)v->rvol * sizeof(u16),
                         cudaMemcpyDeviceToDevice), "capture cells copy"))
        return -1;
    if (d->ncoal != he.nore) {
        int *coal = NULL;
        cudaFree((void *)d->coal);
        d->coal = NULL;
        if (he.nore &&
            cu_ck(cudaMalloc(&coal, (size_t)he.nore * 3 * sizeof(int)),
                  "capture coal alloc")) {
            d->ncoal = 0;
            return -1;
        }
        d->coal = coal;
        d->ncoal = he.nore;
    }
    if (he.nore &&
        cu_ck(cudaMemcpy((void *)d->coal, he.ore,
                         (size_t)he.nore * 3 * sizeof(int),
                         cudaMemcpyDeviceToDevice), "capture coal copy"))
        return -1;
    {   /* the captured ore list IS the assign-source snapshot's (he.ore was
         * bound at reset and never mutates), so its spatial index carries
         * over verbatim; all snapshots share the region dims. */
        const int *src_xy = v->h_snaps[v->h_assign[env]].xy_off;
        size_t xy_nb = ((size_t)v->rnx * v->rny + 1) * sizeof(int);
        if (src_xy) {
            if (!d->xy_off) {
                int *xy = NULL;
                if (cu_ck(cudaMalloc(&xy, xy_nb), "capture xy_off alloc"))
                    return -1;
                d->xy_off = xy;
            }
            if (cu_ck(cudaMemcpy((void *)d->xy_off, src_xy, xy_nb,
                                 cudaMemcpyDeviceToDevice),
                      "capture xy_off copy"))
                return -1;
        } else {
            cudaFree((void *)d->xy_off);
            d->xy_off = NULL;
        }
    }
    {   /* container list: the env's LIVE device list is exactly the captured
         * region's (maintained on every edit). Fixed-cap slot buffer,
         * allocated once; overflow (-1) rides along. */
        if (!d->cont) {
            int *cn = NULL;
            if (cu_ck(cudaMalloc(&cn, (size_t)BLAZE_SNAP_MAX_CONT * 3 *
                                          sizeof(int)),
                      "capture cont alloc"))
                return -1;
            d->cont = cn;
        }
        d->ncont = he.n_cont;
        if (he.n_cont > 0 &&
            cu_ck(cudaMemcpy((void *)d->cont, he.cont,
                             (size_t)he.n_cont * 3 * sizeof(int),
                             cudaMemcpyDeviceToDevice), "capture cont copy"))
            return -1;
    }
    v->has_liquid[slot] = v->has_liquid[v->h_assign[env]];
    return cu_ck(cudaMemcpy(v->d_snaps + slot, d, sizeof *d,
                            cudaMemcpyHostToDevice), "capture snap upload");
}

/* ---- verify helpers (host in/out buffers) ---- */

int blaze_obs_size(void) { return (int)sizeof(CuBinObs); }

int blaze_emit(void *vh, int env, int want_cam, void *out) {
    CuVecCu *v = (CuVecCu *)vh;
    if (!v || env < 0 || env >= v->n || !out) return -1;
    cudaSetDevice(v->device);
    if (want_cam)
        k_emit_cam<<<(CU_NPIX + CU_TPB - 1) / CU_TPB, CU_TPB, 0, v->stream>>>(
            v->d_envs, env, v->d_st);
    k_emit<<<1, 1, 0, v->stream>>>(v->d_envs, env, v->d_st, v->d_obs);
    if (cu_ck(cudaStreamSynchronize(v->stream), "k_emit")) return -1;
    return cu_ck(cudaMemcpy(out, v->d_obs, sizeof(CuBinObs),
                            cudaMemcpyDeviceToHost), "obs readback");
}

/* env == -1 broadcasts the same raw action to ALL envs in one launch (one
 * thread per env; no obs - use blaze_emit per lane). Mirrors the CPU
 * driver's broadcast loop. */
int blaze_tick_raw(void *vh, int env, const double a[13], int want_cam,
                   void *out) {
    CuVecCu *v = (CuVecCu *)vh;
    CuRawAct ra;
    if (!v || env < -1 || env >= v->n || !a) return -1;
    memcpy(ra.a, a, sizeof ra.a);
    cudaSetDevice(v->device);
    if (env == -1)
        k_tick_raw<<<(v->n + CU_TICK_TPB - 1) / CU_TICK_TPB, CU_TICK_TPB, 0,
                     v->stream>>>(v->d_envs, -1, v->n, v->d_st, ra, 0, NULL,
                                  v->d_aabb, v->d_recipes, v->nrecipes);
    else
        k_tick_raw<<<1, 1, 0, v->stream>>>(v->d_envs, env, v->n, v->d_st, ra,
                                           want_cam, out ? v->d_obs : NULL,
                                           v->d_aabb, v->d_recipes,
                                           v->nrecipes);
    if (cu_ck(cudaStreamSynchronize(v->stream), "k_tick_raw")) return -1;
    if (!out || env == -1) return 0;
    return cu_ck(cudaMemcpy(out, v->d_obs, sizeof(CuBinObs),
                            cudaMemcpyDeviceToHost), "obs readback");
}

/* op-trace readout: counters per env (buffer sizing) + the n * CU_OP_N
 * cumulative device counters copied to host `out` (row-major, env-major).
 * Returns -1 when tracing is off (BLAZE_OP_TRACE unset at create). */
int blaze_op_count(void) { return CU_OP_N; }

int blaze_op_trace(void *vh, unsigned long long *out) {
    CuVecCu *v = (CuVecCu *)vh;
    if (!v || !out || !v->d_ops) return -1;
    cudaSetDevice(v->device);
    return cu_ck(cudaMemcpy(out, v->d_ops,
                            (size_t)v->n * CU_OP_N *
                                sizeof(unsigned long long),
                            cudaMemcpyDeviceToHost), "op-trace readback");
}

int blaze_debug_state(void *vh, int env, double *out, int cap) {
    CuVecCu *v = (CuVecCu *)vh;
    Blaze e;
    if (!v || env < 0 || env >= v->n || !out || cap < 21) return -1;
    cudaSetDevice(v->device);
    if (cu_ck(cudaMemcpy(&e, v->d_envs + env, sizeof e,
                         cudaMemcpyDeviceToHost), "env readback"))
        return -1;
    return blaze_debug_fill(&e, out);
}
