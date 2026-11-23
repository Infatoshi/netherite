/* lane/ladder6: the removed macro as the labeller (included by vecenv.c; neth_vec_level in vecenv.h).
 *
 * An env with lvl set plays at the student's level (v->lv's macros: the student drives, its own macros start as they
 * would at play), and on each of its decision steps the labeller (today the seek, NETH_VEC_SEEK) runs beside it on the
 * same state with its own copy of what it keeps between steps (struct vec_shd: the kind, the age, the seek's fields,
 * its memo of cells given up). Its act is computed and never stepped: where it would act, it replaces the step's label
 * (neth_vec_label: mbit NETH_VEC_SEEK, flags NETH_VEC_LBL_SHADOW, the camera bins and keys of its act as vec_label
 * makes any macro's), so the student is told what the macro would do on the states the student itself reaches.
 *
 * When it would act: the step's own macros left the step to the policy (kind M_NONE: a macro of the level driving
 * the step, a screen or death ends the labeller's run, as a running macro would have kept the seek from starting);
 * its start as macro_start's (every 5th step, seek_want on the labeller's macros: the level and the seek, so its reach
 * and its crouch are the L2 seek's, which the student played with); its run as macro_drive's (the age counted, seek's
 * return ending it). The step a run ends is not labelled (the policy's step there, as the label already says). */

/* the labeller's state into the env's fields and back (the same call both ways) */
static void shd_swap(struct vec_env *e)
{
    struct vec_shd *d = &e->shd;
#define SHD_SW(f)                                                                                                     \
    do                                                                                                                \
    {                                                                                                                 \
        __typeof__(e->f) t_ = e->f;                                                                                   \
        e->f = d->f;                                                                                                  \
        d->f = t_;                                                                                                    \
    } while (0)
    SHD_SW(kind);
    SHD_SW(age);
    SHD_SW(seek);
    SHD_SW(gx);
    SHD_SW(gy);
    SHD_SW(gz);
    SHD_SW(ggain);
    SHD_SW(gstuck);
    SHD_SW(gbefore);
    SHD_SW(gon);
    SHD_SW(gdug);
    SHD_SW(ntried);
    SHD_SW(gbest);
    SHD_SW(gpx);
    SHD_SW(gpz);
    SHD_SW(sfirst);     /* lane/ladder18: seek_begin and seek write these too (the walk labeller beside the key's seek) */
    SHD_SW(skrun);
    SHD_SW(swhy);
    SHD_SW(sph);
    SHD_SW(sphl);
#undef SHD_SW
    struct vec_tried t[TRIED];
    int n = e->ntried > d->ntried ? e->ntried : d->ntried;   /* (already swapped: the larger count's entries) */
    if (n > TRIED) n = TRIED;
    memcpy(t, e->tried, (size_t)n * sizeof t[0]);
    memcpy(e->tried, d->tried, (size_t)n * sizeof t[0]);
    memcpy(d->tried, t, (size_t)n * sizeof t[0]);
}

/* lane/ladder9: the surface macro as the labeller (L3b, 8186, removes it). Its run starts as macro_start's surface does
 * (the eye in water, air under 120, on any step the policy has), and its act is the macro's swim (surface_swim, the
 * escape aside: the strip is off at the levels that remove the surface), its age its own; it ends where the swim's
 * return says. The step a run ends is not labelled. */
static void shd_surface(const struct neth_vec *vb, struct vec_env *e, const struct neth_rl_state *s,
                        struct neth_vec_label *l)
{
    const struct vec_last *r = &e->last;
    struct vec_shd *d = &e->shd;
    if (!d->surf && s->eye_water && r->air < 120 && !(r->flags & PF_DEAD))
    {
        d->surf = 1;
        d->sage = 0;
    }
    if (!d->surf) return;
    struct neth_rl_act a;
    act_noop(&a);
    int age = e->age;
    e->age = ++d->sage;
    int over = surface_swim(vb, e, r, s, &a);
    e->age = age;
    if (over)
    {
        d->surf = d->sage = 0;
        return;
    }
    struct neth_vec_label sl;
    vec_label(vb, e, s, &a, M_SURFACE, 0, &sl);
    sl.kind = l->kind;
    sl.mbit = NETH_VEC_SURFACE;
    sl.flags |= NETH_VEC_LBL_SHADOW;
    *l = sl;
}

