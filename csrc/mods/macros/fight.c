/* NETH_VEC_FIGHT: the fight's lock (lane/macromod: cut from vecenv.c, unchanged) */

/* lane/food2 (food_chase far, the food macro's hunt; food.c has the rest of its constants) */
#define FOOD_CHASE_LOST 400     /* steps an animal out of view is walked after (to its last place) */
#define FOOD_CHASE_THERE 40     /* ... of them left once its last place is reached */
#define FOOD_CHASE_STUCK 10     /* steps without moving before the lost animal's walk detours */
#define FOOD_PICKUP_R 1.5       /* a killed animal's drop searched for this far round its last place */
#define FOOD_PICKUP_EACH 12     /* ... this many steps a spot */
#define FOOD_PICKUP_STEPS 100   /* ... and given up after this many from reaching the place */

/* lane/pnight (threat_skeleton, with threat_interrupt on) */
#define THREAT_SKEL_R 16.0      /* a skeleton in view this near (the vis list's own reach) */
#define THREAT_SKEL_HURT 100    /* hit: a hit taken this many steps back at most */
#define THREAT_SKEL_VIEW 12.0   /* view: this near with no hit taken */
#define THREAT_SKEL_STUCK 3     /* the lock's walk to a skeleton: a jump after this many steps without moving */
enum { THS_OFF, THS_HIT, THS_VIEW };

/* NETH_VEC_FIGHT (lane/fight): the kinds a fight locks on (living.h) */
static int fight_kind(int k)
{
    return (k >= HK_ZOMBIE && k <= HK_SPIDER) || k == GK_GHAST || (k >= HK_ENDERMAN && k <= HK_CAVE_SPIDER) ||
           k == SK_SLIME || k == SK_MAGMA_CUBE;
}

/* the hotbar's best weapon (an item's attack damage, swords, axes, pickaxes, shovels: 1.7.10's ToolMaterial plus
 * the tool's own), -1 none */
static int best_weapon(const struct neth_rl_state *s)
{
    static const int16_t w[][2] = {{276, 7}, {267, 6}, {279, 6}, {278, 5}, {272, 5}, {258, 5}, {277, 4}, {257, 4},
                                   {283, 4}, {268, 4}, {275, 4}, {286, 3}, {274, 3}, {256, 3}, {271, 3}, {285, 2},
                                   {270, 2}, {273, 2}, {284, 1}, {269, 1}};
    int best = -1, bd = 1;
    for (int i = 0; i < 9; ++i)
        for (size_t j = 0; j < sizeof w / sizeof w[0]; ++j)
            if (s->inv[i].item == w[j][0] && s->inv[i].count > 0 && w[j][1] > bd)
            {
                best = i;
                bd = w[j][1];
            }
    return best;
}

static int fight_core(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                      struct neth_rl_act *a, double ey);

#define THREAT_ENDER_LEGS 0.4   /* lane/pground (threat_ender low): the lock's look this far over an enderman's feet */

/* lane/pearlhome (2nd pass): food_swim, the food hunt in water */
#define FIGHT_SWIM_AIR 150      /* under this much air with the eye under water: the look up, swum for air */
#define FIGHT_SWIM_PITCH -50.0f

/* lane/dleg (threat_end ender; threat_end_step below): the lock's reach, a hit's age, the dragon's head that yields */
#define THREAT_END_R 4.0
#define THREAT_END_HURT 20
#define THREAT_END_DRAGON 12.0
/* the dragon's head (the client's copy) within lim of the eye */
static int threat_end_dragon(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r, double lim)
{
    const struct neth_rl_end *en = dragon_view(v, e);
    if (!en || !en->on || !en->dragon_id) return 0;
    double dx = en->head[0] - r->x, dy = en->head[1] - (r->y + 1.62), dz = en->head[2] - r->z;
    return dx * dx + dy * dy + dz * dz < lim * lim;
}

