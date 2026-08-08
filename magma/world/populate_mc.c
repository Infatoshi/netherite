/* world/populate_mc.c - see world/populate_mc.h.
 *
 * View-distance decoration overlay. Wraps blaze's position-parametrized populate
 * (`owr_run`, core/overworld_region.h) to decorate ANY chunk, caching each base
 * chunk's populate window and applying the four contributing windows to a chunk.
 * Only this translation unit pulls in the heavy worldgen apparatus, keeping
 * world/light.c lean. */
#include "world/populate_mc.h"
#include "game/caps.h"          /* CrCaps: toroidal owr-pool geometry + cell cap */
#include "core/config.h"       /* cr_cfg()->debug_caps */
#include "game/village_live.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <stdlib.h>

/* populate.h / overworld_region.h are big headers of `static` (MC_NOINLINE)
 * worldgen functions; most go unused in this TU. Silence the noise, not real
 * diagnostics. overworld_region.h is the blaze VERIFIED, read-only source of the
 * parametrized populate (owr_run) - do NOT edit it. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "overworld_region.h"  /* blaze: World, owr_run, st_run, PB_*, pb_opacity, cb_get, w_index */
#pragma GCC diagnostic pop

static CpScratch *g_sc;
enum { POPMC_VILLAGE_CACHE = 128 };
typedef struct {
    long long seed;
    int cx, cz, valid;
    GmVillage village;
} PopmcVillageStart;
static PopmcVillageStart g_villages_cache[POPMC_VILLAGE_CACHE];
static int g_villages_enabled;
static void village_capture_chest(void *opaque, int x, int y, int z,
                                  int facing_meta, long long loot_seed);
static void village_capture_resident(void *opaque, int x, int y, int z,
                                     int profession, int zombie_infested);

typedef struct {
    World *world;
    int min_x, max_x, min_z, max_z;
} PopmcVillageAccess;

static unsigned long long village_cache_hash(long long seed, int cx, int cz) {
    unsigned long long value = (unsigned long long)seed;
    value ^= (unsigned long long)(unsigned int)cx * 0x9e3779b185ebca87ULL;
    value ^= (unsigned long long)(unsigned int)cz * 0xc2b2ae3d27d4eb4fULL;
    return value ^ (value >> 29);
}

static PopmcVillageStart *village_start(long long seed, int cx, int cz,
                                        int biome_type) {
    unsigned slot = (unsigned)village_cache_hash(seed, cx, cz)
        & (POPMC_VILLAGE_CACHE - 1);
    for (int probe = 0; probe < POPMC_VILLAGE_CACHE; ++probe) {
        PopmcVillageStart *entry = &g_villages_cache[slot];
        if (!entry->valid) {
            memset(entry, 0, sizeof *entry);
            entry->valid = 1; entry->seed = seed;
            entry->cx = cx; entry->cz = cz;
            if (!gm_village_build_for_world(seed, cx, cz, biome_type, 0,
                                            &entry->village))
                entry->village.count = 0;
            return entry;
        }
        if (entry->seed == seed && entry->cx == cx && entry->cz == cz)
            return entry;
        slot = (slot + 1) & (POPMC_VILLAGE_CACHE - 1);
    }
    fprintf(stderr, "[populate_mc] FATAL: village start cache full\n");
    abort();
}

static u16 village_get(void *opaque, int x, int y, int z) {
    PopmcVillageAccess *access = (PopmcVillageAccess *)opaque;
    int lx = x - access->world->baseCx * 16;
    int lz = z - access->world->baseCz * 16;
    int block = w_get(access->world, lx, y, lz);
    if (block == PB_GRASS) return (u16)(2 << 4);
    if (block == PB_DIRT) return (u16)(3 << 4);
    if (block == PB_GRAVEL) return (u16)(13 << 4);
    if (block == PB_SAND) return (u16)(12 << 4);
    if (block == PB_RED_SANDSTONE) return (u16)(179 << 4);
    return owr_sd_state_from_pb(block);
}

static void village_set(void *opaque, int x, int y, int z, u16 state) {
    PopmcVillageAccess *access = (PopmcVillageAccess *)opaque;
    int lx = x - access->world->baseCx * 16;
    int lz = z - access->world->baseCz * 16;
    w_set(access->world, lx, y, lz, owr_sd_pb_from_state(state));
}

static int village_contains(void *opaque, int x, int y, int z) {
    PopmcVillageAccess *access = (PopmcVillageAccess *)opaque;
    return y >= 0 && y < W_Y && x >= access->min_x && x <= access->max_x
        && z >= access->min_z && z <= access->max_z;
}

static int village_top(void *opaque, int x, int z) {
    PopmcVillageAccess *access = (PopmcVillageAccess *)opaque;
    return w_topSolidOrLiquid(access->world,
        x - access->world->baseCx * 16,
        z - access->world->baseCz * 16);
}

static int village_floor_div(int value, int divisor) {
    int quotient = value / divisor, remainder = value % divisor;
    return remainder && ((remainder < 0) != (divisor < 0))
        ? quotient - 1 : quotient;
}

static int village_biome_type(long long seed, int cx, int cz) {
    GLNode nodes[GL_MAX_NODES];
    int voronoi;
    gl_build(nodes, (i64)seed, &voronoi);
    g_sc->arena.off = 0;
    int biome = gl_getInts(nodes, &g_sc->arena, voronoi,
                           cx * 16 + 8, cz * 16 + 8, 1, 1)[0];
    if (biome == B_PLAINS) return GM_VILLAGE_PLAINS;
    if (biome == B_DESERT) return GM_VILLAGE_DESERT;
    if (biome == B_SAVANNA) return GM_VILLAGE_SAVANNA;
    if (biome == B_TAIGA) return GM_VILLAGE_TAIGA;
    return -1;
}

static void village_population_hook(World *world, JavaRandom *random,
                                    i64 seed, int bcx, int bcz) {
    PopmcVillageAccess context = {
        world, bcx * 16 + 8, bcx * 16 + 23,
        bcz * 16 + 8, bcz * 16 + 23
    };
    GmVillageAccess access = {
        &context, village_get, village_set, village_contains, village_top,
        village_capture_chest, village_capture_resident
    };
    int min_rx = village_floor_div(bcx - 8, 32);
    int max_rx = village_floor_div(bcx + 8, 32);
    int min_rz = village_floor_div(bcz - 8, 32);
    int max_rz = village_floor_div(bcz + 8, 32);
    for (int rx = min_rx; rx <= max_rx; ++rx)
        for (int rz = min_rz; rz <= max_rz; ++rz) {
            int start_cx, start_cz;
            gm_village_candidate_for_region(seed, rx, rz,
                                             &start_cx, &start_cz);
            int biome_type = village_biome_type(seed, start_cx, start_cz);
            if (biome_type < 0) continue;
            PopmcVillageStart *start = village_start(
                seed, start_cx, start_cz, biome_type);
            GmVillage *village = &start->village;
            if (!village->valid) continue;
            for (int i = 0; i < village->count; ++i) {
                GmVillagePiece *piece = &village->pieces[i];
                if (piece->box.max_x < context.min_x
                        || piece->box.min_x > context.max_x
                        || piece->box.max_z < context.min_z
                        || piece->box.min_z > context.max_z)
                    continue;
                (void)gm_village_place_piece(&access, piece, biome_type,
                                             village->zombie_infested, random);
            }
        }
}

/* W_X/W_Y/W_Z/W_N come from populate.h (32,256,32). CB_INDEX must match world/light.c. */
#define CB_INDEX_LOCAL(lx, y, lz) (((lx) << 12) | ((lz) << 8) | (y))

/* ---------------- one-time owr_run scratch (allocated once, reused) --------- */
static McSinTable  *g_st;
static ChunkPrimer *g_pr;
static FoliageCoord *g_fol;
static u16 *g_owr, *g_stb;                 /* decorated window / its own st base */
static u8  *g_sky, *g_blk, *g_ts, *g_tb;   /* owr_run light scratch */
static u16 *g_skyhm;                       /* populate-time vanilla skylight heightMap */
static u8  *g_skylt;                       /* populate-time vanilla STALE skylight (mushroom gate) */
static u16 *g_cur, *g_tmp, *g_bca;         /* owr_run fluid-CA scratch */
static u8  *g_seeded;                      /* seeded-location mask (window being built) */
static int *g_bio;                         /* base window biome (build_st_base output) */
/* depth-1 nested-cascade buffer set (see ensure_scratch / cascade_hook) */
static FoliageCoord *g2_fol;
static u16 *g2_owr, *g2_stb;
static u8  *g2_sky, *g2_blk, *g2_ts, *g2_tb, *g2_seeded;
static u16 *g2_skyhm;
static u8  *g2_skylt;
static int *g2_bio;

static int ensure_scratch(void) {
    if (g_owr) return 1;
    /* Mineshafts are placed in owr_populate with the shared population RNG. */
    st_map_features_host = -1;
    g_st  = (McSinTable *)malloc(sizeof(McSinTable));
    g_sc  = (CpScratch *)malloc(sizeof(CpScratch));
    g_pr  = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    g_fol = (FoliageCoord *)malloc(sizeof(FoliageCoord) * (size_t)BT_MAX_FOLIAGE);
    g_owr = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    g_stb = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    g_sky = (u8 *)malloc((size_t)W_N);
    g_blk = (u8 *)malloc((size_t)W_N);
    g_ts  = (u8 *)malloc((size_t)W_N);
    g_tb  = (u8 *)malloc((size_t)W_N);
    g_skyhm = (u16 *)malloc(sizeof(u16) * (size_t)(W_X * W_Z));
    g_skylt = (u8 *)malloc((size_t)W_N);
    g_cur = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    g_tmp = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    g_bca = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    g_seeded = (u8 *)malloc((size_t)W_N);
    g_bio = (int *)malloc(sizeof(int) * (size_t)(W_X * W_Z));
    /* Second buffer set for ONE level of nested cascade populate (cascade_hook
     * swaps the live-during-populate buffers; g_sc/g_pr/g_cur/g_tmp/g_bca are only
     * used in phases the parent is not inside when the hook fires, so shared). */
    g2_fol = (FoliageCoord *)malloc(sizeof(FoliageCoord) * (size_t)BT_MAX_FOLIAGE);
    g2_owr = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    g2_stb = (u16 *)malloc(sizeof(u16) * (size_t)W_N);
    g2_sky = (u8 *)malloc((size_t)W_N);
    g2_blk = (u8 *)malloc((size_t)W_N);
    g2_ts  = (u8 *)malloc((size_t)W_N);
    g2_tb  = (u8 *)malloc((size_t)W_N);
    g2_skyhm = (u16 *)malloc(sizeof(u16) * (size_t)(W_X * W_Z));
    g2_skylt = (u8 *)malloc((size_t)W_N);
    g2_seeded = (u8 *)malloc((size_t)W_N);
    g2_bio = (int *)malloc(sizeof(int) * (size_t)(W_X * W_Z));
    if (!g_st || !g_sc || !g_pr || !g_fol || !g_owr || !g_stb || !g_sky ||
        !g_blk || !g_ts || !g_tb || !g_skyhm || !g_skylt || !g_cur || !g_tmp || !g_bca ||
        !g_seeded || !g_bio ||
        !g2_fol || !g2_owr || !g2_stb || !g2_sky || !g2_blk || !g2_ts || !g2_tb ||
        !g2_skyhm || !g2_skylt || !g2_seeded || !g2_bio)
        return 0;
    mc_sin_table_init(g_st);
    return 1;
}

