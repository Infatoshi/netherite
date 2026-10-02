/* netherite: the front door. Each verb runs the repo's own make targets and
 * scripts, printing every command it runs; nothing here is game logic.
 *
 *   netherite help | netherite VERB --help
 *
 * The checkout is the first directory up from the current one that holds
 * AGENTS.md and ENGINE/Makefile, else the checkout this binary was built in
 * (make install copies it out of the tree). */
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef NW_ROOT
#define NW_ROOT ""
#endif
/* the tree's folders (AGENTS.md "Layout") */
#define ENGINE "csrc"
#define ORACLE "oracle"

static char root[PATH_MAX];

/* snprintf that fails loudly instead of truncating a path */
static void fmt(char *b, size_t n, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    int r = vsnprintf(b, n, f, ap);
    va_end(ap);
    if (r < 0 || (size_t)r >= n) { fprintf(stderr, "netherite: a path is too long\n"); exit(2); }
}

struct verb {
    const char *name, *args, *what, *help;
    int (*run)(int argc, char **argv);
};

static int exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static int at_root(const char *dir)
{
    char p[PATH_MAX + 32];
    snprintf(p, sizeof p, "%s/AGENTS.md", dir);
    if (!exists(p)) return 0;
    snprintf(p, sizeof p, "%s/" ENGINE "/Makefile", dir);
    return exists(p);
}

static int find_root(void)
{
    char d[PATH_MAX];
    if (getcwd(d, sizeof d)) {
        for (;;) {
            if (at_root(d)) { snprintf(root, sizeof root, "%s", d); return 0; }
            char *s = strrchr(d, '/');
            if (!s || s == d) break;
            *s = 0;
        }
    }
    if (*NW_ROOT && at_root(NW_ROOT)) { snprintf(root, sizeof root, "%s", NW_ROOT); return 0; }
    fprintf(stderr, "netherite: not inside a netherite checkout (no AGENTS.md and " ENGINE "/Makefile above here, and the checkout it was built in is gone)\n");
    return 1;
}

/* S, single-quoted for sh, appended to B */
static void q(char *b, size_t n, const char *s)
{
    size_t l = strlen(b);
    if (l + 3 >= n) return;
    b[l++] = '\'';
    for (; *s && l + 5 < n; ++s) {
        if (*s == '\'') { memcpy(b + l, "'\\''", 4); l += 4; }
        else b[l++] = *s;
    }
    b[l++] = '\'';
    b[l] = 0;
}

static void cat(char *b, size_t n, const char *fmt, ...)
{
    size_t l = strlen(b);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b + l, n - l, fmt, ap);
    va_end(ap);
}

/* run a shell command in the checkout; its exit status */
static int sh(const char *cmd)
{
    printf("netherite: $ %s\n", cmd);
    fflush(stdout);
    char full[16384];
    snprintf(full, sizeof full, "cd ");
    q(full, sizeof full, root);
    cat(full, sizeof full, " && %s", cmd);
    int rc = system(full);
    if (rc == -1) return 127;
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : 128 + (WIFSIGNALED(rc) ? WTERMSIG(rc) : 0);
}

static int have(const char *tool)
{
    char c[512];
    snprintf(c, sizeof c, "command -v %s > /dev/null 2>&1", tool);
    return system(c) == 0;
}

static int wants_help(int argc, char **argv)
{
    for (int i = 1; i < argc && strcmp(argv[i], "--"); ++i)
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) return 1;
    return 0;
}

/* argv[i..] after "--", or every argument not taken, as quoted words */
static void rest(char *b, size_t n, int argc, char **argv, int from)
{
    for (int i = from; i < argc; ++i) {
        if (!strcmp(argv[i], "--") && i == from) continue;
        cat(b, n, " ");
        q(b, n, argv[i]);
    }
}

static const char *abspath(const char *p, char *buf)
{
    if (p[0] == '/') return p;
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof cwd)) return p;
    fmt(buf, PATH_MAX, "%s/%s", cwd, p);
    return buf;
}

/* ---------------------------------------------------------------- setup */

