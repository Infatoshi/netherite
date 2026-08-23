/* hostile_spawn.h - Java 1.11.2 WorldEntitySpawner MONSTER cycle.
 *
 * Magma magma/game/mob_live.c and blaze-CPU/CUDA compile this one source.
 * Magma wrappers stay thin; do not re-derive the pack loop.
 *
 * Java 1.11.2 (java/oracle-src):
 *   WorldServer.tick                         WorldServer.java:180-206
 *     doMobSpawning then findChunksForSpawning before chunkProvider /
 *     skylightSubtracted / totalWorldTime++ / updateBlocks
 *   WorldEntitySpawner.MOB_COUNT_DIV         WorldEntitySpawner.java:27  17^2=289
 *   findChunksForSpawning                    WorldEntitySpawner.java:36-191
 *     player chunk radius 8, border ring counted in `i` but not eligible
 *     MONSTER cap 70 * i / 289               EnumCreatureType.java:12
 *     Collections.shuffle of eligible set    WorldEntitySpawner.java:92-93
 *     getRandomChunkPosition                 WorldEntitySpawner.java:193-201
 *     3 tries, pack ceil(Math.random()*4)    WorldEntitySpawner.java:109,117
 *     world.rand nextInt(6)-nextInt(6) xz    WorldEntitySpawner.java:121-123
 *     nextInt(1)-nextInt(1) y (always 0)
 *     24-block player / 24-block spawn pt    WorldEntitySpawner.java:128  576.0D
 *     getSpawnListEntryForTypeAt             WorldServer.java:245-249
 *     WeightedRandom.getRandomItem           WeightedRandom.java:28-37,62-64
 *     canCreatureTypeSpawnAtLocation         WorldEntitySpawner.java:208-238
 *     isValidEmptySpawnBlock                 WorldEntitySpawner.java:203-206
 *     EntityMob.getCanSpawnHere              EntityMob.java:186-188
 *     isValidLightLevel                      EntityMob.java:159-180
 *     EntityLiving.getCanSpawnHere           EntityLiving.java:940-944
 *     isNotColliding                         EntityLiving.java:949-952
 *     onInitialSpawn                         EntityLiving.java:1256-1269
 *       EntityZombie                         EntityZombie.java:483-555
 *       AbstractSkeleton                     AbstractSkeleton.java:210-229
 *     Entity constructor UUID                Entity.java:238-241
 *       MathHelper.getRandomUUID             two nextLong on entity.rand
 *   EnumCreatureType.MONSTER max 70          EnumCreatureType.java:12
 *   Biome default monster list               Biome.java:146-153
 *   MathHelper.roundUp                       MathHelper.java:376-396
 *
 * Isolated World.rand: live magma/blaze already split World.rand (weather has
 * ww.rand). Spawn uses its own JavaRandom seeded from the world seed so the
 * weather_optional row stays bit-identical. Math.random() and
 * Collections.shuffle are separate java.util.Random instances in Java
 * (WorldEntitySpawner.java:93,117); they are isolated JavaRandoms here too,
 * salted from the world seed, not world.rand.
 *
 * Entity.rand is `new Random()` (Entity.java:238), not world.rand. Lockstep
 * seeds it from (world seed, tick, packed pos, attempt) without consuming
 * world.rand.
 *
 * Live table cap: EW_MAX_ENTITIES / BLAZE_SNAP_MAX_MOBS. When the table is
 * full the spawn is skipped on BOTH sides. That is a magma/blaze shared cap,
 * not a Java rule.
 *
 * Roster insert is zombie/skeleton/creeper/spider/slime/enderman.
 * Witch / zombie villager / stray picks still consume world.rand /
 * entity.rand in Java order, then skip the insert.
 *
 * Include after HS_W / HS_BLOCK. Optional: HS_SKY, HS_BLK, HS_PLACE,
 * HS_HOSTILE_COUNT, HS_MOB_HIT, HS_HEIGHT.
 */
#ifndef MC_HOSTILE_SPAWN_H
#define MC_HOSTILE_SPAWN_H

#include <math.h>
#include <string.h>

#include "mc.h"
#include "mc_math.h"
#include "mc_rng.h"
#include "block_props_table.h"
#include "entity_hostile_spine.h"

#ifndef HS_W
#error "hostile_spawn.h requires HS_W"
#endif
#ifndef HS_BLOCK
#error "hostile_spawn.h requires HS_BLOCK(w,x,y,z)"
#endif

#ifndef HS_SKY
#define HS_SKY(w, x, y, z) 0
#endif
#ifndef HS_BLK
#define HS_BLK(w, x, y, z) 0
#endif
#ifndef HS_HOSTILE_COUNT
#define HS_HOSTILE_COUNT(w) 0
#endif
#ifndef HS_CREATURE_COUNT
#define HS_CREATURE_COUNT(w) 0
#endif
#ifndef HS_MOB_HIT
#define HS_MOB_HIT(w, x0, y0, z0, x1, y1, z1) 0
#endif
#ifndef HS_PLACE
#define HS_PLACE(w, type, x, y, z, yaw, seed48, have_g, g, extra) 0
#endif

#define HS_MOB_COUNT_DIV 289          /* WorldEntitySpawner.java:27 */
#define HS_MONSTER_CAP 70             /* EnumCreatureType.java:12 */
#define HS_CREATURE_CAP 10            /* EnumCreatureType.java:13 */
#define HS_CHUNK_RADIUS 8             /* WorldEntitySpawner.java:52 */
#define HS_PLAYER_RANGE 24.0          /* WorldEntitySpawner.java:128 */
#define HS_SPAWN_PT_RANGE_SQ 576.0    /* WorldEntitySpawner.java:128 24^2 */
#define HS_MAX_ELIGIBLE 225           /* 15x15 interior of the 17x17 */
#define HS_MAX_PACK 4                 /* EntityLiving.java:967 */
#define HS_TABLE_CAP EW_MAX_ENTITIES  /* magma/blaze shared, not Java */

/* Biome.java:146-153 default monster list, list order = WeightedRandom order. */
enum {
    HS_SPIDER = 0,
    HS_ZOMBIE = 1,
    HS_ZOMBIE_VILLAGER = 2,
    HS_SKELETON = 3,
    HS_CREEPER = 4,
    HS_SLIME = 5,
    HS_ENDERMAN = 6,
    HS_WITCH = 7,
    HS_STRAY = 8,                    /* BiomeSnow.java:49; not on the live roster */
    HS_NTYPES = 9
};

