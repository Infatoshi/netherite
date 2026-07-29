/* block_tickers: BlockGrass spread, BlockFire burn subset, BlockFalling sand/gravel.
 * PORT: BlockGrass.updateTick, BlockFire.updateTick (tryCatchFire neighbors), BlockFalling.checkFallable.
 * Runtime randomness = mc_hash_rng (SPEC rule 1), NOT java.util.Random. Double-buffered 16^2 x 32 cube.
 * Fire subset: flammability table for planks/log/leaves/tallgrass only; no rain/difficulty/end. */
#ifndef MC_BLOCK_TICKERS_H
#define MC_BLOCK_TICKERS_H

#include "mc.h"
#include "mc_world.h"
#include "mc_rng.h"
#include "mc_blocks.h"
#include "block_props_table.h"
#include "mc_gamerules.h"   /* doFireTick gate (BlockFire.updateTick first line) */

#define BT_W 16
#define BT_H 32
#define BT_VOL (BT_W * BT_W * BT_H)
#define BT_NTICKS 24

enum { BT_PURPOSE_GRASS = 1, BT_PURPOSE_FIRE = 2 };

typedef struct {
    u16 blocks_a[BT_VOL];
    u16 blocks_b[BT_VOL];
    u8  light_above[BT_VOL]; /* synthetic skylight at y+1 for grass test (0..15) */
    int cur;
    u64 seed;
    i64 tick;
} BtWorld;

MC_HD static inline int bt_idx(int x, int y, int z) {
    return (y * BT_W + z) * BT_W + x;
}

MC_HD static inline u16 *bt_now(BtWorld *w) { return w->cur ? w->blocks_b : w->blocks_a; }
MC_HD static inline u16 *bt_next(BtWorld *w) { return w->cur ? w->blocks_a : w->blocks_b; }

MC_HD static inline u16 bt_get(const u16 *b, int x, int y, int z) {
    if (x < 0 || x >= BT_W || y < 0 || y >= BT_H || z < 0 || z >= BT_W) return mc_state(BLK_AIR, 0);
    return b[bt_idx(x, y, z)];
}

MC_HD static inline void bt_set(u16 *b, int x, int y, int z, u16 s) {
    if (x < 0 || x >= BT_W || y < 0 || y >= BT_H || z < 0 || z >= BT_W) return;
    b[bt_idx(x, y, z)] = s;
}

MC_HD static inline int bt_id(u16 s) { return mc_state_id(s); }

MC_HD static inline int bt_can_fall_through(u16 s) {
    int id = bt_id(s);
    if (id == BLK_AIR) return 1;
    if (id == 51) return 1; /* fire */
    BptProps p = mc_bpt_props(id);
    return (p.flags & BF_LIQUID) != 0;
}

MC_HD static inline int bt_is_flammable(int id) {
    return id == BLK_PLANKS || id == BLK_LOG || id == BLK_LEAVES || id == 31; /* tallgrass */
}

MC_HD static inline void bt_copy(u16 *dst, const u16 *src) {
    for (int i = 0; i < BT_VOL; ++i) dst[i] = src[i];
}

MC_HD static inline void bt_init(BtWorld *w, u64 seed) {
    w->cur = 0; w->seed = seed; w->tick = 0;
    u16 air = mc_state(BLK_AIR, 0), grass = mc_state(BLK_GRASS, 0), dirt = mc_state(BLK_DIRT, 0);
    u16 stone = mc_state(BLK_STONE, 0), sand = mc_state(BLK_SAND, 0), gravel = mc_state(BLK_GRAVEL, 0);
    u16 fire = mc_state(51, 0), planks = mc_state(BLK_PLANKS, 0);
    u16 *b = w->blocks_a;
    for (int i = 0; i < BT_VOL; ++i) { b[i] = air; w->light_above[i] = 15; }
    /* grass decay: grass under stone (light 0) */
    bt_set(b, 8, 10, 8, grass);
    bt_set(b, 8, 11, 8, stone);
    w->light_above[bt_idx(8, 10, 8)] = 0;
    /* grass spread: dirt patch with bright neighbor */
    bt_set(b, 4, 10, 4, dirt);
    bt_set(b, 4, 11, 4, air);
    w->light_above[bt_idx(4, 10, 4)] = 12;
    bt_set(b, 3, 10, 4, grass);
    /* sand fall: column over air hole */
    bt_set(b, 12, 8, 4, sand);
    bt_set(b, 12, 7, 4, sand);
    bt_set(b, 12, 6, 4, gravel);
    /* fire + planks */
    bt_set(b, 2, 10, 2, fire);
    bt_set(b, 2, 10, 3, planks);
    bt_set(b, 3, 10, 2, planks);
    bt_copy(w->blocks_b, w->blocks_a);
}

