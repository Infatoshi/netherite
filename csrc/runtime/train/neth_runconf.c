/* ntloop's settings: the schema (one row a setting: its flag, its run file key, its type, its default, its help), the
 * run file's policy part read through it (worldconf.c's grammar; mcsr's fastrun.py keys), then the flags, then what
 * the settings imply and refuse, and the vector env's options made from them. */
#define _GNU_SOURCE
#include "neth_runconf.h"
#include "actor.h"

/* ---- the macro sets, the milestones, the starts' archive ---- */

/* a macro set's name's bit, 0 none: the macros' registration table's names (each macro's .sw in csrc/mods/macros, read
 * through vecenv.h neth_vec_macro_switch; brutetab.c reads the same) */
static int macro_bit(const char *name)
{
    int32_t bit;
    for (int i = 0;; ++i)
    {
        const char *n = neth_vec_macro_switch(i, &bit);
        if (!n) return 0;
        if (!strcmp(n, name)) return bit;
    }
}

int neth_macro_set(const char *spec, char *err, size_t en)
{
    static const char *const BASE = "surface,loot,mine,craft";
    static const char *const RECIPE = "surface,loot,mine,craft,seek,carry,iron,late,liquid,portal,end,fight,table-anywhere";
    int bits = 0;
    char buf[512];
    snprintf(buf, sizeof buf, "%s", spec);
    for (char *sv, *t = strtok_r(buf, ",", &sv); t; t = strtok_r(NULL, ",", &sv))
    {
        while (*t == ' ') ++t;
        int drop = *t == '-';
        while (*t == '-' || *t == '+') ++t;
        int add = 0;
        if (!strcmp(t, "none")) continue;   /* no macro (the raw controls alone) */
        if (!strcmp(t, "base") || !strcmp(t, "recipe"))
        {
            char l[256];
            snprintf(l, sizeof l, "%s", !strcmp(t, "base") ? BASE : RECIPE);
            for (char *s2, *u = strtok_r(l, ",", &s2); u; u = strtok_r(NULL, ",", &s2)) add |= macro_bit(u);
        }
        else
        {
            add = macro_bit(t);
            if (!add && *t) return snprintf(err, en, "actions.macros: no such macro %s", t), -1;
        }
        bits = drop ? bits & ~add : bits | add;
    }
    return bits;
}

int64_t neth_ms_mask(const char *spec, char *err, size_t en)
{
    char b[512];
    snprintf(b, sizeof b, "%s", spec);
    int64_t m = 0;
    for (char *sv, *t = strtok_r(b, "+", &sv); t; t = strtok_r(NULL, "+", &sv))
    {
        int k = -1;
        for (int i = 0; i < NETH_VEC_MILESTONES; ++i)
            if (!strcmp(t, neth_ms_names[i])) k = i;
        if (k < 0) return snprintf(err, en, "no such milestone %s", t), -1;
        m |= 1 << k;
    }
    return m;
}

/* --save-starts (fastppo's): "ms:NAME+NAME...:POOL:SHARE" (a start saved at each milestone gained),
 * "cells:POOL:SHARE", "ITEM:COUNT:POOL:SHARE" or "" (off), into the vec env's options as mcsr's VecEnv(save=) puts
 * them */
static int save_starts(const char *spec, struct neth_vec_opts *o, char *err, size_t en)
{
    if (!spec || !*spec) return 0;
    char b[600], *f[5] = {0};
    snprintf(b, sizeof b, "%s", spec);
    int nf = 0;
    for (char *sv, *t = strtok_r(b, ":", &sv); t && nf < 5; t = strtok_r(NULL, ":", &sv)) f[nf++] = t;
    if (!strcmp(f[0], "ms") && nf == 4)
    {
        int mask = 0;
        for (char *sv, *t = strtok_r(f[1], "+", &sv); t; t = strtok_r(NULL, "+", &sv))
        {
            int k = -1;
            for (int i = 0; i < NETH_VEC_MILESTONES; ++i)
                if (!strcmp(t, neth_ms_names[i])) k = i;
            if (!strcmp(t, "nether")) k = NETH_VEC_MILESTONES;
            if (!strcmp(t, "end")) k = NETH_VEC_MS_END;
            if (!strcmp(t, "dragon")) k = NETH_VEC_MS_DRAGON;
            if (k < 0) return snprintf(err, en, "--save-starts: no such milestone %s", t), -1;
            mask |= 1 << k;
        }
        o->save_item = -1, o->save_count = mask, o->save_pool = atoi(f[2]), o->save_share = atof(f[3]);
    }
    else if (!strcmp(f[0], "cells") && nf == 3)
        o->save_item = -2, o->save_count = 0, o->save_pool = atoi(f[1]), o->save_share = atof(f[2]);
    else if (nf == 4)
        o->save_item = atoi(f[0]), o->save_count = atoi(f[1]), o->save_pool = atoi(f[2]), o->save_share = atof(f[3]);
    else
        return snprintf(err, en, "--save-starts %.400s: ms:NAMES:POOL:SHARE, cells:POOL:SHARE or ITEM:COUNT:POOL:SHARE",
                        spec), -1;
    return 0;
}

int neth_zsc_parse(const char *a, int *lo, int *n)
{
    const char *c = strchr(a, ':');
    *lo = atoi(a), *n = c ? atoi(c + 1) : 0;
    if (!strncmp(a, "ore", 3))  /* lane/audit-feat: the run's or block (net.h NETH_NET_OFF_OF), none without it */
        *lo = NETH_NET_OFF_OF(NETH_NET_FEATMASK, NETH_NET_FOR), *n = NETH_NET_FEATMASK & NETH_NET_FOR ? NETH_NET_ORE_N : 0;
    return *n <= 0 || *lo < 0 || *lo + *n > NETH_NET_NS ? -1 : 0;
}

/* ---- the schema ---- */

enum {
    RC_INT,     /* atoi */
    RC_DBL,     /* atof */
    RC_FLT,     /* (float)atof */
    RC_U64,     /* strtoull (RC_ATOF: the run file's through atof) */
    RC_STR,     /* the text (the run file's copied) */
    RC_SET,     /* a flag without a value: 1 */
    RC_BUF,     /* the run file's text into a char array, read once the file is read */
    RC_ONOFF,   /* on or off: the run file's anything else refused, the flag's anything but on off */
    RC_FN,      /* the row's callback */
};
#define RC_POS 1u      /* the run file's value counts only above 0 */
#define RC_NAN 2u      /* the run file's NaN keeps the default */
#define RC_ATOF 4u     /* RC_U64 from the run file through atof */
#define RC_SIM 8u      /* the key is the run file's sim part's (pipeline.*), not its policy part's */
#define RC_NOVAL 16u   /* RC_FN: the flag takes no value */

typedef int (*rc_fn)(struct neth_runconf *c, const char *v, int from_run, char *err, size_t en);

struct rc_row {
    const char *flag, *key;     /* either may be NULL */
    unsigned char type;
    unsigned fl;
    size_t off, size;           /* the field; size: RC_BUF's */
    const char *def;            /* the default, read as a flag's value (NULL: zero, or the code's: see rc_defaults) */
    rc_fn fn;
    const char *arg, *help;
};

#define F(m) offsetof(struct neth_runconf, m), sizeof(((struct neth_runconf *)0)->m)

