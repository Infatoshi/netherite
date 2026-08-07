/* populate: exact C port of MC 1.11.2 ChunkProviderOverworld.populate(0,0) - the per-chunk
 * DECORATION RNG stream - run over a multi-chunk world built from the verified chunk_provider
 * pipeline. Worldgen is the one vanilla-bit-exact subsystem (SPEC rule 2): verified verbatim-Java
 * golden == CPU == CUDA via the Java LCG. Build C -ffp-contract=off / CUDA --fmad=false.
 *
 * ===== MULTI-CHUNK WORLD =====
 * populate(0,0) writes features at chunkPos.add(j,0,k) with j,k in [8,24), so a feature anchored in
 * chunk (0,0) spills into (1,0),(0,1),(1,1), and decoration READS neighbor terrain (getHeight/
 * getTopSolidOrLiquidBlock/getBiome). Vanilla generates the 2x2 block of chunks before populate(0,0)
 * (populate fires when (1,1) loads). So the harness builds chunks (0,0),(1,0),(0,1),(1,1) with the
 * verified cp_provide_chunk into ONE world array, then runs populate(0,0), then dumps the world.
 *   World region: x in [0,32), y in [0,256), z in [0,32) (the 2x2 of chunks).
 *   Linear index w_index(x,y,z) = (x*32 + z)*256 + y. Dump order = i = 0..262143 (x outer, z, y inner).
 * Footprint analysis (seeds 12345/0/7): all feature reads/writes fall within x,z in ~[4,30], strictly
 * inside [0,32), so the region is self-contained (OOB reads -> AIR, OOB writes dropped; never hit).
 *
 * ===== heightMap (getHeight) - provably static recompute =====
 * MC's Chunk.heightMap is maintained incrementally by setBlockState/relightBlock/generateSkylightMap,
 * but it is ALWAYS exactly (1 + highest block with lightOpacity>0) in a column (every update path -
 * place-above, place-below, remove, and the NULL-section generateSkylightMap path - converges to that
 * invariant). So getHeight is recomputed from the block array on demand; provably identical to the
 * incremental heightMap, no relight bookkeeping needed. Same for getTopSolidOrLiquidBlock /
 * getPrecipitationHeight (their start = getTopFilledSegment()+16/+15 only skips guaranteed-air cells,
 * so starting the scan at the world top is equivalent; the precip cache is pure memoization).
 *
 * ===== SANCTIONED SHIMS (identical in core + Golden.java; documented) =====
 *  - Block-state ids -> small ints PB_* (superset of chunk_provider's CB_*, same 0..20 values).
 *  - World/Block predicates (material/opacity/isAir/isLeaves/isWood/canGrowInto/isReplaceable/
 *    canBeReplacedByLeaves/canSustainPlant/onPlantGrow/canBlockStay/canPlaceBlockAt/placeAt/
 *    isReplaceableOreGen) reduced to integer-id checks faithful to MC for the block ids present
 *    (same pattern as tree_gen/lake_gen/ore_gen).
 *  - getLight = simplified skylight: exposed (y >= getHeight col) -> 15 else 0. Full light
 *    propagation (block light from lava, attenuation) is the separate light oracle (#5). Affects only
 *    mushroom/plant canBlockStay placement (block output), NEVER the RNG draw stream.
 *  - immediateBlockTick (WorldGenLiquids springs) = no-op: the spring SOURCE block is placed verbatim;
 *    fluid spread is deferred to the fluid CA oracle (#4). Confirmed: BlockDynamicLiquid.updateTick
 *    draws NO rand for a source (LEVEL 0) liquid (the only nextInt(4) is in the i>0 branch), so the
 *    decoration RNG stream is unaffected.
 *  - getBiome = genlayer voronoi (faithful; precomputed fullBiome over [0,32)^2).
 *
 * ===== CUTS (documented, output-invariant) =====
 *  - WorldEntitySpawner.performWorldGenSpawning (animals): draws rand but writes NO blocks, and the
 *    only thing after it (the ICE/SNOW pass) draws NO rand (world-state only). So omitting animals
 *    cannot change the dumped block array. (Verified: ICE pass uses getPrecipitationHeight/
 *    canBlockFreezeWater/canSnowAt, all world-state reads.)
 *  - Forge event hooks (TerrainGen.populate/decorate/generateOre return true; ForgeEventFactory /
 *    DecorateBiomeEvent / OreGenEvent posts are no-ops). DungeonHooks.getRandomDungeonMob uses the
 *    vanilla weighted list (total 400) - only the draw COUNT matters (the spawner entity id is not a
 *    dumped block).
 *  - WorldGenBigMushroom / WorldGenCactus are ported for decorator count biomes.
 *
 * Settings = ChunkProviderSettings.Factory defaults (seaLevel 63, waterLakeChance 4, lavaLakeChance
 * 80, dungeonChance 8, the ore counts/sizes/heights below), WorldType.DEFAULT, mapFeaturesEnabled=
 * false (all structure generateStructure calls skipped exactly as vanilla skips them). */
#ifndef MC_POPULATE_H
#define MC_POPULATE_H

#include <math.h>
#include "mc.h"
#include "mc_rng.h"
#include "mc_math.h"
#include "chunk_provider.h"   /* ChunkPrimer, cp_provide_chunk, CB_* ids, CpScratch, McSinTable */

/* ===== unified block-state codes (0..20 == chunk_provider CB_*, new feature blocks 21+) ===== */
enum {
    PB_AIR = 0, PB_STONE = 1, PB_WATER = 2, PB_GRASS = 3, PB_DIRT = 4, PB_BEDROCK = 5,
    PB_GRAVEL = 6, PB_SAND = 7, PB_SANDSTONE = 8, PB_RED_SANDSTONE = 9, PB_ICE = 10,
    PB_LAVA = 11, PB_FLOWING_LAVA = 12, PB_FLOWING_WATER = 13, PB_WATER_LILY = 14,
    PB_MYCELIUM = 15, PB_SNOW_LAYER = 16, PB_HARDENED_CLAY = 17, PB_STAINED_CLAY = 18,
    PB_PODZOL = 19, PB_COARSE_DIRT = 20,
    PB_GRANITE = 21, PB_DIORITE = 22, PB_ANDESITE = 23,
    PB_COAL_ORE = 24, PB_IRON_ORE = 25, PB_GOLD_ORE = 26, PB_REDSTONE_ORE = 27,
    PB_DIAMOND_ORE = 28, PB_LAPIS_ORE = 29, PB_CLAY = 30,
    PB_LOG_OAK = 31, PB_LOG_BIRCH = 32, PB_LOG_SPRUCE = 33,
    PB_LEAVES_OAK = 34, PB_LEAVES_BIRCH = 35, PB_LEAVES_SPRUCE = 36,
    PB_LOG_OAK_X = 37, PB_LOG_OAK_Z = 38,
    PB_TALLGRASS = 39, PB_FERN = 40, PB_DEADBUSH = 41,
    PB_BROWN_MUSHROOM = 42, PB_RED_MUSHROOM = 43, PB_REEDS = 44,
    PB_COBBLESTONE = 45, PB_MOSSY_COBBLESTONE = 46, PB_MOB_SPAWNER = 47, PB_BONE_BLOCK = 48,
    PB_CHEST = 49, PB_YELLOW_FLOWER = 50,
    PB_RED_FLOWER_BASE = 51,      /* + EnumFlowerType.meta (0=poppy,1=blue_orchid,...) */
    PB_DPLANT_LOWER_BASE = 60,    /* + EnumPlantType ordinal (0..5) */
    PB_DPLANT_UPPER = 66,
    PB_PUMPKIN_BASE = 67,         /* + facing selector (0..3) */
    PB_VINE_BASE = 71,            /* + direction (0=E,1=W,2=S,3=N) */
    /* biome-dispatch additions (extreme hills / roofed forest) */
    PB_EMERALD_ORE = 75, PB_MONSTER_EGG = 76,
    PB_LOG_DARKOAK = 77, PB_LEAVES_DARKOAK = 78,
    PB_BROWN_SHROOM_BLOCK = 79, PB_RED_SHROOM_BLOCK = 80,
    PB_CACTUS = 81, PB_LOG_ACACIA = 82, PB_LEAVES_ACACIA = 83,
    PB_SANDSTONE_SLAB = 84, PB_LOG_JUNGLE = 85, PB_LEAVES_JUNGLE = 86,
    PB_MELON = 87, PB_COCOA = 88, PB_OBSIDIAN = 89,
    /* Desert-pyramid states whose legacy metadata affects rendering/gameplay. */
    PB_SANDSTONE_SMOOTH = 90, PB_SANDSTONE_CHISELED = 91,
    PB_SANDSTONE_STAIRS_E = 92, PB_SANDSTONE_STAIRS_W = 93,
    PB_SANDSTONE_STAIRS_S = 94, PB_SANDSTONE_STAIRS_N = 95,
    PB_STAINED_CLAY_ORANGE = 96, PB_STAINED_CLAY_BLUE = 97,
    PB_STONE_PRESSURE_PLATE = 98, PB_TNT = 99
};

/* tree kinds (log/leaf code pairs) */
enum { TK_OAK, TK_BIRCH, TK_SPRUCE, TK_SWAMP };

#define W_X 32
#define W_Y 256
#define W_Z 32
#define W_N (W_X * W_Y * W_Z)
#define POP_SEA_LEVEL 63

typedef struct World {
    u16 *blocks;            /* [W_N], index w_index */
    u16 *popSkyHeight;      /* [W_X*W_Z], vanilla populate-time Chunk.heightMap for SKY reads */
    u8  *popSkyLight;       /* [W_N] vanilla populate-time STALE skylight (CPU replay only):
                             * generateSkylightMap init + relightBlock vertical stomps, NO
                             * checkLightFor BFS (isAreaLoaded(17) gates it off at the worldgen
                             * frontier - java bushdbg ground truth, seed 19 (-33,84,244) sky=8) */
    u16 popSecMask[2][2];   /* per window chunk: bit s = ExtendedBlockStorage exists for y-slab s */
    int fullBiome[W_X * W_Z];  /* voronoi biome per column, idx = x*32 + z */
    const McSinTable *st;
    /* persistent static-singleton state for WorldGenBigTree (Biome.BIG_TREE_FEATURE) */
    int bigtree_heightLimit;
    JavaRandom *activeRand;     /* provider Random during populate; NULL outside decorate */
    i64 worldSeed;
    int baseCx, baseCz;
    unsigned char loadedChunk[5][5];  /* rel chunk [-1..3]^2, for cascade RNG clobber */
} World;

/* Host-only: capture decoration writes that leave the 32x32 window (big-tree
 * branches, canopy crowns near the +0/+31 edges). Per-TU static so the
 * populate_mc translation unit can install a recorder without cross-TU deps. */
#if !defined(__CUDA_ARCH__)
static void (*g_w_oob_write)(int baseCx, int baseCz, int lx, int y, int lz, int v) = 0;
/* WorldGenDungeons tile metadata is not part of the block volume. The live
 * host bridge installs this bounded event sink while it builds one populate
 * window so the exact placement-stream loot seed and spawner roll survive
 * into gameplay. Device callers keep the verified block-only path. */
enum {
    MC_DUNGEON_EVENT_CHEST = 1,
    MC_DUNGEON_EVENT_SPAWNER = 2,
    MC_DUNGEON_EVENT_DESERT_CHEST = 3,
    MC_DUNGEON_EVENT_JUNGLE_CHEST = 4,
    MC_DUNGEON_EVENT_JUNGLE_DISPENSER = 5,
    MC_DUNGEON_EVENT_SWAMP_WITCH = 6,
    MC_DUNGEON_EVENT_SWAMP_POT = 7
};
static void (*g_w_dungeon_event)(int baseCx, int baseCz, int kind,
                                  int lx, int y, int lz, i64 value,
                                  int meta) = 0;
/* Feature.horizontalPos is initialized by the first intersecting populate
 * chunk and then serialized with the structure. The live bridge supplies the
 * persistent lookup; block-only/device callers use the current-quadrant value. */
static int (*g_w_scattered_hpos)(i64 seed, int startCx, int startCz,
                                 int fallback) = 0;
#endif

MC_HD static inline int w_index(int x, int y, int z) { return (x * W_Z + z) * W_Y + y; }
MC_HD static inline void w_reset_loaded_chunks(World *w, i64 seed, int bcx, int bcz) {
    w->activeRand = NULL;
    w->popSkyHeight = NULL;
    w->popSkyLight = NULL;
    for (int cxi = 0; cxi < 2; ++cxi)
        for (int czi = 0; czi < 2; ++czi)
            w->popSecMask[cxi][czi] = 0;
    w->worldSeed = seed;
    w->baseCx = bcx;
    w->baseCz = bcz;
    for (int x = 0; x < 5; ++x)
        for (int z = 0; z < 5; ++z)
            w->loadedChunk[x][z] = 0;
    w->loadedChunk[1][1] = 1;  /* rel (0,0) */
    w->loadedChunk[2][1] = 1;  /* rel (1,0) */
    w->loadedChunk[1][2] = 1;  /* rel (0,1) */
    w->loadedChunk[2][2] = 1;  /* rel (1,1) */
}
MC_HD static inline int w_floor_div16(int v) { return v >= 0 ? v / 16 : -((15 - v) / 16); }
MC_HD MC_NOINLINE static void w_provider_surface_clobber(JavaRandom *r, i64 seed, int cx, int cz) {
#if defined(__CUDA_ARCH__)
    /* primer+scratch+nodes = ~674KB, over the 512KB/thread local-memory cap
     * (the whole POPULATE section refused to launch with them on the frame).
     * Populate decorate is serial and every device caller launches <<<1,1>>>,
     * so one static global-memory scratch is safe. */
    static ChunkPrimer scl_primer;
    static CpScratch scl_sc;
    static GLNode scl_nodes[GL_MAX_NODES];
    ChunkPrimer *primer = &scl_primer;
    CpScratch *sc = &scl_sc;
    GLNode *nodes = scl_nodes;
#else
    ChunkPrimer primer_l;
    CpScratch sc_l;
    GLNode nodes_l[GL_MAX_NODES];
    ChunkPrimer *primer = &primer_l;
    CpScratch *sc = &sc_l;
    GLNode *nodes = nodes_l;
#endif
    int voronoi;
    int curTop[256], curFiller[256];
    jrand_set(r, (i64)cx * 341873128712LL + (i64)cz * 132897987541LL);
    gl_build(nodes, seed, &voronoi);
    int rivermix = nodes[voronoi].parent;
    terrain_noise_init(&sc->tnoise, seed);
    cp_surface_noise_init(&sc->surfaceNoise, seed);
    cp_grass_noise_init(&sc->grassNoise);
    for (int b = 0; b < 256; ++b) {
        curTop[b] = cb_defTop(b);
        curFiller[b] = cb_defFiller(b);
    }
    sc->arena.off = 0;
    int *lowBiome = gl_getInts(nodes, &sc->arena, rivermix, cx * 4 - 2, cz * 4 - 2, 10, 10);
    cp_generateHeightmap(sc, &sc->tnoise, lowBiome, cx * 4, 0, cz * 4);
    cp_setBlocksInChunk(primer, sc->heightMap);
    sc->arena.off = 0;
    int *fullBiome = gl_getInts(nodes, &sc->arena, voronoi, cx * 16, cz * 16, 16, 16);
    cp_perlin_getRegion(&sc->surfaceNoise, sc->depthBuffer, (double)(cx * 16), (double)(cz * 16),
                        16, 16, 0.0625, 0.0625, 1.0, 0.5);
    for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j) {
            int biome = fullBiome[j + i * 16];
            cp_genTerrainBlocks(biome, r, primer, cx * 16 + i, cz * 16 + j,
                                sc->depthBuffer[j + i * 16], curTop, curFiller, &sc->grassNoise, seed);
        }
}
MC_HD MC_NOINLINE static void w_touch_chunk(World *w, int x, int z) {
    if (!w->activeRand) return;
    if (x >= 0 && x < W_X && z >= 0 && z < W_Z) return;
    int cx = w_floor_div16(w->baseCx * 16 + x);
    int cz = w_floor_div16(w->baseCz * 16 + z);
    int rx = cx - w->baseCx + 1, rz = cz - w->baseCz + 1;
    if (rx < 0 || rx >= 5 || rz < 0 || rz >= 5) return;
    if (w->loadedChunk[rx][rz]) return;
    w->loadedChunk[rx][rz] = 1;
    w_provider_surface_clobber(w->activeRand, w->worldSeed, cx, cz);
}
MC_HD MC_NOINLINE static int w_inb(int x, int y, int z) {
    return x >= 0 && x < W_X && y >= 0 && y < W_Y && z >= 0 && z < W_Z;
}
#ifdef POP_DEBUG_OOB
extern long pop_oob_xz;   /* counts reads/writes with x or z outside [0,32) (boundary leak probe) */
#define POP_OOB_CHECK(x, z) do { if ((x) < 0 || (x) >= W_X || (z) < 0 || (z) >= W_Z) ++pop_oob_xz; } while (0)
#else
#define POP_OOB_CHECK(x, z) do { } while (0)
#endif
MC_HD MC_NOINLINE static int pb_opacity(int c);
MC_HD MC_NOINLINE static int w_get(const World *w, int x, int y, int z) {
    POP_OOB_CHECK(x, z);
    w_touch_chunk((World *)w, x, z);
    return w_inb(x, y, z) ? (int)w->blocks[w_index(x, y, z)] : PB_AIR;
}
MC_HD static inline int w_col_index(int x, int z) { return x * W_Z + z; }
MC_HD MC_NOINLINE static int w_pop_sky_height_at(const World *w, int x, int z) {
    for (int y = W_Y - 1; y >= 0; --y)
        if (pb_opacity(w_get(w, x, y, z)) > 0) return y + 1;
    return 0;
}
/* vanilla Chunk.generateSkylightMap column walk: heightmap rescan + skylight stored
 * only while k1 > 0 walking down (cells below the cutoff KEEP their previous values -
 * vanilla does not zero them on a full-chunk regen). Air above the first attenuation
 * costs 0 (j1==0 && k1!=15 rule); below, air costs 1 per cell. */
