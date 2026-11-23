/* lane/rod: snow_kit (MO_SNOW_KIT, off by default; on: PRIVILEGED, for lane/chain's search). Snowballs for the End's
 * crystals (any thrown projectile breaks one; lane/shold's dragon_crystals snowball throws them): in the overworld under
 * SN_WANT snowballs, a wooden shovel made on a table when no shovel is held (sn_plan: C_WSHOVEL, one plank and two
 * sticks; vanilla's snow drops a snowball only to a shovel), then the nearest snow layer (1 snowball) or snow block (4)
 * in the client world within SN_R across walked to along the dig walker's tunnel (dig.c) and dug with the shovel: a
 * block is the walk's last cell (the walk digs it), a layer is walked onto and dug at the feet. The shovel is kept.
 * No snow within SN_R: nothing (lane/chain's walk to the stronghold crosses more ground; it asks again every SN_SCAN
 * steps from wherever the player is). Books: snowballs held, layers and blocks dug, the steps of the shovel, 16 and 64 */
#define SN_WANT (MOPT(MO_SNOW_TARGET) == 2 ? 16 : MOPT(MO_SNOW_TARGET) == 1 ? 32 : 64)
#define SN_R 48
#define SN_DY 24                /* the scan's height round the feet */
#define SN_SURF_LO 56           /* and the surface's band, wherever the feet are */
#define SN_SURF_HI 110
#define SN_SCAN 40              /* the cand's look for snow, steps */
#define SN_HEAL_HP 16.0f        /* snow_heal: the health the trip waits for while the player heals */
#define SN_GIVEUP 1500          /* steps on one cell before it is given up */
/* lane/snowleg: snow_first (the stronghold waits) and snow_kit far (the trip to the nearest snowy biome; the wood for
 * the shovel). The trip's legs are lane/pearlhome's (pearl.c ph_travel) over lane/shold's sho_follow */
#define SN_FIRST_MAX 60000      /* steps the stronghold waits for the snowballs at most */
#define SN_FAR_R 1536           /* the snowy column's search, blocks round the feet */
#define SN_HEIGHT_R 256         /* the height-aware alternative's nearby search, blocks round the feet */
#define SN_HEIGHT_STEP 16       /* at most this much rise requested at each horizontal leg */
#define SN_COL_HEIGHT_BASE 256  /* sn_col >= this keeps the high target's standing y without new sidecar bytes */
#define SN_LEG 40               /* a leg's goal this far ahead toward the column */
#define SN_LEG_MOVE 8           /* the goal made again when the ahead point moved this far */
#define SN_STRAIGHT 60          /* steps straight at the column when a leg has no way */
#define SN_ARRIVE 6.0           /* the column reached within this with no snow in the scan: on SN_HOP further */
#define SN_HOP 64               /* ... along the trip's heading, SN_HOPS times, then the trip given up */
#define SN_HOPS 4
#define SN_DUSK 1500            /* ticks before night (13000) the trip stands on land, to the morning (SN_DAWN) */
#define SN_NIGHT0 13000
#define SN_DAWN 23200
#define SN_TRIP_SCAN 20         /* the snow scan's interval on the trip, steps */
#define SN_LOG_R 8              /* a log's search across and up or down, round the feet: the trip's way side only (32
                                 * and 10 tunnelled 5,000 steps toward trunks from node 806's y 48 and dug none) */
#define SN_LOG_DY 3
#define SN_FREE 8               /* empty inventory slots kept (wood, a table, sticks, a shovel, four stacks of snowballs) */
#define SN_KEEP_COBBLE 128      /* cobblestone kept when the surplus is thrown (towers, bridges, the portal room) */

static int sn_on(const struct neth_vec *v, const struct vec_last *r)
{
    return MOPT(MO_SNOW_KIT) && r->dim == 0 && !(r->flags & PF_DEAD);
}

static int sn_shovel(const struct neth_rl_state *s)
{
    return inv_count(s, WOOD_SHOVEL) + inv_count(s, STONE_SHOVEL) + inv_count(s, IRON_SHOVEL) +
               inv_count(s, DIAMOND_SHOVEL) > 0;
}

/* the hotbar slot of a shovel (the best first), -1 none there (snow-32a: best_tool's choice dug 216 cells with no drop:
 * vanilla's snow requires the tool, Material.snow setRequiresTool) */
static int sn_tool(const struct neth_rl_state *s)
{
    static const int SH[4] = {DIAMOND_SHOVEL, IRON_SHOVEL, STONE_SHOVEL, WOOD_SHOVEL};
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i < 9; ++i)
            if (s->inv[i].item == SH[k]) return i;
    return -1;
}

