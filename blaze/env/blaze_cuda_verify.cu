/* blaze_cuda_verify.cu - the CUDA backend's VERIFY-ONLY translation unit.
 *
 * Same headers, same flags and the same C ABI as blaze_cuda.cu; it holds only
 * the kernels and host entry points that the gates call and the trainer never
 * does:
 *   k_tick_raw   : one-thread-per-env blaze_runtime_tick (blaze_tick_raw;
 *                  env == -1 broadcasts to all n envs)
 *   k_emit_cam / k_emit / k_emit_cam_all / k_emit_all : observation records
 *                  (blaze_emit, blaze_emit_all)
 *   k_parity / k_parity_all : parity records (blaze_parity_state,
 *                  blaze_parity_state_all)
 *
 * Why a second unit: every one of these kernels inlines the whole header-only
 * sim, and cicc plus ptxas paid that cost again for each kernel in the single
 * old unit. Splitting lets make -j compile the two units at once, and an edit
 * to the training kernels no longer recompiles these.
 *
 * No device call crosses the unit boundary - blaze_core.h device functions are
 * all MC_HD static inline, so each unit compiles its own copies in whole-
 * program mode. Do NOT add -rdc. Host calls do cross: blaze_tick in
 * blaze_cuda.cu calls blaze_emit here, through the shared extern "C"
 * declarations in blaze_cuda_int.h.
 *
 * The shared CuVecCu state, the defines and cu_ck live in blaze_cuda_int.h. */
#include "blaze_cuda_int.h"

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

/* All-lanes twins of the two kernels above, for blaze_emit_all. Same per-env
 * work in the same order - only the thread->(env,pixel) mapping differs - so
 * a lane's record is bit-identical to what the per-lane blaze_emit produces.
 * The chain gate emitted 64 lanes with 64 serial launch+sync+memcpy round
 * trips per tick; at 2058 ticks that dominated the gate's wall clock. */
__global__ void k_emit_cam_all(Blaze *envs, int n, const McSinTable *st) {
    long long gid = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (gid >= (long long)n * CU_NPIX) return;
    blaze_render_cam_pixel(&envs[gid / CU_NPIX], st, (int)(gid % CU_NPIX));
}

__global__ void k_emit_all(Blaze *envs, int n, const McSinTable *st,
                           CuBinObs *out) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    blaze_emit_bolr(&envs[i], st, &out[i], 0);
}

__global__ void k_parity(Blaze *envs, int env, BpParityRecord *out) {
    if (threadIdx.x || blockIdx.x) return;
    blaze_parity_fill(&envs[env], out);
}

/* All-lanes twin of k_parity for blaze_parity_state_all. Same per-env fill
 * as the single-env kernel - only the thread->env mapping differs - so a
 * lane's record is bit-identical to what blaze_parity_state(lane) produces.
 * Serial 64-lane parity was the same class of launch+sync+memcpy bottleneck
 * the emit_all path already fixed. */
__global__ void k_parity_all(Blaze *envs, int n, BpParityRecord *out) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    blaze_parity_fill(&envs[i], &out[i]);
}


typedef struct { double a[17]; } CuRawAct;

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
    act.inv_click = (int)ra.a[13];
    act.inv_slot = (int)ra.a[14];
    act.inv_button = (int)ra.a[15];
    act.inv_type = (int)ra.a[16];
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

/* Batched blaze_emit: fills `out` with all n records back to back (lane i at
 * out + i*sizeof(CuBinObs)) in ONE launch pair plus ONE DtoH copy. Byte-for-
 * byte what a blaze_emit loop over lanes 0..n-1 would write; the per-lane
 * blaze_emit above is untouched and remains the reference. */
