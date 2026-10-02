#include "pick.h"
#include "jmath.h"

#include <math.h>
#include <stddef.h>

void pick_kind_size(int kind, int part, float *width, float *height, float *y_offset)
{
    static const float part_size[7] = {6.0F, 8.0F, 4.0F, 4.0F, 4.0F, 4.0F, 4.0F};
    float w = 0.6F, h = 1.8F, yo = 0.0F;

    switch (kind)
    {
    case PK_ZOMBIE: w = 0.6F; h = 1.8F; break;
    case PK_LARGE_FIREBALL: w = 1.0F; h = 1.0F; break;
    case PK_SMALL_FIREBALL: w = 0.3125F; h = 0.3125F; break;
    case PK_FALLING: case PK_TNT: w = 0.98F; h = 0.98F; yo = h / 2.0F; break;
    case PK_CRYSTAL: w = 2.0F; h = 2.0F; yo = h / 2.0F; break;
    case PK_PAINTING: case PK_FRAME: case PK_KNOT: w = 0.5F; h = 0.5F; break;
    case PK_DRAGON: w = 16.0F; h = 8.0F; break;
    case PK_PART: w = h = part_size[part < 0 || part > 6 ? 0 : part]; break;
    case PK_ITEM: w = 0.25F; h = 0.25F; yo = h / 2.0F; break;
    case PK_ORB: w = 0.5F; h = 0.5F; yo = h / 2.0F; break;
    case PK_ARROW: w = 0.5F; h = 0.5F; break;
    case PK_THROWN: case PK_EYE: w = 0.25F; h = 0.25F; break;
    }
    *width = w;
    *height = h;
    *y_offset = yo;
}

struct aabb pick_kind_box(int kind, int part, double x, double y, double z)
{
    float w, h, yo;
    pick_kind_size(kind, part, &w, &h, &yo);
    float var7 = w / 2.0F;
    return aabb_make(x - (double)var7, y - (double)yo + 0.0, z - (double)var7,
                     x + (double)var7, y - (double)yo + 0.0 + (double)h, z + (double)var7);
}

int pick_kind_collidable(int kind)
{
    switch (kind)
    {
    case PK_ZOMBIE: case PK_LARGE_FIREBALL: case PK_FALLING: case PK_TNT: case PK_CRYSTAL:
    case PK_PAINTING: case PK_FRAME: case PK_PART: case PK_KNOT:
        return 1;
    }
    return 0;
}

float pick_kind_border(int kind)
{
    /* EntityFireball's 1.0 covers the small fireball too; Entity's 0.1 the
     * rest */
    return kind == PK_LARGE_FIREBALL || kind == PK_SMALL_FIREBALL ? 1.0F : 0.1F;
}

/* Vec3.getIntermediateWith{X,Y,Z}Value. */
static int intermediate(double *out, double ax, double ay, double az,
                        double bx, double by, double bz, double target, int axis)
{
    double dx = bx - ax, dy = by - ay, dz = bz - az;
    double d = axis == 0 ? dx : (axis == 1 ? dy : dz);

    if (d * d < 1.0000000116860974E-7) return 0;

    double t = (target - (axis == 0 ? ax : (axis == 1 ? ay : az))) / d;

    if (!(t >= 0.0 && t <= 1.0)) return 0;

    out[0] = ax + dx * t;
    out[1] = ay + dy * t;
    out[2] = az + dz * t;
    return 1;
}

int pick_intercept(const struct aabb *b, double sx, double sy, double sz,
                   double ex, double ey, double ez,
                   double *hx, double *hy, double *hz, int *side)
{
    double v[6][3];
    int ok[6];
    ok[0] = intermediate(v[0], sx, sy, sz, ex, ey, ez, b->min_x, 0);
    ok[1] = intermediate(v[1], sx, sy, sz, ex, ey, ez, b->max_x, 0);
    ok[2] = intermediate(v[2], sx, sy, sz, ex, ey, ez, b->min_y, 1);
    ok[3] = intermediate(v[3], sx, sy, sz, ex, ey, ez, b->max_y, 1);
    ok[4] = intermediate(v[4], sx, sy, sz, ex, ey, ez, b->min_z, 2);
    ok[5] = intermediate(v[5], sx, sy, sz, ex, ey, ez, b->max_z, 2);

    /* isVecInYZ, isVecInXZ, isVecInXY */
    for (int i = 0; i < 2; ++i)
        if (ok[i] && !(v[i][1] >= b->min_y && v[i][1] <= b->max_y && v[i][2] >= b->min_z && v[i][2] <= b->max_z)) ok[i] = 0;
    for (int i = 2; i < 4; ++i)
        if (ok[i] && !(v[i][0] >= b->min_x && v[i][0] <= b->max_x && v[i][2] >= b->min_z && v[i][2] <= b->max_z)) ok[i] = 0;
    for (int i = 4; i < 6; ++i)
        if (ok[i] && !(v[i][0] >= b->min_x && v[i][0] <= b->max_x && v[i][1] >= b->min_y && v[i][1] <= b->max_y)) ok[i] = 0;

    int best = -1;
    double bestd = 0.0;
    for (int i = 0; i < 6; ++i)
    {
        if (!ok[i]) continue;
        double dx = v[i][0] - sx, dy = v[i][1] - sy, dz = v[i][2] - sz;
        double d = dx * dx + dy * dy + dz * dz;
        if (best < 0 || d < bestd)
        {
            best = i;
            bestd = d;
        }
    }
    if (best < 0) return 0;

    static const int faces[6] = {4, 5, 0, 1, 2, 3};
    *hx = v[best][0];
    *hy = v[best][1];
    *hz = v[best][2];
    if (side) *side = faces[best];
    return 1;
}

