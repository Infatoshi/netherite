/* mob_spawning_oracle: REAL-ORACLE hostile spawn DECISION math (WorldEntitySpawner).
 *
 * Vanilla-faithful battery (Java Golden == CPU == CUDA). Not full worldgen: synthetic packed
 * block/light columns + fixed tape of (seed, player chunk). Covers:
 *   1. eligible chunk set from player-centered range (interior 15x15 of 17x17, border ring cut)
 *   2. getRandomChunkPosition (nextInt 16/16 + roundUp(height+1,16) + nextInt)
 *   3. pack size = ceil(nextDouble()*4)  [seeded JavaRandom stand-in for Math.random]
 *   4. pack walk: nextInt(6)-nextInt(6) on x/z; y walk nextInt(1)-nextInt(1) (==0)
 *   5. player-range 24 + world-spawn 24 (distSq >= 576)
 *   6. canCreatureTypeSpawnAtLocation ON_GROUND (solid below, not bedrock, empty self+above)
 *   7. WeightedRandom over default overworld monster list (Biome.spawnableMonsterList)
 *   8. EntityMob.isValidLightLevel (sky > nextInt(32) fail; combined <= nextInt(8) ok)
 *
 * RNG: JavaRandom (core/mc_rng.h) matching java.util.Random nextInt/nextDouble order.
 * CUT: Collections.shuffle (sorted cx,cz), PlayerChunkMap/world border, Forge events, entity
 * alloc/collision, thunder skylight rewrite, Math.random unseeded (use nextDouble on same stream).
 *
 * Output: u64 hex lines packing (chunkX, chunkZ, spawnY, entityTypeId, success).
 * Traps: ordered temporaries; no a[i]=i++; -ffp-contract=off/--fmad=false. */
#ifndef MC_MOB_SPAWNING_ORACLE_H
#define MC_MOB_SPAWNING_ORACLE_H

#include "mc.h"
#include "mc_rng.h"
#include "mc_blocks.h"
#include "block_props_table.h"

#define MSO_MOB_COUNT_DIV  289
#define MSO_MONSTER_CAP    70
#define MSO_CHUNK_RANGE    8
#define MSO_MAX_ELIGIBLE   225
#define MSO_MAX_OUT        4096
#define MSO_N_PLAYERS      6
#define MSO_N_SEEDS        8
#define MSO_CHUNKS_PER_SEED 4
#define MSO_GROUPS         3

/* Default overworld monster weights (Biome.java constructor). */
#define MSO_N_MONSTER      8
#define MSO_TOTAL_WEIGHT   515

/* Result / success codes in high byte of packed decision. */
enum {
    MSO_RES_SPAWN            = 0,
    MSO_RES_FAIL_INIT_SOLID  = 1,
    MSO_RES_FAIL_PLAYER      = 2,
    MSO_RES_FAIL_SPAWN_PT    = 3,
    MSO_RES_FAIL_PLACE       = 4,
    MSO_RES_FAIL_LIGHT       = 5,
    MSO_RES_FAIL_OOB         = 6,
    MSO_RES_FAIL_PACK0       = 7,
    MSO_RES_ELIGIBLE_COUNT   = 0xE0,
    MSO_RES_ELIGIBLE_CHUNK   = 0xE1,
    MSO_RES_CAP              = 0xE2,
};

/* Monster type ids = index in Biome.spawnableMonsterList. */
enum {
    MSO_SPIDER = 0,
    MSO_ZOMBIE = 1,
    MSO_ZOMBIE_VILLAGER = 2,
    MSO_SKELETON = 3,
    MSO_CREEPER = 4,
    MSO_SLIME = 5,
    MSO_ENDERMAN = 6,
    MSO_WITCH = 7,
    MSO_TYPE_NONE = 0xFF,
};

/* Fixed world-spawn for the spawn-point distance gate (y high so ground spawns pass when far). */
#define MSO_WORLD_SPAWN_X 0
#define MSO_WORLD_SPAWN_Y 200
#define MSO_WORLD_SPAWN_Z 0

/* Floor height of synthetic column world. */
#define MSO_FLOOR_Y 60

typedef struct {
    int n;
    u64 lines[MSO_MAX_OUT];
} MsoOut;

