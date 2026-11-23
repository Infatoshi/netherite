/* the screen macro (lane/macromod: macro_start's and macro_drive's lines cut from vecenv.c unchanged): an open screen
 * closed (the cursor's stack thrown first), the credits left by their respawn; NETH_VEC_NOSCREEN turns it off (the
 * screen the policy's to close), NETH_VEC_NOTABLE and NETH_VEC_RECIPEKEY start the craft macro's recipe half under a
 * window the policy opened. Its label's mbit is NETH_VEC_LBL_SCREEN */

static int screen_m_cand(struct mac_step *c) { return c->r->screen != 0; }

static int screen_m_start(struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct vec_last *r = c->r;
    const struct neth_rl_state *s = c->s;
    if (r->screen != PS_NONE && r->screen != PS_GAMEOVER)
    {
        /* lane/ladder12: NETH_VEC_NOTABLE, the recipe half under a window the policy opened */
        if ((v->macros & NETH_VEC_CRAFT) && !v->picks &&
            ((v->macros & NETH_VEC_NOTABLE) ||
             ((v->macros & NETH_VEC_NORECIPE) && (v->macros & NETH_VEC_RECIPEKEY) && e->rjump)) &&
            recipe_start(v, e, r, s))
            return 1;   /* (NETH_VEC_RECIPEKEY: the policy's jump under the window, the recipe its pick) */
        /* lane/ladder10: NETH_VEC_NOSCREEN, the open screen the policy's to close (the credits' respawn kept); no
         * other macro starts under it, as none did ahead of the screen macro */
        if (!(v->macros & NETH_VEC_NOSCREEN) || r->screen == PS_CREDITS) e->kind = M_SCREEN;
        return 1;
    }
    return 0;
}

static int screen_m_drive(struct mac_step *c, struct neth_rl_act *a)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct vec_last *r = c->r;
    if (e->kind != M_SCREEN) return -1;
    if (r->screen == PS_NONE) return 1;
    if (r->screen == PS_CREDITS && (v->macros & NETH_VEC_DRAGON))
    {
        /* lane/dragon: the exit portal's credits, left as a player does (their Done: the respawn) */
        still(a);
        a->nops = 1;
        a->ops[0] = (struct neth_rl_op){PG_RESPAWN, 0, 0, 0, 0, 0};
        return e->age > 5;
    }
    if (r->cur_item >= 0 && r->cur_count > 0)
    {
        a->nops = 1;
        a->ops[0] = click(-999, 0, 0);
        return 0;
    }
    still(a);
    a->nops = 1;
    a->ops[0] = CLOSE;
    return e->age > 5;
}

static uint32_t screen_m_label(const struct vec_env *e) { return e->kind == M_SCREEN ? NETH_VEC_LBL_SCREEN : 0; }

static const struct mac_switch SCREEN_SW[] = {{"noscreen", NETH_VEC_NOSCREEN}, {NULL, 0}};    /* lane/ladder10: it off */

static const struct vec_macro MAC_SCREEN = {
    .name = "screen", .bit = 0, .sw = SCREEN_SW, .cand = screen_m_cand, .start = screen_m_start, .drive = screen_m_drive,
    .label = screen_m_label,
};