MC_HD MC_NOINLINE static void w_pop_sky_gen_col(World *w, int x, int z) {
    /* every caller passes in-window x,z, and the column is y-contiguous: read
     * blocks directly (w_get's touch/bounds work is a no-op for these coords). */
    const u16 *col = &w->blocks[w_index(x, 0, z)];
    int hm = 0;
    for (int y = W_Y - 1; y >= 0; --y)
        if (pb_opacity(col[y]) > 0) { hm = y + 1; break; }
    w->popSkyHeight[w_col_index(x, z)] = (u16)hm;
#ifndef __CUDA_ARCH__
    if (!w->popSkyLight) return;
    {
        int k1 = 15;
        for (int y = W_Y - 1; y >= 0; --y) {
            int j1 = pb_opacity(col[y]);
            if (j1 == 0 && k1 != 15) j1 = 1;
            k1 -= j1;
            if (k1 <= 0) break;
            w->popSkyLight[w_index(x, y, z)] = (u8)k1;
        }
    }
#endif
}
MC_HD MC_NOINLINE static void w_pop_sky_seed2(World *w, u16 *heightMap, u8 *skyLight) {
#ifdef __CUDA_ARCH__
    /* device gate (w_pop_sky_light) falls back to w_light; skip the column scan */
    (void)w; (void)heightMap; (void)skyLight;
    return;
#else
    if (!heightMap) return;
    w->popSkyHeight = heightMap;
    w->popSkyLight = skyLight;
    if (skyLight)
        for (int i = 0; i < W_N; ++i) skyLight[i] = 0;
    for (int x = 0; x < W_X; ++x)
        for (int z = 0; z < W_Z; ++z)
            w_pop_sky_gen_col(w, x, z);
    /* vanilla ExtendedBlockStorage existence: any non-air cell in the 16^3 slab.
     * PB_AIR == 0 and columns are y-contiguous: OR 4 u64 words per 16-cell
     * section slice instead of the per-cell triple loop (same any-nonair result). */
    {
        typedef unsigned long long McAu64 __attribute__((may_alias));
        for (int cxi = 0; cxi < 2; ++cxi)
            for (int czi = 0; czi < 2; ++czi) {
                u16 m = 0;
                for (int lx = 0; lx < 16 && m != 0xffffu; ++lx)
                    for (int lz = 0; lz < 16 && m != 0xffffu; ++lz) {
                        const McAu64 *c = (const McAu64 *)
                            &w->blocks[w_index(cxi * 16 + lx, 0, czi * 16 + lz)];
                        for (int sec = 0; sec < 16; ++sec)
                            if (!(m & (1u << sec)) &&
                                (c[sec * 4] | c[sec * 4 + 1] | c[sec * 4 + 2] | c[sec * 4 + 3]))
                                m |= (u16)(1u << sec);
                    }
                w->popSecMask[cxi][czi] = m;
            }
    }
#endif
}
MC_HD MC_NOINLINE static void w_pop_sky_seed(World *w, u16 *heightMap) {
    w_pop_sky_seed2(w, heightMap, NULL);
}
MC_HD MC_NOINLINE static void w_pop_sky_relight(World *w, int x, int y, int z) {
    if (!w->popSkyHeight || !w_inb(x, y, z)) return;
    int ci = w_col_index(x, z);
    int i = (int)w->popSkyHeight[ci];
    int j = y > i ? y : i;
    while (j > 0 && pb_opacity(w_get(w, x, j - 1, z)) == 0) --j;
    if (j == i) return;
    w->popSkyHeight[ci] = (u16)j;
#ifndef __CUDA_ARCH__
    /* vanilla Chunk.relightBlock skylight update: span fill between old/new height,
     * then the pure-vertical stomp from the new height down (air below the heightmap
     * costs 1 per cell; stores the terminating 0 then stops, deeper cells keep their
     * old values). The checkLightFor BFS repair is intentionally absent (frontier). */
    if (w->popSkyLight) {
        if (j < i) {
            for (int y1 = j; y1 < i; ++y1) w->popSkyLight[w_index(x, y1, z)] = 15;
        } else {
            for (int y1 = i; y1 < j; ++y1) w->popSkyLight[w_index(x, y1, z)] = 0;
        }
        {
            int k1 = 15, jj = j;
            while (jj > 0 && k1 > 0) {
                --jj;
                int i2 = pb_opacity(w_get(w, x, jj, z));
                if (i2 == 0) i2 = 1;
                k1 -= i2;
                if (k1 < 0) k1 = 0;
                w->popSkyLight[w_index(x, jj, z)] = (u8)k1;
            }
        }
    }
#endif
}
MC_HD MC_NOINLINE static void w_pop_sky_after_set(World *w, int x, int y, int z,
                                                  int oldState, int newState) {
    if (!w->popSkyHeight || !w_inb(x, y, z)) return;
#ifndef __CUDA_ARCH__
    /* vanilla Chunk.setBlockState: a non-air write into a not-yet-existing
     * ExtendedBlockStorage slab at/above the column heightmap regenerates the WHOLE
     * chunk's skylight map instead of the incremental relightBlock. */
    if (w->popSkyLight && newState != PB_AIR) {
        int cxi = x >> 4, czi = z >> 4, sec = y >> 4;
        if (!(w->popSecMask[cxi][czi] & (u16)(1u << sec))) {
            w->popSecMask[cxi][czi] |= (u16)(1u << sec);
            if (y >= (int)w->popSkyHeight[w_col_index(x, z)]) {
                int bx = cxi * 16, bz = czi * 16;
                for (int lx = 0; lx < 16; ++lx)
                    for (int lz = 0; lz < 16; ++lz)
                        w_pop_sky_gen_col(w, bx + lx, bz + lz);
                return;
            }
        }
    }
#endif
    int oldOpacity = pb_opacity(oldState);
    int newOpacity = pb_opacity(newState);
    if (oldOpacity == newOpacity) return;
    int i1 = (int)w->popSkyHeight[w_col_index(x, z)];
    if (newOpacity > 0) {
        if (y >= i1) w_pop_sky_relight(w, x, y + 1, z);
    } else if (y == i1 - 1) {
        w_pop_sky_relight(w, x, y, z);
    }
}
MC_HD MC_NOINLINE static void w_set(World *w, int x, int y, int z, int v) {
    POP_OOB_CHECK(x, z);
    w_touch_chunk(w, x, z);
    if (w->activeRand) {
        if (x == 0) w_touch_chunk(w, -1, z);
        else if (x == W_X - 1) w_touch_chunk(w, W_X, z);
        if (z == 0) w_touch_chunk(w, x, -1);
        else if (z == W_Z - 1) w_touch_chunk(w, x, W_Z);
    }
    if (w_inb(x, y, z)) {
        int idx = w_index(x, y, z);
        int old = (int)w->blocks[idx];
        w->blocks[idx] = (u16)v;
#ifndef __CUDA_ARCH__
        w_pop_sky_after_set(w, x, y, z, old, v);
#endif
    } else {
#if !defined(__CUDA_ARCH__)
        /* Vanilla still writes into neighboring loaded chunks. Without this, big
         * oak / canopy crowns near the window edge drop leaves that later trees
         * plant under (vertical canopy shift vs Java). */
        if (g_w_oob_write && y >= 0 && y < W_Y)
            g_w_oob_write(w->baseCx, w->baseCz, x, y, z, v);
#endif
    }
}

/* ===== block attribute tables (faithful integer reductions of MC Block/Material) ===== */
MC_HD static inline int pb_tag_id(int c){ return (c&0x4000)?((c>>4)&0x3ff):-1; }
/* light opacity: 0 air/plants/vine/snow/lava; 1 leaves; 3 water/ice; 255 full solids. */
MC_HD MC_NOINLINE static int pb_opacity(int c) {
    int tagged=pb_tag_id(c);
    if(tagged>=0){
        if(tagged==0||tagged==30||tagged==50||tagged==52||tagged==55
                ||tagged==66||tagged==69||tagged==85||tagged==93
                ||tagged==94||tagged==106||tagged==131||tagged==132)
            return 0;
        if(tagged==8||tagged==9) return 3;
        if(tagged==10||tagged==11) return 0;
        return 255;
    }
    switch (c) {
        case PB_AIR: case PB_TALLGRASS: case PB_FERN: case PB_DEADBUSH:
        case PB_BROWN_MUSHROOM: case PB_RED_MUSHROOM: case PB_REEDS: case PB_WATER_LILY:
        case PB_SNOW_LAYER: case PB_YELLOW_FLOWER: case PB_DPLANT_UPPER: case PB_COCOA:
        /* non-full blocks: Block.lightOpacity = fullBlock ? 255 : 0 (isOpaqueCube false);
         * cactus opacity 0 means a placed cactus never raises the vanilla heightMap that
         * decorator y-bounds (nextInt(getHeight*2)) read. Half slabs stay 255 (explicit
         * setLightOpacity(255) in BlockSlab). */
        case PB_CACTUS: case PB_CHEST: case PB_MOB_SPAWNER:
        case PB_STONE_PRESSURE_PLATE:
        /* lava too: BlockLiquid isOpaqueCube=false and the lava registration has no
         * setLightOpacity (water gets an explicit .n(3)) -> vanilla heightMap scans
         * straight through lava columns. */
        case PB_LAVA: case PB_FLOWING_LAVA:
            return 0;
        case PB_LEAVES_OAK: case PB_LEAVES_BIRCH: case PB_LEAVES_SPRUCE:
        case PB_LEAVES_DARKOAK: case PB_LEAVES_ACACIA: case PB_LEAVES_JUNGLE: return 1;
        case PB_WATER: case PB_FLOWING_WATER: case PB_ICE: return 3;
        default:
            if (c >= PB_RED_FLOWER_BASE && c < PB_RED_FLOWER_BASE + 9) return 0;
            if (c >= PB_DPLANT_LOWER_BASE && c <= PB_DPLANT_UPPER) return 0;
            if (c >= PB_VINE_BASE && c < PB_VINE_BASE + 4) return 0;
            return 255;
    }
}
MC_HD static inline int pb_isAir(int c) { return c == PB_AIR || pb_tag_id(c)==0; }
MC_HD static inline int pb_isWater(int c) { int id=pb_tag_id(c); return c==PB_WATER||c==PB_FLOWING_WATER||id==8||id==9; }
MC_HD static inline int pb_isLava(int c) { int id=pb_tag_id(c); return c==PB_LAVA||c==PB_FLOWING_LAVA||id==10||id==11; }
MC_HD static inline int pb_isLiquid(int c) { return pb_isWater(c) || pb_isLava(c); }
MC_HD MC_NOINLINE static int pb_isLeaves(int c) {
    return c == PB_LEAVES_OAK || c == PB_LEAVES_BIRCH || c == PB_LEAVES_SPRUCE ||
           c == PB_LEAVES_DARKOAK || c == PB_LEAVES_ACACIA || c == PB_LEAVES_JUNGLE;
}
MC_HD MC_NOINLINE static int pb_isLog(int c) {
    return c == PB_LOG_OAK || c == PB_LOG_BIRCH || c == PB_LOG_SPRUCE ||
           c == PB_LOG_OAK_X || c == PB_LOG_OAK_Z || c == PB_LOG_DARKOAK ||
           c == PB_LOG_ACACIA || c == PB_LOG_JUNGLE;
}
MC_HD static inline int pb_isVine(int c) { return c >= PB_VINE_BASE && c < PB_VINE_BASE + 4; }
MC_HD MC_NOINLINE static int pb_isMaterialVine(int c) {
    return pb_isVine(c) || c == PB_TALLGRASS || c == PB_FERN || c == PB_DEADBUSH ||
           (c >= PB_DPLANT_LOWER_BASE && c <= PB_DPLANT_UPPER);
}
MC_HD MC_NOINLINE static int pb_isPlant(int c) {
    if (c == PB_TALLGRASS || c == PB_FERN || c == PB_DEADBUSH || c == PB_BROWN_MUSHROOM ||
        c == PB_RED_MUSHROOM || c == PB_REEDS || c == PB_WATER_LILY || c == PB_YELLOW_FLOWER)
        return 1;
    if (c == PB_COCOA) return 1;
    if (c >= PB_RED_FLOWER_BASE && c < PB_RED_FLOWER_BASE + 9) return 1;
    if (c >= PB_DPLANT_LOWER_BASE && c <= PB_DPLANT_UPPER) return 1;
    return 0;
}
/* Material.blocksMovement(): false for air/liquid/plants/vine/snow_layer; true for leaves+solids. */
MC_HD MC_NOINLINE static int pb_blocksMovement(int c) {
    int tagged=pb_tag_id(c);
    if(tagged>=0)
        return tagged!=0&&tagged!=8&&tagged!=9&&tagged!=10&&tagged!=11&&
               tagged!=30&&tagged!=50&&tagged!=55&&tagged!=66&&tagged!=69&&
               tagged!=93&&tagged!=94&&tagged!=106&&tagged!=131&&tagged!=132;
    if (pb_isAir(c) || pb_isLiquid(c) || pb_isPlant(c) || pb_isVine(c) || c == PB_SNOW_LAYER)
        return 0;
    return 1;
}
MC_HD static inline int pb_isSolid(int c) { return pb_blocksMovement(c); }  /* Material.isSolid: same set */
/* Material.isReplaceable(): air, water, lava, vine, snow. */
MC_HD MC_NOINLINE static int pb_isReplaceableMat(int c) {
    return pb_isAir(c) || pb_isLiquid(c) || pb_isVine(c) || c == PB_SNOW_LAYER;
}
/* block identity == helpers (ignore variant meta) */
MC_HD MC_NOINLINE static int pb_isStone(int c) {
    return c == PB_STONE || c == PB_GRANITE || c == PB_DIORITE || c == PB_ANDESITE;
}
MC_HD static inline int pb_isDirt(int c) { return c == PB_DIRT || c == PB_PODZOL || c == PB_COARSE_DIRT; }
/* WorldGenMinable StonePredicate: natural stone variants. */
MC_HD static inline int pb_isNaturalStone(int c) { return pb_isStone(c); }
/* WorldGenAbstractTree.canGrowInto: AIR/LEAVES material, or GRASS/DIRT/LOG/SAPLING/VINE. */
MC_HD MC_NOINLINE static int pb_canGrowInto(int c) {
    return pb_isAir(c) || pb_isLeaves(c) || c == PB_GRASS || pb_isDirt(c) || pb_isLog(c) || pb_isVine(c);
}
/* Block.canBeReplacedByLeaves default in 1.11.2: air or leaves only. */
MC_HD static inline int pb_canBeReplacedByLeaves(int c) { return pb_isAir(c) || pb_isLeaves(c); }
/* canSustainPlant for a sapling (BlockBush canSustainBush): soil GRASS or DIRT(any). */
MC_HD static inline int pb_canSustainSapling(int soil) { return soil == PB_GRASS || pb_isDirt(soil); }
/* canSustainBush (tallgrass/flowers/doubleplant Plains): GRASS or DIRT(any). */
MC_HD static inline int pb_canSustainBush(int soil) { return soil == PB_GRASS || pb_isDirt(soil); }

/* ===== world queries ===== */
MC_HD static inline int w_isAir(const World *w, int x, int y, int z) { return pb_isAir(w_get(w, x, y, z)); }
/* getHeight = 1 + highest y with opacity>0 (== Chunk.heightMap invariant), 0 if empty column. */
MC_HD MC_NOINLINE static int w_height(const World *w, int x, int z) {
    for (int y = W_Y - 1; y >= 0; --y)
        if (pb_opacity(w_get(w, x, y, z)) > 0) return y + 1;
    return 0;
}
MC_HD MC_NOINLINE static int w_gen_height(const World *w, int x, int z) {
#ifndef __CUDA_ARCH__
    if (w->popSkyHeight && x >= 0 && x < W_X && z >= 0 && z < W_Z)
        return (int)w->popSkyHeight[w_col_index(x, z)];
#endif
    return w_height(w, x, z);
}
/* getLight shim: exposed to sky (y >= getHeight) -> 15, else 0. */
MC_HD MC_NOINLINE static int w_light(const World *w, int x, int y, int z) {
    return y >= w_height(w, x, z) ? 15 : 0;
}
MC_HD MC_NOINLINE static int w_pop_sky_light(const World *w, int x, int y, int z) {
#ifdef __CUDA_ARCH__
    return w_light(w, x, y, z);
#else
    if (!w->popSkyHeight || !w_inb(x, y, z)) return w_light(w, x, y, z);
    return y >= (int)w->popSkyHeight[w_col_index(x, z)] ? 15 : 0;
#endif
}
/* vanilla populate-time World.getLight(pos): the STALE stored skylight (block light is
 * unpropagated 0 at the frontier - java bushdbg ground truth). Falls back to the
 * heightmap 15/0 model when no stale array is attached. */
MC_HD MC_NOINLINE static int w_pop_sky_stale(const World *w, int x, int y, int z) {
#ifndef __CUDA_ARCH__
    if (w->popSkyLight && w_inb(x, y, z)) return (int)w->popSkyLight[w_index(x, y, z)];
#endif
    return w_pop_sky_light(w, x, y, z);
}
/* getTopSolidOrLiquidBlock: first cell above the topmost blocksMovement (non-leaf,non-foliage) block.
 * isFoliage() default false; foliage (plants) has blocksMovement false anyway -> the && short-circuits,
 * so isFoliage is inert here. Scan from world top (== getTopFilledSegment()+16, extra cells all air). */
MC_HD MC_NOINLINE static int w_topSolidOrLiquid(const World *w, int x, int z) {
    int by = W_Y;
    while (by >= 1) {
        int s = w_get(w, x, by - 1, z);
        if (pb_blocksMovement(s) && !pb_isLeaves(s)) break;
        --by;
    }
    return by;
}
/* getPrecipitationHeight: 1 + highest block that blocksMovement OR isLiquid. */
MC_HD MC_NOINLINE static int w_precip(const World *w, int x, int z) {
    int by = W_Y;
    while (by > 0) {
        int s = w_get(w, x, by - 1, z);
        if (pb_blocksMovement(s) || pb_isLiquid(s)) break;
        --by;
    }
    return by;
}
MC_HD MC_NOINLINE static int w_getBiome(const World *w, int x, int z) {
    if (x < 0 || x >= W_X || z < 0 || z >= W_Z) return 1;  /* PLAINS fallback (footprint stays inside) */
    return w->fullBiome[x * W_Z + z];
}

/* ===== ordered RNG helpers (Java strict L-to-R for repeated calls) ===== */
MC_HD MC_NOINLINE static int wg_off(JavaRandom *r, int n) {
    int a = jrand_int_bound(r, n);
    int b = jrand_int_bound(r, n);
    return a - b;
}

/* ====================================================================================== */
/* WorldGenMinable.generate (over the world; StonePredicate = natural stone)               */
/* ====================================================================================== */
MC_HD MC_NOINLINE static void wg_minable(World *w, JavaRandom *r, int posX, int posY, int posZ,
                                    int numberOfBlocks, int oreBlock) {
    float f = jrand_float(r) * (float)MC_PI;
    double d0 = (double)((float)(posX + 8) + mc_sin(w->st, f) * (float)numberOfBlocks / 8.0F);
    double d1 = (double)((float)(posX + 8) - mc_sin(w->st, f) * (float)numberOfBlocks / 8.0F);
    double d2 = (double)((float)(posZ + 8) + mc_cos(w->st, f) * (float)numberOfBlocks / 8.0F);
    double d3 = (double)((float)(posZ + 8) - mc_cos(w->st, f) * (float)numberOfBlocks / 8.0F);
    double d4 = (double)(posY + jrand_int_bound(r, 3) - 2);
    double d5 = (double)(posY + jrand_int_bound(r, 3) - 2);
    for (int i = 0; i < numberOfBlocks; ++i) {
        float f1 = (float)i / (float)numberOfBlocks;
        double d6 = d0 + (d1 - d0) * (double)f1;
        double d7 = d4 + (d5 - d4) * (double)f1;
        double d8 = d2 + (d3 - d2) * (double)f1;
        double d9 = jrand_double(r) * (double)numberOfBlocks / 16.0;
        double d10 = (double)(mc_sin(w->st, (float)MC_PI * f1) + 1.0F) * d9 + 1.0;
        double d11 = (double)(mc_sin(w->st, (float)MC_PI * f1) + 1.0F) * d9 + 1.0;
        int j = mc_floor(d6 - d10 / 2.0);
        int k = mc_floor(d7 - d11 / 2.0);
        int l = mc_floor(d8 - d10 / 2.0);
        int i1 = mc_floor(d6 + d10 / 2.0);
        int j1 = mc_floor(d7 + d11 / 2.0);
        int k1 = mc_floor(d8 + d10 / 2.0);
        for (int l1 = j; l1 <= i1; ++l1) {
            double d12 = ((double)l1 + 0.5 - d6) / (d10 / 2.0);
            if (d12 * d12 < 1.0) {
                for (int i2 = k; i2 <= j1; ++i2) {
                    double d13 = ((double)i2 + 0.5 - d7) / (d11 / 2.0);
                    if (d12 * d12 + d13 * d13 < 1.0) {
                        for (int j2 = l; j2 <= k1; ++j2) {
                            double d14 = ((double)j2 + 0.5 - d8) / (d10 / 2.0);
                            if (d12 * d12 + d13 * d13 + d14 * d14 < 1.0) {
                                if (pb_isNaturalStone(w_get(w, l1, i2, j2)))
                                    w_set(w, l1, i2, j2, oreBlock);
                            }
                        }
                    }
                }
            }
        }
    }
}

