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
 *   blaze_tick_raw(h, env, a[17], want_cam, out) -> one action line: craft/
 *       interact/smelt primitives then one gm_runtime_tick equivalent + obs;
 *       a = {forward,strafe,dyaw,dpitch,jump,sneak,sprint,attack,use,hotbar,
 *       craft(-1=none),interact,smelt,inv_click,inv_slot,inv_button,inv_type};
 *       env == -1 broadcasts the same action to ALL envs (no obs; out ignored)
 *       - the verify chain gate's batched lane stepper. Trainer blaze_step
 *       stays 13-wide (inv_click=0).
 *   blaze_tick(h, env, a[17], want_cam, out) -> same ABI, production path:
 *       blaze_decision_begin + inv_click + cu_recenter + one
 *       blaze_decision_subtick (CPU serial reference for CUDA k_tick /
 *       k_tick_warp). Kernel pick on CUDA is create opts.warp_tick
 *       (blaze_abi.h / blaze.conf / ppo.conf; default 1 = warp).
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
    CuSnapshot dim_snaps[3];
    int    dim_loaded[3];
    /* capture may replace coal/xy_off while live envs still alias the old
     * buffer (env->ore / env->ore_xy bind by pointer at reset). Do not
     * free those until destroy. */
    void  *retired[BLAZE_MAX_SNAPS * 4];
    int    nretired;
    /* pooled per-env buffers. Region-sized pools (cells/light) are
     * allocated on the FIRST snapshot load - the region dims come from the
     * snapshot header; every loaded snapshot must share them. Still
     * init-time-only: nothing allocates in a tick path. */
    int    rnx, rny, rnz;    /* 0 until the first snapshot is loaded */
    long   rvol;
    u16   *cells_pool, *cam_pool;
    u16   *fluid_cur_pool, *fluid_tmp_pool;
    u16   *grass_pool;       /* per-env grass_sec census (CU_SEC_SPAN cube) */
    int   *rt_leaf_pool;     /* per-env BlockLeaves surroundings[32768] */
    int   *light_q_pool;     /* per-env CU_LIGHT_Q BLOCK flood queue */
    Pf12  *pf_pool;          /* per-env PathFinder scratch, 1.09 MiB each.
                              * Allocated on the first det_entity_rng enable,
                              * never on the default path. */
    u8    *light_pool, *biome_pool, *dep_pool, *edg_pool;
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
    char   nether_bank_path[1024]; /* create opts; empty = unset */
    char   end_bank_path[1024];    /* create opts; empty = unset */
    int    mobs_enabled;     /* magma --mobs on: hostile AI/combat live tick */
    int    natural_spawn;    /* WorldEntitySpawner MONSTER cycle */
    int    natural_spawn_passive;
    i64    world_time_pin;   /* -1 unset; else ww.worldTime after reset */
    int    elytra_kit;       /* magma --elytra on: chest 443 after reset */
    int    det_entity_rng;   /* detmob Java EntityAITasks + PathFinder A* */
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

int blaze_set_mobs_enabled(void *vh, int on) {
    CuVec *v = (CuVec *)vh;
    int i;
    if (!v) return -1;
    v->mobs_enabled = on ? 1 : 0;
    if (v->envs)
        for (i = 0; i < v->n; ++i)
            v->envs[i].mobs_enabled = v->mobs_enabled;
    return 0;
}

int blaze_set_det_entity_rng(void *vh, int on) {
    CuVec *v = (CuVec *)vh;
    int i;
    if (!v) return -1;
    if (on && !v->pf_pool) {
        /* One A* scratch per env, allocated the first time the det path is
         * asked for. Fail closed: the caller (verify_cpu.py) raises. */
        v->pf_pool = (Pf12 *)calloc((size_t)v->n, sizeof *v->pf_pool);
        if (!v->pf_pool) return -1;
    }
    v->det_entity_rng = on ? 1 : 0;
    if (v->envs)
        for (i = 0; i < v->n; ++i) {
            v->envs[i].det_entity_rng = v->det_entity_rng;
            v->envs[i].pf = v->pf_pool ? &v->pf_pool[i] : NULL;
        }
    return 0;
}

int blaze_set_natural_spawn(void *vh, int on) {
    CuVec *v = (CuVec *)vh;
    int i;
    if (!v) return -1;
    v->natural_spawn = on ? 1 : 0;
    if (v->envs)
        for (i = 0; i < v->n; ++i)
            v->envs[i].natural_spawn = v->natural_spawn;
    return 0;
}

int blaze_set_natural_spawn_passive(void *vh, int on) {
    CuVec *v = (CuVec *)vh;
    int i;
    if (!v) return -1;
    v->natural_spawn_passive = on ? 1 : 0;
    if (v->envs)
        for (i = 0; i < v->n; ++i)
            v->envs[i].natural_spawn_passive = v->natural_spawn_passive;
    return 0;
}

int blaze_set_world_time(void *vh, long long world_time) {
    CuVec *v = (CuVec *)vh;
    int i;
    if (!v) return -1;
    v->world_time_pin = world_time;
    if (v->envs)
        for (i = 0; i < v->n; ++i)
            v->envs[i].ww.worldTime = world_time;
    return 0;
}

int blaze_set_elytra_enabled(void *vh, int on) {
    CuVec *v = (CuVec *)vh;
    int i;
    if (!v) return -1;
    v->elytra_kit = on ? 1 : 0;
    if (v->envs)
        for (i = 0; i < v->n; ++i) {
            v->envs[i].elytra_kit = v->elytra_kit;
            if (v->elytra_kit) {
                isr_set_stack(&v->envs[i].pl.inv, ISR_ARMOR_CHEST,
                              ic_mk(ISR_ELYTRA_ITEM, 1, 0));
                v->envs[i].pl.elytra_equipped = 1;
            }
        }
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
    v->world_time_pin = -1;
    v->no_ore_xy = o.no_ore_xy ? 1 : 0;
    v->nether_bank_path[0] = 0;
    v->end_bank_path[0] = 0;
    if (o.nether_bank && o.nether_bank[0])
        snprintf(v->nether_bank_path, sizeof v->nether_bank_path, "%s",
                 o.nether_bank);
    if (o.end_bank && o.end_bank[0])
        snprintf(v->end_bank_path, sizeof v->end_bank_path, "%s", o.end_bank);
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
    v->rt_leaf_pool = (int *)malloc((size_t)n * RT_LIVE_SURR *
                                    sizeof *v->rt_leaf_pool);
    v->light_q_pool = (int *)malloc((size_t)n * CU_LIGHT_Q *
                                    sizeof *v->light_q_pool);
    if (!v->envs || !v->assign || !v->cam_pool || !v->dep_pool ||
        !v->edg_pool || !v->window_pool || !v->cand_pool || !v->cont_pool ||
        !v->blocks || !v->fluid_cur_pool || !v->fluid_tmp_pool ||
        !v->rt_leaf_pool || !v->light_q_pool) {
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
        e->rt_leaf = v->rt_leaf_pool + (size_t)i * RT_LIVE_SURR;
        e->light_q = v->light_q_pool + (size_t)i * CU_LIGHT_Q;
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
    v->biome_pool = (u8 *)malloc((size_t)v->n * (size_t)rnx * (size_t)rnz);
    v->grass_pool = (u16 *)malloc((size_t)v->n * nsec * sizeof *v->grass_pool);
    if (!v->cells_pool || !v->light_pool || !v->biome_pool || !v->grass_pool)
        return 0;
    for (i = 0; i < v->n; ++i) {
        v->envs[i].cells = v->cells_pool + (size_t)i * v->rvol;
        v->envs[i].light = v->light_pool + (size_t)i * v->rvol;
        v->envs[i].biome = v->biome_pool + (size_t)i * (size_t)rnx * (size_t)rnz;
        v->envs[i].grass_sec = v->grass_pool + (size_t)i * nsec;
    }
    return 1;
}

static void cpu_retire(CuVec *v, void *p) {
    if (!p || !v) return;
    if (v->nretired < (int)(sizeof v->retired / sizeof v->retired[0]))
        v->retired[v->nretired++] = p;
}

void blaze_destroy(void *vh) {
    CuVec *v = (CuVec *)vh;
    int i;
    if (!v) return;
    for (i = 0; i < v->nsnaps; ++i) blaze_snapshot_free(&v->snaps[i]);
    if (v->dim_loaded[0]) blaze_snapshot_free(&v->dim_snaps[0]);
    if (v->dim_loaded[2]) blaze_snapshot_free(&v->dim_snaps[2]);
    for (i = 0; i < v->nretired; ++i) free(v->retired[i]);
    free(v->envs); free(v->assign);
    free(v->cells_pool); free(v->light_pool); free(v->biome_pool);
    free(v->grass_pool);
    free(v->cam_pool); free(v->dep_pool); free(v->edg_pool);
    free(v->window_pool); free(v->cand_pool); free(v->cont_pool);
    free(v->blocks);
    free(v->fluid_cur_pool); free(v->fluid_tmp_pool);
    free(v->rt_leaf_pool);
    free(v->light_q_pool);
    free(v->pf_pool);
    free(v->ops_pool);
    free(v);
}

static void cu_trim_inplace(char *s) {
    char *a = s, *e;
    if (!s) return;
    while (*a == ' ' || *a == '\t' || *a == '\n' || *a == '\r') a++;
    if (a != s) memmove(s, a, strlen(a) + 1);
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' ||
                     e[-1] == '\n' || e[-1] == '\r'))
        *--e = 0;
}

