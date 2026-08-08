/* populate_light_shim: integration harness replacing populate.h w_light stub with
 * light_propagation fixpoint CA over the verified 2x2-chunk populate world.
 *
 * INTERNAL verify (CPU==CUDA). READ-ONLY: populate.h, light_propagation.h, chunk_provider.h.
 *
 * Scene: chunk_provider + populate up to (not including) mushrooms, propagate skylight+block
 * light on [0,32)x[0,256)x[0,32), re-run mushroom decorate with propagated sky light vs stub,
 * dump block indices/states that differ (%06x%04x per line: w_index, block u16).
 *
 * Mushroom RNG stream is identical between stub and propagated passes (world forked after
 * pre-shroom decoration). Only canBlockStay light query changes. */
#ifndef MC_POPULATE_LIGHT_SHIM_H
#define MC_POPULATE_LIGHT_SHIM_H

#include "populate.h"
#include "wg_fossils_data.h"
/* light_propagation.h semantics inlined below (READ-ONLY; not included: World type clash with populate.h). */

#define PLS_MAX_LIGHT_ITERS 512

/* ===== light CA on populate world (same Jacobi semantics as light_propagation.h) ===== */

MC_HD static inline int pls_emit(int c) {
    if (pb_isLava(c)) return 15;
    return 0;
}

MC_HD static inline int pls_effective_opacity(int c, int emit) {
    int j = pb_opacity(c);
    if (j >= 15 && emit > 0) j = 1;
    if (j < 1) j = 1;
    return j;
}

MC_HD static inline int pls_height_map_at(const World *w, int x, int z) {
    int y;
    for (y = W_Y - 1; y >= 0; --y) {
        int c = w_get(w, x, y, z);
        if (pb_opacity(c) != 0) return y + 1;
    }
    return 0;
}

MC_HD static inline void pls_build_height_map(const World *w, u8 *hm) {
    int x, z;
    for (z = 0; z < W_Z; ++z)
        for (x = 0; x < W_X; ++x)
            hm[z * W_X + x] = (u8)pls_height_map_at(w, x, z);
}

MC_HD static inline int pls_can_see_sky(const u8 *hm, int x, int y, int z) {
    return y >= (int)hm[z * W_X + x];
}

MC_HD static inline int pls_neighbor_sky(const u8 *sky, int x, int y, int z, int dx, int dy, int dz) {
    int nx = x + dx, ny = y + dy, nz = z + dz;
    if (!w_inb(nx, ny, nz)) return 0;
    return (int)sky[w_index(nx, ny, nz)];
}

MC_HD static inline int pls_neighbor_block(const u8 *blk, int x, int y, int z, int dx, int dy, int dz) {
    int nx = x + dx, ny = y + dy, nz = z + dz;
    if (!w_inb(nx, ny, nz)) return 0;
    return (int)blk[w_index(nx, ny, nz)];
}

MC_HD static inline int pls_raw_sky(const u8 *sky, const World *w, const u8 *hm, int x, int y, int z) {
    if (pls_can_see_sky(hm, x, y, z)) return 15;
    {
        int c = w_get(w, x, y, z);
        int j = pls_effective_opacity(c, 0);
        if (j >= 15) return 0;
        {
            int i = 0, k;
            k = pls_neighbor_sky(sky, x, y, z, 1, 0, 0) - j;  if (k > i) i = k;
            k = pls_neighbor_sky(sky, x, y, z, -1, 0, 0) - j; if (k > i) i = k;
            k = pls_neighbor_sky(sky, x, y, z, 0, 1, 0) - j;  if (k > i) i = k;
            k = pls_neighbor_sky(sky, x, y, z, 0, -1, 0) - j; if (k > i) i = k;
            k = pls_neighbor_sky(sky, x, y, z, 0, 0, 1) - j;  if (k > i) i = k;
            k = pls_neighbor_sky(sky, x, y, z, 0, 0, -1) - j; if (k > i) i = k;
            return i;
        }
    }
}

MC_HD static inline int pls_raw_block(const u8 *blk, const World *w, int x, int y, int z) {
    int c = w_get(w, x, y, z);
    int emit = pls_emit(c);
    int j = pls_effective_opacity(c, emit);
    if (j >= 15) return 0;
    if (emit >= 14) return emit;
    {
        int i = emit, k;
        k = pls_neighbor_block(blk, x, y, z, 1, 0, 0) - j;  if (k > i) i = k;
        k = pls_neighbor_block(blk, x, y, z, -1, 0, 0) - j; if (k > i) i = k;
        k = pls_neighbor_block(blk, x, y, z, 0, 1, 0) - j;  if (k > i) i = k;
        k = pls_neighbor_block(blk, x, y, z, 0, -1, 0) - j; if (k > i) i = k;
        k = pls_neighbor_block(blk, x, y, z, 0, 0, 1) - j;  if (k > i) i = k;
        k = pls_neighbor_block(blk, x, y, z, 0, 0, -1) - j; if (k > i) i = k;
        return i;
    }
}

MC_HD static inline u8 pls_pack_light(int sky, int block) {
    return (u8)(((sky & 0xF) << 4) | (block & 0xF));
}

MC_HD static inline int pls_light_packed_equal(const u8 *a_sky, const u8 *a_blk,
                                               const u8 *b_sky, const u8 *b_blk) {
    int i;
    for (i = 0; i < W_N; ++i) {
        u8 pa = pls_pack_light(a_sky[i], a_blk[i]);
        u8 pb = pls_pack_light(b_sky[i], b_blk[i]);
        if (pa != pb) return 0;
    }
    return 1;
}

MC_HD static inline void pls_ca_step(const u8 *cur_sky, const u8 *cur_blk,
                                    u8 *next_sky, u8 *next_blk,
                                    const World *w, const u8 *hm) {
    int x, y, z;
    for (y = 0; y < W_Y; ++y)
        for (z = 0; z < W_Z; ++z)
            for (x = 0; x < W_X; ++x) {
                int i = w_index(x, y, z);
                next_sky[i] = (u8)pls_raw_sky(cur_sky, w, hm, x, y, z);
                next_blk[i] = (u8)pls_raw_block(cur_blk, w, x, y, z);
            }
}

MC_HD static inline void pls_propagate(const World *w, u8 *sky, u8 *blk,
                                      u8 *tmp_sky, u8 *tmp_blk, int max_iters) {
    u8 hm[W_X * W_Z];
    int i;
    pls_build_height_map(w, hm);
    for (i = 0; i < max_iters; ++i) {
        pls_ca_step(sky, blk, tmp_sky, tmp_blk, w, hm);
        if (pls_light_packed_equal(sky, blk, tmp_sky, tmp_blk)) break;
        {
            int j;
            for (j = 0; j < W_N; ++j) {
                sky[j] = tmp_sky[j];
                blk[j] = tmp_blk[j];
            }
        }
    }
}

/* Propagated sky light (replaces w_light stub for mushroom canBlockStay). */
MC_HD static inline int pls_w_light(const u8 *sky, int x, int y, int z) {
    if (!w_inb(x, y, z)) return 0;
    return (int)sky[w_index(x, y, z)];
}

MC_HD static inline int pls_mushroom_light(const World *w, int x, int y, int z,
                                           int use_pls, const u8 *sky) {
    if (w->popSkyLight) return w_pop_sky_stale(w, x, y, z);   /* vanilla stale skylight */
    return use_pls ? pls_w_light(sky, x, y, z) : w_light(w, x, y, z);
}

MC_HD static inline int pls_mushroom_canStay(const World *w, int x, int y, int z,
                                             int use_pls, const u8 *sky) {
    if (y < 0 || y >= 256) return 0;
    int below = w_get(w, x, y - 1, z);
    if (below == PB_MYCELIUM) return 1;
    if (below == PB_PODZOL) return 1;
    {
        /* leaves are VALID soil (server-side isFullBlock; see wg_mushroom_canStay note) */
        int lit = pls_mushroom_light(w, x, y, z, use_pls, sky);
        return lit < 13 && pb_blocksMovement(below);
    }
}

MC_HD static inline void pls_wg_bush(World *w, JavaRandom *r, int x, int y, int z, int block,
                                     int use_pls, const u8 *sky) {
    int i;
    for (i = 0; i < 64; ++i) {
        int bx = x + wg_off(r, 8);
        int by = y + wg_off(r, 4);
        int bz = z + wg_off(r, 8);
        int ok = w_isAir(w, bx, by, bz) && pls_mushroom_canStay(w, bx, by, bz, use_pls, sky);
        if (ok) w_set(w, bx, by, bz, block);
    }
}