static int find_mc(const char *given, char *out)
{
    const char *home = getenv("HOME");
    char c[4][PATH_MAX];
    int n = 0;
    if (given) snprintf(c[n++], PATH_MAX, "%s", given);
    else {
        if (home) {
            snprintf(c[n++], PATH_MAX, "%s/.minecraft", home);
            snprintf(c[n++], PATH_MAX, "%s/Library/Application Support/minecraft", home);
        }
    }
    for (int i = 0; i < n; ++i) {
        char j[PATH_MAX + 64];
        fmt(j, sizeof j, "%s/versions/1.7.10/1.7.10.jar", c[i]);
        if (exists(j)) { fmt(out, PATH_MAX, "%s", c[i]); return 0; }
    }
    fprintf(stderr, "netherite: no Minecraft 1.7.10 install found (looked for versions/1.7.10/1.7.10.jar in");
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
        else { fprintf(stderr, "netherite setup: unknown argument %s (netherite setup --help)\n", argv[i]); return 2; }
    }
    char mcdir[PATH_MAX], a[PATH_MAX];
    if (mc) mc = abspath(mc, a);
    if (find_mc(mc, mcdir)) return 1;
    printf("netherite: Minecraft 1.7.10 from %s\n", mcdir);
    const char *need[] = {"make", "cc", "jq", "unzip", "perl", "git", "curl"};
    int miss = 0;
    for (size_t i = 0; i < sizeof need / sizeof need[0]; ++i)
        if (!have(need[i])) { fprintf(stderr, "netherite: %s is not installed\n", need[i]); miss = 1; }
    if (miss) return 1;
    char jv[PATH_MAX + 16] = "";
    if (jdk) {
        char b[PATH_MAX], j[PATH_MAX + 16];
        jdk = abspath(jdk, b);
        snprintf(j, sizeof j, "%s/bin/javac", jdk);
        if (!exists(j)) { fprintf(stderr, "netherite: %s has no bin/javac (--jdk takes a JDK 8 home)\n", jdk); return 1; }
        snprintf(jv, sizeof jv, " JAVA8=");
        q(jv, sizeof jv, jdk);
    }
    char mv[PATH_MAX + 16] = " MC_HOME=";
    q(mv, sizeof mv, mcdir);
    char c[8192];
    int rc;
    /* 1. the libraries, the client jar (resources) and natives, from the install */
    snprintf(c, sizeof c, "make -C " ORACLE " deps%s%s", mv, jv);
    if ((rc = sh(c))) return rc;
    /* 2. the oracle's source (the decompile, cached once: Docker unless this
     *    checkout has the private history) and its build */
    snprintf(c, sizeof c, "env%s make -C " ORACLE " build%s%s", mv, mv, jv);
    if ((rc = sh(c))) return rc;
    /* 3. the C engine, its tests and tools, out/assets from the jar */
    if ((rc = sh("make -s -C " ENGINE))) return rc;
    /* 4. play's recorded scenes, the two textures cut from them, and the
     *    fresh seed-1 start play and the pipeline begin at */
    snprintf(c, sizeof c, "make -s -C " ORACLE " render-scenes%s && make -s -C " ENGINE " assets", jv);
    if ((rc = sh(c))) return rc;
    char m[PATH_MAX + 64];
    snprintf(m, sizeof m, "%s/out/java/snapshots/fresh-play-s1/manifest.json", root);
    if (!exists(m)) {
        snprintf(c, sizeof c, "make -s -C " ORACLE " script SEED=1 SCRIPT=tests/fresh-play-s1.jsonl "
                              "TAPE=../out/java/snapshots/fresh-play-s1/tape.jsonl%s", jv);
        if ((rc = sh(c))) return rc;
    }
    /* 5. the playable client (SDL3) */
    if (play) {
        if (sh("pkg-config --exists sdl3") == 0) { if ((rc = sh("make -s -C " ENGINE " play"))) return rc; }
        else printf("netherite: SDL3 not found (pkg-config sdl3): the playable client is not built; install SDL3 and run make -C " ENGINE " play\n");
    }
    printf("netherite: setup done. Next: netherite gate --quick (checks the engine against the Java client), netherite play\n");
    return 0;
}

/* ---------------------------------------------------------------- play */