/* ---- MathHelper subset (verbatim) ---- */

MC_HD static inline int mso_floor_d(double value) {
    int i = (int)value;
    return value < (double)i ? i - 1 : i;
}

MC_HD static inline int mso_ceil_d(double value) {
    int i = (int)value;
    return value > (double)i ? i + 1 : i;
}

MC_HD static inline int mso_round_up(int number, int interval) {
    if (interval == 0) return 0;
    if (number == 0) return interval;
    if (number < 0) interval = -interval;
    {
        int i = number % interval;
        return i == 0 ? number : number + interval - i;
    }
}

/* ---- packing ---- */

MC_HD static inline u64 mso_pack(int chunk_x, int chunk_z, int spawn_y, int type_id, int success) {
    u64 v = 0;
    v |= (u64)(u16)(i16)chunk_x;
    v |= (u64)(u16)(i16)chunk_z << 16;
    v |= (u64)(spawn_y & 0xFFFF) << 32;
    v |= (u64)(type_id & 0xFF) << 48;
    v |= (u64)(success & 0xFF) << 56;
    return v;
}

MC_HD static inline void mso_emit(MsoOut *o, u64 v) {
    if (o->n < MSO_MAX_OUT) o->lines[o->n++] = v;
}

/* ---- synthetic world (pure fn of x,y,z; identical in Golden.java) ---- */

MC_HD static inline int mso_is_normal_cube_id(int id) {
    if (id <= 0) return 0;
    BptProps p = mc_bpt_props(id);
    if (p.flags & BF_LIQUID) return 0;
    return (p.flags & BF_SOLID) != 0;
}

MC_HD static inline int mso_is_liquid_id(int id) {
    if (id <= 0) return 0;
    return (mc_bpt_props(id).flags & BF_LIQUID) != 0;
}

/* Block id at (x,y,z). Bedrock y=0, stone 1..FLOOR-1, grass FLOOR, dark stone pocket, else air. */
MC_HD static inline int mso_block_id(int x, int y, int z) {
    int lx, lz;
    if (y < 0 || y >= 256) return BLK_AIR;
    if (y == 0) return BLK_BEDROCK;
    if (y < MSO_FLOOR_Y) return BLK_STONE;
    if (y == MSO_FLOOR_Y) return BLK_GRASS;
    /* Dark pocket: stone shell y=64..66 over a 7x7 patch every 32 blocks (local 8..14). */
    lx = x % 32; if (lx < 0) lx += 32;
    lz = z % 32; if (lz < 0) lz += 32;
    if (y >= 64 && y <= 66 && lx >= 8 && lx <= 14 && lz >= 8 && lz <= 14)
        return BLK_STONE;
    /* Torch posts for block-light variety (not solid). */
    if (y == MSO_FLOOR_Y + 1) {
        if ((x == 2 && z == 2) || (x == 13 && z == 13)) return BLK_TORCH;
    }
    return BLK_AIR;
}

/* Height map: first air-or-non-opaque above solid column (chunk.getHeight style top+1). */
MC_HD static inline int mso_height_at(int x, int z) {
    int y;
    for (y = 255; y >= 0; --y) {
        int id = mso_block_id(x, y, z);
        BptProps p = mc_bpt_props(id);
        if (p.light_opacity != 0) return y + 1;
    }
    return 0;
}

/* Sky light: 15 if open to sky (no opaque above), else 0. */
MC_HD static inline int mso_sky_light(int x, int y, int z) {
    int yy;
    for (yy = y + 1; yy < 256; ++yy) {
        int id = mso_block_id(x, yy, z);
        if (mc_bpt_props(id).light_opacity != 0) return 0;
    }
    return 15;
}

/* Block light: max over torch sources with chebyshev decay (matches torch emit 14). */
MC_HD static inline int mso_block_light(int x, int y, int z) {
    static const int txs[2] = {2, 13};
    static const int tzs[2] = {2, 13};
    int best = 0, t, dx, dy, dz, r, lv;
    for (t = 0; t < 2; ++t) {
        dx = x - txs[t]; if (dx < 0) dx = -dx;
        dy = y - (MSO_FLOOR_Y + 1); if (dy < 0) dy = -dy;
        dz = z - tzs[t]; if (dz < 0) dz = -dz;
        r = dx; if (dz > r) r = dz; if (dy > r) r = dy;
        if (r >= 14) continue;
        lv = 14 - r;
        if (lv > best) best = lv;
    }
    return best;
}

