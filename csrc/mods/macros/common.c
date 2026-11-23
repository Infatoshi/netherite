/* the macros' shared helpers (lane/macromod: cut from vecenv.c, unchanged): the meat, the chests tried, the table and furnace at hand, the looks at a block, the counts held, the best tool, lava as a macro may know it */

/* lane/food's: the meats, raw then cooked (pork, beef, chicken) */
static const int16_t MEATS[6] = {319, 363, 365, 320, 364, 366};

static double py_dist3(double px, double py, double pz, double qx, double qy, double qz)
{
    double d[3] = {fabs(px - qx), fabs(py - qy), fabs(pz - qz)}, max = 0.0;
    for (int i = 0; i < 3; ++i)
        if (d[i] > max) max = d[i];
    return vector_norm(3, d, max);
}

/* lane/food: the meat held */
static int64_t meat_count(const struct neth_rl_state *s)
{
    int64_t n = 0;
    for (int i = 0; i < 36; ++i)
        for (int j = 0; j < 6; ++j)
            if (s->inv[i].item == MEATS[j]) n += s->inv[i].count;
    return n;
}

/* lane/chain: obsidian_carry: the obsidian macro's want ends at OB_KEEP_N held (OB_CARRY_N with the option: the portal
 * home carried; 10 without) */
#define OB_CARRY_N 20
/* (frame, 2: the first frame at 10, as off; fallback, 3: as on until ob_observe latches fb_on, then 10) */
#define OB_KEEP_N (MOPT(MO_OB_CARRY) == 1 || (MOPT(MO_OB_CARRY) == 3 && !e->ob.fb_on) ? OB_CARRY_N : 10)

static int64_t tried_get(const struct vec_env *e, int x, int y, int z)
{
    for (int i = 0; i < e->ntried; ++i)
        if (e->tried[i].x == x && e->tried[i].y == y && e->tried[i].z == z) return e->tried[i].until;
    return 0;
}

static void tried_set(struct vec_env *e, int x, int y, int z, int64_t until)
{
    int i = 0;
    while (i < e->ntried && !(e->tried[i].x == x && e->tried[i].y == y && e->tried[i].z == z)) ++i;
    if (i == e->ntried)
    {
        if (e->ntried == TRIED) i = TRIED - 1;  /* never: a chest comes from the scans' 64 */
        else ++e->ntried;
    }
    e->tried[i].x = x;
    e->tried[i].y = y;
    e->tried[i].z = z;
    e->tried[i].until = until;
}

static int near_table(const struct vec_env *e, const struct vec_last *r)
{
    return e->has_table && py_dist3(r->x, r->y, r->z, e->tx, e->ty, e->tz) < 4.0;
}

static int near_furnace(const struct vec_env *e, const struct vec_last *r)
{
    return e->has_furnace && py_dist3(r->x, r->y, r->z, e->fx, e->fy, e->fz) < 4.0;
}

/* NETH_VEC_IRON: the look at block (bx, by, bz)'s face nearest the eye
 * (lane/placefix): beside the player its side at the feet's height (a
 * block's centre in a one-high alcove is behind the block above it), under or
 * over it its top or bottom */
static void look_block(const struct vec_last *r, double ey, int bx, int by, int bz, double *yaw, double *pitch)
{
    double x = r->x < bx + 0.05 ? bx + 0.05 : r->x > bx + 0.95 ? bx + 0.95 : r->x;
    double z = r->z < bz + 0.05 ? bz + 0.05 : r->z > bz + 0.95 ? bz + 0.95 : r->z;
    double y = by + 0.5;
    if (bx == (int)floor(r->x) && bz == (int)floor(r->z))
        y = ey > by + 1 ? by + 0.95 : by + 0.05;
    else
        y = ey < by + 0.15 ? by + 0.15 : ey > by + 0.5 ? by + 0.5 : ey;
    look_at(r->x, ey, r->z, x, y, z, yaw, pitch);
}

/* NETH_VEC_IRON: the k-th way of looking at a block until the crosshair is
 * on it (something else may stand in one line of sight): its centre, its
 * face nearest the eye, its top's centre */