/* craft.c's craft_plan: the wooden shovel on a table held or near */
static int sn_plan(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                   const struct neth_rl_state *s, int64_t planks, int64_t sticks, int64_t tables)
{
    if (!sn_on(v, r) || sn_shovel(s) || inv_count(s, SNOWBALL) >= SN_WANT) return -1;
    /* lane/snowleg (far): the table from the planks when none is held or near (a table placed long ago, has_table,
     * keeps craft_plan's own table plan off) */
    if (MOPT(MO_SNOW_KIT) == SNK_FAR && tables == 0 && !near_table(e, r) && planks >= 5 + (sticks >= 2 ? 0 : 2))
        return C_TABLE;
    if (planks < 1 || sticks < 2 || (tables == 0 && !near_table(e, r))) return -1;
    return C_WSHOVEL;
}

/* lane/snowleg (far): no shovel and too little wood for one (a plank and two sticks, and a table's four planks with
 * none held or near; a log four planks) */
static int sn_wood_short(const struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s)
{
    int64_t logs = inv_count(s, LOG) + inv_count(s, LOG2), planks = inv_count(s, PLANKS), sticks = inv_count(s, STICK);
    int table = inv_count(s, TABLE) > 0 || near_table(e, r);
    int64_t need = (table ? 1 : 5) + (sticks >= 2 ? 0 : 2);
    return planks + 4 * logs < need;
}

static int sn_bad(const struct arm_env *m, int x, int y, int z)
{
    for (int i = 0; i < m->sn_nbad && i < 8; ++i)
        if (m->sn_bad[i][0] == x && m->sn_bad[i][1] == y && m->sn_bad[i][2] == z) return 1;
    return 0;
}

/* the nearest snow layer or block with a floor under it (a layer) and nothing hot round it, not given up: 1 found */
static int sn_scan(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r)
{
    struct arm_env *m = &e->arm;
    int id = (int)(e - v->e), fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z), found = 0;
    double bd = 1e30;
    /* round the feet and the surface's band always (snow-32d: 13 of 21 envs short of 64 ended at y 2 to 31, dug down by
     * the other macros, the snow at y 60 to 70 out of a scan round the feet) */
    int ylo = fy - SN_DY < SN_SURF_LO ? fy - SN_DY : SN_SURF_LO, yhi = fy + SN_DY > SN_SURF_HI ? fy + SN_DY : SN_SURF_HI;
    if (ylo < 1) ylo = 1;
    if (yhi > 250) yhi = 250;
    for (int y = ylo; y <= yhi; ++y)
        for (int dz = -SN_R; dz <= SN_R; ++dz)
            for (int dx = -SN_R; dx <= SN_R; ++dx)
            {
                int x = fx + dx, z = fz + dz, b = netherview_block(v->p, id, x, y, z, NULL);
                if (b != SNOW_LAYER && b != SNOW_BLOCK) continue;
                double d = (double)dx * dx + (double)dz * dz + 4.0 * (double)(y - fy) * (y - fy);
                if (d >= bd || sn_bad(m, x, y, z)) continue;
                int hot = 0;
                for (int k = 0; k < 27 && !hot; ++k)
                    hot = dgw_hot(netherview_block(v->p, id, x + k % 3 - 1, y + k / 3 % 3 - 1, z + k / 9 - 1, NULL)) &&
                          k != 13;
                if (hot) continue;
                bd = d, found = 1;
                m->sx = x, m->sy = y, m->sz = z;
            }
    return found;
}

/* a new snow cell (sn_scan's): its walks begun again, the surface's first */
static void sn_target(struct vec_env *e)
{
    struct arm_env *m = &e->arm;
    m->sn_has = 1, m->sn_t0 = e->steps, m->sn_tun = 0;
    dgw_reset(&m->sdw);
    memset(&m->swt, 0, sizeof m->swt);
}

static void sn_give_up(struct arm_env *m)
{
    int i = m->sn_nbad < 8 ? m->sn_nbad++ : m->sn_gave_up % 8;
    m->sn_bad[i][0] = m->sx, m->sn_bad[i][1] = m->sy, m->sn_bad[i][2] = m->sz;
    m->sn_gave_up += 1, m->sn_has = 0;
}

static int sn_want(const struct neth_vec *v, const struct vec_last *r, const struct neth_rl_state *s)
{
    return sn_on(v, r) && sn_shovel(s) && inv_count(s, SNOWBALL) < SN_WANT;
}

/* lane/snowleg: sn_want, or (far) the wood for the shovel to get */
static int sn_want2(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                    const struct neth_rl_state *s)
{
    if (sn_want(v, r, s)) return 1;
    return MOPT(MO_SNOW_KIT) == SNK_FAR && sn_on(v, r) && !e->arm.sn_quit && !sn_shovel(s) &&
           inv_count(s, SNOWBALL) < SN_WANT && sn_wood_short(e, r, s);
}

/* lane/snowleg (snow_first): the stronghold macro's latch held back for the snowballs (stronghold.c sh_want): 1 while
 * snow_kit is not off, the overworld, alive, under SN_WANT snowballs and the snow macro has not given up; the wait's
 * first step kept, given up past SN_FIRST_MAX steps, and with snow_kit on when no shovel can be made from the wood */
