/* Host-only measurement shim. Loads existing cubin kernels, never compiles
 * another simulation. One configured environment/module per process. Build
 * with the ordinary C++ compiler, NOT nvcc. Private CuVecCu ABI must match
 * the simulator source exactly. Duplicate module code consumes device memory. */
#include "../../blaze/env/blaze_cuda_int.h"
#include <cuda.h>
#include <cxxabi.h>
#include <vector>
#include <cstdint>

enum { BEGIN, RECENTER, PRE, PLAYER, WORLD, RANDOM, POST, REWARD, OBS, FINAL,
       DIMENSION, NK };
static const char *names[NK] = {"k_split_begin", "k_split_recenter",
    "k_split_pre", "k_split_player", "k_split_world", "k_split_randtick",
    "k_split_post", "k_split_reward", "k_obs", "k_final", "k_dimension_status"};
static CUmodule module;
static CUfunction fun[NK];
static CuVecCu *owner;
static int grain;
static CUmodule fine_module[3];
static CUfunction fine_fun[3];
static CuAction *fine_actions;
static int graph_enabled;
static cudaGraph_t graph;
static cudaGraphExec_t graph_exec;
static uint64_t graph_key[40];
static unsigned long long graph_builds;
static void release_graph(void) {
    if (graph_exec) cudaGraphExecDestroy(graph_exec);
    if (graph) cudaGraphDestroy(graph);
    graph_exec = NULL; graph = NULL;
    memset(graph_key, 0, sizeof graph_key);
}
extern "C" int blaze_driver_set_graph(int enabled) {
    if (!owner || (enabled != 0 && enabled != 1)) return -1;
    if (cu_ck(cudaSetDevice(owner->device), "graph device") ||
        cu_ck(cudaStreamSynchronize(owner->stream), "graph configure sync")) return -1;
    release_graph(); graph_enabled = enabled;
    return 0;
}
extern "C" unsigned long long blaze_driver_graph_builds(void) { return graph_builds; }

static int ck(CUresult r, const char *what) {
    if (r == CUDA_SUCCESS) return 0;
    const char *msg = NULL;
    cuGetErrorString(r, &msg);
    fprintf(stderr, "driver dispatch: %s: %s\n", what, msg ? msg : "unknown");
    return -1;
}
static int launch(int k, unsigned blocks, unsigned threads, void **args) {
    return ck(cuLaunchKernel(fun[k], blocks, 1, 1, threads, 1, 1, 0,
                            (CUstream)owner->stream, args, NULL), names[k]);
}
static void release_fine(void) {
    release_graph();
    cudaFree(fine_actions); fine_actions = NULL;
    for (int i = 0; i < 3; ++i) {
        if (fine_module[i]) cuModuleUnload(fine_module[i]);
        fine_module[i] = NULL; fine_fun[i] = NULL;
    }
}
extern "C" int blaze_driver_release(void) {
    if (!module) return 0;
    if (owner && cu_ck(cudaSetDevice(owner->device), "driver release device"))
        return -1;
    if (owner && cu_ck(cudaStreamSynchronize(owner->stream), "driver release sync"))
        return -1;
    release_fine();
    int rc = ck(cuModuleUnload(module), "module unload");
    module = NULL; owner = NULL; memset(fun, 0, sizeof fun);
    graph_enabled = 0; graph_builds = 0;
    return rc;
}
extern "C" int blaze_driver_configure(void *env, const char *cubin, int scalar_tpb) {
    CuVecCu *v = (CuVecCu *)env;
    if (!v || !cubin || (scalar_tpb < 1 || scalar_tpb > 128 || (scalar_tpb & (scalar_tpb - 1)))
        || (v->measure_split != 1 && v->measure_split != 2)
        || !v->d_split_exec || (v->measure_split == 2 && !v->d_split_player)) return -1;
    if (blaze_driver_release()) return -1;
    if (cu_ck(cudaSetDevice(v->device), "driver device")) return -1;
    owner = v; grain = scalar_tpb;
    if (ck(cuModuleLoad(&module, cubin), "module load")) { owner = NULL; return -1; }
    unsigned count = 0;
    if (ck(cuModuleGetFunctionCount(&count, module), "function count")) goto bad;
    {
        std::vector<CUfunction> all(count);
        if (ck(cuModuleEnumerateFunctions(all.data(), count, module), "enumerate")) goto bad;
        for (CUfunction f : all) {
            const char *raw = NULL;
            if (ck(cuFuncGetName(&raw, f), "function name")) goto bad;
            int status = 0;
            char *demangled = abi::__cxa_demangle(raw, NULL, NULL, &status);
            const char *name = status == 0 ? demangled : raw;
            for (int k = 0; k < NK; ++k) {
                size_t n = strlen(names[k]);
                if (!strncmp(name, names[k], n) && (name[n] == '(' || !name[n])) {
                    if (fun[k]) { free(demangled); goto bad; }
                    fun[k] = f;
                }
            }
            free(demangled);
        }
    }
    for (int k = 0; k < NK; ++k) if (!fun[k]) {
        fprintf(stderr, "driver dispatch: missing %s\n", names[k]); goto bad;
    }
    return 0;
bad:
    blaze_driver_release(); return -1;
}