#ifndef HS_BIOME
#define HS_BIOME(w, x, z) 1              /* plains. Override from the snapshot plane. */
#endif
#define HS_BIOME_SWAMP 6
#define HS_BIOME_OCEAN 0
#define HS_BIOME_RIVER 7
#define HS_BIOME_FROZEN_OCEAN 10
#define HS_BIOME_ICE_PLAINS 12
#define HS_BIOME_ICE_MOUNTAINS 13
#define HS_BIOME_BEACH 16
#define HS_BIOME_STONE_BEACH 25
#define HS_BIOME_COLD_BEACH 26
#define HS_BIOME_DEEP_OCEAN 24
#define HS_BIOME_MESA 37
#define HS_BIOME_MESA_ROCK 38
#define HS_BIOME_MESA_CLEAR 39

/* Biome.java:146-153 weights. A function, not a host array: nvcc device. */
MC_HD static inline int hs_weight_at(int i) {
    if (i == HS_SPIDER) return 100;
    if (i == HS_ZOMBIE) return 95;
    if (i == HS_ZOMBIE_VILLAGER) return 5;
    if (i == HS_SKELETON) return 100;
    if (i == HS_CREEPER) return 100;
    if (i == HS_SLIME) return 100;
    if (i == HS_ENDERMAN) return 10;
    if (i == HS_WITCH) return 5;
    return 0;                             /* stray 0 on the default list */
}
#define HS_TOTAL_WEIGHT 515

MC_HD static inline int hs_is_snow_biome(int biome) {
    /* BiomeSnow.java:23-49 covers ice plains (12) and ice mountains (13). */
    return biome == HS_BIOME_ICE_PLAINS || biome == HS_BIOME_ICE_MOUNTAINS;
}

/* Snapshot lockstep: outside the region AABB, HS_BIOME is plains 1. Java
 * uses the biome at the candidate BlockPos (WorldEntitySpawner.java:132-133
 * -> WorldServer.java:245-249 -> Chunk.getBiome Chunk.java:1273-1278). Magma
 * gm_world_rt_block clips the same way (world_live.c:705-708). */
MC_HD static inline int hs_biome_or_plains(int in_region, int biome) {
    if (!in_region) return 1;
    return biome < 0 ? 1 : biome;
}

/* WeightedRandom walks the SpawnListEntry list in add-order, not type-id
 * order. BiomeSwamp.java:34 appends a second slime (weight 1) after witch.
 * BiomeSnow.java:36-49 removes skeleton then appends skeleton 20 + stray 80.
 * Combining swamp slime to 101 in the Biome.java slot is not equivalent:
 * the extra entry sits after enderman+witch, so the w ranges differ. */
MC_HD static inline int hs_monster_entry_count(int biome) {
    if (biome == HS_BIOME_SWAMP) return 9;
    if (hs_is_snow_biome(biome)) return 9;
    return 8;
}

MC_HD static inline int hs_monster_entry_type(int biome, int i) {
    if (hs_is_snow_biome(biome)) {
        if (i == 0) return HS_SPIDER;
        if (i == 1) return HS_ZOMBIE;
        if (i == 2) return HS_ZOMBIE_VILLAGER;
        if (i == 3) return HS_CREEPER;     /* skeleton removed :42-45 */
        if (i == 4) return HS_SLIME;
        if (i == 5) return HS_ENDERMAN;
        if (i == 6) return HS_WITCH;
        if (i == 7) return HS_SKELETON;    /* re-added weight 20 :48 */
        if (i == 8) return HS_STRAY;       /* :49 */
        return HS_ZOMBIE;
    }
    if (i == 0) return HS_SPIDER;
    if (i == 1) return HS_ZOMBIE;
    if (i == 2) return HS_ZOMBIE_VILLAGER;
    if (i == 3) return HS_SKELETON;
    if (i == 4) return HS_CREEPER;
    if (i == 5) return HS_SLIME;
    if (i == 6) return HS_ENDERMAN;
    if (i == 7) return HS_WITCH;
    if (i == 8) return HS_SLIME;           /* BiomeSwamp.java:34 */
    return HS_ZOMBIE;
}

MC_HD static inline int hs_monster_entry_weight(int biome, int i) {
    int t = hs_monster_entry_type(biome, i);
    if (hs_is_snow_biome(biome)) {
        if (t == HS_SKELETON) return 20;   /* BiomeSnow.java:48 */
        if (t == HS_STRAY) return 80;      /* BiomeSnow.java:49 */
    }
    if (biome == HS_BIOME_SWAMP && i == 8) return 1;
    return hs_weight_at(t);
}

MC_HD static inline int hs_total_weight(int biome) {
    return HS_TOTAL_WEIGHT + (biome == HS_BIOME_SWAMP ? 1 : 0);
}

/* Type-indexed default weight. Swamp extra slime is a second list entry,
 * not 101 on HS_SLIME. Ice plains skeleton is 20 only on the snow list. */
MC_HD static inline int hs_weight_at_biome(int i, int biome) {
    (void)biome;
    return hs_weight_at(i);
}

typedef struct {
    JavaRandom world_rand;            /* World.rand, isolated spawn stream */
    JavaRandom math_rand;             /* Math.random() at :117 */
    JavaRandom shuffle_rand;          /* Collections.shuffle at :93 */
    i64 seed;
    i64 world_time;
    int difficulty;                   /* EnumDifficulty.NORMAL = 2 */
    int skylight_sub;
    int thundering;
    double spawn_x, spawn_y, spawn_z;
} HsState;

MC_HD static inline void hs_init(HsState *s, i64 seed) {
    if (!s) return;
    memset(s, 0, sizeof *s);
    jrand_set(&s->world_rand, seed);
    jrand_set(&s->math_rand, seed ^ (i64)0x4D415448);   /* "MATH" */
    jrand_set(&s->shuffle_rand, seed ^ (i64)0x5348464C); /* "SHFL" */
    s->seed = seed;
    s->difficulty = 2;
    s->spawn_y = 64.0;
}

/* MathHelper.roundUp MathHelper.java:376-396. */
MC_HD static inline int hs_round_up(int number, int interval) {
    int i;
    if (interval == 0) return 0;
    if (number == 0) return interval;
    if (number < 0) interval = -interval;
    i = number % interval;
    return i == 0 ? number : number + interval - i;
}

/* MathHelper.ceil(double) used at WorldEntitySpawner.java:117. */
MC_HD static inline int hs_ceil_d(double v) {
    int i = (int)v;
    return v > (double)i ? i + 1 : i;
}

MC_HD static inline int hs_monster_cap(int chunk_count_i) {
    return HS_MONSTER_CAP * chunk_count_i / HS_MOB_COUNT_DIV;
}

MC_HD static inline int hs_is_roster(int hs_type) {
    return hs_type == HS_ZOMBIE || hs_type == HS_SKELETON || hs_type == HS_CREEPER
        || hs_type == HS_SPIDER || hs_type == HS_SLIME
        || hs_type == HS_ENDERMAN;
}