/* Mushroom decorate loops (verbatim from bd_genDecorations SHROOM section). */
MC_HD static inline void pls_mushroom_decorate(World *w, JavaRandom *r, const BDCfg *c,
                                               int use_pls, const u8 *sky) {
    int l3;
    for (l3 = 0; l3 < c->mushroomsPerChunk; ++l3) {
        if (jrand_int_bound(r, 4) == 0) {
            int i8 = jrand_int_bound(r, 16) + 8;
            int l11 = jrand_int_bound(r, 16) + 8;
            pls_wg_bush(w, r, i8, w_gen_height(w, i8, l11), l11, PB_BROWN_MUSHROOM, use_pls, sky);
        }
        if (jrand_int_bound(r, 8) == 0) {
            int j8 = jrand_int_bound(r, 16) + 8;
            int i12 = jrand_int_bound(r, 16) + 8;
            int j15 = w_gen_height(w, j8, i12) * 2;
            if (j15 > 0) {
                int k18 = jrand_int_bound(r, j15);
                pls_wg_bush(w, r, j8, k18, i12, PB_RED_MUSHROOM, use_pls, sky);
            }
        }
    }
    if (jrand_int_bound(r, 4) == 0) {
        int i4 = jrand_int_bound(r, 16) + 8;
        int k8 = jrand_int_bound(r, 16) + 8;
        int j12 = w_gen_height(w, i4, k8) * 2;
        if (j12 > 0) {
            int k15 = jrand_int_bound(r, j12);
            pls_wg_bush(w, r, i4, k15, k8, PB_BROWN_MUSHROOM, use_pls, sky);
        }
    }
    if (jrand_int_bound(r, 8) == 0) {
        int j4 = jrand_int_bound(r, 16) + 8;
        int l8 = jrand_int_bound(r, 16) + 8;
        int k12 = w_gen_height(w, j4, l8) * 2;
        if (k12 > 0) {
            int l15 = jrand_int_bound(r, k12);
            pls_wg_bush(w, r, j4, l15, l8, PB_RED_MUSHROOM, use_pls, sky);
        }
    }
}

MC_HD static inline void pls_bd_cfg(int biome, BDCfg *c) {
    if (biome == 4) {
        *c = (BDCfg){10, 2, 2, 0, 0, 0, 0, 0, 1, 3, 1, 0};
    } else if (biome == 6) {
        *c = (BDCfg){2, 1, 5, 1, 8, 10, 0, 0, 0, 0, 1, 4};
    } else {
        *c = (BDCfg){10, 2, 1, 0, 1, 0, 0, 0, 1, 3, 1, 0};
    }
}

MC_HD static inline int pls_is_mesa(int biome) {
    return biome == 37 || biome == 38 || biome == 39 ||
           biome == 165 || biome == 166 || biome == 167;
}

MC_HD static inline void pls_mesa_extra_gold(World *w, JavaRandom *r) {
    MC_PROBE("ORE", "GOLD", r);
    bd_genStandardOre1(w, r, 20, 9, 32, 80, PB_GOLD_ORE);
}

/* bd_genDecorations without the SHROOM section (mushrooms applied separately). */
MC_HD static inline void pls_bd_gen_no_shroom(World *w, JavaRandom *r, int biome, const BDCfg *c,
                                              FoliageCoord *fol) {
    bd_generateOres(w, r);
    if (pls_is_mesa(biome)) pls_mesa_extra_gold(w, r);
    {
        int i;
        for (i = 0; i < c->sandPerChunk2; ++i) {
            int j = jrand_int_bound(r, 16) + 8;
            int k = jrand_int_bound(r, 16) + 8;
            wg_sand(w, r, j, w_topSolidOrLiquid(w, j, k), k, 7, PB_SAND);
        }
    }
    {
        int i;
        for (i = 0; i < c->clayPerChunk; ++i) {
            int j = jrand_int_bound(r, 16) + 8;
            int k = jrand_int_bound(r, 16) + 8;
            wg_clay(w, r, j, w_topSolidOrLiquid(w, j, k), k, 4);
        }
    }
    {
        int i;
        for (i = 0; i < c->sandPerChunk; ++i) {
            int j = jrand_int_bound(r, 16) + 8;
            int k = jrand_int_bound(r, 16) + 8;
            wg_sand(w, r, j, w_topSolidOrLiquid(w, j, k), k, 6, PB_GRAVEL);
        }
    }
    {
        int k1 = c->treesPerChunk;
        if (jrand_float(r) < 0.1F) ++k1;
        {
            int j2;
            for (j2 = 0; j2 < k1; ++j2) {
                int k6 = jrand_int_bound(r, 16) + 8;
                int l = jrand_int_bound(r, 16) + 8;
                /* live getHeight — see pls_bd_gen_full TREE loop comment */
                int py = w_height(w, k6, l);
                bd_genTree(w, r, biome, k6, py, l, fol);
            }
        }
    }
    {
        int l2;
        for (l2 = 0; l2 < c->flowersPerChunk; ++l2) {
            int i7 = jrand_int_bound(r, 16) + 8;
            int l10 = jrand_int_bound(r, 16) + 8;
            int j14 = w_gen_height(w, i7, l10) + 32;
            if (j14 > 0) {
                int k17 = jrand_int_bound(r, j14);
                int state = bd_flowerState(r, biome);
                wg_flowers(w, r, i7, k17, l10, state);
            }
        }
    }
    {
        int i3;
        for (i3 = 0; i3 < c->grassPerChunk; ++i3) {
            int j7 = jrand_int_bound(r, 16) + 8;
            int i11 = jrand_int_bound(r, 16) + 8;
            int k14 = w_gen_height(w, j7, i11) * 2;
            if (k14 > 0) {
                int l17 = jrand_int_bound(r, k14);
                int state = bd_grassState(r, biome);
                wg_tallgrass(w, r, j7, l17, i11, state);
            }
        }
    }
    {
        int j3;
        for (j3 = 0; j3 < c->deadBushPerChunk; ++j3) {
            int k7 = jrand_int_bound(r, 16) + 8;
            int j11 = jrand_int_bound(r, 16) + 8;
            int l14 = w_gen_height(w, k7, j11) * 2;
            if (l14 > 0) {
                int i18 = jrand_int_bound(r, l14);
                wg_deadbush(w, r, k7, i18, j11);
            }
        }
    }
    {
        int k3;
        for (k3 = 0; k3 < c->waterlilyPerChunk; ++k3) {
            int l7 = jrand_int_bound(r, 16) + 8;
            int k11 = jrand_int_bound(r, 16) + 8;
            int i15 = w_gen_height(w, l7, k11) * 2;
            if (i15 > 0) {
                int j18 = jrand_int_bound(r, i15);
                int by = j18;
                for (; by > 0; --by)
                    if (!w_isAir(w, l7, by - 1, k11)) break;
                wg_waterlily(w, r, l7, by, k11);
            }
        }
    }
    /* SHROOM omitted - pls_mushroom_decorate runs after light propagation */
    {
        int k4;
        for (k4 = 0; k4 < c->reedsPerChunk; ++k4) {
            int i9 = jrand_int_bound(r, 16) + 8;
            int l12 = jrand_int_bound(r, 16) + 8;
            int i16 = w_gen_height(w, i9, l12) * 2;
            if (i16 > 0) {
                int l18 = jrand_int_bound(r, i16);
                wg_reed(w, r, i9, l18, l12);
            }
        }
    }
    {
        int l4;
        for (l4 = 0; l4 < 10; ++l4) {
            int j9 = jrand_int_bound(r, 16) + 8;
            int i13 = jrand_int_bound(r, 16) + 8;
            int j16 = w_gen_height(w, j9, i13) * 2;
            if (j16 > 0) {
                int i19 = jrand_int_bound(r, j16);
                wg_reed(w, r, j9, i19, i13);
            }
        }
    }
    MC_PROBE("DEC", "PUMPKIN", r);
    MC_REDIRECT_POLL(w);
    if (jrand_int_bound(r, 32) == 0) {
        int i5 = jrand_int_bound(r, 16) + 8;
        int k9 = jrand_int_bound(r, 16) + 8;
        int j13 = w_gen_height(w, i5, k9) * 2;
        if (j13 > 0) {
            int k16 = jrand_int_bound(r, j13);
            wg_pumpkin(w, r, i5, k16, k9);
        }
    }
    {
        int j5;
        for (j5 = 0; j5 < c->cactiPerChunk; ++j5) {
            int l9 = jrand_int_bound(r, 16) + 8;
            int k13 = jrand_int_bound(r, 16) + 8;
            int l16 = w_gen_height(w, l9, k13) * 2;
            if (l16 > 0) {
                int j19 = jrand_int_bound(r, l16);
                wg_cactus(w, r, l9, j19, k13);
            }
        }
    }
    {
        int k5;
        for (k5 = 0; k5 < 50; ++k5) {
            int i10 = jrand_int_bound(r, 16) + 8;
            int l13 = jrand_int_bound(r, 16) + 8;
            int i17 = jrand_int_bound(r, 248) + 8;
            if (i17 > 0) {
                int k19 = jrand_int_bound(r, i17);
                wg_liquids(w, i10, k19, l13, PB_FLOWING_WATER);
            }
        }
    }
    {
        int l5;
        for (l5 = 0; l5 < 20; ++l5) {
            int j10 = jrand_int_bound(r, 16) + 8;
            int i14 = jrand_int_bound(r, 16) + 8;
            int a = jrand_int_bound(r, 240) + 8;
            int b = jrand_int_bound(r, a) + 8;
            int j17 = jrand_int_bound(r, b);
            wg_liquids(w, j10, j17, i14, PB_FLOWING_LAVA);
        }
    }
}