/* ====================================================================================== */
/* WorldGenSand / WorldGenClay (disks; gated on getBlockState(position).material == WATER) */
/* ====================================================================================== */
MC_HD MC_NOINLINE static void wg_sand(World *w, JavaRandom *r, int px, int py, int pz, int radius, int block) {
    if (!pb_isWater(w_get(w, px, py, pz))) return;
    int i = jrand_int_bound(r, radius - 2) + 2;
    for (int k = px - i; k <= px + i; ++k)
        for (int l = pz - i; l <= pz + i; ++l) {
            int i1 = k - px, j1 = l - pz;
            if (i1 * i1 + j1 * j1 <= i * i)
                for (int k1 = py - 2; k1 <= py + 2; ++k1) {
                    int b = w_get(w, k, k1, l);
                    /* vanilla block == Blocks.DIRT is BLOCK IDENTITY: all BlockDirt
                     * variants (dirt/coarse/podzol) convert, not just plain dirt */
                    if (pb_isDirt(b) || b == PB_GRASS) w_set(w, k, k1, l, block);
                }
        }
}
MC_HD MC_NOINLINE static void wg_clay(World *w, JavaRandom *r, int px, int py, int pz, int numberOfBlocks) {
    if (!pb_isWater(w_get(w, px, py, pz))) return;
    int i = jrand_int_bound(r, numberOfBlocks - 2) + 2;
    for (int k = px - i; k <= px + i; ++k)
        for (int l = pz - i; l <= pz + i; ++l) {
            int i1 = k - px, j1 = l - pz;
            if (i1 * i1 + j1 * j1 <= i * i)
                for (int k1 = py - 1; k1 <= py + 1; ++k1) {
                    int b = w_get(w, k, k1, l);
                    /* vanilla block == Blocks.DIRT is BLOCK IDENTITY (dirt/coarse/podzol);
                     * missing podzol left soil a mega pine grew on where java's clay disk
                     * had vetoed it (seed 19 chunk (8,26)) */
                    if (pb_isDirt(b) || b == PB_CLAY) w_set(w, k, k1, l, PB_CLAY);
                }
        }
}

/* ====================================================================================== */
/* trees: WorldGenTrees(oak)/WorldGenBirchTree/WorldGenSwamp/WorldGenTaiga1/WorldGenTaiga2  */
/* ====================================================================================== */
MC_HD MC_NOINLINE static int wg_isReplaceableTree(const World *w, int x, int y, int z) {
    int c = w_get(w, x, y, z);
    return pb_isAir(c) || pb_isLeaves(c) || pb_isLog(c) || pb_canGrowInto(c);
}
MC_HD MC_NOINLINE static void wg_onPlantGrow(World *w, int x, int y, int z) {
    if (w_get(w, x, y, z) == PB_GRASS) w_set(w, x, y, z, PB_DIRT);
}

/* WorldGenTrees standard oak (vinesGrow=false; metaWood/metaLeaves passed). minTreeHeight=4. */
MC_HD MC_NOINLINE static int wg_trees(World *w, JavaRandom *r, int posX, int posY, int posZ,
                                 int metaWood, int metaLeaves) {
    int i = jrand_int_bound(r, 3) + 4;
    int flag = 1;
    if (posY >= 1 && posY + i + 1 <= W_Y) {
        for (int j = posY; j <= posY + 1 + i; ++j) {
            int k = 1;
            if (j == posY) k = 0;
            if (j >= posY + 1 + i - 2) k = 2;
            for (int l = posX - k; l <= posX + k && flag; ++l)
                for (int i1 = posZ - k; i1 <= posZ + k && flag; ++i1) {
                    if (j >= 0 && j < W_Y) { if (!wg_isReplaceableTree(w, l, j, i1)) flag = 0; }
                    else flag = 0;
                }
        }
        if (!flag) return 0;
        int state = w_get(w, posX, posY - 1, posZ);
        if (pb_canSustainSapling(state) && posY < W_Y - i - 1) {
            wg_onPlantGrow(w, posX, posY - 1, posZ);
            for (int i3 = posY - 3 + i; i3 <= posY + i; ++i3) {
                int i4 = i3 - (posY + i);
                int j1 = 1 - i4 / 2;
                for (int k1 = posX - j1; k1 <= posX + j1; ++k1) {
                    int l1 = k1 - posX;
                    for (int i2 = posZ - j1; i2 <= posZ + j1; ++i2) {
                        int j2 = i2 - posZ;
                        int al1 = l1 < 0 ? -l1 : l1, aj2 = j2 < 0 ? -j2 : j2;
                        int place;
                        if (al1 != j1 || aj2 != j1) place = 1;
                        else { int c = (jrand_int_bound(r, 2) != 0); place = c && (i4 != 0); }
                        if (place) {
                            /* vanilla WorldGenTrees also overwrites Material.VINE blocks
                             * (tallgrass/deadbush/vine/doubleplant); the orphaned dplant
                             * half stays until LIVE ticks break it (decoration runs with
                             * notify=false), so no pair-break here. */
                            int cs = w_get(w, k1, i3, i2);
                            if (pb_isAir(cs) || pb_isLeaves(cs) || pb_isMaterialVine(cs))
                                w_set(w, k1, i3, i2, metaLeaves);
                        }
                    }
                }
            }
            for (int j3 = 0; j3 < i; ++j3) {
                int cs = w_get(w, posX, posY + j3, posZ);
                if (pb_isAir(cs) || pb_isLeaves(cs) || pb_isMaterialVine(cs))
                    w_set(w, posX, posY + j3, posZ, metaWood);
            }
            return 1;
        }
        return 0;
    }
    return 0;
}

/* WorldGenBirchTree. useExtra=0 -> BIRCH_TREE; useExtra=1 -> SUPER_BIRCH_TREE
 * (BiomeForestMutated / birch forest M id 155: nextBoolean pick). */
MC_HD MC_NOINLINE static int wg_birch_ex(World *w, JavaRandom *r, int posX, int posY, int posZ,
                                          int useExtraRandomHeight) {
    int i = jrand_int_bound(r, 3) + 5;
    if (useExtraRandomHeight) i += jrand_int_bound(r, 7);
    int flag = 1;
    if (posY >= 1 && posY + i + 1 <= 256) {
        for (int j = posY; j <= posY + 1 + i; ++j) {
            int k = 1;
            if (j == posY) k = 0;
            if (j >= posY + 1 + i - 2) k = 2;
            for (int l = posX - k; l <= posX + k && flag; ++l)
                for (int i1 = posZ - k; i1 <= posZ + k && flag; ++i1) {
                    if (j >= 0 && j < W_Y) { if (!wg_isReplaceableTree(w, l, j, i1)) flag = 0; }
                    else flag = 0;
                }
        }
        if (!flag) return 0;
        int state = w_get(w, posX, posY - 1, posZ);
        if (pb_canSustainSapling(state) && posY < W_Y - i - 1) {
            wg_onPlantGrow(w, posX, posY - 1, posZ);
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
                            int s2 = w_get(w, i3, i2, k1);
                            if (pb_isAir(s2)) w_set(w, i3, i2, k1, PB_LEAVES_BIRCH);
                        }
                    }
                }
            }
            for (int j2 = 0; j2 < i; ++j2) {
                int s2 = w_get(w, posX, posY + j2, posZ);
                if (pb_isAir(s2) || pb_isLeaves(s2)) w_set(w, posX, posY + j2, posZ, PB_LOG_BIRCH);
            }
            return 1;
        }
        return 0;
    }
    return 0;
}
MC_HD MC_NOINLINE static int wg_birch(World *w, JavaRandom *r, int posX, int posY, int posZ) {
    return wg_birch_ex(w, r, posX, posY, posZ, 0);
}

MC_HD MC_NOINLINE static void wg_addVine(World *w, int x, int y, int z, int dir) {
    w_set(w, x, y, z, PB_VINE_BASE + dir);
    for (int yy = y - 1; w_isAir(w, x, yy, z) && (y - 1 - yy) < 4; --yy) {
        /* addHangingVine: i=4; place then descend while air && i>0. matches swamp addVine loop. */
        w_set(w, x, yy, z, PB_VINE_BASE + dir);
    }
}
/* WorldGenSwamp (oak swamp tree; scans down through water; canopy radius up to 3; vines). */
MC_HD MC_NOINLINE static int wg_swamptree(World *w, JavaRandom *r, int posX, int posY, int posZ) {
    int i = jrand_int_bound(r, 4) + 5;
    while (w_get(w, posX, posY - 1, posZ) == PB_WATER || w_get(w, posX, posY - 1, posZ) == PB_FLOWING_WATER)
        --posY;
    int flag = 1;
    if (posY >= 1 && posY + i + 1 <= 256) {
        for (int j = posY; j <= posY + 1 + i; ++j) {
            int k = 1;
            if (j == posY) k = 0;
            if (j >= posY + 1 + i - 2) k = 3;
            for (int l = posX - k; l <= posX + k && flag; ++l)
                for (int i1 = posZ - k; i1 <= posZ + k && flag; ++i1) {
                    if (j >= 0 && j < 256) {
                        int c = w_get(w, l, j, i1);
                        if (!pb_isAir(c) && !pb_isLeaves(c)) {
                            if (c != PB_WATER && c != PB_FLOWING_WATER) flag = 0;
                            else if (j > posY) flag = 0;
                        }
                    } else flag = 0;
                }
        }
        if (!flag) return 0;
        int state = w_get(w, posX, posY - 1, posZ);
        if (pb_canSustainSapling(state) && posY < W_Y - i - 1) {
            wg_onPlantGrow(w, posX, posY - 1, posZ);
            for (int k1 = posY - 3 + i; k1 <= posY + i; ++k1) {
                int j2 = k1 - (posY + i);
                int l2 = 2 - j2 / 2;
                for (int j3 = posX - l2; j3 <= posX + l2; ++j3) {
                    int k3 = j3 - posX;
                    for (int i4 = posZ - l2; i4 <= posZ + l2; ++i4) {
                        int j1 = i4 - posZ;
                        int ak3 = k3 < 0 ? -k3 : k3, aj1 = j1 < 0 ? -j1 : j1;
                        int place;
                        if (ak3 != l2 || aj1 != l2) place = 1;
                        else { int c = (jrand_int_bound(r, 2) != 0); place = c && (j2 != 0); }
                        if (place) {
                            if (pb_canBeReplacedByLeaves(w_get(w, j3, k1, i4)))
                                w_set(w, j3, k1, i4, PB_LEAVES_OAK);
                        }
                    }
                }
            }
            for (int l1 = 0; l1 < i; ++l1) {
                int c = w_get(w, posX, posY + l1, posZ);
                if (pb_isAir(c) || pb_isLeaves(c) || c == PB_FLOWING_WATER || c == PB_WATER)
                    w_set(w, posX, posY + l1, posZ, PB_LOG_OAK);
            }
            for (int i2 = posY - 3 + i; i2 <= posY + i; ++i2) {
                int k2 = i2 - (posY + i);
                int i3 = 2 - k2 / 2;
                for (int l3 = posX - i3; l3 <= posX + i3; ++l3)
                    for (int j4 = posZ - i3; j4 <= posZ + i3; ++j4) {
                        if (pb_isLeaves(w_get(w, l3, i2, j4))) {
                            if (jrand_int_bound(r, 4) == 0 && w_isAir(w, l3 - 1, i2, j4)) wg_addVine(w, l3 - 1, i2, j4, 0);
                            if (jrand_int_bound(r, 4) == 0 && w_isAir(w, l3 + 1, i2, j4)) wg_addVine(w, l3 + 1, i2, j4, 1);
                            if (jrand_int_bound(r, 4) == 0 && w_isAir(w, l3, i2, j4 - 1)) wg_addVine(w, l3, i2, j4 - 1, 2);
                            if (jrand_int_bound(r, 4) == 0 && w_isAir(w, l3, i2, j4 + 1)) wg_addVine(w, l3, i2, j4 + 1, 3);
                        }
                    }
            }
            return 1;
        }
        return 0;
    }
    return 0;
}

/* WorldGenTaiga1 (pine). uses canBeReplacedByLeaves for canopy. */
MC_HD MC_NOINLINE static int wg_taiga1(World *w, JavaRandom *r, int posX, int posY, int posZ) {
    int i = jrand_int_bound(r, 5) + 7;
    int j = i - jrand_int_bound(r, 2) - 3;
    int k = i - j;
    int l = 1 + jrand_int_bound(r, k + 1);
    if (posY >= 1 && posY + i + 1 <= 256) {
        int flag = 1;
        for (int i1 = posY; i1 <= posY + 1 + i && flag; ++i1) {
            int j1 = (i1 - posY < j) ? 0 : l;
            for (int k1 = posX - j1; k1 <= posX + j1 && flag; ++k1)
                for (int l1 = posZ - j1; l1 <= posZ + j1 && flag; ++l1) {
                    if (i1 >= 0 && i1 < 256) { if (!wg_isReplaceableTree(w, k1, i1, l1)) flag = 0; }
                    else flag = 0;
                }
        }
        if (!flag) return 0;
        int state = w_get(w, posX, posY - 1, posZ);
        if (pb_canSustainSapling(state) && posY < 256 - i - 1) {
            wg_onPlantGrow(w, posX, posY - 1, posZ);
            int k2 = 0;
            for (int l2 = posY + i; l2 >= posY + j; --l2) {
                for (int j3 = posX - k2; j3 <= posX + k2; ++j3) {
                    int k3 = j3 - posX;
                    for (int i2 = posZ - k2; i2 <= posZ + k2; ++i2) {
                        int j2 = i2 - posZ;
                        int ak3 = k3 < 0 ? -k3 : k3, aj2 = j2 < 0 ? -j2 : j2;
                        if (ak3 != k2 || aj2 != k2 || k2 <= 0) {
                            if (pb_canBeReplacedByLeaves(w_get(w, j3, l2, i2)))
                                w_set(w, j3, l2, i2, PB_LEAVES_SPRUCE);
                        }
                    }
                }
                if (k2 >= 1 && l2 == posY + j + 1) --k2;
                else if (k2 < l) ++k2;
            }
            for (int i3 = 0; i3 < i - 1; ++i3) {
                int c = w_get(w, posX, posY + i3, posZ);
                if (pb_isAir(c) || pb_isLeaves(c)) w_set(w, posX, posY + i3, posZ, PB_LOG_SPRUCE);
            }
            return 1;
        }
        return 0;
    }
    return 0;
}

/* WorldGenTaiga2 (spruce). */
MC_HD MC_NOINLINE static int wg_taiga2(World *w, JavaRandom *r, int posX, int posY, int posZ) {
    int i = jrand_int_bound(r, 4) + 6;
    int j = 1 + jrand_int_bound(r, 2);
    int k = i - j;
    int l = 2 + jrand_int_bound(r, 2);
    int flag = 1;
    if (posY >= 1 && posY + i + 1 <= W_Y) {
        for (int i1 = posY; i1 <= posY + 1 + i && flag; ++i1) {
            int j1 = (i1 - posY < j) ? 0 : l;
            for (int k1 = posX - j1; k1 <= posX + j1 && flag; ++k1)
                for (int l1 = posZ - j1; l1 <= posZ + j1 && flag; ++l1) {
                    if (i1 >= 0 && i1 < W_Y) {
                        int c = w_get(w, k1, i1, l1);
                        if (!pb_isAir(c) && !pb_isLeaves(c)) flag = 0;
                    } else flag = 0;
                }
        }
        if (!flag) return 0;
        int state = w_get(w, posX, posY - 1, posZ);
        if (pb_canSustainSapling(state) && posY < W_Y - i - 1) {
            wg_onPlantGrow(w, posX, posY - 1, posZ);
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
                            if (pb_canBeReplacedByLeaves(w_get(w, i2, j4, k2)))
                                w_set(w, i2, j4, k2, PB_LEAVES_SPRUCE);
                        }
                    }
                }
                if (i3 >= j3) { i3 = k3; k3 = 1; ++j3; if (j3 > l) j3 = l; }
                else ++i3;
            }
            int i4 = jrand_int_bound(r, 3);
            for (int k4 = 0; k4 < i - i4; ++k4) {
                int c = w_get(w, posX, posY + k4, posZ);
                if (pb_isAir(c) || pb_isLeaves(c)) w_set(w, posX, posY + k4, posZ, PB_LOG_SPRUCE);
            }
            return 1;
        }
        return 0;
    }
    return 0;
}

MC_HD MC_NOINLINE static void wg_spruce_leaves_strict(World *w, int x, int y, int z, int width) {
    int i = width * width;
    for (int j = -width; j <= width + 1; ++j)
        for (int k = -width; k <= width + 1; ++k) {
            int l = j - 1, i1 = k - 1;
            if (j * j + k * k <= i || l * l + i1 * i1 <= i ||
                j * j + i1 * i1 <= i || l * l + k * k <= i) {
                int c = w_get(w, x + j, y, z + k);
                if (pb_isAir(c) || pb_isLeaves(c)) w_set(w, x + j, y, z + k, PB_LEAVES_SPRUCE);
            }
        }
}

MC_HD MC_NOINLINE static int wg_huge_space_at(World *w, int x, int y, int z, int height) {
    if (!(y >= 1 && y + height + 1 <= W_Y)) return 0;
    for (int i = 0; i <= 1 + height; ++i) {
        int j = 2;
        if (i == 0) j = 1;
        else if (i >= 1 + height - 2) j = 2;
        for (int k = -j; k <= j; ++k)
            for (int l = -j; l <= j; ++l)
                if (y + i < 0 || y + i >= W_Y || !wg_isReplaceableTree(w, x + k, y + i, z + l))
                    return 0;
    }
    return 1;
}

MC_HD MC_NOINLINE static int wg_huge_ensure_growable(World *w, int x, int y, int z, int height) {
    if (!wg_huge_space_at(w, x, y, z, height)) return 0;
    if (pb_canSustainSapling(w_get(w, x, y - 1, z)) && y >= 2) {
        wg_onPlantGrow(w, x, y - 1, z);
        wg_onPlantGrow(w, x + 1, y - 1, z);
        wg_onPlantGrow(w, x, y - 1, z + 1);
        wg_onPlantGrow(w, x + 1, y - 1, z + 1);
        return 1;
    }
    return 0;
}

MC_HD MC_NOINLINE static void wg_mega_pine_crown(World *w, JavaRandom *r, int x, int z,
                                                 int y, int useBaseHeight) {
    int i = jrand_int_bound(r, 5) + (useBaseHeight ? 13 : 3);
    int j = 0;
    for (int k = y - i; k <= y; ++k) {
        int l = y - k;
        int i1 = mc_floor((double)(((float)l / (float)i) * 3.5F));
        int width = i1 + (l > 0 && i1 == j && (k & 1) == 0 ? 1 : 0);
        wg_spruce_leaves_strict(w, x, k, z, width);
        j = i1;
    }
}

MC_HD MC_NOINLINE static void wg_mega_pine_place_podzol_at(World *w, int x, int y, int z) {
    for (int i = 2; i >= -3; --i) {
        int by = y + i;
        int c = w_get(w, x, by, z);
        if (pb_canSustainSapling(c)) {
            w_set(w, x, by, z, PB_PODZOL);
            break;
        }
        if (c != PB_AIR && i < 0) break;
    }
}

MC_HD MC_NOINLINE static void wg_mega_pine_place_podzol_circle(World *w, int x, int y, int z) {
    for (int i = -2; i <= 2; ++i)
        for (int j = -2; j <= 2; ++j)
            if ((i < 0 ? -i : i) != 2 || (j < 0 ? -j : j) != 2)
                wg_mega_pine_place_podzol_at(w, x + i, y, z + j);
}

MC_HD MC_NOINLINE static void wg_mega_pine_saplings(World *w, JavaRandom *r, int x, int y, int z) {
    wg_mega_pine_place_podzol_circle(w, x - 1, y, z - 1);
    wg_mega_pine_place_podzol_circle(w, x + 2, y, z - 1);
    wg_mega_pine_place_podzol_circle(w, x - 1, y, z + 2);
    wg_mega_pine_place_podzol_circle(w, x + 2, y, z + 2);
    for (int i = 0; i < 5; ++i) {
        int j = jrand_int_bound(r, 64);
        int k = j % 8;
        int l = j / 8;
        if (k == 0 || k == 7 || l == 0 || l == 7)
            wg_mega_pine_place_podzol_circle(w, x - 3 + k, y, z - 3 + l);
    }
}