/* lane/ladder11: the craft macro as the labeller (L4b, 33562610, removes it; actions.gui cursor makes its every op a step
 * the 16-wide action says). Beside an env playing at the level, the craft macro's state machine (vec_shc: its plan,
 * phase, flags, op list and queue, swapped into the env's fields around craft()) runs on the student's own state, never
 * stepped, and its act is the step's label (mbit NETH_VEC_CRAFT, NETH_VEC_LBL_SHADOW):
 * - its start: on a policy step with no screen open, craft_plan on the labeller's macros (the level and craft) names a
 *   plan of the crafting kind (SHC_PLAN: the 2x2 and 3x3 recipes, the smelt; not the bucket, portal, frame, throw or
 *   take plans): the macro from CP_OPEN, as macro_start starts it (on any step, not every 5th: a restart after the
 *   student's own act should not wait). Under the inventory, crafting or furnace screen the student opened (or left
 *   in a state of its own), the window is tidied first, one op a step (the cursor's stack put in an empty inventory
 *   slot, else thrown out; then each grid slot holding anything shift clicked back), then the plan's clicks from
 *   CP_CLICKS where the plan fits the screen (a 2x2 plan the inventory, a 3x3 one the table, the smelt the furnace);
 *   where none fits, or there is no plan, the screen closed (key 12);
 * - each step after: the macro's next act from its state; its GUI ops one a step (its own queue, as decode's);
 * - sync: the student's act stepped against the label's. Where they agree on what changes the game (no screen: use,
 *   the inventory key, drop, the attack held, the hotbar; under a screen: the op), the macro's state stays advanced.
 *   Where they differ and the student's act changed nothing the macro tracks (no click, no use, no inventory key, no
 *   drop), the macro's state is rolled back to before the step (it did not act: a press it labelled is labelled
 *   again); where the student did change something, the run ends and the next step starts again from the state;
 * - it remembers the table and the furnace the crosshair shows (a player sees the table it placed itself), where the
 *   macro remembers only the ones it used. A level macro's step (kind not M_NONE) or a death ends the run. */
#define SHC_PLAN(p) plan_gui(p)    /* lane/ladder12: vecenv.c's, shared with the craft macro's halves */

static void shc_swap(struct vec_env *e)
{
    struct vec_shc *d = &e->shd.c;
#define SHC_SW2(ef, df)                                                                                               \
    do                                                                                                                \
    {                                                                                                                 \
        __typeof__(e->ef) t_ = e->ef;                                                                                 \
        e->ef = d->df;                                                                                                \
        d->df = t_;                                                                                                   \
    } while (0)
#define SHC_SW(f) SHC_SW2(f, f)
    SHC_SW2(age, cage);
    SHC_SW(cplan);
    SHC_SW(cphase);
    SHC_SW(pressed);
    SHC_SW(used);
    SHC_SW(aimed);
    SHC_SW(placed);
    SHC_SW(has_ops);
    SHC_SW(nops);
    SHC_SW(op_at);
    SHC_SW(ccap);
    SHC_SW(pdir);
    SHC_SW(ptries);
    SHC_SW(pdig);
    SHC_SW(pdx);
    SHC_SW(pdy);
    SHC_SW(pdz);
    SHC_SW(has_table);
    SHC_SW(has_furnace);
    SHC_SW(tx);
    SHC_SW(ty);
    SHC_SW(tz);
    SHC_SW(fx);
    SHC_SW(fy);
    SHC_SW(fz);
#undef SHC_SW
#undef SHC_SW2
    struct neth_rl_op t[CRAFT_OPS];
    memcpy(t, e->ops, sizeof t);
    memcpy(e->ops, d->ops, sizeof t);
    memcpy(d->ops, t, sizeof t);
}

/* the window tidied: one op into a (1), or 0 clean: the cursor's stack into the first empty inventory slot (else thrown
 * out of the window), then the crafting grid's slots shift clicked back */
