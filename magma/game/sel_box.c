/* game/sel_box.c - vanilla 1.11.2 selection bounding boxes (see sel_box.h). */
#include "game/sel_box.h"
#include "player_survival.h"   /* psv_get_block / psv_get_meta */

#include <math.h>

static void set6(float b[6], float x0, float y0, float z0,
                 float x1, float y1, float z1) {
    b[0] = x0; b[1] = y0; b[2] = z0; b[3] = x1; b[4] = y1; b[5] = z1;
}

static int is_fence(int id)  { return id == 85 || id == 113 || (id >= 188 && id <= 192); }
static int is_gate(int id)   { return id == 107 || (id >= 183 && id <= 187); }
static int is_pane(int id)   { return id == 101 || id == 102 || id == 160; }
static int is_glassy(int id) { return id == 20 || id == 95; }
static int is_solid_cube(int id) { return (mc_bpt_props(id).flags & BF_SOLID) != 0; }
static int is_rs_component(int id) {
    return id == 55 || id == 75 || id == 76 || id == 93 || id == 94 ||
           id == 149 || id == 150 || id == 69 || id == 77 || id == 143 ||
           id == 70 || id == 72 || id == 147 || id == 148 || id == 151 ||
           id == 178 || id == 152;
}

/* torch/lever wall-mount box helpers (exact BlockTorch/BlockLever constants) */
static void torch_box(int meta, float b[6]) {
    switch (meta) {
    case 1: set6(b, 0.0f, 0.2f, 0.35f, 0.3f, 0.8f, 0.65f); break;      /* east */
    case 2: set6(b, 0.7f, 0.2f, 0.35f, 1.0f, 0.8f, 0.65f); break;      /* west */
    case 3: set6(b, 0.35f, 0.2f, 0.0f, 0.65f, 0.8f, 0.3f); break;      /* south */
    case 4: set6(b, 0.35f, 0.2f, 0.7f, 0.65f, 0.8f, 1.0f); break;      /* north */
    default: set6(b, 0.4f, 0.0f, 0.4f, 0.6f, 0.6f, 0.6f); break;       /* standing */
    }
}

