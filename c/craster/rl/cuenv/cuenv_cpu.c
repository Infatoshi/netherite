/* cuenv_cpu.c - batched CPU reference driver over cuenv_core.h: N envs
 * stepped in parallel (OpenMP over env index) with the exact C ABI the CUDA
 * .so (M2) will export, so the trainer/verify scripts are backend-agnostic.
 * The `device` argument is accepted and ignored here. Envs are independent
 * (per-env region/window/cam/AABB scratch); shared tables (sin, recipes,
 * snapshot ore lists) are read-only after init. OMP_NUM_THREADS controls
 * width; OMP_NUM_THREADS=1 recovers the old serial path for bisect.
 *
 * ABI (all pointers caller-owned host memory in this backend):
 *   cuenv_create(device, n) -> handle
 *   cuenv_load_snapshots(h, paths, count, err, cap) -> nloaded or -1
 *   cuenv_snapshot_has_liquid(h, snap) -> 0/1 (flagged snapshots are unsafe:
 *                                         fluids CA is not simulated)
 *   cuenv_assign(h, snap_idx[n])       -> per-env snapshot binding
 *   cuenv_reset(h, mask[n] or NULL)
 *   cuenv_step(h, actions[n][13] doubles, repeat, cam, depth, edge, scal,
 *              rew, done, pose)
 *     actions row = the FULL raw action vector (cuenv_tick_raw layout):
 *     {forward,strafe,dyaw,dpitch,jump,sneak,sprint,attack,use,hotbar(-1),
 *      craft(-1),interact,smelt}; craft/interact/smelt fire once, pre-tick,
 *     before sub-tick 0. Legacy 5-head trainer actions are expanded to this
 *     layout in cuenv.py (bit-identical decode).
 *   cuenv_destroy(h)
 * Verify helpers (batch-of-1 lockstep vs the real craster_game):
 *   cuenv_obs_size() -> sizeof(CuBinObs) == sizeof(RlBinObs)
 *   cuenv_emit(h, env, want_cam, out)  -> BOLR-layout obs, no tick
 *   cuenv_tick_raw(h, env, a[13], want_cam, out) -> one action line: craft/
 *       interact/smelt primitives then one gm_runtime_tick equivalent + obs;
 *       a = {forward,strafe,dyaw,dpitch,jump,sneak,sprint,attack,use,hotbar,
 *       craft(-1=none),interact,smelt}; env == -1 broadcasts the same action to
 *       ALL envs (no obs; out ignored) - the verify chain gate's batched
 *       lane stepper
 *   cuenv_debug_state(h, env, out[32]) -> raw doubles for divergence bisect
 *
 * Reward/scalars (ppo_coal.py semantics) live in cuenv_core.h as MC_HD code
 * (cuenv_decision_ticks/cuenv_decision_finalize) shared with the CUDA driver
 * - single source, so CPU and CUDA rewards are gated against each other. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <omp.h>

#include "cuenv_core.h"

#define CUENV_MAX_SNAPS 128
#define CUENV_ACT_HEADS 13

void cuenv_destroy(void *vh);

typedef struct {
    int n;
    McSinTable st;
    CuEnv *envs;
    int   *assign;
    CuSnapshot snaps[CUENV_MAX_SNAPS];
    int    nsnaps;
    /* pooled per-env buffers. Region-sized pools (cells/camcells) are
     * allocated on the FIRST snapshot load - the region dims come from the
     * snapshot header; every loaded snapshot must share them. Still
     * init-time-only: nothing allocates in a tick path. */
    int    rnx, rny, rnz;    /* 0 until the first snapshot is loaded */
    long   rvol;
    u16   *cells_pool, *camcells_pool, *cam_pool;
    u8    *dep_pool, *edg_pool;
    Chunk *window_pool;
    CuCand *cand_pool;
    int   *cont_pool;        /* per-env CUENV_SNAP_MAX_CONT container cells */
    McAABB *blocks;          /* per-env PSV_MAX_BLOCKS scratch (OpenMP-safe;
                              * same layout as the CUDA per-env pool) */
    unsigned long long *ops_pool;    /* CUENV_OP_TRACE=1: n * CU_OP_N activity
                                      * counters (NULL = tracing off) */
    CRRecipe recipes[CRF_NRECIPES];  /* crf_build once at create */
    int    nrecipes;
    double atk_gate;         /* opt-in +0.03 gate; 0 = off (exact ppo_coal) */
    int    success_item;     /* +10/done=1 item; 263 default (exact ppo_coal),
                              * 0 = disabled. Applied at reset. */
} CuVec;