static int fn_sticky(struct neth_runconf *c, const char *v, int r, char *e, size_t en)
{
    c->task_sticky = !strcmp(v, "on") ? 1 : !strcmp(v, "block") ? 2 : 0;
    return 0;
}
static int fn_dump(struct neth_runconf *c, const char *v, int r, char *e, size_t en)
{
    c->dump_at = atoi(v);
    c->dump_path = strchr(v, ':') ? strchr(v, ':') + 1 : NULL;
    return 0;
}
static int fn_fdigest(struct neth_runconf *c, const char *v, int r, char *e, size_t en)
{
    c->digest = c->fdigest = 1;
    return 0;
}
static int fn_eval_every(struct neth_runconf *c, const char *v, int r, char *e, size_t en)
{
    c->e_every = atof(v), c->e_from_run = r;
    return 0;
}
static int fn_prec(struct neth_runconf *c, const char *v, int r, char *e, size_t en)
{
    c->prec = !strcmp(v, "fp32") ? NETH_LEARN_FP32 : NETH_LEARN_BF16;
    return 0;
}
static int fn_save(struct neth_runconf *c, const char *v, int r, char *e, size_t en)
{
    c->save = v, c->save_from_run = 0;
    return 0;
}
static int fn_rec(struct neth_runconf *c, const char *v, int r, char *e, size_t en)
{
    c->rec_on = strcmp(v, "off") != 0;
    c->rec_dir = c->rec_on && strcmp(v, "on") ? v : NULL;
    return 0;
}
static int fn_sprev(struct neth_runconf *c, const char *v, int r, char *e, size_t en)
{
    if (strcmp(v, "env") && strcmp(v, "label")) return snprintf(e, en, "--student-prev %s: env or label", v), -1;
    c->sprev_env = !strcmp(v, "env");
    return 0;
}
static int fn_hint_shape(struct neth_runconf *c, const char *v, int r, char *e, size_t en)
{
    c->hint_noshape = !strcmp(v, "off");
    return 0;
}
static int fn_mopt(struct neth_runconf *c, const char *v, int r, char *e, size_t en)
{
    /* a macro option NAME=WORD over the run file's (neth_vec_make refuses an unknown one) */
    size_t l = strlen(c->mopts);
    snprintf(c->mopts + l, sizeof c->mopts - l, "%s%s", l ? "," : "", v);
    return 0;
}
static int fn_minutes(struct neth_runconf *c, const char *v, int r, char *e, size_t en)
{
    c->ep_len = (int)lround(atof(v) * 1200);
    return 0;
}
static int fn_clock(struct neth_runconf *c, const char *v, int r, char *e, size_t en)
{
    c->clock_wrap = !strcmp(v, "wrap") ? 1 : !strcmp(v, "raw") ? 0 : -2;
    return 0;
}
static int fn_macros(struct neth_runconf *c, const char *v, int r, char *e, size_t en)
{
    int m = neth_macro_set(v, e, en);
    if (m < 0) return -1;
    c->macros = m;
    return 0;
}
static int fn_ep_seconds(struct neth_runconf *c, const char *v, int r, char *e, size_t en)
{
    double x = atof(v);
    if (!isnan(x)) c->ep_len = (int)(x * 20);
    return 0;
}

