/* lane/wood (actions.wood_search, off by default): the first log from any spawn.
 *
 * lane/fwd's forward panel (64 fresh worlds from spawn, the composite baseline) lost 23 of 64 before a wooden pickaxe:
 * with no pickaxe at all no macro wants wood (seek_want's wood is tier >= 1, lane/pick's switch had no run-file name),
 * so a log came only from the random driver's own attack on a trunk, and no macro walks to a log it has not seen.
 *
 * The rule, what a player does with an empty hand: while no pickaxe of any kind is held, no cobblestone, ore or ingot
 * (the first pickaxe only: lane/pick's switch is the one for a broken one) and too little wood for the table, sticks and
 * a wooden pickaxe:
 *   - a log in view (the cast's seen record, kind 3: block 17, and 162 too with seen_log2 on) not given up on: the seek's
 *     own walk and dig to it (seek_begin, the seek's give-ups and tried cells);
 *   - none: look (wood_search look and walk) a turn on the spot, 10 degrees a step at the level look (the food scout's
 *     sweep), so the cast sees round the player; walk (wood_search walk) then a leg along a heading, the legs a widening
 *     square spiral (WD_LEG, the k-th leg WD_LEG * (1 + k / 2)), a turn on the spot at each leg's end, the heading turned
 *     45 degrees where a step does not move, a quarter turn before a drop over WD_DROP blocks or lava ahead; water swum
 *     across at the top (the jump held) as the food scout does. Nothing is dug.
 * The run gives the step back every WD_RUN steps (the starts before it in order, the swim, the fight and eating, have
 * their turn), with the eye in water under WD_AIR air (the swim's), and with a hostile in view (the fight's).
 *
 * Inputs, each one a player has: the inventory and the player's own place, look, air and screen (neth_rl_state, vec_last:
 * the screen and F3); the cast's seen record (the frame's pixels: rlbind.h neth_rl_state seen); the vis list's livings
 * (never through a wall); and on the leg, the client world's cells in the column one block ahead of the feet, from the
 * feet's level down WD_DROP + 1 (the ground the player is about to step on, as the strip's walk judges its floor first,
 * obsview_block). No server read, no census, no world generator. */
#define WD_SWEEP 36             /* steps of a turn on the spot (10 degrees a step) */
#define WD_LEG 24.0             /* the spiral's first legs, blocks; the k-th leg WD_LEG * (1 + k / 2) */
#define WD_LEGS 24              /* legs before the spiral starts again where the player stands (about 340 blocks out) */
#define WD_STUCK 20             /* a leg that moved under a block in this many steps turns its heading 45 degrees */
#define WD_RUN 100              /* a run's steps before it gives the step back (the next start resumes it) */
#define WD_AIR 150              /* in water, the run given back under this much air (the swim's at 120) */
#define WD_DROP 3               /* a drop deeper than this ahead (air under the next cell's floor) is turned from */
#define WD_LOOK_RETRY 600       /* wood_search look: steps after a sweep found nothing before the next */

enum { WDS_OFF, WDS_LOOK, WDS_WALK };
enum { WD_NONE, WD_TURN, WD_LEGW };

/* the want: no pickaxe at all, none of the stone age's items, too little wood for the first pickaxe */
static int wood_want(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                     const struct neth_rl_state *s)
{
    if (!MOPT(MO_WOOD_SEARCH) || r->dim != 0 || (r->flags & PF_DEAD) || r->screen != PS_NONE) return 0;
    for (int i = 0; i < 36; ++i)
    {
        int it = s->inv[i].item;
        if (it >= 0 && it < 4096 && s->inv[i].count > 0 && v->T.pick[it] > 0) return 0;
    }
    if (inv_count(s, COBBLE) + inv_count(s, IRON_ORE) + inv_count(s, INGOT) > 0) return 0;
    if (inv_count(s, LOG) + inv_count(s, LOG2) > 0) return 0;
    int64_t sticks = inv_count(s, STICK), planks = inv_count(s, PLANKS);
    int bench = inv_count(s, TABLE) > 0 || near_table(e, r);
    int64_t need = bench ? (sticks >= 2 ? 3 : 5) : (sticks >= 2 ? 7 : 9);
    return planks < need;
}

/* a log in view the seek may go to */
static int wood_log_seen(const struct vec_env *e, const struct neth_rl_state *s)
{
    return s->seen_n[3] > 0 && s->seen[3].d > 0.0f && tried_get(e, s->seen[3].x, s->seen[3].y, s->seen[3].z) <= e->steps;
}

/* a hostile the fight locks on in the vis list */
static int wood_threat(const struct neth_rl_state *s)
{
    for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
        if (fight_kind(s->vis[i].kind)) return 1;
    return 0;
}

/* the column one block ahead along yaw: 1 when the next step is a drop over WD_DROP or lava (the floor's cells, the
 * client world: the ground in front of the feet) */
static int wood_bad_ahead(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r, double yaw)
{
    int id = (int)(e - v->e);
    double yr = yaw * M_PI / 180.0;
    int x = (int)floor(r->x - sin(yr) * 0.9), z = (int)floor(r->z + cos(yr) * 0.9), fy = (int)floor(r->y + 0.01);
    if (x == (int)floor(r->x) && z == (int)floor(r->z)) return 0;
    for (int k = 0; k <= WD_DROP + 1; ++k)
    {
        int b = obsview_block(v->p, id, x, fy - k, z, NULL);
        if (b < 0) return 0;                        /* not loaded: nothing known */
        if (b == 10 || b == 11) return 1;           /* lava */
        if (k == 0) { if (b == 0 || !ob_solid(b)) continue; return 0; }   /* a wall: the stuck turn's */
        if (b != 0 && b != 31 && b != 32 && b != 37 && b != 38 && b != 78) return 0;   /* a floor (water too) */
    }
    return 1;
}