MC_HD static inline void pls_biome_decorate_no_shroom(World *w, JavaRandom *r, int biome,
                                                    FoliageCoord *fol) {
    if (biome == 4) {
        int i = jrand_int_bound(r, 5) - 3;
        bd_forest_addDoublePlants(w, r, i);
        {
            BDCfg c;
            pls_bd_cfg(biome, &c);
            pls_bd_gen_no_shroom(w, r, biome, &c, fol);
        }
    } else if (biome == 6) {
        BDCfg c;
        pls_bd_cfg(biome, &c);
        pls_bd_gen_no_shroom(w, r, biome, &c, fol);
        if (jrand_int_bound(r, 64) == 0)
            w->bigtree_heightLimit = w->bigtree_heightLimit;
    } else {
        int i1;
        for (i1 = 0; i1 < 7; ++i1) {
            int j1 = jrand_int_bound(r, 16) + 8;
            int k1 = jrand_int_bound(r, 16) + 8;
            int l1 = jrand_int_bound(r, w_gen_height(w, j1, k1) + 32);
            wg_doubleplant(w, r, j1, l1, k1, 3);
        }
        {
            BDCfg c;
            pls_bd_cfg(biome, &c);
            pls_bd_gen_no_shroom(w, r, biome, &c, fol);
        }
    }
}

/* ====================================================================================== */
/* FULL per-biome decorate dispatch (vanilla Biome subclasses), position-parametrized.     */
/* (bx0,bz0) = world block coords of window local (0,0) == base chunk origin; needed by    */
/* the GRASS_COLOR_NOISE-driven plains gates which sample WORLD coordinates.               */
/* Vanilla stream order restored: the SHROOM section runs IN PLACE (between waterlily and  */
/* reeds) - light is propagated right before it so mushroom canBlockStay sees real sky.    */
/* ====================================================================================== */

/* Block.canBeReplacedByLeaves default in 1.11.2: air or leaves only. Used by              */
/* WorldGenBigMushroom cap+stem cells.                                                     */
MC_HD static inline int pls_canBeReplacedByLeaves(int c) {
    return pb_canBeReplacedByLeaves(c);
}

MC_HD static inline int pls_absi(int v) { return v < 0 ? -v : v; }

/* Biome.GRASS_COLOR_NOISE.getValue at world coords (NoiseGeneratorPerlin(Random(2345),1)). */
MC_HD MC_NOINLINE static double pls_grass_noise(double x, double z) {
    CpPerlin gn;
    cp_grass_noise_init(&gn);
    return cp_perlin_getValue(&gn, x, z);
}

/* WorldGenCanopyTree.generate (dark oak; BiomeForest.ROOFED). */
MC_HD MC_NOINLINE static int pls_wg_canopy(World *w, JavaRandom *r, int x, int y, int z) {
    int i = jrand_int_bound(r, 3) + jrand_int_bound(r, 2) + 6;
    if (!(y >= 1 && y + i + 1 < 256)) return 0;
    if (!pb_canSustainSapling(w_get(w, x, y - 1, z))) return 0;
    if (!(y < 256 - i - 1)) return 0;
    /* placeTreeOfHeight: expanding rings of isReplaceable (air/leaves/wood/canGrowInto) */
    for (int l = 0; l <= i + 1; ++l) {
        int i1 = 1;
        if (l == 0) i1 = 0;
        if (l >= i - 1) i1 = 2;
        for (int j1 = -i1; j1 <= i1; ++j1)
            for (int k1 = -i1; k1 <= i1; ++k1) {
                int c = w_get(w, x + j1, y + l, z + k1);
                if (!(pb_isAir(c) || pb_isLeaves(c) || pb_isLog(c) || pb_canGrowInto(c)))
                    return 0;
            }
    }
    /* onPlantGrow on the 2x2 base: grass -> dirt */
    for (int dx = 0; dx <= 1; ++dx)
        for (int dz = 0; dz <= 1; ++dz)
            if (w_get(w, x + dx, y - 1, z + dz) == PB_GRASS)
                w_set(w, x + dx, y - 1, z + dz, PB_DIRT);
    {
        /* EnumFacing.Plane.HORIZONTAL.random: [NORTH,EAST,SOUTH,WEST][nextInt(4)] */
        static const int FX[4] = { 0, 1, 0, -1 };
        static const int FZ[4] = { -1, 0, 1, 0 };
        int f = jrand_int_bound(r, 4);
        int i1 = i - jrand_int_bound(r, 4);
        int j1 = 2 - jrand_int_bound(r, 3);
        int k1 = x, l1 = z, i2 = y + i - 1;
        for (int j2 = 0; j2 < i; ++j2) {
            if (j2 >= i1 && j1 > 0) { k1 += FX[f]; l1 += FZ[f]; --j1; }
            {
                int k2 = y + j2;
                int c = w_get(w, k1, k2, l1);
                if (pb_isAir(c) || pb_isLeaves(c)) {
                    /* placeLogAt (canGrowInto-guarded) on the 2x2 trunk */
                    static const int TX[4] = { 0, 1, 0, 1 };
                    static const int TZ[4] = { 0, 0, 1, 1 };
                    for (int t = 0; t < 4; ++t)
                        if (pb_canGrowInto(w_get(w, k1 + TX[t], k2, l1 + TZ[t])))
                            w_set(w, k1 + TX[t], k2, l1 + TZ[t], PB_LOG_DARKOAK);
                }
            }
        }
        /* crown quadrant-mirrored leaves at i2-1 / i2+1 */
        for (int i3 = -2; i3 <= 0; ++i3)
            for (int l3 = -2; l3 <= 0; ++l3) {
                int k4 = -1;
#define PLS_CANOPY_LEAF(LX, LY, LZ) do { \
        if (pb_isAir(w_get(w, (LX), (LY), (LZ)))) w_set(w, (LX), (LY), (LZ), PB_LEAVES_DARKOAK); \
    } while (0)
                PLS_CANOPY_LEAF(k1 + i3, i2 + k4, l1 + l3);
                PLS_CANOPY_LEAF(1 + k1 - i3, i2 + k4, l1 + l3);
                PLS_CANOPY_LEAF(k1 + i3, i2 + k4, 1 + l1 - l3);
                PLS_CANOPY_LEAF(1 + k1 - i3, i2 + k4, 1 + l1 - l3);
                if ((i3 > -2 || l3 > -1) && (i3 != -1 || l3 != -2)) {
                    k4 = 1;
                    PLS_CANOPY_LEAF(k1 + i3, i2 + k4, l1 + l3);
                    PLS_CANOPY_LEAF(1 + k1 - i3, i2 + k4, l1 + l3);
                    PLS_CANOPY_LEAF(k1 + i3, i2 + k4, 1 + l1 - l3);
                    PLS_CANOPY_LEAF(1 + k1 - i3, i2 + k4, 1 + l1 - l3);
                }
            }
        if (jrand_next(r, 1) != 0) {   /* nextBoolean: top cap */
            PLS_CANOPY_LEAF(k1, i2 + 2, l1);
            PLS_CANOPY_LEAF(k1 + 1, i2 + 2, l1);
            PLS_CANOPY_LEAF(k1 + 1, i2 + 2, l1 + 1);
            PLS_CANOPY_LEAF(k1, i2 + 2, l1 + 1);
        }
        for (int j3 = -3; j3 <= 4; ++j3)
            for (int i4 = -3; i4 <= 4; ++i4)
                if ((j3 != -3 || i4 != -3) && (j3 != -3 || i4 != 4) &&
                    (j3 != 4 || i4 != -3) && (j3 != 4 || i4 != 4) &&
                    (pls_absi(j3) < 3 || pls_absi(i4) < 3))
                    PLS_CANOPY_LEAF(k1 + j3, i2, l1 + i4);
        for (int k3 = -1; k3 <= 2; ++k3)
            for (int j4 = -1; j4 <= 2; ++j4)
                if ((k3 < 0 || k3 > 1 || j4 < 0 || j4 > 1) && jrand_int_bound(r, 3) <= 0) {
                    int l4 = jrand_int_bound(r, 3) + 2;
                    for (int i5 = 0; i5 < l4; ++i5)
                        if (pb_canGrowInto(w_get(w, x + k3, i2 - i5 - 1, z + j4)))
                            w_set(w, x + k3, i2 - i5 - 1, z + j4, PB_LOG_DARKOAK);
                    for (int j5 = -1; j5 <= 1; ++j5)
                        for (int l2 = -1; l2 <= 1; ++l2)
                            PLS_CANOPY_LEAF(k1 + k3 + j5, i2, l1 + j4 + l2);
                    for (int k5 = -2; k5 <= 2; ++k5)
                        for (int l5 = -2; l5 <= 2; ++l5)
                            if (pls_absi(k5) != 2 || pls_absi(l5) != 2)
                                PLS_CANOPY_LEAF(k1 + k3 + k5, i2 - 1, l1 + j4 + l5);
                }
#undef PLS_CANOPY_LEAF
    }
    return 1;
}

