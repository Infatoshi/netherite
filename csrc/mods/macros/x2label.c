/* the craft macro's 2x2 labeller (lane/ladder17's x2_label, carried unchanged from vecenv.c by lane/macromod; called
 * from craft.c's books at MB_LABEL) */

/* lane/ladder17 (actions.recipe_2x2 clicks; v the env's level's copy, with NETH_VEC_NORECIPE): the 2x2 labeller beside
 * the policy's own steps under the inventory, and its books (vecenv.h NETH_VEC_GUI_BOOKS [55] to [64]). On a policy
 * step (no macro) under the inventory: a run starts where the window is tidy (nothing on the cursor, the grid empty)
 * and craft_plan names a 2x2 plan: its ops are the craft macro's own (recipe_ops: the part's stack picked up, put in
 * the grid or one in each of its cells, the rest put back; the result shift clicked), one a step, labelled in order;
 * the step after the result is taken is labelled the close, as the macro's CP_CLOSE is (the next plan's window is
 * the table key's again). Where the policy's op on a step differs from the label's, the run is left and every step
 * after it under that window is labelled the close (key 12; no key where key 12 was down the step before, since only
 * its edge closes); a step with no op keeps the label. The close too where the window is untidy with no run, or tidy
 * with no 2x2 plan. With l (the env plays at the student's level: neth_vec_level) the label is the step's (mbit
 * NETH_VEC_CRAFT, half 9, NETH_VEC_LBL_SHADOW: the distiller keeps it on the student's own episodes); without, the
 * books alone (the frozen eval at the level). Labels made with the books' gk12 of the step before: call it before
 * gui_books */
