/* NETH_VEC_PORTAL: the frame built and lit from 10 obsidian, a plan of the craft macro (lane/macromod: cut from vecenv.c, unchanged) */

/* NETH_VEC_PORTAL's steps (lane/portal): a block placed in the frame's cell
 * (r, h) on its support's face (side 0: the top of the cell below, 1: the +r
 * side of the cell at r - 1, 2: the -r side of the cell at r + 1; the top
 * row's last block goes on the far corner, whose face toward it faces the
 * player), a tower jump (a cobblestone placed under the
 * feet at the jump's top, looking straight down), the lighting (flint and
 * steel on the bottom obsidian's top), the walk in. The frame stands 2 ahead
 * of the feet cell at the start, r across the look (r = 0, 1 the inside), h
 * up from the feet's height; the columns' upper blocks and the top row are
 * placed from the tower, as a top face over the eye cannot be clicked */
enum { PT_PLACE, PT_TOWER, PT_LIGHT, PT_ENTER, PT_DIG };
struct pstep { int8_t kind, r, h, side; int16_t item; };
/* the bottom row's corners before its middle: a corner beside a placed block is hidden behind it (lane/portalfix).
 * PT_DIG: the cell (r, h) at `side` ahead (lane/portalfix): the room to build in cleared first (a start underground,
 * gold-g6-s42's, has rock in the frame's cells and over the tower): the cells 1 then 2 ahead, the middle columns
 * first and each column from the bottom (a cell's line of sight then crosses cells already cleared), the column over
 * the head for the tower */
#define DIG_COL(f, r) {PT_DIG, r, 0, f, 0}, {PT_DIG, r, 1, f, 0}, {PT_DIG, r, 2, f, 0}, {PT_DIG, r, 3, f, 0}, \
                      {PT_DIG, r, 4, f, 0}
/* portal_room wide's cells (item 1: skipped under frame): the feet's row beside the feet, h 0 to 2 before the frame's
 * cells (a 1x2 tunnel's wall beside the feet), h 3 and 4 after the column over the head (a low roof beside the tower's
 * top hides the frame's upper far cells) */
#define DIG_FEET(r) {PT_DIG, r, 0, 0, 1}, {PT_DIG, r, 1, 0, 1}, {PT_DIG, r, 2, 0, 1}
#define DIG_HEAD(r) {PT_DIG, r, 3, 0, 1}, {PT_DIG, r, 4, 0, 1}
static const struct pstep PORTAL_STEPS[] = {
    DIG_FEET(1), DIG_FEET(-1), DIG_FEET(2), DIG_COL(1, 0), DIG_COL(1, 1), DIG_COL(1, -1), DIG_COL(1, 2), DIG_COL(2, 0), DIG_COL(2, 1), DIG_COL(2, -1),
    DIG_COL(2, 2), {PT_DIG, 0, 2, 0, 0}, {PT_DIG, 0, 3, 0, 0}, {PT_DIG, 0, 4, 0, 0}, {PT_DIG, 0, 5, 0, 0},
    DIG_HEAD(1), DIG_HEAD(-1), DIG_HEAD(2),
    {PT_PLACE, -1, 0, 0, COBBLE}, {PT_PLACE, 2, 0, 0, COBBLE}, {PT_PLACE, 0, 0, 0, OBSIDIAN},
    {PT_PLACE, 1, 0, 0, OBSIDIAN}, {PT_PLACE, -1, 1, 0, OBSIDIAN}, {PT_PLACE, 2, 1, 0, OBSIDIAN},
    {PT_TOWER, 0, 0, 0, COBBLE}, {PT_TOWER, 0, 0, 0, COBBLE}, {PT_TOWER, 0, 0, 0, COBBLE},
    {PT_PLACE, -1, 2, 0, OBSIDIAN}, {PT_PLACE, -1, 3, 0, OBSIDIAN}, {PT_PLACE, 2, 2, 0, OBSIDIAN},
    {PT_PLACE, 2, 3, 0, OBSIDIAN}, {PT_PLACE, -1, 4, 0, COBBLE}, {PT_PLACE, 0, 4, 1, OBSIDIAN},
    {PT_PLACE, 2, 4, 0, COBBLE}, {PT_PLACE, 1, 4, 2, OBSIDIAN}, {PT_LIGHT, 0, 0, 0, FLINT_STEEL},
    {PT_ENTER, 0, 0, 0, 0},
};
#define PORTAL_NSTEPS ((int)(sizeof PORTAL_STEPS / sizeof PORTAL_STEPS[0]))

