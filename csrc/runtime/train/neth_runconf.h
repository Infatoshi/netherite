/* ntloop's settings: one struct, filled from one schema (neth_runconf.c's table: each setting's flag, its run file key,
 * its type, its default and its help), defaults first, then the run file's policy part (--run), then the flags.
 *
 * Adding a setting is a field here and a row in the table. A setting whose meaning needs more than a conversion (an
 * on/off word, a list, a pair of settings) has a callback in the row; the run file's archive, task and ladder keys are
 * kept as their text and read once the file is read (neth_runconf_parse), as the vector env's options are made from
 * the whole struct (neth_runconf_vec_opts). */
#ifndef NETHERITE_RUNTIME_TRAIN_NETH_RUNCONF_H
#define NETHERITE_RUNTIME_TRAIN_NETH_RUNCONF_H

#include <stddef.h>
#include <stdint.h>

#include "neth_pipe_internal.h"

struct neth_runconf {
    /* the run: the config (the run file is one), the policy, its weights, where the start is */
    const char *config, *run, *pol, *out, *init, *start_dir;
    int n, groups, threads, warm, steps, ep_len, macros, T, dev;
    int train_skip_ambient, distant_tick_radius;
    uint64_t seed;
    double hours, max_steps, log_every, save_every;
    char mopts[2048];           /* the run file's actions.NAME options, then --mopt's ("name=word,...") */
    /* the pipe: depth, serving, the cast, the copies' kernel, --dump-cast */
    int depth, serve, cast_max, prio, cast_hold, stage, dump_at;
    int poll_flag, graph;       /* lane/cuda-graph: --poll-flag 1, --graph 1 */
    double idle_us;
    const char *dump_path;
    /* the checks and the digest, --ab */
    int digest, fdigest, ab, ab_swap;
    const char *ab_a, *check_l, *check_a;
    /* the learner: PPO's recipe (hy: clip, lam and vf; lr and gamma apart, set into it), its extras */
    neth_learn_hyper hy;
    double lr, gamma, ref_kl, ref_kl_min, vwarm_lr, kl_stop, vclip;
    float ent;
    int epochs, mb, learn, prec, fresh_opt, vwarm, critic_h;
    const char *ref_sched, *anneal_s, *critic_init;
    int anneal;                 /* anneal_s read (neth_runconf_parse) */
    /* the reward */
    int log_cap, reward_rel, stage_scale, stall_ticks;
    double nether_w, end_w, dragon_w, dragon_hp_w, crystal_w, step_pen, stall_pen, stall_rad;
    const char *ms_weights, *values_arg, *caps_arg;
    /* the frozen eval */
    int score, clock_wrap, pearl_census;
    const char *score_json, *score_books;
    /* distillation (--teacher) */
    const char *teacher, *level_s, *rhold, *wlab, *tlab, *seeklab, *walklab, *hscope, *labeller, *slabels, *szero_arg;
    double beta, hotw, holdw, guiw, x2w, lshare, lalign, lreach;
    int soft, sprev_env;
    /* the checkpoint eval (--eval-every) */
    double e_every, e_min;
    int e_runs, e_thr, e_from_run;
    uint64_t e_seed;
    const char *e_extra;
    /* the recording */
    int rec_on, rec_level, rec_ring;
    double rec_sample;
    const char *rec_dir, *rec_keep, *rec_raw, *rec_ms, *rows_arg, *tape_arg;
    /* the observation's diagnostics */
    const char *obsdump_arg, *zsc_arg, *tone_arg;
    int vbk;
    /* the starts: the archive, the seed set, the library, the curriculum */
    const char *save, *seeds_arg, *seedset_arg, *lib_arg, *stages_arg, *libseeds_arg, *cur_arg, *stagelen_arg,
        *share_arg, *clock_arg;
    int save_from_run, start_pin, start_cycle, cur_mineps;
    double cur_floor, cur_spawn;
    /* the task (vecenv.h neth_vec_opts.task_*; item 0 off): the run file's, then the flags over some */
    int task_item, task_block, task_keys, task_shape, task_sticky, task_hint, task_count;
    int task_hint_arg, task_count_arg, task_item2, task_from2, hint_noshape;
    double task_w[5];           /* win, death, look, crack, dist */
    double task_crack_arg;
    /* the run file's keys read as text once the file is read */
    char archive[16], items[512], pool[32], share[32];
    char t_item[16], t_block[16], t_keys[128], t_win[32], t_death[32], t_look[32], t_crack[32], t_dist[32],
        t_shape[16], t_sticky[16], t_hint[16], t_count[16];
    char lad_rungs[512], lad_sched[32], lad_at[256], lad_fade[32], lad_unit[16], lad_ms[64], lad_window[32],
        lad_shares[256];
    struct neth_ladder lad;     /* the run file's ladder (lad.nr 0: none) */
};

/* the defaults, the run file (--run) and the flags into c, then the learner's recipe read: 0, or the exit code with
 * the reason printed (2: the usage) */
int neth_runconf_parse(struct neth_runconf *c, int argc, char **argv);
/* what one setting implies for another (--score, --teacher, the policy) and every combination refused: 0, or 2 */
int neth_runconf_resolve(struct neth_runconf *c);
/* the vector env's options from c (o is c's: its tables live as long as the process): 0, or 2 */
int neth_runconf_vec_opts(const struct neth_runconf *c, struct neth_vec_opts *o);
/* the flags' usage, from the schema */
void neth_runconf_usage(FILE *f);

/* vecenv.macro_set: names left to right from nothing, -NAME out, base and recipe their lists; -1 on a bad name */
int neth_macro_set(const char *spec, char *err, size_t en);
/* "NAME+NAME..." of reward.MILESTONES into a mask, -1 on a bad name */
int64_t neth_ms_mask(const char *spec, char *err, size_t en);
/* a scalar span LO:N, or ore; 0, or -1 when outside the build */
int neth_zsc_parse(const char *a, int *lo, int *n);

/* the run file's ladder keys into D (D->nr 0: none): 0, or -1 with err (neth_schedule.c) */
int neth_ladder_parse(struct neth_ladder *D, const struct neth_runconf *c, char *err, size_t en);

#endif
