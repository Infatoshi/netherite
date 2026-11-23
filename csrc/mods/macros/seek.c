/* NETH_VEC_SEEK and NETH_VEC_SEEKKEY: the seek (lane/macromod: cut from vecenv.c, unchanged) */


/* NETH_VEC_SEEK (lane/seek): the seen kind to go to (0 iron ore, 1 diamond
 * ore), -1 none: in view, still needed, its nearest cell not given up on */
static int st3_wood_want(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s);   /* strip */

static int seek_want(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s)
{
    if (sh_owns(v, e, &e->last) && provision_mode(v, e, &e->last, s) != PV_STOCK)
        return -1;     /* the food stage may seek a visible fuel log through the stronghold's wait */
    int tier = 0;
    for (int i = 0; i < 36; ++i)
    {
        int it = s->inv[i].item, t = it >= 0 && it < 4096 ? v->T.pick[it] : 0;
        if (t > tier) tier = t;
    }
    int64_t need = (inv_count(s, IRON_PICK) + inv_count(s, DIAMOND_PICK) == 0 ? 3 : 0) +
                   (inv_count(s, BUCKET) + inv_count(s, WATER_BUCKET) + inv_count(s, LAVA_BUCKET) == 0 ? 3 : 0) +
                   (inv_count(s, FLINT_STEEL) == 0 ? 1 : 0);
    /* lane/woodseek: a log when a craft waits on wood: the stone pickaxe or the furnace on a table none held, none
     * near and no wood to make (the table left behind), or the iron pickaxe on sticks */
    int64_t ore = inv_count(s, IRON_ORE), ingots = inv_count(s, INGOT), cobble = inv_count(s, COBBLE);
    int64_t planks = inv_count(s, PLANKS), logs = inv_count(s, LOG) + inv_count(s, LOG2);
    int on_table = (tier < 2 && cobble >= 3) || (ore > 0 && inv_count(s, FURNACE) == 0 && cobble >= 8);
    int on_sticks = tier < 3 && ingots >= 3 && inv_count(s, STICK) < 2;
    int wood = tier >= 1 && logs == 0 &&
               ((on_table && inv_count(s, TABLE) == 0 && !near_table(e, &e->last) && planks < 4) || (on_sticks && planks < 2));
#ifndef ST3_KEEP6
    if ((v->macros & NETH_VEC_STRIP2) && st3_wood_want(v, e, s)) wood = 1;   /* lane/strip3: the wood errand's log */
#endif
    if (provision_mode(v, e, &e->last, s) == PV_STOCK && st3_provision_wood_want(v, e, s)) wood = 1;
    /* lane/pick: the wood the next pickaxe needs, the held one wooden or stone or none at all (a broken one): the
     * table's 4 planks and the pickaxe's 3, its sticks' 2 when none are held, a table held or in reach sparing the 4;
     * a log in view gone to, so the craft below has the wood when the held pickaxe breaks */
    if ((v->macros & NETH_VEC_PICK) && tier < 3 && logs == 0)
    {
        int64_t sticks = inv_count(s, STICK);
        int bench = inv_count(s, TABLE) > 0 || near_table(e, &e->last);
        int64_t need = bench ? (sticks >= 2 ? 3 : 5) : (sticks >= 2 ? 7 : 9);
        if (planks < need) wood = 1;
    }
    /* lane/flint (NETH_VEC_STRIP): gravel in the overworld (the third seen block there) while flint and steel is
     * wanted, after the iron and the diamonds: 48 of 512 hour-long runs of s64-w2-it55 made it, 370 held an ingot */
    int flint_want = (v->macros & NETH_VEC_STRIP) && e->last.dim == 0 && tier >= 2 && inv_count(s, FLINT) == 0 &&
                     inv_count(s, FLINT_STEEL) == 0;
    int want[4] = {tier >= 2 && need > ore + ingots, tier >= 3 && inv_count(s, DIAMOND_PICK) == 0 && inv_count(s, DIAMOND) < 3,
                   flint_want, wood};
    static const int order[4] = {3, 0, 1, 2};
    for (int k = 0; k < 4; ++k)
    {
        int q = order[k];
        /* lane/strip: with the strip mine, ore within 8 blocks only (the tunnel's walls are 1 to 3 away; ore seen
         * across a cave stopped the tunnel a third of a 20-minute run, out of reach) */
        if (want[q] && s->seen_n[q] > 0 && s->seen[q].d > 0.0f &&
            (q == 3 || !(v->macros & NETH_VEC_STRIP) || MOPT(MO_SEEK_REACH) == 1 || s->seen[q].d <= 8.0f) &&
            tried_get(e, s->seen[q].x, s->seen[q].y, s->seen[q].z) <= e->steps)
            return q;
    }
    return -1;
}

