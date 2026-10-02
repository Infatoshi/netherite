/* The trainer loop's per-env work in C (lane/looppy): rlloop.h. Each piece
 * names the Python it replaces (mcsr-speedrun mcsr/rl) and computes what it
 * computes, in the same order and width. */
#include "rlloop.h"

#include <math.h>
#include <string.h>

/* mcsr macros.py's kinds (M_NONE ..) and the binding's constants it uses
 * (mcsr nwrl.py: PF_DEAD, PF_DIED, PS gameover, PK, LOOK_*, PG_RESPAWN) */
enum { M_NONE, M_SURFACE, M_SCREEN, M_LOOT, M_MINE, M_CRAFT };
enum { F_DEAD = 1, F_DIED = 2 };
enum { S_CHEST = 3, S_GAMEOVER = 8 };
enum { K_ATTACK = 7, K_USE = 8, K_DROP = 9 };
enum { L_ABS = 1, L_DELTA = 2 };
enum { G_RESPAWN = 3 };

static inline int inrange(int64_t x) { return x >= 0 && x < 4096; }
static inline int64_t clip4k(int64_t x) { return x < 0 ? 0 : x > 4095 ? 4095 : x; }

/* numpy's float32 add.reduce of one contiguous row of 36 (pairwise_sum's
 * 8-way unrolled block for 8 <= n <= 128, then the tail; the reduction
 * starts from the identity, so a row of -0.0 sums to +0.0) */
static float sum36(const float *a)
{
    float r[8];
    for (int j = 0; j < 8; ++j) r[j] = a[j];
    int i = 8;
    for (; i < 36 - 36 % 8; i += 8)
        for (int j = 0; j < 8; ++j) r[j] = r[j] + a[i + j];
    float s = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
    for (; i < 36; ++i) s = s + a[i];
    return 0.0f + s;
}

static int popc(uint64_t x) { return __builtin_popcountll(x); }

/* reward.Reward.step and rollout.Runner._metrics for one env */
static void arrive1(const struct nwrl_loop_tabs *T, const struct nwrl_loop_env *E, const struct nwrl_result *r, int e)
{
    const struct nwrl_state *s = &r->st;
    float p[36];
    uint64_t ms = 0;
    for (int j = 0; j < 36; ++j)
    {
        int64_t it = s->inv[j].item, itc = clip4k(it);
        /* (values()[itc] * count.astype(float32)) * (it >= 0) */
        p[j] = (T->value[itc] * (float)s->inv[j].count) * (float)(it >= 0);
        if (s->inv[j].count > 0) ms |= T->msbit[itc];
    }
    float val = sum36(p);
    int64_t logs = s->mined_logs, opened = s->chests_opened, near = s->nearest;
    uint64_t ach = s->ach;
    float dist = s->nearest_dist;
    int f = E->fresh[e] != 0;
    float pv = E->prev_val[e], pd = E->prev_dist[e];
    float t[NWRL_LOOP_TERMS];
    float dv = val - pv;
    t[0] = f ? 0.0f : dv;                                                         /* items */
    int chest_open = r->screen == S_CHEST || E->prev_screen[e] == S_CHEST;
    t[1] = chest_open && !f ? (dv >= 0.0f || isnan(dv) ? dv : 0.0f) : 0.0f;     /* looted: np.maximum(dv, 0) */
    E->prev_screen[e] = r->screen;
    t[2] = f ? 0.0f : (float)(logs - E->prev_logs[e]);                           /* logs */
    t[3] = f ? 0.0f : (float)(opened - E->prev_opened[e]);                       /* chest */
    int same = !f && near >= 0 && near == E->prev_near[e] && dist < 64.0f && pd < 64.0f;
    t[4] = same ? pd - dist : 0.0f;                                              /* progress */
    t[5] = f ? 0.0f : (float)popc(ach & ~E->prev_ach[e]);                        /* ach */
    t[6] = (r->flags & 2) != 0 ? 1.0f : 0.0f;                                    /* death */
    uint64_t got = E->got[e];
    t[7] = f ? 0.0f : (float)popc(ms & ~got);                                    /* milestone */
    E->got[e] = f ? ms : got | ms;
    /* sum(w[k] * t[k] for k in t): Python's sum from 0, each product in float32 (the weight cast first) */
    float total = 0.0f;
    for (int j = 0; j < NWRL_LOOP_TERMS; ++j) total = total + (float)T->w[j] * t[j];
    E->prev_val[e] = val;
    E->prev_logs[e] = logs;
    E->prev_opened[e] = opened;
    E->prev_ach[e] = ach;
    E->prev_near[e] = near;
    E->prev_dist[e] = dist;
    E->fresh[e] = 0;
    int64_t te = E->te[e];
    if (te > 0) E->rew[e * E->ep_len + te - 1] = total;
    /* the metrics (float64) */
    double *const *M = E->m;
    double x = r->x, z = r->z;
    if (isnan(M[0][e]))
    {
        M[0][e] = x;
        M[1][e] = z;
        M[2][e] = x;
        M[3][e] = z;
    }
    double step = hypot(x - M[2][e], z - M[3][e]);
    M[4][e] += step;
    M[5][e] += step < 0.01 ? 1.0 : 0.0;
    M[2][e] = x;
    M[3][e] = z;
    M[6][e] += 1.0;
    if (te == 1200) M[7][e] = hypot(x - M[0][e], z - M[1][e]);
    M[8][e] += (r->flags & F_DIED) != 0 ? 1.0 : 0.0;
    M[9][e] += (double)total;
    /* Runner.M's t_ arrays in KEYS' order: items logs chest progress ach death milestone looted */
    static const int tk[NWRL_LOOP_TERMS] = {0, 7, 1, 2, 3, 4, 5, 6};   /* term index -> t_ array after ret */
    for (int j = 0; j < NWRL_LOOP_TERMS; ++j) M[10 + tk[j]][e] += (double)t[j];
}