void gm_sel_box(const GmSelIn *in, float b[6]) {
    int id = in->id, meta = in->meta;
    set6(b, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f);   /* default: FULL_BLOCK_AABB */
    switch (id) {
    /* ---- plants (BlockBush family) ---- */
    case 6:  case 31: case 32:                       /* sapling/tallgrass/deadbush */
        set6(b, 0.1f, 0.f, 0.1f, 0.9f, 0.8f, 0.9f); break;
    case 37: case 38:                                /* flowers */
        set6(b, 0.3f, 0.f, 0.3f, 0.7f, 0.6f, 0.7f); break;
    case 39: case 40:                                /* mushrooms */
        set6(b, 0.3f, 0.f, 0.3f, 0.7f, 0.4f, 0.7f); break;
    case 59: case 141: case 142:                     /* crops: (age+1)/8 tall */
        set6(b, 0.f, 0.f, 0.f, 1.f, (float)((meta & 7) + 1) * 0.125f, 1.f); break;
    case 104: case 105:                              /* pumpkin/melon stem */
        set6(b, 0.375f, 0.f, 0.375f, 0.625f, (float)((meta & 7) + 1) * 0.125f, 0.625f); break;
    case 115:                                        /* nether wart */
    {
        static const float wart_h[4] = { 0.3125f, 0.5f, 0.6875f, 0.875f };
        set6(b, 0.f, 0.f, 0.f, 1.f, wart_h[meta & 3], 1.f); break;
    }
    case 83:  set6(b, 0.125f, 0.f, 0.125f, 0.875f, 1.f, 0.875f); break;   /* reeds */
    case 81:  set6(b, 0.0625f, 0.f, 0.0625f, 0.9375f, 1.f, 0.9375f); break; /* cactus */
    case 111: set6(b, 0.0625f, 0.f, 0.0625f, 0.9375f, 0.09375f, 0.9375f); break; /* lily */

    /* ---- flat / partial-height blocks ---- */
    case 29: case 33:                                /* piston bases */
        if (meta & 8) {
            switch (meta & 7) {
            case 0: set6(b, 0.f, 0.25f, 0.f, 1.f, 1.f, 1.f); break;
            case 1: set6(b, 0.f, 0.f, 0.f, 1.f, 0.75f, 1.f); break;
            case 2: set6(b, 0.f, 0.f, 0.25f, 1.f, 1.f, 1.f); break;
            case 3: set6(b, 0.f, 0.f, 0.f, 1.f, 1.f, 0.75f); break;
            case 4: set6(b, 0.25f, 0.f, 0.f, 1.f, 1.f, 1.f); break;
            case 5: set6(b, 0.f, 0.f, 0.f, 0.75f, 1.f, 1.f); break;
            default: break;
            }
        }
        break;
    case 44: case 126: case 182:                     /* slabs: top bit 8 */
        if (meta & 8) set6(b, 0.f, 0.5f, 0.f, 1.f, 1.f, 1.f);
        else          set6(b, 0.f, 0.f, 0.f, 1.f, 0.5f, 1.f);
        break;
    case 78:                                          /* snow layer */
        set6(b, 0.f, 0.f, 0.f, 1.f, (float)((meta & 7) + 1) * 0.125f, 1.f); break;
    case 60:  set6(b, 0.f, 0.f, 0.f, 1.f, 0.9375f, 1.f); break;  /* farmland */
    case 88:  set6(b, 0.f, 0.f, 0.f, 1.f, 0.875f, 1.f); break;   /* soul sand */
    case 171: set6(b, 0.f, 0.f, 0.f, 1.f, 0.0625f, 1.f); break;  /* carpet */
    case 26:  set6(b, 0.f, 0.f, 0.f, 1.f, 0.5625f, 1.f); break;  /* bed */
    case 116: set6(b, 0.f, 0.f, 0.f, 1.f, 0.75f, 1.f); break;    /* enchant table */
    case 151: case 178: set6(b, 0.f, 0.f, 0.f, 1.f, 0.375f, 1.f); break; /* daylight */
    case 93: case 94: case 149: case 150:              /* redstone diodes */
        set6(b, 0.f, 0.f, 0.f, 1.f, 0.125f, 1.f); break;
    case 117: set6(b, 0.f, 0.f, 0.f, 1.f, 0.125f, 1.f); break; /* brewing stand base */
    case 120: set6(b, 0.f, 0.f, 0.f, 1.f, 0.8125f, 1.f); break;  /* end portal frame */
    case 27: case 28: case 66: case 157:              /* rails */
        set6(b, 0.f, 0.f, 0.f, 1.f, 0.125f, 1.f); break;
    case 70: case 72: case 147: case 148:             /* pressure plates */
        set6(b, 0.0625f, 0.f, 0.0625f, 0.9375f, meta ? 0.03125f : 0.0625f, 0.9375f); break;
    case 132:                                         /* tripwire */
        if (meta & 4) set6(b, 0.f, 0.0625f, 0.f, 1.f, 0.15625f, 1.f);
        else          set6(b, 0.f, 0.f, 0.f, 1.f, 0.5f, 1.f);
        break;
    case 92:                                          /* cake: bites eat from x0 */
        set6(b, 0.0625f + (float)(meta & 7) * 0.125f, 0.f, 0.0625f,
             0.9375f, 0.5f, 0.9375f); break;

    /* ---- wall-mounted ---- */
    case 50: case 75: case 76: torch_box(meta, b); break;   /* torches */
    case 65:                                          /* ladder (facing meta) */
        switch (meta) {
        case 2: set6(b, 0.f, 0.f, 0.8125f, 1.f, 1.f, 1.f); break;    /* north */
        case 3: set6(b, 0.f, 0.f, 0.f, 1.f, 1.f, 0.1875f); break;    /* south */
        case 4: set6(b, 0.8125f, 0.f, 0.f, 1.f, 1.f, 1.f); break;    /* west */
        default: set6(b, 0.f, 0.f, 0.f, 0.1875f, 1.f, 1.f); break;   /* east */
        } break;
    case 68:                                          /* wall sign */
        switch (meta) {
        case 2: set6(b, 0.f, 0.28125f, 0.875f, 1.f, 0.78125f, 1.f); break;
        case 3: set6(b, 0.f, 0.28125f, 0.f, 1.f, 0.78125f, 0.125f); break;
        case 4: set6(b, 0.875f, 0.28125f, 0.f, 1.f, 0.78125f, 1.f); break;
        default: set6(b, 0.f, 0.28125f, 0.f, 0.125f, 0.78125f, 1.f); break;
        } break;
    case 63: set6(b, 0.25f, 0.f, 0.25f, 0.75f, 1.f, 0.75f); break;  /* standing sign */
    case 69:                                          /* lever */
        switch (meta & 7) {
        case 1: set6(b, 0.f, 0.2f, 0.3125f, 0.375f, 0.8f, 0.6875f); break;
        case 2: set6(b, 0.625f, 0.2f, 0.3125f, 1.f, 0.8f, 0.6875f); break;
        case 3: set6(b, 0.3125f, 0.2f, 0.f, 0.6875f, 0.8f, 0.375f); break;
        case 4: set6(b, 0.3125f, 0.2f, 0.625f, 0.6875f, 0.8f, 1.f); break;
        case 5: case 6: set6(b, 0.25f, 0.f, 0.25f, 0.75f, 0.6f, 0.75f); break;
        default: set6(b, 0.25f, 0.4f, 0.25f, 0.75f, 1.f, 0.75f); break;
        } break;
    case 77: case 143:                                /* buttons (unpressed depth) */
        switch (meta & 7) {
        case 0: set6(b, 0.3125f, 0.875f, 0.375f, 0.6875f, 1.f, 0.625f); break;
        case 1: set6(b, 0.f, 0.375f, 0.3125f, 0.125f, 0.625f, 0.6875f); break;
        case 2: set6(b, 0.875f, 0.375f, 0.3125f, 1.f, 0.625f, 0.6875f); break;
        case 3: set6(b, 0.3125f, 0.375f, 0.f, 0.6875f, 0.625f, 0.125f); break;
        case 4: set6(b, 0.3125f, 0.375f, 0.875f, 0.6875f, 0.625f, 1.f); break;
        default: set6(b, 0.3125f, 0.f, 0.375f, 0.6875f, 0.125f, 0.625f); break;
        } break;
    case 106:                                         /* vine: union of wall slabs */
        if (meta == 0) { set6(b, 0.f, 0.9375f, 0.f, 1.f, 1.f, 1.f); break; }
        {
            /* per attached face, a 1/16 slab on that wall; box = union */
            float x0 = 1.f, z0 = 1.f, x1 = 0.f, z1 = 0.f;
            if (meta & 1) { x0 = 0.f; x1 = 1.f; if (z0 > 0.9375f) z0 = 0.9375f; z1 = 1.f; } /* south */
            if (meta & 2) { z0 = 0.f; z1 = 1.f; x0 = 0.f; if (x1 < 0.0625f) x1 = 0.0625f; } /* west */
            if (meta & 4) { x0 = 0.f; x1 = 1.f; z0 = 0.f; if (z1 < 0.0625f) z1 = 0.0625f; } /* north */
            if (meta & 8) { z0 = 0.f; z1 = 1.f; if (x0 > 0.9375f) x0 = 0.9375f; x1 = 1.f; } /* east */
            set6(b, x0, 0.f, z0, x1, 1.f, z1);
        } break;
    case 144:                                         /* skull */
        switch (meta & 7) {
        case 2: set6(b, 0.25f, 0.25f, 0.5f, 0.75f, 0.75f, 1.f); break;
        case 3: set6(b, 0.25f, 0.25f, 0.f, 0.75f, 0.75f, 0.5f); break;
        case 4: set6(b, 0.5f, 0.25f, 0.25f, 1.f, 0.75f, 0.75f); break;
        case 5: set6(b, 0.f, 0.25f, 0.25f, 0.5f, 0.75f, 0.75f); break;
        default: set6(b, 0.25f, 0.f, 0.25f, 0.75f, 0.5f, 0.75f); break;
        } break;
    case 140: set6(b, 0.3125f, 0.f, 0.3125f, 0.6875f, 0.375f, 0.6875f); break; /* pot */
    case 198:                                         /* end rod */
        switch (meta & 7) {
        case 2: case 3: set6(b, 0.375f, 0.375f, 0.f, 0.625f, 0.625f, 1.f); break;
        case 4: case 5: set6(b, 0.f, 0.375f, 0.375f, 1.f, 0.625f, 0.625f); break;
        default: set6(b, 0.375f, 0.f, 0.375f, 0.625f, 1.f, 0.625f); break;
        } break;

    /* ---- containers / doors / connected shapes ---- */
    case 54: case 130: case 146:                      /* chest (single-box) */
        set6(b, 0.0625f, 0.f, 0.0625f, 0.9375f, 0.875f, 0.9375f); break;
    case 96: case 167:                                /* trapdoor */
        if (meta & 4) {                               /* open: hinge wall */
            switch (meta & 3) {
            case 0: set6(b, 0.f, 0.f, 0.8125f, 1.f, 1.f, 1.f); break;
            case 1: set6(b, 0.f, 0.f, 0.f, 1.f, 1.f, 0.1875f); break;
            case 2: set6(b, 0.8125f, 0.f, 0.f, 1.f, 1.f, 1.f); break;
            default: set6(b, 0.f, 0.f, 0.f, 0.1875f, 1.f, 1.f); break;
            }
        } else if (meta & 8) set6(b, 0.f, 0.8125f, 0.f, 1.f, 1.f, 1.f);
        else                 set6(b, 0.f, 0.f, 0.f, 1.f, 0.1875f, 1.f);
        break;
    case 64: case 71: case 193: case 194: case 195: case 196: case 197: /* doors */
    {
        int upper  = (meta & 8) != 0;
        int lm     = upper ? in->below_meta : meta;       /* facing+open half */
        int hm     = upper ? meta : in->above_meta;       /* hinge half */
        int facing = lm & 3;                              /* 0 E, 1 S, 2 W, 3 N */
        int open   = (lm & 4) != 0;
        int right  = (hm & 1) != 0;
        /* BlockDoor.getBoundingBox: closed -> facing wall; open -> hinge side */
        static const float FB[4][6] = {
            { 0.f, 0.f, 0.f, 0.1875f, 1.f, 1.f },      /* EAST_AABB  */
            { 0.f, 0.f, 0.f, 1.f, 1.f, 0.1875f },      /* SOUTH_AABB */
            { 0.8125f, 0.f, 0.f, 1.f, 1.f, 1.f },      /* WEST_AABB  */
            { 0.f, 0.f, 0.8125f, 1.f, 1.f, 1.f },      /* NORTH_AABB */
        };
        int box;
        if (!open) box = facing;
        else switch (facing) {
        case 0:  box = right ? 3 : 1; break;   /* east:  N or S */
        case 1:  box = right ? 0 : 2; break;   /* south: E or W */
        case 2:  box = right ? 1 : 3; break;   /* west:  S or N */
        default: box = right ? 2 : 0; break;   /* north: W or E */
        }
        set6(b, FB[box][0], FB[box][1], FB[box][2], FB[box][3], FB[box][4], FB[box][5]);
        break;
    }
    case 85: case 113: case 188: case 189: case 190: case 191: case 192: /* fences */
    case 101: case 102: case 160:                                        /* panes/bars */
    {
        int fence = is_fence(id);
        float lo = fence ? 0.375f : 0.4375f, hi = fence ? 0.625f : 0.5625f;
        float x0 = lo, x1 = hi, z0 = lo, z1 = hi;
        for (int d = 0; d < 4; ++d) {
            int nb = in->nid[d];
            int conn = fence ? (is_fence(nb) || is_gate(nb) || is_solid_cube(nb))
                             : (is_pane(nb) || is_glassy(nb) || is_solid_cube(nb));
            if (!conn) continue;
            if (d == 0) z0 = 0.f; else if (d == 1) z1 = 1.f;
            else if (d == 2) x0 = 0.f; else x1 = 1.f;
        }
        set6(b, x0, 0.f, z0, x1, 1.f, z1);
        break;
    }
    case 107: case 183: case 184: case 185: case 186: case 187:  /* fence gates */
        if ((meta & 1) == 0) set6(b, 0.f, 0.f, 0.375f, 1.f, 1.f, 0.625f); /* S/N */
        else                 set6(b, 0.375f, 0.f, 0.f, 0.625f, 1.f, 1.f); /* W/E */
        break;
    case 55:                                          /* redstone wire */
    {
        float x0 = 0.1875f, x1 = 0.8125f, z0 = 0.1875f, z1 = 0.8125f;
        for (int d = 0; d < 4; ++d) {
            if (!is_rs_component(in->nid[d])) continue;
            if (d == 0) z0 = 0.f; else if (d == 1) z1 = 1.f;
            else if (d == 2) x0 = 0.f; else x1 = 1.f;
        }
        set6(b, x0, 0.f, z0, x1, 0.0625f, z1);
        break;
    }
    default: break;   /* full cube */
    }
}