static const struct rc_row ROWS[] = {
    /* the run */
    {"--config", NULL, RC_STR, 0, F(config), "configs/tests/train-fast.yaml", NULL, "F", "the sim (a run file is one)"},
    {"--ns", "pipeline.worlds", RC_INT, RC_POS | RC_SIM, F(n), "512", NULL, "N", "envs"},
    {"--groups", "pipeline.groups", RC_INT, RC_POS | RC_SIM, F(groups), "4", NULL, "G", "groups of envs"},
    {NULL, "pipeline.episode_seconds", RC_FN, RC_SIM, F(ep_len), NULL, fn_ep_seconds, NULL, "an episode's seconds"},
    {"--ep-len", NULL, RC_INT, 0, F(ep_len), "3600", NULL, "L", "an episode's steps"},
    {"--minutes", NULL, RC_FN, 0, F(ep_len), NULL, fn_minutes, "M", "an episode's minutes (--score's)"},
    {"--threads", NULL, RC_INT, 0, F(threads), "0", NULL, "T", "the env's workers (0: its own count)"},
    {"--warm", NULL, RC_INT, 0, F(warm), "100", NULL, "W", "group steps a group before the measured stretch"},
    {"--steps", NULL, RC_INT, 0, F(steps), "300", NULL, "S", "group steps a group measured"},
    {"--hours", "run.hours", RC_DBL, RC_NAN, F(hours), NULL, NULL, "H", "the run's length in hours"},
    {"--env-steps", "run.steps", RC_DBL, RC_NAN, F(max_steps), NULL, NULL, "N", "the run's length in env-steps"},
    {"--seed", "run.seed", RC_U64, RC_NAN | RC_ATOF, F(seed), "1", NULL, "K", "the sampler's seed"},
    {"--macros", NULL, RC_INT, 0, F(macros), "15", NULL, "BITS", "the macro set"},
    {NULL, "actions.macros", RC_FN, 0, F(macros), NULL, fn_macros, NULL, "the macro set by name"},
    {"--mopt", NULL, RC_FN, 0, F(mopts), NULL, fn_mopt, "NAME=WORD", "a macro option over the run file's"},
    {"--policy", NULL, RC_STR, 0, F(pol), "standin", NULL, "standin|noop|actor", "the policy (a run file's: actor)"},
    {"--init", NULL, RC_STR, 0, F(init), NULL, NULL, "W.ntw", "the actor's (or student's) weights"},
    /* lane/audit-feat: the scalar features (net.h's words), the run's: without them, --init's NTW2 header's */
    {"--features", NULL, RC_STR, 0, F(feat_arg), NULL, NULL, "WORDS",
     "the scalars past 155 (ls hs dk or gv tk, or none; --init's own, else the build's)"},
    {NULL, "net.features", RC_STR, 0, F(feat_run), NULL, NULL, NULL, "the scalars past 155"},
    {"--out", "run.out", RC_STR, 0, F(out), NULL, NULL, "DIR", "episodes.jsonl, iters.jsonl, model.ntw"},
    {"--log-every", "run.log_every", RC_DBL, RC_NAN, F(log_every), "30", NULL, "SEC", "a row's seconds"},
    {"--save-every", "run.save_every", RC_DBL, RC_NAN, F(save_every), NULL, NULL, "MIN", "a snapshot's minutes"},
    {"--start", NULL, RC_STR, 0, F(start_dir), NULL, NULL, "DIR", "the start (the config's pipeline.start)"},
    {"--dev", NULL, RC_SET, 0, F(dev), NULL, NULL, NULL, "the start a dev start"},
    {"--train-skip-ambient", NULL, RC_SET, 0, F(train_skip_ambient), NULL, NULL, NULL, "opt-in: skip ambient mobs (bats, squids) during spawn and tick"},
    {"--distant-tick-radius", NULL, RC_INT, 0, F(distant_tick_radius), NULL, NULL, "R", "opt-in: skip chunk ticks and sparsely tick distant non-boss mobs past R blocks (0 off)"},
    /* the pipe */
    {"--depth", NULL, RC_INT, 0, F(depth), "-1", NULL, "D", "groups launched and unsent (0 to groups - 1)"},
    {"--serve", NULL, RC_INT, 0, F(serve), "-1", NULL, "0|1", "serve (the default with the device's cast)"},
    {"--cast-max", NULL, RC_INT, 0, F(cast_max), NULL, NULL, "K", "groups a served cast (0 all)"},
    {"--cast-hold", NULL, RC_INT, 0, F(cast_hold), NULL, NULL, "0|1", "no cast while the last runs"},
    {"--prio", NULL, RC_INT, 0, F(prio), "-1", NULL, "0|1", "the groups' streams ahead of the learner's"},
    {"--idle-us", NULL, RC_DBL, 0, F(idle_us), NULL, NULL, "US", "a served wait's sleep (0 spin)"},
    {"--stage", NULL, RC_INT, 0, F(stage), NULL, NULL, "0|1", "the copies' kernel"},
    {"--poll-flag", NULL, RC_INT, 0, F(poll_flag), NULL, NULL, "0|1", "a group done by a mapped flag, no event query"},
    {"--graph", NULL, RC_INT, 0, F(graph), NULL, NULL, "0|1", "a group step one CUDA graph (implies the flag)"},
    {"--dump-cast", NULL, RC_FN, 0, F(dump_at), NULL, fn_dump, "N:PATH", "group 0's N-th cast to PATH"},
    {"--T", "ppo.horizon", RC_INT, RC_POS, F(T), "64", NULL, "T", "a segment's steps"},
    /* the checks */
    {"--digest", NULL, RC_SET, 0, F(digest), NULL, NULL, NULL, "the trajectory digest"},
    {"--frame-digest", NULL, RC_FN, RC_NOVAL, F(fdigest), NULL, fn_fdigest, NULL, "and the segments' digest"},
    {"--ab", NULL, RC_INT, 0, F(ab), NULL, NULL, "R", "R pairs of A and B stretches"},
    {"--ab-swap", NULL, RC_SET, 0, F(ab_swap), NULL, NULL, NULL, "B A A B"},
    {"--ab-a", NULL, RC_STR, 0, F(ab_a), "vec", NULL, "vec|loop", "A: vec_bench's loop or this one's stand-in"},
    {"--check-learn", NULL, RC_STR, 0, F(check_l), NULL, NULL, "DIR", "the learner's gate on a fixture"},
    {"--check-actor", NULL, RC_STR, 0, F(check_a), NULL, NULL, "DIR", "the actor's gate on a fixture"},
    /* the learner */
    {"--learn", NULL, RC_INT, 0, F(learn), "-1", NULL, "0|1", "learn (the actor's default)"},
    {"--prec", NULL, RC_FN, 0, F(prec), NULL, fn_prec, "bf16|fp32", "the learner's GEMMs"},
    {"--fresh-opt", NULL, RC_SET, 0, F(fresh_opt), NULL, NULL, NULL, "Adam afresh (not --init's state)"},
    {"--lr", "ppo.lr", RC_DBL, RC_NAN, F(lr), NULL, NULL, "X", "Adam's rate"},
    {"--ent", "ppo.ent", RC_FLT, RC_NAN, F(ent), "0.003", NULL, "X", "the entropy's weight"},
    {"--clip", "ppo.clip", RC_FLT, RC_NAN, F(hy.clip), NULL, NULL, "X", "PPO's clip"},
    {"--gamma", "ppo.gamma", RC_DBL, RC_NAN, F(gamma), "0.998", NULL, "X", "the discount"},
    {"--lam", "ppo.lam", RC_DBL, RC_NAN, F(hy.lam), NULL, NULL, "X", "GAE's lambda"},
    {"--vf", "ppo.vf", RC_FLT, RC_NAN, F(hy.vf), NULL, NULL, "X", "the value loss's weight"},
    {"--epochs", "ppo.epochs", RC_INT, RC_POS, F(epochs), "3", NULL, "E", "epochs an update"},
    {"--mb", "ppo.mb", RC_INT, RC_POS, F(mb), "8192", NULL, "M", "a minibatch's samples"},
    {"--ref-kl", "ppo.ref_kl", RC_DBL, 0, F(ref_kl), NULL, NULL, "X", "the KL to the start's weights"},
    {"--ref-kl-min", "ppo.ref_kl_min", RC_DBL, 0, F(ref_kl_min), NULL, NULL, "X", "the linear ref_kl schedule's floor"},
    {"--ref-kl-schedule", "ppo.ref_kl_schedule", RC_STR, 0, F(ref_sched), "const", NULL, "const|linear", NULL},
    {"--value-warmup", "ppo.value_warmup", RC_INT, 0, F(vwarm), NULL, NULL, "N", "updates of the value head alone"},
    {"--value-warmup-lr", "ppo.value_warmup_lr", RC_DBL, 0, F(vwarm_lr), NULL, NULL, "X", "their rate (0 lr)"},
    {"--anneal", "ppo.anneal", RC_STR, 0, F(anneal_s), "off", NULL, "off|on", "the rate to 0 over the run"},
    {"--trunc-boot", "ppo.trunc_boot", RC_STR, 0, F(trunc_boot_s), "on", NULL, "on|off",
     "a timed-out episode's last step bootstraps from its value (off: from zero, as a death)"},
    {NULL, "ppo.stage_scale", RC_ONOFF, 0, F(stage_scale), NULL, NULL, NULL, "rewards over their stage's scale"},
    {"--stage-scale", NULL, RC_INT, 0, F(stage_scale), NULL, NULL, "0|1", NULL},
    {"--critic", NULL, RC_INT, 0, F(critic_h), NULL, NULL, "H", "the privileged critic's width (0 off)"},
    {"--critic-init", NULL, RC_STR, 0, F(critic_init), NULL, NULL, "C.neth_critic", "its weights"},
    {"--kl-stop", NULL, RC_DBL, 0, F(kl_stop), NULL, NULL, "X", "an update's epochs stopped past this KL"},
    {"--vclip", NULL, RC_DBL, 0, F(vclip), NULL, NULL, "X", "the value's clip"},
    /* the reward */
    {"--log-cap", "reward.log_cap", RC_INT, 0, F(log_cap), NULL, NULL, "N", NULL},
    {"--ms-weights", "reward.ms_weights", RC_STR, 0, F(ms_weights), NULL, NULL, "NAME=W,...", NULL},
    {"--values", "reward.values", RC_STR, 0, F(values_arg), NULL, NULL, "ID=V,...", NULL},
    {"--caps", "reward.caps", RC_STR, 0, F(caps_arg), NULL, NULL, "ID=N,...", NULL},
    {"--nether", "reward.nether", RC_DBL, 0, F(nether_w), NULL, NULL, "W", NULL},
    {"--end-weight", "reward.end", RC_DBL, 0, F(end_w), NULL, NULL, "W", NULL},
    {"--dragon-weight", "reward.dragon", RC_DBL, 0, F(dragon_w), NULL, NULL, "W", NULL},
    {"--dragon-hp-weight", "reward.dragon_hp", RC_DBL, 0, F(dragon_hp_w), NULL, NULL, "W", "reward per HP of new dragon_min low (0 off)"},
    {"--crystal-weight", "reward.crystals", RC_DBL, 0, F(crystal_w), NULL, NULL, "W", "reward per newly destroyed End crystal (0 off)"},
    {"--step-pen", "reward.step_pen", RC_DBL, 0, F(step_pen), NULL, NULL, "W", "per-step reward penalty (0 off)"},
    {"--stall-pen", "reward.stall_pen", RC_DBL, 0, F(stall_pen), NULL, NULL, "W", "per-step stall penalty (0 off)"},
    {"--stall-ticks", "reward.stall_ticks", RC_INT, 0, F(stall_ticks), NULL, NULL, "N", "ticks before stall penalty (0: 400)"},
    {"--stall-radius", "reward.stall_radius", RC_DBL, 0, F(stall_rad), NULL, NULL, "R", "stall radius in blocks (0: 2)"},
    {NULL, "reward.relative", RC_ONOFF, 0, F(reward_rel), NULL, NULL, NULL, "the reward relative"},
    {"--reward-rel", NULL, RC_INT, 0, F(reward_rel), NULL, NULL, "0|1", NULL},
    /* the frozen eval */
    {"--score", NULL, RC_SET, 0, F(score), NULL, NULL, NULL, "the frozen eval: one episode an env"},
    {"--clock", NULL, RC_FN, 0, F(clock_wrap), NULL, fn_clock, "raw|wrap", "the time scalar (--score's wrap)"},
    {"--score-json", NULL, RC_STR, 0, F(score_json), NULL, NULL, "PATH", NULL},
    {"--score-books", NULL, RC_STR, 0, F(score_books), NULL, NULL, "FILE", "each run's books"},
    /* lane/audit-fixes: the actor's draw, run-time (were the .tN and .gk/.gc/.gkc builds) */
    {"--temp", NULL, RC_STR, 0, F(temp_arg), NULL, NULL, "T", "the keys and camera drawn at temperature T (a frozen policy's)"},
    {"--greedy", NULL, RC_STR, 0, F(greedy_arg), NULL, NULL, "keys|cam|both", "drawn greedily (a frozen policy's)"},
    {"--pearl-census", NULL, RC_SET, 0, F(pearl_census), NULL, NULL, NULL, "the books' privileged census"},
    /* distillation */
    {"--teacher", "distill.teacher", RC_STR, 0, F(teacher), NULL, NULL, "T.ntw", "the teacher drives"},
    {"--level", "distill.level", RC_STR, 0, F(level_s), NULL, NULL, "BITS|NAMES", "the student's macros"},
    {"--beta", "distill.beta", RC_DBL, 0, F(beta), NULL, NULL, "B", "the student's share (DAgger)"},
    {"--soft", NULL, RC_INT, 0, F(soft), NULL, NULL, "0|1", "the teacher's distribution the target"},
    {"--hot-weight", "distill.hot_weight", RC_DBL, 0, F(hotw), "1", NULL, "W", NULL},
    {"--hold-weight", "distill.hold_weight", RC_DBL, 0, F(holdw), "1", NULL, "W", NULL},
    {"--hold-scope", "distill.hold_scope", RC_STR, 0, F(hscope), "all", NULL, "all|lock|dig|digs", NULL},
    {"--gui-weight", "distill.gui_weight", RC_DBL, 0, F(guiw), "1", NULL, "W", NULL},
    {"--x2-weight", "distill.x2_weight", RC_DBL, 0, F(x2w), "1", NULL, "W", NULL},
    {"--recipe-hold", "distill.recipe_hold", RC_STR, 0, F(rhold), "off", NULL, "on|close|off", NULL},
    {"--window-label", "distill.window_label", RC_STR, 0, F(wlab), "off", NULL, "on|level|off", NULL},
    {"--table-label", "distill.table_label", RC_STR, 0, F(tlab), "off", NULL, "on|off", NULL},
    {"--seek-label", "distill.seek_label", RC_STR, 0, F(seeklab), "off", NULL, "on|off", NULL},
    {"--walk-label", "distill.walk_label", RC_STR, 0, F(walklab), "off", NULL, "on|off", NULL},
    {"--labeller", "distill.labeller", RC_STR, 0, F(labeller), "off", NULL, "seek|surface|craft", NULL},
    {"--student-labels", "distill.student_labels", RC_STR, 0, F(slabels), "teacher", NULL, "teacher|shadow|none",
     NULL},
    {"--level-share", "distill.level_share", RC_DBL, 0, F(lshare), "1", NULL, "F", NULL},
    {"--labeller-align", "distill.labeller_align", RC_DBL, 0, F(lalign), NULL, NULL, "DEG", NULL},
    {"--labeller-reach", "distill.labeller_reach", RC_DBL, 0, F(lreach), NULL, NULL, "B", NULL},
    {"--student-zero", NULL, RC_STR, 0, F(szero_arg), NULL, NULL, "LO:N|ore", "the student's scalars zeroed"},
    {"--student-prev", NULL, RC_FN, 0, F(sprev_env), NULL, fn_sprev, "env|label", NULL},
    /* the checkpoint eval */
    {"--eval-every", "distill.eval_every", RC_FN, 0, F(e_every), NULL, fn_eval_every, "N", "env-steps (0 off)"},
    {"--eval-runs", "distill.eval_runs", RC_INT, 0, F(e_runs), "64", NULL, "N", NULL},
    {"--eval-minutes", "distill.eval_minutes", RC_DBL, 0, F(e_min), "10", NULL, "M", NULL},
    {"--eval-seed", "distill.eval_seed", RC_U64, 0, F(e_seed), "1000", NULL, "K", NULL},
    {"--eval-threads", "distill.eval_threads", RC_INT, 0, F(e_thr), "8", NULL, "T", NULL},
    {"--eval-extra", NULL, RC_STR, 0, F(e_extra), NULL, NULL, "\"ARGS\"", "the child's extra arguments"},
    /* the recording */
    {"--rec", NULL, RC_FN, 0, F(rec_on), NULL, fn_rec, "off|on|DIR", "the episodes recorded"},
    {NULL, "record.episodes", RC_ONOFF, 0, F(rec_on), NULL, NULL, NULL, NULL},
    {"--rec-keep", "record.keep", RC_STR, 0, F(rec_keep), "gain", NULL, "gain|all|none", NULL},
    {"--rec-sample", "record.sample", RC_DBL, 0, F(rec_sample), "0.02", NULL, "X", "the share of the rest kept"},
    {"--rec-level", "record.level", RC_INT, 0, F(rec_level), "3", NULL, "N", "zstd's"},
    {"--rec-ms", "record.milestones", RC_STR, 0, F(rec_ms), NULL, NULL, "NAME+...", NULL},
    {"--rec-ring", NULL, RC_INT, 0, F(rec_ring), NULL, NULL, "MB", NULL},
    {"--rec-raw", NULL, RC_STR, 0, F(rec_raw), NULL, NULL, "PATH", NULL},
    {"--rec-rows", NULL, RC_STR, 0, F(rows_arg), NULL, NULL, "E:PATH", "env E's rows"},
    {"--tape", NULL, RC_STR, 0, F(tape_arg), NULL, NULL, "E:PATH", "env E's next episode's script"},
    /* the observation's diagnostics */
    {"--obs-dump", NULL, RC_STR, 0, F(obsdump_arg), NULL, NULL, "E:DIR[:MAX]", NULL},
    {"--zero-sc", NULL, RC_STR, 0, F(zsc_arg), NULL, NULL, "LO:N|ore", "scalars zeroed before the policy"},
    {"--view-books", NULL, RC_INT, 0, F(vbk), "-1", NULL, "K", NULL},
    {"--obs-tone", NULL, RC_STR, 0, F(tone_arg), NULL, NULL, "auto|gamma:G", NULL},
    /* the starts */
    {"--save-starts", NULL, RC_FN, 0, F(save), NULL, fn_save, "SPEC", "the milestone archive"},
    {NULL, "starts.archive", RC_BUF, 0, F(archive), NULL, NULL, NULL, "ms, cells or none"},
    {NULL, "starts.archive_items", RC_BUF, 0, F(items), NULL, NULL, NULL, NULL},
    {NULL, "starts.archive_pool", RC_BUF, 0, F(pool), NULL, NULL, NULL, NULL},
    {NULL, "starts.archive_share", RC_BUF, 0, F(share), NULL, NULL, NULL, NULL},
    {"--seeds", "starts.seeds", RC_STR, 0, F(seeds_arg), NULL, NULL, "A-B,C", NULL},
    {"--seed-set", "starts.seed_set", RC_STR, 0, F(seedset_arg), NULL, NULL, "DIR", NULL},
    {"--library", "starts.library", RC_STR, 0, F(lib_arg), NULL, NULL, "LIB", NULL},
    {"--stages", "starts.stages", RC_STR, 0, F(stages_arg), NULL, NULL, "S+S", NULL},
    {"--lib-seeds", "starts.library_seeds", RC_STR, 0, F(libseeds_arg), NULL, NULL, "A-B", NULL},
    {"--curriculum", "starts.curriculum", RC_STR, 0, F(cur_arg), NULL, NULL, "adaptive|uniform|off", NULL},
    {NULL, "starts.floor", RC_DBL, 0, F(cur_floor), "-1", NULL, NULL, NULL},
    {NULL, "starts.spawn_min", RC_DBL, 0, F(cur_spawn), "-1", NULL, NULL, NULL},
    {NULL, "starts.min_eps", RC_INT, 0, F(cur_mineps), "-1", NULL, NULL, NULL},
    {"--stage-seconds", "starts.episode_seconds", RC_STR, 0, F(stagelen_arg), NULL, NULL, "STAGE/S,...", NULL},
    {"--stage-share", "starts.share", RC_STR, 0, F(share_arg), "resets", NULL, "resets|steps", NULL},
    {"--obs-clock", "starts.clock", RC_STR, 0, F(clock_arg), "played", NULL, "played|left", NULL},
    {"--start-pin", "starts.pin", RC_ONOFF, 0, F(start_pin), NULL, NULL, "on|off", NULL},
    {"--start-cycle", NULL, RC_ONOFF, 0, F(start_cycle), NULL, NULL, "on|off", NULL},
    /* the task */
    {NULL, "task.item", RC_BUF, 0, F(t_item), NULL, NULL, NULL, NULL},
    {NULL, "task.block", RC_BUF, 0, F(t_block), NULL, NULL, NULL, NULL},
    {NULL, "task.keys", RC_BUF, 0, F(t_keys), NULL, NULL, NULL, NULL},
    {NULL, "task.win", RC_BUF, 0, F(t_win), NULL, NULL, NULL, NULL},
    {NULL, "task.death", RC_BUF, 0, F(t_death), NULL, NULL, NULL, NULL},
    {NULL, "task.look", RC_BUF, 0, F(t_look), NULL, NULL, NULL, NULL},
    {NULL, "task.crack", RC_BUF, 0, F(t_crack), NULL, NULL, NULL, NULL},
    {NULL, "task.dist", RC_BUF, 0, F(t_dist), NULL, NULL, NULL, NULL},
    {NULL, "task.shaping", RC_BUF, 0, F(t_shape), NULL, NULL, NULL, NULL},
    {NULL, "task.sticky", RC_BUF, 0, F(t_sticky), NULL, NULL, NULL, NULL},
    {NULL, "task.hint", RC_BUF, 0, F(t_hint), NULL, NULL, NULL, NULL},
    {NULL, "task.count", RC_BUF, 0, F(t_count), NULL, NULL, NULL, NULL},
    {"--sticky", NULL, RC_FN, 0, F(task_sticky), NULL, fn_sticky, "on|block|off", NULL},
    {"--task-crack", NULL, RC_DBL, 0, F(task_crack_arg), NULL, NULL, "X", NULL},
    {"--task-hint", NULL, RC_INT, 0, F(task_hint_arg), "-1", NULL, "R", NULL},
    {"--task-count", NULL, RC_INT, 0, F(task_count_arg), "-1", NULL, "N", NULL},
    {"--task-hint-shape", NULL, RC_FN, 0, F(hint_noshape), NULL, fn_hint_shape, "on|off", NULL},
    {"--task-item2", NULL, RC_INT, 0, F(task_item2), NULL, NULL, "N", NULL},
    {"--task-from2", NULL, RC_INT, 0, F(task_from2), NULL, NULL, "K", NULL},
    /* the ladder */
    {NULL, "ladder.rungs", RC_BUF, 0, F(lad_rungs), NULL, NULL, NULL, NULL},
    {NULL, "ladder.schedule", RC_BUF, 0, F(lad_sched), NULL, NULL, NULL, NULL},
    {NULL, "ladder.at", RC_BUF, 0, F(lad_at), NULL, NULL, NULL, NULL},
    {NULL, "ladder.fade", RC_BUF, 0, F(lad_fade), NULL, NULL, NULL, NULL},
    {NULL, "ladder.unit", RC_BUF, 0, F(lad_unit), NULL, NULL, NULL, NULL},
    {NULL, "ladder.milestone", RC_BUF, 0, F(lad_ms), NULL, NULL, NULL, NULL},
    {NULL, "ladder.window", RC_BUF, 0, F(lad_window), NULL, NULL, NULL, NULL},
    {NULL, "ladder.shares", RC_BUF, 0, F(lad_shares), NULL, NULL, NULL, NULL},
};
#define NROWS (sizeof ROWS / sizeof *ROWS)