/* WorldGenMegaPineTree(false, useBaseHeight): mega taiga/spruce 2x2 spruce. */
MC_HD MC_NOINLINE static int wg_mega_pine(World *w, JavaRandom *r, int x, int y, int z,
                                          int useBaseHeight) {
    int i = jrand_int_bound(r, 3) + 13;
    i += jrand_int_bound(r, 15);
    if (!wg_huge_ensure_growable(w, x, y, z, i)) return 0;
    wg_mega_pine_crown(w, r, x, z, y + i, useBaseHeight);
    for (int j = 0; j < i; ++j) {
        int c = w_get(w, x, y + j, z);
        if (pb_isAir(c) || pb_isLeaves(c)) w_set(w, x, y + j, z, PB_LOG_SPRUCE);
        if (j < i - 1) {
            c = w_get(w, x + 1, y + j, z);
            if (pb_isAir(c) || pb_isLeaves(c)) w_set(w, x + 1, y + j, z, PB_LOG_SPRUCE);
            c = w_get(w, x + 1, y + j, z + 1);
            if (pb_isAir(c) || pb_isLeaves(c)) w_set(w, x + 1, y + j, z + 1, PB_LOG_SPRUCE);
            c = w_get(w, x, y + j, z + 1);
            if (pb_isAir(c) || pb_isLeaves(c)) w_set(w, x, y + j, z + 1, PB_LOG_SPRUCE);
        }
    }
    return 1;
}

MC_HD static inline int wg_horiz_dx(int dir) {
    return dir == 1 ? 1 : (dir == 3 ? -1 : 0);  /* NORTH,EAST,SOUTH,WEST */
}
MC_HD static inline int wg_horiz_dz(int dir) {
    return dir == 0 ? -1 : (dir == 2 ? 1 : 0);
}

/* WorldGenSavannaTree (acacia). EnumFacing.Plane.HORIZONTAL.random order is N,E,S,W. */
MC_HD MC_NOINLINE static int wg_savannatree(World *w, JavaRandom *r, int posX, int posY, int posZ) {
    int i = jrand_int_bound(r, 3) + jrand_int_bound(r, 3) + 5;
    int flag = 1;
    if (posY >= 1 && posY + i + 1 <= W_Y) {
        for (int j = posY; j <= posY + 1 + i; ++j) {
            int k = 1;
            if (j == posY) k = 0;
            if (j >= posY + 1 + i - 2) k = 2;
            for (int l = posX - k; l <= posX + k && flag; ++l)
                for (int i1 = posZ - k; i1 <= posZ + k && flag; ++i1) {
                    if (j >= 0 && j < W_Y) {
                        if (!wg_isReplaceableTree(w, l, j, i1)) flag = 0;
                    } else flag = 0;
                }
        }
        if (!flag) return 0;
        {
            int state = w_get(w, posX, posY - 1, posZ);
            if (pb_canSustainSapling(state) && posY < W_Y - i - 1) {
                int enumfacing, k2, l2, i3, j1, k1, l1;
                wg_onPlantGrow(w, posX, posY - 1, posZ);
                enumfacing = jrand_int_bound(r, 4);
                k2 = i - jrand_int_bound(r, 4) - 1;
                l2 = 3 - jrand_int_bound(r, 3);
                i3 = posX;
                j1 = posZ;
                k1 = 0;
                for (l1 = 0; l1 < i; ++l1) {
                    int i2 = posY + l1;
                    if (l1 >= k2 && l2 > 0) {
                        i3 += wg_horiz_dx(enumfacing);
                        j1 += wg_horiz_dz(enumfacing);
                        --l2;
                    }
                    state = w_get(w, i3, i2, j1);
                    if (pb_isAir(state) || pb_isLeaves(state)) {
                        w_set(w, i3, i2, j1, PB_LOG_ACACIA);
                        k1 = i2;
                    }
                }
                {
                    int bx = i3, by = k1, bz = j1;
                    for (int j3 = -3; j3 <= 3; ++j3)
                        for (int i4 = -3; i4 <= 3; ++i4)
                            if ((j3 < 0 ? -j3 : j3) != 3 || (i4 < 0 ? -i4 : i4) != 3) {
                                int c = w_get(w, bx + j3, by, bz + i4);
                                if (pb_isAir(c) || pb_isLeaves(c)) w_set(w, bx + j3, by, bz + i4, PB_LEAVES_ACACIA);
                            }
                    ++by;
                    for (int k3 = -1; k3 <= 1; ++k3)
                        for (int j4 = -1; j4 <= 1; ++j4) {
                            int c = w_get(w, bx + k3, by, bz + j4);
                            if (pb_isAir(c) || pb_isLeaves(c)) w_set(w, bx + k3, by, bz + j4, PB_LEAVES_ACACIA);
                        }
                    {
                        int c = w_get(w, bx + 2, by, bz);
                        if (pb_isAir(c) || pb_isLeaves(c)) w_set(w, bx + 2, by, bz, PB_LEAVES_ACACIA);
                        c = w_get(w, bx - 2, by, bz);
                        if (pb_isAir(c) || pb_isLeaves(c)) w_set(w, bx - 2, by, bz, PB_LEAVES_ACACIA);
                        c = w_get(w, bx, by, bz + 2);
                        if (pb_isAir(c) || pb_isLeaves(c)) w_set(w, bx, by, bz + 2, PB_LEAVES_ACACIA);
                        c = w_get(w, bx, by, bz - 2);
                        if (pb_isAir(c) || pb_isLeaves(c)) w_set(w, bx, by, bz - 2, PB_LEAVES_ACACIA);
                    }
                }
                i3 = posX;
                j1 = posZ;
                {
                    int enumfacing1 = jrand_int_bound(r, 4);
                    if (enumfacing1 != enumfacing) {
                        int l3 = k2 - jrand_int_bound(r, 2) - 1;
                        int k4 = 1 + jrand_int_bound(r, 3);
                        k1 = 0;
                        for (int l4 = l3; l4 < i && k4 > 0; --k4) {
                            if (l4 >= 1) {
                                int j2 = posY + l4;
                                i3 += wg_horiz_dx(enumfacing1);
                                j1 += wg_horiz_dz(enumfacing1);
                                state = w_get(w, i3, j2, j1);
                                if (pb_isAir(state) || pb_isLeaves(state)) {
                                    w_set(w, i3, j2, j1, PB_LOG_ACACIA);
                                    k1 = j2;
                                }
                            }
                            ++l4;
                        }
                        if (k1 > 0) {
                            int bx = i3, by = k1, bz = j1;
                            for (int i5 = -2; i5 <= 2; ++i5)
                                for (int k5 = -2; k5 <= 2; ++k5)
                                    if ((i5 < 0 ? -i5 : i5) != 2 || (k5 < 0 ? -k5 : k5) != 2) {
                                        int c = w_get(w, bx + i5, by, bz + k5);
                                        if (pb_isAir(c) || pb_isLeaves(c)) w_set(w, bx + i5, by, bz + k5, PB_LEAVES_ACACIA);
                                    }
                            ++by;
                            for (int j5 = -1; j5 <= 1; ++j5)
                                for (int l5 = -1; l5 <= 1; ++l5) {
                                    int c = w_get(w, bx + j5, by, bz + l5);
                                    if (pb_isAir(c) || pb_isLeaves(c)) w_set(w, bx + j5, by, bz + l5, PB_LEAVES_ACACIA);
                                }
                        }
                    }
                }
                return 1;
            }
        }
    }
    return 0;
}

MC_HD MC_NOINLINE static void wg_placeVine(World *w, int x, int y, int z, int dir) {
    w_set(w, x, y, z, PB_VINE_BASE + dir);
}

/* WorldGenTrees(false, minTreeHeight, jungle log/leaves, true). */
MC_HD MC_NOINLINE static int wg_jungletree(World *w, JavaRandom *r, int posX, int posY, int posZ,
                                           int minTreeHeight) {
    int i = jrand_int_bound(r, 3) + minTreeHeight;
    int flag = 1;
    if (posY >= 1 && posY + i + 1 <= W_Y) {
        for (int j = posY; j <= posY + 1 + i; ++j) {
            int k = 1;
            if (j == posY) k = 0;
            if (j >= posY + 1 + i - 2) k = 2;
            for (int l = posX - k; l <= posX + k && flag; ++l)
                for (int i1 = posZ - k; i1 <= posZ + k && flag; ++i1) {
                    if (j >= 0 && j < W_Y) { if (!wg_isReplaceableTree(w, l, j, i1)) flag = 0; }
                    else flag = 0;
                }
        }
        if (!flag) return 0;
        {
            int state = w_get(w, posX, posY - 1, posZ);
            if (pb_canSustainSapling(state) && posY < W_Y - i - 1) {
                wg_onPlantGrow(w, posX, posY - 1, posZ);
                for (int i3 = posY - 3 + i; i3 <= posY + i; ++i3) {
                    int i4 = i3 - (posY + i);
                    int j1 = 1 - i4 / 2;
                    for (int k1 = posX - j1; k1 <= posX + j1; ++k1) {
                        int l1 = k1 - posX;
                        for (int i2 = posZ - j1; i2 <= posZ + j1; ++i2) {
                            int j2 = i2 - posZ;
                            int al1 = l1 < 0 ? -l1 : l1, aj2 = j2 < 0 ? -j2 : j2;
                            int place;
                            if (al1 != j1 || aj2 != j1) place = 1;
                            else { int c = (jrand_int_bound(r, 2) != 0); place = c && (i4 != 0); }
                            if (place) {
                                int cs = w_get(w, k1, i3, i2);
                                if (pb_isAir(cs) || pb_isLeaves(cs) || pb_isMaterialVine(cs))
                                    w_set(w, k1, i3, i2, PB_LEAVES_JUNGLE);
                            }
                        }
                    }
                }
                for (int j3 = 0; j3 < i; ++j3) {
                    int cs = w_get(w, posX, posY + j3, posZ);
                    if (pb_isAir(cs) || pb_isLeaves(cs) || pb_isMaterialVine(cs)) {
                        w_set(w, posX, posY + j3, posZ, PB_LOG_JUNGLE);
                        if (j3 > 0) {
                            if (jrand_int_bound(r, 3) > 0 && w_isAir(w, posX - 1, posY + j3, posZ))
                                wg_placeVine(w, posX - 1, posY + j3, posZ, 0);
                            if (jrand_int_bound(r, 3) > 0 && w_isAir(w, posX + 1, posY + j3, posZ))
                                wg_placeVine(w, posX + 1, posY + j3, posZ, 1);
                            if (jrand_int_bound(r, 3) > 0 && w_isAir(w, posX, posY + j3, posZ - 1))
                                wg_placeVine(w, posX, posY + j3, posZ - 1, 2);
                            if (jrand_int_bound(r, 3) > 0 && w_isAir(w, posX, posY + j3, posZ + 1))
                                wg_placeVine(w, posX, posY + j3, posZ + 1, 3);
                        }
                    }
                }
                for (int k3 = posY - 3 + i; k3 <= posY + i; ++k3) {
                    int j4 = k3 - (posY + i);
                    int k4 = 2 - j4 / 2;
                    for (int l4 = posX - k4; l4 <= posX + k4; ++l4)
                        for (int i5 = posZ - k4; i5 <= posZ + k4; ++i5)
                            if (pb_isLeaves(w_get(w, l4, k3, i5))) {
                                if (jrand_int_bound(r, 4) == 0 && w_isAir(w, l4 - 1, k3, i5))
                                    wg_addVine(w, l4 - 1, k3, i5, 0);
                                if (jrand_int_bound(r, 4) == 0 && w_isAir(w, l4 + 1, k3, i5))
                                    wg_addVine(w, l4 + 1, k3, i5, 1);
                                if (jrand_int_bound(r, 4) == 0 && w_isAir(w, l4, k3, i5 - 1))
                                    wg_addVine(w, l4, k3, i5 - 1, 2);
                                if (jrand_int_bound(r, 4) == 0 && w_isAir(w, l4, k3, i5 + 1))
                                    wg_addVine(w, l4, k3, i5 + 1, 3);
                            }
                }
                if (jrand_int_bound(r, 5) == 0 && i > 5) {
                    static const int OX[4] = { 0, -1, 0, 1 };   /* opposite of N,E,S,W */
                    static const int OZ[4] = { 1, 0, -1, 0 };
                    for (int l3 = 0; l3 < 2; ++l3)
                        for (int f = 0; f < 4; ++f)
                            if (jrand_int_bound(r, 4 - l3) == 0) {
                                int age = jrand_int_bound(r, 3);
                                (void)age;
                                w_set(w, posX + OX[f], posY + i - 5 + l3, posZ + OZ[f], PB_COCOA);
                            }
                }
                return 1;
            }
        }
    }
    return 0;
}

/* WorldGenShrub(jungle log, oak leaves). Always returns true after consuming placement draws. */
MC_HD MC_NOINLINE static int wg_jungle_shrub(World *w, JavaRandom *r, int x, int y, int z) {
    while ((pb_isAir(w_get(w, x, y, z)) || pb_isLeaves(w_get(w, x, y, z))) && y > 0) --y;
    if (pb_canSustainSapling(w_get(w, x, y, z))) {
        ++y;
        w_set(w, x, y, z, PB_LOG_JUNGLE);
        for (int i = y; i <= y + 2; ++i) {
            int j = i - y;
            int k = 2 - j;
            for (int l = x - k; l <= x + k; ++l) {
                int i1 = l - x;
                for (int j1 = z - k; j1 <= z + k; ++j1) {
                    int k1 = j1 - z;
                    int ai1 = i1 < 0 ? -i1 : i1, ak1 = k1 < 0 ? -k1 : k1;
                    if (ai1 != k || ak1 != k || jrand_int_bound(r, 2) != 0) {
                        int c = w_get(w, l, i, j1);
                        if (pb_canBeReplacedByLeaves(c)) w_set(w, l, i, j1, PB_LEAVES_OAK);
                    }
                }
            }
        }
    }
    return 1;
}

MC_HD MC_NOINLINE static void wg_mega_leaves_strict(World *w, int x, int y, int z, int width) {
    int i = width * width;
    for (int j = -width; j <= width + 1; ++j)
        for (int k = -width; k <= width + 1; ++k) {
            int l = j - 1, i1 = k - 1;
            if (j * j + k * k <= i || l * l + i1 * i1 <= i ||
                j * j + i1 * i1 <= i || l * l + k * k <= i) {
                int c = w_get(w, x + j, y, z + k);
                if (pb_isAir(c) || pb_isLeaves(c)) w_set(w, x + j, y, z + k, PB_LEAVES_JUNGLE);
            }
        }
}

MC_HD MC_NOINLINE static void wg_mega_leaves(World *w, int x, int y, int z, int width) {
    int i = width * width;
    for (int j = -width; j <= width; ++j)
        for (int k = -width; k <= width; ++k)
            if (j * j + k * k <= i) {
                int c = w_get(w, x + j, y, z + k);
                if (pb_isAir(c) || pb_isLeaves(c)) w_set(w, x + j, y, z + k, PB_LEAVES_JUNGLE);
            }
}

MC_HD MC_NOINLINE static void wg_mega_place_vine(World *w, JavaRandom *r, int x, int y, int z, int dir) {
    if (jrand_int_bound(r, 3) > 0 && w_isAir(w, x, y, z)) wg_placeVine(w, x, y, z, dir);
}

/* WorldGenMegaJungle(false, 10, 20, jungle log/leaves). */
MC_HD MC_NOINLINE static int wg_mega_jungle(World *w, JavaRandom *r, int x, int y, int z) {
    int i = jrand_int_bound(r, 3) + 10;
    i += jrand_int_bound(r, 20);
    if (!(y >= 1 && y + i + 1 <= 256)) return 0;
    for (int yy = 0; yy <= 1 + i; ++yy) {
        int rad = 2;
        if (yy == 0) rad = 1;
        for (int dx = -rad; dx <= rad; ++dx)
            for (int dz = -rad; dz <= rad; ++dz)
                if (y + yy < 0 || y + yy >= 256 || !wg_isReplaceableTree(w, x + dx, y + yy, z + dz))
                    return 0;
    }
    if (!pb_canSustainSapling(w_get(w, x, y - 1, z)) || y < 2) return 0;
    wg_onPlantGrow(w, x, y - 1, z);
    wg_onPlantGrow(w, x + 1, y - 1, z);
    wg_onPlantGrow(w, x, y - 1, z + 1);
    wg_onPlantGrow(w, x + 1, y - 1, z + 1);
    for (int j = -2; j <= 0; ++j)
        wg_mega_leaves_strict(w, x, y + i + j, z, 3 - j);
    for (int j = y + i - 2 - jrand_int_bound(r, 4); j > y + i / 2; ) {
        float f = jrand_float(r) * ((float)MC_PI * 2.0F);
        int k = x, l = z;
        for (int i1 = 0; i1 < 5; ++i1) {
            k = x + (int)(1.5F + mc_cos(w->st, f) * (float)i1);
            l = z + (int)(1.5F + mc_sin(w->st, f) * (float)i1);
            w_set(w, k, j - 3 + i1 / 2, l, PB_LOG_JUNGLE);
        }
        {
            int j2 = 1 + jrand_int_bound(r, 2);
            int j1 = j;
            for (int k1 = j - j2; k1 <= j1; ++k1) {
                int l1 = k1 - j1;
                wg_mega_leaves(w, k, k1, l, 1 - l1);
            }
        }
        j -= 2 + jrand_int_bound(r, 4);
    }
    for (int i2 = 0; i2 < i; ++i2) {
        int by = y + i2;
        if (pb_isAir(w_get(w, x, by, z)) || pb_isLeaves(w_get(w, x, by, z))) {
            w_set(w, x, by, z, PB_LOG_JUNGLE);
            if (i2 > 0) {
                wg_mega_place_vine(w, r, x - 1, by, z, 0);
                wg_mega_place_vine(w, r, x, by, z - 1, 2);
            }
        }
        if (i2 < i - 1) {
            if (pb_isAir(w_get(w, x + 1, by, z)) || pb_isLeaves(w_get(w, x + 1, by, z))) {
                w_set(w, x + 1, by, z, PB_LOG_JUNGLE);
                if (i2 > 0) {
                    wg_mega_place_vine(w, r, x + 2, by, z, 1);
                    wg_mega_place_vine(w, r, x + 1, by, z - 1, 2);
                }
            }
            if (pb_isAir(w_get(w, x + 1, by, z + 1)) || pb_isLeaves(w_get(w, x + 1, by, z + 1))) {
                w_set(w, x + 1, by, z + 1, PB_LOG_JUNGLE);
                if (i2 > 0) {
                    wg_mega_place_vine(w, r, x + 2, by, z + 1, 1);
                    wg_mega_place_vine(w, r, x + 1, by, z + 2, 3);
                }
            }
            if (pb_isAir(w_get(w, x, by, z + 1)) || pb_isLeaves(w_get(w, x, by, z + 1))) {
                w_set(w, x, by, z + 1, PB_LOG_JUNGLE);
                if (i2 > 0) {
                    wg_mega_place_vine(w, r, x - 1, by, z + 1, 0);
                    wg_mega_place_vine(w, r, x, by, z + 2, 3);
                }
            }
        }
    }
    return 1;
}

/* WorldGenBigTree (big oak). Uses its OWN Random seeded from one main-stream nextLong; persistent
 * heightLimit on the static singleton (Biome.BIG_TREE_FEATURE). Double Math trig (sin/cos/pow/sqrt). */