static int v_play(int argc, char **argv)
{
    long seed = 1;
    int i = 1;
    for (; i < argc; ++i) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            char *e;
            seed = strtol(argv[++i], &e, 10);
            if (*e) { fprintf(stderr, "netherite play: --seed takes a number\n"); return 2; }
        } else break;
    }
    char c[8192], m[PATH_MAX + 96];
    snprintf(m, sizeof m, "%s/out/java/snapshots/fresh-play-s%ld/manifest.json", root, seed);
    int rc;
    if (!exists(m)) {
        /* a fresh world of that seed: the oracle records its start */
        snprintf(c, sizeof c, "make -s -C " ORACLE " script SEED=%ld SCRIPT=tests/fresh-play-s1.jsonl "
                              "TAPE=../out/java/snapshots/fresh-play-s%ld/tape.jsonl", seed, seed);
        if ((rc = sh(c))) return rc;
    }
    snprintf(c, sizeof c, "bash " ENGINE "/play/session.sh");
    if (seed != 1) cat(c, sizeof c, " --from out/java/snapshots/fresh-play-s%ld", seed);
    rest(c, sizeof c, argc, argv, i);
    return sh(c);
}

/* ---------------------------------------------------------------- check */

static int v_check(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "netherite check: TAPE (netherite check --help)\n"); return 2; }
    char c[8192] = "bash " ENGINE "/play/check.sh", b[PATH_MAX];
    cat(c, sizeof c, " ");
    q(c, sizeof c, abspath(argv[1], b));
    rest(c, sizeof c, argc, argv, 2);
    return sh(c);
}

/* ---------------------------------------------------------------- bench */

static int v_bench(int argc, char **argv)
{
    char c[8192] = "make -s -C " ENGINE "/runtime pipe-bench";
    if (argc > 1) {
        char a[4096] = "";
        rest(a, sizeof a, argc, argv, 1);
        cat(c, sizeof c, " ARGS=");
        q(c, sizeof c, a + 1);
    }
    return sh(c);
}

/* ---------------------------------------------------------------- gate */

/* the quick set's recordings: script, seed, dev */
static const struct { const char *name, *script; int seed, dev; } QUICK[] = {
    {"fresh-play-s1", "tests/fresh-play-s1.jsonl", 1, 0},
    {"fresh-s42", "tests/fresh-s42.jsonl", 42, 0},
    {"break-place-s1", "tests/break-place-s1.jsonl", 1, 1},
};

static int v_gate(int argc, char **argv)
{
    int quick = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--quick")) quick = 1;
        else { fprintf(stderr, "netherite gate: unknown argument %s (netherite gate --help)\n", argv[i]); return 2; }
    }
    int rc, fail = 0;
    char c[8192];
    if ((rc = sh("make -s -C " ORACLE " gate"))) { fprintf(stderr, "netherite gate: the oracle's gate failed\n"); fail = 1; }
    if ((rc = sh("make -s -C " ENGINE))) return rc;
    if (quick) {
        /* a few recordings made now by the Java client, each replayed by the
         * C engine row for row (the quick set: no stored recordings needed) */
        for (size_t i = 0; i < sizeof QUICK / sizeof QUICK[0]; ++i) {
            char d[PATH_MAX + 96];
            snprintf(d, sizeof d, "%s/out/java/snapshots/%s/manifest.json", root, QUICK[i].name);
            if (!exists(d)) {
                snprintf(c, sizeof c, "make -s -C " ORACLE " script SEED=%d%s SCRIPT=%s TAPE=../out/java/snapshots/%s/tape.jsonl",
                         QUICK[i].seed, QUICK[i].dev ? " DEV=1" : "", QUICK[i].script, QUICK[i].name);
                if ((rc = sh(c))) { fprintf(stderr, "netherite gate: recording %s failed\n", QUICK[i].name); fail = 1; continue; }
            }
            snprintf(c, sizeof c, "out/native/test_snapshots out/java/snapshots/%s", QUICK[i].name);
            if ((rc = sh(c))) { fprintf(stderr, "netherite gate: FAIL %s\n", QUICK[i].name); fail = 1; }
            else printf("netherite gate: PASS %s\n", QUICK[i].name);
        }
    } else {
        if (sh("make -s -C " ENGINE " test")) fail = 1;
        if (sh("make -s -C " ENGINE " clangcheck")) fail = 1;
    }
    printf("netherite gate: %s\n", fail ? "FAIL" : "PASS");
    return fail;
}

/* ---------------------------------------------------------------- train, demo */

/* The run file names its trainer in a directive comment (both parsers skip
 * comments): "# netherite VERB: DIR: COMMAND", DIR relative to the checkout,
 * COMMAND run there by sh with {run} the run file's path and {root} the
 * checkout's. */
