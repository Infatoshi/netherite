/* overworld_region: generalized overworld_full_live re-compose anchored at an ARBITRARY base
 * chunk (bcx,bcz). Fills the same 2x2-chunk World (local coords [0,32)x[0,256)x[0,32)) as owfl_run,
 * but the base chunk is a PARAMETER threaded into (a) the biome voronoi window, (b) st_run's real
 * chunk coords, and (c) the per-chunk populate RNG seed - exactly as vanilla
 * ChunkProviderOverworld.populate(cx,cz) does `rand.setSeed(cx*k + cz*l ^ worldSeed)` with the REAL
 * chunk coords. At (bcx,bcz)==(0,0) this is BYTE-IDENTICAL to owfl_run (origin regression lock).
 *
 * READ-ONLY re-compose: calls the SAME verified sub-kernels (st_run, pll_wg_lakes, wg_dungeons,
 * pll_biome_decorate, owfl_fluid_pass) in the SAME order as owfl_run/pll_populate. The ONLY deltas
 * vs owfl_run are the three (bcx,bcz)-threaded lines noted below. No edits to any included header. */
#ifndef MC_OVERWORLD_REGION_H
#define MC_OVERWORLD_REGION_H

#include "mc.h"

#include "overworld_full_live.h"
#include "populate_ice_snow.h"
#include "scattered_desert.h"
#include "scattered_jungle.h"
#include "scattered_swamp.h"

typedef struct {
    World *world;
    int min_x, max_x, min_z, max_z;
} OwrScatteredAccess;

#ifndef __CUDA_ARCH__
/* Optional host product hook at ChunkProviderOverworld.populate's exact
 * village.generateStructure position. Kept null in verified CPU/CUDA kernels. */
static void (*owr_village_hook)(World *, JavaRandom *, i64, int, int);
#endif

MC_HD static inline int owr_sd_pb_from_state(u16 state) {
    int id = state >> 4, meta = state & 15;
    if (id == 0) return PB_AIR;
    if (id == 24) return meta == 1 ? PB_SANDSTONE_CHISELED
                          : meta == 2 ? PB_SANDSTONE_SMOOTH : PB_SANDSTONE;
    if (id == 44 && meta == 1) return PB_SANDSTONE_SLAB;
    if (id == 128) return PB_SANDSTONE_STAIRS_E + (meta & 3);
    if (id == 159) return meta == 11 ? PB_STAINED_CLAY_BLUE
                                     : PB_STAINED_CLAY_ORANGE;
    if (id == 70) return PB_STONE_PRESSURE_PLATE;
    if (id == 46) return PB_TNT;
    if (id == 54) return PB_CHEST;
    if (id == 4) return PB_COBBLESTONE;
    if (id == 48) return PB_MOSSY_COBBLESTONE;
    return 0x4000 | state;
}

MC_HD static inline u16 owr_sd_state_from_pb(int block) {
    if (block == PB_AIR) return sd_state(0, 0);
    if (block == PB_WATER) return sd_state(9, 0);
    if (block == PB_FLOWING_WATER) return sd_state(8, 0);
    if (block == PB_LAVA) return sd_state(11, 0);
    if (block == PB_FLOWING_LAVA) return sd_state(10, 0);
    if (block == PB_CHEST) return sd_state(54, 2);
    if (block == PB_SANDSTONE) return sd_state(24, 0);
    if (block == PB_SANDSTONE_SMOOTH) return sd_state(24, 2);
    if (block == PB_SANDSTONE_CHISELED) return sd_state(24, 1);
    if (block >= PB_SANDSTONE_STAIRS_E && block <= PB_SANDSTONE_STAIRS_N)
        return sd_state(128, block - PB_SANDSTONE_STAIRS_E);
    if (block == PB_STAINED_CLAY_ORANGE) return sd_state(159, 1);
    if (block == PB_STAINED_CLAY_BLUE) return sd_state(159, 11);
    if (block == PB_STONE_PRESSURE_PLATE) return sd_state(70, 0);
    if (block == PB_TNT) return sd_state(46, 0);
    if (block == PB_COBBLESTONE) return sd_state(4, 0);
    if (block == PB_MOSSY_COBBLESTONE) return sd_state(48, 0);
    if (block & 0x4000) return (u16)(block & 0x3fff);
    return sd_state(1, 0);
}

MC_HD static inline u16 owr_sd_get(void *opaque, int x, int y, int z) {
    OwrScatteredAccess *a = (OwrScatteredAccess *)opaque;
    int lx = x - a->world->baseCx * 16;
    int lz = z - a->world->baseCz * 16;
    return owr_sd_state_from_pb(w_get(a->world, lx, y, lz));
}

