/* explosion_live.h - magma live creeper fuse + Explosion.doExplosionA apply.
 *
 * Magma magma/game/runtime.c + mob_live.c and blaze-CPU/CUDA compile this
 * one source. Magma wrappers stay thin; do not re-derive runtime.c.
 *
 * Java 1.11.2 (java/oracle-src):
 *   EntityCreeper.fuseTime            EntityCreeper.java:52
 *   EntityCreeper.explosionRadius     EntityCreeper.java:54
 *   EntityCreeper.hasIgnited/ignite   EntityCreeper.java:338-346
 *   EntityCreeper.onUpdate            EntityCreeper.java:158-191
 *     hasIgnited -> setCreeperState(1) (:164-167)
 *     timeSinceIgnited += state (:176)
 *     clamp < 0 to 0 (:178-181)
 *     >= fuseTime -> explode() (:183-186)
 *   EntityCreeper.explode             EntityCreeper.java:303-314
 *     World.createExplosion(this, posX, posY, posZ, radius * powered, grief)
 *   World.createExplosion             World.java:2436-2438
 *   World.newExplosion                World.java:2444-2450
 *     doExplosionA then doExplosionB(true)
 *   WorldServer.newExplosion          WorldServer.java:1245-1266
 *     doExplosionA then doExplosionB(false) + SPacketExplosion
 *   Explosion.doExplosionA            Explosion.java:82-191
 *     16x16x16 face rays, step 0.3D / 0.22500001F, resistance
 *     World.getBlockDensity, d10=(1-d12)*density, damage d2i,
 *     EnchantmentProtection.getBlastDamageReduction, motion += d5*d11,
 *     playerKnockbackMap (d5*d10)
 *   Explosion.doExplosionB            Explosion.java:196-248
 *     sound two nextFloat (:198); particles CUT on server (false)
 *     HashSet order drops (explosion_drops.h)
 *     isFlaming fire: explosionRNG.nextInt(3)==0 on air above full block
 *     (:249-257). explosionRNG is new Random() (Explosion.java:65), not
 *     World.rand. Live sources with isFlaming=true: EntityLargeFireball
 *     (newExplosion null, power, grief, grief Fireball.java:47). Creeper
 *     and TNT use createExplosion -> isFlaming false (World.java:2438).
 *     ItemFireball / EntitySmallFireball place fire without an explosion.
 *
 * Server RNG draws, in order:
 *   EntityCreeper.explode (EntityCreeper.java:303-314): none
 *   doExplosionA: world.rand.nextFloat per face ray (:102)
 *   doExplosionB: world.rand.nextFloat twice for sound pitch (:198);
 *     spawnParticles false on WorldServer.newExplosion so no per-block
 *     particle draws; drop chance / spawnAsEntity as explosion_drops.h;
 *     isFlaming: explosionRNG.nextInt(3) only when air && down full
 *
 * Magma extras (M1 is magma semantics; do not "fix" to Java here):
 *   density rand consumes world.rand.nextFloat per face ray when the
 *     live JavaRandom is passed (Explosion.java:102); NULL keeps 0.5F
 *   getBlockDensity uses movement collision AABBs (explosion.h)
 *   blast-prot: knockback via getBlastDamageReduction; magic absorb is
 *     identity unless armor ICStack.n_enchants is set (snapshot inv has
 *     no enchant payload)
 *   particles CUT; doExplosionB drops live (HashSet order)
 *   creeper origin is Entity.posY (not feet+0.5)
 *   size = explosionRadius * (powered ? 2 : 1); isSmoking = mobGriefing
 *   TNT createExplosion isSmoking true always (EntityTNTPrimed.java:114)
 *   --mobs off: ignited living creepers (hasIgnited), not AICreeperSwell
 *   explosion attackEntityFrom 0.4F knockBack stays on the mobs row
 *   powered flag aliases RlSnapMob.screaming on EW_TYPE_CREEPER (zero
 *     default, no snapshot version bump; enderman keeps screaming)
 *
 * Include after defining EXL_W for the apply half:
 *   exl_block / exl_meta / exl_set_air
 */
#ifndef MC_EXPLOSION_LIVE_H
#define MC_EXPLOSION_LIVE_H

