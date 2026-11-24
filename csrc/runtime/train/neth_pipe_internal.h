/* ntloop's modules' shared state: the pipe (struct neth_pipe, the trainer's loop over libneth's vector env), its
 * segments, the learner, the ladder and the checkpoint eval's queue, and each module's entry points. Internal to
 * train/neth_*.c and train/loop.c; the contract they keep is loop.c's header. */
#ifndef NETHERITE_RUNTIME_TRAIN_NETH_PIPE_INTERNAL_H
#define NETHERITE_RUNTIME_TRAIN_NETH_PIPE_INTERNAL_H

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <cuda_runtime.h>

#include "../../engine/env.h"
#include "../../engine/fidelity.h"
#include "../../engine/worldconf.h"
#include "../pipeline.h"
#include "../pool.h"
#include "../rlgui.h"
#include "../vecenv.h"
#include "distill.h"
#include "learn.h"
#include "loop.h"
#include "net.h"
#include "rec.h"
#include "neth_util.h"

/* ---- the names the reports print ---- */

extern const char *const neth_ms_names[NETH_VEC_MILESTONES];   /* reward.MILESTONES, the records' ms bits */

/* fromspawn.py's TIMED: the items whose first holding --score times, by their column in the scalars' counts */
#define SC_TIMED 8
extern const int neth_timed_col[SC_TIMED];

/* the main thread's parts of a group step (the report's and iters.jsonl's t_loop) */
enum { T_RECV, T_HOST, T_LAUNCH, T_SYNC, T_SEND, T_TAIL, T_N };
extern const char *const neth_t_names[T_N];

/* ---- a group's segment: T steps of what the learner reads, on the device (neth_segment.c) ---- */

struct neth_seg {
    uint8_t *obs;       /* [T][gs][oe] */
    neth_sc_t *sc;      /* [T][gs][NETH_NET_NS] */
    int32_t *act;       /* [T][gs][NETH_NET_ACTION] */
    float *logp, *val;  /* [T][gs] */
    float *rew;         /* [T][gs] */
    uint8_t *done, *drv;/* [T][gs] */
    uint8_t *term;      /* [T][gs] lane/audit-fixes: the env's terminations beside done (its truncations with
                         * --trunc-boot off), the learner's and the bootstrap's (learn.h) */
    float *boot;        /* [gs]: the next segment's first value, zero where the episode ended (fastppo's boot) */
    float *h0;          /* (net.h NETH_NET_RNN) [gs][NETH_NET_D]: each env's minGRU state at the segment's start,
                         * zero where its first step starts an episode (the learner's scan starts there) */
    neth_sc_t *psc;     /* (--critic) [T][gs][NETH_NET_NS]: the scalars before --zero-sc, the critic's input (may alias sc) */
    cudaEvent_t ready;  /* the group's stream: the segment and its bootstrap made */
    cudaEvent_t freed;  /* the learner's stream: its update has read the segment */
    int job;            /* 0 free or filling, 1 waiting for the learner, 2 its update enqueued */
    float scale;        /* the reward scale when it was finished (fastppo's scale() at jobs.put) */
};

void neth_seg_alloc(struct neth_seg *s, int T, int gs, size_t oe);
void neth_seg_free(struct neth_seg *s);
/* page-lock a buffer of the vector env's (already locked is fine), mapped: the device pointer, NULL none */
void *neth_pin(void *p, size_t n);

/* ---- the learner (--policy actor): fastppo's Learner and its job queue (neth_train_stage.c) ---- */

