/* populate_animals: performWorldGenSpawning passive subset (cow/pig/chicken) on the verified
 * pop_run 2x2-chunk world. Previously CUT from populate.h (output-invariant for block dump).
 *
 * INTERNAL verify (CPU==CUDA). Runtime RNG = mc_hash_rng keyed (seed,tick,x,y,z,purpose) per
 * SPEC rule 1 (replaces java.util.Random in the vanilla hook). READ-ONLY deps: populate.h,
 * block_props_table.h.
 *
 * Scene: pop_run(seed) -> passive spawn in chunk-local box x,z in [8,24) (ChunkProviderOverworld
 * populate(0,0) calls performWorldGenSpawning at i+8,j+8,16,16). Biome from w_getBiome(16,16).
 * Spawn records dumped as packed u64 hex (attempt,x,y,z,biome,block_below,result,mob_type).
 *
 * CUT: entity alloc/collision/onInitialSpawn, Forge events, sheep/wolf/rabbit, world border,
 * WeightedRandom via decoration RNG (hash pick instead). Block checks via block_props_table. */
#ifndef MC_POPULATE_ANIMALS_H
#define MC_POPULATE_ANIMALS_H

#include "populate.h"
#include "block_props_table.h"
#include "mc_blocks.h"
#include "mc_rng.h"

#define PA_SPAWN_OX 8
#define PA_SPAWN_OZ 8
#define PA_SPAWN_W  16
#define PA_SPAWN_H  16
#define PA_SPAWN_CHANCE 0.1f

#define PA_MAX_WAVES    48
#define PA_MAX_RECORDS  1024

#define PA_PASSIVE_TOTAL_WEIGHT 28

enum {
    PA_PURPOSE_WAVE  = 1,
    PA_PURPOSE_PICK  = 2,
    PA_PURPOSE_GROUP = 3,
    PA_PURPOSE_POS   = 4,
    PA_PURPOSE_WALK  = 5,
    PA_PURPOSE_YAW   = 6,
};

enum {
    PA_ENT_COW     = 1,
    PA_ENT_PIG     = 2,
    PA_ENT_CHICKEN = 3,
};

enum {
    PA_RES_SPAWN       = 0,
    PA_RES_FAIL_BLOCK  = 1,
    PA_RES_FAIL_BOUNDS = 2,
    PA_RES_FAIL_TOP    = 3,
};

typedef struct {
    u64 seed;
    i64 tick;
    int biome;
    int n_records;
    u64 records[PA_MAX_RECORDS];
} PaScene;

/* Faithful PB_* -> vanilla block id for block_props_table lookups (subset of pfs_pb_to_mc). */
MC_HD MC_NOINLINE static int pa_pb_blk_id(int pb) {
    if (pb == PB_AIR) return BLK_AIR;
    if (pb == PB_STONE || pb == PB_GRANITE || pb == PB_DIORITE || pb == PB_ANDESITE) return BLK_STONE;
    if (pb == PB_WATER || pb == PB_FLOWING_WATER) return BLK_WATER;
    if (pb == PB_GRASS) return BLK_GRASS;
    if (pb == PB_DIRT || pb == PB_PODZOL || pb == PB_COARSE_DIRT) return BLK_DIRT;
    if (pb == PB_BEDROCK) return BLK_BEDROCK;
    if (pb == PB_GRAVEL) return BLK_GRAVEL;
    if (pb == PB_SAND) return BLK_SAND;
    if (pb == PB_SANDSTONE || pb == PB_RED_SANDSTONE) return BLK_SANDSTONE;
    if (pb == PB_ICE) return BLK_ICE;
    if (pb == PB_LAVA || pb == PB_FLOWING_LAVA) return BLK_LAVA;
    if (pb == PB_COBBLESTONE || pb == PB_MOSSY_COBBLESTONE) return BLK_COBBLESTONE;
    if (pb == PB_COAL_ORE) return BLK_COAL_ORE;
    if (pb == PB_IRON_ORE) return BLK_IRON_ORE;
    if (pb == PB_GOLD_ORE) return BLK_GOLD_ORE;
    if (pb == PB_REDSTONE_ORE) return BLK_REDSTONE_ORE;
    if (pb == PB_DIAMOND_ORE) return BLK_DIAMOND_ORE;
    if (pb == PB_LAPIS_ORE) return BLK_LAPIS_ORE;
    if (pb == PB_CLAY) return BLK_CLAY;
    if (pb_isLog(pb)) return BLK_LOG;
    if (pb_isLeaves(pb)) return BLK_LEAVES;
    if (pb == PB_SNOW_LAYER) return BLK_SNOW_LAYER;
    if (pb == PB_MYCELIUM) return BLK_DIRT;
    if (pb_isPlant(pb) || pb_isVine(pb)) return BLK_AIR;
    if (pb >= PB_PUMPKIN_BASE && pb < PB_PUMPKIN_BASE + 4) return BLK_AIR;
    if (pb == PB_MOB_SPAWNER || pb == PB_CHEST || pb == PB_BONE_BLOCK) return BLK_STONE;
    return BLK_STONE;
}