static int sn_holds(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s)
{
    struct arm_env *m = &e->arm;
    if (!MOPT(MO_SNOW_FIRST) || MOPT(MO_SNOW_KIT) == SNK_OFF || !sn_on(v, r) || m->sn_quit ||
        inv_count(s, SNOWBALL) >= SN_WANT)
        return 0;
    if (m->sn_w0 < 0) m->sn_w0 = e->steps;
    if (e->steps - m->sn_w0 > SN_FIRST_MAX) m->sn_quit = 1;
    if (MOPT(MO_SNOW_KIT) == SNK_ON && !sn_shovel(s) && sn_wood_short(e, r, s)) m->sn_quit = 1;
    return !m->sn_quit;
}

/* lane/snowleg (snow_first): the wait begun (sn_holds) and on, the stronghold not latched: stronghold.c's sh_owns, so
 * the strip, the seek and strip2's errands stand down as they do for the stronghold (far2-s1: 11 of 13 first deaths
 * on steps another macro drove, digging down to y 24 to 33 in the snow country while the snow run was given back) */
static int sn_waits(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    const struct arm_env *m = &e->arm;
    return MOPT(MO_SNOW_FIRST) && MOPT(MO_SNOW_KIT) != SNK_OFF && r->dim == 0 && !(r->flags & PF_DEAD) &&
           m->sn_w0 >= 0 && !m->sn_quit && m->n_snow < SN_WANT && !e->sh.go;
}

/* lane/snowleg (far): the trip's step toward the nearest snowy column (found once: shoracle_snow_column), in legs of
 * SN_LEG along sho_follow (stronghold.c; its way state e->sh's, which the stronghold's own walk makes again for its
 * target), straight at it for SN_STRAIGHT steps when a leg has no way, stood on land from SN_DUSK before night to the
 * morning. 0 it drove, 1 over (no column, or reached with no snow found: given up) */
