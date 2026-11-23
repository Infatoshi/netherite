/* nether: the front door. Each verb runs the repo's own make targets, tools and trainer, printing every command
 * it runs; nothing here is game logic, and nothing here knows a host, a user, a seed list or an experiment (those are
 * YAML and TSV files under configs/).
 *
 *   nether help | nether VERB --help
 *
 * Every child is started by posix_spawn with an argv (csrc/cli/cli_proc.c): no system(), no sh -c, no quoting.
 * The verbs in their own files: eval and stats (cli_eval.c), ab (cli_ab.c), gate (cli_gate.c), harvest
 * (cli_harvest.c), flywheel (cli_flywheel.c).
 *
 * The checkout is the first directory up from the current one that holds AGENTS.md and ENGINE/Makefile, else the
 * checkout this binary was built in (make install copies it out of the tree). */
#define _GNU_SOURCE
#include "cli.h"
#include "cli_proc.h"
#include "cli_run.h"
#include "cli_verbs.h"
#include "cli_compose.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef NETH_ROOT
#define NETH_ROOT ""
#endif
/* the tree's folders (AGENTS.md "Layout") */
#define ENGINE "csrc"
#define ORACLE "oracle"

/* snprintf that fails loudly instead of truncating a path */
static void fmt(char *b, size_t n, const char *f, ...) __attribute__((format(printf, 3, 4)));
static void fmt(char *b, size_t n, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    int r = vsnprintf(b, n, f, ap);
    va_end(ap);
    if (r < 0 || (size_t)r >= n) {
        fprintf(stderr, "nether: a path is too long\n");
        exit(2);
    }
}

struct verb {
    const char *name, *args, *what, *example, *help;
    int (*run)(int argc, char **argv);
};

static int at_root(const char *dir)
{
    char p[PATH_MAX + 32];
    snprintf(p, sizeof p, "%s/AGENTS.md", dir);
    if (!cli_exists(p)) return 0;
    snprintf(p, sizeof p, "%s/" ENGINE "/Makefile", dir);
    return cli_exists(p);
}

static int find_root(void)
{
    char d[PATH_MAX];
    if (getcwd(d, sizeof d)) {
        for (;;) {
            if (at_root(d)) {
                snprintf(cli_root, sizeof cli_root, "%s", d);
                return 0;
            }
            char *s = strrchr(d, '/');
            if (!s || s == d) break;
            *s = 0;
        }
    }
    if (*NETH_ROOT && at_root(NETH_ROOT)) {
        snprintf(cli_root, sizeof cli_root, "%s", NETH_ROOT);
        return 0;
    }
    fprintf(stderr, "nether: not inside a netherite checkout (no AGENTS.md and " ENGINE "/Makefile above here, and the "
                    "checkout it was built in is gone)\n");
    return 1;
}

/* argv run in the checkout; its exit status */
static int run_argv(const char *const *av)
{
    struct cli_spawn_opts o = {.argv = av, .cwd = cli_root};
    struct cli_spawn_res r;
    if (cli_spawn(&o, &r)) return 127;
    return r.rc;
}
#define RUN(...) run_argv((const char *const[]){__VA_ARGS__, NULL})

static int run_sv(struct sv *a)
{
    int rc = run_argv((const char *const *)a->v);
    sv_free(a);
    return rc;
}

static int wants_help(int argc, char **argv)
{
    for (int i = 1; i < argc && strcmp(argv[i], "--"); ++i)
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) return 1;
    return 0;
}

/* ---------------------------------------------------------------- setup */

static int find_mc(const char *given, char *out)
{
    const char *home = getenv("HOME");
    char c[4][PATH_MAX];
    int n = 0;
    if (given) snprintf(c[n++], PATH_MAX, "%s", given);
    else if (home) {
        snprintf(c[n++], PATH_MAX, "%s/.minecraft", home);
        snprintf(c[n++], PATH_MAX, "%s/Library/Application Support/minecraft", home);
    }
    for (int i = 0; i < n; ++i) {
        char j[PATH_MAX + 64];
        fmt(j, sizeof j, "%s/versions/1.7.10/1.7.10.jar", c[i]);
        if (cli_exists(j)) {
            fmt(out, PATH_MAX, "%s", c[i]);
            return 0;
        }
    }
    fprintf(stderr, "nether: no Minecraft 1.7.10 install found (looked for versions/1.7.10/1.7.10.jar in");
    for (int i = 0; i < n; ++i) fprintf(stderr, " %s", c[i]);
    fprintf(stderr, ").\n  Install and launch 1.7.10 once with the official launcher, or pass --mc DIR (a launcher\n"
                    "  directory with versions/, libraries/ and assets/).\n");
    return 1;
}

static int v_setup(int argc, char **argv)
{
    const char *mc = NULL, *jdk = NULL;
    int play = 1;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--mc") && i + 1 < argc) mc = argv[++i];
        else if (!strcmp(argv[i], "--jdk") && i + 1 < argc) jdk = argv[++i];
        else if (!strcmp(argv[i], "--no-play")) play = 0;
        else {
            fprintf(stderr, "nether setup: unknown argument %s (nether setup --help)\n", argv[i]);
            return 2;
        }
    }
    char mcdir[PATH_MAX], a[PATH_MAX];
    if (mc) mc = cli_abs(mc, a);
    if (find_mc(mc, mcdir)) return 1;
    printf("nether: Minecraft 1.7.10 from %s\n", mcdir);
    const char *need[] = {"make", "cc", "jq", "unzip", "perl", "git", "curl"};
    int miss = 0;
    for (size_t i = 0; i < sizeof need / sizeof need[0]; ++i)
        if (!cli_have(need[i])) {
            fprintf(stderr, "nether: %s is not installed\n", need[i]);
            miss = 1;
        }
    if (miss) return 1;
    char jv[PATH_MAX + 16] = "", jb[PATH_MAX];
    if (jdk) {
        char j[PATH_MAX + 16];
        jdk = cli_abs(jdk, jb);
        snprintf(j, sizeof j, "%s/bin/javac", jdk);
        if (!cli_exists(j)) {
            fprintf(stderr, "nether: %s has no bin/javac (--jdk takes a JDK 8 home)\n", jdk);
            return 1;
        }
        fmt(jv, sizeof jv, "JAVA8=%s", jdk);
    }
    char mv[PATH_MAX + 16];
    fmt(mv, sizeof mv, "MC_HOME=%s", mcdir);
    struct sv c = {0};
    int rc;
    /* 1. the libraries, the client jar (resources) and natives, from the install */
    sv_push(&c, "make"), sv_push(&c, "-C"), sv_push(&c, ORACLE), sv_push(&c, "deps"), sv_push(&c, mv);
    if (jv[0]) sv_push(&c, jv);
    if ((rc = run_sv(&c))) return rc;
    /* 2. the oracle's source (the decompile, cached once: Docker unless this checkout has the private history) and
     *    its build; MC_HOME in its environment too (decomp/src.sh reads it) */
    sv_push(&c, "make"), sv_push(&c, "-C"), sv_push(&c, ORACLE), sv_push(&c, "build"), sv_push(&c, mv);
    if (jv[0]) sv_push(&c, jv);
    {
        const char *env[] = {mv, NULL};
        struct cli_spawn_opts o = {.argv = (const char *const *)c.v, .cwd = cli_root, .env_set = env};
        struct cli_spawn_res r;
        rc = cli_spawn(&o, &r) ? 127 : r.rc;
        sv_free(&c);
        if (rc) return rc;
    }
    /* 3. the C engine, its tests and tools, out/assets from the jar */
    if ((rc = RUN("make", "-s", "-C", ENGINE))) return rc;
    /* 4. play's recorded scenes, the two textures cut from them, and the fresh seed-1 start play and the pipeline
     *    begin at */
    sv_push(&c, "make"), sv_push(&c, "-s"), sv_push(&c, "-C"), sv_push(&c, ORACLE), sv_push(&c, "render-scenes");
    if (jv[0]) sv_push(&c, jv);
    if ((rc = run_sv(&c)) || (rc = RUN("make", "-s", "-C", ENGINE, "assets"))) return rc;
    char m[PATH_MAX + 64];
    fmt(m, sizeof m, "%s/out/java/snapshots/fresh-play-s1/manifest.json", cli_root);
    if (!cli_exists(m)) {
        sv_push(&c, "make"), sv_push(&c, "-s"), sv_push(&c, "-C"), sv_push(&c, ORACLE), sv_push(&c, "script");
        sv_push(&c, "SEED=1"), sv_push(&c, "SCRIPT=tests/fresh-play-s1.jsonl");
        sv_push(&c, "TAPE=../out/java/snapshots/fresh-play-s1/tape.jsonl");
        if (jv[0]) sv_push(&c, jv);
        if ((rc = run_sv(&c))) return rc;
    }
    /* 5. the playable client (SDL3) */
    if (play) {
        if (RUN("pkg-config", "--exists", "sdl3") == 0) {
            if ((rc = RUN("make", "-s", "-C", ENGINE, "play"))) return rc;
        } else
            printf("nether: SDL3 not found (pkg-config sdl3): the playable client is not built; install SDL3 and run "
                   "make -C " ENGINE " play\n");
    }
    printf("nether: setup done. Next: nether gate --java (checks the engine against the Java client), nether play\n");
    return 0;
}

