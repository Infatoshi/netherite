/* world_tick_vanilla.h - the CANONICAL WorldServer.tick phase order (P0 item 2, PORT_MATRIX C/P0).
 * PORT TARGET: world/WorldServer.java tick() L180-242 + updateBlocks() L389-505 +
 * tickUpdates(false) L720-800, world/World.java updateLCG coordinate hash.
 *
 * This driver is ADDITIVE: the CA-style world_step.h / tick_compose_full.h paths are
 * untouched and keep building. Unlike those (double-buffered CAs, stateless hash RNG),
 * this driver is deliberately VANILLA-SHAPED: one world buffer mutated IN PLACE in
 * vanilla's sequential order, a stateful world JavaRandom, and the int32 updateLCG
 * stream for coordinate picks - because RNG-stream alignment against the real game
 * (PORT_MATRIX B.2) is impossible under the CA model. CPU==CUDA still holds: every
 * function is MC_HD and sequential-deterministic; device use is one thread per world
 * (batch across worlds), the same execution model as the worldgen kernels.
 *
 * PHASE ORDER (WorldServer.tick, then updateEntities SEPARATELY from the server loop):
 *   0. super.tick() weather timers        -> documented no-op (no weather state, P4)
 *   1. mobSpawner findChunksForSpawning   -> msw harness scene when provided, gated on
 *      doMobSpawning + spawnHostileMobs (msw is hostile-only; peaceful + the %400
 *      spawnOnSetTickRate flag are P2 wiring)
 *   2. chunkProvider.tick                 -> documented no-op (fixed region, no
 *      chunk load/unload/drop queue)
 *   3. skylightSubtracted = calculateSkylightSubtracted(1.0F)  (exact formula, rain=
 *      thunder=0 until P4 weather)
 *   4. totalWorldTime++; worldTime++ if doDaylightCycle
 *   5. tickUpdates(false)                 -> scheduled_ticks.h drain (65536 cap,
 *      area-loaded recheck radius 0 per the Forge-patched 1.11.2 source, block-match
 *      gate, updateTick dispatch)
 *   6. updateBlocks per chunk             -> playerCheckLight (no players -> no rand
 *      draw, faithful), thunder (gated on raining&&thundering, both false -> no rand
 *      draw, faithful), iceandsnow (rand.nextInt(16) drawn EVERY chunk + updateLCG
 *      advance on hit, freeze applied), randomTick loop (randomTickSpeed per eligible
 *      16^3 section, updateLCG 3-coordinate extraction, exact)
 *   7. village tick                       -> documented no-op (villages not ported, P5)
 *   8. sendQueuedBlockEvents              -> documented no-op (no block-event
 *      producers ported yet - comparators/pistons/note blocks are P7)
 *   wt_vanilla_update_entities            -> documented no-op skeleton in vanilla
 *      World.updateEntities order (entity spine is P2)
 *
 * KNOWN FIRST-CUT DIVERGENCES (each marked at the call site):
 *   - updateTick BODIES reuse the existing CA-era single-cell logic (bth_ fire spread,
 *     ff_ flow rule, hash-RNG grass/crops); their INTERNAL rand draws are not yet the
 *     vanilla this.rand stream. P1 tick-traces verify/replace them one by one. The
 *     QUEUE timing, phase order, world-rand draw points, and updateLCG stream - the
 *     P0 contract - are exact.
 *   - chunk iteration is row-major over the fixed region; vanilla iterates the
 *     playerChunkMap order.
 *   - biome temperature for freezing uses the flat biome byte (no per-coord noise
 *     above y=64; vanilla getFloatTemperature adds Perlin jitter there). */
#ifndef MC_WORLD_TICK_VANILLA_H
#define MC_WORLD_TICK_VANILLA_H

#include "mc.h"
#include "mc_world.h"
#include "mc_rng.h"
#include "mc_math.h"
#include "mc_blocks.h"
#include "block_props_table.h"
#include "mc_gamerules.h"        /* read-only contract: driver takes const McGameRules * */
#include "scheduled_ticks.h"
#include "block_tickers.h"       /* bth_ grid accessors + grass/fire single-cell tickers */
#include "block_tickers_crops.h" /* btc_ wheat/farmland/ice/snow helpers */
#include "fluid_flow.h"          /* ff_flow_cell single-cell liquid rule */
#include "biome_props_full.h"    /* mc_bpf_temperature for the freeze check */
#include "mob_spawning_world.h"  /* msw harness scene for phase 1 */

#include <math.h>                /* Math.cos in calculateCelestialAngle (JDK libm) */

#define WT_DIM MC_WORLD_DIM                    /* region chunks per side (TU-selectable) */
#define WT_NXZ (WT_DIM * 16)                   /* region blocks per side */

#define WT_BLK_FIRE 51

/* liquid updateTick working window: ff_flow_distance recurses 4 cells laterally, so
 * radius 6 covers every read; writes stay within 1 cell of the center. */
#define WT_FW 13
#define WT_FH 3
#define WT_FVOL (WT_FW * WT_FW * WT_FH)

/* scheduled-tick dispatch log (test/trace evidence; wraps by dropping). */
#define WT_FIRED_MAX 64

/* EntityFallingBlock spine (allocate-once pool; P1 falling tick-trace). Spawned
 * centered on its column with zero horizontal motion, so only posY/motionY evolve
 * and only the spawn column can collide (AABB 0.98 wide inside the block). */
#define WT_FALL_MAX 16
typedef struct WtFallEnt {
    double posY, motionY;          /* AABB bottom; EntityFallingBlock vertical motion */
    i32 x, z;                      /* column */
    u16 tile;                      /* fallTile block state */
    i32 fallTime;
    i32 active;
} WtFallEnt;