MC_HD MC_NOINLINE static int pa_valid_empty_spawn_pb(int pb) {
    int id = pa_pb_blk_id(pb);
    if (id == BLK_AIR) return 1;
    {
        BptProps p = mc_bpt_props(id);
        if (p.flags & BF_LIQUID) return 0;
        if (p.flags & BF_SOLID) return 0;
    }
    return 1;
}

MC_HD MC_NOINLINE static int pa_can_spawn_on_ground(const World *w, int x, int y, int z) {
    if (!w_inb(x, y, z) || !w_inb(x, y + 1, z) || y < 1) return 0;
    {
        int below = pa_pb_blk_id(w_get(w, x, y - 1, z));
        if (below == BLK_BEDROCK) return 0;
        if (!(mc_bpt_props(below).flags & BF_SOLID)) return 0;
    }
    if (!pa_valid_empty_spawn_pb(w_get(w, x, y, z))) return 0;
    if (!pa_valid_empty_spawn_pb(w_get(w, x, y + 1, z))) return 0;
    return 1;
}

MC_HD MC_NOINLINE static u64 pa_pack_record(int attempt, int x, int y, int z, int biome,
                                       int block_below, int result, int mob_type) {
    u64 v = 0;
    v |= (u64)(attempt & 0xFFFF);
    v |= (u64)(x & 0xFF) << 16;
    v |= (u64)(y & 0xFF) << 24;
    v |= (u64)(z & 0xFF) << 32;
    v |= (u64)(biome & 0xFF) << 40;
    v |= (u64)(block_below & 0xFF) << 48;
    v |= (u64)(result & 0xF) << 56;
    v |= (u64)(mob_type & 0xF) << 60;
    return v;
}

MC_HD MC_NOINLINE static void pa_record(PaScene *s, u64 rec) {
    if (s->n_records < PA_MAX_RECORDS)
        s->records[s->n_records++] = rec;
}

MC_HD MC_NOINLINE static i32 pa_rand5(u64 seed, i64 tick, int wave, int ent, int retry, int step) {
    u64 h = mc_hash_seed(seed, tick, wave * 64 + ent, retry * 16 + step, 0, PA_PURPOSE_WALK);
    i32 a = mc_hash_bound(h, 5);
    h = mc_hash64(h + 1);
    i32 b = mc_hash_bound(h, 5);
    return a - b;
}

MC_HD MC_NOINLINE static u8 pa_pick_passive(u64 seed, i64 tick, int wave) {
    u64 h = mc_hash_seed(seed, tick, wave, 0, 0, PA_PURPOSE_PICK);
    i32 roll = mc_hash_bound(h, PA_PASSIVE_TOTAL_WEIGHT);
    if (roll < 8) return (u8)PA_ENT_COW;
    if (roll < 18) return (u8)PA_ENT_PIG;
    return (u8)PA_ENT_CHICKEN;
}