/* getLightFromNeighbors stand-in: max(sky, block) with skylightSubtracted=0. */
MC_HD static inline int mso_combined_light(int x, int y, int z) {
    int sky = mso_sky_light(x, y, z);
    int bl = mso_block_light(x, y, z);
    return sky > bl ? sky : bl;
}

/* isValidEmptySpawnBlock subset (no power/rail in our block set). */
MC_HD static inline int mso_valid_empty(int id) {
    if (mso_is_normal_cube_id(id)) return 0;
    if (mso_is_liquid_id(id)) return 0;
    return 1;
}

/* canCreatureTypeSpawnAtLocation ON_GROUND (border always ok). */
MC_HD static inline int mso_can_place(int x, int y, int z) {
    int below = mso_block_id(x, y - 1, z);
    int self = mso_block_id(x, y, z);
    int above = mso_block_id(x, y + 1, z);
    if (below == BLK_BEDROCK) return 0;
    /* BARRIER omitted (not in synthetic set). canCreatureSpawn ~= normal cube. */
    if (!mso_is_normal_cube_id(below)) return 0;
    if (!mso_valid_empty(self)) return 0;
    if (!mso_valid_empty(above)) return 0;
    return 1;
}

/* WeightedRandom.getRandomItem over monster weights. Returns type id or -1. */
MC_HD static inline int mso_pick_monster(JavaRandom *r) {
    static const int w[MSO_N_MONSTER] = {100, 95, 5, 100, 100, 100, 10, 5};
    int weight = jrand_int_bound(r, MSO_TOTAL_WEIGHT);
    int i;
    for (i = 0; i < MSO_N_MONSTER; ++i) {
        weight -= w[i];
        if (weight < 0) return i;
    }
    return -1;
}

/* EntityMob.isValidLightLevel (no thunder branch). */
MC_HD static inline int mso_valid_light(JavaRandom *r, int x, int y, int z) {
    int sky = mso_sky_light(x, y, z);
    int thr = jrand_int_bound(r, 32);
    int comb, thr2;
    if (sky > thr) return 0;
    comb = mso_combined_light(x, y, z);
    thr2 = jrand_int_bound(r, 8);
    return comb <= thr2;
}

MC_HD static inline double mso_dist_sq(double ax, double ay, double az,
                                      double bx, double by, double bz) {
    double dx = ax - bx, dy = ay - by, dz = az - bz;
    return dx * dx + dy * dy + dz * dz;
}

/* getRandomChunkPosition (WorldEntitySpawner). */
MC_HD static inline void mso_random_chunk_pos(JavaRandom *r, int cx, int cz,
                                              int *ox, int *oy, int *oz) {
    int i = cx * 16 + jrand_int_bound(r, 16);
    int j = cz * 16 + jrand_int_bound(r, 16);
    int h = mso_height_at(i, j);
    int k = mso_round_up(h + 1, 16);
    int bound = k > 0 ? k : 15; /* topFilledSegment+16-1 stand-in for empty */
    int l = jrand_int_bound(r, bound);
    *ox = i; *oy = l; *oz = j;
}

/* ---- eligible chunks (player-centered, border ring excluded) ---- */

MC_HD static inline int mso_collect_eligible(int player_cx, int player_cz,
                                             int *out_cx, int *out_cz) {
    int n = 0;
    int i1, j1;
    for (i1 = -MSO_CHUNK_RANGE; i1 <= MSO_CHUNK_RANGE; ++i1) {
        for (j1 = -MSO_CHUNK_RANGE; j1 <= MSO_CHUNK_RANGE; ++j1) {
            int flag = (i1 == -MSO_CHUNK_RANGE || i1 == MSO_CHUNK_RANGE ||
                        j1 == -MSO_CHUNK_RANGE || j1 == MSO_CHUNK_RANGE);
            if (!flag) {
                out_cx[n] = i1 + player_cx;
                out_cz[n] = j1 + player_cz;
                ++n;
            }
        }
    }
    /* Sorted by (cz, cx) - CUT Collections.shuffle (unseeded in vanilla). */
    {
        int a, b;
        for (a = 0; a < n; ++a) {
            for (b = a + 1; b < n; ++b) {
                int swap = 0;
                if (out_cz[b] < out_cz[a]) swap = 1;
                else if (out_cz[b] == out_cz[a] && out_cx[b] < out_cx[a]) swap = 1;
                if (swap) {
                    int t = out_cx[a]; out_cx[a] = out_cx[b]; out_cx[b] = t;
                    t = out_cz[a]; out_cz[a] = out_cz[b]; out_cz[b] = t;
                }
            }
        }
    }
    return n;
}

