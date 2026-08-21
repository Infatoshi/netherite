/* blaze_cpu.c - batched CPU reference driver over blaze_core.h: N envs
 * stepped in parallel (OpenMP over env index) with the exact C ABI the CUDA
 * .so (M2) will export, so the trainer/verify scripts are backend-agnostic.
 * The `device` argument is accepted and ignored here. Envs are independent
 * (per-env region/window/cam/AABB scratch); shared tables (sin, recipes,
 * snapshot ore lists) are read-only after init. OMP_NUM_THREADS controls
 * width; OMP_NUM_THREADS=1 recovers the old serial path for bisect.
 *
 * ABI (all pointers caller-owned host memory in this backend):
 *   blaze_create(device, n, opts) -> handle  (opts NULL = defaults)
 *   blaze_load_snapshots(h, paths, count, err, cap) -> nloaded or -1
 *   blaze_snapshot_has_liquid(h, snap) -> 0/1 (snapshot requires BP_FLUIDS)
 *   blaze_assign(h, snap_idx[n])       -> per-env snapshot binding
 *   blaze_reset(h, mask[n] or NULL)
 *   blaze_step(h, actions[n][13] doubles, repeat, cam, depth, edge, scal,
 *              rew, done, pose)
 *     actions row = the FULL raw action vector (blaze_tick_raw layout):
 *     {forward,strafe,dyaw,dpitch,jump,sneak,sprint,attack,use,hotbar(-1),
 *      craft(-1),interact,smelt}; craft/interact/smelt fire once, pre-tick,
 *     before sub-tick 0. Legacy 5-head trainer actions are expanded to this
 *     layout in blaze.py (bit-identical decode).
 *   blaze_destroy(h)
 * Verify helpers (batch-of-1 lockstep vs the real magma_game):
 *   blaze_obs_size() -> sizeof(CuBinObs) == sizeof(RlBinObs)
 *   blaze_emit(h, env, want_cam, out)  -> BOLR-layout obs, no tick
 *   blaze_tick_raw(h, env, a[13], want_cam, out) -> one action line: craft/
 *       interact/smelt primitives then one gm_runtime_tick equivalent + obs;
 *       a = {forward,strafe,dyaw,dpitch,jump,sneak,sprint,attack,use,hotbar,
 *       craft(-1=none),interact,smelt}; env == -1 broadcasts the same action to
 *       ALL envs (no obs; out ignored) - the verify chain gate's batched
 *       lane stepper
 *   blaze_debug_state(h, env, out[32]) -> raw doubles for divergence bisect
 *
 * Reward/scalars (ppo_coal.py semantics) live in blaze_core.h as MC_HD code
 * (blaze_decision_ticks/blaze_decision_finalize) shared with the CUDA driver
 * - single source, so CPU and CUDA rewards are gated against each other. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <omp.h>

#include "blaze_core.h"
#include "blaze_abi.h"

#define BLAZE_MAX_SNAPS 128
#define BLAZE_ACT_HEADS 13
#define BLAZE_COMMON_CAPABILITIES ((unsigned long long)BP_IMPLEMENTED_MASK)

void blaze_destroy(void *vh);

typedef struct {
    int n;
    McSinTable st;
    Blaze *envs;
    int   *assign;
    CuSnapshot snaps[BLAZE_MAX_SNAPS];
    int    nsnaps;
    /* pooled per-env buffers. Region-sized pools (cells/light) are
     * allocated on the FIRST snapshot load - the region dims come from the
     * snapshot header; every loaded snapshot must share them. Still
     * init-time-only: nothing allocates in a tick path. */
    int    rnx, rny, rnz;    /* 0 until the first snapshot is loaded */
    long   rvol;
    u16   *cells_pool, *cam_pool;
    u16   *fluid_cur_pool, *fluid_tmp_pool;
    u16   *grass_pool;       /* per-env grass_sec census (CU_SEC_SPAN cube) */
    u8    *light_pool, *dep_pool, *edg_pool;
    Chunk *window_pool;
    CuCand *cand_pool;
    int   *cont_pool;        /* per-env BLAZE_SNAP_MAX_CONT container cells */
    McAABB *blocks;          /* per-env PSV_MAX_BLOCKS scratch (OpenMP-safe;
                              * same layout as the CUDA per-env pool) */
    unsigned long long *ops_pool;    /* op_trace=1: n * CU_OP_N activity
                                      * counters (NULL = tracing off) */
    CRRecipe recipes[CRF_NRECIPES];  /* crf_build once at create */
    int    nrecipes;
    double atk_gate;         /* opt-in +0.03 gate; 0 = off (exact ppo_coal) */
    int    success_item;     /* +10/done=1 item; 263 default (exact ppo_coal),
                              * 0 = disabled. Applied at reset. */
    int    no_ore_xy;        /* create-time: skip ore spatial index on load */
} CuVec;

