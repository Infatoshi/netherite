/* lane/pground (orders 1:49 PM, the prebuilt floor screen; a WHAT-IF's fixed rule): pearl_floor watch. Included by
 * macros.c after fight.c and food.c (their statics: best_weapon, fight_kind, eat_want_e, eat_m_start).
 *
 * The bot holds the cell it stands in at the episode's first step (the shelter a dev start built round it) and every
 * step, in the overworld, before anything else drives (vecenv.c decode: no other macro starts, provision, the shore and
 * threat_interrupt are passed by): a hostile within PFL_REACH of the eye (the client's entity list's nearest enderman
 * within 4, whatever the look: neth_rl_far's near; any other in the view list) is looked at (an enderman PFL_LEGS over its
 * feet: one stared at within 4 teleports) and hit on the crosshair every PFL_HIT steps with the hotbar's best weapon;
 * then eating (the food macro's own rule and drive); then a kill's drop: after the stats screen's endermen killed
 * rises, a walk to where the target was last seen (PFL_DROP steps at most), PFL_DROP_WAIT steps stood there, then back;
 * then back to the cell when off it; then an enderman in view (neth_rl_far's list: within 64, a clear line) that has not
 * screamed is stared at (the box middle, PFL_STARE steps at most, then left alone PFL_BAD steps), one that has is watched
 * PFL_LOW under its feet (out of the stare's cone, so it walks in); else the look sweeps PFL_SWEEP either side of the
 * first step's yaw (the floor's way) at the level. Reads only what the player has: its own state, the view list, the
 * client's entity list within 4, the stats screen. Never a benchmark's macro: a WHAT-IF's (the dev start's structure) */

#define PFL_REACH 3.2        /* a hostile's middle (an enderman's feet + PFL_LEGS) within this of the eye is fought */
#define PFL_LEGS 0.4         /* an enderman's look: this far over its feet (threat_ender low's) */
#define PFL_HIT 10           /* steps between swings (the target's hurt time) */
#define PFL_STARE 80         /* steps an enderman is stared at for its scream */
#define PFL_BAD 600          /* steps one stared at without a scream is left alone */
#define PFL_LOW 2.0          /* a screaming one farther than reach is watched this far under its feet */
#define PFL_SWEEP 30.0       /* the idle look's sweep either side of the floor's way (degrees) */
#define PFL_SWEEP_T 160      /* its period (steps) */
#define PFL_WANDER 2         /* the start's walk out: steps per yaw camera bin (0 to 80 steps, about 17 blocks) */
#define PFL_NOISE 8.0       /* the idle look's noise at the camera bins' ends (degrees; the sampler's) */
#define PFL_DROP 60          /* steps walking to a kill's drop at most */
#define PFL_DROP_WAIT 10     /* steps stood on it */
#define PFL_HOME 0.3         /* the feet within this of the cell's middle: home */
#define PFL_HOME_MAX 200     /* steps walking home at most before the walk is given up for PFL_HOME_OFF */
#define PFL_HOME_OFF 100

enum { PFL_D_NONE, PFL_D_GO, PFL_D_WAIT };

static void pfl_look(struct neth_rl_act *a, const struct vec_last *r, double ey, double x, double y, double z)
{
    double yaw, pitch;
    look_at(r->x, ey, r->z, x, y, z, &yaw, &pitch);
    a->look_mode = 1;
    a->look[0] = (float)yaw;
    a->look[1] = (float)pitch;
}

static int pfl_bad(const struct vec_env *e, int id)
{
    for (int i = 0; i < PFL_NBAD; ++i)
        if (e->pfl.bad[i] == id && e->steps < e->pfl.bad_until[i]) return 1;
    return 0;
}