typedef struct WtvState {
    McScheduledTicks stq;
    JavaRandom rand;               /* World.rand (worldgen-style stateful LCG) */
    i64 totalWorldTime;            /* WorldInfo.getWorldTotalTime */
    i64 worldTime;                 /* WorldInfo.getWorldTime (daylight) */
    i32 updateLCG;                 /* World.updateLCG, seeded from rand.nextInt() */
    i32 skylightSubtracted;
    i32 spawnHostileMobs, spawnPeacefulMobs;   /* WorldServer flags, default 1 */
    /* dispatch evidence */
    i32 fired_n;
    StqEntry fired[WT_FIRED_MAX];
    i64 fired_at[WT_FIRED_MAX];
    /* liquid window scratch (allocate-once: lives in this struct) */
    u16 fcur[WT_FVOL], fnext[WT_FVOL];
    /* falling-block entity pool (loadedEntityList order = spawn order) */
    WtFallEnt fall[WT_FALL_MAX];
    i32 fall_n;
} WtvState;

/* ---- region accessors (bth_ grid convention: row-major chunk[gz*gdim+gx]) ---- */

MC_HD static inline int wt_in_region(int rx, int y, int rz) {
    return rx >= 0 && rx < WT_NXZ && rz >= 0 && rz < WT_NXZ && y >= 0 && y < MC_CY;
}
MC_HD static inline u16 wt_get(const World *w, int rx, int y, int rz) {
    return bth_get(w->chunk, WT_DIM, rx, y, rz);
}
MC_HD static inline void wt_set(World *w, int rx, int y, int rz, u16 s) {
    bth_set(w->chunk, WT_DIM, rx, y, rz, s);
}
MC_HD static inline u8 wt_light_raw(const World *w, int rx, int y, int rz) {
    if (!wt_in_region(rx, y, rz)) return 0;
    return w->chunk[(rz >> 4) * WT_DIM + (rx >> 4)].light[mc_idx(rx & 15, y, rz & 15)];
}
MC_HD static inline int wt_biome(const World *w, int rx, int rz) {
    if (rx < 0 || rx >= WT_NXZ || rz < 0 || rz >= WT_NXZ) return 1;
    return w->chunk[(rz >> 4) * WT_DIM + (rx >> 4)].biome[(rz & 15) * MC_CX + (rx & 15)];
}

/* ---- World.updateLCG: `updateLCG = updateLCG * 3 + 1013904223` on int32, coordinates
 * from `j = updateLCG >> 2` (arithmetic): x = j & 15, z = j >> 8 & 15, y = j >> 16 & 15.
 * ONE advance yields all three coordinates. ---- */
MC_HD static inline i32 wt_lcg_advance(i32 *lcg) {
    *lcg = (i32)((u32)*lcg * 3u + 1013904223u);
    return *lcg >> 2;
}

/* ---- phase 3: World.calculateSkylightSubtracted(1.0F), rain/thunder strength 0 ---- */

/* WorldProvider.calculateCelestialAngle(worldTime, partialTicks=1.0F): JDK Math.cos. */
MC_HD static inline float wt_celestial_angle(i64 world_time) {
    i32 i = (i32)(world_time % 24000LL);
    float f = ((float)i + 1.0f) / 24000.0f - 0.25f;
    float f1;
    if (f < 0.0f) f += 1.0f;
    if (f > 1.0f) f -= 1.0f;
    f1 = 1.0f - (float)((cos((double)f * MC_PI) + 1.0) / 2.0);
    return f + (f1 - f) / 3.0f;
}

/* World.getSunBrightnessFactor: MathHelper.cos (sin table); rain/thunder terms are
 * (1 - strength*5/16) with strength 0 -> multiply by 1 (P4 wires weather state). */
MC_HD static inline i32 wt_calc_skylight_subtracted(const McSinTable *st, i64 world_time) {
    float f = wt_celestial_angle(world_time);
    float f1 = 1.0f - (mc_cos(st, f * ((float)MC_PI * 2.0f)) * 2.0f + 0.5f);
    if (f1 < 0.0f) f1 = 0.0f;
    if (f1 > 1.0f) f1 = 1.0f;
    f1 = 1.0f - f1;
    f1 = 1.0f - f1;                 /* calculateSkylightSubtracted: f = 1 - factor */
    return (i32)(f1 * 11.0f);
}

/* ---- scheduled updateTick dispatch bodies (first-cut, see header note) ---- */

/* BlockLiquid.tickRate: water 5; lava 30 overworld (10 with hasNoSky - nether is a
 * separate provider config, this driver is overworld). */
MC_HD static inline i32 wt_liquid_tick_rate(int id) {
    return (id == BLK_FLOWING_WATER || id == BLK_WATER) ? 5 : 30;
}

MC_HD static inline int wt_fidx(int x, int y, int z) { return (y * WT_FW + z) * WT_FW + x; }

/* BlockStaticLiquid.neighborChanged -> updateLiquid: a NOTIFYING block change at
 * (x,y,z) re-dynamicizes adjacent static liquid (same meta) and schedules it at
 * tickRate. Shared by the liquid updateTick write-back and every other ported
 * setBlockState-flag-3 site (ice melt, snow melt). */
MC_HD static inline void wt_wake_static_liquid_neighbors(WtvState *s, World *w,
                                                         int x, int y, int z) {
    static const int DX[6] = { 0, 0, 0, 0, -1, 1 };
    static const int DY[6] = { -1, 1, 0, 0, 0, 0 };
    static const int DZ[6] = { 0, 0, -1, 1, 0, 0 };
    int d;
    for (d = 0; d < 6; ++d) {
        int ax = x + DX[d], ay = y + DY[d], az = z + DZ[d];
        u16 av; int aid, fid;
        if (!wt_in_region(ax, ay, az)) continue;
        av = wt_get(w, ax, ay, az);
        aid = mc_state_id(av);
        if (aid != BLK_WATER && aid != BLK_LAVA) continue;
        fid = (aid == BLK_WATER) ? BLK_FLOWING_WATER : BLK_FLOWING_LAVA;
        wt_set(w, ax, ay, az, mc_state(fid, mc_state_meta(av)));
        stq_update_block_tick(&s->stq, ax, ay, az, fid,
                              wt_liquid_tick_rate(fid), 0, s->totalWorldTime, 0, 1);
    }
}