/* OPT-IN training-reward mode: gate the +0.03 crosshair-attack bonus on
 * nearest-coal dist <= dist_gate. dist_gate <= 0 restores the default
 * (exact bitwise-gated ppo_coal semantics). */
int blaze_set_reward_gate(void *vh, double dist_gate) {
    CuVec *v = (CuVec *)vh;
    if (!v) return -1;
    v->atk_gate = dist_gate;
    return 0;
}

/* OPT-IN chain-training mode: which inventory item id fires the in-kernel
 * +10/done=1 on count increase vs its at-reset baseline. 263 (default) =
 * exact legacy mine-coal semantics; 50 = torches (full chain); 0 = never
 * (trainer-side termination only). Applies to envs at their NEXT reset. */
int blaze_set_success_item(void *vh, int item) {
    CuVec *v = (CuVec *)vh;
    if (!v || item < 0) return -1;
    v->success_item = item;
    return 0;
}

void *blaze_create(int device, int n, const BlazeCreateOpts *opts) {
    CuVec *v;
    int i;
    BlazeCreateOpts o;
    (void)device;
    if (n <= 0) return NULL;
    if (opts) o = *opts;
    else blaze_create_opts_default(&o);
    v = (CuVec *)calloc(1, sizeof *v);
    if (!v) return NULL;
    v->n = n;
    v->success_item = 263;
    v->no_ore_xy = o.no_ore_xy ? 1 : 0;
    mc_sin_table_init(&v->st);
    v->nrecipes = crf_build(v->recipes);
    v->envs = (Blaze *)calloc((size_t)n, sizeof *v->envs);
    v->assign = (int *)calloc((size_t)n, sizeof *v->assign);
    v->cam_pool = (u16 *)malloc((size_t)n * CU_NPIX * sizeof *v->cam_pool);
    v->dep_pool = (u8 *)malloc((size_t)n * CU_NPIX);
    v->edg_pool = (u8 *)malloc((size_t)n * CU_NPIX);
    v->window_pool = (Chunk *)malloc((size_t)n * PSV_NCHUNKS *
                                     sizeof *v->window_pool);
    v->cand_pool = (CuCand *)malloc((size_t)n * CU_COAL_CAND *
                                    sizeof *v->cand_pool);
    v->cont_pool = (int *)malloc((size_t)n * BLAZE_SNAP_MAX_CONT * 3 *
                                 sizeof *v->cont_pool);
    /* one AABB scratch slab per env so OpenMP workers never share it */
    v->blocks = (McAABB *)malloc((size_t)n * PSV_MAX_BLOCKS *
                                 sizeof *v->blocks);
    v->fluid_cur_pool = (u16 *)malloc((size_t)n * CU_FLUID_VOL *
                                      sizeof *v->fluid_cur_pool);
    v->fluid_tmp_pool = (u16 *)malloc((size_t)n * CU_FLUID_VOL *
                                      sizeof *v->fluid_tmp_pool);
    if (!v->envs || !v->assign || !v->cam_pool || !v->dep_pool ||
        !v->edg_pool || !v->window_pool || !v->cand_pool || !v->cont_pool ||
        !v->blocks || !v->fluid_cur_pool || !v->fluid_tmp_pool) {
        blaze_destroy(v);
        return NULL;
    }
    if (o.op_trace) {
        v->ops_pool = (unsigned long long *)calloc((size_t)n * CU_OP_N,
                                                   sizeof *v->ops_pool);
        if (!v->ops_pool) {
            blaze_destroy(v);
            return NULL;
        }
    }
    for (i = 0; i < n; ++i) {
        Blaze *e = &v->envs[i];
        e->cam = v->cam_pool + (size_t)i * CU_NPIX;
        e->dep = v->dep_pool + (size_t)i * CU_NPIX;
        e->edg = v->edg_pool + (size_t)i * CU_NPIX;
        e->window = v->window_pool + (size_t)i * PSV_NCHUNKS;
        e->coal_cand = v->cand_pool + (size_t)i * CU_COAL_CAND;
        e->cont = v->cont_pool + (size_t)i * BLAZE_SNAP_MAX_CONT * 3;
        e->fluid_cur = v->fluid_cur_pool + (size_t)i * CU_FLUID_VOL;
        e->fluid_tmp = v->fluid_tmp_pool + (size_t)i * CU_FLUID_VOL;
        e->ops = v->ops_pool ? v->ops_pool + (size_t)i * CU_OP_N : NULL;
        v->assign[i] = -1;
    }
    return v;
}