/* the walk to (x, z) on the level: 1 there */
static int pfl_walk(struct neth_rl_act *a, const struct vec_last *r, double ey, double x, double z, double near)
{
    if (py_hypot2(x - r->x, z - r->z) <= near) return 1;
    pfl_look(a, r, ey, x, ey - 0.5, z);
    a->hold = 1u << PK_FORWARD;
    return 0;
}

/* 0 not this env's step (off, another dimension), 1 the step taken (a is the act), 2 the eat macro runs this step on
 * the macro path (vecenv.c passes provision, the shore and the threats by) */
static int pfloor_step(struct mac_step *c, struct neth_rl_act *a, int *kind)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct vec_last *r = c->r;
    const struct neth_rl_state *s = c->s;
    if (!MOPT(MO_PEARL_FLOOR) || r->dim != 0 || (r->flags & PF_DEAD)) return 0;
    struct pfl_env *p = &e->pfl;
    double ey = r->y + 1.62;
    if (!p->set)
    {
        p->set = 1;
        p->hx = floor(r->x) + 0.5, p->hz = floor(r->z) + 0.5;
        p->yaw0 = r->yaw;
        p->kills = -1;
    }
    if (e->kind == M_CRAFT && e->eat) return 2;
    if (e->kind != M_NONE) mac_abort(e), mac_end(e);
    still(a);
    a->hotbar = -1;
    *kind = M_MINE;
    e->lmbit = NETH_VEC_FIGHT;
    e->lhalf = 0;
    if (r->screen != PS_NONE)
    {
        a->nops = 1;
        a->ops[0] = CLOSE;
        return 1;
    }
    const struct neth_rl_far *f = neth_rl_far(v->h, (int)(e - v->e));
    if (f && p->kills < 0) p->kills = f->ender_kills;
    if (f && f->ender_kills > p->kills)
    {
        p->kills = f->ender_kills;
        if (p->has_last) p->drop = PFL_D_GO, p->dsteps = 0, p->dx = p->lx, p->dz = p->lz;
    }
    /* 1. a hostile within reach */
    int tid = 0;
    double tx = 0, ty = 0, tz = 0, bd = PFL_REACH;
    if (f && f->near.id)
    {
        double my = f->near.y + PFL_LEGS, d = py_dist3(r->x, ey, r->z, f->near.x, my, f->near.z);
        if (d <= bd) tid = f->near.id, tx = f->near.x, ty = my, tz = f->near.z, bd = d;
    }
    if (!tid)
        for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
        {
            int k = s->vis[i].kind;
            if (!fight_kind(k)) continue;
            double my = k == HK_ENDERMAN ? s->vis[i].y + PFL_LEGS : s->vis[i].y + 0.5 * s->vis[i].h;
            double d = py_dist3(r->x, ey, r->z, s->vis[i].x, my, s->vis[i].z);
            if (d <= bd) tid = s->vis[i].id, tx = s->vis[i].x, ty = my, tz = s->vis[i].z, bd = d;
        }
    if (tid)
    {
        p->has_last = 1, p->lx = tx, p->lz = tz;
        pfl_look(a, r, ey, tx, ty, tz);
        int wb = best_weapon(s);
        if (wb >= 0 && wb != s->current) a->hotbar = wb;
        if (s->mo_entity == tid && e->steps - p->hit >= PFL_HIT)
        {
            a->press[PK_ATTACK] = 1;
            p->hit = e->steps;
            p->swings += 1;
        }
        return 1;
    }
    /* 2. eating, the food macro's own */
    if (eat_want_e(v, e, r, s) && eat_m_start(c))
    {
        e->age = 0;         /* (mac_start's: eat() ends at an age over 60) */
        return 2;
    }
    /* 2b. the sampler's part that the world reads (a look is not: 8 attempts with the look noise alone were one
     * history): at the start, PFL_WANDER steps per camera bin of the policy's yaw walked out along the floor's way, then
     * back (4.), so the player's place differs by attempt (the spawner's 24-block rule, despawning's distances, the
     * mobs' targets) */
    if (!p->wander_set) p->wander_set = 1, p->wander = c->act ? PFL_WANDER * c->act[1] : 0;
    if (p->wander > 0)
    {
        p->wander -= 1;
        a->look_mode = 1, a->look[0] = p->yaw0, a->look[1] = 0.0f;
        a->hold = 1u << PK_FORWARD;
        return 1;
    }
    /* 3. a kill's drop */
    if (p->drop == PFL_D_GO)
    {
        if (pfl_walk(a, r, ey, p->dx, p->dz, 0.4) || ++p->dsteps > PFL_DROP) p->drop = PFL_D_WAIT, p->dsteps = 0;
        return 1;
    }
    if (p->drop == PFL_D_WAIT)
    {
        if (++p->dsteps > PFL_DROP_WAIT) p->drop = PFL_D_NONE, p->home_n = 0;
        a->look_mode = 1, a->look[0] = r->yaw, a->look[1] = 60.0f;
        return 1;
    }
    /* 4. back to the cell */
    if (py_hypot2(p->hx - r->x, p->hz - r->z) > PFL_HOME && e->steps >= p->home_off)
    {
        if (++p->home_n > PFL_HOME_MAX) p->home_off = e->steps + PFL_HOME_OFF, p->home_n = 0;
        else
        {
            pfl_walk(a, r, ey, p->hx, p->hz, PFL_HOME);
            return 1;
        }
    }
    else p->home_n = 0;
    /* 5. an enderman in view: the stare for its scream, or watched under its feet */
    int best = -1, scr = -1;
    double sd = 1e9, ad = 1e9;
    for (int i = 0; f && i < f->n && i < NETH_RL_FAR; ++i)
    {
        double d = f->d2[i];
        if (f->e[i].scream)
        {
            if (d < ad) ad = d, scr = i;
        }
        else if (!pfl_bad(e, f->e[i].id) && d < sd) sd = d, best = i;
    }
    if (scr >= 0)
    {
        p->tid = 0;
        pfl_look(a, r, ey, f->e[scr].x, f->e[scr].y - PFL_LOW, f->e[scr].z);
        p->has_last = 1, p->lx = f->e[scr].x, p->lz = f->e[scr].z;
        return 1;
    }
    if (best >= 0)
    {
        if (p->tid != f->e[best].id) p->tid = f->e[best].id, p->tst = 0;
        if (++p->tst > PFL_STARE)
        {
            p->bad[p->nbad % PFL_NBAD] = p->tid;
            p->bad_until[p->nbad % PFL_NBAD] = e->steps + PFL_BAD;
            p->nbad += 1;
            p->tid = 0;
        }
        pfl_look(a, r, ey, f->e[best].x, f->e[best].y + 0.5 * f->e[best].h, f->e[best].z);
        p->stares += 1;
        return 1;
    }
    p->tid = 0;
    /* 6. the sweep over the floor */
    /* the sampler's part (an idle rule is one trajectory otherwise): the sweep's phase from the policy's yaw bin at its
     * first idle step, and each step a look noise of PFL_NOISE degrees a bin off the middle from its two camera bins */
    double ny = 0.0, np = 0.0;
    if (c->act)
    {
        if (!p->sweep) p->sweep = (int64_t)c->act[1] * PFL_SWEEP_T / NETH_VEC_CAM;
        ny = PFL_NOISE * (c->act[1] - NETH_VEC_CAM / 2) / (NETH_VEC_CAM / 2);
        np = PFL_NOISE * (c->act[0] - NETH_VEC_CAM / 2) / (NETH_VEC_CAM / 2);
    }
    p->sweep += 1;
    a->look_mode = 1;
    a->look[0] = p->yaw0 + (float)(PFL_SWEEP * sin(6.283185307179586 * (double)p->sweep / (double)PFL_SWEEP_T) + ny);
    a->look[1] = (float)np;
    return 1;
}