/* BlockDynamicLiquid.updateTick via the verified single-cell CA rule (ff_flow_cell)
 * over a local window, with vanilla TIMING: every changed cell that is a flowing
 * liquid is (re)scheduled at its tickRate - the onBlockAdded/updateTick schedule
 * points. Settled cells (center turned static / nothing changed) stop the chain.
 * FIRST-CUT: flow geometry is the netherite-derived CA rule, not a line-for-line
 * BlockDynamicLiquid port; P1 fluid tick-trace replaces the body if it diverges. */
MC_HD MC_NOINLINE static void wt_liquid_update_tick(World *w, WtvState *s, int rx, int y, int rz) {
    int x, yy, z;
    for (yy = 0; yy < WT_FH; ++yy)
        for (z = 0; z < WT_FW; ++z)
            for (x = 0; x < WT_FW; ++x) {
                u16 v = wt_get(w, rx + x - 6, y + yy - 1, rz + z - 6);
                s->fcur[wt_fidx(x, yy, z)] = v;
                s->fnext[wt_fidx(x, yy, z)] = v;
            }
    ff_react_lava(s->fcur, s->fnext, WT_FW, WT_FH, WT_FW, 6, 1, 6);
    ff_flow_cell(s->fcur, s->fnext, WT_FW, WT_FH, WT_FW, 6, 1, 6);
    /* pass A: apply every change; (re)schedule cells that end up flowing (the
     * onBlockAdded/updateTick schedule points). placeStaticBlock conversions apply
     * here too so pass B sees the post-conversion world (vanilla converts BEFORE
     * tryFlowInto fires neighbor notifications). */
    for (yy = 0; yy < WT_FH; ++yy)
        for (z = 0; z < WT_FW; ++z)
            for (x = 0; x < WT_FW; ++x) {
                u16 nv = s->fnext[wt_fidx(x, yy, z)];
                int wx, wy, wz, nid;
                if (nv == s->fcur[wt_fidx(x, yy, z)]) continue;
                wx = rx + x - 6; wy = y + yy - 1; wz = rz + z - 6;
                if (!wt_in_region(wx, wy, wz)) continue;
                wt_set(w, wx, wy, wz, nv);
                nid = mc_state_id(nv);
                if (nid == BLK_FLOWING_WATER || nid == BLK_FLOWING_LAVA)
                    stq_update_block_tick(&s->stq, wx, wy, wz, nid,
                                          wt_liquid_tick_rate(nid), 0,
                                          s->totalWorldTime, 0, 1);
            }
    /* pass B: BlockStaticLiquid.neighborChanged -> updateLiquid. Every NOTIFYING change
     * re-dynamicizes adjacent static liquid (same meta) and schedules it at tickRate.
     * placeStaticBlock uses setBlockState flag 2 (no neighbor notify), so a pure
     * flowing->static conversion of the same liquid+meta does not notify - that is what
     * lets a settled cell STAY static until some real neighbor change wakes it. */
    for (yy = 0; yy < WT_FH; ++yy)
        for (z = 0; z < WT_FW; ++z)
            for (x = 0; x < WT_FW; ++x) {
                u16 ov = s->fcur[wt_fidx(x, yy, z)], nv = s->fnext[wt_fidx(x, yy, z)];
                int oid, nid, wx, wy, wz;
                if (nv == ov) continue;
                oid = mc_state_id(ov); nid = mc_state_id(nv);
                if (((oid == BLK_FLOWING_WATER && nid == BLK_WATER) ||
                     (oid == BLK_FLOWING_LAVA && nid == BLK_LAVA)) &&
                    mc_state_meta(ov) == mc_state_meta(nv))
                    continue;                                  /* flag-2: no notify */
                wx = rx + x - 6; wy = y + yy - 1; wz = rz + z - 6;
                if (!wt_in_region(wx, wy, wz)) continue;
                wt_wake_static_liquid_neighbors(s, w, wx, wy, wz);
            }
}

/* ---- BlockFire fire-info tables (BlockFire.init: setFireInfo(block, encouragement,
 * flammability)). getFireSpreadSpeed == encouragement; getFlammability == flammability.
 * Legacy numeric ids; blocks not listed return 0 (non-flammable). ---- */
MC_HD static inline int wt_fire_flammability(int id) {
    switch (id) {
        case 5:                                          /* planks */
        case 125: case 126:                              /* double/wooden slab */
        case 107: case 183: case 184: case 185: case 186: case 187:  /* fence gates */
        case 85:  case 188: case 189: case 190: case 191: case 192:  /* fences */
        case 53:  case 134: case 135: case 136: case 163: case 164:  /* wood stairs */
            return 20;
        case 17: case 162: return 5;                     /* log / log2 */
        case 18: case 161: return 60;                    /* leaves / leaves2 */
        case 47: return 20;                              /* bookshelf */
        case 46: return 100;                             /* tnt */
        case 31: case 175: case 37: case 38: case 32: return 100;    /* plants/flowers/deadbush */
        case 35: return 60;                              /* wool */
        case 106: return 100;                            /* vine */
        case 173: return 5;                              /* coal block */
        case 170: return 20;                             /* hay block */
        case 171: return 20;                             /* carpet */
        default: return 0;
    }
}
MC_HD static inline int wt_fire_encouragement(int id) {
    switch (id) {
        case 5:
        case 125: case 126:
        case 107: case 183: case 184: case 185: case 186: case 187:
        case 85:  case 188: case 189: case 190: case 191: case 192:
        case 53:  case 134: case 135: case 136: case 163: case 164:
        case 17: case 162: case 173: return 5;
        case 18: case 161: case 35: case 47: return 30;  /* leaves / wool / bookshelf */
        case 46: case 106: return 15;                    /* tnt / vine */
        case 31: case 175: case 37: case 38: case 32:
        case 170: case 171: return 60;                   /* plants / hay / carpet */
        default: return 0;
    }
}

/* Block.isFullyOpaque proxy: an opaque full cube (light_opacity 255, solid, non-air). */
MC_HD static inline int wt_is_fully_opaque(int id) {
    BptProps p;
    if (id == BLK_AIR) return 0;
    p = mc_bpt_props(id);
    return (p.flags & BF_SOLID) && !(p.flags & BF_LIQUID) && p.light_opacity >= 255;
}

