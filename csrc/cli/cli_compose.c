/* nether CLI: the three-layer registry, composed (cli_compose.h).
 *
 *   nether train|eval [RUN.yaml] --env E --sim S --algo A [--set KEY=VALUE ...] ...
 *   nether flywheel [GYM.yaml] --env E --sim S --algo A [--budget N] [--set eval.episodes=16] ...
 *   nether harvest --env E --out DIR [--seeds 1..16] ...
 *   nether list
 *
 * A name resolves to configs/envs/NAME.yaml, configs/sim/NAME.yaml or configs/algo/NAME.yaml; a path (one with a '/'
 * or ending .yaml) is taken as it is. The layers merge in a fixed order, sim, then algo, then env, over the run file
 * given (a monolithic one keeps working alone, and a layer only changes the keys it names), then the verb's own
 * --set: so a scenario's reward is never a simulator's, and --set has the last word. What each layer's keys become is
 * the table MAP below: a sim layer's sections are the run file's sim sections; an algo layer's ppo, net and macros are
 * policy's, its variant and critic the trainer's build and flags; an env layer's episode.seconds is
 * sim.pipeline.episode_seconds, its reward policy.task, its actions.keys policy.task.keys, its starts policy.starts,
 * and its eval, harvest and flywheel sections are what nether eval, harvest and flywheel read. sim: and policy:
 * sections pass through any layer as they are. A key no table line takes is refused (a typo never silently does
 * nothing).
 *
 * --set KEY=VALUE whose KEY starts with eval., harvest., train. or flywheel. (or is variant, primary_metric, max_p,
 * min_z, budget_steps) sets the layers' own keys; any other --set is the verb's, on the composed run file.
 *
 * Nothing here knows a scenario, a seed or a host: a harvest mode only says which tool's flags the env's keys become
 * (spawn: rawstarts --near-block and --biome, then test_snapshots on each start; milestone: the steps or the source
 * library the env names, cut at its milestone, then test_snapshots on each start). */
#define _GNU_SOURCE
#include "cli_compose.h"
#include "cli_run.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>

enum { K_ENV, K_SIM, K_ALGO, K_N, K_ANY = K_N };
static const char *const KDIR[K_N] = {"envs", "sim", "algo"};
static const char *const KFLAG[K_N] = {"--env", "--sim", "--algo"};

/* a layer's key (src: a dotted path, or a prefix of one) and where it goes: dst in the run file, or (meta) in the
 * layers' own tree, the gym's and the bank's keys; dst "" is the top. The longest src that matches wins. */
struct cc_map {
    int kind;
    const char *src, *dst;
    int meta;
};
static const struct cc_map MAP[] = {
    {K_ANY, "sim", "sim", 0},
    {K_ANY, "policy", "policy", 0},
    {K_ANY, "name", "name", 1},
    {K_ANY, "description", "description", 1},
    {K_ANY, "flywheel", "", 1},
    {K_SIM, "world", "sim.world", 0},
    {K_SIM, "client", "sim.client", 0},
    {K_SIM, "engine", "sim.engine", 0},
    {K_SIM, "pipeline", "sim.pipeline", 0},
    {K_SIM, "fidelity", "sim.fidelity", 0},
    {K_SIM, "performance", "sim.performance", 0},
    {K_ALGO, "variant", "variant", 1},
    {K_ALGO, "critic", "train.critic", 1},
    {K_ALGO, "train", "train", 1},
    {K_ALGO, "ppo", "policy.ppo", 0},
    {K_ALGO, "net", "policy.net", 0},
    {K_ALGO, "run", "policy.run", 0},
    {K_ALGO, "macros", "policy.actions.macros", 0},
    {K_ALGO, "actions", "policy.actions", 0},
    {K_ENV, "episode.seconds", "sim.pipeline.episode_seconds", 0},
    {K_ENV, "reward", "policy.task", 0},
    {K_ENV, "actions.keys", "policy.task.keys", 0},
    {K_ENV, "actions", "policy.actions", 0},
    {K_ENV, "macros", "policy.actions.macros", 0},
    {K_ENV, "starts", "policy.starts", 0},
    {K_ENV, "harvest", "harvest", 1},
    {K_ENV, "train", "train", 1},
    {K_ENV, "eval.primary_metric", "primary_metric", 1},
    {K_ENV, "eval.max_p", "max_p", 1},
    {K_ENV, "eval.min_z", "min_z", 1},
    {K_ENV, "eval", "eval", 1},
};

