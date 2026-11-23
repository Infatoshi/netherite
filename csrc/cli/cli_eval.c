/* nether eval: a model's frozen eval and its exact comparison with another (cli_verbs.h, cli_stats.h).
 *
 *   nether eval --dir CAND_DIR [--compare-dir BASE_DIR] [--json OUT.json]
 *       two eval directories already on disk (ntloop --score --out DIR's episodes.jsonl), compared offline
 *   nether eval RUN.yaml --model CAND.ntw [--compare BASE.ntw] [--split SEEDS] [--episodes N] [--minutes M]
 *       [--seed S] [--variant V] [--gpu G [--gpu-guard] [--gpu-wait S]] [--lock FILE] [--allow-approx]
 *       [--fidelity FILE] [--set KEY=VALUE ...] [--out DIR] [--json OUT.json] [--no-cache] [-- NTLOOP FLAGS]
 *       ntloop --score for each model (posix_spawn), then the same comparison; OUT/eval.json */
#define _GNU_SOURCE
#include "cli_json.h"
#include "cli_proc.h"
#include "cli_run.h"
#include "cli_verbs.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int ce_split_seeds(const char *verb, const char *path, const char *key, struct sb *out)
{
    char *s = cli_read_file(path, NULL);
    if (!s) {
        fprintf(stderr, "nether %s: cannot read the split %s\n", verb, path);
        return 2;
    }
    int n = 0;
    const char *p = s;
    while (*p == ' ' || *p == '\n' || *p == '\t') ++p;
    if (*p == '{') {
        char err[200];
        struct jv *v = jv_parse(s, err, sizeof err);
        const struct jv *a = v ? jv_get(v, key) : NULL;
        if (!a || a->t != JV_ARR) {
            fprintf(stderr, "nether %s: %s has no \"%s\" list%s%s\n", verb, path, key, v ? "" : ": ", v ? "" : err);
            jv_free(v);
            free(s);
            return 2;
        }
        for (int i = 0; i < a->n; ++i) sb_printf(out, "%s%.0f", n++ ? "," : "", a->kids[i]->num);
        jv_free(v);
    } else {
        for (char *q = s, *e; *q; q = e) {
            e = strchr(q, '\n');
            if (e) *e++ = 0;
            else e = q + strlen(q);
            char *h = strchr(q, '#');
            if (h) *h = 0;
            while (*q == ' ' || *q == '\t') ++q;
            size_t l = strcspn(q, " \t\r");
            q[l] = 0;
            if (!*q) continue;
            if (q[strspn(q, "0123456789-")]) {
                fprintf(stderr, "nether %s: %s: '%s' is not a seed or A-B range\n", verb, path, q);
                free(s);
                return 2;
            }
            sb_printf(out, "%s%s", n++ ? "," : "", q);
        }
    }
    free(s);
    if (!n) {
        fprintf(stderr, "nether %s: the split %s names no seeds\n", verb, path);
        return 2;
    }
    sb_str(out);
    return 0;
}

