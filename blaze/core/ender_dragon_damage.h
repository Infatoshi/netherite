/* ender_dragon_damage: EntityDragon contact damage + destroyBlocksInAABB subset on a fixed End
 * arena. Composes ender_dragon.h movement (READ-ONLY) with combat_math.h player armor reduction.
 *
 * PORT: entity/boss/EntityDragon.attackEntitiesInList (10 mob dmg), destroyBlocksInAABB
 * (mobGriefing=true; skip End structural blocks). Simplified vs vanilla: single body AABB
 * (netherite tick_dragon path) instead of head/neck/body parts; no wing collide knockback.
 *
 * Fixed 33x64x33 voxel slice (world y 64..127, x/z +-16). Seed places breakable dirt/glass
 * clouds + a dirt shell in the dragon destroy box; player is nudged adjacent to dragon spawn so
 * contact damage fires deterministically. EDD_NUM_TICKS=80; dump 8 u64 fields/tick.
 * Build -ffp-contract=off / --fmad=false. */
#ifndef MC_ENDER_DRAGON_DAMAGE_H
#define MC_ENDER_DRAGON_DAMAGE_H

#include <math.h>
#include <string.h>
#include "mc.h"
#include "mc_blocks.h"
#include "mc_math.h"
#include "mc_rng.h"
#include "mc_world.h"
#include "combat_math.h"
#include "ender_dragon.h"
#include "mc_gamerules.h"   /* mobGriefing gate (EntityDragon.destroyBlocksInAABB) */

#define EDD_W           33
#define EDD_H           64
#define EDD_D           33
#define EDD_VOL         (EDD_W * EDD_H * EDD_D)
#define EDD_Y0          64
#define EDD_NUM_TICKS   80
#define EDD_DUMP_FIELDS 8

#define EDD_RAW_CONTACT 10.0f
#define EDD_CONTACT_R_SQ (10.0 * 10.0)

enum {
    EDD_BLK_END_PORTAL       = 119,
    EDD_BLK_END_PORTAL_FRAME = 120
};

typedef struct {
    u16      blocks[EDD_VOL];
    EdArena  arena;
    float    player_health;
    McCombatArmor player_armor;
    u32      total_blocks_broken;
    u64      seed;
    i64      tick;
} EddWorld;

MC_HD static inline int edd_idx(int lx, int ly, int lz) {
    return (ly * EDD_D + lz) * EDD_W + lx;
}

MC_HD static inline int edd_in_local(int lx, int ly, int lz) {
    return lx >= 0 && lx < EDD_W && ly >= 0 && ly < EDD_H && lz >= 0 && lz < EDD_D;
}

MC_HD static inline int edd_world_to_local(int wx, int wy, int wz, int *lx, int *ly, int *lz) {
    *lx = wx + EDD_W / 2;
    *ly = wy - EDD_Y0;
    *lz = wz + EDD_D / 2;
    return edd_in_local(*lx, *ly, *lz);
}

MC_HD static inline u16 edd_get_world(const EddWorld *w, int wx, int wy, int wz) {
    int lx, ly, lz;
    if (!edd_world_to_local(wx, wy, wz, &lx, &ly, &lz)) return mc_state(BLK_AIR, 0);
    return w->blocks[edd_idx(lx, ly, lz)];
}

MC_HD static inline void edd_set_world(EddWorld *w, int wx, int wy, int wz, u16 st) {
    int lx, ly, lz;
    if (!edd_world_to_local(wx, wy, wz, &lx, &ly, &lz)) return;
    w->blocks[edd_idx(lx, ly, lz)] = st;
}

MC_HD static inline int edd_skip_destroy(int id) {
    if (id == BLK_AIR) return 1;
    if (id == BLK_BEDROCK) return 1;
    if (id == BLK_END_STONE) return 1;
    if (id == BLK_OBSIDIAN) return 1;
    if (id == EDD_BLK_END_PORTAL) return 1;
    if (id == EDD_BLK_END_PORTAL_FRAME) return 1;
    return 0;
}

MC_HD static inline void edd_fill_box(EddWorld *w, int x0, int y0, int z0,
                                      int x1, int y1, int z1, u16 st) {
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                edd_set_world(w, x, y, z, st);
}

MC_HD static inline void edd_init_scene(EddWorld *w, u64 seed) {
    w->seed = seed;
    w->tick = 0;
    w->total_blocks_broken = 0;
    w->player_health = 20.0f;
    w->player_armor = mc_combat_armor_set((int)(seed % 6u));

    for (int ly = 0; ly < EDD_H; ++ly) {
        u16 st = (ly <= 6) ? mc_state(BLK_END_STONE, 0) : mc_state(BLK_AIR, 0);
        for (int lz = 0; lz < EDD_D; ++lz)
            for (int lx = 0; lx < EDD_W; ++lx)
                w->blocks[edd_idx(lx, ly, lz)] = st;
    }

    for (int i = 0; i < 180; ++i) {
        u64 h = mc_hash64(seed ^ (u64)i * 0x9E3779B97F4A7C15ULL);
        int wx = mc_hash_bound(h, EDD_W) - EDD_W / 2;
        int wy = 88 + mc_hash_bound(h >> 17, 24);
        int wz = mc_hash_bound(h >> 34, EDD_D) - EDD_D / 2;
        int bid = ((h >> 51) & 1u) ? BLK_GLASS : BLK_DIRT;
        edd_set_world(w, wx, wy, wz, mc_state(bid, 0));
    }

    ed_init(&w->arena, seed);

    /* Subset battery: player adjacent to dragon so contact damage is exercised. */
    w->arena.player.x = w->arena.dragon.x + 4.0;
    w->arena.player.y = w->arena.dragon.y - 1.0;
    w->arena.player.z = w->arena.dragon.z + 2.0;
    w->arena.player.alive = 1;

    int bx = mc_floor(w->arena.dragon.x);
    int by = mc_floor(w->arena.dragon.y);
    int bz = mc_floor(w->arena.dragon.z);
    edd_fill_box(w, bx - 4, by, bz - 4, bx + 4, by + 8, bz + 4, mc_state(BLK_DIRT, 0));
}