/* --set keys that are the layers' own, not the run file's */
static int meta_key(const char *kv)
{
    static const char *const NS[] = {"eval.", "harvest.", "train.", "flywheel."};
    static const char *const ONE[] = {"variant", "primary_metric", "max_p", "min_z", "budget_steps"};
    for (size_t i = 0; i < sizeof NS / sizeof NS[0]; ++i)
        if (!strncmp(kv, NS[i], strlen(NS[i]))) return 1;
    size_t n = strcspn(kv, "=");
    for (size_t i = 0; i < sizeof ONE / sizeof ONE[0]; ++i)
        if (n == strlen(ONE[i]) && !strncmp(kv, ONE[i], n)) return 1;
    return 0;
}

static const struct cc_map *find(int kind, const char *path, int *deeper)
{
    const struct cc_map *best = NULL;
    size_t bl = 0, pl = strlen(path);
    *deeper = 0;
    for (size_t i = 0; i < sizeof MAP / sizeof MAP[0]; ++i) {
        const struct cc_map *m = &MAP[i];
        if (m->kind != kind && m->kind != K_ANY) continue;
        size_t n = strlen(m->src);
        if (!strncmp(path, m->src, n) && (path[n] == 0 || path[n] == '.') && n >= bl) best = m, bl = n;
        if (n > pl && !strncmp(m->src, path, pl) && m->src[pl] == '.') *deeper = 1;
    }
    return best;
}

/* every key of a layer put where the table says */
static int walk(const struct cy *node, const char *path, int kind, const char *file, struct cy *run, struct cy *meta)
{
    for (int i = 0; i < node->nk; ++i) {
        const struct cy *k = node->kid[i];
        if (!k->key) {
            fprintf(stderr, "nether: %s: a list item where a key is wanted (under %s)\n", file, path ? path : "the top");
            return 2;
        }
        char *p = path ? xasprintf("%s.%s", path, k->key) : xstrdup(k->key);
        int deeper, rc = 0;
        const struct cc_map *m = find(kind, p, &deeper);
        int is_map = !k->val && k->nk && k->kid[0]->key;
        if (is_map && deeper) rc = walk(k, p, kind, file, run, meta);
        else if (m) {
            const char *rest = p + strlen(m->src);
            char *dst = *m->dst ? xasprintf("%s%s", m->dst, rest) : xstrdup(*rest == '.' ? rest + 1 : rest);
            if (!*dst ? cy_merge(meta, k) : cy_put(m->meta ? meta : run, dst, k)) {
                fprintf(stderr, "nether: %s: %s (as %s) meets a key of another shape\n", file, p, dst);
                rc = 2;
            }
            free(dst);
        } else {
            fprintf(stderr, "nether: %s:%d: %s is not a key a %s layer takes (csrc/cli/cli_compose.c MAP)\n", file, k->line, p,
                    kind == K_ENV ? "env" : kind == K_SIM ? "sim" : "algo");
            rc = 2;
        }
        free(p);
        if (rc) return rc;
    }
    return 0;
}

/* NAME, or a path: configs/KDIR/NAME.yaml; 0 or 2 (the entries there listed) */
static int resolve(int kind, const char *name, char *out)
{
    size_t n = strlen(name);
    char b[PATH_MAX];
    if (strchr(name, '/') || (n > 5 && !strcmp(name + n - 5, ".yaml"))) {
        if (realpath(cli_resolve(name, b), out)) return 0;
        fprintf(stderr, "nether: %s %s: no such file\n", KFLAG[kind], name);
        return 2;
    }
    cli_fmt(out, PATH_MAX, "%s/configs/%s/%s.yaml", cli_root, KDIR[kind], name);
    if (cli_exists(out)) return 0;
    fprintf(stderr, "nether: %s %s: no configs/%s/%s.yaml (nether list shows the entries)\n", KFLAG[kind], name, KDIR[kind], name);
    return 2;
}

