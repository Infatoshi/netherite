/* mob_spawning_passive: findChunksForSpawning CREATURE pass on the verified pop_run 2x2 world.
 *
 * INTERNAL verify (CPU==CUDA). Runtime hash RNG keyed (seed,tick,x,y,z,purpose) per SPEC rule 1.
 * READ-ONLY compose: mob_spawning.h (spawn cycle structure/constants), populate_animals.h
 * (block checks + passive mob weights). Scene: pop_run(seed) then one passive spawn cycle with
 * player at (16.5,70,16.5) chunk (1,1); eligible interior chunks (0,0)..(1,1). Spawn records
 * packed u64 hex (attempt,x,y,z,biome,block_below,result,mob_type) same layout as populate_animals.
 *
 * CUT: entity alloc/collision/onInitialSpawn, Forge events, sheep/wolf/rabbit, biome WeightedRandom
 * (hash pick cow/pig/chicken like populate_animals), PlayerChunkMap/world border, existing mob cap
 * tracking (existing=0). Animal checks: grass below + w_light>8 + pa_valid_empty_spawn. */
#ifndef MC_MOB_SPAWNING_PASSIVE_H
#define MC_MOB_SPAWNING_PASSIVE_H

#include "populate.h"
#include "populate_animals.h"
#include "mc_rng.h"

#define MSP_MAX_RECORDS   4096
#define MSP_ANIMAL_CAP    10
#define MSP_MOB_COUNT_DIV 289
#define MSP_MAX_CHUNKS    4

#define MSP_PLAYER_X      16.5f
#define MSP_PLAYER_Y      70.0f
#define MSP_PLAYER_Z      16.5f
#define MSP_PLAYER_CX     1
#define MSP_PLAYER_CZ     1

#define MSP_WORLD_SPAWN_X 0
#define MSP_WORLD_SPAWN_Y 200
#define MSP_WORLD_SPAWN_Z 0

enum {
    MSP_PURPOSE_BASE    = 1,
    MSP_PURPOSE_GROUP   = 2,
    MSP_PURPOSE_WALK    = 3,
    MSP_PURPOSE_MOB     = 5,
    MSP_PURPOSE_SHUFFLE = 6,
};

enum {
    MSP_RES_SPAWN           = 0,
    MSP_RES_FAIL_BLOCK      = 1,
    MSP_RES_FAIL_BOUNDS     = 2,
    MSP_RES_FAIL_TOP        = 3,
    MSP_RES_FAIL_PLAYER     = 4,
    MSP_RES_FAIL_LIGHT      = 5,
    MSP_RES_FAIL_CHUNK_DIST = 6,
    MSP_RES_FAIL_INIT_SOLID = 7,
};

typedef struct {
    u64 seed;
    i64 tick;
    int n_records;
    u64 records[MSP_MAX_RECORDS];
} MspScene;

typedef struct {
    int cx;
    int cz;
    u64 order;
} MspChunkEntry;

MC_HD static inline void msp_record(MspScene *s, u64 rec) {
    if (s->n_records < MSP_MAX_RECORDS)
        s->records[s->n_records++] = rec;
}

MC_HD static inline float msp_dist_sq(float ax, float ay, float az, float bx, float by, float bz) {
    float dx = ax - bx, dy = ay - by, dz = az - bz;
    return dx * dx + dy * dy + dz * dz;
}

MC_HD static inline float msp_chunk_dist_sq(int cx, int cz, float fx, float fz) {
    float cx8 = (float)(cx * 16 + 8);
    float cz8 = (float)(cz * 16 + 8);
    float dx = cx8 - fx;
    float dz = cz8 - fz;
    return dx * dx + dz * dz;
}

MC_HD static inline int msp_initial_air(const World *w, int x, int y, int z) {
    int pb = w_get(w, x, y, z);
    int id = pa_pb_blk_id(pb);
    if (id != BLK_AIR) {
        BptProps p = mc_bpt_props(id);
        if (p.flags & BF_LIQUID) return 0;
        if (p.flags & BF_SOLID) return 0;
    }
    return 1;
}

MC_HD static inline int msp_can_animal_spawn(const World *w, int x, int y, int z) {
    if (!w_inb(x, y, z) || !w_inb(x, y + 1, z) || y < 1) return 0;
    if (pa_pb_blk_id(w_get(w, x, y - 1, z)) != BLK_GRASS) return 0;
    if (!pa_valid_empty_spawn_pb(w_get(w, x, y, z))) return 0;
    if (!pa_valid_empty_spawn_pb(w_get(w, x, y + 1, z))) return 0;
    if (w_light(w, x, y, z) <= 8) return 0;
    return 1;
}