void nwrl_loop_arrive(const struct nwrl_loop_tabs *T, const struct nwrl_loop_env *E, const struct nwrl_result *R,
                      int k, int lo)
{
    for (int q = 0; q < k; ++q) arrive1(T, E, &R[q], lo + q);
}

/* CPython's math.degrees */
static double degrees(double x) { return x * (180.0 / 3.14159265358979323846); }

/* macros.look_at: yaw, pitch that put (x, y, z) under the crosshair from the eye */
static void look_at(double ex, double ey, double ez, double x, double y, double z, double *yaw, double *pitch)
{
    double dx = x - ex, dy = y - ey, dz = z - ez;
    double h = sqrt(dx * dx + dz * dz);
    *yaw = degrees(atan2(dz, dx)) - 90.0;
    double p = -degrees(atan2(dy, h));
    p = p < 90.0 ? p : 90.0;             /* min(90.0, p) */
    *pitch = p > -90.0 ? p : -90.0;      /* max(-90.0, ...) */
}

/* macros._best_tool over the hotbar */
static int best_tool(const struct nwrl_loop_tabs *T, const struct nwrl_state *s, int64_t block)
{
    int64_t tb = inrange(block) ? T->tool_of[block] : -1;
    if (tb < 0) return -1;
    int best = -1;
    int64_t bt = 0;
    for (int i = 0; i < 9; ++i)
    {
        int64_t it = s->inv[i].item, t = inrange(it) ? T->tool[tb * 4096 + it] : 0;
        if (t > bt)
        {
            best = i;
            bt = t;
        }
    }
    return best;
}

/* a craft plan may exist (macros._craft_plan's conditions without the table's place: a superset; Python decides) */
static int craft_maybe(const struct nwrl_loop_tabs *T, const struct nwrl_state *s)
{
    int64_t c[5] = {0, 0, 0, 0, 0}, tier = 0;
    for (int j = 0; j < 36; ++j)
    {
        int64_t it = clip4k(s->inv[j].item);
        for (int q = 0; q < 5; ++q) c[q] += T->cnt[q * 4096 + it] * s->inv[j].count;
        if (T->pick[it] > tier) tier = T->pick[it];
    }
    int64_t logs = c[0], planks = c[1], sticks = c[2], tables = c[3], cobble = c[4];
    return (logs > 0 && planks < 12) || (tables == 0 && planks >= 4) || (sticks < 2 && planks >= 2) ||
           (tier < 1 && planks >= 3 && sticks >= 2) || (tier < 2 && cobble >= 3 && sticks >= 2);
}