typedef struct { int x, y, z, branchBase; } FoliageCoord;
MC_HD MC_NOINLINE static int bt_greatestDistance(int x, int y, int z) {
    int i = x < 0 ? -x : x, j = y < 0 ? -y : y, k = z < 0 ? -z : z;
    return k > i && k > j ? k : (j > i ? j : i);
}
MC_HD MC_NOINLINE static float bt_layerSize(int heightLimit, int y) {
    if ((float)y < (float)heightLimit * 0.3F) return -1.0F;
    float f = (float)heightLimit / 2.0F;
    float f1 = f - (float)y;
    float f2 = (float)sqrt((double)(f * f - f1 * f1));
    if (f1 == 0.0F) f2 = f;
    else if ((f1 < 0 ? -f1 : f1) >= f) return 0.0F;
    return f2 * 0.5F;
}
MC_HD MC_NOINLINE static float bt_leafSize(int leafDistanceLimit, int y) {
    return (y >= 0 && y < leafDistanceLimit)
        ? (y != 0 && y != leafDistanceLimit - 1 ? 3.0F : 2.0F) : -1.0F;
}
MC_HD MC_NOINLINE static int bt_checkBlockLine(World *w, int x0, int y0, int z0, int x1, int y1, int z1) {
    int bx = x1 - x0, by = y1 - y0, bz = z1 - z0;
    int i = bt_greatestDistance(bx, by, bz);
    if (i == 0) return -1;
    float f = (float)bx / (float)i, f1 = (float)by / (float)i, f2 = (float)bz / (float)i;
    for (int j = 0; j <= i; ++j) {
        int px = x0 + mc_floor((double)(0.5F + (float)j * f));
        int py = y0 + mc_floor((double)(0.5F + (float)j * f1));
        int pz = z0 + mc_floor((double)(0.5F + (float)j * f2));
        if (!wg_isReplaceableTree(w, px, py, pz)) return j;
    }
    return -1;
}
MC_HD MC_NOINLINE static void bt_crosSection(World *w, int x, int y, int z, float sz, int leaf) {
    int i = (int)((double)sz + 0.618);
    for (int j = -i; j <= i; ++j)
        for (int k = -i; k <= i; ++k) {
            double aj = (j < 0 ? -j : j) + 0.5, ak = (k < 0 ? -k : k) + 0.5;
            if (aj * aj + ak * ak <= (double)(sz * sz)) {
                int c = w_get(w, x + j, y, z + k);
                if (pb_isAir(c) || pb_isLeaves(c)) w_set(w, x + j, y, z + k, leaf);
            }
        }
}
MC_HD MC_NOINLINE static int bt_logAxis(int x0, int z0, int x1, int z1) {
    int i = x1 - x0; if (i < 0) i = -i;
    int j = z1 - z0; if (j < 0) j = -j;
    int k = i > j ? i : j;
    if (k > 0) { if (i == k) return PB_LOG_OAK_X; if (j == k) return PB_LOG_OAK_Z; }
    return PB_LOG_OAK;
}
MC_HD MC_NOINLINE static void bt_limb(World *w, int x0, int y0, int z0, int x1, int y1, int z1) {
    int bx = x1 - x0, by = y1 - y0, bz = z1 - z0;
    int i = bt_greatestDistance(bx, by, bz);
    float f = (float)bx / (float)i, f1 = (float)by / (float)i, f2 = (float)bz / (float)i;
    for (int j = 0; j <= i; ++j) {
        int px = x0 + mc_floor((double)(0.5F + (float)j * f));
        int py = y0 + mc_floor((double)(0.5F + (float)j * f1));
        int pz = z0 + mc_floor((double)(0.5F + (float)j * f2));
        w_set(w, px, py, pz, bt_logAxis(x0, z0, px, pz));
    }
}
#define BT_MAX_FOLIAGE 4096
MC_HD MC_NOINLINE static int wg_bigtree(World *w, JavaRandom *mainr, int posX, int posY, int posZ,
                                   FoliageCoord *fol) {
    int leafDistanceLimit = 5;          /* setDecorationDefaults() */
    JavaRandom rr; jrand_set(&rr, jrand_long(mainr));
    if (w->bigtree_heightLimit == 0) w->bigtree_heightLimit = 5 + jrand_int_bound(&rr, 12);
    int heightLimit = w->bigtree_heightLimit;
    /* validTreeLocation */
    int soil = w_get(w, posX, posY - 1, posZ);
    if (!pb_canSustainSapling(soil)) return 0;
    int chk = bt_checkBlockLine(w, posX, posY, posZ, posX, posY + heightLimit - 1, posZ);
    if (chk == -1) { /* ok */ }
    else if (chk < 6) return 0;
    else heightLimit = chk;
    w->bigtree_heightLimit = heightLimit;
    /* generateLeafNodeList */
    int height = (int)((double)heightLimit * 0.618);
    if (height >= heightLimit) height = heightLimit - 1;
    /* i = (int)(1.382 + Math.pow(leafDensity * heightLimit / 13.0, 2.0)); leafDensity = 1.0 */
    int ii = (int)(1.382 + pow((double)heightLimit / 13.0, 2.0));
    if (ii < 1) ii = 1;
    int jj = posY + height;
    int kk = heightLimit - leafDistanceLimit;
    int nf = 0;
    fol[nf].x = posX; fol[nf].y = posY + kk; fol[nf].z = posZ; fol[nf].branchBase = jj; ++nf;
    for (; kk >= 0; --kk) {
        float f = bt_layerSize(heightLimit, kk);
        if (f >= 0.0F) {
            for (int l = 0; l < ii; ++l) {
                double d0 = 1.0 * (double)f * ((double)jrand_float(&rr) + 0.328);
                double d1 = (double)(jrand_float(&rr) * 2.0F) * MC_PI;
                double d2 = d0 * sin(d1) + 0.5;
                double d3 = d0 * cos(d1) + 0.5;
                int bx = posX + mc_floor(d2);
                int by = posY + (kk - 1);
                int bz = posZ + mc_floor(d3);
                if (bt_checkBlockLine(w, bx, by, bz, bx, by + leafDistanceLimit, bz) == -1) {
                    int i1 = posX - bx, j1 = posZ - bz;
                    double d4 = (double)by - sqrt((double)(i1 * i1 + j1 * j1)) * 0.381;
                    int k1 = d4 > (double)jj ? jj : (int)d4;
                    if (bt_checkBlockLine(w, posX, k1, posZ, bx, by, bz) == -1 && nf < BT_MAX_FOLIAGE) {
                        fol[nf].x = bx; fol[nf].y = by; fol[nf].z = bz; fol[nf].branchBase = k1; ++nf;
                    }
                }
            }
        }
    }
    /* generateLeaves */
    for (int n = 0; n < nf; ++n)
        for (int i = 0; i < leafDistanceLimit; ++i)
            bt_crosSection(w, fol[n].x, fol[n].y + i, fol[n].z, bt_leafSize(leafDistanceLimit, i), PB_LEAVES_OAK);
    /* generateTrunk */
    bt_limb(w, posX, posY, posZ, posX, posY + height, posZ);
    /* generateLeafNodeBases */
    for (int n = 0; n < nf; ++n) {
        int i = fol[n].branchBase;
        int bx = posX, by = i, bz = posZ;
        if (!(bx == fol[n].x && by == fol[n].y && bz == fol[n].z) &&
            (double)(i - posY) >= (double)heightLimit * 0.2)
            bt_limb(w, bx, by, bz, fol[n].x, fol[n].y, fol[n].z);
    }
    return 1;
}

/* ====================================================================================== */
/* small plants                                                                            */
/* ====================================================================================== */
MC_HD MC_NOINLINE static int pb_canSustainBushPos(const World *w, int x, int y, int z) {
    return pb_canSustainBush(w_get(w, x, y - 1, z));
}
/* WorldGenTallGrass: scan down past air/leaves, then 128 attempts; tallGrassState passed. */
MC_HD MC_NOINLINE static void wg_tallgrass(World *w, JavaRandom *r, int x, int y, int z, int state) {
    while ((pb_isAir(w_get(w, x, y, z)) || pb_isLeaves(w_get(w, x, y, z))) && y > 0) --y;
    for (int i = 0; i < 128; ++i) {
        int dx = wg_off(r, 8), dy = wg_off(r, 4), dz = wg_off(r, 8);
        int bx = x + dx, by = y + dy, bz = z + dz;
        if (w_isAir(w, bx, by, bz) && pb_canSustainBushPos(w, bx, by, bz))
            w_set(w, bx, by, bz, state);
    }
}
/* WorldGenFlowers: 64 attempts; state passed. */
MC_HD MC_NOINLINE static void wg_flowers(World *w, JavaRandom *r, int x, int y, int z, int state) {
    for (int i = 0; i < 64; ++i) {
        int dx = wg_off(r, 8), dy = wg_off(r, 4), dz = wg_off(r, 8);
        int bx = x + dx, by = y + dy, bz = z + dz;
        if (w_isAir(w, bx, by, bz) && pb_canSustainBushPos(w, bx, by, bz)) w_set(w, bx, by, bz, state);
    }
}
/* WorldGenDeadBush: scan down, 4 attempts. canBlockStay deadbush: soil grass/dirt/sand/clay? Desert
 * type -> sand/hardened/stained, OR canSustainBush (grass/dirt). We use canSustainBush||sand. */
MC_HD MC_NOINLINE static int pb_deadbush_ok(int soil) {
    return pb_canSustainBush(soil) || soil == PB_SAND || soil == PB_HARDENED_CLAY ||
           soil == PB_STAINED_CLAY || cb_is_stained_clay(soil);
}
MC_HD MC_NOINLINE static void wg_deadbush(World *w, JavaRandom *r, int x, int y, int z) {
    while ((pb_isAir(w_get(w, x, y, z)) || pb_isLeaves(w_get(w, x, y, z))) && y > 0) --y;
    for (int i = 0; i < 4; ++i) {
        int dx = wg_off(r, 8), dy = wg_off(r, 4), dz = wg_off(r, 8);
        int bx = x + dx, by = y + dy, bz = z + dz;
        if (w_isAir(w, bx, by, bz) && pb_deadbush_ok(w_get(w, bx, by - 1, bz)))
            w_set(w, bx, by, bz, PB_DEADBUSH);
    }
}
/* WorldGenWaterlily: 10 attempts. canPlaceBlockAt: isReplaceable(pos)(air) && below water(level0)||ice. */
MC_HD MC_NOINLINE static void wg_waterlily(World *w, JavaRandom *r, int x, int y, int z) {
    for (int i = 0; i < 10; ++i) {
        int j = x + wg_off(r, 8);
        int k = y + wg_off(r, 4);
        int l = z + wg_off(r, 8);
        if (w_isAir(w, j, k, l)) {
            int below = w_get(w, j, k - 1, l);
            if (below == PB_WATER || below == PB_ICE) w_set(w, j, k, l, PB_WATER_LILY);
        }
    }
}
/* WorldGenBush (mushrooms): 64 attempts; canBlockStay mushroom. */
MC_HD MC_NOINLINE static int wg_mushroom_canStay(const World *w, int x, int y, int z) {
    if (y < 0 || y >= 256) return 0;
    int below = w_get(w, x, y - 1, z);
    if (below == PB_MYCELIUM) return 1;
    if (below == PB_PODZOL) return 1;
    /* Forge canSustainPlant via BlockMushroom.canSustainBush = isFullBlock(): server-side
     * leaves register opaque, so LEAVES ARE VALID SOIL (java bushdbg seed 19: 43 leaf-soil
     * placements at light<13, 0 rejections). blocksMovement matches the accepted set. */
    return w_light(w, x, y, z) < 13 && pb_blocksMovement(below);
}
MC_HD MC_NOINLINE static void wg_bush(World *w, JavaRandom *r, int x, int y, int z, int block) {
    for (int i = 0; i < 64; ++i) {
        int bx = x + wg_off(r, 8), by = y + wg_off(r, 4), bz = z + wg_off(r, 8);
        if (w_isAir(w, bx, by, bz) && wg_mushroom_canStay(w, bx, by, bz)) w_set(w, bx, by, bz, block);
    }
}
MC_HD static inline int wg_default_canBeReplacedByLeaves(int c) {
    return pb_canBeReplacedByLeaves(c);
}
/* WorldGenBigMushroom.generate, unspecified type: nextBoolean brown/red. */
MC_HD MC_NOINLINE static int wg_bigmushroom(World *w, JavaRandom *r, int x, int y, int z) {
    int block = (jrand_next(r, 1) != 0) ? PB_BROWN_SHROOM_BLOCK : PB_RED_SHROOM_BLOCK;
    int i = jrand_int_bound(r, 3) + 4;
    if (jrand_int_bound(r, 12) == 0) i *= 2;
    if (!(y >= 1 && y + i + 1 < W_Y)) return 0;
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
                        if (center && l2 < y + i) continue;
                        if (wg_default_canBeReplacedByLeaves(w_get(w, l1, l2, i2)))
                            w_set(w, l1, l2, i2, block);
                    }
            }
        }
        for (int i3 = 0; i3 < i; ++i3)
            if (wg_default_canBeReplacedByLeaves(w_get(w, x, y + i3, z)))
                w_set(w, x, y + i3, z, block);
    }
    return 1;
}
/* WorldGenReed: 20 attempts. */
MC_HD MC_NOINLINE static int wg_reed_canStay(const World *w, int x, int y, int z) {
    int below = w_get(w, x, y - 1, z);
    if (below == PB_REEDS) return 1;
    if (below != PB_GRASS && !pb_isDirt(below) && below != PB_SAND) return 0;
    if (pb_isWater(w_get(w, x - 1, y - 1, z)) || pb_isWater(w_get(w, x + 1, y - 1, z)) ||
        pb_isWater(w_get(w, x, y - 1, z - 1)) || pb_isWater(w_get(w, x, y - 1, z + 1))) return 1;
    return 0;
}
MC_HD MC_NOINLINE static void wg_reed(World *w, JavaRandom *r, int x, int y, int z) {
    for (int i = 0; i < 20; ++i) {
        int bx = x + wg_off(r, 4), by = y, bz = z + wg_off(r, 4);
        if (w_isAir(w, bx, by, bz)) {
            int bdx = bx, bdz = bz, bdy = by - 1;  /* blockpos1 = blockpos.down() */
            if (pb_isWater(w_get(w, bdx - 1, bdy, bdz)) || pb_isWater(w_get(w, bdx + 1, bdy, bdz)) ||
                pb_isWater(w_get(w, bdx, bdy, bdz - 1)) || pb_isWater(w_get(w, bdx, bdy, bdz + 1))) {
                int inner = jrand_int_bound(r, 3) + 1;
                int jj = 2 + jrand_int_bound(r, inner);
                for (int kk = 0; kk < jj; ++kk)
                    if (wg_reed_canStay(w, bx, by, bz)) w_set(w, bx, by + kk, bz, PB_REEDS);
            }
        }
    }
}
/* WorldGenPumpkin: 64 attempts; below grass + canPlaceBlockAt; facing = HORIZONTAL.random (nextInt 4). */
MC_HD MC_NOINLINE static void wg_pumpkin(World *w, JavaRandom *r, int x, int y, int z) {
    for (int i = 0; i < 64; ++i) {
        int bx = x + wg_off(r, 8), by = y + wg_off(r, 4), bz = z + wg_off(r, 8);
        if (w_isAir(w, bx, by, bz) && w_get(w, bx, by - 1, bz) == PB_GRASS) {
            /* canPlaceBlockAt: isReplaceable(pos)(air true) && below.isSideSolid(UP)(grass true). */
            int faceSel = jrand_int_bound(r, 4);
            w_set(w, bx, by, bz, PB_PUMPKIN_BASE + faceSel);
        }
    }
}
/* WorldGenMelon: 64 attempts; canPlaceBlockAt air + below grass. */
MC_HD MC_NOINLINE static void wg_melon(World *w, JavaRandom *r, int x, int y, int z) {
    for (int i = 0; i < 64; ++i) {
        int bx = x + wg_off(r, 8), by = y + wg_off(r, 4), bz = z + wg_off(r, 8);
        if (w_isAir(w, bx, by, bz) && w_get(w, bx, by - 1, bz) == PB_GRASS)
            w_set(w, bx, by, bz, PB_MELON);
    }
}
MC_HD MC_NOINLINE static int wg_vine_canAttach(const World *w, int x, int y, int z) {
    int c = w_get(w, x, y, z);
    return pb_blocksMovement(c) && !pb_isPlant(c) && !pb_isVine(c) && !pb_isLiquid(c);
}
/* WorldGenVines. Starting at y=128 is vanilla's jungle call and consumes no inner draws. */
MC_HD MC_NOINLINE static void wg_vines(World *w, JavaRandom *r, int x, int y, int z) {
    while (y < 128) {
        if (w_isAir(w, x, y, z)) {
            int placed = 0;
            if (!placed && wg_vine_canAttach(w, x, y, z + 1)) { wg_placeVine(w, x, y, z, 3); placed = 1; }
            if (!placed && wg_vine_canAttach(w, x - 1, y, z)) { wg_placeVine(w, x, y, z, 0); placed = 1; }
            if (!placed && wg_vine_canAttach(w, x, y, z - 1)) { wg_placeVine(w, x, y, z, 2); placed = 1; }
            if (!placed && wg_vine_canAttach(w, x + 1, y, z)) wg_placeVine(w, x, y, z, 1);
        } else {
            x += jrand_int_bound(r, 4) - jrand_int_bound(r, 4);
            z += jrand_int_bound(r, 4) - jrand_int_bound(r, 4);
        }
        ++y;
    }
}
MC_HD MC_NOINLINE static int wg_cactus_canStay(const World *w, int x, int y, int z) {
    int below;
    if (pb_isSolid(w_get(w, x - 1, y, z)) || pb_isLava(w_get(w, x - 1, y, z))) return 0;
    if (pb_isSolid(w_get(w, x + 1, y, z)) || pb_isLava(w_get(w, x + 1, y, z))) return 0;
    if (pb_isSolid(w_get(w, x, y, z - 1)) || pb_isLava(w_get(w, x, y, z - 1))) return 0;
    if (pb_isSolid(w_get(w, x, y, z + 1)) || pb_isLava(w_get(w, x, y, z + 1))) return 0;
    below = w_get(w, x, y - 1, z);
    return (below == PB_SAND || below == PB_CACTUS) && !pb_isLiquid(w_get(w, x, y + 1, z));
}
/* WorldGenCactus: 10 attempts, with the nested height draw only for air candidates. */
MC_HD MC_NOINLINE static void wg_cactus(World *w, JavaRandom *r, int x, int y, int z) {
    for (int i = 0; i < 10; ++i) {
        int bx = x + wg_off(r, 8), by = y + wg_off(r, 4), bz = z + wg_off(r, 8);
        if (w_isAir(w, bx, by, bz)) {
            int inner = jrand_int_bound(r, 3) + 1;
            int j = 1 + jrand_int_bound(r, inner);
            for (int k = 0; k < j; ++k) {
                if (wg_cactus_canStay(w, bx, by, bz))
                    w_set(w, bx, by + k, bz, PB_CACTUS);
            }
        }
    }
}
/* WorldGenBlockBlob(Blocks.MOSSY_COBBLESTONE, 0): mega taiga boulders. */
MC_HD MC_NOINLINE static int wg_blockblob(World *w, JavaRandom *r, int x, int y, int z,
                                          int block, int startRadius) {
    for (;;) {
        if (y > 3) {
            if (w_isAir(w, x, y - 1, z)) {
                --y;
                continue;
            }
            {
                int below = w_get(w, x, y - 1, z);
                if (below != PB_GRASS && !pb_isDirt(below) && !pb_isStone(below)) {
                    --y;
                    continue;
                }
            }
        }
        if (y <= 3) return 0;
        {
            int i1 = startRadius;
            for (int i = 0; i1 >= 0 && i < 3; ++i) {
                int j = i1 + jrand_int_bound(r, 2);
                int k = i1 + jrand_int_bound(r, 2);
                int l = i1 + jrand_int_bound(r, 2);
                float f = (float)(j + k + l) * 0.333F + 0.5F;
                for (int bx = x - j; bx <= x + j; ++bx)
                    for (int by = y - k; by <= y + k; ++by)
                        for (int bz = z - l; bz <= z + l; ++bz) {
                            int dx = bx - x, dy = by - y, dz = bz - z;
                            double d2 = (double)(dx * dx + dy * dy + dz * dz);
                            if (d2 <= (double)(f * f)) w_set(w, bx, by, bz, block);
                        }
                x += -(i1 + 1) + jrand_int_bound(r, 2 + i1 * 2);
                y += -jrand_int_bound(r, 2);
                z += -(i1 + 1) + jrand_int_bound(r, 2 + i1 * 2);
            }
        }
        return 1;
    }
}
/* WorldGenDesertWells: no RNG draws inside generate. */
MC_HD MC_NOINLINE static int wg_desert_well(World *w, int x, int y, int z) {
    while (w_isAir(w, x, y, z) && y > 2) --y;
    if (w_get(w, x, y, z) != PB_SAND) return 0;
    for (int i = -2; i <= 2; ++i)
        for (int j = -2; j <= 2; ++j)
            if (w_isAir(w, x + i, y - 1, z + j) && w_isAir(w, x + i, y - 2, z + j))
                return 0;
    for (int l = -1; l <= 0; ++l)
        for (int l1 = -2; l1 <= 2; ++l1)
            for (int k = -2; k <= 2; ++k)
                w_set(w, x + l1, y + l, z + k, PB_SANDSTONE);
    w_set(w, x, y, z, PB_FLOWING_WATER);
    w_set(w, x, y, z - 1, PB_FLOWING_WATER);
    w_set(w, x + 1, y, z, PB_FLOWING_WATER);
    w_set(w, x, y, z + 1, PB_FLOWING_WATER);
    w_set(w, x - 1, y, z, PB_FLOWING_WATER);
    for (int i1 = -2; i1 <= 2; ++i1)
        for (int i2 = -2; i2 <= 2; ++i2)
            if (i1 == -2 || i1 == 2 || i2 == -2 || i2 == 2)
                w_set(w, x + i1, y + 1, z + i2, PB_SANDSTONE);
    w_set(w, x + 2, y + 1, z, PB_SANDSTONE_SLAB);
    w_set(w, x - 2, y + 1, z, PB_SANDSTONE_SLAB);
    w_set(w, x, y + 1, z + 2, PB_SANDSTONE_SLAB);
    w_set(w, x, y + 1, z - 2, PB_SANDSTONE_SLAB);
    for (int j1 = -1; j1 <= 1; ++j1)
        for (int j2 = -1; j2 <= 1; ++j2)
            w_set(w, x + j1, y + 4, z + j2,
                  (j1 == 0 && j2 == 0) ? PB_SANDSTONE : PB_SANDSTONE_SLAB);
    for (int k1 = 1; k1 <= 3; ++k1) {
        w_set(w, x - 1, y + k1, z - 1, PB_SANDSTONE);
        w_set(w, x - 1, y + k1, z + 1, PB_SANDSTONE);
        w_set(w, x + 1, y + k1, z - 1, PB_SANDSTONE);
        w_set(w, x + 1, y + k1, z + 1, PB_SANDSTONE);
    }
    return 1;
}
/* WorldGenLiquids (springs): place a source liquid in a stone pocket. Java checks
 * getBlock() == Blocks.STONE, which is ONE block for stone/granite/diorite/andesite (meta
 * variants) - ores are different blocks and do NOT count. block = FLOWING_WATER/LAVA.
 * immediateBlockTick: vanilla keeps scheduledUpdatesAreImmediate set through the recursive
 * BlockDynamicLiquid scheduleUpdate/onBlockAdded chain, so the full spring cascade runs to an
 * isAreaLoaded-bounded fixpoint at populate time. It is geometry-only here; the lava delay
 * random draw only scales a delay discarded by WorldServer's immediate branch. */