MC_HD static inline void owr_sd_set(void *opaque, int x, int y, int z, u16 state) {
    OwrScatteredAccess *a = (OwrScatteredAccess *)opaque;
    int lx = x - a->world->baseCx * 16;
    int lz = z - a->world->baseCz * 16;
    w_set(a->world, lx, y, lz, owr_sd_pb_from_state(state));
}

MC_HD static inline int owr_sd_contains(void *opaque, int x, int y, int z) {
    OwrScatteredAccess *a = (OwrScatteredAccess *)opaque;
    return y >= 0 && y < W_Y && x >= a->min_x && x <= a->max_x
        && z >= a->min_z && z <= a->max_z;
}

/* MapGenScatteredFeature spacing check, including Java's negative-coordinate
 * pre-adjustment before truncating division. */
MC_HD static inline int owr_sd_is_candidate(i64 seed, int cx, int cz) {
    int ax = cx < 0 ? cx - 31 : cx;
    int az = cz < 0 ? cz - 31 : cz;
    int rx = ax / 32, rz = az / 32;
    JavaRandom random;
    u64 mixed = (u64)seed + (u64)(i64)rx * 341873128712ULL
                          + (u64)(i64)rz * 132897987541ULL + 14357617ULL;
    jrand_set(&random, (i64)mixed);
    return cx == rx * 32 + jrand_int_bound(&random, 24)
        && cz == rz * 32 + jrand_int_bound(&random, 24);
}

MC_HD static inline int owr_sd_facing(i64 seed, int cx, int cz) {
    static const int facings[4] = { SD_NORTH, SD_EAST, SD_SOUTH, SD_WEST };
    JavaRandom random;
    i64 xmul, zmul;
    jrand_set(&random, seed);
    xmul = jrand_long(&random);
    zmul = jrand_long(&random);
    jrand_set(&random, (i64)((u64)(i64)cx * (u64)xmul
                           ^ (u64)(i64)cz * (u64)zmul ^ (u64)seed));
    (void)jrand_next(&random, 32); /* MapGenStructure.recursiveGenerate */
    return facings[jrand_int_bound(&random, 4)];
}

MC_HD static inline int owr_sd_current_hpos(
        const World *w, int start_cx, int start_cz, int bcx, int bcz,
        int width, int depth) {
    int sx0 = start_cx * 16, sz0 = start_cz * 16;
    int x0 = bcx * 16 + 8, z0 = bcz * 16 + 8;
    int x1 = x0 + 15, z1 = z0 + 15;
    i64 sum = 0;
    int count = 0;
    if (x0 < sx0) x0 = sx0;
    if (z0 < sz0) z0 = sz0;
    if (x1 > sx0 + width - 1) x1 = sx0 + width - 1;
    if (z1 > sz0 + depth - 1) z1 = sz0 + depth - 1;
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z) {
            int top = w_topSolidOrLiquid(w, x - bcx * 16, z - bcz * 16);
            sum += top < 64 ? 64 : top;
            ++count;
        }
    return count ? (int)(sum / count) : 64;
}