/* BlockFire.canCatchFire(pos, face) == neighbor.isFlammable == getFlammability > 0. */
MC_HD static inline int wt_can_catch_fire(const World *w, int x, int y, int z) {
    return wt_fire_flammability(mc_state_id(wt_get(w, x, y, z))) > 0;
}

/* BlockFire.canNeighborCatchFire: any of the 6 faces flammable. */
MC_HD static inline int wt_can_neighbor_catch_fire(const World *w, int x, int y, int z) {
    static const int dx[] = {1, -1, 0, 0, 0, 0};
    static const int dy[] = {0, 0, -1, 1, 0, 0};
    static const int dz[] = {0, 0, 0, 0, 1, -1};
    int f;
    for (f = 0; f < 6; ++f)
        if (wt_can_catch_fire(w, x + dx[f], y + dy[f], z + dz[f])) return 1;
    return 0;
}

/* BlockFire.getNeighborEncouragement: 0 unless pos is air, else the max
 * getFireSpreadSpeed over the 6 neighbors. */
MC_HD static inline int wt_neighbor_encouragement(const World *w, int x, int y, int z) {
    static const int dx[] = {1, -1, 0, 0, 0, 0};
    static const int dy[] = {0, 0, -1, 1, 0, 0};
    static const int dz[] = {0, 0, 0, 0, 1, -1};
    int f, best = 0, e;
    if (mc_state_id(wt_get(w, x, y, z)) != BLK_AIR) return 0;
    for (f = 0; f < 6; ++f) {
        e = wt_fire_encouragement(mc_state_id(wt_get(w, x + dx[f], y + dy[f], z + dz[f])));
        if (e > best) best = e;
    }
    return best;
}

/* WorldServer difficulty for the fire spread rate (getDifficultyId()*7). Overworld
 * scenario runs NORMAL (2); the trace world sets /difficulty normal to match. */
#define WT_DIFFICULTY_ID 2

/* BlockFire.onBlockAdded (overworld, no portal): extinguish if it cannot stay, else
 * scheduleUpdate(tickRate + rand.nextInt(10)) drawing world.rand - called at each new
 * fire placement so the schedule + its rand draw land in vanilla order. */
MC_HD static inline void wt_fire_on_block_added(World *w, WtvState *s, int x, int y, int z) {
    if (!wt_is_fully_opaque(mc_state_id(wt_get(w, x, y - 1, z))) &&
        !wt_can_neighbor_catch_fire(w, x, y, z)) {
        wt_set(w, x, y, z, mc_state(BLK_AIR, 0));
    } else {
        stq_update_block_tick(&s->stq, x, y, z, WT_BLK_FIRE,
                              30 + jrand_int_bound(&s->rand, 10), 0,
                              s->totalWorldTime, 0, 1);
    }
}

/* BlockFire.tryCatchFire(pos, chance, rand, age, face): ALWAYS draws rand.nextInt(chance)
 * (even for non-flammable targets - draw order is load-bearing); on a catch, sets fire
 * (age + rand.nextInt(5)/4) or clears to air, and onBlockAdded schedules the new fire.
 * isRainingAt is always false in the trace scenario (weather cleared). */
MC_HD static inline void wt_try_catch_fire(World *w, WtvState *s, int x, int y, int z,
                                           int chance, int age) {
    int i = wt_fire_flammability(mc_state_id(wt_get(w, x, y, z)));
    if (jrand_int_bound(&s->rand, chance) < i) {
        if (jrand_int_bound(&s->rand, age + 10) < 5 /* && !isRainingAt */) {
            int j = age + jrand_int_bound(&s->rand, 5) / 4;
            if (j > 15) j = 15;
            wt_set(w, x, y, z, mc_state(WT_BLK_FIRE, j));
            wt_fire_on_block_added(w, s, x, y, z);
        } else {
            wt_set(w, x, y, z, mc_state(BLK_AIR, 0));
        }
    }
}

/* BlockFire.updateTick faithful re-port (BlockFire.java L146-253), driven by the
 * scheduled-tick queue. Overworld, non-rain, non-humid scenario simplifications
 * (each valid for the trace world and marked): isFireSource(below)=false (no
 * netherrack/magma), isRaining()=false (rain branch + canDie short-circuit before any
 * draw), isBlockinHighHumidity=false (flag1=0, j=0). Difficulty = WT_DIFFICULTY_ID.
 * The rand DRAW ORDER matches vanilla exactly, so this WOULD match the real game on an
 * aligned rand stream (P1 caveat: the world rand history differs, so exact cells do not
 * match; the trace verifies the reschedule cadence + spread legality instead). */