#include "explosion.h"
#include "entity_hostile_spine.h"
#include "port_parity.h"

#define EXL_FUSE_TIME 30          /* EntityCreeper.java:52 */
#define EXL_RADIUS 3.0f           /* EntityCreeper.java:54 */
#define EXL_TNT_FUSE 80           /* EntityTNTPrimed.java:25 */
#define EXL_TNT_SIZE 4.0f         /* EntityTNTPrimed.java:113 */
#define EXL_TNT_HEIGHT 0.98f      /* EntityTNTPrimed.java:27 */
/* javap explode: posY + (double)(height / 16.0F) */
#define EXL_TNT_Y_OFF ((double)(EXL_TNT_HEIGHT / 16.0f))
#define EXL_TNT_GRAVITY 0.03999999910593033  /* EntityTNTPrimed.java:78 */
#define EXL_TNT_DRAG 0.9800000190734863      /* :82 (double)0.98F */
#define EXL_TNT_GROUND_XZ 0.699999988079071  /* :88 (double)0.7F */
#define EXL_TNT_SPAWN_MY 0.20000000298023224 /* ctor :36 */
#define EXL_BLK_TNT 46                       /* Blocks.TNT */
#define EXL_BLK_FIRE 51                      /* Blocks.FIRE */
#include "explosion_drops.h"

/* EntityCreeper.explode EntityCreeper.java:308-310:
 * (float)explosionRadius * (getPowered() ? 2.0F : 1.0F). Powered is
 * RlSnapMob.screaming on EW_TYPE_CREEPER only (zero default). */
MC_HD static inline int exl_creeper_powered(int type, int screaming) {
    return type == EW_TYPE_CREEPER && screaming != 0;
}
MC_HD static inline float exl_creeper_size(int powered) {
    return EXL_RADIUS * (powered ? 2.0f : 1.0f);
}
/* EntityCreeper.onStruckByLightning EntityCreeper.java:274-277. */
MC_HD static inline int exl_creeper_on_struck_by_lightning(int type, int *screaming) {
    if (!screaming || type != EW_TYPE_CREEPER) return 0;
    *screaming = 1;
    return 1;
}

/* EntityPlayer.java:2488 eyeHeight 1.62; zombie/skeleton 1.74F
 * (EntityZombie.java:461, AbstractSkeleton.java:303); else Entity.java:3193
 * height * 0.85F. */
MC_HD static inline float exl_eye_height(int type, float height) {
    if (type == EW_TYPE_PLAYER) return 1.62f;
    if (type == EW_TYPE_TNT_PRIMED) return 0.0f; /* EntityTNTPrimed.java:142 */
    if (type == EW_TYPE_ZOMBIE || type == EW_TYPE_SKELETON
        || type == EW_TYPE_WITHER_SKELETON)
        return 1.74f;
    return height * 0.85f;
}

/* EntityTNTPrimed.onUpdate EntityTNTPrimed.java:70-108.
 * Magma extra: Y clamp to floor_y instead of Entity.move AABB.
 * Ctor Math.random() horizontal kick CUT (java.lang.Math.random, not
 * world.rand). Chain fuse is world.rand.nextInt (BlockTNT.java:72). */

/* BlockTNT.onBlockDestroyedByExplosion fuse: nextInt(fuse/4)+fuse/8
 * (BlockTNT.java:72). Default fuse 80 -> [10, 29]. */
MC_HD static inline int exl_chain_fuse(JavaRandom *r) {
    int fuse = EXL_TNT_FUSE;
    if (!r) return fuse / 8;
    return jrand_int_bound(r, fuse / 4) + fuse / 8;
}
MC_HD static inline int exl_tnt_on_update(double *x, double *y, double *z,
                                          double *mx, double *my, double *mz,
                                          int *on_ground, int *fuse,
                                          int floor_hit, double floor_y) {
    if (!x || !y || !z || !mx || !my || !mz || !on_ground || !fuse) return 0;
    *my -= EXL_TNT_GRAVITY;
    *y += *my;
    if (floor_hit && *y < floor_y) {
        *y = floor_y;
        *on_ground = 1;
    } else {
        *on_ground = 0;
    }
    *x += *mx;
    *z += *mz;
    *mx *= EXL_TNT_DRAG;
    *my *= EXL_TNT_DRAG;
    *mz *= EXL_TNT_DRAG;
    if (*on_ground) {
        *mx *= EXL_TNT_GROUND_XZ;
        *mz *= EXL_TNT_GROUND_XZ;
        *my *= -0.5;
    }
    --*fuse;
    return *fuse <= 0;
}

