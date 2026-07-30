/* tile_entity_spawner: TileEntityMobSpawner.update / MobSpawnerBaseLogic.updateSpawner subset.
 *
 * Synthetic 16x16x48 flat chunk (mob_spawning MsScene) with MOB_SPAWNER block id 52 in a small
 * stone room. Player within RequiredPlayerRange (16). Hash RNG keyed (tick,x,y,z,purpose) per
 * SPEC rule 1. Per-tick dump: spawn_delay, entity_id, spawn_count.
 *
 * READ-ONLY deps: mob_spawning.h (scene + spawn block checks), block_props_table.h (via ms_*).
 * CUT: client particles/rotation, Forge events, NBT/SpawnPotentials, entity alloc/render,
 * AnvilChunkLoader.readWorldEntityPos failure path, setDelayToMin. CPU==CUDA. */
#ifndef MC_TILE_ENTITY_SPAWNER_H
#define MC_TILE_ENTITY_SPAWNER_H

#include "mc.h"
#include "mc_rng.h"
#include "mob_spawning.h"

/* mc_entity.h ENT_* values (avoid include: struct World fwd conflicts with mc_world.h under nvcc). */
enum {
    TES_ENT_ZOMBIE = 3, TES_ENT_SKELETON = 4, TES_ENT_SPIDER = 6, TES_ENT_SILVERFISH = 11
};

#define TES_BLK_MOB_SPAWNER 52

#define TES_NUM_TICKS    600
#define TES_DUMP_FIELDS  3

enum {
    TES_PURPOSE_RESET = 20,
    TES_PURPOSE_SPAWN = 21,
};

typedef struct {
    MsScene world;
    i32 sx, sy, sz;
    float player_x, player_y, player_z;
    i32 spawn_delay;
    u8  entity_id;
    i32 spawn_count;
    i32 min_spawn_delay;
    i32 max_spawn_delay;
    i32 max_nearby;
    i32 activate_range;
    i32 spawn_range;
    i32 nearby_count;
} TeSpawnerScene;

MC_HD static inline i32 tes_floor_double(double v) {
    i32 iv = (i32)v;
    if ((double)iv > v) iv--;
    return iv;
}

MC_HD static inline int tes_is_activated(const TeSpawnerScene *s) {
    double cx = (double)s->sx + 0.5;
    double cy = (double)s->sy + 0.5;
    double cz = (double)s->sz + 0.5;
    double dx = (double)s->player_x - cx;
    double dy = (double)s->player_y - cy;
    double dz = (double)s->player_z - cz;
    double r = (double)s->activate_range;
    /* World.isAnyPlayerWithinRangeAt: `range < 0 || getDistanceSq < range*range` -- STRICT
     * less-than (a player exactly at the range boundary does NOT activate the spawner). */
    if (r < 0.0) return 1;
    return dx * dx + dy * dy + dz * dz < r * r;
}

MC_HD static inline void tes_reset_timer(TeSpawnerScene *s) {
    u64 h;
    i32 span;
    if (s->max_spawn_delay <= s->min_spawn_delay) {
        s->spawn_delay = s->min_spawn_delay;
        return;
    }
    span = s->max_spawn_delay - s->min_spawn_delay;
    h = mc_hash_seed(s->world.seed, s->world.tick, s->sx, s->sy, s->sz, TES_PURPOSE_RESET);
    s->spawn_delay = s->min_spawn_delay + mc_hash_bound(h, span);
}

MC_HD static inline void tes_build_room(TeSpawnerScene *s) {
    u16 stone = mc_state(BLK_STONE, 0);
    u16 spawner = mc_state(TES_BLK_MOB_SPAWNER, 0);
    int x, z;
    int px = s->sx;
    int py = s->sy;
    int pz = s->sz;

    for (x = px - 2; x <= px + 2; ++x)
        for (z = pz - 2; z <= pz + 2; ++z) {
            if (!ms_in(x, py, z)) continue;
            if (x == px - 2 || x == px + 2 || z == pz - 2 || z == pz + 2) {
                ms_set(s->world.blocks, x, py, z, stone);
                if (ms_in(x, py + 1, z))
                    ms_set(s->world.blocks, x, py + 1, z, stone);
            }
        }

    ms_set(s->world.blocks, px, py, pz, spawner);
    ms_build_light(s->world.blocks, s->world.sky, s->world.blk);
}