static int shc_tidy(const struct vec_last *r, struct neth_rl_act *a)
{
    int n = r->nslots > 90 ? 90 : r->nslots, inv0 = n - 36;
    if (inv0 < 0) return 0;
    if (r->cur_item >= 0 && r->cur_count > 0)
    {
        int to = -999;
        for (int k = inv0; k < n && to == -999; ++k)
            if (r->slot_item[k] < 0 || r->slot_count[k] <= 0) to = k;
        still(a);
        a->nops = 1;
        a->ops[0] = click(to, 0, 0);
        return 1;
    }
    int grid = r->screen == PS_INVENTORY ? 4 : r->screen == PS_CRAFTING ? 9 : 0;
    for (int k = 1; k <= grid && k < inv0; ++k)
        if (r->slot_item[k] >= 0 && r->slot_count[k] > 0)
        {
            still(a);
            a->nops = 1;
            a->ops[0] = click(k, 0, 1);
            return 1;
        }
    return 0;
}

/* the use key active in an act (pressed, or held: a held use repeats) */
static int shc_use(const struct neth_rl_act *a) { return a->press[PK_USE] || ((a->hold >> PK_USE) & 1); }

/* the student's act sa and the labeller's a agree on what changes the game (screen sc open) */
static int shc_same(int sc, int cur, const struct neth_rl_act *sa, const struct neth_rl_act *a)
{
    if (gui_screen(sc))
    {
        if ((sa->nops > 0) != (a->nops > 0)) return 0;
        if (!a->nops) return 1;
        const struct neth_rl_op *p = &sa->ops[0], *q = &a->ops[0];
        return p->kind == q->kind && (p->kind != PG_CLICK || (p->slot == q->slot && p->button == q->button &&
                                                               p->mode == q->mode));
    }
    int hs = sa->hotbar >= 0 && sa->hotbar != cur ? sa->hotbar : -1, ha = a->hotbar >= 0 && a->hotbar != cur ? a->hotbar : -1;
    return shc_use(sa) == shc_use(a) && sa->press[PK_INVENTORY] == a->press[PK_INVENTORY] &&
           sa->press[PK_DROP] == a->press[PK_DROP] && ((sa->hold >> PK_ATTACK) & 1) == ((a->hold >> PK_ATTACK) & 1) &&
           hs == ha;
}

/* the student's act changed something the macro tracks: a GUI op, use, the inventory key, a drop */
static int shc_changed(int sc, const struct neth_rl_act *sa)
{
    if (gui_screen(sc)) return sa->nops > 0;
    return shc_use(sa) || sa->press[PK_INVENTORY] || sa->press[PK_DROP] || sa->nops > 0;
}

/* plan p's open (open_bench, open_furnace with NETH_VEC_IRON) would fetch its block from the main inventory under the
 * inventory screen: the inventory open, the block in no hotbar slot and in the main inventory, none remembered near */
static int shc_fetch(const struct neth_vec *vb, const struct vec_env *e, const struct vec_last *r,
                     const struct neth_rl_state *s, int p)
{
    if (r->screen != PS_INVENTORY || !(vb->macros & NETH_VEC_IRON) || !(p == C_SMELT || plan_bench(p))) return 0;
    const struct vec_shc *d = &e->shd.c;
    int item = p == C_SMELT ? FURNACE : TABLE;
    int near = p == C_SMELT ? d->has_furnace && py_dist3(r->x, r->y, r->z, d->fx, d->fy, d->fz) < 4.0
                            : d->has_table && py_dist3(r->x, r->y, r->z, d->tx, d->ty, d->tz) < 4.0;
    if (near) return 0;
    int hot = 0, main = 0;
    for (int i = 0; i < 36; ++i)
        if (s->inv[i].item == item && s->inv[i].count > 0)
        {
            if (i < 9) hot = 1;
            else main = 1;
        }
    return !hot && main;
}

/* the craft macro's plan at the labeller's macros (its memory of the table swapped in) */
static int shc_plan(const struct neth_vec *vb, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s)
{
    shc_swap(e);
    int p = craft_plan(vb, e, r, s);
    shc_swap(e);
    return p;
}

static void shc_begin(const struct neth_vec *vb, struct vec_env *e, const struct neth_rl_state *s, int plan, int phase)
{
    struct vec_shc *d = &e->shd.c;
    d->con = 1;
    d->cage = 0;
    d->cplan = plan;
    d->ccap = plan == C_SMELT ? 80 + 210 * (int)inv_count(s, IRON_ORE) : (vb->macros & NETH_VEC_IRON) ? 160 : 80;
    d->cphase = phase;
    d->pressed = d->used = d->aimed = d->placed = 0;
    d->pdir = -1;
    d->ptries = d->pdig = 0;
    d->has_ops = 0;
    d->gq_n = d->gq_at = 0;
    e->shd.cb[SHC_START] += 1;
}

