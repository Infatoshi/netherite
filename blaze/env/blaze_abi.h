/* blaze_abi.h - shared create-time options for blaze_cpu.so / blaze_cuda.so.
 *
 * Runtime knobs that used to be getenv() at blaze_create are fields here,
 * threaded from the Python wrapper (or any other host) as explicit parameters.
 * NULL opts on blaze_create means compiled defaults (warp_tick=1, rest 0).
 */
#ifndef BLAZE_ABI_H
#define BLAZE_ABI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BlazeCreateOpts {
    int ktime;            /* BLAZE_KTIME: kernel event timing at destroy */
    int stage_time;       /* per-env k_tick phase clocks [n][CU_PHASE_K] */
    int legacy_recenter;  /* removed A/B knob. The CUDA backend deleted
                           * k_tick_legacy. blaze_create fails when this
                           * is nonzero. Keep it 0. */
    int warp_tick;        /* BLAZE_WARP_TICK: 1 = warp-per-env (default), 0 = flat */
    int op_trace;         /* BLAZE_OP_TRACE: per-env CU_OP_* activity counters */
    int no_ore_xy;        /* BLAZE_NO_ORE_XY: skip ore spatial index at snap load */
    int stack_kib;        /* CUDA per-thread stack limit, KiB. 0 = 128 default.
                           * CPU backend ignores it. See blaze_cuda.cu
                           * blaze_create for why the default is 128. */
    const char *nether_bank; /* path to Nether .bsnp bank, NULL = unset */
    const char *end_bank;    /* path to End .bsnp bank, NULL = unset */
} BlazeCreateOpts;

/* Fill *o with the historical unset-env defaults. */
static inline void blaze_create_opts_default(BlazeCreateOpts *o) {
    if (!o) return;
    o->ktime = 0;
    o->stage_time = 0;
    o->legacy_recenter = 0;
    o->warp_tick = 1;
    o->op_trace = 0;
    o->no_ore_xy = 0;
    o->stack_kib = 128;
    o->nether_bank = NULL;
    o->end_bank = NULL;
}

/* opts may be NULL (defaults). device is ignored by the CPU backend. */
void *blaze_create(int device, int n, const BlazeCreateOpts *opts);

/* stage_time readout: column count, then n * columns u64s (env-major)
 * copied to host `out`. Returns -1 when stage_time was 0 at create.
 * blaze_phase_clear zeros the device (or host) counters. */
int blaze_phase_k(void);
int blaze_copy_phase(void *vh, unsigned long long *out);
int blaze_phase_clear(void *vh);

#ifdef __cplusplus
}
#endif
#endif /* BLAZE_ABI_H */