MC_HD static inline int hs_to_ew(int hs_type) {
    if (hs_type == HS_ZOMBIE) return EW_TYPE_ZOMBIE;
    if (hs_type == HS_SKELETON) return EW_TYPE_SKELETON;
    if (hs_type == HS_CREEPER) return EW_TYPE_CREEPER;
    if (hs_type == HS_SPIDER) return EW_TYPE_SPIDER;
    if (hs_type == HS_ENDERMAN) return EW_TYPE_ENDERMAN;
    if (hs_type == HS_SLIME) return EW_TYPE_SLIME;
    return EW_TYPE_NONE;
}

/* WeightedRandom.getRandomItem WeightedRandom.java:28-37 then :41-56. */
MC_HD static inline int hs_weighted_pick_biome(JavaRandom *r, int biome) {
    int w, i, tot, n, t;
    if (!r) return HS_ZOMBIE;
    tot = hs_total_weight(biome);
    n = hs_monster_entry_count(biome);
    w = jrand_int_bound(r, tot);
    for (i = 0; i < n; ++i) {
        t = hs_monster_entry_type(biome, i);
        w -= hs_monster_entry_weight(biome, i);
        if (w < 0) return t;
    }
    return HS_ZOMBIE;
}

MC_HD static inline int hs_weighted_pick(JavaRandom *r) {
    return hs_weighted_pick_biome(r, 1);  /* plains */
}

MC_HD static inline int hs_is_rail(int id) {
    return id == 27 || id == 28 || id == 66 || id == 157;
}

MC_HD static inline int hs_can_provide_power(int id) {
    return id == 55 || id == 69 || id == 70 || id == 72 || id == 75 || id == 76
        || id == 77 || id == 93 || id == 94 || id == 131 || id == 132
        || id == 143 || id == 149 || id == 150 || id == 152;
}

MC_HD static inline int hs_is_normal_cube(int id) {
    BptProps p;
    if (id == 0) return 0;
    p = mc_bpt_props(id);
    if (p.flags & BF_LIQUID) return 0;
    /* Block.isBlockNormalCube: blocksMovement && isFullCube. KEEP cubes with
     * light_opacity 255 are full; slabs/ladders in the table have opacity 0. */
    return (p.flags & BF_SOLID) && p.light_opacity == 255;
}

MC_HD static inline int hs_is_side_solid_up(int id) {
    return hs_is_normal_cube(id);
}

/* WorldEntitySpawner.isValidEmptySpawnBlock :203-206. */
MC_HD static inline int hs_valid_empty(int id) {
    BptProps p;
    if (hs_is_normal_cube(id)) return 0;
    if (hs_can_provide_power(id)) return 0;
    p = mc_bpt_props(id);
    if (p.flags & BF_LIQUID) return 0;
    if (hs_is_rail(id)) return 0;
    return 1;
}

/* WorldEntitySpawner.canCreatureTypeSpawnAtLocation ON_GROUND :223-235. */
MC_HD static inline int hs_can_spawn_at(HS_W *w, int x, int y, int z) {
    int down, here, up;
    if (y < 1 || y > 254) return 0;
    down = HS_BLOCK(w, x, y - 1, z);
    if (!hs_is_side_solid_up(down)) return 0;
    if (down == BLK_BEDROCK || down == 166) return 0; /* BEDROCK / BARRIER */
    here = HS_BLOCK(w, x, y, z);
    up = HS_BLOCK(w, x, y + 1, z);
    return hs_valid_empty(here) && hs_valid_empty(up);
}

MC_HD static inline int hs_combined_light(HS_W *w, int x, int y, int z, int sub) {
    int sky, blk, i;
    (void)w; (void)x; (void)y; (void)z;
    sky = HS_SKY(w, x, y, z);
    blk = HS_BLK(w, x, y, z);
    i = sky - sub;
    if (i < 0) i = 0;
    if (blk > i) i = blk;
    return i;
}

/* EntityMob.isValidLightLevel EntityMob.java:159-180. Uses entity.rand. */
MC_HD static inline int hs_valid_light(HS_W *w, JavaRandom *er, int x, int y, int z,
                                       int sub, int thundering) {
    int sky, i;
    if (!er) return 0;
    sky = HS_SKY(w, x, y, z);
    if (sky > jrand_int_bound(er, 32)) return 0;
    if (thundering) sub = 10;                 /* EntityMob.java:171-176 */
    i = hs_combined_light(w, x, y, z, sub);
    return i <= jrand_int_bound(er, 8);
}

MC_HD static inline int hs_aabb_solid_or_liquid(HS_W *w,
                                                double minx, double miny, double minz,
                                                double maxx, double maxy, double maxz) {
    int x0, y0, z0, x1, y1, z1, x, y, z, id;
    BptProps p;
    x0 = mc_floor(minx);
    y0 = mc_floor(miny);
    z0 = mc_floor(minz);
    x1 = mc_floor(maxx);
    y1 = mc_floor(maxy);
    z1 = mc_floor(maxz);
    if (y0 < 0) y0 = 0;
    if (y1 > 255) y1 = 255;
    for (x = x0; x <= x1; ++x)
        for (y = y0; y <= y1; ++y)
            for (z = z0; z <= z1; ++z) {
                id = HS_BLOCK(w, x, y, z);
                if (id == 0) continue;
                p = mc_bpt_props(id);
                if (p.flags & BF_LIQUID) return 1;
                if ((p.flags & BF_SOLID) && p.light_opacity == 255) return 1;
            }
    return 0;
}

/* EntityLiving.isNotColliding EntityLiving.java:949-952 (blocks + liquids +
 * other living AABBs; player is 24 blocks away by construction). */
MC_HD static inline int hs_not_colliding(HS_W *w,
                                         double minx, double miny, double minz,
                                         double maxx, double maxy, double maxz) {
    if (hs_aabb_solid_or_liquid(w, minx, miny, minz, maxx, maxy, maxz))
        return 0;
    if (HS_MOB_HIT(w, minx, miny, minz, maxx, maxy, maxz))
        return 0;
    return 1;
}

#ifndef HS_HEIGHT
MC_HD static inline int hs_height_scan(HS_W *w, int x, int z) {
    int y;
    for (y = 255; y >= 0; --y) {
        int id = HS_BLOCK(w, x, y, z);
        if (id && mc_bpt_props(id).light_opacity != 0) return y + 1;
    }
    return 0;
}
#define HS_HEIGHT(w, x, z) hs_height_scan((w), (x), (z))
#endif

/* WorldEntitySpawner.getRandomChunkPosition :193-201. */
MC_HD static inline void hs_random_chunk_pos(HS_W *w, JavaRandom *wr,
                                             int cx, int cz,
                                             int *ox, int *oy, int *oz) {
    int i, j, k, l, h, top;
    i = cx * 16 + jrand_int_bound(wr, 16);
    j = cz * 16 + jrand_int_bound(wr, 16);
    h = HS_HEIGHT(w, i, j);
    k = hs_round_up(h + 1, 16);
    if (k > 0) {
        l = jrand_int_bound(wr, k);
    } else {
        top = ((h + 15) / 16);
        if (top < 1) top = 1;
        l = jrand_int_bound(wr, top * 16 - 1);
    }
    *ox = i;
    *oy = l;
    *oz = j;
}

