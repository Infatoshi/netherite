/* lane/rod: the dig walker, PRIVILEGED (its plan reads the server's blocks): to a target cell along a 1x2 tunnel the
 * tunnel planner lays over the server world of the player's dimension (netherview_oracle_path_dim, NV_PATH_TUNNEL:
 * solid dug cheap, open cells dear, no lava, fire or water beside or over a cell), each next cell's blocks dug top
 * first (the best tool held), then walked into to its middle. The Nether's tunnel home (nether.c nt_digger) is the same
 * walk with the Nether's own fallbacks; this one is the plain walk the overworld's macros share (armor.c).
 *
 * dgw_step: 1 the feet in the target cell, 0 under way (the act set), -1 no way (DGW_FAILS plans in a row with no
 * step taken, or none found). The target cell itself is dug into: an ore as the target is mined by the walk in */
#define DGW_AGE 1500            /* a plan made again after this many steps */
#define DGW_GAP 40              /* and at most this often */
#define DGW_FAILS 6
#define DGW_WALK 40             /* steps to reach a cell's middle before the next plan */
#define DGW_STILL 20            /* steps without moving on a walk: no crouch and a jump */

/* lane/supply (debugging only): a DGW_TRACE build prints, for env DGW_TRACE_ENV, each step dgw_step returns 0 with no
 * walk under way: DGW step feet target plan-length k fails why cell block (why: noplan, offplan, bad, nofloor) */
#if defined(DGW_TRACE) && !defined(DGW_TRACE_ENV)
#define DGW_TRACE_ENV 0
#endif
#ifdef DGW_TRACE
#define DGW_T(why, cx, cy, cz, b)                                                                                  \
    do {                                                                                                           \
        if (e == v->e + DGW_TRACE_ENV)                                                                             \
            fprintf(stderr, "DGW %lld %d %d %d -> %d %d %d n %d k %d fails %d %s %d %d %d %d\n", (long long)e->steps, \
                    fx, fy, fz, tx, ty, tz, w->n, w->k, w->fails, why, cx, cy, cz, b);                             \
    } while (0)
#else
#define DGW_T(why, cx, cy, cz, b) ((void)0)
#endif

static void dgw_reset(struct dig_walk *w)
{
    memset(w, 0, sizeof *w);
    w->t = -1;
}

/* the block in the client world (raw: lava the player has not seen is lava still; the walker is privileged) */
static int dgw_block(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z)
{
    return netherview_block(v->p, (int)(e - v->e), x, y, z, NULL);
}

static int dgw_hot(int b) { return b == 8 || b == 9 || b == 10 || b == 11 || b == 51; }

/* lane/supply (dig_bridge): cobblestone in the hotbar (what nt_cover_place places from) */
static int dgw_cobble(const struct neth_rl_state *s)
{
    for (int i = 0; i < 9; ++i)
        if (s->inv[i].item == COBBLE && s->inv[i].count > 0) return 1;
    return 0;
}

static int dgw_open(int b) { return b == 0 || b == 31 || b == 32 || b == 37 || b == 38 || b == 39 || b == 40 || b == 78 ||
                                    b == 50 || b == 90 || b == 106; }

/* lane/supply (dig_wade): from the feet's cell (in water), the first cell of the shortest walk on the feet's level
 * through cells with water or air at the feet, a solid floor and the head in air, to the nearest such cell whose feet
 * are in air with no lava beside, or a step up onto the shore (a solid block at the feet's level with air over it two
 * high, and head room over the jump): 1 with the first cell in (*nx, *ny, *nz), 0 none within DGW_WADE_R across */
#define DGW_WADE_R 8
static int nt_cover_place(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                          const struct neth_rl_state *s, struct neth_rl_act *a, double ey, int tx, int ty, int tz, int depth,
                          int material);   /* nether.c (dig_bridge) */