static int directive(const char *run, const char *verb, char *dir, size_t dn, char *cmd, size_t cn)
{
    FILE *f = fopen(run, "r");
    if (!f) { fprintf(stderr, "netherite: cannot read %s: %s\n", run, strerror(errno)); return -1; }
    char line[4096], key[64];
    snprintf(key, sizeof key, "# netherite %s:", verb);
    int found = 0;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, key, strlen(key))) continue;
        char *p = line + strlen(key), *colon;
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

static int trainer(const char *verb, int argc, char **argv, const char *dflt)
{
    const char *run = dflt, *tdir = NULL;
    int i = 1;
    if (i < argc && argv[i][0] != '-') run = argv[i++];
    for (; i < argc; ++i) {
        if (!strcmp(argv[i], "--trainer") && i + 1 < argc) tdir = argv[++i];
        else if (!strcmp(argv[i], "--")) { ++i; break; }
        else { fprintf(stderr, "netherite %s: unknown argument %s (netherite %s --help)\n", verb, argv[i], verb); return 2; }
    }
    if (!run) { fprintf(stderr, "netherite %s: RUN.yaml (netherite %s --help)\n", verb, verb); return 2; }
    char rb[PATH_MAX], runabs[PATH_MAX];
    const char *r = abspath(run, rb);
    if (!realpath(r, runabs)) {
        /* a default run file is under the checkout */
        fmt(rb, sizeof rb, "%s/%s", root, run);
        if (!realpath(rb, runabs)) { fprintf(stderr, "netherite %s: no run file %s\n", verb, run); return 2; }
    }
    char dir[PATH_MAX], cmd[4096];
    int d = directive(runabs, verb, dir, sizeof dir, cmd, sizeof cmd);
    if (d < 0) return 2;
    if (!d) {
        fprintf(stderr, "netherite %s: %s names no trainer: add a line\n  # netherite %s: DIR: COMMAND\n"
                        "(DIR the trainer's checkout, relative to this one; COMMAND with {run} and {root})\n", verb, run, verb);
        if (!strcmp(verb, "demo")) fprintf(stderr, "The recorded demo is docs/demo.gif (imitation against GRPO, 16 worlds).\n");
        return 2;
    }
    char td[PATH_MAX + 8], tb[PATH_MAX];
    if (tdir) snprintf(td, sizeof td, "%s", abspath(tdir, tb));
    else if (dir[0] == '/') snprintf(td, sizeof td, "%s", dir);
    else fmt(td, sizeof td, "%s/%s", root, dir);
    if (!exists(td)) {
        fprintf(stderr, "netherite %s: the trainer's checkout %s is not there (clone it, or pass --trainer DIR)\n", verb, td);
        return 2;
    }
    /* {run} and {root}, quoted */
    char c[16384] = "cd ";
    q(c, sizeof c, td);
    cat(c, sizeof c, " && ");
    for (const char *p = cmd; *p; ) {
        if (!strncmp(p, "{run}", 5)) { q(c, sizeof c, runabs); p += 5; }
        else if (!strncmp(p, "{root}", 6)) { q(c, sizeof c, root); p += 6; }
        else { size_t l = strlen(c); if (l + 1 < sizeof c) { c[l] = *p; c[l + 1] = 0; } ++p; }
    }
    rest(c, sizeof c, argc, argv, i);
    return sh(c);
}

static int v_train(int argc, char **argv) { return trainer("train", argc, argv, NULL); }
static int v_demo(int argc, char **argv) { return trainer("demo", argc, argv, "configs/speedrun-grpo.yaml"); }

/* ---------------------------------------------------------------- the verbs */

static int v_help(int argc, char **argv);