/* java.util.Random.nextGaussian polar method; gauss cache on the entity. */
MC_HD static inline double hs_next_gaussian(JavaRandom *r, int *have, double *cached) {
    double v1, v2, s, mul;
    if (!r || !have || !cached) return 0.0;
    if (*have) {
        *have = 0;
        return *cached;
    }
    do {
        v1 = 2.0 * jrand_double(r) - 1.0;
        v2 = 2.0 * jrand_double(r) - 1.0;
        s = v1 * v1 + v2 * v2;
    } while (s >= 1.0 || s == 0.0);
    mul = sqrt(-2.0 * log(s) / s);
    *cached = v2 * mul;
    *have = 1;
    return v1 * mul;
}

/* DifficultyInstance.getClampedAdditionalDifficulty DifficultyInstance.java:23-25
 * with inhabited=0, moon=0. worldTime < 72000 => additional = 2*0.75 = 1.5 => 0. */
MC_HD static inline float hs_clamped_add(int difficulty, i64 world_time) {
    float f, f1, add;
    int id;
    if (difficulty <= 0) return 0.0f;
    id = difficulty;
    f = 0.75f;
    f1 = (float)world_time + -72000.0f;
    f1 = f1 / 1440000.0f;
    if (f1 < 0.0f) f1 = 0.0f;
    if (f1 > 1.0f) f1 = 1.0f;
    f1 *= 0.25f;
    f += f1;
    add = (float)id * f;
    if (add < 2.0f) return 0.0f;
    if (add > 4.0f) return 1.0f;
    return (add - 2.0f) / 2.0f;
}

/* EntityLiving.setEquipmentBasedOnDifficulty EntityLiving.java:1067-1116.
 * Consumes entity.rand; does not store gear on this path. */
MC_HD static inline void hs_equip_rand(JavaRandom *er, int difficulty, float clamped) {
    float f;
    int i, slot, flag;
    if (jrand_float(er) >= 0.15f * clamped) return;
    i = jrand_int_bound(er, 2);
    f = difficulty == 3 ? 0.1f : 0.25f;
    if (jrand_float(er) < 0.095f) ++i;
    if (jrand_float(er) < 0.095f) ++i;
    if (jrand_float(er) < 0.095f) ++i;
    (void)i;
    flag = 1;
    /* EntityEquipmentSlot.values ARMOR: FEET, LEGS, CHEST, HEAD. */
    for (slot = 0; slot < 4; ++slot) {
        if (!flag && jrand_float(er) < f) break;
        flag = 0;
        /* empty slot: no further draws */
    }
}

/* EntityLiving.setEnchantmentBasedOnDifficulty EntityLiving.java:1228-1248.
 * Held/armor empty on this path => no nextFloat. */
MC_HD static inline void hs_enchant_rand(JavaRandom *er, float clamped) {
    (void)er;
    (void)clamped;
}

MC_HD static inline void hs_living_init(JavaRandom *er, int *have, double *g) {
    (void)hs_next_gaussian(er, have, g);      /* FOLLOW_RANGE bonus :1258 */
    (void)jrand_float(er);                    /* left-handed 0.05F :1260 */
}

/* EntityZombie.onInitialSpawn EntityZombie.java:483-555. baby GroupData uses
 * world.rand; equipment uses entity.rand. */
MC_HD static inline void hs_zombie_init(JavaRandom *wr, JavaRandom *er,
                                        int *have, double *g,
                                        int difficulty, i64 world_time,
                                        int *livingdata, int first_in_pack) {
    float f = hs_clamped_add(difficulty, world_time);
    hs_living_init(er, have, g);
    (void)jrand_float(er);                    /* canPickUpLoot 0.55F*f :487 */
    if (first_in_pack || *livingdata < 0) {
        *livingdata = jrand_float(wr) < 0.05f ? 1 : 0; /* Forge baby 0.05 :491 */
    }
    if (*livingdata == 1) {
        if ((double)jrand_float(wr) < 0.05) {
            /* chicken jockey nearby: no chickens in the live table. */
        } else if ((double)jrand_float(wr) < 0.05) {
            /* would spawn a chicken; passives stay out. consume the float. */
        }
    }
    (void)jrand_float(er);                    /* break doors f*0.1F :525 */
    hs_equip_rand(er, difficulty, f);
    /* EntityZombie.setEquipmentBasedOnDifficulty extra iron sword :377 */
    if (jrand_float(er) < (difficulty == 3 ? 0.05f : 0.01f))
        (void)jrand_int_bound(er, 3);
    hs_enchant_rand(er, f);
    /* Halloween pumpkin skipped: calendar is not Oct 31. */
    (void)jrand_double(er);                   /* knockback resist :540 */
    (void)jrand_double(er);                   /* follow bonus d0 :541 */
    if (jrand_float(er) < f * 0.05f) {
        (void)jrand_double(er);
        (void)jrand_double(er);
    }
}

MC_HD static inline void hs_skeleton_init(JavaRandom *er, int *have, double *g,
                                          int difficulty, i64 world_time) {
    float f = hs_clamped_add(difficulty, world_time);
    hs_living_init(er, have, g);
    hs_equip_rand(er, difficulty, f);
    /* AbstractSkeleton always sets a bow; no extra rand. */
    hs_enchant_rand(er, f);
    (void)jrand_float(er);                    /* canPickUpLoot :216 */
}

MC_HD static inline void hs_creeper_init(JavaRandom *er, int *have, double *g) {
    hs_living_init(er, have, g);
}

MC_HD static inline void hs_spider_init(JavaRandom *wr, JavaRandom *er,
                                        int *have, double *g,
                                        int difficulty, i64 world_time) {
    float f = hs_clamped_add(difficulty, world_time);
    hs_living_init(er, have, g);
    /* EntitySpider.onInitialSpawn EntitySpider.java:200-207: world.rand.nextInt(100)==0
     * spawns a skeleton rider. The live table has no riding slots; consume the
     * draw (and a scratch skeleton onInitialSpawn) and skip the insert. */
    if (jrand_int_bound(wr, 100) == 0) {
        JavaRandom sk;
        int shave = 0;
        double sg = 0.0;
        jrand_set(&sk, wr->seed ^ 0x534B454C); /* scratch skeleton, not world.rand */
        hs_skeleton_init(&sk, &shave, &sg, difficulty, world_time);
    }
    /* EntitySpider.GroupData potion roll EntitySpider.java:213-216: HARD only
     * (`nextFloat() < 0.1F * getClampedAdditionalDifficulty()`) then
     * GroupData.setRandomEffect nextInt(5) (EntitySpider.java:291-308:
     * speed/strength/regeneration/invisibility). living_base.h / RlSnapMob
     * carry no PotionEffect list; consume the draws and skip addPotionEffect. */
    if (difficulty == 3 && jrand_float(wr) < 0.1f * f)
        (void)jrand_int_bound(wr, 5); /* GroupData.setRandomEffect :291 */
    (void)f;
}