static int mineshaft_type_at(i64 seed, int x, int z) {
    GLNode nodes[GL_MAX_NODES];
    int voronoi;
    gl_build(nodes, seed, &voronoi);
    g_sc->arena.off = 0;
    int biome = gl_getInts(nodes, &g_sc->arena, voronoi, x, z, 1, 1)[0];
    return biome == B_MESA || biome == B_MESA_ROCK ||
           biome == B_MESA_CLEAR_ROCK || biome == 165 ||
           biome == 166 || biome == 167 ? MS_TYPE_MESA : MS_TYPE_NORMAL;
}

/* ---------------- per-base-chunk populate window cache --------------------- */
/* A decoration cell: a world-coord block owr_run added on top of its base terrain.
 * y in [0,256) fits u16; id is a PB_* code (fits u16). */
typedef struct { int wx, wz; unsigned short y, id; } DecCell;

#define POPMC_DUNGEON_CHESTS_MAX 16
#define POPMC_DUNGEON_SPAWNERS_MAX 8
#define POPMC_MINESHAFT_CARTS_MAX 32
#define POPMC_MINESHAFT_SPAWNERS_MAX 16
#define POPMC_DESERT_CHESTS_MAX 4
#define POPMC_JUNGLE_CHESTS_MAX 2
#define POPMC_JUNGLE_DISPENSERS_MAX 2
#define POPMC_VILLAGE_CHESTS_MAX 8
#define POPMC_VILLAGE_RESIDENTS_MAX 64
#define POPMC_SWAMP_WITCHES_MAX 1
#define POPMC_SWAMP_POTS_MAX 1
typedef struct {
    int wx, wz;
    unsigned short y;
    unsigned char facing_meta;
    long long loot_seed;
} DungeonChestSite;
typedef struct {
    int wx, wz;
    unsigned short y;
    unsigned short roll;
} DungeonSpawnerSite;
typedef struct {
    int wx, wz;
    unsigned short y;
    long long loot_seed;
} MineshaftCartSite;
typedef struct {
    int wx, wz;
    unsigned short y;
} MineshaftSpawnerSite;
typedef DungeonChestSite DesertChestSite;
typedef DungeonChestSite JungleSite;
typedef struct {
    int wx, wz;
    unsigned short y;
    unsigned short value;
    unsigned char meta;
} SwampSite;
typedef struct {
    int wx, wz;
    unsigned short y;
    signed char profession;
    unsigned char zombie_infested;
} VillageResidentSite;

typedef struct {
    int bcx, bcz;
    DungeonChestSite chests[POPMC_DUNGEON_CHESTS_MAX];
    DungeonSpawnerSite spawners[POPMC_DUNGEON_SPAWNERS_MAX];
    MineshaftCartSite mineshaft_carts[POPMC_MINESHAFT_CARTS_MAX];
    MineshaftSpawnerSite mineshaft_spawners[POPMC_MINESHAFT_SPAWNERS_MAX];
    DesertChestSite desert_chests[POPMC_DESERT_CHESTS_MAX];
    JungleSite jungle_chests[POPMC_JUNGLE_CHESTS_MAX];
    JungleSite jungle_dispensers[POPMC_JUNGLE_DISPENSERS_MAX];
    DungeonChestSite village_chests[POPMC_VILLAGE_CHESTS_MAX];
    VillageResidentSite village_residents[POPMC_VILLAGE_RESIDENTS_MAX];
    SwampSite swamp_witches[POPMC_SWAMP_WITCHES_MAX];
    SwampSite swamp_pots[POPMC_SWAMP_POTS_MAX];
    int n_chests, n_spawners, n_mineshaft_carts, n_mineshaft_spawners;
    int n_desert_chests;
    int n_jungle_chests, n_jungle_dispensers;
    int n_village_chests, n_village_residents;
    int n_swamp_witches, n_swamp_pots;
} DungeonCapture;

/* build_window may recurse once for a populate cascade. Keep one capture frame
 * per recursion level so the parent's already-recorded dungeon metadata is not
 * clobbered by the nested build. */
static DungeonCapture g_dungeon_capture[3];
static int g_dungeon_capture_depth;

static void village_capture_chest(void *opaque, int x, int y, int z,
                                  int facing_meta, long long loot_seed) {
    DungeonCapture *capture;
    DungeonChestSite *site;
    (void)opaque;
    if (g_dungeon_capture_depth <= 0) return;
    capture = &g_dungeon_capture[g_dungeon_capture_depth - 1];
    if (capture->n_village_chests >= POPMC_VILLAGE_CHESTS_MAX) {
        fprintf(stderr, "[populate_mc] FATAL: village chest cap exceeded at (%d,%d)\n",
                capture->bcx, capture->bcz);
        abort();
    }
    site = &capture->village_chests[capture->n_village_chests++];
    site->wx = x;
    site->wz = z;
    site->y = (unsigned short)y;
    site->facing_meta = (unsigned char)facing_meta;
    site->loot_seed = loot_seed;
}

static void village_capture_resident(void *opaque, int x, int y, int z,
                                     int profession, int zombie_infested) {
    DungeonCapture *capture;
    VillageResidentSite *site;
    (void)opaque;
    if (g_dungeon_capture_depth <= 0) return;
    capture = &g_dungeon_capture[g_dungeon_capture_depth - 1];
    if (capture->n_village_residents >= POPMC_VILLAGE_RESIDENTS_MAX) {
        fprintf(stderr, "[populate_mc] FATAL: village resident cap exceeded at (%d,%d)\n",
                capture->bcx, capture->bcz);
        abort();
    }
    site = &capture->village_residents[capture->n_village_residents++];
    site->wx = x; site->wz = z; site->y = (unsigned short)y;
    site->profession = (signed char)profession;
    site->zombie_infested = (unsigned char)!!zombie_infested;
}

static void dungeon_capture_event(int baseCx, int baseCz, int kind,
                                  int lx, int y, int lz, i64 value,
                                  int meta) {
    DungeonCapture *c;
    if (g_dungeon_capture_depth <= 0) return;
    c = &g_dungeon_capture[g_dungeon_capture_depth - 1];
    if (c->bcx != baseCx || c->bcz != baseCz) return;
    if (kind == MC_DUNGEON_EVENT_CHEST) {
        DungeonChestSite *s;
        if (c->n_chests >= POPMC_DUNGEON_CHESTS_MAX) {
            fprintf(stderr, "[populate_mc] FATAL: dungeon chest event cap exceeded at (%d,%d)\n",
                    baseCx, baseCz);
            abort();
        }
        s = &c->chests[c->n_chests++];
        s->wx = baseCx * 16 + lx;
        s->wz = baseCz * 16 + lz;
        s->y = (unsigned short)y;
        s->facing_meta = (unsigned char)meta;
        s->loot_seed = (long long)value;
    } else if (kind == MC_DUNGEON_EVENT_SPAWNER) {
        DungeonSpawnerSite *s;
        if (c->n_spawners >= POPMC_DUNGEON_SPAWNERS_MAX) {
            fprintf(stderr, "[populate_mc] FATAL: dungeon spawner event cap exceeded at (%d,%d)\n",
                    baseCx, baseCz);
            abort();
        }
        s = &c->spawners[c->n_spawners++];
        s->wx = baseCx * 16 + lx;
        s->wz = baseCz * 16 + lz;
        s->y = (unsigned short)y;
        s->roll = (unsigned short)value;
    } else if (kind == MC_DUNGEON_EVENT_DESERT_CHEST) {
        DesertChestSite *s;
        if (c->n_desert_chests >= POPMC_DESERT_CHESTS_MAX) {
            fprintf(stderr, "[populate_mc] FATAL: desert chest event cap exceeded at (%d,%d)\n",
                    baseCx, baseCz);
            abort();
        }
        s = &c->desert_chests[c->n_desert_chests++];
        s->wx = baseCx * 16 + lx;
        s->wz = baseCz * 16 + lz;
        s->y = (unsigned short)y;
        s->facing_meta = (unsigned char)meta;
        s->loot_seed = (long long)value;
    } else if (kind == MC_DUNGEON_EVENT_JUNGLE_CHEST
            || kind == MC_DUNGEON_EVENT_JUNGLE_DISPENSER) {
        JungleSite *s;
        int *count = kind == MC_DUNGEON_EVENT_JUNGLE_CHEST
            ? &c->n_jungle_chests : &c->n_jungle_dispensers;
        int cap = kind == MC_DUNGEON_EVENT_JUNGLE_CHEST
            ? POPMC_JUNGLE_CHESTS_MAX : POPMC_JUNGLE_DISPENSERS_MAX;
        JungleSite *sites = kind == MC_DUNGEON_EVENT_JUNGLE_CHEST
            ? c->jungle_chests : c->jungle_dispensers;
        if (*count >= cap) {
            fprintf(stderr, "[populate_mc] FATAL: jungle site event cap exceeded at (%d,%d)\n",
                    baseCx, baseCz);
            abort();
        }
        s = &sites[(*count)++];
        s->wx = baseCx * 16 + lx;
        s->wz = baseCz * 16 + lz;
        s->y = (unsigned short)y;
        s->facing_meta = (unsigned char)meta;
        s->loot_seed = (long long)value;
    } else if (kind == MC_DUNGEON_EVENT_SWAMP_WITCH
            || kind == MC_DUNGEON_EVENT_SWAMP_POT) {
        SwampSite *s;
        int *count = kind == MC_DUNGEON_EVENT_SWAMP_WITCH
            ? &c->n_swamp_witches : &c->n_swamp_pots;
        int cap = kind == MC_DUNGEON_EVENT_SWAMP_WITCH
            ? POPMC_SWAMP_WITCHES_MAX : POPMC_SWAMP_POTS_MAX;
        SwampSite *sites = kind == MC_DUNGEON_EVENT_SWAMP_WITCH
            ? c->swamp_witches : c->swamp_pots;
        if (*count >= cap) {
            fprintf(stderr, "[populate_mc] FATAL: swamp site event cap exceeded at (%d,%d)\n",
                    baseCx, baseCz);
            abort();
        }
        s = &sites[(*count)++];
        s->wx = baseCx * 16 + lx;
        s->wz = baseCz * 16 + lz;
        s->y = (unsigned short)y;
        s->value = (unsigned short)value;
        s->meta = (unsigned char)meta;
    }
}

