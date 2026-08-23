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
 *     sound/particles/drops/fire  CUT (magma does not port)
 *
 * Magma extras (M1 is magma semantics; do not "fix" to Java here):
 *   density rand consumes world.rand.nextFloat per face ray when the
 *     live JavaRandom is passed (Explosion.java:102); NULL keeps 0.5F
 *   getBlockDensity is full-cube BF_SOLID on the 16^3 sample (no non-cube BB)
 *   blast-prot level 0 (no armor enchant scan)
 *   no doExplosionB drops / particles / flaming
 *   explosion Y is feetY + 0.5 (not Entity.posY)
 *   unpowered radius 3.0F, always destroys (no mobGriefing / powered 2x)
 *   --mobs off: ignited living creepers (hasIgnited), not AICreeperSwell
 *   explosion attackEntityFrom 0.4F knockBack stays on the mobs row
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
#define EXL_Y_OFF 0.5             /* magma gm_mobs_take_explosion y */
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
                                           JavaRandom *rand) {
    int x, y, z;
    *ox = (int)floor(ex) - 8;
    *oy = (int)floor(ey) - 8;
    *oz = (int)floor(ez) - 8;
    for (x = 0; x < EX_DIM; ++x)
        for (y = 0; y < EX_DIM; ++y)
            for (z = 0; z < EX_DIM; ++z)
                grid[ex_idx(x, y, z)] = mc_state(
                    exl_block(w, *ox + x, *oy + y, *oz + z),
                    exl_meta(w, *ox + x, *oy + y, *oz + z));
    ex_do_explosion_blocks(grid, ex - (double)*ox, ey - (double)*oy,
                           ez - (double)*oz, size, hit, rand);
}

/* doExplosionB block destroy: sound draws, optional chain TNT, air.
 * Drops (Block.dropBlockAsItemWithChance) stay out: HashSet order plus
 * getDrops/quantityDropped are not in the live item table contract. */
MC_HD static inline void exl_apply_hits(EXL_W *w, const u8 *hit,
                                        int ox, int oy, int oz,
                                        uint32_t *ndestroyed, uint64_t *rays,
                                        JavaRandom *rand) {
    int x, y, z;
    if (rand) {
        /* Explosion.java:198 playSound pitch: two nextFloat. Server
         * WorldServer.newExplosion doExplosionB(false) skips particle draws. */
        (void)jrand_float(rand);
        (void)jrand_float(rand);
    }
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

#endif /* MC_EXPLOSION_LIVE_APPLY_H */
#endif /* EXL_W */
