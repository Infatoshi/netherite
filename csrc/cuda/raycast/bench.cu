/* The device cast's kernel alone on stored inputs (lane/rcsplit): a dump
 * (rcdev_dump: the tables, each env's head and ring, and the frames and seen
 * records the device drew from them) loaded to the device, the cast launched
 * on it again, its frames and seen records against the dump's byte for byte,
 * and its time a launch. The kernel's inner loop: an edit to cast.cu, its
 * unit rebuilt and this run, in seconds (the full gate stays
 * gpu-raycast-check).
 *   out/native/cuda/rcbench DUMP [--reps N]   (make -C csrc gpu-raycast-bench) */
#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <vector>

#include "kern.h"

#ifdef RC_STATS
extern "C" int rc_stats_set(unsigned *p);
#endif

static FILE *in;
static void need(void *p, size_t n)
{
    if (fread(p, 1, n, in) != n)
    {
        fprintf(stderr, "rcbench: the dump ends early\n");
        exit(2);
    }
}
static void *dev_read(size_t n)
{
    void *h = malloc(n ? n : 1), *d = NULL;
    need(h, n);
    if (cudaMalloc(&d, n ? n : 16) != cudaSuccess || cudaMemcpy(d, h, n, cudaMemcpyHostToDevice) != cudaSuccess)
    {
        fprintf(stderr, "rcbench: device memory\n");
        exit(2);
    }
    free(h);
    return d;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: rcbench DUMP [--reps N]\n");
        return 2;
    }
    int reps = 20, fp_override = -1;
    for (int i = 2; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--reps") && i + 1 < argc) reps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fp") && i + 1 < argc) fp_override = atoi(argv[++i]);
    }
    in = fopen(argv[1], "rb");
    if (in == NULL)
    {
        perror(argv[1]);
        return 2;
    }
    char magic[8];
    int32_t hd[6];
    struct rcd_tabdev t;
    need(magic, 8);
    if (memcmp(magic, "RCDUMP1", 8))
    {
        fprintf(stderr, "rcbench: not a dump\n");
        return 2;
    }
    need(hd, sizeof hd);
    need(&t, sizeof t);
    const int k = hd[0], w = hd[1], h = hd[2], R = hd[3], nspr = hd[4];
    const size_t nf = (size_t)hd[5], RR = (size_t)R * R, px = (size_t)w * h * 3;
    t.tex = (const uint32_t(*)[256])dev_read((size_t)nspr * 256 * 4);
    t.face[0] = (const uint16_t *)dev_read(nf * 2);
    t.face[1] = (const uint16_t *)dev_read(nf * 2);
    t.bounds = (const double *)dev_read(nf * 8);
    t.kind = (const uint8_t *)dev_read(4096);
    t.tint = (const uint8_t *)dev_read(4096);
    t.emit = (const uint8_t *)dev_read(4096);
    t.cull_same = (const uint8_t *)dev_read(4096);
    t.grass = (const float(*)[3])dev_read(768 * 4);
    t.foliage = (const float(*)[3])dev_read(768 * 4);
    t.water = (const float(*)[3])dev_read(768 * 4);
    if (rc_tables_set(&t))
    {
        fprintf(stderr, "rcbench: the tables' symbol\n");
        return 2;
    }
    std::vector<struct rcd_head> heads(k);
    std::vector<uint8_t> frames((size_t)k * px), got((size_t)k * px);
    std::vector<struct rcd_seen> seen(k), gseen(k);
    for (int i = 0; i < k; ++i)
    {
        need(&heads[i], sizeof heads[i]);
        need(&frames[(size_t)i * px], px);
        need(&seen[i], sizeof seen[i]);
    }
    std::vector<struct rcd_col> cols((size_t)k * RR);
    struct rcd_sec *secs_h = (struct rcd_sec *)calloc((size_t)k * RR * 16, sizeof *secs_h);
    size_t nsec = 0;
    for (int i = 0; i < k; ++i)
    {
        need(&cols[(size_t)i * RR], RR * sizeof(struct rcd_col));
        for (;;)
        {
            int32_t at[2];
            need(at, sizeof at);
            if (at[0] < 0) break;
            need(&secs_h[((size_t)i * RR + (size_t)at[0]) * 16 + at[1]], sizeof *secs_h);
            rc_sec_order(&secs_h[((size_t)i * RR + (size_t)at[0]) * 16 + at[1]], 1);   /* into the device's cell order */
            ++nsec;
        }
    }
    fclose(in);
    struct rcd_head *d_heads;
    struct rcd_col *d_cols;
    struct rcd_sec *d_secs;
    uint8_t *d_frames;
    unsigned long long *d_key;
    int *d_n, *d_xyz, *d_ids;
    struct rcd_seen *d_seen;
    std::vector<int> ids(k);
    for (int i = 0; i < k; ++i) ids[i] = i;
    if (cudaMalloc(&d_heads, k * sizeof *d_heads) || cudaMalloc(&d_cols, cols.size() * sizeof cols[0]) ||
        cudaMalloc(&d_secs, (size_t)k * RR * 16 * sizeof *d_secs) || cudaMalloc(&d_frames, frames.size()) ||
        cudaMalloc(&d_key, (size_t)k * RCD_SEEN * 8) || cudaMalloc(&d_n, (size_t)k * RCD_SEEN * 4) ||
        cudaMalloc(&d_xyz, (size_t)k * w * h * 3 * 4) || cudaMalloc(&d_ids, k * 4) || cudaMalloc(&d_seen, k * sizeof *d_seen))
    {
        fprintf(stderr, "rcbench: device memory\n");
        return 2;
    }
    cudaMemcpy(d_heads, heads.data(), k * sizeof *d_heads, cudaMemcpyHostToDevice);
    cudaMemcpy(d_cols, cols.data(), cols.size() * sizeof cols[0], cudaMemcpyHostToDevice);
    cudaMemcpy(d_secs, secs_h, (size_t)k * RR * 16 * sizeof *d_secs, cudaMemcpyHostToDevice);
    cudaMemcpy(d_ids, ids.data(), k * 4, cudaMemcpyHostToDevice);
    free(secs_h);
    cudaStream_t s;
    cudaStreamCreate(&s);
