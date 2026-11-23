/* nether gate: the checks of a declarative file (configs/gates.yaml), one mode's set, run by posix_spawn with a
 * JSON pass/fail manifest (cli_verbs.h).
 *
 * The file: protect_links (paths that must stay the symlinks they are: a build in a lane replaces out/assets with
 * a directory of its own; the gate puts the link back and moves the directory into its logs), and checks, each:
 *   modes        the modes that run it (quick, full, java, default: no flag), comma-separated
 *   cmd          the command, split into words (no shell); {gpu} {root} {pin_args} substituted first
 *   gpu          yes: runs only with --gpu G (CUDA_VISIBLE_DEVICES=G); every other check sees no GPU
 *   if_missing, first   run first once when the path if_missing does not exist (a recording made on demand)
 *   require      a regex the output must match
 *   unset_env    names taken out of the environment (CACHE: the oracle's decomp/src.sh)
 *   env          NAME=VALUE words added
 *   timeout      seconds (default 3600)
 *   build        yes: a build, skipped by --no-build (the tree already built: the checks alone, in seconds)
 *   builtin      macro-freeze (no edit, staged, unstaged or untracked, under path; and none since --check-macro-freeze
 *                REF), pinned-digest (pins: a TSV, pin: its row; the row's arguments as {pin_args}, then the
 *                trajectory and observation digests printed must be the row's) */
#define _GNU_SOURCE
#include "cli_proc.h"
#include "cli_verbs.h"
#include "cli_yaml.h"

#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct link_guard {
    char path[PATH_MAX], target[PATH_MAX];
};

static int has_mode(const char *modes, const char *mode)
{
    for (const char *p = modes; *p;) {
        size_t n = strcspn(p, ",+ ");
        if (n == strlen(mode) && !strncmp(p, mode, n)) return 1;
        p += n;
        while (*p == ',' || *p == '+' || *p == ' ') ++p;
    }
    return 0;
}

static int in_list(const char *list, const char *name)
{
    return !list || has_mode(list, name);
}

/* every guarded link that is no longer its link: its replacement moved into dir, the link made again */
static void restore_links(struct link_guard *g, int n, const char *logdir, struct sb *notes)
{
    for (int i = 0; i < n; ++i) {
        char t[PATH_MAX];
        ssize_t k = readlink(g[i].path, t, sizeof t - 1);
        if (k >= 0) {
            t[k] = 0;
            if (!strcmp(t, g[i].target)) continue;
        }
        struct stat st;
        if (!lstat(g[i].path, &st)) {
            static int seq;
            const char *base = strrchr(g[i].path, '/') ? strrchr(g[i].path, '/') + 1 : g[i].path;
            char *aside = xasprintf("%s/replaced-%s.%d", logdir, base, ++seq);
            if (rename(g[i].path, aside)) {
                fprintf(stderr, "nether gate: cannot move %s aside to %s\n", g[i].path, aside);
                free(aside);
                continue;
            }
            printf("nether gate: %s had been replaced (a build made it a %s): moved to %s, the link put back\n", g[i].path,
                   S_ISDIR(st.st_mode) ? "directory" : "file", aside);
            free(aside);
        }
        if (symlink(g[i].target, g[i].path)) fprintf(stderr, "nether gate: cannot link %s -> %s\n", g[i].path, g[i].target);
        else sb_printf(notes, "%s%s", notes->n ? "," : "", g[i].path);
    }
}

static int run_capture(const char *const *av, const char *const *env, const char *const *unset, const char *log, double timeout,
                       struct sb *out, struct cli_spawn_res *r)
{
    struct cli_spawn_opts o = {.argv = av, .cwd = cli_root, .env_set = env, .env_unset = unset, .out_path = log, .capture = out,
                               .timeout_s = timeout, .new_pgroup = 1, .stdin_null = 1, .quiet = 1};
    return cli_spawn(&o, r);
}