/* OPT-IN training-reward mode: gate the +0.03 crosshair-attack bonus on
 * nearest-coal dist <= dist_gate. dist_gate <= 0 restores the default
 * (exact bitwise-gated ppo_coal semantics). */
int cuenv_set_reward_gate(void *vh, double dist_gate) {
    CuVec *v = (CuVec *)vh;
    if (!v) return -1;
    v->atk_gate = dist_gate;
    return 0;
}

/* OPT-IN chain-training mode: which inventory item id fires the in-kernel
 * +10/done=1 on count increase vs its at-reset baseline. 263 (default) =
 * exact legacy mine-coal semantics; 50 = torches (full chain); 0 = never
 * (trainer-side termination only). Applies to envs at their NEXT reset. */
int cuenv_set_success_item(void *vh, int item) {
    CuVec *v = (CuVec *)vh;
    if (!v || item < 0) return -1;
    v->success_item = item;
    return 0;
}

void *cuenv_create(int device, int n) {
    CuVec *v;
    int i;
    (void)device;
    if (n <= 0) return NULL;
    v = (CuVec *)calloc(1, sizeof *v);
    if (!v) return NULL;
    v->n = n;
    v->success_item = 263;
    mc_sin_table_init(&v->st);
    v->nrecipes = crf_build(v->recipes);
    v->envs = (CuEnv *)calloc((size_t)n, sizeof *v->envs);
    v->assign = (int *)calloc((size_t)n, sizeof *v->assign);
    v->cam_pool = (u16 *)malloc((size_t)n * CU_NPIX * sizeof *v->cam_pool);
    v->dep_pool = (u8 *)malloc((size_t)n * CU_NPIX);
    v->edg_pool = (u8 *)malloc((size_t)n * CU_NPIX);
    v->window_pool = (Chunk *)malloc((size_t)n * PSV_NCHUNKS *
                                     sizeof *v->window_pool);
    v->cand_pool = (CuCand *)malloc((size_t)n * CU_COAL_CAND *
                                    sizeof *v->cand_pool);
    v->cont_pool = (int *)malloc((size_t)n * CUENV_SNAP_MAX_CONT * 3 *
                                 sizeof *v->cont_pool);
    /* one AABB scratch slab per env so OpenMP workers never share it */
    v->blocks = (McAABB *)malloc((size_t)n * PSV_MAX_BLOCKS *
                                 sizeof *v->blocks);
    if (!v->envs || !v->assign || !v->cam_pool || !v->dep_pool ||
        !v->edg_pool || !v->window_pool || !v->cand_pool || !v->cont_pool ||
        !v->blocks) {
        cuenv_destroy(v);
        return NULL;
    }
    if (getenv("CUENV_OP_TRACE") && atoi(getenv("CUENV_OP_TRACE"))) {
        v->ops_pool = (unsigned long long *)calloc((size_t)n * CU_OP_N,
                                                   sizeof *v->ops_pool);
        if (!v->ops_pool) {
            cuenv_destroy(v);
            return NULL;
        }
    }
    for (i = 0; i < n; ++i) {
        CuEnv *e = &v->envs[i];
        e->cam = v->cam_pool + (size_t)i * CU_NPIX;
        e->dep = v->dep_pool + (size_t)i * CU_NPIX;
        e->edg = v->edg_pool + (size_t)i * CU_NPIX;
        e->window = v->window_pool + (size_t)i * PSV_NCHUNKS;
        e->coal_cand = v->cand_pool + (size_t)i * CU_COAL_CAND;
        e->cont = v->cont_pool + (size_t)i * CUENV_SNAP_MAX_CONT * 3;
        e->ops = v->ops_pool ? v->ops_pool + (size_t)i * CU_OP_N : NULL;
        v->assign[i] = -1;
    }
    return v;
}

/* op-trace readout: number of counters per env (buffer sizing) and the
 * n * CU_OP_N cumulative counters (row-major, env-major). Returns -1 when
 * tracing is off (CUENV_OP_TRACE unset at create). */
int cuenv_op_count(void) { return CU_OP_N; }

int cuenv_op_trace(void *vh, unsigned long long *out) {
    CuVec *v = (CuVec *)vh;
    if (!v || !out || !v->ops_pool) return -1;
    memcpy(out, v->ops_pool,
           (size_t)v->n * CU_OP_N * sizeof *v->ops_pool);
    return 0;
}

/* Size the region pools from the first-loaded snapshot's dims (all further
 * snapshots must match). Init-time only. */