/* ---------------------------------------------------------------- play, check, bench */

static int v_play(int argc, char **argv)
{
    long seed = 1;
    int i = 1;
    for (; i < argc; ++i) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            if (cli_long(argv[++i], &seed)) {
                fprintf(stderr, "nether play: --seed takes a number\n");
                return 2;
            }
        } else
            break;
    }
    char m[PATH_MAX + 96], d[PATH_MAX + 96];
    fmt(d, sizeof d, "%s/out/java/snapshots/fresh-play-s%ld", cli_root, seed);
    fmt(m, sizeof m, "%s/manifest.json", d);
    int rc;
    struct sv c = {0};
    if (!cli_exists(m)) {
        /* a fresh world of that seed: the oracle records its start (into a directory that must exist: the oracle
         * opens the tape and stops the game when it cannot, lane/seeds) */
        if (cli_mkdirs(d)) {
            fprintf(stderr, "nether play: cannot make %s\n", d);
            return 1;
        }
        sv_push(&c, "make"), sv_push(&c, "-s"), sv_push(&c, "-C"), sv_push(&c, ORACLE), sv_push(&c, "script");
        sv_pushf(&c, "SEED=%ld", seed), sv_push(&c, "SCRIPT=tests/fresh-play-s1.jsonl");
        sv_pushf(&c, "TAPE=../out/java/snapshots/fresh-play-s%ld/tape.jsonl", seed);
        if ((rc = run_sv(&c))) return rc;
    }
    sv_push(&c, "bash"), sv_push(&c, ENGINE "/play/session.sh");
    if (seed != 1) sv_push(&c, "--from"), sv_pushf(&c, "out/java/snapshots/fresh-play-s%ld", seed);
    for (; i < argc; ++i) sv_push(&c, argv[i]);
    return run_sv(&c);
}

static int v_check(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "nether check: TAPE (nether check --help)\n");
        return 2;
    }
    char b[PATH_MAX];
    struct sv c = {0};
    sv_push(&c, "bash"), sv_push(&c, ENGINE "/play/check.sh"), sv_push(&c, cli_abs(argv[1], b));
    for (int i = 2; i < argc; ++i) sv_push(&c, argv[i]);
    return run_sv(&c);
}

static int v_bench(int argc, char **argv)
{
    struct sv c = {0};
    sv_push(&c, "make"), sv_push(&c, "-s"), sv_push(&c, "-C"), sv_push(&c, ENGINE "/runtime"), sv_push(&c, "pipe-bench");
    if (argc > 1) {
        /* ARGS is one make variable the target's recipe splits */
        struct sb a = {0};
        cli_argv_line((const char *const *)argv + 1, &a);
        sv_pushf(&c, "ARGS=%s", a.s);
        sb_free(&a);
    }
    return run_sv(&c);
}

/* ---------------------------------------------------------------- train, score */

/* train --trainer DIR, the external trainer (mcsr-speedrun's, Python, outside this tree): the run file names it in
 * a directive comment (both parsers skip comments): "# nether VERB: DIR: COMMAND", DIR relative to the checkout,
 * COMMAND split into words (leading NAME=VALUE words are its environment) and run there, {run} the run file's path
 * and {root} the checkout's. */
static int directive(const char *run, const char *verb, char *dir, size_t dn, char *cmd, size_t cn)
{
    FILE *f = fopen(run, "r");
    if (!f) {
        fprintf(stderr, "nether: cannot read %s: %s\n", run, strerror(errno));
        return -1;
    }
    /* "# nether VERB:" or the older "# netherite VERB:" (run files written before the rename) */
    char line[4096], key[64], old[64];
    snprintf(key, sizeof key, "# nether %s:", verb);
    snprintf(old, sizeof old, "# netherite %s:", verb);
    int found = 0;
    while (fgets(line, sizeof line, f)) {
        size_t kl = !strncmp(line, key, strlen(key)) ? strlen(key) : !strncmp(line, old, strlen(old)) ? strlen(old) : 0;
        if (!kl) continue;
        char *p = line + kl, *colon;
        while (*p == ' ') ++p;
        if (!(colon = strstr(p, ": "))) continue;
        *colon = 0;
        snprintf(dir, dn, "%s", p);
        p = colon + 2;
        p[strcspn(p, "\n")] = 0;
        snprintf(cmd, cn, "%s", p);
        found = 1;
        break;
    }
    fclose(f);
    return found;
}

