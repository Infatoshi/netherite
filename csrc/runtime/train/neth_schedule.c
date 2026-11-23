/* ntloop's schedules: the starts' mix and its curriculum (curric.h), the sim's own fidelity, and the fidelity ladder
 * (the run file's policy.ladder), parsed and moved at run time. */
#define _GNU_SOURCE
#include "neth_pipe_internal.h"
#include "neth_runconf.h"
#include "curric.h"

/* ---- the starts: the seed set, the library and the curriculum (curric.h; off by default) ---- */

int neth_schedule_starts(struct neth_pipe *L, const struct neth_runconf *c, struct neth_vec_opts *o)
{
    static struct curric C0;
    const char *seeds_arg = c->seeds_arg;
    if (seeds_arg && !c->lib_arg && seeds_arg[0] && !seeds_arg[strspn(seeds_arg, "0123456789")])
    {
        /* a seed set of the world's own seed alone (configs/tests/brute16fast.yaml's starts.seeds 42) is the run's own
         * start, not a mix: no seed set, so the milestone archive still runs with it */
        static struct wconf c1;
        char err[1024];
        if (!wconf_load(&c1, c->config, err, sizeof err) && atoll(seeds_arg) == atoll(c1.val[WC_SEED])) seeds_arg = NULL;
    }
    if (seeds_arg || c->lib_arg)
    {
        /* the mix of the seed set's and the library's starts (curric.h) */
        static struct wconf wc;
        char err[1024];
        if (wconf_load(&wc, c->config, err, sizeof err))
        {
            printf("FAIL: %s\n", err);
            return 2;
        }
        if (cur_build(&C0, c->config, atoi(wc.val[WC_SEED]), seeds_arg, c->seedset_arg, c->lib_arg, c->stages_arg,
                      c->libseeds_arg, c->cur_arg, err, sizeof err))
        {
            printf("FAIL: %s\n", err);
            return 2;
        }
        if (c->cur_floor >= 0) C0.floor = c->cur_floor;
        if (c->cur_spawn >= 0) C0.spawn_min = c->cur_spawn;
        if (c->cur_mineps >= 0) C0.min_eps = c->cur_mineps;
        if (C0.mode == CUR_ADAPTIVE)
        {
            int S = 0;
            for (int j = 0; j < STAGE_N; ++j) S += C0.cnt[j] > 0;
            if (C0.floor * S > 1.0 || C0.spawn_min > 1.0)
            {
                printf("FAIL: starts.floor %g over %d stages, starts.spawn_min %g: more than every share\n", C0.floor, S,
                       C0.spawn_min);
                return 2;
            }
        }
        if (o->save_item || c->lad.nr)
        {
            printf("FAIL: starts.seeds and starts.library with starts.archive or a ladder are not done yet\n");
            return 2;
        }
        if (c->stagelen_arg)
        {
            /* each start's episode length by its stage; with share steps, the draws by length */
            int32_t *sep = malloc((size_t)(C0.nstart + 1) * sizeof *sep);
            if (cur_stage_len(&C0, c->stagelen_arg, c->ep_len, sep, err, sizeof err))
            {
                printf("FAIL: %s\n", err);
                return 2;
            }
            o->start_ep_len = sep;
            if (!strcmp(c->share_arg, "steps"))
            {
                C0.len = malloc((size_t)(C0.nstart + 1) * sizeof *C0.len);
                for (int k = 0; k <= C0.nstart; ++k) C0.len[k] = (double)sep[k] / c->ep_len;
            }
            printf("lane/stagelen: episode seconds by stage (the rest %.0f), shares of %s:", c->ep_len / 20.0,
                   C0.len ? "env-steps" : "resets");
            for (int j = 0; j < STAGE_N; ++j)
                if (C0.cnt[j] && C0.stage_sec[j] > 0) printf(" %s %.0f", STAGE_CHAIN[j].name, C0.stage_sec[j]);
            printf("\n");
        }
        cur_weights(&C0);
        L->sscale = c->stage_scale != 0;
        o->starts = (const char *const *)C0.path;
        o->nstarts = C0.nstart;
        o->start_weights = C0.sw;
        o->flags |= NETH_RL_ANYSEED;
        L->crc = &C0;
        int S = 0;
        for (int j = 0; j < STAGE_N; ++j) S += C0.cnt[j] > 0;
        printf("starts: %d besides the sim's own (%s), %d stages, curriculum %s:", C0.nstart,
               C0.start0 ? "in the draws" : "not drawn", S,
               C0.mode == CUR_ADAPTIVE ? "adaptive" : C0.mode == CUR_UNIFORM ? "uniform" : "off");
        for (int j = 0; j < STAGE_N; ++j)
            if (C0.cnt[j]) printf(" %s %d (%.3f)", STAGE_CHAIN[j].name, C0.cnt[j], C0.w[j]);
        printf("\n");
    }
    /* o->nstarts is the mix's, set just above */
    if (c->start_cycle)
        printf("lane/eval1k: start cycle on: env i's episode j plays library start (i + j n) mod %d, no draw\n",
               o->nstarts);
    if ((c->stagelen_arg && !L->crc) || (strcmp(c->share_arg, "resets") && strcmp(c->share_arg, "steps")) ||
        (strcmp(c->clock_arg, "played") && strcmp(c->clock_arg, "left")))
    {
        printf("FAIL: starts.episode_seconds needs starts.seeds or starts.library (the stages are the mix's); "
               "starts.share is resets or steps (%s); starts.clock is played or left (%s)\n", c->share_arg,
               c->clock_arg);
        return 2;
    }
    o->clock = !strcmp(c->clock_arg, "left");
    if (o->clock) printf("lane/stagelen: starts.clock left (o[7] the last five minutes' share gone)\n");
    if (c->stage_scale && !L->crc)
    {
        printf("FAIL: ppo.stage_scale needs starts.seeds or starts.library (the stages are the mix's)\n");
        return 2;
    }
    if (c->reward_rel || c->stage_scale)
        printf("lane/curric2: reward.relative %s, ppo.stage_scale %s\n", c->reward_rel ? "on" : "off",
               c->stage_scale ? "on" : "off");
    return 0;
}

