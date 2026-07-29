/* item_bucket_world: ItemBucket.onItemUse on real chunk slice (fill/place fluid sources).
 *
 * Subset: empty-bucket fill from flowing source meta 0, water/lava bucket place as flowing
 * source meta 0, empty bucket after place. Uses tick_world_copy 16x256x16 chunk + 17x4x17 fluid
 * slice fixtures (fluid_flow water_bucket pattern). Per-step battery: fill/place fail+success,
 * one ff_ca_step after placements. READ-ONLY: items_core.h (ic_mk, item ids), fluid_flow.h
 * (ff_ca_step, slice dims), tick_world_copy.h (twc_init_world, twc_blocks_hash). CUT: rayTrace,
 * Forge onBucketUse, sounds/stats, creative mode, nether vaporize, destroyBlock replaceable. */
#ifndef MC_ITEM_BUCKET_WORLD_H
#define MC_ITEM_BUCKET_WORLD_H

#include "tick_world_copy.h"
#include "items_core.h"
#include "fluid_flow.h"

#define IBW_SLICE_OX 0
#define IBW_SLICE_OY 62
#define IBW_SLICE_OZ 0
#define IBW_SLICE_NX FF_DIM_WB_X
#define IBW_SLICE_NY FF_DIM_WB_Y
#define IBW_SLICE_NZ FF_DIM_WB_Z
#define IBW_SLICE_VOL (IBW_SLICE_NX * IBW_SLICE_NY * IBW_SLICE_NZ)

#define IBW_NUM_STEPS 10
#define IBW_DUMP_FIELDS 5
#define IBW_OUT (IBW_NUM_STEPS * IBW_DUMP_FIELDS)

enum {
    IBW_EVT_NONE      = 0,
    IBW_EVT_FILL_OK   = 1,
    IBW_EVT_FILL_FAIL = 2,
    IBW_EVT_PLACE_OK  = 3,
    IBW_EVT_PLACE_FAIL = 4,
    IBW_EVT_CA        = 5
};

typedef struct {
    int wx0, wy0, wz0;
    int wx1, wy1, wz1;
    int lx, ly, lz;
    int pwx, pwy, pwz;
    int plx, ply, plz;
    int fail_x, fail_y, fail_z;
} IbwScene;

/* Same semantics as ic_bucket_fill / ic_bucket_place on a real Chunk cell. */
MC_HD static inline ICStack ibw_bucket_fill(Chunk *c, ICStack bucket, int x, int y, int z) {
    u16 s = mc_get(c, x, y, z);
    int id = mc_state_id(s);
    if (bucket.item != IC_BUCKET || bucket.count != 1) return bucket;
    if (id == IC_FLOWING_WATER && mc_state_meta(s) == 0) {
        mc_set(c, x, y, z, mc_state(BLK_AIR, 0));
        return ic_mk(IC_WATER_BUCKET, 1, 0);
    }
    if (id == IC_FLOWING_LAVA && mc_state_meta(s) == 0) {
        mc_set(c, x, y, z, mc_state(BLK_AIR, 0));
        return ic_mk(IC_LAVA_BUCKET, 1, 0);
    }
    return bucket;
}

MC_HD static inline ICStack ibw_bucket_place(Chunk *c, ICStack bucket, int x, int y, int z) {
    u16 s = mc_get(c, x, y, z);
    if (mc_state_id(s) != BLK_AIR) return bucket;
    if (bucket.item == IC_WATER_BUCKET && bucket.count == 1) {
        mc_set(c, x, y, z, mc_state(BLK_FLOWING_WATER, 0));
        return ic_mk(IC_BUCKET, 1, 0);
    }
    if (bucket.item == IC_LAVA_BUCKET && bucket.count == 1) {
        mc_set(c, x, y, z, mc_state(BLK_FLOWING_LAVA, 0));
        return ic_mk(IC_BUCKET, 1, 0);
    }
    return bucket;
}

MC_HD static inline void ibw_extract_slice(const Chunk *c, u16 *buf) {
    int x, y, z;
    for (y = 0; y < IBW_SLICE_NY; ++y)
        for (z = 0; z < IBW_SLICE_NZ; ++z)
            for (x = 0; x < IBW_SLICE_NX; ++x)
                ff_set(buf, IBW_SLICE_NX, IBW_SLICE_NY, IBW_SLICE_NZ, x, y, z,
                       mc_get(c, IBW_SLICE_OX + x, IBW_SLICE_OY + y, IBW_SLICE_OZ + z));
}