static void stem(const char *path, char *out, size_t n)
{
    const char *s = strrchr(path, '/');
    snprintf(out, n, "%s", s ? s + 1 : path);
    char *d = strrchr(out, '.');
    if (d && !strcmp(d, ".yaml")) *d = 0;
}

/* tree written as out/compose/LABEL-HASH.EXT (HASH: the first 8 hex digits of its body's SHA-256, so a changed --set
 * is a new file and two runs of one composition share one), path in out */
static int write_composed(const struct cy *t, const char *label, const char *ext, const char *header, char *out)
{
    struct sb body = {0}, all = {0};
    cy_dump(t, &body);
    uint8_t d[32];
    char hex[65];
    cli_sha256(body.s ? body.s : "", body.n, d);
    cli_hex(d, 4, hex);
    char dir[PATH_MAX];
    cli_fmt(dir, sizeof dir, "%s/out/compose", cli_root);
    if (cli_mkdirs(dir)) {
        fprintf(stderr, "nether: cannot make %s\n", dir);
        sb_free(&body);
        return 2;
    }
    cli_fmt(out, PATH_MAX, "%s/%s-%s.%s", dir, label, hex, ext);
    sb_puts(&all, header);
    sb_puts(&all, body.s ? body.s : "");
    int rc = cli_write_file_atomic(out, all.s, all.n);
    sb_free(&body), sb_free(&all);
    if (rc) fprintf(stderr, "nether: cannot write %s\n", out);
    return rc ? 2 : 0;
}

struct cc {
    char lay[K_N][PATH_MAX];  /* each layer's file, "" when not named */
    char base[PATH_MAX];      /* the positional file, "" none */
    char label[256];
    struct cy *run, *meta;
    struct sv msets;          /* --set of the layers' own keys */
    struct sv rest;           /* every other argument, in order, but the positional */
};

static void cc_free(struct cc *c)
{
    cy_free(c->run);
    cy_free(c->meta);
    sv_free(&c->msets);
    sv_free(&c->rest);
}

/* argv into the layers, the base (the first positional before any --), the layers' --set and the rest; the number
 * of layers named, or -1 on an error */
static int take(const char *verb, int argc, char **argv, struct cc *c)
{
    int n = 0, seen_dd = 0;
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (seen_dd) {
            sv_push(&c->rest, a);
            continue;
        }
        if (!strcmp(a, "--")) {
            seen_dd = 1;
            sv_push(&c->rest, a);
            continue;
        }
        int k = -1;
        for (int j = 0; j < K_N; ++j)
            if (!strcmp(a, KFLAG[j])) k = j;
        if (k >= 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "nether %s: %s takes a name or a path\n", verb, a);
                return -1;
            }
            if (resolve(k, argv[++i], c->lay[k])) return -1;
            ++n;
        } else if (!strcmp(a, "--set") && i + 1 < argc && meta_key(argv[i + 1])) sv_push(&c->msets, argv[++i]);
        else if (a[0] != '-' && !c->base[0] && c->rest.n == 0) {
            char b[PATH_MAX];
            if (!realpath(cli_resolve(a, b), c->base)) {
                fprintf(stderr, "nether %s: no file %s\n", verb, a);
                return -1;
            }
        } else {
            sv_push(&c->rest, a);
            /* a flag's value is never the base */
            if (a[0] == '-' && i + 1 < argc && argv[i + 1][0] != '-') sv_push(&c->rest, argv[++i]);
        }
    }
    return n;
}