struct neth_learner {
    neth_learn *ln;
    neth_learn_hyper h;
    float entc;
    double lr;              /* the row's lr (fastppo's Python float) */
    int epochs, mb, N, step;    /* step: Adam's steps so far */
    cudaStream_t s;
    float *w, *grad, *m, *v, *acc, *gap;    /* device: the weights, Adam's moments, the books summed, actor_gap */
    int32_t *cam, *perm;    /* device scratch: the camera bins, each epoch's order */
    float *keys;
    int32_t *hot;           /* NETH_NET_HOTBAR: the hotbar's draws (NULL otherwise) */
    int32_t *hperm;         /* page-locked: the order made on the host */
    const int32_t *perm_fix;    /* --check-learn: the fixture's order instead */
    float *hacc;            /* page-locked: the books and actor_gap read back */
    uint64_t rng;
    struct neth_seg **q, *busy;  /* the segments waiting, oldest first; the one whose update is in flight */
    int nq;
    cudaEvent_t e0, e1;     /* the update's device time */
    /* the row's books (fastppo's upd), and the run's (the report's) */
    int64_t n, jobs, samples, refreshes, jobs_all;
    double t_learn, gpu_learn, gpu_all;
    char snap[4096];        /* --save-every: the next update's weights go here */
    char esnap[4096];       /* --eval-every's checkpoint, saved the same way, then handed to the eval */
    /* --ref-kl's coefficient (refc0, linear: times 1 - the run's share done) and the start's weights
     * (wref, device), --value-warmup's updates (vwarm; vstep the value head's own Adam count), --anneal (lr0 times 1 -
     * the share done); the share is the larger of env-steps over --env-steps and seconds over --hours */
    double refc0, refc_min, lr0, t0, hours, max_steps, vlr;   /* vlr: --value-warmup-lr (0: the run's lr) */
    int ref_lin, anneal, vwarm, vstep;
    float *wref;
    neth_critic *crit;              /* --critic's privileged critic (NULL none) */
    /* lane/xhigh-patch: ppo.mb past a segment's T x gs samples, kseg = mb / (T x gs) segments a batch (1: each
     * segment its own update, read in place). With kseg > 1 the learner copies each segment into the batch's slot
     * kfill (its group's stream may refill it at once) and the kseg-th's update reads the batch: [kseg][T][gs] */
    int kseg, kfill;
    struct {
        uint8_t *obs, *done, *drv, *term;
        neth_sc_t *sc, *psc;
        float *logp, *val, *rew, *boot;
        float scale[64];
    } bt;
};

/* ---- the fidelity ladder (neth_schedule.c has the run file's keys) ---- */

#define LAD_MAX 8
enum { LAD_ENV_STEPS, LAD_MINUTES, LAD_MILESTONE, LAD_FIXED };
extern const char *const neth_lad_sched[4];

struct neth_ladder {
    int nr, sched, unit_env, ms, window;
    struct fidelity f[LAD_MAX];
    int vanilla[LAD_MAX];
    uint32_t code[LAD_MAX];             /* rec.h REC_FID_* */
    char name[LAD_MAX][160];
    double at[LAD_MAX], fade, share[LAD_MAX];
    /* the run */
    struct pool *pool;
    double t0;                          /* the wall clock at the start (minutes) */
    int top;                            /* the highest rung open */
    double open[LAD_MAX];               /* the progress at which each rung opened (the milestone's: env-steps) */
    int *cur, *pend, *ended;            /* [n]: the rung the episode runs (-1 the make's), the next reset's, the one
                                         * the env's last ended episode ran */
    uint8_t *mring;                     /* the milestone schedule's window on the top rung */
    int mn, mat, mhit;
    /* the books, [1 + rung] (0: the make's episodes) */
    int64_t eps[LAD_MAX + 1], steps[LAD_MAX + 1], msn[LAD_MAX + 1][NETH_VEC_MILESTONES], moves;
    double ret[LAD_MAX + 1];
};

/* ---- the held-out checkpoint eval's queue (neth_eval.c) ---- */

#define CKE_MAXQ 64
struct neth_eval_queue {
    int64_t every, next;
    int runs, threads, level;
    double minutes;
    uint64_t seed;
    const char *exe, *run, *out, *mopts;
    const char *extra;          /* --eval-extra "ARGS" (spaces apart) after the child's own, and its
                                 * episodes to CKPT.eval/episodes.jsonl (--out): the held-out starts, --dev */
    char q[CKE_MAXQ][4096];
    int64_t qsteps[CKE_MAXQ];
    int nq;
    pid_t pid;
    char cur[4096];
    int64_t cur_steps;
    double t0;
    double best;
    int64_t best_steps;
    int nevals;
    FILE *log;
};

/* ---- the pipe: the env's groups through the policy and into the learner's segments (neth_pipe.c) ---- */

struct curric;