MC_HD static inline void tes_init(TeSpawnerScene *s, u64 seed) {
    static const u8 ents[4] = {
        TES_ENT_ZOMBIE, TES_ENT_SKELETON, TES_ENT_SPIDER, TES_ENT_SILVERFISH
    };

    ms_init_flat(&s->world, seed);
    s->sx = 8;
    s->sy = MS_FLOOR_Y + 1;
    s->sz = 8;
    tes_build_room(s);

    s->player_x = 8.5f;
    s->player_y = (float)(MS_FLOOR_Y + 1);
    s->player_z = 4.5f;

    s->spawn_delay = 20 + (i32)(seed % 17);
    s->entity_id = ents[seed % 4];
    s->spawn_count = 4;
    s->min_spawn_delay = 200;
    s->max_spawn_delay = 800;
    s->max_nearby = 6;
    s->activate_range = 16;
    s->spawn_range = 4;
    s->nearby_count = 0;
    s->world.tick = 0;
}

MC_HD static inline void tes_update(TeSpawnerScene *s) {
    int i;
    int spawned = 0;

    if (!tes_is_activated(s))
        return;

    if (s->spawn_delay == -1) {
        tes_reset_timer(s);
    }

    if (s->spawn_delay > 0) {
        s->spawn_delay--;
        return;
    }

    for (i = 0; i < s->spawn_count; ++i) {
        u64 h;
        double r1, r2;
        double px, py, pz;
        i32 bx, by, bz;

        if (s->nearby_count >= s->max_nearby) {
            tes_reset_timer(s);
            return;
        }

        h = mc_hash_seed(s->world.seed, s->world.tick, s->sx, s->sy, s->sz, TES_PURPOSE_SPAWN);
        h = mc_hash64(h ^ (u64)(i * 7919ULL));
        r1 = (double)mc_hash_f01(h);
        h = mc_hash64(h + 1);
        r2 = (double)mc_hash_f01(h);
        px = (double)s->sx + (r1 - r2) * (double)s->spawn_range + 0.5;
        h = mc_hash64(h + 2);
        {
            i32 yoff = mc_hash_bound(h, 3) - 1;
            py = (double)(s->sy + yoff);
        }
        h = mc_hash64(h + 3);
        r1 = (double)mc_hash_f01(h);
        h = mc_hash64(h + 4);
        r2 = (double)mc_hash_f01(h);
        pz = (double)s->sz + (r1 - r2) * (double)s->spawn_range + 0.5;

        bx = tes_floor_double(px);
        by = tes_floor_double(py);
        bz = tes_floor_double(pz);

        if (!ms_in(bx, by, bz)) continue;

        if (ms_can_spawn_blocks(s->world.blocks, bx, by, bz)) {
            s->nearby_count++;
            spawned = 1;
        }
    }

    if (spawned)
        tes_reset_timer(s);
}

MC_HD static inline void tes_run(TeSpawnerScene *s, u64 seed, int nticks, u64 *out) {
    int t;
    tes_init(s, seed);
    for (t = 0; t < nticks; ++t) {
        s->world.tick = t;
        tes_update(s);
        out[t * TES_DUMP_FIELDS + 0] = (u64)(u32)s->spawn_delay;
        out[t * TES_DUMP_FIELDS + 1] = (u64)(u32)s->entity_id;
        out[t * TES_DUMP_FIELDS + 2] = (u64)(u32)s->spawn_count;
    }
}

#endif /* MC_TILE_ENTITY_SPAWNER_H */