/* NETH_VEC_FIGHT's lock: 1 when over */
static int fight(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                 struct neth_rl_act *a, double ey)
{
    int swim = e->hunt && MOPT(MO_FOOD_SWIM) && s->eye_water;
    if (swim && r->air < FIGHT_SWIM_AIR)
    {
        /* (the hunt's own clock runs on: it ends at its limit as before) */
        a->hold = (1u << PK_JUMP) | (1u << PK_FORWARD);
        memset(a->press, 0, sizeof a->press);
        a->look_mode = 1;
        a->look[0] = r->yaw;
        a->look[1] = FIGHT_SWIM_PITCH;
        return 0;
    }
    /* lane/dleg (threat_end ender): in the End the lock yields to the dragon's charge */
    if (r->dim == 1 && MOPT(MO_THREAT_END) && !e->hunt && threat_end_dragon(v, e, r, THREAT_END_DRAGON)) return 1;
    int done = fight_core(v, e, r, s, a, ey);
    if (swim && !done) a->hold |= 1u << PK_JUMP;
    return done;
}

static int fight_core(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r, const struct neth_rl_state *s,
                      struct neth_rl_act *a, double ey)
{
    (void)v;
    a->hold = 0;
    memset(a->press, 0, sizeof a->press);
    int k = -1;
    for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
        if (s->vis[i].id == e->fid) k = i;
    if (e->hunt && meat_count(s) > e->hmeat) return e->fb.hunt_end[0] += 1, e->hunt = 2, 1;  /* (2: booked) */
    if (k < 0 && e->hunt)
    {
        /* lane/food: out of view (killed, its drop where it fell, or gone): walked to its last place 40 steps */
        double yaw, pitch, tx = e->hx, tz = e->hz;
        /* lane/food2 (food_chase far): killed (it died in view: the kill stat moved), its drop searched for: its last
         * place, then the four cells FOOD_PICKUP_R round it, FOOD_PICKUP_EACH steps each, FOOD_PICKUP_STEPS at most
         * (75 of 288 hunts of the first 120-minute arm killed the animal and never picked its meat up) */
        int killed = MOPT(MO_FOOD_CHASE) == FC_FAR && s->mob_kills > e->hk0;
        static const double PK_OX[5] = {0.0, FOOD_PICKUP_R, 0.0, -FOOD_PICKUP_R, 0.0};
        static const double PK_OZ[5] = {0.0, 0.0, FOOD_PICKUP_R, 0.0, -FOOD_PICKUP_R};
        if (killed && e->hpk > 0)
        {
            int j = (e->hpk / FOOD_PICKUP_EACH) % 5;
            tx += PK_OX[j], tz += PK_OZ[j];
            if (++e->hpk > FOOD_PICKUP_STEPS)
            {
                e->fb.hunt_end[1] += 1, e->hunt = 2;
                e->fb.lost_why[0] += 1;
                e->hunt_off = e->steps + 200;
                return 1;
            }
        }
        look_at(r->x, ey, r->z, tx, e->hy + 0.2, tz, &yaw, &pitch);
        a->look_mode = 1;
        a->look[0] = (float)yaw;
        a->look[1] = (float)pitch;
        double far = py_hypot2(r->x - tx, r->z - tz);
        if (killed && e->hpk == 0 && far <= 0.6) e->hpk = 1;
        if (killed && e->hpk > 0) e->flost = 0;     /* (the search's own limit; the walk there the lost one's) */
        if (far > 0.4) a->hold = (1u << PK_FORWARD) | (e->flost % 8 == 7 ? 1u << PK_JUMP : 0);
        if (MOPT(MO_FOOD_CHASE) == FC_FAR && far > 0.4)
        {
            /* lane/food2 (food_chase far): a jump where a step does not move, and after FOOD_CHASE_STUCK such steps
             * a 60-degree detour for as many (6 of 9 lost hunts in a 64-run round never reached the last place) */
            double mv = py_hypot2(r->x - e->gpx, r->z - e->gpz);
            e->gstuck = mv < 0.05 ? e->gstuck + 1 : 0;
            e->gpx = r->x, e->gpz = r->z;
            if (e->gstuck > 2) a->hold |= 1u << PK_JUMP;
            if (e->gstuck > FOOD_CHASE_STUCK) e->fs_det = FOOD_CHASE_STUCK * 2, e->gstuck = 0;
            if (e->fs_det > 0)
            {
                e->fs_det -= 1;
                a->look[0] = (float)(yaw + 60.0);
            }
        }
        /* lane/food2 (food_chase far): walked to its last place however far, FOOD_CHASE_LOST steps at most, and given
         * up FOOD_CHASE_THERE steps after reaching it (a pig behind a hill or a tree is in view again from there) */
        int chase = MOPT(MO_FOOD_CHASE) == FC_FAR;
        if (chase && far <= 1.0 && e->flost < FOOD_CHASE_LOST - FOOD_CHASE_THERE) e->flost = FOOD_CHASE_LOST - FOOD_CHASE_THERE;
        if (++e->flost > (chase ? FOOD_CHASE_LOST : 40))
        {
            e->fb.hunt_end[1] += 1, e->hunt = 2;
            /* (the census of a lost hunt: killed with its drop not picked up, hit but not killed, never hit with
             * its last place reached or not, in water) */
            e->fb.lost_why[s->eye_water ? 4 : s->mob_kills > e->hk0 ? 0 : e->flast > -100 ? 1 : far <= 1.0 ? 2 : 3] += 1;
            e->hunt_off = e->steps + 200;
            return 1;
        }
        return 0;
    }
    /* lane/pground (threat_ender reach): the lock's enderman out of the view list but within reach in the client's
     * entity list is still fought (beside or behind the player, or its line through a block corner) */
    const struct neth_rl_far *nf = NULL;
    if (k < 0 && MOPT(MO_THREAT_ENDER) >= 1 && !e->hunt)
    {
        nf = neth_rl_far(v->h, (int)(e - v->e));
        if (nf && nf->near.id != e->fid) nf = NULL;
    }
    if (k < 0 && !nf) return ++e->flost > 10 || e->age > 400;
    e->flost = 0;
    float vx = nf ? nf->near.x : s->vis[k].x, vy = nf ? nf->near.y : s->vis[k].y, vz = nf ? nf->near.z : s->vis[k].z;
    float vh = nf ? nf->near.h : s->vis[k].h;
    double tx = vx, ty = vy + 0.5 * vh, tz = vz, yaw, pitch;
    /* lane/pground (threat_ender low): an enderman is looked at THREAT_ENDER_LEGS over its feet, not at its middle:
     * EntityEnderman.shouldAttackPlayer's stare is at the box middle and one stared at within 4 teleports */
    if (MOPT(MO_THREAT_ENDER) == 2 && !e->hunt && (nf || s->vis[k].kind == HK_ENDERMAN)) ty = vy + THREAT_ENDER_LEGS;
    e->hx = vx, e->hy = vy, e->hz = vz;
    look_at(r->x, ey, r->z, tx, ty, tz, &yaw, &pitch);
    a->look_mode = 1;
    a->look[0] = (float)yaw;
    a->look[1] = (float)pitch;
    double d = py_dist3(r->x, ey, r->z, tx, ty, tz);
    if (d > 2.6 && (d < 10.0 || e->hunt)) a->hold = 1u << PK_FORWARD;
    if (MOPT(MO_THREAT_SKELETON) && !e->hunt && k >= 0 && s->vis[k].kind == HK_SKELETON && d > 2.6)
    {
        /* lane/pnight (threat_skeleton): a skeleton walked to however far (it backs off as it shoots), a jump where a
         * step does not move (the hunt's rule; its gstuck and place are unused by a lock that is not a hunt) */
        a->hold = 1u << PK_FORWARD;
        double mv = py_hypot2(r->x - e->gpx, r->z - e->gpz);
        e->gstuck = mv < 0.05 ? e->gstuck + 1 : 0;
        if (e->gstuck > THREAT_SKEL_STUCK) a->hold |= 1u << PK_JUMP;
        e->gpx = r->x, e->gpz = r->z;
    }
    if (e->hunt)
    {
        /* lane/food: a jump when a step does not move (the seek's rule), the hunt given up at 600 steps */
        double mv = py_hypot2(r->x - e->gpx, r->z - e->gpz);
        e->gstuck = mv < 0.05 ? e->gstuck + 1 : 0;
        if (e->gstuck > 2 && d > 2.6) a->hold |= 1u << PK_JUMP;
        e->gpx = r->x, e->gpz = r->z;
        if (e->age > 1200)   /* lane/food: an animal up to 80 blocks out is 60 to 80 s of walking */
        {
            e->fb.hunt_end[2] += 1, e->hunt = 2;
            e->hunt_off = e->steps + 200;
            return 1;
        }
    }
    int wb = best_weapon(s);
    if (r->dim == 1 && MOPT(MO_THREAT_END))
    {
        /* lane/dleg (threat_end ender): a sword first in the End (the stone sword ties the diamond pickaxe's 5) */
        static const int16_t SW[5] = {276, 267, 272, 283, 268};
        int sb = -1;
        for (int j = 0; j < 5 && sb < 0; ++j)
            for (int i = 0; i < 9 && sb < 0; ++i)
                if (s->inv[i].item == SW[j] && s->inv[i].count > 0) sb = i;
        if (sb >= 0) wb = sb;
    }
    if (wb >= 0 && wb != s->current) a->hotbar = wb;
    if (s->mo_entity == e->fid && e->age - e->flast >= 10)
    {
        a->press[PK_ATTACK] = 1;
        e->flast = e->age;
    }
    /* lane/food2 (food_chase far): a hunt's own limit (1200 steps above), not the fight's 400 */
    if (e->hunt && MOPT(MO_FOOD_CHASE) == FC_FAR) return 0;
    return e->age > 400;
}