MC_HD static inline void bt_tick_grass(BtWorld *w, const u16 *now, u16 *next, int x, int y, int z) {
    u16 s = bt_get(now, x, y, z);
    if (bt_id(s) != BLK_GRASS) return;
    int la = (int)w->light_above[bt_idx(x, y, z)];
    u16 above = bt_get(now, x, y + 1, z);
    BptProps ap = mc_bpt_props(bt_id(above));
    if (la < 4 && ap.light_opacity > 2) {
        bt_set(next, x, y, z, mc_state(BLK_DIRT, 0));
        return;
    }
    if (la < 9) return;
    for (int i = 0; i < 4; ++i) {
        u64 h = mc_hash_seed(w->seed, w->tick, x, y, z, BT_PURPOSE_GRASS);
        h = mc_hash64(h ^ (u64)i);
        i32 dx = mc_hash_bound(h, 3) - 1;
        h = mc_hash64(h + 1);
        i32 dy = mc_hash_bound(h, 5) - 3;
        h = mc_hash64(h + 2);
        i32 dz = mc_hash_bound(h, 3) - 1;
        int nx = x + dx, ny = y + dy, nz = z + dz;
        if (ny < 0 || ny >= BT_H) continue;
        u16 ns = bt_get(now, nx, ny, nz);
        if (bt_id(ns) != BLK_DIRT) continue;
        u16 ab = bt_get(now, nx, ny + 1, nz);
        BptProps abp = mc_bpt_props(bt_id(ab));
        int nla = (ny + 1 < BT_H) ? (int)w->light_above[bt_idx(nx, ny, nz)] : 15;
        if (nla >= 4 && abp.light_opacity <= 2)
            bt_set(next, nx, ny, nz, mc_state(BLK_GRASS, 0));
    }
}

MC_HD static inline void bt_tick_falling(BtWorld *w, const u16 *now, u16 *next) {
    (void)w;
    for (int y = 1; y < BT_H; ++y)
        for (int z = 0; z < BT_W; ++z)
            for (int x = 0; x < BT_W; ++x) {
                u16 s = bt_get(next, x, y, z);
                int id = bt_id(s);
                if (id != BLK_SAND && id != BLK_GRAVEL) continue;
                u16 below = bt_get(next, x, y - 1, z);
                if (!bt_can_fall_through(below)) continue;
                int ly = y - 1;
                while (ly > 0 && bt_can_fall_through(bt_get(next, x, ly - 1, z))) --ly;
                bt_set(next, x, y, z, mc_state(BLK_AIR, 0));
                bt_set(next, x, ly, z, s);
            }
}

/* BlockFire.updateTick: `if (getGameRules().getBoolean("doFireTick"))` wraps the whole body,
 * so doFireTick=0 -> updateTick returns immediately and fire never spreads/ages/extinguishes.
 * gr threads GameRules; default (doFireTick=1) is bit-identical to prior behavior. */
MC_HD static inline void bt_tick_fire_gr(BtWorld *w, const u16 *now, u16 *next,
                                         const McGameRules *gr) {
    if (!gr->doFireTick) return;
    for (int z = 0; z < BT_W; ++z)
        for (int y = 0; y < BT_H; ++y)
            for (int x = 0; x < BT_W; ++x) {
                u16 s = bt_get(now, x, y, z);
                if (bt_id(s) != 51) continue;
                static const int dx[] = {1,-1,0,0,0,0}, dy[] = {0,0,-1,1,0,0}, dz[] = {0,0,0,0,1,-1};
                for (int f = 0; f < 6; ++f) {
                    int nx = x + dx[f], ny = y + dy[f], nz = z + dz[f];
                    u16 ns = bt_get(now, nx, ny, nz);
                    int nid = bt_id(ns);
                    if (!bt_is_flammable(nid)) continue;
                    u64 h = mc_hash_seed(w->seed, w->tick, nx, ny, nz, BT_PURPOSE_FIRE);
                    if (mc_hash_bound(h, 100) > 15) continue; /* ~15% spread chance */
                    if (bt_get(next, nx, ny, nz) == mc_state(BLK_AIR, 0))
                        bt_set(next, nx, ny, nz, mc_state(51, 0));
                }
            }
}

/* Default-rules wrapper (doFireTick=1). */
MC_HD static inline void bt_tick_fire(BtWorld *w, const u16 *now, u16 *next) {
    McGameRules gr = mc_gamerules_default();
    bt_tick_fire_gr(w, now, next, &gr);
}