static int cu_alloc_region_pools(CuVec *v, int rnx, int rny, int rnz) {
    int i;
    v->rnx = rnx; v->rny = rny; v->rnz = rnz;
    v->rvol = (long)rnx * rny * rnz;
    v->cells_pool = (u16 *)malloc((size_t)v->n * v->rvol *
                                  sizeof *v->cells_pool);
    v->camcells_pool = (u16 *)malloc((size_t)v->n * v->rvol *
                                     sizeof *v->camcells_pool);
    if (!v->cells_pool || !v->camcells_pool) return 0;
    for (i = 0; i < v->n; ++i) {
        v->envs[i].cells = v->cells_pool + (size_t)i * v->rvol;
        v->envs[i].cam_cells = v->camcells_pool + (size_t)i * v->rvol;
    }
    return 1;
}

void cuenv_destroy(void *vh) {
    CuVec *v = (CuVec *)vh;
    int i;
    if (!v) return;
    for (i = 0; i < v->nsnaps; ++i) cuenv_snapshot_free(&v->snaps[i]);
    free(v->envs); free(v->assign);
    free(v->cells_pool); free(v->camcells_pool);
    free(v->cam_pool); free(v->dep_pool); free(v->edg_pool);
    free(v->window_pool); free(v->cand_pool); free(v->cont_pool);
    free(v->blocks);
    free(v->ops_pool);
    free(v);
}

int cuenv_load_snapshots(void *vh, const char *const *paths, int count,
                         char *err, int err_cap) {
    CuVec *v = (CuVec *)vh;
    int i;
    if (!v || count < 0 || v->nsnaps + count > CUENV_MAX_SNAPS) return -1;
    for (i = 0; i < count; ++i) {
        const RlSnapHead *h;
        if (!cuenv_snapshot_load(paths[i], &v->snaps[v->nsnaps], err, err_cap))
            return -1;
        h = &v->snaps[v->nsnaps].head;
        if (h->rny > CU_RNY_MAX) {   /* window y>=128 air invariant */
            if (err && err_cap > 0)
                snprintf(err, (size_t)err_cap, "region rny %d > %d: %s",
                         h->rny, CU_RNY_MAX, paths[i]);
            cuenv_snapshot_free(&v->snaps[v->nsnaps]);
            return -1;
        }
        if (v->rvol == 0) {
            if (!cu_alloc_region_pools(v, h->rnx, h->rny, h->rnz)) {
                if (err && err_cap > 0)
                    snprintf(err, (size_t)err_cap,
                             "region pool alloc failed (%dx%dx%d x %d envs)",
                             h->rnx, h->rny, h->rnz, v->n);
                cuenv_snapshot_free(&v->snaps[v->nsnaps]);
                return -1;
            }
        } else if (h->rnx != v->rnx || h->rny != v->rny || h->rnz != v->rnz) {
            if (err && err_cap > 0)
                snprintf(err, (size_t)err_cap,
                         "region dims %dx%dx%d != pool %dx%dx%d: %s",
                         h->rnx, h->rny, h->rnz, v->rnx, v->rny, v->rnz,
                         paths[i]);
            cuenv_snapshot_free(&v->snaps[v->nsnaps]);
            return -1;
        }
        v->nsnaps++;
    }
    return v->nsnaps;
}

int cuenv_snapshot_has_liquid(void *vh, int snap) {
    CuVec *v = (CuVec *)vh;
    if (!v || snap < 0 || snap >= v->nsnaps) return -1;
    return v->snaps[snap].has_liquid;
}

int cuenv_assign(void *vh, const int *snap_idx) {
    CuVec *v = (CuVec *)vh;
    int i;
    if (!v || !snap_idx) return -1;
    for (i = 0; i < v->n; ++i) {
        if (snap_idx[i] < 0 || snap_idx[i] >= v->nsnaps) return -1;
        v->assign[i] = snap_idx[i];
    }
    return 0;
}

static void cu_reset_env(CuVec *v, int i) {
    const CuSnapshot *s = &v->snaps[v->assign[i]];
    cuenv_reset_from_snapshot(&v->envs[i], &s->head, s->items, s->cells,
                              s->coal, (int)s->ncoal, s->xy_off,
                              s->cont, s->ncont, v->success_item);
}

int cuenv_reset(void *vh, const unsigned char *mask) {
    CuVec *v = (CuVec *)vh;
    int i;
    if (!v) return -1;
    for (i = 0; i < v->n; ++i) {
        if (mask && !mask[i]) continue;
        if (v->assign[i] < 0) return -1;
        cu_reset_env(v, i);
    }
    return 0;
}