/* EntitySlime.onInitialSpawn EntitySlime.java:406-415. Returns 1<<i (1,2,4). */
MC_HD static inline int hs_slime_init(JavaRandom *er, int *have, double *g,
                                      i64 world_time, int difficulty) {
    float f = hs_clamped_add(difficulty, world_time);
    int i = jrand_int_bound(er, 3);
    if (i < 2 && jrand_float(er) < 0.5f * f) ++i;
    hs_living_init(er, have, g);
    return 1 << i;
}

MC_HD static inline int hs_on_initial_spawn(int hs_type, JavaRandom *wr,
                                            JavaRandom *er, int *have, double *g,
                                            int difficulty, i64 world_time,
                                            int *livingdata, int first_in_pack) {
    if (hs_type == HS_ZOMBIE || hs_type == HS_ZOMBIE_VILLAGER) {
        if (hs_type == HS_ZOMBIE_VILLAGER)
            (void)jrand_int_bound(wr, 6);     /* profession :105 */
        hs_zombie_init(wr, er, have, g, difficulty, world_time,
                       livingdata, first_in_pack);
        return 0;
    } else if (hs_type == HS_SKELETON) {
        hs_skeleton_init(er, have, g, difficulty, world_time);
        return 0;
    } else if (hs_type == HS_CREEPER) {
        hs_creeper_init(er, have, g);
        return 0;
    } else if (hs_type == HS_SPIDER) {
        hs_spider_init(wr, er, have, g, difficulty, world_time);
        return 0;
    } else if (hs_type == HS_SLIME) {
        return hs_slime_init(er, have, g, world_time, difficulty);
    } else if (hs_type == HS_STRAY) {
        /* EntityStray extends AbstractSkeleton. Skip insert (not roster). */
        hs_skeleton_init(er, have, g, difficulty, world_time);
        return 0;
    }
    hs_living_init(er, have, g);              /* witch */
    return 0;
}

/* WorldProvider.getMoonPhase WorldProvider.java:113-116 + MOON_PHASE_FACTORS :24 */
MC_HD static inline float hs_moon_phase_factor(i64 world_time) {
    int p = (int)((world_time / 24000LL % 8LL + 8LL) % 8LL);
    if (p == 0) return 1.0f;
    if (p == 1 || p == 7) return 0.75f;
    if (p == 2 || p == 6) return 0.5f;
    if (p == 3 || p == 5) return 0.25f;
    return 0.0f;
}

/* Chunk.getRandomWithSeed Chunk.java:1019 with 987234911L (EntitySlime.java:355).
 * Java int overflow on x*x*4987142 / x*5947611 / z*389711; z*z is int then * long. */
MC_HD static inline int hs_is_slime_chunk(i64 world_seed, int cx, int cz) {
    i32 x = (i32)cx, z = (i32)cz;
    i32 xx = (i32)((u32)x * (u32)x);
    i32 zz = (i32)((u32)z * (u32)z);
    i64 seed = world_seed
        + (i64)(i32)((u32)xx * 4987142u)
        + (i64)(i32)((u32)x * 5947611u)
        + (i64)zz * 4392871LL
        + (i64)(i32)((u32)z * 389711u);
    JavaRandom r;
    seed ^= 987234911LL;
    jrand_set(&r, seed);
    return jrand_int_bound(&r, 10) == 0;
}

/* EntityMob.getCanSpawnHere :186-188 + isValidLightLevel. Slime is not
 * EntityMob; EntitySlime.getCanSpawnHere :335-362. */
MC_HD static inline int hs_can_spawn_here(HS_W *w, JavaRandom *er, int hs_type,
                                          int x, int y, int z, HsState *st) {
    int sub, biome;
    double py;
    if (!st || st->difficulty == 0) return 0;
    if (hs_type == HS_SLIME) {
        /* WorldType.handleSlimeSpawnReduction WorldType.java:196-198: default
         * false, no rand draw (FLAT would nextInt(4)!=1). */
        biome = HS_BIOME(w, x, z);
        py = (double)y;
        /* swamp y 50..70 EntitySlime.java:350. Short-circuit &&. Combined
         * light at pos stands in for getLightFromNeighbors (same as the
         * EntityMob spawn light in this header). */
        if (biome == HS_BIOME_SWAMP && py > 50.0 && py < 70.0
            && jrand_float(er) < 0.5f
            && jrand_float(er) < hs_moon_phase_factor(st->world_time)
            && hs_combined_light(w, x, y, z, 0) <= jrand_int_bound(er, 8))
            return 1;
        /* slime-chunk EntitySlime.java:355 */
        if (jrand_int_bound(er, 10) == 0
            && hs_is_slime_chunk(st->seed, x >> 4, z >> 4)
            && py < 40.0)
            return 1;
        return 0;
    }
    sub = st->skylight_sub;
    return hs_valid_light(w, er, x, y, z, sub, st->thundering);
}

MC_HD static inline void hs_aabb_for(int hs_type, int slime_size,
                                     double x, double y, double z,
                                     double *x0, double *y0, double *z0,
                                     double *x1, double *y1, double *z1) {
    float w, h;
    int ew = hs_to_ew(hs_type);
    if (ew == EW_TYPE_NONE) ew = EW_TYPE_ZOMBIE;
    if (ew == EW_TYPE_SLIME || ew == EW_TYPE_MAGMA) {
        if (slime_size < 1) slime_size = 1; /* entityInit SLIME_SIZE=1 */
        ehs_size_scaled((u8)ew, slime_size, &w, &h);
    } else {
        ehs_size((u8)ew, &w, &h);
    }
    *x0 = x - (double)w * 0.5;
    *y0 = y;
    *z0 = z - (double)w * 0.5;
    *x1 = x + (double)w * 0.5;
    *y1 = y + (double)h;
    *z1 = z + (double)w * 0.5;
}

/* Seed entity.rand without consuming world.rand (Entity.java:238 is unseeded). */
MC_HD static inline void hs_seed_entity(JavaRandom *er, i64 world_seed, i64 tick,
                                        int x, int y, int z, int attempt) {
    i64 s = world_seed ^ (tick * 6364136223846793005LL)
        ^ ((i64)x << 32) ^ ((i64)z << 16) ^ ((i64)y << 8) ^ (i64)attempt
        ^ 0x45524E44LL; /* "ERND" */
    jrand_set(er, s);
}

MC_HD static inline void hs_consume_uuid(JavaRandom *er) {
    (void)jrand_long(er);
    (void)jrand_long(er);
}