MC_HD static inline void owr_scattered_feature(
        World *w, JavaRandom *population_random, i64 seed, int bcx, int bcz) {
    for (int scx = bcx; scx <= bcx + 1; ++scx)
        for (int scz = bcz; scz <= bcz + 1; ++scz) {
            int bx = (scx - bcx) * 16 + 8;
            int bz = (scz - bcz) * 16 + 8;
            int biome;
            int hpos;
            int facing, local_x, local_z, width, depth;
            OwrScatteredAccess world_access;
            SdAccess access;
            if (!owr_sd_is_candidate(seed, scx, scz)) continue;
            biome = w_getBiome(w, bx, bz);
            if (biome != B_DESERT && biome != B_DESERT_HILLS
                    && biome != B_JUNGLE && biome != B_JUNGLE_HILLS
                    && biome != B_SWAMP) continue;
            facing = owr_sd_facing(seed, scx, scz);
            if (biome == B_SWAMP) {
                local_x = 7;
                local_z = 9;
            } else if (biome == B_JUNGLE || biome == B_JUNGLE_HILLS) {
                local_x = 12;
                local_z = 15;
            } else {
                local_x = local_z = 21;
            }
            width = (facing == SD_NORTH || facing == SD_SOUTH) ? local_x : local_z;
            depth = (facing == SD_NORTH || facing == SD_SOUTH) ? local_z : local_x;
            hpos = owr_sd_current_hpos(w, scx, scz, bcx, bcz, width, depth);
#ifndef __CUDA_ARCH__
            if (g_w_scattered_hpos)
                hpos = g_w_scattered_hpos(seed, scx, scz, hpos);
#endif
            world_access.world = w;
            world_access.min_x = bcx * 16 + 8;
            world_access.max_x = world_access.min_x + 15;
            world_access.min_z = bcz * 16 + 8;
            world_access.max_z = world_access.min_z + 15;
            access.ctx = &world_access;
            access.get = owr_sd_get;
            access.set = owr_sd_set;
            access.contains = owr_sd_contains;
            if (biome == B_SWAMP) {
                SsSwampHut hut;
                memset(&hut, 0, sizeof hut);
                hut.base.origin_x = scx * 16;
                hut.base.origin_z = scz * 16;
                hut.base.base_y = hpos;
                hut.base.facing = facing;
                hut.base.size_z = local_z;
                ss_swamp_generate(&access, &hut);
#ifndef __CUDA_ARCH__
                if (g_w_dungeon_event && hut.pot_placed)
                    g_w_dungeon_event(bcx, bcz,
                        MC_DUNGEON_EVENT_SWAMP_POT,
                        hut.pot_x - bcx * 16, hut.pot_y,
                        hut.pot_z - bcz * 16, 0, 0);
                if (g_w_dungeon_event && hut.witch_placed)
                    g_w_dungeon_event(bcx, bcz,
                        MC_DUNGEON_EVENT_SWAMP_WITCH,
                        hut.witch_x - bcx * 16, hut.witch_y,
                        hut.witch_z - bcz * 16, 0, 0);
#endif
            } else if (biome == B_JUNGLE || biome == B_JUNGLE_HILLS) {
                SjJunglePyramid pyramid;
                memset(&pyramid, 0, sizeof pyramid);
                pyramid.base.origin_x = scx * 16;
                pyramid.base.origin_z = scz * 16;
                pyramid.base.base_y = hpos;
                pyramid.base.facing = facing;
                pyramid.base.size_z = local_z;
                sj_jungle_generate(&access, &pyramid, population_random);
#ifndef __CUDA_ARCH__
                if (g_w_dungeon_event)
                    for (int i = 0; i < pyramid.site_count; ++i) {
                        const SjLootSite *site = &pyramid.sites[i];
                        int kind = site->kind == SJ_SITE_DISPENSER
                            ? MC_DUNGEON_EVENT_JUNGLE_DISPENSER
                            : MC_DUNGEON_EVENT_JUNGLE_CHEST;
                        g_w_dungeon_event(bcx, bcz, kind,
                            site->x - bcx * 16, site->y,
                            site->z - bcz * 16, site->loot_seed, site->facing);
                    }
#endif
            } else {
                SdDesertPyramid pyramid;
                memset(&pyramid, 0, sizeof pyramid);
                pyramid.origin_x = scx * 16;
                pyramid.origin_z = scz * 16;
                pyramid.base_y = hpos;
                pyramid.facing = facing;
                sd_desert_generate(&access, &pyramid, population_random);
#ifndef __CUDA_ARCH__
                if (g_w_dungeon_event)
                    for (int i = 0; i < pyramid.chest_count; ++i) {
                        const SdChest *chest = &pyramid.chests[i];
                        g_w_dungeon_event(bcx, bcz,
                            MC_DUNGEON_EVENT_DESERT_CHEST,
                            chest->x - bcx * 16, chest->y,
                            chest->z - bcz * 16,
                            chest->loot_seed, chest->facing);
                    }
#endif
            }
        }
}

/* pll_populate re-compose with the per-chunk seed anchored at the REAL base chunk (bcx,bcz).
 * Verbatim copy of pll_populate() EXCEPT the setSeed formula uses (bcx,bcz) instead of (0,0). */