MC_HD static inline void bt_tick(BtWorld *w) {
    const u16 *now = bt_now(w);
    u16 *next = bt_next(w);
    bt_copy(next, now);
    for (int z = 0; z < BT_W; ++z)
        for (int y = 0; y < BT_H; ++y)
            for (int x = 0; x < BT_W; ++x)
                bt_tick_grass(w, now, next, x, y, z);
    bt_tick_falling(w, now, next);
    bt_tick_fire(w, now, next);
    w->tick++;
    w->cur ^= 1;
}

MC_HD static inline void bt_run(BtWorld *w) {
    bt_init(w, w->seed);
    for (int t = 0; t < BT_NTICKS; ++t) bt_tick(w);
}

/* ==================================================================================
 * HALO-AWARE (cross-chunk) block tickers.  ADDED for tick_world_halo (Wave 15+).
 *
 * The verified per-chunk trb_* tickers key their hash RNG on the GLOBAL world seed but
 * LOCAL (in-chunk) x,z, so the same cell would draw a DIFFERENT stream depending on which
 * chunk owns it - wrong once neighbors interact. These variants (bth_*) operate over a
 * gdim x gdim grid of Chunks (grid position (gx,gz) -> chunks[gz*gdim+gx], matching
 * tick_world_multi) in REGION coordinates and key the RNG on the WORLD coordinate
 * (world_seed, tick, wx, wy, wz, purpose) so a cell's stream is identical regardless of
 * chunk decomposition (SPEC rule 1). Neighbor reads/writes cross chunk borders (grass can
 * spread across a boundary); OOB region edge = AIR. SPEC rule 3: read now_grid, write
 * next_grid (caller pre-copies now->next). Scope mirrors this file: grass, fire, falling.
 * ================================================================================== */

MC_HD static inline u16 bth_get(const Chunk *grid, int gdim, int rx, int y, int rz) {
    int gx, gz;
    if (rx < 0 || rz < 0 || y < 0 || y >= MC_CY) return mc_state(BLK_AIR, 0);
    gx = rx >> 4; gz = rz >> 4;
    if (gx >= gdim || gz >= gdim) return mc_state(BLK_AIR, 0);
    return mc_get(&grid[gz * gdim + gx], rx & 15, y, rz & 15);
}
MC_HD static inline void bth_set(Chunk *grid, int gdim, int rx, int y, int rz, u16 s) {
    int gx, gz;
    if (rx < 0 || rz < 0 || y < 0 || y >= MC_CY) return;
    gx = rx >> 4; gz = rz >> 4;
    if (gx >= gdim || gz >= gdim) return;
    mc_set(&grid[gz * gdim + gx], rx & 15, y, rz & 15, s);
}
MC_HD static inline int bth_sky(const Chunk *grid, int gdim, int rx, int y, int rz) {
    int gx, gz;
    if (rx < 0 || rz < 0 || y < 0 || y >= MC_CY) return 0;
    gx = rx >> 4; gz = rz >> 4;
    if (gx >= gdim || gz >= gdim) return 0;
    return mc_light_sky(grid[gz * gdim + gx].light[mc_idx(rx & 15, y, rz & 15)]);
}

/* BlockGrass.updateTick, world-coordinate RNG. rx,y,rz are region/world coords. */
MC_HD static inline void bth_tick_grass(const Chunk *now, Chunk *next, int gdim,
                                        u64 world_seed, i64 tick, int rx, int y, int rz) {
    u16 s = bth_get(now, gdim, rx, y, rz);
    int sky;
    u16 above;
    BptProps ap;
    if (mc_state_id(s) != BLK_GRASS) return;
    sky = bth_sky(now, gdim, rx, y, rz);
    above = bth_get(now, gdim, rx, y + 1, rz);
    ap = mc_bpt_props(mc_state_id(above));
    if (sky < 4 && ap.light_opacity > 2) {
        bth_set(next, gdim, rx, y, rz, mc_state(BLK_DIRT, 0));
        return;
    }
    if (sky < 9) return;
    {
        int i;
        for (i = 0; i < 4; ++i) {
            u64 h = mc_hash_seed(world_seed, tick, rx, y, rz, BT_PURPOSE_GRASS);
            i32 dx, dy, dz;
            int nx, ny, nz, nsky;
            u16 ns, ab;
            BptProps abp;
            h = mc_hash64(h ^ (u64)i);   dx = mc_hash_bound(h, 3) - 1;
            h = mc_hash64(h + 1);        dy = mc_hash_bound(h, 5) - 3;
            h = mc_hash64(h + 2);        dz = mc_hash_bound(h, 3) - 1;
            nx = rx + dx; ny = y + dy; nz = rz + dz;
            if (ny < 0 || ny >= MC_CY) continue;
            ns = bth_get(now, gdim, nx, ny, nz);
            if (mc_state_id(ns) != BLK_DIRT) continue;
            ab = bth_get(now, gdim, nx, ny + 1, nz);
            abp = mc_bpt_props(mc_state_id(ab));
            nsky = bth_sky(now, gdim, nx, ny, nz);
            if (nsky >= 4 && abp.light_opacity <= 2)
                bth_set(next, gdim, nx, ny, nz, mc_state(BLK_GRASS, 0));
        }
    }
}