MC_HD static inline int hs_try_one(HS_W *w, HsState *st, int hs_type,
                                   int x, int y, int z, int attempt,
                                   int *livingdata, int first_in_pack) {
    JavaRandom er;
    int have = 0;
    double gauss = 0.0;
    double px, py, pz, yaw;
    double x0, y0, z0, x1, y1, z1;
    float f, f1;
    int ew, extra;
    if (!st) return 0;
    f = (float)x + 0.5f;
    f1 = (float)z + 0.5f;
    px = (double)f;
    py = (double)y;
    pz = (double)f1;
    hs_seed_entity(&er, st->seed, st->world_time, x, y, z, attempt);
    hs_consume_uuid(&er);                     /* Entity.java:241 */
    yaw = jrand_float(&st->world_rand) * 360.0f; /* :154 */
    if (!hs_can_spawn_here(w, &er, hs_type, x, y, z, st))
        return 0;
    hs_aabb_for(hs_type, 1, px, py, pz, &x0, &y0, &z0, &x1, &y1, &z1);
    if (!hs_not_colliding(w, x0, y0, z0, x1, y1, z1))
        return 0;
    extra = hs_on_initial_spawn(hs_type, &st->world_rand, &er, &have, &gauss,
                                st->difficulty, st->world_time, livingdata, first_in_pack);
    if (hs_type == HS_SLIME && extra > 0)
        hs_aabb_for(hs_type, extra, px, py, pz, &x0, &y0, &z0, &x1, &y1, &z1);
    if (!hs_not_colliding(w, x0, y0, z0, x1, y1, z1))
        return 0;
    if (!hs_is_roster(hs_type))
        return 0;
    if (HS_HOSTILE_COUNT(w) >= HS_TABLE_CAP)
        return 0;                             /* magma/blaze shared cap */
    ew = hs_to_ew(hs_type);
    return HS_PLACE(w, ew, px, py, pz, yaw, er.seed, have, gauss, extra);
}

MC_HD static inline void hs_shuffle_chunks(JavaRandom *r, int *cx, int *cz, int n) {
    int i, j, t;
    for (i = n; i > 1; --i) {
        j = jrand_int_bound(r, i);
        t = cx[i - 1]; cx[i - 1] = cx[j]; cx[j] = t;
        t = cz[i - 1]; cz[i - 1] = cz[j]; cz[j] = t;
    }
}

/* World.calculateSkylightSubtracted with rain=thunder=0. Night ~11. */
MC_HD static inline int hs_skylight_sub(i64 world_time) {
    i32 i;
    float f, f1, ang;
    i = (i32)(world_time % 24000LL);
    if (i < 0) i += 24000;
    f = ((float)i + 1.0f) / 24000.0f - 0.25f;
    if (f < 0.0f) f += 1.0f;
    if (f > 1.0f) f -= 1.0f;
    f1 = 1.0f - (float)((cos((double)f * MC_PI) + 1.0) / 2.0);
    ang = f + (f1 - f) / 3.0f;
    f1 = 1.0f - (float)(cos((double)ang * (double)MC_PI * 2.0) * 2.0 + 0.5);
    if (f1 < 0.0f) f1 = 0.0f;
    if (f1 > 1.0f) f1 = 1.0f;
    f1 = 1.0f - f1;                           /* sun brightness factor */
    f1 = 1.0f - f1;                           /* calculateSkylightSubtracted */
    return (int)(f1 * 11.0f);
}

/* WorldEntitySpawner.findChunksForSpawning MONSTER body. One non-spectator
 * player. Returns number of inserted roster hostiles this tick. */
MC_HD MC_NOINLINE static int hs_find_chunks_for_spawning(HS_W *w, HsState *st,
                                                         double px, double py, double pz) {
    int pcx, pcz, i1, j1, n_el, i_count, cap, n_host, placed;
    int cx[HS_MAX_ELIGIBLE], cz[HS_MAX_ELIGIBLE];
    int c, k2, i4, l3, type, livingdata, first, pack, rx, ry, rz;
    int bx, by, bz, here;
    double f, f1, dx, dy, dz, dsq, dspawn;
    if (!w || !st) return 0;
    if (st->difficulty == 0) return 0;
    st->skylight_sub = hs_skylight_sub(st->world_time);
    n_host = HS_HOSTILE_COUNT(w);
    pcx = mc_floor(px / 16.0);
    pcz = mc_floor(pz / 16.0);
    i_count = 0;
    n_el = 0;
    for (i1 = -HS_CHUNK_RADIUS; i1 <= HS_CHUNK_RADIUS; ++i1) {
        for (j1 = -HS_CHUNK_RADIUS; j1 <= HS_CHUNK_RADIUS; ++j1) {
            int border = (i1 == -HS_CHUNK_RADIUS || i1 == HS_CHUNK_RADIUS
                          || j1 == -HS_CHUNK_RADIUS || j1 == HS_CHUNK_RADIUS);
            ++i_count;                        /* :63 unique 17x17 including border */
            if (border) continue;
            if (n_el < HS_MAX_ELIGIBLE) {
                cx[n_el] = i1 + pcx;
                cz[n_el] = j1 + pcz;
                ++n_el;
            }
        }
    }
    cap = hs_monster_cap(i_count);
    if (n_host > cap) return 0;
    hs_shuffle_chunks(&st->shuffle_rand, cx, cz, n_el);
    placed = 0;
    for (c = 0; c < n_el; ++c) {
        hs_random_chunk_pos(w, &st->world_rand, cx[c], cz[c], &bx, &by, &bz);
        here = HS_BLOCK(w, bx, by, bz);
        if (hs_is_normal_cube(here)) continue; /* :105 */
        pack = 0;
        type = -1;
        livingdata = -1;
        l3 = hs_ceil_d(jrand_double(&st->math_rand) * 4.0);
        for (k2 = 0; k2 < 3; ++k2) {          /* :109 */
            int l2 = bx, i3 = by, j3 = bz;
            for (i4 = 0; i4 < l3; ++i4) {     /* :119 */
                l2 += jrand_int_bound(&st->world_rand, 6)
                    - jrand_int_bound(&st->world_rand, 6);
                i3 += jrand_int_bound(&st->world_rand, 1)
                    - jrand_int_bound(&st->world_rand, 1);
                j3 += jrand_int_bound(&st->world_rand, 6)
                    - jrand_int_bound(&st->world_rand, 6);
                f = (float)l2 + 0.5f;
                f1 = (float)j3 + 0.5f;
                dx = (double)f - px;
                dy = (double)i3 - py;
                dz = (double)f1 - pz;
                dsq = dx * dx + dy * dy + dz * dz;
                dx = (double)f - st->spawn_x;
                dy = (double)i3 - st->spawn_y;
                dz = (double)f1 - st->spawn_z;
                dspawn = dx * dx + dy * dy + dz * dz;
                if (dsq < HS_PLAYER_RANGE * HS_PLAYER_RANGE
                    || dspawn < HS_SPAWN_PT_RANGE_SQ)
                    continue;
                if (type < 0)
                    type = hs_weighted_pick_biome(&st->world_rand,
                                                 HS_BIOME(w, l2, j3));
                if (type < 0) break;
                rx = l2;
                ry = i3;
                rz = j3;
                if (!hs_can_spawn_at(w, rx, ry, rz))
                    continue;
                first = (livingdata < 0);
                if (hs_try_one(w, st, type, rx, ry, rz, i4 + k2 * 8,
                               &livingdata, first)) {
                    ++pack;
                    ++placed;
                    ++n_host;
                    if (n_host >= HS_TABLE_CAP) return placed;
                    if (pack >= HS_MAX_PACK) goto next_chunk;
                }
            }
        }
    next_chunk:
        (void)0;
    }
    return placed;
}

