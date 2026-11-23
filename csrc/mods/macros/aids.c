/* the policy's aids (lane/ladder2, ladder4, ladder10; lane/macromod: decode's lines cut from vecenv.c unchanged): run-file
 * options (actions.policy_hold, policy_place, policy_tool) that change the policy's own act on a step, for the ladder's
 * students without the macros they stand in for. No switch: each is its option */

static void aids_m_aid(struct mac_step *c, struct neth_rl_act *a, int cand)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct neth_rl_state *s = c->s;
    if (MOPT(MO_POLICY_HOLD) && !cand && !c->attack && s->mo_hit && neth_env->survival.dig.is_hitting &&
        neth_env->survival.dig.cur_x == s->mo_x && neth_env->survival.dig.cur_y == s->mo_y &&
        neth_env->survival.dig.cur_z == s->mo_z)
    {
        /* lane/ladder4: policy_hold (the worker runs this hook with the env's neth_env: the client's dig before this
         * tick); held as before, so no new press */
        a->hold |= 1u << PK_ATTACK;
        e->prev_hold |= 1u << PK_ATTACK;
        c->attack = 1;
    }
    if (MOPT(MO_POLICY_PLACE) && c->ph < 0 && a->press[PK_USE] && s->mo_hit && !blocks_use(s))
    {
        /* lane/ladder10: policy_place, the table's slot on the policy's own use press */
        for (int i = 0; i < 9 && c->ph < 0; ++i)
            if (s->inv[i].item == TABLE && s->inv[i].count > 0 && i != s->current) c->ph = i;
    }
    if (MOPT(MO_POLICY_TOOL) && c->ph < 0 && c->attack && s->mo_hit)
    {
        int t = best_tool(&v->T, s, s->mo_block);  /* lane/ladder2: policy_tool */
        if (t >= 0 && t != s->current) c->ph = t;
    }
}

/* lane/ladder3: with policy_tool on, a macro's slot change to the tool the env's own pick would take (the attack held on
 * the crosshair's block, the hotbar's best tool for it) is not the student's to say: no label, so the hotbar head
 * learns only the switches the env does not make */
static int aids_m_envpick(const struct neth_vec *v, const struct neth_rl_state *st, const struct neth_rl_act *a, int kind)
{
    return MOPT(MO_POLICY_TOOL) && kind != M_NONE && ((a->hold >> PK_ATTACK) & 1) && st->mo_hit && a->hotbar >= 0 &&
           a->hotbar == best_tool(&v->T, st, st->mo_block);
}

static const struct vec_macro MAC_AIDS = {
    .name = "aids", .bit = 0, .aid = aids_m_aid, .envpick = aids_m_envpick,
};