/* BlockFire.updateTick spread subset, world-coordinate RNG. doFireTick=0 -> no-op (see bt_tick_fire_gr).
 * Default rules (doFireTick=1) are bit-identical to prior behavior. */
MC_HD static inline void bth_tick_fire_gr(const Chunk *now, Chunk *next, int gdim,
                                          u64 world_seed, i64 tick, int rx, int y, int rz,
                                          const McGameRules *gr) {
    static const int dx[] = {1, -1, 0, 0, 0, 0};
    static const int dy[] = {0, 0, -1, 1, 0, 0};
    static const int dz[] = {0, 0, 0, 0, 1, -1};
    int f;
    if (!gr->doFireTick) return;
    if (mc_state_id(bth_get(now, gdim, rx, y, rz)) != 51) return;
    for (f = 0; f < 6; ++f) {
        int nx = rx + dx[f], ny = y + dy[f], nz = rz + dz[f];
        int nid = mc_state_id(bth_get(now, gdim, nx, ny, nz));
        u64 h;
        if (!bt_is_flammable(nid)) continue;
        h = mc_hash_seed(world_seed, tick, nx, ny, nz, BT_PURPOSE_FIRE);
        if (mc_hash_bound(h, 100) > 15) continue;
        if (mc_state_id(bth_get(next, gdim, nx, ny, nz)) == BLK_AIR)
            bth_set(next, gdim, nx, ny, nz, mc_state(51, 0));
    }
}

/* Default-rules wrapper (doFireTick=1). */
MC_HD static inline void bth_tick_fire(const Chunk *now, Chunk *next, int gdim,
                                       u64 world_seed, i64 tick, int rx, int y, int rz) {
    McGameRules gr = mc_gamerules_default();
    bth_tick_fire_gr(now, next, gdim, world_seed, tick, rx, y, rz, &gr);
}

/* BlockFalling: sand/gravel drop within a column (operates on next like trb_tick_falling). */
MC_HD static inline void bth_tick_falling_cell(Chunk *next, int gdim, int rx, int y, int rz) {
    u16 s = bth_get(next, gdim, rx, y, rz);
    int id = mc_state_id(s);
    int ly;
    if (id != BLK_SAND && id != BLK_GRAVEL) return;
    if (!bt_can_fall_through(bth_get(next, gdim, rx, y - 1, rz))) return;
    ly = y - 1;
    while (ly > 0 && bt_can_fall_through(bth_get(next, gdim, rx, ly - 1, rz))) --ly;
    bth_set(next, gdim, rx, y, rz, mc_state(BLK_AIR, 0));
    bth_set(next, gdim, rx, ly, rz, s);
}

/* One halo-aware block-ticker pass over the region's y-window [oy, oy+h). */
MC_HD static inline void bth_tick_grid(const Chunk *now, Chunk *next, int gdim,
                                       u64 world_seed, i64 tick, int oy, int h) {
    int rnx = gdim * 16, rnz = gdim * 16;
    int rx, rz, y;
    for (rz = 0; rz < rnz; ++rz)
        for (y = oy; y < oy + h; ++y)
            for (rx = 0; rx < rnx; ++rx)
                bth_tick_grass(now, next, gdim, world_seed, tick, rx, y, rz);
    for (rz = 0; rz < rnz; ++rz)
        for (y = oy; y < oy + h; ++y)
            for (rx = 0; rx < rnx; ++rx)
                bth_tick_fire(now, next, gdim, world_seed, tick, rx, y, rz);
    for (y = oy + 1; y < oy + h; ++y)
        for (rz = 0; rz < rnz; ++rz)
            for (rx = 0; rx < rnx; ++rx)
                bth_tick_falling_cell(next, gdim, rx, y, rz);
}

#endif /* MC_BLOCK_TICKERS_H */