/* EntityLiving.despawnEntity EntityLiving.java:787-831.
 * persist => age=0, no nextInt. 128^2 hard. age>600 then nextInt(800)==0
 * then 32^2 soft (Java && order at :821). d3<1024 resets age. entity.rand
 * is the raw 48-bit cursor. */
MC_HD static inline int hs_despawn_tick(int persist, double d,
                                        int *entity_age, u64 *seed48) {
    double d3;
    JavaRandom er;
    if (persist) {
        *entity_age = 0;
        return 0;
    }
    ++*entity_age;
    d3 = d * d;
    if (d3 > 16384.0) return 1;               /* :816-818 */
    if (*entity_age > 600) {
        er.seed = seed48 ? *seed48 : 0;
        if (jrand_int_bound(&er, 800) == 0 && d3 > 1024.0) {
            if (seed48) *seed48 = er.seed;
            return 1;
        }
        if (seed48) *seed48 = er.seed;
    }
    if (d3 < 1024.0)                          /* :825 */
        *entity_age = 0;
    return 0;
}

/* ---- CREATURE (EntityAnimal) -------------------------------------------
 * WorldEntitySpawner.findChunksForSpawning CREATURE pass.
 * EnumCreatureType.CREATURE max 10, peaceful, isAnimal.
 * WorldServer.tick spawnOnSetTickRate = worldTotalTime % 400L == 0
 * (WorldServer.java:206). Caller gates that; this body assumes it is time.
 * Isolated spawn JavaRandom, same as MONSTER. */

enum {
    HS_CREATURE_SHEEP = 0,
    HS_CREATURE_PIG = 1,
    HS_CREATURE_CHICKEN = 2,
    HS_CREATURE_COW = 3,
    HS_CREATURE_NTYPES = 4
};

/* Biome.java:142-145 default creature list. */
MC_HD static inline int hs_creature_weight_at(int i) {
    if (i == HS_CREATURE_SHEEP) return 12;
    if (i == HS_CREATURE_PIG) return 10;
    if (i == HS_CREATURE_CHICKEN) return 10;
    if (i == HS_CREATURE_COW) return 8;
    return 0;
}
#define HS_CREATURE_TOTAL_WEIGHT 40

/* Biomes that clear spawnableCreatureList in their ctor. Horse/donkey/wolf/
 * rabbit extras are not in the lockstep roster (same as plains horse). */
MC_HD static inline int hs_creature_list_empty(int biome) {
    /* BiomeOcean.java:8, BiomeRiver.java:8, BiomeBeach.java:10,
     * BiomeStoneBeach.java:10, BiomeMesa.java:41 / :49. */
    if (biome == HS_BIOME_OCEAN || biome == HS_BIOME_DEEP_OCEAN ||
        biome == HS_BIOME_RIVER || biome == HS_BIOME_FROZEN_OCEAN ||
        biome == HS_BIOME_BEACH || biome == HS_BIOME_STONE_BEACH ||
        biome == HS_BIOME_COLD_BEACH || biome == HS_BIOME_MESA ||
        biome == HS_BIOME_MESA_ROCK || biome == HS_BIOME_MESA_CLEAR)
        return 1;
    /* BiomeSnow.java:33-35: clear then rabbit+polar bear. Roster has none. */
    if (biome == HS_BIOME_ICE_PLAINS || biome == HS_BIOME_ICE_MOUNTAINS)
        return 1;
    return 0;
}

MC_HD static inline int hs_creature_weight_at_biome(int i, int biome) {
    if (hs_creature_list_empty(biome)) return 0;
    return hs_creature_weight_at(i);
}

MC_HD static inline int hs_creature_total_weight(int biome) {
    if (hs_creature_list_empty(biome)) return 0;
    return HS_CREATURE_TOTAL_WEIGHT;
}

MC_HD static inline int hs_creature_cap(int chunk_count_i) {
    return HS_CREATURE_CAP * chunk_count_i / HS_MOB_COUNT_DIV;
}

MC_HD static inline int hs_creature_weighted_pick_biome(JavaRandom *r, int biome) {
    int w, i, tot;
    if (!r) return HS_CREATURE_SHEEP;
    tot = hs_creature_total_weight(biome);
    if (tot <= 0) return -1;
    w = jrand_int_bound(r, tot);
    for (i = 0; i < HS_CREATURE_NTYPES; ++i) {
        w -= hs_creature_weight_at_biome(i, biome);
        if (w < 0) return i;
    }
    return HS_CREATURE_SHEEP;
}

MC_HD static inline int hs_creature_weighted_pick(JavaRandom *r) {
    return hs_creature_weighted_pick_biome(r, 1); /* plains */
}

MC_HD static inline int hs_creature_to_ew(int c) {
    if (c == HS_CREATURE_SHEEP) return EW_TYPE_SHEEP;
    if (c == HS_CREATURE_PIG) return EW_TYPE_PIG;
    if (c == HS_CREATURE_CHICKEN) return EW_TYPE_CHICKEN;
    if (c == HS_CREATURE_COW) return EW_TYPE_COW;
    return EW_TYPE_NONE;
}

/* EntitySheep.getRandomSheepColor EntitySheep.java:333-336. world.rand. */
MC_HD static inline int hs_random_sheep_color(JavaRandom *r) {
    int i;
    if (!r) return 0;
    i = jrand_int_bound(r, 100);
    if (i < 5) return 15;
    if (i < 10) return 7;
    if (i < 15) return 8;
    if (i < 18) return 12;
    if (jrand_int_bound(r, 500) == 0) return 6;
    return 0;
}

/* EntityAnimal.getCanSpawnHere EntityAnimal.java:117-124:
 * grass below, World.getLight(pos)>8 (getLightSubtracted amount 0),
 * super EntityLiving.getCanSpawnHere (down.canEntitySpawn). */
