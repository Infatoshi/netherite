/* game/fluid_live.c - live water/lava flow over GmWorld (see fluid_live.h). */
#include "game/fluid_live.h"

#include "fluid_flow.h"   /* verified blaze CA: ff_ca_step + mc_state encode */

#include <string.h>

/* MC numeric liquid ids (== BLK_* in blaze/core/mc_blocks.h). */
#define FL_FLOWING_WATER 8
#define FL_WATER         9
#define FL_FLOWING_LAVA  10
#define FL_LAVA          11

/* A mark farther than this from a region's AABB starts a NEW region. */
#define FL_JOIN_DIST 20

static int fl_is_liquid(int id) { return id >= FL_FLOWING_WATER && id <= FL_LAVA; }
static int fl_is_water(int id)  { return id == FL_FLOWING_WATER || id == FL_WATER; }

/* Vanilla-displaceable cover blocks (BlockLiquid can flow into these): the CA
 * itself treats every non-air block as flow-blocking, so we present them to the
 * grid as air and let the write-back destroy them only if liquid arrived. */
static int fl_is_displaceable(int id) {
    return id == 31 /* tallgrass */ || id == 32 /* deadbush */ ||
           id == 37 || id == 38     /* flowers  */ ||
           id == 39 || id == 40     /* mushrooms */ ||
           id == 51 /* fire */      || id == 78 /* snow_layer */;
}

void gm_fluid_init(GmFluidLive *f) { memset(f, 0, sizeof *f); f->dim = -99; }

int gm_fluid_active(const GmFluidLive *f) {
    for (int i = 0; i < GM_FLUID_REGIONS; ++i)
        if (f->reg[i].active) return 1;
    return 0;
}

static int fl_dist_to_box(const GmFluidRegion *r, int wx, int wy, int wz) {
    int d = 0, t;
    t = r->x0 - wx; if (t > d) d = t;  t = wx - r->x1; if (t > d) d = t;
    t = r->y0 - wy; if (t > d) d = t;  t = wy - r->y1; if (t > d) d = t;
    t = r->z0 - wz; if (t > d) d = t;  t = wz - r->z1; if (t > d) d = t;
    return d;
}

static void fl_union(GmFluidRegion *r, int wx, int wy, int wz) {
    if (wx < r->x0) r->x0 = wx; if (wx > r->x1) r->x1 = wx;
    if (wy < r->y0) r->y0 = wy; if (wy > r->y1) r->y1 = wy;
    if (wz < r->z0) r->z0 = wz; if (wz > r->z1) r->z1 = wz;
    r->quiet_steps = 0;
}

void gm_fluid_mark(GmFluidLive *f, GmWorld *w, int dim, int wx, int wy, int wz) {
    static const int dx[7] = {0, 1,-1, 0, 0, 0, 0};
    static const int dy[7] = {0, 0, 0, 1,-1, 0, 0};
    static const int dz[7] = {0, 0, 0, 0, 0, 1,-1};
    int found = 0, water = 0;
    for (int i = 0; i < 7 && !water; ++i) {
        int id = gm_world_block(w, wx + dx[i], wy + dy[i], wz + dz[i]);
        if (fl_is_liquid(id)) { found = 1; water |= fl_is_water(id); }
    }
    if (!found) return;
    if (f->dim != dim) gm_fluid_init(f);   /* stale cross-dim regions */
    f->dim = dim;

    /* join the nearest region within FL_JOIN_DIST, else claim a free slot,
     * else fall back to the globally nearest region */
    int best = -1, best_d = 0, free_slot = -1;
    for (int i = 0; i < GM_FLUID_REGIONS; ++i) {
        GmFluidRegion *r = &f->reg[i];
        if (!r->active) { if (free_slot < 0) free_slot = i; continue; }
        int d = fl_dist_to_box(r, wx, wy, wz);
        if (best < 0 || d < best_d) { best = i; best_d = d; }
    }
    GmFluidRegion *r;
    if (best >= 0 && best_d <= FL_JOIN_DIST) r = &f->reg[best];
    else if (free_slot >= 0) {
        r = &f->reg[free_slot];
        r->active = 1; r->has_water = 0; r->quiet_steps = 0;
        r->x0 = r->x1 = wx; r->y0 = r->y1 = wy; r->z0 = r->z1 = wz;
    } else if (best >= 0) r = &f->reg[best];
    else return;
    fl_union(r, wx, wy, wz);
    if (water) r->has_water = 1;
}