/* fastrun.py's policy keys this loop does not do: refused unless the value is fastppo's default */
static const struct { const char *key, *def; } NOT_YET[] = {
    {"actions.picks", "off"}, {"ppo.kl_max", "0"}, {"ppo.ent_floor", "0"},
    {"ppo.ent_max", "0.02"}, {"net.width", "1"}, {"net.res", "0"}, {"net.lstm", "0"}, {"net.kind", "small"},
    {"reward.objective", "full"}, {"reward.explore_weight", "0"}, {"reward.explore_cap", "200"},
    {"run.compile", "max"}, {"run.card", "0"}};

/* row r's value v into c (from_run: the run file's, path and its key for the messages): 0, -1 with err */
static int rc_set(struct neth_runconf *c, const struct rc_row *r, const char *v, int from_run, const char *path,
                  char *err, size_t en)
{
    char *p = (char *)c + r->off;
    double x;
    switch (r->type)
    {
    case RC_INT:
    {
        int i = atoi(v);
        if (!(from_run && (r->fl & RC_POS) && i <= 0)) *(int *)p = i;
        return 0;
    }
    case RC_DBL:
        x = atof(v);
        if (!(from_run && (r->fl & RC_NAN) && isnan(x))) *(double *)p = x;
        return 0;
    case RC_FLT:
        x = atof(v);
        if (!(from_run && (r->fl & RC_NAN) && isnan(x))) *(float *)p = (float)x;
        return 0;
    case RC_U64:
        if (from_run && (r->fl & RC_ATOF))
        {
            x = atof(v);
            if (!((r->fl & RC_NAN) && isnan(x))) *(uint64_t *)p = (uint64_t)x;
        }
        else
            *(uint64_t *)p = strtoull(v, NULL, 10);
        return 0;
    case RC_STR:
        *(const char **)p = from_run ? strdup(v) : v;
        return 0;
    case RC_SET:
        *(int *)p = 1;
        return 0;
    case RC_BUF:
        snprintf(p, r->size, "%s", v);
        return 0;
    case RC_ONOFF:
        if (from_run && strcmp(v, "on") && strcmp(v, "off"))
            return snprintf(err, en, "%s: policy.%s is on or off, not %s", path, r->key, v), -1;
        *(int *)p = !strcmp(v, "on");
        return 0;
    default:
        return r->fn(c, from_run ? strdup(v) : v, from_run, err, en);
    }
}

