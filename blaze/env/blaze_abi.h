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
 * 4..14 split former WORLD_REST (runtime_tick_nr minus player minus collect). */
#define BLAZE_PHASE_BEGIN       0
#define BLAZE_PHASE_RECENTER    1
#define BLAZE_PHASE_COLLECT     2
#define BLAZE_PHASE_PLAYER_REST 3
#define BLAZE_PHASE_WR_PRE      4  /* hazards + world edits */
#define BLAZE_PHASE_WEATHER     5
#define BLAZE_PHASE_FLUID       6
#define BLAZE_PHASE_RANDTICK    7
#define BLAZE_PHASE_MOBS        8
#define BLAZE_PHASE_BOAT        9
#define BLAZE_PHASE_XP          10
#define BLAZE_PHASE_EXPLOSION   11
#define BLAZE_PHASE_PROJ        12
#define BLAZE_PHASE_LIVE        13
#define BLAZE_PHASE_TILE        14 /* furnaces + chests */
#define BLAZE_PHASE_COAL        15
#define BLAZE_PHASE_POST        16
#define BLAZE_PHASE_K           17

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

/* phase_time readout: column count, then n * BLAZE_PHASE_K u64s (env-major)
 * copied to host `out`. Returns -1 when phase_time was 0 at create. */
int blaze_phase_k(void);
int blaze_copy_phase(void *vh, unsigned long long *out);

#ifdef __cplusplus
}
#endif
#endif /* BLAZE_ABI_H */
