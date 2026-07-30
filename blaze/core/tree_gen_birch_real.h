/* tree_gen_birch_real: WorldGenBirchTree (useExtraRandomHeight=false) on REAL grass/dirt from
 * sbr_run (chunk 0,0).
 *
 * Pipeline:
 *   1. sbr_run -> cpbw + replaceBiomeBlocks primer (surface_blocks_real.h)
 *   2. Fixed column (TGBR_PLANT_X,TGBR_PLANT_Z)=(8,8): plant at surfaceY+1 (scan down from y=255)
 *   3. WorldGenBirchTree.generate verbatim (same control flow/RNG as core/populate.h wg_birch)
 *
 * Block-state ids extend chunk_provider CB_* with decoration codes (populate PB_*): LOG_BIRCH=32,
 * LEAVES_BIRCH=35. Predicates mirror populate.h for every block id the sbr primer can hold.
 * worldIn.getHeight()=256. generate() reads mid-loop writes -> single-threaded.
 *
 * READ-ONLY compose: surface_blocks_real.h (sbr_run primer only).
 * Output: full ChunkPrimer char[65536] in index order, %04x one per line. */
#ifndef MC_TREE_GEN_BIRCH_REAL_H
#define MC_TREE_GEN_BIRCH_REAL_H

#include "surface_blocks_real.h"

#define TGBR_PLANT_X      8
#define TGBR_PLANT_Z      8
#define TGBR_CHUNK_Y      256
#define TGBR_LOG_BIRCH    32
#define TGBR_LEAVES_BIRCH 35

MC_HD MC_NOINLINE static int tgbr_inb(int x, int y, int z) {
    return x >= 0 && x < 16 && y >= 0 && y < 256 && z >= 0 && z < 16;
}
MC_HD MC_NOINLINE static int tgbr_get(const ChunkPrimer *p, int x, int y, int z) {
    return tgbr_inb(x, y, z) ? cb_get(p, x, y, z) : CB_AIR;
}
MC_HD MC_NOINLINE static void tgbr_set(ChunkPrimer *p, int x, int y, int z, int v) {
    if (tgbr_inb(x, y, z)) cb_set(p, x, y, z, v);
}

MC_HD static inline int tgbr_is_air(int c) { return c == CB_AIR; }
MC_HD static inline int tgbr_is_leaves(int c) { return c == TGBR_LEAVES_BIRCH; }
MC_HD static inline int tgbr_is_log(int c) { return c == TGBR_LOG_BIRCH; }
MC_HD MC_NOINLINE static int tgbr_is_dirt(int c) {
    return c == CB_DIRT || c == CB_PODZOL || c == CB_COARSE_DIRT;
}
MC_HD MC_NOINLINE static int tgbr_can_grow_into(int c) {
    return tgbr_is_air(c) || tgbr_is_leaves(c) || c == CB_GRASS || tgbr_is_dirt(c) || tgbr_is_log(c);
}
MC_HD MC_NOINLINE static int tgbr_is_replaceable(const ChunkPrimer *p, int x, int y, int z) {
    int c = tgbr_get(p, x, y, z);
    return tgbr_is_air(c) || tgbr_is_leaves(c) || tgbr_is_log(c) || tgbr_can_grow_into(c);
}
MC_HD MC_NOINLINE static int tgbr_can_sustain_plant(int soil) {
    return soil == CB_GRASS || tgbr_is_dirt(soil);
}
MC_HD MC_NOINLINE static void tgbr_on_plant_grow(ChunkPrimer *p, int x, int y, int z) {
    if (tgbr_get(p, x, y, z) == CB_GRASS) tgbr_set(p, x, y, z, CB_DIRT);
}

MC_HD MC_NOINLINE static int tgbr_plant_y(const ChunkPrimer *p, int x, int z) {
    for (int y = 255; y >= 0; --y) {
        if (tgbr_get(p, x, y, z) != CB_AIR) return y + 1;
    }
    return -1;
}

/* Verbatim WorldGenBirchTree.generate (BIRCH_TREE), ChunkPrimer world. Returns 1/0. */
MC_HD MC_NOINLINE static int tgbr_birch(ChunkPrimer *p, JavaRandom *r, int posX, int posY, int posZ) {
    int i = jrand_int_bound(r, 3) + 5;
    int flag = 1;
    if (posY >= 1 && posY + i + 1 <= TGBR_CHUNK_Y) {
        for (int j = posY; j <= posY + 1 + i; ++j) {
            int k = 1;
            if (j == posY) k = 0;
            if (j >= posY + 1 + i - 2) k = 2;
            for (int l = posX - k; l <= posX + k && flag; ++l)
                for (int i1 = posZ - k; i1 <= posZ + k && flag; ++i1) {
                    if (j >= 0 && j < TGBR_CHUNK_Y) {
                        if (!tgbr_is_replaceable(p, l, j, i1)) flag = 0;
                    } else {
                        flag = 0;
                    }
                }
        }
        if (!flag) return 0;
        int state = tgbr_get(p, posX, posY - 1, posZ);
        if (tgbr_can_sustain_plant(state) && posY < TGBR_CHUNK_Y - i - 1) {
            tgbr_on_plant_grow(p, posX, posY - 1, posZ);
            for (int i2 = posY - 3 + i; i2 <= posY + i; ++i2) {
                int k2 = i2 - (posY + i);
                int l2 = 1 - k2 / 2;
                for (int i3 = posX - l2; i3 <= posX + l2; ++i3) {
                    int j1 = i3 - posX;
                    for (int k1 = posZ - l2; k1 <= posZ + l2; ++k1) {
                        int l1 = k1 - posZ;
                        int aj1 = j1 < 0 ? -j1 : j1, al1 = l1 < 0 ? -l1 : l1;
                        int place;
                        if (aj1 != l2 || al1 != l2) place = 1;
                        else { int c = (jrand_int_bound(r, 2) != 0); place = c && (k2 != 0); }
                        if (place) {
                            int s2 = tgbr_get(p, i3, i2, k1);
                            if (tgbr_is_air(s2)) tgbr_set(p, i3, i2, k1, TGBR_LEAVES_BIRCH);
                        }
                    }
                }
            }
            for (int j2 = 0; j2 < i; ++j2) {
                int s2 = tgbr_get(p, posX, posY + j2, posZ);
                if (tgbr_is_air(s2) || tgbr_is_leaves(s2))
                    tgbr_set(p, posX, posY + j2, posZ, TGBR_LOG_BIRCH);
            }
            return 1;
        }
        return 0;
    }
    return 0;
}

MC_HD MC_NOINLINE static void tgbr_run(ChunkPrimer *primer, CpScratch *sc, i64 seed) {
    sbr_run(primer, sc, seed, 0, 0);
    int plantY = tgbr_plant_y(primer, TGBR_PLANT_X, TGBR_PLANT_Z);
    if (plantY > 0) {
        JavaRandom r;
        jrand_set(&r, seed);
        tgbr_birch(primer, &r, TGBR_PLANT_X, plantY, TGBR_PLANT_Z);
    }
}

#endif /* MC_TREE_GEN_BIRCH_REAL_H */