/* Serialized ComponentScatteredFeaturePieces.Feature.horizontalPos. A start is
 * sparse (one candidate per 32x32 chunk region), so a fixed open-address table
 * gives persistent first-populate semantics without per-window allocation. */
#define POPMC_SCATTERED_HPOS_CAP 4096
typedef struct {
    long long seed;
    int cx, cz, hpos, valid;
} ScatteredHPos;
static ScatteredHPos g_scattered_hpos[POPMC_SCATTERED_HPOS_CAP];

static int scattered_hpos_resolve(i64 seed, int cx, int cz, int fallback) {
    unsigned long long h = (unsigned long long)seed;
    h ^= (unsigned long long)(unsigned int)cx * 0x9e3779b185ebca87ULL;
    h ^= (unsigned long long)(unsigned int)cz * 0xc2b2ae3d27d4eb4fULL;
    unsigned slot = (unsigned)(h ^ (h >> 32)) & (POPMC_SCATTERED_HPOS_CAP - 1);
    for (int probe = 0; probe < POPMC_SCATTERED_HPOS_CAP; ++probe) {
        ScatteredHPos *entry = &g_scattered_hpos[slot];
        if (!entry->valid) {
            entry->valid = 1;
            entry->seed = (long long)seed;
            entry->cx = cx; entry->cz = cz; entry->hpos = fallback;
            return fallback;
        }
        if (entry->seed == (long long)seed && entry->cx == cx && entry->cz == cz)
            return entry->hpos;
        slot = (slot + 1) & (POPMC_SCATTERED_HPOS_CAP - 1);
    }
    fprintf(stderr, "[populate_mc] FATAL: scattered horizontal-position table full\n");
    abort();
}

static void mineshaft_capture_event(int baseCx, int baseCz, int kind,
                                    int x, int y, int z, i64 value) {
    DungeonCapture *c;
    if (g_dungeon_capture_depth <= 0) return;
    c = &g_dungeon_capture[g_dungeon_capture_depth - 1];
    if (c->bcx != baseCx || c->bcz != baseCz) return;
    if (getenv("MAGMA_DEBUG_MINESHAFT"))
        fprintf(stderr, "[populate_mc] mineshaft event base=(%d,%d) kind=%d "
                "pos=(%d,%d,%d) value=%lld\n",
                baseCx, baseCz, kind, x, y, z, (long long)value);
    if (kind == MS_EVENT_CART) {
        MineshaftCartSite *s;
        if (c->n_mineshaft_carts >= POPMC_MINESHAFT_CARTS_MAX) {
            fprintf(stderr, "[populate_mc] FATAL: mineshaft cart event cap exceeded at (%d,%d)\n",
                    baseCx, baseCz);
            abort();
        }
        s = &c->mineshaft_carts[c->n_mineshaft_carts++];
        s->wx = x; s->wz = z; s->y = (unsigned short)y;
        s->loot_seed = (long long)value;
    } else if (kind == MS_EVENT_SPAWNER) {
        MineshaftSpawnerSite *s;
        if (c->n_mineshaft_spawners >= POPMC_MINESHAFT_SPAWNERS_MAX) {
            fprintf(stderr, "[populate_mc] FATAL: mineshaft spawner event cap exceeded at (%d,%d)\n",
                    baseCx, baseCz);
            abort();
        }
        s = &c->mineshaft_spawners[c->n_mineshaft_spawners++];
        s->wx = x; s->wz = z; s->y = (unsigned short)y;
    }
}

/* OOB decoration spill: writes that leave the 32x32 window during owr_populate.
 * Recorded with absolute world coords so later neighbor windows can seed them
 * (donor path) and so apply_window can place them when the target chunk is built. */
#define OOB_SPILL_MAX 65536
static DecCell g_oob_spill[OOB_SPILL_MAX];
static int g_oob_n;
static int g_oob_dropped;

static void oob_spill_reset(void) { g_oob_n = 0; g_oob_dropped = 0; }

static void oob_spill_write(int baseCx, int baseCz, int lx, int y, int lz, int v) {
    if (y < 0 || y >= W_Y) return;
    int wx = baseCx * 16 + lx, wz = baseCz * 16 + lz;
    /* last-write-wins for the same cell within one populate */
    for (int i = g_oob_n - 1; i >= 0 && i >= g_oob_n - 64; --i) {
        if (g_oob_spill[i].wx == wx && g_oob_spill[i].wz == wz &&
            (int)g_oob_spill[i].y == y) {
            g_oob_spill[i].id = (unsigned short)v;
            return;
        }
    }
    if (g_oob_n >= OOB_SPILL_MAX) { ++g_oob_dropped; return; }
    g_oob_spill[g_oob_n].wx = wx;
    g_oob_spill[g_oob_n].wz = wz;
    g_oob_spill[g_oob_n].y = (unsigned short)y;
    g_oob_spill[g_oob_n].id = (unsigned short)v;
    ++g_oob_n;
}

typedef struct {
    long long seed;
    int       bcx, bcz;
    int       valid;      /* slot holds a real built window */
    long      seq;        /* build order (g_builds at build time): donor apply order */
    DecCell  *cells;      /* post-fluid-pass cells, applied to chunks */
    int       ncells;
    DecCell  *pop_cells;  /* PRE-fluid-pass cells, seeded into later windows: vanilla
                           * runs initial-load populates back-to-back with NO world
                           * ticks between them, so a window sees earlier windows'
                           * placed blocks but NOT their fluid spread. */
    int       npop;
    DungeonChestSite dungeon_chests[POPMC_DUNGEON_CHESTS_MAX];
    DungeonSpawnerSite dungeon_spawners[POPMC_DUNGEON_SPAWNERS_MAX];
    MineshaftCartSite mineshaft_carts[POPMC_MINESHAFT_CARTS_MAX];
    MineshaftSpawnerSite mineshaft_spawners[POPMC_MINESHAFT_SPAWNERS_MAX];
    DesertChestSite desert_chests[POPMC_DESERT_CHESTS_MAX];
    JungleSite jungle_chests[POPMC_JUNGLE_CHESTS_MAX];
    JungleSite jungle_dispensers[POPMC_JUNGLE_DISPENSERS_MAX];
    DungeonChestSite village_chests[POPMC_VILLAGE_CHESTS_MAX];
    VillageResidentSite village_residents[POPMC_VILLAGE_RESIDENTS_MAX];
    SwampSite swamp_witches[POPMC_SWAMP_WITCHES_MAX];
    SwampSite swamp_pots[POPMC_SWAMP_POTS_MAX];
    int       n_dungeon_chests, n_dungeon_spawners;
    int       n_mineshaft_carts, n_mineshaft_spawners;
    int       n_desert_chests;
    int       n_jungle_chests, n_jungle_dispensers;
    int       n_village_chests, n_village_residents;
    int       n_swamp_witches, n_swamp_pots;
} Window;

/* ALLOCATE-ONCE toroidal owr-window pool. A fixed (2R+4)^2 pool indexed by (bcx,bcz)
 * modulo owr_D. The base chunks decorating the lit region span exactly owr_D x owr_D,
 * so each maps to a unique slot; a base chunk scrolling out frees its slot for the
 * incoming one, which RECYCLES it (re-run owr_run). Each slot owns one pre-allocated
 * cells buffer. Window eviction is safe: the decoration a window produces is written
 * into each lit chunk's block store at gen time and persists there; re-running owr_run
 * for a recycled window is deterministic. No malloc/free after init. */
static Window      *g_slots;
static const CrCaps *g_caps;

/* `debug_caps` registry key, read once. Same shape as the getenv it replaced:
 * a cached flag, never a per-window config read. */
static int popmc_debug_caps(void) {
    static int on = -1;
    if (on < 0) on = cr_cfg()->debug_caps;
    return on;
}
static int          g_owr_D, g_owr_slots;
static long         g_builds;   /* number of owr_run executions (compute-once metric) */
static int          g_bigtree_carry;   /* WorldGenBigTree.heightLimit session carry (see build_window) */
static const PopmcCascadeEvt *g_casc;  /* cascade RNG-clobber events (popmc_set_cascade) */
static int          g_ncasc;
static int          g_casc_depth;      /* 1 while inside a nested cascade build */
/* Parent window mid-populate, exposed as an extra donor for the nested build:
 * vanilla's cascade-populated chunk sees the parent's PARTIAL decorations. */
static struct {
    int active, bcx, bcz;
    const u16 *owr, *stb;
    const u8 *seeded;
} g_partial;
/* Vanilla chunkPos corruption (cascade_hook): the nested decorate() overwrites the
 * shared BiomeDecorator.chunkPos and vanilla never restores it, so the PARENT's
 * remaining decoration positions are relative to the NESTED chunk's corner. */
static World  *g_live_w;     /* World of the build in progress (redirect target) */
static Window *g_redir_win;  /* non-NULL: parent decoration redirected into this nested window */
static int     g_redir_ncx, g_redir_ncz;   /* nested chunk the corrupted tail decorates */
static int     g_redir_pbcx, g_redir_pbcz; /* parent base chunk (frame restore) */
static int     g_redir_applied;            /* redirect_apply fired for this build */

static int skyhm_scan_col(const u16 *blocks, int x, int z) {
    for (int y = W_Y - 1; y >= 0; --y)
        if (pb_opacity((int)blocks[w_index(x, y, z)]) > 0) return y + 1;
    return 0;
}

static void skyhm_rescan_col(u16 *heightMap, const u16 *blocks, int x, int z) {
    heightMap[w_col_index(x, z)] = (u16)skyhm_scan_col(blocks, x, z);
}

/* stale-skylight mode: default ON; MAGMA_SHROOMLIGHT=ca falls back to the fixpoint CA */
static int popmc_stale_light_enabled(void) {
    static int mode = -1;
    if (mode < 0) {
        const char *s = getenv("MAGMA_SHROOMLIGHT");
        mode = !(s && strcmp(s, "ca") == 0);
    }
    return mode;
}