/* AxisAlignedBB.calculateIntercept: return the first box intercept and the
 * outward normal of its face.  The normal matters for non-full selection
 * boxes: the cell entered by the DDA is not necessarily adjacent to the face
 * eventually struck inside that cell. */
static double ray_box_hit(double ex, double ey, double ez,
                          double dx, double dy, double dz,
                          double x0, double y0, double z0,
                          double x1, double y1, double z1,
                          int *nx, int *ny, int *nz) {
    double tmin = -1e30, tmax = 1e30;
    const double o[3] = { ex, ey, ez }, d[3] = { dx, dy, dz };
    const double lo[3] = { x0, y0, z0 }, hi[3] = { x1, y1, z1 };
    int enter_axis = -1, enter_sign = 0;
    int exit_axis = -1, exit_sign = 0;
    for (int i = 0; i < 3; ++i) {
        if (d[i] > -1e-12 && d[i] < 1e-12) {
            if (o[i] < lo[i] || o[i] > hi[i]) return -1.0;
            continue;
        }
        double t0 = (lo[i] - o[i]) / d[i], t1 = (hi[i] - o[i]) / d[i];
        int n0 = -1, n1 = 1;
        if (t0 > t1) {
            double tt = t0; t0 = t1; t1 = tt;
            int nt = n0; n0 = n1; n1 = nt;
        }
        if (t0 > tmin) {
            tmin = t0;
            enter_axis = i;
            enter_sign = n0;
        }
        if (t1 < tmax) {
            tmax = t1;
            exit_axis = i;
            exit_sign = n1;
        }
        if (tmin > tmax) return -1.0;
    }
    int axis = enter_axis, sign = enter_sign;
    double t = tmin;
    if (t < 0.0) {
        t = tmax;
        axis = exit_axis;
        sign = exit_sign;
    }
    if (t < 0.0 || axis < 0) return -1.0;
    *nx = axis == 0 ? sign : 0;
    *ny = axis == 1 ? sign : 0;
    *nz = axis == 2 ? sign : 0;
    return t;
}