/* WorldGenBigMushroom.generate (type unspecified: nextBoolean brown/red). Id-level port    */
/* (cap/stem variants share one block id per color).                                       */
MC_HD MC_NOINLINE static int pls_wg_bigmushroom(World *w, JavaRandom *r, int x, int y, int z) {
    int block = (jrand_next(r, 1) != 0) ? PB_BROWN_SHROOM_BLOCK : PB_RED_SHROOM_BLOCK;
    int i = jrand_int_bound(r, 3) + 4;
    if (jrand_int_bound(r, 12) == 0) i *= 2;
    if (!(y >= 1 && y + i + 1 < 256)) return 0;
    for (int j = y; j <= y + 1 + i; ++j) {
        int k = (j <= y + 3) ? 0 : 3;
        for (int l = x - k; l <= x + k; ++l)
            for (int i1 = z - k; i1 <= z + k; ++i1) {
                int c = w_get(w, l, j, i1);
                if (!(pb_isAir(c) || pb_isLeaves(c))) return 0;
            }
    }
    {
        int below = w_get(w, x, y - 1, z);
        if (!(pb_isDirt(below) || below == PB_GRASS || below == PB_MYCELIUM)) return 0;
    }
    {
        int k2 = (block == PB_RED_SHROOM_BLOCK) ? y + i - 3 : y + i;
        for (int l2 = k2; l2 <= y + i; ++l2) {
            int j3 = (l2 < y + i) ? 2 : 1;
            if (block == PB_BROWN_SHROOM_BLOCK) j3 = 3;
            {
                int k3 = x - j3, l3 = x + j3, j1 = z - j3, k1 = z + j3;
                for (int l1 = k3; l1 <= l3; ++l1)
                    for (int i2 = j1; i2 <= k1; ++i2) {
                        int corner = (l1 == k3 || l1 == l3) && (i2 == j1 || i2 == k1);
                        int center = (l1 != k3 && l1 != l3 && i2 != j1 && i2 != k1);
                        if ((block == PB_BROWN_SHROOM_BLOCK || l2 < y + i) && corner) continue;
                        if (center && l2 < y + i) continue;   /* ALL_INSIDE skipped */
                        if (pls_canBeReplacedByLeaves(w_get(w, l1, l2, i2)))
                            w_set(w, l1, l2, i2, block);
                    }
            }
        }
        for (int i3 = 0; i3 < i; ++i3)
            if (pls_canBeReplacedByLeaves(w_get(w, x, y + i3, z)))
                w_set(w, x, y + i3, z, block);   /* stem (same block id) */
    }
    return 1;
}

/* BiomeDecorator.extraTreeChance: vanilla plains overrides to 0.05F, but FORGE loses it -
 * DeferredBiomeDecorator.fireCreateEventAndReplace copies every count field EXCEPT
 * extraTreeChance, so the REAL (Forge) game runs every biome at the 0.1F default. */
MC_HD static inline float pls_extra_tree_chance(int biome) {
    (void)biome;
    return 0.1f;
}

/* Per-biome BiomeDecorator field config (vanilla constructors; base defaults elsewhere). */
MC_HD static inline void pls_bd_cfg2(int biome, BDCfg *c) {
    switch (biome) {
        case 4: case 18:                       /* forest / forest hills (BiomeForest NORMAL) */
            *c = (BDCfg){10, 2, 2, 0, 0, 0, 0, 0, 1, 3, 1, 0}; break;
        case 27: case 28: case 155:            /* birch forest (hills) + Birch Forest M */
            *c = (BDCfg){10, 2, 2, 0, 0, 0, 0, 0, 1, 3, 1, 0}; break;
        case 132:                              /* flower forest (BiomeForest FLOWER) */
            *c = (BDCfg){6, 100, 1, 0, 0, 0, 0, 0, 1, 3, 1, 0}; break;
        case 29: case 157:                     /* roofed forest (+M): trees via canopy pass */
            *c = (BDCfg){-999, 2, 2, 0, 0, 0, 0, 0, 1, 3, 1, 0}; break;
        case 6: case 134:                      /* swamp + Swampland M (same BiomeSwamp ctor) */
            *c = (BDCfg){2, 1, 5, 1, 8, 10, 0, 0, 0, 0, 1, 4}; break;
        case 1: case 129:                      /* plains (+sunflower): flowers/grass re-set by noise gate */
            *c = (BDCfg){0, 4, 10, 0, 0, 0, 0, 0, 1, 3, 1, 0}; break;
        case 3: case 20: case 34: case 131: case 162: /* hills (+ edge 20; +trees 34) */
            *c = (BDCfg){biome == 34 ? 3 : 0, 2, 1, 0, 0, 0, 0, 0, 1, 3, 1, 0}; break;
        case 16: case 25:                      /* beach / stone beach: no trees/deadbush/reeds */
            *c = (BDCfg){-999, 2, 1, 0, 0, 0, 0, 0, 1, 3, 1, 0}; break;
        case 5: case 19: case 30: case 31: case 133: case 158:  /* taiga (all non-MEGA) */
            *c = (BDCfg){10, 2, 1, 0, 1, 0, 0, 0, 1, 3, 1, 0}; break;
        case 32: case 33: case 160: case 161:  /* mega taiga / mega spruce taiga */
            *c = (BDCfg){10, 2, 7, 1, 3, 0, 0, 0, 1, 3, 1, 0}; break;
        case 14: case 15:                      /* mushroom island(/shore) */
            *c = (BDCfg){-100, -100, -100, 0, 1, 0, 0, 1, 1, 3, 1, 0}; break;
        case 2: case 17: case 130:             /* desert/desert hills/mutated desert */
            *c = (BDCfg){-999, 2, 1, 2, 0, 50, 10, 0, 1, 3, 1, 0}; break;
        case 35: case 36:                      /* savanna / savanna plateau */
            *c = (BDCfg){1, 4, 20, 0, 0, 0, 0, 0, 1, 3, 1, 0}; break;
        case 163: case 164:                    /* BiomeSavannaMutated */
            *c = (BDCfg){2, 2, 5, 0, 0, 0, 0, 0, 1, 3, 1, 0}; break;
        case 21: case 22: case 149:            /* jungle / jungle hills / mutated jungle */
            *c = (BDCfg){50, 4, 25, 0, 0, 0, 0, 0, 1, 3, 1, 0}; break;
        case 23: case 151:                     /* jungle edge / mutated jungle edge */
            *c = (BDCfg){2, 4, 25, 0, 0, 0, 0, 0, 1, 3, 1, 0}; break;
        case 37: case 39: case 165: case 167:  /* mesa / plateau / bryce: BiomeMesa ctor */
            *c = (BDCfg){-999, 0, 1, 20, 0, 3, 5, 0, 1, 3, 1, 0}; break;
        case 38: case 166:                     /* mesa plateau F: hasForest => trees=5 */
            *c = (BDCfg){5, 0, 1, 20, 0, 3, 5, 0, 1, 3, 1, 0}; break;
        default:                               /* ocean(0)/river(7)/deep ocean(24)/...: base */
            *c = (BDCfg){0, 2, 1, 0, 0, 0, 0, 0, 1, 3, 1, 0}; break;
    }
}