MC_HD MC_NOINLINE static void wt_fire_update_tick(World *w, WtvState *s, const McGameRules *gr,
                                                  int rx, int y, int rz) {
    int i, k, l, i1;
    if (!gr->doFireTick) return;                    /* vanilla: no reschedule either */

    /* canPlaceBlockAt: below fullyOpaque OR a neighbor can catch fire. */
    if (!wt_is_fully_opaque(mc_state_id(wt_get(w, rx, y - 1, rz))) &&
        !wt_can_neighbor_catch_fire(w, rx, y, rz))
        wt_set(w, rx, y, rz, mc_state(BLK_AIR, 0));

    /* flag = isFireSource(below): false for overworld fuels (netherrack/magma unported). */
    i = mc_state_meta(wt_get(w, rx, y, rz));         /* AGE (re-read: may have gone to air) */
    if (mc_state_id(wt_get(w, rx, y, rz)) != WT_BLK_FIRE) return;  /* cleared above */

    /* rain branch: `!flag && isRaining() && ...` - isRaining()=false short-circuits (no draw). */
    if (i < 15) {
        i = i + jrand_int_bound(&s->rand, 3) / 2;    /* age bump (world-rand) */
        if (i > 15) i = 15;
        wt_set(w, rx, y, rz, mc_state(WT_BLK_FIRE, i));   /* flag 4: type unchanged, no onBlockAdded */
    }
    stq_update_block_tick(&s->stq, rx, y, rz, WT_BLK_FIRE,
                          30 + jrand_int_bound(&s->rand, 10), 0,   /* self-reschedule */
                          s->totalWorldTime, 0, 1);

    /* !flag branch: die out when no fuel around, or the age-15 no-fuel-below clear. */
    if (!wt_can_neighbor_catch_fire(w, rx, y, rz)) {
        if (!wt_is_fully_opaque(mc_state_id(wt_get(w, rx, y - 1, rz))) || i > 3)
            wt_set(w, rx, y, rz, mc_state(BLK_AIR, 0));
        return;
    }
    if (!wt_can_catch_fire(w, rx, y - 1, rz) && i == 15 && jrand_int_bound(&s->rand, 4) == 0) {
        wt_set(w, rx, y, rz, mc_state(BLK_AIR, 0));
        return;
    }

    /* tryCatchFire the 6 faces in vanilla order: E W D U N S (chances 300/250, humidity 0). */
    wt_try_catch_fire(w, s, rx + 1, y, rz, 300, i);   /* east  */
    wt_try_catch_fire(w, s, rx - 1, y, rz, 300, i);   /* west  */
    wt_try_catch_fire(w, s, rx, y - 1, rz, 250, i);   /* down  */
    wt_try_catch_fire(w, s, rx, y + 1, rz, 250, i);   /* up    */
    wt_try_catch_fire(w, s, rx, y, rz + 1, 300, i);   /* north (+z) */
    wt_try_catch_fire(w, s, rx, y, rz - 1, 300, i);   /* south (-z) */

    /* spread into air pockets in the -1..+1 (x,z) x -1..+4 (y) box. */
    for (k = -1; k <= 1; ++k)
        for (l = -1; l <= 1; ++l)
            for (i1 = -1; i1 <= 4; ++i1) {
                int j1, k1;
                if (k == 0 && i1 == 0 && l == 0) continue;
                j1 = 100;
                if (i1 > 1) j1 += (i1 - 1) * 100;
                k1 = wt_neighbor_encouragement(w, rx + k, y + i1, rz + l);
                if (k1 > 0) {
                    int l1 = (k1 + 40 + WT_DIFFICULTY_ID * 7) / (i + 30);   /* humidity 0 */
                    if (l1 > 0 && jrand_int_bound(&s->rand, j1) <= l1 /* && !isRaining */) {
                        int i2 = i + jrand_int_bound(&s->rand, 5) / 4;
                        if (i2 > 15) i2 = 15;
                        wt_set(w, rx + k, y + i1, rz + l, mc_state(WT_BLK_FIRE, i2));
                        wt_fire_on_block_added(w, s, rx + k, y + i1, rz + l);
                    }
                }
            }
}

/* ---- BlockFalling (sand/gravel) + EntityFallingBlock port ---- */

/* BlockFalling.canFallThrough: FIRE block, or material AIR / WATER / LAVA. */
MC_HD static inline int wt_can_fall_through(int id) {
    return id == WT_BLK_FIRE || id == BLK_AIR ||
           id == BLK_FLOWING_WATER || id == BLK_WATER ||
           id == BLK_FLOWING_LAVA || id == BLK_LAVA;
}

/* BlockFalling.updateTick -> checkFallable, area-loaded path: spawn EntityFallingBlock
 * at (x+0.5, y, z+0.5); setSize(0.98) shifts posY up by (1.0F-0.98F)/2. The BLOCK stays
 * until the entity's first onUpdate (same server tick: entities run after block ticks).
 * The fallInstantly / area-not-loaded teleport path is worldgen-only here. */
MC_HD static inline void wt_falling_update_tick(World *w, WtvState *s, int rx, int y, int rz) {
    u16 v = wt_get(w, rx, y, rz);
    int id = mc_state_id(v);
    WtFallEnt *e;
    if (id != BLK_SAND && id != BLK_GRAVEL) return;
    if (y <= 0 || !wt_can_fall_through(mc_state_id(wt_get(w, rx, y - 1, rz)))) return;
    if (s->fall_n >= WT_FALL_MAX) {                /* compact dead entries, keep order */
        int i, n = 0;
        for (i = 0; i < s->fall_n; ++i)
            if (s->fall[i].active) s->fall[n++] = s->fall[i];
        s->fall_n = n;
        if (s->fall_n >= WT_FALL_MAX) return;      /* pool truly full: drop spawn */
    }
    e = &s->fall[s->fall_n++];
    e->active = 1;
    e->fallTime = 0;
    e->x = rx; e->z = rz;
    e->posY = (double)y + (double)((1.0f - 0.98f) / 2.0f);
    e->motionY = 0.0;
    e->tile = v;
}

/* BlockFalling.neighborChanged: any notifying change next to sand/gravel reschedules
 * it at tickRate 2 (checkFallable decides at fire time). Wired for the entity paths
 * (block claim -> setBlockToAir, landing -> setBlockState flag 3); other producers of
 * neighbor notifications (fluids, fire) are not yet wired to falling (documented gap). */
MC_HD static inline void wt_notify_falling_neighbors(WtvState *s, const World *w,
                                                     int x, int y, int z) {
    static const int DX[6] = { 0, 0, 0, 0, -1, 1 };
    static const int DY[6] = { -1, 1, 0, 0, 0, 0 };
    static const int DZ[6] = { 0, 0, -1, 1, 0, 0 };
    int d;
    for (d = 0; d < 6; ++d) {
        int ax = x + DX[d], ay = y + DY[d], az = z + DZ[d], aid;
        if (!wt_in_region(ax, ay, az)) continue;
        aid = mc_state_id(wt_get(w, ax, ay, az));
        if (aid == BLK_SAND || aid == BLK_GRAVEL)
            stq_update_block_tick(&s->stq, ax, ay, az, aid, 2, 0, s->totalWorldTime, 0, 1);
    }
}

/* EntityFallingBlock.onUpdate: claim block on first tick, gravity + move + drag, land
 * (place fallTile) when the downward sweep clips a non-fall-through top. Zero horizontal
 * motion -> only the spawn column collides. */