/* EntityCreeper.onUpdate ignited path (:164-186). ignited is hasIgnited. */
MC_HD static inline int exl_fuse_tick(int *fuse, int ignited) {
    if (!fuse || !ignited) return 0;
    *fuse += 1;
    if (*fuse < 0) *fuse = 0;
    if (*fuse >= EXL_FUSE_TIME) {
        *fuse = 0;
        return 1;
    }
    return 0;
}

/* Explosion.java:253: air && down.isFullBlock() && explosionRNG.nextInt(3)==0.
 * Short-circuit skips the draw when air or full-block is false. */
MC_HD static inline int exl_flaming_place(JavaRandom *expl_rng, int air,
                                          int down_full) {
    if (!air || !down_full || !expl_rng) return 0;
    return jrand_int_bound(expl_rng, 3) == 0;
}

#endif /* MC_EXPLOSION_LIVE_H */

#ifdef EXL_W
#ifndef MC_EXPLOSION_LIVE_APPLY_H
#define MC_EXPLOSION_LIVE_APPLY_H

#ifndef exl_block
#error "explosion_live.h apply requires exl_block/exl_meta/exl_set_air"
#endif
#ifndef exl_meta
#error "explosion_live.h apply requires exl_meta"
#endif
#ifndef exl_set_air
#error "explosion_live.h apply requires exl_set_air"
#endif

/* Sample 16^3 about the blast, then Explosion.doExplosionA block rays. */
MC_HD static inline void exl_fill_and_rays(EXL_W *w, u16 *grid, u8 *hit,
                                           double ex, double ey, double ez,
                                           float size,
                                           int *ox, int *oy, int *oz,
                                           JavaRandom *rand,
                                           JavaHashSet *hs) {
    int x, y, z;
    *ox = (int)floor(ex) - 8;
    *oy = (int)floor(ey) - 8;
    *oz = (int)floor(ez) - 8;
    if (hs) jhs_init(hs);
    for (x = 0; x < EX_DIM; ++x)
        for (y = 0; y < EX_DIM; ++y)
            for (z = 0; z < EX_DIM; ++z)
                grid[ex_idx(x, y, z)] = mc_state(
                    exl_block(w, *ox + x, *oy + y, *oz + z),
                    exl_meta(w, *ox + x, *oy + y, *oz + z));
    ex_do_explosion_blocks(grid, ex - (double)*ox, ey - (double)*oy,
                           ez - (double)*oz, size, hit, rand, hs, *ox, *oy, *oz);
}

#ifdef exl_spawn_item
/* dropBlockAsItemWithChance + spawnAsEntity (Block.java:688-725). */
MC_HD static inline void exl_drop_with_chance(EXL_W *w, int id, int meta,
                                              int wx, int wy, int wz,
                                              float chance, JavaRandom *rand) {
    ExlStack st[EXL_DROP_STACKS];
    int n, i;
    if (!rand || chance < 0.0f) return;
    n = exl_get_drops(id, meta, rand, st, EXL_DROP_STACKS);
    for (i = 0; i < n; ++i) {
        double d0, d1, d2;
        if (jrand_float(rand) > chance) continue;
        /* spawnAsEntity :719-721 then EntityItem ctor Class C zeros motion. */
        d0 = (double)(jrand_float(rand) * 0.5f) + 0.25;
        d1 = (double)(jrand_float(rand) * 0.5f) + 0.25;
        d2 = (double)(jrand_float(rand) * 0.5f) + 0.25;
        if (exl_spawn_item(w, (double)wx + d0, (double)wy + d1,
                           (double)wz + d2, st[i].item, st[i].count,
                           st[i].meta, EXL_PICKUP_DELAY)) {
#ifdef exl_note_drop
            exl_note_drop(w, st[i].item, st[i].count, st[i].meta);
#endif
        }
    }
}
#endif

