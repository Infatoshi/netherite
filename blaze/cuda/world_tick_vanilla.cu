/* CUDA driver for world_tick_vanilla - same core as the CPU path, one thread per world
 * (the driver is vanilla-sequential by design; parallelism is across worlds). Emits the
 * identical hex lines as cpu/world_tick_vanilla.c for the CPU==CUDA diff. */
#define MC_WORLD_R 0
#include <cstdio>
#include <cstdlib>
#include "../core/world_tick_vanilla.h"

#define WTV_TICKS 6
#define WTV_MAX_LINES 256

struct WtvCudaBox {
    World w;
    WtvState s;
    McSinTable st;
    u64 lines[WTV_MAX_LINES];
    int n_lines;
};

__device__ static void d_emit(WtvCudaBox *b, u64 v) { b->lines[b->n_lines++] = v; }

__device__ static void d_test_lcg(WtvCudaBox *b) {
    i32 lcg = 123456789;
    for (int i = 0; i < 6; ++i) {
        i32 j = wt_lcg_advance(&lcg);
        d_emit(b, (u64)(u32)lcg);
        d_emit(b, ((u64)(j & 15) << 16) | ((u64)((j >> 8) & 15) << 8) | (u64)((j >> 16) & 15));
    }
}

__device__ static void d_test_order_dedup_cap(WtvCudaBox *b) {
    McScheduledTicks *q = &b->s.stq;
    stq_init(q);
    stq_update_block_tick(q, 1, 0, 0, 1, 10, 0, 0, 0, 1);
    stq_update_block_tick(q, 2, 0, 0, 1, 5, 0, 0, 0, 1);
    stq_update_block_tick(q, 3, 0, 0, 1, 5, -1, 0, 0, 1);
    stq_update_block_tick(q, 4, 0, 0, 1, 5, -1, 0, 0, 1);
    stq_begin_tick(q, 0, 1);
    d_emit(b, (u64)q->this_tick[0].x << 24 | (u64)q->this_tick[1].x << 16 |
              (u64)q->this_tick[2].x << 8 | (u64)q->this_tick[3].x);
    stq_update_block_tick(q, 3, 0, 0, 1, 7, 0, 0, 0, 1);
    stq_end_tick(q);

    stq_init(q);
    stq_update_block_tick(q, 9, 9, 9, 1, 4, 0, 0, 0, 1);
    stq_update_block_tick(q, 9, 9, 9, 1, 9, 5, 0, 0, 1);
    stq_is_update_scheduled(q, 9, 9, 9, 1);
    stq_update_block_tick(q, 9, 9, 9, 2, 4, 0, 0, 0, 1);
    stq_update_block_tick(q, 9, 9, 8, 1, 4, 0, 0, 0, 0);
    d_emit(b, (u64)q->heap_n);

    stq_init(q);
    for (i32 i = 0; i < 70000; ++i)
        stq_update_block_tick(q, i, 0, 0, 1, 0, 0, 0, 0, 1);
    {
        i32 n = stq_begin_tick(q, 0, 0);
        stq_end_tick(q);
        d_emit(b, (u64)n);
        n = stq_begin_tick(q, 0, 0);
        stq_end_tick(q);
        d_emit(b, (u64)n);
    }
}

__device__ static u64 d_blocks_hash(const World *w) {
    u64 h = 0xcbf29ce484222325ULL;
    for (int i = 0; i < MC_CHUNK_VOL; ++i) {
        h ^= (u64)w->chunk[0].blocks[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

__device__ static void d_scene_init(World *w, u64 seed) {
    Chunk *c = &w->chunk[0];
    w->seed = seed;
    w->tick = 0;
    c->cx = 0; c->cz = 0;
    for (int y = 0; y < MC_CY; ++y)
        for (int z = 0; z < MC_CZ; ++z)
            for (int x = 0; x < MC_CX; ++x) {
                mc_set(c, x, y, z, mc_state(y < 5 ? BLK_STONE : BLK_AIR, 0));
                c->light[mc_idx(x, y, z)] = mc_light(15, 0);
            }
    for (int z = 0; z < MC_CZ; ++z)
        for (int x = 0; x < MC_CX; ++x)
            c->biome[z * MC_CX + x] = 1;
    mc_set(c, 5, 9, 5, mc_state(BLK_PLANKS, 0));
    mc_set(c, 5, 10, 5, mc_state(WT_BLK_FIRE, 0));
    mc_set(c, 10, 20, 10, mc_state(BLK_FLOWING_WATER, 0));
}

__global__ void run_wtv(WtvCudaBox *b) {
    if (threadIdx.x || blockIdx.x) return;
    b->n_lines = 0;
    d_test_lcg(b);
    d_emit(b, ((u64)wt_fire_flammability(47) << 32)
        | (u64)wt_fire_encouragement(47));
    d_emit(b, ((u64)wt_fire_flammability(173) << 32)
        | (u64)wt_fire_encouragement(173));
    d_emit(b, ((u64)wt_fire_flammability(170) << 48)
        | ((u64)wt_fire_encouragement(170) << 32)
        | ((u64)wt_fire_flammability(171) << 16)
        | (u64)wt_fire_encouragement(171));
    d_test_order_dedup_cap(b);

    {
        McGameRules gr = mc_gamerules_default();
        mc_sin_table_init(&b->st);
        d_scene_init(&b->w, 12345ULL);
        wt_vanilla_init(&b->s, 12345ULL);
        stq_update_block_tick(&b->s.stq, 10, 20, 10, BLK_FLOWING_WATER, 5, 0,
                              b->s.totalWorldTime, 0, 1);
        stq_update_block_tick(&b->s.stq, 5, 10, 5, WT_BLK_FIRE, 3, 0,
                              b->s.totalWorldTime, 0, 1);
        for (int t = 0; t < WTV_TICKS; ++t) {
            wt_vanilla_tick(&b->w, &b->s, &gr, &b->st, (MswScene *)0);
            wt_vanilla_update_entities(&b->w, &b->s);
            d_emit(b, (u64)b->s.totalWorldTime);
            d_emit(b, d_blocks_hash(&b->w));
            d_emit(b, (u64)(u32)b->s.stq.heap_n);
        }
        for (int i = 0; i < b->s.fired_n; ++i)
            d_emit(b, ((u64)(u32)b->s.fired[i].block << 32) | (u64)(u32)b->s.fired_at[i]);
    }
}

int main(void) {
    WtvCudaBox *d_b = NULL;
    if (cudaMalloc(&d_b, sizeof(WtvCudaBox)) != cudaSuccess) {
        fprintf(stderr, "cudaMalloc failed\n");
        return 1;
    }
    cudaDeviceSetLimit(cudaLimitStackSize, (size_t)512 * 1024);

    run_wtv<<<1, 1>>>(d_b);
    {
        cudaError_t err = cudaDeviceSynchronize();
        if (err != cudaSuccess) {
            fprintf(stderr, "cuda sync: %s\n", cudaGetErrorString(err));
            cudaFree(d_b);
            return 1;
        }
    }
    {
        static u64 h_lines[WTV_MAX_LINES];
        int n = 0;
        cudaMemcpy(&n, &d_b->n_lines, sizeof(int), cudaMemcpyDeviceToHost);
        cudaMemcpy(h_lines, d_b->lines, sizeof(u64) * WTV_MAX_LINES, cudaMemcpyDeviceToHost);
        for (int i = 0; i < n; ++i)
            printf("%016llx\n", (unsigned long long)h_lines[i]);
    }
    cudaFree(d_b);
    return 0;
}