/* lane/ladder18: the seek's two halves (actions.seek_walk, seek_dig). The tuned constant: the dig half begins with the
 * cell's centre this near the eye (blocks; the seek's own dig branch attacks a crosshair block within 4 of the eye, so
 * from here on the seek only digs, the cell or a block in the line to it) */
#define SEEK_DIG_REACH 4.0
/* which halves are the policy's in this env: bit 0 the walk, bit 1 the dig; 0 at macros without NETH_VEC_SEEKKEY */
static int seek_split(const struct neth_vec *v)
{
    if (!(v->macros & NETH_VEC_SEEKKEY)) return 0;
    return (MOPT(MO_SEEK_WALK) ? 1 : 0) | (MOPT(MO_SEEK_DIG) ? 2 : 0);
}
/* the half (1 the walk, 2 the dig) a seek on cell x, y, z would act in from r, nothing dug */
static int seek_half_at(const struct vec_last *r, int x, int y, int z)
{
    return py_dist3(r->x, r->y + 1.62, r->z, x + 0.5, y + 0.5, z + 0.5) <= SEEK_DIG_REACH ? 2 : 1;
}
/* a seek of kind q may begin here: its half (on seek_want's cell) not the policy's */
static int seek_half_ok(const struct neth_vec *v, const struct vec_last *r, const struct neth_rl_state *s, int q)
{
    int sp = seek_split(v);
    return !sp || !(sp & seek_half_at(r, s->seen[q].x, s->seen[q].y, s->seen[q].z));
}

/* NETH_VEC_SEEK: the block a seen kind q is sought as and the item its dig gives */
static int seek_block(int q) { return q == 0 ? IRON_ORE : q == 1 ? 56 : q == 2 ? GRAVEL : LOG; }
static int seek_item(int q) { return q == 0 ? IRON_ORE : q == 1 ? DIAMOND : q == 2 ? FLINT : LOG; }


/* NETH_VEC_SEEK: the walk to the seen cell and the dig (the mine macro's
 * kind). The macro's return */