struct neth_pipe {
    struct neth_vec *v;
    struct neth_vec_bufs b;
    struct neth_policy P, PA; /* the policy; --ab-a loop's A */
    struct neth_policy *pol;  /* the one launches use */
    int n, G, gs, T, depth, digest, quota, learn, learning, saving;
    size_t oe;
    cudaStream_t *st;
    cudaEvent_t *ev;
    struct neth_seg *sg;     /* [G][2]: the group's segment being filled and its other */
    float *hst;         /* (NETH_NET_RNN) [n][NETH_NET_D] every env's minGRU state, carried step to step (device) */
    int *cur, *tg;      /* [G]: which of the two, its next step */
    int64_t segments;
    struct neth_learner lr;
    /* the books: fastppo.py's running return and its Welford spread (the reward scale) */
    double *ret_run, w_n, w_mean, w_m2, gamma;
    int wstride;
    /* --digest, as vec_bench's; --ab's host draws; --frame-digest: each env's observations hashed too, so
     * the host cast and the device cast are held equal byte for byte (fhost: a group's frames copied back) */
    uint64_t *hash, *hrng, *fhash;
    uint8_t *fhost;
    uint8_t *seen;
    /* gcount: each group's receives (the quota's); pq the launched groups in order */
    int *gcount, *pq, np, inflight, live, fail;
    /* --serve: rq the groups received and not launched, in order; with the cast on the device, castev[g]
     * the cast that drew group g (cev's ring, -1 not cast yet), every received group not cast yet cast in one launch
     * on the first one's stream (at most cast_max groups, 0 every one) */
    int serve, *rq, nrq, *castev, ce, cast_max;
    long idle_ns;               /* --idle-us: sleep between looks when nothing is ready (0: spin) */
    int dump_at, dumps;         /* --dump-cast N:PATH: group 0's N-th cast to dump_path (the kernel bench's input) */
    int stage;                  /* --stage 1: neth_stage's one launch a group step for the copies (the buffers' device
                                 * pointers: dsc, drew, ddone, ddrv, and dobs with the host cast) */
    float *dsc, *drew;
    uint8_t *ddone, *ddrv, *dobs;
    uint8_t *dterm;             /* lane/audit-fixes: the terminations' (b.terminations: with --trunc-boot off the
                                 * pipe's copy of the buffers points it at b.truncations, neth_seg_create) */
    /* lane/cuda-graph. --poll-flag 1: a group's launch ends in one kernel that writes its actions to the env's mapped
     * buffer (dact) and then its flag done_d[g] = done_seq[g] (done_h the host's view, page-locked and mapped): the
     * main thread reads the flag (no cudaEventQuery), ev[g] recorded only for the learner's refresh. --graph 1 (the
     * flag too): the group step (stage, actor, critic, commit) one cudaGraphLaunch of gx[g][image key], its varying
     * places and the last action's outcome in the group's staging block (gstg_h + g * gstg_b, page-locked, written
     * before each launch; the graph's first node copies it to gstg_d, its scalars from the env's buffer to g_scf on a
     * forked branch: gaux, gfork, gjoin, used only while capturing); the actor and critic read fixed per-env scratch
     * (g_*) and the cast's frames in place */
    int poll, graph;
    int32_t *dact;
    uint32_t *done_h, *done_d, *done_seq;
    uint8_t *gstg_h, *gstg_d;
    size_t gstg_b;
    float *g_scf;
    cudaStream_t gaux;
    cudaEvent_t gfork, gjoin;
    /* --graph (not with --cast-hold): each group's cast on its own side stream gcst[g] (the group stream's priority),
     * gcev[g] recorded after it; the graph waits on gcev[g] inside (an event wait node before the stage kernel), so
     * its copies run beside the cast and the graph does not start behind it (21 us to 2.1 us, measured) */
    cudaStream_t *gcst;
    cudaEvent_t *gcev;
    cudaGraphExec_t *gx;    /* [G][4]: key actor image * 2 + critic image, made on first use */
    neth_sc_t *g_sc;
    int32_t *g_act;
    float *g_logp, *g_val;
    uint64_t *phash;        /* --frame-digest's companion: each env's policy outputs (actions, logp, value, boot) */
    double *t_lch, t_rt;    /* [G] each group's last launch's clock; the launches' summed launch-to-sent times */
    const char *dump_path;
    int cast_hold, last_ce;     /* --cast-hold 1: no cast launched while the last one runs (last_ce, -1 none): the
                                 * groups received meanwhile go in one launch after it */
    int64_t casts, cast_groups;
    double t_cast;      /* the main thread's seconds in neth_vec_cast_groups (within launch) */
    cudaEvent_t *cev;
    double tm[T_N], tm_row[T_N], tick;   /* tm_row: tm at the last row (a row's t_loop is its window's, as fastppo's) */
    int64_t gsteps, env_steps;
    /* --rec: the recorder (rec.h), handed each received group's buffers; the episode records recv made
     * are taken at once for it (its group's own) and kept in stash for the report's rows; --rec-rows and --tape (the
     * gate's) arm neth_rl_rows and neth_vec_tape for one env at its group's first recv */
    struct rec *rec;
    struct neth_vec_episode *stash, *eptmp, *pend;
    int nstash, stash_cap, npend, rows_env, tape_env;
    int64_t stash_drop;
    const char *rows_path, *tape_path;
    /* --obs-dump E:DIR[:MAX], env E's observation as the policy reads it each step (the host cast's
     * bytes) to DIR/o-SEQ.ppm, DIR/index.tsv a line each (seq, the env's tick in its episode), at most MAX */
    const char *obs_dir;
    int obs_env;
    /* --zero-sc LO:N, the scalars LO..LO+N-1 of every env zeroed before the policy reads them (a policy
     * trained with the seen-ore scalars played without them); N 0 (the default) none */
    int zsc_lo, zsc_n;
    /* --critic H, every env's scalars before --zero-sc (page-locked [n][NETH_NET_NS]), copied to the segment's psc
     * for the critic; NULL (the default) none. sch: pinned BF16 staging buffer for b->scalars when NETH_SC_BF16=1 */
    neth_sc_t *psch;
    neth_sc_t *sch;
    /* --view-books K (a run with the or feature), the seen-object scalars' flag of kind K (0 iron ore, 1 diamond ore,
     * 2 logs: drawn on screen this step) read before --zero-sc, each env's episode's view as books, a line an episode
     * to OUT/view.jsonl: steps, steps in view, the first step in view, the times it left the view (lost) and came back
     * (reacq), the longest gap out of view, in view at the end; NULL vbf (the default) none */
    struct vbook {
        int64_t te;
        int32_t ep, steps, nin, first, lost, reacq, gap, gapmax, in;
    } *vb;
    FILE *vbf;
    int vb_col;
    /* --obs-tone auto|gamma:G, the observation's bytes through a tone in the segment (loop.h neth_tone)
     * before the policy and the learner read them; 0 (the default) none */
    int tone;
    uint8_t *tlut, hlut[256];
    int64_t obs_seq, obs_max;
    /* the ladder (NULL: none); every recorded episode's rung and fidelity (eprung, epfid beside eptmp;
     * sim_code the sim's own fidelity, every episode's without a ladder) */
    struct neth_ladder *lad;
    struct curric *crc;         /* the seed set, library and curriculum (NULL: none) */
    int sscale;                 /* ppo.stage_scale, each env's rewards over its stage's scale (curric.h
                                 * cur_scale) in place in the env's buffer at recv, the learner's scale then 1 */
    int *stash_rung;            /* beside stash: each record's rung (-1 none, or the make's episode) */
    int32_t *eprung;
    uint32_t *epfid, sim_code;
    /* --teacher's distillation (train/distill.h; NULL: none): the actor is the teacher, the learner the
     * student; its rows in DIR/distill.jsonl */
    struct neth_distill *dist;
    int lab;                    /* --labeller on (each env's level set from its driver at each episode) */
    FILE *dist_rows;
    struct neth_policy PS;        /* --beta: the student as an actor (refreshed from the learner as the trainer's is) */
    /* --clock wrap, the time scalar (scalars[7], minutes played / 5) read modulo 1 as mcsr's fromspawn.py
     * does by default (training saw 5-minute episodes); --score, the frozen eval: one episode an env, each TIMED
     * item's first held receive (fromspawn's "first held", from the item counts in the scalars), -1 never */
    int clock_wrap;
    FILE *sbf;                  /* --score-books FILE (NULL: off) */
    int32_t *first;
    struct neth_eval_queue *ckev;   /* --eval-every's held-out checkpoint eval (NULL: off) */
    int stud_level;             /* the student's macros (--teacher's --level; -1 none): the checkpoint eval's */
    /* the hooks a step may run, each NULL unless its option is on (one NULL check on the hot path): staged, a group
     * about to be staged (--obs-dump, --view-books: before --zero-sc touches the scalars); received, a group just
     * booked (--score-books) */
    void (*staged)(struct neth_pipe *L, int g);
    void (*received)(struct neth_pipe *L, int g);
    /* the run (neth_pipe_create to neth_pipe_destroy): the clocks, --out's files, the episodes read back */
    double t_start, t_make, t_rec0, last_log;
    int64_t last_steps;
    FILE *rows, *eprows;
    struct neth_vec_episode *eps;
    int epcap, nep, snapk, pinned;
    float *flat0, *tflat;       /* --init's weights and the teacher's (--soft), the learner's layout, while made */
    char err[1024];
};