/* the run file and the layers' tree: base, sim, algo, env, then the layers' --set */
static int compose(const char *verb, struct cc *c, int base_is_run)
{
    char err[512];
    c->meta = cy_parse("", "meta", err, sizeof err);
    if (base_is_run && c->base[0]) {
        c->run = cy_load(c->base, err, sizeof err);
        if (!c->run) {
            fprintf(stderr, "nether %s: %s\n", verb, err);
            return 2;
        }
        if (!cy_node(c->run, "sim") && !cy_node(c->run, "policy")) {
            /* a file without parts is sim (AGENTS.md) */
            struct cy *w = cy_parse("", "run", err, sizeof err);
            cy_put(w, "sim", c->run);
            cy_free(c->run);
            c->run = w;
        }
        char bd[PATH_MAX];
        snprintf(bd, sizeof bd, "%s", c->base);
        char *sl = strrchr(bd, '/');
        if (sl) *sl = 0;
        cr_start_abs(c->run, bd);
    } else
        c->run = cy_parse("", "run", err, sizeof err);
    static const int ORDER[K_N] = {K_SIM, K_ALGO, K_ENV};
    for (int o = 0; o < K_N; ++o) {
        int k = ORDER[o];
        if (!c->lay[k][0]) continue;
        struct cy *l = cy_load(c->lay[k], err, sizeof err);
        if (!l) {
            fprintf(stderr, "nether %s: %s\n", verb, err);
            return 2;
        }
        /* an env's name is the composition's; a sim's or an algo's describes only itself */
        struct cy *m = k == K_ENV ? c->meta : cy_parse("", "m", err, sizeof err);
        int rc = walk(l, NULL, k, c->lay[k], c->run, m);
        if (k != K_ENV) {
            const struct cy *nm = cy_node(m, "name"), *ds = cy_node(m, "description");
            (void)nm, (void)ds;
            for (int i = 0; i < m->nk; ++i)
                if (strcmp(m->kid[i]->key, "name") && strcmp(m->kid[i]->key, "description") && !rc &&
                    cy_put(c->meta, m->kid[i]->key, m->kid[i]))
                    rc = 2;
            cy_free(m);
        }
        cy_free(l);
        if (rc) return rc;
    }
    /* a layer's paths are the checkout's (as a file in configs/ names them): the composed file lives in out/compose */
    cr_start_abs(c->run, cli_root);
    for (int i = 0; i < c->msets.n; ++i) {
        const char *kv = c->msets.v[i], *eq = strchr(kv, '=');
        if (!eq) {
            fprintf(stderr, "nether %s: --set %s: KEY=VALUE\n", verb, kv);
            return 2;
        }
        char *key = xstrndup(kv, (size_t)(eq - kv));
        int rc = cy_set(c->meta, key, eq + 1);
        free(key);
        if (rc) {
            fprintf(stderr, "nether %s: --set %s runs through a scalar\n", verb, kv);
            return 2;
        }
        printf("nether %s: --set %s (the layers')\n", verb, kv);
    }
    /* the label: the env's name (else the base's), then the sim's and the algo's */
    char a[96] = "", s[96] = "", g[96] = "";
    if (c->lay[K_ENV][0]) stem(c->lay[K_ENV], a, sizeof a);
    else if (c->base[0]) stem(c->base, a, sizeof a);
    if (c->lay[K_SIM][0]) stem(c->lay[K_SIM], s, sizeof s);
    if (c->lay[K_ALGO][0]) stem(c->lay[K_ALGO], g, sizeof g);
    snprintf(c->label, sizeof c->label, "%s%s%s%s%s", a[0] ? a : "run", s[0] ? "+" : "", s, g[0] ? "+" : "", g);
    return 0;
}

static void header(struct sb *h, const char *verb, const struct cc *c)
{
    sb_printf(h, "# composed by nether %s; do not edit (the layers and --set make it again)\n", verb);
    if (c->base[0]) sb_printf(h, "#   base %s\n", c->base);
    static const int ORDER[K_N] = {K_SIM, K_ALGO, K_ENV};
    for (int o = 0; o < K_N; ++o)
        if (c->lay[ORDER[o]][0]) sb_printf(h, "#   %-4s %s\n", KFLAG[ORDER[o]] + 2, c->lay[ORDER[o]]);
}