MC_HD static inline float edd_apply_contact(EddWorld *w) {
    if (!w->arena.player.alive || w->player_health <= 0.0f) return 0.0f;
    if (!w->arena.dragon.alive || w->arena.dragon.death_ticks > 0) return 0.0f;

    const EdPlayer *p = &w->arena.player;
    const EdDragon *d = &w->arena.dragon;
    double pdx = p->x - d->x;
    double pdy = (p->y + 0.9) - (d->y + 4.0);
    double pdz = p->z - d->z;
    double pd2 = pdx * pdx + pdy * pdy + pdz * pdz;
    if (pd2 >= EDD_CONTACT_R_SQ) return 0.0f;

    float final = mc_combat_final_damage(EDD_RAW_CONTACT, &w->player_armor);
    if (final <= 0.0f) return 0.0f;
    w->player_health -= final;
    if (w->player_health <= 0.0f) {
        w->player_health = 0.0f;
        w->arena.player.alive = 0;
    }
    return final;
}

/* EntityDragon.destroyBlocksInAABB is gated on the mobGriefing gamerule (guarded in
 * onLivingUpdate). gr threads GameRules; default rules (mobGriefing=1) are bit-identical
 * to the prior always-destroy behavior. */
MC_HD static inline u32 edd_destroy_blocks_gr(EddWorld *w, const McGameRules *gr) {
    if (!gr->mobGriefing) return 0;
    if (!w->arena.dragon.alive || w->arena.dragon.death_ticks > 0) return 0;

    const EdDragon *d = &w->arena.dragon;
    int i0 = mc_floor(d->x);
    int j0 = mc_floor(d->y);
    int k0 = mc_floor(d->z);
    int i1 = i0 + 4;
    int j1 = j0 + 8;
    int k1 = k0 + 4;
    i0 -= 4;
    k0 -= 4;

    u32 broken = 0;
    for (int bx = i0; bx <= i1; ++bx) {
        for (int by = j0; by <= j1; ++by) {
            for (int bz = k0; bz <= k1; ++bz) {
                u16 st = edd_get_world(w, bx, by, bz);
                int id = mc_state_id(st);
                if (edd_skip_destroy(id)) continue;
                edd_set_world(w, bx, by, bz, mc_state(BLK_AIR, 0));
                broken++;
            }
        }
    }
    w->total_blocks_broken += broken;
    return broken;
}

/* Default-rules wrapper (mobGriefing=1). */
MC_HD static inline u32 edd_destroy_blocks(EddWorld *w) {
    McGameRules gr = mc_gamerules_default();
    return edd_destroy_blocks_gr(w, &gr);
}

MC_HD static inline u64 edd_bitcopy64(const void *p) {
    u64 bits;
    memcpy(&bits, p, 8);
    return bits;
}

MC_HD static inline void edd_pack_tick(const EddWorld *w, float tick_dmg, u32 tick_broken, u64 *out) {
    out[0] = edd_bitcopy64(&w->player_health);
    out[1] = edd_bitcopy64(&tick_dmg);
    out[2] = (u64)tick_broken;
    out[3] = (u64)w->total_blocks_broken;
    out[4] = edd_bitcopy64(&w->arena.dragon.x);
    out[5] = edd_bitcopy64(&w->arena.dragon.y);
    out[6] = edd_bitcopy64(&w->arena.dragon.z);
    out[7] = (u64)(u32)w->arena.player.alive;
}

MC_HD static inline void edd_run(EddWorld *w, const McSinTable *st, u64 seed, int nticks, u64 *out) {
    edd_init_scene(w, seed);
    int destroy_tick = 50 + (int)(seed % 30u);
    int destroy_idx  = (int)(seed % (u64)ED_NUM_CRYSTALS);

    for (int t = 0; t < nticks; ++t) {
        w->tick = t;
        if (t == destroy_tick
                && ed_mark_crystal_destroyed(&w->arena, destroy_idx))
            ed_on_crystal_destroyed(&w->arena, destroy_idx, 1, 1);
        ed_tick_dragon(&w->arena, st);
        float dmg = edd_apply_contact(w);
        u32 broken = edd_destroy_blocks(w);
        edd_pack_tick(w, dmg, broken, out + (size_t)t * EDD_DUMP_FIELDS);
    }
}

#endif /* MC_ENDER_DRAGON_DAMAGE_H */
