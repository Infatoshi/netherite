/* ntloop's held-out checkpoint eval (--eval-every N; run file distill.eval_every).
 *
 * Every N env-steps of a learning run the next update's weights go to OUT/ckpt-NNNNNM.ntw, and a child ntloop (this
 * binary, argv[0]) plays it frozen: --score at the student's level (--level, or --macros without a teacher) from the run
 * file's start, --eval-runs envs one --eval-minutes episode each, sampler seed --eval-seed (held out: the reports' evals
 * are seeds 7 and 8), the raw clock (the 20-minute eval's: ntloop --learn 0), --eval-threads workers, beside the
 * training on the same GPU and CPUs. One child at a time, the checkpoints queued; each result a line of
 * OUT/evals.jsonl, and the best depth so far copied to OUT/model-best.ntw (model-best.json: its env-steps and depth).
 * At the run's end the queue and the final model are evaluated before the trainer exits. Erosion (a
 * student at 3.21 at 64M, 2.27 at 256M) costs a run nothing then: its best point is kept. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "neth_pipe_internal.h"
#include "neth_runconf.h"

/* the child's argv, bounded: a push past the end is refused (the last slot is the NULL) */
#define EVAL_ARGV 64
struct eval_argv {
    const char *a[EVAL_ARGV];
    int k;
};

static int argv_push(struct eval_argv *v, const char *s)
{
    if (v->k >= EVAL_ARGV - 1) return -1;
    v->a[v->k++] = s;
    v->a[v->k] = NULL;
    return 0;
}

/* the front of the queue as a child --score run, its log and JSON beside the checkpoint */
static void cke_spawn(struct neth_eval_queue *c)
{
    if (c->pid > 0 || c->nq == 0) return;
    snprintf(c->cur, sizeof c->cur, "%s", c->q[0]);
    c->cur_steps = c->qsteps[0];
    memmove(c->q, c->q + 1, (size_t)(c->nq - 1) * sizeof c->q[0]);
    memmove(c->qsteps, c->qsteps + 1, (size_t)(c->nq - 1) * sizeof c->qsteps[0]);
    --c->nq;
    char lg[4200], js[4200], lev[32], runs[32], grp[32], mins[32], seed[32], thr[32];
    snprintf(lg, sizeof lg, "%s.eval.log", c->cur);
    snprintf(js, sizeof js, "%s.eval.json", c->cur);
    snprintf(lev, sizeof lev, "%d", c->level);
    snprintf(runs, sizeof runs, "%d", c->runs);
    snprintf(grp, sizeof grp, "%d", c->runs % 4 == 0 && c->runs >= 16 ? 4 : 1);
    snprintf(mins, sizeof mins, "%g", c->minutes);
    snprintf(seed, sizeof seed, "%llu", (unsigned long long)c->seed);
    snprintf(thr, sizeof thr, "%d", c->threads);
    remove(js);
    struct eval_argv v = {{NULL}, 0};
    const char *fixed[] = {c->exe, "--score", "--init", c->cur, "--macros", lev, "--ns", runs, "--groups", grp,
                           "--minutes", mins, "--seed", seed, "--clock", "raw", "--threads", thr, "--score-json", js};
    for (size_t i = 0; i < sizeof fixed / sizeof *fixed; ++i)
    {
        argv_push(&v, fixed[i]);
        if (i == 0 && c->run) argv_push(&v, "--run"), argv_push(&v, c->run);
    }
    if (c->mopts && c->mopts[0]) argv_push(&v, "--mopt"), argv_push(&v, c->mopts);
    static char xb[4096], od[4200];
    if (c->extra && c->extra[0])
    {
        /* the extra arguments (two slots kept for --out), then the episodes beside the checkpoint */
        snprintf(xb, sizeof xb, "%s", c->extra);
        int dropped = 0;
        for (char *sv, *t = strtok_r(xb, " ", &sv); t; t = strtok_r(NULL, " ", &sv))
            dropped += v.k + 3 >= EVAL_ARGV || argv_push(&v, t);
        if (dropped) printf("eval: --eval-extra: %d arguments past the child's %d dropped\n", dropped, EVAL_ARGV);
        snprintf(od, sizeof od, "%s.eval", c->cur);
        argv_push(&v, "--out"), argv_push(&v, od);
    }
    fflush(stdout);
    pid_t p = fork();
    if (p == 0)
    {
        int fd = open(lg, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) dup2(fd, 1), dup2(fd, 2), close(fd);
        execv(c->exe, (char *const *)v.a);
        _exit(127);
    }
    if (p < 0)
    {
        printf("FAIL --eval-every: fork: %s\n", strerror(errno));
        return;
    }
    c->pid = p;
    c->t0 = wall_s();
}