static int sn_trip(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                   struct neth_rl_act *a)
{
    struct arm_env *m = &e->arm;
    int id = (int)(e - v->e);
    /* State 2 can arrive in a saved node from an older search. Scan it once before
     * retaining that far target; state 3 means this option already made the choice. */
    if (MOPT(MO_SNOW_HEIGHT) && (m->sn_col == 0 || m->sn_col == 1 || m->sn_col == 2))
    {
        int hx, hy, hz;
        if (shoracle_snow_height_column(v->p, id, r->x, r->z, SN_HEIGHT_R, &hx, &hy, &hz) == 0)
        {
            double hd = hypot(hx + 0.5 - r->x, hz + 0.5 - r->z);
            double old = m->sn_col > 0 ? hypot(m->sn_cx + 0.5 - r->x, m->sn_cz + 0.5 - r->z) : 1e9;
            if (hd < old)
            {
                m->sn_col = SN_COL_HEIGHT_BASE + hy;
                m->sn_cx = hx, m->sn_cz = hz, m->sn_col_d = (int)hd;
                double d = hd > 0 ? hd : 1.0;
                m->sn_ux = (int)lround(1000.0 * (hx + 0.5 - r->x) / d);
                m->sn_uz = (int)lround(1000.0 * (hz + 0.5 - r->z) / d);
                m->sn_leg = m->sn_straight = 0;
                e->sh.nop = 0, e->sh.onew = 1, e->sh.ofail = 0;
            }
            else m->sn_col = 3; /* keep the nearer cold target; high scan done */
        }
        else if (m->sn_col == 1 || m->sn_col == 2) m->sn_col = 3;
    }
    if (m->sn_col == 0)
    {
        int tx, tz;
        if (shoracle_snow_column(v->p, id, r->x, r->z, SN_FAR_R, &tx, &tz) == 0)
        {
            m->sn_col = MOPT(MO_SNOW_HEIGHT) ? 3 : 1;
            m->sn_cx = tx, m->sn_cz = tz, m->sn_col_d = (int)hypot(tx + 0.5 - r->x, tz + 0.5 - r->z);
            double d = m->sn_col_d > 0 ? (double)m->sn_col_d : 1.0;
            m->sn_ux = (int)lround(1000.0 * (tx + 0.5 - r->x) / d), m->sn_uz = (int)lround(1000.0 * (tz + 0.5 - r->z) / d);
        }
        else
            m->sn_col = -1;
    }
#ifdef SN_TRACE
    if (e->steps % 1000 == 0)
        fprintf(stderr, "SN_TARGET step=%lld state=%d target=%d,%d high_y=%d saved_distance=%d current_distance=%.1f snow=%lld pos=%.1f,%.1f,%.1f\n",
                (long long)e->steps, m->sn_col, m->sn_cx, m->sn_cz,
                m->sn_col >= SN_COL_HEIGHT_BASE ? m->sn_col - SN_COL_HEIGHT_BASE : 0,
                m->sn_col_d, hypot(m->sn_cx + 0.5 - r->x, m->sn_cz + 0.5 - r->z),
                (long long)inv_count(s, SNOWBALL), r->x, r->y, r->z);
#endif
    if (m->sn_col < 0)
    {
        m->sn_quit = 1;
        return 1;
    }
    if (m->t_trip0 < 0) m->t_trip0 = e->steps;
    m->sn_trip_steps += 1;
    m->sn_part = 2;
    still(a);
    double dh = hypot(m->sn_cx + 0.5 - r->x, m->sn_cz + 0.5 - r->z);
    int high = m->sn_col >= SN_COL_HEIGHT_BASE;
    int high_y = high ? m->sn_col - SN_COL_HEIGHT_BASE : 0;
    if (dh < SN_ARRIVE && (!high || r->y >= high_y - 3))
    {
        /* there and no snow in the scan (dug out, or none on this column's ground): on along the heading, deeper into
         * the biome, SN_HOPS times; then given up (far-s2: 5 envs gave up there holding 8 to 40) */
        if (m->sn_hops >= SN_HOPS)
        {
            m->sn_quit = 1;
            return 1;
        }
        m->sn_hops += 1;
        m->sn_cx += (int)lround(SN_HOP * m->sn_ux / 1000.0), m->sn_cz += (int)lround(SN_HOP * m->sn_uz / 1000.0);
        m->sn_leg = 0;
        dh = hypot(m->sn_cx + 0.5 - r->x, m->sn_cz + 0.5 - r->z);
    }
    int64_t t = ((r->wtime % 24000) + 24000) % 24000;
    int wet = s->eye_water || sho_water_at(v, e, (int)floor(r->x), (int)floor(r->y + 0.1), (int)floor(r->z));
    if (t >= SN_NIGHT0 - SN_DUSK && t < SN_DAWN && r->on_ground && !wet)
    {
        /* the night stood out on land: the look level, round slowly */
        a->look_mode = 1;
        a->look[0] = r->yaw + 2.0f;
        a->look[1] = 0.0f;
        m->sn_leg = 0;
        return 0;
    }
    if (m->sn_straight > 0)
    {
        m->sn_straight -= 1;
        double yaw, pitch;
        look_at(r->x, r->y + 1.62, r->z, m->sn_cx + 0.5, r->y + 1.62, m->sn_cz + 0.5, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = 0.0f;
        a->hold = 1u << PK_FORWARD;
        if (wet || !r->on_ground) a->hold |= 1u << PK_JUMP;
        else if (r->food > 6) a->hold |= 1u << PK_SPRINT;
        if (m->sn_straight == 0) e->sh.nop = 0, e->sh.onew = 1, e->sh.ofail = 0, m->sn_leg = 0;
        return 0;
    }
    double ux = (m->sn_cx + 0.5 - r->x) / dh, uz = (m->sn_cz + 0.5 - r->z) / dh;
    double leg = dh < SN_LEG ? dh : SN_LEG;
    int lx = (int)floor(r->x + ux * leg), lz = (int)floor(r->z + uz * leg), ly = sho_feet_y(r);
    if (high && ly < high_y)
    {
        ly += SN_HEIGHT_STEP;
        if (ly > high_y) ly = high_y;
    }
    if (!m->sn_leg || abs(lx - m->sn_lx) + abs(lz - m->sn_lz) > SN_LEG_MOVE)
        m->sn_leg = 1, m->sn_lx = lx, m->sn_ly = ly, m->sn_lz = lz;
    int rc = sho_follow(v, e, r, s, a, m->sn_lx, m->sn_ly, m->sn_lz, 3.0, SHO_ANYY);
    if (rc != 0) m->sn_leg = 0;
    if (rc < 0)
    {
        m->sn_noway += 1;
        m->sn_straight = SN_STRAIGHT;
    }
    return 0;
}

/* lane/snowleg (far): the wood errand's step: the nearest trunk foot log (shoracle_log) dug into along the dig walker
 * (its cell the walk's last: the feet end in it, the drop picked up); none within SN_LOG_R: the trip goes on (the cold
 * taiga's spruces are on the way). 0 it drove, 1 over */
static int sn_wood(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                   struct neth_rl_act *a, double ey)
{
    struct arm_env *m = &e->arm;
    int id = (int)(e - v->e);
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z);
    if (m->sn_log)
    {
        int b = shoracle_block(v->p, id, m->sn_lgx, m->sn_lgy, m->sn_lgz, NULL);
        if (b != 17 && b != 162 && fx == m->sn_lgx && fy == m->sn_lgy && fz == m->sn_lgz) m->sn_log = 0, m->sn_logs += 1;
        else if (e->steps - m->sn_lg_t0 > SN_GIVEUP)
        {
            int i = m->sn_nbad < 8 ? m->sn_nbad++ : m->sn_gave_up % 8;
            m->sn_bad[i][0] = m->sn_lgx, m->sn_bad[i][1] = m->sn_lgy, m->sn_bad[i][2] = m->sn_lgz;
            m->sn_gave_up += 1, m->sn_log = 0;
        }
    }
    if (!m->sn_log && e->steps % SN_TRIP_SCAN == 0)
    {
        int c[3];
        if (shoracle_log(v->p, id, fx, fy, fz, SN_LOG_R, SN_LOG_DY, (const int(*)[3])m->sn_bad,
                         m->sn_nbad < 8 ? m->sn_nbad : 8, c) == 0)
        {
            m->sn_log = 1, m->sn_lgx = c[0], m->sn_lgy = c[1], m->sn_lgz = c[2], m->sn_lg_t0 = e->steps;
            dgw_reset(&m->sdw);
        }
    }
    if (!m->sn_log) return sn_trip(v, e, r, s, a);
    int w = dgw_step(v, e, r, s, a, ey, &m->sdw, m->sn_lgx, m->sn_lgy, m->sn_lgz);
    if (w < 0)
    {
        m->sn_lg_t0 = e->steps - SN_GIVEUP - 1;    /* given up at the next step */
        still(a);
    }
    return 0;
}

