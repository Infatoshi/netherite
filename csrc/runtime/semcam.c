/* The semantic camera (semcam.h). */
#include "semcam.h"

#include <math.h>
#include <string.h>

#include "../engine/jmath.h"
#include "../engine/player.h"
#include "../engine/session.h"
#include "../engine/world.h"

/* tan(35 degrees): half the renderer's vertical field of view of 70 */
#define SC_TAN_HALF 0.7002075382097097

/* the camera's axes: forward f, right r and up u = r x f */
struct basis { double fx, fy, fz, rx, ry, rz, ux, uy, uz, aspect; };

static void basis(const struct semcam_spec *sp, float yaw, float pitch, struct basis *b)
{
    /* Entity.getLook: (f2 * f3, f4, f1 * f3) */
    float f1 = mh_cos(-yaw * 0.017453292F - 3.1415927F);
    float f2 = mh_sin(-yaw * 0.017453292F - 3.1415927F);
    float f3 = -mh_cos(-pitch * 0.017453292F);
    float f4 = mh_sin(-pitch * 0.017453292F);
    b->fx = (double)(f2 * f3), b->fy = (double)f4, b->fz = (double)(f1 * f3);
    /* right: horizontal, (-cos yaw, 0, -sin yaw) */
    b->rx = (double)f1, b->ry = 0.0, b->rz = (double)-f2;
    b->ux = b->ry * b->fz - b->rz * b->fy;
    b->uy = b->rz * b->fx - b->rx * b->fz;
    b->uz = b->rx * b->fy - b->ry * b->fx;
    b->aspect = (double)sp->w / (double)sp->h;
}

/* ray (i, j): through the centre of pixel i of row j, unit length */
static inline void ray(const struct semcam_spec *sp, const struct basis *b, int i, int j, double *d)
{
    double sy = (1.0 - 2.0 * ((double)j + 0.5) / (double)sp->h) * SC_TAN_HALF;
    double sx = (2.0 * ((double)i + 0.5) / (double)sp->w - 1.0) * SC_TAN_HALF * b->aspect;
    double dx = b->fx + sx * b->rx + sy * b->ux, dy = b->fy + sx * b->ry + sy * b->uy, dz = b->fz + sx * b->rz + sy * b->uz;
    double len = sqrt(dx * dx + dy * dy + dz * dz);
    d[0] = dx / len;
    d[1] = dy / len;
    d[2] = dz / len;
}

void semcam_rays(const struct semcam_spec *sp, float yaw, float pitch, double *dir)
{
    struct basis b;
    basis(sp, yaw, pitch, &b);
    for (int j = 0; j < sp->h; ++j)
        for (int i = 0; i < sp->w; ++i) ray(sp, &b, i, j, &dir[3 * ((size_t)j * (size_t)sp->w + (size_t)i)]);
}

/* one cast's shared state */
struct cast {
    struct world *w;
    double ox, oy, oz;
    int liquid;                 /* the eye's liquid: 8 water, 10 lava, 0 none */
    int range;
    int top;                    /* fast: no loaded section within reach reaches y top or above */
    /* fast: the last cell's chunk and band */
    int ccx, ccz, csy;
    struct chunk *c;
    const struct chunk_sec *s;
};

static int liquid_of(int id)
{
    return id == 8 || id == 9 ? 8 : id == 10 || id == 11 ? 10 : 0;
}

/* the cell's id | meta << 12, the reference's way (each cell through the
 * world's chunk lookup and chunk_get_block) */
static int cell_ref(struct cast *k, int x, int y, int z, int *meta)
{
    struct chunk *c = world_chunk(k->w, x >> 4, z >> 4);
    if (c == NULL) return 0;
    int id = chunk_get_block(c, x & 15, y, z & 15);
    *meta = id ? chunk_get_meta(c, x & 15, y, z & 15) : 0;
    return id;
}

/* the fast path's: the chunk and the band of the last cell kept */
static inline int cell_fast(struct cast *k, int x, int y, int z, int *meta)
{
    int cx = x >> 4, cz = z >> 4, sy = y >> 4;
    if (cx != k->ccx || cz != k->ccz)
    {
        k->ccx = cx;
        k->ccz = cz;
        k->c = world_chunk(k->w, cx, cz);
        k->csy = -1;
    }
    if (k->c == NULL) return 0;
    if (sy != k->csy)
    {
        k->csy = sy;
        k->s = (k->c->mask >> sy & 1) ? chunk_sec_at(k->c, sy) : NULL;
    }
    if (k->s == NULL) return 0;
    int i = SEC_XYZ(x & 15, y, z & 15);
    int id = chunk_sec_id(k->s, i);
    *meta = id ? nibble_get(chunk_sec_metas(k->s), i) : 0;
    return id;
}

/* one ray: the same arithmetic for both casts; fast only changes how a cell
 * is read and ends a ray that can meet nothing more */