/* words: leading NAME=VALUE into env, the rest into av */
static void split_env(struct sv *words, struct sv *env, struct sv *av)
{
    int i = 0;
    for (; i < words->n; ++i) {
        const char *w = words->v[i];
        size_t n = strcspn(w, "=");
        if (!w[n] || !n || w[strspn(w, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_")] != '=') break;
        sv_push(env, w);
    }
    for (; i < words->n; ++i) sv_push(av, words->v[i]);
}

static int external(const char *verb, int argc, char **argv)
{
    const char *run = NULL, *tdir = NULL;
    int i = 1;
    if (i < argc && argv[i][0] != '-') run = argv[i++];
    for (; i < argc; ++i) {
        if (!strcmp(argv[i], "--trainer") && i + 1 < argc) tdir = argv[++i];
        else if (!strcmp(argv[i], "--")) {
            ++i;
            break;
        } else {
            fprintf(stderr, "nether %s: unknown argument %s (nether %s --help)\n", verb, argv[i], verb);
            return 2;
        }
    }
    if (!run) {
        fprintf(stderr, "nether %s: RUN.yaml (nether %s --help)\n", verb, verb);
        return 2;
    }
    char runabs[PATH_MAX];
    if (cr_path(verb, run, runabs)) return 2;
    char dir[PATH_MAX], cmd[4096];
    int d = directive(runabs, verb, dir, sizeof dir, cmd, sizeof cmd);
    if (d < 0) return 2;
    if (!d) {
        fprintf(stderr, "nether %s: %s names no trainer: add a line\n  # nether %s: DIR: COMMAND\n"
                        "(DIR the trainer's checkout, relative to this one; COMMAND with {run} and {root})\n", verb, run, verb);
        return 2;
    }
    char td[PATH_MAX + 8], tb[PATH_MAX];
    if (tdir) snprintf(td, sizeof td, "%s", cli_abs(tdir, tb));
    else if (dir[0] == '/') snprintf(td, sizeof td, "%s", dir);
    else fmt(td, sizeof td, "%s/%s", cli_root, dir);
    if (!cli_exists(td)) {
        fprintf(stderr, "nether %s: the trainer's checkout %s is not there (clone it, or pass --trainer DIR)\n", verb, td);
        return 2;
    }
    const char *keys[2] = {"run", "root"}, *vals[2] = {runabs, cli_root};
    char *c = cli_subst(cmd, keys, vals, 2), err[256];
    struct sv words = {0}, env = {0}, av = {0};
    if (cli_split_words(c, &words, err, sizeof err)) {
        fprintf(stderr, "nether %s: the directive's COMMAND: %s\n", verb, err);
        free(c);
        return 2;
    }
    free(c);
    split_env(&words, &env, &av);
    for (; i < argc; ++i) sv_push(&av, argv[i]);
    if (!av.n) {
        fprintf(stderr, "nether %s: the directive's COMMAND is empty\n", verb);
        return 2;
    }
    struct cli_spawn_opts o = {.argv = (const char *const *)av.v, .cwd = td, .env_set = (const char *const *)env.v};
    struct cli_spawn_res r;
    int rc = cli_spawn(&o, &r) ? 127 : r.rc;
    sv_free(&words), sv_free(&env), sv_free(&av);
    return rc;
}

static int prep(const char *runabs)
{
    char r[PATH_MAX + 8];
    fmt(r, sizeof r, "RUN=%s", runabs);
    return RUN("make", "-s", "-C", ENGINE "/runtime", "train-prep", r);
}

static int v_train(int argc, char **argv)
{
    for (int i = 1; i < argc && strcmp(argv[i], "--"); ++i)
        if (!strcmp(argv[i], "--trainer")) return external("train", argc, argv);
    if (argc < 2 || argv[1][0] == '-') {
        fprintf(stderr, "nether train: RUN.yaml (nether train --help)\n");
        return 2;
    }
    char runabs[PATH_MAX];
    int rc;
    if ((rc = cr_path("train", argv[1], runabs))) return rc;
    /* --set KEY=VALUE (ours) and --variant V; everything else is ntloop's */
    struct sv sets = {0}, rest = {0};
    const char *variant = NULL;
    int from = 2;
    if (from < argc && !strcmp(argv[from], "--")) ++from;
    for (int i = from; i < argc; ++i) {
        if (!strcmp(argv[i], "--set") && i + 1 < argc) sv_push(&sets, argv[++i]);
        else if (!strcmp(argv[i], "--variant") && i + 1 < argc) variant = argv[++i];
        else sv_push(&rest, argv[i]);
    }
    char name[256], stamp[32];
    {
        const char *b = strrchr(runabs, '/');
        fmt(name, sizeof name, "%s", b ? b + 1 : runabs);
        char *dot = strrchr(name, '.');
        if (dot) *dot = 0;
        cli_stamp(stamp, sizeof stamp, "%Y%m%d-%H%M%S");
    }
    char tdir[PATH_MAX];
    fmt(tdir, sizeof tdir, "%s/out/train", cli_root);
    if (cli_mkdirs(tdir)) {
        fprintf(stderr, "nether train: cannot make %s\n", tdir);
        return 1;
    }
    /* the run file with the --set overrides: out/train/NAME-DATE-TIME.run.yaml (configs/ stays clean) */
    char use[PATH_MAX];
    snprintf(use, sizeof use, "%s", runabs);
    if (sets.n) {
        fmt(use, sizeof use, "%s/%s-%s.run.yaml", tdir, name, stamp);
        if ((rc = cr_derive("train", runabs, &sets, NULL, use, NULL))) return rc;
        printf("nether train: the run file with the overrides: %s\n", use);
    }
    char err[512];
    struct cy *t = cy_load(use, err, sizeof err);
    if (!t) {
        fprintf(stderr, "nether train: %s\n", err);
        return 2;
    }
    if (!variant && (rc = prep(use))) {
        cy_free(t);
        return rc;
    }
    struct cr_trainer tr;
    rc = cr_trainer_find("train", variant, t, "", 1, &tr);
    /* the run's directory: the run file's run.out, a --out given, else out/train/NAME-DATE-TIME */
    int given = cy_get(t, "policy.run.out", NULL) != NULL;
    cy_free(t);
    if (rc) return rc;
    int init = 0;
    const char *seed = "1";
    for (int i = 0; i < rest.n; ++i) {
        if (!strcmp(rest.v[i], "--out")) given = 1;
        if (!strcmp(rest.v[i], "--init")) init = 1;
        if (!strcmp(rest.v[i], "--seed") && i + 1 < rest.n) seed = rest.v[i + 1];
    }
    char dir[PATH_MAX] = "", fresh[PATH_MAX + 16] = "";
    if (!given) fmt(dir, sizeof dir, "out/train/%s-%s", name, stamp);
    /* the start: --init W.ntw given, else a fresh net (ntinit, fastppo Small's initialisation) drawn from --seed's K */
    if (!init) {
        fmt(fresh, sizeof fresh, "out/train/%s-%s.init.ntw", name, stamp);
        if ((rc = RUN(tr.ntinit, fresh, seed))) return rc;
    }
    struct sv c = {0};
    sv_push(&c, tr.bin), sv_push(&c, "--run"), sv_push(&c, use);
    if (dir[0]) sv_push(&c, "--out"), sv_push(&c, dir);
    if (fresh[0]) sv_push(&c, "--init"), sv_push(&c, fresh);
    for (int i = 0; i < rest.n; ++i) sv_push(&c, rest.v[i]);
    rc = run_sv(&c);
    if (dir[0] && !rc)
        printf("nether train: done; the model, its Adam moments and the logs are in %s (model.ntw, iters.jsonl, "
               "episodes.jsonl)\nnext: nether score %s %s/model.ntw\n", dir, use, dir);
    sv_free(&sets), sv_free(&rest);
    return rc;
}

static int v_score(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "nether score: RUN.yaml W.ntw [RUNS [MIN [SEED]]] (nether score --help)\n");
        return 2;
    }
    char runabs[PATH_MAX], wabs[PATH_MAX], err[512];
    int rc;
    if ((rc = cr_path("score", argv[1], runabs))) return rc;
    struct cy *t = cy_load(runabs, err, sizeof err);
    if (!t) {
        fprintf(stderr, "nether score: %s\n", err);
        return 2;
    }
    const char *obs = cy_get(t, "sim.client.observation", cy_get(t, "client.observation", ""));
    char v[8];
    if (!strcmp(obs, "16x16")) v[0] = 0;
    else if (!strcmp(obs, "64x64")) snprintf(v, sizeof v, "s1");
    else {
        fprintf(stderr, "nether score: %s's client.observation is '%s': the native trainer is built for 16x16 and 64x64\n",
                argv[1], obs);
        cy_free(t);
        return 2;
    }
    cy_free(t);
    if (cr_path("score", argv[2], wabs)) return 2;
    static const char *const K[3] = {"RUNS", "MIN", "SEED"};
    for (int i = 3; i < argc; ++i) {
        long x;
        if (i > 5 || cli_long(argv[i], &x)) {
            fprintf(stderr, "nether score: RUNS, MIN and SEED are numbers (nether score --help)\n");
            return 2;
        }
    }
    if ((rc = prep(runabs))) return rc;
    struct sv c = {0};
    sv_push(&c, "make"), sv_push(&c, "-s"), sv_push(&c, "-C"), sv_push(&c, ENGINE "/runtime"), sv_push(&c, "train-score");
    sv_pushf(&c, "RUN=%s", runabs), sv_pushf(&c, "W=%s", wabs), sv_pushf(&c, "V=%s", v);
    for (int i = 3; i < argc; ++i) sv_pushf(&c, "%s=%s", K[i - 3], argv[i]);
    return run_sv(&c);
}

/* ---------------------------------------------------------------- replay */

/* KEY=VALUE's value in s (up to a space or the line's end), "" when absent */
static void word_after(const char *s, const char *key, char *val, size_t n)
{
    *val = 0;
    const char *p = strstr(s, key);
    if (!p) return;
    p += strlen(key);
    size_t l = strcspn(p, " \n");
    snprintf(val, n, "%.*s", (int)l, p);
}

/* the last k lines of a file to stderr */
static void tail_file(const char *path, int k)
{
    char *s = cli_read_file(path, NULL);
    if (!s) return;
    size_t n = strlen(s);
    while (n && s[n - 1] == '\n') --n;
    size_t i = n;
    for (int seen = 0; i > 0; --i)
        if (s[i - 1] == '\n' && ++seen == k) break;
    fprintf(stderr, "%.*s\n", (int)(n - i), s + i);
    free(s);
}

/* DIR's replay checked by the C engine (test_snapshots: every row and field of the Java client's recording) and its
 * receipt, DIR/replay.receipt.json: each file's SHA-256, the rows and field mismatches, ntrec end's verdict */
static int receipt(const char *dir, const char *nrec, const char *ep, int end_rc)
{
    char ts[PATH_MAX + 64], log[PATH_MAX + 64];
    fmt(ts, sizeof ts, "%s/out/native/test_snapshots", cli_root);
    fmt(log, sizeof log, "%s/test_snapshots.log", dir);
    int rows = -1, mism = -1, trc = 127;
    if (!cli_exists(ts)) RUN("make", "-s", "-C", ENGINE);
    if (cli_exists(ts)) {
        const char *av[] = {ts, dir, NULL};
        struct sb o = {0};
        struct cli_spawn_opts so = {.argv = av, .cwd = cli_root, .capture = &o, .out_path = log, .stdin_null = 1};
        struct cli_spawn_res r;
        if (!cli_spawn(&so, &r)) trc = r.rc;
        const char *p = strstr(sb_str(&o), "replay: ");
        if (p) sscanf(p, "replay: %d rows simulated from the snapshot, %d field mismatches", &rows, &mism);
        sb_free(&o);
    } else
        fprintf(stderr, "nether replay: no %s: the replay is not checked by the C engine\n", ts);
    char et[PATH_MAX + 16];
    fmt(et, sizeof et, "%s/end.txt", dir);
    char *end = cli_read_file(et, NULL);
    const char *verdict = !end ? "none" : strstr(end, "END EQUAL") ? "END EQUAL" : strstr(end, "END DIFF") ? "END DIFF"
                                                                     : strstr(end, "INCOMPLETE")      ? "INCOMPLETE"
                                                                                                      : "unrecognised";
    struct sb j = {0};
    char stamp[40], sha[16];
    cli_stamp(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%S%z");
    cli_git_sha(sha, sizeof sha);
    sb_puts(&j, "{\"dir\": ");
    cli_json_str(&j, dir);
    sb_puts(&j, ", \"recording\": ");
    cli_json_str(&j, nrec ? nrec : "");
    sb_puts(&j, ", \"episode\": ");
    cli_json_str(&j, ep ? ep : "");
    sb_printf(&j, ", \"made\": \"%s\", \"commit\": \"%s\", \"end\": \"%s\", \"end_rc\": %d, \"test_snapshots\": {\"rc\": %d, "
                  "\"rows\": %d, \"field_mismatches\": %d}, \"files\": {", stamp, sha, verdict, end_rc, trc, rows, mism);
    static const char *const F[] = {"script.jsonl", "world.yaml", "tape.jsonl", "end.txt", "manifest.json"};
    int nf = 0;
    for (size_t i = 0; i < sizeof F / sizeof F[0]; ++i) {
        char p[PATH_MAX + 32], h[65];
        fmt(p, sizeof p, "%s/%s", dir, F[i]);
        if (cli_sha256_file(p, h)) continue;
        sb_puts(&j, nf++ ? ", " : "");
        cli_json_str(&j, F[i]);
        sb_printf(&j, ": \"%s\"", h);
    }
    /* the mp4s */
    char mp[PATH_MAX + 32];
    const char *base = strrchr(dir, '/');
    fmt(mp, sizeof mp, "%s/%s.mp4", dir, base ? base + 1 : dir);
    char h[65];
    if (!cli_sha256_file(mp, h)) {
        sb_puts(&j, nf++ ? ", " : "");
        cli_json_str(&j, strrchr(mp, '/') + 1);
        sb_printf(&j, ": \"%s\"", h);
    }
    int ok = trc == 0 && mism == 0 && rows > 0 && (end_rc == 0 || !end);
    sb_printf(&j, "}, \"ok\": %s}\n", ok ? "true" : "false");
    char rp[PATH_MAX + 32];
    fmt(rp, sizeof rp, "%s/replay.receipt.json", dir);
    if (cli_write_file_atomic(rp, j.s, j.n)) fprintf(stderr, "nether replay: cannot write %s\n", rp);
    else printf("nether replay: %s: %s; test_snapshots %d rows, %d field mismatches; %s\n", rp, ok ? "OK" : "FAIL", rows,
                mism, verdict);
    sb_free(&j);
    free(end);
    return ok ? 0 : 1;
}

static int v_replay(int argc, char **argv)
{
    const char *nrec = NULL, *ep = NULL, *only = NULL;
    long every = 2;
    int force = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--every") && i + 1 < argc) {
            if (cli_long(argv[++i], &every) || every < 0) {
                fprintf(stderr, "nether replay: --every takes ticks (0: no frames)\n");
                return 2;
            }
        } else if (!strcmp(argv[i], "--force")) force = 1;
        else if (!strcmp(argv[i], "--receipt") && i + 1 < argc) only = argv[++i];
        else if (!nrec) nrec = argv[i];
        else if (!ep) ep = argv[i];
        else {
            fprintf(stderr, "nether replay: unknown argument %s (nether replay --help)\n", argv[i]);
            return 2;
        }
    }
    if (only) {
        char b[PATH_MAX];
        if (!realpath(cli_abs(only, b), b) || !cli_is_dir(b)) {
            fprintf(stderr, "nether replay: no directory %s\n", only);
            return 2;
        }
        return receipt(b, NULL, NULL, 0);
    }
    if (!nrec || !ep) {
        fprintf(stderr, "nether replay: EPISODES.nrec EPISODE (nether replay --help)\n");
        return 2;
    }
    char nabs[PATH_MAX];
    if (cr_path("replay", nrec, nabs)) return 2;
    /* NAME: the run's directory and the episode (out/train/RUN/episodes.nrec, episode 3:1: RUN-3-1) */
    char name[512], run[256] = "run";
    {
        char d[PATH_MAX];
        snprintf(d, sizeof d, "%s", nabs);
        char *s = strrchr(d, '/');
        if (s) {
            *s = 0;
            s = strrchr(d, '/');
            fmt(run, sizeof run, "%s", s ? s + 1 : d);
        }
        fmt(name, sizeof name, "%s-%s", run, ep);
        for (char *p = name; *p; ++p)
            if (*p == ':' || *p == '/' || *p == ' ') *p = '-';
    }
    int rc;
    if ((rc = RUN("make", "-s", "-C", ENGINE "/runtime", "ntrec-bin"))) return rc;
    char dir[PATH_MAX];
    fmt(dir, sizeof dir, "%s/out/replay/%s", cli_root, name);
    if (cli_exists(dir) && (rc = RUN("rm", "-rf", dir))) return rc;
    if (cli_mkdirs(dir)) {
        fprintf(stderr, "nether replay: cannot make %s\n", dir);
        return 1;
    }
    /* 1. the episode as the oracle's script and its world */
    char script[PATH_MAX + 32], world[PATH_MAX + 32], tape[PATH_MAX + 32], jlog[PATH_MAX + 32], endp[PATH_MAX + 32];
    fmt(script, sizeof script, "%s/script.jsonl", dir);
    fmt(world, sizeof world, "%s/world.yaml", dir);
    fmt(tape, sizeof tape, "%s/tape.jsonl", dir);
    fmt(jlog, sizeof jlog, "%s/java.log", dir);
    fmt(endp, sizeof endp, "%s/end.txt", dir);
    struct sb o = {0};
    {
        const char *av[] = {"out/runtime/train/ntrec", "tape", nabs, ep, "-o", script, "--world", world, force ? "--force" : NULL,
                            NULL};
        struct cli_spawn_opts so = {.argv = av, .cwd = cli_root, .capture = &o, .tee = 1};
        struct cli_spawn_res r;
        rc = cli_spawn(&so, &r) ? 127 : r.rc;
    }
    if (rc) {
        fprintf(stderr, "nether replay: ntrec tape refused or failed (rc %d)%s\n", rc,
                rc == 3 ? ": only a vanilla-fidelity episode from the run's own start replays on the Java client "
                          "(--force writes it anyway; the rows will not match)" : "");
        sb_free(&o);
        return rc;
    }
    char seed[32], from[PATH_MAX];
    word_after(sb_str(&o), " SEED=", seed, sizeof seed);
    word_after(o.s, " FROM=", from, sizeof from);
    int dev = strstr(o.s, " DEV=1") != NULL; /* lane/rawskill: a dev start's episode (its setup's lines) */
    sb_free(&o);
    if (!seed[0]) {
        fprintf(stderr, "nether replay: ntrec tape printed no seed\n");
        return 1;
    }
    /* client.gamma 1.0 (options.txt's Bright): the frames' light only, never the rows (oracle WorldConf) */
    FILE *f = fopen(world, "a");
    if (!f || fputs("client:\n  gamma: 1.0\n", f) < 0 || fclose(f)) {
        fprintf(stderr, "nether replay: cannot write %s\n", world);
        return 1;
    }
    /* 2. the Java client plays it from the episode's world, frames every N ticks */
    struct sv c = {0};
    sv_push(&c, "make"), sv_push(&c, "-s"), sv_push(&c, "-C"), sv_push(&c, ORACLE), sv_push(&c, "script");
    sv_pushf(&c, "SEED=%s", seed), sv_pushf(&c, "CONF=%s", world), sv_pushf(&c, "SCRIPT=%s", script);
    sv_pushf(&c, "TAPE=%s", tape), sv_push(&c, "CACHE=0");
    if (every) sv_pushf(&c, "FRAMES=%s/frames", dir), sv_pushf(&c, "EVERY=%ld", every);
    if (from[0]) sv_pushf(&c, "FROM=%s%s", from[0] == '/' ? "" : "../", from);
    if (dev) sv_push(&c, "DEV=1");
    {
        struct cli_spawn_opts so = {.argv = (const char *const *)c.v, .cwd = cli_root, .out_path = jlog, .stdin_null = 1};
        struct cli_spawn_res r;
        rc = cli_spawn(&so, &r) ? 127 : r.rc;
        sv_free(&c);
    }
    if (rc) {
        tail_file(jlog, 5);
        fprintf(stderr, "nether replay: the Java client's run failed (%s)\n", jlog);
        return rc;
    }
    /* 3. its last row against the record's end (shown and kept in end.txt) */
    int end;
    {
        const char *av[] = {"out/runtime/train/ntrec", "end", nabs, ep, tape, NULL};
        struct cli_spawn_opts so = {.argv = av, .cwd = cli_root, .out_path = endp, .tee = 1};
        struct cli_spawn_res r;
        end = cli_spawn(&so, &r) ? 127 : r.rc;
    }
    /* 4. the frames as an mp4 at 30 frames a second (1.5x the game's 20 ticks a second at --every 2) */
    int vid = 0, have_ff = cli_have("ffmpeg");
    if (every) {
        if (!have_ff) printf("nether replay: ffmpeg is not installed: the frames stay in %s/frames\n", dir);
        else {
            char pat[PATH_MAX + 32], mp4[PATH_MAX + 600];
            fmt(pat, sizeof pat, "%s/frames/f_*.png", dir);
            fmt(mp4, sizeof mp4, "%s/%s.mp4", dir, name);
            if ((vid = RUN("ffmpeg", "-loglevel", "error", "-y", "-framerate", "30", "-pattern_type", "glob", "-i", pat,
                           "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "23", "-preset", "medium", "-movflags",
                           "+faststart", mp4)))
                fprintf(stderr, "nether replay: ffmpeg failed\n");
        }
    }
    printf("nether replay: %s: script.jsonl, world.yaml, tape.jsonl (the Java client's rows), end.txt%s%s\n", dir,
           every ? ", frames/" : "", every && !vid && have_ff ? ", the mp4" : "");
    /* 5. the C engine on the Java client's rows, and the receipt */
    int rcpt = receipt(dir, nabs, ep, end);
    return end ? end : vid ? vid : rcpt;
}

