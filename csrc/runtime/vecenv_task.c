/* lane/rawskill: neth_vec_opts.task_item's task (vecenv.h has the rule; vecenv.c includes this file once, after
 * reward()). One physical skill on the raw controls: no macro is needed (actions.macros none), the policy's keys are
 * masked to task_keys, and the reward is the task's own: the pickup (task_win), a death (task_death), and the three
 * shaping terms, each paid at most once per unit of progress so no loop farms them:
 *   look   once an episode, the first step the crosshair (neth_rl_state mo_hit, mo_block) is on the task's block
 *   crack  each crack stage the client draws on that block past the episode's highest (survival.c
 *          surv_client_dig_stage: -1 none, 0 to 9), so a dig let go and started again pays nothing until it is past
 *          the old stage
 *   dist   each block of the eye's distance to the nearest cell of the block the cast shows (neth_rl_state seen) under
 *          the episode's least so far; the first sighting sets the least and pays nothing
 * The episode ends with the step of the pickup or the death (vec_env ended through eplen: recv resets the env alone,
 * the step a truncation). Nothing here reads the world: only the player's own state and what the screen shows. */

/* task_keys' bit j is the action's key j (forward back left right sprint jump sneak mine place throw drop hit
 * swap_item paused); a run file's task.keys (loop.c) defaults to forward+back+left+right+jump+mine */
#define TASK_CRACK_STAGES 10    /* RenderGlobal's destroy stages, 0 to 9 */

static void task_cfg_set(struct task_cfg *c, const struct neth_vec_opts *o)
{
    memset(c, 0, sizeof *c);
    c->item = o->task_item > 0 ? o->task_item : 0;
    c->block = o->task_block > 0 ? o->task_block : c->item;
    /* neth_rl_state seen's blocks: iron ore 0, diamond ore 1, gravel (nether brick in the Nether) 2, a log 3 */
    c->seen = c->block == IRON_ORE ? 0 : c->block == 56 ? 1 : c->block == GRAVEL ? 2 : c->block == LOG ? 3 : -1;
    c->keys = o->task_keys ? (uint32_t)o->task_keys : 0x3fffu;
    c->win = o->task_win, c->death = o->task_death, c->look = o->task_look, c->crack = o->task_crack;
    c->dist = o->task_dist;
    c->shape = o->task_shape != 0;
    c->sticky = o->task_sticky == 1 || o->task_sticky == 2 ? o->task_sticky : 0;
    c->hint = c->seen >= 0 && (o->task_hint > 0 || (o->task_hint == -1 && c->shape)) ? o->task_hint : 0;
    c->count = o->task_count > 1 ? o->task_count : 1;
}

/* lane/rawsearch (task_hint): the privileged scan's box around the eye, under and over its cell (blocks); its half
 * width in x and z is task_hint */
#define TASK_HINT_BELOW 12
#define TASK_HINT_ABOVE 12

/* env id's nearest task block in the scan's box (rlscan_out near[0], its offset from the eye cell), hint_ok 0: none */
static void task_hint_read(const struct neth_vec *v, int id, int *ok, int32_t *cell)
{
    const struct rlscan_out *o = &v->scan[id];
    const struct rlscan_near *n = &o->near[0];
    *ok = n->found != 0;
    if (!*ok) return;
    cell[0] = o->fx + n->dx;
    cell[1] = (int32_t)floor((double)o->y + 1.62) + n->dy;
    cell[2] = o->fz + n->dz;
}

/* shape 1's potential: the look's angle to the last seen cell's middle, the eye's distance to it, the crack stage on
 * the block under the crosshair (TASK_ANGLE_CAP degrees and beyond count the same) */
#define TASK_ANGLE_CAP 90.0
static double task_phi(const struct task_cfg *c, struct task_env *t, const struct pool_result *r,
                       const struct neth_rl_state *s, const int32_t *hint)
{
    if (c->seen >= 0 && s->seen_n[c->seen] > 0 && s->seen[c->seen].d > 0.0f)
        t->has_cell = 1, t->cx = s->seen[c->seen].x, t->cy = s->seen[c->seen].y, t->cz = s->seen[c->seen].z;
    else if (hint)  /* lane/rawsearch (task_hint): none in view, the scan's nearest */
        t->has_cell = 1, t->cx = hint[0], t->cy = hint[1], t->cz = hint[2];
    double phi = 0.0;
    if (t->has_cell)
    {
        double ex = r->priv.x, ey = r->priv.y + 1.62, ez = r->priv.z, yaw, pitch;
        double tx = t->cx + 0.5, ty = t->cy + 0.5, tz = t->cz + 0.5;
        look_at(ex, ey, ez, tx, ty, tz, &yaw, &pitch);
        double dy = fmod(yaw - (double)r->priv.yaw, 360.0);
        if (dy > 180.0) dy -= 360.0;
        if (dy < -180.0) dy += 360.0;
        double dp = pitch - (double)r->priv.pitch, a = sqrt(dy * dy + dp * dp);
        phi -= c->look * (a < TASK_ANGLE_CAP ? a : TASK_ANGLE_CAP) / TASK_ANGLE_CAP;
        phi -= c->dist * sqrt((tx - ex) * (tx - ex) + (ty - ey) * (ty - ey) + (tz - ez) * (tz - ez));
    }
    int dx, dyy, dz, stage = surv_client_dig_stage(&dx, &dyy, &dz);
    if (stage >= 0 && stage < TASK_CRACK_STAGES && s->mo_hit == 1 && s->mo_block == c->block && dx == s->mo_x &&
        dyy == s->mo_y && dz == s->mo_z)
        phi += c->crack * (double)(stage + 1);
    return phi;
}