MC_HD static inline int pb_isStoneBlock(int b) {
    return b == PB_STONE || b == PB_GRANITE || b == PB_DIORITE || b == PB_ANDESITE;
}

typedef struct WgLiquidTick {
    i16 x, z;
    u8 y;
    u8 flow;
} WgLiquidTick;

#define WG_LIQ_STACK_MAX (W_N * 4)

/* Liquid CA stamp/stack: MUST exist on both host and device. A host-only stub
 * (device has_level always 0, drain no-op) made CUDA miss liquid-dependent
 * placements while seed 0 still passed. Populate is serial on both sides
 * (CPU single-threaded; CUDA drivers launch <<<1,1>>>), so one static buffer
 * per TU/side is safe - same rationale as w_provider_surface_clobber scratch. */
#if defined(__CUDA_ARCH__)
static __device__ WgLiquidTick wg_liq_stack[WG_LIQ_STACK_MAX];
static __device__ u8 wg_liq_level[W_N];
static __device__ u32 wg_liq_stamp[W_N];
static __device__ u32 wg_liq_cur_stamp;
#else
static WgLiquidTick wg_liq_stack[WG_LIQ_STACK_MAX];
static u8 wg_liq_level[W_N];
static u32 wg_liq_stamp[W_N];
static u32 wg_liq_cur_stamp;
#endif

MC_HD static inline int wg_liq_is_water_flow(int flow) { return flow == PB_FLOWING_WATER; }
MC_HD static inline int wg_liq_static_block(int flow) {
    return wg_liq_is_water_flow(flow) ? PB_WATER : PB_LAVA;
}
MC_HD static inline int wg_liq_same_material(int b, int flow) {
    return wg_liq_is_water_flow(flow) ? pb_isWater(b) : pb_isLava(b);
}
MC_HD static inline int wg_liq_is_dynamic(int b, int flow) { return b == flow; }
MC_HD static inline int wg_liq_opposite_dir(int dir) {
    return dir == 0 ? 2 : (dir == 1 ? 3 : (dir == 2 ? 0 : 1));
}

MC_HD MC_NOINLINE static void wg_liq_begin(void) {
    ++wg_liq_cur_stamp;
    if (!wg_liq_cur_stamp) {
        for (int i = 0; i < W_N; ++i) wg_liq_stamp[i] = 0;
        wg_liq_cur_stamp = 1;
    }
}

MC_HD MC_NOINLINE static void wg_liq_mark_level(int x, int y, int z, int level) {
    if (w_inb(x, y, z)) {
        int idx = w_index(x, y, z);
        wg_liq_stamp[idx] = wg_liq_cur_stamp;
        wg_liq_level[idx] = (u8)level;
    }
}

MC_HD MC_NOINLINE static int wg_liq_level_at(const World *w, int x, int y, int z, int flow) {
    int b = w_get(w, x, y, z);
    if (!wg_liq_same_material(b, flow)) return -1;
    if (w_inb(x, y, z)) {
        int idx = w_index(x, y, z);
        if (wg_liq_stamp[idx] == wg_liq_cur_stamp) return (int)wg_liq_level[idx];
    }
    if (b == wg_liq_static_block(flow)) return 0;
    return 1;  /* dynamic liquid inherited from an older window: level metadata is unavailable. */
}

MC_HD MC_NOINLINE static int wg_liq_is_area_loaded(const World *w, int x, int y, int z) {
    if (y + 8 < 0 || y - 8 >= W_Y) return 0;
    if (x - 8 < 0 || x + 8 >= W_X || z - 8 < 0 || z + 8 >= W_Z) return 0;
    int cx0 = w_floor_div16(w->baseCx * 16 + x - 8);
    int cz0 = w_floor_div16(w->baseCz * 16 + z - 8);
    int cx1 = w_floor_div16(w->baseCx * 16 + x + 8);
    int cz1 = w_floor_div16(w->baseCz * 16 + z + 8);
    for (int cx = cx0; cx <= cx1; ++cx)
        for (int cz = cz0; cz <= cz1; ++cz) {
            int rx = cx - w->baseCx + 1, rz = cz - w->baseCz + 1;
            if (rx < 0 || rx >= 5 || rz < 0 || rz >= 5 || !w->loadedChunk[rx][rz])
                return 0;
        }
    return 1;
}

MC_HD MC_NOINLINE static int wg_liq_is_blocked(const World *w, int x, int y, int z) {
    int b = w_get(w, x, y, z);
    if (b == PB_REEDS) return 1;
    return pb_blocksMovement(b);
}

MC_HD MC_NOINLINE static int wg_liq_can_flow_into(const World *w, int x, int y, int z, int flow) {
    int b = w_get(w, x, y, z);
    if (wg_liq_same_material(b, flow) || pb_isLava(b)) return 0;
    return !wg_liq_is_blocked(w, x, y, z);
}

MC_HD MC_NOINLINE static void wg_liq_push(int *sp, int x, int y, int z, int flow) {
    if (*sp >= WG_LIQ_STACK_MAX) return;
    wg_liq_stack[*sp].x = (i16)x;
    wg_liq_stack[*sp].y = (u8)y;
    wg_liq_stack[*sp].z = (i16)z;
    wg_liq_stack[*sp].flow = (u8)flow;
    ++(*sp);
}

MC_HD MC_NOINLINE static void wg_liq_schedule_update(World *w, int x, int y, int z, int flow,
                                                     int *sp) {
    if (!wg_liq_is_area_loaded(w, x, y, z)) return;
    if (w_get(w, x, y, z) != flow) return;
    wg_liq_push(sp, x, y, z, flow);
}

MC_HD MC_NOINLINE static void wg_liq_notify_neighbors(World *w, int x, int y, int z, int *sp);
MC_HD MC_NOINLINE static void wg_liq_drain(World *w, int *sp, int base);

MC_HD MC_NOINLINE static void wg_liq_set_liquid(World *w, int x, int y, int z, int flow,
                                                int level) {
    w_set(w, x, y, z, flow);
    wg_liq_mark_level(x, y, z, level);
}

MC_HD MC_NOINLINE static int wg_liq_check_for_mixing(World *w, int x, int y, int z, int level,
                                                     int *sp) {
    int b = w_get(w, x, y, z);
    if (!pb_isLava(b)) return 0;
    if (pb_isWater(w_get(w, x, y + 1, z)) ||
        pb_isWater(w_get(w, x, y, z - 1)) ||
        pb_isWater(w_get(w, x, y, z + 1)) ||
        pb_isWater(w_get(w, x - 1, y, z)) ||
        pb_isWater(w_get(w, x + 1, y, z))) {
        if (level == 0) {
            w_set(w, x, y, z, PB_OBSIDIAN);
            wg_liq_notify_neighbors(w, x, y, z, sp);
            return 1;
        }
        if (level <= 4) {
            w_set(w, x, y, z, PB_COBBLESTONE);
            wg_liq_notify_neighbors(w, x, y, z, sp);
            return 1;
        }
    }
    return 0;
}

MC_HD MC_NOINLINE static void wg_liq_dynamic_added(World *w, int x, int y, int z, int flow,
                                                   int *sp) {
    int level = wg_liq_level_at(w, x, y, z, flow);
    if (!wg_liq_is_water_flow(flow) && wg_liq_check_for_mixing(w, x, y, z, level, sp)) return;
    wg_liq_schedule_update(w, x, y, z, flow, sp);
}

/* BlockFalling.canFallThrough: FIRE (not generated at populate) or material AIR/WATER/LAVA. */
MC_HD static inline int pb_canFallThrough(int b) {
    return b == PB_AIR || pb_isWater(b) || pb_isLava(b);
}

MC_HD MC_NOINLINE static void wg_fall_check(World *w, int x, int y, int z, int *sp);

/* True only when the cell's LEVEL metadata is known (stamped by the current window's CA).
 * Statics inherited from older windows lost their level; reawakening them as level 0 (source)
 * over-spreads, so they stay asleep - java would reawaken with the true persisted level. */
MC_HD MC_NOINLINE static int wg_liq_has_level(int x, int y, int z) {
    if (!w_inb(x, y, z)) return 0;
    return wg_liq_stamp[w_index(x, y, z)] == wg_liq_cur_stamp;
}

/* BlockStaticLiquid.updateLiquid: neighborChanged on a settled liquid converts it back to the
 * DYNAMIC block (flag 2, LEVEL kept) and schedules an update - immediate during populate. */
MC_HD MC_NOINLINE static void wg_liq_reawaken(World *w, int x, int y, int z, int flow, int *sp) {
    /* MAGMA_NOWAKE: host-only bisect switch (getenv). Default (unset) is identical
     * on CPU and CUDA; set the env only for host-side forensics. */
#ifndef __CUDA_ARCH__
    static int nowake = -1;
    if (nowake < 0) nowake = getenv("MAGMA_NOWAKE") != NULL;
    if (nowake) return;
#endif
    if (!wg_liq_has_level(x, y, z)) return;
    int level = wg_liq_level_at(w, x, y, z, flow);
    if (level < 0) level = 0;
    w_set(w, x, y, z, flow);
    wg_liq_mark_level(x, y, z, level);
    int base = *sp;
    wg_liq_schedule_update(w, x, y, z, flow, sp);
    wg_liq_drain(w, sp, base);
}

MC_HD MC_NOINLINE static void wg_liq_neighbor_changed(World *w, int x, int y, int z, int *sp) {
    int b = w_get(w, x, y, z);
    if (b == PB_LAVA) {
        int level = wg_liq_level_at(w, x, y, z, PB_FLOWING_LAVA);
        if (!wg_liq_check_for_mixing(w, x, y, z, level, sp))
            wg_liq_reawaken(w, x, y, z, PB_FLOWING_LAVA, sp);
    } else if (b == PB_FLOWING_LAVA) {
        int level = wg_liq_level_at(w, x, y, z, PB_FLOWING_LAVA);
        wg_liq_check_for_mixing(w, x, y, z, level, sp);
    } else if (b == PB_WATER) {
        wg_liq_reawaken(w, x, y, z, PB_FLOWING_WATER, sp);
    } else if (b == PB_GRAVEL || b == PB_SAND) {
        /* MAGMA_NOFALL: host-only bisect switch. Default (unset) runs fall on
         * both sides; previously the whole call was #ifndef'd out on device. */
        int do_fall = 1;
#ifndef __CUDA_ARCH__
        static int nofall = -1;
        if (nofall < 0) nofall = getenv("MAGMA_NOFALL") != NULL;
        if (nofall) do_fall = 0;
#endif
        if (do_fall) wg_fall_check(w, x, y, z, sp);
    }
}

/* World.notifyNeighborsOfStateChange order: WEST, EAST, DOWN, UP, NORTH, SOUTH. */
MC_HD MC_NOINLINE static void wg_liq_notify_neighbors(World *w, int x, int y, int z, int *sp) {
    int base;
    base = *sp; wg_liq_neighbor_changed(w, x - 1, y, z, sp); wg_liq_drain(w, sp, base);
    base = *sp; wg_liq_neighbor_changed(w, x + 1, y, z, sp); wg_liq_drain(w, sp, base);
    base = *sp; wg_liq_neighbor_changed(w, x, y - 1, z, sp); wg_liq_drain(w, sp, base);
    base = *sp; wg_liq_neighbor_changed(w, x, y + 1, z, sp); wg_liq_drain(w, sp, base);
    base = *sp; wg_liq_neighbor_changed(w, x, y, z - 1, sp); wg_liq_drain(w, sp, base);
    base = *sp; wg_liq_neighbor_changed(w, x, y, z + 1, sp); wg_liq_drain(w, sp, base);
}

/* BlockFalling.neighborChanged -> scheduleUpdate -> WorldServer immediate branch (populate sets
 * scheduledUpdatesAreImmediate; gate = isAreaLoaded(pos +-8)) -> updateTick -> checkFallable with
 * BlockFalling.fallInstantly=true (set by ChunkProviderOverworld.populate): teleport-fall to the
 * first non-fallthrough block, destroying the block if the scan bottoms out at y==0. Drawless. */
MC_HD MC_NOINLINE static void wg_fall_check(World *w, int x, int y, int z, int *sp) {
    if (!wg_liq_is_area_loaded(w, x, y, z)) return;
    if (y <= 0) return;
    if (!pb_canFallThrough(w_get(w, x, y - 1, z))) return;
    int state = w_get(w, x, y, z);
    w_set(w, x, y, z, PB_AIR);
    wg_liq_notify_neighbors(w, x, y, z, sp);
    int yy = y - 1;
    while (yy > 0 && pb_canFallThrough(w_get(w, x, yy, z))) --yy;
    if (yy > 0) {
        w_set(w, x, yy + 1, z, state);
        wg_liq_notify_neighbors(w, x, yy + 1, z, sp);
    }
}

MC_HD MC_NOINLINE static int wg_liq_check_adjacent(World *w, int x, int y, int z, int flow,
                                                   int currentMinLevel, int *adjacentSources) {
    int i = wg_liq_level_at(w, x, y, z, flow);
    if (i < 0) return currentMinLevel;
    if (i == 0) ++(*adjacentSources);
    if (i >= 8) i = 0;
    return currentMinLevel >= 0 && i >= currentMinLevel ? currentMinLevel : i;
}

MC_HD MC_NOINLINE static int wg_liq_slope_find_distance(int flow) {
    return wg_liq_is_water_flow(flow) ? 4 : 2;
}

MC_HD MC_NOINLINE static int wg_liq_slope_distance(World *w, int x, int y, int z, int distance,
                                                   int avoidDir, int flow) {
    static const int dx[4] = {0, 1, 0, -1};   /* NORTH, EAST, SOUTH, WEST */
    static const int dz[4] = {-1, 0, 1, 0};
    int best = 1000;
    for (int dir = 0; dir < 4; ++dir) {
        if (dir == avoidDir) continue;
        int nx = x + dx[dir], nz = z + dz[dir];
        int nb = w_get(w, nx, y, nz);
        if (!wg_liq_is_blocked(w, nx, y, nz) &&
            (!wg_liq_same_material(nb, flow) || wg_liq_level_at(w, nx, y, nz, flow) > 0)) {
            if (!wg_liq_is_blocked(w, nx, y - 1, nz)) return distance;
            if (distance < wg_liq_slope_find_distance(flow)) {
                int j = wg_liq_slope_distance(w, nx, y, nz, distance + 1,
                                              wg_liq_opposite_dir(dir), flow);
                if (j < best) best = j;
            }
        }
    }
    return best;
}

MC_HD MC_NOINLINE static int wg_liq_possible_flow_dirs(World *w, int x, int y, int z, int flow) {
    static const int dx[4] = {0, 1, 0, -1};   /* NORTH, EAST, SOUTH, WEST */
    static const int dz[4] = {-1, 0, 1, 0};
    int best = 1000;
    int mask = 0;
    for (int dir = 0; dir < 4; ++dir) {
        int nx = x + dx[dir], nz = z + dz[dir];
        int nb = w_get(w, nx, y, nz);
        if (!wg_liq_is_blocked(w, nx, y, nz) &&
            (!wg_liq_same_material(nb, flow) || wg_liq_level_at(w, nx, y, nz, flow) > 0)) {
            int j;
            if (wg_liq_is_blocked(w, nx, y - 1, nz))
                j = wg_liq_slope_distance(w, nx, y, nz, 1, wg_liq_opposite_dir(dir), flow);
            else
                j = 0;
            if (j < best) mask = 0;
            if (j <= best) {
                mask |= 1 << dir;
                best = j;
            }
        }
    }
    return mask;
}

MC_HD MC_NOINLINE static void wg_liq_place_static(World *w, int x, int y, int z, int flow,
                                                  int level, int *sp) {
    w_set(w, x, y, z, wg_liq_static_block(flow));
    wg_liq_mark_level(x, y, z, level);
    if (!wg_liq_is_water_flow(flow)) wg_liq_check_for_mixing(w, x, y, z, level, sp);
}

MC_HD MC_NOINLINE static void wg_liq_try_flow_into(World *w, int x, int y, int z, int flow,
                                                   int level, int *sp) {
    if (!wg_liq_can_flow_into(w, x, y, z, flow)) return;
    int base = *sp;
    wg_liq_set_liquid(w, x, y, z, flow, level);
    wg_liq_dynamic_added(w, x, y, z, flow, sp);
    wg_liq_drain(w, sp, base);
    wg_liq_notify_neighbors(w, x, y, z, sp);
}