/* lane/proom (threat_interrupt, opt-in): a melee hostile close in view starts the lock on it before provision, food,
 * craft or the pearl's walk drive the step (decode calls this ahead of provision_mode). 1: the step is taken (turn's
 * look round), 0: go on (with the lock started where one was). Reads only what the player sees: the vis list, the
 * health, fire, air and food the screen shows */
#define THREAT_R 3.0
static int threat_kind(int k) { return k == HK_ZOMBIE || k == HK_SPIDER || k == HK_CAVE_SPIDER; }

static int threat_lock(const struct vec_env *e) { return e->kind == M_MINE && e->fid && !e->hunt; }

/* lane/dleg (threat_end ender, with threat_interrupt on): in the End, an enderman in view within THREAT_END_R after a
 * hit taken in the last THREAT_END_HURT steps starts the same lock on it (the sword first: fight_core), unless the
 * dragon's head is within THREAT_END_DRAGON (its charge comes first: the lock ends there too, fight()); a hit with
 * neither the dragon near nor an enderman in view turns the look round (mode turn's rule). On dw's rows the 96
 * enderman deaths took 11.8 hits each over 150 to 400 steps with attack pressed in 4 */
static int threat_end_step(struct mac_step *c, struct neth_rl_act *a, int *kind)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct vec_last *r = c->r;
    const struct neth_rl_state *s = c->s;
    struct vec_dragon *g = &e->dg;
    if (r->flags & PF_DEAD) { g->te_lhp = 20.0f; return 0; }
    if (g->te_lhp > 0.0f && r->health < g->te_lhp - 0.5f && !r->fire) g->te_hurt = e->steps, g->te_ok = 1;
    g->te_lhp = r->health;
    if (r->screen != PS_NONE || s->eye_water || threat_lock(e) || !g->te_ok || e->steps - g->te_hurt > THREAT_END_HURT)
        return 0;
    if (threat_end_dragon(v, e, r, THREAT_END_DRAGON)) return 0;
    double ey = r->y + 1.62, bd = THREAT_END_R;
    int best = -1, any = 0;
    for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
    {
        if (s->vis[i].kind != HK_ENDERMAN) continue;
        any = 1;
        double d = py_dist3(r->x, ey, r->z, s->vis[i].x, s->vis[i].y + 0.5 * s->vis[i].h, s->vis[i].z);
        if (d <= bd) best = i, bd = d;
    }
    if (best >= 0)
    {
        if (e->kind != M_NONE) mac_abort(e), mac_end(e);
        e->kind = M_MINE;
        e->fid = s->vis[best].id;
        e->flost = 0;
        e->flast = -100;
        e->age = 0;
        ++g->te_locks;
        return 0;
    }
    if (g->te_hurt == e->steps && !any && !threat_end_dragon(v, e, r, 2.0 * THREAT_END_DRAGON) && r->air > 0)
    {
        still(a);
        a->look_mode = 1;
        a->look[0] = r->yaw + 180.0f;
        a->look[1] = 0.0f;
        *kind = M_MINE;
        e->lmbit = NETH_VEC_FIGHT;
        e->lhalf = 0;
        return 1;
    }
    return 0;
}