static void rc_defaults(struct neth_runconf *c)
{
    memset(c, 0, sizeof *c);
    /* the learner's recipe: learn.h's (the brute recipe's), lr its float widened as fastppo's */
    c->hy = neth_learn_hyper_default();
    c->lr = c->hy.lr;
    c->prec = NETH_LEARN_BF16;
    c->clock_wrap = -1;     /* --score's wrap, else raw */
    c->task_crack_arg = NAN;
    c->itemp = 1.0f;        /* lane/audit-fixes: the policy's own draw until --temp is read */
    char e[64];
    for (size_t i = 0; i < NROWS; ++i)
        if (ROWS[i].def) rc_set(c, &ROWS[i], ROWS[i].def, 0, NULL, e, sizeof e);
}

static void trim(char *s)
{
    char *h = strchr(s, '#');
    /* a comment starts at '#' after a space or at the start (plain scalars hold no '#') */
    if (h) *h = 0;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == '\t')) s[--n] = 0;
}

/* a policy key the schema has no row for: an action's option, or refused (task, ladder and starts keys name theirs) */
static int rc_other(struct neth_runconf *c, const char *key, const char *v, const char *path, int ln, char *err,
                    size_t en)
{
    if (!strncmp(key, "actions.", 8) && strcmp(key, "actions.picks"))
    {
        /* a macro option, refused when its name or word is unknown */
        char e2[512];
        size_t l = strlen(c->mopts);
        if (neth_vec_mopt_check(key + 8, v, e2, sizeof e2) < 0)
            return snprintf(err, en, "%s:%d: policy.%s: %s", path, ln, key, e2), -1;
        if (l + strlen(key) + strlen(v) + 2 >= sizeof c->mopts)
            return snprintf(err, en, "%s:%d: policy.%s: too many macro options", path, ln, key), -1;
        char *q = c->mopts + l;
        if (l) *q++ = ',';
        size_t lk = strlen(key + 8), lv = strlen(v);
        memcpy(q, key + 8, lk), q[lk] = '=', memcpy(q + lk + 1, v, lv + 1);
        return 0;
    }
    if (!strncmp(key, "task.", 5))
        return snprintf(err, en, "%s:%d: policy.%s: the task's keys are item, block, keys, win, death, look, crack, "
                        "dist, shaping, sticky, hint and count", path, ln, key), -1;
    if (!strncmp(key, "ladder.", 7))
        return snprintf(err, en, "%s:%d: policy.%s: the ladder's keys are rungs, schedule, at, fade, unit, milestone, "
                        "window and shares", path, ln, key), -1;
    if (!strncmp(key, "starts.", 7))
        return snprintf(err, en, "%s:%d: policy.%s: not a starts key (archive, archive_items, archive_pool, "
                        "archive_share, seeds, seed_set, library, stages, library_seeds, curriculum, floor, spawn_min, "
                        "min_eps, episode_seconds, share, clock, pin; a seed window is not done natively yet)", path,
                        ln, key), -1;
    for (size_t i = 0; i < sizeof NOT_YET / sizeof *NOT_YET; ++i)
        if (!strcmp(key, NOT_YET[i].key))
        {
            char *e1, *e2;
            double x = strtod(v, &e1), y = strtod(NOT_YET[i].def, &e2);
            if (strcmp(v, NOT_YET[i].def) && !(!*e1 && !*e2 && x == y))
                return snprintf(err, en, "%s:%d: policy.%s %s: not done natively yet (only %s)", path, ln, key, v,
                                NOT_YET[i].def), -1;
            return 0;
        }
    return snprintf(err, en, "%s:%d: policy.%s is not a fastppo key", path, ln, key), -1;
}

/* the run file: its sim part's pipeline keys and its policy part (and a top-level actions part), each key through
 * its row */