/* a shovel in the main inventory and none in the hotbar: swapped into the hotbar slot the macros need least (craft.c
 * fetch_item's rule: no tool, no craft material, no food) by a number key's click under the inventory screen (snow-t7: the
 * kit's eight stacks and the sticks filled the hotbar, the crafted shovel went to the pack, 216 cells dug by hand with no
 * drop). 1 swapping (the act set), 0 nothing to do */
static int sn_fetch(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                    struct neth_rl_act *a)
{
    struct arm_env *m = &e->arm;
    if (sn_tool(s) >= 0) return 0;
    int src = -1;
    for (int i = 9; i < 36 && src < 0; ++i)
        if (s->inv[i].item == WOOD_SHOVEL || s->inv[i].item == STONE_SHOVEL || s->inv[i].item == IRON_SHOVEL ||
            s->inv[i].item == DIAMOND_SHOVEL)
            src = i;
    if (src < 0) return 0;
    if (r->screen == PS_INVENTORY)
    {
        int to = 8, best = 1 << 30;
        for (int i = 8; i >= 0; --i)
        {
            int it = s->inv[i].item, itc = it < 0 ? 0 : it > 4095 ? 4095 : it, score = 0;
            if (it >= 0 && s->inv[i].count > 0)
            {
                int tool = v->T.pick[itc] || v->T.axe[itc] || v->T.shovel[itc] || it == 268 || it == 272 ||
                           it == 267 || it == 276 || it == 283;
                int food = it == 364 || it == 320 || it == 366 || it == 297 || it == 260;
                score = tool ? 100 : food ? 50 : v->T.craft[itc] || v->T.craft_iron[itc] || it == COAL || it == TABLE ? 10 : 1;
            }
            if (score < best) best = score, to = i;
        }
        a->nops = 2;
        a->ops[0] = click(inv_slot(src, 0), to, 2);
        a->ops[1] = CLOSE;
        m->sn_press = 0;
        return 1;
    }
    if (r->screen == PS_NONE)
    {
        if (m->sn_press++ % 40 == 0) a->press[PK_INVENTORY] = 1;   /* (again after 40 steps unopened) */
        return 1;
    }
    return 0;
}

/* lane/snowleg (far): room for the wood, the shovel and the snowballs (16 a stack: four slots for 64). Under SN_FREE
 * empty slots, the surplus thrown out of the inventory screen, three stacks a screen (strip2.c's errand: NETH_RL_OPS 8),
 * the look turned away from the column first (the throw goes where the look is, and the walk goes the other way): dirt,
 * gravel, saplings, netherrack, flowers, rotten flesh, string, gunpowder, bones, seeds, spider eyes, redstone, then
 * cobblestone past SN_KEEP_COBBLE, the smallest stacks first (lane/chain's node 806: 36 of 36 slots full, the logs dug
 * never picked up, 411 of them over 32 envs). 1 it took the step */
static int sn_junk(int it)
{
    return it == 3 || it == 13 || it == 6 || it == 87 || it == 37 || it == 38 || it == 367 || it == 287 || it == 289 ||
           it == 352 || it == 295 || it == 375 || it == 331;
}

static int sn_surplus(const struct neth_rl_state *s, int *out, int max)
{
    int n = 0;
    for (int i = 0; i < 36 && n < max; ++i)
        if (s->inv[i].count > 0 && sn_junk(s->inv[i].item)) out[n++] = i;
    int64_t extra = inv_count(s, COBBLE) - SN_KEEP_COBBLE;
    while (n < max && extra > 0)
    {
        int best = -1;
        for (int i = 0; i < 36; ++i)
        {
            if (s->inv[i].item != COBBLE || s->inv[i].count <= 0 || s->inv[i].count > extra) continue;
            int dup = 0;
            for (int j = 0; j < n; ++j) dup |= out[j] == i;
            if (!dup && (best < 0 || s->inv[i].count < s->inv[best].count)) best = i;
        }
        if (best < 0) break;
        extra -= s->inv[best].count;
        out[n++] = best;
    }
    return n;
}

