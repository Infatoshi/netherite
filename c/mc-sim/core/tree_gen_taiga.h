/* tree_gen_taiga: WorldGenTaiga1/2 (pine/spruce) on REAL grass/dirt from sbr_run (chunk 0,0).
 *
 * Pipeline:
 *   1. sbr_run -> cpbw + replaceBiomeBlocks primer (surface_blocks_real.h)
 *   2. Fixed column (TGT_PLANT_X,TGT_PLANT_Z)=(8,8): plant at surfaceY+1 (scan down from y=255)
 *   3. Taiga dispatch (BiomeDecorator TAIGA NORMAL): nextInt(3)==0 ? WorldGenTaiga1 : WorldGenTaiga2
 *
 * Block-state ids: LOG_SPRUCE=33, LEAVES_SPRUCE=36 (populate PB_*). Predicates mirror populate.h
 * for every block id the sbr primer can hold. worldIn.getHeight()=256. generate() reads mid-loop
 * writes -> single-threaded.
 *
 * READ-ONLY compose: surface_blocks_real.h, populate.h (algorithm reference only).
 * Output: full ChunkPrimer char[65536] in index order, %04x one per line. */
#ifndef MC_TREE_GEN_TAIGA_H
#define MC_TREE_GEN_TAIGA_H

#include "surface_blocks_real.h"

#define TGT_PLANT_X       8
#define TGT_PLANT_Z       8
#define TGT_CHUNK_Y       256
#define TGT_LOG_SPRUCE    33
#define TGT_LEAVES_SPRUCE 36

MC_HD MC_NOINLINE static int tgt_inb(int x, int y, int z) {
    return x >= 0 && x < 16 && y >= 0 && y < 256 && z >= 0 && z < 16;
}
MC_HD MC_NOINLINE static int tgt_get(const ChunkPrimer *p, int x, int y, int z) {
    return tgt_inb(x, y, z) ? cb_get(p, x, y, z) : CB_AIR;
}
MC_HD MC_NOINLINE static void tgt_set(ChunkPrimer *p, int x, int y, int z, int v) {
    if (tgt_inb(x, y, z)) cb_set(p, x, y, z, v);
}

MC_HD static inline int tgt_is_air(int c) { return c == CB_AIR; }
MC_HD static inline int tgt_is_leaves(int c) { return c == TGT_LEAVES_SPRUCE; }
MC_HD static inline int tgt_is_log(int c) { return c == TGT_LOG_SPRUCE; }
MC_HD MC_NOINLINE static int tgt_is_dirt(int c) {
    return c == CB_DIRT || c == CB_PODZOL || c == CB_COARSE_DIRT;
}
MC_HD MC_NOINLINE static int tgt_can_grow_into(int c) {
    return tgt_is_air(c) || tgt_is_leaves(c) || c == CB_GRASS || tgt_is_dirt(c) || tgt_is_log(c);
}
MC_HD MC_NOINLINE static int tgt_is_replaceable_tree(const ChunkPrimer *p, int x, int y, int z) {
    int c = tgt_get(p, x, y, z);
    return tgt_is_air(c) || tgt_is_leaves(c) || tgt_is_log(c) || tgt_can_grow_into(c);
}
MC_HD MC_NOINLINE static int tgt_can_be_replaced_by_leaves(int c) {
    return tgt_is_air(c) || tgt_is_leaves(c);
}
MC_HD MC_NOINLINE static int tgt_can_sustain_plant(int soil) {
    return soil == CB_GRASS || tgt_is_dirt(soil);
}
MC_HD MC_NOINLINE static void tgt_on_plant_grow(ChunkPrimer *p, int x, int y, int z) {
    if (tgt_get(p, x, y, z) == CB_GRASS) tgt_set(p, x, y, z, CB_DIRT);
}

MC_HD MC_NOINLINE static int tgt_plant_y(const ChunkPrimer *p, int x, int z) {
    for (int y = 255; y >= 0; --y) {
        if (tgt_get(p, x, y, z) != CB_AIR) return y + 1;
    }
    return -1;
}