/* doExplosionB: sound draws, HashSet-order drops, chain TNT, air.
 * isSmoking (Explosion.java:75,209) gates block destroy; TNT always true
 * (EntityTNTPrimed.java:114 createExplosion smoking), creeper uses
 * mobGriefing (EntityCreeper.java:307). isFlaming fire after that loop.
 * affectedBlockPositions is ArrayList.addAll(HashSet) (Explosion.java:66,132)
 * so iteration is the first HashSet's bucket/next order. */
MC_HD static inline void exl_apply_hits(EXL_W *w, const u8 *hit,
                                        int ox, int oy, int oz,
                                        uint32_t *ndestroyed, uint64_t *rays,
                                        JavaRandom *rand, JavaHashSet *hs,
                                        float size, int smoking, int flaming,
                                        JavaRandom *expl_rng) {
    if (rand) {
        /* Explosion.java:198 playSound pitch: two nextFloat. Server
         * WorldServer.newExplosion doExplosionB(false) skips particle draws. */
        (void)jrand_float(rand);
        (void)jrand_float(rand);
    }
    if (hs && hs->size > 0) {
        JhsIter it;
        i32 wx, wy, wz;
        float chance = (size > 0.0f) ? (1.0f / size) : 1.0f;
        if (smoking) {
            jhs_iter_init(hs, &it);
            while (jhs_iter_next(hs, &it, &wx, &wy, &wz)) {
                int id = exl_block(w, wx, wy, wz);
                int meta;
                if (exl_is_air_id(id)) continue;
                meta = exl_meta(w, wx, wy, wz);
#ifdef exl_spawn_item
                if (exl_can_drop_from_explosion(id) && rand)
                    exl_drop_with_chance(w, id, meta, wx, wy, wz, chance, rand);
#else
                (void)meta;
                (void)chance;
#endif
#ifdef exl_spawn_tnt
                if (id == EXL_BLK_TNT && rand) {
                    int fuse = exl_chain_fuse(rand);
                    exl_spawn_tnt(w, wx, wy, wz, fuse);
                }
#endif
                exl_set_air(w, wx, wy, wz);
                if (ndestroyed) ++*ndestroyed;
                if (rays) {
                    *rays = bp_hash_i32(*rays, wx);
                    *rays = bp_hash_i32(*rays, wy);
                    *rays = bp_hash_i32(*rays, wz);
                }
            }
        }
        if (flaming) {
            jhs_iter_init(hs, &it);
            while (jhs_iter_next(hs, &it, &wx, &wy, &wz)) {
                int id = exl_block(w, wx, wy, wz);
                int down = exl_block(w, wx, wy - 1, wz);
                if (!exl_flaming_place(expl_rng, exl_is_air_id(id),
                                       ex_is_full_block(down)))
                    continue;
#ifdef exl_set_block
                exl_set_block(w, wx, wy, wz, EXL_BLK_FIRE, 0);
#endif
            }
        }
        return;
    }
    /* hs empty/NULL: XYZ bitset (battery / no-HashSet callers). */
    if (!smoking) return;
    {
        int x, y, z;
        for (x = 0; x < EX_DIM; ++x)
            for (y = 0; y < EX_DIM; ++y)
                for (z = 0; z < EX_DIM; ++z) {
                    int id;
                    if (!hit[ex_idx(x, y, z)]) continue;
                    id = exl_block(w, ox + x, oy + y, oz + z);
#ifdef exl_spawn_tnt
                    if (id == EXL_BLK_TNT && rand) {
                        int fuse = exl_chain_fuse(rand);
                        exl_spawn_tnt(w, ox + x, oy + y, oz + z, fuse);
                    }
#else
                    (void)id;
#endif
                    exl_set_air(w, ox + x, oy + y, oz + z);
                    if (ndestroyed) ++*ndestroyed;
                    if (rays) {
                        *rays = bp_hash_i32(*rays, ox + x);
                        *rays = bp_hash_i32(*rays, oy + y);
                        *rays = bp_hash_i32(*rays, oz + z);
                    }
                }
    }
}

#endif /* MC_EXPLOSION_LIVE_APPLY_H */
#endif /* EXL_W */
