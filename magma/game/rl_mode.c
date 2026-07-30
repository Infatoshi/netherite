/* RL step mode (--rl): line protocol over stdio, no rendering.
 *
 * On start (after runtime init) the process emits the tick-0 obs line, then
 * loops: read ONE action JSON line from stdin, run ONE gm_runtime_tick, emit
 * ONE obs JSON line. EOF on stdin (or --ticks exhausted) ends the episode.
 *
 * Action keys (all optional, default 0): forward, strafe, dyaw, dpitch,
 * jump, sneak, sprint, attack, use. dyaw/dpitch are DELTA DEGREES this tick
 * (GmAction semantics).
 *
 * Obs line - ALL FIELDS ARE STATIC SHAPES (fixed lengths, zero padded) so a
 * learner can batch without ragged handling:
 *   {"t":N,"x":..,"y":..,"z":..,"yaw":..,"pitch":..,"dead":0,
 *    "blocks":[[id,wx,wy,wz] x RL_NBLOCKS],   nearest-first, [0,0,0,0] pad
 *    "logs":[[wx,wy,wz] x RL_NLOGS],          nearest log blocks (id 17);
 *                                             blocks may TRUNCATE logs away
 *                                             (surface blocks are closer), so
 *                                             reward code reads this list
 *    "cam":[id x 64*36],                      semantic camera, row-major,
 *    "depth":[d x 64*36],                     u8: dist*4 clamped, 255 = sky
 *    "edge":[e x 64*36]}                      1 = hit point within 0.05 of
 *                                             the struck face's block border
 *                                             (world-space width, so edges
 *                                             thin with distance)
 *
 * blocks is the oracle-side sparse summary (per-column topmost non-air within
 * RL_OBS_R plus every log even under leaves, truncated to the RL_NBLOCKS
 * nearest). cam/depth is the visibility-honest obs: a DDA voxel raycast per
 * pixel from the eye through a 70-degree pinhole at the player's yaw/pitch -
 * the agent only "sees" what a camera would (occlusion included), at a
 * fixed 64x36. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "game/config.h"
#include "game/game.h"
#include "game/runtime.h"
#include "game/rl_mode.h"
#include "game/frame_capture.h"
#include "game/hud.h"
#include "obs_camera.h"   /* blaze verified semantic camera (LUT trig) */

#define RL_OBS_R    16   /* horizontal obs radius, blocks */
#define RL_Y_DOWN   24   /* scan band below player feet   */
#define RL_Y_UP     40   /* scan band above player feet   */
#define RL_BLOCK_LOG 17
#define RL_BLOCK_COAL 16 /* coal ore */
#define RL_NBLOCKS  256  /* fixed blocks-list length      */
#define RL_NLOGS    64   /* fixed logs-list length        */
#define RL_NCOAL    32   /* fixed coal-ore-list length    */

/* Item ids the inv_counts obs tracks (and the craft table consumes):
 * log, planks, stick, cobblestone, crafting table, wooden pick, stone
 * pick, coal, torch. */
static const int rl_inv_ids[9] = {17, 5, 280, 4, 58, 270, 274, 263, 50};

/* Discrete craft primitives ("craft":N). Grid cells are gm_runtime_craft
 * layout (3x3 row-major; 2x2 uses cells 0,1,3,4); value = required item id,
 * 0 = empty. 3x3 recipes need an OPEN crafting table (r->container == 1,
 * via "interact":1 near a placed table). */
typedef struct { int width; int cell[9]; } RlCraft;
static const RlCraft rl_crafts[] = {
    /* 0 planks  */ {2, {17, 0, 0, 0, 0, 0, 0, 0, 0}},
    /* 1 sticks  */ {2, {5, 0, 0, 5, 0, 0, 0, 0, 0}},
    /* 2 table   */ {2, {5, 5, 0, 5, 5, 0, 0, 0, 0}},
    /* 3 w.pick  */ {3, {5, 5, 5, 0, 280, 0, 0, 280, 0}},
    /* 4 s.pick  */ {3, {4, 4, 4, 0, 280, 0, 0, 280, 0}},
    /* 5 torch   */ {2, {263, 0, 0, 280, 0, 0, 0, 0, 0}},
    /* 6 furnace */ {3, {4, 4, 4, 4, 0, 4, 4, 4, 4}},
    /* 7 i.pick  */ {3, {265, 265, 265, 0, 280, 0, 0, 280, 0}},
};
#define RL_NCRAFTS (int)(sizeof rl_crafts / sizeof rl_crafts[0])

#define RL_CAM_W    64   /* == OC_W; FOV 70 deg and 48-block reach now come */
#define RL_CAM_H    36   /* from obs_camera.h (OC_TANY / OC_FAR)            */

typedef struct { int id, x, y, z; double d2; } RlBlock;

/* --- step-time profile (stderr summary at EOF) --- */
static double rl_prof[4];   /* tick, scan+sort, camera, serialize (s) */
static long long rl_prof_n;

static double rl_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static double rl_num(const char *line, const char *key, double dflt) {
    char pat[40];
    const char *p;
    snprintf(pat, sizeof pat, "\"%s\"", key);
    p = strstr(line, pat);
    if (!p) return dflt;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') ++p;
    return strtod(p, NULL);
}

/* TOTAL order (d2, then x/y/z): with a plain d2 compare, tie order depended
 * on the input permutation, i.e. on scan-cache rebuild HISTORY (the cache is
 * sorted in place every emit) - the same (world, pose) could emit two block
 * orders. A total order makes the emitted lists a pure function of world and
 * pose, which snapshot restore (--snapshot-in) relies on byte-for-byte. */
static int rl_block_lt(const RlBlock *a, const RlBlock *b) {
    if (a->d2 != b->d2) return a->d2 < b->d2;
    if (a->x != b->x) return a->x < b->x;
    if (a->y != b->y) return a->y < b->y;
    return a->z < b->z;
}