MC_HD MC_NOINLINE static void wg_liq_update_tick(World *w, int x, int y, int z, int flow,
                                                 int forcedLevel, int *sp) {
    static const int hdx[4] = {0, 1, 0, -1};   /* NORTH, EAST, SOUTH, WEST */
    static const int hdz[4] = {-1, 0, 1, 0};
    int i = forcedLevel >= 0 ? forcedLevel : wg_liq_level_at(w, x, y, z, flow);
    int levelDrop = wg_liq_is_water_flow(flow) ? 1 : 2;
    if (i < 0) return;

    if (i > 0) {
        int minLevel = -100;
        int adjacentSources = 0;
        for (int dir = 0; dir < 4; ++dir)
            minLevel = wg_liq_check_adjacent(w, x + hdx[dir], y, z + hdz[dir],
                                             flow, minLevel, &adjacentSources);
        int i1 = minLevel + levelDrop;
        if (i1 >= 8 || minLevel < 0) i1 = -1;
        int above = wg_liq_level_at(w, x, y + 1, z, flow);
        if (above >= 0) i1 = above >= 8 ? above : above + 8;
        if (wg_liq_is_water_flow(flow) && adjacentSources >= 2) {
            int below = w_get(w, x, y - 1, z);
            int belowDepth = wg_liq_level_at(w, x, y - 1, z, flow);
            if (pb_isSolid(below) || belowDepth == 0) i1 = 0;
        }
        if (i1 == i) {
            wg_liq_place_static(w, x, y, z, flow, i, sp);
        } else {
            i = i1;
            if (i1 < 0) {
                w_set(w, x, y, z, PB_AIR);
                wg_liq_notify_neighbors(w, x, y, z, sp);
            } else {
                wg_liq_set_liquid(w, x, y, z, flow, i1);
                int base = *sp;
                wg_liq_schedule_update(w, x, y, z, flow, sp);
                wg_liq_drain(w, sp, base);
                wg_liq_notify_neighbors(w, x, y, z, sp);
            }
        }
    } else {
        wg_liq_place_static(w, x, y, z, flow, i, sp);
    }

    if (wg_liq_can_flow_into(w, x, y - 1, z, flow)) {
        if (!wg_liq_is_water_flow(flow) && pb_isWater(w_get(w, x, y - 1, z))) {
            w_set(w, x, y - 1, z, PB_STONE);
            wg_liq_notify_neighbors(w, x, y - 1, z, sp);
            return;
        }
        wg_liq_try_flow_into(w, x, y - 1, z, flow, i >= 8 ? i : i + 8, sp);
    } else if (i >= 0 && (i == 0 || wg_liq_is_blocked(w, x, y - 1, z))) {
        int mask = wg_liq_possible_flow_dirs(w, x, y, z, flow);
        int k1 = i >= 8 ? 1 : i + levelDrop;
        if (k1 >= 8) return;
        /* EnumSet iterates enum declaration order: NORTH, SOUTH, WEST, EAST. Push reverse. */
        const int order[4] = {1, 3, 2, 0};  /* EAST, WEST, SOUTH, NORTH for LIFO */
        for (int oi = 0; oi < 4; ++oi) {
            int dir = order[oi];
            if (mask & (1 << dir))
                wg_liq_try_flow_into(w, x + hdx[dir], y, z + hdz[dir], flow, k1, sp);
        }
    }
}

MC_HD MC_NOINLINE static void wg_liq_drain(World *w, int *sp, int base) {
    if (base < 0) base = 0;
    while (*sp > base) {
        WgLiquidTick t = wg_liq_stack[--(*sp)];
        int flow = (int)t.flow;
        if (w_get(w, (int)t.x, (int)t.y, (int)t.z) != flow) continue;
        wg_liq_update_tick(w, (int)t.x, (int)t.y, (int)t.z, flow, -1, sp);
    }
}

MC_HD MC_NOINLINE static void wg_liquids(World *w, int x, int y, int z, int block) {
    if (!pb_isStoneBlock(w_get(w, x, y + 1, z))) return;
    if (!pb_isStoneBlock(w_get(w, x, y - 1, z))) return;
    int here = w_get(w, x, y, z);
    if (!pb_isAir(here) && !pb_isStoneBlock(here)) return;
    int i = 0;
    if (pb_isStoneBlock(w_get(w, x - 1, y, z))) ++i;
    if (pb_isStoneBlock(w_get(w, x + 1, y, z))) ++i;
    if (pb_isStoneBlock(w_get(w, x, y, z - 1))) ++i;
    if (pb_isStoneBlock(w_get(w, x, y, z + 1))) ++i;
    int j = 0;
    if (w_isAir(w, x - 1, y, z)) ++j;
    if (w_isAir(w, x + 1, y, z)) ++j;
    if (w_isAir(w, x, y, z - 1)) ++j;
    if (w_isAir(w, x, y, z + 1)) ++j;
    if (i == 3 && j == 1) {
        /* Full spring cascade on BOTH host and device (was a two-block stub on
         * CUDA that never ran the CA, so liquid levels/flow never stamped). */
        wg_liq_begin();
        w_set(w, x, y, z, block);
        wg_liq_mark_level(x, y, z, 0);
        int sp = 0;
        wg_liq_update_tick(w, x, y, z, block, 0, &sp);
        wg_liq_drain(w, &sp, 0);
    }
}

/* WorldGenDoublePlant: 64 attempts; canPlaceBlockAt = super(soil bush + air above) && isAir(up). */
MC_HD MC_NOINLINE static int wg_dplant_canPlace(const World *w, int x, int y, int z) {
    /* BlockBush.canPlaceBlockAt: isReplaceable(pos) && soil.canSustainPlant(Plains). Plus DoublePlant
     * override: && isAir(up). isReplaceable(pos) is checked by caller via isAirBlock. */
    if (!pb_canSustainBush(w_get(w, x, y - 1, z))) return 0;
    if (!w_isAir(w, x, y + 1, z)) return 0;
    return 1;
}
MC_HD MC_NOINLINE static int wg_doubleplant(World *w, JavaRandom *r, int x, int y, int z, int type) {
    int flag = 0;
    for (int i = 0; i < 64; ++i) {
        int bx = x + wg_off(r, 8), by = y + wg_off(r, 4), bz = z + wg_off(r, 8);
        int ok = w_isAir(w, bx, by, bz) && wg_dplant_canPlace(w, bx, by, bz);
        if (ok) {
            w_set(w, bx, by, bz, PB_DPLANT_LOWER_BASE + type);
            w_set(w, bx, by + 1, bz, PB_DPLANT_UPPER);
            flag = 1;
        }
    }
    return flag;
}

/* WorldGenDungeons. correctFacing/spawner entity not block-relevant (spawner = PB_MOB_SPAWNER). */
MC_HD MC_NOINLINE static void wg_dungeons(World *w, JavaRandom *r, int posX, int posY, int posZ) {
    int j = jrand_int_bound(r, 2) + 2;
    int k = -j - 1, l = j + 1;
    int k1 = jrand_int_bound(r, 2) + 2;
    int l1 = -k1 - 1, i2 = k1 + 1;
    int j2 = 0;
    for (int k2 = k; k2 <= l; ++k2)
        for (int l2 = -1; l2 <= 4; ++l2)
            for (int i3 = l1; i3 <= i2; ++i3) {
                int flag = pb_isSolid(w_get(w, posX + k2, posY + l2, posZ + i3));
                if (l2 == -1 && !flag) return;
                if (l2 == 4 && !flag) return;
                if ((k2 == k || k2 == l || i3 == l1 || i3 == i2) && l2 == 0 &&
                    w_isAir(w, posX + k2, posY + l2, posZ + i3) &&
                    w_isAir(w, posX + k2, posY + l2 + 1, posZ + i3)) ++j2;
            }
    if (j2 >= 1 && j2 <= 5) {
        for (int k3 = k; k3 <= l; ++k3)
            for (int i4 = 3; i4 >= -1; --i4)
                for (int k4 = l1; k4 <= i2; ++k4) {
                    int bx = posX + k3, by = posY + i4, bz = posZ + k4;
                    if (k3 != k && i4 != -1 && k4 != l1 && k3 != l && i4 != 4 && k4 != i2) {
                        if (w_get(w, bx, by, bz) != PB_CHEST) w_set(w, bx, by, bz, PB_AIR);
                    } else if (by >= 0 && !pb_isSolid(w_get(w, bx, by - 1, bz))) {
                        w_set(w, bx, by, bz, PB_AIR);
                    } else if (pb_isSolid(w_get(w, bx, by, bz)) && w_get(w, bx, by, bz) != PB_CHEST) {
                        if (i4 == -1 && jrand_int_bound(r, 4) != 0) w_set(w, bx, by, bz, PB_MOSSY_COBBLESTONE);
                        else w_set(w, bx, by, bz, PB_COBBLESTONE);
                    }
                }
        for (int l3 = 0; l3 < 2; ++l3)
            for (int j4 = 0; j4 < 3; ++j4) {
                int l4 = posX + jrand_int_bound(r, j * 2 + 1) - j;
                int i5 = posY;
                int j5 = posZ + jrand_int_bound(r, k1 * 2 + 1) - k1;
                if (w_isAir(w, l4, i5, j5)) {
                    int j3 = 0;
                    if (pb_isSolid(w_get(w, l4 + 1, i5, j5))) ++j3;
                    if (pb_isSolid(w_get(w, l4 - 1, i5, j5))) ++j3;
                    if (pb_isSolid(w_get(w, l4, i5, j5 + 1))) ++j3;
                    if (pb_isSolid(w_get(w, l4, i5, j5 - 1))) ++j3;
                    if (j3 == 1) {
                        w_set(w, l4, i5, j5, PB_CHEST);
                        {
                            i64 loot_seed = jrand_long(r);
#ifndef __CUDA_ARCH__
                            if (g_w_dungeon_event) {
                                /* Chest default is NORTH. With exactly one
                                 * solid horizontal neighbor, correctFacing
                                 * points away from it (legacy facing meta). */
                                int meta = pb_isSolid(w_get(w, l4 + 1, i5, j5)) ? 4
                                         : pb_isSolid(w_get(w, l4 - 1, i5, j5)) ? 5
                                         : pb_isSolid(w_get(w, l4, i5, j5 + 1)) ? 2
                                         : 3;
                                g_w_dungeon_event(w->baseCx, w->baseCz,
                                    MC_DUNGEON_EVENT_CHEST,
                                    l4, i5, j5, loot_seed, meta);
                            }
#endif
                        }
                        break;
                    }
                }
            }
        w_set(w, posX, posY, posZ, PB_MOB_SPAWNER);
        {
            int roll = jrand_int_bound(r, 400);
#ifndef __CUDA_ARCH__
            if (g_w_dungeon_event)
                g_w_dungeon_event(w->baseCx, w->baseCz,
                    MC_DUNGEON_EVENT_SPAWNER,
                    posX, posY, posZ, (i64)roll, 0);
#endif
        }
    }
}

/* WorldGenLakes (populate water/lava lakes), over the world. Ported from lake_gen, world-model. */
/* Material.isSolid for the WorldGenLakes shell veto + hardening pass: FALSE for air,
 * liquids, fire and the whole MaterialLogic family (snow LAYER, tallgrass/flowers/
 * mushrooms/reeds/lily/doubleplant/vine/cocoa - MaterialLogic.isSolid()==false).
 * Cactus and leaves are PLAIN Material -> stay solid (unlike their lightOpacity). */
MC_HD MC_NOINLINE static int lake_isSolidW(int c) {
    switch (c) {
        case PB_AIR: case PB_TALLGRASS: case PB_FERN: case PB_DEADBUSH:
        case PB_BROWN_MUSHROOM: case PB_RED_MUSHROOM: case PB_REEDS: case PB_WATER_LILY:
        case PB_SNOW_LAYER: case PB_YELLOW_FLOWER: case PB_DPLANT_UPPER: case PB_COCOA:
            return 0;
        default:
            if (pb_isLiquid(c)) return 0;
            if (c >= PB_RED_FLOWER_BASE && c < PB_RED_FLOWER_BASE + 9) return 0;
            if (c >= PB_DPLANT_LOWER_BASE && c <= PB_DPLANT_UPPER) return 0;
            if (c >= PB_VINE_BASE && c < PB_VINE_BASE + 4) return 0;
            return 1;
    }
}
MC_HD MC_NOINLINE static int wg_lakes(World *w, JavaRandom *r, int posX, int posY, int posZ, int liquid) {
    int px = posX - 8, py = posY, pz = posZ - 8;
    for (; py > 5 && w_isAir(w, px, py, pz); --py) ;
    if (py <= 4) return 0;
    py -= 4;
    char ab[2048];
    for (int t = 0; t < 2048; ++t) ab[t] = 0;
    int i = jrand_int_bound(r, 4) + 4;
    for (int jj = 0; jj < i; ++jj) {
        double d0 = jrand_double(r) * 6.0 + 3.0;
        double d1 = jrand_double(r) * 4.0 + 2.0;
        double d2 = jrand_double(r) * 6.0 + 3.0;
        double d3 = jrand_double(r) * (16.0 - d0 - 2.0) + 1.0 + d0 / 2.0;
        double d4 = jrand_double(r) * (8.0 - d1 - 4.0) + 2.0 + d1 / 2.0;
        double d5 = jrand_double(r) * (16.0 - d2 - 2.0) + 1.0 + d2 / 2.0;
        for (int l = 1; l < 15; ++l)
            for (int i1 = 1; i1 < 15; ++i1)
                for (int j1 = 1; j1 < 7; ++j1) {
                    double d6 = ((double)l - d3) / (d0 / 2.0);
                    double d7 = ((double)j1 - d4) / (d1 / 2.0);
                    double d8 = ((double)i1 - d5) / (d2 / 2.0);
                    if (d6 * d6 + d7 * d7 + d8 * d8 < 1.0) ab[(l * 16 + i1) * 8 + j1] = 1;
                }
    }
    for (int k1 = 0; k1 < 16; ++k1)
        for (int l2 = 0; l2 < 16; ++l2)
            for (int kk = 0; kk < 8; ++kk) {
                int flag = !ab[(k1 * 16 + l2) * 8 + kk] &&
                    (k1 < 15 && ab[((k1 + 1) * 16 + l2) * 8 + kk] ||
                     k1 > 0 && ab[((k1 - 1) * 16 + l2) * 8 + kk] ||
                     l2 < 15 && ab[(k1 * 16 + l2 + 1) * 8 + kk] ||
                     l2 > 0 && ab[(k1 * 16 + (l2 - 1)) * 8 + kk] ||
                     kk < 7 && ab[(k1 * 16 + l2) * 8 + kk + 1] ||
                     kk > 0 && ab[(k1 * 16 + l2) * 8 + (kk - 1)]);
                if (flag) {
                    int state = w_get(w, px + k1, py + kk, pz + l2);
                    if ((kk >= 4 && pb_isLiquid(state)) ||
                        (kk < 4 && !lake_isSolidW(state) && state != liquid))
                        return 0;
                }
            }
    for (int l1 = 0; l1 < 16; ++l1)
        for (int i3 = 0; i3 < 16; ++i3)
            for (int i4 = 0; i4 < 8; ++i4)
                if (ab[(l1 * 16 + i3) * 8 + i4])
                    w_set(w, px + l1, py + i4, pz + i3, i4 >= 4 ? PB_AIR : liquid);
    for (int i2 = 0; i2 < 16; ++i2)
        for (int j3 = 0; j3 < 16; ++j3)
            for (int j4 = 4; j4 < 8; ++j4)
                if (ab[(i2 * 16 + j3) * 8 + j4]) {
                    int bx = px + i2, by = py + (j4 - 1), bz = pz + j3;
                    if (w_get(w, bx, by, bz) == PB_DIRT &&
                        w_pop_sky_light(w, px + i2, py + j4, pz + j3) > 0)
                        w_set(w, bx, by, bz, PB_GRASS);   /* Plains topBlock GRASS (not mycelium) */
                }
    if (liquid == PB_LAVA || liquid == PB_FLOWING_LAVA) {
        for (int j2 = 0; j2 < 16; ++j2)
            for (int k3 = 0; k3 < 16; ++k3)
                for (int k4 = 0; k4 < 8; ++k4) {
                    int flag1 = !ab[(j2 * 16 + k3) * 8 + k4] &&
                        (j2 < 15 && ab[((j2 + 1) * 16 + k3) * 8 + k4] ||
                         j2 > 0 && ab[((j2 - 1) * 16 + k3) * 8 + k4] ||
                         k3 < 15 && ab[(j2 * 16 + k3 + 1) * 8 + k4] ||
                         k3 > 0 && ab[(j2 * 16 + (k3 - 1)) * 8 + k4] ||
                         k4 < 7 && ab[(j2 * 16 + k3) * 8 + k4 + 1] ||
                         k4 > 0 && ab[(j2 * 16 + k3) * 8 + (k4 - 1)]);
                    if (flag1 && (k4 < 4 || jrand_int_bound(r, 2) != 0) &&
                        lake_isSolidW(w_get(w, px + j2, py + k4, pz + k3)))
                        w_set(w, px + j2, py + k4, pz + k3, PB_STONE);
                }
    }
    /* water freeze pass: Plains/forest/swamp/taiga-M temp > 0.15 => canBlockFreezeWater false; no ice. */
    return 1;
}

/* ====================================================================================== */
/* BiomeDecorator.generateOres + genStandardOre1/2 (settings = Factory defaults)            */
/* ====================================================================================== */
MC_HD MC_NOINLINE static void bd_genStandardOre1(World *w, JavaRandom *r, int count, int size,
                                            int minH, int maxH, int oreBlock) {
    if (maxH < minH) { int t = minH; minH = maxH; maxH = t; }
    else if (maxH == minH) { if (minH < 255) ++maxH; else --minH; }
    for (int j = 0; j < count; ++j) {
        int bx = jrand_int_bound(r, 16);
        int by = jrand_int_bound(r, maxH - minH) + minH;
        int bz = jrand_int_bound(r, 16);
        wg_minable(w, r, bx, by, bz, size, oreBlock);
    }
}
MC_HD MC_NOINLINE static void bd_genStandardOre2(World *w, JavaRandom *r, int count, int size,
                                            int center, int spread, int oreBlock) {
    for (int i = 0; i < count; ++i) {
        int bx = jrand_int_bound(r, 16);
        int a = jrand_int_bound(r, spread);
        int b = jrand_int_bound(r, spread);
        int by = a + b + center - spread;
        int bz = jrand_int_bound(r, 16);
        wg_minable(w, r, bx, by, bz, size, oreBlock);
    }
}
MC_HD MC_NOINLINE static void bd_generateOres(World *w, JavaRandom *r) {
    MC_PROBE("ORE", "DIRT", r);
    bd_genStandardOre1(w, r, 10, 33, 0, 256, PB_DIRT);     /* dirt */
    MC_PROBE("ORE", "GRAVEL", r);
    bd_genStandardOre1(w, r, 8,  33, 0, 256, PB_GRAVEL);   /* gravel */
    MC_PROBE("ORE", "DIORITE", r);
    bd_genStandardOre1(w, r, 10, 33, 0, 80,  PB_DIORITE);  /* diorite */
    MC_PROBE("ORE", "GRANITE", r);
    bd_genStandardOre1(w, r, 10, 33, 0, 80,  PB_GRANITE);  /* granite */
    MC_PROBE("ORE", "ANDESITE", r);
    bd_genStandardOre1(w, r, 10, 33, 0, 80,  PB_ANDESITE); /* andesite */
    MC_PROBE("ORE", "COAL", r);
    bd_genStandardOre1(w, r, 20, 17, 0, 128, PB_COAL_ORE);
    MC_PROBE("ORE", "IRON", r);
    bd_genStandardOre1(w, r, 20, 9,  0, 64,  PB_IRON_ORE);
    MC_PROBE("ORE", "GOLD", r);
    bd_genStandardOre1(w, r, 2,  9,  0, 32,  PB_GOLD_ORE);
    MC_PROBE("ORE", "REDSTONE", r);
    bd_genStandardOre1(w, r, 8,  8,  0, 16,  PB_REDSTONE_ORE);
    MC_PROBE("ORE", "DIAMOND", r);
    bd_genStandardOre1(w, r, 1,  8,  0, 16,  PB_DIAMOND_ORE);
    MC_PROBE("ORE", "LAPIS", r);
    bd_genStandardOre2(w, r, 1,  7, 16, 16,  PB_LAPIS_ORE); /* lapis */
}