int ce_eval_model(const struct ce_job *j, const char *model, char *dir, int *hit)
{
    *hit = 0;
    char ms[65], bs[65];
    if (cli_sha256_file(model, ms)) {
        fprintf(stderr, "nether %s: cannot read the model %s\n", j->verb, model);
        return 2;
    }
    if (cli_sha256_file(j->bin, bs)) {
        fprintf(stderr, "nether %s: cannot read the trainer %s\n", j->verb, j->bin);
        return 2;
    }
    char *run = cli_read_file(j->run, NULL);
    if (!run) {
        fprintf(stderr, "nether %s: cannot read %s\n", j->verb, j->run);
        return 2;
    }
    /* the key: the model, the binary, the run file's content (its "derived from" line aside) and the flags */
    struct sb k = {0};
    const char *body = run[0] == '#' && strchr(run, '\n') ? strchr(run, '\n') + 1 : run;
    sb_printf(&k, "model %s\nbin %s\nrun\n%s\nargs", ms, bs, body);
    for (int i = 0; j->args && i < j->args->n; ++i) sb_printf(&k, " %s", j->args->v[i]);
    uint8_t d[32];
    cli_sha256(k.s, k.n, d);
    char key[65];
    cli_hex(d, 32, key);
    key[24] = 0;
    sb_free(&k);
    free(run);
    cli_fmt(dir, PATH_MAX, "%s/%s", j->cache, key);
    char ep[PATH_MAX + 32], meta[PATH_MAX + 32];
    cli_fmt(ep, sizeof ep, "%s/episodes.jsonl", dir);
    cli_fmt(meta, sizeof meta, "%s/eval.meta.json", dir);
    if (!j->no_cache && cli_exists(ep) && cli_exists(meta)) {
        *hit = 1;
        printf("nether %s: %s: cached eval %s (model %.12s, trainer %.12s)\n", j->verb, model, dir, ms, bs);
        return 0;
    }
    if (cli_mkdirs(j->cache)) {
        fprintf(stderr, "nether %s: cannot make %s\n", j->verb, j->cache);
        return 2;
    }
    char tmp[PATH_MAX + 32], sj[PATH_MAX + 64], log[PATH_MAX + 64];
    cli_fmt(tmp, sizeof tmp, "%s.tmp%ld", dir, (long)getpid());
    if (cli_mkdirs(tmp)) {
        fprintf(stderr, "nether %s: cannot make %s\n", j->verb, tmp);
        return 2;
    }
    cli_fmt(sj, sizeof sj, "%s/score.json", tmp);
    cli_fmt(log, sizeof log, "%s/ntloop.log", tmp);
    struct sv av = {0};
    sv_push(&av, j->bin);
    sv_push(&av, "--run");
    sv_push(&av, j->run);
    sv_push(&av, "--score");
    sv_push(&av, "--init");
    sv_push(&av, model);
    for (int i = 0; j->args && i < j->args->n; ++i) sv_push(&av, j->args->v[i]);
    sv_push(&av, "--out");
    sv_push(&av, tmp);
    sv_push(&av, "--score-json");
    sv_push(&av, sj);
    char gv[64];
    const char *env[2] = {NULL, NULL};
    if (j->gpu) cli_fmt(gv, sizeof gv, "CUDA_VISIBLE_DEVICES=%s", j->gpu), env[0] = gv;
    struct cli_spawn_opts o = {.argv = (const char *const *)av.v, .cwd = cli_root, .env_set = env, .out_path = log, .tee = 1,
                               .timeout_s = j->timeout_s, .stdin_null = 1};
    struct cli_spawn_res r;
    if (j->gpu) printf("nether %s: CUDA_VISIBLE_DEVICES=%s\n", j->verb, j->gpu);
    int sp = cli_spawn(&o, &r);
    sv_free(&av);
    char tep[PATH_MAX + 32];
    cli_fmt(tep, sizeof tep, "%s/episodes.jsonl", tmp);
    if (sp || r.rc || !cli_exists(tep) || !cli_exists(sj)) {
        fprintf(stderr, "nether %s: ntloop --score failed for %s (rc %d%s); its log: %s\n", j->verb, model, r.rc,
                r.timed_out ? ", timed out" : "", log);
        return r.rc ? r.rc : 1;
    }
    struct sb m = {0};
    char stamp[40];
    cli_stamp(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%S%z");
    sb_puts(&m, "{\"model\": ");
    cli_json_str(&m, model);
    sb_printf(&m, ", \"model_sha256\": \"%s\", \"trainer\": ", ms);
    cli_json_str(&m, j->bin);
    sb_printf(&m, ", \"trainer_sha256\": \"%s\", \"key\": \"%s\", \"made\": \"%s\", \"wall\": %.1f}\n", bs, key, stamp, r.wall);
    char tm[PATH_MAX + 64];
    cli_fmt(tm, sizeof tm, "%s/eval.meta.json", tmp);
    int bad = cli_write_file_atomic(tm, m.s, m.n);
    sb_free(&m);
    if (bad) return 2;
    if (cli_exists(dir)) {
        /* a stale entry (no meta): kept aside, never deleted */
        char old[PATH_MAX + 48];
        cli_fmt(old, sizeof old, "%s.old%ld", dir, (long)getpid());
        rename(dir, old);
    }
    if (rename(tmp, dir)) {
        fprintf(stderr, "nether %s: cannot move %s to %s\n", j->verb, tmp, dir);
        return 2;
    }
    return 0;
}

int ce_report(const char *cand_dir, const char *base_dir, const char *json, struct cs_run *cr, struct cs_summary *cs,
              struct cs_run *br, struct cs_summary *bs, struct cs_compare *cmp, int *have_base)
{
    char err[512];
    *have_base = 0;
    double t0 = cli_now();
    int bad = cs_load(cand_dir, cr, err, sizeof err);
    cli_prof_add(CLI_P_JSON, t0);
    if (bad) {
        fprintf(stderr, "nether eval: %s\n", err);
        return 2;
    }
    t0 = cli_now();
    cs_summarize(cr, cs);
    cli_prof_add(CLI_P_STATS, t0);
    if (base_dir) {
        t0 = cli_now();
        bad = cs_load(base_dir, br, err, sizeof err);
        cli_prof_add(CLI_P_JSON, t0);
        if (bad) {
            fprintf(stderr, "nether eval: %s\n", err);
            return 2;
        }
        t0 = cli_now();
        cs_summarize(br, bs);
        bad = cs_compare_runs(cr, br, cmp);
        cli_prof_add(CLI_P_STATS, t0);
        if (bad) {
            fprintf(stderr, "nether eval: no episode of %s pairs with one of %s (env ids)\n", cand_dir, base_dir);
            return 2;
        }
        *have_base = 1;
    }
    cs_print(stdout, cr, cs, *have_base ? br : NULL, bs, *have_base ? cmp : NULL);
    if (json) {
        struct sb o = {0};
        cs_json(&o, cr, cs, *have_base ? br : NULL, bs, *have_base ? cmp : NULL);
        if (cli_write_file_atomic(json, o.s, o.n)) {
            fprintf(stderr, "nether eval: cannot write %s\n", json);
            sb_free(&o);
            return 2;
        }
        sb_free(&o);
        printf("nether eval: %s\n", json);
    }
    return 0;
}

static void usage(void)
{
    fprintf(stderr, "nether eval --dir CAND_DIR [--compare-dir BASE_DIR] [--json OUT]\n"
                    "nether eval RUN.yaml --model CAND.ntw [--compare BASE.ntw] [--split FILE] [--episodes N] [--minutes M]\n"
                    "    [--seed S] [--variant V] [--gpu G [--gpu-guard] [--gpu-wait S]] [--lock FILE] [--allow-approx]\n"
                    "    [--fidelity FILE] [--set KEY=VALUE ...] [--out DIR] [--json OUT] [--no-cache] [-- NTLOOP FLAGS]\n"
                    "(nether eval --help)\n");
}

int v_eval(int argc, char **argv)
{
    const char *dir = NULL, *cdir = NULL, *json = NULL, *run = NULL, *model = NULL, *base = NULL, *split = NULL;
    const char *variant = NULL, *gpu = NULL, *lock = NULL, *out = NULL, *fid = "configs/fidelity.yaml";
    const char *episodes = NULL, *minutes = NULL, *seed = NULL, *cache = NULL;
    int approx = 0, guard = 0, nocache = 0, max_mib = 500, max_util = 5;
    double gwait = 0, timeout = 0;
    struct sv sets = {0}, extra = {0};
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!strcmp(a, "--")) {
            for (++i; i < argc; ++i) sv_push(&extra, argv[i]);
            break;
        }
#define V() cli_arg(argc, argv, &i, "eval")
        if (!strcmp(a, "--dir")) dir = V();
        else if (!strcmp(a, "--compare-dir")) cdir = V();
        else if (!strcmp(a, "--json")) json = V();
        else if (!strcmp(a, "--model")) model = V();
        else if (!strcmp(a, "--compare")) base = V();
        else if (!strcmp(a, "--split")) split = V();
        else if (!strcmp(a, "--episodes")) episodes = V();
        else if (!strcmp(a, "--minutes")) minutes = V();
        else if (!strcmp(a, "--seed")) seed = V();
        else if (!strcmp(a, "--variant")) variant = V();
        else if (!strcmp(a, "--gpu")) gpu = V();
        else if (!strcmp(a, "--gpu-guard")) guard = 1;
        else if (!strcmp(a, "--gpu-wait")) gwait = atof(V());
        else if (!strcmp(a, "--gpu-max-mib")) max_mib = atoi(V());
        else if (!strcmp(a, "--gpu-max-util")) max_util = atoi(V());
        else if (!strcmp(a, "--lock")) lock = V();
        else if (!strcmp(a, "--allow-approx")) approx = 1;
        else if (!strcmp(a, "--fidelity")) fid = V();
        else if (!strcmp(a, "--set")) sv_push(&sets, V());
        else if (!strcmp(a, "--out")) out = V();
        else if (!strcmp(a, "--cache")) cache = V();
        else if (!strcmp(a, "--no-cache")) nocache = 1;
        else if (!strcmp(a, "--timeout")) timeout = atof(V());
        else if (a[0] == '-') {
            fprintf(stderr, "nether eval: unknown flag %s (ntloop's go after --)\n", a);
            usage();
            return 2;
        } else if (!run) run = a;
        else {
            fprintf(stderr, "nether eval: one run file (got %s and %s)\n", run, a);
            return 2;
        }
#undef V
    }
    struct cs_run cr, br;
    struct cs_summary cs, bs;
    struct cs_compare cmp;
    int hb = 0, rc;
    /* the offline form */
    if (dir) {
        if (run || model) {
            fprintf(stderr, "nether eval: --dir compares directories on disk; it takes no run file or model\n");
            return 2;
        }
        rc = ce_report(dir, cdir, json, &cr, &cs, &br, &bs, &cmp, &hb);
        cs_free(&cr);
        if (cdir) cs_free(&br);
        return rc;
    }
    if (!run || !model) {
        usage();
        return 2;
    }
    if (gpu && (!*gpu || gpu[strspn(gpu, "0123456789")])) {
        fprintf(stderr, "nether eval: --gpu takes one GPU index\n");
        return 2;
    }
    char runabs[PATH_MAX], mabs[PATH_MAX], babs[PATH_MAX], fabs_[PATH_MAX], sabs[PATH_MAX];
    if ((rc = cr_path("eval", run, runabs)) || (rc = cr_path("eval", model, mabs))) return rc;
    if (base && (rc = cr_path("eval", base, babs))) return rc;
    if (split && (rc = cr_path("eval", split, sabs))) return rc;
    if (!approx) {
        if ((rc = cr_path("eval", fid, fabs_))) return rc;
        if ((rc = cr_refused("eval", fabs_, &extra))) return rc;
    }
    /* the size and the sampler's seed: the flags, else the fidelity file's eval section (C holds no default) */
    char ebuf[32], sbuf[32];
    if (!episodes && (rc = cr_eval_default("eval", fid, "episodes", ebuf, sizeof ebuf))) return rc;
    if (!seed && (rc = cr_eval_default("eval", fid, "seed", sbuf, sizeof sbuf))) return rc;
    if (!episodes) episodes = ebuf;
    if (!seed) seed = sbuf;
    /* the output directory */
    char od[PATH_MAX], b2[PATH_MAX];
    if (out) cli_abs(out, od);
    else {
        char stamp[32], name[128];
        cli_stamp(stamp, sizeof stamp, "%Y%m%d-%H%M%S");
        const char *s = strrchr(runabs, '/');
        cli_fmt(name, sizeof name, "%s", s ? s + 1 : runabs);
        char *dot = strrchr(name, '.');
        if (dot) *dot = 0;
        cli_fmt(od, sizeof od, "%s/out/eval/%s-%s", cli_root, name, stamp);
    }
    if (cli_mkdirs(od)) {
        fprintf(stderr, "nether eval: cannot make %s\n", od);
        return 2;
    }
    char derived[PATH_MAX + 16];
    cli_fmt(derived, sizeof derived, "%s/run.yaml", od);
    struct sv fargs = {0};
    if ((rc = cr_derive("eval", runabs, &sets, approx ? NULL : fabs_, derived, &fargs))) return rc;
    char err[512];
    struct cy *t = cy_load(derived, err, sizeof err);
    if (!t) {
        fprintf(stderr, "nether eval: %s\n", err);
        return 2;
    }
    struct cr_trainer tr;
    rc = cr_trainer_find("eval", variant, t, gpu ? gpu : "", 0, &tr);
    int library = cy_node(t, "policy.starts.library") != NULL;
    cy_free(t);
    if (rc) return rc;
    for (int i = 0; i < extra.n; ++i)
        if (!strcmp(extra.v[i], "--library")) library = 1;
    /* ntloop's flags: the episodes, the minutes, the sampler's seed, the split, then the caller's, then the fidelity's */
    struct sv args = {0};
    sv_push(&args, "--ns");
    sv_push(&args, episodes);
    if (minutes) sv_push(&args, "--minutes"), sv_push(&args, minutes);
    sv_push(&args, "--seed");
    sv_push(&args, seed);
    if (split) {
        struct sb s = {0};
        if ((rc = ce_split_seeds("eval", sabs, "eval_seeds", &s))) return rc;
        sv_push(&args, library ? "--lib-seeds" : "--seeds");
        sv_push(&args, s.s);
        sb_free(&s);
    }
    for (int i = 0; i < extra.n; ++i) sv_push(&args, extra.v[i]);
    for (int i = 0; i < fargs.n; ++i) sv_push(&args, fargs.v[i]);
    /* the GPU, the lock */
    int lfd = -1;
    if (lock && (lfd = cli_lock_acquire(cli_abs(lock, b2), 0)) < 0) return 1;
    if (gpu && guard) {
        int mib, util;
        int g = cli_gpu_check_idle(atoi(gpu), max_mib, max_util, gwait, &mib, &util);
        if (g) {
            cli_lock_release(lfd);
            return g < 0 ? 1 : 3;
        }
        printf("nether eval: GPU %s idle (%d MiB, %d%%)\n", gpu, mib, util);
    }
    char cdirb[PATH_MAX];
    if (cache) cli_abs(cache, cdirb);
    else cli_fmt(cdirb, sizeof cdirb, "%s/out/evalcache", cli_root);
    struct ce_job job = {"eval", tr.bin, derived, &args, gpu, cdirb, nocache, timeout};
    char cd[PATH_MAX], bd[PATH_MAX];
    int hit;
    rc = ce_eval_model(&job, mabs, cd, &hit);
    if (!rc && base) rc = ce_eval_model(&job, babs, bd, &hit);
    cli_lock_release(lfd);
    sv_free(&args);
    sv_free(&fargs);
    sv_free(&sets);
    sv_free(&extra);
    if (rc) return rc;
    /* OUT/cand and OUT/base name the cached directories */
    char ln[PATH_MAX + 16];
    cli_fmt(ln, sizeof ln, "%s/cand", od);
    unlink(ln);
    if (symlink(cd, ln)) fprintf(stderr, "nether eval: cannot link %s\n", ln);
    if (base) {
        cli_fmt(ln, sizeof ln, "%s/base", od);
        unlink(ln);
        if (symlink(bd, ln)) fprintf(stderr, "nether eval: cannot link %s\n", ln);
    }
    char jp[PATH_MAX + 16];
    if (json) cli_abs(json, jp);
    else cli_fmt(jp, sizeof jp, "%s/eval.json", od);
    rc = ce_report(cd, base ? bd : NULL, jp, &cr, &cs, &br, &bs, &cmp, &hb);
    cs_free(&cr);
    if (base) cs_free(&br);
    return rc;
}

