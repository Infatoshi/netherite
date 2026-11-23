/* The semantic camera's cast in one scalar (semcam.c includes this twice: SC_R double, SC_N(x) x##64, the reference;
 * SC_R float, SC_N(x) x##32, the FP32 instance, raycast.h's definition). SC_K(x) is a literal in the scalar, SC_FP 64
 * or 32. FP64 is the cast as it was: the eye's world position in double. FP32: the eye as its cell e and its offset
 * o = fl(w - e) (raycast.c setup's, o == 1 the next cell's 0), every coordinate a cell's (i - e) less o in float. */

/* the camera's axes: forward f, right r and up u = r x f */
struct SC_N(basis) { SC_R fx, fy, fz, rx, ry, rz, ux, uy, uz, aspect; };

static void SC_N(basis_make)(const struct semcam_spec *sp, float yaw, float pitch, struct SC_N(basis) *b)
{
    /* Entity.getLook: (f2 * f3, f4, f1 * f3) */
    float f1 = mh_cos(-yaw * 0.017453292F - 3.1415927F);
    float f2 = mh_sin(-yaw * 0.017453292F - 3.1415927F);
    float f3 = -mh_cos(-pitch * 0.017453292F);
    float f4 = mh_sin(-pitch * 0.017453292F);
    b->fx = (SC_R)(f2 * f3), b->fy = (SC_R)f4, b->fz = (SC_R)(f1 * f3);
    /* right: horizontal, (-cos yaw, 0, -sin yaw) */
    b->rx = (SC_R)f1, b->ry = SC_K(0.0), b->rz = (SC_R)-f2;
    b->ux = b->ry * b->fz - b->rz * b->fy;
    b->uy = b->rz * b->fx - b->rx * b->fz;
    b->uz = b->rx * b->fy - b->ry * b->fx;
    b->aspect = (SC_R)sp->w / (SC_R)sp->h;
}

/* ray (i, j): through the centre of pixel i of row j, unit length */
static inline void SC_N(ray)(const struct semcam_spec *sp, const struct SC_N(basis) *b, int i, int j, SC_R *d)
{
    SC_R sy = (SC_K(1.0) - SC_K(2.0) * ((SC_R)j + SC_K(0.5)) / (SC_R)sp->h) * SC_TAN_HALF;
    SC_R sx = (SC_K(2.0) * ((SC_R)i + SC_K(0.5)) / (SC_R)sp->w - SC_K(1.0)) * SC_TAN_HALF * b->aspect;
    SC_R dx = b->fx + sx * b->rx + sy * b->ux, dy = b->fy + sx * b->ry + sy * b->uy, dz = b->fz + sx * b->rz + sy * b->uz;
    SC_R len = SC_SQRT(dx * dx + dy * dy + dz * dz);
    d[0] = dx / len;
    d[1] = dy / len;
    d[2] = dz / len;
}

/* one cast's shared state */
struct SC_N(cast) {
    struct world *w;
    SC_R ox, oy, oz;            /* the eye in the frame's coordinates (FP32: its offset in its cell) */
    int bx, by, bz;             /* the frame's origin cell: FP64 0, FP32 the eye's */
    int ex, ey, ez;             /* the eye's cell */
    int liquid;                 /* the eye's liquid: 8 water, 10 lava, 0 none */
    int range;
    int top;                    /* fast: no loaded section within reach reaches y top or above */
    /* fast: the last cell's chunk and band */
    int ccx, ccz, csy;
    struct chunk *c;
    const struct chunk_sec *s;
};

/* the cell's id | meta << 12, the reference's way (each cell through the
 * world's chunk lookup and chunk_get_block) */
static int SC_N(cell_ref)(struct SC_N(cast) *k, int x, int y, int z, int *meta)
{
    struct chunk *c = world_chunk(k->w, x >> 4, z >> 4);
    if (c == NULL) return 0;
    int id = chunk_get_block(c, x & 15, y, z & 15);
    *meta = id ? chunk_get_meta(c, x & 15, y, z & 15) : 0;
    return id;
}