MC_HD static inline void ibw_merge_slice(Chunk *c, const u16 *buf) {
    int x, y, z;
    for (y = 0; y < IBW_SLICE_NY; ++y)
        for (z = 0; z < IBW_SLICE_NZ; ++z)
            for (x = 0; x < IBW_SLICE_NX; ++x)
                mc_set(c, IBW_SLICE_OX + x, IBW_SLICE_OY + y, IBW_SLICE_OZ + z,
                       ff_get(buf, IBW_SLICE_NX, IBW_SLICE_NY, IBW_SLICE_NZ, x, y, z));
}

MC_HD static inline u64 ibw_slice_hash(const u16 *buf) {
    u64 h = 0xcbf29ce484222325ULL;
    int i;
    for (i = 0; i < IBW_SLICE_VOL; ++i) {
        h ^= (u64)buf[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

MC_HD static inline void ibw_init_fluids(Chunk *c, u64 seed, IbwScene *sc) {
    u16 stone = mc_state(FF_BLK_STONE, 0);
    u16 air = mc_state(FF_BLK_AIR, 0);
    int x, z;

    for (z = 0; z < IBW_SLICE_NZ; ++z)
        for (x = 0; x < IBW_SLICE_NX; ++x) {
            mc_set(c, IBW_SLICE_OX + x, IBW_SLICE_OY + 0, IBW_SLICE_OZ + z, air);
            mc_set(c, IBW_SLICE_OX + x, IBW_SLICE_OY + 1, IBW_SLICE_OZ + z, stone);
            mc_set(c, IBW_SLICE_OX + x, IBW_SLICE_OY + 2, IBW_SLICE_OZ + z, air);
            mc_set(c, IBW_SLICE_OX + x, IBW_SLICE_OY + 3, IBW_SLICE_OZ + z, air);
        }

    {
        u64 hv = mc_hash_seed(seed, 2, 0, 0, 0, 3);
        sc->wx0 = 4 + (int)mc_hash_bound(hv, IBW_SLICE_NX - 8);
        hv = mc_hash64(hv + 1ULL);
        sc->wz0 = 4 + (int)mc_hash_bound(hv, IBW_SLICE_NZ - 8);
        sc->wy0 = IBW_SLICE_OY + 2;
        hv = mc_hash64(hv + 2ULL);
        sc->wx1 = 4 + (int)mc_hash_bound(hv, IBW_SLICE_NX - 8);
        hv = mc_hash64(hv + 3ULL);
        sc->wz1 = 4 + (int)mc_hash_bound(hv, IBW_SLICE_NZ - 8);
        sc->wy1 = IBW_SLICE_OY + 2;
        hv = mc_hash64(hv + 4ULL);
        sc->lx = 4 + (int)mc_hash_bound(hv, IBW_SLICE_NX - 8);
        hv = mc_hash64(hv + 5ULL);
        sc->lz = 4 + (int)mc_hash_bound(hv, IBW_SLICE_NZ - 8);
        sc->ly = IBW_SLICE_OY + 1;
        hv = mc_hash64(hv + 6ULL);
        sc->pwx = 4 + (int)mc_hash_bound(hv, IBW_SLICE_NX - 8);
        hv = mc_hash64(hv + 7ULL);
        sc->pwz = 4 + (int)mc_hash_bound(hv, IBW_SLICE_NZ - 8);
        sc->pwy = IBW_SLICE_OY + 2;
        hv = mc_hash64(hv + 8ULL);
        sc->plx = 4 + (int)mc_hash_bound(hv, IBW_SLICE_NX - 8);
        hv = mc_hash64(hv + 9ULL);
        sc->plz = 4 + (int)mc_hash_bound(hv, IBW_SLICE_NZ - 8);
        sc->ply = IBW_SLICE_OY + 1;
        sc->fail_x = IBW_SLICE_OX + 8;
        sc->fail_y = IBW_SLICE_OY + 1;
        sc->fail_z = IBW_SLICE_OZ + 8;
    }

    mc_set(c, sc->wx0, sc->wy0, sc->wz0, mc_state(BLK_FLOWING_WATER, 0));
    mc_set(c, sc->wx1, sc->wy1, sc->wz1, mc_state(BLK_FLOWING_WATER, 0));
    mc_set(c, sc->lx, sc->ly, sc->lz, mc_state(BLK_FLOWING_LAVA, 0));
}

MC_HD static inline void ibw_emit_step(const ICStack *stack, int tx, int ty, int tz,
                                       const Chunk *c, u64 slice_hash, i32 event,
                                       u64 *out, int base) {
    u16 cell = mc_get(c, tx, ty, tz);
    out[base + 0] = (u64)(u32)stack->item;
    out[base + 1] = (u64)(u32)stack->count;
    out[base + 2] = (u64)(u32)mc_state_id(cell);
    out[base + 3] = slice_hash;
    out[base + 4] = (u64)(u32)event;
}

MC_HD static inline ICStack ibw_do_fill(Chunk *c, ICStack stack, int x, int y, int z, i32 *event) {
    ICStack next = ibw_bucket_fill(c, stack, x, y, z);
    *event = (next.item != stack.item) ? IBW_EVT_FILL_OK : IBW_EVT_FILL_FAIL;
    return next;
}

MC_HD static inline ICStack ibw_do_place(Chunk *c, ICStack stack, int x, int y, int z, i32 *event) {
    ICStack next = ibw_bucket_place(c, stack, x, y, z);
    *event = (next.item != stack.item) ? IBW_EVT_PLACE_OK : IBW_EVT_PLACE_FAIL;
    return next;
}

MC_HD static inline void ibw_run(u64 seed, u64 *out, World *w, u16 *cur, u16 *tmp) {
    Chunk *c;
    IbwScene sc;
    ICStack stack;
    u64 slice_hash;
    int step;

    twc_init_world(w, seed);
    c = &w->chunk[0];
    ibw_init_fluids(c, seed, &sc);
    stack = ic_mk(IC_BUCKET, 1, 0);

    for (step = 0; step < IBW_NUM_STEPS; ++step) {
        i32 event = IBW_EVT_NONE;
        int tx = sc.fail_x, ty = sc.fail_y, tz = sc.fail_z;

        switch (step) {
        case 0:
            tx = sc.wx0; ty = sc.wy0; tz = sc.wz0;
            stack = ibw_do_fill(c, stack, tx, ty, tz, &event);
            break;
        case 1:
            tx = sc.pwx; ty = sc.pwy; tz = sc.pwz;
            stack = ibw_do_place(c, stack, tx, ty, tz, &event);
            break;
        case 2:
            tx = sc.fail_x; ty = sc.fail_y; tz = sc.fail_z;
            stack = ibw_do_fill(c, stack, tx, ty, tz, &event);
            break;
        case 3:
            tx = sc.lx; ty = sc.ly; tz = sc.lz;
            stack = ibw_do_fill(c, stack, tx, ty, tz, &event);
            break;
        case 4:
            tx = sc.plx; ty = sc.ply; tz = sc.plz;
            stack = ibw_do_place(c, stack, tx, ty, tz, &event);
            break;
        case 5:
            tx = sc.pwx; ty = sc.pwy; tz = sc.pwz;
            stack = ibw_do_fill(c, stack, tx, ty, tz, &event);
            break;
        case 6:
            tx = sc.fail_x; ty = sc.fail_y; tz = sc.fail_z;
            stack = ibw_do_place(c, stack, tx, ty, tz, &event);
            break;
        case 7:
            tx = sc.wx1; ty = sc.wy1; tz = sc.wz1;
            stack = ibw_do_fill(c, stack, tx, ty, tz, &event);
            break;
        case 8:
            ibw_extract_slice(c, cur);
            ff_ca_step(cur, tmp, IBW_SLICE_NX, IBW_SLICE_NY, IBW_SLICE_NZ);
            ibw_merge_slice(c, tmp);
            tx = sc.pwx; ty = sc.pwy; tz = sc.pwz;
            event = IBW_EVT_CA;
            break;
        case 9:
            tx = sc.pwx; ty = sc.pwy; tz = sc.pwz;
            stack = ibw_do_fill(c, stack, tx, ty, tz, &event);
            break;
        default:
            break;
        }

        ibw_extract_slice(c, cur);
        slice_hash = ibw_slice_hash(cur);
        ibw_emit_step(&stack, tx, ty, tz, c, slice_hash, event, out, step * IBW_DUMP_FIELDS);
    }

    (void)twc_blocks_hash(w);
}

#endif /* MC_ITEM_BUCKET_WORLD_H */