/* One hostile spawn cycle over first MSO_CHUNKS_PER_SEED eligible chunks. */
MC_HD static inline void mso_spawn_cycle(MsoOut *o, JavaRandom *r,
                                         int player_cx, int player_cz,
                                         double player_x, double player_y, double player_z,
                                         int existing_monsters) {
    int ecx[MSO_MAX_ELIGIBLE], ecz[MSO_MAX_ELIGIBLE];
    int n_elig = mso_collect_eligible(player_cx, player_cz, ecx, ecz);
    int cap = MSO_MONSTER_CAP * n_elig / MSO_MOB_COUNT_DIV;
    int n_use, ci, k2, i4;

    mso_emit(o, mso_pack(player_cx, player_cz, n_elig & 0xFFFF, (cap & 0xFF), MSO_RES_CAP));

    if (existing_monsters > cap) return;

    n_use = n_elig < MSO_CHUNKS_PER_SEED ? n_elig : MSO_CHUNKS_PER_SEED;
    for (ci = 0; ci < n_use; ++ci) {
        int cx = ecx[ci], cz = ecz[ci];
        int k1, l1, i2;
        int j2;

        mso_random_chunk_pos(r, cx, cz, &k1, &l1, &i2);

        if (mso_is_normal_cube_id(mso_block_id(k1, l1, i2))) {
            mso_emit(o, mso_pack(cx, cz, l1, MSO_TYPE_NONE, MSO_RES_FAIL_INIT_SOLID));
            continue;
        }

        j2 = 0;
        for (k2 = 0; k2 < MSO_GROUPS; ++k2) {
            int l2 = k1, i3 = l1, j3 = i2;
            int type_id = -1;
            /* Pack size: ceil(nextDouble()*4) stands in for Math.random (same distribution). */
            int l3 = mso_ceil_d(jrand_double(r) * 4.0);
            if (l3 <= 0) {
                mso_emit(o, mso_pack(cx, cz, i3, MSO_TYPE_NONE, MSO_RES_FAIL_PACK0));
                continue;
            }

            for (i4 = 0; i4 < l3; ++i4) {
                int a, b;
                float f, f1;
                double dsq_p, dsq_s;
                int res, tid;

                a = jrand_int_bound(r, 6);
                b = jrand_int_bound(r, 6);
                l2 += a - b;
                a = jrand_int_bound(r, 1);
                b = jrand_int_bound(r, 1);
                i3 += a - b;
                a = jrand_int_bound(r, 6);
                b = jrand_int_bound(r, 6);
                j3 += a - b;

                f = (float)l2 + 0.5f;
                f1 = (float)j3 + 0.5f;

                if (i3 < 0 || i3 >= 256) {
                    mso_emit(o, mso_pack(cx, cz, i3 & 0xFFFF, MSO_TYPE_NONE, MSO_RES_FAIL_OOB));
                    continue;
                }

                dsq_p = mso_dist_sq((double)f, (double)i3, (double)f1,
                                    player_x, player_y, player_z);
                if (dsq_p < 576.0) {
                    mso_emit(o, mso_pack(cx, cz, i3, MSO_TYPE_NONE, MSO_RES_FAIL_PLAYER));
                    continue;
                }
                dsq_s = mso_dist_sq((double)f, (double)i3, (double)f1,
                                    (double)MSO_WORLD_SPAWN_X, (double)MSO_WORLD_SPAWN_Y,
                                    (double)MSO_WORLD_SPAWN_Z);
                if (dsq_s < 576.0) {
                    mso_emit(o, mso_pack(cx, cz, i3, MSO_TYPE_NONE, MSO_RES_FAIL_SPAWN_PT));
                    continue;
                }

                if (type_id < 0) {
                    type_id = mso_pick_monster(r);
                    if (type_id < 0) break;
                }

                if (!mso_can_place(l2, i3, j3)) {
                    mso_emit(o, mso_pack(cx, cz, i3, type_id, MSO_RES_FAIL_PLACE));
                    continue;
                }

                /* isValidLightLevel consumes 1-2 nextInt from entity.rand; use world stream. */
                if (!mso_valid_light(r, l2, i3, j3)) {
                    mso_emit(o, mso_pack(cx, cz, i3, type_id, MSO_RES_FAIL_LIGHT));
                    continue;
                }

                /* Success: would spawn (entity construction CUT). */
                tid = type_id;
                res = MSO_RES_SPAWN;
                mso_emit(o, mso_pack(cx, cz, i3, tid, res));
                ++j2;
                /* max pack size CUT (Forge getMaxSpawnPackSize); no early continue-label. */
                (void)j2;
            }
        }
    }
}