/* blocks the selection ray passes through (vanilla collisionRayTrace null) */
static int ray_transparent(int id) {
    return id == 0 || (id >= 8 && id <= 11) || id == 51;
}

/* PlayerControllerMP.getBlockReachDistance: creative 5.0F, survival 4.5F.
 * EntityRenderer.getMouseOver uses that for entity.rayTrace / the outline. */
#ifndef GM_SEL_REACH
#define GM_SEL_REACH 4.5
#endif

void gm_player_look_ray(const McSinTable *st, const PsvPlayer *pl,
                        double *ex, double *ey, double *ez,
                        double *dx, double *dy, double *dz) {
    /* vanilla getVectorForRotation(pitch, yaw), same as psv_raycast */
    float f  = mc_cos(st, -pl->yaw * 0.017453292f - 3.1415927f);
    float f1 = mc_sin(st, -pl->yaw * 0.017453292f - 3.1415927f);
    float f2 = -mc_cos(st, -pl->pitch * 0.017453292f);
    float f3 = mc_sin(st, -pl->pitch * 0.017453292f);
    *dx = (double)(f1 * f2);
    *dy = (double)f3;
    *dz = (double)(f * f2);
    *ex = pl->ent.posX;
    *ey = pl->ent.posY + psv_player_eye_height(pl);
    *ez = pl->ent.posZ;
}