MC_HD static inline u8 msp_pick_passive(u64 seed, i64 tick, int x, int y, int z) {
    u64 h = mc_hash_seed(seed, tick, x, y, z, MSP_PURPOSE_MOB);
    i32 roll = mc_hash_bound(h, PA_PASSIVE_TOTAL_WEIGHT);
    if (roll < 8) return (u8)PA_ENT_COW;
    if (roll < 18) return (u8)PA_ENT_PIG;
    return (u8)PA_ENT_CHICKEN;
}

MC_HD static inline void msp_fill_chunks(MspChunkEntry *out, int *n_out) {
    static const int k_cx[MSP_MAX_CHUNKS] = {0, 1, 0, 1};
    static const int k_cz[MSP_MAX_CHUNKS] = {0, 0, 1, 1};
    int i;
    *n_out = MSP_MAX_CHUNKS;
    for (i = 0; i < MSP_MAX_CHUNKS; ++i) {
        out[i].cx = k_cx[i];
        out[i].cz = k_cz[i];
        out[i].order = 0;
    }
}

MC_HD static inline void msp_sort_chunks(MspChunkEntry *arr, int n, u64 seed, i64 tick) {
    int i;
    for (i = 0; i < n; ++i)
        arr[i].order = mc_hash_seed(seed, tick, arr[i].cx, arr[i].cz, 0, MSP_PURPOSE_SHUFFLE);
    for (i = 1; i < n; ++i) {
        MspChunkEntry key = arr[i];
        int j = i - 1;
        while (j >= 0 && (arr[j].order > key.order ||
               (arr[j].order == key.order &&
                (arr[j].cx > key.cx || (arr[j].cx == key.cx && arr[j].cz > key.cz))))) {
            arr[j + 1] = arr[j];
            --j;
        }
        arr[j + 1] = key;
    }
}

MC_HD static inline int msp_chunk_is_edge(int cx, int cz) {
    int dx = cx - MSP_PLAYER_CX;
    int dz = cz - MSP_PLAYER_CZ;
    return dx == -8 || dx == 8 || dz == -8 || dz == 8;
}