/* lane/pground (threat_ender reach): a screaming enderman within THREAT_ENDER_R of the eye in the client's entity list
 * (neth_rl_far's near: any look, no line test) starts the fight lock on it whatever runs, a lock on another living moved
 * to it (rd 8's ender deaths: the enderman on the bot within 3 on 2,460 steps, the hunt swinging on 3% of them while
 * strip, food, mine, the hunt's travel and guard or a lock on another mob held the step). 1: the lock was started */
#define THREAT_ENDER_R 3.0
static int threat_ender_step(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                             const struct neth_rl_state *s)
{
    if (MOPT(MO_THREAT_ENDER) < 1 || r->dim != 0 || r->screen != PS_NONE || (r->flags & PF_DEAD)) return 0;
    const struct neth_rl_far *f = neth_rl_far(v->h, (int)(e - v->e));
    if (!f || !f->near.id || !f->near.scream || f->near.d > THREAT_ENDER_R) return 0;
    if (e->kind == M_MINE && e->fid == f->near.id && !e->hunt) return 0;     /* locked on it already */
    (void)s;
    if (e->kind != M_NONE) mac_abort(e), mac_end(e);
    e->kind = M_MINE;
    e->fid = f->near.id;
    e->flost = 0;
    e->flast = -100;
    e->age = 0;
    return 1;
}