/* re-seed World.popSecMask from the frame's current blocks (redirect frame swaps) */
static void popmc_sec_mask_seed(World *lw) {
    for (int cxi = 0; cxi < 2; ++cxi)
        for (int czi = 0; czi < 2; ++czi) {
            u16 m = 0;
            for (int sec = 0; sec < 16; ++sec) {
                int found = 0;
                for (int lx = 0; lx < 16 && !found; ++lx)
                    for (int lz = 0; lz < 16 && !found; ++lz)
                        for (int y = sec * 16; y < sec * 16 + 16; ++y)
                            if (lw->blocks[w_index(cxi * 16 + lx, y, czi * 16 + lz)] != PB_AIR) {
                                found = 1;
                                break;
                            }
                if (found) m |= (u16)(1u << sec);
            }
            lw->popSecMask[cxi][czi] = m;
        }
}

/* vanilla generateSkylightMap column walk over a raw buffer (regen keeps deep cells) */
static void skylt_regen_col(u8 *light, const u16 *blocks, int x, int z) {
    int k1 = 15;
    for (int y = W_Y - 1; y >= 0; --y) {
        int j1 = pb_opacity((int)blocks[w_index(x, y, z)]);
        if (j1 == 0 && k1 != 15) j1 = 1;
        k1 -= j1;
        if (k1 <= 0) break;
        light[w_index(x, y, z)] = (u8)k1;
    }
}

static void skylt_copy_col(u8 *dst, const u8 *srcl, int dxi, int dzi, int sxi, int szi) {
    for (int y = 0; y < W_Y; ++y)
        dst[w_index(dxi, y, dzi)] = srcl[w_index(sxi, y, szi)];
}

static void skyhm_copy_or_rescan_redirect_dst(int dx, int dz) {
    for (int lx = 0; lx < W_X; ++lx)
        for (int lz = 0; lz < W_Z; ++lz) {
            int px = lx + dx, pz = lz + dz;   /* nested-local -> parent-local */
            if (px >= 0 && px < W_X && pz >= 0 && pz < W_Z) {
                g2_skyhm[w_col_index(lx, lz)] = g_skyhm[w_col_index(px, pz)];
                skylt_copy_col(g2_skylt, g_skylt, lx, lz, px, pz);
            } else {
                skyhm_rescan_col(g2_skyhm, g2_owr, lx, lz);
                skylt_regen_col(g2_skylt, g2_owr, lx, lz);
            }
        }
}

static void skyhm_copy_nested_col_to_parent(int pbcx, int pbcz, int nbcx, int nbcz,
                                            int wx, int wz) {
    int px = wx - pbcx * 16, pz = wz - pbcz * 16;
    int nx = wx - nbcx * 16, nz = wz - nbcz * 16;
    if (px < 0 || px >= W_X || pz < 0 || pz >= W_Z ||
        nx < 0 || nx >= W_X || nz < 0 || nz >= W_Z)
        return;
    g_skyhm[w_col_index(px, pz)] = g2_skyhm[w_col_index(nx, nz)];
    skylt_copy_col(g_skylt, g2_skylt, px, pz, nx, nz);
}

/* MC_REDIRECT_POLL target: swap the live World to the nested window's buffers at the
 * next decoration position draw. Deferred (not done in cascade_hook) because the
 * feature in flight at clobber time keeps its absolute position in vanilla - it must
 * finish in the PARENT frame; only draws after it return see the corrupted chunkPos. */
static void redirect_apply(struct World *lw) {
    mc_redirect_pending = 0;
    g_redir_applied = 1;
    /* Vanilla has ONE world: everything the parent wrote after the nested snapshot
     * (the in-flight tree's trunk/vines, pre-cascade decorations) must be visible to
     * the corrupted tail. The parent window is ground truth for the overlap (the
     * nested results were already copied back into it) - mirror it into g2. */
    {
        int dx = (g_redir_ncx - g_redir_pbcx) * 16, dz = (g_redir_ncz - g_redir_pbcz) * 16;
        for (int lx = 0; lx < W_X; ++lx)
            for (int lz = 0; lz < W_Z; ++lz) {
                int px = lx + dx, pz = lz + dz;   /* nested-local -> parent-local */
                if (px < 0 || px >= W_X || pz < 0 || pz >= W_Z) continue;
                for (int y = 0; y < W_Y; ++y) {
                    int nidx = w_index(lx, y, lz), pidx = w_index(px, y, pz);
                    g2_owr[nidx] = g_owr[pidx];
                    g2_seeded[nidx] = g_seeded[pidx];
                }
            }
        skyhm_copy_or_rescan_redirect_dst(dx, dz);
    }
    lw->blocks = g2_owr;
    lw->popSkyHeight = g2_skyhm;
    if (lw->popSkyLight) { lw->popSkyLight = g2_skylt; popmc_sec_mask_seed(lw); }
    for (int x = 0; x < W_X; ++x)
        for (int z = 0; z < W_Z; ++z)
            lw->fullBiome[x * W_Z + z] = g2_bio[x * W_Z + z];
    lw->baseCx = g_redir_ncx;
    lw->baseCz = g_redir_ncz;
    fprintf(stderr, "[populate_mc] cascade: chunkPos corruption applied -> tail decorates "
            "(%d,%d) frame\n", g_redir_ncx, g_redir_ncz);
}

/* MC_REDIRECT_RESTORE target: genDecorations returned - the biome decorate() extras
 * (jungle melon/vines etc.) and the populate tail use the uncorrupted pos parameter.
 * Swap the live World back to the parent frame, first copying the corrupted tail's
 * writes that overlap the parent window into it (the extras READ them: vines/reeds
 * are block-conditional). */
static void redirect_restore(struct World *lw) {
    if (!g_redir_applied || lw->blocks != g2_owr) return;
    int dx = (g_redir_ncx - g_redir_pbcx) * 16, dz = (g_redir_ncz - g_redir_pbcz) * 16;
    for (int lx = 0; lx < W_X; ++lx)
        for (int lz = 0; lz < W_Z; ++lz) {
            int px = lx + dx, pz = lz + dz;   /* nested-local -> parent-local */
            if (px < 0 || px >= W_X || pz < 0 || pz >= W_Z) continue;
            for (int y = 0; y < W_Y; ++y) {
                int nidx = w_index(lx, y, lz);
                if (g2_owr[nidx] == g2_stb[nidx] && !g2_seeded[nidx]) continue;
                int pidx = w_index(px, y, pz);
                g_owr[pidx] = g2_owr[nidx];
                g_seeded[pidx] = 1;
            }
            g_skyhm[w_col_index(px, pz)] = g2_skyhm[w_col_index(lx, lz)];
        }
    lw->blocks = g_owr;
    lw->popSkyHeight = g_skyhm;
    if (lw->popSkyLight) { lw->popSkyLight = g_skylt; popmc_sec_mask_seed(lw); }
    for (int x = 0; x < W_X; ++x)
        for (int z = 0; z < W_Z; ++z)
            lw->fullBiome[x * W_Z + z] = g_bio[x * W_Z + z];
    lw->baseCx = g_redir_pbcx;
    lw->baseCz = g_redir_pbcz;
    fprintf(stderr, "[populate_mc] cascade: parent frame (%d,%d) restored post-genDecorations\n",
            g_redir_pbcx, g_redir_pbcz);
}

static inline int owr_tor(int v, int D) { int m = v % D; if (m < 0) m += D; return m; }

/* base-chunk memo storage (see stb_chunk below find_window) */
typedef struct { long long seed; int cx, cz, valid; } StbKey;
static StbKey *g_stb_keys;
static u16    *g_stb_pool;   /* g_stb_D^2 x 65536 block ids */
static int     g_stb_D;

static int owr_pool_init(void) {
    if (g_slots) return 1;
    g_caps      = cr_caps();
    g_owr_D     = g_caps->owr_D;
    g_owr_slots = g_caps->owr_slots;
    g_slots = (Window *)calloc((size_t)g_owr_slots, sizeof(Window));
    if (!g_slots) return 0;
    for (int i = 0; i < g_owr_slots; ++i) {
        g_slots[i].cells = (DecCell *)malloc((size_t)g_caps->owr_cells_max * sizeof(DecCell));
        g_slots[i].pop_cells = (DecCell *)malloc((size_t)g_caps->owr_cells_max * sizeof(DecCell));
        if (!g_slots[i].cells || !g_slots[i].pop_cells) return 0;
    }
    /* base-chunk memo: window bcx spans g_owr_D, base chunks reach bcx+1 -> +2
     * margin keeps live windows collision-free. Failure just disables the memo. */
    g_stb_D = g_owr_D + 2;
    g_stb_keys = (StbKey *)calloc((size_t)g_stb_D * g_stb_D, sizeof(StbKey));
    g_stb_pool = (u16 *)malloc((size_t)g_stb_D * g_stb_D * 65536 * sizeof(u16));
    if (!g_stb_keys || !g_stb_pool) {
        free(g_stb_keys); free(g_stb_pool);
        g_stb_keys = NULL; g_stb_pool = NULL;
    }
    return 1;
}

static Window *find_window(long long seed, int bcx, int bcz) {
    if (!g_slots) return NULL;
    Window *w = &g_slots[owr_tor(bcx, g_owr_D) * g_owr_D + owr_tor(bcz, g_owr_D)];
    if (w->valid && w->seed == seed && w->bcx == bcx && w->bcz == bcz) return w;
    return NULL;
}

/* Base-chunk memo: neighboring windows share base chunks, so build_st_base used
 * to regenerate each one up to 4x (~21% of a 12k replay). Cache the primer bytes
 * per (seed,cx,cz), toroidal + allocate-once like the owr pool. Because this TU
 * runs st_map_features_host = -1, the bytes are IDENTICAL to the live gen_chunk
 * product, so the gen_prefetch worker's copy (weak symbols; may be unlinked in
 * unit tests) can seed the memo for free. */
extern int genpf_take(int cx, int cz, unsigned short *out) __attribute__((weak));
extern int genpf_active(long long seed) __attribute__((weak));

static const u16 *stb_chunk(long long seed, int cx, int cz) {
    if (!g_stb_keys) {                       /* memo alloc failed: old path */
        st_run_features(g_pr, g_sc, g_st, (i64)seed, cx, cz, ST_MAP_FEATURES);
        return g_pr->data;
    }
    int si = owr_tor(cx, g_stb_D) * g_stb_D + owr_tor(cz, g_stb_D);
    StbKey *k = &g_stb_keys[si];
    u16 *d = g_stb_pool + (size_t)si * 65536;
    if (k->valid && k->seed == seed && k->cx == cx && k->cz == cz) return d;
    if (!(genpf_take && genpf_active && genpf_active(seed) &&
          genpf_take(cx, cz, d))) {
        st_run_features(g_pr, g_sc, g_st, (i64)seed, cx, cz, ST_MAP_FEATURES);
        memcpy(d, g_pr->data, sizeof g_pr->data);
    }
    k->valid = 1; k->seed = seed; k->cx = cx; k->cz = cz;
    return d;
}