/* the sim's own fidelity: every episode's without a ladder (the records' fid), and a ladder's floor */
int neth_schedule_fidelity(struct neth_pipe *L, const struct neth_runconf *c, const struct neth_vec_opts *o)
{
    static struct wconf wc;
    struct fidelity f;
    char err[1024];
    if (wconf_load(&wc, c->config, err, sizeof err) || fidelity_from_conf(&f, &wc, err, sizeof err))
    {
        printf("FAIL: %s\n", err);
        return 2;
    }
    if (c->train_skip_ambient) f.bits |= FID_SKIP_AMBIENT;
    if (c->distant_tick_radius > 0)
    {
        f.distant_tick_radius = c->distant_tick_radius > 255 ? 255 : c->distant_tick_radius;
        f.bits |= FID_DISTANT_TICKS;
    }
    L->sim_code = rec_fid_code(&f);
    if (!c->lad.nr) return 0;
    char d[256];
    fidelity_describe(&f, d, sizeof d);
    /* a rung is put on the start's image at a reset; the image holds the sim's own fidelity, which a reset cannot
     * take off (fidelity_apply sets a world's switches, it does not bring back the spawner or a living set dead), so
     * the sim must be vanilla */
    if (!fidelity_vanilla(&f) || strcmp(wconf_get(&wc, WC_FID_RUNG), "exact"))
    {
        printf("FAIL: %s: policy.ladder needs the sim at vanilla fidelity and fidelity.rung exact (its rungs go on at "
               "the resets), not %s, rung %s\n", c->run, d, wconf_get(&wc, WC_FID_RUNG));
        return 2;
    }
    /* a saved start is an earlier episode's replay at that episode's rung: its image would carry a lower rung's world
     * into a higher one's episode */
    if (o->save_item)
    {
        printf("FAIL: %s: policy.ladder with starts.archive (--save-starts) is not done yet: a saved start carries the "
               "rung its episode ran\n", c->run);
        return 2;
    }
    return 0;
}