/* a measured stretch: env-steps/s and the main thread's parts */
struct neth_mark {
    double s[8], tm[T_N], t, gpu_learn, t_cast, t_rt;
    int64_t gsteps, jobs, casts, cast_groups;
};

struct neth_runconf;

/* neth_pipe.c: the lifecycle (create, run or bench, finish, destroy: destroy is safe after a create that failed part
 * way) and the hot path. Setup returns an exit code with the reason printed; a CUDA failure inside the hot path's CK
 * prints where and exits (the run cannot go on and its state is the device's) */
int neth_pipe_create(struct neth_pipe *L, struct neth_runconf *c, const struct neth_vec_opts *o, int argc,
                     char **argv);
void neth_pipe_run(struct neth_pipe *L, const struct neth_runconf *c);
void neth_pipe_finish(struct neth_pipe *L, const struct neth_runconf *c);
void neth_pipe_destroy(struct neth_pipe *L);
void neth_pipe_lap(struct neth_pipe *L, int k);
int neth_pipe_iterate(struct neth_pipe *L, int host);
void neth_pipe_drain(struct neth_pipe *L);
void neth_pipe_launch(struct neth_pipe *L, int g);
void neth_pipe_mark(struct neth_pipe *L, struct neth_mark *m);
double neth_pipe_report(struct neth_pipe *L, const struct neth_mark *a, const struct neth_mark *z, const char *what,
                        double t_make);