/* op-trace readout: number of counters per env (buffer sizing) and the
 * n * CU_OP_N cumulative counters (row-major, env-major). Returns -1 when
 * tracing is off (op_trace was 0 at create). */
int blaze_op_count(void) { return CU_OP_N; }

int blaze_op_trace(void *vh, unsigned long long *out) {
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
    /* worst-case section grid for these dims over any region origin */
    long nsec = (long)CU_SEC_SPAN(rnx) * CU_SEC_SPAN(rny) * CU_SEC_SPAN(rnz);
    v->rnx = rnx; v->rny = rny; v->rnz = rnz;
    v->rvol = (long)rnx * rny * rnz;
    v->cells_pool = (u16 *)malloc((size_t)v->n * v->rvol *
                                  sizeof *v->cells_pool);
    v->light_pool = (u8 *)malloc((size_t)v->n * v->rvol);
    v->grass_pool = (u16 *)malloc((size_t)v->n * nsec * sizeof *v->grass_pool);
    if (!v->cells_pool || !v->light_pool || !v->grass_pool)
        return 0;
    for (i = 0; i < v->n; ++i) {
        v->envs[i].cells = v->cells_pool + (size_t)i * v->rvol;
        v->envs[i].light = v->light_pool + (size_t)i * v->rvol;
        v->envs[i].grass_sec = v->grass_pool + (size_t)i * nsec;
    }
    return 1;
}

void blaze_destroy(void *vh) {
    CuVec *v = (CuVec *)vh;
    int i;
    if (!v) return;
    for (i = 0; i < v->nsnaps; ++i) blaze_snapshot_free(&v->snaps[i]);
    free(v->envs); free(v->assign);
    free(v->cells_pool); free(v->light_pool);
    free(v->grass_pool);
    free(v->cam_pool); free(v->dep_pool); free(v->edg_pool);
    free(v->window_pool); free(v->cand_pool); free(v->cont_pool);
    free(v->blocks);
    free(v->fluid_cur_pool); free(v->fluid_tmp_pool);
    free(v->ops_pool);
    free(v);
}

int blaze_load_snapshots(void *vh, const char *const *paths, int count,
                         char *err, int err_cap) {
    CuVec *v = (CuVec *)vh;
    int i;
    if (!v || count < 0 || v->nsnaps + count > BLAZE_MAX_SNAPS) return -1;
    for (i = 0; i < count; ++i) {
        const RlSnapHead *h;
        if (!blaze_snapshot_load(paths[i], &v->snaps[v->nsnaps], err, err_cap,
                                 v->no_ore_xy))
            return -1;
        h = &v->snaps[v->nsnaps].head;
        if (h->rny > CU_RNY_MAX) {   /* window y>=128 air invariant */
            if (err && err_cap > 0)
                snprintf(err, (size_t)err_cap, "region rny %d > %d: %s",
                         h->rny, CU_RNY_MAX, paths[i]);
            blaze_snapshot_free(&v->snaps[v->nsnaps]);
            return -1;
        }
        if (v->rvol == 0) {
            if (!cu_alloc_region_pools(v, h->rnx, h->rny, h->rnz)) {
                if (err && err_cap > 0)
                    snprintf(err, (size_t)err_cap,
                             "region pool alloc failed (%dx%dx%d x %d envs)",
                             h->rnx, h->rny, h->rnz, v->n);
                blaze_snapshot_free(&v->snaps[v->nsnaps]);
                return -1;
            }
        } else if (h->rnx != v->rnx || h->rny != v->rny || h->rnz != v->rnz) {
            if (err && err_cap > 0)
                snprintf(err, (size_t)err_cap,
                         "region dims %dx%dx%d != pool %dx%dx%d: %s",
                         h->rnx, h->rny, h->rnz, v->rnx, v->rny, v->rnz,
                         paths[i]);
            blaze_snapshot_free(&v->snaps[v->nsnaps]);
            return -1;
        }
        v->nsnaps++;
    }
    return v->nsnaps;
}

int blaze_parity_size(void) { return (int)sizeof(BpParityRecord); }

unsigned long long blaze_capabilities(void) {
    return BLAZE_COMMON_CAPABILITIES;
}