/* the run file's ladder on the made env: each env's rungs, the schedule's window, the ladder's line */
void neth_schedule_ladder_start(struct neth_pipe *L, struct neth_runconf *c)
{
    if (!c->lad.nr) return;
    struct neth_ladder *D = L->lad = &c->lad;
    int n = L->n;
    D->pool = pipe_pool(neth_rl_pipe(neth_vec_env(L->v)));
    D->t0 = L->t_start;
    D->cur = malloc((size_t)n * sizeof *D->cur);
    D->pend = malloc((size_t)n * sizeof *D->pend);
    D->ended = malloc((size_t)n * sizeof *D->ended);
    for (int i = 0; i < n; ++i) D->cur[i] = D->pend[i] = D->ended[i] = -1;
    if (D->sched == LAD_MILESTONE) D->mring = calloc((size_t)D->window, 1);
    printf("ladder: %d rung%s, low to high:", D->nr, D->nr > 1 ? "s" : "");
    for (int r = 0; r < D->nr; ++r) printf("%s %d %s", r ? "," : "", r, D->name[r]);
    printf("; schedule %s", neth_lad_sched[D->sched]);
    if (D->sched == LAD_FIXED)
        for (int r = 0; r < D->nr; ++r) printf("%s%g", r ? "/" : " shares ", D->share[r]);
    else
        for (int r = 0; r + 1 < D->nr; ++r) printf("%s%g", r ? "," : " at ", D->at[r]);
    if (D->sched == LAD_MILESTONE) printf(" (%s over %d episodes)", neth_ms_names[D->ms], D->window);
    printf(", fade %g, by %s; each env's first episode (the make's) at the sim's own fidelity, rung -1\n", D->fade,
           D->unit_env ? "env" : "group");
}

/* each rung's books: the episodes that ended there and the env-steps run there */
void neth_schedule_report(struct neth_pipe *L)
{
    if (!L->lad || L->fail) return;
    const struct neth_ladder *D = L->lad;
    static const int MS5[5] = {0, 1, 4, 5, 2};
    for (int r = -1; r < D->nr; ++r)
    {
        int64_t e = D->eps[1 + r];
        if (r < 0 && e == 0 && D->steps[0] == 0) continue;
        printf("ladder rung %d %s: %lld episodes, %lld env-steps; wooden/stone pickaxe, furnace, iron ingot, iron "
               "pickaxe", r, r < 0 ? "(the make's episodes, the sim's fidelity)" : D->name[r], (long long)e,
               (long long)D->steps[1 + r]);
        for (int q = 0; q < 5; ++q)
            printf("%s%.1f", q ? " / " : " ", e ? 100.0 * D->msn[1 + r][MS5[q]] / e : 0.0);
        printf("%%, return %.3f\n", e ? D->ret[1 + r] / e : 0.0);
    }
    printf("ladder: %lld env moves; the top rung open %d\n", (long long)D->moves,
           D->sched == LAD_MILESTONE ? D->top : -1);
}

/* ---- the fidelity ladder ---- */

/* ---- the fidelity ladder (SPEC.md "The fidelity ladder") ----
 *
 * The run file's policy.ladder section: a run starts on a low-fidelity rung and moves the batch up in intervals, the
 * batch fading across (some groups or envs a rung up, more over time). Every rung is the one engine on the one start
 * image, which is the sim's own and so must be vanilla: a rung's fidelity is put on an env at its reset (pool.h
 * pool_rung_set, pool_rung_fid: the stripped rung with the rung's switches), never mid-episode, the actor and the
 * learner unchanged. The keys:
 *   rungs      low to high, a comma between rungs: vanilla; fast (train-fast.yaml's: spawning, random_ticks and weather
 *              off); stripped (the pool's stripped rung: fast and mobs none); or a '+' list of the switches the rung
 *              turns off, of spawning, random_ticks, weather and mobs (none): "spawning+weather" (the policy part is
 *              held to the config's grammar: a value has no spaces and no '=')
 *   schedule   what moves the batch up: env_steps (the default), minutes, milestone (the rate at which the top open
 *              rung's episodes reach `milestone`, over its last `window` episodes, default 256), or fixed (`shares`)
 *   at         where each rung past the first opens (a comma list, one a boundary, rising): env-steps, minutes or the
 *              milestone's rate
 *   fade       0 (the default): a boundary moves every unit at once; else the units cross one after another over this
 *              span past the boundary (the schedule's unit; the milestone schedule's in env-steps), unit u (0 first)
 *              once the share past the boundary reaches (u + 0.5) / units
 *   unit       group (the default: a group is rung-homogeneous, its step waits for its slowest env) or env
 *   shares     fixed's: each rung's share of the units, summing to 1 (the lowest units on the highest rung)
 * Each env's first episode is the vector env's make's, before ntloop can set a rung: it runs at the sim's own
 * fidelity (vanilla) and is booked as rung -1. */
