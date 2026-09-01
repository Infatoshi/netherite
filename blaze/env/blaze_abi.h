/* blaze_abi.h - shared create-time options for blaze_cpu.so / blaze_cuda.so.
 *
 * Runtime knobs that used to be getenv() at blaze_create are fields here,
 * threaded from the Python wrapper (or any other host) as explicit parameters.
 * NULL opts on blaze_create means compiled defaults (warp_tick=1, rest 0).
 */
#ifndef BLAZE_ABI_H
#define BLAZE_ABI_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BlazeCreateOpts {
    int ktime;            /* BLAZE_KTIME: kernel event timing at destroy */
    int stage_time;       /* BLAZE_STAGE_TIME: k_tick stage cycle counters */
    int legacy_recenter;  /* BLAZE_LEGACY_RECENTER: serial-recenter kernel */
    int warp_tick;        /* BLAZE_WARP_TICK: 1 = warp-per-env (default), 0 = flat */
    int op_trace;         /* BLAZE_OP_TRACE: per-env CU_OP_* activity counters */
    int no_ore_xy;        /* BLAZE_NO_ORE_XY: skip ore spatial index at snap load */
    int phase_time;       /* per-env k_tick_warp phase clocks [n][BLAZE_PHASE_K] */
} BlazeCreateOpts;

/* Per-env phase timer columns (create opts.phase_time=1, CUDA warp path).
 * 4..17 split former WORLD_REST (runtime_tick_nr minus player minus collect).
 * wr_pre (hazards + world edits) further split after it owned the 1.3s tail. */
#define BLAZE_PHASE_BEGIN       0
#define BLAZE_PHASE_RECENTER    1
#define BLAZE_PHASE_COLLECT     2
#define BLAZE_PHASE_PLAYER_REST 3
#define BLAZE_PHASE_HZ          4  /* post-player hazard hurts */
#define BLAZE_PHASE_SETSTATE    5  /* cu_world_set_state (incl. light) */
#define BLAZE_PHASE_FLCH        6  /* fl_block_changed */
#define BLAZE_PHASE_EDITREST    7  /* fluid_mark, plants, fluid mix, spawn */
#define BLAZE_PHASE_WEATHER     8
#define BLAZE_PHASE_FLUID       9
#define BLAZE_PHASE_RANDTICK    10
#define BLAZE_PHASE_MOBS        11
#define BLAZE_PHASE_BOAT        12
#define BLAZE_PHASE_XP          13
#define BLAZE_PHASE_EXPLOSION   14
#define BLAZE_PHASE_PROJ        15
#define BLAZE_PHASE_LIVE        16
#define BLAZE_PHASE_TILE        17 /* furnaces + chests */
#define BLAZE_PHASE_COAL        18
#define BLAZE_PHASE_POST        19
#define BLAZE_PHASE_DIGEST      20 /* set_state digest/census, not light */
#define BLAZE_PHASE_SKY_CHUNK   21 /* cu_skylight_chunk */
#define BLAZE_PHASE_SKY_BOX     22 /* cu_skylight_spread_box */
#define BLAZE_PHASE_BLK         23 /* cu_check_light_for_block */
#define BLAZE_PHASE_K           24

/* Fill *o with the historical unset-env defaults. */
static inline void blaze_create_opts_default(BlazeCreateOpts *o) {
    if (!o) return;
    o->ktime = 0;
    o->stage_time = 0;
    o->legacy_recenter = 0;
    o->warp_tick = 1;
    o->op_trace = 0;
    o->no_ore_xy = 0;
    o->phase_time = 0;
}

/* opts may be NULL (defaults). device is ignored by the CPU backend. */
void *blaze_create(int device, int n, const BlazeCreateOpts *opts);

/* CUDA warp path: ndec decisions in one k_tick_warp. actions is device
 * [ndec][n][13]. obs/final once at the end. Returns -1 on CPU / non-warp. */
int blaze_step_ndec(void *vh, const double *actions, int ndec, int repeat,
                    unsigned short *cam, unsigned char *depth,
                    unsigned char *edge, float *scal, float *rew,
                    unsigned char *done, float *pose);

/* phase_time readout: column count, then n * BLAZE_PHASE_K u64s (env-major)
 * copied to host `out`. Returns -1 when phase_time was 0 at create. */
int blaze_phase_k(void);
int blaze_copy_phase(void *vh, unsigned long long *out);

/* Packed sky<<4|block nibble plane. cap must be >= region volume.
 * Returns volume on success, -1 on error. */
int blaze_region_vol(void *vh);
int blaze_copy_light(void *vh, int env, unsigned char *out, int cap);

#ifdef __cplusplus
}
#endif
#endif /* BLAZE_ABI_H */