/* nether stats: the engine's formulas on numbers given, for checking them against another implementation */
int v_stats(int argc, char **argv)
{
    if (argc >= 4 && !strcmp(argv[1], "mcnemar")) {
        long b, c;
        if (cli_long(argv[2], &b) || cli_long(argv[3], &c) || b < 0 || c < 0) return fprintf(stderr, "nether stats mcnemar B C\n"), 2;
        printf("%.17g\n", cs_mcnemar_exact(b, c));
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "wilson")) {
        long k, n;
        double lo, hi;
        if (cli_long(argv[2], &k) || cli_long(argv[3], &n) || k < 0 || n <= 0 || k > n) return fprintf(stderr, "nether stats wilson K N\n"), 2;
        cs_wilson(k, n, &lo, &hi);
        printf("%.17g %.17g\n", lo, hi);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "normp")) {
        double z;
        if (cli_num(argv[2], &z)) return fprintf(stderr, "nether stats normp Z\n"), 2;
        printf("%.17g\n", cs_norm_p2(z));
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "paired")) {
        /* a file of "cand base" pairs, one a line: the paired mean difference, its se, z and p */
        char *s = cli_read_file(argv[2], NULL);
        if (!s) return fprintf(stderr, "nether stats paired: cannot read %s\n", argv[2]), 2;
        int n = 0, cap = 0;
        double *d = NULL;
        for (char *p = s; *p;) {
            double x, y;
            int used;
            if (sscanf(p, " %lf %lf%n", &x, &y, &used) != 2) break;
            if (n == cap) cap = cap ? cap * 2 : 1024, d = xrealloc(d, sizeof *d * (size_t)cap);
            d[n++] = x - y;
            p += used;
        }
        free(s);
        if (n < 2) return fprintf(stderr, "nether stats paired: fewer than two pairs\n"), free(d), 2;
        double m = 0, ss = 0;
        for (int i = 0; i < n; ++i) m += d[i];
        m /= n;
        for (int i = 0; i < n; ++i) ss += (d[i] - m) * (d[i] - m);
        double se = sqrt(ss / (n - 1)) / sqrt((double)n), z = m / se;
        printf("%d %.17g %.17g %.17g %.17g\n", n, m, se, z, cs_norm_p2(z));
        free(d);
        return 0;
    }
    fprintf(stderr, "nether stats mcnemar B C | wilson K N | normp Z | paired FILE\n");
    return 2;
}