static const struct verb VERBS[] = {
    {"setup", "[--mc DIR] [--jdk DIR] [--no-play]", "find your Minecraft 1.7.10, rebuild the oracle and assets from it, build everything",
     "Finds the Minecraft 1.7.10 install (--mc DIR, else ~/.minecraft or\n"
     "~/Library/Application Support/minecraft: a launcher directory with versions/1.7.10/), then:\n"
     "  make -C " ORACLE " deps          the libraries, client jar and natives, copied from the install\n"
     "  make -C " ORACLE " build         the oracle's source (MCP 9.08's decompile in Docker, once, plus\n"
     "                               " ORACLE "/decomp/oracle.patch) and its build; nothing of it is committed\n"
     "  make -C " ENGINE "                 the C engine, its tests and tools, and out/assets from the jar\n"
     "  make -C " ORACLE " render-scenes play's two recorded scenes; the fresh seed-1 start\n"
     "  make -C " ENGINE " play            the playable client, when SDL3 is installed (--no-play skips it)\n"
     "Needs JDK 8 (--jdk DIR, else " ORACLE "/Makefile's JAVA8), make, a C compiler, jq, unzip, perl, git,\n"
     "curl, and Docker for the one decompile.", v_setup},
    {"play", "[--seed N] [SESSION ARGS...]", "play the C engine's client; quitting checks the session against Java",
     "Plays a session of a new world (seed N, default 1) in the native client. When you quit,\n"
     "the Java client replays your inputs and the engine is compared with it row by row\n"
     "(" ENGINE "/play/session.sh; its --continue, --run NAME, --list and --status pass through).\n"
     "A seed's first start is recorded by the oracle once (out/java/snapshots/fresh-play-sN).", v_play},
    {"check", "TAPE", "check a played session's tape against the Java client",
     "The Java client replays TAPE's inputs and the C engine replays Java's rows and compares\n"
     "every field (" ENGINE "/play/check.sh). Prints the first differing row and field on a FAIL.", v_check},
    {"bench", "[PIPE_BENCH ARGS...]", "measure env-steps per second with observations (CUDA)",
     "make -C " ENGINE "/runtime pipe-bench ARGS=\"...\": the RL pipeline's throughput, every step's\n"
     "frame drawn on the GPU (e.g. netherite bench --kinds village --ns 128 --device-mesh 1).\n"
     "Linux with CUDA and clang only.", v_bench},
    {"gate", "[--quick]", "check the tree: the oracle's gates and the engine against Java",
     "--quick: the oracle's gate (make -C " ORACLE " gate), the engine's build, and three recordings\n"
     "made now by the Java client (fresh-play-s1, fresh-s42, break-place-s1), each replayed by\n"
     "the C engine row for row. Needs nothing but setup.\n"
     "Without --quick: the oracle's gate, make -C " ENGINE " test (every stored recording) and clangcheck.", v_gate},
    {"train", "RUN.yaml [--trainer DIR] [-- ARGS...]", "run the trainer a run file names (netherite has no Python)",
     "Runs the command in RUN.yaml's directive line\n"
     "  # netherite train: DIR: COMMAND\n"
     "in DIR (the trainer's checkout, relative to this one, or --trainer DIR), with {run} the run\n"
     "file and {root} this checkout (configs/speedrun-grpo.yaml: mcsr-speedrun's GRPO).", v_train},
    {"demo", "[RUN.yaml] [--trainer DIR]", "run a run file's demo (default configs/speedrun-grpo.yaml)",
     "Runs RUN.yaml's \"# netherite demo: DIR: COMMAND\" directive as train does. Without one it\n"
     "says so and points at docs/demo.gif (imitation against GRPO, 16 worlds).", v_demo},
    {"help", "[VERB]", "this list, or a verb's help", NULL, v_help},
};

static const struct verb *lookup(const char *name)
{
    for (size_t i = 0; i < sizeof VERBS / sizeof VERBS[0]; ++i)
        if (!strcmp(VERBS[i].name, name)) return &VERBS[i];
    return NULL;
}

static void verb_help(const struct verb *v)
{
    printf("netherite %s %s\n  %s\n", v->name, v->args, v->what);
    if (v->help) printf("\n%s\n", v->help);
}

static int v_help(int argc, char **argv)
{
    if (argc > 1) {
        const struct verb *v = lookup(argv[1]);
        if (!v) { fprintf(stderr, "netherite: no verb %s\n", argv[1]); return 2; }
        verb_help(v);
        return 0;
    }
    printf("netherite: Minecraft 1.7.10 in C, exact against the Java client, fast enough for RL.\n\n");
    for (size_t i = 0; i < sizeof VERBS / sizeof VERBS[0]; ++i)
        printf("  netherite %-6s %-40s %s\n", VERBS[i].name, VERBS[i].args, VERBS[i].what);
    printf("\nnetherite VERB --help says what a verb runs. Start with netherite setup.\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2 || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")) return v_help(1, argv);
    const struct verb *v = lookup(argv[1]);
    if (!v) { fprintf(stderr, "netherite: no verb %s (netherite help)\n", argv[1]); return 2; }
    if (v->run != v_help && wants_help(argc - 1, argv + 1)) { verb_help(v); return 0; }
    if (v->run == v_help) return v_help(argc - 1, argv + 1);
    if (find_root()) return 1;
    return v->run(argc - 1, argv + 1);
}