static int rc_read_run(struct neth_runconf *c, const char *path, char *err, size_t en)
{
    FILE *f = fopen(path, "r");
    if (!f) return snprintf(err, en, "--run %s: %s", path, strerror(errno)), -1;
    /* lane/audit-fixes: getline, so a line of any length (a long starts.seeds) is one line, not cut at 1023 bytes into
     * two */
    char *line = NULL, part[64] = "", sec[64] = "";
    size_t cap = 0;
    int ln = 0, bad = 0;
    while (!bad && getline(&line, &cap, f) != -1)
    {
        ++ln;
        trim(line);
        if (!line[0]) continue;
        int ind = 0;
        while (line[ind] == ' ') ++ind;
        char *k = line + ind, *col = strchr(k, ':');
        if (!col) { snprintf(err, en, "%s:%d: not KEY: VALUE", path, ln); bad = 1; break; }
        *col = 0;
        char *v = col + 1;
        while (*v == ' ') ++v;
        if (ind == 0) { snprintf(part, sizeof part, "%s", k); continue; }
        if (ind == 2 && !*v) { snprintf(sec, sizeof sec, "%s", k); continue; }
        if (ind != 4 || !*v) { snprintf(err, en, "%s:%d: outside the run file's grammar", path, ln); bad = 1; break; }
        char key[160];
        snprintf(key, sizeof key, "%s.%s", sec, k);
        int sim = !strcmp(part, "sim");
        if (!strcmp(part, "actions")) snprintf(key, sizeof key, "actions.%s", k);
        else if (!sim && strcmp(part, "policy")) continue;
        const struct rc_row *r = NULL;
        for (size_t i = 0; i < NROWS && !r; ++i)
            if (ROWS[i].key && !strcmp(ROWS[i].key, key) && !(ROWS[i].fl & RC_SIM) == !sim) r = &ROWS[i];
        if (sim && !r) continue;    /* the sim's own keys are worldconf.c's */
        bad = r ? rc_set(c, r, v, 1, path, err, en) : rc_other(c, key, v, path, ln, err, en);
    }
    free(line);
    fclose(f);
    if (bad) return -1;
    /* starts.archive into --save-starts, with fastrun.py's defaults */
    char ss[600] = "";
    if (!strcmp(c->archive, "ms"))
        snprintf(ss, sizeof ss, "ms:%s:%s:%s",
                 c->items[0] ? c->items : "wooden_pickaxe+stone_pickaxe+furnace+iron_ingot+iron_pickaxe",
                 c->pool[0] ? c->pool : "16", c->share[0] ? c->share : "0.4");
    else if (!strcmp(c->archive, "cells"))
        snprintf(ss, sizeof ss, "cells:%s:%s", c->pool[0] ? c->pool : "250", c->share[0] ? c->share : "0.5");
    else if (c->archive[0] && strcmp(c->archive, "none"))
        return snprintf(err, en, "%s: starts.archive is ms, cells or none, not %s", path, c->archive), -1;
    if (ss[0]) c->save = strdup(ss), c->save_from_run = 1;
    return 0;
}

/* the run file's task section (read only with task.item): its keys a '+' list of the action's key names (all: every
 * key); 0, or -1 with err */
static int rc_task(struct neth_runconf *c, const char *run, char *err, size_t en)
{
    if (!c->t_item[0]) return 0;
    c->task_item = atoi(c->t_item);
    if (c->t_shape[0] && strcmp(c->t_shape, "once") && strcmp(c->t_shape, "potential"))
        return snprintf(err, en, "%s: policy.task.shaping is once or potential, not %s", run, c->t_shape), -1;
    c->task_shape = !strcmp(c->t_shape, "potential");
    if (c->t_sticky[0] && strcmp(c->t_sticky, "on") && strcmp(c->t_sticky, "block") && strcmp(c->t_sticky, "off"))
        return snprintf(err, en, "%s: policy.task.sticky is on, block or off, not %s", run, c->t_sticky), -1;
    c->task_sticky = !strcmp(c->t_sticky, "on") ? 1 : !strcmp(c->t_sticky, "block") ? 2 : 0;
    c->task_hint = c->t_hint[0] ? atoi(c->t_hint) : 0;
    c->task_count = c->t_count[0] ? atoi(c->t_count) : 0;
    c->task_block = c->t_block[0] ? atoi(c->t_block) : 0;
    const char *tw[5] = {c->t_win, c->t_death, c->t_look, c->t_crack, c->t_dist};
    for (int q = 0; q < 5; ++q) c->task_w[q] = tw[q][0] ? atof(tw[q]) : q == 0 ? 1.0 : 0.0;
    static const char *const KN[NETH_VEC_KEYS] = {"forward", "back", "left", "right", "sprint", "jump", "sneak",
                                                  "mine", "place", "throw", "drop", "hit", "swap_item", "paused"};
    char kb[128];
    snprintf(kb, sizeof kb, "%s", c->t_keys[0] ? c->t_keys : "forward+back+left+right+jump+mine");
    for (char *sv, *t = strtok_r(kb, "+", &sv); t; t = strtok_r(NULL, "+", &sv))
    {
        int j = 0;
        if (!strcmp(t, "all")) { c->task_keys = (1 << NETH_VEC_KEYS) - 1; continue; }
        while (j < NETH_VEC_KEYS && strcmp(t, KN[j])) ++j;
        if (j == NETH_VEC_KEYS)
            return snprintf(err, en, "%s: policy.task.keys: no key %s (forward back left right sprint jump sneak mine "
                            "place throw drop hit swap_item paused, or all)", run, t), -1;
        c->task_keys |= 1 << j;
    }
    return 0;
}

void neth_runconf_usage(FILE *f)
{
    fputs("usage: ntloop [--run FILE] [FLAG VALUE ...] | --check-learn DIR | --check-actor DIR | --mopts\n"
          "  the run file's policy part, then the flags over it; each flag, its run file key and its default:\n", f);
    for (size_t i = 0; i < NROWS; ++i)
    {
        const struct rc_row *r = &ROWS[i];
        if (!r->flag) continue;
        char a[64];
        snprintf(a, sizeof a, "%s%s%s", r->flag, r->arg ? " " : "", r->arg ? r->arg : "");
        fprintf(f, "  %-34s", a);
        if (r->key) fprintf(f, " %s:", r->key);
        if (r->help) fprintf(f, " %s", r->help);
        if (r->def) fprintf(f, " (%s)", r->def);
        fputc('\n', f);
    }
}

int neth_runconf_parse(struct neth_runconf *c, int argc, char **argv)
{
    rc_defaults(c);
    /* --run first: the file's settings, then the flags over them */
    for (int i = 1; i + 1 < argc; ++i)
        if (!strcmp(argv[i], "--run")) c->run = argv[i + 1];
    char err[1024];
    if (c->run)
    {
        if (rc_read_run(c, c->run, err, sizeof err) || rc_task(c, c->run, err, sizeof err))
        {
            printf("FAIL: %s\n", err);
            return 2;
        }
        c->config = c->run;
        if (neth_ladder_parse(&c->lad, c, err, sizeof err))
        {
            printf("FAIL: %s: %s\n", c->run, err);
            return 2;
        }
        c->pol = "actor";
    }
    /* the run's length given on the command line: --warm or --steps alone means W + S group steps, so the run file's
     * hours and env-steps stop counting unless --hours or --env-steps is given too (2026-11-01: three lanes' short
     * checks ran for the run file's hours) */
    int len_cli = 0, stop_cli = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--warm") || !strcmp(argv[i], "--steps")) len_cli = 1;
        if (!strcmp(argv[i], "--hours") || !strcmp(argv[i], "--env-steps")) stop_cli = 1;
    }
    if (c->run && len_cli && !stop_cli) c->hours = 0, c->max_steps = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--run") && i + 1 < argc)
        {
            ++i;
            continue;
        }
        const struct rc_row *r = NULL;
        for (size_t j = 0; j < NROWS && !r; ++j)
            if (ROWS[j].flag && !strcmp(ROWS[j].flag, argv[i])) r = &ROWS[j];
        int noval = r && (r->type == RC_SET || (r->fl & RC_NOVAL));
        if (!r || (!noval && i + 1 >= argc))
        {
            neth_runconf_usage(stderr);
            return 2;
        }
        if (rc_set(c, r, noval ? "" : argv[++i], 0, NULL, err, sizeof err))
        {
            printf("FAIL: %s\n", err);
            return 2;
        }
    }
    c->hy.lr = (float)c->lr;
    c->hy.gamma = c->gamma;
    c->anneal = !strcmp(c->anneal_s, "on") || !strcmp(c->anneal_s, "1") || !strcmp(c->anneal_s, "true");
    if ((!c->anneal && strcmp(c->anneal_s, "off") && strcmp(c->anneal_s, "0") && strcmp(c->anneal_s, "false")) ||
        (strcmp(c->ref_sched, "const") && strcmp(c->ref_sched, "linear")) || !(c->ref_kl >= 0) || c->vwarm < 0)
    {
        printf("FAIL: --anneal off|on (%s), --ref-kl X >= 0 (%g), --ref-kl-schedule const|linear (%s), "
               "--value-warmup N >= 0 (%d)\n", c->anneal_s, c->ref_kl, c->ref_sched, c->vwarm);
        return 2;
    }
    /* lane/audit-fixes: the clock's end bootstrapped from the last value (on) or from zero as the task's end (off) */
    c->trunc_boot = !strcmp(c->trunc_boot_s, "on");
    if (!c->trunc_boot && strcmp(c->trunc_boot_s, "off"))
    {
        printf("FAIL: --trunc-boot on|off (%s)\n", c->trunc_boot_s);
        return 2;
    }
    if ((c->anneal || (c->ref_kl > 0 && !strcmp(c->ref_sched, "linear"))) && !(c->hours > 0 || c->max_steps > 0))
    {
        printf("FAIL: --anneal on and --ref-kl-schedule linear run over the run's length: --hours or --env-steps\n");
        return 2;
    }
    return 0;
}