/* biome.genBigTreeChance(rand) + generate: per-biome tree selector (exact draw order). */
MC_HD MC_NOINLINE static void pls_gen_tree(World *w, JavaRandom *r, int biome,
                                           int px, int py, int pz, FoliageCoord *fol) {
    switch (biome) {
        case 6: case 134:                      /* swamp (+M): verified bd_genTree path */
        case 5: case 19: case 30: case 31: case 133: case 158:  /* taiga (non-MEGA) */
            bd_genTree(w, r, biome, px, py, pz, fol); return;
        case 32: case 33: case 160: case 161: /* BiomeTaiga MEGA / MEGA_SPRUCE */
            bd_genTree(w, r, biome, px, py, pz, fol); return;
        case 4: case 18: case 132:             /* forest (hills, flower): bd_genTree else-branch */
            bd_genTree(w, r, biome, px, py, pz, fol); return;
        case 27: case 28:                      /* birch forest: BIRCH_TREE, zero selector draws */
            wg_birch(w, r, px, py, pz); return;
        case 155:                              /* BiomeForestMutated: nextBoolean SUPER vs BIRCH */
            if (jrand_next(r, 1) != 0) wg_birch_ex(w, r, px, py, pz, 1);
            else wg_birch(w, r, px, py, pz);
            return;
        case 12: case 13: case 140:            /* BiomeSnow: WorldGenTaiga2(false), no selector draw */
            wg_taiga2(w, r, px, py, pz); return;
        case 35: case 36: case 163: case 164:  /* BiomeSavanna: nextInt(5)>0 ? acacia : oak */
            if (jrand_int_bound(r, 5) > 0) wg_savannatree(w, r, px, py, pz);
            else wg_trees(w, r, px, py, pz, PB_LOG_OAK, PB_LEAVES_OAK);
            return;
        case 21: case 22: case 23: case 149: case 151:  /* BiomeJungle selector */
            bd_genTree(w, r, biome, px, py, pz, fol); return;
        case 37: case 38: case 39: case 165: case 166: case 167:  /* BiomeMesa: TREE_FEATURE */
            wg_trees(w, r, px, py, pz, PB_LOG_OAK, PB_LEAVES_OAK); return;
        case 1: case 129:                      /* plains: nextInt(3)==0 ? big : oak */
            if (jrand_int_bound(r, 3) == 0) wg_bigtree(w, r, px, py, pz, fol);
            else wg_trees(w, r, px, py, pz, PB_LOG_OAK, PB_LEAVES_OAK);
            return;
        case 3: case 20: case 34: case 131: case 162: /* hills (+edge): nextInt(3)>0 ? spruce : base */
            if (jrand_int_bound(r, 3) > 0) wg_taiga2(w, r, px, py, pz);
            else if (jrand_int_bound(r, 10) == 0) wg_bigtree(w, r, px, py, pz, fol);
            else wg_trees(w, r, px, py, pz, PB_LOG_OAK, PB_LEAVES_OAK);
            return;
        case 29: case 157:                     /* roofed (+M): nextInt(3)>0 ? canopy : forest chain */
            if (jrand_int_bound(r, 3) > 0) { pls_wg_canopy(w, r, px, py, pz); return; }
            if (jrand_int_bound(r, 5) != 0) {
                if (jrand_int_bound(r, 10) == 0) wg_bigtree(w, r, px, py, pz, fol);
                else wg_trees(w, r, px, py, pz, PB_LOG_OAK, PB_LEAVES_OAK);
            } else wg_birch(w, r, px, py, pz);
            return;
        default:                               /* base Biome: nextInt(10)==0 ? big : oak */
            if (jrand_int_bound(r, 10) == 0) wg_bigtree(w, r, px, py, pz, fol);
            else wg_trees(w, r, px, py, pz, PB_LOG_OAK, PB_LEAVES_OAK);
            return;
    }
}

/* biome.pickRandomFlower(rand, pos) -> PB flower code. (wx,wz) are WORLD coords of pos.   */
MC_HD MC_NOINLINE static int pls_pick_flower(JavaRandom *r, int biome, int wx, int wz) {
    if (biome == 6 || biome == 134) return PB_RED_FLOWER_BASE + 1;  /* swamp (+M): BLUE_ORCHID */
    if (biome == 1 || biome == 129) {                          /* plains(+sunflower): noise + tulips */
        double d0 = pls_grass_noise((double)wx / 200.0, (double)wz / 200.0);
        if (d0 < -0.8) {
            int j = jrand_int_bound(r, 4);
            /* ORANGE_TULIP(5) RED_TULIP(4) PINK_TULIP(7) WHITE_TULIP(6) */
            return PB_RED_FLOWER_BASE + (j == 0 ? 5 : (j == 1 ? 4 : (j == 2 ? 7 : 6)));
        }
        if (jrand_int_bound(r, 3) > 0) {
            int i = jrand_int_bound(r, 3);
            /* POPPY(0) HOUSTONIA(3) OXEYE_DAISY(8) */
            return PB_RED_FLOWER_BASE + (i == 0 ? 0 : (i == 1 ? 3 : 8));
        }
        return PB_YELLOW_FLOWER;
    }
    if (biome == 132) {
        /* BiomeForest FLOWER: GRASS_COLOR_NOISE-picked EnumFlowerType, NO rand draws.
         * values() order: DANDELION,POPPY,BLUE_ORCHID,ALLIUM,HOUSTONIA,RED_TULIP,
         * ORANGE_TULIP,WHITE_TULIP,PINK_TULIP,OXEYE_DAISY; BLUE_ORCHID -> POPPY. */
        double d0 = (1.0 + pls_grass_noise((double)wx / 48.0, (double)wz / 48.0)) / 2.0;
        if (d0 < 0.0) d0 = 0.0;
        if (d0 > 0.9999) d0 = 0.9999;
        int idx = (int)(d0 * 10.0);
        if (idx == 0) return PB_YELLOW_FLOWER;
        if (idx == 2) idx = 1;
        return PB_RED_FLOWER_BASE + (idx - 1);
    }
    return jrand_int_bound(r, 3) > 0 ? PB_YELLOW_FLOWER : (PB_RED_FLOWER_BASE + 0);
}

/* WorldGenFossils: separate chunk-seeded Random; never consumes the decorate stream. */
MC_HD static inline i64 pls_java_int_mul3(int a, int b, int c) {
    return (i64)(i32)((u32)(i32)a * (u32)(i32)b * (u32)(i32)c);
}

MC_HD static inline i64 pls_java_int_mul2(int a, int b) {
    return (i64)(i32)((u32)(i32)a * (u32)(i32)b);
}

MC_HD static inline int pls_fossil_rot_meta(int id, int meta, int rot) {
    if (id == 216 && (rot == 1 || rot == 3)) {
        if (meta == 4) return 8;
        if (meta == 8) return 4;
    }
    return meta;
}

MC_HD static inline void pls_fossil_transform(int x, int y, int z, int rot,
                                              int *tx, int *ty, int *tz) {
    *ty = y;
    if (rot == 1) { *tx = -z; *tz = x; }
    else if (rot == 2) { *tx = -x; *tz = -z; }
    else if (rot == 3) { *tx = z; *tz = -x; }
    else { *tx = x; *tz = z; }
}

MC_HD MC_NOINLINE static void pls_fossil_add_template(World *w, JavaRandom *r,
                                                      const WgFossilTemplate *t,
                                                      int rot, int ox, int oy, int oz,
                                                      float integrity) {
    int n;
    for (n = 0; n < (int)t->count; ++n) {
        const WgFossilBlock *b = &wg_tbl_WG_FOSSIL_BLOCKS()[(int)t->first + n];
        int tx, ty, tz, wx, wy, wz, meta, pb;
        if (jrand_float(r) > integrity) continue;  /* BlockRotationProcessor first */
        pls_fossil_transform((int)b->x, (int)b->y, (int)b->z, rot, &tx, &ty, &tz);
        wx = ox + tx;
        wy = oy + ty;
        wz = oz + tz;
        if (wx < 0 || wx > 15 || wy < 0 || wy > 256 || wz < 0 || wz > 15) continue;
        meta = pls_fossil_rot_meta((int)b->id, (int)b->meta, rot);
        (void)meta;  /* PB world stores the fossil block id; metadata is retained in data table. */
        if ((int)b->id == 216) pb = PB_BONE_BLOCK;
        else if ((int)b->id == 16) pb = PB_COAL_ORE;
        else continue;
        w_set(w, wx, wy, wz, pb);
    }
}

MC_HD MC_NOINLINE static void pls_wg_fossils(World *w, i64 worldSeed, int bx0, int bz0) {
    int bcx = bx0 / 16;
    int bcz = bz0 / 16;
    i64 s = worldSeed +
            pls_java_int_mul3(bcx, bcx, 4987142) +
            pls_java_int_mul2(bcx, 5947611) +
            (i64)(i32)((u32)(i32)bcz * (u32)(i32)bcz) * 4392871LL +
            pls_java_int_mul2(bcz, 389711);
    JavaRandom fr;
    int rot, i, sx, sz, rx, rz, j, k, l, i1, j1, k1, ox, oz;
    const WgFossilTemplate *t;
    jrand_set(&fr, s ^ 987234911LL);
    rot = jrand_int_bound(&fr, 4);   /* Rotation.values() order */
    i = jrand_int_bound(&fr, 8);
    t = &wg_tbl_WG_FOSSILS()[i];
    sx = (int)t->sx;
    sz = (int)t->sz;
    rx = (rot == 1 || rot == 3) ? sz : sx;
    rz = (rot == 1 || rot == 3) ? sx : sz;
    j = jrand_int_bound(&fr, 16 - rx);
    k = jrand_int_bound(&fr, 16 - rz);
    l = 256;
    for (i1 = 0; i1 < rx; ++i1)
        for (j1 = 0; j1 < rx; ++j1) {  /* vanilla bug: sizeX used for both axes */
            int h = w_gen_height(w, i1 + j, j1 + k);
            if (h < l) l = h;
        }
    k1 = l - 15 - jrand_int_bound(&fr, 10);
    if (k1 < 10) k1 = 10;
    ox = j;
    oz = k;
    if (rot == 1) ox += sz - 1;
    else if (rot == 2) { ox += sx - 1; oz += sz - 1; }
    else if (rot == 3) oz += sx - 1;
    pls_fossil_add_template(w, &fr, t, rot, ox, k1, oz, 0.9f);
    pls_fossil_add_template(w, &fr, &wg_tbl_WG_FOSSILS_COAL()[i], rot, ox, k1, oz, 0.1f);
}