/* the child's end (wait: block for it): its depth to evals.jsonl, the best kept; then the next spawned */
static void eval_poll(struct neth_eval_queue *c, int wait)
{
    if (c->pid > 0)
    {
        int st = 0;
        pid_t r = waitpid(c->pid, &st, wait ? 0 : WNOHANG);
        if (r == c->pid)
        {
            c->pid = 0;
            char js[4200];
            snprintf(js, sizeof js, "%s.eval.json", c->cur);
            size_t len;
            char *b = (char *)slurp(js, &len);
            double depth = NAN, se = NAN, deaths = NAN;
            const char *p;
            if (b && (p = strstr(b, "\"depth\": "))) depth = atof(p + 9);
            if (b && (p = strstr(b, "\"depth_se\": "))) se = atof(p + 12);
            if (b && (p = strstr(b, "\"deaths\": "))) deaths = atof(p + 10);
            free(b);
            int ok = WIFEXITED(st) && WEXITSTATUS(st) == 0 && !isnan(depth);
            int better = ok && (c->nevals == 0 || depth > c->best);
            if (ok) ++c->nevals;
            if (better)
            {
                char dst[4200];
                snprintf(dst, sizeof dst, "%s/model-best.ntw", c->out);
                if (neth_file_copy(c->cur, dst)) printf("FAIL --eval-every: %s: %s\n", dst, strerror(errno));
                else
                {
                    c->best = depth, c->best_steps = c->cur_steps;
                    snprintf(dst, sizeof dst, "%s/model-best.json", c->out);
                    FILE *f = fopen(dst, "w");
                    if (f)
                        fprintf(f, "{\"from\": \"%s\", \"env_steps\": %lld, \"depth\": %.4f, \"depth_se\": %.4f}\n",
                                c->cur, (long long)c->cur_steps, depth, se), fclose(f);
                }
            }
            if (c->log)
            {
                fprintf(c->log, "{\"ckpt\": \"%s\", \"env_steps\": %lld, \"ok\": %d, \"depth\": %.4f, \"depth_se\": %.4f, "
                        "\"deaths\": %.4f, \"best\": %d, \"wall\": %.1f}\n", c->cur, (long long)c->cur_steps, ok,
                        ok ? depth : 0.0, ok && !isnan(se) ? se : 0.0, ok && !isnan(deaths) ? deaths : 0.0, better,
                        wall_s() - c->t0);
                fflush(c->log);
            }
            printf("eval: %s at %lld env-steps: %s depth %.3f (se %.3f)%s, %.0f s\n", c->cur, (long long)c->cur_steps,
                   ok ? "" : "FAIL", depth, se, better ? ", the best so far: model-best.ntw" : "", wall_s() - c->t0);
        }
    }
    cke_spawn(c);
}

void neth_eval_push(struct neth_eval_queue *c, const char *path, int64_t steps)
{
    if (c->nq == CKE_MAXQ)
    {
        printf("eval: the queue is full, %s not evaluated\n", path);
        return;
    }
    snprintf(c->q[c->nq], sizeof c->q[0], "%s", path);
    c->qsteps[c->nq++] = steps;
    cke_spawn(c);
}

/* --eval-every's queue (a learning run with --out): 0, or the exit code. The run file's distill.eval_every is
 * training's: an eval of the same run file (the checkpoint eval's own child, --score, --learn 0) plays without it */
int neth_eval_setup(struct neth_pipe *L, const struct neth_runconf *rc, const char *exe)
{
    static struct neth_eval_queue q0;
    double every = rc->e_every;
    if (every > 0 && rc->e_from_run && (!rc->learn || rc->score || rc->digest)) every = 0;
    if (!(every > 0)) return 0;
    if (!rc->learn || !rc->out || rc->score || rc->digest || rc->e_runs < 1 || rc->e_min <= 0 || rc->e_thr < 1)
    {
        printf("FAIL: --eval-every needs a learning run with --out (not --score or --digest), --eval-runs, "
               "--eval-minutes and --eval-threads above 0\n");
        return 2;
    }
    struct neth_eval_queue *c = &q0;
    c->every = (int64_t)every, c->next = (int64_t)every;
    c->runs = rc->e_runs, c->threads = rc->e_thr, c->minutes = rc->e_min, c->seed = rc->e_seed;
    c->level = L->stud_level >= 0 ? L->stud_level : rc->macros;
    c->exe = exe, c->run = rc->run, c->out = rc->out, c->mopts = rc->mopts;
    c->extra = rc->e_extra;
    char p[4096];
    snprintf(p, sizeof p, "%s/evals.jsonl", rc->out);
    c->log = fopen(p, "a");
    L->ckev = c;
    printf("ntloop: lane/ladder4: every %lld env-steps a checkpoint played frozen at macros %d, %d runs of %g "
           "minutes, eval seed %llu, %d threads (OUT/evals.jsonl, the best OUT/model-best.ntw)\n",
           (long long)c->every, c->level, c->runs, c->minutes, (unsigned long long)c->seed, c->threads);
    return 0;
}

/* a log row's: a checkpoint named past each --eval-every env-steps (the next update's weights), the child polled */
void neth_eval_tick(struct neth_pipe *L, const char *out)
{
    struct neth_eval_queue *c = L->ckev;
    if (L->env_steps >= c->next && !L->lr.esnap[0])
    {
        /* with --eval-extra (checkpoints a few hundred thousand env-steps apart) by thousands, ckpt-NNNNNNNK */
        if (c->extra && c->extra[0])
            snprintf(L->lr.esnap, sizeof L->lr.esnap, "%s/ckpt-%07lldK.ntw", out, (long long)(L->env_steps / 1000));
        else
            snprintf(L->lr.esnap, sizeof L->lr.esnap, "%s/ckpt-%05lldM.ntw", out,
                     (long long)(L->env_steps / 1000000));
        while (c->next <= L->env_steps) c->next += c->every;
    }
    eval_poll(c, 0);
}

/* the queue and the final model evaluated before the trainer exits */
void neth_eval_finish(struct neth_pipe *L, const char *out)
{
    struct neth_eval_queue *c = L->ckev;
    if (!c) return;
    char p[4096];
    snprintf(p, sizeof p, "%s/model.ntw", out);
    if (L->learn) neth_eval_push(c, p, L->env_steps);
    while (c->pid > 0 || c->nq > 0) eval_poll(c, 1);
    printf("eval: the best of %d checkpoints: depth %.3f at %lld env-steps, %s/model-best.ntw\n", c->nevals, c->best,
           (long long)c->best_steps, out);
    if (c->log) fclose(c->log);
    L->ckev = NULL;
}