MC_HD static void wt_fall_onupdate(World *w, WtvState *s, WtFallEnt *e) {
    int bx = e->x, bz = e->z, by, onGround = 0;
    double b0, b1;
    if (e->fallTime++ == 0) {
        by = (int)floor(e->posY);
        if (mc_state_id(wt_get(w, bx, by, bz)) == mc_state_id(e->tile)) {
            wt_set(w, bx, by, bz, mc_state(BLK_AIR, 0));
            wt_notify_falling_neighbors(s, w, bx, by, bz);
        } else { e->active = 0; return; }          /* block replaced under us: die */
    }
    e->motionY -= 0.03999999910593033;             /* gravity */
    b0 = e->posY;
    b1 = b0 + e->motionY;
    if (e->motionY < 0.0) {                        /* move(SELF): clip the down sweep */
        for (by = (int)floor(b0); by >= (int)floor(b1); --by) {
            if (by < 0) break;
            if ((double)(by + 1) > b0) continue;   /* cell not fully below the start */
            if (!wt_can_fall_through(mc_state_id(wt_get(w, bx, by, bz)))) {
                b1 = (double)(by + 1);
                onGround = 1;                      /* collidedVertically, motionY < 0 */
                break;
            }
        }
    }
    e->posY = b1;
    e->motionY *= 0.9800000190734863;              /* drag */
    if (onGround) {
        /* Forge guard: support became fall-through-able again -> keep falling. */
        by = (int)floor(e->posY - 0.009999999776482582);
        if (by >= 0 && wt_can_fall_through(mc_state_id(wt_get(w, bx, by, bz))))
            return;
        e->motionY *= -0.5;
        e->active = 0;                             /* setDead */
        by = (int)floor(e->posY);
        /* mayPlace(air here) && !canFallThrough(below) -> setBlockState(fallTile, 3) */
        if (mc_state_id(wt_get(w, bx, by, bz)) == BLK_AIR &&
            !wt_can_fall_through(mc_state_id(wt_get(w, bx, by - 1, bz)))) {
            wt_set(w, bx, by, bz, e->tile);
            wt_notify_falling_neighbors(s, w, bx, by, bz);
        }
        /* else vanilla drops an item entity: nothing to place */
    } else if (e->fallTime > 600 ||
               (e->fallTime > 100 && ((int)floor(e->posY) < 1 || (int)floor(e->posY) > 256))) {
        e->active = 0;                             /* out-of-world / timeout: drop item */
    }
}

/* Block.updateTick dispatch for a drained scheduled entry. Blocks without a ported
 * scheduled body (redstone, buttons, saplings-from-bonemeal, ...) no-op here. */
MC_HD MC_NOINLINE static void wt_dispatch_scheduled(World *w, WtvState *s, const McGameRules *gr,
                                                    const StqEntry *e) {
    switch (e->block) {
        case BLK_FLOWING_WATER:
        case BLK_FLOWING_LAVA:
            wt_liquid_update_tick(w, s, e->x, e->y, e->z);
            break;
        case WT_BLK_FIRE:
            wt_fire_update_tick(w, s, gr, e->x, e->y, e->z);
            break;
        case BLK_SAND:
        case BLK_GRAVEL:
            wt_falling_update_tick(w, s, e->x, e->y, e->z);
            break;
        default:
            break;                                 /* unported updateTick: no-op */
    }
}

/* ---- random-tick bodies (Block.randomTick == updateTick for these blocks) ---- */

/* Block.getTickRandomly() for the PORTED set. Vanilla also randomly ticks saplings,
 * farmland, leaves, static liquids (lava fire seeding), mycelium, cactus, reed, vines,
 * cocoa, stems, netherwart, chorus, grass-path, snow-melt-by-light: unported (P4). */
MC_HD static inline int wt_ticks_randomly(int id) {
    return id == BLK_GRASS || id == BTC_BLK_WHEAT || id == BLK_ICE || id == BLK_SNOW_LAYER;
}

/* BlockCrops.updateTick (wheat) with region accessors; growth-chance math is the
 * verified btc port shape; RNG is the hash draw (first-cut, see header). */
MC_HD MC_NOINLINE static void wt_tick_crop(World *w, i64 tick, int rx, int y, int rz) {
    u16 sst = wt_get(w, rx, y, rz);
    int age, sky, bound;
    float gf = 1.0f;
    if (mc_state_id(sst) != BTC_BLK_WHEAT) return;
    if (!btc_is_farmland(wt_get(w, rx, y - 1, rz))) {
        wt_set(w, rx, y, rz, mc_state(BLK_AIR, 0));  /* canBlockStay fail -> drop (item drop unported) */
        return;
    }
    sky = mc_light_sky(wt_light_raw(w, rx, y, rz));
    if (sky < 9) return;                             /* getLightFromNeighbors(pos.up()) >= 9 */
    age = btc_crop_age(sst);
    if (age < 0 || age >= 7) return;
    {
        int i, j;
        for (i = -1; i <= 1; ++i)
            for (j = -1; j <= 1; ++j) {
                float f1 = 0.0f;
                u16 soil = wt_get(w, rx + i, y - 1, rz + j);
                if (btc_is_farmland(soil)) {
                    f1 = 1.0f;
                    if (btc_farmland_fertile(soil)) f1 = 3.0f;
                }
                if (i != 0 || j != 0) f1 /= 4.0f;
                gf += f1;
            }
    }
    {
        int fx = (mc_state_id(wt_get(w, rx - 1, y, rz)) == BTC_BLK_WHEAT ||
                  mc_state_id(wt_get(w, rx + 1, y, rz)) == BTC_BLK_WHEAT);
        int fz = (mc_state_id(wt_get(w, rx, y, rz - 1)) == BTC_BLK_WHEAT ||
                  mc_state_id(wt_get(w, rx, y, rz + 1)) == BTC_BLK_WHEAT);
        if (fx && fz) gf /= 2.0f;
        else if (mc_state_id(wt_get(w, rx - 1, y, rz - 1)) == BTC_BLK_WHEAT ||
                 mc_state_id(wt_get(w, rx + 1, y, rz - 1)) == BTC_BLK_WHEAT ||
                 mc_state_id(wt_get(w, rx + 1, y, rz + 1)) == BTC_BLK_WHEAT ||
                 mc_state_id(wt_get(w, rx - 1, y, rz + 1)) == BTC_BLK_WHEAT)
            gf /= 2.0f;
    }
    bound = (int)(25.0f / gf) + 1;
    if (bound < 1) bound = 1;
    if (mc_hash_bound(mc_hash_seed(w->seed, tick, rx, y, rz, BTC_PURPOSE_CROP), bound) == 0)
        wt_set(w, rx, y, rz, btc_wheat_age(age + 1));
}