static int dgw_wade_up(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z, int nx, int nz)
{
    int b = dgw_block(v, e, nx, y, nz);
    return b > 0 && !dgw_open(b) && !dgw_hot(b) && dgw_open(dgw_block(v, e, nx, y + 1, nz)) &&
           dgw_open(dgw_block(v, e, nx, y + 2, nz)) && dgw_open(dgw_block(v, e, x, y + 2, z));
}
static int dgw_wade(const struct neth_vec *v, const struct vec_env *e, int fx, int fy, int fz, int *nx, int *ny,
                    int *nz)
{
    enum { S = 2 * DGW_WADE_R + 1 };
    int16_t par[S * S], q[S * S];
    for (int i = 0; i < S * S; ++i) par[i] = -1;
    int qh = 0, qt = 0, c0 = DGW_WADE_R * S + DGW_WADE_R;
    par[c0] = (int16_t)c0, q[qt++] = (int16_t)c0;
    while (qh < qt)
    {
        int c = q[qh++], cx = fx + c % S - DGW_WADE_R, cz = fz + c / S - DGW_WADE_R;
        int feet = dgw_block(v, e, cx, fy, cz);
        if (c != c0 && dgw_open(feet))
        {
            int hot = 0;
            for (int k = 0; k < 4 && !hot; ++k)
            {
                int b = dgw_block(v, e, cx + NDX[k], fy, cz + NDZ[k]);
                hot = b == 10 || b == 11 || b == 51;
            }
            if (!hot)
            {
                while (par[c] != c0) c = par[c];
                *nx = fx + c % S - DGW_WADE_R, *ny = fy, *nz = fz + c / S - DGW_WADE_R;
                return 1;
            }
        }
        for (int k = 0; k < 4; ++k)
        {
            int ix = c % S + NDX[k], iz = c / S + NDZ[k];
            if (ix < 0 || ix >= S || iz < 0 || iz >= S || par[iz * S + ix] >= 0) continue;
            int x = fx + ix - DGW_WADE_R, z = fz + iz - DGW_WADE_R;
            if (dgw_wade_up(v, e, cx, fy, cz, x, z))
            {
                /* the shore one up: the walk's first cell (this one, from the feet's cell) */
                if (c == c0) *nx = x, *ny = fy + 1, *nz = z;
                else
                {
                    while (par[c] != c0) c = par[c];
                    *nx = fx + c % S - DGW_WADE_R, *ny = fy, *nz = fz + c / S - DGW_WADE_R;
                }
                return 1;
            }
            int fb = dgw_block(v, e, x, fy, z), fl = dgw_block(v, e, x, fy - 1, z), hd = dgw_block(v, e, x, fy + 1, z);
            if (!(fb == 8 || fb == 9 || dgw_open(fb)) || fl <= 0 || dgw_open(fl) || dgw_hot(fl) || !dgw_open(hd))
                continue;
            par[iz * S + ix] = (int16_t)c, q[qt++] = (int16_t)(iz * S + ix);
        }
    }
    return 0;
}