static int wood_m_cand(struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    if (!MOPT(MO_WOOD_SEARCH)) return 0;
    if (c->e->kind == M_MINE && c->e->wd) return 1;
    return wood_want(c->v, c->e, c->r, c->s) && (wood_log_seen(c->e, c->s) || c->e->steps >= c->e->wd_off);
}

static int wood_m_start(struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct vec_last *r = c->r;
    const struct neth_rl_state *s = c->s;
    if (!MOPT(MO_WOOD_SEARCH) || !wood_want(v, e, r, s)) return 0;
    if (s->eye_water && r->air < WD_AIR) return 0;
    if (wood_log_seen(e, s))
    {
        seek_begin(e, r, s, 3);
        e->wd_seeks += 1;
        return 1;
    }
    if (e->steps < e->wd_off || wood_threat(s)) return 0;
    e->kind = M_MINE;
    e->wd = 1;
    e->wd_run = 0;
    if (e->wd_ph == WD_NONE || e->wd_ph == WD_TURN)
    {
        /* a turn on the spot (again from its start: one cut short is begun again) */
        if (e->wd_ph == WD_NONE) e->wd_yaw = (double)r->yaw, e->wd_leg = 0;
        e->wd_ph = WD_TURN;
        e->wd_t = e->steps;
    }
    e->wd_mx = r->x, e->wd_mz = r->z, e->wd_mt = e->steps;
    e->gstuck = 0;
    e->gpx = r->x, e->gpz = r->z;
    return 1;
}

static int wood_m_drive(struct mac_step *c, struct neth_rl_act *a)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct vec_last *r = c->r;
    const struct neth_rl_state *s = c->s;
    if (e->kind != M_MINE || !e->wd) return -1;
    still(a);
    a->hotbar = -1;
    e->wd_steps += 1;
    if (!wood_want(v, e, r, s) || wood_log_seen(e, s) || (s->eye_water && r->air < WD_AIR) || wood_threat(s) ||
        ++e->wd_run > WD_RUN)
        return 1;
    a->look_mode = 1;
    a->look[1] = 0.0f;
    int64_t t = e->steps - e->wd_t;
    if (e->wd_ph == WD_TURN)
    {
        a->look[0] = (float)(e->wd_yaw + 10.0 * (double)t);
        if (s->eye_water) a->hold = 1u << PK_JUMP;     /* afloat */
        if (t < WD_SWEEP) return 0;
        e->wd_sweeps += 1;
        if (MOPT(MO_WOOD_SEARCH) == WDS_LOOK)
        {
            e->wd_ph = WD_NONE;
            e->wd_off = e->steps + WD_LOOK_RETRY;
            return 1;
        }
        e->wd_ph = WD_LEGW;
        e->wd_x = e->wd_mx = r->x, e->wd_z = e->wd_mz = r->z;
        e->wd_mt = e->steps;
    }
    /* the leg: forward along its heading, a jump where a step does not move, swum across water at the top */
    if (wood_bad_ahead(v, e, r, e->wd_yaw)) e->wd_yaw += 90.0, e->wd_turns += 1;
    a->look[0] = (float)e->wd_yaw;
    a->hold = 1u << PK_FORWARD;
    double mv = py_hypot2(r->x - e->gpx, r->z - e->gpz);
    e->gstuck = mv < 0.05 ? e->gstuck + 1 : 0;
    if (e->gstuck > 2 || s->eye_water) a->hold |= 1u << PK_JUMP;
    e->gpx = r->x, e->gpz = r->z;
    if (py_hypot2(r->x - e->wd_mx, r->z - e->wd_mz) >= 1.0) e->wd_mx = r->x, e->wd_mz = r->z, e->wd_mt = e->steps;
    if (e->steps - e->wd_mt > WD_STUCK)
    {
        e->wd_yaw += 45.0;      /* blocked (a wall, a tree's leaves): the heading turned, the leg goes on */
        e->wd_mt = e->steps;
        e->wd_turns += 1;
        return 0;
    }
    double len = WD_LEG * (1.0 + (double)(e->wd_leg / 2));
    if (py_hypot2(r->x - e->wd_x, r->z - e->wd_z) >= len)
    {
        /* the leg's end: a quarter turn and a turn on the spot */
        e->wd_leg = (e->wd_leg + 1) % WD_LEGS;
        e->wd_yaw += 90.0;
        e->wd_ph = WD_TURN;
        e->wd_t = e->steps;
    }
    return 0;
}

/* the run is the seek's half that finds the cell: labelled the seek's */
static uint32_t wood_m_label(const struct vec_env *e) { return e->kind == M_MINE && e->wd ? NETH_VEC_SEEK : 0; }

static void wood_m_end(struct vec_env *e) { e->wd = 0; }

static void wood_m_reset(struct vec_env *e)
{
    e->wd = e->wd_ph = e->wd_leg = e->wd_run = 0;
    e->wd_off = e->wd_t = e->wd_mt = 0;
    e->wd_seeks = e->wd_sweeps = e->wd_turns = e->wd_steps = 0;
}

/* its switch is the seek's (the option turns it on): no run-file name of its own */
static const struct vec_macro MAC_WOOD = {
    .name = "wood", .bit = NETH_VEC_SEEK, .sw = NULL, .cand = wood_m_cand, .start = wood_m_start,
    .drive = wood_m_drive, .label = wood_m_label, .end = wood_m_end, .reset = wood_m_reset,
};