/* BiomeDecorator.genDecorations, vanilla section order, SHROOM in place (light propagated */
/* immediately before it).                                                                 */
MC_HD MC_NOINLINE static void pls_bd_gen_full(World *w, JavaRandom *r, int biome, const BDCfg *c,
                                              FoliageCoord *fol, int bx0, int bz0,
                                              u8 *lt_sky, u8 *lt_blk, u8 *lt_tsky, u8 *lt_tblk) {
    MC_PROBE("DECPRE", "-", r);
    bd_generateOres(w, r);
    if (pls_is_mesa(biome)) pls_mesa_extra_gold(w, r);
    MC_PROBE("DEC", "SAND", r);
    MC_REDIRECT_POLL(w);
    for (int i = 0; i < c->sandPerChunk2; ++i) {
        int j = jrand_int_bound(r, 16) + 8;
        int k = jrand_int_bound(r, 16) + 8;
        wg_sand(w, r, j, w_topSolidOrLiquid(w, j, k), k, 7, PB_SAND);
    }
    MC_PROBE("DEC", "CLAY", r);
    MC_REDIRECT_POLL(w);
    for (int i = 0; i < c->clayPerChunk; ++i) {
        int j = jrand_int_bound(r, 16) + 8;
        int k = jrand_int_bound(r, 16) + 8;
        wg_clay(w, r, j, w_topSolidOrLiquid(w, j, k), k, 4);
    }
    MC_PROBE("DEC", "SAND_PASS2", r);
    MC_REDIRECT_POLL(w);
    for (int i = 0; i < c->sandPerChunk; ++i) {
        int j = jrand_int_bound(r, 16) + 8;
        int k = jrand_int_bound(r, 16) + 8;
        wg_sand(w, r, j, w_topSolidOrLiquid(w, j, k), k, 6, PB_GRAVEL);
    }
    {
        int k1 = c->treesPerChunk;
        if (jrand_float(r) < pls_extra_tree_chance(biome)) ++k1;
        MC_PROBE("DEC", "TREE", r);
        MC_REDIRECT_POLL(w);
        for (int j2 = 0; j2 < k1; ++j2) {
            MC_REDIRECT_POLL(w);   /* a cascading tree corrupts the NEXT attempt's frame */
            int k6 = jrand_int_bound(r, 16) + 8;
            int l = jrand_int_bound(r, 16) + 8;
            /* Java: worldIn.getHeight(chunkPos.add(k6,0,l)) — LIVE heightMap after
             * prior setBlockState. Leaves opacity=1 raises the map, so later tree
             * attempts on canopy columns plant higher and often fail canSustainPlant.
             * w_gen_height uses STALE popSkyHeight (pre-decorate) and over-places trees.
             * Use w_height (live block scan) for plant Y only. */
            pls_gen_tree(w, r, biome, k6, w_height(w, k6, l), l, fol);
        }
    }
    MC_PROBE("DEC", "BIG_SHROOM", r);
    MC_REDIRECT_POLL(w);
    for (int k2 = 0; k2 < c->bigMushroomsPerChunk; ++k2) {
        MC_REDIRECT_POLL(w);   /* a cascading big shroom corrupts the NEXT attempt's frame */
        int l6 = jrand_int_bound(r, 16) + 8;
        int k10 = jrand_int_bound(r, 16) + 8;
        pls_wg_bigmushroom(w, r, l6, w_gen_height(w, l6, k10), k10);
    }
    MC_PROBE("DEC", "FLOWERS", r);
    MC_REDIRECT_POLL(w);
    for (int l2 = 0; l2 < c->flowersPerChunk; ++l2) {
        int i7 = jrand_int_bound(r, 16) + 8;
        int l10 = jrand_int_bound(r, 16) + 8;
        int j14 = w_gen_height(w, i7, l10) + 32;
        if (j14 > 0) {
            int k17 = jrand_int_bound(r, j14);
            int state = pls_pick_flower(r, biome, bx0 + i7, bz0 + l10);
            wg_flowers(w, r, i7, k17, l10, state);
        }
    }
    MC_PROBE("DEC", "GRASS", r);
    MC_REDIRECT_POLL(w);
    for (int i3 = 0; i3 < c->grassPerChunk; ++i3) {
        int j7 = jrand_int_bound(r, 16) + 8;
        int i11 = jrand_int_bound(r, 16) + 8;
        int k14 = w_gen_height(w, j7, i11) * 2;
        if (k14 > 0) {
            int l17 = jrand_int_bound(r, k14);
            int state = bd_grassState(r, biome);
            wg_tallgrass(w, r, j7, l17, i11, state);
        }
    }
    MC_PROBE("DEC", "DEAD_BUSH", r);
    MC_REDIRECT_POLL(w);
    for (int j3 = 0; j3 < c->deadBushPerChunk; ++j3) {
        int k7 = jrand_int_bound(r, 16) + 8;
        int j11 = jrand_int_bound(r, 16) + 8;
        int l14 = w_gen_height(w, k7, j11) * 2;
        if (l14 > 0) {
            int i18 = jrand_int_bound(r, l14);
            wg_deadbush(w, r, k7, i18, j11);
        }
    }
    MC_PROBE("DEC", "LILYPAD", r);
    MC_REDIRECT_POLL(w);
    for (int k3 = 0; k3 < c->waterlilyPerChunk; ++k3) {
        int l7 = jrand_int_bound(r, 16) + 8;
        int k11 = jrand_int_bound(r, 16) + 8;
        int i15 = w_gen_height(w, l7, k11) * 2;
        if (i15 > 0) {
            int j18 = jrand_int_bound(r, i15);
            int by = j18;
            for (; by > 0; --by)
                if (!w_isAir(w, l7, by - 1, k11)) break;
            wg_waterlily(w, r, l7, by, k11);
        }
    }
    /* SHROOM section IN PLACE (vanilla order). canBlockStay light: the stale vanilla
     * skylight array when attached (w->popSkyLight, incremental, nothing to do here),
     * else the fixpoint CA. */
    MC_PROBE("DEC", "SHROOM", r);
    MC_REDIRECT_POLL(w);
    if (!w->popSkyLight)
        pls_propagate(w, lt_sky, lt_blk, lt_tsky, lt_tblk, PLS_MAX_LIGHT_ITERS);
    pls_mushroom_decorate(w, r, c, 1, lt_sky);
    MC_PROBE("DEC", "REED", r);
    MC_REDIRECT_POLL(w);
    for (int k4 = 0; k4 < c->reedsPerChunk; ++k4) {
        int i9 = jrand_int_bound(r, 16) + 8;
        int l12 = jrand_int_bound(r, 16) + 8;
        int i16 = w_gen_height(w, i9, l12) * 2;
        if (i16 > 0) {
            int l18 = jrand_int_bound(r, i16);
            wg_reed(w, r, i9, l18, l12);
        }
    }
    for (int l4 = 0; l4 < 10; ++l4) {
        int j9 = jrand_int_bound(r, 16) + 8;
        int i13 = jrand_int_bound(r, 16) + 8;
        int j16 = w_gen_height(w, j9, i13) * 2;
        if (j16 > 0) {
            int i19 = jrand_int_bound(r, j16);
            wg_reed(w, r, j9, i19, i13);
        }
    }
    MC_PROBE("DEC", "PUMPKIN", r);
    MC_REDIRECT_POLL(w);
    if (jrand_int_bound(r, 32) == 0) {
        int i5 = jrand_int_bound(r, 16) + 8;
        int k9 = jrand_int_bound(r, 16) + 8;
        int j13 = w_gen_height(w, i5, k9) * 2;
        if (j13 > 0) {
            int k16 = jrand_int_bound(r, j13);
            wg_pumpkin(w, r, i5, k16, k9);
        }
    }
    MC_PROBE("DEC", "CACTUS", r);
    MC_REDIRECT_POLL(w);
    for (int j5 = 0; j5 < c->cactiPerChunk; ++j5) {
        int l9 = jrand_int_bound(r, 16) + 8;
        int k13 = jrand_int_bound(r, 16) + 8;
        int l16 = w_gen_height(w, l9, k13) * 2;
        if (l16 > 0) {
            int j19 = jrand_int_bound(r, l16);
            wg_cactus(w, r, l9, j19, k13);
        }
    }
    MC_PROBE("DEC", "LAKE_WATER", r);
    MC_REDIRECT_POLL(w);
    for (int k5 = 0; k5 < 50; ++k5) {
        int i10 = jrand_int_bound(r, 16) + 8;
        int l13 = jrand_int_bound(r, 16) + 8;
        int i17 = jrand_int_bound(r, 248) + 8;
        if (i17 > 0) {
            int k19 = jrand_int_bound(r, i17);
            wg_liquids(w, i10, k19, l13, PB_FLOWING_WATER);
        }
    }
    MC_PROBE("DEC", "LAKE_LAVA", r);
    MC_REDIRECT_POLL(w);
    for (int l5 = 0; l5 < 20; ++l5) {
        int j10 = jrand_int_bound(r, 16) + 8;
        int i14 = jrand_int_bound(r, 16) + 8;
        int a = jrand_int_bound(r, 240) + 8;
        int b = jrand_int_bound(r, a) + 8;
        int j17 = jrand_int_bound(r, b);
        wg_liquids(w, j10, j17, i14, PB_FLOWING_LAVA);
    }
    MC_PROBE("DECPOST", "-", r);
    MC_REDIRECT_RESTORE(w);   /* chunkPos corruption ends with genDecorations */
}