/* the macro freeze: 0 pass, 1 fail; detail written */
static int macro_freeze(const char *path, const char *ref, const char *log, char *detail, size_t dn)
{
    struct sb o = {0}, all = {0};
    struct cli_spawn_res r;
    const char *st[] = {"git", "status", "--porcelain", "--", path, NULL};
    if (run_capture(st, NULL, NULL, NULL, 60, &o, &r) || r.rc) {
        snprintf(detail, dn, "git status failed (rc %d)", r.rc);
        sb_free(&o);
        return 1;
    }
    int bad = o.n > 0;
    sb_printf(&all, "$ git status --porcelain -- %s\n%s", path, sb_str(&o));
    int nlines = 0;
    for (const char *p = o.s; p && *p; ++p) nlines += *p == '\n';
    sb_free(&o);
    int since = 0;
    if (ref) {
        const char *df[] = {"git", "diff", "--stat", ref, "--", path, NULL};
        if (run_capture(df, NULL, NULL, NULL, 60, &o, &r) || r.rc) {
            snprintf(detail, dn, "git diff %s failed (rc %d)", ref, r.rc);
            sb_free(&o), sb_free(&all);
            return 1;
        }
        since = o.n > 0;
        sb_printf(&all, "$ git diff --stat %s -- %s\n%s", ref, path, sb_str(&o));
        sb_free(&o);
    }
    if (bad) snprintf(detail, dn, "%d uncommitted or untracked changes under %s", nlines, path);
    else if (since) snprintf(detail, dn, "%s differs from %s", path, ref);
    else snprintf(detail, dn, "no edit under %s%s%s", path, ref ? " since " : "", ref ? ref : "");
    if (log) cli_write_file_atomic(log, all.s ? all.s : "", all.n);
    sb_free(&all);
    return bad || since;
}

/* gates.tsv's row NAME: its arguments, trajectory and observation digests */
static int pin_row(const char *tsv, const char *name, char **args, char *traj, char *obs, char *err, size_t en)
{
    char *s = cli_read_file(tsv, NULL);
    if (!s) {
        snprintf(err, en, "cannot read %.400s", tsv);
        return -1;
    }
    int found = 0;
    for (char *p = s, *e; *p && !found; p = e) {
        e = strchr(p, '\n');
        if (e) *e++ = 0;
        else e = p + strlen(p);
        if (*p == '#') continue;
        char *f[5] = {0};
        int k = 0;
        f[k++] = p;
        for (char *q = p; *q && k < 5; ++q)
            if (*q == '\t') *q = 0, f[k++] = q + 1;
        if (k >= 4 && !strcmp(f[0], name)) {
            *args = xstrdup(f[1]);
            snprintf(traj, 64, "%s", f[2]);
            snprintf(obs, 64, "%s", f[3]);
            found = 1;
        }
    }
    free(s);
    if (!found) snprintf(err, en, "%.300s has no row %.100s", tsv, name);
    return found ? 0 : -1;
}

static void word_after(const char *s, const char *key, char *out, size_t n)
{
    *out = 0;
    const char *p = NULL, *q = s;
    while ((q = strstr(q, key))) p = q, q += strlen(key);
    if (!p) return;
    p += strlen(key);
    snprintf(out, n, "%.*s", (int)strcspn(p, " \n"), p);
}