/* One trainer decision for every env: `repeat` game ticks with dyaw/dpitch
 * applied on sub-tick 0 only, camera rendered on the LAST sub-tick (the
 * "cam":0 economy). actions[i*12..] = the FULL raw action vector (see the
 * header comment / cuenv_tick_raw layout).
 * Outputs (any may be NULL): cam u16[n*2304], depth/edge u8[n*2304],
 * scal f32[n*6], rew f32[n] (summed over the repeat), done u8[n], pose
 * f32[n*5] = x,y,z,yaw,pitch (float world view). All logic lives in the
 * shared MC_HD core (cuenv_decision_ticks/_finalize) - the CUDA driver runs
 * the identical source with the camera moved to the per-pixel k_obs.
 * Parallelism: OpenMP over env index; each env uses its own AABB scratch. */
/* cuenv_step + an optional int32[n][CU_STATUS_K] status readout (the 9
 * rl_inv_ids counts, hotbar_sel, held item id, container) - everything the
 * milestone-chain trainer needs. status may be NULL (== legacy cuenv_step). */
int cuenv_step_full(void *vh, const double *actions, int repeat,
                    unsigned short *cam, unsigned char *depth,
                    unsigned char *edge, float *scal, float *rew,
                    unsigned char *done, float *pose, int *status) {
    CuVec *v = (CuVec *)vh;
    int i;
    if (!v || !actions || repeat < 1) return -1;
#pragma omp parallel for schedule(static) if(v->n > 1)
    for (i = 0; i < v->n; ++i) {
        CuEnv *e = &v->envs[i];
        McAABB *blocks = v->blocks + (size_t)i * PSV_MAX_BLOCKS;
        cuenv_decision_ticks(e, &v->st, &actions[i * CUENV_ACT_HEADS], repeat,
                             blocks, 1, v->atk_gate, v->recipes,
                             v->nrecipes);
        if (cam)   memcpy(cam + (size_t)i * CU_NPIX, e->cam,
                          CU_NPIX * sizeof *cam);
        if (depth) memcpy(depth + (size_t)i * CU_NPIX, e->dep, CU_NPIX);
        if (edge)  memcpy(edge + (size_t)i * CU_NPIX, e->edg, CU_NPIX);
        cuenv_decision_finalize(e, &v->st,
                                scal ? scal + (size_t)i * 6 : NULL,
                                rew ? rew + i : NULL,
                                done ? done + i : NULL,
                                pose ? pose + (size_t)i * 5 : NULL,
                                v->atk_gate);
        if (status) cuenv_fill_status(e, status + (size_t)i * CU_STATUS_K);
    }
    return 0;
}

int cuenv_step(void *vh, const double *actions, int repeat,
               unsigned short *cam, unsigned char *depth, unsigned char *edge,
               float *scal, float *rew, unsigned char *done, float *pose) {
    return cuenv_step_full(vh, actions, repeat, cam, depth, edge, scal, rew,
                           done, pose, NULL);
}

/* Capture a LIVE env's full state into snapshot slot `slot` (self-generated
 * start-state curriculum). slot may overwrite an existing snapshot or append
 * at nsnaps (dense growth). The slot inherits the env's current region cells
 * (post-edit world), static ore list and has-liquid flag. Rare host call -
 * the malloc here is outside every tick path. */