MC_HD static inline int hs_creature_can_spawn_here(HS_W *w, int x, int y, int z) {
    int down, light;
    if (y < 1 || y > 254) return 0;
    down = HS_BLOCK(w, x, y - 1, z);
    if (down != BLK_GRASS) return 0;
    light = hs_combined_light(w, x, y, z, 0); /* World.getLight amount=0 */
    return light > 8;
}

MC_HD static inline void hs_creature_aabb(int ew, double x, double y, double z,
                                          double *x0, double *y0, double *z0,
                                          double *x1, double *y1, double *z1) {
    float w, h;
    ehs_size((u8)ew, &w, &h);
    *x0 = x - (double)w * 0.5;
    *y0 = y;
    *z0 = z - (double)w * 0.5;
    *x1 = x + (double)w * 0.5;
    *y1 = y + (double)h;
    *z1 = z + (double)w * 0.5;
}

MC_HD static inline int hs_creature_try_one(HS_W *w, HsState *st, int ctype,
                                            int x, int y, int z, int attempt,
                                            int *livingdata, int first_in_pack) {
    JavaRandom er;
    int have = 0;
    double gauss = 0.0;
    double px, py, pz, yaw;
    double x0, y0, z0, x1, y1, z1;
    float f, f1;
    int ew, extra = 0;
    (void)livingdata;
    (void)first_in_pack;
    if (!st) return 0;
    f = (float)x + 0.5f;
    f1 = (float)z + 0.5f;
    px = (double)f;
    py = (double)y;
    pz = (double)f1;
    ew = hs_creature_to_ew(ctype);
    hs_seed_entity(&er, st->seed, st->world_time, x, y, z, attempt);
    hs_consume_uuid(&er);                     /* Entity.java:241 */
    yaw = jrand_float(&st->world_rand) * 360.0f;
    if (!hs_creature_can_spawn_here(w, x, y, z))
        return 0;
    hs_creature_aabb(ew, px, py, pz, &x0, &y0, &z0, &x1, &y1, &z1);
    if (!hs_not_colliding(w, x0, y0, z0, x1, y1, z1))
        return 0;
    hs_living_init(&er, &have, &gauss);
    if (ctype == HS_CREATURE_SHEEP)
        extra = hs_random_sheep_color(&st->world_rand); /* EntitySheep.java:368 */
    if (!hs_not_colliding(w, x0, y0, z0, x1, y1, z1))
        return 0;
    if (HS_CREATURE_COUNT(w) >= HS_TABLE_CAP)
        return 0;
    return HS_PLACE(w, ew, px, py, pz, yaw, er.seed, have, gauss, extra);
}

MC_HD MC_NOINLINE static int hs_find_chunks_for_creatures(HS_W *w, HsState *st,
                                                          double px, double py, double pz) {
    int pcx, pcz, i1, j1, n_el, i_count, cap, n_cr, placed;
    int cx[HS_MAX_ELIGIBLE], cz[HS_MAX_ELIGIBLE];
    int c, k2, i4, l3, type, livingdata, first, pack, rx, ry, rz;
    int bx, by, bz, here;
    double f, f1, dx, dy, dz, dsq, dspawn;
    if (!w || !st) return 0;
    st->skylight_sub = hs_skylight_sub(st->world_time);
    n_cr = HS_CREATURE_COUNT(w);
    pcx = mc_floor(px / 16.0);
    pcz = mc_floor(pz / 16.0);
    i_count = 0;
    n_el = 0;
    for (i1 = -HS_CHUNK_RADIUS; i1 <= HS_CHUNK_RADIUS; ++i1) {
        for (j1 = -HS_CHUNK_RADIUS; j1 <= HS_CHUNK_RADIUS; ++j1) {
            int border = (i1 == -HS_CHUNK_RADIUS || i1 == HS_CHUNK_RADIUS
                          || j1 == -HS_CHUNK_RADIUS || j1 == HS_CHUNK_RADIUS);
            ++i_count;
            if (border) continue;
            if (n_el < HS_MAX_ELIGIBLE) {
                cx[n_el] = i1 + pcx;
                cz[n_el] = j1 + pcz;
                ++n_el;
            }
        }
    }
    cap = hs_creature_cap(i_count);
    if (n_cr > cap) return 0;
    hs_shuffle_chunks(&st->shuffle_rand, cx, cz, n_el);
    placed = 0;
    for (c = 0; c < n_el; ++c) {
        hs_random_chunk_pos(w, &st->world_rand, cx[c], cz[c], &bx, &by, &bz);
        here = HS_BLOCK(w, bx, by, bz);
        if (hs_is_normal_cube(here)) continue;
        pack = 0;
        type = -1;
        livingdata = -1;
        l3 = hs_ceil_d(jrand_double(&st->math_rand) * 4.0);
        for (k2 = 0; k2 < 3; ++k2) {
            int l2 = bx, i3 = by, j3 = bz;
            for (i4 = 0; i4 < l3; ++i4) {
                l2 += jrand_int_bound(&st->world_rand, 6)
                    - jrand_int_bound(&st->world_rand, 6);
                i3 += jrand_int_bound(&st->world_rand, 1)
                    - jrand_int_bound(&st->world_rand, 1);
                j3 += jrand_int_bound(&st->world_rand, 6)
                    - jrand_int_bound(&st->world_rand, 6);
                f = (float)l2 + 0.5f;
                f1 = (float)j3 + 0.5f;
                dx = (double)f - px;
                dy = (double)i3 - py;
                dz = (double)f1 - pz;
                dsq = dx * dx + dy * dy + dz * dz;
                dx = (double)f - st->spawn_x;
                dy = (double)i3 - st->spawn_y;
                dz = (double)f1 - st->spawn_z;
                dspawn = dx * dx + dy * dy + dz * dz;
                if (dsq < HS_PLAYER_RANGE * HS_PLAYER_RANGE
                    || dspawn < HS_SPAWN_PT_RANGE_SQ)
                    continue;
                if (type < 0) {
                    int biome = HS_BIOME(w, l2, j3);
                    if (hs_creature_total_weight(biome) <= 0) break;
                    type = hs_creature_weighted_pick_biome(&st->world_rand,
                                                          biome);
                }
                if (type < 0) break;
                rx = l2;
                ry = i3;
                rz = j3;
                if (!hs_can_spawn_at(w, rx, ry, rz))
                    continue;
                first = (livingdata < 0);
                if (hs_creature_try_one(w, st, type, rx, ry, rz, i4 + k2 * 8,
                                        &livingdata, first)) {
                    ++pack;
                    ++placed;
                    ++n_cr;
                    if (n_cr >= HS_TABLE_CAP) return placed;
                    if (pack >= HS_MAX_PACK) goto next_chunk;
                }
            }
        }
    next_chunk:
        (void)0;
    }
    return placed;
}

#endif /* MC_HOSTILE_SPAWN_H */