static int seek(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                struct neth_rl_act *a, double ey)
{
    int q = e->seek - 1, ore = seek_block(q), got = seek_item(q);
    a->hold = 0;
    memset(a->press, 0, sizeof a->press);
    if ((v->macros & NETH_VEC_STRIP2) && e->stf) return st_errand(v, e, r, s, a);   /* lane/strip2 */
    e->swhy = 3;                /* lane/ladder16: books only (every return 1 below but the two named is a give-up) */
    if (inv_count(s, got) > e->gbefore)
    {
        e->swhy = 1;
        return 1;
    }
    double yaw, pitch;
    a->look_mode = 1;
    if (e->gdug)
    {
        e->sph = 2;             /* lane/ladder18: the dig half */
        /* dug: the walk to where it was (forward, a jump when it was above the feet) until the item is picked up */
        if (e->age - e->gdug > 60)
        {
            e->swhy = 2;
            return 1;
        }
        double dx = e->gx + 0.5 - r->x, dz = e->gz + 0.5 - r->z;
        look_at(r->x, ey, r->z, e->gx + 0.5, e->gy + 0.2, e->gz + 0.5, &yaw, &pitch);
        a->look[0] = (float)yaw;
        a->look[1] = (float)pitch;
        if (dx * dx + dz * dz > 0.09) a->hold |= 1u << PK_FORWARD;
        if (e->gy > (int)floor(r->y)) a->hold |= 1u << PK_JUMP;
        return 0;
    }
    double d = py_dist3(r->x, ey, r->z, e->gx + 0.5, e->gy + 0.5, e->gz + 0.5);
    int on = s->mo_hit && s->mo_x == e->gx && s->mo_y == e->gy && s->mo_z == e->gz;
    if (e->gon && !(on && s->mo_block == ore))
    {
        e->sph = 2;             /* lane/ladder18 */
        e->gdug = e->age;
        return 0;
    }
    e->gon = on && s->mo_block == ore;
    /* a nearer cell of the kind in view, the cell's own not yet dug at: the lock moves to it */
    if (!e->gon && s->seen_n[q] > 0 && s->seen[q].d > 0.0f && (double)s->seen[q].d < d - 0.5 &&
        tried_get(e, s->seen[q].x, s->seen[q].y, s->seen[q].z) <= e->steps)
    {
        e->gx = s->seen[q].x, e->gy = s->seen[q].y, e->gz = s->seen[q].z;
        d = py_dist3(r->x, ey, r->z, e->gx + 0.5, e->gy + 0.5, e->gz + 0.5);
        on = s->mo_hit && s->mo_x == e->gx && s->mo_y == e->gy && s->mo_z == e->gz;
    }
    /* lane/ladder18: the half this step acts in (nothing dug: by the cell's distance); a half that is the policy's
     * (actions.seek_walk, seek_dig at a SEEKKEY level) ends the seek, the cell not given up on */
    e->sph = d <= SEEK_DIG_REACH ? 2 : 1;
    if (seek_split(v) & e->sph)
    {
        e->swhy = 4;
        return 1;
    }
    int mb = s->mo_block;
    int blocked = s->mo_hit && !on && (mb == 7 || mb == OBSIDIAN || mb == 8 || mb == 9 || mb == 10 || mb == 11);
    if (d < e->gbest - 0.5)
    {
        e->gbest = d;
        e->ggain = e->age;
    }
    if (r->screen != 0 || s->eye_water || blocked || (on && mb != ore) || e->age - e->ggain > 120 || e->age > 600)
    {
        tried_set(e, e->gx, e->gy, e->gz, e->steps + 1200);
        return 1;
    }
    look_at(r->x, ey, r->z, e->gx + 0.5, e->gy + 0.5, e->gz + 0.5, &yaw, &pitch);
    a->look[0] = (float)yaw;
    a->look[1] = (float)pitch;
    if (dig_under(v, r, s))
    {
        tried_set(e, e->gx, e->gy, e->gz, e->steps + 1200);
        return 1;
    }
    if (s->mo_hit && py_dist3(r->x, ey, r->z, s->mo_x + 0.5, s->mo_y + 0.5, s->mo_z + 0.5) <= 4.0)
    {
        /* the ore under the crosshair, or a block in the line to it within reach: dug */
        int fetch = 0, blk = on ? ore : mb;
        int best = (v->macros & NETH_VEC_STRIP2) ? st_tool(v, e, s, blk, &fetch) : best_tool(&v->T, s, blk);
        if (fetch)
        {
            st_begin(e, a, ST_FETCH);                 /* lane/strip2: the pickaxe from the main inventory first */
            return 0;
        }
        a->hold = 1u << PK_ATTACK;
        if (best >= 0 && best != s->current) a->hotbar = best;
        e->gstuck = 0;
        return 0;
    }
    /* the line clear past reach: walked along, a jump when a step does not move; lane/lavaesc: crouched below y 16
     * when the cell is not below the feet (a crouch walks off no edge: the seek had walked over holes into lava pools) */
    a->hold = 1u << PK_FORWARD;
    if ((v->macros & NETH_VEC_STRIP) && r->y < 16.0 && e->gy >= (int)floor(r->y)) a->hold |= 1u << PK_SNEAK;
    /* lane/strip3: the wood errand's walk to a log crouched too (the seek fell from the surface's ledges) */
    if ((v->macros & NETH_VEC_STRIP2) && q == 3 && e->gy >= (int)floor(r->y)) a->hold |= 1u << PK_SNEAK;
    double mv = py_hypot2(r->x - e->gpx, r->z - e->gpz);
    e->gstuck = mv < 0.05 ? e->gstuck + 1 : 0;
    if (e->gstuck > 2) a->hold |= 1u << PK_JUMP;
    e->gpx = r->x, e->gpz = r->z;
    return 0;
}