#ifdef RC_STATS
    unsigned *d_st;
    const size_t np = (size_t)k * w * h;
    cudaMalloc(&d_st, np * 20);
    cudaMemset(d_st, 0, np * 20);
    rc_stats_set(d_st);
#endif
    cudaMemset(d_frames, 0, frames.size());
    /* the dump's instance (its heads', raycast.h "The two instances") */
    const int fp = fp_override >= 0 ? fp_override : heads[0].fp;
    if (fp == RCD_FP_FIXED) printf("rcbench: the fixed cast\n");
    else if (fp == RCD_FP_FAST16) printf("rcbench: the fast16 cast\n");
    else if (fp == RCD_FP_MIXED16) printf("rcbench: the mixed16 cast\n");
    else if (fp == RCD_FP_FAST32) printf("rcbench: the fast32 cast\n");
    else printf("rcbench: the FP%d cast\n", fp == 32 || fp == 16 ? fp : 64);
    rc_launch_cast(d_heads, d_cols, d_secs, d_ids, k, R, w, h, 3, d_frames, d_key, d_n, d_xyz, d_seen, fp, s);
    cudaMemcpy(got.data(), d_frames, got.size(), cudaMemcpyDeviceToHost);
    cudaMemcpy(gseen.data(), d_seen, k * sizeof *d_seen, cudaMemcpyDeviceToHost);
#ifdef RC_STATS
    {
        /* each pixel's counters (cast.cu RC_STAT), and over each warp (32 pixels in launch order): the sum against
         * 32 times the warp's longest, the lanes a step keeps busy */
        std::vector<unsigned> st(np * 5);
        cudaMemcpy(st.data(), d_st, np * 20, cudaMemcpyDeviceToHost);
        const char *nm[5] = {"air steps", "section jumps", "general turns", "outer turns", "in air bricks"};
        for (int c = 0; c < 5; ++c)
        {
            double sum = 0, wmax = 0;
            unsigned mx = 0;
            for (size_t q = 0; q < np; q += 32)
            {
                unsigned m = 0;
                for (size_t l = q; l < q + 32 && l < np; ++l)
                {
                    unsigned v = st[l * 5 + c];
                    sum += v, m = v > m ? v : m;
                }
                wmax += 32.0 * m, mx = m > mx ? m : mx;
            }
            printf("stats: %-14s %8.2f a ray, max %u; a warp's lanes busy %.1f%%\n", nm[c], sum / np, mx,
                   wmax > 0 ? 100.0 * sum / wmax : 0.0);
        }
        /* per pixel row of the frame: the air steps' mean (where the long rays are) */
        printf("stats: air steps by frame row (every 8th):");
        for (int j = 0; j < h; j += 8)
        {
            double sum = 0;
            for (int e = 0; e < k; ++e)
                for (int i = 0; i < w; ++i) sum += st[(((size_t)e * h + j) * w + i) * 5];
            printf(" %.0f", sum / (k * w));
        }
        printf("\n");
    }
#endif
    cudaError_t e = cudaGetLastError();
    if (e != cudaSuccess)
    {
        fprintf(stderr, "rcbench: %s\n", cudaGetErrorString(e));
        return 2;
    }
    long bad = 0, first = -1;
    for (size_t i = 0; i < got.size(); ++i)
        if (got[i] != frames[i])
        {
            if (first < 0) first = (long)i;
            ++bad;
        }
    int sbad = memcmp(gseen.data(), seen.data(), k * sizeof *d_seen) != 0;
    cudaEvent_t e0, e1;
    cudaEventCreate(&e0);
    cudaEventCreate(&e1);
    std::vector<float> ms;
    for (int r = 0; r < 7; ++r)
    {
        cudaEventRecord(e0, s);
        for (int i = 0; i < reps; ++i)
            rc_launch_cast(d_heads, d_cols, d_secs, d_ids, k, R, w, h, 3, d_frames, d_key, d_n, d_xyz, d_seen, fp, s);
        cudaEventRecord(e1, s);
        cudaEventSynchronize(e1);
        float m;
        cudaEventElapsedTime(&m, e0, e1);
        ms.push_back(m / reps);
    }
    std::sort(ms.begin() + 1, ms.end());
    printf("rcbench: %d envs at %dx%d, %zu sections; frames %s", k, w, h, nsec, bad ? "DIFFER" : "equal");
    if (bad)
    {
        long p = first / 3;
        printf(" (%ld bytes; first env %ld pixel %ld,%ld)", bad, p / (w * h), p % (w * h) % w, p % (w * h) / w);
    }
    printf(", seen %s; %.4f ms a launch (median of 6, %d launches each; min %.4f)\n", sbad ? "DIFFER" : "equal", ms[3], reps,
           ms[1]);
    return bad || sbad ? 1 : 0;
}
