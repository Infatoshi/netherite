/* blaze_cuda_int.h - private declarations shared by the CUDA backend's two
 * translation units.
 *
 * blaze_cuda.cu holds the training path: reset, k_tick, k_tick_warp, k_obs,
 * k_final and the whole host driver.
 * blaze_cuda_verify.cu holds the verify-only kernels: k_tick_raw, the emit
 * kernels and the parity kernels, plus their host entry points.
 *
 * The split exists for COMPILE TIME. Each tick-class kernel inlines the whole
 * header-only sim (blaze_core.h), so one unit with every kernel made cicc and
 * ptxas re-optimise the same 1800 device functions four times. Two units
 * compile in parallel, and an edit to one does not recompile the other.
 *
 * Each unit is SELF-CONTAINED: every device function in blaze_core.h is
 * MC_HD static inline, so each unit gets its own copies and no device call
 * crosses a unit boundary. Do not add -rdc; whole-program mode per unit is
 * what keeps the register counts and the SASS the same as the single unit.
 *
 * The exported C ABI (blaze_abi.h plus the extern "C" block below) is the
 * same as blaze_cpu.c. Nothing here is public. */
#ifndef BLAZE_CUDA_INT_H
#define BLAZE_CUDA_INT_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cuda_runtime.h>

#include "blaze_core.h"
#include "blaze_abi.h"

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
    const u8 *light;             /* device, packed (sky<<4)|block or NULL */
    const u8 *biome;             /* device, rnx*rnz column ids; v7 = plains */
    const int *coal;             /* device, ncoal x 3 */
    int ncoal;
    const int *xy_off;           /* device, rnx*rny+1 CSR (ix,iy)->coal-range
                                  * offsets (blaze_build_ore_xy); NULL =
                                  * full-scan candidate rebuild */
    const int *cont;             /* device, ncont x 3 container cells (58/61/
                                  * 62); interact-candidate seed */
    int ncont;                   /* -1 = overflow: full window scan fallback */
    unsigned n_mobs;
    RlSnapMob mobs[BLAZE_SNAP_MAX_MOBS];
    unsigned n_orbs;
    RlSnapOrb orbs[BLAZE_SNAP_MAX_ORBS];
    unsigned long long world_rand_seed;
    int update_lcg;
    int player_fire;
    int player_air;
    long long ww_total_time;
    long long ww_world_time;
    int ww_rain_time;
    int ww_thunder_time;
    int ww_raining;
    int ww_thundering;
    unsigned long long ww_rand_seed48;
    unsigned rt_mutations;
    unsigned n_proj;
    RlSnapProj proj[BLAZE_SNAP_MAX_PROJ];
    unsigned parity_proj_hits;
    unsigned n_fall;
    RlSnapFall falls[BLAZE_SNAP_MAX_FALL];
    unsigned n_fall_upd;
    RlSnapFallUpdate fall_upd[BLAZE_SNAP_MAX_FALL_UPD];
    unsigned n_fall_land;
    RlSnapFallLanding fall_land[BLAZE_SNAP_MAX_FALL];
    unsigned fall_mutations;
    int live_ticks;
    unsigned n_furn;
    RlSnapFurnace furn[BLAZE_SNAP_MAX_FURN];
    int active_furnace;
    unsigned n_chest;
    RlSnapChest chest[BLAZE_SNAP_MAX_CHEST];
    int active_chest;
    int craft[9][3];
    int cursor[3];
    unsigned craft_attempts, craft_successes, container_opens;
    int left_click_counter, eat_ticks, eat_item;
    int bow_ticks, bow_drawing;
    int xp_level, xp_total, xp_cooldown;
    float xp_experience;
    int armor[4][3];
    int fluid_dim;
    RlSnapFluidReg fluid[BLAZE_SNAP_FLUID_REGS];
    unsigned fluid_mutations;
    int boat_ride;
    int explosion_pending, explosion_smoking, explosion_flaming;
    double explosion_x, explosion_y, explosion_z;
    float explosion_size;
    RlSnapV10Xtra xtra;
    int n_potions;
    RlSnapPotion potions[BLAZE_SNAP_POTION_MAX];
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
    /* pooled per-env buffers. Region-sized pools (d_cells/d_light) are
     * allocated lazily at the FIRST snapshot load - dims come from the
     * snapshot header. Still init-time-only: nothing allocates in a tick
     * path. */
    int rnx, rny, rnz;           /* 0 until the first snapshot is loaded */
    long rvol;
    u16 *d_cells, *d_cam;
    u16 *d_grass;            /* per-env grass_sec census (CU_SEC_SPAN cube) */
    u16 *d_fluid_cur, *d_fluid_tmp; /* n * CU_FLUID_VOL CA grids; same as CPU */
    int *d_rt_leaf;              /* n * RT_LIVE_SURR BlockLeaves scratch */
    int *d_light_q;              /* n * CU_LIGHT_Q BLOCK flood queue */
    u8 *d_light, *d_biome, *d_dep, *d_edg;
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
    /* capture may replace coal/xy_off while live envs still alias the old
     * buffer (env->ore / env->ore_xy bind by pointer at reset). Do not
     * cudaFree those until destroy. */
    const void *retired[BLAZE_MAX_SNAPS * 4];
    int nretired;
    int has_liquid[BLAZE_MAX_SNAPS];
    int has_unrepresented[BLAZE_MAX_SNAPS];
    /* verify-helper scratch */
    CuBinObs *d_obs;
    /* blaze_tick host staging: n * 13 trainer actions + n * 4 inv_click
     * extras. Lazy; training blaze_step never touches these. */
    double *d_tick_act, *h_tick_act;
    double *d_tick_inv, *h_tick_inv;
    /* blaze_emit_all's n-record staging buffer. Allocated on first use rather
     * than at create (same pattern as blaze_capture's slot buffers): only the
     * verify gates emit, and at n=4096 this would be 60 MB of pool the
     * training path never touches. */
    CuBinObs *d_obs_all;
    /* blaze_parity_state_all's n-record staging (n * 592 B; lazy like d_obs_all). */
    BpParityRecord *d_parity_all;
    double atk_gate;  /* opt-in +0.03 gate; 0 = off (exact ppo_coal) */
    int success_item; /* +10/done=1 item id; 263 default (exact ppo_coal),
                       * 0 = never. Applied to envs at their next reset. */
    int mobs_enabled; /* magma --mobs on: hostile AI/combat live tick */
    int natural_spawn;
    int natural_spawn_passive;
    long long world_time_pin;
    int elytra_kit;   /* magma --elytra on: chest 443 after reset */
    int warp_tick;        /* create opts (default 1): one env per WARP -
                           * 32x resident warps for the latency-bound serial
                           * chains + warp-parallel coal sweep. 0 = flat
                           * one-env-per-thread k_tick. */
    int no_ore_xy;        /* create opts: skip ore spatial index at snap load */
    /* optional kernel timing (create opts.ktime) */
    int ktime;
    cudaEvent_t ev[4];
    double ms_tick, ms_obs, ms_final;
    long nsteps;
    /* optional k_tick stage cycle counters (create opts.stage_time):
     * [0] decision_begin  [1] recenter (pose+coop fill)  [2] decision_subtick
     * sum of per-thread clock64 deltas; relative share of work, not wall. */
    int stage_time;
    unsigned long long *d_stage_cycles; /* device, 3 counters */
    unsigned long long h_stage_cycles[3];
    /* optional op-trace activity counters (create opts.op_trace): device pool
     * of n * CU_OP_N u64s, sliced into every env's ->ops at create. */
    int op_trace;
    unsigned long long *d_ops;
} CuVecCu;