int cuenv_capture(void *vh, int env, int slot) {
    CuVec *v = (CuVec *)vh;
    CuEnv *e;
    CuSnapshot *s;
    if (!v || env < 0 || env >= v->n || slot < 0 ||
        slot >= CUENV_MAX_SNAPS || slot > v->nsnaps || v->rvol == 0)
        return -1;
    if (v->assign[env] < 0) return -1;
    e = &v->envs[env];
    s = &v->snaps[slot];
    if (slot == v->nsnaps) {
        memset(s, 0, sizeof *s);
        v->nsnaps++;
    }
    (void)cuenv_capture_head(e, &s->head, s->items);
    if (!s->cells) {
        s->cells = (unsigned short *)malloc((size_t)v->rvol *
                                            sizeof *s->cells);
        if (!s->cells) return -1;
    }
    memcpy(s->cells, e->cells, (size_t)v->rvol * sizeof *s->cells);
    if ((int)s->ncoal != e->nore) {
        free(s->coal);
        s->coal = e->nore ? (int *)malloc((size_t)e->nore * 3 *
                                          sizeof *s->coal) : NULL;
        if (e->nore && !s->coal) { s->ncoal = 0; return -1; }
        s->ncoal = (unsigned)e->nore;
    }
    if (e->nore)
        memcpy(s->coal, e->ore, (size_t)e->nore * 3 * sizeof *s->coal);
    {   /* the captured ore list IS the assign-source snapshot's (e->ore was
         * bound at reset and never mutates), so its spatial index carries
         * over verbatim. NULL source index -> NULL (full-scan fallback). */
        const int *src_xy = v->snaps[v->assign[env]].xy_off;
        size_t nb = ((size_t)v->rnx * v->rny + 1) * sizeof *s->xy_off;
        if (src_xy) {
            if (!s->xy_off) s->xy_off = (int *)malloc(nb);
            if (s->xy_off) memcpy(s->xy_off, src_xy, nb);
        } else {
            free(s->xy_off);
            s->xy_off = NULL;
        }
    }
    {   /* container list: the env's LIVE list is exactly the captured
         * region's (maintained on every edit); overflow (-1) rides along
         * and keeps the full-scan fallback. */
        if (!s->cont)
            s->cont = (int *)malloc((size_t)CUENV_SNAP_MAX_CONT * 3 *
                                    sizeof *s->cont);
        s->ncont = s->cont ? e->n_cont : -1;
        if (s->cont && e->n_cont > 0)
            memcpy(s->cont, e->cont,
                   (size_t)e->n_cont * 3 * sizeof *s->cont);
    }
    s->has_liquid = v->snaps[v->assign[env]].has_liquid;
    return 0;
}

/* ---- verify helpers ---- */

int cuenv_obs_size(void) { return (int)sizeof(CuBinObs); }

int cuenv_emit(void *vh, int env, int want_cam, void *out) {
    CuVec *v = (CuVec *)vh;
    if (!v || env < 0 || env >= v->n || !out) return -1;
    cuenv_emit_bolr(&v->envs[env], &v->st, (CuBinObs *)out, want_cam);
    return 0;
}

/* One raw tick mirroring the real env's action-line loop: a[13] =
 * {forward,strafe,dyaw,dpitch,jump,sneak,sprint,attack,use,hotbar,
 *  craft,interact,smelt} (craft = rl_crafts index or -1; interact/smelt =
 * 0/1). The discrete primitives are applied BEFORE the tick, in rl_mode's
 * order (craft, then interact, then smelt, then gm_runtime_tick). Then
 * emits the obs exactly as rl_emit_obs would (want_cam semantics included). */
int cuenv_tick_raw(void *vh, int env, const double a[13], int want_cam,
                   void *out) {
    CuVec *v = (CuVec *)vh;
    CuAction act;
    if (!v || env < -1 || env >= v->n || !a) return -1;
    if (env == -1) {   /* broadcast: same raw action to ALL envs, no obs */
        int i, rc = 0;
#pragma omp parallel for schedule(static) reduction(|:rc) if(v->n > 1)
        for (i = 0; i < v->n; ++i)
            rc |= cuenv_tick_raw(vh, i, a, 0, NULL);
        return rc;
    }
    memset(&act, 0, sizeof act);
    act.forward = (float)a[0];
    act.strafe = (float)a[1];
    act.dyaw = (float)a[2];
    act.dpitch = (float)a[3];
    act.jump = (int)a[4];
    act.sneak = (int)a[5];
    act.sprint = (int)a[6];
    act.attack = (int)a[7];
    act.use = (int)a[8];
    act.hotbar_sel = (int)a[9];
    if ((int)a[10] >= 0)
        (void)cuenv_do_craft(&v->envs[env], (int)a[10], v->recipes,
                             v->nrecipes);
    if ((int)a[11])
        (void)cuenv_do_interact(&v->envs[env]);
    if ((int)a[12])
        (void)cuenv_do_smelt(&v->envs[env]);
    cuenv_runtime_tick(&v->envs[env], &v->st, act,
                       v->blocks + (size_t)env * PSV_MAX_BLOCKS);
    if (out) cuenv_emit_bolr(&v->envs[env], &v->st, (CuBinObs *)out, want_cam);
    return 0;
}

/* Raw sim state for divergence bisecting: layout in cuenv_debug_fill. */
int cuenv_debug_state(void *vh, int env, double *out, int cap) {
    CuVec *v = (CuVec *)vh;
    if (!v || env < 0 || env >= v->n || !out || cap < 21) return -1;
    return cuenv_debug_fill(&v->envs[env], out);
}