unsigned long long blaze_snapshot_requirements(void *vh, int snap) {
    CuVec *v = (CuVec *)vh;
    unsigned long long requirements = 0;
    if (!v || snap < 0 || snap >= v->nsnaps) return ~0ULL;
    if (v->snaps[snap].has_liquid)
        requirements |= (unsigned long long)BP_BIT(BP_FLUIDS);
    if (v->snaps[snap].head.container != 0 || !v->snaps[snap].light)
        requirements |= (unsigned long long)BP_REQ_UNREPRESENTED_SNAPSHOT;
    return requirements;
}

int blaze_snapshot_has_liquid(void *vh, int snap) {
    CuVec *v = (CuVec *)vh;
    if (!v || snap < 0 || snap >= v->nsnaps) return -1;
    return v->snaps[snap].has_liquid;
}

int blaze_assign(void *vh, const int *snap_idx) {
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
    blaze_reset_from_snapshot(&v->envs[i], &s->head, s->items, s->cells,
                              s->light, s->coal, (int)s->ncoal, s->xy_off,
                              s->cont, s->ncont, v->success_item);
}

int blaze_reset(void *vh, const unsigned char *mask) {
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
 * header comment / blaze_tick_raw layout).
 * Outputs (any may be NULL): cam u16[n*2304], depth/edge u8[n*2304],
 * scal f32[n*6], rew f32[n] (summed over the repeat), done u8[n], pose
 * f32[n*5] = x,y,z,yaw,pitch (float world view). All logic lives in the
 * shared MC_HD core (blaze_decision_ticks/_finalize) - the CUDA driver runs
 * the identical source with the camera moved to the per-pixel k_obs.
 * Parallelism: OpenMP over env index; each env uses its own AABB scratch. */
/* blaze_step + an optional int32[n][CU_STATUS_K] status readout (the 9
 * rl_inv_ids counts, hotbar_sel, held item id, container) - everything the
 * milestone-chain trainer needs. status may be NULL (== legacy blaze_step). */
int blaze_step_full(void *vh, const double *actions, int repeat,
                    unsigned short *cam, unsigned char *depth,
                    unsigned char *edge, float *scal, float *rew,
                    unsigned char *done, float *pose, int *status) {
    CuVec *v = (CuVec *)vh;
    int i;
    if (!v || !actions || repeat < 1) return -1;
#pragma omp parallel for schedule(static) if(v->n > 1)
    for (i = 0; i < v->n; ++i) {
        Blaze *e = &v->envs[i];
        McAABB *blocks = v->blocks + (size_t)i * PSV_MAX_BLOCKS;
        blaze_decision_ticks(e, &v->st, &actions[i * BLAZE_ACT_HEADS], repeat,
                             blocks, 1, v->atk_gate, v->recipes,
                             v->nrecipes);
        if (cam)   memcpy(cam + (size_t)i * CU_NPIX, e->cam,
                          CU_NPIX * sizeof *cam);
        if (depth) memcpy(depth + (size_t)i * CU_NPIX, e->dep, CU_NPIX);
        if (edge)  memcpy(edge + (size_t)i * CU_NPIX, e->edg, CU_NPIX);
        blaze_decision_finalize(e, &v->st,
                                scal ? scal + (size_t)i * 6 : NULL,
                                rew ? rew + i : NULL,
                                done ? done + i : NULL,
                                pose ? pose + (size_t)i * 5 : NULL,
                                v->atk_gate);
        if (status) blaze_fill_status(e, status + (size_t)i * CU_STATUS_K);
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
 * start-state curriculum). slot may overwrite an existing snapshot or append
 * at nsnaps (dense growth). The slot inherits the env's current region cells
 * (post-edit world), static ore list and has-liquid flag. Rare host call -
 * the malloc here is outside every tick path. */
int blaze_capture(void *vh, int env, int slot) {
    CuVec *v = (CuVec *)vh;
    Blaze *e;
    CuSnapshot *s;
    if (!v || env < 0 || env >= v->n || slot < 0 ||
        slot >= BLAZE_MAX_SNAPS || slot > v->nsnaps || v->rvol == 0)
        return -1;
    if (v->assign[env] < 0) return -1;
    e = &v->envs[env];
    s = &v->snaps[slot];
    if (slot == v->nsnaps) {
        memset(s, 0, sizeof *s);
        v->nsnaps++;
    }
    (void)blaze_capture_head(e, &s->head, s->items);
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
         * and keeps the full-scan fallback. Slot buffer is always
         * BLAZE_SNAP_MAX_CONT (same as load); do not memcpy a torn n_cont. */
        if (e->n_cont < -1 || e->n_cont > BLAZE_SNAP_MAX_CONT)
            return -1;
        if (e->n_cont >= 0 && !s->cont)
            s->cont = (int *)malloc((size_t)BLAZE_SNAP_MAX_CONT * 3 *
                                    sizeof *s->cont);
        s->ncont = (e->n_cont < 0 || !s->cont) ? -1 : e->n_cont;
        if (s->cont && e->n_cont > 0)
            memcpy(s->cont, e->cont,
                   (size_t)e->n_cont * 3 * sizeof *s->cont);
    }
    s->has_liquid = v->snaps[v->assign[env]].has_liquid;
    return 0;
}