MC_HD MC_NOINLINE static void wt_random_tick(World *w, WtvState *s, const McGameRules *gr,
                                             int rx, int y, int rz, int id) {
    (void)gr;
    switch (id) {
        case BLK_GRASS:
            /* BlockGrass.updateTick (hash-RNG spread subset; in place, first-cut) */
            bth_tick_grass(w->chunk, w->chunk, WT_DIM, w->seed, s->totalWorldTime, rx, y, rz);
            break;
        case BTC_BLK_WHEAT:
            wt_tick_crop(w, s->totalWorldTime, rx, y, rz);
            break;
        case BLK_ICE:
            /* BlockIce.updateTick -> turnIntoWater: setBlockState(WATER) (flag 3:
             * adjacent static liquid reawakens) then neighborChanged on ITSELF ->
             * BlockStaticLiquid.updateLiquid, so the melted cell is observably
             * FLOWING_WATER scheduled @5 until it settles back to static. */
            if (mc_light_block(wt_light_raw(w, rx, y, rz)) >
                11 - (int)mc_bpt_props(BLK_ICE).light_opacity) {
                wt_set(w, rx, y, rz, mc_state(BLK_FLOWING_WATER, 0));
                stq_update_block_tick(&s->stq, rx, y, rz, BLK_FLOWING_WATER, 5, 0,
                                      s->totalWorldTime, 0, 1);
                wt_wake_static_liquid_neighbors(s, w, rx, y, rz);
            }
            break;
        case BLK_SNOW_LAYER:
            /* BlockSnow.updateTick: melt when blockLight > 11 (setBlockToAir: flag 3) */
            if (mc_light_block(wt_light_raw(w, rx, y, rz)) > 11) {
                wt_set(w, rx, y, rz, mc_state(BLK_AIR, 0));
                wt_wake_static_liquid_neighbors(s, w, rx, y, rz);
            }
            break;
        default:
            break;
    }
}

/* ---- phase 6 weather helpers ---- */

/* World.getPrecipitationHeight: first block from the top whose material blocks
 * movement or is liquid; returns that y + 1. */
MC_HD static inline int wt_precipitation_height(const World *w, int rx, int rz) {
    int y;
    for (y = MC_CY - 1; y >= 0; --y) {
        u16 sv = wt_get(w, rx, y, rz);
        int id = mc_state_id(sv);
        BptProps p;
        if (id == BLK_AIR) continue;
        p = mc_bpt_props(id);
        if (p.light_opacity > 0 || (p.flags & BF_LIQUID)) return y + 1;
    }
    return 0;
}

/* World.canBlockFreeze(pos, noWaterAdj=true) shape: biome temperature >= 0.15F ->
 * no; blockLight >= 10 -> no; must be a water SOURCE; fully-water-surrounded -> no. */
MC_HD MC_NOINLINE static int wt_can_block_freeze_no_water(const World *w, int rx, int y, int rz) {
    u16 sv;
    int id;
    if (mc_bpf_temperature(wt_biome(w, rx, rz)) >= 0.15f) return 0;
    if (y < 0 || y >= MC_CY) return 0;
    if (mc_light_block(wt_light_raw(w, rx, y, rz)) >= 10) return 0;
    sv = wt_get(w, rx, y, rz);
    id = mc_state_id(sv);
    if (!(id == BLK_WATER || id == BLK_FLOWING_WATER) || mc_state_meta(sv) != 0) return 0;
    {
        int wsurr = ff_is_water(wt_get(w, rx - 1, y, rz)) && ff_is_water(wt_get(w, rx + 1, y, rz)) &&
                    ff_is_water(wt_get(w, rx, y, rz - 1)) && ff_is_water(wt_get(w, rx, y, rz + 1));
        if (wsurr) return 0;                       /* noWaterAdj: needs a non-water side */
    }
    return 1;
}

/* ---- phase 5: WorldServer.tickUpdates(false) ---- */

MC_HD MC_NOINLINE static int wt_tick_updates(World *w, WtvState *s, const McGameRules *gr, int tick_all) {
    i32 n = stq_begin_tick(&s->stq, s->totalWorldTime, tick_all);
    i32 i;
    for (i = 0; i < n; ++i) {
        StqEntry e = s->stq.this_tick[i];       /* copy: dispatch may grow the queue */
        /* isAreaLoaded(pos.add(0,0,0), pos.add(0,0,0)): radius 0 in the Forge-patched
         * 1.11.2 source (the +-8 pre-Forge byte survives only as a comment there);
         * for the fixed region that is a bounds check. */
        if (wt_in_region(e.x, e.y, e.z)) {
            u16 sv = wt_get(w, e.x, e.y, e.z);
            int id = mc_state_id(sv);
            /* material != AIR && Block.isEqualTo(current, scheduled) */
            if (id != BLK_AIR && id == e.block) {
                if (s->fired_n < WT_FIRED_MAX) {
                    s->fired[s->fired_n] = e;
                    s->fired_at[s->fired_n] = s->totalWorldTime;
                    s->fired_n++;
                }
                wt_dispatch_scheduled(w, s, gr, &e);
            }
        } else {
            /* not loaded: vanilla re-schedules with delay 0 */
            stq_update_block_tick(&s->stq, e.x, e.y, e.z, e.block, 0, 0,
                                  s->totalWorldTime, 0, 1);
        }
    }
    stq_end_tick(&s->stq);
    return stq_has_pending(&s->stq);
}

/* ---- phase 6: WorldServer.updateBlocks ---- */

