/* tree_gen_oak_real: WorldGenTrees (standard oak) on REAL grass/dirt from sbr_run (chunk 0,0).
 *
 * Pipeline:
 *   1. sbr_run -> cpbw + replaceBiomeBlocks primer (surface_blocks_real.h)
 *   2. Fixed column (TGOR_PLANT_X,TGOR_PLANT_Z)=(8,8): plant at surfaceY+1 (scan down from y=255)
 *   3. WorldGenTrees.generate verbatim (same control flow/RNG as core/tree_gen.h / populate wg_trees)
 *
 * Block-state ids extend chunk_provider CB_* with decoration codes (populate PB_*): LOG_OAK=31,
 * LEAVES_OAK=34. Predicates mirror populate.h for every block id the sbr primer can hold.
 * worldIn.getHeight()=256. generate() reads mid-loop writes -> single-threaded.
 *
 * READ-ONLY compose: surface_blocks_real.h, tree_gen.h (algorithm reference only).
 * Output: full ChunkPrimer char[65536] in index order, %04x one per line. */
#ifndef MC_TREE_GEN_OAK_REAL_H
#define MC_TREE_GEN_OAK_REAL_H

#include "surface_blocks_real.h"

#define TGOR_PLANT_X    8
#define TGOR_PLANT_Z    8
#define TGOR_CHUNK_Y    256
#define TGOR_LOG_OAK    31
#define TGOR_LEAVES_OAK 34

MC_HD MC_NOINLINE static int tgor_inb(int x, int y, int z) {
    return x >= 0 && x < 16 && y >= 0 && y < 256 && z >= 0 && z < 16;
}
MC_HD MC_NOINLINE static int tgor_get(const ChunkPrimer *p, int x, int y, int z) {
    return tgor_inb(x, y, z) ? cb_get(p, x, y, z) : CB_AIR;
}
MC_HD MC_NOINLINE static void tgor_set(ChunkPrimer *p, int x, int y, int z, int v) {
    if (tgor_inb(x, y, z)) cb_set(p, x, y, z, v);
}

MC_HD static inline int tgor_is_air(int c) { return c == CB_AIR; }
MC_HD static inline int tgor_is_leaves(int c) { return c == TGOR_LEAVES_OAK; }
MC_HD static inline int tgor_is_log(int c) { return c == TGOR_LOG_OAK; }
MC_HD MC_NOINLINE static int tgor_is_dirt(int c) {
    return c == CB_DIRT || c == CB_PODZOL || c == CB_COARSE_DIRT;
}
MC_HD MC_NOINLINE static int tgor_can_grow_into(int c) {
    return tgor_is_air(c) || tgor_is_leaves(c) || c == CB_GRASS || tgor_is_dirt(c) || tgor_is_log(c);
}
MC_HD MC_NOINLINE static int tgor_is_replaceable(const ChunkPrimer *p, int x, int y, int z) {
    int c = tgor_get(p, x, y, z);
    return tgor_is_air(c) || tgor_is_leaves(c) || tgor_is_log(c) || tgor_can_grow_into(c);
}
MC_HD MC_NOINLINE static int tgor_can_sustain_plant(int soil) {
    return soil == CB_GRASS || tgor_is_dirt(soil);
}
MC_HD MC_NOINLINE static void tgor_on_plant_grow(ChunkPrimer *p, int x, int y, int z) {
    if (tgor_get(p, x, y, z) == CB_GRASS) tgor_set(p, x, y, z, CB_DIRT);
}

/* surface scan: first non-air y in column; returns plant Y (surface+1), or -1 if empty column. */
MC_HD MC_NOINLINE static int tgor_plant_y(const ChunkPrimer *p, int x, int z) {
    for (int y = 255; y >= 0; --y) {
        if (tgor_get(p, x, y, z) != CB_AIR) return y + 1;
    }
    return -1;
}

/* Verbatim WorldGenTrees.generate (standard oak), ChunkPrimer world. Returns 1/0. */
MC_HD MC_NOINLINE static int tgor_trees(ChunkPrimer *p, JavaRandom *r, int posX, int posY, int posZ) {
    int i = jrand_int_bound(r, 3) + 4;
    int flag = 1;
    if (posY >= 1 && posY + i + 1 <= TGOR_CHUNK_Y) {
        for (int j = posY; j <= posY + 1 + i; ++j) {
            int k = 1;
            if (j == posY) k = 0;
            if (j >= posY + 1 + i - 2) k = 2;
            for (int l = posX - k; l <= posX + k && flag; ++l)
                for (int i1 = posZ - k; i1 <= posZ + k && flag; ++i1) {
                    if (j >= 0 && j < TGOR_CHUNK_Y) {
                        if (!tgor_is_replaceable(p, l, j, i1)) flag = 0;
                    } else {
                        flag = 0;
                    }
                }
        }
        if (!flag) return 0;
        int state = tgor_get(p, posX, posY - 1, posZ);
        if (tgor_can_sustain_plant(state) && posY < TGOR_CHUNK_Y - i - 1) {
            tgor_on_plant_grow(p, posX, posY - 1, posZ);
            for (int i3 = posY - 3 + i; i3 <= posY + i; ++i3) {
                int i4 = i3 - (posY + i);
                int j1 = 1 - i4 / 2;
                for (int k1 = posX - j1; k1 <= posX + j1; ++k1) {
                    int l1 = k1 - posX;
                    for (int i2 = posZ - j1; i2 <= posZ + j1; ++i2) {
                        int j2 = i2 - posZ;
                        int al1 = l1 < 0 ? -l1 : l1;
                        int aj2 = j2 < 0 ? -j2 : j2;
                        int place;
                        if (al1 != j1 || aj2 != j1) {
                            place = 1;
                        } else {
                            int c = (jrand_int_bound(r, 2) != 0);
                            place = c && (i4 != 0);
                        }
                        if (place) {
                            int cs = tgor_get(p, k1, i3, i2);
                            if (tgor_is_air(cs) || tgor_is_leaves(cs))
                                tgor_set(p, k1, i3, i2, TGOR_LEAVES_OAK);
                        }
                    }
                }
            }
            for (int j3 = 0; j3 < i; ++j3) {
                int cs = tgor_get(p, posX, posY + j3, posZ);
                if (tgor_is_air(cs) || tgor_is_leaves(cs))
                    tgor_set(p, posX, posY + j3, posZ, TGOR_LOG_OAK);
            }
            return 1;
        }
        return 0;
    }
    return 0;
}

MC_HD MC_NOINLINE static void tgor_run(ChunkPrimer *primer, CpScratch *sc, i64 seed) {
    sbr_run(primer, sc, seed, 0, 0);
    int plantY = tgor_plant_y(primer, TGOR_PLANT_X, TGOR_PLANT_Z);
    if (plantY > 0) {
        JavaRandom r;
        jrand_set(&r, seed);
        tgor_trees(primer, &r, TGOR_PLANT_X, plantY, TGOR_PLANT_Z);
    }
}

#endif /* MC_TREE_GEN_OAK_REAL_H */
