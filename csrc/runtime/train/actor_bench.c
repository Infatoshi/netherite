/* actor_bench: the actor's device time a call (actor.h), by CUDA events around each call on one stream, the median
 * of N calls after a warm-up, at each batch size, launched directly and as a captured CUDA graph (the way a loop
 * would replay it), with the device RNG's noise; the mean of N calls queued back to back (finer than the events'
 * 1 us steps); and the samples a second the back-to-back replays imply.
 *
 *   actor_bench [--fixture DIR] [--n N] [--mode fp32|bf16] [B...]      (default the fixture's weights, N 300, B 128 512) */
#include "actor.h"
#include "net.h"

#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CK(x)                                                                                  \
    do {                                                                                       \
        cudaError_t e_ = (x);                                                                  \
        if (e_ != cudaSuccess) {                                                               \
            fprintf(stderr, "actor_bench: %s: %s\n", #x, cudaGetErrorString(e_));               \
            exit(2);                                                                           \
        }                                                                                      \
    } while (0)

static int cmpf(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}

/* the median, p10 and p90 of N timed calls (graph: replays of one captured call), and q[3] the mean of N calls
 * queued back to back between one pair of events (a loop's rate: no per-call event, finer than the events' 1 us) */
static void timeit(neth_actor_ws *w, int B, const neth_actor_io *io, cudaStream_t s, int N, int graph, float q[4])
{
    cudaGraphExec_t ge = NULL;
    if (graph) {
        cudaGraph_t g;
        CK(cudaStreamBeginCapture(s, cudaStreamCaptureModeThreadLocal));
        CK(neth_actor_step(w, B, io, s));
        CK(cudaStreamEndCapture(s, &g));
        CK(cudaGraphInstantiate(&ge, g, 0));
        CK(cudaGraphDestroy(g));
    }
    cudaEvent_t *ev = malloc(2 * N * sizeof *ev);
    for (int i = 0; i < 2 * N; i++)
        CK(cudaEventCreate(&ev[i]));
    for (int i = 0; i < 50; i++)
        CK(graph ? cudaGraphLaunch(ge, s) : (cudaError_t)neth_actor_step(w, B, io, s));
    for (int i = 0; i < N; i++) {
        CK(cudaEventRecord(ev[2 * i], s));
        CK(graph ? cudaGraphLaunch(ge, s) : (cudaError_t)neth_actor_step(w, B, io, s));
        CK(cudaEventRecord(ev[2 * i + 1], s));
    }
    CK(cudaStreamSynchronize(s));
    float *t = malloc(N * sizeof *t);
    for (int i = 0; i < N; i++) {
        CK(cudaEventElapsedTime(&t[i], ev[2 * i], ev[2 * i + 1]));
        t[i] *= 1000;
    }
    qsort(t, N, sizeof *t, cmpf);
    q[0] = t[N / 2];
    q[1] = t[N / 10];
    q[2] = t[9 * N / 10];
    CK(cudaEventRecord(ev[0], s));
    for (int i = 0; i < N; i++)
        CK(graph ? cudaGraphLaunch(ge, s) : (cudaError_t)neth_actor_step(w, B, io, s));
    CK(cudaEventRecord(ev[1], s));
    CK(cudaEventSynchronize(ev[1]));
    CK(cudaEventElapsedTime(&q[3], ev[0], ev[1]));
    q[3] *= 1000.f / N;
    for (int i = 0; i < 2 * N; i++)
        cudaEventDestroy(ev[i]);
    free(ev);
    free(t);
    if (ge)
        cudaGraphExecDestroy(ge);
}

int main(int argc, char **argv)
{
    char dir[4096];
    const char *croot = getenv("NETHER_CACHE");
    snprintf(dir, sizeof dir, "%s/native/actor-a05-070", (croot && croot[0]) ? croot : ".cache");
    int N = 300, mode = NETH_NET_ACTOR_FP32, bs[16], nb = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--fixture") && i + 1 < argc)
            snprintf(dir, sizeof dir, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--n") && i + 1 < argc)
            N = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc)
            mode = !strcmp(argv[++i], "bf16") ? NETH_NET_ACTOR_BF16 : NETH_NET_ACTOR_FP32;
        else if (nb < 16 && atoi(argv[i]) > 0)
            bs[nb++] = atoi(argv[i]);
        else {
            fprintf(stderr, "usage: actor_bench [--fixture DIR] [--n N] [--mode fp32|bf16] [B...]\n");
            return 2;
        }
    }
    if (!nb) {
        bs[nb++] = 128;
        bs[nb++] = 512;
    }
    int maxb = 0;
    for (int i = 0; i < nb; i++)
        maxb = bs[i] > maxb ? bs[i] : maxb;
    char p[4200];
    snprintf(p, sizeof p, "%s/weights.ntw", dir);
    const char *err = NULL;
    neth_actor *a = neth_actor_load(p, &err);
    if (!a) {
        fprintf(stderr, "actor_bench: %s: %s\n", p, err);
        return 2;
    }
    neth_actor_set_mode(a, mode);
    neth_actor_ws *w = neth_actor_ws_new(a, maxb, 7);
    /* the inputs: the fixture's first maxb samples, repeated if it has fewer */
    uint8_t *obs = malloc((size_t)maxb * NETH_NET_H * NETH_NET_W * NETH_NET_C);
    float *sc = malloc((size_t)maxb * NETH_NET_NS * 4);
    {
        snprintf(p, sizeof p, "%s/obs.u8", dir);
        FILE *f = fopen(p, "rb");
        snprintf(p, sizeof p, "%s/sc.f32", dir);
        FILE *g = fopen(p, "rb");
        if (!f || !g) {
            fprintf(stderr, "actor_bench: no fixture at %s\n", dir);
            return 2;
        }
        size_t ho = fread(obs, NETH_NET_H * NETH_NET_W * NETH_NET_C, maxb, f), hs = fread(sc, NETH_NET_NS * 4, maxb, g);
        for (size_t i = ho; i < (size_t)maxb; i++)
            memcpy(obs + i * NETH_NET_H * NETH_NET_W * NETH_NET_C, obs + (i % ho) * NETH_NET_H * NETH_NET_W * NETH_NET_C, NETH_NET_H * NETH_NET_W * NETH_NET_C);
        for (size_t i = hs; i < (size_t)maxb; i++)
            memcpy(sc + i * NETH_NET_NS, sc + (i % hs) * NETH_NET_NS, NETH_NET_NS * 4);
        fclose(f);
        fclose(g);
    }
    uint8_t *dobs;
    neth_sc_t *dsc;
    float *logp, *value;
    int32_t *act;
    CK(cudaMalloc((void **)&dobs, (size_t)maxb * NETH_NET_H * NETH_NET_W * NETH_NET_C));
    CK(cudaMalloc((void **)&dsc, (size_t)maxb * NETH_NET_NS * sizeof(neth_sc_t)));
    CK(cudaMalloc((void **)&act, (size_t)maxb * NETH_NET_ACTION * 4));
    CK(cudaMalloc((void **)&logp, maxb * 4));
    CK(cudaMalloc((void **)&value, maxb * 4));
    CK(cudaMemcpy(dobs, obs, (size_t)maxb * NETH_NET_H * NETH_NET_W * NETH_NET_C, cudaMemcpyHostToDevice));
#if NETH_SC_BF16
    {
        size_t nsc = (size_t)maxb * NETH_NET_NS;
        neth_sc_t *hbf = malloc(nsc * sizeof(neth_sc_t));
        for (size_t i = 0; i < nsc; i++) hbf[i] = neth_f32_to_bf16_host(sc[i]);
        CK(cudaMemcpy(dsc, hbf, nsc * sizeof(neth_sc_t), cudaMemcpyHostToDevice));
        free(hbf);
    }
#else
    CK(cudaMemcpy(dsc, sc, (size_t)maxb * NETH_NET_NS * 4, cudaMemcpyHostToDevice));
#endif
    cudaStream_t s;
    CK(cudaStreamCreateWithFlags(&s, cudaStreamNonBlocking));
    neth_actor_io io = {dobs, dsc, act, logp, value, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
#if NETH_NET_RNN
    CK(cudaMalloc((void **)&io.h, (size_t)maxb * NETH_NET_D * 4));    /* lane/ntgru: the envs' state, carried call to call */
    CK(cudaMemset(io.h, 0, (size_t)maxb * NETH_NET_D * 4));
#endif
    for (int i = 0; i < nb; i++) {
        float d[4], g[4];
        timeit(w, bs[i], &io, s, N, 0, d);
        timeit(w, bs[i], &io, s, N, 1, g);
        printf("actor %s %s B %d: %.1f us a call launched (p10 %.1f, p90 %.1f), %.1f us a graph replay (p10 %.1f, p90 %.1f); "
               "back to back %.2f us a call, %.2f a replay: "
               "%.0f samples/s\n",
               NETH_NET_VARIANT, mode == NETH_NET_ACTOR_BF16 ? "bf16" : "fp32", bs[i], d[0], d[1], d[2], g[0], g[1], g[2], d[3], g[3],
               bs[i] / g[3] * 1e6);
    }
    return 0;
}
