/* nether flywheel: one autoresearch step from a declarative gym file (cli_verbs.h).
 *
 *   nether flywheel GYM.yaml [--name EXP] [--budget STEPS] [--incumbent W.ntw] [--gpu G [--no-gpu-guard]
 *       [--gpu-wait S] [--gpu-max-mib M] [--gpu-max-util U]] [--lock FILE] [--log RESULTS.tsv] [--set KEY=VALUE ...]
 *       [--dry-run]
 *
 * GYM.yaml (every experiment fact is here, none in C):
 *   name, variant                      the experiment and the trainer build (out/runtime/train/VARIANT/ntloop)
 *   train_config, eval_config          run files (the eval's is held to configs/fidelity.yaml unless allow_approx)
 *   budget_steps                       env-steps of training a step
 *   incumbent                          the model a step starts from and is compared with (--incumbent wins)
 *   primary_metric                     depth, ret, deaths, raw_share, or a milestone (dragon, iron_pickaxe, ...; win,
 *                                      a task environment's win rate: lane/audit-fixes)
 *   max_p, min_z                       KEEP when the metric is better with p <= max_p or z >= min_z
 *   eval_split                         a seeds file or splits.json (eval_seeds) for the eval
 *   build_cmd, quick_gate              a command run first (no shell), and on: nether gate --quick first
 *   log                                the directory of the runs and results.tsv / results.jsonl
 *   train.KEY, eval.KEY                ntloop flags by name (KEY with _ for -); train.flags, eval.flags: bare flags
 *   eval.episodes, eval.minutes, eval.seed   the eval's size
 *   init_seed                          ntinit's seed for a fresh net when there is no incumbent
 *
 * The step: flock (--lock), the GPU guard (with --gpu: GPU G holds under 500 MiB and 5% use, or the step ends
 * with exit 3 before anything runs; --gpu-wait S polls up to S seconds first), the build, the gate, ntloop for the budget from the
 * incumbent (OUT/run.yaml: the train config with --set's overrides), the exact eval of the candidate and the
 * incumbent (each cached by content in out/evalcache), the verdict, one row appended to results.tsv and
 * results.jsonl under flock. KEEP and BASELINE point LOG/incumbent.ntw at the new model.
 * Exit 0 KEEP or BASELINE, 10 REVERT, 2 a usage error, 3 the GPU busy, other codes the failed step's. */
#define _GNU_SOURCE
#include "cli_json.h"
#include "cli_proc.h"
#include "cli_run.h"
#include "cli_verbs.h"
#include "cli_yaml.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct fw_row {
    char made[40], name[160], sha[32], variant[96], verdict[16], ckpt[PATH_MAX], run[PATH_MAX], metric[64], step[32];
    double steps, sps, nan, base, cand, delta, p, z, wall;
};