static int threat_step(struct mac_step *c, struct neth_rl_act *a, int *kind)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct vec_last *r = c->r;
    const struct neth_rl_state *s = c->s;
    if (threat_ender_step(v, e, r, s)) return 0;
    int mode = MOPT(MO_THREAT_INTERRUPT);
    if (mode && r->dim == 1 && MOPT(MO_THREAT_END)) return threat_end_step(c, a, kind);   /* lane/dleg */
    /* lane/supply (threat_screen=close): the furnace's screen open does not hide a threat (the smelt's wait, CP_WAIT,
     * stands with it open: at's 15 and ap's 6 first hits there, threat_interrupt never answering) */
    int scr_ok = r->screen == PS_NONE || (MOPT(MO_THREAT_SCREEN) && r->screen == PS_FURNACE);
    if (!mode || r->dim != 0 || !scr_ok || s->eye_water || (r->flags & PF_DEAD) || threat_lock(e))
        return 0;
    double ey = r->y + 1.62, bd = THREAT_R, nd = THREAT_R + 1.0;
    int best = -1;
    for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
    {
        int k = s->vis[i].kind;
        if (k >= AK_PIG && k <= AK_CHICKEN) continue;
        double d = py_dist3(r->x, ey, r->z, s->vis[i].x, s->vis[i].y + 0.5 * s->vis[i].h, s->vis[i].z);
        if (d < nd) nd = d;
        if (threat_kind(k) && d <= bd) best = i, bd = d;
    }
    /* lane/pnight (threat_skeleton, with threat_interrupt on): a skeleton in view within THREAT_SKEL_R, after a hit
     * taken in the last THREAT_SKEL_HURT steps (hit) or within THREAT_SKEL_VIEW (view), is closed on and fought the same
     * way (the lock walks to it however far: fight_core) */
    int sk = MOPT(MO_THREAT_SKELETON);
    if (best < 0 && sk)
    {
        double sd = THREAT_SKEL_R;
        int hurt = e->steps - e->st3_hurt <= THREAT_SKEL_HURT;
        for (int i = 0; i < s->nvis && i < NETH_RL_VIS; ++i)
        {
            if (s->vis[i].kind != HK_SKELETON) continue;
            double d = py_dist3(r->x, ey, r->z, s->vis[i].x, s->vis[i].y + 0.5 * s->vis[i].h, s->vis[i].z);
            if (d <= sd && (hurt || (sk == THS_VIEW && d <= THREAT_SKEL_VIEW))) best = i, sd = d;
        }
    }
    if (r->screen == PS_FURNACE && (best >= 0 || e->st3_hurt == e->steps))
    {
        /* (threat_screen=close) the screen closed this step (the furnace keeps smelting; the craft opens it again for
         * the output); the threat is taken on the next, with no screen, by the branch below; a hit taken with nothing
         * in view closes it too, so the turn (mode 2) answers it next (ts: 3 first hits left in the smelt's wait,
         * each from a zombie or skeleton within 1.8 blocks and out of view) */
        if (e->kind != M_NONE) mac_abort(e), mac_end(e);
        still(a);
        a->nops = 1;
        a->ops[0] = CLOSE;
        *kind = M_MINE;
        e->lmbit = NETH_VEC_FIGHT;
        e->lhalf = 0;
        return 1;
    }
    if (r->screen != PS_NONE) return 0;
    if (best >= 0)
    {
        if (e->kind != M_NONE) mac_abort(e), mac_end(e);
        e->kind = M_MINE;
        e->fid = s->vis[best].id;
        e->flost = 0;
        e->flast = -100;
        e->age = 0;
        return 0;
    }
    if (mode == 2 && e->st3_hurt == e->steps && !r->fire && r->air > 0 && r->food > 0 && nd >= THREAT_R + 1.0)
    {
        still(a);
        a->look_mode = 1;
        a->look[0] = r->yaw + 180.0f;
        a->look[1] = 0.0f;
        *kind = M_MINE;
        e->lmbit = NETH_VEC_FIGHT;
        e->lhalf = 0;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------ the mod (lane/macromod: macro.h's hooks) */

static int fight_m_cand(struct mac_step *c)
{
    return (c->v->macros & NETH_VEC_FIGHT) && c->attack && c->s->mo_entity != 0;
}

static int fight_m_start(struct mac_step *c)
{
    const struct neth_vec *v = c->v;
    struct vec_env *e = c->e;
    const struct neth_rl_state *s = c->s;
    if ((v->macros & NETH_VEC_FIGHT) && c->attack && s->mo_entity && fight_kind(s->mo_ekind) &&
        !((v->macros & NETH_VEC_NETHER) && s->mo_ekind == HK_PIGMAN))      /* lane/nether: a pigman never hit */
    {
        e->kind = M_MINE;
        e->fid = s->mo_entity;
        e->flost = 0;
        e->flast = -100;
        return 1;
    }
    return 0;
}

/* the lock's fight (the food macro's hunt too: a hunt is this lock on an animal) */
static int fight_m_drive(struct mac_step *c, struct neth_rl_act *a)
{
    if (c->e->kind != M_MINE || !c->e->fid) return -1;
    return fight(c->v, c->e, c->r, c->s, a, c->ey);
}

static uint32_t fight_m_label(const struct vec_env *e) { return e->kind == M_MINE && e->fid ? NETH_VEC_FIGHT : 0; }

static void fight_m_end(struct vec_env *e) { e->fid = 0; }

/* an episode cut mid-fight leaves no lock for the next */
static void fight_m_reset(struct vec_env *e) { e->fid = 0; }

static const struct mac_switch FIGHT_SW[] = {{"fight", NETH_VEC_FIGHT}, {NULL, 0}};

static const struct vec_macro MAC_FIGHT = {
    .name = "fight", .bit = NETH_VEC_FIGHT, .sw = FIGHT_SW, .cand = fight_m_cand, .start = fight_m_start, .drive = fight_m_drive,
    .label = fight_m_label, .end = fight_m_end, .reset = fight_m_reset,
};