static int rl_block_cmp(const void *a_, const void *b_) {
    const RlBlock *a = (const RlBlock *)a_, *b = (const RlBlock *)b_;
    return rl_block_lt(a, b) ? -1 : rl_block_lt(b, a) ? 1 : 0;
}

/* Quickselect: partition a[0..n) so the k smallest by d2 sit in a[0..k).
 * A full qsort of the ~3k cached entries every tick cost ~250us; only the
 * nearest RL_NBLOCKS ever leave the process, so select first, sort the
 * survivors. Median-of-three pivot keeps the sorted-ish steady state (the
 * cache barely changes tick to tick) off the quadratic path. */
static void rl_select_k(RlBlock *a, int n, int k) {
    int lo = 0, hi = n - 1;
    if (k >= n) return;
    while (hi - lo > 8) {
        int mid = lo + (hi - lo) / 2, i, j;
        RlBlock tmp, piv;
        if (rl_block_lt(&a[mid], &a[lo])) { tmp = a[lo]; a[lo] = a[mid]; a[mid] = tmp; }
        if (rl_block_lt(&a[hi], &a[lo]))  { tmp = a[lo]; a[lo] = a[hi];  a[hi] = tmp; }
        if (rl_block_lt(&a[hi], &a[mid])) { tmp = a[mid]; a[mid] = a[hi]; a[hi] = tmp; }
        piv = a[mid];
        tmp = a[mid]; a[mid] = a[hi - 1]; a[hi - 1] = tmp;
        i = lo; j = hi - 1;
        for (;;) {
            while (rl_block_lt(&a[++i], &piv)) {}
            while (rl_block_lt(&piv, &a[--j])) {}
            if (i >= j) break;
            tmp = a[i]; a[i] = a[j]; a[j] = tmp;
        }
        tmp = a[i]; a[i] = a[hi - 1]; a[hi - 1] = tmp;
        if (i == k) return;
        if (i > k)  hi = i - 1;
        else        lo = i + 1;
    }
    qsort(a + lo, (size_t)(hi - lo + 1), sizeof a[0], rl_block_cmp);
}

/* ---- camera world view: dense block-ID region fed to oc_pixel ----
 * The camera is the blaze verified obs_camera.h source (LUT trig, DDA), so
 * the real env and the batched blaze are bit-identical. oc_pixel reads an
 * OcRegion dense tensor; this cache mirrors the live world over the full ray
 * reach (OC_FAR 48 -> voxels up to 49 from the eye) with a 16-block origin
 * quantum so walking does not refill every block crossing. Cells hold PLAIN
 * BLOCK IDS (cam records ids, not packed states). Refilled whenever the
 * quantized eye cell moves or any world mutation bumps gm_world_block_gen,
 * so oc_pixel always sees the live world. Rays can never leave the region
 * (49 + 15 quantum + margin <= RL_CAMREG_N/2 span), preserving the previous
 * camera's behavior apart from the libm->LUT trig switch. */
#define RL_CAMREG_N 114  /* 49 reach + 49 reach + 16 origin quantum */
static u16 rl_camreg[RL_CAMREG_N * RL_CAMREG_N * RL_CAMREG_N];
static int rl_camreg_valid;
static int rl_camreg_x0, rl_camreg_y0, rl_camreg_z0;
static long long rl_camreg_gen;

static void rl_camreg_refresh(const GmRuntime *r, double ex, double ey,
                              double ez, OcRegion *reg) {
    int qx = psv_floordiv16(mc_floor(ex)) * 16;
    int qy = psv_floordiv16(mc_floor(ey)) * 16;
    int qz = psv_floordiv16(mc_floor(ez)) * 16;
    int x0 = qx - 49, y0 = qy - 49, z0 = qz - 49;
    long long g = gm_world_block_gen(r->world);
    if (!rl_camreg_valid || x0 != rl_camreg_x0 || y0 != rl_camreg_y0 ||
        z0 != rl_camreg_z0 || g != rl_camreg_gen) {
        int ix, iy, iz;
        for (ix = 0; ix < RL_CAMREG_N; ++ix) {
            int wx = x0 + ix;
            for (iy = 0; iy < RL_CAMREG_N; ++iy) {
                int wy = y0 + iy;
                u16 *row = &rl_camreg[((long)ix * RL_CAMREG_N + iy) *
                                      RL_CAMREG_N];
                if (wy < 0 || wy > 255) {
                    memset(row, 0, RL_CAMREG_N * sizeof *row);
                    continue;
                }
                for (iz = 0; iz < RL_CAMREG_N; ++iz)
                    row[iz] = (u16)gm_world_block(r->world, wx, wy, z0 + iz);
            }
        }
        rl_camreg_x0 = x0; rl_camreg_y0 = y0; rl_camreg_z0 = z0;
        rl_camreg_gen = g;
        rl_camreg_valid = 1;
    }
    reg->cells = rl_camreg;
    reg->x0 = rl_camreg_x0; reg->y0 = rl_camreg_y0; reg->z0 = rl_camreg_z0;
    reg->nx = RL_CAMREG_N; reg->ny = RL_CAMREG_N; reg->nz = RL_CAMREG_N;
}

/* Scan cache (filled in rl_emit_obs, also searched by rl_do_interact). */
static RlBlock rl_cache[(2 * RL_OBS_R + 1) * (2 * RL_OBS_R + 1) * 8];
static int rl_cache_n = -1;                /* -1 = invalid */
static int rl_cache_px, rl_cache_py, rl_cache_pz;
static int rl_world_dirty = 1;
static RlBlock rl_logs[RL_NLOGS];          /* nearest logs, sorted */
static int rl_nlog;
static RlBlock rl_coal[RL_NCOAL];          /* nearest coal ore, sorted */
static int rl_ncoal;