/* the labeller's act on this step into a: 1 a label (*src its kind: SHC_RUN, SHC_TIDY, SHC_CLOSE), 0 none */
static int shc_act(const struct neth_vec *vb, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                   struct neth_rl_act *a, int *src)
{
    struct vec_shc *d = &e->shd.c;
    int sc = r->screen;
    *src = SHC_RUN;
    if (d->gq_at < d->gq_n)
    {
        if (gui_screen(sc))
        {
            still(a);
            a->nops = 1;
            a->ops[0] = d->gq[d->gq_at++];
            return 1;
        }
        d->gq_n = d->gq_at = 0;     /* the screen closed: the rest dropped, as decode drops it */
    }
    if (!d->con)
    {
        if (sc == PS_NONE)
        {
            int p = shc_plan(vb, e, r, s);
            if (p < 0 || !SHC_PLAN(p)) return 0;
            shc_begin(vb, e, s, p, CP_OPEN);
        }
        else if (sc == PS_INVENTORY || sc == PS_CRAFTING || sc == PS_FURNACE)
        {
            if (shc_tidy(r, a))
            {
                *src = SHC_TIDY;
                return 1;
            }
            int p = shc_plan(vb, e, r, s);
            int fits = plan_fits(p, sc);
            if (!fits && p >= 0 && SHC_PLAN(p) && shc_fetch(vb, e, r, s, p))
            {
                /* the plan's table (or furnace) to be fetched to the hotbar under this inventory screen: the macro from
                 * CP_OPEN, whose fetch_item makes the swap and closes (the screen closed here instead, with the open
                 * labelled outside it, was an open and close loop: L4bC2-s1, 1,005 of each a run at 33M) */
                shc_begin(vb, e, s, p, CP_OPEN);
                d->pressed = 1;
                goto run;
            }
            if (!fits)
            {
                still(a);
                a->nops = 1;
                a->ops[0] = CLOSE;
                *src = SHC_CLOSE;
                return 1;
            }
            int busy = p == C_SMELT && r->nslots > 0 && r->slot_item[0] >= 0 && r->slot_count[0] > 0;
            shc_begin(vb, e, s, p, busy ? CP_WAIT : CP_CLICKS);
        }
        else
            return 0;
    }
run:
    act_noop(a);
    shc_swap(e);
    e->age += 1;
    int done = craft(vb, e, r, s, a, r->y + 1.62);
    shc_swap(e);
    if (a->nops > 1)
    {
        d->gq_n = a->nops - 1;
        d->gq_at = 0;
        for (int j = 1; j < a->nops; ++j) d->gq[j - 1] = a->ops[j];
        a->nops = 1;
    }
    if (done)
    {
        d->con = 0;
        e->shd.cb[SHC_DONE] += 1;
        /* the step a run ends labels only an act it makes (the age cap's close) */
        int any = a->nops > 0 || a->hold || a->hotbar >= 0 || a->look_mode;
        for (int k = 0; k < NETH_RL_KEYS && !any; ++k) any = a->press[k];
        return any;
    }
    return 1;
}