/* neth_loop_bench.c: --ab, vec_bench's loop against this one in one process */
void neth_bench_run(struct neth_pipe *L, const struct neth_runconf *c);

/* neth_segment.c: the segments, the streams, the books' arrays, the env's buffers page-locked */
int neth_seg_create(struct neth_pipe *L, const struct neth_runconf *c);

/* neth_train_stage.c: the policy and the learner */
int neth_train_policy(struct neth_pipe *L, const struct neth_runconf *c);
int neth_train_setup(struct neth_pipe *L, const struct neth_runconf *c);
double neth_train_reward_scale(const struct neth_pipe *L);
void neth_train_init(struct neth_pipe *L, int nmax, int prec);
void neth_train_free(struct neth_learner *R);
void neth_train_done(struct neth_pipe *L);
void neth_train_poll(struct neth_pipe *L, int wait);
void neth_train_claim(struct neth_pipe *L, struct neth_seg *X, cudaStream_t s);
void neth_train_drain(struct neth_pipe *L);

/* neth_distill_stage.c: --teacher's student, its labels and books */
int neth_distill_setup(struct neth_pipe *L, const struct neth_runconf *c);
void neth_distill_report(struct neth_pipe *L);

/* neth_schedule.c: the starts' mix and curriculum, the sim's fidelity, the ladder */
int neth_schedule_starts(struct neth_pipe *L, const struct neth_runconf *c, struct neth_vec_opts *o);
int neth_schedule_fidelity(struct neth_pipe *L, const struct neth_runconf *c, const struct neth_vec_opts *o);
void neth_schedule_ladder_start(struct neth_pipe *L, struct neth_runconf *c);
void neth_schedule_report(struct neth_pipe *L);
void neth_ladder_ends(struct neth_pipe *L, int g, int was);
void neth_ladder_assign(struct neth_pipe *L, int g);
void neth_ladder_book(struct neth_pipe *L, const struct neth_vec_episode *e, int r);