static void look_try(const struct vec_last *r, double ey, int bx, int by, int bz, int k, double *yaw, double *pitch)
{
    if (k % 3 == 1)
        look_block(r, ey, bx, by, bz, yaw, pitch);
    else
        look_at(r->x, ey, r->z, bx + 0.5, k % 3 == 0 ? by + 0.5 : by + 0.95, bz + 0.5, yaw, pitch);
}

/* macros._best_tool */
static int best_tool(const struct vec_tabs *T, const struct neth_rl_state *s, int block)
{
    int tb = block >= 0 && block < 4096 ? T->tool_of[block] : -1;
    if (tb < 0) return -1;
    const int8_t *tab = tb == 0 ? T->axe : tb == 1 ? T->shovel : T->pick;
    int best = -1, bt = 0;
    for (int i = 0; i < 9; ++i)
    {
        int it = s->inv[i].item, t = it >= 0 && it < 4096 ? tab[it] : 0;
        if (t > bt)
        {
            best = i;
            bt = t;
        }
    }
    return best;
}

/* lane/armed (dig_tool): the dig walker's and the armour reach's tool for block b. best: best_tool; cheap, for a block
 * a pickaxe mines: the hotbar's cheapest pickaxe that mines it (strip2.c st_pick_r's rank: stone first, iron and
 * diamond last), else best_tool */
static int st_pick_r(const struct vec_tabs *T, const struct neth_rl_state *s, int need, int i0, int i1, int *rank);
static int dig_tool(const struct neth_vec *v, const struct neth_rl_state *s, int b)
{
    if (!MOPT(MO_DIG_TOOL) || b < 0 || b > 4095 || v->T.tool_of[b] != 2) return best_tool(&v->T, s, b);
    int h = st_pick_r(&v->T, s, v->T.picky[b] > 0 ? v->T.picky[b] : 1, 0, 9, NULL);
    return h >= 0 ? h : best_tool(&v->T, s, b);
}

/* lane/macroopts: lava as a macro may know it (lava_sense, MOPTS; the obsidian, bportal and nether macros' reads of
 * the client world). Lava counts only as the player could see it: view, the eye's line to the cell's middle meets no
 * other block first; light, also when its glow reaches an open cell beside it that the eye sees (its lit face); near,
 * every cell (the light-and-sound rule before: Elliot, 2026-11-02, no macro uses sound). The line has no view cone (a
 * cell behind the player is one a turn of the head shows). b: the cell's block as the client world has it; returns b,
 * or 0 (air: unknown, no floor, never walked onto) for lava not seen. Every other block passes as it is */
static int lava_seen_1(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z)
{
    /* its middle, then just under its top (a pool's cell seen from above past its neighbours, whose sources stop the
     * line to its middle; the ray's box of a source is the whole cell) */
    const struct vec_last *r = &e->last;
    static const double H[2] = {0.5, 0.98};
    for (int k = 0; k < 2; ++k)
    {
        int bx, by, bz, b, m;
        int rc = shview_ray(v->p, (int)(e - v->e), r->x, r->y + 1.62, r->z, x + 0.5, y + H[k], z + 0.5, &bx, &by, &bz,
                            &b, &m);
        if (rc == 0 || (rc == 1 && bx == x && by == y && bz == z)) return 1;
    }
    return 0;
}