static void task_reset(struct task_env *t) { memset(t, 0, sizeof *t); }

/* the policy's action with the task's keys only (a copy: bufs.actions keeps what the policy said); task_sticky: an
 * attack (key 7, mine) pressed with the crosshair on a block latches, and the key is held every later step while the
 * crosshair stays on that cell (the block broken or the look moved off it ends the latch: task_step), whatever the
 * policy says. The held key is in the act the engine steps, so a recorded episode's script holds it too */
#define TASK_KEY_MINE 7
static const int32_t *task_keys(const struct neth_vec *v, struct task_env *t, const int32_t *act, int32_t *buf)
{
    if (!v->tk.item || (v->tk.keys == 0x3fffu && !v->tk.sticky)) return act;
    memcpy(buf, act, (size_t)v->aw * sizeof *buf);
    for (int j = 0; j < NETH_VEC_KEYS; ++j)
        if (!((v->tk.keys >> j) & 1)) buf[2 + j] = 0;
    if (v->tk.sticky)
    {
        /* sticky 2 (block): only on the task's block */
        if (buf[2 + TASK_KEY_MINE] && !t->latch && t->mo_ok && (v->tk.sticky == 1 || t->mb == v->tk.block))
            t->latch = 1, t->lx = t->mx, t->ly = t->my, t->lz = t->mz;
        if (t->latch) buf[2 + TASK_KEY_MINE] = 1;
    }
    return buf;
}

/* sticky: the crosshair's cell after the step, and the latch let go once it is not the latched cell */
static void task_mo(struct task_env *t, const struct neth_rl_state *s)
{
    t->mo_ok = s->mo_hit == 1;
    t->mx = s->mo_x, t->my = s->mo_y, t->mz = s->mo_z, t->mb = s->mo_block;
    if (t->latch && (!t->mo_ok || t->mx != t->lx || t->my != t->ly || t->mz != t->lz)) t->latch = 0;
}

/* the step's task reward (the episode's first step: the zeros, nothing paid); sets t->done on the pickup or a death */
static float task_step(const struct neth_vec *v, const struct task_cfg *c, struct task_env *t,
                       const struct pool_result *r, const struct neth_rl_state *s, int first, const int32_t *hint)
{
    (void)v;    /* c: the env's task (lane/acrl task_of: v->tk, or task_item2's v->tk2) */
    int64_t n = inv_count(s, c->item);
    if (first)
    {
        task_reset(t);
        t->n0 = t->nlast = n;
        if (c->shape) t->phi = task_phi(c, t, r, s, hint);
        if (c->sticky) task_mo(t, s);
        return 0.0f;
    }
    if (c->sticky) task_mo(t, s);
    double rew = 0.0;
    if (c->shape)
    {
        /* the potential's change (a cell first seen this step: the change from no cell, nothing paid for it) */
        int had = t->has_cell;
        double phi = task_phi(c, t, r, s, hint);
        if (had || !t->has_cell) rew += phi - t->phi;
        t->phi = phi;
    }
    else
    {
        if (c->look != 0.0 && !t->looked && s->mo_hit == 1 && s->mo_block == c->block) t->looked = 1, rew += c->look;
        if (c->crack != 0.0)
        {
            int dx, dy, dz, stage = surv_client_dig_stage(&dx, &dy, &dz);
            if (stage >= 0 && stage < TASK_CRACK_STAGES && s->mo_hit == 1 && s->mo_block == c->block &&
                dx == s->mo_x && dy == s->mo_y && dz == s->mo_z && stage + 1 > t->crack)
            {
                rew += c->crack * (double)(stage + 1 - t->crack);
                t->crack = stage + 1;
            }
        }
        if (c->dist != 0.0 && c->seen >= 0 && s->seen_n[c->seen] > 0 && s->seen[c->seen].d > 0.0f)
        {
            float d = s->seen[c->seen].d;
            if (t->dmin <= 0.0f) t->dmin = d;
            else if (d < t->dmin)
            {
                rew += c->dist * (double)(t->dmin - d);
                t->dmin = d;
            }
        }
    }
    if (c->count > 1)
    {
        /* lane/rawsearch (task_count): each one more paid win / count, the episode over at count more */
        if (n > t->nlast)
        {
            rew += c->win * (double)(n - t->nlast) / c->count;
            t->nlast = n;
        }
        if (n - t->n0 >= c->count) t->done = 1;
        else if (r->flags & PF_DIED)
        {
            rew += c->death;
            t->done = 2;
        }
    }
    else if (n > t->n0)
    {
        rew += c->win;
        t->done = 1;
    }
    else if (r->flags & PF_DIED)
    {
        rew += c->death;
        t->done = 2;
    }
    return (float)rew;
}