static int dgw_step(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                    struct neth_rl_act *a, double ey, struct dig_walk *w, int tx, int ty, int tz)
{
    int id = (int)(e - v->e);
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z);
    if (fx == tx && fy == ty && fz == tz) return 1;
    if (w->tx != tx || w->ty != ty || w->tz != tz || w->t < 0)
    {
        int plans = w->plans, cells = w->cells;
        dgw_reset(w);
        w->plans = plans, w->cells = cells;
        w->tx = tx, w->ty = ty, w->tz = tz, w->replan = 1;
    }
    double yaw, pitch;
    if (!r->on_ground)
    {
        /* mid-jump or mid-drop: on toward the cell walked into */
        still(a);
        if (w->have)
        {
            look_at(r->x, ey, r->z, w->ax + 0.5, ey, w->az + 0.5, &yaw, &pitch);
            a->look_mode = 1;
            a->look[0] = (float)yaw, a->look[1] = 10.0f;
            a->hold = 1u << PK_FORWARD;
            /* lane/supply (dig_wade): afloat in the water (never on the ground there), a step up onto the shore is
             * swum up to: the jump held too */
            int fb = dgw_block(v, e, fx, fy, fz);
            if (MOPT(MO_DIG_WADE) && (fb == 8 || fb == 9) && w->ay > fy) a->hold |= 1u << PK_JUMP;
        }
        return 0;
    }
    if (w->walk > 0)
    {
        /* a step begun is walked to the cell's middle (nether.c's dig-9lt: the next cell chosen at a cell's edge, the
         * player still on the old floor) */
        double cx = w->ax + 0.5, cz = w->az + 0.5;
        if (fabs(r->x - cx) > 0.3 || fabs(r->z - cz) > 0.3)
        {
            w->walk -= 1;
            still(a);
            look_at(r->x, ey, r->z, cx, ey, cz, &yaw, &pitch);
            a->look_mode = 1;
            a->look[0] = (float)yaw, a->look[1] = 10.0f;
            a->hold = 1u << PK_FORWARD;
            if (w->ay > fy) a->hold |= 1u << PK_JUMP;
            else if (w->ay == fy && w->walk > DGW_STILL) a->hold |= 1u << PK_SNEAK;
            if (fabs(r->x - w->wx) < 0.01 && fabs(r->z - w->wz) < 0.01) w->still += 1;
            else w->still = 0, w->wx = r->x, w->wz = r->z;
            if (w->still > DGW_STILL / 2) a->hold = (a->hold & ~(1u << PK_SNEAK)) | (1u << PK_JUMP);
            return 0;
        }
        w->walk = 0;
    }
    /* the plan: advanced past the feet's cell; made again when it ends, ages or a step of it was refused */
    for (int j = w->k - 1 < 0 ? 0 : w->k - 1; j < w->n && j <= w->k + 3; ++j)
        if (w->path[j][0] == fx && w->path[j][1] == fy && w->path[j][2] == fz) w->k = j + 1, w->fails = 0;
    int stale = w->n == 0 || w->k >= w->n || e->steps - w->t > DGW_AGE || w->replan;
    if (stale && (w->t < 0 || e->steps - w->t >= DGW_GAP))
    {
        if (++w->fails > DGW_FAILS) return -1;
        w->plans += 1, w->t = e->steps, w->k = 0, w->replan = 0;
        /* lane/supply (dig_bridge): with no cobblestone in the hotbar to place, a plan that bridges is not made */
        int nb = MOPT(MO_DIG_BRIDGE) && !dgw_cobble(s) ? NV_PATH_NOBRIDGE : 0;
        w->n = netherview_oracle_path_dim(v->p, id, r->dim, fx, fy, fz, tx, ty, tz, 0.5,
                                          NV_PATH_BUILD | NV_PATH_EXACT | NV_PATH_TUNNEL | nb, w->path, DGW_PATH);
        if (w->n < 0) w->n = 0;
    }
    still(a);
    if (w->k >= w->n)
    {
        DGW_T("noplan", 0, 0, 0, -1);
#ifdef DGW_TRACE
        /* the first plan for a target that found no way: the client world's blocks round the feet (x, z +-2, y -1 ..
         * +2) and round the target (+-1, -1 .. +2), a line a layer, z rows west to east */
        if (e == v->e + DGW_TRACE_ENV && w->plans >= 1 && w->fails == 1 && e->steps == w->t)
            for (int part = 0; part < 2; ++part)
            {
                int cx = part ? tx : fx, cy = part ? ty : fy, cz = part ? tz : fz, rr = part ? 1 : 2;
                for (int y = cy - 1; y <= cy + 2; ++y)
                {
                    fprintf(stderr, "DGWB %s %d %d %d y %d:", part ? "target" : "feet", cx, cy, cz, y);
                    for (int z = cz - rr; z <= cz + rr; ++z)
                    {
                        fputs(" |", stderr);
                        for (int x = cx - rr; x <= cx + rr; ++x) fprintf(stderr, " %d", dgw_block(v, e, x, y, z));
                    }
                    fputc('\n', stderr);
                }
            }
#endif
        /* lane/supply (dig_wade): no way from a flooded floor: the wade's next cell walked into (the walk above takes
         * it to the cell's middle), the plans' count begun again */
        int fb0 = dgw_block(v, e, fx, fy, fz), wx = 0, wy = fy, wz = 0;
        int wade = MOPT(MO_DIG_WADE) && (fb0 == 8 || fb0 == 9) && dgw_wade(v, e, fx, fy, fz, &wx, &wy, &wz);
        if (MOPT(MO_DIG_WADE) && e->steps == w->t) DGW_T(wade ? "wade" : "nowade", wx, wy, wz, fb0);
        if (wade)
        {
            w->have = 1, w->ax = wx, w->ay = wy, w->az = wz, w->walk = DGW_WALK, w->fails = 0, w->replan = 1;
            look_at(r->x, ey, r->z, wx + 0.5, ey, wz + 0.5, &yaw, &pitch);
            a->look_mode = 1;
            a->look[0] = (float)yaw, a->look[1] = 10.0f;
            a->hold = 1u << PK_FORWARD;
            if (wy > fy) a->hold |= 1u << PK_JUMP;
            return 0;
        }
        return 0;
    }
    const int16_t *c = w->path[w->k];
    int ax = c[0], ay = c[1], az = c[2];
    if (abs(ax - fx) + abs(az - fz) != 1 || abs(ay - fy) > 1)
    {
        DGW_T("offplan", ax, ay, az, -1);
        w->replan = 1;          /* off the plan (a fall, a push): a new one */
        return 0;
    }
    /* the cells to open: the feet's and head's ahead, and a stair's third (over the landing going down, over the head
     * going up); a half block under the feet (a slab, a stair) reaches the cell 2 over the feet ahead too */
    int cells[4][3], nc = 2;
    cells[0][0] = ax, cells[0][1] = ay + 1, cells[0][2] = az;
    cells[1][0] = ax, cells[1][1] = ay, cells[1][2] = az;
    if (ay < fy)
    {
        cells[0][1] = ay + 2, cells[1][1] = ay + 1;
        cells[2][0] = ax, cells[2][1] = ay, cells[2][2] = az, nc = 3;
    }
    else if (ay > fy)
    {
        cells[0][0] = fx, cells[0][1] = fy + 2, cells[0][2] = fz;
        cells[1][0] = ax, cells[1][1] = ay + 1, cells[1][2] = az;
        cells[2][0] = ax, cells[2][1] = ay, cells[2][2] = az, nc = 3;
    }
    if (r->y - floor(r->y) > 0.2 && ay <= fy)
    {
        for (int i = nc; i > 0; --i) memcpy(cells[i], cells[i - 1], sizeof cells[i]);
        cells[0][0] = ax, cells[0][1] = fy + 2, cells[0][2] = az, nc += 1;
    }
    /* each cell: nothing hot or wet in it, beside it or over it (sand or gravel over falls in and is dug again, but
     * not over the player's own column) */
    for (int i = 0; i < nc; ++i)
    {
        int b = dgw_block(v, e, cells[i][0], cells[i][1], cells[i][2]);
        int bad = b < 0 || b == 7 || b == OBSIDIAN || dgw_hot(b);
        for (int k = 0; k < 4 && !bad; ++k) bad = dgw_hot(dgw_block(v, e, cells[i][0] + NDX[k], cells[i][1], cells[i][2] + NDZ[k]));
        int u = dgw_block(v, e, cells[i][0], cells[i][1] + 1, cells[i][2]);
        if (!bad) bad = dgw_hot(u) || ((u == 12 || u == 13) && cells[i][0] == fx && cells[i][2] == fz);
        if (bad)
        {
            DGW_T("bad", cells[i][0], cells[i][1], cells[i][2], b * 10000 + (u & 4095));
            w->replan = 1;
            return 0;
        }
    }
    /* dig the first cell not open, top first */
    for (int i = 0; i < nc; ++i)
    {
        int b = dgw_block(v, e, cells[i][0], cells[i][1], cells[i][2]);
        if (dgw_open(b)) continue;
        look_at(r->x, ey, r->z, cells[i][0] + 0.5, cells[i][1] + 0.5, cells[i][2] + 0.5, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
        if (e->steps % 50 == 0) DGW_T("dig", cells[i][0], cells[i][1], cells[i][2], b);
        int tb = dig_tool(v, s, b);
        if (tb >= 0 && tb != s->current) a->hotbar = tb;
        a->hold = (1u << PK_ATTACK) | (1u << PK_SNEAK);
        return 0;
    }
    /* open: the walk in (no floor: the planner's bridge is not built here, a new plan) */
    int fl = dgw_block(v, e, ax, ay - 1, az);
    if (fl < 0 || dgw_open(fl) || dgw_hot(fl))
    {
        DGW_T("nofloor", ax, ay - 1, az, fl);
        /* lane/supply (dig_bridge): the plan's bridge (the planner's NV_TUN_OPEN floor over air) placed, cobblestone
         * from the hotbar against a solid face of the cell (nether.c nt_cover_place); the plan kept */
        if (MOPT(MO_DIG_BRIDGE) && fl >= 0 && dgw_open(fl) && dgw_cobble(s) &&
            nt_cover_place(v, e, r, s, a, ey, ax, ay - 1, az, 0, COBBLE))
            return 0;
        w->replan = 1;
        return 0;
    }
    w->have = 1, w->ax = ax, w->ay = ay, w->az = az, w->walk = DGW_WALK, w->cells += 1;
    look_at(r->x, ey, r->z, ax + 0.5, ey, az + 0.5, &yaw, &pitch);
    a->look_mode = 1;
    a->look[0] = (float)yaw, a->look[1] = 10.0f;
    a->hold = 1u << PK_FORWARD;
    if (ay > fy) a->hold |= 1u << PK_JUMP;
    else if (ay == fy) a->hold |= 1u << PK_SNEAK;
    return 0;
}
