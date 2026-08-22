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
 *     entity exposure, damage, knockback
 *   Explosion.doExplosionB            Explosion.java:196-248
 *     sound/particles/drops/fire  CUT (magma does not port)
 *
 * Magma extras (M1 is magma semantics; do not "fix" to Java here):
 *   density rand fixed at 0.5F (explosion.h ex_density_scale)
 *   exposure always 1.0 (no World.getBlockDensity)
 *   no knockback applied (Java Explosion.java:174-176)
 *   no doExplosionB drops / particles / flaming
 *   explosion Y is feetY + 0.5 (not Entity.posY)
 *   unpowered radius 3.0F, always destroys (no mobGriefing / powered 2x)
 *   player + dragon/crystal damage only (no other living entities)
 *   --mobs off: ignited living creepers (hasIgnited), not AICreeperSwell
 *
 * Include after defining EXL_W for the apply half:
 *   exl_block / exl_meta / exl_set_air
 */
#ifndef MC_EXPLOSION_LIVE_H
#define MC_EXPLOSION_LIVE_H

#include "explosion.h"
#include "port_parity.h"

#define EXL_FUSE_TIME 30          /* EntityCreeper.java:52 */
#define EXL_RADIUS 3.0f           /* EntityCreeper.java:54 */
#define EXL_Y_OFF 0.5             /* magma gm_mobs_take_explosion y */

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
                                           int *ox, int *oy, int *oz) {
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
                           ez - (double)*oz, size, hit);
}

/* doExplosionB block destroy without drops: air + hash packed world cells. */
MC_HD static inline void exl_apply_hits(EXL_W *w, const u8 *hit,
                                        int ox, int oy, int oz,
                                        uint32_t *ndestroyed, uint64_t *rays) {
    int x, y, z;
    for (x = 0; x < EX_DIM; ++x)
        for (y = 0; y < EX_DIM; ++y)
            for (z = 0; z < EX_DIM; ++z) {
                if (!hit[ex_idx(x, y, z)]) continue;
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