static void row_write(const char *tsv, const char *jsonl, const struct fw_row *r)
{
    char line[PATH_MAX * 2 + 1024];
    snprintf(line, sizeof line, "%s\t%s\t%s\t%s\t%.0f\t%.0f\t%.0f\t%s\t%.6g\t%.6g\t%.6g\t%.6g\t%.6g\t%s\t%s\t%s\n", r->made,
             r->name, r->sha, r->variant, r->steps, r->sps, r->nan, r->metric, r->base, r->cand, r->delta, r->p, r->z,
             r->verdict, r->ckpt, r->run);
    if (cli_append_locked(tsv, "timestamp\texp_name\tgit_sha\tvariant\tsteps\tsps\tnan_skips\tprimary_metric\tbase_val\tcand_val\t"
                               "delta\tp_val\tz_val\tverdict\tckpt_path\trun_dir\n", line))
        fprintf(stderr, "nether flywheel: cannot append to %s\n", tsv);
    struct sb j = {0};
    sb_printf(&j, "{\"timestamp\": \"%s\", \"exp_name\": ", r->made);
    cli_json_str(&j, r->name);
    sb_printf(&j, ", \"git_sha\": \"%s\", \"variant\": ", r->sha);
    cli_json_str(&j, r->variant);
    sb_puts(&j, ", \"steps\": ");
    cli_json_num(&j, r->steps);
    sb_puts(&j, ", \"sps\": ");
    cli_json_num(&j, r->sps);
    sb_puts(&j, ", \"nan_skips\": ");
    cli_json_num(&j, r->nan);
    sb_puts(&j, ", \"primary_metric\": ");
    cli_json_str(&j, r->metric);
    sb_puts(&j, ", \"base_val\": ");
    cli_json_num(&j, r->base);
    sb_puts(&j, ", \"cand_val\": ");
    cli_json_num(&j, r->cand);
    sb_puts(&j, ", \"delta\": ");
    cli_json_num(&j, r->delta);
    sb_puts(&j, ", \"p_val\": ");
    cli_json_num(&j, r->p);
    sb_puts(&j, ", \"z_val\": ");
    cli_json_num(&j, r->z);
    sb_printf(&j, ", \"verdict\": \"%s\", \"failed_step\": ", r->verdict);
    if (r->step[0]) cli_json_str(&j, r->step);
    else sb_puts(&j, "null");
    sb_puts(&j, ", \"ckpt_path\": ");
    cli_json_str(&j, r->ckpt);
    sb_puts(&j, ", \"run_dir\": ");
    cli_json_str(&j, r->run);
    sb_puts(&j, ", \"wall\": ");
    cli_json_num(&j, r->wall);
    sb_puts(&j, "}\n");
    if (cli_append_locked(jsonl, NULL, j.s)) fprintf(stderr, "nether flywheel: cannot append to %s\n", jsonl);
    sb_free(&j);
}

/* iters.jsonl: the median env-steps/s, the NaN skips (each row's count summed), the last env_steps and wall */
/* the number of rows with a rate (0: sps is the whole run's, or NaN) */
static int read_iters(const char *path, double *sps, double *nan, double *steps, double *wall)
{
    *sps = *nan = *steps = *wall = NAN;
    char *s = cli_read_file(path, NULL);
    if (!s) return 0;
    double *v = NULL, ns = 0;
    int n = 0, cap = 0, any_nan = 0;
    for (char *q = s, *e; *q; q = e) {
        e = strchr(q, '\n');
        if (e) *e++ = 0;
        else e = q + strlen(q);
        if (*q != '{') continue;
        char err[64];
        struct jv *j = jv_parse(q, err, sizeof err);
        if (!j) continue;
        double x = jv_num(j, "env_steps_per_s", NAN);
        if (!isnan(x) && !jv_get(j, "final")) {
            if (n == cap) cap = cap ? cap * 2 : 64, v = xrealloc(v, sizeof *v * (size_t)cap);
            v[n++] = x;
        }
        double k = jv_num(j, "nan_skips", NAN);
        if (!isnan(k)) ns += k, any_nan = 1;
        double st = jv_num(j, "env_steps", NAN), w = jv_num(j, "wall", NAN);
        if (!isnan(st)) *steps = st;
        if (!isnan(w)) *wall = w;
        jv_free(j);
    }
    free(s);
    if (n) {
        for (int i = 1; i < n; ++i)
            for (int k = i; k > 0 && v[k - 1] > v[k]; --k) {
                double t = v[k];
                v[k] = v[k - 1], v[k - 1] = t;
            }
        *sps = n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
    } else if (*steps > 0 && *wall > 0)
        *sps = *steps / *wall; /* a run shorter than one log interval has only its final row: the whole run's rate,
                                  the envs' make included */
    *nan = any_nan ? ns : 0;
    free(v);
    return n;
}

static int spawn_logged(const char *const *av, const char *gpu, const char *log, double timeout)
{
    char gv[48];
    const char *env[2] = {NULL, NULL};
    if (gpu) snprintf(gv, sizeof gv, "CUDA_VISIBLE_DEVICES=%s", gpu), env[0] = gv;
    struct cli_spawn_opts o = {.argv = av, .cwd = cli_root, .env_set = env, .out_path = log, .tee = 1, .timeout_s = timeout,
                               .stdin_null = 1};
    struct cli_spawn_res r;
    if (cli_spawn(&o, &r)) return 127;
    return r.rc;
}