const char *const neth_lad_sched[4] = {"env_steps", "minutes", "milestone", "fixed"};

/* one rung's text into its fidelity: 0, or -1 with err */
static int ladder_rung(const char *txt, struct fidelity *f, char *err, size_t en)
{
    static struct wconf c;
    wconf_defaults(&c);
    const char *offs = !strcmp(txt, "vanilla") ? ""
                       : !strcmp(txt, "fast") ? "spawning+random_ticks+weather"
                       : !strcmp(txt, "stripped") ? "spawning+random_ticks+weather+mobs"
                                                  : txt;
    char b[512];
    snprintf(b, sizeof b, "%s", offs);
    static const char *const OFF[5][2] = {{"spawning", "fidelity.spawning=off"},
                                          {"random_ticks", "fidelity.random_ticks=off"},
                                          {"weather", "fidelity.weather=off"},
                                          {"mobs", "fidelity.mobs=none"},
                                          {"skip_ambient", "fidelity.skip_ambient=on"}};
    for (char *sv, *t = strtok_r(b, "+", &sv); t; t = strtok_r(NULL, "+", &sv))
    {
        int k = -1;
        for (int i = 0; i < 5; ++i)
            if (!strcmp(t, OFF[i][0])) k = i;
        if (k < 0)
            return snprintf(err, en, "ladder.rungs %s: %s: a rung is vanilla, fast, stripped or a '+' list of the "
                            "switches it turns off (spawning, random_ticks, weather, mobs, skip_ambient)", txt, t), -1;
        char e[256];
        if (wconf_set(&c, OFF[k][1], e, sizeof e)) return snprintf(err, en, "ladder.rungs %s: %s", txt, e), -1;
    }
    char e[256];
    if (fidelity_from_conf(f, &c, e, sizeof e)) return snprintf(err, en, "ladder.rungs %s: %s", txt, e), -1;
    return 0;
}

/* a comma list of numbers into x[0..max): how many, or -1 */
static int numlist(const char *s, double *x, int max)
{
    char b[256];
    snprintf(b, sizeof b, "%s", s);
    int k = 0;
    for (char *sv, *t = strtok_r(b, ",", &sv); t; t = strtok_r(NULL, ",", &sv))
    {
        char *e;
        if (k == max) return -1;
        x[k++] = strtod(t, &e);
        while (*e == ' ') ++e;
        if (*e) return -1;
    }
    return k;
}