static void shc_step(const struct neth_vec *vb, struct vec_env *e, const struct neth_rl_state *s, int kind,
                     const struct neth_rl_act *sa, struct neth_vec_label *l)
{
    const struct vec_last *r = &e->last;
    struct vec_shd *h = &e->shd;
    struct vec_shc *d = &h->c;
    /* the student's key 12 and use on its last step, and this step's for the next (decode and vec_act have already
     * made prev_hold and prev_keys this step's) */
    int pk12 = h->pk12, puse = h->puse;
    h->pk12 = e->prev_keys[NETH_VEC_KEY_INV] > 0;
    h->puse = (e->prev_hold >> PK_USE) & 1;
    /* what the crosshair shows remembered (open_bench's and open_furnace's memory, as the macro keeps for its own) */
    if (s->mo_hit && s->mo_block == TABLE)
        d->has_table = 1, d->tx = s->mo_x + 0.5, d->ty = s->mo_y + 0.5, d->tz = s->mo_z + 0.5;
    if (s->mo_hit && (s->mo_block == FURNACE || s->mo_block == FURNACE_LIT))
        d->has_furnace = 1, d->fx = s->mo_x + 0.5, d->fy = s->mo_y + 0.5, d->fz = s->mo_z + 0.5;
    if (kind != M_NONE || (r->flags & PF_DEAD) || !(l->flags & NETH_VEC_LBL_VALID))
    {
        d->con = 0;     /* the level's macro has the step (or the player is dead): the labeller gives way */
        d->gq_n = d->gq_at = 0;
        return;
    }
    struct vec_shc save = *d;
    struct neth_rl_act a;
    act_noop(&a);
    int src;
    if (!shc_act(vb, e, r, s, &a, &src)) return;
    if (src == SHC_RUN)
    {
        /* the books by plan and by what the label asks (the inventory key, use, a slot change, a GUI op, else the look
         * or nothing), and the student's equal acts by plan */
        int pl = d->cplan >= 0 && d->cplan < SHC_NPLAN ? d->cplan : SHC_NPLAN - 1;
        int what = a.press[PK_INVENTORY] ? 0 : shc_use(&a) ? 1 : a.nops ? 3 : a.hotbar >= 0 && a.hotbar != s->current ? 2 : 4;
        h->cp[pl][what] += 1;
        if (shc_same(r->screen, s->current, sa, &a)) h->cp[pl][5] += 1;
        if (shc_same(r->screen, s->current, sa, &a))
            h->cb[SHC_SAME] += 1;
        else if (shc_changed(r->screen, sa))
        {
            d->con = 0;     /* the student changed what the macro tracks: started again from the next state */
            d->gq_n = d->gq_at = 0;
            h->cb[SHC_RESET] += 1;
        }
        else
        {
            int ht = d->has_table, hf = d->has_furnace;     /* (the memory is the state's, never rolled back) */
            double m[6] = {d->tx, d->ty, d->tz, d->fx, d->fy, d->fz};
            *d = save;  /* the macro did not act: its state as before the step */
            d->has_table = ht, d->has_furnace = hf;
            d->tx = m[0], d->ty = m[1], d->tz = m[2], d->fx = m[3], d->fy = m[4], d->fz = m[5];
            h->cb[SHC_ROLLBACK] += 1;
        }
    }
    h->cb[src] += 1;
    struct neth_vec_label sl;
    vec_label(vb, e, s, &a, M_CRAFT, 0, &sl);
    sl.kind = l->kind;
    sl.mbit = NETH_VEC_CRAFT;
    sl.flags |= NETH_VEC_LBL_SHADOW;
    /* a press is made on the key's down step (gui_policy's key 12; decode's use, outside a screen): where the student
     * held the key the step before, the act that presses it next is letting go now, so the label is the release (the
     * first collapse, L4bC-s1: the press labelled every step it was not made taught key 12 held, which opens the
     * inventory once and never closes it: 20,448 of 24,000 steps a run under a screen at 65M) */
    if ((sl.keys >> NETH_VEC_KEY_INV & 1) && pk12)
    {
        sl.keys &= (uint16_t)~(1u << NETH_VEC_KEY_INV);
        h->cb[SHC_RELEASE] += 1;
    }
    if (r->screen == PS_NONE && (sl.keys >> 8 & 1) && puse)
    {
        sl.keys &= (uint16_t)~(1u << 8);
        h->cb[SHC_RELEASE] += 1;
    }
    *l = sl;
}

/* lane/ladder18 (neth_vec_level_walk; actions.seek_walk policy): the seek's walk as the labeller, beside the surface
 * labeller on a level episode's policy step. Its run starts on any step where seek_want holds (on the labeller's
 * macros: the level's seek_want, unchanged) on a cell farther than SEEK_DIG_REACH, as the key's seek would at the
 * teacher's macros; it runs as seek() does with its own fields and memo of cells given up (shd_swap), never stepped;
 * where it acts in the walk half its act is the label (mbit NETH_VEC_SEEK, NETH_VEC_LBL_SHADOW, half 8). It ends where
 * the seek ends, or on the step its cell comes within reach (the dig half: there seek_chance labels the key), or when
 * the surface labeller has the step. The step it ends is not labelled */