/* Total count of item id across the main inventory. */
static int rl_inv_count(const GmRuntime *r, int item) {
    int i, n = 0;
    for (i = 0; i < ISR_MAIN_SLOTS; ++i) {
        ICStack s = isr_get_stack(&r->player.inv, i);
        if (!isr_is_empty(&s) && s.item == item) n += s.count;
    }
    return n;
}

/* Execute a discrete craft primitive: for each required item id find ONE
 * inventory slot holding enough of it (stacks merge on pickup, so split
 * stacks are rare) and feed that slot to gm_runtime_craft for every grid
 * cell wanting the id. Returns 1 on success. */
static int rl_do_craft(GmRuntime *r, int which) {
    const RlCraft *c;
    int slots[9], need_id[9], need_n[9], nids = 0, i, j;
    if (which < 0 || which >= RL_NCRAFTS) return 0;
    c = &rl_crafts[which];
    for (i = 0; i < 9; ++i) slots[i] = -1;
    for (i = 0; i < 9; ++i) {
        if (!c->cell[i]) continue;
        for (j = 0; j < nids && need_id[j] != c->cell[i]; ++j) {}
        if (j == nids) { need_id[nids] = c->cell[i]; need_n[nids] = 0; ++nids; }
        ++need_n[j];
    }
    for (j = 0; j < nids; ++j) {
        int slot = -1;
        for (i = 0; i < ISR_MAIN_SLOTS; ++i) {
            ICStack s = isr_get_stack(&r->player.inv, i);
            if (!isr_is_empty(&s) && s.item == need_id[j] &&
                s.count >= need_n[j]) { slot = i; break; }
        }
        if (slot < 0) return 0;
        for (i = 0; i < 9; ++i)
            if (c->cell[i] == need_id[j]) slots[i] = slot;
    }
    return gm_runtime_craft(r, c->width, slots);
}

/* "interact":1 - use the nearest crafting table (or furnace) within reach,
 * scanning the cached block list. Opens the 3x3 grid (r->container = 1). */
static int rl_do_interact(GmRuntime *r) {
    int i, best = -1;
    double bd = 36.0;  /* gm_runtime_use_block reach check, squared */
    for (i = 0; i < rl_cache_n; ++i) {
        if (rl_cache[i].id != 58 && rl_cache[i].id != 61 &&
            rl_cache[i].id != 62) continue;
        if (rl_cache[i].d2 < bd) { bd = rl_cache[i].d2; best = i; }
    }
    if (best < 0) return 0;
    return gm_runtime_use_block(r, rl_cache[best].x, rl_cache[best].y,
                                rl_cache[best].z);
}

/* "smelt":1 - operate the OPEN furnace (container == 2, via "interact":1
 * next to a placed furnace): pull the whole output slot into the inventory,
 * top the input slot up with iron ore (first inventory slot holding any),
 * and when the furnace fuel slot is empty feed it ONE coal (1600 burn ticks
 * = 8 smelts; the primitive stays coal-frugal so torches are not starved).
 * Deterministic, silent failure - the obs inv_counts/hotbar tell the learner
 * what moved. Returns 1 when anything moved. */
static int rl_do_smelt(GmRuntime *r) {
    int did = 0, i;
    if (!r || r->container != 2 || r->active_furnace < 0) return 0;
    if (gm_runtime_furnace_extract(r, FURNACE_LIVE_SLOT_OUTPUT, 64) > 0)
        did = 1;
    for (i = 0; i < ISR_MAIN_SLOTS; ++i) {
        ICStack s = isr_get_stack(&r->player.inv, i);
        if (isr_is_empty(&s) || s.item != 15) continue;
        if (gm_runtime_furnace_insert(r, FURNACE_LIVE_SLOT_INPUT, i,
                                      s.count) > 0)
            did = 1;
        break;
    }
    if (sr_isEmpty(r->furnaces[r->active_furnace].state.fuel)) {
        for (i = 0; i < ISR_MAIN_SLOTS; ++i) {
            ICStack s = isr_get_stack(&r->player.inv, i);
            if (isr_is_empty(&s) || s.item != 263) continue;
            if (gm_runtime_furnace_insert(r, FURNACE_LIVE_SLOT_FUEL, i, 1) > 0)
                did = 1;
            break;
        }
    }
    return did;
}

/* ---- binary obs record (--rl-bin): one packed struct per step ---- */
#define RL_BIN_MAGIC 0x524c4f42u  /* "BOLR" little-endian */
#pragma pack(push, 1)
typedef struct {
    unsigned magic;
    long long tick;
    double x, y, z;
    float yaw, pitch;
    int dead;
    int hotbar_ids[9];
    int hotbar_counts[9];
    int hotbar_sel;
    int container;                         /* 0 none/2x2, 1 table, 2 furnace */
    int inv_counts[9];                     /* totals for rl_inv_ids order    */
    int blocks[RL_NBLOCKS][4];             /* id,wx,wy,wz nearest-first */
    int logs[RL_NLOGS][3];                 /* wx,wy,wz nearest logs     */
    int coal[RL_NCOAL][3];                 /* wx,wy,wz nearest coal ore */
    unsigned short cam[RL_CAM_W * RL_CAM_H];
    unsigned char depth[RL_CAM_W * RL_CAM_H];
    unsigned char edge[RL_CAM_W * RL_CAM_H];
} RlBinObs;
#pragma pack(pop)

/* ---- .bsnp state snapshot (export: "snapshot":"<path>" action key;
 * restore: --snapshot-in). RlSnapHead/RlSnapItem + the file layout live in
 * blaze/env/blaze_snapshot.h, SHARED with the batched-env reader - this file
 * remains the canonical writer. NOT captured (see gm_player_ctl_dig_export
 * note + report): furnace states, craft grid, cursor stack, world clock,
 * eat/bow/fov statics, break/place/swing counters, projectiles, mobs. */
#include "../../blaze/env/blaze_snapshot.h"