static int cu_join_snap_dir(char *out, size_t cap, const char *snap,
                            const char *rel) {
    const char *slash;
    size_t n, rl;
    if (!out || cap == 0 || !rel || !rel[0]) return -1;
    rl = strlen(rel);
    if (rel[0] == '/') {
        if (rl + 1 > cap) return -1;
        memcpy(out, rel, rl + 1);
        return 0;
    }
    slash = snap ? strrchr(snap, '/') : NULL;
    if (!slash) {
        if (rl + 1 > cap) return -1;
        memcpy(out, rel, rl + 1);
        return 0;
    }
    n = (size_t)(slash - snap);
    if (n + 1 + rl + 1 > cap) return -1;
    memcpy(out, snap, n);
    out[n] = '/';
    memcpy(out + n + 1, rel, rl + 1);
    return 0;
}

/* Sidecar PATH.banks names sibling nether/end banks. Missing sidecar is
 * not an error; a named path that cannot be resolved is. */
static int cu_read_banks_sidecar(const char *snap_path,
                                 char *nether_out, size_t ncap,
                                 char *end_out, size_t ecap,
                                 char *err, int err_cap) {
    char side[1080];
    char line[1024];
    FILE *f;
    if (!snap_path || !snap_path[0]) return 0;
    if (snprintf(side, sizeof side, "%s.banks", snap_path) >= (int)sizeof side)
        return 0;
    f = fopen(side, "r");
    if (!f) return 0;
    while (fgets(line, (int)sizeof line, f)) {
        char *sep, *key, *val, resolved[1024];
        cu_trim_inplace(line);
        if (!line[0] || line[0] == '#') continue;
        sep = strchr(line, '=');
        if (sep) {
            *sep = 0;
            key = line;
            val = sep + 1;
        } else {
            key = line;
            val = line;
            while (*val && *val != ' ' && *val != '\t') val++;
            if (*val) *val++ = 0;
            else val = (char *)"";
        }
        cu_trim_inplace(key);
        cu_trim_inplace(val);
        if (!val[0]) continue;
        if (cu_join_snap_dir(resolved, sizeof resolved, snap_path, val) != 0) {
            fclose(f);
            if (err && err_cap > 0)
                snprintf(err, (size_t)err_cap,
                         "dimension bank path too long in %s", side);
            return -1;
        }
        if (!strcmp(key, "nether") && nether_out && ncap && !nether_out[0]) {
            if (strlen(resolved) + 1 > ncap) {
                fclose(f);
                if (err && err_cap > 0)
                    snprintf(err, (size_t)err_cap,
                             "nether bank path too long in %s", side);
                return -1;
            }
            memcpy(nether_out, resolved, strlen(resolved) + 1);
        } else if (!strcmp(key, "end") && end_out && ecap && !end_out[0]) {
            if (strlen(resolved) + 1 > ecap) {
                fclose(f);
                if (err && err_cap > 0)
                    snprintf(err, (size_t)err_cap,
                             "end bank path too long in %s", side);
                return -1;
            }
            memcpy(end_out, resolved, strlen(resolved) + 1);
        }
    }
    fclose(f);
    return 0;
}