/* the meta tree's section as flags (cr_section_flags), skipping names */
static int flags_of(const struct cc *c, const char *sec, struct sv *out, const char *const *skip)
{
    return cr_section_flags("compose", c->meta, sec, out, skip);
}

static int has_flag(const struct sv *l, const char *f)
{
    for (int i = 0; i < l->n && strcmp(l->v[i], "--"); ++i)
        if (!strcmp(l->v[i], f)) return 1;
    return 0;
}

/* the bank of an env's harvest section: the mode's tool flags filled in where the env does not name them */
static int bank_of(const char *verb, struct cc *c, struct cy **out)
{
    char err[512];
    const struct cy *h = cy_node(c->meta, "harvest");
    if (!h) {
        fprintf(stderr, "nether %s: %s has no harvest section\n", verb, c->lay[K_ENV]);
        return 2;
    }
    struct cy *b = cy_parse("", "bank", err, sizeof err);
    const char *mode = cy_get(h, "mode", "");
    static const char *const BANK[] = {"seeds", "seeds_file", "seeds_tsv", "seeds_column", "exclude", "exclude_files",
                                       "split_train", "split_key"};
    static const char *const SKIP[] = {"mode", "seed_set", "milestone", "near_block", "near_radius", "biome", "record"};
    for (int i = 0; i < h->nk; ++i) {
        const struct cy *k = h->kid[i];
        int dst = 0; /* 0 harvest., 1 bank., 2 skipped */
        for (size_t j = 0; j < sizeof BANK / sizeof BANK[0]; ++j) dst = !strcmp(k->key, BANK[j]) ? 1 : dst;
        for (size_t j = 0; j < sizeof SKIP / sizeof SKIP[0]; ++j) dst = !strcmp(k->key, SKIP[j]) ? 2 : dst;
        if (dst == 2) continue;
        char *p = xasprintf("%s.%s", dst ? "bank" : "harvest", k->key);
        cy_put(b, p, k);
        free(p);
    }
    if (!cy_get(b, "bank.split_key", NULL)) cy_set(b, "bank.split_key", cy_get(c->meta, "name", c->label));
    const char *ss = cy_get(h, "seed_set", NULL);
    if (ss) {
        char a[PATH_MAX];
        cy_set(b, "vars.seed_set", cli_resolve(ss, a));
    }
    if (cy_get(h, "milestone", NULL)) cy_set(b, "vars.milestone", cy_get(h, "milestone", ""));
#define DFLT(k, v)                                                                                                             \
    do {                                                                                                                       \
        if (!cy_node(b, k)) cy_set(b, k, v);                                                                                   \
    } while (0)
    if (!strcmp(mode, "spawn")) {
        const char *nb = cy_get(h, "near_block", NULL), *nr = cy_get(h, "near_radius", NULL), *bi = cy_get(h, "biome", "any");
        if (!ss || !nb || !nr) {
            fprintf(stderr, "nether %s: %s: harvest.mode spawn needs harvest.seed_set, near_block and near_radius\n", verb,
                    c->lay[K_ENV]);
            cy_free(b);
            return 2;
        }
        if (!cy_node(b, "harvest.prepare")) {
            cy_set(b, "harvest.prepare.native", "make -s -C csrc ../out/native/test_snapshots");
            cy_set(b, "harvest.prepare.rawstarts", "make -s -C csrc/runtime rawstarts");
        }
        char *cmd = xasprintf("out/runtime/train/rawstarts --near-block %s:%s%s%s --set {seed_set} --seeds {seed} --out {work}/lib",
                              nb, nr, strcmp(bi, "any") ? " --biome " : "", strcmp(bi, "any") ? bi : "");
        DFLT("harvest.cmd", cmd);
        free(cmd);
        DFLT("harvest.reject", "^RAWSTARTS logs 0 kept");
        DFLT("harvest.index", "{work}/lib/index.tsv");
        DFLT("harvest.verify", "out/native/test_snapshots {start}");
        if (cy_get(h, "record", NULL)) {
            DFLT("harvest.if_missing", "{seed_set}/s{seed}/manifest.json");
            DFLT("harvest.first", cy_get(h, "record", ""));
        }
    } else if (!strcmp(mode, "milestone")) {
        const char *ms = cy_get(h, "milestone", NULL);
        if (!ms || (!cy_get(h, "source", NULL) && !cy_node(h, "steps") && !cy_get(h, "cmd", NULL))) {
            fprintf(stderr, "nether %s: %s: harvest.mode milestone needs harvest.milestone and harvest.source (a library of "
                            "Java-proved starts) or harvest.steps\n", verb, c->lay[K_ENV]);
            cy_free(b);
            return 2;
        }
        DFLT("harvest.index_stage", ms);
        DFLT("harvest.index", "{work}/lib/index.tsv");
        DFLT("harvest.verify", "out/native/test_snapshots {start}");
        if (!cy_node(b, "harvest.prepare")) cy_set(b, "harvest.prepare.native", "make -s -C csrc ../out/native/test_snapshots");
    } else if (*mode) {
        fprintf(stderr, "nether %s: %s: harvest.mode %s: spawn or milestone\n", verb, c->lay[K_ENV], mode);
        cy_free(b);
        return 2;
    }
#undef DFLT
    *out = b;
    return 0;
}