/* ---------------------------------------------------------------- the verbs */

static int v_help(int argc, char **argv);

/* the verbs that take --env/--sim/--algo: the layers composed first (cli_compose.c), the verb as it was otherwise */
static int c_train(int argc, char **argv) { return cc_dispatch("train", argc, argv, v_train); }
static int c_eval(int argc, char **argv) { return cc_dispatch("eval", argc, argv, v_eval); }
static int c_harvest(int argc, char **argv) { return cc_dispatch("harvest", argc, argv, v_harvest); }
static int c_flywheel(int argc, char **argv) { return cc_dispatch("flywheel", argc, argv, v_flywheel); }

#define LAYERS                                                                                                         \
    "\n--env NAME|PATH, --sim NAME|PATH, --algo NAME|PATH (configs/envs, configs/sim, configs/algo; nether list):\n"   \
    "the layers merged in C, sim then algo then env over the run file given (if any), then --set, into\n"         \
    "out/compose/LABEL-HASH.*.yaml, and the verb run on that. A monolithic run file alone works as before.\n"

static const struct verb VERBS[] = {
    {"setup", "[--mc DIR] [--jdk DIR] [--no-play]", "find your Minecraft 1.7.10, rebuild the oracle and assets from it, build everything",
     "nether setup --mc ~/.minecraft",
     "Finds the Minecraft 1.7.10 install (--mc DIR, else ~/.minecraft or\n"
     "~/Library/Application Support/minecraft: a launcher directory with versions/1.7.10/), then:\n"
     "  make -C " ORACLE " deps          the libraries, client jar and natives, copied from the install\n"
     "  make -C " ORACLE " build         the oracle's source (MCP 9.08's decompile in Docker, once, plus\n"
     "                               " ORACLE "/decomp/oracle.patch) and its build; nothing of it is committed\n"
     "  make -C " ENGINE "                 the C engine, its tests and tools, and out/assets from the jar\n"
     "  make -C " ORACLE " render-scenes play's two recorded scenes (the ray cast's block atlas is one); the\n"
     "                               fresh seed-1 start\n"
     "  make -C " ENGINE " play            the playable client, when SDL3 is installed (--no-play skips it)\n"
     "Needs JDK 8 (--jdk DIR, else " ORACLE "/Makefile's JAVA8), make, a C compiler, jq, unzip, perl, git,\n"
     "curl, and Docker for the one decompile. The trainer (nether train) also needs CUDA.", v_setup},
    {"gate", "[--quick | --full | --java] [--no-build] [--json OUT] [--gpu G] [--config FILE] [--only NAMES] [--list]",
     "check the tree: the checks of configs/gates.yaml for a mode, a JSON pass/fail manifest",
     "nether gate --quick --json out/gate/quick.json",
     "Runs every check of configs/gates.yaml whose modes include the one asked (posix_spawn, no shell), one line\n"
     "each, logs in out/gate/MODE-DATE-TIME/, and the manifest {\"ok\": ..., \"checks\": [...]} (--json OUT, else\n"
     "beside the logs). Modes as configs/gates.yaml defines them:\n"
     "  --quick  the macro freeze (no edit under csrc/mods/macros), the native build, the globals check, a\n"
     "           pinned vec_bench trajectory digest, and the trainer's wire check when --gpu G is given\n"
     "  --full   the quick set, the macros' digest matrix, the ray cast's frame and seen parity, the recording\n"
     "           gate at ticks_per_step 1 and 2\n"
     "  --java   the oracle's gate and three recordings the Java client makes now, each replayed by the C\n"
     "           engine row for row (CACHE unset: fresh), what a fresh clone checks after nether setup\n"
     "  (none)   --java, then make -C " ENGINE " test and clangcheck (the old default)\n"
     "A check with gpu: yes runs only with --gpu G (CUDA_VISIBLE_DEVICES=G, CUDA_DEV=G); every other one sees\n"
     "no GPU. --check-macro-freeze REF holds csrc/mods/macros to REF as well as to HEAD. The out/assets link is\n"
     "put back after any check that replaced it (a build in a lane does). --no-build skips the checks marked\n"
     "build: yes (the tree already built: the quick set's checks alone). Each check's ms is in the manifest.", v_gate},
    {"train", "RUN.yaml [--set KEY=VALUE ...] [--variant V] [NTLOOP ARGS...]",
     "train a policy on a run file with the native trainer (C and CUDA, no Python)",
     "nether train configs/tests/brute16fast.yaml --set ppo.lr=1e-4 --hours 0.01",
     "Makes what the run file needs where it is missing (make -C " ENGINE "/runtime train-prep RUN=...: the\n"
     "trainer built for its observation, 16x16 the default build and 64x64 the s1 one; out/assets; the\n"
     "cast's atlas; its start, recorded by the oracle), then runs\n"
     "  out/runtime/train/[V/]ntloop --run RUN.yaml --out out/train/NAME-DATE-TIME ARGS...\n"
     "(no --out when the run file's policy.run.out or ARGS name one). ARGS are ntloop's and win over\n"
     "the file: --hours H, --env-steps N, --init W.ntw, --rec on (the episodes as data for replay).\n"
     "--set KEY=VALUE (any number) writes the run file with the overrides to out/train/NAME-DATE-TIME.run.yaml\n"
     "and trains on that (KEY a full path such as sim.client.ticks_per_step, or a path under one part:\n"
     "ppo.lr). --variant V uses out/runtime/train/V/ntloop (built with make train-loop-v NT_V=V).\n"
     "Without --init the net starts fresh: out/train/NAME-DATE-TIME.init.ntw, fastppo Small's\n"
     "initialisation drawn by ntinit from --seed K's K (default 1).\n"
     "--trainer DIR runs the run file's \"# nether train: DIR: COMMAND\" directive instead (an\n"
     "external trainer's checkout, such as mcsr-speedrun's Python one).\n" LAYERS "With --algo: its variant and train keys become --variant and ntloop flags.", c_train},
    {"score", "RUN.yaml W.ntw [RUNS [MIN [SEED]]]", "the frozen eval: a model's milestones and depth over RUNS episodes",
     "nether score configs/tests/brute16fast.yaml out/train/brute16fast-20261101-120000/model.ntw 512 60 1000",
     "W's actor frozen (nothing learned), RUNS envs (default 512) from RUN's start, one MIN-minute\n"
     "episode each (default 60) with RUN's macros and fidelity, the sampler seeded by SEED (default\n"
     "1000): make -C " ENGINE "/runtime train-score (ntloop --score). Prints each milestone's share,\n"
     "the first time each is held, deaths, depth and its standard error, and writes\n"
     "out/runtime/train/score/NAME-SEED/score.json. nether eval is the exact, paired form.", v_score},
    {"eval", "--dir D [--compare-dir B] | RUN.yaml --model W.ntw [--compare B.ntw] [...]",
     "a frozen eval at exact fidelity, compared episode by episode with another: McNemar p, paired z, eval.json",
     "nether eval --dir out/eval/a/cand --compare-dir out/eval/b/cand",
     "Offline: --dir CAND_DIR [--compare-dir BASE_DIR] [--json OUT] reads ntloop --score's episodes.jsonl\n"
     "directly and prints n, depth, return and deaths (mean +/- standard error), the raw (no-macro) share,\n"
     "each milestone's k/n with its exact Wilson 95% interval and, with a baseline, the two paired on env:\n"
     "each milestone's exact two-sided McNemar binomial p (+candidate only/-baseline only), the paired z\n"
     "and two-sided p of depth, return and deaths.\n"
     "Live: RUN.yaml --model CAND.ntw [--compare BASE.ntw] runs ntloop --score for each (posix_spawn) with\n"
     "  --episodes N (one an env), --minutes M, --seed S (the sampler's; both default to the fidelity file's eval:),\n"
     "  --split FILE (seeds a line, or a splits.json's eval_seeds: --lib-seeds with a library, else --seeds),\n"
     "  --variant V, --gpu G (CUDA_VISIBLE_DEVICES; --gpu-guard checks it idle first, --gpu-wait S polls),\n"
     "  --lock FILE (flock), --set KEY=VALUE, --out DIR (default out/eval/NAME-DATE-TIME), -- NTLOOP FLAGS.\n"
     "  Exact fidelity unless --allow-approx: configs/fidelity.yaml's keys forced into OUT/run.yaml (FP32\n"
     "  cast, one tick a step, no cast radius, the full tick scope), its flags appended, its refused flags\n"
     "  refused. Each model's eval is cached in out/evalcache/KEY (KEY: the model's and trainer's SHA-256,\n"
     "  the run file, the flags), so a baseline is evaluated once; --no-cache runs it again.\n"
     "  OUT/cand and OUT/base link the evals; OUT/eval.json (or --json) has every number.\n" LAYERS
     "With --env: its eval keys give --episodes, --minutes and --seed where no flag does, the rest ntloop flags.", c_eval},
    {"ab", "NAME=CMD ... | --spec FILE [--rounds R] [--metric RE] [--lower] [--perf] [--json F] [--tsv F]",
     "variants timed against each other in interleaved rounds, held to one digest, with hardware counters",
     "nether ab --rounds 3 --perf --gpu 3 a='out/runtime/vec_bench --ns 128' b='out/runtime/vec_bench --ns 128 --groups 4'",
     "tools/abrun.c's engine with no shell: each variant's command is split into words (quotes kept, a\n"
     "leading NAME=VALUE is its environment; a pipe or redirection is refused) and run by posix_spawn in\n"
     "its own process group. Round r runs the variants in order (odd) or reversed (even). From each run:\n"
     "the metric (--metric RE, POSIX extended, group 1; default '([0-9][0-9.,]*) env-steps/s'), the digest\n"
     "(--digest RE, default 'trajectory digest ([0-9a-f]+)'; every run must print the same, --no-digest\n"
     "skips it), --require RE, wall, user and system time, peak RSS, minor faults, and with --perf user\n"
     "cycles, instructions and IPC (perf_event_open; rusage alone where perf_event_paranoid forbids it).\n"
     "The table: median, min, max, spread, ratio to the first, apart or overlap. --spec FILE: a YAML file\n"
     "(variants: NAME: {cmd:, env:, cwd:}, and any option as a key) or NAME<TAB>CMD lines. --lock FILE\n"
     "holds a flock for the whole run; --gpu G sets CUDA_VISIBLE_DEVICES=G (--gpu-guard: idle first).\n"
     "--json F every run; --tsv F a scoreboard row a variant, appended under flock.", v_ab},
    {"harvest", "BANK.yaml | --env NAME --out DIR [--seeds SPEC] [--target-seeds N] [--threads T] [--lock FILE] [--dry-run]",
     "build a bank of starts seed by seed (resumable), verified, with disjoint train and eval splits",
     "nether harvest --env wood-punch --seeds 1..16 --out out/bank/wood-smoke",
     "BANK.yaml names the seeds (bank.seeds: A-B,C or A..B; bank.seeds_file; bank.exclude; bank.exclude_files;\n"
     "--seeds SPEC overrides them all) and each seed's work, every command split into words (no shell) with\n"
     "{seed} {work} {out} {root} {start} and vars.* substituted: harvest.prepare (once), harvest.if_missing +\n"
     "first, harvest.source (libraries whose rows for the seed at harvest.index_stage are copied), harvest.cmd\n"
     "and harvest.steps, harvest.reject (a regex: a seed whose output matches is rejected, not failed),\n"
     "harvest.produces, harvest.verify (once, or once a start when it names {start}; e.g. test_snapshots).\n"
     "Seeds run --threads at a time; each seed's result (pass, fail, reject) is a line of DIR/progress.tsv\n"
     "(appended under flock), so a rerun skips the seeds done and SIGTERM ends the running ones.\n"
     "DIR/splits.json: the passed seeds split by a keyed SHA-256 (bank.split_train the train fraction,\n"
     "bank.split_key the key) into train_seeds and eval_seeds, overlap 0 checked; with harvest.index, each\n"
     "seed's starts merged into DIR/train/index.tsv and DIR/eval/index.tsv (a library ntloop --library reads).\n"
     "--env NAME: the bank composed from the env's harvest section (mode spawn: rawstarts --near-block and\n"
     "--biome on the seed set, test_snapshots on each start; mode milestone: its source or steps cut at\n"
     "harvest.milestone, test_snapshots on each start).", c_harvest},
    {"list", "", "the registry: every env, sim and algo layer with its description",
     "nether list",
     "configs/envs, configs/sim and configs/algo, sorted, one line each; the names --env, --sim and --algo take.", v_list},
    {"flywheel", "GYM.yaml [--name N] [--budget STEPS] [--incumbent W.ntw] [--gpu G] [--lock F] [--set K=V ...] [--dry-run]",
     "one autoresearch step: build, gate, train, exact eval against the incumbent, KEEP or REVERT, one logged row",
     "nether flywheel configs/gyms/dragon-smoke.yaml --incumbent cache/models/r1.ntw --gpu 3",
     "GYM.yaml declares the experiment: variant, train_config, eval_config, eval.* (ntloop --score flags\n"
     "by name: library, stages, start, groups, minutes, episodes, seed, dev), eval_split, budget_steps,\n"
     "primary_metric (depth, ret, deaths, or a milestone) with min_z / max_p, optional build_cmd and\n"
     "quick_gate, train.* (extra ntloop flags by name), log. One step: flock (--lock), the GPU guard\n"
     "(with --gpu G: idle or exit 3; --no-gpu-guard, --gpu-wait S), the build, the quick gate, ntloop for --budget env-steps from the incumbent (a fresh\n"
     "net without one) on the run file with --set's overrides (OUT/run.yaml; configs/ stays clean), the\n"
     "exact eval of the candidate and the incumbent (cached by content: an incumbent is evaluated once),\n"
     "then the verdict: KEEP (no NaN skip and the primary metric better with p <= max_p or z >= min_z),\n"
     "BASELINE (no incumbent) or REVERT, appended as one row to results.tsv and results.jsonl under flock.\n"
     "Exit 0 KEEP or BASELINE, 10 REVERT, other codes a failed step.\n" LAYERS
     "With layers GYM.yaml is optional: the composition's keys fill or override it, train_config and eval_config\n"
     "become the composed run file, and the log is out/flywheel/LABEL.", c_flywheel},
    {"replay", "EPISODES.nrec EPISODE [--every N] [--force] | --receipt DIR",
     "play a trained episode on the real Java client: its end checked against the record, frames, an mp4, a receipt",
     "nether replay out/train/RUN/episodes.nrec 3:1 --every 2",
     "The episode (an index of ntrec list, or E:K: env E's K-th episode) of a run recorded with\n"
     "--rec on, as the oracle's script (ntrec tape), played by the Java 1.7.10 client from the\n"
     "episode's world (make -C " ORACLE " script, frames every N ticks, default 2, 0 none), its last row's\n"
     "inventory checked against the record's end (ntrec end: END EQUAL, DIFF or INCOMPLETE), the\n"
     "frames as an mp4 at 30 frames a second (ffmpeg), then the Java client's rows replayed by the C\n"
     "engine (test_snapshots: every row and field) and DIR/replay.receipt.json (each file's SHA-256, the\n"
     "rows, the field mismatches, the end's verdict), all in out/replay/RUN-EPISODE. Only a vanilla-\n"
     "fidelity episode from the run's own start replays row for row (--force writes the others' scripts).\n"
     "--receipt DIR checks and writes the receipt of a replay directory already made.", v_replay},
    {"play", "[--seed N] [SESSION ARGS...]", "play the C engine's client; quitting checks the session against Java",
     "nether play --seed 42",
     "Plays a session of a new world (seed N, default 1) in the native client. When you quit,\n"
     "the Java client replays your inputs and the engine is compared with it row by row\n"
     "(" ENGINE "/play/session.sh; its --continue, --run NAME, --list and --status pass through).\n"
     "A seed's first start is recorded by the oracle once (out/java/snapshots/fresh-play-sN).", v_play},
    {"check", "TAPE", "check a played session's tape against the Java client",
     "nether check out/human/game/play/game-s1-A.jsonl",
     "The Java client replays TAPE's inputs and the C engine replays Java's rows and compares\n"
     "every field (" ENGINE "/play/check.sh). Prints the first differing row and field on a FAIL.", v_check},
    {"bench", "[PIPE_BENCH ARGS...]", "measure env-steps per second with rendered observations (CUDA)",
     "nether bench --kinds village --ns 128 --device-mesh 1",
     "make -C " ENGINE "/runtime pipe-bench ARGS=\"...\": the RL pipeline's throughput, every step's\n"
     "frame drawn on the GPU. Linux with CUDA and clang only.", v_bench},
    {"stats", "mcnemar B C | wilson K N | normp Z | paired FILE", "the eval's exact formulas on numbers given",
     "nether stats mcnemar 137 0",
     "The statistics engine of nether eval on numbers given, to check it against another\n"
     "implementation: the exact two-sided McNemar p of b and c, the Wilson 95% interval of k of n,\n"
     "the two-sided normal p of z, and the paired z of a file of \"candidate baseline\" lines.", v_stats},
    {"help", "[VERB]", "this list, or a verb's help", "nether help train", NULL, v_help},
};