/* Optional finer player boundary, after base configure and mode2 selection.
 * Calls execute the exact pre-actions/player/hazards sequence in three kernels.
 * Inputs are separately compiled cubins, never a source/runtime compiler. */
extern "C" int blaze_driver_configure_fine(const char *pre, const char *pure,
                                           const char *after) {
    if (!owner || !module || owner->measure_split != 2 || !pre || !pure || !after)
        return -1;
    if (cu_ck(cudaSetDevice(owner->device), "fine device") ||
        cu_ck(cudaStreamSynchronize(owner->stream), "fine sync")) return -1;
    release_fine();
    const char *paths[3] = {pre, pure, after};
    const char *symbols[3] = {"measure_pre_actions", "measure_pure_player",
                              "measure_after_player"};
    for (int i = 0; i < 3; ++i) {
        if (ck(cuModuleLoad(&fine_module[i], paths[i]), "fine module") ||
            ck(cuModuleGetFunction(&fine_fun[i], fine_module[i], symbols[i]),
               symbols[i])) { release_fine(); return -1; }
    }
    if (cu_ck(cudaMalloc(&fine_actions, (size_t)owner->n * sizeof(CuAction)),
              "fine action continuation")) { release_fine(); return -1; }
    return 0;
}

extern "C" int blaze_driver_step_full(void *env, const double *actions, int repeat,
    unsigned short *cam, unsigned char *depth, unsigned char *edge, float *scal,
    float *rew, unsigned char *done, float *pose, int *status) {
    CuVecCu *v = (CuVecCu *)env;
    if (!module || v != owner || !actions || repeat < 1 ||
        (v->measure_split != 1 && v->measure_split != 2)) return -1;
    if (cu_ck(cudaSetDevice(v->device), "driver step device")) return -1;
    int n = v->n;
    unsigned nb = (n + grain - 1) / grain;
    unsigned nw = (unsigned)(((size_t)n * 32 + 127) / 128);
    unsigned np = (unsigned)(((size_t)n * CU_NPIX + 127) / 128);
    auto enqueue = [&]() -> int {
    const double *inv = NULL;
    if (v->ktime) cudaEventRecord(v->ev[0], v->stream);
    void *begin[] = {&v->d_envs, &n, &v->d_st, &actions, &v->d_recipes,
                     &v->nrecipes, &inv, &v->d_split_exec};
    if (launch(BEGIN, nb, grain, begin)) return -1;
    for (int rep = 0; rep < repeat; ++rep) {
        void *recent[] = {&v->d_envs, &n, &v->d_split_exec};
        if (launch(RECENTER, nw, 128, recent)) return -1;
        void *pre[] = {&v->d_envs, &n, &v->d_st, &actions, &rep, &v->d_aabb,
                       &v->d_split_exec, &v->d_split_player};
        if (v->measure_split == 2) {
            if (fine_actions) {
                void *before[] = {&v->d_envs, &n, &v->d_st, &actions, &rep,
                                   &fine_actions, &v->d_split_exec};
                void *pure[] = {&v->d_envs, &n, &v->d_st, &fine_actions,
                                 &v->d_aabb, &v->d_split_player, &v->d_split_exec};
                void *after[] = {&v->d_envs, &n, &v->d_split_exec};
                void **params[3] = {before, pure, after};
                for (int j = 0; j < 3; ++j)
                    if (ck(cuLaunchKernel(fine_fun[j], nb, 1, 1, grain, 1, 1,
                        0, (CUstream)v->stream, params[j], NULL), "fine player")) return -1;
            } else if (launch(PLAYER, nb, grain, pre)) return -1;
            void *world[] = {&v->d_envs, &n, &v->d_split_exec, &v->d_split_player};
            if (launch(WORLD, nb, grain, world)) return -1;
        } else if (launch(PRE, nb, grain, pre)) return -1;
        if (launch(RANDOM, nb, grain, recent)) return -1;
        void *post[] = {&v->d_envs, &n, &v->d_st, &rep, &repeat, &v->d_split_exec};
        if (launch(POST, nb, grain, post)) return -1;
        void *reward[] = {&v->d_envs, &n, &rep, &repeat, &v->atk_gate,
                          &v->d_split_exec};
        if (launch(REWARD, nw, 128, reward)) return -1;
    }
    if (v->ktime) cudaEventRecord(v->ev[1], v->stream);
    void *obs[] = {&v->d_envs, &n, &v->d_st, &cam, &depth, &edge};
    if (launch(OBS, np, 128, obs)) return -1;
    if (v->ktime) cudaEventRecord(v->ev[2], v->stream);
    void *final[] = {&v->d_envs, &n, &v->d_st, &scal, &rew, &done, &pose,
                     &v->atk_gate, &status};
    if (launch(FINAL, (n + 127) / 128, 128, final)) return -1;
    if (v->ktime) cudaEventRecord(v->ev[3], v->stream);
    return 0;
    };
    if (graph_enabled) {
        /* Every captured by-value argument and launch/config choice is keyed.
         * Mutable simulation/observation CONTENTS are intentionally not keyed.
         * Changed addresses/repeat/mode/recipe count/reward gate rebuild before
         * execution. Fine/module reconfiguration also destroys this cache. */
        uint64_t gate_bits;
        memcpy(&gate_bits, &v->atk_gate, sizeof gate_bits);
        uint64_t key[40] = {
            (uint64_t)repeat, (uint64_t)n, (uint64_t)v->measure_split,
            (uint64_t)grain, (uint64_t)v->nrecipes, (uint64_t)v->ktime, gate_bits,
            (uintptr_t)v, (uintptr_t)v->stream,
            (uintptr_t)v->d_envs, (uintptr_t)v->d_st, (uintptr_t)v->d_recipes,
            (uintptr_t)v->d_aabb, (uintptr_t)v->d_split_exec,
            (uintptr_t)v->d_split_player, (uintptr_t)fine_actions,
            (uintptr_t)actions, (uintptr_t)cam, (uintptr_t)depth,
            (uintptr_t)edge, (uintptr_t)scal, (uintptr_t)rew, (uintptr_t)done,
            (uintptr_t)pose, (uintptr_t)status, (uintptr_t)v->ev[0],
            (uintptr_t)v->ev[1], (uintptr_t)v->ev[2], (uintptr_t)v->ev[3]
        };
        if (!graph_exec || memcmp(key, graph_key, sizeof key)) {
            release_graph();
            if (cu_ck(cudaStreamBeginCapture(v->stream, cudaStreamCaptureModeThreadLocal),
                      "graph capture begin")) return -1;
            int rc = enqueue();
            cudaError_t ended = cudaStreamEndCapture(v->stream, &graph);
            if (rc || cu_ck(ended, "graph capture end")) {
                release_graph(); return -1;
            }
            if (cu_ck(cudaGraphInstantiate(&graph_exec, graph, NULL, NULL, 0),
                      "graph instantiate")) { release_graph(); return -1; }
            memcpy(graph_key, key, sizeof key);
            ++graph_builds;
        }
        if (cu_ck(cudaGraphLaunch(graph_exec, v->stream), "graph launch")) return -1;
    } else if (enqueue()) return -1;
    if (cu_ck(cudaStreamSynchronize(v->stream), "driver step")) return -1;

    if (v->ktime) {
        float ms;
        cudaEventElapsedTime(&ms, v->ev[0], v->ev[1]); v->ms_tick += ms;
        cudaEventElapsedTime(&ms, v->ev[1], v->ev[2]); v->ms_obs += ms;
        cudaEventElapsedTime(&ms, v->ev[2], v->ev[3]); v->ms_final += ms;
        v->nsteps++;
    }
    int error = 0;
    if (cu_ck(cudaMemsetAsync(v->d_dimension_error, 0, sizeof(int), v->stream),
              "driver dimension clear")) return -1;
    void *dim[] = {&v->d_envs, &n, &v->d_dimension_error};
    if (launch(DIMENSION, (n + 127) / 128, 128, dim)) return -1;
    if (cu_ck(cudaMemcpyAsync(&error, v->d_dimension_error, sizeof error,
                              cudaMemcpyDeviceToHost, v->stream), "driver dimension read") ||
        cu_ck(cudaStreamSynchronize(v->stream), "driver dimension sync")) return -1;
    if (error) fprintf(stderr, "driver dispatch: dimension transfer failed (code %d)\n", error);
    return error ? -1 : 0;
}