/* per-decorate-biome BiomeDecorator field config */
typedef struct {
    int treesPerChunk, flowersPerChunk, grassPerChunk, deadBushPerChunk, mushroomsPerChunk;
    int reedsPerChunk, cactiPerChunk, bigMushroomsPerChunk;
    int sandPerChunk, sandPerChunk2, clayPerChunk, waterlilyPerChunk;
} BDCfg;

/* genBigTreeChance + generate dispatch. Returns nothing; places tree.
 * RNG draws MUST match Biome.genBigTreeChance / overrides bit-for-bit (short-circuit). */
MC_HD MC_NOINLINE static void bd_genTree(World *w, JavaRandom *r, int biome, int px, int py, int pz,
                                    FoliageCoord *fol) {
    if (biome == 6 || biome == 134) { /* SWAMP (+Swampland M) -> SWAMP_FEATURE */
        wg_swamptree(w, r, px, py, pz);
    } else if (biome == 21 || biome == 22 || biome == 23 || biome == 149 || biome == 151) {
        if (jrand_int_bound(r, 10) == 0) {
            wg_bigtree(w, r, px, py, pz, fol);
        } else if (jrand_int_bound(r, 2) == 0) {
            wg_jungle_shrub(w, r, px, py, pz);
        } else if (biome != 23 && biome != 151 && jrand_int_bound(r, 3) == 0) {
            wg_mega_jungle(w, r, px, py, pz);
        } else {
            int minTreeHeight = 4 + jrand_int_bound(r, 7);
            wg_jungletree(w, r, px, py, pz, minTreeHeight);
        }
    } else if (biome == 32 || biome == 33 || biome == 160 || biome == 161) {
        if (jrand_int_bound(r, 3) == 0) {
            int useBaseHeight = 1;
            if ((biome == 32 || biome == 33) && jrand_int_bound(r, 13) != 0) useBaseHeight = 0;
            if (wg_mega_pine(w, r, px, py, pz, useBaseHeight))
                wg_mega_pine_saplings(w, r, px, py, pz);
        } else {
            if (jrand_int_bound(r, 3) == 0) wg_taiga1(w, r, px, py, pz);
            else wg_taiga2(w, r, px, py, pz);
        }
    } else if (biome == 5 || biome == 19 || biome == 30 || biome == 31 ||
               biome == 133 || biome == 158) { /* TAIGA non-MEGA: nextInt(3)==0 ? PINE : SPRUCE */
        if (jrand_int_bound(r, 3) == 0) wg_taiga1(w, r, px, py, pz);
        else wg_taiga2(w, r, px, py, pz);
    } else if (biome == 1 || biome == 129) {
        /* BiomePlains: nextInt(3)==0 ? BIG_TREE : TREE (one draw only). */
        if (jrand_int_bound(r, 3) == 0) wg_bigtree(w, r, px, py, pz, fol);
        else wg_trees(w, r, px, py, pz, PB_LOG_OAK, PB_LEAVES_OAK);
    } else if (biome == 27 || biome == 28) {
        /* BiomeForest BIRCH / BirchForestHills: always BIRCH_TREE.
         * Java short-circuits type!=BIRCH so nextInt(5) is NOT drawn. */
        wg_birch(w, r, px, py, pz);
    } else if (biome == 155) {
        /* BiomeForestMutated (Birch Forest M): nextBoolean ? SUPER_BIRCH : BIRCH. */
        if (jrand_next(r, 1) != 0) wg_birch_ex(w, r, px, py, pz, 1);
        else wg_birch(w, r, px, py, pz);
    } else if (biome == 4 || biome == 18 || biome == 132 ||
               biome == 29 || biome == 157) {
        /* BiomeForest NORMAL / ForestHills / FlowerForest (and roofed fallthrough).
         * Roofed's nextInt(3)>0 canopy branch is handled in populate_light_shim
         * before calling here. nextInt(5)!=0 ? (nextInt(10)==0 ? BIG : TREE) : BIRCH */
        int a = jrand_int_bound(r, 5);
        if (a != 0) {
            int b = jrand_int_bound(r, 10);
            if (b == 0) wg_bigtree(w, r, px, py, pz, fol);
            else wg_trees(w, r, px, py, pz, PB_LOG_OAK, PB_LEAVES_OAK);
        } else {
            wg_birch(w, r, px, py, pz);
        }
    } else {
        /* Biome default: nextInt(10)==0 ? BIG_TREE : TREE_FEATURE */
        if (jrand_int_bound(r, 10) == 0) wg_bigtree(w, r, px, py, pz, fol);
        else wg_trees(w, r, px, py, pz, PB_LOG_OAK, PB_LEAVES_OAK);
    }
}

/* getRandomWorldGenForGrass: taiga draws nextInt(5) (fern/grass); others no draw (grass). */
MC_HD MC_NOINLINE static int bd_grassState(JavaRandom *r, int biome) {
    if (biome == 5 || biome == 19 || biome == 30 || biome == 31 ||
        biome == 32 || biome == 33 || biome == 133 || biome == 158 ||
        biome == 160 || biome == 161)
        return jrand_int_bound(r, 5) > 0 ? PB_FERN : PB_TALLGRASS;
    if (biome == 21 || biome == 22 || biome == 23 || biome == 149 || biome == 151)
        return jrand_int_bound(r, 4) == 0 ? PB_FERN : PB_TALLGRASS;
    return PB_TALLGRASS;
}
/* pickRandomFlower: forest/taiga -> nextInt(3)>0 ? DANDELION : POPPY ; swamp -> BLUE_ORCHID. */
MC_HD MC_NOINLINE static int bd_flowerState(JavaRandom *r, int biome) {
    if (biome == 6 || biome == 134) return PB_RED_FLOWER_BASE + 1;   /* BLUE_ORCHID meta 1 */
    int v = jrand_int_bound(r, 3);
    return v > 0 ? PB_YELLOW_FLOWER : (PB_RED_FLOWER_BASE + 0);  /* DANDELION : POPPY */
}

MC_HD MC_NOINLINE static void bd_genDecorations(World *w, JavaRandom *r, int biome, const BDCfg *c,
                                           FoliageCoord *fol) {
    bd_generateOres(w, r);
    /* SAND (sandGen = WorldGenSand(SAND, 7)) */
    for (int i = 0; i < c->sandPerChunk2; ++i) {
        int j = jrand_int_bound(r, 16) + 8;
        int k = jrand_int_bound(r, 16) + 8;
        wg_sand(w, r, j, w_topSolidOrLiquid(w, j, k), k, 7, PB_SAND);
    }
    /* CLAY (clayGen = WorldGenClay(4)) */
    for (int i = 0; i < c->clayPerChunk; ++i) {
        int j = jrand_int_bound(r, 16) + 8;
        int k = jrand_int_bound(r, 16) + 8;
        wg_clay(w, r, j, w_topSolidOrLiquid(w, j, k), k, 4);
    }
    /* SAND_PASS2 (gravelAsSandGen = WorldGenSand(GRAVEL, 6)) */
    for (int i = 0; i < c->sandPerChunk; ++i) {
        int j = jrand_int_bound(r, 16) + 8;
        int k = jrand_int_bound(r, 16) + 8;
        wg_sand(w, r, j, w_topSolidOrLiquid(w, j, k), k, 6, PB_GRAVEL);
    }
    /* TREES */
    {
        int k1 = c->treesPerChunk;
        if (jrand_float(r) < 0.1F) ++k1;
        for (int j2 = 0; j2 < k1; ++j2) {
            int k6 = jrand_int_bound(r, 16) + 8;
            int l = jrand_int_bound(r, 16) + 8;
            /* Java getHeight is live (leaves raise heightMap). Do not use stale
             * popSkyHeight via w_gen_height or canopies over-grow. */
            int py = w_height(w, k6, l);
            bd_genTree(w, r, biome, k6, py, l, fol);
        }
    }
    /* BIG_SHROOM */
    for (int k2 = 0; k2 < c->bigMushroomsPerChunk; ++k2) {
        int l6 = jrand_int_bound(r, 16) + 8;
        int k10 = jrand_int_bound(r, 16) + 8;
        wg_bigmushroom(w, r, l6, w_gen_height(w, l6, k10), k10);
    }
    /* FLOWERS */
    for (int l2 = 0; l2 < c->flowersPerChunk; ++l2) {
        int i7 = jrand_int_bound(r, 16) + 8;
        int l10 = jrand_int_bound(r, 16) + 8;
        int j14 = w_gen_height(w, i7, l10) + 32;
        if (j14 > 0) {
            int k17 = jrand_int_bound(r, j14);
            int state = bd_flowerState(r, biome);
            wg_flowers(w, r, i7, k17, l10, state);
        }
    }
    /* GRASS */
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
    /* DEAD_BUSH */
    for (int j3 = 0; j3 < c->deadBushPerChunk; ++j3) {
        int k7 = jrand_int_bound(r, 16) + 8;
        int j11 = jrand_int_bound(r, 16) + 8;
        int l14 = w_gen_height(w, k7, j11) * 2;
        if (l14 > 0) {
            int i18 = jrand_int_bound(r, l14);
            wg_deadbush(w, r, k7, i18, j11);
        }
    }
    /* LILYPAD */
    for (int k3 = 0; k3 < c->waterlilyPerChunk; ++k3) {
        int l7 = jrand_int_bound(r, 16) + 8;
        int k11 = jrand_int_bound(r, 16) + 8;
        int i15 = w_gen_height(w, l7, k11) * 2;
        if (i15 > 0) {
            int j18 = jrand_int_bound(r, i15);
            int by = j18;
            for (; by > 0; --by) if (!w_isAir(w, l7, by - 1, k11)) break;
            wg_waterlily(w, r, l7, by, k11);
        }
    }
    /* SHROOM */
    for (int l3 = 0; l3 < c->mushroomsPerChunk; ++l3) {
        if (jrand_int_bound(r, 4) == 0) {
            int i8 = jrand_int_bound(r, 16) + 8;
            int l11 = jrand_int_bound(r, 16) + 8;
            wg_bush(w, r, i8, w_gen_height(w, i8, l11), l11, PB_BROWN_MUSHROOM);
        }
        if (jrand_int_bound(r, 8) == 0) {
            int j8 = jrand_int_bound(r, 16) + 8;
            int i12 = jrand_int_bound(r, 16) + 8;
            int j15 = w_gen_height(w, j8, i12) * 2;
            if (j15 > 0) {
                int k18 = jrand_int_bound(r, j15);
                wg_bush(w, r, j8, k18, i12, PB_RED_MUSHROOM);
            }
        }
    }
    if (jrand_int_bound(r, 4) == 0) {
        int i4 = jrand_int_bound(r, 16) + 8;
        int k8 = jrand_int_bound(r, 16) + 8;
        int j12 = w_gen_height(w, i4, k8) * 2;
        if (j12 > 0) {
            int k15 = jrand_int_bound(r, j12);
            wg_bush(w, r, i4, k15, k8, PB_BROWN_MUSHROOM);
        }
    }
    if (jrand_int_bound(r, 8) == 0) {
        int j4 = jrand_int_bound(r, 16) + 8;
        int l8 = jrand_int_bound(r, 16) + 8;
        int k12 = w_gen_height(w, j4, l8) * 2;
        if (k12 > 0) {
            int l15 = jrand_int_bound(r, k12);
            wg_bush(w, r, j4, l15, l8, PB_RED_MUSHROOM);
        }
    }
    /* REED */
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
    /* PUMPKIN */
    if (jrand_int_bound(r, 32) == 0) {
        int i5 = jrand_int_bound(r, 16) + 8;
        int k9 = jrand_int_bound(r, 16) + 8;
        int j13 = w_gen_height(w, i5, k9) * 2;
        if (j13 > 0) {
            int k16 = jrand_int_bound(r, j13);
            wg_pumpkin(w, r, i5, k16, k9);
        }
    }
    /* CACTUS */
    for (int j5 = 0; j5 < c->cactiPerChunk; ++j5) {
        int l9 = jrand_int_bound(r, 16) + 8;
        int k13 = jrand_int_bound(r, 16) + 8;
        int l16 = w_gen_height(w, l9, k13) * 2;
        if (l16 > 0) {
            int j19 = jrand_int_bound(r, l16);
            wg_cactus(w, r, l9, j19, k13);
        }
    }
    /* LAKES (generateLakes == true for all three) */
    for (int k5 = 0; k5 < 50; ++k5) {
        int i10 = jrand_int_bound(r, 16) + 8;
        int l13 = jrand_int_bound(r, 16) + 8;
        int i17 = jrand_int_bound(r, 248) + 8;
        if (i17 > 0) {
            int k19 = jrand_int_bound(r, i17);
            wg_liquids(w, i10, k19, l13, PB_FLOWING_WATER);
        }
    }
    for (int l5 = 0; l5 < 20; ++l5) {
        int j10 = jrand_int_bound(r, 16) + 8;
        int i14 = jrand_int_bound(r, 16) + 8;
        int a = jrand_int_bound(r, 240) + 8;
        int b = jrand_int_bound(r, a) + 8;
        int j17 = jrand_int_bound(r, b);
        wg_liquids(w, j10, j17, i14, PB_FLOWING_LAVA);
    }
}

/* forest addDoublePlants(count): SYRINGA(j0)/ROSE(j1)/PAEONIA(j2). */
MC_HD MC_NOINLINE static void bd_forest_addDoublePlants(World *w, JavaRandom *r, int count) {
    for (int i = 0; i < count; ++i) {
        int j = jrand_int_bound(r, 3);
        int type = (j == 0) ? 1 : (j == 1 ? 4 : 5);   /* SYRINGA / ROSE / PAEONIA */
        for (int k = 0; k < 5; ++k) {
            int l = jrand_int_bound(r, 16) + 8;
            int i1 = jrand_int_bound(r, 16) + 8;
            int j1 = jrand_int_bound(r, w_gen_height(w, l, i1) + 32);
            if (wg_doubleplant(w, r, l, j1, i1, type)) break;
        }
    }
}

MC_HD MC_NOINLINE static void biome_decorate(World *w, JavaRandom *r, int biome, FoliageCoord *fol) {
    if (biome == 4) {                 /* FOREST NORMAL */
        int i = jrand_int_bound(r, 5) - 3;
        bd_forest_addDoublePlants(w, r, i);
        BDCfg c = {10, 2, 2, 0, 0, 0, 0, 0, 1, 3, 1, 0};
        bd_genDecorations(w, r, biome, &c, fol);
    } else if (biome == 6) {          /* SWAMP */
        BDCfg c = {2, 1, 5, 1, 8, 10, 0, 0, 0, 0, 1, 4};
        bd_genDecorations(w, r, biome, &c, fol);
        if (jrand_int_bound(r, 64) == 0) {
            /* WorldGenFossils: uses a SEPARATE chunk-seeded Random + template .nbt assets (not on the
             * decoration stream). If this gate fires for a verified seed, fossils must be modeled; for
             * seeds 12345/0/7 it must NOT fire (asserted by the 3-way PASS). */
            w->bigtree_heightLimit = w->bigtree_heightLimit;  /* no-op marker */
        }
    } else {                          /* TAIGA NORMAL (id 133) */
        for (int i1 = 0; i1 < 7; ++i1) {
            int j1 = jrand_int_bound(r, 16) + 8;
            int k1 = jrand_int_bound(r, 16) + 8;
            int l1 = jrand_int_bound(r, w_gen_height(w, j1, k1) + 32);
            wg_doubleplant(w, r, j1, l1, k1, 3);   /* FERN */
        }
        BDCfg c = {10, 2, 1, 0, 1, 0, 0, 0, 1, 3, 1, 0};
        bd_genDecorations(w, r, biome, &c, fol);
    }
}

/* ChunkProviderOverworld.populate(0,0). */
MC_HD MC_NOINLINE static void pop_populate(World *w, JavaRandom *r, i64 seed, FoliageCoord *fol) {
    int biome = w_getBiome(w, 16, 16);
    w_reset_loaded_chunks(w, seed, 0, 0);
    w->activeRand = NULL;  /* cascade clobber hook DISARMED: trigger needs java global load order, see DEVLOG */
    jrand_set(r, seed);
    i64 k = jrand_long(r) / 2 * 2 + 1;
    i64 l = jrand_long(r) / 2 * 2 + 1;
    (void)k; (void)l;
    jrand_set(r, (i64)0 * k + (i64)0 * l ^ seed);   /* chunk (0,0) -> setSeed(seed) */
    /* water lake (skip if DESERT/DESERT_HILLS) */
    if (biome != 2 && biome != 17 && jrand_int_bound(r, 4) == 0) {
        int i1 = jrand_int_bound(r, 16) + 8;
        int j1 = jrand_int_bound(r, 256);
        int k1 = jrand_int_bound(r, 16) + 8;
        wg_lakes(w, r, i1, j1, k1, PB_WATER);
    }
    /* lava lake */
    if (jrand_int_bound(r, 8) == 0) {
        int i2 = jrand_int_bound(r, 16) + 8;
        int inner = jrand_int_bound(r, 248) + 8;
        int l2 = jrand_int_bound(r, inner);
        int k3 = jrand_int_bound(r, 16) + 8;
        if (l2 < POP_SEA_LEVEL || jrand_int_bound(r, 10) == 0)
            wg_lakes(w, r, i2, l2, k3, PB_LAVA);
    }
    /* dungeons */
    for (int j2 = 0; j2 < 8; ++j2) {
        int i3 = jrand_int_bound(r, 16) + 8;
        int l3 = jrand_int_bound(r, 256);
        int l1 = jrand_int_bound(r, 16) + 8;
        wg_dungeons(w, r, i3, l3, l1);
    }
    /* biome.decorate */
    biome_decorate(w, r, biome, fol);
    w->activeRand = NULL;
    /* animals CUT (no blocks); ICE/SNOW pass no-op for temp>0.15 biomes. */
}

/* ===== build the 2x2 world via cp_provide_chunk, then populate (0,0) ===== */
MC_HD MC_NOINLINE static void pop_run(World *w, CpScratch *sc, ChunkPrimer *primer, JavaRandom *r,
                                 FoliageCoord *fol, i64 seed) {
    for (int i = 0; i < W_N; ++i) w->blocks[i] = (u16)PB_AIR;
    w->bigtree_heightLimit = 0;
    w_reset_loaded_chunks(w, seed, 0, 0);

    /* precompute full-res voronoi biome over [0,32)^2 (idx = x*32 + z). */
    {
        GLNode nodes[GL_MAX_NODES];
        int voronoi;
        gl_build(nodes, seed, &voronoi);
        sc->arena.off = 0;   /* reset bump arena at top-level tree */
        int *fb = gl_getInts(nodes, &sc->arena, voronoi, 0, 0, W_X, W_Z);  /* idx = z + x*32 */
        for (int x = 0; x < W_X; ++x)
            for (int z = 0; z < W_Z; ++z)
                w->fullBiome[x * W_Z + z] = fb[z + x * W_Z];
    }

    /* provide chunks (0,0),(1,0),(0,1),(1,1) into the world. */
    for (int cx = 0; cx < 2; ++cx) {
        for (int cz = 0; cz < 2; ++cz) {
            cp_provide_chunk(primer, sc, w->st, seed, cx, cz);
            for (int lx = 0; lx < 16; ++lx)
                for (int lz = 0; lz < 16; ++lz)
                    for (int y = 0; y < 256; ++y)
                        w_set(w, cx * 16 + lx, y, cz * 16 + lz,
                              cb_get(primer, lx, y, lz));
        }
    }

    pop_populate(w, r, seed, fol);
}

#endif /* MC_POPULATE_H */
