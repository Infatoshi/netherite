/* lane/lava (lava_hit seen, opt-in; off by default): the lava escape's hit for a player whose armour cuts lava's 4 below
 * the 3 strip.c's lava hit needs. Traced on dev128's base (lane/wood's A1, chain --lava-log): of 21 lava deaths, the 9
 * armoured ones (15 points worn) never began the escape: the largest health lost in one step 1.0 to 2.2, burning for 10
 * to 144 steps beside or in lava. seen: a hit is also the screen's fire (burning), any health lost since the step
 * before, and lava (source or flowing) in view within LH_VIEW_R of the eye (obsview OV_HOT: its centre seen past every
 * block within 75 degrees of the look). What it reads: the player's own fire and health, and the macros' view of the
 * client world; no server block. */

#define LH_VIEW_R 3             /* lava in view this near the eye */
#define LH_LOST 0.01f           /* health lost since the step before that counts */

static int lava_hit_seen(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r)
{
    if (MOPT(MO_LAVA_HIT) != 1 || !r->fire || e->lhp - r->health < LH_LOST) return 0;
    struct obsview_hit h[OV_KINDS];
    if (obsview_scan(v->p, (int)(e - v->e), r->x, r->y + 1.62, r->z, r->yaw, r->pitch, LH_VIEW_R, 1 << OV_HOT, NULL, NULL,
                     h) != 0)
        return 0;
    return h[OV_HOT].d > 0.0f;
}