/* the fast path's: the chunk and the band of the last cell kept */
static inline int SC_N(cell_fast)(struct SC_N(cast) *k, int x, int y, int z, int *meta)
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
static inline __attribute__((always_inline)) void SC_N(walk)(struct SC_N(cast) *k, const SC_R *d, int fast,
                                                             struct semcam_cell *out)
{
    SC_R dx = d[0], dy = d[1], dz = d[2];
    int ix = k->ex, iy = k->ey, iz = k->ez;
    int sx = dx > 0 ? 1 : dx < 0 ? -1 : 0, sy = dy > 0 ? 1 : dy < 0 ? -1 : 0, sz = dz > 0 ? 1 : dz < 0 ? -1 : 0;
    SC_R tdx = sx ? SC_FABS(SC_K(1.0) / dx) : INFINITY, tdy = sy ? SC_FABS(SC_K(1.0) / dy) : INFINITY,
         tdz = sz ? SC_FABS(SC_K(1.0) / dz) : INFINITY;
    /* the first boundary on each axis: (c(i) + 1 - o) / d going +, (c(i) - o) / d going - (c(i) = i - b) */
    SC_R tx = sx > 0 ? ((SC_R)(ix - k->bx) + SC_K(1.0) - k->ox) / dx : sx < 0 ? ((SC_R)(ix - k->bx) - k->ox) / dx : INFINITY;
    SC_R ty = sy > 0 ? ((SC_R)(iy - k->by) + SC_K(1.0) - k->oy) / dy : sy < 0 ? ((SC_R)(iy - k->by) - k->oy) / dy : INFINITY;
    SC_R tz = sz > 0 ? ((SC_R)(iz - k->bz) + SC_K(1.0) - k->oz) / dz : sz < 0 ? ((SC_R)(iz - k->bz) - k->oz) / dz : INFINITY;
    SC_R range = (SC_R)k->range;
    out->block = 0;
    out->dist = 0xffff;
    out->face = SC_MISS;
    out->flags = k->liquid ? SCF_EYE_LIQUID : 0;
    out->pad = 0;
    if (fast && iy >= k->top && sy >= 0) return;
    for (;;)
    {
        SC_R t;
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
            out->dist = (uint16_t)(t * SC_K(256.0) < SC_K(65534.0) ? SC_FLOOR(t * SC_K(256.0)) : SC_K(65534.0));
            return;
        }
        if (fast && iy >= k->top && sy >= 0) return;
        int meta = 0;
        int id = fast ? SC_N(cell_fast)(k, ix, iy, iz, &meta) : SC_N(cell_ref)(k, ix, iy, iz, &meta);
        if (id == 0 || (k->liquid && liquid_of(id) == k->liquid)) continue;
        out->block = (uint16_t)(id | meta << 12);
        out->dist = (uint16_t)(t * SC_K(256.0) < SC_K(65534.0) ? SC_FLOOR(t * SC_K(256.0)) : SC_K(65534.0));
        out->face = (uint8_t)face;
        return;
    }
}

static int SC_N(cast_run)(const struct semcam_spec *sp, struct session *ss, struct semcam_cell *out, int fast)
{
    size_t n = (size_t)sp->w * (size_t)sp->h;
    struct world *w = ss->client_world;
    if (w == NULL)
    {
        for (size_t i = 0; i < n; ++i) out[i] = (struct semcam_cell){0, 0xffff, SC_MISS, 0, 0};
        return -1;
    }
    struct client_player *cp = &ss->cp;
    struct SC_N(cast) k;
    memset(&k, 0, sizeof k);
    k.w = w;
    /* EntityRenderer.orientCamera's yOffset - 1.62 (0 standing) */
    const double w3[3] = {cp->e.pos_x, cp->e.pos_y + (double)(cp->e.y_offset - 1.62F), cp->e.pos_z};
    int e3[3];
    SC_R o3[3];
    for (int a = 0; a < 3; ++a)
    {
        e3[a] = (int)floor(w3[a]);
#if SC_FP == 32
        o3[a] = (float)(w3[a] - (double)e3[a]);
        if (o3[a] >= 1.0f) e3[a] += 1, o3[a] = 0.0f;
#else
        o3[a] = w3[a];
#endif
    }
    k.ox = o3[0], k.oy = o3[1], k.oz = o3[2];
    k.ex = e3[0], k.ey = e3[1], k.ez = e3[2];
#if SC_FP == 32
    k.bx = k.ex, k.by = k.ey, k.bz = k.ez;
#endif
    k.range = sp->range;
    k.ccx = k.ccz = INT32_MIN;
    k.csy = -1;
    int m0 = 0;
    k.liquid = k.ey >= 0 && k.ey < 256 ? liquid_of(SC_N(cell_ref)(&k, k.ex, k.ey, k.ez, &m0)) : 0;
    if (fast)
    {
        /* the highest loaded section a ray within RANGE can reach */
        int r = sp->range + 1;
        int cx0 = (int)floor(w3[0] - r) >> 4, cx1 = (int)floor(w3[0] + r) >> 4;
        int cz0 = (int)floor(w3[2] - r) >> 4, cz1 = (int)floor(w3[2] + r) >> 4;
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
    struct SC_N(basis) b;
    SC_N(basis_make)(sp, cp->rotation_yaw, cp->rotation_pitch, &b);
    for (int j = 0; j < sp->h; ++j)
        for (int i = 0; i < sp->w; ++i)
        {
            SC_R d[3];
            SC_N(ray)(sp, &b, i, j, d);
            /* two instances: the reference's reads and the fast path's */
            if (fast) SC_N(walk)(&k, d, 1, &out[(size_t)j * (size_t)sp->w + (size_t)i]);
            else SC_N(walk)(&k, d, 0, &out[(size_t)j * (size_t)sp->w + (size_t)i]);
        }
    return 0;
}