/* Biome.decorate per subclass: pre/post passes around genDecorations.                     */
MC_HD MC_NOINLINE static void pls_biome_decorate_full(World *w, JavaRandom *r, int biome,
                                                      FoliageCoord *fol, int bx0, int bz0,
                                                      i64 worldSeed,
                                                      u8 *lt_sky, u8 *lt_blk, u8 *lt_tsky, u8 *lt_tblk) {
    MC_REDIRECT_CANCEL();   /* vanilla decorate() re-sets chunkPos at entry */
    BDCfg c;
    pls_bd_cfg2(biome, &c);
    if (biome == 4 || biome == 18 || biome == 27 || biome == 28 || biome == 132) {
        /* BiomeForest NORMAL/BIRCH/FLOWER: addDoublePlants(nextInt(5)-3, FLOWER +2) then super */
        MC_PROBE("DEC", "FLOWERS", r);
        MC_REDIRECT_POLL(w);
        int i = jrand_int_bound(r, 5) - 3;
        if (biome == 132) i += 2;
        bd_forest_addDoublePlants(w, r, i);
        pls_bd_gen_full(w, r, biome, &c, fol, bx0, bz0, lt_sky, lt_blk, lt_tsky, lt_tblk);
    } else if (biome == 29 || biome == 157) {
        /* BiomeForest ROOFED (id 157 = Roofed Forest M, same BiomeForest(ROOFED) instance):
         * 4x4 canopy grid, then addDoublePlants, then super */
        for (int gi = 0; gi < 4; ++gi)
            for (int gj = 0; gj < 4; ++gj) {
                int k = gi * 4 + 1 + 8 + jrand_int_bound(r, 3);
                int l = gj * 4 + 1 + 8 + jrand_int_bound(r, 3);
                int py = w_height(w, k, l); /* live getHeight, same as TREE loop */
                if (jrand_int_bound(r, 20) == 0) {
                    MC_PROBE("DEC", "BIG_SHROOM", r);
                    MC_REDIRECT_POLL(w);
                    pls_wg_bigmushroom(w, r, k, py, l);
                } else {
                    MC_PROBE("DEC", "TREE", r);
                    MC_REDIRECT_POLL(w);
                    pls_gen_tree(w, r, 29, k, py, l, fol);
                }
            }
        {
            MC_PROBE("DEC", "FLOWERS", r);
            MC_REDIRECT_POLL(w);
            int i = jrand_int_bound(r, 5) - 3;
            bd_forest_addDoublePlants(w, r, i);
        }
        pls_bd_gen_full(w, r, biome, &c, fol, bx0, bz0, lt_sky, lt_blk, lt_tsky, lt_tblk);
    } else if (biome == 6 || biome == 134) {
        /* swamp (+M): super then the fossil gate (template-driven; not on this stream's blocks) */
        pls_bd_gen_full(w, r, biome, &c, fol, bx0, bz0, lt_sky, lt_blk, lt_tsky, lt_tblk);
        MC_PROBE("DEC", "FOSSIL", r);
        MC_REDIRECT_POLL(w);
        if (jrand_int_bound(r, 64) == 0) pls_wg_fossils(w, worldSeed, bx0, bz0);
    } else if (biome == 2 || biome == 17 || biome == 130) {
        /* BiomeDesert: super, then DESERT_WELL gate, then FOSSIL gate. */
        pls_bd_gen_full(w, r, biome, &c, fol, bx0, bz0, lt_sky, lt_blk, lt_tsky, lt_tblk);
        MC_PROBE("DEC", "DESERT_WELL", r);
        MC_REDIRECT_POLL(w);
        if (jrand_int_bound(r, 1000) == 0) {
            int i = jrand_int_bound(r, 16) + 8;
            int j = jrand_int_bound(r, 16) + 8;
            wg_desert_well(w, i, w_gen_height(w, i, j) + 1, j);
        }
        MC_PROBE("DEC", "FOSSIL", r);
        MC_REDIRECT_POLL(w);
        if (jrand_int_bound(r, 64) == 0) pls_wg_fossils(w, worldSeed, bx0, bz0);
    } else if (biome == 35 || biome == 36) {
        /* BiomeSavanna: 7 GRASS double plants before super.decorate. */
        MC_PROBE("DEC", "GRASS", r);
        MC_REDIRECT_POLL(w);
        for (int i = 0; i < 7; ++i) {
            int j = jrand_int_bound(r, 16) + 8;
            int k = jrand_int_bound(r, 16) + 8;
            int l = jrand_int_bound(r, w_gen_height(w, j, k) + 32);
            wg_doubleplant(w, r, j, l, k, 2);
        }
        pls_bd_gen_full(w, r, biome, &c, fol, bx0, bz0, lt_sky, lt_blk, lt_tsky, lt_tblk);
    } else if (biome == 1 || biome == 129) {
        /* BiomePlains.decorate: GRASS_COLOR_NOISE gate at (chunk+8) WORLD coords */
        double d0 = pls_grass_noise((double)(bx0 + 8) / 200.0, (double)(bz0 + 8) / 200.0);
        if (d0 < -0.8) {
            c.flowersPerChunk = 15;
            c.grassPerChunk = 5;
        } else {
            c.flowersPerChunk = 4;
            c.grassPerChunk = 10;
            MC_PROBE("DEC", "GRASS", r);
            MC_REDIRECT_POLL(w);
            for (int i = 0; i < 7; ++i) {
                int j = jrand_int_bound(r, 16) + 8;
                int k = jrand_int_bound(r, 16) + 8;
                int l = jrand_int_bound(r, w_gen_height(w, j, k) + 32);
                wg_doubleplant(w, r, j, l, k, 2);   /* EnumPlantType.GRASS */
            }
        }
        if (biome == 129) {                    /* sunflower plains: 10x SUNFLOWER double plants */
            MC_PROBE("DEC", "FLOWERS", r);
            MC_REDIRECT_POLL(w);
            for (int i1 = 0; i1 < 10; ++i1) {
                int j1 = jrand_int_bound(r, 16) + 8;
                int k1 = jrand_int_bound(r, 16) + 8;
                int l1 = jrand_int_bound(r, w_gen_height(w, j1, k1) + 32);
                wg_doubleplant(w, r, j1, l1, k1, 0);   /* EnumPlantType.SUNFLOWER */
            }
        }
        pls_bd_gen_full(w, r, biome, &c, fol, bx0, bz0, lt_sky, lt_blk, lt_tsky, lt_tblk);
    } else if (biome == 3 || biome == 20 || biome == 34 || biome == 131 || biome == 162) {
        /* BiomeHills (+ Extreme Hills Edge id 20): super, emerald, 7 silverfish */
        pls_bd_gen_full(w, r, biome, &c, fol, bx0, bz0, lt_sky, lt_blk, lt_tsky, lt_tblk);
        {
            MC_PROBE("ORE", "EMERALD", r);
            int count = 3 + jrand_int_bound(r, 6);
            for (int i = 0; i < count; ++i) {
                int bx = jrand_int_bound(r, 16);
                int by = jrand_int_bound(r, 28) + 4;
                int bz = jrand_int_bound(r, 16);
                if (pb_isNaturalStone(w_get(w, bx, by, bz)))
                    w_set(w, bx, by, bz, PB_EMERALD_ORE);
            }
        }
        for (int k1 = 0; k1 < 7; ++k1) {
            int l1 = jrand_int_bound(r, 16);
            int i2 = jrand_int_bound(r, 64);
            int j2 = jrand_int_bound(r, 16);
            MC_PROBE("ORE", "SILVERFISH", r);
            wg_minable(w, r, l1, i2, j2, 9, PB_MONSTER_EGG);
        }
    } else if (biome == 32 || biome == 33 || biome == 160 || biome == 161) {
        /* BiomeTaiga MEGA/MEGA_SPRUCE: boulders, 7 FERN double plants, then super. */
        MC_PROBE("DEC", "ROCK", r);
        MC_REDIRECT_POLL(w);
        {
            int i = jrand_int_bound(r, 3);
            for (int j = 0; j < i; ++j) {
                int k = jrand_int_bound(r, 16) + 8;
                int l = jrand_int_bound(r, 16) + 8;
                wg_blockblob(w, r, k, w_gen_height(w, k, l), l, PB_MOSSY_COBBLESTONE, 0);
            }
        }
        MC_PROBE("DEC", "FLOWERS", r);
        MC_REDIRECT_POLL(w);
        for (int i1 = 0; i1 < 7; ++i1) {
            int j1 = jrand_int_bound(r, 16) + 8;
            int k1 = jrand_int_bound(r, 16) + 8;
            int l1 = jrand_int_bound(r, w_gen_height(w, j1, k1) + 32);
            wg_doubleplant(w, r, j1, l1, k1, 3);
        }
        pls_bd_gen_full(w, r, biome, &c, fol, bx0, bz0, lt_sky, lt_blk, lt_tsky, lt_tblk);
    } else if (biome == 5 || biome == 19 || biome == 30 || biome == 31 ||
               biome == 133 || biome == 158) {
        /* BiomeTaiga (all non-MEGA ids): 7 FERN double plants then super.
         * MEGA/MEGA_SPRUCE ids (32/33/160/161) are handled above. */
        MC_PROBE("DEC", "FLOWERS", r);
        MC_REDIRECT_POLL(w);
        for (int i1 = 0; i1 < 7; ++i1) {
            int j1 = jrand_int_bound(r, 16) + 8;
            int k1 = jrand_int_bound(r, 16) + 8;
            int l1 = jrand_int_bound(r, w_gen_height(w, j1, k1) + 32);
            wg_doubleplant(w, r, j1, l1, k1, 3);
        }
        pls_bd_gen_full(w, r, biome, &c, fol, bx0, bz0, lt_sky, lt_blk, lt_tsky, lt_tblk);
    } else if (biome == 21 || biome == 22 || biome == 23 || biome == 149 || biome == 151) {
        /* BiomeJungle: super, then one melon patch and 50 WorldGenVines attempts. */
        pls_bd_gen_full(w, r, biome, &c, fol, bx0, bz0, lt_sky, lt_blk, lt_tsky, lt_tblk);
        {
            int i = jrand_int_bound(r, 16) + 8;
            int j = jrand_int_bound(r, 16) + 8;
            int height = w_gen_height(w, i, j) * 2;
            if (height < 1) height = 1;
            {
                int k = jrand_int_bound(r, height);
                MC_PROBE("DEC", "PUMPKIN", r);
                MC_REDIRECT_POLL(w);
                wg_melon(w, r, i, k, j);
            }
        }
        MC_PROBE("DEC", "GRASS", r);
        MC_REDIRECT_POLL(w);
        for (int j = 0; j < 50; ++j) {
            int k = jrand_int_bound(r, 16) + 8;
            int i1 = jrand_int_bound(r, 16) + 8;
            wg_vines(w, r, k, 128, i1);
        }
    } else {
        /* base Biome.decorate (ocean/river/beach/stone beach/deep ocean/...): super only */
        pls_bd_gen_full(w, r, biome, &c, fol, bx0, bz0, lt_sky, lt_blk, lt_tsky, lt_tblk);
    }
}