int v_flywheel(int argc, char **argv)
{
    const char *gym = NULL, *name = NULL, *inc = NULL, *gpu = NULL, *lock = NULL, *logtsv = NULL;
    long budget = -1;
    int dry = 0, guard = 1, max_mib = 500, max_util = 5;
    double gwait = 0;
    struct sv sets = {0};
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
#define V() cli_arg(argc, argv, &i, "flywheel")
        if (!strcmp(a, "--name")) name = V();
        else if (!strcmp(a, "--budget")) budget = atol(V());
        else if (!strcmp(a, "--incumbent")) inc = V();
        else if (!strcmp(a, "--gpu")) gpu = V();
        else if (!strcmp(a, "--gpu-guard")) guard = 1;
        else if (!strcmp(a, "--no-gpu-guard")) guard = 0;
        else if (!strcmp(a, "--gpu-wait")) gwait = atof(V());
        else if (!strcmp(a, "--gpu-max-mib")) max_mib = atoi(V());
        else if (!strcmp(a, "--gpu-max-util")) max_util = atoi(V());
        else if (!strcmp(a, "--lock")) lock = V();
        else if (!strcmp(a, "--log")) logtsv = V();
        else if (!strcmp(a, "--set")) sv_push(&sets, V());
        else if (!strcmp(a, "--dry-run")) dry = 1;
        else if (a[0] == '-') {
            fprintf(stderr, "nether flywheel: unknown flag %s (nether flywheel --help)\n", a);
            return 2;
        } else if (!gym) gym = a;
        else {
            fprintf(stderr, "nether flywheel: one gym file\n");
            return 2;
        }
#undef V
    }
    if (!gym) {
        fprintf(stderr, "nether flywheel GYM.yaml [--name N] [--budget STEPS] [--incumbent W.ntw] [--gpu G] [--lock F] "
                        "[--log RESULTS.tsv] [--set K=V ...] [--dry-run]\n");
        return 2;
    }
    if (gpu && (!*gpu || gpu[strspn(gpu, "0123456789")])) {
        fprintf(stderr, "nether flywheel: --gpu takes one GPU index\n");
        return 2;
    }
    char gp[PATH_MAX], err[512];
    if (cr_path("flywheel", gym, gp)) return 2;
    struct cy *y = cy_load(gp, err, sizeof err);
    if (!y) {
        fprintf(stderr, "nether flywheel: %s\n", err);
        return 2;
    }
    double t0 = cli_now();
    struct fw_row row;
    memset(&row, 0, sizeof row);
    row.steps = row.sps = row.nan = row.base = row.cand = row.delta = row.p = row.z = NAN;
    const char *gname = cy_get(y, "name", "gym");
    char stamp[32];
    cli_stamp(stamp, sizeof stamp, "%Y%m%d-%H%M%S");
    cli_stamp(row.made, sizeof row.made, "%Y-%m-%dT%H:%M:%S%z");
    snprintf(row.name, sizeof row.name, "%s", name ? name : gname);
    cli_git_sha(row.sha, sizeof row.sha);
    const char *variant = cy_get(y, "variant", NULL);
    snprintf(row.variant, sizeof row.variant, "%s", variant ? variant : "");
    const char *metric = cy_get(y, "primary_metric", "depth");
    snprintf(row.metric, sizeof row.metric, "%s", metric);
    if (budget < 0) budget = cy_long(y, "budget_steps", -1);
    if (!inc) inc = cy_get(y, "incumbent", NULL);
    /* the KEEP rule is the gym's: a better primary metric with p <= max_p, or z >= min_z (either, or both) */
    double max_p = cy_num(y, "max_p", -1), min_z = cy_num(y, "min_z", INFINITY);
    const char *tc = cy_get(y, "train_config", NULL), *ec = cy_get(y, "eval_config", tc);
    if (!tc || budget <= 0 || !cy_get(y, "primary_metric", NULL) || (max_p < 0 && isinf(min_z))) {
        fprintf(stderr, "nether flywheel: %s needs train_config, budget_steps (or --budget), primary_metric and "
                        "max_p or min_z\n", gp);
        cy_free(y);
        return 2;
    }
    char tcp[PATH_MAX], ecp[PATH_MAX], incp[PATH_MAX] = "", fid[PATH_MAX];
    if (cr_path("flywheel", tc, tcp) || cr_path("flywheel", ec, ecp) || (inc && cr_path("flywheel", inc, incp))) {
        cy_free(y);
        return 2;
    }
    int approx = cy_bool(y, "allow_approx", 0);
    if (!approx && cr_path("flywheel", cy_get(y, "fidelity", "configs/fidelity.yaml"), fid)) {
        cy_free(y);
        return 2;
    }
    /* the eval's size and sampler seed: the gym's, else the fidelity file's eval section */
    char eps[32], eseed[32];
    if (cy_get(y, "eval.episodes", NULL)) snprintf(eps, sizeof eps, "%s", cy_get(y, "eval.episodes", ""));
    else if (cr_eval_default("flywheel", cy_get(y, "fidelity", "configs/fidelity.yaml"), "episodes", eps, sizeof eps)) {
        cy_free(y);
        return 2;
    }
    if (cy_get(y, "eval.seed", NULL)) snprintf(eseed, sizeof eseed, "%s", cy_get(y, "eval.seed", ""));
    else if (cr_eval_default("flywheel", cy_get(y, "fidelity", "configs/fidelity.yaml"), "seed", eseed, sizeof eseed)) {
        cy_free(y);
        return 2;
    }
    /* the run's directory and the results' files */
    char logdir[PATH_MAX], run[PATH_MAX], tsv[PATH_MAX], jsonl[PATH_MAX + 16];
    const char *ld = cy_get(y, "log", NULL);
    if (ld) cli_resolve(ld, logdir);
    else cli_fmt(logdir, sizeof logdir, "%s/out/flywheel/%s", cli_root, gname);
    cli_fmt(run, sizeof run, "%s/%s-%s", logdir, row.name, stamp);
    if (logtsv) cli_abs(logtsv, tsv);
    else cli_fmt(tsv, sizeof tsv, "%s/results.tsv", logdir);
    cli_fmt(jsonl, sizeof jsonl, "%.*s.jsonl", (int)(strlen(tsv) > 4 && !strcmp(tsv + strlen(tsv) - 4, ".tsv") ? strlen(tsv) - 4 : strlen(tsv)), tsv);
    snprintf(row.run, sizeof row.run, "%s", run);
    printf("nether flywheel: %s: %s, variant %s, %ld env-steps, primary %s (KEEP at p <= %g or z >= %g), incumbent %s\n",
           gp, row.name, variant ? variant : "(from the run file)", budget, metric, max_p, min_z, incp[0] ? incp : "none (BASELINE)");
    printf("nether flywheel: run %s; results %s\n", run, tsv);
    /* the flags */
    struct sv targs = {0}, eargs = {0}, fargs = {0};
    static const char *const ESKIP[] = {"episodes", "minutes", "seed", NULL};
    if (cr_section_flags("flywheel", y, "train", &targs, NULL) || cr_section_flags("flywheel", y, "eval", &eargs, ESKIP)) {
        cy_free(y);
        return 2;
    }
    int rc = 0, verdict_rc = 0, lfd = -1;
    const char *failed = NULL;
    if (dry) {
        printf("nether flywheel: --dry-run: the build (%s), the gate (%s), then:\n", cy_get(y, "build_cmd", "none"),
               cy_bool(y, "quick_gate", 0) ? "nether gate --quick" : "off");
        struct sb l = {0};
        cli_argv_line((const char *const *)targs.v ? (const char *const *)targs.v : (const char *const[]){NULL}, &l);
        printf("  train: ntloop --run %s/run.yaml (%s with %d --set) --init %s --env-steps %ld --out %s/train %s\n", run, tcp,
               sets.n, incp[0] ? incp : "(fresh ntinit)", budget, run, l.s ? l.s : "");
        sb_free(&l);
        /* both run files derived for real into a scratch file, so a bad --set or fidelity key fails here too */
        char tmp[64];
        snprintf(tmp, sizeof tmp, "/tmp/nether-flywheel-dry-%ld.yaml", (long)getpid());
        int drc = cr_derive("flywheel", tcp, &sets, NULL, tmp, NULL);
        if (!drc) drc = cr_derive("flywheel", ecp, NULL, approx ? NULL : fid, tmp, &fargs);
        if (!drc && !approx) {
            struct sv all = {0};
            for (int i = 0; i < eargs.n; ++i) sv_push(&all, eargs.v[i]);
            for (int i = 0; i < fargs.n; ++i) sv_push(&all, fargs.v[i]);
            drc = cr_refused("flywheel", fid, &all);
            sv_free(&all);
        }
        unlink(tmp);
        for (int i = 0; i < fargs.n; ++i) sv_push(&eargs, fargs.v[i]);
        if (eargs.n) cli_argv_line((const char *const *)eargs.v, &l);
        printf("  eval:  ntloop --run %s/eval.run.yaml (%s%s) --score --ns %s --seed %s%s%s %s\n", run, ecp,
               approx ? ", approximate" : ", exact fidelity", eps, eseed,
               cy_get(y, "eval.minutes", NULL) ? " --minutes " : "", cy_get(y, "eval.minutes", ""), l.s ? l.s : "");
        sb_free(&l);
        sv_free(&targs), sv_free(&eargs), sv_free(&fargs), sv_free(&sets);
        cy_free(y);
        return drc ? 2 : 0;
    }
    if (cli_mkdirs(run)) {
        fprintf(stderr, "nether flywheel: cannot make %s\n", run);
        cy_free(y);
        return 2;
    }
    char b2[PATH_MAX];
    if (lock && (lfd = cli_lock_acquire(cli_abs(lock, b2), 0)) < 0) {
        cy_free(y);
        return 1;
    }
    /* 1. the GPU */
    if (gpu && guard) {
        int mib, util, g = cli_gpu_check_idle(atoi(gpu), max_mib, max_util, gwait, &mib, &util);
        if (g) {
            failed = "gpu-guard", rc = g < 0 ? 1 : 3;
            goto done;
        }
        printf("nether flywheel: GPU %s idle (%d MiB, %d%%)\n", gpu, mib, util);
    }
    /* 2. the build */
    if (cy_get(y, "build_cmd", NULL)) {
        struct sv av = {0};
        if (cli_split_words(cy_get(y, "build_cmd", ""), &av, err, sizeof err)) {
            fprintf(stderr, "nether flywheel: build_cmd: %s\n", err);
            failed = "build", rc = 2;
            goto done;
        }
        char log[PATH_MAX + 16];
        cli_fmt(log, sizeof log, "%s/build.log", run);
        rc = spawn_logged((const char *const *)av.v, NULL, log, cy_num(y, "build_timeout", 3600));
        sv_free(&av);
        if (rc) {
            failed = "build";
            goto done;
        }
    }
    /* 3. the quick gate */
    if (cy_bool(y, "quick_gate", 0)) {
        char self[PATH_MAX + 32], gj[PATH_MAX + 32], log[PATH_MAX + 32];
        cli_fmt(self, sizeof self, "%s/out/bin/nether", cli_root);
        cli_fmt(gj, sizeof gj, "%s/gate.json", run);
        cli_fmt(log, sizeof log, "%s/gate.log", run);
        const char *av[] = {self, "gate", "--quick", "--json", gj, gpu ? "--gpu" : NULL, gpu, NULL};
        if ((rc = spawn_logged(av, NULL, log, 0))) {
            failed = "gate";
            goto done;
        }
    }
    /* 4. train */
    {
        char derived[PATH_MAX + 16], tlog[PATH_MAX + 16], tout[PATH_MAX + 16];
        cli_fmt(derived, sizeof derived, "%s/run.yaml", run);
        cli_fmt(tout, sizeof tout, "%s/train", run);
        cli_fmt(tlog, sizeof tlog, "%s/train.log", run);
        if ((rc = cr_derive("flywheel", tcp, &sets, NULL, derived, NULL))) {
            failed = "config";
            goto done;
        }
        struct cy *t = cy_load(derived, err, sizeof err);
        struct cr_trainer tr;
        rc = cr_trainer_find("flywheel", variant, t, gpu ? gpu : "", !incp[0], &tr);
        cy_free(t);
        if (rc) {
            failed = "trainer";
            goto done;
        }
        if (!incp[0]) {
            /* BASELINE: a fresh net */
            cli_fmt(incp, sizeof incp, "%s/init.ntw", run);
            const char *av[] = {tr.ntinit, incp, cy_get(y, "init_seed", "1"), NULL};
            struct cli_spawn_opts o = {.argv = av, .cwd = cli_root};
            struct cli_spawn_res r;
            if (cli_spawn(&o, &r) || r.rc) {
                failed = "init", rc = r.rc ? r.rc : 127;
                goto done;
            }
            row.base = NAN;
            inc = NULL;
        }
        struct sv av = {0};
        char bs[32];
        snprintf(bs, sizeof bs, "%ld", budget);
        sv_push(&av, tr.bin), sv_push(&av, "--run"), sv_push(&av, derived), sv_push(&av, "--init"), sv_push(&av, incp);
        sv_push(&av, "--env-steps"), sv_push(&av, bs);
        for (int i = 0; i < targs.n; ++i) sv_push(&av, targs.v[i]);
        sv_push(&av, "--out"), sv_push(&av, tout);
        if (gpu) printf("nether flywheel: CUDA_VISIBLE_DEVICES=%s\n", gpu);
        rc = spawn_logged((const char *const *)av.v, gpu, tlog, cy_num(y, "train_timeout", 0));
        sv_free(&av);
        char ip[PATH_MAX + 32], mp[PATH_MAX + 32];
        cli_fmt(ip, sizeof ip, "%s/iters.jsonl", tout);
        cli_fmt(mp, sizeof mp, "%s/model.ntw", tout);
        int nrate = read_iters(ip, &row.sps, &row.nan, &row.steps, &row.wall);
        cli_fmt(row.ckpt, sizeof row.ckpt, "%s", mp);
        if (rc || !cli_exists(mp)) {
            fprintf(stderr, "nether flywheel: training failed (rc %d%s); %s\n", rc, cli_exists(mp) ? "" : ", no model.ntw", tlog);
            failed = "train", rc = rc ? rc : 1;
            goto done;
        }
        printf("nether flywheel: trained %.0f env-steps at %s %.0f env-steps/s, %.0f NaN skips: %s\n", row.steps,
               nrate ? "a median" : "overall (shorter than one log interval: the envs' make included)", row.sps, row.nan, mp);
        /* 5. the exact eval, candidate and incumbent */
        char edrv[PATH_MAX + 32];
        cli_fmt(edrv, sizeof edrv, "%s/eval.run.yaml", run);
        if ((rc = cr_derive("flywheel", ecp, NULL, approx ? NULL : fid, edrv, &fargs))) {
            failed = "eval-config";
            goto done;
        }
        struct sv eav = {0};
        sv_push(&eav, "--ns"), sv_push(&eav, eps);
        if (cy_get(y, "eval.minutes", NULL)) sv_push(&eav, "--minutes"), sv_push(&eav, cy_get(y, "eval.minutes", ""));
        sv_push(&eav, "--seed"), sv_push(&eav, eseed);
        if (cy_get(y, "eval_split", NULL)) {
            char spp[PATH_MAX];
            struct sb s = {0};
            if (cr_path("flywheel", cy_get(y, "eval_split", ""), spp) || ce_split_seeds("flywheel", spp, "eval_seeds", &s)) {
                failed = "eval-split", rc = 2;
                goto done;
            }
            int lib = cy_node(y, "eval.library") != NULL;
            sv_push(&eav, lib ? "--lib-seeds" : "--seeds"), sv_push(&eav, s.s);
            sb_free(&s);
        }
        for (int i = 0; i < eargs.n; ++i) sv_push(&eav, eargs.v[i]);
        for (int i = 0; i < fargs.n; ++i) sv_push(&eav, fargs.v[i]);
        if (!approx && cr_refused("flywheel", fid, &eav)) {
            failed = "eval-fidelity", rc = 2;
            goto done;
        }
        char cache[PATH_MAX];
        cli_fmt(cache, sizeof cache, "%s/out/evalcache", cli_root);
        struct ce_job job = {"flywheel", tr.bin, edrv, &eav, gpu, cache, 0, cy_num(y, "eval_timeout", 0)};
        char cd[PATH_MAX], bd[PATH_MAX];
        int hit;
        if ((rc = ce_eval_model(&job, mp, cd, &hit))) {
            failed = "eval";
            goto done;
        }
        if (inc && (rc = ce_eval_model(&job, incp, bd, &hit))) {
            failed = "eval-incumbent";
            goto done;
        }
        sv_free(&eav);
        /* 6. the verdict */
        char ej[PATH_MAX + 16];
        cli_fmt(ej, sizeof ej, "%s/eval.json", run);
        struct cs_run cr, br;
        struct cs_summary cs, bsum;
        struct cs_compare cmp;
        int hb;
        if ((rc = ce_report(cd, inc ? bd : NULL, ej, &cr, &cs, &br, &bsum, &cmp, &hb))) {
            failed = "report";
            goto done;
        }
        int lower;
        if (cs_metric(&cr, &cs, hb ? &bsum : NULL, hb ? &cmp : NULL, metric, &row.cand, &row.base, &row.delta, &row.p, &row.z,
                      &lower)) {
            fprintf(stderr, "nether flywheel: primary_metric %s is not depth, ret, deaths, raw_share or a milestone of the eval\n",
                    metric);
            failed = "metric", rc = 2;
        } else if (!hb)
            snprintf(row.verdict, sizeof row.verdict, "BASELINE");
        else {
            int better = lower ? row.delta < 0 : row.delta > 0;
            int sig = (!isnan(row.p) && row.p <= max_p) || (!isnan(row.z) && row.z >= min_z);
            int keep = row.nan == 0 && better && sig;
            snprintf(row.verdict, sizeof row.verdict, "%s", keep ? "KEEP" : "REVERT");
            printf("nether flywheel: %s %s: candidate %.6g, incumbent %.6g, delta %+.6g (%s), p %.3g, z %+.2f, NaN skips %.0f\n",
                   row.verdict, metric, row.cand, row.base, row.delta, better ? "better" : "not better", row.p, row.z, row.nan);
            verdict_rc = keep ? 0 : 10;
        }
        if (!hb && !failed)
            printf("nether flywheel: BASELINE %s: %.6g (no incumbent to compare with)\n", metric, row.cand);
        cs_free(&cr);
        if (hb) cs_free(&br);
    }