/* radius = horizontal half-extent of the region in blocks ("snapshot_r"
 * action key, default 32 = the original 64x128x64 region). Everything the
 * camera can see from any in-episode pose must be inside the region (rays
 * reach 49 blocks), so full-chain snapshots use a larger radius. */
static int rl_snapshot_write(GmRuntime *r, const char *path, int radius) {
    RlSnapHead h;
    GmPlayerCtlSnap d;
    RlSnapItem items[GM_LIVE_MAX];
    u16 *cells;
    FILE *f;
    unsigned ncoal = 0;
    int i, x, y, z, ok = 1;

    memset(&h, 0, sizeof h);
    memcpy(h.magic, "BSNP", 4);
    h.version = 1;
    h.seed = r->seed; h.tick = r->tick;
    h.ox = r->ox; h.oz = r->oz;
    h.px = r->player.ent.posX; h.py = r->player.ent.posY;
    h.pz = r->player.ent.posZ;
    h.box[0] = r->player.ent.box.minX; h.box[1] = r->player.ent.box.minY;
    h.box[2] = r->player.ent.box.minZ; h.box[3] = r->player.ent.box.maxX;
    h.box[4] = r->player.ent.box.maxY; h.box[5] = r->player.ent.box.maxZ;
    h.yaw = r->player.yaw; h.pitch = r->player.pitch;
    h.mx = r->player.ent.motionX; h.my = r->player.ent.motionY;
    h.mz = r->player.ent.motionZ;
    h.on_ground = r->player.ent.onGround;
    h.collided_h = r->player.ent.collidedHorizontally;
    h.collided_v = r->player.ent.collidedVertically;
    h.is_collided = r->player.ent.isCollided;
    h.fall_distance = r->player.fall_distance;
    h.sprinting = r->player.sprinting;
    h.sprint_toggle_timer = r->player.sprint_toggle_timer;
    h.jump_factor_sprint = r->player.jump_factor_sprint;
    h.jump_ticks = r->player.jump_ticks;
    h.prev_move_forward = r->player.prev_move_forward;
    h.prev_sneak = r->player.prev_sneak;
    h.health = r->vitals.health; h.food = r->vitals.foodLevel;
    h.saturation = r->vitals.saturation; h.exhaustion = r->vitals.exhaustion;
    h.food_timer = r->vitals.foodTimer;
    gm_player_ctl_dig_export(&d);
    h.dig_progress = d.dig_progress;
    h.dig_hx = d.dig_hx; h.dig_hy = d.dig_hy; h.dig_hz = d.dig_hz;
    h.dig_hitting = d.dig_hitting; h.dig_delay = d.dig_delay;
    h.atk_prev = d.atk_prev; h.rc_delay = d.rc_delay;
    h.use_prev = d.use_prev; h.hurt_vel_reset = d.hurt_vel_reset;
    h.server_motion_x = d.server_motion_x;
    h.server_motion_z = d.server_motion_z;
    h.container = r->container;
    h.container_wx = r->container_wx; h.container_wy = r->container_wy;
    h.container_wz = r->container_wz;
    h.world_dirty = rl_world_dirty;
    h.hotbar_sel = r->player.inv.current_item;
    for (i = 0; i < 37; ++i) {
        ICStack s = isr_get_stack(&r->player.inv,
                                  i < 36 ? i : ISR_OFFHAND_SLOT);
        h.inv[i][0] = s.item; h.inv[i][1] = s.count; h.inv[i][2] = s.meta;
    }
    h.n_items = 0;
    for (i = 0; i < GM_LIVE_MAX; ++i) {
        const GmLiveEnt *e = &r->entities.ents[i];
        RlSnapItem *it = &items[h.n_items];
        if (!e->active || e->type != 0) continue;
        it->x = e->x; it->y = e->y; it->z = e->z;
        it->mx = e->mx; it->my = e->my; it->mz = e->mz;
        it->item = e->item; it->count = e->count; it->meta = e->meta;
        it->age = e->age; it->pickup_delay = e->pickup_delay;
        it->lifespan = e->lifespan; it->on_ground = e->on_ground;
        ++h.n_items;
    }
    if (radius < 8) radius = 8;
    if (radius > 128) radius = 128;
    h.rx0 = (int)floor(h.px + (double)h.ox) - radius;
    h.rz0 = (int)floor(h.pz + (double)h.oz) - radius;
    h.ry0 = 0; h.rnx = 2 * radius; h.rny = 128; h.rnz = 2 * radius;

    gm_world_ensure(r->world, psv_floordiv16(h.rx0 + radius),
                    psv_floordiv16(h.rz0 + radius), (radius + 15) / 16 + 1);
    cells = (u16 *)malloc((size_t)h.rnx * h.rny * h.rnz * sizeof *cells);
    if (!cells) return 0;
    for (x = 0; x < h.rnx; ++x)
        for (y = 0; y < h.rny; ++y)
            for (z = 0; z < h.rnz; ++z) {
                int id = gm_world_block(r->world, h.rx0 + x, h.ry0 + y,
                                        h.rz0 + z);
                int meta = gm_world_meta(r->world, h.rx0 + x, h.ry0 + y,
                                         h.rz0 + z);
                cells[((long)x * h.rny + y) * h.rnz + z] = mc_state(id, meta);
                if (id == RL_BLOCK_COAL) ++ncoal;
            }

    f = fopen(path, "wb");
    if (!f) { free(cells); return 0; }
    ok = ok && fwrite(&h, sizeof h, 1, f) == 1;
    ok = ok && (h.n_items == 0 ||
                fwrite(items, sizeof items[0], h.n_items, f) == h.n_items);
    ok = ok && fwrite(cells, sizeof *cells,
                      (size_t)h.rnx * h.rny * h.rnz, f) ==
                   (size_t)h.rnx * h.rny * h.rnz;
    ok = ok && fwrite(&ncoal, sizeof ncoal, 1, f) == 1;
    for (x = 0; x < h.rnx && ok; ++x)
        for (y = 0; y < h.rny && ok; ++y)
            for (z = 0; z < h.rnz && ok; ++z)
                if (mc_state_id(cells[((long)x * h.rny + y) * h.rnz + z]) ==
                    RL_BLOCK_COAL) {
                    int c[3];
                    c[0] = h.rx0 + x; c[1] = h.ry0 + y; c[2] = h.rz0 + z;
                    ok = fwrite(c, sizeof c, 1, f) == 1;
                }
    free(cells);
    if (fclose(f) != 0) ok = 0;
    fprintf(stderr, "[rl] snapshot %s: %s (tick %lld, %u items, %u coal)\n",
            ok ? "written" : "WRITE FAILED", path, h.tick, h.n_items, ncoal);
    return ok;
}