/* Reconstruct owr_run's OWN base terrain (st_run x4) into g_stb, mirroring the exact
 * base-construction prefix of owr_run (biome voronoi at the same world offset, same
 * arena reset, same st_run coords) so g_stb equals owr_run's pre-populate blocks.
 * Diffing against this isolates DECORATION cells only. */
static void build_st_base(long long seed, int bcx, int bcz) {
    World wb;
    wb.st = g_st;
    wb.blocks = g_stb;
    for (int i = 0; i < W_N; ++i) g_stb[i] = (u16)PB_AIR;
    wb.bigtree_heightLimit = 0;
    w_reset_loaded_chunks(&wb, seed, bcx, bcz);
    {
        GLNode nodes[GL_MAX_NODES];
        int voronoi;
        gl_build(nodes, (i64)seed, &voronoi);
        g_sc->arena.off = 0;
        int *fb = gl_getInts(nodes, &g_sc->arena, voronoi, bcx * 16, bcz * 16, W_X, W_Z);
        /* GenLayer.getInts layout is fb[dx + dz*width] for world (areaX+dx, areaZ+dz);
         * fullBiome storage is [x*W_Z + z]. A transposed read here was invisible to the
         * decoration biome (diagonal (16,16)) but broke every off-diagonal per-column
         * consumer (ice/snow temperatures). */
        for (int x = 0; x < W_X; ++x)
            for (int z = 0; z < W_Z; ++z) {
                wb.fullBiome[x * W_Z + z] = fb[x + z * W_X];
                g_bio[x * W_Z + z] = fb[x + z * W_X];
            }
    }
    for (int cx = 0; cx < 2; ++cx)
        for (int cz = 0; cz < 2; ++cz) {
            const u16 *cp = stb_chunk(seed, bcx + cx, bcz + cz);
            /* Both layouts are y-fastest (cb_index = x<<12|z<<8|y, w_index ends
             * *W_Y+y), and w_set is pure blocks[] stores here (activeRand and
             * popSkyHeight are NULL after w_reset_loaded_chunks), so a straight
             * column memcpy is byte-identical to the old per-cell w_set loop. */
            for (int lx = 0; lx < 16; ++lx)
                for (int lz = 0; lz < 16; ++lz)
                    memcpy(&wb.blocks[w_index(cx * 16 + lx, 0, cz * 16 + lz)],
                           &cp[(lx << 12) | (lz << 8)], 256 * sizeof(u16));
        }
}

static long long g_build_seed;   /* seed of the build in progress (cascade_hook) */

/* Re-snapshot a window's cell lists from the given buffers (chunkPos-corruption
 * resnap: the parent's post-resume writes landed in the nested window's buffers
 * AFTER its cells were recorded). Collapses pop_cells==cells - fine while the
 * fluid CA is off (default); the CA distinction only matters with MAGMA_FLUID_CA. */
typedef unsigned long long PmAu64 __attribute__((may_alias));

static void record_window_cells(Window *win, const u16 *owr, const u16 *stb,
                                const u8 *seeded) {
    /* One pass, 64-cell stride: most of the window is undecorated, so compare
     * 16 u64 words of blocks + 8 of seeded and skip clean runs. Cell order is
     * unchanged - the old lx,lz,y nesting IS ascending w_index order. */
    int n = 0, cap = g_caps->owr_cells_max;
    const PmAu64 *o64 = (const PmAu64 *)owr;
    const PmAu64 *s64 = (const PmAu64 *)stb;
    for (int base = 0; base < W_N; base += 64) {
        PmAu64 d = 0;
        int q = base >> 2;
        for (int j = 0; j < 16; ++j) d |= o64[q + j] ^ s64[q + j];
        const PmAu64 *e = (const PmAu64 *)(seeded + base);
        for (int j = 0; j < 8; ++j) d |= e[j];
        if (!d) continue;
        for (int i = base; i < base + 64; ++i) {
            if (owr[i] == stb[i] && !seeded[i]) continue;
            if (n < cap) {
                int lx = i / (W_Z * W_Y), lz = (i >> 8) & (W_Z - 1), y = i & (W_Y - 1);
                win->cells[n].wx = win->bcx * 16 + lx;
                win->cells[n].wz = win->bcz * 16 + lz;
                win->cells[n].y  = (unsigned short)y;
                win->cells[n].id = (unsigned short)owr[i];
                win->pop_cells[n] = win->cells[n];
            }
            ++n;
        }
    }
    if (n > cap) {
        fprintf(stderr,
            "[populate_mc] FATAL: window (%d,%d) resnap has %d cells > cap %d "
            "(raise owr_cells_max in magma.conf / caps.h)\n",
            win->bcx, win->bcz, n, cap);
        assert(0 && "owr_cells_max exceeded");
        abort();
    }
    win->ncells = win->npop = n;
}