/* One CA step over one region; returns cells changed in the world. */
static int fl_step_region(GmFluidLive *f, GmFluidRegion *rg, GmWorld *w) {
    /* grid box: dirty AABB + margin, clamped to the fixed grid size and world Y */
    int gx0 = rg->x0 - GM_FLUID_MARGIN * 2, gy0 = rg->y0 - GM_FLUID_MARGIN * 2;
    int gz0 = rg->z0 - GM_FLUID_MARGIN * 2;
    int nx = (rg->x1 - gx0) + 1 + GM_FLUID_MARGIN * 2;
    int ny = (rg->y1 - gy0) + 1 + GM_FLUID_MARGIN * 2;
    int nz = (rg->z1 - gz0) + 1 + GM_FLUID_MARGIN * 2;
    if (nx > GM_FLUID_NX) nx = GM_FLUID_NX;
    if (ny > GM_FLUID_NY) ny = GM_FLUID_NY;
    if (nz > GM_FLUID_NZ) nz = GM_FLUID_NZ;
    if (gy0 < 0) gy0 = 0;
    if (gy0 + ny > 256) ny = 256 - gy0;

    /* copy world -> CA grid. Displaceable covers are presented as air, and
     * STATIC liquids as their dynamic ids: ff_flow_cell early-returns on
     * static cells, and the CA's own global static->dynamic pass would fight
     * the per-step write-back (settled cells churn 8<->9 forever). Stable
     * cells settle back to static in the CA output and diff to no-op below. */
    for (int y = 0; y < ny; ++y)
        for (int z = 0; z < nz; ++z)
            for (int x = 0; x < nx; ++x) {
                int id = gm_world_block(w, gx0 + x, gy0 + y, gz0 + z);
                int meta = gm_world_meta(w, gx0 + x, gy0 + y, gz0 + z);
                if (fl_is_displaceable(id)) { id = 0; meta = 0; }
                if (id == FL_WATER) id = FL_FLOWING_WATER;
                else if (id == FL_LAVA) id = FL_FLOWING_LAVA;
                f->cur[(y * nz + z) * nx + x] = mc_state(id, meta);
            }

    /* nether lava decays 1 per spread (vanilla doesWaterVaporize), else 2 */
    ff_ca_step_ex(f->cur, f->tmp, nx, ny, nz, f->dim == -1 ? 1 : 2);

    /* write back diffs, skipping the untrustworthy boundary shell */
    int changed = 0;
    int nx0 = rg->x0, ny0 = rg->y0, nz0 = rg->z0;
    int nx1 = rg->x1, ny1 = rg->y1, nz1 = rg->z1;
    for (int y = GM_FLUID_MARGIN; y < ny - GM_FLUID_MARGIN; ++y)
        for (int z = GM_FLUID_MARGIN; z < nz - GM_FLUID_MARGIN; ++z)
            for (int x = GM_FLUID_MARGIN; x < nx - GM_FLUID_MARGIN; ++x) {
                int i = (y * nz + z) * nx + x;
                int wx = gx0 + x, wy = gy0 + y, wz = gz0 + z;
                int nid = mc_state_id(f->tmp[i]), nmeta = mc_state_meta(f->tmp[i]);
                /* diff against the REAL world cell (the grid remapped ids) */
                int wid = gm_world_block(w, wx, wy, wz);
                int wmeta = gm_world_meta(w, wx, wy, wz);
                if (nid == wid && nmeta == wmeta) continue;
                /* the grid hid a displaceable cover as air: only liquid may
                 * actually replace it in the world (vanilla destroys it) */
                if (fl_is_displaceable(wid) && !fl_is_liquid(nid)) continue;
                gm_world_set_block_meta(w, wx, wy, wz, nid, nmeta);
                if (++changed == 1) {
                    nx0 = nx1 = wx; ny0 = ny1 = wy; nz0 = nz1 = wz;
                } else {
                    if (wx < nx0) nx0 = wx; if (wx > nx1) nx1 = wx;
                    if (wy < ny0) ny0 = wy; if (wy > ny1) ny1 = wy;
                    if (wz < nz0) nz0 = wz; if (wz > nz1) nz1 = wz;
                }
            }

    if (changed) {
        /* keep following the flow front */
        rg->x0 = nx0; rg->y0 = ny0; rg->z0 = nz0;
        rg->x1 = nx1; rg->y1 = ny1; rg->z1 = nz1;
        rg->quiet_steps = 0;
    } else if (++rg->quiet_steps >= 2) {
        rg->active = 0;
        rg->has_water = 0;
    }
    return changed;
}

int gm_fluid_tick(GmFluidLive *f, GmWorld *w, int dim, long long world_time) {
    if (f->dim != dim) return 0;
    int total = 0;
    for (int i = 0; i < GM_FLUID_REGIONS; ++i) {
        GmFluidRegion *rg = &f->reg[i];
        if (!rg->active) continue;
        /* vanilla tick cadence: water 5; lava 30 overworld, 10 nether */
        int period = rg->has_water ? 5 : (dim == -1 ? 10 : 30);
        if (world_time % period != 0) continue;
        total += fl_step_region(f, rg, w);
    }
    return total;
}