/* one env of nwrl_loop_harvest: its act, kind, chosen and visit */
static void harvest1(const struct nwrl_loop_tabs *T, const struct nwrl_loop_env *E, const struct nwrl_result *r,
                     int e, const float *pk, struct nwrl_act *a, int8_t *kind, uint8_t *chosen, uint8_t *visit)
{
    const struct nwrl_state *s = &r->st;
    /* actmap.to_acts (the camera bins are the packed floats' whole values, as astype(int64) reads them) */
    memset(a, 0, sizeof *a);
    a->hotbar = -1;
    uint32_t hold = 0;
    const float *kq = pk + 2;
    for (int j = 0; j < T->nkeys; ++j)
        if (kq[j] > 0.5f) hold |= T->keybit[j];
    a->hold = hold;
    uint32_t nw = hold & ~E->prev_hold[e];
    a->press[K_ATTACK] = (uint8_t)((nw >> K_ATTACK) & 1);
    a->press[K_USE] = (uint8_t)((nw >> K_USE) & 1);
    a->press[K_DROP] = kq[T->key_drop] > 0.5f;
    a->look_mode = L_DELTA;
    a->look[0] = (float)T->camdeg[(int64_t)pk[1]];
    a->look[1] = (float)T->camdeg[(int64_t)pk[0]];
    E->prev_hold[e] = hold;
    int attack = (hold >> K_ATTACK) & 1;
    /* Macros.apply: which envs a macro holds or may start */
    *kind = M_NONE;
    *chosen = 0;
    *visit = 0;
    int64_t steps = ++E->steps[e];
    int8_t kk = E->kind[e];
    int dead = (r->flags & F_DEAD) != 0;
    int surf = T->on_surface && s->eye_water != 0 && r->air < 120;
    int loot = T->on_loot && s->nearest >= 0 && s->nearest_dist < 4.5f;
    int64_t b = s->mo_block;
    int lockc = T->on_mine && attack && s->mo_hit != 0 && T->lockable[clip4k(b)];
    int has = 0;
    if (T->on_craft && steps % 5 == 0)
        for (int j = 0; j < 36 && !has; ++j) has = inrange(s->inv[j].item) && T->craft[s->inv[j].item];
    if (!(kk != M_NONE || dead || r->screen != 0 || surf || loot || lockc || has)) return;
    if (dead)
    {
        a->nops = 1;
        a->ops[0].kind = G_RESPAWN;
        if (kk != M_NONE && kk != M_MINE) *visit |= NWRL_V_DEAD;
        E->kind[e] = M_NONE;
        return;
    }
    if (kk == M_NONE)
    {
        /* a macro Python runs may start (or the start needs Macros' state: the chests tried, the craft plan):
         * Macros._start decides, the lock in its order too */
        if ((r->screen != 0 && r->screen != S_GAMEOVER) || loot || surf || (has && craft_maybe(T, s)))
        {
            *visit |= NWRL_V_START;
            return;
        }
        int64_t tier = 0;
        for (int j = 0; j < 9; ++j)
        {
            int64_t pt = T->pick[clip4k(s->inv[j].item)];
            if (pt > tier) tier = pt;
        }
        if (T->on_mine && attack && s->mo_hit && inrange(b) && (T->hand[b] || (T->picky[b] > 0 && tier >= T->picky[b])))
        {
            kk = E->kind[e] = M_MINE;
            E->mpos[3 * e] = s->mo_x;
            E->mpos[3 * e + 1] = s->mo_y;
            E->mpos[3 * e + 2] = s->mo_z;
            E->mblock[e] = b;
            E->mage[e] = 0;
            *chosen = 1;
        }
    }
    if (kk == M_NONE) return;
    if (kk != M_MINE)
    {
        *visit |= NWRL_V_DRIVE;
        return;
    }
    /* the mine lock (Macros._drive) */
    *kind = M_MINE;
    int64_t x = E->mpos[3 * e], y = E->mpos[3 * e + 1], z = E->mpos[3 * e + 2];
    double ey = r->y + 1.62;
    double cx = (double)x + 0.5, cy = (double)y + 0.5, cz = (double)z + 0.5;
    double dx = r->x - cx, dy = ey - cy, dz = r->z - cz;
    double d = sqrt(dx * dx + dy * dy + dz * dz);
    if (fabs(d - 5.0) < T->near5)
    {
        *visit |= NWRL_V_MINE;
        return;
    }
    int64_t age = ++E->mage[e];
    int here = s->mo_hit && s->mo_x == x && s->mo_y == y && s->mo_z == z;
    if ((age > 1 && !here) || age > 200 || d > 5.0 || r->screen != 0)
    {
        if (age > 1 && !here && d <= 5.0) *visit |= NWRL_V_MINED;
        E->kind[e] = M_NONE;
        return;
    }
    double yaw, pitch;
    look_at(r->x, ey, r->z, cx, cy, cz, &yaw, &pitch);
    a->hold = 1u << K_ATTACK;
    memset(a->press, 0, sizeof a->press);
    a->look_mode = L_ABS;
    a->look[0] = (float)yaw;
    a->look[1] = (float)pitch;
    int best = best_tool(T, s, E->mblock[e]);
    if (best >= 0 && best != s->current) a->hotbar = best;
}

int nwrl_loop_harvest(const struct nwrl_loop_tabs *T, const struct nwrl_loop_env *E, const struct nwrl_result *R,
                      int k, int lo, const float *packed, int stride, struct nwrl_act *acts, int8_t *kind,
                      uint8_t *chosen, uint8_t *visit)
{
    for (int q = 0; q < k; ++q)    /* a camera bin outside the table: refused before anything moves */
    {
        float p = packed[(int64_t)q * stride], y = packed[(int64_t)q * stride + 1];
        if (!(p >= 0.0f && p <= 40.0f && y >= 0.0f && y <= 40.0f)) return -1;
    }
    int nvisit = 0;
    for (int q = 0; q < k; ++q)
    {
        int e = lo + q;
        harvest1(T, E, &R[q], e, packed + (int64_t)q * stride, &acts[q], &kind[q], &chosen[q], &visit[q]);
        /* AsyncRunner._harvest's books (a visited env's are Python's, after its visit) */
        int64_t te = E->te[e]++;
        if (visit[q])
        {
            ++nvisit;
            continue;
        }
        E->macro_n[e * 6 + kind[q]] += 1;
        if (te < E->ep_len) E->mask[e * E->ep_len + te] = kind[q] == M_NONE || chosen[q];
    }
    return nvisit;
}