static int rl_snapshot_load(GmRuntime *r, const char *path,
                            char *err, int err_cap) {
    RlSnapHead h;
    GmPlayerCtlSnap d;
    u16 *cells = NULL;
    FILE *f = fopen(path, "rb");
    int i, x, y, z;

    if (!f) { snprintf(err, (size_t)err_cap, "cannot open %s", path); return 0; }
    if (fread(&h, sizeof h, 1, f) != 1 || memcmp(h.magic, "BSNP", 4) != 0 ||
        h.version != 1) {
        snprintf(err, (size_t)err_cap, "bad .bsnp header: %s", path);
        fclose(f); return 0;
    }
    if (h.seed != r->seed) {
        snprintf(err, (size_t)err_cap,
                 "snapshot seed %lld != --seed %lld", h.seed, r->seed);
        fclose(f); return 0;
    }
    if (h.n_items > GM_LIVE_MAX || h.rnx <= 0 || h.rny <= 0 || h.rnz <= 0 ||
        (long)h.rnx * h.rny * h.rnz > (long)1 << 24) {
        snprintf(err, (size_t)err_cap, "implausible .bsnp counts: %s", path);
        fclose(f); return 0;
    }
    memset(&r->entities, 0, sizeof r->entities);
    for (i = 0; i < (int)h.n_items; ++i) {
        RlSnapItem it;
        GmLiveEnt *e = &r->entities.ents[i];
        if (fread(&it, sizeof it, 1, f) != 1) {
            snprintf(err, (size_t)err_cap, "truncated .bsnp items: %s", path);
            fclose(f); return 0;
        }
        e->active = 1; e->type = 0;
        e->x = it.x; e->y = it.y; e->z = it.z;
        e->mx = it.mx; e->my = it.my; e->mz = it.mz;
        e->item = it.item; e->count = it.count; e->meta = it.meta;
        e->age = it.age; e->pickup_delay = it.pickup_delay;
        e->lifespan = it.lifespan; e->on_ground = it.on_ground;
        r->entities.n_active++;
    }
    cells = (u16 *)malloc((size_t)h.rnx * h.rny * h.rnz * sizeof *cells);
    if (!cells || fread(cells, sizeof *cells,
                        (size_t)h.rnx * h.rny * h.rnz, f) !=
                      (size_t)h.rnx * h.rny * h.rnz) {
        snprintf(err, (size_t)err_cap, "truncated .bsnp region: %s", path);
        free(cells); fclose(f); return 0;
    }
    fclose(f);  /* trailing coal list is a derivable mirror; not needed */

    gm_world_ensure(r->world, psv_floordiv16(h.rx0 + h.rnx / 2),
                    psv_floordiv16(h.rz0 + h.rnz / 2), (h.rnx / 2 + 15) / 16 + 1);
    for (x = 0; x < h.rnx; ++x)
        for (y = 0; y < h.rny; ++y)
            for (z = 0; z < h.rnz; ++z) {
                u16 s = cells[((long)x * h.rny + y) * h.rnz + z];
                gm_runtime_load_block(r, h.rx0 + x, h.ry0 + y, h.rz0 + z,
                                      mc_state_id(s), mc_state_meta(s));
            }
    free(cells);

    r->ccx = psv_floordiv16(h.ox); r->ccz = psv_floordiv16(h.oz);
    r->ox = h.ox; r->oz = h.oz;
    r->player.ent.posX = h.px; r->player.ent.posY = h.py;
    r->player.ent.posZ = h.pz;
    r->player.ent.box.minX = h.box[0]; r->player.ent.box.minY = h.box[1];
    r->player.ent.box.minZ = h.box[2]; r->player.ent.box.maxX = h.box[3];
    r->player.ent.box.maxY = h.box[4]; r->player.ent.box.maxZ = h.box[5];
    r->player.yaw = h.yaw; r->player.pitch = h.pitch;
    r->player.ent.motionX = h.mx; r->player.ent.motionY = h.my;
    r->player.ent.motionZ = h.mz;
    r->player.ent.onGround = h.on_ground;
    r->player.ent.collidedHorizontally = h.collided_h;
    r->player.ent.collidedVertically = h.collided_v;
    r->player.ent.isCollided = h.is_collided;
    r->player.fall_distance = h.fall_distance;
    r->player.sprinting = h.sprinting;
    r->player.sprint_toggle_timer = h.sprint_toggle_timer;
    r->player.jump_factor_sprint = h.jump_factor_sprint;
    r->player.jump_ticks = h.jump_ticks;
    r->player.prev_move_forward = h.prev_move_forward;
    r->player.prev_sneak = h.prev_sneak;
    r->vitals.health = h.health; r->vitals.foodLevel = h.food;
    r->vitals.saturation = h.saturation; r->vitals.exhaustion = h.exhaustion;
    r->vitals.foodTimer = h.food_timer;
    r->player.health = h.health; r->player.food = (float)h.food;
    d.dig_progress = h.dig_progress;
    d.dig_hx = h.dig_hx; d.dig_hy = h.dig_hy; d.dig_hz = h.dig_hz;
    d.dig_hitting = h.dig_hitting; d.dig_delay = h.dig_delay;
    d.atk_prev = h.atk_prev; d.rc_delay = h.rc_delay;
    d.use_prev = h.use_prev; d.hurt_vel_reset = h.hurt_vel_reset;
    d.server_motion_x = h.server_motion_x;
    d.server_motion_z = h.server_motion_z;
    gm_player_ctl_dig_import(&d);
    r->container = h.container;
    r->container_wx = h.container_wx; r->container_wy = h.container_wy;
    r->container_wz = h.container_wz;
    rl_world_dirty = h.world_dirty;
    r->player.inv.current_item = h.hotbar_sel;
    for (i = 0; i < 37; ++i)
        isr_set_stack(&r->player.inv, i < 36 ? i : ISR_OFFHAND_SLOT,
                      h.inv[i][1] == 0 ? ic_empty()
                                       : ic_mk(h.inv[i][0], h.inv[i][1],
                                               h.inv[i][2]));
    r->tick = h.tick;
    return 1;
}