int gm_raycast_sel_reach_distance(
        const Chunk *win, const McSinTable *st, const PsvPlayer *pl,
        double reach, int *hx, int *hy, int *hz,
        int *ax, int *ay, int *az, double *distance) {
    double ex, ey, ez, dx, dy, dz;
    gm_player_look_ray(st, pl, &ex, &ey, &ez, &dx, &dy, &dz);

    int lastx = mc_floor(ex), lasty = mc_floor(ey), lastz = mc_floor(ez);
    int have_air = 0;
    for (double t = PSV_RAY_DT; t <= reach; t += PSV_RAY_DT) {
        int bx = mc_floor(ex + dx * t);
        int by = mc_floor(ey + dy * t);
        int bz = mc_floor(ez + dz * t);
        if (bx == lastx && by == lasty && bz == lastz) continue;
        int id = psv_get_block(win, bx, by, bz);
        if (!ray_transparent(id)) {
            float b[6];
            int nx, ny, nz;
            gm_sel_box_at(win, bx, by, bz, b);
            double th = ray_box_hit(ex, ey, ez, dx, dy, dz,
                                    bx + (double)b[0], by + (double)b[1], bz + (double)b[2],
                                    bx + (double)b[3], by + (double)b[4], bz + (double)b[5],
                                    &nx, &ny, &nz);
            if (th >= 0.0 && th <= reach) {
                *hx = bx; *hy = by; *hz = bz;
                *ax = bx + nx; *ay = by + ny; *az = bz + nz;
                if (distance)
                    *distance = th * sqrt(dx * dx + dy * dy + dz * dz);
                return have_air;
            }
        }
        lastx = bx; lasty = by; lastz = bz;
        have_air = 1;
    }
    return -1;
}