static int sn_room(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                   struct neth_rl_act *a)
{
    (void)v;
    struct arm_env *m = &e->arm;
    int free = 0, sl[3];
    for (int i = 0; i < 36; ++i) free += s->inv[i].item < 0 || s->inv[i].count <= 0;
    if (free >= SN_FREE || e->steps < m->sn_room_off) return 0;
    int k = sn_surplus(s, sl, 3);
    if (k == 0) return 0;
    if (r->screen == PS_INVENTORY)
    {
        int n = 0;
        for (int j = 0; j < k; ++j)
        {
            a->ops[n++] = click(inv_slot(sl[j], 0), 0, 0);
            a->ops[n++] = click(-999, 0, 0);
        }
        a->ops[n++] = CLOSE;
        a->nops = n;
        m->sn_press = 0, m->sn_thrown += k;
        m->sn_room_off = e->steps + 5;          /* never a screen every step */
        return 1;
    }
    if (r->screen != PS_NONE) return 0;
    a->look_mode = 1;
    a->look[0] = m->sn_col > 0 ? (float)(atan2(m->sn_cx + 0.5 - r->x, -(m->sn_cz + 0.5 - r->z)) * 180.0 / M_PI)
                               : r->yaw + 180.0f;   /* (look_at's yaw convention: away from the column) */
    a->look[1] = 0.0f;
    if (m->sn_press++ % 40 == 0) a->press[PK_INVENTORY] = 1;
    return 1;
}

/* the run: 1 over */
static int snow(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                struct neth_rl_act *a, double ey)
{
    struct arm_env *m = &e->arm;
    still(a);
    m->sn_age += 1;
    int far = MOPT(MO_SNOW_KIT) == SNK_FAR;
    if (!sn_want2(v, e, r, s)) return 1;
    /* lane/snowleg (far): the step given back to eat (the stronghold's rule), the wood for the shovel first */
    if (far && eat_want(v, r, s)) return 1;
    m->sn_last = e->steps, m->sn_part = 3;
    if (far && sn_room(v, e, r, s, a)) return 0;
    if (far && !sn_shovel(s))
    {
        m->sn_part = 1;
        return r->screen != PS_NONE ? 1 : sn_wood(v, e, r, s, a, ey);
    }
    if (sn_fetch(v, e, r, s, a)) return 0;
    if (r->screen != PS_NONE) return 1;
    int id = (int)(e - v->e);
    int b = m->sn_has ? netherview_block(v->p, id, m->sx, m->sy, m->sz, NULL) : -1;
    if (m->sn_has && b != SNOW_LAYER && b != SNOW_BLOCK) m->sn_has = 0, m->sn_dug += 1;     /* dug */
    if (m->sn_has && e->steps - m->sn_t0 > SN_GIVEUP) sn_give_up(m);
    if (!m->sn_has)
    {
        /* lane/snowleg (far): no snow in the scan, the trip (the scan every SN_TRIP_SCAN steps on it) */
        if (far && m->t_trip0 >= 0 && m->t_trip1 < 0 && e->steps % SN_TRIP_SCAN != 0) return sn_trip(v, e, r, s, a);
        if (!sn_scan(v, e, r)) return far ? sn_trip(v, e, r, s, a) : 1;
        if (far && m->t_trip0 >= 0 && m->t_trip1 < 0) m->t_trip1 = e->steps;
        sn_target(e);
        b = netherview_block(v->p, id, m->sx, m->sy, m->sz, NULL);
    }
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.13), fz = (int)floor(r->z);
    /* the feet's cell for the dig: a layer's own (stood on), a block's over it (stood on, dug at the feet) */
    int ty = b == SNOW_LAYER ? m->sy : m->sy + 1;
    if (fx == m->sx && fz == m->sz && fy >= m->sy && fy <= ty + 1 && r->on_ground)
    {
        /* there: dug at the feet, the shovel held */
        double yaw, pitch;
        look_at(r->x, ey, r->z, m->sx + 0.5, m->sy + (b == SNOW_LAYER ? 0.05 : 0.9), m->sz + 0.5, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = (float)pitch;
        int tb = sn_tool(s);
        if (tb >= 0 && tb != s->current) a->hotbar = tb;
        a->hold = 1u << PK_ATTACK;
        return 0;
    }
    if (!m->sn_tun)
    {
        /* the surface walk (vanilla's pathfinder, walk_to; snow-32e: the tunnel's price of an open cell dug under the
         * snow fields, 50 to 80 steps a cell); no path (the feet far under the surface): the tunnel for this cell */
        int w = walk_to(v, e, r, s, a, &m->swt, m->sx, ty, m->sz);
        if (w >= 0)
        {
            if (w == 1) m->sn_tun = 1;          /* walked, not there by the dig's rule: the tunnel's last steps */
            return 0;
        }
        m->sn_tun = 1;
        dgw_reset(&m->sdw);
        still(a);
        return 0;
    }
    int w = dgw_step(v, e, r, s, a, ey, &m->sdw, m->sx, m->sy, m->sz);
    /* a snow block dug on the way (the walk's last cell): the shovel held, not the walker's best tool */
    if (w == 0 && (a->hold & (1u << PK_ATTACK)) && sn_tool(s) >= 0 && s->mo_hit &&
        (s->mo_block == SNOW_BLOCK || s->mo_block == SNOW_LAYER))
        a->hotbar = sn_tool(s) != s->current ? sn_tool(s) : a->hotbar;
    if (w < 0) sn_give_up(m), still(a);
    return 0;
}