static void x2_label(const struct neth_vec *v, struct vec_env *e, const struct neth_rl_state *s, const struct neth_rl_act *a,
                     int kind, struct neth_vec_label *l)
{
    const struct vec_last *r = &e->last;
    if (r->screen != PS_INVENTORY || kind != M_NONE || (r->flags & PF_DEAD))
    {
        if (r->screen != PS_INVENTORY) e->x2st = 0;
        return;
    }
    int n = r->nslots > 90 ? 90 : r->nslots, tidy = !(r->cur_item >= 0 && r->cur_count > 0);
    for (int k = 1; k <= 4 && k < n; ++k)
        if (r->slot_item[k] >= 0 && r->slot_count[k] > 0) tidy = 0;
    int pop = a->nops == 1 ? a->ops[0].kind : -1;     /* the policy's op this step (PG_CLICK, PG_CLOSE) */
    e->gb[57] += 1;
    e->gb[58] += pop < 0;
    e->gb[59] += pop == PG_CLICK;
    if (pop == PG_CLICK && a->ops[0].slot == 0 && a->ops[0].mode == 1 && n > 0 && r->slot_item[0] >= 0 &&
        r->slot_count[0] > 0)
        e->gb[55] += 1;     /* a 2x2 craft by the policy's clicks: the result shift clicked */
    if (pop == PG_CLICK && a->ops[0].slot == 0 && a->ops[0].mode == 0 && n > 0 && r->slot_item[0] >= 0 &&
        r->slot_count[0] > 0)
        e->gb[56] += 1;     /* the result picked up by a plain click (onto the cursor) */
    if (e->x2st == 0 && tidy)
    {
        int p = craft_plan(v, e, r, s);
        if (plan_2x2(p))
        {
            struct neth_rl_op sv[CRAFT_OPS];
            int svn = e->nops, sva = e->op_at;
            memcpy(sv, e->ops, sizeof sv);
            if (recipe_ops(v, e, s, p) == 0 && e->nops > 0 && e->nops <= CRAFT_OPS)
            {
                memcpy(e->x2ops, e->ops, sizeof e->x2ops);
                e->x2n = e->nops;
                e->x2at = 0;
                e->x2st = 1;
                e->x2plan = p;
                e->gb[60] += 1;
            }
            memcpy(e->ops, sv, sizeof sv);
            e->nops = svn, e->op_at = sva;
        }
    }
    int run = e->x2st == 1 && e->x2at < e->x2n;
    struct neth_rl_op op = run ? e->x2ops[e->x2at] : CLOSE;
    int nokey = !run && e->gk12;    /* the close where key 12 was down the step before: no key (its release) */
    if (!run) e->gb[63] += 1;
    if (!run && pop == PG_CLOSE) e->gb[64] += 1;
    if (run)
    {
        const struct neth_rl_op *q = &a->ops[0];
        /* another stack of the part (x2_source any; [72] counts them either way): a plain left click on a slot past
         * the armour where the label's is one too, holding the label's source item, to pick the part up (the cursor
         * empty, at least the grid cells that follow) or to put the rest back (an empty slot or the same item) */
        int sub = 0;
        if (pop == PG_CLICK && q->slot != op.slot && q->mode == 0 && q->button == 0 && op.mode == 0 && op.button == 0 &&
            op.slot >= 9 && q->slot >= 9 && op.slot < n && q->slot < n)
        {
            int cur = r->cur_item >= 0 && r->cur_count > 0, need = 0;
            for (int j = e->x2at + 1; j < e->x2n && e->x2ops[j].slot >= 1 && e->x2ops[j].slot <= 4; ++j) ++need;
            int qi = r->slot_item[q->slot], qc = r->slot_count[q->slot];
            if (!cur)
                sub = qi >= 0 && qc >= (need > 0 ? need : 1) && qi == r->slot_item[op.slot];
            else
                sub = qi < 0 || qc <= 0 || qi == r->cur_item;
            e->gb[72] += sub;
        }
        if (sub && MOPT(MO_X2_SOURCE) == X2S_ANY)
        {
            for (int j = e->x2at + 1; j < e->x2n; ++j)
                if (e->x2ops[j].slot == op.slot) e->x2ops[j].slot = q->slot;
            if (++e->x2at == e->x2n)
            {
                e->x2st = 2;
                e->gb[61] += 1;
            }
        }
        else if (pop == PG_CLICK && q->slot == op.slot && q->button == op.button && q->mode == op.mode)
        {
            if (++e->x2at == e->x2n)
            {
                e->x2st = 2;
                e->gb[61] += 1;
            }
        }
        else if (pop >= 0)
        {
            e->x2st = 3;
            e->gb[62] += 1;
            /* where and how the run is left: [66] at its first op, [67] a middle one, [68] the result's; [69] by a
             * close, [70] a click on the label's slot with another button or mode, [71] a click on another slot */
            e->gb[e->x2at == 0 ? 66 : e->x2at == e->x2n - 1 ? 68 : 67] += 1;
            e->gb[pop == PG_CLOSE ? 69 : q->slot == op.slot ? 70 : 71] += 1;
        }
    }
    struct neth_vec_label t;
    memset(&t, 0, sizeof t);
    t.kind = (uint8_t)kind;
    t.cam[0] = t.cam[1] = (int8_t)(NETH_VEC_CAM / 2);
    t.flags = (uint8_t)(((l ? l->flags : NETH_VEC_LBL_VALID) & (NETH_VEC_LBL_VALID | NETH_VEC_LBL_FITS)) |
                        NETH_VEC_LBL_SHADOW);
    t.mbit = NETH_VEC_CRAFT;
    t.half = 9;
    int said = 1;
    if (nokey)
        ;
    else if (op.kind == PG_CLOSE)
        t.keys = 1u << NETH_VEC_KEY_INV;
    else
    {
        int cell = gui_cell(r->screen, r->nslots, op.slot);
        if (cell < 0)
        {
            t.flags = (uint8_t)(t.flags & ~NETH_VEC_LBL_VALID);    /* (never: recipe_ops' slots are cells) */
            said = 0;
        }
        else
        {
            t.cam[0] = (int8_t)(NETH_VEC_GUI_B0 + cell / NETH_VEC_GUI_COLS);
            t.cam[1] = (int8_t)(NETH_VEC_GUI_B0 + cell % NETH_VEC_GUI_COLS);
            t.keys = op.mode == 1 ? (1u << 7) | (1u << 6) : op.button == 1 ? 1u << 8 : 1u << 7;
        }
    }
    if (said && !nokey)
    {
        /* the label decoded as a policy's action (a press after a release) makes the op: else a bug ([65]) */
        int32_t qa[NETH_VEC_ACTION + 1] = {0};
        qa[0] = t.cam[0];
        qa[1] = t.cam[1];
        for (int j = 0; j < NETH_VEC_KEYS; ++j) qa[2 + j] = (t.keys >> j) & 1;
        struct neth_rl_act b;
        act_noop(&b);
        gui_policy(v, r, qa, 0, &b);
        int same = b.nops == 1 && b.ops[0].kind == op.kind &&
                   (op.kind == PG_CLOSE || (b.ops[0].slot == op.slot && b.ops[0].button == op.button &&
                                            b.ops[0].mode == op.mode));
        e->gb[65] += !same;
    }
    if (l) *l = t;
}