static Window *build_window(long long seed, int bcx, int bcz) {
    DungeonCapture *dungeon_capture;
    if (!ensure_scratch() || !owr_pool_init()) return NULL;
    if (g_dungeon_capture_depth >= (int)(sizeof g_dungeon_capture /
                                         sizeof g_dungeon_capture[0])) {
        fprintf(stderr, "[populate_mc] FATAL: dungeon capture recursion exceeded\n");
        abort();
    }
    dungeon_capture = &g_dungeon_capture[g_dungeon_capture_depth++];
    memset(dungeon_capture, 0, sizeof *dungeon_capture);
    dungeon_capture->bcx = bcx;
    dungeon_capture->bcz = bcz;
    g_build_seed = seed;

    /* owr_run's own base terrain (also fills g_bio with the window biome map). */
    build_st_base(seed, bcx, bcz);

    /* CUMULATIVE populate: vanilla populates into one SHARED world in load order,
     * so window (bcx,bcz) READS the decorations that populate-order-earlier windows
     * spilled into its 2x2-chunk footprint (trees test isReplaceable/heights, lakes
     * and dungeons test world blocks). Reproduce that by starting from the pure
     * st base and SEEDING the cells of the four earlier overlapping windows -
     * (bcx-1,bcz-1), (bcx-1,bcz), (bcx-1,bcz+1), (bcx,bcz-1), applied in vanilla
     * populate (raster) order so later writes win - before running owr_populate.
     * Windows absent from the pool contribute nothing (the sweep anchor: vanilla
     * never populated them, or best-effort in the live streaming path). */
    World w;
    w.st = g_st;
    w.blocks = g_owr;
    w_reset_loaded_chunks(&w, seed, bcx, bcz);
    for (int i = 0; i < W_N; ++i) {
        g_owr[i] = g_stb[i];
        g_sky[i] = 0;
        g_blk[i] = 0;
        g_seeded[i] = 0;
    }
    /* WorldGenBigTree.heightLimit is SESSION-GLOBAL in vanilla (Biome.BIG_TREE_FEATURE
     * singleton field): the first big oak of the session draws 5+nextInt(12) from its
     * private rand, every later one reuses / checkBlockLine-clamps the carried value.
     * Thread that carry across window builds in populate order (g_bigtree_carry). */
    w.bigtree_heightLimit = g_bigtree_carry;
    for (int x = 0; x < W_X; ++x)
        for (int z = 0; z < W_Z; ++z)
            w.fullBiome[x * W_Z + z] = g_bio[x * W_Z + z];

    {
        /* Donors: neighbor windows built EARLIER (recorded populate order). Include
         * ±2 so OOB canopy spills from a base two chunks away still seed plant-Y. */
        int wx0 = bcx * 16, wz0 = bcz * 16;
        Window *don[24];
        int nd = 0;
        for (int dx = -2; dx <= 2; ++dx)
            for (int dz = -2; dz <= 2; ++dz) {
                if (!dx && !dz) continue;
                Window *nb = find_window(seed, bcx + dx, bcz + dz);
                if (nb) don[nd++] = nb;
            }
        for (int a = 1; a < nd; ++a) {          /* insertion sort by build seq */
            Window *k = don[a];
            int b = a - 1;
            while (b >= 0 && don[b]->seq > k->seq) { don[b + 1] = don[b]; --b; }
            don[b + 1] = k;
        }
        for (int n = 0; n < nd; ++n) {
            Window *nb = don[n];
            for (int i = 0; i < nb->npop; ++i) {
                const DecCell *c = &nb->pop_cells[i];
                int lx = c->wx - wx0, lz = c->wz - wz0;
                if (lx < 0 || lx >= W_X || lz < 0 || lz >= W_Z) continue;
                int idx = w_index(lx, c->y, lz);
                g_owr[idx] = c->id;
                g_seeded[idx] = 1;
            }
        }
        /* Nested cascade build: overlay the interrupted parent's PARTIAL window
         * last (it is the latest writer in vanilla's shared world). */
        if (g_partial.active) {
            int px0 = g_partial.bcx * 16, pz0 = g_partial.bcz * 16;
            for (int lx = 0; lx < W_X; ++lx)
                for (int lz = 0; lz < W_Z; ++lz) {
                    int mx = px0 + lx - wx0, mz = pz0 + lz - wz0;
                    if (mx < 0 || mx >= W_X || mz < 0 || mz >= W_Z) continue;
                    for (int y = 0; y < W_Y; ++y) {
                        int pidx = w_index(lx, y, lz);
                        if (g_partial.owr[pidx] == g_partial.stb[pidx] &&
                            !g_partial.seeded[pidx]) continue;
                        int idx = w_index(mx, y, mz);
                        g_owr[idx] = g_partial.owr[pidx];
                        g_seeded[idx] = 1;
                    }
                }
        }
    }

    JavaRandom r;
    {
        PllLight lt = { g_sky, g_blk, g_ts, g_tb, g_skyhm,
                        popmc_stale_light_enabled() ? g_skylt : NULL };
        mc_probe_cx = bcx;
        mc_probe_cz = bcz;
        /* Arm the cascade RNG-clobber watch (mc_rng.h) with this base's events. */
        mc_jr_watch_n = 0;
        mc_jr_watch_fired = 0;
        for (int i = 0; i < g_ncasc && mc_jr_watch_n < MC_JR_WATCH_MAX; ++i)
            if (g_casc[i].bcx == bcx && g_casc[i].bcz == bcz) {
                mc_jr_watch_before[mc_jr_watch_n] = g_casc[i].before;
                mc_jr_watch_after[mc_jr_watch_n] = g_casc[i].after;
                ++mc_jr_watch_n;
            }
        int armed = mc_jr_watch_n;
        g_live_w = &w;
        oob_spill_reset();
        g_w_oob_write = oob_spill_write;
        g_w_dungeon_event = dungeon_capture_event;
        g_w_scattered_hpos = scattered_hpos_resolve;
        g_ms_event = mineshaft_capture_event;
        g_ms_type_at = mineshaft_type_at;
        owr_populate(&w,&r,(i64)seed,g_fol,&lt,g_sc,bcx,bcz);
        g_w_oob_write = 0;
        g_w_dungeon_event = g_dungeon_capture_depth > 1
            ? dungeon_capture_event : 0;
        g_ms_event = g_dungeon_capture_depth > 1
            ? mineshaft_capture_event : 0;
        g_ms_type_at = g_dungeon_capture_depth > 1
            ? mineshaft_type_at : 0;
        g_w_scattered_hpos = g_dungeon_capture_depth > 1
            ? scattered_hpos_resolve : 0;
        g_live_w = NULL;
        if (armed)
            fprintf(stderr, "[populate_mc] cascade (%d,%d): %d/%d clobber jumps fired\n",
                    bcx, bcz, mc_jr_watch_fired, armed);
        mc_jr_watch_n = 0;
    }
    mc_redirect_pending = 0;   /* armed but never polled: populate ended first */
    if (g_redir_win && g_redir_applied) {
        /* chunkPos corruption: the parent's post-resume writes went into the nested
         * window's g2 buffers (see cascade_hook). Re-snapshot the nested slot so those
         * writes apply/donate with its cells, then refresh the parent-window copy-back
         * so the parent snapshot below (the later writer) carries them too. */
        record_window_cells(g_redir_win, g2_owr, g2_stb, g2_seeded);
        int wx0 = bcx * 16, wz0 = bcz * 16;
        for (int i = 0; i < g_redir_win->npop; ++i) {
            const DecCell *c = &g_redir_win->pop_cells[i];
            int lx = c->wx - wx0, lz = c->wz - wz0;
            if (lx < 0 || lx >= W_X || lz < 0 || lz >= W_Z) continue;
            int idx = w_index(lx, c->y, lz);
            g_owr[idx] = c->id;
            g_seeded[idx] = 1;
            skyhm_copy_nested_col_to_parent(bcx, bcz, g_redir_win->bcx, g_redir_win->bcz,
                                            c->wx, c->wz);
        }
        fprintf(stderr, "[populate_mc] cascade (%d,%d): chunkPos-corrupted tail resnapped "
                "into (%d,%d)\n", bcx, bcz, g_redir_win->bcx, g_redir_win->bcz);
    }
    g_redir_win = NULL;
    g_redir_applied = 0;
    g_bigtree_carry = w.bigtree_heightLimit;
    g_builds++;

    /* RECYCLE the toroidal slot now; record the PRE-fluid cells (seed set), then
     * run the fluid pass and record the post-fluid cells (chunk-apply set). */
    Window *win = &g_slots[owr_tor(bcx, g_owr_D) * g_owr_D + owr_tor(bcz, g_owr_D)];
    win->seed = seed; win->bcx = bcx; win->bcz = bcz; win->valid = 1; win->seq = g_builds;
    win->n_dungeon_chests = dungeon_capture->n_chests;
    win->n_dungeon_spawners = dungeon_capture->n_spawners;
    memcpy(win->dungeon_chests, dungeon_capture->chests,
           (size_t)dungeon_capture->n_chests * sizeof win->dungeon_chests[0]);
    memcpy(win->dungeon_spawners, dungeon_capture->spawners,
           (size_t)dungeon_capture->n_spawners * sizeof win->dungeon_spawners[0]);
    win->n_mineshaft_carts = dungeon_capture->n_mineshaft_carts;
    win->n_mineshaft_spawners = dungeon_capture->n_mineshaft_spawners;
    memcpy(win->mineshaft_carts, dungeon_capture->mineshaft_carts,
           (size_t)dungeon_capture->n_mineshaft_carts * sizeof win->mineshaft_carts[0]);
    memcpy(win->mineshaft_spawners, dungeon_capture->mineshaft_spawners,
           (size_t)dungeon_capture->n_mineshaft_spawners * sizeof win->mineshaft_spawners[0]);
    win->n_desert_chests = dungeon_capture->n_desert_chests;
    memcpy(win->desert_chests, dungeon_capture->desert_chests,
           (size_t)dungeon_capture->n_desert_chests * sizeof win->desert_chests[0]);
    win->n_jungle_chests = dungeon_capture->n_jungle_chests;
    win->n_jungle_dispensers = dungeon_capture->n_jungle_dispensers;
    memcpy(win->jungle_chests, dungeon_capture->jungle_chests,
           (size_t)dungeon_capture->n_jungle_chests * sizeof win->jungle_chests[0]);
    memcpy(win->jungle_dispensers, dungeon_capture->jungle_dispensers,
           (size_t)dungeon_capture->n_jungle_dispensers * sizeof win->jungle_dispensers[0]);
    win->n_village_chests = dungeon_capture->n_village_chests;
    memcpy(win->village_chests, dungeon_capture->village_chests,
           (size_t)dungeon_capture->n_village_chests * sizeof win->village_chests[0]);
    win->n_village_residents = dungeon_capture->n_village_residents;
    memcpy(win->village_residents, dungeon_capture->village_residents,
           (size_t)dungeon_capture->n_village_residents
               * sizeof win->village_residents[0]);
    win->n_swamp_witches = dungeon_capture->n_swamp_witches;
    win->n_swamp_pots = dungeon_capture->n_swamp_pots;
    memcpy(win->swamp_witches, dungeon_capture->swamp_witches,
           (size_t)dungeon_capture->n_swamp_witches * sizeof win->swamp_witches[0]);
    memcpy(win->swamp_pots, dungeon_capture->swamp_pots,
           (size_t)dungeon_capture->n_swamp_pots * sizeof win->swamp_pots[0]);
    {
        int npop = 0;
        for (int i = 0; i < W_N; ++i) if (g_owr[i] != g_stb[i] || g_seeded[i]) npop++;
        npop += g_oob_n;
        { static int g_maxpop = 0;
          if (popmc_debug_caps() && npop > g_maxpop) {
              g_maxpop = npop;
              fprintf(stderr, "[owrcaps] max_pre_fluid_cells=%d\n", npop);
          } }
        if (npop > g_caps->owr_cells_max) {
            fprintf(stderr,
                "[populate_mc] FATAL: window (%d,%d) has %d pre-fluid cells > cap %d "
                "(raise owr_cells_max in magma.conf / caps.h)\n",
                bcx, bcz, npop, g_caps->owr_cells_max);
            assert(0 && "owr_cells_max exceeded");
            abort();
        }
        win->npop = npop;
        int k = 0;
        for (int lx = 0; lx < W_X; ++lx)
            for (int lz = 0; lz < W_Z; ++lz)
                for (int y = 0; y < W_Y; ++y) {
                    int idx = w_index(lx, y, lz);
                    if (g_owr[idx] != g_stb[idx] || g_seeded[idx]) {
                        win->pop_cells[k].wx = bcx * 16 + lx;
                        win->pop_cells[k].wz = bcz * 16 + lz;
                        win->pop_cells[k].y  = (unsigned short)y;
                        win->pop_cells[k].id = (unsigned short)g_owr[idx];
                        ++k;
                    }
                }
        for (int i = 0; i < g_oob_n; ++i)
            win->pop_cells[k++] = g_oob_spill[i];
    }

    /* Fluid CA default OFF: vanilla populate runs NO fluid ticks - the save's spread
     * water/lava is live-tick evolution. The baked CA (pfs_steps = 16 + seed%17 from the
     * internal stress scene) re-tags sources as flowing en masse (seed 7 swamp: 92k cells
     * java WATER -> magma FLOWING_WATER; disabling: 157k -> 14.6k mismatches, seed 0
     * roughly neutral at +712). Opt back in with MAGMA_FLUID_CA=1. */
    if (getenv("MAGMA_FLUID_CA"))
        owfl_fluid_pass(&w, (i64)seed, g_cur, g_tmp, g_bca);

    /* Record cells = (window != its own pure base) UNION seeded locations UNION
     * OOB spills (absolute world coords outside this 32x32). Spills let neighbor
     * windows seed edge canopies and let apply_window place them on the right chunk. */
    int ndec = 0;
    for (int i = 0; i < W_N; ++i) if (g_owr[i] != g_stb[i] || g_seeded[i]) ndec++;
    ndec += g_oob_n;
    { static int g_maxdec = 0; if (popmc_debug_caps() && ndec > g_maxdec) {
        g_maxdec = ndec; fprintf(stderr, "[owrcaps] max_window_cells=%d\n", ndec); } }

    if (ndec > g_caps->owr_cells_max) {
        fprintf(stderr,
            "[populate_mc] FATAL: window (%d,%d) has %d decoration cells > cap %d "
            "(raise owr_cells_max in magma.conf / caps.h)\n",
            bcx, bcz, ndec, g_caps->owr_cells_max);
        assert(0 && "owr_cells_max exceeded");
        abort();
    }

    win->ncells = ndec;

    int k = 0;
    for (int lx = 0; lx < W_X; ++lx)
        for (int lz = 0; lz < W_Z; ++lz)
            for (int y = 0; y < W_Y; ++y) {
                int idx = w_index(lx, y, lz);
                if (g_owr[idx] != g_stb[idx] || g_seeded[idx]) {
                    win->cells[k].wx = bcx * 16 + lx;
                    win->cells[k].wz = bcz * 16 + lz;
                    win->cells[k].y  = (unsigned short)y;
                    win->cells[k].id = (unsigned short)g_owr[idx];
                    ++k;
                }
            }
    for (int i = 0; i < g_oob_n; ++i)
        win->cells[k++] = g_oob_spill[i];
    --g_dungeon_capture_depth;
    return win;
}

/* Apply the window's decoration cells that fall inside chunk (cx,cz). */
static void apply_window(const Window *win, int cx, int cz, u16 *chunk_block,
                         u8 *chunk_meta) {
    if (!win) return;
    int bx = cx * 16, bz = cz * 16;
    for (int i = 0; i < win->ncells; ++i) {
        const DecCell *c = &win->cells[i];
        if (c->wx < bx || c->wx >= bx + 16 || c->wz < bz || c->wz >= bz + 16) continue;
        int lx = c->wx - bx, lz = c->wz - bz;
        int index = CB_INDEX_LOCAL(lx, c->y, lz);
        chunk_block[index] = c->id;
        if (chunk_meta)
            chunk_meta[index] = c->id >= PB_SANDSTONE_STAIRS_E
                && c->id <= PB_SANDSTONE_STAIRS_N
                ? (u8)(c->id - PB_SANDSTONE_STAIRS_E) : 0;
    }
}