int gm_raycast_sel_reach(const Chunk *win, const McSinTable *st,
                         const PsvPlayer *pl, double reach,
                         int *hx, int *hy, int *hz, int *ax, int *ay, int *az) {
    return gm_raycast_sel_reach_distance(
        win, st, pl, reach, hx, hy, hz, ax, ay, az, NULL);
}

int gm_raycast_sel(const Chunk *win, const McSinTable *st,
                   const PsvPlayer *pl,
                   int *hx, int *hy, int *hz, int *ax, int *ay, int *az) {
    return gm_raycast_sel_reach(win, st, pl, GM_SEL_REACH, hx, hy, hz, ax, ay, az);
}

void gm_sel_box_at(const Chunk *win, int x, int y, int z, float b[6]) {
    const Chunk *w = win;
    GmSelIn in;
    in.id   = psv_get_block(w, x, y, z);
    in.meta = psv_get_meta(w, x, y, z);
    in.nid[0] = psv_get_block(w, x, y, z - 1);
    in.nid[1] = psv_get_block(w, x, y, z + 1);
    in.nid[2] = psv_get_block(w, x - 1, y, z);
    in.nid[3] = psv_get_block(w, x + 1, y, z);
    in.below_meta = psv_get_meta(w, x, y - 1, z);
    in.above_meta = psv_get_meta(w, x, y + 1, z);
    gm_sel_box(&in, b);
}