MC_HD static inline void pls_populate_no_shroom(World *w, JavaRandom *r, i64 seed, FoliageCoord *fol) {
    int biome = w_getBiome(w, 16, 16);
    w_reset_loaded_chunks(w, seed, 0, 0);
    w->activeRand = NULL;  /* cascade clobber hook DISARMED: trigger needs java global load order, see DEVLOG */
    jrand_set(r, seed);
    {
        i64 k = jrand_long(r) / 2 * 2 + 1;
        i64 l = jrand_long(r) / 2 * 2 + 1;
        (void)k;
        (void)l;
        jrand_set(r, (i64)0 * k + (i64)0 * l ^ seed);
    }
    if (biome != 2 && biome != 17 && jrand_int_bound(r, 4) == 0) {
        int i1 = jrand_int_bound(r, 16) + 8;
        int j1 = jrand_int_bound(r, 256);
        int k1 = jrand_int_bound(r, 16) + 8;
        wg_lakes(w, r, i1, j1, k1, PB_WATER);
    }
    if (jrand_int_bound(r, 8) == 0) {
        int i2 = jrand_int_bound(r, 16) + 8;
        int inner = jrand_int_bound(r, 248) + 8;
        int l2 = jrand_int_bound(r, inner);
        int k3 = jrand_int_bound(r, 16) + 8;
        if (l2 < POP_SEA_LEVEL || jrand_int_bound(r, 10) == 0)
            wg_lakes(w, r, i2, l2, k3, PB_LAVA);
    }
    {
        int j2;
        for (j2 = 0; j2 < 8; ++j2) {
            int i3 = jrand_int_bound(r, 16) + 8;
            int l3 = jrand_int_bound(r, 256);
            int l1 = jrand_int_bound(r, 16) + 8;
            wg_dungeons(w, r, i3, l3, l1);
        }
    }
    pls_biome_decorate_no_shroom(w, r, biome, fol);
    w->activeRand = NULL;
}

MC_HD static inline void pls_build_world(World *w, CpScratch *sc, ChunkPrimer *primer,
                                         JavaRandom *r, FoliageCoord *fol, i64 seed) {
    int i;
    for (i = 0; i < W_N; ++i) w->blocks[i] = (u16)PB_AIR;
    w->bigtree_heightLimit = 0;
    w_reset_loaded_chunks(w, seed, 0, 0);
    {
        GLNode nodes[GL_MAX_NODES];
        int voronoi;
        gl_build(nodes, seed, &voronoi);
        {
            sc->arena.off = 0;   /* reset bump arena at top-level tree */
            int *fb = gl_getInts(nodes, &sc->arena, voronoi, 0, 0, W_X, W_Z);
            int x, z;
            for (x = 0; x < W_X; ++x)
                for (z = 0; z < W_Z; ++z)
                    w->fullBiome[x * W_Z + z] = fb[z + x * W_Z];
        }
    }
    {
        int cx, cz;
        for (cx = 0; cx < 2; ++cx) {
            for (cz = 0; cz < 2; ++cz) {
                cp_provide_chunk(primer, sc, w->st, seed, cx, cz);
                {
                    int lx, lz, y;
                    for (lx = 0; lx < 16; ++lx)
                        for (lz = 0; lz < 16; ++lz)
                            for (y = 0; y < 256; ++y)
                                w_set(w, cx * 16 + lx, y, cz * 16 + lz,
                                      cb_get(primer, lx, y, lz));
                }
            }
        }
    }
    pls_populate_no_shroom(w, r, seed, fol);
}

MC_HD static inline void pls_copy_world_blocks(const World *src, World *dst) {
    int i;
    for (i = 0; i < W_N; ++i) dst->blocks[i] = src->blocks[i];
}

typedef struct {
    int n;
    int *idx;
    u16 *blk;
} PlsOutBuf;

MC_HD static inline void pls_emit_buf(int idx, u16 block, void *ctx) {
    PlsOutBuf *b = (PlsOutBuf *)ctx;
    if (b->n < W_N) {
        b->idx[b->n] = idx;
        b->blk[b->n] = block;
        b->n++;
    }
}

typedef void (*PlsEmitFn)(int idx, u16 block, void *ctx);

MC_HD static inline void pls_run_mushroom_scene(i64 seed, PlsEmitFn emit, void *ctx,
                                                McSinTable *st, u16 *blocks_a, u16 *blocks_b,
                                                u8 *sky, u8 *blk, u8 *tmp_sky, u8 *tmp_blk,
                                                CpScratch *sc, ChunkPrimer *primer,
                                                FoliageCoord *fol, JavaRandom *r) {
    World wa, wb;
    BDCfg cfg;
    int biome;
    int i;

    wa.st = st;
    wa.blocks = blocks_a;
    wb.st = st;
    wb.blocks = blocks_b;
    w_reset_loaded_chunks(&wa, seed, 0, 0);
    w_reset_loaded_chunks(&wb, seed, 0, 0);

    pls_build_world(&wa, sc, primer, r, fol, seed);
    pls_copy_world_blocks(&wa, &wb);

    biome = w_getBiome(&wa, 16, 16);
    pls_bd_cfg(biome, &cfg);

    {
        u64 shroom_seed = r->seed;

        pls_mushroom_decorate(&wa, r, &cfg, 0, NULL);

        for (i = 0; i < W_N; ++i) {
            sky[i] = 0;
            blk[i] = 0;
        }
        pls_propagate(&wb, sky, blk, tmp_sky, tmp_blk, PLS_MAX_LIGHT_ITERS);

        r->seed = shroom_seed;
        pls_mushroom_decorate(&wb, r, &cfg, 1, sky);
    }

    for (i = 0; i < W_N; ++i) {
        if (wa.blocks[i] != wb.blocks[i])
            emit(i, wb.blocks[i], ctx);
    }
}

#endif /* MC_POPULATE_LIGHT_SHIM_H */