/* lane/audit-fixes: --temp T into itemp (actor.h neth_actor_itemp), --greedy into its bits; 0, or -1 with the reason
 * printed */
static int draw_resolve(struct neth_runconf *c)
{
    c->itemp = 1.0f;
    c->greedy = 0;
    if (c->temp_arg)
    {
        char *end = NULL;
        float t = strtof(c->temp_arg, &end);
        if (!c->temp_arg[0] || *end || !(t > 0.0f) || isinf(t))
        {
            printf("FAIL: --temp T, a number over 0 (%s; 1 is the policy's own distribution)\n", c->temp_arg);
            return -1;
        }
        c->itemp = neth_actor_itemp(t);
    }
    if (c->greedy_arg)
    {
        c->greedy = !strcmp(c->greedy_arg, "keys") ? 1 : !strcmp(c->greedy_arg, "cam") ? 2
                    : !strcmp(c->greedy_arg, "both") ? 3 : -1;
        if (c->greedy < 0)
        {
            printf("FAIL: --greedy keys|cam|both (%s)\n", c->greedy_arg);
            return -1;
        }
    }
    return 0;
}

/* lane/audit-feat: a .ntw file's header (its first bytes): its version (1 or 2) and NTW2's features, or -1 (the file's
 * loader reports it) */
static int feat_head(const char *path, int *mask, char *variant, size_t vn)
{
    unsigned char b[4096];
    char e[512];
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(b, 1, sizeof b, f);
    fclose(f);
    return neth_ntw_features(b, n, path, mask, variant, vn, e, sizeof e);
}

/* lane/audit-feat: the run's scalar features into net.h's neth_net_fs, before anything is made: --features or the
 * run file's net.features (the same set when both are given), the --init file holding no feature outside it; else
 * the --init file's NTW2 header; else (an NTW1 file, no --init) the build's default (net.h NETH_NET_FEAT0, actor.mk's
 * words in the build's name). A file with fewer features loads with zero weights on the rest (neth_checkpoint.c's
 * parser, which also holds the teacher to the run's set). 0, or -1 with the reason printed */
static int feat_resolve(struct neth_runconf *c)
{
    char e[512], wa[64], wb[64], wc[64], v[128];
    int fm = -1, rm = -1;
    if (c->feat_arg && neth_net_words(c->feat_arg, &fm, e, sizeof e))
    {
        printf("FAIL: --features %s: %s\n", c->feat_arg, e);
        return -1;
    }
    if (c->feat_run && neth_net_words(c->feat_run, &rm, e, sizeof e))
    {
        printf("FAIL: %s: policy.net.features %s: %s\n", c->run, c->feat_run, e);
        return -1;
    }
    if (fm >= 0 && rm >= 0 && fm != rm)
    {
        neth_net_mask_words(fm, wa, sizeof wa);
        neth_net_mask_words(rm, wb, sizeof wb);
        printf("FAIL: --features %s and the run file's policy.net.features %s (%s) differ: one set a run\n", wa, wb,
               c->run);
        return -1;
    }
    int req = fm >= 0 ? fm : rm, m = req >= 0 ? req : NETH_NET_FEAT0, im = 0;
    if (c->init && feat_head(c->init, &im, v, sizeof v) == 2)
    {
        if (req < 0) m = im;
        else if (im & ~req)
        {
            neth_net_mask_words(im, wa, sizeof wa);
            neth_net_mask_words(req, wb, sizeof wb);
            neth_net_mask_words(im & ~req, wc, sizeof wc);
            printf("FAIL: --init %s has the features %s (variant %s); %s %s lacks %s: the run's set holds every one of "
                   "the file's (without a set the run is the file's own)\n", c->init, wa, v,
                   fm >= 0 ? "--features" : "the run file's policy.net.features", wb, wc);
            return -1;
        }
    }
    if (neth_net_set_features(m))
    {
        printf("FAIL: the run's features are set once, before the nets are made\n");
        return -1;
    }
    return 0;
}

int neth_runconf_resolve(struct neth_runconf *c)
{
    if (feat_resolve(c)) return 2;
    if (c->teacher) c->pol = "actor";     /* the teacher is the actor, --init the student */
    if (c->score && c->teacher)
    {
        printf("FAIL: --score evaluates one model: not with --teacher\n");
        return 2;
    }
    if (c->score)
    {
        /* the frozen eval: the actor at its --init weights, nothing learned, one episode an env from the start */
        if (c->learn > 0 || c->digest || c->ab || c->rec_on || c->lad.nr || (c->save && !c->save_from_run) ||
            !c->init || c->clock_wrap == -2)
        {
            printf("FAIL: --score needs --init W.ntw and takes none of --learn 1, --digest, --ab, --rec, a ladder or "
                   "--save-starts; --clock is raw or wrap\n");
            return 2;
        }
        /* the run file's starts.archive is training's: an eval plays from the start alone */
        c->save = NULL;
        /* an eval's episodes are its --minutes long (the quota is a group's steps) */
        if (c->stagelen_arg)
            printf("lane/stagelen: starts.episode_seconds dropped: --score plays %d ticks an episode\n", c->ep_len);
        c->stagelen_arg = NULL;
        c->share_arg = "resets";
        c->pol = "actor";
        c->learn = 0;
        if (c->clock_wrap < 0) c->clock_wrap = 1;
    }
    if (c->clock_wrap == -2)
    {
        printf("FAIL: --clock raw or wrap\n");
        return 2;
    }
    int actor = !strcmp(c->pol, "actor");
    if (c->learn < 0) c->learn = actor;
    if (c->teacher && !c->learn)
    {
        printf("FAIL: --teacher trains the student: not with --learn 0\n");
        return 2;
    }
    if (draw_resolve(c)) return 2;
    if ((c->itemp != 1.0f || c->greedy) && (c->learn || c->teacher))
    {
        printf("FAIL: --temp (other than 1) and --greedy play a frozen policy only: --learn 0, no --teacher\n");
        return 2;
    }
    if ((c->itemp != 1.0f || c->greedy) && neth_actor_fused())
    {
        printf("FAIL: --temp (other than 1) and --greedy draw in heads_draw, and this build's bf16 actor step is fused "
               "(the .af word): build the same variant without .af (make -C csrc/runtime train-loop-v NT_V=V)\n");
        return 2;
    }
    if (c->groups < 1 || c->n % c->groups || c->depth < -1 || c->depth >= c->groups || c->T < 1 || c->ab < 0 ||
        (c->ab && c->digest) || c->cast_max < 0 || (c->serve > 0 && c->depth >= 0) || (c->learn && !actor) ||
        (actor && !c->init) || (c->learn && (c->epochs < 1 || c->mb < 1)) ||
        (c->ab && strcmp(c->ab_a, "vec") && strcmp(c->ab_a, "loop")))
    {
        printf("FAIL: --ns %d in --groups %d, --depth %d (0 to groups - 1, not with --serve 1), --cast-max at least 0, "
               "--T %d, --ab %d (not with --digest), "
               "--ab-a %s (vec or loop), --learn %d (with --policy actor alone), --policy actor needs --init W.ntw\n",
               c->n, c->groups, c->depth, c->T, c->ab, c->ab_a, c->learn);
        return 2;
    }
    return 0;
}