int blaze_emit_all(void *vh, int want_cam, void *out) {
    CuVecCu *v = (CuVecCu *)vh;
    if (!v || !out) return -1;
    cudaSetDevice(v->device);
    if (!v->d_obs_all &&
        cu_ck(cudaMalloc(&v->d_obs_all, (size_t)v->n * sizeof(CuBinObs)),
              "emit_all staging alloc"))
        return -1;
    if (want_cam) {
        long long npix = (long long)v->n * CU_NPIX;
        k_emit_cam_all<<<(unsigned)((npix + CU_TPB - 1) / CU_TPB), CU_TPB, 0,
                         v->stream>>>(v->d_envs, v->n, v->d_st);
    }
    /* One thread PER BLOCK, not CU_TPB threads per block: each thread fills a
     * whole 14.6 KB record with serial stores, so packing 64 of them into one
     * block serialises 0.94 MB of stores onto a single SM (measured 3.06
     * ms/tick). One block per env spreads them over the GPU's SMs instead. */
    k_emit_all<<<v->n, 1, 0, v->stream>>>(v->d_envs, v->n, v->d_st,
                                          v->d_obs_all);
    if (cu_ck(cudaStreamSynchronize(v->stream), "k_emit_all")) return -1;
    return cu_ck(cudaMemcpy(out, v->d_obs_all,
                            (size_t)v->n * sizeof(CuBinObs),
                            cudaMemcpyDeviceToHost), "obs_all readback");
}

/* env == -1 broadcasts the same raw action to ALL envs in one launch (one
 * thread per env; no obs - use blaze_emit per lane). Mirrors the CPU
 * driver's broadcast loop. */
int blaze_tick_raw(void *vh, int env, const double a[17], int want_cam,
                   void *out) {
    CuVecCu *v = (CuVecCu *)vh;
    CuRawAct ra;
    if (!v || env < -1 || env >= v->n || !a) return -1;
    memcpy(ra.a, a, sizeof ra.a);
    cudaSetDevice(v->device);
    if (env == -1)
        /* one block per env, not 32: k_tick_raw has no collectives (the
         * env>=0 branch already runs <<<1,1>>>) and 32 divergent envs in one
         * warp serialize on divergence. Measured 18.4s -> 17.6s on the 2058
         * -tick chain gate; the rest of the tick cost is per-env critical
         * path, not scheduling. */
        k_tick_raw<<<v->n, 1, 0,
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


int blaze_parity_state(void *vh, int env, void *out) {
    CuVecCu *v = (CuVecCu *)vh;
    BpParityRecord *d_out;
    if (!v || env < 0 || env >= v->n || !out) return -1;
    cudaSetDevice(v->device);
    d_out = (BpParityRecord *)v->d_obs;
    k_parity<<<1, 1, 0, v->stream>>>(v->d_envs, env, d_out);
    if (cu_ck(cudaStreamSynchronize(v->stream), "k_parity")) return -1;
    return cu_ck(cudaMemcpy(out, d_out, sizeof(BpParityRecord),
                            cudaMemcpyDeviceToHost), "parity readback");
}

/* Batched blaze_parity_state: fills `out` with all n PARY records back to back
 * (lane i at out + i*sizeof(BpParityRecord)) in ONE launch + ONE DtoH copy.
 * Byte-for-byte what a blaze_parity_state loop over lanes 0..n-1 would write;
 * the per-lane entry above is untouched and remains the reference. */
int blaze_parity_state_all(void *vh, void *out) {
    CuVecCu *v = (CuVecCu *)vh;
    if (!v || !out) return -1;
    cudaSetDevice(v->device);
    if (!v->d_parity_all &&
        cu_ck(cudaMalloc(&v->d_parity_all,
                         (size_t)v->n * sizeof(BpParityRecord)),
              "parity_all staging alloc"))
        return -1;
    /* One block per env (same rationale as k_emit_all): each thread runs the
     * full blaze_parity_fill serial work, so packing them into one block would
     * serialize on a single SM. */
    k_parity_all<<<v->n, 1, 0, v->stream>>>(v->d_envs, v->n, v->d_parity_all);
    if (cu_ck(cudaStreamSynchronize(v->stream), "k_parity_all")) return -1;
    return cu_ck(cudaMemcpy(out, v->d_parity_all,
                            (size_t)v->n * sizeof(BpParityRecord),
                            cudaMemcpyDeviceToHost), "parity_all readback");
}