/* the run file's ladder section into D (D->nr 0: none): 0, or -1 with err */
int neth_ladder_parse(struct neth_ladder *D, const struct neth_runconf *rc, char *err, size_t en)
{
    memset(D, 0, sizeof *D);
    if (!rc->lad_rungs[0])
    {
        if (rc->lad_sched[0] || rc->lad_at[0] || rc->lad_fade[0] || rc->lad_unit[0] || rc->lad_ms[0] ||
            rc->lad_shares[0] || rc->lad_window[0])
            return snprintf(err, en, "policy.ladder: no rungs"), -1;
        return 0;
    }
    char b[512];
    snprintf(b, sizeof b, "%s", rc->lad_rungs);
    for (char *sv, *t = strtok_r(b, ",", &sv); t; t = strtok_r(NULL, ",", &sv))
    {
        while (*t == ' ') ++t;
        size_t l = strlen(t);
        while (l && t[l - 1] == ' ') t[--l] = 0;
        if (D->nr == LAD_MAX) return snprintf(err, en, "ladder.rungs: at most %d", LAD_MAX), -1;
        if (ladder_rung(t, &D->f[D->nr], err, en)) return -1;
        D->vanilla[D->nr] = fidelity_vanilla(&D->f[D->nr]);
        D->code[D->nr] = rec_fid_code(&D->f[D->nr]);
        char d[128];
        fidelity_describe(&D->f[D->nr], d, sizeof d);
        snprintf(D->name[D->nr], sizeof D->name[0], "%s%s%s%s", t, strcmp(t, d) ? " (" : "", strcmp(t, d) ? d : "",
                 strcmp(t, d) ? ")" : "");
        ++D->nr;
    }
    if (D->nr < 1) return snprintf(err, en, "ladder.rungs: none"), -1;
    D->sched = LAD_ENV_STEPS;
    if (rc->lad_sched[0])
    {
        D->sched = -1;
        for (int s = 0; s < 4; ++s)
            if (!strcmp(rc->lad_sched, neth_lad_sched[s])) D->sched = s;
        if (D->sched < 0)
            return snprintf(err, en, "ladder.schedule %s: env_steps, minutes, milestone or fixed", rc->lad_sched), -1;
    }
    D->unit_env = !strcmp(rc->lad_unit, "env");
    if (rc->lad_unit[0] && !D->unit_env && strcmp(rc->lad_unit, "group"))
        return snprintf(err, en, "ladder.unit %s: group or env", rc->lad_unit), -1;
    D->fade = rc->lad_fade[0] ? atof(rc->lad_fade) : 0;
    if (D->fade < 0) return snprintf(err, en, "ladder.fade %s: at least 0", rc->lad_fade), -1;
    if (D->sched == LAD_FIXED)
    {
        double s = 0;
        if (numlist(rc->lad_shares, D->share, LAD_MAX) != D->nr)
            return snprintf(err, en, "ladder.shares %s: one share a rung (%d)", rc->lad_shares, D->nr), -1;
        for (int i = 0; i < D->nr; ++i) s += D->share[i];
        if (fabs(s - 1) > 1e-6) return snprintf(err, en, "ladder.shares %s: they sum to 1", rc->lad_shares), -1;
        if (rc->lad_at[0]) return snprintf(err, en, "ladder.at: the fixed schedule takes shares"), -1;
        return 0;
    }
    if (rc->lad_shares[0]) return snprintf(err, en, "ladder.shares: the fixed schedule's"), -1;
    if (numlist(rc->lad_at, D->at, LAD_MAX) != D->nr - 1)
        return snprintf(err, en, "ladder.at %s: one a boundary (%d)", rc->lad_at, D->nr - 1), -1;
    for (int i = 1; i < D->nr - 1; ++i)
        if (D->sched != LAD_MILESTONE && D->at[i] < D->at[i - 1]) return snprintf(err, en, "ladder.at: rising"), -1;
    if (D->sched == LAD_MILESTONE)
    {
        D->ms = -1;
        for (int i = 0; i < NETH_VEC_MILESTONES; ++i)
            if (!strcmp(rc->lad_ms, neth_ms_names[i])) D->ms = i;
        if (D->ms < 0) return snprintf(err, en, "ladder.milestone %s: one of reward.MILESTONES", rc->lad_ms), -1;
        D->window = rc->lad_window[0] ? atoi(rc->lad_window) : 256;
        if (D->window < 1) return snprintf(err, en, "ladder.window: at least 1"), -1;
    }
    else if (rc->lad_ms[0] || rc->lad_window[0])
        return snprintf(err, en, "ladder.milestone and window: the milestone schedule's"), -1;
    return 0;
}
/* ---- the ladder at run time ---- */

/* the progress the schedule reads */
static double lad_progress(const struct neth_pipe *L)
{
    const struct neth_ladder *D = L->lad;
    return D->sched == LAD_MINUTES ? (wall_s() - D->t0) / 60 : (double)L->env_steps;
}

/* each boundary's share of the units past it (sh[i]: on a rung above i) */
static void lad_shares(const struct neth_pipe *L, double *sh)
{
    const struct neth_ladder *D = L->lad;
    if (D->sched == LAD_FIXED)
    {
        for (int i = 0; i < D->nr - 1; ++i)
        {
            sh[i] = 0;
            for (int j = i + 1; j < D->nr; ++j) sh[i] += D->share[j];
        }
        return;
    }
    double p = lad_progress(L);
    for (int i = 0; i < D->nr - 1; ++i)
    {
        double o = D->sched == LAD_MILESTONE ? (i + 1 <= D->top ? D->open[i + 1] : INFINITY) : D->at[i];
        double s = p < o ? 0 : D->fade > 0 ? (p - o) / D->fade : 1;
        s = s > 1 ? 1 : s;
        sh[i] = i > 0 && s > sh[i - 1] ? sh[i - 1] : s;
    }
}