int neth_runconf_vec_opts(const struct neth_runconf *c, struct neth_vec_opts *o)
{
    memset(o, 0, sizeof *o);
    o->env.config = c->config;
    {
        /* sim.performance.region_spill: auto is off here (the spill file wrote 135 to 348 GB an hour on gpu-workstation's
         * NVMe, lane/xhigh-audit, and changes no row); on for a run whose explorers would fill the chunk slots */
        struct wconf wc;
        char e[512];
        const char *rs = wconf_load(&wc, c->config, e, sizeof e) == 0 ? wconf_get(&wc, WC_REGION_SPILL) : "auto";
        o->env.region_spill = !strcmp(rs, "on") ? "on" : "off";
    }
    o->env.start = c->start_dir;
    o->env.n = c->n;
    o->env.threads = c->threads;
    o->env.device_mesh = -1;
    o->dbench = c->score_books != NULL && c->score;
    o->groups = c->groups;
    o->ep_len = c->ep_len;
    o->macros = c->macros | (NETH_NET_HOTBAR ? NETH_VEC_HOTBAR : 0);  /* the action's hotbar element */
    o->macro_opts = c->mopts;
    o->objective = NETH_VEC_FULL;
    /* the scalar features after the 155 (ls hs dk or gv tk): lane/audit-feat, the run's set (net.h's mask is
     * late_scalars bit for bit), the net's and the env's one value */
    o->late_scalars = NETH_NET_FEATMASK;
    o->reward_rel = c->reward_rel != 0;
    o->task_item = c->task_item, o->task_block = c->task_block, o->task_keys = c->task_keys;
    o->task_win = c->task_w[0], o->task_death = c->task_w[1], o->task_look = c->task_w[2], o->task_crack = c->task_w[3];
    o->task_dist = c->task_w[4];
    o->task_shape = c->task_shape;
    o->task_sticky = c->task_sticky;
    if (!isnan(c->task_crack_arg)) o->task_crack = c->task_crack_arg;
    o->task_hint = c->task_hint_arg >= 0 ? c->task_hint_arg : c->task_hint;
    if (c->task_item && o->task_hint)
        printf("lane/rawsearch: task hint %d: the task block's seen-object slot %s when none is in view\n", o->task_hint,
               o->task_hint < 0 ? "from the cell last drawn (memory)" : "and the potential's cell from the scan's nearest");
    o->task_hint_noshape = c->hint_noshape;
    o->task_item2 = c->task_item2, o->task_from2 = c->task_from2;
    if (c->task_item && c->task_item2)
        printf("lane/acrl: a second task, item %d, for the envs whose start is %d or later\n", c->task_item2,
               c->task_from2);
    if (c->task_item && o->task_hint > 0 && c->hint_noshape)
        printf("lane/acrl: --task-hint-shape off: the hint fills the seen-object slot only, the shaping's cell the last drawn\n");
    o->task_count = c->task_count_arg >= 0 ? c->task_count_arg : c->task_count;
    if (c->task_item && o->task_count > 1)
        printf("lane/rawsearch: task count %d: the episode ends at %d more of the item, each paid win / %d\n",
               o->task_count, o->task_count, o->task_count);
    o->start_pin = c->start_pin;
    o->start_cycle = c->start_cycle;
    if (c->start_pin) printf("lane/rawskill: starts.pin on: each env keeps the start the make drew for it\n");
    if (c->task_item)
        printf("lane/rawskill: task item %d, block %d, keys 0x%x, win %g, death %g, look %g, crack %g, dist %g, "
               "shaping %s, sticky %s\n", c->task_item, c->task_block ? c->task_block : c->task_item, c->task_keys,
               c->task_w[0], c->task_w[1], c->task_w[2], o->task_crack, c->task_w[4],
               c->task_shape ? "potential" : "once",
               c->task_sticky == 1 ? "on" : c->task_sticky == 2 ? "block" : "off");
    o->flags |= c->dev ? NETH_RL_DEV : 0;
    if (c->train_skip_ambient) o->flags |= NETH_RL_SKIP_AMBIENT;
    if (c->distant_tick_radius > 0)
        o->flags |= NETH_RL_DISTANT_R(c->distant_tick_radius > 255 ? 255 : c->distant_tick_radius);
    /* fastppo's --log-cap and --ms-weights (G3's recipe: 8, stone_pickaxe=4,furnace=4,iron_ingot=8,
     * iron_pickaxe=16), neth_vec_opts' log_cap and ms_weights; every other milestone at 1 */
    static double msw[NETH_VEC_MILESTONES];
    o->log_cap = c->log_cap;
    if (c->ms_weights)
    {
        for (int m = 0; m < NETH_VEC_MILESTONES; ++m) msw[m] = 1;
        char buf[512];
        snprintf(buf, sizeof buf, "%s", c->ms_weights);
        for (char *sv, *t = strtok_r(buf, ",", &sv); t; t = strtok_r(NULL, ",", &sv))
        {
            char *eq = strpbrk(t, "=/");
            int k = -1;
            if (eq) *eq = 0;
            for (int m = 0; m < NETH_VEC_MILESTONES; ++m)
                if (!strcmp(t, neth_ms_names[m])) k = m;
            if (!eq || k < 0)
            {
                printf("FAIL: --ms-weights %s: NAME=W,..., NAME a milestone\n", c->ms_weights);
                return 2;
            }
            msw[k] = atof(eq + 1);
        }
        o->ms_weights = msw;
    }
    /* fastppo's --values ID=V,... (over C's reward.values(), as mcsr's VecEnv(values=) copies them),
     * --caps ID=N,... (item_caps) and --nether W (nether_weight); the run file's reward.values, caps and nether, the
     * pairs ID/V there */
    static float vtab[4096];
    static double camtab[NETH_VEC_CAM];
    static int32_t captab[4096];
    for (int w = 0; w < 2; ++w)
    {
        const char *spec = w ? c->caps_arg : c->values_arg;
        if (!spec) continue;
        if (!w) neth_vec_tables(vtab, camtab);
        char buf[512];
        snprintf(buf, sizeof buf, "%s", spec);
        for (char *sv, *t = strtok_r(buf, ",", &sv); t; t = strtok_r(NULL, ",", &sv))
        {
            char *eq = strpbrk(t, "=/"), *e = NULL;
            long id = eq ? strtol(t, &e, 10) : -1;
            if (!eq || e != eq || id < 0 || id >= 4096)
            {
                printf("FAIL: --%s %s: ID=%s,..., ID an item id under 4096\n", w ? "caps" : "values", spec,
                       w ? "N" : "V");
                return 2;
            }
            if (w) captab[id] = atoi(eq + 1);
            else vtab[id] = (float)atof(eq + 1);
        }
        if (w) o->item_caps = captab;
        else o->values = vtab, o->camdeg = camtab;
    }
    o->nether_weight = c->nether_w;
    o->end_weight = c->end_w;
    o->dragon_weight = c->dragon_w;
    o->dragon_hp_weight = c->dragon_hp_w;
    o->crystal_weight = c->crystal_w;
    o->step_pen = c->step_pen;
    o->stall_pen = c->stall_pen;
    o->stall_ticks = c->stall_ticks;
    o->stall_radius = c->stall_rad;
    if (c->step_pen != 0 || c->stall_pen != 0)
        printf("lane/speedrun-recipe: step_pen %g, stall_pen %g a step past %d ticks within %g blocks\n", c->step_pen,
               c->stall_pen, c->stall_ticks > 0 ? c->stall_ticks : 400, c->stall_rad > 0 ? c->stall_rad : 2.0);
    char err[1024];
    if (save_starts(c->save, o, err, sizeof err))
    {
        printf("FAIL: %s\n", err);
        return 2;
    }
    return 0;
}