static const struct verb *lookup(const char *name)
{
    for (size_t i = 0; i < sizeof VERBS / sizeof VERBS[0]; ++i)
        if (!strcmp(VERBS[i].name, name)) return &VERBS[i];
    return NULL;
}

static void verb_help(const struct verb *v)
{
    printf("nether %s %s\n  %s\n  e.g. %s\n", v->name, v->args, v->what, v->example);
    if (v->help) printf("\n%s\n", v->help);
}

static int v_help(int argc, char **argv)
{
    if (argc > 1) {
        const struct verb *v = lookup(argv[1]);
        if (!v) {
            fprintf(stderr, "nether: no verb %s\n", argv[1]);
            return 2;
        }
        verb_help(v);
        return 0;
    }
    printf("nether: Minecraft 1.7.10 in C, exact against the Java client, fast enough for RL.\n\n");
    for (size_t i = 0; i < sizeof VERBS / sizeof VERBS[0]; ++i)
        printf("  %-8s %s\n           e.g. %s\n", VERBS[i].name, VERBS[i].what, VERBS[i].example);
    printf("\nnether VERB --help: its arguments and what it runs. Start with nether setup, then nether gate --java.\n");
    return 0;
}

/* --profile (or NETHER_PROFILE=1): where the CLI's own time went, ms, on stderr at exit; spawn_exec is the children's
 * wall time from spawn to reap (the work a verb runs), so total less it is the CLI's own cost */