/* ---- verify helpers ---- */

int blaze_obs_size(void) { return (int)sizeof(CuBinObs); }

/* Export the exact region + eye + yaw/pitch blaze_render_cam_pixel feeds to
 * oc_pixel. cells points into the env's packed-state cells pool ((id<<4)|meta,
 * the OcRegion contract; valid until the next mutating tick). Used by the
 * Metal k_obs parity gate; no sim logic. */
int blaze_obs_cam_inputs(void *vh, int env,
                         double *ex, double *ey, double *ez,
                         float *yaw, float *pitch,
                         int *x0, int *y0, int *z0,
                         int *nx, int *ny, int *nz,
                         const unsigned short **cells) {
    CuVec *v = (CuVec *)vh;
    Blaze *e;
    if (!v || env < 0 || env >= v->n) return -1;
    e = &v->envs[env];
    if (!e->cells || e->rnx <= 0 || e->rny <= 0 || e->rnz <= 0) return -1;
    if (ex) *ex = e->pl.ent.posX + (double)e->ox;
    if (ey) *ey = e->pl.ent.posY + PSV_EYE_HEIGHT;
    if (ez) *ez = e->pl.ent.posZ + (double)e->oz;
    if (yaw) *yaw = e->pl.yaw;
    if (pitch) *pitch = e->pl.pitch;
    if (x0) *x0 = e->rx0;
    if (y0) *y0 = e->ry0;
    if (z0) *z0 = e->rz0;
    if (nx) *nx = e->rnx;
    if (ny) *ny = e->rny;
    if (nz) *nz = e->rnz;
    if (cells) *cells = e->cells;
    return 0;
}

int blaze_emit(void *vh, int env, int want_cam, void *out) {
    CuVec *v = (CuVec *)vh;
    if (!v || env < 0 || env >= v->n || !out) return -1;
    blaze_emit_bolr(&v->envs[env], &v->st, (CuBinObs *)out, want_cam);
    return 0;
}

/* One raw tick mirroring the real env's action-line loop: a[13] =
 * {forward,strafe,dyaw,dpitch,jump,sneak,sprint,attack,use,hotbar,
 *  craft,interact,smelt} (craft = rl_crafts index or -1; interact/smelt =
 * 0/1). The discrete primitives are applied BEFORE the tick, in rl_mode's
 * order (craft, then interact, then smelt, then gm_runtime_tick). Then
 * emits the obs exactly as rl_emit_obs would (want_cam semantics included). */
int blaze_tick_raw(void *vh, int env, const double a[13], int want_cam,
                   void *out) {
    CuVec *v = (CuVec *)vh;
    CuAction act;
    if (!v || env < -1 || env >= v->n || !a) return -1;
    if (env == -1) {   /* broadcast: same raw action to ALL envs, no obs */
        int i, rc = 0;
#pragma omp parallel for schedule(static) reduction(|:rc) if(v->n > 1)
        for (i = 0; i < v->n; ++i)
            rc |= blaze_tick_raw(vh, i, a, 0, NULL);
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
        (void)blaze_do_craft(&v->envs[env], (int)a[10], v->recipes,
                             v->nrecipes);
    if ((int)a[11])
        (void)blaze_do_interact(&v->envs[env]);
    if ((int)a[12])
        (void)blaze_do_smelt(&v->envs[env]);
    blaze_runtime_tick(&v->envs[env], &v->st, act,
                       v->blocks + (size_t)env * PSV_MAX_BLOCKS);
    if (out) blaze_emit_bolr(&v->envs[env], &v->st, (CuBinObs *)out, want_cam);
    return 0;
}

/* Raw sim state for divergence bisecting: layout in blaze_debug_fill. */
int blaze_debug_state(void *vh, int env, double *out, int cap) {
    CuVec *v = (CuVec *)vh;
    if (!v || env < 0 || env >= v->n || !out || cap < 21) return -1;
    return blaze_debug_fill(&v->envs[env], out);
}

int blaze_parity_state(void *vh, int env, void *out) {
    CuVec *v = (CuVec *)vh;
    if (!v || env < 0 || env >= v->n || !out) return -1;
    blaze_parity_fill(&v->envs[env], (BpParityRecord *)out);
    return 0;
}