static int lava_seen(const struct neth_vec *v, const struct vec_env *e, int x, int y, int z, int b)
{
    if ((b != 10 && b != 11) || MOPT(MO_LAVA_SENSE) == LS_NEAR || lava_seen_1(v, e, x, y, z)) return b;
    if (MOPT(MO_LAVA_SENSE) == LS_LIGHT)
    {
        static const int N[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (int k = 0; k < 6; ++k)
            if (netherview_block(v->p, (int)(e - v->e), x + N[k][0], y + N[k][1], z + N[k][2], NULL) == 0 &&
                lava_seen_1(v, e, x + N[k][0], y + N[k][1], z + N[k][2]))
                return b;
    }
    return 0;
}


/* lane/chain (nether_portal_home): the frame's site in the Nether, a quarter turn k (0..3: yaw 0, 90, 180, 270; the one
 * the player faces tried first) whose build stays clear of lava: no lava as the macros may know it (lava_seen: the run
 * file's lava_sense) a block around the cells the build digs, places and stands in, no portal or obsidian there (an
 * arrival frame left whole), and a floor to stand and place on under the frame and the feet's row; -1 none. Its cells
 * are portal.c's: the frame 2 ahead of the feet's cell, r across the look (-1..2), h up from the feet (0..4) */
static int pt_site(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    static const int dx[4] = {0, -1, 0, 1}, dz[4] = {1, 0, -1, 0};    /* yaw 0, 90, 180, 270 (portal_build's) */
    int id = (int)(e - v->e), fx = (int)floor(r->x), fy = (int)floor(r->y + 1e-3), fz = (int)floor(r->z);
    int k0 = ((int)lround((double)r->yaw / 90.0) % 4 + 4) % 4;
    for (int j = 0; j < 4; ++j)
    {
        int k = (k0 + j) % 4, ok = 1;
        int ffx = dx[k], ffz = dz[k], rx = -dz[k], rz = dx[k];
        for (int fo = -1; fo <= 3 && ok; ++fo)
            for (int ro = -2; ro <= 3 && ok; ++ro)
                for (int h = -1; h <= 6 && ok; ++h)
                {
                    int x = fx + fo * ffx + ro * rx, y = fy + h, z = fz + fo * ffz + ro * rz;
                    int b = obsview_block(v->p, id, x, y, z, NULL);
                    /* under the feet's level only the floor counts (lava below beside it touches no cell built in) */
                    int floor = fo >= 0 && fo <= 2 && ro >= -1 && ro <= 2;
                    if (h == -1 && !floor) continue;
                    int lb = b == 10 || b == 11 ? lava_seen(v, e, x, y, z, b) : 0;   /* (0: lava not known) */
                    if (b < 0 || lb == 10 || lb == 11) ok = 0;
                    else if (fo >= 0 && (b == 90 || (b == OBSIDIAN && h >= 0 && h <= 4 && ro >= -1 && ro <= 2)))
                        ok = 0;
                    else if (h == -1 && fo >= 0 && fo <= 2 && ro >= -1 && ro <= 2 &&
                             (b == 0 || (b >= 8 && b <= 11) || b == 51))
                        ok = 0;
#ifdef PP_TRACE
                    if (!ok && e->steps < 2)
                        fprintf(stderr, "PPSITE %d k %d fails at fo %d ro %d h %d: block %d (%d %d %d)\n", id, k, fo, ro,
                                h, b, x, y, z);
#endif
                }
        if (ok) return k;
    }
    return -1;
}

/* lane/chain: nether_portal_home: in the Nether with seven rods' worth (lane/chain's rod_eq: rods + (powder + eyes +
 * 1) / 2), a portal's 10 obsidian, its 7 cobblestone (the frame's corners and the tower) and the flint and steel held,
 * no build given up in the last 200 steps and a site clear of lava (pt_site), the portal macro builds the way home
 * there (the Nether macro yields to it) */
#define NT_PH_HP 16.0f
static int nt_portal_home(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                          const struct neth_rl_state *s)
{
    if (!MOPT(MO_NT_PORTAL_HOME) || !(v->macros & NETH_VEC_PORTAL) || r->dim != -1 || (r->flags & PF_DEAD) ||
        e->steps < e->pp_retry)
        return 0;
    int64_t rod_eq = inv_count(s, BLAZE_ROD) + (inv_count(s, BLAZE_POWDER) + inv_count(s, EYE) + 1) / 2;
#ifdef PP_TRACE
    if (e->steps % 500 == 0 || e->steps < 4)
        fprintf(stderr, "PPWANT %d %ld rods %ld obs %ld cobble %ld fs %ld site %d kind %d at %.1f %.1f %.1f\n",
                (int)(e - v->e), (long)e->steps, (long)rod_eq, (long)inv_count(s, OBSIDIAN), (long)inv_count(s, COBBLE),
                (long)inv_count(s, FLINT_STEEL), pt_site(v, e, r), e->kind, r->x, r->y, r->z);
#endif
    /* healed first: under NT_PH_HP the Nether macro keeps the player (its hood heals a fed one: e42t, easy, the fed
     * line's rod_7 states at 6 to 9 health, food 18 to 20, 151 of 151 builds dead at the spawner, 119 burning) */
    return rod_eq >= 7 && inv_count(s, OBSIDIAN) >= 10 && inv_count(s, COBBLE) >= 7 && inv_count(s, FLINT_STEEL) > 0 &&
           r->health >= NT_PH_HP && pt_site(v, e, r) >= 0;
}