/* neth_report.c: episodes.jsonl, iters.jsonl, the score, the books; an episode's line carries its rung unless
 * NETH_NO_RUNG */
#define NETH_NO_RUNG INT_MIN
void neth_report_episode(FILE *f, const struct neth_vec_episode *e, int64_t steps, int rung);
void neth_report_rows(struct neth_pipe *L, int final);
void neth_report_score(const struct neth_pipe *L, const struct neth_vec_episode *eps, int k, double minutes,
                       double wall, int64_t env_steps, const char *init, uint64_t seed, int macros, FILE *json);
int neth_report_open(struct neth_pipe *L, const struct neth_runconf *c);
void neth_report_score_run(struct neth_pipe *L, const struct neth_runconf *c, double wall);
void neth_report_digest(const struct neth_pipe *L);
void neth_report_finish(struct neth_pipe *L, const struct neth_runconf *c);
void neth_report_staged(struct neth_pipe *L, int g);    /* the staged hook: --obs-dump, --view-books */
void neth_report_received(struct neth_pipe *L, int g);  /* the received hook: --score-books */

/* neth_checkpoint.c: net.h's blobs and the model's files. lane/audit-fixes: neth_ntw_parse is every .ntw reader's
 * (NTW2 and NTW1, net.h's rules; actor.cu, distill.cu and ntinit.c declare it as here), neth_ntw_write the one
 * writer (NTW2, the build's header), neth_ntw_migrate and neth_ntw_info ntinit's */
int neth_ntw_parse(const void *data, size_t len, const char *name, int flags, float *t[NETH_NET_TENSORS],
                   int dims[NETH_NET_TENSORS][4], char *err, size_t en);
unsigned char *neth_ntw_write(const float *const t[NETH_NET_TENSORS], size_t *len);
unsigned char *neth_ntw_migrate(const void *data, size_t len, const char *name, const char *words, size_t *olen,
                                char *err, size_t en);
long neth_ntw_info(const void *data, size_t len, const char *name, char *variant, size_t vn, char *features, size_t fn,
                   int *scalars, char *err, size_t en);
/* lane/audit-feat: feature words into a mask and back, and a file's version and header features (the run's set) */
int neth_net_words(const char *words, int *mask, char *err, size_t en);
void neth_net_mask_words(int mask, char *s, size_t n);
int neth_ntw_features(const void *data, size_t len, const char *name, int *mask, char *variant, size_t vn, char *err,
                      size_t en);
int neth_ntw_flat(const unsigned char *b, size_t len, float *flat, char *err, size_t en);
int neth_ntw_load(const char *path, float *flat, char *err, size_t en);
int neth_ntw_save(const char *path, const float *flat);
int neth_file_copy(const char *src, const char *dst);
int neth_checkpoint_save(struct neth_pipe *L, const char *dir, double wall);
int neth_checkpoint_load_opt(struct neth_learner *R, const char *init, char *err, size_t en);

/* neth_eval.c: the checkpoint eval's children */
int neth_eval_setup(struct neth_pipe *L, const struct neth_runconf *c, const char *exe);
void neth_eval_tick(struct neth_pipe *L, const char *out);     /* a log row's: the next checkpoint named, polled */
void neth_eval_finish(struct neth_pipe *L, const char *out);   /* the queue and the final model evaluated */
void neth_eval_push(struct neth_eval_queue *c, const char *path, int64_t steps);

/* neth_loop_check.c: the gates without the env */
int neth_check_learn(const char *dir, const neth_learn_hyper *h0, float ent0, int epochs0, int mb0, int prec);
int neth_check_actor(const char *dir);

/* neth_record_hook.c: the episode records (the recorder's, the ladder's) and the recorder on DIR/episodes.nrec */
int neth_record_setup(struct neth_pipe *L, const struct neth_runconf *c, const struct neth_vec_opts *o, int argc,
                      char **argv);
void neth_record_finish(struct neth_pipe *L);

#endif