extern "C" {
/* blaze_create declared in blaze_abi.h */
void blaze_destroy(void *vh);
int blaze_load_snapshots(void *vh, const char *const *paths, int count,
                         char *err, int err_cap);
int blaze_snapshot_has_liquid(void *vh, int snap);
int blaze_parity_size(void);
unsigned long long blaze_capabilities(void);
unsigned long long blaze_snapshot_requirements(void *vh, int snap);
int blaze_parity_state(void *vh, int env, void *out);
int blaze_parity_state_all(void *vh, void *out);
int blaze_assign(void *vh, const int *snap_idx);
int blaze_set_reward_gate(void *vh, double dist_gate);
int blaze_set_success_item(void *vh, int item);
int blaze_set_mobs_enabled(void *vh, int on);
int blaze_set_natural_spawn(void *vh, int on);
int blaze_set_natural_spawn_passive(void *vh, int on);
int blaze_set_world_time(void *vh, long long world_time);
int blaze_set_elytra_enabled(void *vh, int on);
int blaze_reset(void *vh, const unsigned char *mask);
int blaze_step(void *vh, const double *actions, int repeat,
               unsigned short *cam, unsigned char *depth, unsigned char *edge,
               float *scal, float *rew, unsigned char *done, float *pose);
int blaze_step_full(void *vh, const double *actions, int repeat,
                    unsigned short *cam, unsigned char *depth,
                    unsigned char *edge, float *scal, float *rew,
                    unsigned char *done, float *pose, int *status);
int blaze_capture(void *vh, int env, int slot);
int blaze_dump_snapshot(void *vh, int env, const char *path,
                        char *err, int err_cap);
int blaze_obs_size(void);
int blaze_emit(void *vh, int env, int want_cam, void *out);
int blaze_emit_all(void *vh, int want_cam, void *out);
int blaze_tick_raw(void *vh, int env, const double a[17], int want_cam,
                   void *out);
int blaze_tick(void *vh, int env, const double a[17], int want_cam,
               void *out);
int blaze_debug_state(void *vh, int env, double *out, int cap);
int blaze_op_count(void);
int blaze_op_trace(void *vh, unsigned long long *out);
}

/* BLAZE_SCALAR_TICK: build the scalar one-env-per-thread k_tick. OFF.
 *
 * k_tick is the flat kernel that create opts warp_tick=0 selects. The trainer
 * and every default gate run k_tick_warp instead, so the scalar kernel only
 * serves verify_cuda.py --m2-kernel scalar and the port-matrix scalar rows.
 * Building it costs a whole extra inline of the header-only sim in cicc and
 * ptxas, which is minutes.
 *
 * The warp_tick knob is unchanged. In a build without the kernel,
 * blaze_create REFUSES warp_tick=0 with a message instead of quietly running
 * the warp kernel, so a scalar-row gate fails loudly. Build the kernel with
 *   make -C blaze/rl env-cuda BLAZE_SCALAR_TICK=1
 *   make -C magma blaze_cuda_so BLAZE_SCALAR_TICK=1
 */
#ifndef BLAZE_SCALAR_TICK
#define BLAZE_SCALAR_TICK 0
#endif

/* CUDA error check + report, shared by both units. */
static inline int cu_ck(cudaError_t e, const char *what) {
    if (e == cudaSuccess) return 0;
    fprintf(stderr, "blaze_cuda: %s: %s\n", what, cudaGetErrorString(e));
    return -1;
}

#endif /* BLAZE_CUDA_INT_H */