/* NETH_VEC_SEEK: the seek of seen kind q begun (macro_start, and the strip's handoff) */
static void seek_begin(struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s, int q)
{
    e->kind = M_MINE;
    e->seek = 1 + q;
    e->gx = s->seen[q].x, e->gy = s->seen[q].y, e->gz = s->seen[q].z;
    e->gbest = 1e9;
    e->ggain = 0;
    e->gstuck = 0;
    e->gpx = r->x, e->gpz = r->z;
    e->gbefore = (int)inv_count(s, seek_item(q));
    e->gon = e->gdug = 0;
    e->sfirst = 1;              /* lane/ladder16: its first step's label is half 6 */
    e->skrun = 0;               /* (macro_start sets it after, when the key started it) */
    e->sph = e->sphl = 0;       /* lane/ladder18 */
}

/* lane/ladder16 (NETH_VEC_SEEKKEY): a policy step's seek books (vecenv.h NETH_VEC_GUI_BOOKS [47] to [49]); v the env's
 * level's copy. Reads only */
static void seek_books(const struct neth_vec *v, struct vec_env *e, const int32_t *act, int kind,
                       const struct neth_rl_state *s)
{
    if (kind != M_NONE || !(v->macros & NETH_VEC_SEEKKEY) || (v->macros & NETH_VEC_SEEK)) return;
    if (e->last.screen != PS_NONE || (e->last.flags & PF_DEAD)) return;
    int q = seek_want(v, e, s);
    e->gb[47] += q >= 0;
    e->gb[48] += q == 0;
    e->gb[49] += q < 0 && act[2 + NETH_VEC_KEY_PAUSED] > 0;
    e->gb[54] += q >= 0 && !seek_half_ok(v, &e->last, s, q);    /* lane/ladder18: the start in the policy's half */
}


/* ------------------------------------------------------------ the mod (lane/macromod: macro.h's hooks) */

/* lane/ladder16: NETH_VEC_SEEKKEY's key (read only with the bit) */
static void seek_m_observe(struct mac_step *c, struct neth_rl_act *a)
{
    c->e->skey = c->act[2 + NETH_VEC_KEY_PAUSED] > 0;
}

static int seek_m_cand(struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct neth_rl_state *s = c->s;
    int cand = 0;
    if ((v->macros & NETH_VEC_SEEK) && e->steps % 5 == 0)
        cand |= s->seen_n[0] > 0 || s->seen_n[1] > 0 || s->seen_n[3] > 0 || (s->seen_n[2] > 0 && c->r->dim == 0);
    /* lane/ladder16: NETH_VEC_SEEKKEY's key down with a kind in view (the start asks seek_want) */
    if ((v->macros & NETH_VEC_SEEKKEY) && !(v->macros & NETH_VEC_SEEK) && e->skey)
        cand |= s->seen_n[0] > 0 || s->seen_n[1] > 0 || s->seen_n[3] > 0 || (s->seen_n[2] > 0 && c->r->dim == 0);
    return cand;
}

static int seek_m_start(struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct neth_rl_state *s = c->s;
    int q;
    /* lane/ladder16: NETH_VEC_SEEKKEY, with the seek off, the policy's key 13 starts it where its start holds, any step */
    int skk = (v->macros & NETH_VEC_SEEKKEY) && !(v->macros & NETH_VEC_SEEK) && e->skey;
    /* lane/ladder18: and only where the seek's half on that cell is the macro's (actions.seek_walk, seek_dig) */
    int sok = (((v->macros & NETH_VEC_SEEK) && e->steps % 5 == 0) || skk) && (q = seek_want(v, e, s)) >= 0;
    if (sok && !seek_half_ok(v, c->r, s, q))
    {
        sok = 0;
        if (skk) e->gb[50] += 1;    /* the key down with the start holding where its half is the policy's */
    }
    if (sok)
    {
        seek_begin(e, c->r, s, q);
        if (skk)
        {
            e->skrun = 1;
            e->gb[q == 0 ? 39 : q == 1 ? 40 : 41] += 1;
        }
        return 1;
    }
    return 0;
}