int v_gate(int argc, char **argv)
{
    const char *mode = "default", *json = NULL, *gpu = NULL, *conf = "configs/gates.yaml", *only = NULL, *ref = NULL;
    int list = 0, keep = 1, no_build = 0;
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!strcmp(a, "--quick")) mode = "quick";
        else if (!strcmp(a, "--no-build")) no_build = 1;
        else if (!strcmp(a, "--full")) mode = "full";
        else if (!strcmp(a, "--java")) mode = "java";
        else if (!strcmp(a, "--json")) json = cli_arg(argc, argv, &i, "gate");
        else if (!strcmp(a, "--gpu")) gpu = cli_arg(argc, argv, &i, "gate");
        else if (!strcmp(a, "--config")) conf = cli_arg(argc, argv, &i, "gate");
        else if (!strcmp(a, "--only")) only = cli_arg(argc, argv, &i, "gate");
        else if (!strcmp(a, "--check-macro-freeze")) ref = cli_arg(argc, argv, &i, "gate");
        else if (!strcmp(a, "--list")) list = 1;
        else if (!strcmp(a, "--stop")) keep = 0;
        else {
            fprintf(stderr, "nether gate: unknown argument %s (nether gate --help)\n", a);
            return 2;
        }
    }
    if (gpu && (!*gpu || gpu[strspn(gpu, "0123456789")])) {
        fprintf(stderr, "nether gate: --gpu takes one GPU index\n");
        return 2;
    }
    char cpath[PATH_MAX], err[512];
    cli_resolve(conf, cpath);
    struct cy *y = cy_load(cpath, err, sizeof err);
    if (!y) {
        fprintf(stderr, "nether gate: %s\n", err);
        return 2;
    }
    const struct cy *checks = cy_node(y, "checks");
    if (!checks || !checks->nk) {
        fprintf(stderr, "nether gate: %s has no checks\n", cpath);
        cy_free(y);
        return 2;
    }
    if (list) {
        for (int i = 0; i < checks->nk; ++i) {
            const struct cy *c = checks->kid[i];
            printf("%-24s %-22s %s%s\n", c->key, cy_get(c, "modes", "-"), cy_get(c, "builtin", cy_get(c, "cmd", "")),
                   cy_bool(c, "gpu", 0) ? "  (gpu)" : "");
        }
        cy_free(y);
        return 0;
    }
    char stamp[32], made[40], sha[32];
    cli_stamp(stamp, sizeof stamp, "%Y%m%d-%H%M%S");
    cli_stamp(made, sizeof made, "%Y-%m-%dT%H:%M:%S%z");
    cli_git_sha(sha, sizeof sha);
    char logdir[PATH_MAX];
    cli_fmt(logdir, sizeof logdir, "%s/out/gate/%s-%s", cli_root, mode, stamp);
    if (cli_mkdirs(logdir)) {
        fprintf(stderr, "nether gate: cannot make %s\n", logdir);
        cy_free(y);
        return 2;
    }
    /* the links to keep */
    struct link_guard guards[16];
    int ng = 0;
    const char *pl = cy_get(y, "protect_links", "");
    for (const char *p = pl; *p && ng < 16;) {
        size_t n = strcspn(p, ", ");
        if (n) {
            cli_fmt(guards[ng].path, sizeof guards[ng].path, "%s/%.*s", cli_root, (int)n, p);
            ssize_t k = readlink(guards[ng].path, guards[ng].target, sizeof guards[ng].target - 1);
            if (k > 0) guards[ng].target[k] = 0, ++ng;
        }
        p += n;
        while (*p == ',' || *p == ' ') ++p;
    }
    printf("nether gate: %s mode, %s, commit %s%s%s; logs in %s\n", mode, cpath, sha, gpu ? ", GPU " : ", no GPU", gpu ? gpu : "",
           logdir);
    struct sb js = {0}, restored = {0};
    sb_printf(&js, "{\"mode\": \"%s\", \"commit\": \"%s\", \"made\": \"%s\", \"config\": ", mode, sha, made);
    cli_json_str(&js, cpath);
    sb_puts(&js, ", \"gpu\": ");
    if (gpu) cli_json_str(&js, gpu);
    else sb_puts(&js, "null");
    sb_puts(&js, ", \"checks\": [");
    int fail = 0, ran = 0, nchk = 0;
    double t0 = cli_now();
    for (int i = 0; i < checks->nk; ++i) {
        const struct cy *c = checks->kid[i];
        if (!c->key || !has_mode(cy_get(c, "modes", ""), mode) || !in_list(only, c->key)) continue;
        int needs_gpu = cy_bool(c, "gpu", 0);
        const char *builtin = cy_get(c, "builtin", NULL), *cmd = cy_get(c, "cmd", NULL);
        char log[PATH_MAX + 64], detail[512] = "";
        cli_fmt(log, sizeof log, "%s/%s.log", logdir, c->key);
        const char *status = "pass";
        int rc = 0;
        double ts = cli_now();
        printf("nether gate: %-24s ", c->key);
        fflush(stdout);
        if (fail && !keep) status = "skip", snprintf(detail, sizeof detail, "an earlier check failed (--stop)");
        else if (no_build && cy_bool(c, "build", 0)) status = "skip", snprintf(detail, sizeof detail, "a build (--no-build)");
        else if (needs_gpu && !gpu) status = "skip", snprintf(detail, sizeof detail, "needs --gpu G");
        else if (builtin && !strcmp(builtin, "macro-freeze")) {
            rc = macro_freeze(cy_get(c, "path", "csrc/mods/macros"), ref, log, detail, sizeof detail);
            if (rc) status = "fail";
        } else if (cmd) {
            /* the environment: no GPU unless the check needs one, the file's env and unset_env */
            struct sv env = {0}, unset = {0};
            if (needs_gpu) sv_pushf(&env, "CUDA_VISIBLE_DEVICES=%s", gpu);
            else sv_push(&env, "CUDA_VISIBLE_DEVICES=");
            char e2[512];
            if (cy_get(c, "env", NULL) && cli_split_words(cy_get(c, "env", ""), &env, e2, sizeof e2)) {
                status = "fail", rc = 2, snprintf(detail, sizeof detail, "env: %.400s", e2);
            }
            for (const char *p = cy_get(c, "unset_env", ""); *p;) {
                size_t n = strcspn(p, ", ");
                if (n) {
                    char *w = xstrndup(p, n);
                    sv_push(&unset, w);
                    free(w);
                }
                p += n;
                while (*p == ',' || *p == ' ') ++p;
            }
            char *pin_args = NULL, traj[64] = "", obs[64] = "";
            if (builtin && !strcmp(builtin, "pinned-digest")) {
                char pins[PATH_MAX];
                cli_resolve(cy_get(c, "pins", "csrc/runtime/gates.tsv"), pins);
                if (pin_row(pins, cy_get(c, "pin", ""), &pin_args, traj, obs, e2, sizeof e2))
                    status = "fail", rc = 2, snprintf(detail, sizeof detail, "%s", e2);
            }
            const char *keys[3] = {"gpu", "root", "pin_args"}, *vals[3] = {gpu ? gpu : "", cli_root, pin_args ? pin_args : ""};
            /* first, when its path is missing */
            const char *im = cy_get(c, "if_missing", NULL), *first = cy_get(c, "first", NULL);
            char imp[PATH_MAX];
            if (!strcmp(status, "pass") && im && first && !cli_exists(cli_resolve(im, imp))) {
                /* the recording's directory: the oracle writes into it but does not make it */
                char *sl = strrchr(imp, '/');
                if (sl) *sl = 0, cli_mkdirs(imp), *sl = '/';
                char *f = cli_subst(first, keys, vals, 3);
                struct sv av = {0};
                if (cli_split_words(f, &av, e2, sizeof e2)) status = "fail", rc = 2, snprintf(detail, sizeof detail, "first: %.400s", e2);
                else {
                    char flog[PATH_MAX + 80];
                    cli_fmt(flog, sizeof flog, "%s/%s.first.log", logdir, c->key);
                    struct cli_spawn_res r;
                    if (run_capture((const char *const *)av.v, (const char *const *)env.v, (const char *const *)unset.v, flog,
                                    cy_num(c, "timeout", 3600), NULL, &r) || r.rc)
                        status = "fail", rc = r.rc ? r.rc : 127,
                        snprintf(detail, sizeof detail, "first (%.150s) failed rc %d: %.300s", im, rc, flog);
                }
                sv_free(&av);
                free(f);
            }
            if (!strcmp(status, "pass")) {
                char *x = cli_subst(cmd, keys, vals, 3);
                struct sv av = {0};
                if (cli_split_words(x, &av, e2, sizeof e2)) status = "fail", rc = 2, snprintf(detail, sizeof detail, "cmd: %.400s", e2);
                else {
                    struct sb out = {0};
                    struct cli_spawn_res r;
                    int sp = run_capture((const char *const *)av.v, (const char *const *)env.v, (const char *const *)unset.v, log,
                                         cy_num(c, "timeout", 3600), &out, &r);
                    rc = sp ? 127 : r.rc;
                    const char *o = sb_str(&out);
                    if (rc) {
                        status = "fail";
                        snprintf(detail, sizeof detail, "rc %d%s", rc, r.timed_out ? " (timed out)" : "");
                    } else if (pin_args) {
                        char gt[64], go[64];
                        word_after(o, "trajectory digest ", gt, sizeof gt);
                        word_after(o, "observation digest ", go, sizeof go);
                        if (strcmp(gt, traj) || strcmp(go, obs))
                            status = "fail", snprintf(detail, sizeof detail, "digests %s %s, pinned %s %s", gt[0] ? gt : "none",
                                                      go[0] ? go : "none", traj, obs);
                        else snprintf(detail, sizeof detail, "trajectory %s, observation %s as pinned (%s)", gt, go, cy_get(c, "pin", ""));
                    }
                    const char *req = cy_get(c, "require", NULL);
                    if (!strcmp(status, "pass") && req) {
                        regex_t re;
                        if (regcomp(&re, req, REG_EXTENDED | REG_NEWLINE | REG_NOSUB))
                            status = "fail", snprintf(detail, sizeof detail, "require: a bad regex %s", req);
                        else {
                            if (regexec(&re, o, 0, NULL, 0)) status = "fail", snprintf(detail, sizeof detail, "require /%s/ did not match", req);
                            regfree(&re);
                        }
                    }
                    if (!detail[0]) {
                        /* the last line, as a summary */
                        size_t n = strlen(o);
                        while (n && o[n - 1] == '\n') --n;
                        size_t b = n;
                        while (b && o[b - 1] != '\n') --b;
                        snprintf(detail, sizeof detail, "%.*s", (int)(n - b > 200 ? 200 : n - b), o + b);
                    }
                    sb_free(&out);
                }
                sv_free(&av);
                free(x);
            }
            free(pin_args);
            sv_free(&env), sv_free(&unset);
        } else
            status = "fail", rc = 2, snprintf(detail, sizeof detail, "neither cmd nor a known builtin");
        restore_links(guards, ng, logdir, &restored);
        double wall = cli_now() - ts;
        if (!strcmp(status, "fail")) fail = 1;
        if (strcmp(status, "skip")) ++ran;
        printf("%-4s %6.1f s  %s\n", !strcmp(status, "pass") ? "PASS" : !strcmp(status, "fail") ? "FAIL" : "SKIP", wall, detail);
        sb_puts(&js, nchk++ ? ", {\"name\": " : "{\"name\": ");
        cli_json_str(&js, c->key);
        sb_printf(&js, ", \"status\": \"%s\", \"rc\": %d, \"wall\": %.2f, \"ms\": %.1f, \"log\": ", status, rc, wall, wall * 1e3);
        cli_json_str(&js, cli_exists(log) ? log : "");
        sb_puts(&js, ", \"detail\": ");
        cli_json_str(&js, detail);
        sb_puts(&js, "}");
    }
    if (!ran) fail = 1;
    sb_printf(&js, "], \"ran\": %d, \"wall\": %.1f, \"restored_links\": ", ran, cli_now() - t0);
    cli_json_str(&js, restored.s ? restored.s : "");
    sb_printf(&js, ", \"ok\": %s}\n", fail ? "false" : "true");
    char jp[PATH_MAX + 32];
    if (json) cli_abs(json, jp);
    else cli_fmt(jp, sizeof jp, "%s/gate.json", logdir);
    if (cli_write_file_atomic(jp, js.s, js.n)) fprintf(stderr, "nether gate: cannot write %s\n", jp);
    printf("nether gate: %s (%d checks run in %.0f s); manifest %s\n", fail ? "FAIL" : "PASS", ran, cli_now() - t0, jp);
    sb_free(&js), sb_free(&restored);
    cy_free(y);
    return fail;
}
