/* learn_bench: the learner's device time for one update of the trainer's shape (fastppo's Learner.train at the brute
 * recipe: GAE over a segment of T x gs, then epochs x minibatches of mb, each the forward, loss, backward, clip and
 * Adam), on the learner fixture's segment and weights.
 *
 *   learn_bench [--fixture DIR] [--updates U] [--warm W] [--graph 0|1] [--prec fp32|tf32|bf16] [--mb M]
 *
 * --mb M (lane/ntfc): minibatches of M instead of the fixture's (the trainer's at 256 envs is 4096: a segment of 64
 * steps of 64 envs), the segment's N / M of them an epoch.
 *
 * W updates unmeasured, then U updates timed by CUDA events on the learner's stream, one after another with no host
 * sync between them (graph 1: the update captured once as a CUDA graph and replayed). Prints the device time an
 * update (mean, min and max over the U), and samples trained a second (epochs x N over the mean). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "learn_io.h"

int main(int argc, char **argv) {
    char dir[4096];
    const char *croot = getenv("NETHER_CACHE");
    snprintf(dir, sizeof dir, "%s/native/learn-a05-070", (croot && croot[0]) ? croot : ".cache");
    int U = 20, W = 3, graph = 1, prec = NETH_LEARN_FP32, mb = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--fixture")) snprintf(dir, sizeof dir, "%s", argv[i + 1]);
        else if (!strcmp(argv[i], "--updates")) U = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--warm")) W = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--graph")) graph = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--mb")) mb = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--prec"))
            prec = !strcmp(argv[i + 1], "bf16") ? NETH_LEARN_BF16 : !strcmp(argv[i + 1], "tf32") ? NETH_LEARN_TF32 : NETH_LEARN_FP32;
        else {
            fprintf(stderr, "learn_bench: unknown %s\n", argv[i]);
            return 2;
        }
    }
    lio_seg s = lio_segment(dir);
    if (mb > 0) s.mb = mb < s.N ? mb : s.N;
    size_t P = NETH_LEARN_NPARAM * sizeof(float);
    neth_learn_hyper h = neth_learn_hyper_default();
    float entc = (float)lio_num(s.man, "ent");
    cudaStream_t st;
    cudaStreamCreateWithFlags(&st, cudaStreamNonBlocking);
    neth_learn *L = neth_learn_new(s.N, s.mb, prec, st);
    if (!L) return 2;
#if NETH_NET_RNN
    neth_learn_seq(L, s.T, s.done, s.h0);  /* lane/ntgru: the recurrent net's start states */
#endif
    float *w0 = lio_ntw(dir, "weights.ntw"), *m0 = lio_ntw(dir, "adam_m.ntw"), *v0 = lio_ntw(dir, "adam_v.ntw");
    float *dw = (float *)lio_dev(w0, P), *dm = (float *)lio_dev(m0, P), *dv = (float *)lio_dev(v0, P), *dg, *books;
    cudaMalloc((void **)&dg, P);
    cudaMalloc((void **)&books, NETH_LEARN_NBOOK * 4);
    int step = s.adam_step;
    cudaGraphExec_t ge = NULL;
    if (graph) {    /* the step count is a kernel argument: the graph replays one update's steps (timing only) */
        cudaGraph_t g;
        cudaStreamBeginCapture(st, cudaStreamCaptureModeThreadLocal);
        neth_learn_update(L, &h, entc, s.T, s.gs, s.scale, s.epochs, s.mb, s.perm, s.obs, s.sc, s.cam, s.keys, s.hot, s.logp,
                   s.val, s.rew, s.done, s.done, s.drv, s.boot, dw, dg, dm, dv, &step, books);
        if (cudaStreamEndCapture(st, &g) != cudaSuccess || cudaGraphInstantiate(&ge, g, 0) != cudaSuccess) {
            fprintf(stderr, "learn_bench: graph capture failed\n");
            return 2;
        }
    }
    cudaEvent_t *ev = (cudaEvent_t *)malloc((U + 1) * sizeof *ev);
    for (int i = 0; i <= U; i++) cudaEventCreate(&ev[i]);
    for (int i = 0; i < W + U; i++) {
        if (i >= W) cudaEventRecord(ev[i - W], st);
        if (ge)
            cudaGraphLaunch(ge, st);
        else
            neth_learn_update(L, &h, entc, s.T, s.gs, s.scale, s.epochs, s.mb, s.perm, s.obs, s.sc, s.cam, s.keys, s.hot,
                       s.logp, s.val, s.rew, s.done, s.done, s.drv, s.boot, dw, dg, dm, dv, &step, books);
    }
    cudaEventRecord(ev[U], st);
    cudaError_t e = cudaStreamSynchronize(st);
    if (e != cudaSuccess) {
        fprintf(stderr, "learn_bench: %s\n", cudaGetErrorString(e));
        return 2;
    }
    float tot = 0, mn = 1e30f, mx = 0;
    for (int i = 0; i < U; i++) {
        float ms;
        cudaEventElapsedTime(&ms, ev[i], ev[i + 1]);
        tot += ms;
        mn = ms < mn ? ms : mn;
        mx = ms > mx ? ms : mx;
    }
    float mean = tot / U;
    printf("learn_bench %s: N %d, %d epochs of mb %d, %s, graph %d: %.3f ms an update (min %.3f, max %.3f, %d updates), "
           "%.0f samples trained a second\n",
           NETH_NET_VARIANT, s.N, s.epochs, s.mb, prec == NETH_LEARN_BF16 ? "bf16" : prec == NETH_LEARN_TF32 ? "tf32" : "fp32", graph, mean, mn, mx, U, (double)s.epochs * s.N / (mean * 1e-3));
    neth_learn_free(L);
    return 0;
}