/* Dungeon cells are also donor-seeded into later populate windows. Applying a
 * donor copy clears its legacy metadata, so facing must be restored only after
 * all four contributing windows have applied their final block writes. */
static void apply_dungeon_meta(const Window *win, int cx, int cz,
                               const u16 *chunk_block, u8 *chunk_meta) {
    int bx = cx * 16, bz = cz * 16;
    if (!win || !chunk_meta) return;
    for (int i = 0; i < win->n_dungeon_chests; ++i) {
        const DungeonChestSite *s = &win->dungeon_chests[i];
        int index;
        if (s->wx < bx || s->wx >= bx + 16 ||
            s->wz < bz || s->wz >= bz + 16) continue;
        index = CB_INDEX_LOCAL(s->wx - bx, s->y, s->wz - bz);
        if (chunk_block[index] == PB_CHEST)
            chunk_meta[index] = s->facing_meta;
    }
    for (int i = 0; i < win->n_desert_chests; ++i) {
        const DesertChestSite *s = &win->desert_chests[i];
        int index;
        if (s->wx < bx || s->wx >= bx + 16 ||
            s->wz < bz || s->wz >= bz + 16) continue;
        index = CB_INDEX_LOCAL(s->wx - bx, s->y, s->wz - bz);
        if (chunk_block[index] == PB_CHEST)
            chunk_meta[index] = s->facing_meta;
    }
    for (int i = 0; i < win->n_village_chests; ++i) {
        const DungeonChestSite *s = &win->village_chests[i];
        int index;
        if (s->wx < bx || s->wx >= bx + 16 ||
            s->wz < bz || s->wz >= bz + 16) continue;
        index = CB_INDEX_LOCAL(s->wx - bx, s->y, s->wz - bz);
        if (chunk_block[index] == PB_CHEST)
            chunk_meta[index] = s->facing_meta;
    }
    for (int group = 0; group < 2; ++group) {
        const JungleSite *sites = group
            ? win->jungle_dispensers : win->jungle_chests;
        int count = group
            ? win->n_jungle_dispensers : win->n_jungle_chests;
        for (int i = 0; i < count; ++i) {
            const JungleSite *s = &sites[i];
            int index;
            if (s->wx < bx || s->wx >= bx + 16
                    || s->wz < bz || s->wz >= bz + 16) continue;
            index = CB_INDEX_LOCAL(s->wx - bx, s->y, s->wz - bz);
            if ((!group && chunk_block[index] == PB_CHEST)
                    || (group && pb_tag_id(chunk_block[index]) == 23))
                chunk_meta[index] = s->facing_meta;
        }
    }
}

/* =============================== public API ============================== */

void popmc_set_villages(int enabled) {
    enabled = !!enabled;
    if (enabled == g_villages_enabled) return;
    g_villages_enabled = enabled;
    owr_village_hook = enabled ? village_population_hook : NULL;
    memset(g_villages_cache, 0, sizeof g_villages_cache);
    if (g_slots)
        for (int i = 0; i < g_owr_slots; ++i) g_slots[i].valid = 0;
}

void popmc_decorate_chunk(long long seed, int cx, int cz, unsigned short *chunk_block) {
    popmc_decorate_chunk_meta(seed, cx, cz, chunk_block, NULL);
}

void popmc_decorate_chunk_meta(long long seed, int cx, int cz,
                               unsigned short *chunk_block,
                               unsigned char *chunk_meta) {
    Window *wins[4];
    int nwins = 0;
    if (!chunk_block) return;
    /* Chunk (cx,cz) is decorated by the four populate windows whose +8 footprint
     * overlaps it: base chunks (bcx,bcz) in {cx-1,cx} x {cz-1,cz}. OOB spills from
     * earlier neighbors arrive via donor seeding into those windows' cell lists. */
    for (int bcx = cx - 1; bcx <= cx; ++bcx)
        for (int bcz = cz - 1; bcz <= cz; ++bcz) {
            Window *win = find_window(seed, bcx, bcz);
            if (!win) win = build_window(seed, bcx, bcz);
            wins[nwins++] = win;
            apply_window(win, cx, cz, chunk_block, chunk_meta);
        }
    if (chunk_meta)
        for (int i = 0; i < nwins; ++i)
            apply_dungeon_meta(wins[i], cx, cz, chunk_block, chunk_meta);
}

int popmc_dungeon_chest_info(long long seed, int x, int y, int z,
                             long long *loot_seed, int *facing_meta) {
    const DungeonChestSite *best = NULL;
    long best_seq = -1;
    if (!g_slots || y < 0 || y >= W_Y) return 0;
    for (int wi = 0; wi < g_owr_slots; ++wi) {
        const Window *w = &g_slots[wi];
        if (!w->valid || w->seed != seed || w->seq < best_seq) continue;
        for (int i = 0; i < w->n_dungeon_chests; ++i) {
            const DungeonChestSite *s = &w->dungeon_chests[i];
            if (s->wx == x && (int)s->y == y && s->wz == z) {
                best = s;
                best_seq = w->seq;
            }
        }
    }
    if (!best) return 0;
    if (loot_seed) *loot_seed = best->loot_seed;
    if (facing_meta) *facing_meta = (int)best->facing_meta;
    return 1;
}

int popmc_desert_chest_info(long long seed, int x, int y, int z,
                            long long *loot_seed, int *facing_meta) {
    const DesertChestSite *best = NULL;
    long best_seq = -1;
    if (!g_slots || y < 0 || y >= W_Y) return 0;
    for (int wi = 0; wi < g_owr_slots; ++wi) {
        const Window *w = &g_slots[wi];
        if (!w->valid || w->seed != seed || w->seq < best_seq) continue;
        for (int i = 0; i < w->n_desert_chests; ++i) {
            const DesertChestSite *s = &w->desert_chests[i];
            if (s->wx == x && (int)s->y == y && s->wz == z) {
                best = s;
                best_seq = w->seq;
            }
        }
    }
    if (!best) return 0;
    if (loot_seed) *loot_seed = best->loot_seed;
    if (facing_meta) *facing_meta = (int)best->facing_meta;
    return 1;
}

static int popmc_jungle_site_info(long long seed, int x, int y, int z,
                                  int dispenser, long long *loot_seed,
                                  int *facing_meta) {
    const JungleSite *best = NULL;
    long best_seq = -1;
    if (!g_slots || y < 0 || y >= W_Y) return 0;
    for (int wi = 0; wi < g_owr_slots; ++wi) {
        const Window *w = &g_slots[wi];
        const JungleSite *sites = dispenser
            ? w->jungle_dispensers : w->jungle_chests;
        int count = dispenser
            ? w->n_jungle_dispensers : w->n_jungle_chests;
        if (!w->valid || w->seed != seed || w->seq < best_seq) continue;
        for (int i = 0; i < count; ++i) {
            const JungleSite *s = &sites[i];
            if (s->wx == x && (int)s->y == y && s->wz == z) {
                best = s;
                best_seq = w->seq;
            }
        }
    }
    if (!best) return 0;
    if (loot_seed) *loot_seed = best->loot_seed;
    if (facing_meta) *facing_meta = (int)best->facing_meta;
    return 1;
}

int popmc_jungle_chest_info(long long seed, int x, int y, int z,
                            long long *loot_seed, int *facing_meta) {
    return popmc_jungle_site_info(
        seed, x, y, z, 0, loot_seed, facing_meta);
}

int popmc_village_chest_info(long long seed, int x, int y, int z,
                             long long *loot_seed, int *facing_meta) {
    const DungeonChestSite *best = NULL;
    long best_seq = -1;
    if (!g_slots || y < 0 || y >= W_Y) return 0;
    for (int wi = 0; wi < g_owr_slots; ++wi) {
        const Window *w = &g_slots[wi];
        if (!w->valid || w->seed != seed || w->seq < best_seq) continue;
        for (int i = 0; i < w->n_village_chests; ++i) {
            const DungeonChestSite *site = &w->village_chests[i];
            if (site->wx == x && (int)site->y == y && site->wz == z) {
                best = site;
                best_seq = w->seq;
            }
        }
    }
    if (!best) return 0;
    if (loot_seed) *loot_seed = best->loot_seed;
    if (facing_meta) *facing_meta = (int)best->facing_meta;
    return 1;
}

int popmc_village_residents(long long seed,
                            int min_x, int min_z, int max_x, int max_z,
                            PopmcVillageResident *out, int capacity) {
    int count = 0;
    if (!g_slots || !out || capacity <= 0) return 0;
    for (int wi = 0; wi < g_owr_slots && count < capacity; ++wi) {
        const Window *w = &g_slots[wi];
        if (!w->valid || w->seed != seed) continue;
        for (int i = 0; i < w->n_village_residents && count < capacity; ++i) {
            const VillageResidentSite *site = &w->village_residents[i];
            int duplicate = 0;
            if (site->wx < min_x || site->wx > max_x
                    || site->wz < min_z || site->wz > max_z) continue;
            for (int j = 0; j < count; ++j)
                if (out[j].x == site->wx && out[j].y == (int)site->y
                        && out[j].z == site->wz) {
                    duplicate = 1;
                    break;
                }
            if (duplicate) continue;
            out[count].x = site->wx;
            out[count].y = (int)site->y;
            out[count].z = site->wz;
            out[count].profession = (int)site->profession;
            out[count].zombie_infested = (int)site->zombie_infested;
            ++count;
        }
    }
    return count;
}

int popmc_jungle_dispenser_info(long long seed, int x, int y, int z,
                                long long *loot_seed, int *facing_meta) {
    return popmc_jungle_site_info(
        seed, x, y, z, 1, loot_seed, facing_meta);
}

static const SwampSite *popmc_swamp_site_info(
        long long seed, int x, int y, int z, int pot) {
    const SwampSite *best = NULL;
    long best_seq = -1;
    if (!g_slots || y < 0 || y >= W_Y) return NULL;
    for (int wi = 0; wi < g_owr_slots; ++wi) {
        const Window *w = &g_slots[wi];
        const SwampSite *sites = pot ? w->swamp_pots : w->swamp_witches;
        int count = pot ? w->n_swamp_pots : w->n_swamp_witches;
        if (!w->valid || w->seed != seed || w->seq < best_seq) continue;
        for (int i = 0; i < count; ++i) {
            const SwampSite *s = &sites[i];
            if (s->wx == x && (int)s->y == y && s->wz == z) {
                best = s;
                best_seq = w->seq;
            }
        }
    }
    return best;
}