/* the rung of unit u of U */
static int lad_rung_of(const struct neth_ladder *D, const double *sh, int u, int U)
{
    int r = 0;
    for (int i = 0; i < D->nr - 1; ++i)
        if (u < (int)floor(sh[i] * U + 0.5)) r = i + 1;
    return r;
}

/* group g from recv, first: its last step booked at the rung each env's episode runs, and where an episode ended
 * with it, that rung kept as the ended episode's (ended[i]) and the rung its reset (inside recv) put on taken as the
 * new episode's */
void neth_ladder_ends(struct neth_pipe *L, int g, int was)
{
    struct neth_ladder *D = L->lad;
    const struct neth_vec_bufs *b = &L->b;
    for (int i = g * L->gs; i < (g + 1) * L->gs; ++i)
    {
        if (was) ++D->steps[1 + D->cur[i]];
        if (!was || !b->truncations[i]) continue;
        D->ended[i] = D->cur[i];
        D->cur[i] = D->pend[i];
    }
}

/* then, once the ended episodes' records are booked (ladder_book: the milestone schedule reads them): each env's
 * rung from its next reset, set where the schedule moved it. Nothing here touches an env in flight: g is received */
void neth_ladder_assign(struct neth_pipe *L, int g)
{
    struct neth_ladder *D = L->lad;
    int lo = g * L->gs, hi = lo + L->gs;
    double sh[LAD_MAX];
    lad_shares(L, sh);
    int U = D->unit_env ? L->n : L->G, moved = 0, to = -1;
    for (int i = lo; i < hi; ++i)
    {
        int r = lad_rung_of(D, sh, D->unit_env ? i : g, U);
        if (r == D->pend[i]) continue;
        if (pool_rung_set(D->pool, i, D->vanilla[r] ? NETH_RUNG_EXACT : NETH_RUNG_STRIPPED) ||
            pool_rung_fid(D->pool, i, D->vanilla[r] ? NULL : &D->f[r]))
        {
            printf("FAIL: the ladder: env %d takes no rung\n", i);
            L->fail = 1;
            return;
        }
        D->pend[i] = r;
        ++moved;
        to = r;
        ++D->moves;
    }
    if (moved)
        printf("ladder: %s%d%s to rung %d (%s) from %s next reset, at %lld env-steps, minute %.2f\n",
               D->unit_env ? "" : "group ", D->unit_env ? moved : g, D->unit_env ? " envs of group" : "", to,
               D->name[to], D->unit_env || moved > 1 ? "their" : "its", (long long)L->env_steps, (wall_s() - D->t0) / 60);
}

/* an ended episode's record on the books (its rung r, -1 the make's); the milestone schedule's window */
void neth_ladder_book(struct neth_pipe *L, const struct neth_vec_episode *e, int r)
{
    struct neth_ladder *D = L->lad;
    ++D->eps[1 + r];
    D->ret[1 + r] += e->ret;
    for (int m = 0; m < NETH_VEC_MILESTONES; ++m) D->msn[1 + r][m] += (e->ms >> m) & 1;
    if (D->sched != LAD_MILESTONE || r != D->top || D->top >= D->nr - 1) return;
    int hit = (e->ms >> D->ms) & 1;
    if (D->mn == D->window) D->mhit -= D->mring[D->mat];
    else ++D->mn;
    D->mring[D->mat] = (uint8_t)hit;
    D->mhit += hit;
    D->mat = (D->mat + 1) % D->window;
    if (D->mn == D->window && (double)D->mhit / D->window >= D->at[D->top])
    {
        D->open[++D->top] = (double)L->env_steps;
        printf("ladder: rung %d (%s) opens at %lld env-steps, minute %.2f: %s in %.1f%% of rung %d's last %d episodes\n",
               D->top, D->name[D->top], (long long)L->env_steps, (wall_s() - D->t0) / 60, neth_ms_names[D->ms],
               100.0 * D->mhit / D->window, D->top - 1, D->window);
        D->mn = D->mat = D->mhit = 0;
    }
}
