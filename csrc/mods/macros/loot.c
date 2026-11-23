/* NETH_VEC_LOOT: the loot macro (lane/macromod: cut from vecenv.c, unchanged) */

/* macros._loot */
static int loot(struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s, struct neth_rl_act *a, double ey)
{
    int x = e->lx, y = e->ly, z = e->lz;
    still(a);
    if (e->lphase == 0)
    {
        int on = s->mo_hit && s->mo_block == CHEST && abs(s->mo_x - x) + abs(s->mo_z - z) <= 1 && s->mo_y == y;
        if (on)
        {
            a->press[PK_USE] = 1;
            e->lphase = 1;
            e->lwait = 0;
            return 0;
        }
        if (e->age > 4)
        {
            tried_set(e, x, y, z, e->steps + 40);
            return 1;
        }
        double yaw, pitch;
        look_at(r->x, ey, r->z, x + 0.5, y + 0.5, z + 0.5, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw;
        a->look[1] = (float)pitch;
        return 0;
    }
    if (e->lphase == 1)
    {
        if (r->screen != PS_CHEST)
        {
            e->lwait += 1;
            if (e->lwait > 5)
            {
                tried_set(e, x, y, z, e->steps + 40);
                return 1;
            }
            return 0;
        }
        int nchest = r->nslots - 36, full[90], nfull = 0;
        for (int i = 0; i < nchest && i < 90; ++i)
            if (r->slot_item[i] >= 0 && r->slot_count[i] > 0) full[nfull++] = i;
        if (!nfull || e->age > 60)
        {
            a->nops = 1;
            a->ops[0] = CLOSE;
            tried_set(e, x, y, z, (int64_t)1 << 62);
            e->lphase = 2;
            return 0;
        }
        int k = nfull < 8 ? nfull : 8;
        a->nops = k;
        for (int j = 0; j < k; ++j) a->ops[j] = click(full[j], 0, 1);
        return 0;
    }
    return r->screen == PS_NONE || e->age > 70;
}


/* ------------------------------------------------------------ the mod (lane/macromod: macro.h's hooks) */

static int loot_m_cand(struct mac_step *c)
{
    return (c->v->macros & NETH_VEC_LOOT) && c->s->nearest >= 0 && c->s->nearest_dist < 4.5f;
}

static int loot_m_start(struct mac_step *c)
{
    struct vec_env *e = c->e;
    const struct neth_rl_state *s = c->s;
    if ((c->v->macros & NETH_VEC_LOOT) && s->nearest >= 0 && s->nearest_dist < 4.5f)
    {
        int k = s->nearest;
        int x = s->chest[k].x, y = s->chest[k].y, z = s->chest[k].z;
        if (tried_get(e, x, y, z) <= e->steps)
        {
            e->kind = M_LOOT;
            e->lx = x;
            e->ly = y;
            e->lz = z;
            e->lphase = 0;
            return 1;
        }
    }
    return 0;
}

static int loot_m_drive(struct mac_step *c, struct neth_rl_act *a)
{
    if (c->e->kind != M_LOOT) return -1;
    return loot(c->e, c->r, c->s, a, c->ey);
}

static uint32_t loot_m_label(const struct vec_env *e) { return e->kind == M_LOOT ? NETH_VEC_LOOT : 0; }

static const struct mac_switch LOOT_SW[] = {{"loot", NETH_VEC_LOOT}, {NULL, 0}};

static const struct vec_macro MAC_LOOT = {
    .name = "loot", .bit = NETH_VEC_LOOT, .sw = LOOT_SW, .cand = loot_m_cand, .start = loot_m_start, .drive = loot_m_drive,
    .label = loot_m_label,
};
