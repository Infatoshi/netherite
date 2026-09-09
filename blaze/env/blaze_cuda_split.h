/* Measurement-only phase boundaries. Each environment retains its original
 * operation order. No runtime subsystem is skipped or executed twice.
 * Global stream ordering replaces the warp-local sequence points. */
__global__ void k_split_begin(Blaze *envs, int n, const McSinTable *st,
                              const double *a, const CRRecipe *recipes,
                              int nr, const double *inv, int *exec) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    Blaze *e = envs + i;
    exec[i] = !e->dimension_error &&
        blaze_decision_begin(e, st, a + (size_t)i * BLAZE_ACT_HEADS, recipes, nr);
    if (exec[i]) cu_apply_inv_click(e, inv, i);
}

__global__ void k_split_recenter(Blaze *envs, int n, const int *exec) {
    int i = (blockIdx.x * blockDim.x + threadIdx.x) >> 5;
    int lane = threadIdx.x & 31;
    if (i >= n) return;
    Blaze *e = envs + i;
    int need = 0, dx = 0, dz = 0, cx = 0, cz = 0;
    if (lane == 0 && exec[i] && !e->dead && cu_recenter_pose(e, &dx, &dz)) {
        need = 1; cx = e->ccx; cz = e->ccz;
    }
    need = __shfl_sync(0xffffffffu, need, 0);
    if (need) {
        dx = __shfl_sync(0xffffffffu, dx, 0);
        dz = __shfl_sync(0xffffffffu, dz, 0);
        cx = __shfl_sync(0xffffffffu, cx, 0);
        cz = __shfl_sync(0xffffffffu, cz, 0);
        __syncwarp();
        cu_recenter_fill(e, cx, cz, dx, dz, lane, 32);
    }
}

__global__ void k_split_pre(Blaze *envs, int n, const McSinTable *st,
                            const double *a, int rep, McAABB *blocks, int *exec) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n || !(exec[i] & 1)) return;
    CuAction act;
    CU_OP(envs + i, CU_OP_SUBTICK);
    blaze_act_from_row(&act, a + (size_t)i * BLAZE_ACT_HEADS, rep);
    exec[i] = 1 | (blaze_runtime_tick_pre_rt(envs + i, st, act,
                 blocks + (size_t)i * PSV_MAX_BLOCKS) ? 2 : 0);
}

__global__ void k_split_randtick(Blaze *envs, int n, const int *exec) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n || !(exec[i] & 2)) return;
    CU_PHASE_T0(envs + i);
    cu_randtick_pass(envs + i);
    CU_PHASE_END(envs + i, CU_PHASE_RANDTICK);
}

__global__ void k_split_post(Blaze *envs, int n, const McSinTable *st,
                             int rep, int repeat, const int *exec) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n || !(exec[i] & 1)) return;
    Blaze *e = envs + i;
    if (exec[i] & 2) blaze_runtime_tick_post_rt(e, st);
    /* These run even when runtime's dead gate rejected the tick, exactly as
     * blaze_subtick_phys does after blaze_runtime_tick_nr returns. */
    if (e->swap_pending) cu_dimension_swap_apply(e);
    if (rep == repeat - 1) e->dec_cam_fresh = 1;
}

__global__ void k_split_reward(Blaze *envs, int n, int rep, int repeat,
                               double gate, int *exec) {
    int i = (blockIdx.x * blockDim.x + threadIdx.x) >> 5;
    int lane = threadIdx.x & 31;
    if (i >= n || !(exec[i] & 1)) return;
    Blaze *e = envs + i;
    int have = 0;
    double ry = 0.0, rp = 0.0, dist = 0.0;
    cu_coal_warp(e, lane, &have, &ry, &rp, &dist);
    if (lane == 0) {
        blaze_subtick_post(e, rep, repeat, gate, have, ry, rp, dist);
        exec[i] = (e->done || e->dimension_error) ? 0 : 1;
    }
}

static void cu_launch_split(CuVecCu *v, Blaze *envs, int n,
                             const double *a, int repeat, McAABB *blocks,
                             const double *inv) {
    const int tpb = 128, nb = (n + tpb - 1) / tpb;
    const unsigned nw = (unsigned)(((size_t)n * 32 + tpb - 1) / tpb);
    k_split_begin<<<nb, tpb, 0, v->stream>>>(envs, n, v->d_st, a,
        v->d_recipes, v->nrecipes, inv, v->d_split_exec);
    for (int rep = 0; rep < repeat; ++rep) {
        k_split_recenter<<<nw, tpb, 0, v->stream>>>(envs, n, v->d_split_exec);
        k_split_pre<<<nb, tpb, 0, v->stream>>>(envs, n, v->d_st, a, rep,
            blocks, v->d_split_exec);
        k_split_randtick<<<nb, tpb, 0, v->stream>>>(envs, n, v->d_split_exec);
        k_split_post<<<nb, tpb, 0, v->stream>>>(envs, n, v->d_st, rep, repeat,
            v->d_split_exec);
        k_split_reward<<<nw, tpb, 0, v->stream>>>(envs, n, rep, repeat,
            v->atk_gate, v->d_split_exec);
    }
}

/* Optional extension symbol; existing create options and step ABI unchanged.
 * Call only between synchronous step/tick calls. No work runs on selection. */
extern "C" int blaze_measure_set_split(void *vh, int mode) {
    CuVecCu *v = (CuVecCu *)vh;
    if (!v || mode < 0 || mode > 1) return -1;
    if (mode && !v->d_split_exec) {
        if (cu_ck(cudaSetDevice(v->device), "split device") ||
            cu_ck(cudaMalloc(&v->d_split_exec, (size_t)v->n * sizeof(int)),
                  "split continuation allocation")) return -1;
    }
    v->measure_split = mode;
    return 0;
}
