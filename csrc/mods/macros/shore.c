/* lane/chain3: the opt-in post-Nether dry surface exit, a hard switch before the food and policy macros. */
#define SHORE_EXIT_R 48
#define SHORE_PILLAR_R 10.0
#define SHORE_NOPATH_MAX 20
#define SHORE_SEEN 32768   /* fs_home: the first full-armor step made the one-shot choice */
#define SHORE_ARMED 65536  /* its first full-armor step was under rock */
#define SHORE_DONE 131072  /* a dry exit or failed plans handed the turn back for this episode */

static int shore_mode(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                      const struct neth_rl_state *s)
{
    if (!MOPT(MO_PEARL_SHORE_EXIT) || r->dim != 0 || !(e->dims & 1) || (r->flags & PF_DEAD)) return 0;
    if (pearl_held(s) >= 12)
    {
        e->fs_home = (e->fs_home | SHORE_DONE) & ~SHORE_ARMED;
        return 0;
    }
    if (e->fs_home & SHORE_DONE) return 0;
    const struct neth_rl_far *f = neth_rl_far(v->h, (int)(e - v->e));
    if (!f || f->armor_worn < 15) return 0;
    int fx = (int)floor(r->x), fy = (int)floor(r->y + 0.1), fz = (int)floor(r->z);
    if (!(e->fs_home & SHORE_SEEN))
    {
        e->fs_home |= SHORE_SEEN;
        int eye_y = (int)floor(r->y + 1.62);
        if (shoracle_sky_here(v->p, (int)(e - v->e), fx, eye_y, fz))
            e->fs_home |= SHORE_DONE;
        else
            e->fs_home |= SHORE_ARMED;
    }
    if (!(e->fs_home & SHORE_ARMED)) return 0;
    if (r->on_ground && shoracle_dry_here(v->p, (int)(e - v->e), fx, fy, fz))
    {
#ifdef SHORE_TRACE
        if (e->pearl.ph_leg == 2)
            fprintf(stderr, "SHORE DONE step=%lld world=%lld pos=%.2f,%.2f,%.2f\n",
                    (long long)e->steps, (long long)r->wtime, r->x, r->y, r->z);
#endif
        if (e->pearl.ph_leg >= 2) e->pearl.ph_leg = 0;
        e->fs_home = (e->fs_home | SHORE_DONE) & ~SHORE_ARMED;
#ifdef SHORE_TRACE
        fprintf(stderr, "SHORE READY step=%lld world=%lld pos=%.2f,%.2f,%.2f\n",
                (long long)e->steps, (long long)r->wtime, r->x, r->y, r->z);
#endif
        return 0;
    }
    return 1;
}

/* Full armor and a pearl shortage make the night the pearl macro's turn, including a stale strip climb state. */
static int shore_night_yield(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                             const struct neth_rl_state *s)
{
    if (!MOPT(MO_PEARL_STRIP_YIELD) || !(v->macros & NETH_VEC_PEARL) || r->dim != 0 ||
        (r->flags & PF_DEAD) || pearl_held(s) >= 12) return 0;
    const struct neth_rl_far *f = neth_rl_far(v->h, (int)(e - v->e));
    int64_t t = ((r->wtime % 24000) + 24000) % 24000;
    return f && f->armor_worn >= 15 && t >= 13000 && t < 23000;
}

static void shore_failed_plan(struct vec_env *e, const struct vec_last *r)
{
    if (e->pearl.ph_leg < SHORE_NOPATH_MAX + 2) e->pearl.ph_leg += 1;
    if (e->pearl.ph_leg >= SHORE_NOPATH_MAX + 2)
    {
        e->fs_home = (e->fs_home | SHORE_DONE) & ~SHORE_ARMED;
        e->pearl.ph_leg = 0;
#ifdef SHORE_TRACE
        fprintf(stderr, "SHORE RELEASE step=%lld world=%lld pos=%.2f,%.2f,%.2f\n",
                (long long)e->steps, (long long)r->wtime, r->x, r->y, r->z);
#endif
    }
}

static void shore_apply(struct mac_step *c, struct neth_rl_act *a, int *kind, int *chosen)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct vec_last *r = c->r;
    const struct neth_rl_state *s = c->s;
    int id = (int)(e - v->e);
    act_noop(a);
    *kind = M_SURFACE, *chosen = 0;
    if (e->kind != M_NONE) mac_abort(e), mac_end(e);
    e->lmbit = NETH_VEC_SURFACE, e->lhalf = 0;
    if (r->screen != PS_NONE)
    {
        a->nops = 1;
        a->ops[0] = CLOSE;
        return;
    }
    if (s->eye_water || r->air < 100)
    {
        surface_swim(v, e, r, s, a);
        return;
    }
    if (e->pearl.ph_leg < 2 || !shoracle_dry_here(v->p, id, e->pearl.ph_lx, e->pearl.ph_ly, e->pearl.ph_lz))
    {
        int target[3];
        if (shoracle_dry_near(v->p, id, (int)floor(r->x), (int)floor(r->z), SHORE_EXIT_R, target) < 0)
        {
            /* All nearby loaded columns are wet or covered: let the ordinary surface climb open a way. */
            shore_failed_plan(e, r);
            strip_m_start(c);
            if (e->kind != M_NONE) strip_m_drive(c, a);
            return;
        }
        e->pearl.ph_lx = target[0], e->pearl.ph_ly = target[1], e->pearl.ph_lz = target[2];
        e->pearl.ph_leg = 2;
#ifdef SHORE_TRACE
        fprintf(stderr, "SHORE TARGET step=%lld world=%lld from=%.2f,%.2f,%.2f to=%d,%d,%d floor=%d\n",
                (long long)e->steps, (long long)r->wtime, r->x, r->y, r->z, target[0], target[1], target[2],
                shoracle_block(v->p, id, target[0], target[1] - 1, target[2], NULL));
#endif
    }
    int rc = sho_follow(v, e, r, s, a, e->pearl.ph_lx, e->pearl.ph_ly, e->pearl.ph_lz,
                        0.6, SHO_EXACT | SHO_BRIDGE);
    if (rc < 0)
    {
        if (hypot(e->pearl.ph_lx + 0.5 - r->x, e->pearl.ph_lz + 0.5 - r->z) <= SHORE_PILLAR_R &&
            e->pearl.ph_ly > r->y + 1.0 && st3_pillar(v, e, r, s, a))
        {
            e->pearl.ph_leg = 2;
            return;
        }
        act_noop(a);
        shore_failed_plan(e, r);
    }
}