/* the item's hotbar slot made current: 1 once it is (its slot in *hb), else
 * 0 with *rc the macro's return (being fetched from the main inventory) */
static int nt_entry_fill(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                         const struct neth_rl_state *s, struct neth_rl_act *a);   /* lane/rod: nether.c */
static int pp_select(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                     const struct neth_rl_state *s, struct neth_rl_act *a, int item, int *hb, int *rc)
{
    *hb = -1;
    *rc = 0;
    for (int i = 0; i < 9 && *hb < 0; ++i)
        if (s->inv[i].item == item && s->inv[i].count > 0) *hb = i;
    if (*hb < 0)
    {
        /* fetch_item's limit is the macro's age (50), which a portal's build is past from its first dug cell: the
         * step's own try limit bounds it here, an item gone from the inventory still ends the build */
        e->placed = 0;
        *rc = fetch_item(v, e, r, s, a, item) && inv_count(s, item) == 0;
        return 0;
    }
    e->pressed = 0;
    a->hotbar = *hb;
    return s->current == *hb && r->screen == PS_NONE;
}

static int portal_fail(struct vec_env *e)
{
    e->pp_retry = e->steps + 200;
    return 1;
}

static int portal_build(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                        const struct neth_rl_state *s, struct neth_rl_act *a, double ey)
{
    static const int dx[4] = {0, -1, 0, 1}, dz[4] = {1, 0, -1, 0};    /* yaw 0, 90, 180, 270 */
    if (e->pp_step < 0)
    {
        if (MOPT(MO_PT_BASE) && !r->on_ground) return 0;     /* portal_base ground: not mid-jump */
        int k = ((int)lround((double)r->yaw / 90.0) % 4 + 4) % 4;
        if (r->dim == -1)
        {
            /* lane/chain (nether_portal_home): the Nether's frame faces the quarter whose site is clear of lava */
            k = pt_site(v, e, r);
#ifdef CHAMBER_TRACE
            if (k >= 0 && MOPT(MO_NT_CHAMBER))
            {
                struct netherview_blaze bz[64];
                int nb = netherview_oracle_blazes(v->p, (int)(e - v->e), r->x, r->y + 1.62, r->z, 16.0, bz, 64);
                fprintf(stderr, "CHAMBER_PORTAL_START env=%d step=%lld pos=%.1f,%.1f,%.1f blazes16=%d\n",
                        (int)(e - v->e), (long long)e->steps, r->x, r->y, r->z, nb);
            }
#endif
#ifdef PP_TRACE
            fprintf(stderr, "PPNETHER %d %ld site %d at %.1f %.1f %.1f yaw %.0f\n", (int)(e - v->e), (long)e->steps, k,
                    r->x, r->y, r->z, r->yaw);
#endif
            if (k < 0) return portal_fail(e);
        }
        e->pp_fx = dx[k];
        e->pp_fz = dz[k];
        e->pp_rx = -dz[k];
        e->pp_rz = dx[k];
        e->pp_x = (int)floor(r->x);
        e->pp_y = (int)floor(r->y + 1e-3);
        e->pp_z = (int)floor(r->z);
        e->pp_step = e->pp_wait = e->pp_tries = e->pp_dig = 0;
        e->pp_dim = r->dim;
    }
    while (e->pp_step < PORTAL_NSTEPS && PORTAL_STEPS[e->pp_step].kind == PT_DIG && PORTAL_STEPS[e->pp_step].item &&
           !MOPT(MO_PT_ROOM))
        e->pp_step += 1;
    if (e->pp_step >= PORTAL_NSTEPS) return 1;
    const struct pstep *st = &PORTAL_STEPS[e->pp_step];
#ifdef PP_TRACE
    if (r->dim == -1 && inv_count(s, BLAZE_ROD) >= 7 && e->age % 50 == 0)
        fprintf(stderr, "PPSTEP world=%lld age=%d step=%d kind=%d side=%d r=%d h=%d tries=%d base=%d,%d,%d player=%.2f,%.2f,%.2f screen=%d mo=%d:%d,%d,%d face=%d hold=%u\n",
                (long long)r->wtime, e->age, e->pp_step, st->kind, st->side, st->r, st->h, e->pp_tries,
                e->pp_x, e->pp_y, e->pp_z, r->x, r->y, r->z, r->screen,
                s->mo_block, s->mo_x, s->mo_y, s->mo_z, s->mo_side, a->hold);
#endif
    int yawk = (int)lround(atan2(-(double)e->pp_fx, (double)e->pp_fz) * 180.0 / M_PI);
    int hb, rc;
    /* The first Nether corner cannot be aimed from a knockback two blocks off the remembered base. Walk back before
     * its per-step timer runs out; the whole craft still has its 1,600-step cap. This is opt-in for the dragon chain. */
    if (MOPT(MO_PT_RETURN) && r->dim == -1 && r->screen == PS_NONE && r->on_ground &&
        r->y <= e->pp_y + 0.5 && (st->kind == PT_DIG || (st->kind == PT_PLACE && st->h <= 1)) &&
        py_hypot2(r->x - (e->pp_x + 0.5), r->z - (e->pp_z + 0.5)) > 1.2)
    {
#ifdef PP_TRACE
        if (e->steps % 20 == 0)
            fprintf(stderr, "PPRETURN %d %ld step %d age %d hp %.1f at %.2f %.2f %.2f base %d %d %d\n",
                    (int)(e - v->e), (long)e->steps, e->pp_step, e->age, r->health, r->x, r->y, r->z,
                    e->pp_x, e->pp_y, e->pp_z);
#endif
        double yaw, pitch;
        still(a);
        look_at(r->x, ey, r->z, e->pp_x + 0.5, ey, e->pp_z + 0.5, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw, a->look[1] = 0.0f;
        a->hotbar = -1;
        a->hold = 1u << PK_FORWARD;
        e->pp_tries = 0;
        return 0;
    }
    if (++e->pp_tries > (st->kind == PT_ENTER ? 400 : st->kind == PT_TOWER ? 80 : st->kind == PT_DIG ? 260 : 60))
    {
#ifdef PP_TRACE
        fprintf(stderr, "PPFAIL %d %ld step %d kind %d r %d h %d at %.1f %.1f %.1f obs %d cobble %d | base %d %d %d f %d %d "
                "mo %d %d %d %d side %d blk %d cur %d item %d scr %d yaw %.0f pitch %.0f ground %d\n", (int)(e - v->e),
                (long)e->steps, e->pp_step, st->kind, st->r, st->h, r->x, r->y, r->z, (int)inv_count(s, OBSIDIAN),
                (int)inv_count(s, COBBLE), e->pp_x, e->pp_y, e->pp_z, e->pp_fx, e->pp_fz, s->mo_hit, s->mo_x, s->mo_y,
                s->mo_z, s->mo_side, s->mo_block, s->current, s->inv[s->current].item, r->screen, r->yaw, r->pitch,
                (int)r->on_ground);
#endif
        return portal_fail(e);
    }
    if (st->kind == PT_TOWER)
    {
        a->look_mode = 1;
        a->look[0] = (float)yawk;
        a->look[1] = 90.0f;
        if (!pp_select(v, e, r, s, a, COBBLE, &hb, &rc)) return rc;
        if (MOPT(MO_PT_TOWER) >= 2)
        {
            /* portal_tower centre: the feet kept over the column's cell (along the look: forward and back; across:
             * the strafe keys, +r is the right hand) */
            double ox = r->x - (e->pp_x + 0.5), oz = r->z - (e->pp_z + 0.5);
            double fw = ox * e->pp_fx + oz * e->pp_fz, ac = ox * e->pp_rx + oz * e->pp_rz;
            if (fw > 0.15) a->hold |= 1u << PK_BACK;
            else if (fw < -0.15) a->hold |= 1u << PK_FORWARD;
            if (ac > 0.15) a->hold |= 1u << PK_LEFT;
            else if (ac < -0.15) a->hold |= 1u << PK_RIGHT;
            if (e->pp_wait == 0 && (fabs(fw) > 0.3 || fabs(ac) > 0.3)) return 0;     /* a jump begins centred */
        }
        /* lane/bportal (portal_tower ground): the jump's base taken with the feet on the ground only (centre's took it on
         * the step after a fall back, the feet already rising: a base of 64.42, the landing at 65.00 never one higher,
         * the step out of tries; s2's first tower step failed 9 times so, its frame's 4 cells mined back) */
        if (e->pp_wait == 0 && MOPT(MO_PT_TOWER) == 3 && !r->on_ground) return 0;
        if (e->pp_wait == 0)
        {
            e->pp_base = r->y;
            e->pp_before = (int)inv_count(s, COBBLE);
            e->pp_wait = 1;
        }
        if (e->pp_wait == 1)
        {
            a->hold |= 1u << PK_JUMP;
            if (r->y > e->pp_base + 1.05)
            {
                a->press[PK_USE] = 1;
                e->pp_wait = 2;
            }
            return 0;
        }
        if ((MOPT(MO_PT_TOWER) ? r->on_ground : inv_count(s, COBBLE) < e->pp_before) &&
            fabs(r->y - (e->pp_base + 1.0)) < 0.01)
        {
            e->pp_step += 1;
            e->pp_wait = e->pp_tries = 0;
        }
        else if (r->y < e->pp_base + 0.5)
            e->pp_wait = 0;             /* landed where it jumped from: again */
        return 0;
    }
    int cx = e->pp_x + 2 * e->pp_fx, cz = e->pp_z + 2 * e->pp_fz;
    if (st->kind == PT_DIG)
    {
        /* the cell's nearest face looked at; whatever block of the room the crosshair is on dug (the cell or one
         * in its way), the step over once the crosshair is on no block of the room */
        int dx_ = e->pp_x + st->side * e->pp_fx + st->r * e->pp_rx, dz_ = e->pp_z + st->side * e->pp_fz + st->r * e->pp_rz;
        double yaw, pitch;
        look_block(r, ey, dx_, e->pp_y + st->h, dz_, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw;
        a->look[1] = (float)pitch;
        if (e->pp_tries < 3) return 0;
        int fo = (s->mo_x - e->pp_x) * e->pp_fx + (s->mo_z - e->pp_z) * e->pp_fz;
        int ro = (s->mo_x - e->pp_x) * e->pp_rx + (s->mo_z - e->pp_z) * e->pp_rz, ho = s->mo_y - e->pp_y;
        int room = s->mo_hit && ((fo >= 1 && fo <= 2 && ro >= -1 && ro <= 2 && ho >= 0 && ho <= 4) ||
                                 (fo == 0 && ro == 0 && ho >= 2 && ho <= 5) ||
                                 (MOPT(MO_PT_ROOM) && fo == 0 && ro >= -1 && ro <= 2 && ro != 0 && ho >= 0 && ho <= 4));
        if (room)
        {
            a->hold = 1u << PK_ATTACK;
            int brick = s->mo_block == 112 || s->mo_block == 113 || s->mo_block == 114;
            int tool_block = s->mo_block == OBSIDIAN || (MOPT(MO_PT_BRICK_PICK) && brick) ? 1 : s->mo_block;
            int best = best_tool(&v->T, s, tool_block);
            if (best >= 0) a->hotbar = best;
            return 0;
        }
        e->pp_step += 1;
        e->pp_tries = 0;
        return 0;
    }
    if (st->kind == PT_ENTER)
    {
        /* the dimension left (lane/chain nether_portal_home: a build in the Nether ends in the overworld) */
        if (r->dim != e->pp_dim) return 1;
        if (e->pp_dim == 0 && nt_entry_fill(v, e, r, s, a)) return 0;     /* lane/rod (nether_entry_fill, nether.c) */
        a->look_mode = 1;
        a->look[0] = (float)yawk;
        a->look[1] = 0.0f;
        /* along the frame's axis from the start cell's centre: the portal's cells are at 2; forward (jumping onto
         * the bottom obsidian from 0.6 on) until 1.7, back past 2.3 (a jump's momentum carries), still between */
        double fw = (r->x - (e->pp_x + 0.5)) * e->pp_fx + (r->z - (e->pp_z + 0.5)) * e->pp_fz;
        if (fw < 1.7)
            a->hold |= (1u << PK_FORWARD) | (fw > 0.6 ? 1u << PK_JUMP : 0);
        else if (fw > 2.3)
            a->hold |= 1u << PK_BACK;
        return 0;
    }
    /* a place or the lighting: T the cell to fill (the lighting's: the fire's, over the bottom obsidian), first
     * looked at through its support's face (side 0: the top of the cell below, 1: the +r side of the one at r - 1,
     * 2: the -r side of the one at r + 1) */
    int tx = cx + st->r * e->pp_rx, ty = e->pp_y + st->h + (st->kind == PT_LIGHT), tz = cz + st->r * e->pp_rz;
    double px = tx + 0.5, py = ty - 0.01, pz = tz + 0.5;
    if (st->side)
    {
        double sg = st->side == 1 ? -0.51 : 0.51;
        px = tx + 0.5 + sg * e->pp_rx;
        py = ty + 0.5;
        pz = tz + 0.5 + sg * e->pp_rz;
    }
    /* the look: the planned face, then each face of T's cell in turn (a block beside T may hide the planned one,
     * and any face whose neighbour cell is T places there) */
    static const int nx[6] = {0, 0, 0, 0, -1, 1}, ny[6] = {-1, 1, 0, 0, 0, 0}, nz[6] = {0, 0, -1, 1, 0, 0};
    int k = (e->pp_tries / 3) % 7;
    if (k > 0)
    {
        px = tx + 0.5 - 0.49 * nx[k - 1];
        py = ty + 0.5 - 0.49 * ny[k - 1];
        pz = tz + 0.5 - 0.49 * nz[k - 1];
    }
    double yaw, pitch;
    look_at(r->x, ey, r->z, px, py, pz, &yaw, &pitch);
    a->look_mode = 1;
    a->look[0] = (float)yaw;
    a->look[1] = (float)pitch;
    int item = st->item;
    if (e->pp_wait > 0)
    {
        /* the use sent: done once the item's count fell (flint and steel: its damage rose); a count that rose is an
         * item picked up (a dug block's drop), not the place */
        int hbn = -1;
        for (int i = 0; i < 9 && hbn < 0; ++i)
            if (s->inv[i].item == item) hbn = i;
        int now = st->kind == PT_LIGHT ? (hbn >= 0 ? s->inv[hbn].damage : -1) : (int)inv_count(s, item);
        if (st->kind == PT_LIGHT ? now > e->pp_before : now < e->pp_before)
        {
            e->pp_step += 1;
            e->pp_wait = e->pp_tries = 0;
        }
        else
            --e->pp_wait;
        return 0;
    }
    /* the item selected from the step's start (a hotbar change shows a step or two later: a face found while it
     * changed was lost as the look moved on) */
    int sel = pp_select(v, e, r, s, a, item, &hb, &rc);
    if (!sel && rc) return rc;
    if (e->pp_tries < 3) return 0;
    int sd = s->mo_side >= 0 && s->mo_side < 6 ? s->mo_side : 0;
    int on_s = s->mo_hit && s->mo_x + nx[sd] == tx && s->mo_y + ny[sd] == ty && s->mo_z + nz[sd] == tz;
    int on_t = s->mo_hit && s->mo_x == tx && s->mo_y == ty && s->mo_z == tz;
    int b = s->mo_block;
    int replace = on_t && (b == 31 || b == 32 || b == 78 || b == 106);
    if (on_t && st->kind == PT_PLACE && (b == item || (b == OBSIDIAN && item == COBBLE)))
    {
        /* the cell holds what it should already (obsidian left from a cast: a corner takes it as well) */
        e->pp_step += 1;
        e->pp_tries = 0;
        return 0;
    }
    if (on_t && !replace && st->kind == PT_PLACE)
    {
        /* the cell taken: dug (obsidian with the best pickaxe) */
        a->hold = 1u << PK_ATTACK;
        int best = best_tool(&v->T, s, b == OBSIDIAN ? 1 : b);
        if (best >= 0) a->hotbar = best;
        return 0;
    }
    /* never a use in the step the look moves to its next point: the client clicks where the new look lands (a
     * column's base went into the cell over it) */
    if ((!on_s && !replace) || !sel || (k > 0 && e->pp_tries % 3 == 0)) return 0;
    a->press[PK_USE] = 1;
    e->pp_before = st->kind == PT_LIGHT ? s->inv[hb].damage : (int)inv_count(s, item);
    e->pp_wait = 6;
    return 0;
}