int popmc_swamp_witch_info(long long seed, int x, int y, int z) {
    return popmc_swamp_site_info(seed, x, y, z, 0) != NULL;
}

int popmc_swamp_pot_info(long long seed, int x, int y, int z,
                         int *item, int *meta) {
    const SwampSite *site = popmc_swamp_site_info(seed, x, y, z, 1);
    if (!site) return 0;
    if (item) *item = (int)site->value;
    if (meta) *meta = (int)site->meta;
    return 1;
}

int popmc_dungeon_spawner_info(long long seed, int x, int y, int z,
                               int *mob_kind) {
    const DungeonSpawnerSite *best = NULL;
    long best_seq = -1;
    if (!g_slots || y < 0 || y >= W_Y) return 0;
    for (int wi = 0; wi < g_owr_slots; ++wi) {
        const Window *w = &g_slots[wi];
        if (!w->valid || w->seed != seed || w->seq < best_seq) continue;
        for (int i = 0; i < w->n_dungeon_spawners; ++i) {
            const DungeonSpawnerSite *s = &w->dungeon_spawners[i];
            if (s->wx == x && (int)s->y == y && s->wz == z) {
                best = s;
                best_seq = w->seq;
            }
        }
    }
    if (!best) return 0;
    if (mob_kind) {
        /* Forge 1.11.2 DungeonHooks registration order: skeleton 100,
         * zombie 200, spider 100. WeightedRandom subtracts in order. */
        *mob_kind = best->roll < 100 ? POPMC_DUNGEON_MOB_SKELETON
                  : best->roll < 300 ? POPMC_DUNGEON_MOB_ZOMBIE
                  : POPMC_DUNGEON_MOB_SPIDER;
    }
    return 1;
}

int popmc_mineshaft_cart_info(long long seed, int x, int y, int z,
                              long long *loot_seed) {
    const MineshaftCartSite *best = NULL;
    long best_seq = -1;
    if (!g_slots || y < 0 || y >= W_Y) return 0;
    for (int wi = 0; wi < g_owr_slots; ++wi) {
        const Window *w = &g_slots[wi];
        if (!w->valid || w->seed != seed || w->seq < best_seq) continue;
        for (int i = 0; i < w->n_mineshaft_carts; ++i) {
            const MineshaftCartSite *s = &w->mineshaft_carts[i];
            if (s->wx == x && (int)s->y == y && s->wz == z) {
                best = s;
                best_seq = w->seq;
            }
        }
    }
    if (!best) return 0;
    if (loot_seed) *loot_seed = best->loot_seed;
    return 1;
}

int popmc_mineshaft_spawner_info(long long seed, int x, int y, int z) {
    const MineshaftSpawnerSite *best = NULL;
    long best_seq = -1;
    if (!g_slots || y < 0 || y >= W_Y) return 0;
    for (int wi = 0; wi < g_owr_slots; ++wi) {
        const Window *w = &g_slots[wi];
        if (!w->valid || w->seed != seed || w->seq < best_seq) continue;
        for (int i = 0; i < w->n_mineshaft_spawners; ++i) {
            const MineshaftSpawnerSite *s = &w->mineshaft_spawners[i];
            if (s->wx == x && (int)s->y == y && s->wz == z) {
                best = s;
                best_seq = w->seq;
            }
        }
    }
    return best != NULL;
}

long popmc_window_builds(void) { return g_builds; }

void popmc_set_probe(void (*fn)(int, int, const char *, const char *,
                                unsigned long long)) {
    mc_probe_fn = fn;
}

/* Nested cascade populate (fires from jrand_next via mc_jr_watch_hookfn at the
 * EXACT interrupted draw): vanilla generated the touched chunk and then cascade-
 * populated a newly-2x2-complete neighbor right here, mid-parent-populate. Build
 * that neighbor's window NOW - seeded with the parent's PARTIAL window (vanilla's
 * shared world) - and copy its cells back so the parent's remaining block-
 * conditional draws (jungle vines, reeds, cocoa) see them. Depth 1 only: a
 * deeper cascade keeps the cursor jump but skips block emulation. */
static void cascade_hook(unsigned long long before) {
    if (g_casc_depth) return;
    if (g_redir_win) return;  /* already redirected: g2 buffers are LIVE (parent tail is
                               * writing them) - keep the cursor jump, skip block emulation */
    int pbcx = mc_probe_cx, pbcz = mc_probe_cz;
    const PopmcCascadeEvt *e = NULL;
    for (int i = 0; i < g_ncasc; ++i)
        if (g_casc[i].bcx == pbcx && g_casc[i].bcz == pbcz &&
            g_casc[i].before == before) { e = &g_casc[i]; break; }
    if (!e || e->ncx == POPMC_CASCADE_NONE) return;

    /* save parent watch state (nested build re-arms for its own events) */
    int save_n = mc_jr_watch_n, save_fired = mc_jr_watch_fired;
    u64 save_b[MC_JR_WATCH_MAX], save_a[MC_JR_WATCH_MAX];
    memcpy(save_b, mc_jr_watch_before, sizeof(save_b));
    memcpy(save_a, mc_jr_watch_after, sizeof(save_a));

    /* expose the parent's in-progress window as the latest-writer donor */
    g_partial.active = 1;
    g_partial.bcx = pbcx; g_partial.bcz = pbcz;
    g_partial.owr = g_owr; g_partial.stb = g_stb; g_partial.seeded = g_seeded;

    /* swap the live-during-populate buffers to the depth-1 set */
    FoliageCoord *s_fol = g_fol;
    u16 *s_owr = g_owr, *s_stb = g_stb;
    u8 *s_sky = g_sky, *s_blk = g_blk, *s_ts = g_ts, *s_tb = g_tb, *s_seeded = g_seeded;
    u16 *s_skyhm = g_skyhm;
    u8 *s_skylt = g_skylt;
    int *s_bio = g_bio;
    g_fol = g2_fol; g_owr = g2_owr; g_stb = g2_stb; g_sky = g2_sky; g_blk = g2_blk;
    g_ts = g2_ts; g_tb = g2_tb; g_skyhm = g2_skyhm; g_skylt = g2_skylt;
    g_seeded = g2_seeded; g_bio = g2_bio;

    g_casc_depth = 1;
    World *s_live_w = g_live_w;
    /* WorldGenBigTree.heightLimit is a SESSION singleton in vanilla: the nested
     * populate must see the parent's current value and the parent's remaining
     * big trees must see the nested evolution (foliage geometry depends on it;
     * the main RNG stream does not, so only blocks betray a stale carry). */
    if (g_live_w) g_bigtree_carry = g_live_w->bigtree_heightLimit;
    Window *nb = build_window(g_build_seed, e->ncx, e->ncz);
    g_live_w = s_live_w;
    if (g_live_w) g_live_w->bigtree_heightLimit = g_bigtree_carry;
    g_casc_depth = 0;

    g_fol = s_fol; g_owr = s_owr; g_stb = s_stb; g_sky = s_sky; g_blk = s_blk;
    g_ts = s_ts; g_tb = s_tb; g_skyhm = s_skyhm; g_skylt = s_skylt;
    g_seeded = s_seeded; g_bio = s_bio;
    g_partial.active = 0;
    mc_probe_cx = pbcx;
    mc_probe_cz = pbcz;
    mc_jr_watch_n = save_n;
    mc_jr_watch_fired = save_fired;
    memcpy(mc_jr_watch_before, save_b, sizeof(save_b));
    memcpy(mc_jr_watch_after, save_a, sizeof(save_a));

    if (nb) {   /* copy nested cells overlapping the parent window back in */
        int wx0 = pbcx * 16, wz0 = pbcz * 16;
        for (int i = 0; i < nb->npop; ++i) {
            const DecCell *c = &nb->pop_cells[i];
            int lx = c->wx - wx0, lz = c->wz - wz0;
            if (lx < 0 || lx >= W_X || lz < 0 || lz >= W_Z) continue;
            int idx = w_index(lx, c->y, lz);
            g_owr[idx] = c->id;
            g_seeded[idx] = 1;
            skyhm_copy_nested_col_to_parent(pbcx, pbcz, e->ncx, e->ncz, c->wx, c->wz);
        }
        fprintf(stderr, "[populate_mc] cascade (%d,%d): nested populate (%d,%d) emulated\n",
                pbcx, pbcz, e->ncx, e->ncz);
        /* Vanilla chunkPos corruption: the nested decorate() overwrote the shared
         * BiomeDecorator.chunkPos and it is NEVER restored - the parent's remaining
         * decoration positions are relative to the NESTED chunk's corner (proven by
         * seed-1 (9,5) TreeProbeDecorator trace: post-resume attempts log under
         * (10,5) with (10,5)-frame heights). Only the SAME Biome instance shares the
         * decorator, so arm the deferred swap only when the nested decorate biome
         * matches the parent's (window-center voronoi ids). redirect_apply fires at
         * the next position draw (MC_REDIRECT_POLL); the nested slot is then
         * re-snapshotted when the parent's populate returns (build_window). Foliage
         * coords and the light windows keep the parent frame - cosmetic only. */
        if (g_live_w && g2_bio[16 * W_Z + 16] == g_bio[16 * W_Z + 16]) {
            g_redir_ncx = e->ncx;
            g_redir_ncz = e->ncz;
            g_redir_pbcx = pbcx;
            g_redir_pbcz = pbcz;
            g_redir_win = nb;
            mc_redirect_pending = 1;
        }
    }
    /* Tell the probe log the parent's populate stream resumes here (the C log has
     * no POST lines, so genprobe_diff needs this to re-attribute what follows). */
    if (mc_probe_fn) mc_probe_fn(pbcx, pbcz, "RESUME", "-", 0ULL);
}

void popmc_set_cascade(const PopmcCascadeEvt *evts, int n) {
    g_casc = evts;
    g_ncasc = n;
    mc_jr_watch_hookfn = n ? cascade_hook : (void (*)(unsigned long long))0;
    mc_redirect_apply = n ? redirect_apply : (void (*)(struct World *))0;
    mc_redirect_restore = n ? redirect_restore : (void (*)(struct World *))0;
}

void popmc_prepare(long long seed, const int *bases_xz, int nbases) {
    for (int i = 0; i < nbases; ++i) {
        int bcx = bases_xz[i * 2], bcz = bases_xz[i * 2 + 1];
        if (!find_window(seed, bcx, bcz)) build_window(seed, bcx, bcz);
    }
}

int popmc_opacity(int id) { return pb_opacity(id); }

int popmc_emission(int id) {
    return pb_isLava(id) ? 15 : 0;   /* only lava emits in this block set */
}