static double prof_t0;

static void prof_print(void)
{
    double total = cli_now() - prof_t0, sum = 0;
    fflush(stdout);
    fprintf(stderr, "nether profile (ms):");
    for (int p = 0; p < CLI_P_N; ++p) {
        fprintf(stderr, " %s %.3f", cli_prof_name[p], cli_prof_s[p] * 1e3);
        sum += cli_prof_s[p];
    }
    fprintf(stderr, " other %.3f total %.3f\n", (total - sum) * 1e3, total * 1e3);
}

int main(int argc, char **argv)
{
    prof_t0 = cli_now();
    setvbuf(stdout, NULL, _IOLBF, 0);
    const char *pe = getenv("NETHER_PROFILE");
    if (argc > 1 && !strcmp(argv[1], "--profile")) cli_prof_on = 1, --argc, ++argv;
    else if (pe && *pe && strcmp(pe, "0")) cli_prof_on = 1;
    if (cli_prof_on) atexit(prof_print);
    if (argc < 2 || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")) return v_help(1, argv);
    const struct verb *v = lookup(argv[1]);
    if (!v) {
        fprintf(stderr, "nether: no verb %s (nether help)\n", argv[1]);
        return 2;
    }
    if (v->run != v_help && wants_help(argc - 1, argv + 1)) {
        verb_help(v);
        return 0;
    }
    if (v->run == v_help) return v_help(argc - 1, argv + 1);
    if (v->run != v_stats && find_root()) return 1;
    cli_prof_s[CLI_P_STARTUP] = cli_now() - prof_t0;
    return v->run(argc - 1, argv + 1);
}