static void shd_walk(const struct neth_vec *vb, struct vec_env *e, const struct neth_rl_state *s,
                     struct neth_vec_label *l)
{
    const struct vec_last *r = &e->last;
    if (!(l->flags & NETH_VEC_LBL_VALID) || (l->flags & NETH_VEC_LBL_SHADOW) || r->screen != PS_NONE ||
        (r->flags & PF_DEAD))
    {
        e->shd.kind = e->shd.age = e->shd.seek = 0;
        return;
    }
    struct neth_rl_act a;
    int lab = 0;
    shd_swap(e);
    if (!e->seek)
    {
        int q = seek_want(vb, e, s);
        if (q >= 0 && seek_half_at(r, s->seen[q].x, s->seen[q].y, s->seen[q].z) == 1)
        {
            e->age = 0;
            seek_begin(e, r, s, q);
        }
    }
    if (e->seek)
    {
        act_noop(&a);
        e->age += 1;
        if (seek(vb, e, r, s, &a, r->y + 1.62) || e->sph != 1)
        {
            e->kind = M_NONE;
            e->age = 0;
            e->seek = 0;
        }
        else
            lab = 1;
    }
    shd_swap(e);
    if (!lab) return;
    struct neth_vec_label sl;
    vec_label(vb, e, s, &a, M_MINE, 0, &sl);   /* M_MINE: the env's own tool pick on the attack is not the label's */
    sl.kind = l->kind;
    sl.mbit = NETH_VEC_SEEK;
    sl.half = 8;
    sl.flags |= NETH_VEC_LBL_SHADOW;
    *l = sl;
}

/* the labeller on env e's step (after decode and vec_label): vb the level's and the labeller's macros, kind the macro
 * that drove the step (M_NONE the policy), sa the act stepped, l the step's label, replaced where the labeller acts */
static void shd_step(const struct neth_vec *vb, struct vec_env *e, const struct neth_rl_state *s, int kind,
                     const struct neth_rl_act *sa, struct neth_vec_label *l)
{
    if (vb->lbits == NETH_VEC_CRAFT)
    {
        shc_step(vb, e, s, kind, sa, l);    /* lane/ladder11 */
        return;
    }
    if (kind != M_NONE)
    {
        e->shd.kind = e->shd.age = e->shd.seek = 0;     /* the level's macro has the step: the labeller gives way */
        e->shd.surf = e->shd.sage = 0;
        return;
    }
    if (vb->lbits == NETH_VEC_SURFACE)
    {
        if (l->flags & NETH_VEC_LBL_VALID) shd_surface(vb, e, s, l);
        if (vb->lwalk) shd_walk(vb, e, s, l);     /* lane/ladder18 */
        return;
    }
    if (!(vb->macros & NETH_VEC_SEEK) || !(l->flags & NETH_VEC_LBL_VALID)) return;
    const struct vec_last *r = &e->last;
    struct neth_rl_act a;
    int lab = 0;
    shd_swap(e);
    if (!e->seek && e->steps % 5 == 0 && r->screen == PS_NONE && !(r->flags & PF_DEAD))
    {
        int q = seek_want(vb, e, s);
        /* neth_vec_level_reach: only a cell within the bound starts a run (the student far from it labelled nothing) */
        if (q >= 0 && (vb->lreach <= 0 || s->seen[q].d <= vb->lreach))
        {
            e->age = 0;
            seek_begin(e, r, s, q);
        }
    }
    if (e->seek)
    {
        act_noop(&a);
        e->age += 1;
        if (seek(vb, e, r, s, &a, r->y + 1.62))
        {
            e->kind = M_NONE;
            e->age = 0;
            e->seek = 0;
        }
        else
            lab = 1;
    }
    shd_swap(e);
    if (!lab) return;
    if (vb->lalign > 0 && a.look_mode == 1 && ((a.hold >> PK_ATTACK) & 1))
    {
        /* neth_vec_level_align: the seek digs what the crosshair meets within reach because its own last step aimed
         * the look along the line to its cell; on the student's state the crosshair is wherever the student left it,
         * so the attack stays in the label only with the look already within the bound of the seek's aim (the label
         * otherwise the turn alone) */
        double dy = fmod((double)a.look[0] - (double)e->last.yaw, 360.0);
        if (dy >= 180.0) dy -= 360.0;
        if (dy < -180.0) dy += 360.0;
        double dp = (double)a.look[1] - (double)e->last.pitch;
        int on = s->mo_hit && s->mo_x == e->shd.gx && s->mo_y == e->shd.gy && s->mo_z == e->shd.gz;   /* its cell */
        if (!on && (fabs(dy) > vb->lalign || fabs(dp) > vb->lalign))
        {
            a.hold &= ~(1u << PK_ATTACK);
            a.press[PK_ATTACK] = 0;
            a.hotbar = -1;
        }
    }
    struct neth_vec_label sl;
    vec_label(vb, e, s, &a, M_MINE, 0, &sl);   /* M_MINE: the env's own tool pick on the attack is not the label's */
    sl.kind = l->kind;
    sl.mbit = NETH_VEC_SEEK;
    sl.flags |= NETH_VEC_LBL_SHADOW;
    *l = sl;
}