/* WorldGenTaiga1 (pine). uses canBeReplacedByLeaves for canopy. */
MC_HD MC_NOINLINE static int tgt_taiga1(ChunkPrimer *p, JavaRandom *r, int posX, int posY, int posZ) {
    int i = jrand_int_bound(r, 5) + 7;
    int j = i - jrand_int_bound(r, 2) - 3;
    int k = i - j;
    int l = 1 + jrand_int_bound(r, k + 1);
    if (posY >= 1 && posY + i + 1 <= TGT_CHUNK_Y) {
        int flag = 1;
        for (int i1 = posY; i1 <= posY + 1 + i && flag; ++i1) {
            int j1 = (i1 - posY < j) ? 0 : l;
            for (int k1 = posX - j1; k1 <= posX + j1 && flag; ++k1)
                for (int l1 = posZ - j1; l1 <= posZ + j1 && flag; ++l1) {
                    if (i1 >= 0 && i1 < TGT_CHUNK_Y) {
                        if (!tgt_is_replaceable_tree(p, k1, i1, l1)) flag = 0;
                    } else flag = 0;
                }
        }
        if (!flag) return 0;
        int state = tgt_get(p, posX, posY - 1, posZ);
        if (tgt_can_sustain_plant(state) && posY < TGT_CHUNK_Y - i - 1) {
            tgt_on_plant_grow(p, posX, posY - 1, posZ);
            int k2 = 0;
            for (int l2 = posY + i; l2 >= posY + j; --l2) {
                for (int j3 = posX - k2; j3 <= posX + k2; ++j3) {
                    int k3 = j3 - posX;
                    for (int i2 = posZ - k2; i2 <= posZ + k2; ++i2) {
                        int j2 = i2 - posZ;
                        int ak3 = k3 < 0 ? -k3 : k3, aj2 = j2 < 0 ? -j2 : j2;
                        if (ak3 != k2 || aj2 != k2 || k2 <= 0) {
                            if (tgt_can_be_replaced_by_leaves(tgt_get(p, j3, l2, i2)))
                                tgt_set(p, j3, l2, i2, TGT_LEAVES_SPRUCE);
                        }
                    }
                }
                if (k2 >= 1 && l2 == posY + j + 1) --k2;
                else if (k2 < l) ++k2;
            }
            for (int i3 = 0; i3 < i - 1; ++i3) {
                int c = tgt_get(p, posX, posY + i3, posZ);
                if (tgt_is_air(c) || tgt_is_leaves(c))
                    tgt_set(p, posX, posY + i3, posZ, TGT_LOG_SPRUCE);
            }
            return 1;
        }
        return 0;
    }
    return 0;
}

/* WorldGenTaiga2 (spruce). */
MC_HD MC_NOINLINE static int tgt_taiga2(ChunkPrimer *p, JavaRandom *r, int posX, int posY, int posZ) {
    int i = jrand_int_bound(r, 4) + 6;
    int j = 1 + jrand_int_bound(r, 2);
    int k = i - j;
    int l = 2 + jrand_int_bound(r, 2);
    int flag = 1;
    if (posY >= 1 && posY + i + 1 <= TGT_CHUNK_Y) {
        for (int i1 = posY; i1 <= posY + 1 + i && flag; ++i1) {
            int j1 = (i1 - posY < j) ? 0 : l;
            for (int k1 = posX - j1; k1 <= posX + j1 && flag; ++k1)
                for (int l1 = posZ - j1; l1 <= posZ + j1 && flag; ++l1) {
                    if (i1 >= 0 && i1 < TGT_CHUNK_Y) {
                        int c = tgt_get(p, k1, i1, l1);
                        if (!tgt_is_air(c) && !tgt_is_leaves(c)) flag = 0;
                    } else flag = 0;
                }
        }
        if (!flag) return 0;
        int state = tgt_get(p, posX, posY - 1, posZ);
        if (tgt_can_sustain_plant(state) && posY < TGT_CHUNK_Y - i - 1) {
            tgt_on_plant_grow(p, posX, posY - 1, posZ);
            int i3 = jrand_int_bound(r, 2);
            int j3 = 1, k3 = 0;
            for (int l3 = 0; l3 <= k; ++l3) {
                int j4 = posY + i - l3;
                for (int i2 = posX - i3; i2 <= posX + i3; ++i2) {
                    int j2 = i2 - posX;
                    for (int k2 = posZ - i3; k2 <= posZ + i3; ++k2) {
                        int l2 = k2 - posZ;
                        int aj2 = j2 < 0 ? -j2 : j2, al2 = l2 < 0 ? -l2 : l2;
                        if (aj2 != i3 || al2 != i3 || i3 <= 0) {
                            if (tgt_can_be_replaced_by_leaves(tgt_get(p, i2, j4, k2)))
                                tgt_set(p, i2, j4, k2, TGT_LEAVES_SPRUCE);
                        }
                    }
                }
                if (i3 >= j3) { i3 = k3; k3 = 1; ++j3; if (j3 > l) j3 = l; }
                else ++i3;
            }
            int i4 = jrand_int_bound(r, 3);
            for (int k4 = 0; k4 < i - i4; ++k4) {
                int c = tgt_get(p, posX, posY + k4, posZ);
                if (tgt_is_air(c) || tgt_is_leaves(c))
                    tgt_set(p, posX, posY + k4, posZ, TGT_LOG_SPRUCE);
            }
            return 1;
        }
        return 0;
    }
    return 0;
}

MC_HD MC_NOINLINE static void tgt_run(ChunkPrimer *primer, CpScratch *sc, i64 seed) {
    sbr_run(primer, sc, seed, 0, 0);
    int plantY = tgt_plant_y(primer, TGT_PLANT_X, TGT_PLANT_Z);
    if (plantY > 0) {
        JavaRandom r;
        jrand_set(&r, seed);
        if (jrand_int_bound(&r, 3) == 0)
            tgt_taiga1(primer, &r, TGT_PLANT_X, plantY, TGT_PLANT_Z);
        else
            tgt_taiga2(primer, &r, TGT_PLANT_X, plantY, TGT_PLANT_Z);
    }
}

#endif /* MC_TREE_GEN_TAIGA_H */