/* Value of a quoted-string action key ("snapshot":"/path"); 1 if present. */
static int rl_str(const char *line, const char *key, char *out, int cap) {
    char pat[40];
    const char *p;
    int n = 0;
    snprintf(pat, sizeof pat, "\"%s\"", key);
    p = strstr(line, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') ++p;
    if (*p != '"') return 0;
    ++p;
    while (*p && *p != '"' && n < cap - 1) out[n++] = *p++;
    out[n] = 0;
    return *p == '"' && n > 0;
}

/* Scan cache note: block-list MEMBERSHIP depends only on world contents and
 * the player's block coordinate (the scan window), so it is reused until the
 * player crosses a block boundary or the world may have changed (attack/use
 * held - the only mutation sources with mobs/fluid-CA off). Distances are
 * recomputed from the exact position and re-sorted every step, so emitted
 * obs are identical to an uncached scan. */
/* want_cam=0 skips the semantic-camera raycast (the single largest obs cost,
 * ~190us/step) and re-emits the previous frame's cam/depth/edge buffers.
 * Action-repeat trainers only consume the camera once per decision, so they
 * send "cam":0 on repeat ticks; scan/logs/coal/inventory stay per-tick
 * fresh. Record layout is unchanged - parsers need no update. */
static void rl_emit_obs(GmRuntime *r, int bin, int want_cam) {
    GmPlayerView v;
    RlBlock *blk = rl_cache;
    int nblk;
    int pwx, pwy, pwz, i;
    static unsigned short cam[RL_CAM_W * RL_CAM_H];
    static unsigned char dep[RL_CAM_W * RL_CAM_H];
    static unsigned char edg[RL_CAM_W * RL_CAM_H];

    double tp = rl_now();

    gm_runtime_view(r, &v);
    pwx = (int)floor((double)v.x);
    pwy = (int)floor((double)v.y);
    pwz = (int)floor((double)v.z);

    if (rl_cache_n < 0 || rl_world_dirty > 0 ||
        pwx != rl_cache_px || pwy != rl_cache_py || pwz != rl_cache_pz) {
        int y0 = pwy - RL_Y_DOWN, y1 = pwy + RL_Y_UP, dx, dz;
        if (y0 < 0)   y0 = 0;
        if (y1 > 255) y1 = 255;

        /* render-off runs only generate the physics window; the obs radius
         * needs the full 3-chunk ring (same trap as prof_scan). */
        gm_world_ensure(r->world,
                        (int)floor((double)v.x / 16.0),
                        (int)floor((double)v.z / 16.0), 3);

        nblk = 0;
        for (dx = -RL_OBS_R; dx <= RL_OBS_R; ++dx) {
            for (dz = -RL_OBS_R; dz <= RL_OBS_R; ++dz) {
                int wx = pwx + dx, wz = pwz + dz, y, top_done = 0;
                for (y = y1; y >= y0; --y) {
                    int id = gm_world_block(r->world, wx, y, wz);
                    int always = id == RL_BLOCK_LOG || id == RL_BLOCK_COAL ||
                                 id == 58 || id == 61 || id == 62;
                    if (!id) continue;
                    if ((!top_done || always) &&
                        nblk < (int)(sizeof rl_cache / sizeof rl_cache[0])) {
                        blk[nblk].id = id; blk[nblk].x = wx;
                        blk[nblk].y = y;   blk[nblk].z = wz;
                        ++nblk;
                    }
                    top_done = 1;
                    /* NO early break below the surface: an earlier `y < pwy`
                     * cutoff made the emitted log set depend on player FEET
                     * Y - a jump pruned trunk logs under the canopy for a
                     * tick, which reads as a phantom block-break to reward
                     * code. The full-band scan keeps the set pose-
                     * independent. */
                }
            }
        }
        rl_cache_n = nblk;
        rl_cache_px = pwx; rl_cache_py = pwy; rl_cache_pz = pwz;
        if (rl_world_dirty > 0)
            --rl_world_dirty;
    }
    nblk = rl_cache_n;
    for (i = 0; i < nblk; ++i) {
        double ddx = blk[i].x + 0.5 - v.x, ddy = blk[i].y + 0.5 - v.y,
               ddz = blk[i].z + 0.5 - v.z;
        blk[i].d2 = ddx * ddx + ddy * ddy + ddz * ddz;
    }
    /* logs are all emitted regardless of blocks-list rank, so they must
     * survive the top-256 selection: select into blk[0..RL_NBLOCKS) for the
     * blocks field, but gather logs from the FULL cache first. */
    {
        static RlBlock logblk[512], coalblk[512];
        int nlog = 0, ncoal = 0;
        for (i = 0; i < nblk; ++i) {
            if (blk[i].id == RL_BLOCK_LOG &&
                nlog < (int)(sizeof logblk / sizeof logblk[0]))
                logblk[nlog++] = blk[i];
            if (blk[i].id == RL_BLOCK_COAL &&
                ncoal < (int)(sizeof coalblk / sizeof coalblk[0]))
                coalblk[ncoal++] = blk[i];
        }
        qsort(logblk, (size_t)nlog, sizeof logblk[0], rl_block_cmp);
        rl_nlog = nlog < RL_NLOGS ? nlog : RL_NLOGS;
        memcpy(rl_logs, logblk, (size_t)rl_nlog * sizeof logblk[0]);
        qsort(coalblk, (size_t)ncoal, sizeof coalblk[0], rl_block_cmp);
        rl_ncoal = ncoal < RL_NCOAL ? ncoal : RL_NCOAL;
        memcpy(rl_coal, coalblk, (size_t)rl_ncoal * sizeof coalblk[0]);
    }
    rl_select_k(blk, nblk, RL_NBLOCKS);
    qsort(blk, (size_t)(nblk < RL_NBLOCKS ? nblk : RL_NBLOCKS),
          sizeof blk[0], rl_block_cmp);
    rl_prof[1] += rl_now() - tp; tp = rl_now();

    /* ---- semantic camera: fixed 64x36 id + depth + edge (oc_pixel) ----
     * Eye is the exact double pose (not the float GmPlayerView), matching
     * what the batched blaze computes from its CuPlayer doubles. */
    if (want_cam) {
        double ex = r->player.ent.posX + (double)r->ox;
        double ey = r->player.ent.posY + PSV_EYE_HEIGHT;
        double ez = r->player.ent.posZ + (double)r->oz;
        OcRegion reg;
        int px, py;
        rl_camreg_refresh(r, ex, ey, ez, &reg);
        for (py = 0; py < RL_CAM_H; ++py)
            for (px = 0; px < RL_CAM_W; ++px)
                oc_pixel(&reg, &r->sin_table, ex, ey, ez,
                         r->player.yaw, r->player.pitch, px, py,
                         &cam[py * RL_CAM_W + px], &dep[py * RL_CAM_W + px],
                         &edg[py * RL_CAM_W + px]);
    }
    rl_prof[2] += rl_now() - tp; tp = rl_now();

    if (bin) {
        static RlBinObs o;
        memset(&o, 0, sizeof o);
        o.magic = RL_BIN_MAGIC;
        o.tick = r->tick;
        o.x = (double)v.x; o.y = (double)v.y; o.z = (double)v.z;
        o.yaw = (float)v.yaw; o.pitch = (float)v.pitch;
        o.dead = v.dead;
        for (i = 0; i < 9; ++i) {
            o.hotbar_ids[i] = v.hotbar_ids[i];
            o.hotbar_counts[i] = v.hotbar_counts[i];
            o.inv_counts[i] = rl_inv_count(r, rl_inv_ids[i]);
        }
        o.hotbar_sel = v.hotbar_sel;
        o.container = r->container;
        for (i = 0; i < nblk && i < RL_NBLOCKS; ++i) {
            o.blocks[i][0] = blk[i].id; o.blocks[i][1] = blk[i].x;
            o.blocks[i][2] = blk[i].y;  o.blocks[i][3] = blk[i].z;
        }
        for (i = 0; i < rl_nlog; ++i) {
            o.logs[i][0] = rl_logs[i].x; o.logs[i][1] = rl_logs[i].y;
            o.logs[i][2] = rl_logs[i].z;
        }
        for (i = 0; i < rl_ncoal; ++i) {
            o.coal[i][0] = rl_coal[i].x; o.coal[i][1] = rl_coal[i].y;
            o.coal[i][2] = rl_coal[i].z;
        }
        memcpy(o.cam, cam, sizeof o.cam);
        memcpy(o.depth, dep, sizeof o.depth);
        memcpy(o.edge, edg, sizeof o.edge);
        fwrite(&o, sizeof o, 1, stdout);
    } else {
        printf("{\"t\":%lld,\"x\":%.6f,\"y\":%.6f,\"z\":%.6f,"
               "\"yaw\":%.4f,\"pitch\":%.4f,\"dead\":%d,",
               r->tick, (double)v.x, (double)v.y, (double)v.z,
               (double)v.yaw, (double)v.pitch, v.dead);
        printf("\"hotbar_ids\":[");
        for (i = 0; i < 9; ++i) printf("%s%d", i ? "," : "", v.hotbar_ids[i]);
        printf("],\"hotbar_counts\":[");
        for (i = 0; i < 9; ++i)
            printf("%s%d", i ? "," : "", v.hotbar_counts[i]);
        printf("],\"hotbar_sel\":%d,\"container\":%d,\"inv_counts\":[",
               v.hotbar_sel, r->container);
        for (i = 0; i < 9; ++i)
            printf("%s%d", i ? "," : "", rl_inv_count(r, rl_inv_ids[i]));
        /* full-inventory iron-chain counts (furnace, iron ore, ingot, iron
         * pick) - JSON path only, the binary BOLR layout stays frozen. The
         * hotbar overflows during the iron chain, so hotbar-only visibility
         * cannot confirm pickups. Mirrors the qrl bridge's inv_iron. */
        printf("],\"inv_iron\":[%d,%d,%d,%d",
               rl_inv_count(r, 61), rl_inv_count(r, 15),
               rl_inv_count(r, 265), rl_inv_count(r, 257));
        printf("],\"blocks\":[");
        for (i = 0; i < RL_NBLOCKS; ++i) {
            if (i < nblk)
                printf("%s[%d,%d,%d,%d]", i ? "," : "",
                       blk[i].id, blk[i].x, blk[i].y, blk[i].z);
            else
                printf("%s[0,0,0,0]", i ? "," : "");
        }
        printf("],\"logs\":[");
        for (i = 0; i < RL_NLOGS; ++i) {
            if (i < rl_nlog)
                printf("%s[%d,%d,%d]", i ? "," : "",
                       rl_logs[i].x, rl_logs[i].y, rl_logs[i].z);
            else
                printf("%s[0,0,0]", i ? "," : "");
        }
        printf("],\"coal\":[");
        for (i = 0; i < RL_NCOAL; ++i) {
            if (i < rl_ncoal)
                printf("%s[%d,%d,%d]", i ? "," : "",
                       rl_coal[i].x, rl_coal[i].y, rl_coal[i].z);
            else
                printf("%s[0,0,0]", i ? "," : "");
        }
        printf("],\"cam\":[");
        for (i = 0; i < RL_CAM_W * RL_CAM_H; ++i)
            printf("%s%d", i ? "," : "", (int)cam[i]);
        printf("],\"depth\":[");
        for (i = 0; i < RL_CAM_W * RL_CAM_H; ++i)
            printf("%s%d", i ? "," : "", (int)dep[i]);
        printf("],\"edge\":[");
        for (i = 0; i < RL_CAM_W * RL_CAM_H; ++i)
            printf("%s%d", i ? "," : "", (int)edg[i]);
        printf("]}\n");
    }
    fflush(stdout);
    rl_prof[3] += rl_now() - tp;
    ++rl_prof_n;
}

int gm_rl_run(const GmConfig *cfg) {
    static GmRuntime r;
    char err[256];
    char *line = NULL;
    size_t cap = 0;
    long long t;
    GmFrameCapture *frames = NULL;

    if (!gm_runtime_init(&r, cfg, err, sizeof err)) {
        fprintf(stderr, "runtime: %s\n", err);
        return 1;
    }

    if (cfg->snapshot_in &&
        !rl_snapshot_load(&r, cfg->snapshot_in, err, sizeof err)) {
        fprintf(stderr, "snapshot-in: %s\n", err);
        gm_runtime_destroy(&r);
        return 1;
    }

    /* --frames-out during an RL run: render the real game view alongside the
     * step protocol (obs stay on stdout, PPMs go to the directory). Used to
     * cut videos of exactly the episode the agent played - script-mode replay
     * cannot express the dynamic craft:N / interact:1 primitives. */
    if (cfg->frames_out_dir) {
        gm_hud_init();
        frames = gm_frame_capture_open(cfg, err, sizeof err);
        if (!frames) {
            fprintf(stderr, "frames-out: %s\n", err);
            gm_runtime_destroy(&r);
            return 1;
        }
    }

    rl_emit_obs(&r, cfg->rl_bin, 1);
    for (t = 0; cfg->ticks < 0 || t < cfg->ticks; ++t) {
        GmAction a;
        if (getline(&line, &cap, stdin) < 0) break;
        memset(&a, 0, sizeof a);
        a.hotbar_sel = -1;
        a.forward = (float)rl_num(line, "forward", 0);
        a.strafe  = (float)rl_num(line, "strafe", 0);
        a.dyaw    = (float)rl_num(line, "dyaw", 0);
        a.dpitch  = (float)rl_num(line, "dpitch", 0);
        a.jump    = (int)rl_num(line, "jump", 0);
        a.sneak   = (int)rl_num(line, "sneak", 0);
        a.sprint  = (int)rl_num(line, "sprint", 0);
        a.attack  = (int)rl_num(line, "attack", 0);
        a.use     = (int)rl_num(line, "use", 0);
        a.hotbar_sel = (int)rl_num(line, "hotbar", -1);
        if (a.attack || a.use)
            rl_world_dirty = 40; /* attack/use are the only world-mutation
                                  * triggers here, but gravity blocks keep
                                  * settling after a break - stay dirty for
                                  * 2s of ticks past the last one */
        /* discrete primitives, applied BEFORE the tick so their effect is
         * visible in this step's obs: "craft":N (rl_crafts index; fails
         * silently when inputs/table are missing - the obs inv_counts tell
         * the learner what happened), "interact":1 (open the nearest
         * crafting table / furnace in reach) and "smelt":1 (operate the
         * open furnace: collect output, load iron ore + coal). */
        /* "snapshot":"<path>": dump the exact pre-tick state (runs before
         * craft/interact so the file is the clean tick-boundary state). */
        {
            char snap_path[512];
            if (rl_str(line, "snapshot", snap_path, sizeof snap_path))
                (void)rl_snapshot_write(&r, snap_path,
                                        (int)rl_num(line, "snapshot_r", 32));
        }
        {
            int craft = (int)rl_num(line, "craft", -1);
            if (craft >= 0) (void)rl_do_craft(&r, craft);
            if ((int)rl_num(line, "interact", 0)) (void)rl_do_interact(&r);
            if ((int)rl_num(line, "smelt", 0)) (void)rl_do_smelt(&r);
        }
        {
            double t0 = rl_now();
            gm_runtime_tick(&r, a);
            rl_prof[0] += rl_now() - t0;
        }
        if (frames) {
            int render = t >= cfg->frame_offset &&
                         (t - cfg->frame_offset) % cfg->frame_every == 0;
            if (!gm_frame_capture_write(frames, &r, &a, render,
                                        err, sizeof err)) {
                fprintf(stderr, "frames-out: %s\n", err);
                break;
            }
        }
        rl_emit_obs(&r, cfg->rl_bin, (int)rl_num(line, "cam", 1));
    }
    if (frames) gm_frame_capture_close(frames);

    if (rl_prof_n > 0) {
        double tot = rl_prof[0] + rl_prof[1] + rl_prof[2] + rl_prof[3];
        fprintf(stderr, "[rl prof] %lld obs: tick %.1fus scan %.1fus "
                "cam %.1fus json %.1fus  (%.1fus/step, %.0f steps/s)\n",
                rl_prof_n,
                1e6 * rl_prof[0] / rl_prof_n, 1e6 * rl_prof[1] / rl_prof_n,
                1e6 * rl_prof[2] / rl_prof_n, 1e6 * rl_prof[3] / rl_prof_n,
                1e6 * tot / rl_prof_n, rl_prof_n / tot);
    }
    free(line);
    gm_runtime_destroy(&r);
    return 0;
}