static int cu_load_named_bank(CuVec *v, int bank_idx, const char *path,
                              const char *label, char *err, int err_cap) {
    FILE *tf;
    const RlSnapHead *h;
    if (!v || !path || !path[0]) return 0;
    tf = fopen(path, "rb");
    if (!tf) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "%s bank missing: %s",
                     label ? label : "dimension", path);
        return -1;
    }
    fclose(tf);
    if (v->rvol == 0) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap,
                     "%s bank requires an overworld snapshot first: %s",
                     label ? label : "dimension", path);
        return -1;
    }
    if (v->dim_loaded[bank_idx]) {
        blaze_snapshot_free(&v->dim_snaps[bank_idx]);
        v->dim_loaded[bank_idx] = 0;
        memset(&v->dim_snaps[bank_idx], 0, sizeof v->dim_snaps[bank_idx]);
    }
    if (!blaze_snapshot_load(path, &v->dim_snaps[bank_idx], err, err_cap,
                             v->no_ore_xy))
        return -1;
    h = &v->dim_snaps[bank_idx].head;
    if (h->rny > CU_RNY_MAX) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "region rny %d > %d: %s",
                     h->rny, CU_RNY_MAX, path);
        blaze_snapshot_free(&v->dim_snaps[bank_idx]);
        memset(&v->dim_snaps[bank_idx], 0, sizeof v->dim_snaps[bank_idx]);
        return -1;
    }
    if (h->rnx != v->rnx || h->rny != v->rny || h->rnz != v->rnz) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap,
                     "%s bank region dims %dx%dx%d != pool %dx%dx%d: %s",
                     label ? label : "dimension",
                     h->rnx, h->rny, h->rnz, v->rnx, v->rny, v->rnz, path);
        blaze_snapshot_free(&v->dim_snaps[bank_idx]);
        memset(&v->dim_snaps[bank_idx], 0, sizeof v->dim_snaps[bank_idx]);
        return -1;
    }
    v->dim_loaded[bank_idx] = 1;
    return 0;
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
    if (v->nsnaps > 0) {
        char sc_nether[1024];
        char sc_end[1024];
        const char *nether;
        const char *endb;
        sc_nether[0] = 0;
        sc_end[0] = 0;
        if (count > 0 && paths[0] &&
            cu_read_banks_sidecar(paths[0], sc_nether, sizeof sc_nether,
                                  sc_end, sizeof sc_end, err, err_cap) != 0)
            return -1;
        nether = v->nether_bank_path[0] ? v->nether_bank_path : sc_nether;
        endb = v->end_bank_path[0] ? v->end_bank_path : sc_end;
        if (nether[0] &&
            cu_load_named_bank(v, 0, nether, "nether", err, err_cap) != 0)
            return -1;
        if (endb[0] &&
            cu_load_named_bank(v, 2, endb, "end", err, err_cap) != 0)
            return -1;
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
    /* v1 snapshots omit the light plane by format. That is not the
     * open-container gap this bit names; capture already flags only
     * container != 0. A v2+ file with no light fails load. */
    if (v->snaps[snap].head.container != 0)
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
                              s->cont, s->ncont, s->mobs, s->n_mobs,
                              s->orbs, s->n_orbs, s->biome,
                              s->world_rand_seed, v->success_item);
    v->envs[i].dim_bank = v->dim_snaps;
    v->envs[i].dim_ow = &v->snaps[v->assign[i]];
    v->envs[i].dimension = 0;
    v->envs[i].pl.fire = s->player_fire;
    v->envs[i].pl.air = s->player_air;
    {
        int k, n = s->n_potions;
        if (n < 0) n = 0;
        if (n > PSV_POTION_MAX) n = PSV_POTION_MAX;
        psv_potion_clear(&v->envs[i].pl);
        v->envs[i].pl.n_potions = n;
        for (k = 0; k < n; ++k) {
            v->envs[i].pl.potions[k].id = s->potions[k].id;
            v->envs[i].pl.potions[k].amplifier = s->potions[k].amplifier;
            v->envs[i].pl.potions[k].duration = s->potions[k].duration;
            v->envs[i].pl.potions[k].ambient = s->potions[k].ambient;
            v->envs[i].pl.potions[k].show_particles = s->potions[k].show_particles;
        }
    }
    v->envs[i].update_lcg = s->update_lcg;
    if (s->head.version >= BLAZE_SNAP_VERSION_RESUME) {
        v->envs[i].ww.totalTime = s->ww_total_time;
        v->envs[i].ww.worldTime = s->ww_world_time;
        v->envs[i].ww.rainTime = s->ww_rain_time;
        v->envs[i].ww.thunderTime = s->ww_thunder_time;
        v->envs[i].ww.raining = s->ww_raining ? 1 : 0;
        v->envs[i].ww.thundering = s->ww_thundering ? 1 : 0;
        v->envs[i].ww.rand.seed = s->ww_rand_seed48 & MC_JR_MASK;
        v->envs[i].parity_rt_mutations = s->rt_mutations;
        {
            unsigned pi, fi;
            Blaze *e = &v->envs[i];
            memset(e->projectiles, 0, sizeof e->projectiles);
            memset(e->proj_in_ground, 0, sizeof e->proj_in_ground);
            memset(e->proj_shake, 0, sizeof e->proj_shake);
            memset(e->proj_pickup, 0, sizeof e->proj_pickup);
            memset(e->proj_ground_ticks, 0, sizeof e->proj_ground_ticks);
            e->parity_proj_hits = s->parity_proj_hits;
            for (pi = 0; pi < s->n_proj && pi < CU_MAX_PROJECTILES; ++pi) {
                e->projectiles[pi].active = s->proj[pi].active;
                e->projectiles[pi].type = s->proj[pi].type;
                e->projectiles[pi].age = s->proj[pi].age;
                e->projectiles[pi].x = s->proj[pi].x;
                e->projectiles[pi].y = s->proj[pi].y;
                e->projectiles[pi].z = s->proj[pi].z;
                e->projectiles[pi].vx = s->proj[pi].vx;
                e->projectiles[pi].vy = s->proj[pi].vy;
                e->projectiles[pi].vz = s->proj[pi].vz;
                e->proj_in_ground[pi] = s->proj[pi].in_ground;
                e->proj_shake[pi] = s->proj[pi].shake;
                e->proj_pickup[pi] = s->proj[pi].pickup;
                e->proj_ground_ticks[pi] = s->proj[pi].ground_ticks;
            }
            memset(e->falls, 0, sizeof e->falls);
            e->n_falls = 0;
            for (fi = 0; fi < s->n_fall && fi < CU_MAX_ITEMS; ++fi) {
                e->falls[fi].active = s->falls[fi].active;
                e->falls[fi].type = s->falls[fi].type;
                e->falls[fi].x = s->falls[fi].x;
                e->falls[fi].y = s->falls[fi].y;
                e->falls[fi].z = s->falls[fi].z;
                e->falls[fi].mx = s->falls[fi].mx;
                e->falls[fi].my = s->falls[fi].my;
                e->falls[fi].mz = s->falls[fi].mz;
                e->falls[fi].on_ground = s->falls[fi].on_ground;
                e->falls[fi].age = s->falls[fi].age;
                e->falls[fi].item = s->falls[fi].item;
                e->falls[fi].count = s->falls[fi].count;
                e->falls[fi].meta = s->falls[fi].meta;
                e->falls[fi].pickup_delay = s->falls[fi].pickup_delay;
                e->falls[fi].lifespan = s->falls[fi].lifespan;
                if (e->falls[fi].active) e->n_falls++;
            }
            memset(e->fall_updates, 0, sizeof e->fall_updates);
            for (fi = 0; fi < s->n_fall_upd && fi < CU_FALL_UPDATES; ++fi) {
                e->fall_updates[fi].active = s->fall_upd[fi].active;
                e->fall_updates[fi].x = s->fall_upd[fi].x;
                e->fall_updates[fi].y = s->fall_upd[fi].y;
                e->fall_updates[fi].z = s->fall_upd[fi].z;
                e->fall_updates[fi].block_id = s->fall_upd[fi].block_id;
                e->fall_updates[fi].due_tick = s->fall_upd[fi].due_tick;
            }
            memset(e->fall_landings, 0, sizeof e->fall_landings);
            for (fi = 0; fi < s->n_fall_land && fi < CU_MAX_ITEMS; ++fi) {
                e->fall_landings[fi].active = s->fall_land[fi].active;
                e->fall_landings[fi].x = s->fall_land[fi].x;
                e->fall_landings[fi].y = s->fall_land[fi].y;
                e->fall_landings[fi].z = s->fall_land[fi].z;
                e->fall_landings[fi].block_id = s->fall_land[fi].block_id;
                e->fall_landings[fi].block_meta = s->fall_land[fi].block_meta;
                e->fall_landings[fi].due_tick = s->fall_land[fi].due_tick;
            }
            e->parity_fall_mutations = s->fall_mutations;
            e->live_ticks = s->live_ticks;
            {
                unsigned ui, si;
                memset(e->furnaces, 0, sizeof e->furnaces);
                e->active_furnace = s->active_furnace;
                for (ui = 0; ui < s->n_furn && ui < CU_MAX_FURNACES; ++ui) {
                    e->furnaces[ui].active = s->furn[ui].active;
                    e->furnaces[ui].wx = s->furn[ui].wx;
                    e->furnaces[ui].wy = s->furn[ui].wy;
                    e->furnaces[ui].wz = s->furn[ui].wz;
                    e->furnaces[ui].input = sr_mk(s->furn[ui].in_item,
                                                 s->furn[ui].in_count,
                                                 s->furn[ui].in_meta);
                    e->furnaces[ui].fuel = sr_mk(s->furn[ui].fuel_item,
                                                s->furn[ui].fuel_count,
                                                s->furn[ui].fuel_meta);
                    e->furnaces[ui].output = sr_mk(s->furn[ui].out_item,
                                                  s->furn[ui].out_count,
                                                  s->furn[ui].out_meta);
                    e->furnaces[ui].burn_time = s->furn[ui].burn_time;
                    e->furnaces[ui].current_burn_time =
                        s->furn[ui].current_burn_time;
                    e->furnaces[ui].cook_time = s->furn[ui].cook_time;
                    e->furnaces[ui].total_cook = s->furn[ui].total_cook;
                }
                memset(e->chests, 0, sizeof e->chests);
                e->active_chest = s->active_chest;
                for (ui = 0; ui < s->n_chest && ui < CU_MAX_CHESTS; ++ui) {
                    e->chests[ui].active = s->chest[ui].active;
                    e->chests[ui].wx = s->chest[ui].wx;
                    e->chests[ui].wy = s->chest[ui].wy;
                    e->chests[ui].wz = s->chest[ui].wz;
                    e->chests[ui].te.num_players_using =
                        s->chest[ui].num_using;
                    for (si = 0; si < BLAZE_SNAP_CHEST_SLOTS; ++si) {
                        int ei, n;
                        TecStack ts = tec_mk(
                            s->chest[ui].slot[si][0],
                            s->chest[ui].slot[si][1],
                            s->chest[ui].slot[si][2]);
                        n = s->chest[ui].slot_ench[si].n;
                        if (n < 0) n = 0;
                        if (n > TEC_MAX_ENCHANTS) n = TEC_MAX_ENCHANTS;
                        ts.n_enchants = n;
                        for (ei = 0; ei < n; ++ei) {
                            ts.enchants[ei].id =
                                s->chest[ui].slot_ench[si].id[ei];
                            ts.enchants[ei].level =
                                s->chest[ui].slot_ench[si].level[ei];
                        }
                        e->chests[ui].te.slots[si] = ts;
                    }
                }
                for (si = 0; si < 9; ++si)
                    e->craft_grid[si] = ic_mk(s->craft[si][0], s->craft[si][1],
                                              s->craft[si][2]);
                e->cursor = ic_mk(s->cursor[0], s->cursor[1], s->cursor[2]);
                e->parity_craft_attempts = s->craft_attempts;
                e->parity_craft_successes = s->craft_successes;
                e->parity_container_opens = s->container_opens;
                e->left_click_counter = s->left_click_counter;
                e->eat_ticks = s->eat_ticks;
                e->eat_item = s->eat_item;
                e->bow_ticks = s->bow_ticks;
                e->bow_drawing = s->bow_drawing;
                e->pl.experienceLevel = s->xp_level;
                e->pl.experienceTotal = s->xp_total;
                e->pl.xpCooldown = s->xp_cooldown;
                e->pl.experience = s->xp_experience;
                for (si = 0; si < 4; ++si)
                    isr_set_stack(&e->pl.inv, ISR_ARMOR0 + (int)si,
                                  ic_mk(s->armor[si][0], s->armor[si][1],
                                        s->armor[si][2]));
                e->fluid_dim = s->fluid_dim;
                e->parity_fluid_mutations = s->fluid_mutations;
                for (si = 0; si < CU_FLUID_REGIONS && si < BLAZE_SNAP_FLUID_REGS;
                     ++si) {
                    e->fluid_reg[si].active = s->fluid[si].active;
                    e->fluid_reg[si].x0 = s->fluid[si].x0;
                    e->fluid_reg[si].y0 = s->fluid[si].y0;
                    e->fluid_reg[si].z0 = s->fluid[si].z0;
                    e->fluid_reg[si].x1 = s->fluid[si].x1;
                    e->fluid_reg[si].y1 = s->fluid[si].y1;
                    e->fluid_reg[si].z1 = s->fluid[si].z1;
                    e->fluid_reg[si].has_water = s->fluid[si].has_water;
                    e->fluid_reg[si].quiet_steps = s->fluid[si].quiet_steps;
                }
                e->boat_ride = s->boat_ride;
                e->explosion_pending = s->explosion_pending;
                e->explosion_smoking = s->explosion_smoking;
                e->explosion_flaming = s->explosion_flaming;
                e->explosion_x = s->explosion_x;
                e->explosion_y = s->explosion_y;
                e->explosion_z = s->explosion_z;
                e->explosion_size = s->explosion_size;
                e->parity_xp_pickups = s->xtra.xp_pickups;
                e->next_orb_id = s->xtra.next_orb_id;
                /* magma rl_mode.c:2195-2198 restores these into GmMobs. */
                e->look_px = s->xtra.look_px;
                e->look_py = s->xtra.look_py;
                e->look_pz = s->xtra.look_pz;
                e->look_have = s->xtra.look_have ? 1 : 0;
                e->spawn_world_seed48 = s->xtra.spawn_world_seed48;
                e->spawn_math_seed48 = s->xtra.spawn_math_seed48;
                e->spawn_shuffle_seed48 = s->xtra.spawn_shuffle_seed48;
                e->parity_ex_blasts = s->xtra.parity_ex_blasts;
                e->parity_ex_destroyed = s->xtra.parity_ex_destroyed;
                e->parity_ex_drop_n = s->xtra.parity_ex_drop_n;
                e->parity_ex_drop_ids = s->xtra.parity_ex_drop_ids;
                e->parity_ex_damage = s->xtra.parity_ex_damage;
                e->parity_ex_kb_x = s->xtra.parity_ex_kb_x;
                e->parity_ex_kb_y = s->xtra.parity_ex_kb_y;
                e->parity_ex_kb_z = s->xtra.parity_ex_kb_z;
                e->parity_ex_rays = s->xtra.parity_ex_rays;
                e->parity_ex_last_x = s->xtra.parity_ex_last_x;
                e->parity_ex_last_y = s->xtra.parity_ex_last_y;
                e->parity_ex_last_z = s->xtra.parity_ex_last_z;
                e->parity_ex_last_size = s->xtra.parity_ex_last_size;
                e->dead = s->xtra.player_dead ? 1 : 0;
                e->player_hurt_resistant = s->xtra.player_hurt_resistant;
                e->player_attack_cooldown = s->xtra.player_attack_cooldown;
                e->player_last_damage = s->xtra.player_last_damage;
                e->mob_tick = s->xtra.mob_tick;
                {
                    int ei, n;
                    ICStack lc = ic_mk(s->xtra.last_craft[0],
                                       s->xtra.last_craft[1],
                                       s->xtra.last_craft[2]);
                    n = s->xtra.last_craft_ench.n;
                    if (n < 0) n = 0;
                    if (n > IC_MAX_ENCHANTS) n = IC_MAX_ENCHANTS;
                    lc.n_enchants = n;
                    for (ei = 0; ei < n; ++ei) {
                        lc.enchants[ei].id = s->xtra.last_craft_ench.id[ei];
                        lc.enchants[ei].level = s->xtra.last_craft_ench.level[ei];
                    }
                    e->parity_last_craft = lc;
                }
                e->pl.elytra_equipped = s->xtra.elytra_equipped ? 1 : 0;
                e->pl.elytra_flying = s->xtra.elytra_flying ? 1 : 0;
                e->pl.elytra_flying_pending = s->xtra.elytra_pending ? 1 : 0;
                e->pl.elytra_pose = s->xtra.elytra_pose;
                e->pl.ticks_elytra_flying = s->xtra.ticks_elytra_flying;
                e->pl.elytra_wall_damage = s->xtra.elytra_wall_damage;
                for (si = 0; si < s->n_mobs && si < BLAZE_SNAP_MAX_MOBS; ++si) {
                    e->boat_delta_rot[si] = s->xtra.boat_delta_rot[si];
                    e->boat_glide[si] = s->xtra.boat_glide[si];
                    e->mob_repath[si] = s->xtra.sidecar_repath[si];
                    e->mob_despawn[si] = s->xtra.sidecar_despawn[si];
                    e->mob_fire[si] = s->xtra.sidecar_fire[si];
                }
                for (si = 0; si < 37; ++si) {
                    int slot = si < 36 ? (int)si : ISR_OFFHAND_SLOT;
                    int ei, n;
                    ICStack st = isr_get_stack(&e->pl.inv, slot);
                    n = s->xtra.inv_ench[si].n;
                    if (n < 0) n = 0;
                    if (n > IC_MAX_ENCHANTS) n = IC_MAX_ENCHANTS;
                    st.n_enchants = n;
                    for (ei = 0; ei < n; ++ei) {
                        st.enchants[ei].id = s->xtra.inv_ench[si].id[ei];
                        st.enchants[ei].level = s->xtra.inv_ench[si].level[ei];
                    }
                    isr_set_stack(&e->pl.inv, slot, st);
                }
                for (si = 0; si < 4; ++si) {
                    int ei, n;
                    ICStack st = isr_get_stack(&e->pl.inv, ISR_ARMOR0 + (int)si);
                    n = s->xtra.armor_ench[si].n;
                    if (n < 0) n = 0;
                    if (n > IC_MAX_ENCHANTS) n = IC_MAX_ENCHANTS;
                    st.n_enchants = n;
                    for (ei = 0; ei < n; ++ei) {
                        st.enchants[ei].id = s->xtra.armor_ench[si].id[ei];
                        st.enchants[ei].level = s->xtra.armor_ench[si].level[ei];
                    }
                    isr_set_stack(&e->pl.inv, ISR_ARMOR0 + (int)si, st);
                }
                for (si = 0; si < 9; ++si) {
                    int ei, n;
                    n = s->xtra.craft_ench[si].n;
                    if (n < 0) n = 0;
                    if (n > IC_MAX_ENCHANTS) n = IC_MAX_ENCHANTS;
                    e->craft_grid[si].n_enchants = n;
                    for (ei = 0; ei < n; ++ei) {
                        e->craft_grid[si].enchants[ei].id =
                            s->xtra.craft_ench[si].id[ei];
                        e->craft_grid[si].enchants[ei].level =
                            s->xtra.craft_ench[si].level[ei];
                    }
                }
                {
                    int ei, n;
                    n = s->xtra.cursor_ench.n;
                    if (n < 0) n = 0;
                    if (n > IC_MAX_ENCHANTS) n = IC_MAX_ENCHANTS;
                    e->cursor.n_enchants = n;
                    for (ei = 0; ei < n; ++ei) {
                        e->cursor.enchants[ei].id = s->xtra.cursor_ench.id[ei];
                        e->cursor.enchants[ei].level =
                            s->xtra.cursor_ench.level[ei];
                    }
                }
            }
        }
    }
    v->envs[i].mobs_enabled = v->mobs_enabled;
    v->envs[i].natural_spawn = v->natural_spawn;
    v->envs[i].natural_spawn_passive = v->natural_spawn_passive;
    v->envs[i].det_entity_rng = v->det_entity_rng;
    v->envs[i].pf = v->pf_pool ? &v->pf_pool[i] : NULL;
    if (v->world_time_pin >= 0)
        v->envs[i].ww.worldTime = v->world_time_pin;
    v->envs[i].elytra_kit = v->elytra_kit;
    if (v->elytra_kit) {
        if (s->head.version < BLAZE_SNAP_VERSION_RESUME) {
            isr_set_stack(&v->envs[i].pl.inv, ISR_ARMOR_CHEST,
                          ic_mk(ISR_ELYTRA_ITEM, 1, 0));
            v->envs[i].pl.elytra_equipped = 1;
        } else {
            ICStack st = isr_get_stack(&v->envs[i].pl.inv, ISR_ARMOR_CHEST);
            v->envs[i].pl.elytra_equipped = (st.item == ISR_ELYTRA_ITEM);
        }
    }
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
    s->head.version = BLAZE_SNAP_VERSION;
    s->player_fire = e->pl.fire;
    s->player_air = e->pl.air;
    s->ww_total_time = e->ww.totalTime;
    s->ww_world_time = e->ww.worldTime;
    s->ww_rain_time = e->ww.rainTime;
    s->ww_thunder_time = e->ww.thunderTime;
    s->ww_raining = e->ww.raining;
    s->ww_thundering = e->ww.thundering;
    s->ww_rand_seed48 = e->ww.rand.seed & MC_JR_MASK;
    s->rt_mutations = e->parity_rt_mutations;
    {
        int k, n = e->pl.n_potions;
        if (n < 0) n = 0;
        if (n > BLAZE_SNAP_POTION_MAX) n = BLAZE_SNAP_POTION_MAX;
        s->n_potions = n;
        memset(s->potions, 0, sizeof s->potions);
        for (k = 0; k < n; ++k) {
            s->potions[k].id = e->pl.potions[k].id;
            s->potions[k].amplifier = e->pl.potions[k].amplifier;
            s->potions[k].duration = e->pl.potions[k].duration;
            s->potions[k].ambient = e->pl.potions[k].ambient;
            s->potions[k].show_particles = e->pl.potions[k].show_particles;
        }
    }
    s->n_mobs = e->n_mobs;
    if (e->n_mobs) {
        unsigned mi;
        memcpy(s->mobs, e->mobs, (size_t)e->n_mobs * sizeof s->mobs[0]);
        for (mi = 0; mi < e->n_mobs; ++mi) {
            s->mobs[mi].repath_timer = e->mob_repath[mi];
            s->mobs[mi].despawn_ticks = e->mob_despawn[mi];
            s->mobs[mi].fire_ticks = e->mob_fire[mi];
        }
    }
    if (!s->cells) {
        s->cells = (unsigned short *)malloc((size_t)v->rvol *
                                            sizeof *s->cells);
        if (!s->cells) return -1;
    }
    memcpy(s->cells, e->cells, (size_t)v->rvol * sizeof *s->cells);
    {
        size_t bvol = (size_t)v->rnx * (size_t)v->rnz;
        if (e->biome && bvol) {
            if (!s->biome) s->biome = (unsigned char *)malloc(bvol);
            if (s->biome) memcpy(s->biome, e->biome, bvol);
        }
    }
    if ((int)s->ncoal != e->nore) {
        /* Grow without freeing: live envs may still alias s->coal via
         * env->ore (bound at reset). Shrink keeps the existing allocation. */
        if (e->nore > (int)s->ncoal || !s->coal) {
            int *coal = NULL;
            if (e->nore) {
                coal = (int *)malloc((size_t)e->nore * 3 * sizeof *coal);
                if (!coal) return -1;
            }
            cpu_retire(v, s->coal);
            s->coal = coal;
        }
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
            cpu_retire(v, s->xy_off);
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

/* Mid-episode dump for the resume gate. Aliases the env's region buffers
 * for the write; they are not freed. */
int blaze_dump_snapshot(void *vh, int env, const char *path,
                        char *err, int err_cap) {
    CuVec *v = (CuVec *)vh;
    Blaze *e;
    CuSnapshot s;
    unsigned k;
    if (!v || env < 0 || env >= v->n || !path)
        return -1;
    e = &v->envs[env];
    memset(&s, 0, sizeof s);
    (void)blaze_capture_head(e, &s.head, s.items);
    s.head.version = BLAZE_SNAP_VERSION;
    s.player_fire = e->pl.fire;
    s.player_air = e->pl.air;
    s.ww_total_time = e->ww.totalTime;
    s.ww_world_time = e->ww.worldTime;
    s.ww_rain_time = e->ww.rainTime;
    s.ww_thunder_time = e->ww.thunderTime;
    s.ww_raining = e->ww.raining;
    s.ww_thundering = e->ww.thundering;
    s.ww_rand_seed48 = e->ww.rand.seed & MC_JR_MASK;
    s.rt_mutations = e->parity_rt_mutations;
    s.world_rand_seed = e->world_rand.seed & MC_JR_MASK;
    s.update_lcg = e->update_lcg;
    s.cells = e->cells;
    s.light = e->light;
    s.biome = e->biome;
    s.ncoal = (unsigned)e->nore;
    s.coal = (int *)e->ore;
    s.n_mobs = e->n_mobs;
    if (e->n_mobs)
        memcpy(s.mobs, e->mobs, (size_t)e->n_mobs * sizeof s.mobs[0]);
    s.n_proj = 0;
    for (k = 0; k < CU_MAX_PROJECTILES && s.n_proj < BLAZE_SNAP_MAX_PROJ; ++k) {
        if (!e->projectiles[k].active) continue;
        s.proj[s.n_proj].active = 1;
        s.proj[s.n_proj].type = e->projectiles[k].type;
        s.proj[s.n_proj].age = e->projectiles[k].age;
        s.proj[s.n_proj].x = e->projectiles[k].x;
        s.proj[s.n_proj].y = e->projectiles[k].y;
        s.proj[s.n_proj].z = e->projectiles[k].z;
        s.proj[s.n_proj].vx = e->projectiles[k].vx;
        s.proj[s.n_proj].vy = e->projectiles[k].vy;
        s.proj[s.n_proj].vz = e->projectiles[k].vz;
        s.proj[s.n_proj].in_ground = e->proj_in_ground[k];
        s.proj[s.n_proj].shake = e->proj_shake[k];
        s.proj[s.n_proj].pickup = e->proj_pickup[k];
        s.proj[s.n_proj].ground_ticks = e->proj_ground_ticks[k];
        s.n_proj++;
    }
    s.parity_proj_hits = e->parity_proj_hits;
    s.n_fall = 0;
    for (k = 0; k < CU_MAX_ITEMS && s.n_fall < BLAZE_SNAP_MAX_FALL; ++k) {
        if (!e->falls[k].active) continue;
        s.falls[s.n_fall].active = 1;
        s.falls[s.n_fall].type = e->falls[k].type;
        s.falls[s.n_fall].x = e->falls[k].x;
        s.falls[s.n_fall].y = e->falls[k].y;
        s.falls[s.n_fall].z = e->falls[k].z;
        s.falls[s.n_fall].mx = e->falls[k].mx;
        s.falls[s.n_fall].my = e->falls[k].my;
        s.falls[s.n_fall].mz = e->falls[k].mz;
        s.falls[s.n_fall].on_ground = e->falls[k].on_ground;
        s.falls[s.n_fall].age = e->falls[k].age;
        s.falls[s.n_fall].item = e->falls[k].item;
        s.falls[s.n_fall].count = e->falls[k].count;
        s.falls[s.n_fall].meta = e->falls[k].meta;
        s.falls[s.n_fall].pickup_delay = e->falls[k].pickup_delay;
        s.falls[s.n_fall].lifespan = e->falls[k].lifespan;
        s.n_fall++;
    }
    s.n_fall_upd = 0;
    for (k = 0; k < CU_FALL_UPDATES && s.n_fall_upd < BLAZE_SNAP_MAX_FALL_UPD;
         ++k) {
        if (!e->fall_updates[k].active) continue;
        s.fall_upd[s.n_fall_upd].active = 1;
        s.fall_upd[s.n_fall_upd].x = e->fall_updates[k].x;
        s.fall_upd[s.n_fall_upd].y = e->fall_updates[k].y;
        s.fall_upd[s.n_fall_upd].z = e->fall_updates[k].z;
        s.fall_upd[s.n_fall_upd].block_id = e->fall_updates[k].block_id;
        s.fall_upd[s.n_fall_upd].due_tick = e->fall_updates[k].due_tick;
        s.n_fall_upd++;
    }
    s.n_fall_land = 0;
    for (k = 0; k < CU_MAX_ITEMS && s.n_fall_land < BLAZE_SNAP_MAX_FALL; ++k) {
        if (!e->fall_landings[k].active) continue;
        s.fall_land[s.n_fall_land].active = 1;
        s.fall_land[s.n_fall_land].x = e->fall_landings[k].x;
        s.fall_land[s.n_fall_land].y = e->fall_landings[k].y;
        s.fall_land[s.n_fall_land].z = e->fall_landings[k].z;
        s.fall_land[s.n_fall_land].block_id = e->fall_landings[k].block_id;
        s.fall_land[s.n_fall_land].block_meta = e->fall_landings[k].block_meta;
        s.fall_land[s.n_fall_land].due_tick = e->fall_landings[k].due_tick;
        s.n_fall_land++;
    }
    s.fall_mutations = e->parity_fall_mutations;
    s.live_ticks = e->live_ticks;
    s.n_furn = 0;
    s.active_furnace = e->active_furnace;
    for (k = 0; k < CU_MAX_FURNACES && s.n_furn < BLAZE_SNAP_MAX_FURN; ++k) {
        if (!e->furnaces[k].active) continue;
        s.furn[s.n_furn].active = 1;
        s.furn[s.n_furn].wx = e->furnaces[k].wx;
        s.furn[s.n_furn].wy = e->furnaces[k].wy;
        s.furn[s.n_furn].wz = e->furnaces[k].wz;
        s.furn[s.n_furn].in_item = e->furnaces[k].input.item;
        s.furn[s.n_furn].in_count = e->furnaces[k].input.count;
        s.furn[s.n_furn].in_meta = e->furnaces[k].input.meta;
        s.furn[s.n_furn].fuel_item = e->furnaces[k].fuel.item;
        s.furn[s.n_furn].fuel_count = e->furnaces[k].fuel.count;
        s.furn[s.n_furn].fuel_meta = e->furnaces[k].fuel.meta;
        s.furn[s.n_furn].out_item = e->furnaces[k].output.item;
        s.furn[s.n_furn].out_count = e->furnaces[k].output.count;
        s.furn[s.n_furn].out_meta = e->furnaces[k].output.meta;
        s.furn[s.n_furn].burn_time = e->furnaces[k].burn_time;
        s.furn[s.n_furn].current_burn_time = e->furnaces[k].current_burn_time;
        s.furn[s.n_furn].cook_time = e->furnaces[k].cook_time;
        s.furn[s.n_furn].total_cook = e->furnaces[k].total_cook;
        s.n_furn++;
    }
    s.n_chest = 0;
    s.active_chest = e->active_chest;
    for (k = 0; k < CU_MAX_CHESTS && s.n_chest < BLAZE_SNAP_MAX_CHEST; ++k) {
        unsigned si;
        if (!e->chests[k].active) continue;
        s.chest[s.n_chest].active = 1;
        s.chest[s.n_chest].wx = e->chests[k].wx;
        s.chest[s.n_chest].wy = e->chests[k].wy;
        s.chest[s.n_chest].wz = e->chests[k].wz;
        s.chest[s.n_chest].num_using = e->chests[k].te.num_players_using;
        for (si = 0; si < BLAZE_SNAP_CHEST_SLOTS; ++si) {
            const TecStack *ts = &e->chests[k].te.slots[si];
            int ei, n;
            s.chest[s.n_chest].slot[si][0] = ts->item;
            s.chest[s.n_chest].slot[si][1] = ts->count;
            s.chest[s.n_chest].slot[si][2] = ts->meta;
            n = ts->n_enchants;
            if (n < 0) n = 0;
            if (n > 8) n = 8;
            s.chest[s.n_chest].slot_ench[si].n = n;
            for (ei = 0; ei < n; ++ei) {
                s.chest[s.n_chest].slot_ench[si].id[ei] = ts->enchants[ei].id;
                s.chest[s.n_chest].slot_ench[si].level[ei] =
                    ts->enchants[ei].level;
            }
        }
        s.n_chest++;
    }
    for (k = 0; k < 9; ++k) {
        s.craft[k][0] = e->craft_grid[k].item;
        s.craft[k][1] = e->craft_grid[k].count;
        s.craft[k][2] = e->craft_grid[k].meta;
    }
    s.cursor[0] = e->cursor.item;
    s.cursor[1] = e->cursor.count;
    s.cursor[2] = e->cursor.meta;
    s.craft_attempts = e->parity_craft_attempts;
    s.craft_successes = e->parity_craft_successes;
    s.container_opens = e->parity_container_opens;
    s.left_click_counter = e->left_click_counter;
    s.eat_ticks = e->eat_ticks;
    s.eat_item = e->eat_item;
    s.bow_ticks = e->bow_ticks;
    s.bow_drawing = e->bow_drawing;
    s.xp_level = e->pl.experienceLevel;
    s.xp_total = e->pl.experienceTotal;
    s.xp_cooldown = e->pl.xpCooldown;
    s.xp_experience = e->pl.experience;
    for (k = 0; k < 4; ++k) {
        ICStack st = isr_get_stack(&e->pl.inv, ISR_ARMOR0 + (int)k);
        s.armor[k][0] = st.item;
        s.armor[k][1] = st.count;
        s.armor[k][2] = st.meta;
    }
    s.fluid_dim = e->fluid_dim;
    s.fluid_mutations = e->parity_fluid_mutations;
    for (k = 0; k < CU_FLUID_REGIONS && k < BLAZE_SNAP_FLUID_REGS; ++k) {
        s.fluid[k].active = e->fluid_reg[k].active;
        s.fluid[k].x0 = e->fluid_reg[k].x0;
        s.fluid[k].y0 = e->fluid_reg[k].y0;
        s.fluid[k].z0 = e->fluid_reg[k].z0;
        s.fluid[k].x1 = e->fluid_reg[k].x1;
        s.fluid[k].y1 = e->fluid_reg[k].y1;
        s.fluid[k].z1 = e->fluid_reg[k].z1;
        s.fluid[k].has_water = e->fluid_reg[k].has_water;
        s.fluid[k].quiet_steps = e->fluid_reg[k].quiet_steps;
    }
    s.boat_ride = e->boat_ride;
    s.explosion_pending = e->explosion_pending;
    s.explosion_smoking = e->explosion_smoking;
    s.explosion_flaming = e->explosion_flaming;
    s.explosion_x = e->explosion_x;
    s.explosion_y = e->explosion_y;
    s.explosion_z = e->explosion_z;
    s.explosion_size = e->explosion_size;
    {
        unsigned si, ei;
        int n;
        memset(&s.xtra, 0, sizeof s.xtra);
        s.xtra.xp_pickups = e->parity_xp_pickups;
        s.xtra.next_orb_id = e->next_orb_id;
        s.xtra.spawn_world_seed48 = e->spawn_world_seed48;
        s.xtra.spawn_math_seed48 = e->spawn_math_seed48;
        s.xtra.spawn_shuffle_seed48 = e->spawn_shuffle_seed48;
        s.xtra.parity_ex_blasts = e->parity_ex_blasts;
        s.xtra.parity_ex_destroyed = e->parity_ex_destroyed;
        s.xtra.parity_ex_drop_n = e->parity_ex_drop_n;
        s.xtra.parity_ex_drop_ids = e->parity_ex_drop_ids;
        s.xtra.parity_ex_damage = e->parity_ex_damage;
        s.xtra.parity_ex_kb_x = e->parity_ex_kb_x;
        s.xtra.parity_ex_kb_y = e->parity_ex_kb_y;
        s.xtra.parity_ex_kb_z = e->parity_ex_kb_z;
        s.xtra.parity_ex_rays = e->parity_ex_rays;
        s.xtra.parity_ex_last_x = e->parity_ex_last_x;
        s.xtra.parity_ex_last_y = e->parity_ex_last_y;
        s.xtra.parity_ex_last_z = e->parity_ex_last_z;
        s.xtra.parity_ex_last_size = e->parity_ex_last_size;
        s.xtra.player_dead = e->dead;
        s.xtra.player_hurt_resistant = e->player_hurt_resistant;
        s.xtra.player_attack_cooldown = e->player_attack_cooldown;
        s.xtra.player_last_damage = e->player_last_damage;
        s.xtra.mob_tick = (int)e->mob_tick;
        s.xtra.last_craft[0] = e->parity_last_craft.item;
        s.xtra.last_craft[1] = e->parity_last_craft.count;
        s.xtra.last_craft[2] = e->parity_last_craft.meta;
        {
            int n = e->parity_last_craft.n_enchants, ei;
            if (n < 0) n = 0;
            if (n > 8) n = 8;
            s.xtra.last_craft_ench.n = n;
            for (ei = 0; ei < n; ++ei) {
                s.xtra.last_craft_ench.id[ei] =
                    e->parity_last_craft.enchants[ei].id;
                s.xtra.last_craft_ench.level[ei] =
                    e->parity_last_craft.enchants[ei].level;
            }
        }
        s.xtra.elytra_equipped = e->pl.elytra_equipped;
        s.xtra.elytra_flying = e->pl.elytra_flying;
        s.xtra.elytra_pending = e->pl.elytra_flying_pending;
        s.xtra.elytra_pose = e->pl.elytra_pose;
        s.xtra.ticks_elytra_flying = e->pl.ticks_elytra_flying;
        s.xtra.elytra_wall_damage = e->pl.elytra_wall_damage;
        for (si = 0; si < e->n_mobs && si < BLAZE_SNAP_MAX_MOBS; ++si) {
            s.xtra.boat_delta_rot[si] = e->boat_delta_rot[si];
            s.xtra.boat_glide[si] = e->boat_glide[si];
            s.xtra.sidecar_repath[si] = e->mob_repath[si];
            s.xtra.sidecar_despawn[si] = e->mob_despawn[si];
            s.xtra.sidecar_fire[si] = e->mob_fire[si];
        }
        for (si = 0; si < 37; ++si) {
            ICStack st = isr_get_stack(&e->pl.inv,
                                       si < 36 ? (int)si : ISR_OFFHAND_SLOT);
            n = st.n_enchants;
            if (n < 0) n = 0;
            if (n > 8) n = 8;
            s.xtra.inv_ench[si].n = n;
            for (ei = 0; ei < (unsigned)n; ++ei) {
                s.xtra.inv_ench[si].id[ei] = st.enchants[ei].id;
                s.xtra.inv_ench[si].level[ei] = st.enchants[ei].level;
            }
        }
        for (si = 0; si < 4; ++si) {
            ICStack st = isr_get_stack(&e->pl.inv, ISR_ARMOR0 + (int)si);
            n = st.n_enchants;
            if (n < 0) n = 0;
            if (n > 8) n = 8;
            s.xtra.armor_ench[si].n = n;
            for (ei = 0; ei < (unsigned)n; ++ei) {
                s.xtra.armor_ench[si].id[ei] = st.enchants[ei].id;
                s.xtra.armor_ench[si].level[ei] = st.enchants[ei].level;
            }
        }
        for (si = 0; si < 9; ++si) {
            n = e->craft_grid[si].n_enchants;
            if (n < 0) n = 0;
            if (n > 8) n = 8;
            s.xtra.craft_ench[si].n = n;
            for (ei = 0; ei < (unsigned)n; ++ei) {
                s.xtra.craft_ench[si].id[ei] = e->craft_grid[si].enchants[ei].id;
                s.xtra.craft_ench[si].level[ei] =
                    e->craft_grid[si].enchants[ei].level;
            }
        }
        n = e->cursor.n_enchants;
        if (n < 0) n = 0;
        if (n > 8) n = 8;
        s.xtra.cursor_ench.n = n;
        for (ei = 0; ei < (unsigned)n; ++ei) {
            s.xtra.cursor_ench.id[ei] = e->cursor.enchants[ei].id;
            s.xtra.cursor_ench.level[ei] = e->cursor.enchants[ei].level;
        }
    }
    {
        int k, n = e->pl.n_potions;
        if (n < 0) n = 0;
        if (n > BLAZE_SNAP_POTION_MAX) n = BLAZE_SNAP_POTION_MAX;
        s.n_potions = n;
        memset(s.potions, 0, sizeof s.potions);
        for (k = 0; k < n; ++k) {
            s.potions[k].id = e->pl.potions[k].id;
            s.potions[k].amplifier = e->pl.potions[k].amplifier;
            s.potions[k].duration = e->pl.potions[k].duration;
            s.potions[k].ambient = e->pl.potions[k].ambient;
            s.potions[k].show_particles = e->pl.potions[k].show_particles;
        }
    }
    s.n_orbs = 0;
    for (k = 0; k < XL_MAX && s.n_orbs < BLAZE_SNAP_MAX_ORBS; ++k) {
        const McOrb *o = &e->orbs[k];
        RlSnapOrb *d;
        if (o->dead || o->xpValue <= 0) continue;
        d = &s.orbs[s.n_orbs++];
        memset(d, 0, sizeof *d);
        d->x = o->posX; d->y = o->posY; d->z = o->posZ;
        d->mx = o->motionX; d->my = o->motionY; d->mz = o->motionZ;
        d->on_ground = o->onGround;
        d->xpOrbAge = o->xpOrbAge;
        d->delayBeforeCanPickup = o->delayBeforeCanPickup;
        d->xpValue = o->xpValue;
        d->eid = o->eid;
        d->xpColor = o->xpColor;
        d->xpTargetColor = o->xpTargetColor;
        d->has_closest = o->has_closest;
        d->dead = o->dead;
    }
    if (!blaze_snapshot_write(path, &s, err, err_cap))
        return -1;
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

/* One raw tick mirroring the real env's action-line loop: a[17] =
 * {forward,strafe,dyaw,dpitch,jump,sneak,sprint,attack,use,hotbar,
 *  craft,interact,smelt,inv_click,inv_slot,inv_button,inv_type}
 * (craft = rl_crafts index or -1; interact/smelt/inv_click = 0/1). The
 * discrete primitives are applied BEFORE the tick, in rl_mode's order
 * (craft, then interact, then smelt, then gm_runtime_tick which consumes
 * inv_click). Then emits the obs exactly as rl_emit_obs would. */
int blaze_tick_raw(void *vh, int env, const double a[17], int want_cam,
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
    act.attack_entity = 0;
    act.hotbar_sel = (int)a[9];
    act.inv_click = (int)a[13];
    act.inv_slot = (int)a[14];
    act.inv_button = (int)a[15];
    act.inv_type = (int)a[16];
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

/* Production-path tick: same 17-double ABI as blaze_tick_raw. CPU runs the
 * serial decision body (begin, optional inv_click, recenter, one subtick)
 * that k_tick / k_tick_warp execute on CUDA. Trainer blaze_step stays 13-wide
 * and does not call this. */
int blaze_tick(void *vh, int env, const double a[17], int want_cam,
               void *out) {
    CuVec *v = (CuVec *)vh;
    Blaze *e;
    McAABB *blocks;
    if (!v || env < -1 || env >= v->n || !a) return -1;
    if (env == -1) {
        int i, rc = 0;
#pragma omp parallel for schedule(static) reduction(|:rc) if(v->n > 1)
        for (i = 0; i < v->n; ++i)
            rc |= blaze_tick(vh, i, a, 0, NULL);
        return rc;
    }
    e = &v->envs[env];
    blocks = v->blocks + (size_t)env * PSV_MAX_BLOCKS;
    if (!blaze_decision_begin(e, &v->st, a, v->recipes, v->nrecipes)) {
        if (out) blaze_emit_bolr(e, &v->st, (CuBinObs *)out, want_cam);
        return 0;
    }
    if ((int)a[13])
        (void)blaze_container_click(e, (int)a[14], (int)a[15], (int)a[16]);
    if (!e->dead) cu_recenter(e);
    blaze_decision_subtick(e, &v->st, a, 0, 1, blocks, 1, v->atk_gate);
    if (out) blaze_emit_bolr(e, &v->st, (CuBinObs *)out, want_cam);
    return 0;
}

/* Raw sim state for divergence bisecting: layout in blaze_debug_fill. */
int blaze_debug_state(void *vh, int env, double *out, int cap) {
    CuVec *v = (CuVec *)vh;
    if (!v || env < 0 || env >= v->n || !out || cap < 21) return -1;
    return blaze_debug_fill(&v->envs[env], out);
}

/* Harness-only live table dump (verify_cpu.py --dump-mobs). Not sim state. */
int blaze_mobs_count(void *vh, int env) {
    CuVec *v = (CuVec *)vh;
    if (!v || env < 0 || env >= v->n) return -1;
    return (int)v->envs[env].n_mobs;
}

int blaze_mobs_get(void *vh, int env, int i, int *slot, int *type, int *alive,
                   double *x, double *y, double *z) {
    CuVec *v = (CuVec *)vh;
    const RlSnapMob *m;
    if (!v || env < 0 || env >= v->n || i < 0) return -1;
    if ((unsigned)i >= v->envs[env].n_mobs) return -1;
    m = &v->envs[env].mobs[i];
    if (slot) *slot = m->slot;
    if (type) *type = m->type;
    if (alive) *alive = m->alive;
    if (x) *x = m->x;
    if (y) *y = m->y;
    if (z) *z = m->z;
    return 0;
}

/* mobs_det coverage evidence: cumulative PathFinder call / non-empty-path
 * counts for one env. Not sim state (never hashed, never snapshotted, not
 * cleared by reset). The gate fails closed when calls == 0. */
int blaze_mob_ai_stats(void *vh, int env, unsigned long long *calls,
                       unsigned long long *paths) {
    CuVec *v = (CuVec *)vh;
    if (!v || env < 0 || env >= v->n) return -1;
    if (calls) *calls = v->envs[env].pf_calls;
    if (paths) *paths = v->envs[env].pf_paths;
    return 0;
}

int blaze_parity_state(void *vh, int env, void *out) {
    CuVec *v = (CuVec *)vh;
    if (!v || env < 0 || env >= v->n || !out) return -1;
    blaze_parity_fill(&v->envs[env], (BpParityRecord *)out);
    return 0;
}