MC_HD static inline void msp_spawn_chunk(MspScene *s, const World *w, int cx, int cz) {
    int base_x, base_y, base_z;
    u64 h;
    int k2, i4, l3;
    int ax, ay, az;
    float fx, fy, fz;
    int x0 = cx * 16;
    int z0 = cz * 16;

    h = mc_hash_seed(s->seed, s->tick, cx, cz, 0, MSP_PURPOSE_BASE);
    base_x = x0 + mc_hash_bound(h, 16);
    h = mc_hash64(h + 1);
    base_z = z0 + mc_hash_bound(h, 16);
    {
        int hm = w_height(w, base_x, base_z);
        int y_bound = hm + 16 - 1;
        if (y_bound < 1) y_bound = POP_SEA_LEVEL + 1;
        if (y_bound >= W_Y) y_bound = W_Y - 1;
        h = mc_hash64(h + 2);
        base_y = mc_hash_bound(h, y_bound);
    }

    if (!msp_initial_air(w, base_x, base_y, base_z)) {
        int biome = w_getBiome(w, base_x, base_z);
        int below = (base_y >= 1) ? pa_pb_blk_id(w_get(w, base_x, base_y - 1, base_z)) : 0;
        msp_record(s, pa_pack_record(s->n_records, base_x, base_y, base_z, biome, below,
                                       MSP_RES_FAIL_INIT_SOLID, 0));
        return;
    }

    for (k2 = 0; k2 < 3; ++k2) {
        h = mc_hash_seed(s->seed, s->tick, base_x, base_y, base_z, MSP_PURPOSE_GROUP);
        h = mc_hash64(h ^ (u64)k2);
        l3 = 1 + mc_hash_bound(h, 4);

        ax = base_x;
        ay = base_y;
        az = base_z;

        for (i4 = 0; i4 < l3; ++i4) {
            int attempt = s->n_records;
            u64 hw = mc_hash_seed(s->seed, s->tick, ax, ay, az, MSP_PURPOSE_WALK);
            hw = mc_hash64(hw ^ (u64)(k2 * 16 + i4));
            {
                i32 a = mc_hash_bound(hw, 6);
                hw = mc_hash64(hw + 1);
                i32 b = mc_hash_bound(hw, 6);
                ax += a - b;
            }
            {
                i32 a = mc_hash_bound(hw, 2);
                hw = mc_hash64(hw + 1);
                i32 b = mc_hash_bound(hw, 2);
                ay += a - b;
            }
            {
                i32 a = mc_hash_bound(hw, 6);
                hw = mc_hash64(hw + 1);
                i32 b = mc_hash_bound(hw, 6);
                az += a - b;
            }

            if (!w_inb(ax, ay, az)) {
                msp_record(s, pa_pack_record(attempt, ax, ay, az, 0, 0, MSP_RES_FAIL_BOUNDS, 0));
                continue;
            }

            fx = (float)ax + 0.5f;
            fy = (float)ay;
            fz = (float)az + 0.5f;

            if (msp_dist_sq(fx, fy, fz, MSP_PLAYER_X, MSP_PLAYER_Y, MSP_PLAYER_Z) < 576.0f) {
                int biome = w_getBiome(w, ax, az);
                int below = (ay >= 1) ? pa_pb_blk_id(w_get(w, ax, ay - 1, az)) : 0;
                msp_record(s, pa_pack_record(attempt, ax, ay, az, biome, below,
                                               MSP_RES_FAIL_PLAYER, 0));
                continue;
            }

            if (msp_dist_sq(fx, fy, fz,
                           (float)MSP_WORLD_SPAWN_X, (float)MSP_WORLD_SPAWN_Y,
                           (float)MSP_WORLD_SPAWN_Z) < 576.0f) {
                int biome = w_getBiome(w, ax, az);
                int below = (ay >= 1) ? pa_pb_blk_id(w_get(w, ax, ay - 1, az)) : 0;
                msp_record(s, pa_pack_record(attempt, ax, ay, az, biome, below,
                                               MSP_RES_FAIL_CHUNK_DIST, 0));
                continue;
            }

            if (msp_chunk_dist_sq(cx, cz, fx, fz) < 576.0f) {
                int biome = w_getBiome(w, ax, az);
                int below = (ay >= 1) ? pa_pb_blk_id(w_get(w, ax, ay - 1, az)) : 0;
                msp_record(s, pa_pack_record(attempt, ax, ay, az, biome, below,
                                               MSP_RES_FAIL_CHUNK_DIST, 0));
                continue;
            }

            {
                int biome = w_getBiome(w, ax, az);
                int below = (ay >= 1) ? pa_pb_blk_id(w_get(w, ax, ay - 1, az)) : 0;
                u8 mob = msp_pick_passive(s->seed, s->tick, ax, ay, az);

                if (!msp_can_animal_spawn(w, ax, ay, az)) {
                    if (w_light(w, ax, ay, az) <= 8) {
                        msp_record(s, pa_pack_record(attempt, ax, ay, az, biome, below,
                                                       MSP_RES_FAIL_LIGHT, mob));
                    } else {
                        msp_record(s, pa_pack_record(attempt, ax, ay, az, biome, below,
                                                       MSP_RES_FAIL_BLOCK, mob));
                    }
                    continue;
                }

                below = pa_pb_blk_id(w_get(w, ax, ay - 1, az));
                msp_record(s, pa_pack_record(attempt, ax, ay, az, biome, below,
                                               MSP_RES_SPAWN, mob));
            }
        }
    }
}

MC_HD static inline void msp_passive_spawn_cycle(MspScene *s, const World *w) {
    MspChunkEntry order[MSP_MAX_CHUNKS];
    int n_chunks = 0;
    int eligible = 0;
    int cap;
    int existing = 0;
    int i;

    msp_fill_chunks(order, &n_chunks);
    eligible = n_chunks;
    cap = MSP_ANIMAL_CAP * eligible / MSP_MOB_COUNT_DIV;
    if (existing > cap) return;

    msp_sort_chunks(order, n_chunks, s->seed, s->tick);

    for (i = 0; i < n_chunks; ++i) {
        if (msp_chunk_is_edge(order[i].cx, order[i].cz)) continue;
        msp_spawn_chunk(s, w, order[i].cx, order[i].cz);
    }
}

MC_HD static inline void msp_run(MspScene *s, World *w, CpScratch *sc, ChunkPrimer *primer,
                                 JavaRandom *r, FoliageCoord *fol, i64 seed, i64 tick) {
    s->seed = (u64)seed;
    s->tick = tick;
    s->n_records = 0;
    pop_run(w, sc, primer, r, fol, seed);
    msp_passive_spawn_cycle(s, w);
}

#endif /* MC_MOB_SPAWNING_PASSIVE_H */