/* ------------------------------------------------------------ the mod (lane/macromod: macro.h's hooks) */

static void sn_m_observe(struct mac_step *c, struct neth_rl_act *a)
{
    (void)a;
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
#ifdef SN_TRACE                 /* debugging only (a VARIANT build: XFLAGS=-DSN_TRACE): every env each 100 steps */
    if (e->steps % 100 == 1) fprintf(stderr, "DBG t=%lld eyes=%lld snow=%lld dim=%d go=%d phase=%d kind=%d\n", (long long)e->steps, (long long)inv_count(c->s, EYE), (long long)inv_count(c->s, SNOWBALL), c->r->dim, e->sh.go, e->sh.phase, e->kind), fprintf(stderr, "DBG2 sn_on=%d arm_on=%d w0=%lld quit=%d col=%d trip0=%lld has=%d shovel=%d wood_short=%d pos=%.1f %.1f %.1f planks=%lld logs=%lld tables=%lld sticks=%lld cplan=%d near=%d has_table=%d sn_log=%d\n", e->arm.sn_on, e->arm.on, (long long)e->arm.sn_w0, e->arm.sn_quit, e->arm.sn_col, (long long)e->arm.t_trip0, e->arm.sn_has, sn_shovel(c->s), sn_wood_short(e, c->r, c->s), c->r->x, c->r->y, c->r->z, (long long)inv_count(c->s, PLANKS), (long long)(inv_count(c->s, LOG)+inv_count(c->s, LOG2)), (long long)inv_count(c->s, TABLE), (long long)inv_count(c->s, STICK), e->cplan, near_table(e, c->r), e->has_table, e->arm.sn_log);
#endif
    if (!MOPT(MO_SNOW_KIT)) return;
    struct arm_env *m = &e->arm;
    m->n_snow = (int)inv_count(c->s, SNOWBALL);
    if (m->t_shovel < 0 && sn_shovel(c->s)) m->t_shovel = e->steps;
    if (m->t_snow16 < 0 && m->n_snow >= 16) m->t_snow16 = e->steps;
    if (m->t_snow64 < 0 && m->n_snow >= 64) m->t_snow64 = e->steps;
    /* lane/snowleg: the End reached, the snowballs held then */
    if (m->t_end < 0 && c->r->dim == 1) m->t_end = e->steps, m->sn_end_snow = m->n_snow;
    /* the first death: who drove the step before, where, the air */
    if ((c->r->flags & PF_DEAD) && !m->sn_dead)
    {
        m->sn_dead = 1, m->sn_dstep = e->steps, m->sn_dago = (int)(e->steps - m->sn_last);
        m->sn_dpart = m->sn_last >= 0 && e->steps - m->sn_last <= 2 ? m->sn_part : 0;
        m->sn_dpos[0] = (int)floor(c->r->x), m->sn_dpos[1] = (int)floor(c->r->y), m->sn_dpos[2] = (int)floor(c->r->z);
        m->sn_dair = c->r->air;
    }
}

/* 1 when the trip may not begin yet (the cand's rules; lane/bportal snow_gate start: the start's too) */
static int sn_held(const struct mac_step *c)
{
    /* lane/chain: after the twelve pearls (eyes) with the pearl macro on: the snowballs are the stronghold walk's
     * (the orchestrator, 5:46 am: armor, then pearls, then snowballs on the way) */
    if ((c->v->macros & NETH_VEC_PEARL) && inv_count(c->s, PEARL) + inv_count(c->s, EYE) < 12) return 1;
    /* lane/chain: snow_heal, the trip not begun under SN_HEAL_HP while the player heals (the bar regenerating, or
     * food held that the food macro eats at 17 and under with health missing) */
    const struct neth_vec *v = c->v;
    return MOPT(MO_SNOW_HEAL) && c->r->health < SN_HEAL_HP &&
           (c->r->food >= 18 || ((v->macros & NETH_VEC_FOOD) && food_held(v, c->s, c->r->food)));
}

static int sn_m_cand(struct mac_step *c)
{
    struct vec_env *e = c->e;
    if (e->kind == M_MINE && e->arm.sn_on) return 1;
    if (sn_held(c)) return 0;
    return c->r->screen == PS_NONE && e->steps % SN_SCAN == 0 && sn_want2(c->v, e, c->r, c->s);
}

/* the run begun (both entries'): a snow cell from the scan, or (far) the trip or the wood with none in it; with
 * snow_kit on and snow_first's wait, no snow in the scan gives the wait up */