double pick_distance(double ax, double ay, double az, double bx, double by, double bz)
{
    double dx = bx - ax, dy = by - ay, dz = bz - az;
    return (double)(float)sqrt(dx * dx + dy * dy + dz * dz);
}

struct pick_result pick_entity_pass(const struct pick_cand *c, int n,
                                    double ex, double ey, double ez,
                                    double lx, double ly, double lz,
                                    double reach, double block_dist, int have_mouse_over)
{
    struct pick_result r = {-1, 0.0, 0.0, 0.0, block_dist, 0};
    double tx = ex + lx * reach, ty = ey + ly * reach, tz = ez + lz * reach;
    double var12 = block_dist;

    for (int i = 0; i < n; ++i)
    {
        if (!c[i].collidable) continue;

        double b = (double)c[i].border;
        struct aabb var17 = aabb_expand(c[i].box, b, b, b);
        double hx = 0.0, hy = 0.0, hz = 0.0;
        int hit = pick_intercept(&var17, ex, ey, ez, tx, ty, tz, &hx, &hy, &hz, NULL);

        if (ex > var17.min_x && ex < var17.max_x && ey > var17.min_y && ey < var17.max_y &&
            ez > var17.min_z && ez < var17.max_z)
        {
            if (0.0 < var12 || var12 == 0.0)
            {
                r.chosen = i;
                if (hit) { r.hx = hx; r.hy = hy; r.hz = hz; }
                else { r.hx = ex; r.hy = ey; r.hz = ez; }
                var12 = 0.0;
            }
        }
        else if (hit)
        {
            double var19 = pick_distance(ex, ey, ez, hx, hy, hz);

            if (var19 < var12 || var12 == 0.0)
            {
                if (c[i].riding)
                {
                    if (var12 == 0.0)
                    {
                        r.chosen = i;
                        r.hx = hx; r.hy = hy; r.hz = hz;
                    }
                }
                else
                {
                    r.chosen = i;
                    r.hx = hx; r.hy = hy; r.hz = hz;
                    var12 = var19;
                }
            }
        }
    }

    r.dist = var12;
    r.replaced = r.chosen >= 0 && (var12 < block_dist || !have_mouse_over);
    return r;
}

static int clamp16(int v)
{
    return v < 0 ? 0 : (v > 15 ? 15 : v);
}

int pick_order(const struct pick_entry *e, int n, const struct aabb *search, int *out, int cap)
{
    int x0 = mh_floor((search->min_x - 2.0) / 16.0), x1 = mh_floor((search->max_x + 2.0) / 16.0);
    int z0 = mh_floor((search->min_z - 2.0) / 16.0), z1 = mh_floor((search->max_z + 2.0) / 16.0);
    int y0 = clamp16(mh_floor((search->min_y - 2.0) / 16.0));
    int y1 = clamp16(mh_floor((search->max_y + 2.0) / 16.0));
    int m = 0;

    for (int cx = x0; cx <= x1; ++cx)
        for (int cz = z0; cz <= z1; ++cz)
            for (int cy = y0; cy <= y1; ++cy)
            {
                /* the slice's list in insertion order */
                uint64_t last = 0;
                int have_last = 0;
                for (;;)
                {
                    int next = -1;
                    for (int i = 0; i < n; ++i)
                    {
                        if (e[i].owner >= 0 || e[i].cx != cx || e[i].cz != cz || e[i].cy != cy) continue;
                        if (have_last && e[i].seq <= last) continue;
                        if (next < 0 || e[i].seq < e[next].seq) next = i;
                    }
                    if (next < 0) break;
                    last = e[next].seq;
                    have_last = 1;
                    if (!aabb_intersects(&e[next].box, search)) continue;
                    if (m < cap) out[m] = next;
                    ++m;
                    for (int j = 0; j < n; ++j)
                    {
                        if (e[j].owner != next || !aabb_intersects(&e[j].box, search)) continue;
                        if (m < cap) out[m] = j;
                        ++m;
                    }
                }
            }
    return m;
}
