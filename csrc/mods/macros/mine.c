/* NETH_VEC_MINE: the mine lock (lane/macromod: macro_start's and macro_drive's lines cut from vecenv.c unchanged): the
 * policy's attack on a block it can break (the hand's blocks, or a pickaxe of the tier the block needs in the hotbar)
 * locked on until the block breaks or the crosshair leaves it, with the hotbar's best tool (actions.mine_tool). Its
 * label's mbit is NETH_VEC_MINE, the mine kind's when no other macro of that kind names the step */

static int mine_food_wait(const struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    return MOPT(MO_SH_FOOD) == SHW_SURFACE && c->e->sh.fwait && c->r->dim == 0;
}

/* lane/bportal (mine_yield, a driver what-if): a controller wants the step, so no lock begins */
static int mine_yield(const struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    const struct vec_env *e = c->e;
    if (!MOPT(MO_MINE_YIELD)) return 0;
    return prov_want(v, e, c->r, c->s) || e->fs_on || e->hunt || bp_want(v, e, c->r, c->s);
}

static int mine_m_cand(struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    const struct neth_rl_state *s = c->s;
    int b = s->mo_block < 0 ? 0 : s->mo_block > 4095 ? 4095 : s->mo_block;
    return !mine_food_wait(c) && !mine_yield(c) && (v->macros & NETH_VEC_MINE) && c->attack && s->mo_hit != 0 &&
           (v->T.lockable[b] || ((v->macros & NETH_VEC_LIQUID) && b == OBSIDIAN));
}

static int mine_m_start(struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct neth_rl_state *s = c->s;
    if (!mine_food_wait(c) && !mine_yield(c) && (v->macros & NETH_VEC_MINE) && c->attack && s->mo_hit)
    {
        int b = s->mo_block, tier = 0;
        for (int i = 0; i < 9; ++i)
        {
            int it = s->inv[i].item, t = it >= 0 && it < 4096 ? v->T.pick[it] : 0;
            if (t > tier) tier = t;
        }
        int need = b >= 0 && b < 4096 ? v->T.picky[b] : 0;
        if ((v->macros & NETH_VEC_LIQUID) && b == OBSIDIAN) need = 4;
        if (b >= 0 && b < 4096 && (v->T.hand[b] || (need > 0 && tier >= need)) && !dig_under(v, c->r, s))
        {
            e->kind = M_MINE;
            e->mx = s->mo_x;
            e->my = s->mo_y;
            e->mz = s->mo_z;
            e->mblock = b;
            return 1;
        }
    }
    return 0;
}

/* the lock: the mine kind's run no other macro owns */
static int mine_m_drive(struct mac_step *c, struct neth_rl_act *a)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct vec_last *r = c->r;
    const struct neth_rl_state *s = c->s;
    double ey = c->ey;
    if (e->kind != M_MINE) return -1;
    if (mine_food_wait(c)) return 1; /* a saved mine lock yields to the food trip's climb */
    int x = e->mx, y = e->my, z = e->mz;
    int here = s->mo_hit && s->mo_x == x && s->mo_y == y && s->mo_z == z;
    double d = py_dist3(r->x, ey, r->z, x + 0.5, y + 0.5, z + 0.5);
    if ((e->age > 1 && !here) || e->age > (e->mblock == OBSIDIAN ? 260 : 200) || d > 5.0 || r->screen != 0) return 1;
    double yaw, pitch;
    look_at(r->x, ey, r->z, x + 0.5, y + 0.5, z + 0.5, &yaw, &pitch);
    a->hold = 1u << PK_ATTACK;
    memset(a->press, 0, sizeof a->press);
    a->look_mode = 1;
    a->look[0] = (float)yaw;
    a->look[1] = (float)pitch;
    int best = best_tool(&v->T, s, e->mblock == OBSIDIAN ? 1 : e->mblock);   /* obsidian: the best pickaxe */
    if ((v->macros & NETH_VEC_STRIP2) && e->mblock != OBSIDIAN)
    {
        int fetch = 0;                          /* lane/strip2: the strip's pickaxe rule and fetch */
        best = st_tool(v, e, s, e->mblock, &fetch);
        if (fetch)
        {
            a->hold = 0;
            st_begin(e, a, ST_FETCH);
            return 0;
        }
    }
    if (best >= 0 && best != s->current && MOPT(MO_MINE_TOOL)) a->hotbar = best;
    return 0;
}

static uint32_t mine_m_label(const struct vec_env *e) { return e->kind == M_MINE ? NETH_VEC_MINE : 0; }

static const struct mac_switch MINE_SW[] = {{"mine", NETH_VEC_MINE}, {NULL, 0}};

static const struct vec_macro MAC_MINE = {
    .name = "mine", .bit = NETH_VEC_MINE, .sw = MINE_SW, .cand = mine_m_cand, .start = mine_m_start, .drive = mine_m_drive,
    .label = mine_m_label,
};