int neth_vec_level(struct neth_vec *v, int level, int labeller, char *err, int en)
{
    if (labeller != NETH_VEC_SEEK && labeller != NETH_VEC_SURFACE && labeller != NETH_VEC_CRAFT)
    {
        seterr(err, en, "neth_vec_level: the labeller is the seek (4096), the surface (1) or craft (8)%s", "");
        return -1;
    }
    if (v->lv) free(v->lv);
    if (v->lb) free(v->lb);
    v->lv = malloc(sizeof *v);
    v->lb = malloc(sizeof *v);
    if (!v->lv || !v->lb)
    {
        seterr(err, en, "neth_vec_level: no memory%s", "");
        return -1;
    }
    *v->lv = *v;
    v->lv->lv = v->lv->lb = NULL;
    v->lv->macros = (v->macros & level) | (level & NETH_VEC_OFF_BITS);   /* lane/ladder10: a switch that turns off
                                                                             (ladder12: the craft macro's halves too) */
    v->lv->picks = v->picks && (v->lv->macros & NETH_VEC_CRAFT);
    *v->lb = *v->lv;
    v->lb->macros = (v->macros & (level | labeller)) | (level & NETH_VEC_NOSCREEN);
    v->lbits = v->lb->lbits = labeller;     /* lane/ladder9: shd_step reads the labeller's copy's */
    return 0;
}

int neth_vec_shc_books(const struct neth_vec *v, int64_t *out)
{
    _Static_assert(SHC_NB == NETH_VEC_SHC_BOOKS, "the craft labeller's books");
    if (!v || !out) return -1;
    memset(out, 0, SHC_NB * sizeof *out);
    for (int i = 0; i < v->n; ++i)
        for (int k = 0; k < SHC_NB; ++k) out[k] += v->e[i].shd.cb[k];
    return 0;
}

int neth_vec_shc_plans(const struct neth_vec *v, int64_t *out)
{
    _Static_assert(SHC_NPLAN * 6 == NETH_VEC_SHC_PLANS, "the craft labeller's plan books");
    if (!v || !out) return -1;
    memset(out, 0, SHC_NPLAN * 6 * sizeof *out);
    for (int i = 0; i < v->n; ++i)
        for (int p = 0; p < SHC_NPLAN; ++p)
            for (int k = 0; k < 6; ++k) out[p * 6 + k] += v->e[i].shd.cp[p][k];
    return 0;
}

int neth_vec_level_align(struct neth_vec *v, double deg)
{
    if (!v->lb || !(deg >= 0)) return -1;
    v->lb->lalign = deg;
    return 0;
}

int neth_vec_level_reach(struct neth_vec *v, double blocks)
{
    if (!v->lb || !(blocks >= 0)) return -1;
    v->lb->lreach = blocks;
    return 0;
}

int neth_vec_level_walk(struct neth_vec *v, int on)
{
    if (!v->lb || v->lb->lbits != NETH_VEC_SURFACE) return -1;   /* beside the surface labeller (l5aseek.yaml's) */
    v->lb->lwalk = on != 0;
    return 0;
}

int neth_vec_env_level(struct neth_vec *v, int id, int on)
{
    if (id < 0 || id >= v->n || (on && !v->lv)) return -1;
    struct vec_env *e = &v->e[id];
    if (e->lvl != (on != 0))
    {
        e->shd.kind = e->shd.age = e->shd.seek = e->shd.ntried = e->shd.surf = e->shd.sage = 0;
        memset(&e->shd.c, 0, sizeof e->shd.c);  /* lane/ladder11 */
    }
    e->lvl = on != 0;
    return 0;
}