/* Fixed tape: player chunk coords (player stands at chunk center, y=FLOOR+1). */
MC_HD static inline void mso_player_tape(int idx, int *pcx, int *pcz,
                                         double *px, double *py, double *pz) {
    static const int pcs[MSO_N_PLAYERS][2] = {
        {0, 0}, {5, -3}, {-10, 12}, {1, 1}, {20, 20}, {-1, 0},
    };
    int i = idx;
    if (i < 0) i = 0;
    if (i >= MSO_N_PLAYERS) i = MSO_N_PLAYERS - 1;
    *pcx = pcs[i][0];
    *pcz = pcs[i][1];
    *px = (double)(*pcx * 16) + 8.5;
    *py = (double)(MSO_FLOOR_Y + 1);
    *pz = (double)(*pcz * 16) + 8.5;
}

MC_HD static inline i64 mso_seed_tape(int idx) {
    static const i64 seeds[MSO_N_SEEDS] = {
        0LL, 1LL, 7LL, 12345LL, 99991LL, 42LL, 0xC0FFEELL, 0xBADC0DELL,
    };
    int i = idx;
    if (i < 0) i = 0;
    if (i >= MSO_N_SEEDS) i = MSO_N_SEEDS - 1;
    return seeds[i];
}

/* Full battery: eligible dumps + spawn cycles over seeds x players. */
MC_HD static inline void mso_run(MsoOut *o) {
    int pi, si;
    int ecx[MSO_MAX_ELIGIBLE], ecz[MSO_MAX_ELIGIBLE];
    o->n = 0;

    /* Phase 1: eligible chunk sets (RNG-free). */
    for (pi = 0; pi < MSO_N_PLAYERS; ++pi) {
        int pcx, pcz, n, i;
        double px, py, pz;
        mso_player_tape(pi, &pcx, &pcz, &px, &py, &pz);
        n = mso_collect_eligible(pcx, pcz, ecx, ecz);
        mso_emit(o, mso_pack(pcx, pcz, n & 0xFFFF, (u8)pi, MSO_RES_ELIGIBLE_COUNT));
        for (i = 0; i < n; ++i)
            mso_emit(o, mso_pack(ecx[i], ecz[i], 0, (u8)pi, MSO_RES_ELIGIBLE_CHUNK));
    }

    /* Phase 2: JavaRandom spawn decision cycles. */
    for (si = 0; si < MSO_N_SEEDS; ++si) {
        for (pi = 0; pi < MSO_N_PLAYERS; ++pi) {
            int pcx, pcz;
            double px, py, pz;
            JavaRandom r;
            i64 seed = mso_seed_tape(si);
            /* Mix player index into seed so streams differ per cell without extra draws. */
            jrand_set(&r, seed ^ ((i64)pi * 0x9E3779B97F4A7C15LL));
            mso_player_tape(pi, &pcx, &pcz, &px, &py, &pz);
            mso_spawn_cycle(o, &r, pcx, pcz, px, py, pz, 0);
        }
    }
}

#endif /* MC_MOB_SPAWNING_ORACLE_H */