done:
    if (failed) snprintf(row.verdict, sizeof row.verdict, "ERROR"), snprintf(row.step, sizeof row.step, "%s", failed);
    row.wall = cli_now() - t0;
    if (cli_mkdirs(logdir) == 0) row_write(tsv, jsonl, &row);
    /* the incumbent for the next step */
    if (!failed && (!strcmp(row.verdict, "KEEP") || !strcmp(row.verdict, "BASELINE"))) {
        char lp[PATH_MAX + 32], tmp[PATH_MAX + 48];
        cli_fmt(lp, sizeof lp, "%s/incumbent.ntw", logdir);
        cli_fmt(tmp, sizeof tmp, "%s.%ld", lp, (long)getpid());
        if (symlink(row.ckpt, tmp) || rename(tmp, lp)) fprintf(stderr, "nether flywheel: cannot point %s at %s\n", lp, row.ckpt);
        else printf("nether flywheel: %s -> %s (the next step's incumbent)\n", lp, row.ckpt);
    }
    printf("nether flywheel: %s%s%s in %.0f s; a row in %s and %s\n", row.verdict, failed ? " at " : "", failed ? failed : "",
           row.wall, tsv, jsonl);
    cli_lock_release(lfd);
    sv_free(&targs), sv_free(&eargs), sv_free(&fargs), sv_free(&sets);
    cy_free(y);
    if (failed) return rc ? rc : 1;
    return verdict_rc;
}
