/* learn_check's and learn_bench's fixture reading (scripts/ntref.py learner's directory): its files whole, net.h's
 * weight blobs into the learner's layout, the manifest's numbers, and the segment on the device. */
#ifndef NETH_NET_LEARN_IO_H
#define NETH_NET_LEARN_IO_H

#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "learn.h"

static void *lio_read(const char *dir, const char *name, size_t want) {
    char p[4096];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb");
    if (!f) {
        fprintf(stderr, "%s: cannot open\n", p);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (want && (size_t)n != want) {
        fprintf(stderr, "%s: %ld bytes, %zu expected\n", p, n, want);
        exit(2);
    }
    char *b = (char *)malloc(n + 1);
    if (fread(b, 1, n, f) != (size_t)n) {
        fprintf(stderr, "%s: short read\n", p);
        exit(2);
    }
    b[n] = 0;
    fclose(f);
    return b;
}

/* a .ntw blob into the layout (host, NETH_LEARN_NPARAM floats) */
static float *lio_ntw(const char *dir, const char *name) {
    char *b = (char *)lio_read(dir, name, 0), *q = b + 8;
    if (memcmp(b, "NTW1", 4)) {
        fprintf(stderr, "%s/%s: not an NTW1 blob\n", dir, name);
        exit(2);
    }
    int cnt;
    memcpy(&cnt, q, 4);
    q += 4;
    if (cnt != NETH_NET_TENSORS) {
        fprintf(stderr, "%s/%s: %d tensors\n", dir, name, cnt);
        exit(2);
    }
    const float *t[NETH_NET_TENSORS];
    for (int i = 0; i < NETH_NET_TENSORS; i++) {
        int rank, n = 1, d;
        memcpy(&rank, q, 4);
        q += 4;
        for (int k = 0; k < rank; k++, q += 4) {
            memcpy(&d, q, 4);
            n *= d;
        }
        t[i] = (const float *)q;
        q += 4 * (size_t)n;
    }
    float *flat = (float *)malloc(NETH_LEARN_NPARAM * sizeof(float));
    neth_learn_from_ntw(t, flat);
    free(b);
    return flat;
}

/* the manifest's top-level number KEY */
static double lio_num(const char *man, const char *key) {
    char k[128];
    snprintf(k, sizeof k, "\"%s\":", key);
    const char *p = strstr(man, k);
    if (!p) {
        fprintf(stderr, "manifest: no %s\n", key);
        exit(2);
    }
    return strtod(p + strlen(k), NULL);
}

static void *lio_dev(const void *h, size_t n) {
    void *d;
    if (cudaMalloc(&d, n) != cudaSuccess || cudaMemcpy(d, h, n, cudaMemcpyHostToDevice) != cudaSuccess) {
        fprintf(stderr, "lio_dev: %zu bytes\n", n);
        exit(2);
    }
    return d;
}

/* the segment on the device (Seg's tensors, T-major) */
typedef struct lio_seg {
    int T, gs, N;
    uint8_t *obs, *done, *drv;
    neth_sc_t *sc;
    float *keys, *logp, *val, *rew, *boot;
    float *h0;                  /* (NETH_NET_RNN) [gs][D] each env's state at the segment's start */
    int32_t *cam, *perm;
    int32_t *hot;       /* NETH_NET_HOTBAR: hot.i32 [N], the hotbar's draws (NULL otherwise) */
    float scale;
    int epochs, mb, adam_step;
    char *man;
} lio_seg;

static void *lio_rdev(const char *dir, const char *name, size_t n) {
    void *h = lio_read(dir, name, n), *d = lio_dev(h, n);
    free(h);
    return d;
}

static lio_seg lio_segment(const char *dir) {
    lio_seg s;
    s.man = (char *)lio_read(dir, "manifest.json", 0);
    s.T = (int)lio_num(s.man, "T");
    s.gs = (int)lio_num(s.man, "gs");
    s.N = s.T * s.gs;
    s.scale = (float)lio_num(s.man, "scale");
    s.epochs = (int)lio_num(s.man, "epochs");
    s.mb = (int)lio_num(s.man, "mb");
    s.adam_step = (int)lio_num(s.man, "adam_step");
    size_t N = s.N;
    s.obs = (uint8_t *)lio_rdev(dir, "obs.u8", N * NETH_NET_H * NETH_NET_W * NETH_NET_C);
#if NETH_SC_BF16
    {
        size_t nsc = N * NETH_NET_NS;
        float *hsc = (float *)lio_read(dir, "sc.f32", nsc * 4);
        neth_sc_t *hbf = (neth_sc_t *)malloc(nsc * sizeof(neth_sc_t));
        for (size_t i = 0; i < nsc; i++) hbf[i] = neth_f32_to_bf16_host(hsc[i]);
        s.sc = (neth_sc_t *)lio_dev(hbf, nsc * sizeof(neth_sc_t));
        free(hsc), free(hbf);
    }
#else
    s.sc = (neth_sc_t *)lio_rdev(dir, "sc.f32", N * NETH_NET_NS * 4);
#endif
    s.cam = (int32_t *)lio_rdev(dir, "cam.i32", N * NETH_NET_CAM * 4);
    s.keys = (float *)lio_rdev(dir, "keys.f32", N * NETH_NET_KEYS * 4);
    s.hot = NETH_NET_HOTBAR ? (int32_t *)lio_rdev(dir, "hot.i32", N * 4) : NULL;
    s.logp = (float *)lio_rdev(dir, "logp.f32", N * 4);
    s.val = (float *)lio_rdev(dir, "val.f32", N * 4);
    s.rew = (float *)lio_rdev(dir, "rew.f32", N * 4);
    s.done = (uint8_t *)lio_rdev(dir, "done.u8", N);
    s.drv = (uint8_t *)lio_rdev(dir, "drv.u8", N);
    s.boot = (float *)lio_rdev(dir, "boot.f32", s.gs * 4);
#if NETH_NET_RNN
    /* the recurrent net's (lane/ntgru): each epoch's order is of the envs (perm.i32 [epochs][gs]), made into
     * neth_learn_update's column form at the fixture's minibatch (mb / T envs); the start states h0.f32 [gs][D] (the
     * caller's neth_learn_seq) */
    {
        int32_t *pe = (int32_t *)lio_read(dir, "perm.i32", (size_t)s.epochs * s.gs * 4);
        int32_t *pc = (int32_t *)malloc((size_t)s.epochs * N * 4);
        for (int e = 0; e < s.epochs; e++) neth_learn_cols(pe + (size_t)e * s.gs, s.T, s.gs, s.mb / s.T, pc + (size_t)e * N);
        s.perm = (int32_t *)lio_dev(pc, (size_t)s.epochs * N * 4);
        free(pe), free(pc);
    }
    s.h0 = (float *)lio_rdev(dir, "h0.f32", (size_t)s.gs * NETH_NET_D * 4);
#else
    s.perm = (int32_t *)lio_rdev(dir, "perm.i32", (size_t)s.epochs * N * 4);
    s.h0 = NULL;
#endif
    return s;
}

#endif