MC_HD MC_NOINLINE static void wt_update_blocks(World *w, WtvState *s, const McGameRules *gr) {
    int ci;
    int rts = gr->randomTickSpeed;
    /* playerCheckLight: draws rand only when playerEntities is non-empty; the driver
     * carries no players yet, so faithfully no draw (P2 wires players). */
    for (ci = 0; ci < WT_DIM * WT_DIM; ++ci) {
        int cbx = (ci % WT_DIM) * 16;              /* chunk.xPosition * 16 */
        int cbz = (ci / WT_DIM) * 16;
        int sec;
        /* enqueueRelightChecks + chunk.onTick(false): no-op stubs (relight queue and
         * chunk population retries do not exist in the fixed region). */
        /* thunder: `canDoLightning && raining && thundering && rand.nextInt(100000)`
         * short-circuits before the draw while weather is off -> faithfully no draw. */
        /* iceandsnow: rand.nextInt(16) EVERY chunk, LCG advance + freeze on hit. */
        if (jrand_int_bound(&s->rand, 16) == 0) {
            i32 j2 = wt_lcg_advance(&s->updateLCG);
            int px = cbx + (j2 & 15);
            int pz = cbz + ((j2 >> 8) & 15);
            int ph = wt_precipitation_height(w, px, pz);
            if (wt_can_block_freeze_no_water(w, px, ph - 1, pz))
                wt_set(w, px, ph - 1, pz, mc_state(BLK_ICE, 0));
            /* canSnowAt + fillWithRain are gated on isRaining -> skipped (P4). */
        }
        /* randomTick loop: per NON-EMPTY section that needsRandomTick (vanilla keeps a
         * per-section flag maintained on set; here a scan of the ported predicate). */
        if (rts > 0) {
            for (sec = 0; sec < MC_NSEC; ++sec) {
                const Chunk *c = &w->chunk[ci];
                int needs = 0, i1, x, y, z;
                for (y = 0; y < MC_SEC && !needs; ++y)
                    for (z = 0; z < MC_CZ && !needs; ++z)
                        for (x = 0; x < MC_CX && !needs; ++x)
                            if (wt_ticks_randomly(mc_state_id(mc_get(c, x, sec * MC_SEC + y, z))))
                                needs = 1;
                if (!needs) continue;
                for (i1 = 0; i1 < rts; ++i1) {
                    i32 j1 = wt_lcg_advance(&s->updateLCG);
                    int k1 = j1 & 15;              /* x */
                    int l1 = (j1 >> 8) & 15;       /* z */
                    int i2 = (j1 >> 16) & 15;      /* y within section */
                    int id = mc_state_id(mc_get(&w->chunk[ci], k1, sec * MC_SEC + i2, l1));
                    if (wt_ticks_randomly(id))
                        wt_random_tick(w, s, gr, cbx + k1, sec * MC_SEC + i2, cbz + l1, id);
                }
            }
        }
    }
}

/* ---- init + the canonical tick ---- */

MC_HD static inline void wt_vanilla_init(WtvState *s, u64 seed) {
    stq_init(&s->stq);
    jrand_set(&s->rand, (i64)seed);
    s->updateLCG = jrand_int(&s->rand);            /* World: `updateLCG = rand.nextInt()` */
    s->totalWorldTime = 0;
    s->worldTime = 0;
    s->skylightSubtracted = 0;
    s->spawnHostileMobs = 1;
    s->spawnPeacefulMobs = 1;
    s->fired_n = 0;
    s->fall_n = 0;
    { int i; for (i = 0; i < WT_FALL_MAX; ++i) s->fall[i].active = 0; }
}

/* WorldServer.tick canonical order. `spawner` is the standalone msw harness scene
 * (nullable); until P2 wires WorldEntitySpawner over World blocks it spawns into its
 * own scene, keeping phase-1 ordering + gating exercisable. */
MC_HD MC_NOINLINE static void wt_vanilla_tick(World *w, WtvState *s, const McGameRules *gr,
                                              const McSinTable *st, MswScene *spawner) {
    /* 0. super.tick(): weather timers (doWeatherCycle), hardcore difficulty clamp,
     *    biome cache cleanup, all-players-asleep skip - no-op stubs (P4/P2). */

    /* 1. mobSpawner: findChunksForSpawning(hostile, peaceful, totalTime%400==0). */
    if (gr->doMobSpawning && spawner && s->spawnHostileMobs)
        msw_run(spawner, s->totalWorldTime, (u8 *)0, (u8 *)0);

    /* 2. chunkProvider.tick(): no-op stub (fixed region: no unload queue). */

    /* 3. skylightSubtracted. */
    {
        i32 j = wt_calc_skylight_subtracted(st, s->worldTime);
        if (j != s->skylightSubtracted) s->skylightSubtracted = j;
    }

    /* 4. totalWorldTime++; worldTime++ under doDaylightCycle. */
    s->totalWorldTime += 1;
    if (gr->doDaylightCycle) s->worldTime += 1;

    /* 5. tickUpdates(false). */
    wt_tick_updates(w, s, gr, 0);

    /* 6. updateBlocks (weather draws + random ticks). */
    wt_update_blocks(w, s, gr);

    /* 7. playerChunkMap.tick / village / villageSiege / portalForcer stale-portal
     *    sweep: no-op stubs (villages P5; portal timeout list not yet runtime state). */

    /* 8. sendQueuedBlockEvents(): no-op stub (no block-event producers ported). */

    w->tick = s->totalWorldTime;
}

/* World.updateEntities order, run SEPARATELY by the server loop after world.tick():
 *   1. weatherEffects (lightning bolts)   2. unloaded-entity cleanup
 *   3. tickPlayers                        4. loadedEntityList: updateEntity(onUpdate)
 *   5. dead-entity removal                6. tickable tile entities
 * Only the EntityFallingBlock pool is live (P1 falling trace); the rest of the P2
 * entity spine remains stubbed. Pool order == spawn order == loadedEntityList order. */
MC_HD static inline void wt_vanilla_update_entities(World *w, WtvState *s) {
    int i;
    for (i = 0; i < s->fall_n; ++i)
        if (s->fall[i].active) wt_fall_onupdate(w, s, &s->fall[i]);
    while (s->fall_n > 0 && !s->fall[s->fall_n - 1].active) --s->fall_n;
}

#endif /* MC_WORLD_TICK_VANILLA_H */