static int seek_m_drive(struct mac_step *c, struct neth_rl_act *a)
{
    if (c->e->kind != M_MINE || !c->e->seek) return -1;
    return seek(c->v, c->e, c->r, c->s, a, c->ey);
}

static uint32_t seek_m_label(const struct vec_env *e) { return e->kind == M_MINE && e->seek ? NETH_VEC_SEEK : 0; }

/* lane/ladder16: the seek's first step (half 6: NETH_VEC_SEEKKEY's key), and the key's seek's books */
static int seek_m_half(struct mac_step *c, int done)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    int half = e->sfirst ? 6 : 0;
    /* lane/ladder18 (actions.seek_walk or seek_dig policy, at any macros: the teacher's labels say the split
     * too): a step in the policy's half is half 8 (its act the label), the first step of a run in the macro's
     * half half 6 (the key), the rest 0 (the level's macro); the step the seek ends 0 */
    int osp = (MOPT(MO_SEEK_WALK) ? 1 : 0) | (MOPT(MO_SEEK_DIG) ? 2 : 0);
    if (osp && e->sph)
        half = done ? 0 : (osp & e->sph) ? 8 : e->sph != e->sphl ? 6 : 0;
    if (!done && (e->sph == 1 || e->sph == 2)) e->gb[e->sph == 1 ? 51 : 52] += 1;   /* the census */
    e->sphl = e->sph;
    e->sfirst = 0;
    if (e->skrun)
    {
        e->gb[46] += 1;
        if (done) e->gb[e->swhy == 1 ? 42 : e->swhy == 2 ? 43 : e->swhy == 4 ? 53 : 44] += 1;
    }
    return half;
}

static void seek_m_end(struct vec_env *e) { e->seek = 0; }

static void seek_m_abort(struct vec_env *e)
{
    if (e->seek && e->skrun) e->gb[45] += 1;    /* lane/ladder16: the key's seek ended by a death or the lava escape */
}

/* an episode cut mid-seek leaves no seek for the next */
static void seek_m_reset(struct vec_env *e)
{
    e->seek = 0;
    e->sfirst = e->skrun = 0;               /* lane/ladder16 */
}

/* lane/ladder16 (seek_chance on): the policy's step at a SEEKKEY level (the env's own) with the seek's start holding */
static int seek_m_chance(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *st, int kind,
                         int half)
{
    const struct neth_vec *lv = e->lvl ? v->lv : v;
    if (MOPT(MO_SEEK_CHANCE) && kind == M_NONE && half == 0 && (lv->macros & NETH_VEC_SEEKKEY) &&
        !(lv->macros & NETH_VEC_SEEK) && e->last.screen == PS_NONE && !(e->last.flags & PF_DEAD))
    {
        /* lane/ladder18: where the key would start the seek (its half on the cell the macro's: seek_walk policy, the
         * cell within SEEK_DIG_REACH) */
        int q = seek_want(lv, e, st);
        if (q >= 0 && seek_half_ok(lv, &e->last, st, q)) return 7;
    }
    return half;
}

static void seek_m_books(struct mac_books *b)
{
    if (b->at == MB_DECISION) seek_books(b->e->lvl ? b->v->lv : b->v, b->e, b->act, b->kind, b->s);   /* lane/ladder16 */
}

static const struct mac_switch SEEK_SW[] = {
    {"seek", NETH_VEC_SEEK}, {"seekkey", NETH_VEC_SEEKKEY},     /* lane/ladder16: the seek started by the policy's key 13 */
    {"pick", NETH_VEC_PICK},    /* lane/wood: lane/pick's switch had no run-file name (seek_want's wood with no pickaxe) */
    {NULL, 0}};

static const struct vec_macro MAC_SEEK = {
    .name = "seek", .bit = NETH_VEC_SEEK, .sw = SEEK_SW, .observe = seek_m_observe, .cand = seek_m_cand, .start = seek_m_start,
    .drive = seek_m_drive, .label = seek_m_label, .half = seek_m_half, .end = seek_m_end, .abort = seek_m_abort,
    .reset = seek_m_reset, .books = seek_m_books, .chance = seek_m_chance,
};