MC_HD MC_NOINLINE static int pa_in_spawn_box(int j, int k, int ox, int oz, int w, int h) {
    return j >= ox && j < ox + w && k >= oz && k < oz + h;
}

/* WorldEntitySpawner.performWorldGenSpawning passive subset (cow/pig/chicken). */
MC_HD MC_NOINLINE static void pa_worldgen_spawning(PaScene *s, const World *w, int ox, int oz,
                                              int sw, int sh) {
    int wave;
    s->biome = w_getBiome(w, 16, 16);

    for (wave = 0; wave < PA_MAX_WAVES; ++wave) {
        u64 hw = mc_hash_seed(s->seed, s->tick, wave, 0, 0, PA_PURPOSE_WAVE);
        if (mc_hash_f01(hw) >= PA_SPAWN_CHANCE) break;

        {
            u8 mob = pa_pick_passive(s->seed, s->tick, wave);
            u64 hg = mc_hash_seed(s->seed, s->tick, wave, 0, 0, PA_PURPOSE_GROUP);
            int group = 4; /* cow/pig/chicken minGroup=maxGroup=4 */
            (void)hg;

            u64 hp = mc_hash_seed(s->seed, s->tick, wave, 0, 0, PA_PURPOSE_POS);
            int j = ox + mc_hash_bound(hp, sw);
            hp = mc_hash64(hp + 1);
            int k = oz + mc_hash_bound(hp, sh);
            int l = j;
            int i1 = k;
            int j1;

            for (j1 = 0; j1 < group; ++j1) {
                int flag = 0;
                int k1;
                for (k1 = 0; !flag && k1 < 4; ++k1) {
                    int attempt = s->n_records;
                    int top_y = w_topSolidOrLiquid(w, j, k);

                    if (top_y < 1 || top_y >= W_Y) {
                        pa_record(s, pa_pack_record(attempt, j, top_y, k, s->biome, 0,
                                                    PA_RES_FAIL_TOP, mob));
                    } else if (pa_can_spawn_on_ground(w, j, top_y, k)) {
                        int below = pa_pb_blk_id(w_get(w, j, top_y - 1, k));
                        pa_record(s, pa_pack_record(attempt, j, top_y, k, s->biome, below,
                                                    PA_RES_SPAWN, mob));
                        flag = 1;
                    } else {
                        int below = (top_y >= 1) ? pa_pb_blk_id(w_get(w, j, top_y - 1, k)) : 0;
                        pa_record(s, pa_pack_record(attempt, j, top_y, k, s->biome, below,
                                                    PA_RES_FAIL_BLOCK, mob));
                    }

                    j += pa_rand5(s->seed, s->tick, wave, j1, k1, 0);
                    k += pa_rand5(s->seed, s->tick, wave, j1, k1, 1);

                    while (!pa_in_spawn_box(j, k, ox, oz, sw, sh)) {
                        k = i1 + pa_rand5(s->seed, s->tick, wave, j1, k1, 2);
                        j = l + pa_rand5(s->seed, s->tick, wave, j1, k1, 3);
                    }
                }
            }
        }
    }
}

MC_HD MC_NOINLINE static void pa_run(PaScene *s, World *w, CpScratch *sc, ChunkPrimer *primer,
                                JavaRandom *r, FoliageCoord *fol, i64 seed, i64 tick) {
    s->seed = (u64)seed;
    s->tick = tick;
    s->n_records = 0;
    pop_run(w, sc, primer, r, fol, seed);
    pa_worldgen_spawning(s, w, PA_SPAWN_OX, PA_SPAWN_OZ, PA_SPAWN_W, PA_SPAWN_H);
}

#endif /* MC_POPULATE_ANIMALS_H */