int cc_dispatch(const char *verb, int argc, char **argv, int (*run)(int, char **))
{
    int named = 0;
    for (int i = 1; i < argc && strcmp(argv[i], "--"); ++i)
        for (int k = 0; k < K_N; ++k) named |= !strcmp(argv[i], KFLAG[k]);
    if (!named) return run(argc, argv);
    struct cc c;
    memset(&c, 0, sizeof c);
    int rc = take(verb, argc, argv, &c) < 0 ? 2 : 0;
    int is_fly = !strcmp(verb, "flywheel"), is_harv = !strcmp(verb, "harvest");
    if (!rc && is_harv && c.base[0]) {
        fprintf(stderr, "nether harvest: a bank file or --env, not both\n");
        rc = 2;
    }
    if (!rc) rc = compose(verb, &c, !is_fly && !is_harv);
    struct sv av = {0};
    struct sb h = {0};
    header(&h, verb, &c);
    char runp[PATH_MAX] = "", other[PATH_MAX] = "";
    if (!rc && !is_harv) rc = write_composed(c.run, c.label, "run.yaml", h.s, runp);
    if (!rc && runp[0]) printf("nether %s: composed %s\n", verb, runp);
    if (rc) goto out;
    sv_push(&av, verb);
    if (!strcmp(verb, "train")) {
        sv_push(&av, runp);
        const char *v = cy_get(c.meta, "variant", NULL);
        if (v && !has_flag(&c.rest, "--variant")) sv_push(&av, "--variant"), sv_push(&av, v);
        if ((rc = flags_of(&c, "train", &av, NULL))) goto out;
        for (int i = 0; i < c.rest.n; ++i) sv_push(&av, c.rest.v[i]);
    } else if (!strcmp(verb, "eval")) {
        sv_push(&av, runp);
        int dd = c.rest.n;
        for (int i = 0; i < c.rest.n; ++i)
            if (!strcmp(c.rest.v[i], "--")) {
                dd = i;
                break;
            }
        for (int i = 0; i < dd; ++i) sv_push(&av, c.rest.v[i]);
        const char *v = cy_get(c.meta, "variant", NULL);
        if (v && !has_flag(&c.rest, "--variant")) sv_push(&av, "--variant"), sv_push(&av, v);
        static const char *const OWN[] = {"episodes", "minutes", "seed", NULL};
        for (const char *const *o = OWN; *o; ++o) {
            char k[32], f[32];
            snprintf(k, sizeof k, "eval.%s", *o);
            snprintf(f, sizeof f, "--%s", *o);
            if (cy_get(c.meta, k, NULL) && !has_flag(&c.rest, f)) sv_push(&av, f), sv_push(&av, cy_get(c.meta, k, ""));
        }
        sv_push(&av, "--");
        if ((rc = flags_of(&c, "eval", &av, OWN))) goto out;
        for (int i = dd + 1; i < c.rest.n; ++i) sv_push(&av, c.rest.v[i]);
    } else if (is_fly) {
        char err[512];
        struct cy *g = c.base[0] ? cy_load(c.base, err, sizeof err) : cy_parse("", "gym", err, sizeof err);
        if (!g) {
            fprintf(stderr, "nether flywheel: %s\n", err);
            rc = 2;
            goto out;
        }
        if (cy_merge(g, c.meta)) {
            fprintf(stderr, "nether flywheel: the layers' keys meet the gym's of another shape\n");
            cy_free(g);
            rc = 2;
            goto out;
        }
        cy_set(g, "train_config", runp);
        cy_set(g, "eval_config", runp);
        cy_set(g, "name", c.label); /* the log directory: one composition's results and incumbent apart from another's */
        rc = write_composed(g, c.label, "gym.yaml", h.s, other);
        cy_free(g);
        if (rc) goto out;
        printf("nether flywheel: composed %s\n", other);
        sv_push(&av, other);
        for (int i = 0; i < c.rest.n; ++i) sv_push(&av, c.rest.v[i]);
    } else if (is_harv) {
        struct cy *b = NULL;
        if ((rc = bank_of(verb, &c, &b))) goto out;
        rc = write_composed(b, c.label, "bank.yaml", h.s, other);
        cy_free(b);
        if (rc) goto out;
        printf("nether harvest: composed %s\n", other);
        sv_push(&av, other);
        for (int i = 0; i < c.rest.n; ++i) sv_push(&av, c.rest.v[i]);
    } else {
        fprintf(stderr, "nether %s: takes no --env, --sim or --algo\n", verb);
        rc = 2;
        goto out;
    }
    rc = run(av.n, av.v);
out:
    sv_free(&av);
    sb_free(&h);
    cc_free(&c);
    return rc;
}