static inline __attribute__((always_inline)) void walk(struct cast *k, const double *d, int fast, struct semcam_cell *out)
{
    double dx = d[0], dy = d[1], dz = d[2];
    int ix = (int)floor(k->ox), iy = (int)floor(k->oy), iz = (int)floor(k->oz);
    int sx = dx > 0 ? 1 : dx < 0 ? -1 : 0, sy = dy > 0 ? 1 : dy < 0 ? -1 : 0, sz = dz > 0 ? 1 : dz < 0 ? -1 : 0;
    double tdx = sx ? fabs(1.0 / dx) : INFINITY, tdy = sy ? fabs(1.0 / dy) : INFINITY, tdz = sz ? fabs(1.0 / dz) : INFINITY;
    double tx = sx > 0 ? ((double)ix + 1.0 - k->ox) / dx : sx < 0 ? ((double)ix - k->ox) / dx : INFINITY;
    double ty = sy > 0 ? ((double)iy + 1.0 - k->oy) / dy : sy < 0 ? ((double)iy - k->oy) / dy : INFINITY;
    double tz = sz > 0 ? ((double)iz + 1.0 - k->oz) / dz : sz < 0 ? ((double)iz - k->oz) / dz : INFINITY;
    double range = (double)k->range;
    out->block = 0;
    out->dist = 0xffff;
    out->face = SC_MISS;
    out->flags = k->liquid ? SCF_EYE_LIQUID : 0;
    out->pad = 0;
    if (fast && iy >= k->top && sy >= 0) return;
    for (;;)
    {
        double t;
        int face;
        if (tx <= ty && tx <= tz)
        {
            t = tx;
            ix += sx;
            tx += tdx;
            face = sx > 0 ? 4 : 5;
        }
        else if (ty <= tz)
        {
            t = ty;
            iy += sy;
            ty += tdy;
            face = sy > 0 ? 0 : 1;
        }
        else
        {
            t = tz;
            iz += sz;
            tz += tdz;
            face = sz > 0 ? 2 : 3;
        }
        if (!(t <= range)) return;
        if (iy > 255) return;
        if (iy < 0)
        {
            out->face = SC_VOID;
            out->dist = (uint16_t)(t * 256.0 < 65534.0 ? floor(t * 256.0) : 65534.0);
            return;
        }
        if (fast && iy >= k->top && sy >= 0) return;
        int meta = 0;
        int id = fast ? cell_fast(k, ix, iy, iz, &meta) : cell_ref(k, ix, iy, iz, &meta);
        if (id == 0 || (k->liquid && liquid_of(id) == k->liquid)) continue;
        out->block = (uint16_t)(id | meta << 12);
        out->dist = (uint16_t)(t * 256.0 < 65534.0 ? floor(t * 256.0) : 65534.0);
        out->face = (uint8_t)face;
        return;
    }
}

static int cast(const struct semcam_spec *sp, struct session *ss, struct semcam_cell *out, int fast)
{
    size_t n = (size_t)sp->w * (size_t)sp->h;
    struct world *w = ss->client_world;
    if (w == NULL)
    {
        for (size_t i = 0; i < n; ++i) out[i] = (struct semcam_cell){0, 0xffff, SC_MISS, 0, 0};
        return -1;
    }
    struct client_player *cp = &ss->cp;
    struct cast k;
    memset(&k, 0, sizeof k);
    k.w = w;
    k.ox = cp->e.pos_x;
    /* EntityRenderer.orientCamera's yOffset - 1.62 (0 standing) */
    k.oy = cp->e.pos_y + (double)(cp->e.y_offset - 1.62F);
    k.oz = cp->e.pos_z;
    k.range = sp->range;
    k.ccx = k.ccz = INT32_MIN;
    k.csy = -1;
    int m0 = 0, ex = (int)floor(k.ox), ey = (int)floor(k.oy), ez = (int)floor(k.oz);
    k.liquid = ey >= 0 && ey < 256 ? liquid_of(cell_ref(&k, ex, ey, ez, &m0)) : 0;
    if (fast)
    {
        /* the highest loaded section a ray within RANGE can reach */
        int r = sp->range + 1;
        int cx0 = (int)floor(k.ox - r) >> 4, cx1 = (int)floor(k.ox + r) >> 4;
        int cz0 = (int)floor(k.oz - r) >> 4, cz1 = (int)floor(k.oz + r) >> 4;
        for (int cx = cx0; cx <= cx1; ++cx)
            for (int cz = cz0; cz <= cz1; ++cz)
            {
                struct chunk *c = world_chunk(w, cx, cz);
                if (c != NULL && c->mask != 0)
                {
                    int t = 16 * (32 - __builtin_clz((unsigned)c->mask));
                    if (t > k.top) k.top = t;
                }
            }
    }
    struct basis b;
    basis(sp, cp->rotation_yaw, cp->rotation_pitch, &b);
    for (int j = 0; j < sp->h; ++j)
        for (int i = 0; i < sp->w; ++i)
        {
            double d[3];
            ray(sp, &b, i, j, d);
            /* two instances: the reference's reads and the fast path's */
            if (fast) walk(&k, d, 1, &out[(size_t)j * (size_t)sp->w + (size_t)i]);
            else walk(&k, d, 0, &out[(size_t)j * (size_t)sp->w + (size_t)i]);
        }
    return 0;
}

int semcam_cast(const struct semcam_spec *sp, struct session *ss, struct semcam_cell *out) { return cast(sp, ss, out, 1); }

int semcam_cast_ref(const struct semcam_spec *sp, struct session *ss, struct semcam_cell *out) { return cast(sp, ss, out, 0); }