MC_HD static inline void owr_populate(World *w, JavaRandom *r, i64 seed, FoliageCoord *fol,
                                      PllLight *lt, CpScratch *sc, int bcx, int bcz) {
    int biome = w_getBiome(w, 16, 16);   /* local (16,16) == chunk (bcx,bcz) center block */
    w_reset_loaded_chunks(w, seed, bcx, bcz);
    w_pop_sky_seed2(w, lt->pop_sky_height, lt->pop_sky_stale);
    w->activeRand = NULL;  /* cascade clobber hook DISARMED: trigger needs java global load order, see DEVLOG */
#ifndef __CUDA_ARCH__
    /* flywheel debug: the decoration biome this window will use (differ skips BIO lines) */
    if (mc_probe_fn) mc_probe_fn(bcx, bcz, "BIO", "-", (unsigned long long)biome);
#endif
    jrand_set(r, seed);
    {
        i64 k = jrand_long(r) / 2 * 2 + 1;
        i64 l = jrand_long(r) / 2 * 2 + 1;
        jrand_set(r, (i64)bcx * k + (i64)bcz * l ^ seed);   /* REAL chunk coords (vs 0*k+0*l) */
    }
    {
        MSWorld mw;
        memset(&mw,0,sizeof(mw));
        mw.worldSeed=seed; mw.seaLevel=POP_SEA_LEVEL; mw.storeMeta=2;
        mw.window=w->blocks; mw.windowBaseX=bcx*16; mw.windowBaseZ=bcz*16;
        mw.windowWidth=W_X;
        ms_generate_population_window(&mw,(MSGen *)sc->stgen,r,seed,bcx,bcz);
    }
#ifndef __CUDA_ARCH__
    if (owr_village_hook) owr_village_hook(w, r, seed, bcx, bcz);
#endif
    owr_scattered_feature(w, r, seed, bcx, bcz);
    MC_PROBE("PRE", "-", r);
    if (biome != 2 && biome != 17 && jrand_int_bound(r, 4) == 0) {
        MC_PROBE("POP", "LAKE", r);
        int i1 = jrand_int_bound(r, 16) + 8;
        int j1 = jrand_int_bound(r, 256);
        int k1 = jrand_int_bound(r, 16) + 8;
        pll_wg_lakes(w, r, i1, j1, k1, PB_WATER, lt);
    }
    if (jrand_int_bound(r, 8) == 0) {
        MC_PROBE("POP", "LAVA", r);
        int i2 = jrand_int_bound(r, 16) + 8;
        int inner = jrand_int_bound(r, 248) + 8;
        int l2 = jrand_int_bound(r, inner);
        int k3 = jrand_int_bound(r, 16) + 8;
        if (l2 < POP_SEA_LEVEL || jrand_int_bound(r, 10) == 0)
            pll_wg_lakes(w, r, i2, l2, k3, PB_LAVA, lt);
    }
    MC_PROBE("POP", "DUNGEON", r);
    {
        int j2;
        for (j2 = 0; j2 < 8; ++j2) {
            int i3 = jrand_int_bound(r, 16) + 8;
            int l3 = jrand_int_bound(r, 256);
            int l1 = jrand_int_bound(r, 16) + 8;
            wg_dungeons(w, r, i3, l3, l1);
        }
    }
    pll_biome_decorate2(w, r, biome, fol, lt, bcx * 16, bcz * 16, seed);
    w->activeRand = NULL;
    /* vanilla populate tail (after decorate + animals): per-column freeze/snow at
     * blockpos.add(8,0,8) - consumes NO rand, so it is checkpoint-neutral. */
    pis_ice_snow_pass(w, lt->blk, bcx * 16, bcz * 16);
}

/* owfl_run re-compose parametrized by base chunk (bcx,bcz). Deltas vs owfl_run:
 *   1. biome voronoi window sampled at world offset (bcx*16, bcz*16).
 *   2. st_run generates chunk (bcx+cx, bcz+cz) with REAL coords.
 *   3. owr_populate seeds the per-chunk RNG from the REAL (bcx,bcz). */
MC_HD static inline void owr_run(World *w, CpScratch *sc, ChunkPrimer *primer, JavaRandom *r,
                                 FoliageCoord *fol, i64 seed, u8 *sky, u8 *blk,
                                 u8 *tmp_sky, u8 *tmp_blk, u16 *mc_cur, u16 *mc_tmp,
                                 u16 *before_ca, int bcx, int bcz) {
    u16 pop_sky_height[W_X * W_Z];
    PllLight lt = { sky, blk, tmp_sky, tmp_blk, pop_sky_height };
    int i;

    for (i = 0; i < W_N; ++i) {
        w->blocks[i] = (u16)PB_AIR;
        sky[i] = 0;
        blk[i] = 0;
    }
    w->bigtree_heightLimit = 0;
    w_reset_loaded_chunks(w, seed, bcx, bcz);

    {
        GLNode nodes[GL_MAX_NODES];
        int voronoi;
        gl_build(nodes, seed, &voronoi);
        {
            sc->arena.off = 0;   /* reset bump arena at top-level tree */
            int *fb = gl_getInts(nodes, &sc->arena, voronoi, bcx * 16, bcz * 16, W_X, W_Z);  /* delta 1 */
            int x, z;
            for (x = 0; x < W_X; ++x)
                for (z = 0; z < W_Z; ++z)
                    w->fullBiome[x * W_Z + z] = fb[x + z * W_X];  /* getInts = fb[dx + dz*width] */
        }
    }

    {
        int cx, cz;
        for (cx = 0; cx < 2; ++cx) {
            for (cz = 0; cz < 2; ++cz) {
                st_run_features(primer, sc, w->st, seed, bcx + cx, bcz + cz, ST_MAP_FEATURES);  /* delta 2 */
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

    owr_populate(w, r, seed, fol, &lt, sc, bcx, bcz);   /* delta 3 */
    owfl_fluid_pass(w, seed, mc_cur, mc_tmp, before_ca);
}

#endif /* MC_OVERWORLD_REGION_H */
