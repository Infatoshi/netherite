/* NETH_VEC_END (lane/throweye; lane/macromod: macro_start's lines cut from vecenv.c unchanged): the policy's throw key
 * with eyes held throws one where the look is (the craft macro's C_THROW plan drives it; an eye put in a frame under the
 * crosshair is the craft macro's C_FRAME plan, craft.c) */

static int end_m_cand(struct mac_step *c) { return (c->v->macros & NETH_VEC_END) && c->throw_key; }

/* lane/stronghold3 (stronghold_throw_key macro): not while the stronghold macro holds the eyes (its throws are watched
 * and picked up; the frozen policy's own throws between its yields spent all 12 eyes in 64 of 64 runs from the eye
 * starts) */
static int end_m_start(struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct vec_last *r = c->r;
    const struct neth_rl_state *s = c->s;
    if ((v->macros & NETH_VEC_END) && c->throw_key && r->dim == 0 && r->screen == PS_NONE && inv_count(s, EYE) > 0 &&
        !(s->mo_hit && s->mo_block == END_FRAME) && !(sh_owns(v, e, r) && MOPT(MO_SH_THROW_KEY) == SHK_MACRO))
    {
        /* lane/throweye: the policy's throw key with eyes held: one thrown */
        e->kind = M_CRAFT;
        e->cplan = C_THROW;
        e->ccap = 40;
        e->cphase = CP_OPEN;
        e->pressed = e->used = e->aimed = e->placed = 0;
        e->has_ops = 0;
        return 1;
    }
    return 0;
}

static const struct mac_switch END_SW[] = {{"end", NETH_VEC_END}, {NULL, 0}};

static const struct vec_macro MAC_END = {
    .name = "end", .bit = NETH_VEC_END, .sw = END_SW, .cand = end_m_cand, .start = end_m_start,
};