static int sn_begin(struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    struct arm_env *m = &e->arm;
    int far = MOPT(MO_SNOW_KIT) == SNK_FAR;
    if (!m->sn_has && sn_shovel(c->s))
    {
        if (sn_scan(v, e, c->r)) sn_target(e);
        else if (!far)
        {
            if (MOPT(MO_SNOW_FIRST) && m->sn_w0 >= 0) m->sn_quit = 1;   /* lane/snowleg: nothing to wait for */
            return 0;
        }
    }
    e->kind = M_MINE;
    m->sn_on = 1, m->sn_age = 0;
    return 1;
}

static int sn_m_start(struct mac_step *c)
{
    if (c->r->screen != PS_NONE || c->e->steps % SN_SCAN != 0 || !sn_want2(c->v, c->e, c->r, c->s)) return 0;
    /* lane/bportal (snow_gate start): the cand's rules on the start too: the start loop runs when any macro's cand is
     * open, so without it the trip began with no pearl held, between the obsidian macro's jobs (seed 1's obsidian
     * start: its night stand spun under ground at y 10 to 11 to the limit in 17 of 64 attempts) */
    const struct neth_vec *v = c->v;
    if (MOPT(MO_SNOW_GATE) && sn_held(c)) return 0;
    return sn_begin(c);
}

/* lane/snowleg (snow_first): the start in the stronghold's place (rank 10), while sn_holds keeps its latch back: the
 * twelve eyes held (the stronghold's SH_START_EYES) and the run wanted */
static int sn_first_want(struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    if (!MOPT(MO_SNOW_FIRST) || !(v->macros & NETH_VEC_STRONGHOLD) || c->r->screen != PS_NONE) return 0;
    if (inv_count(c->s, EYE) < SH_START_EYES || c->e->sh.go) return 0;
    return sn_holds(v, c->e, c->r, c->s) && sn_want2(v, c->e, c->r, c->s);
}

static int sn_m_first_cand(struct mac_step *c) { return sn_first_want(c); }

/* lane/snowleg (snow_first): a step the policy's keys reach the game in the overworld before the stronghold latches,
 * with an eye of ender or a snowball in the hand: its use (a throw) and its drop let go (the stronghold's
 * sh_policy_keys rule, for the wait: far-s2, four envs threw or dropped all twelve eyes on the trip) */
static void sn_m_first_policy(struct mac_step *c, struct neth_rl_act *a)
{
    const struct neth_vec *v = c->v;
    const struct neth_rl_state *s = c->s;
    if (!MOPT(MO_SNOW_FIRST) || MOPT(MO_SNOW_KIT) == SNK_OFF || c->r->dim != 0 || c->e->sh.go) return;
    int cur = a->hotbar >= 0 && a->hotbar < 9 ? a->hotbar : s->current;
    if (cur < 0 || cur >= 9 || s->inv[cur].count <= 0 || (s->inv[cur].item != EYE && s->inv[cur].item != SNOWBALL))
        return;
    a->hold &= ~(1u << PK_USE);
    a->press[PK_USE] = 0;
    a->press[PK_DROP] = 0;
}

static int sn_m_first_start(struct mac_step *c) { return sn_first_want(c) && sn_begin(c); }

static int sn_m_drive(struct mac_step *c, struct neth_rl_act *a)
{
    struct vec_env *e = c->e;
    if (e->kind != M_MINE || !e->arm.sn_on) return -1;
    return snow(c->v, e, c->r, c->s, a, c->ey);
}

static void sn_m_end(struct vec_env *e)
{
    /* lane/snowleg: a trip's legs ran on the stronghold's way state: its failures not the stronghold's */
    if (e->arm.sn_on && e->arm.t_trip0 >= 0) e->sh.nop = 0, e->sh.onew = 1, e->sh.ofail = 0;
    e->arm.sn_on = 0;
    e->arm.sn_leg = 0, e->arm.sn_straight = 0;
}

static void sn_m_reset(struct vec_env *e)
{
    /* (armor's reset clears e->arm first: it is earlier in the table) */
    dgw_reset(&e->arm.sdw);
    e->arm.t_shovel = e->arm.t_snow16 = e->arm.t_snow64 = -1;
    e->arm.sn_w0 = e->arm.t_trip0 = e->arm.t_trip1 = e->arm.t_end = -1;     /* lane/snowleg */
    e->arm.sn_end_snow = -1;
    e->arm.sn_last = e->arm.sn_dstep = -1;
}

static const struct vec_macro MAC_SNOW = {
    .name = "snow", .bit = 0, .observe = sn_m_observe, .cand = sn_m_cand, .start = sn_m_start, .drive = sn_m_drive,
    .end = sn_m_end, .abort = sn_m_end, .reset = sn_m_reset,
};

/* lane/snowleg (snow_first): the second entry, its start and candidate test only (the run is MAC_SNOW's) */
static const struct vec_macro MAC_SNOW_FIRST = {
    .name = "snow_first", .bit = 0, .cand = sn_m_first_cand, .policy = sn_m_first_policy, .start = sn_m_first_start,
};