/* ---- nether list */

/* a layer file's description: its description: key, else its first comment line */
static void describe(const char *path, char *out, size_t n)
{
    *out = 0;
    char *t = cli_read_file(path, NULL);
    if (!t) return;
    const char *d = strstr(t, "\ndescription:");
    if (d || !strncmp(t, "description:", 12)) {
        const char *p = (d ? d + 1 : t) + 12;
        while (*p == ' ') ++p;
        size_t l = strcspn(p, "\n");
        if (l && (*p == '"' || *p == '\'') && p[l - 1] == *p) ++p, l -= 2;
        snprintf(out, n, "%.*s", (int)l, p);
    } else if (t[0] == '#') {
        const char *p = t + 1;
        while (*p == ' ') ++p;
        snprintf(out, n, "%.*s", (int)strcspn(p, "\n"), p);
    }
    free(t);
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

int v_list(int argc, char **argv)
{
    (void)argc, (void)argv;
    for (int k = 0; k < K_N; ++k) {
        char dir[PATH_MAX];
        cli_fmt(dir, sizeof dir, "%s/configs/%s", cli_root, KDIR[k]);
        DIR *d = opendir(dir);
        struct sv names = {0};
        for (struct dirent *e; d && (e = readdir(d));) {
            size_t l = strlen(e->d_name);
            if (l > 5 && !strcmp(e->d_name + l - 5, ".yaml")) sv_push(&names, e->d_name);
        }
        if (d) closedir(d);
        if (names.n) qsort(names.v, (size_t)names.n, sizeof *names.v, cmp_str);
        printf("%s (configs/%s, %s NAME):\n", KDIR[k], KDIR[k], KFLAG[k]);
        for (int i = 0; i < names.n; ++i) {
            char p[PATH_MAX + 300], ds[200];
            cli_fmt(p, sizeof p, "%s/%s", dir, names.v[i]);
            describe(p, ds, sizeof ds);
            names.v[i][strlen(names.v[i]) - 5] = 0;
            printf("  %-18s %s\n", names.v[i], ds);
        }
        if (!names.n) printf("  (none)\n");
        sv_free(&names);
    }
    printf("compose: nether train|eval|flywheel|harvest --env NAME [--sim NAME] [--algo NAME] (merged sim, algo, env, --set)\n");
    return 0;
}
