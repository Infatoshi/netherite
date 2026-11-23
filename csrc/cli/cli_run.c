/* nether CLI: run files and the native trainer (cli_run.h). */
#define _GNU_SOURCE
#include "cli_run.h"
#include "cli_proc.h"

#include <stdlib.h>
#include <string.h>

int cr_path(const char *verb, const char *p, char *out)
{
    char b[PATH_MAX];
    cli_resolve(p, b);
    if (realpath(b, out)) return 0;
    fprintf(stderr, "nether %s: no file %s\n", verb, p);
    return 2;
}

int cr_eval_default(const char *verb, const char *fid, const char *key, char *out, size_t n)
{
    char p[PATH_MAX], err[512], k[64];
    cli_resolve(fid, p);
    struct cy *y = cy_load(p, err, sizeof err);
    snprintf(k, sizeof k, "eval.%s", key);
    const char *v = y ? cy_get(y, k, NULL) : NULL;
    if (v) snprintf(out, n, "%s", v);
    else fprintf(stderr, "nether %s: no eval %s given, and %s has no %s\n", verb, key, y ? p : err, k);
    cy_free(y);
    return v ? 0 : 2;
}

static int make_target(const char *target, const char *var, const char *gpu)
{
    char dir[PATH_MAX + 32], dev[64];
    snprintf(dir, sizeof dir, "%s/csrc/runtime", cli_root);
    snprintf(dev, sizeof dev, "CUDA_DEV=%s", gpu ? gpu : "");
    const char *av[8] = {"make", "-s", "-C", dir, target, dev, var, NULL};
    struct cli_spawn_opts o = {.argv = av};
    struct cli_spawn_res r;
    if (cli_spawn(&o, &r)) return 127;
    return r.rc;
}

int cr_trainer_find(const char *verb, const char *variant, const struct cy *run, const char *gpu, int need_init,
                    struct cr_trainer *t)
{
    memset(t, 0, sizeof *t);
    if (variant) snprintf(t->variant, sizeof t->variant, "%s", variant);
    else {
        const char *obs = run ? cy_get(run, "sim.client.observation", cy_get(run, "client.observation", "")) : "";
        if (!strcmp(obs, "16x16")) t->variant[0] = 0;
        else if (!strcmp(obs, "64x64")) snprintf(t->variant, sizeof t->variant, "s1");
        else {
            fprintf(stderr, "nether %s: client.observation is '%s': the native trainer is built for 16x16 and 64x64 "
                            "(or name a build with --variant V)\n", verb, obs);
            return 2;
        }
    }
    for (const char *p = t->variant; *p; ++p)
        if (!(*p == '.' || *p == '_' || (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z'))) {
            fprintf(stderr, "nether %s: variant %s: letters, digits, '.' and '_' only\n", verb, t->variant);
            return 2;
        }
    cli_fmt(t->bin, sizeof t->bin, "%s/out/runtime/train/%s%sntloop", cli_root, t->variant, t->variant[0] ? "/" : "");
    cli_fmt(t->ntinit, sizeof t->ntinit, "%s/out/runtime/train/%s%sntinit", cli_root, t->variant, t->variant[0] ? "/" : "");
    if (!cli_exists(t->bin)) {
        printf("nether %s: %s is not built: building it\n", verb, t->bin);
        char v[96];
        snprintf(v, sizeof v, "NT_V=%s", t->variant[0] ? t->variant : "w1");
        int rc = t->variant[0] ? make_target("train-loop-v", v, gpu) : make_target(t->bin, "", gpu);
        if (rc || !cli_exists(t->bin)) {
            fprintf(stderr, "nether %s: building %s failed (rc %d)\n", verb, t->bin, rc);
            return rc ? rc : 1;
        }
    }
    if (need_init && !cli_exists(t->ntinit)) {
        int rc = make_target(t->ntinit, "", gpu);
        if (rc || !cli_exists(t->ntinit)) {
            fprintf(stderr, "nether %s: building %s failed (rc %d)\n", verb, t->ntinit, rc);
            return rc ? rc : 1;
        }
    }
    return 0;
}

/* a key of the fidelity file as it is in this run file: sim.X.Y, or X.Y in a file of sim's sections alone */
static const char *in_file(const struct cy *t, const char *key)
{
    if (!strncmp(key, "sim.", 4) && !cy_node(t, "sim")) return key + 4;
    return key;
}

void cr_start_abs(struct cy *t, const char *src_dir)
{
    const char *key = cy_node(t, "sim") ? "sim.pipeline.start" : "pipeline.start";
    const char *s = cy_get(t, key, NULL);
    if (!s || !*s || s[0] == '/') return;
    /* csrc/engine/worldconf.c wconf_start_path: the file's directory, configs/'s parent for a file in configs/ */
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s", src_dir);
    const char *b = strrchr(dir, '/');
    if (b && !strcmp(b + 1, "configs")) dir[b - dir] = 0;
    char *a = xasprintf("%s/%s", dir, s);
    cy_set(t, key, a);
    free(a);
}

int cr_derive(const char *verb, const char *src, const struct sv *sets, const char *fid, const char *dst, struct sv *fargs)
{
    char err[512];
    struct cy *t = cy_load(src, err, sizeof err);
    if (!t) {
        fprintf(stderr, "nether %s: %s\n", verb, err);
        return 2;
    }
    /* the derived file lives elsewhere (out/...): its start as the source file names it, made absolute */
    {
        char sd[PATH_MAX], ab[PATH_MAX];
        snprintf(sd, sizeof sd, "%s", cli_abs(src, ab));
        char *sl = strrchr(sd, '/');
        if (sl) *sl = 0;
        cr_start_abs(t, sd);
    }
    for (int i = 0; sets && i < sets->n; ++i) {
        if (cy_override(t, sets->v[i], 1, err, sizeof err)) {
            fprintf(stderr, "nether %s: %s\n", verb, err);
            cy_free(t);
            return 2;
        }
        printf("nether %s: --set %s\n", verb, sets->v[i]);
    }
    if (fid) {
        struct cy *f = cy_load(fid, err, sizeof err);
        if (!f) {
            fprintf(stderr, "nether %s: the fidelity file: %s\n", verb, err);
            cy_free(t);
            return 2;
        }
        const struct cy *set = cy_node(f, "set");
        for (int i = 0; set && i < set->nk; ++i) {
            const struct cy *k = set->kid[i];
            if (!k->key || !k->val) continue;
            const char *key = in_file(t, k->key), *was = cy_get(t, key, NULL);
            if (!was || strcmp(was, k->val))
                printf("nether %s: exact fidelity: %s %s -> %s (%s; --allow-approx keeps the run file's)\n", verb, key,
                       was ? was : "(unset)", k->val, fid);
            if (cy_set(t, key, k->val)) {
                fprintf(stderr, "nether %s: %s: cannot set %s\n", verb, fid, key);
                cy_free(f);
                cy_free(t);
                return 2;
            }
        }
        const char *args = cy_get(f, "args", NULL);
        if (args && fargs && cli_split_words(args, fargs, err, sizeof err)) {
            fprintf(stderr, "nether %s: %s args: %s\n", verb, fid, err);
            cy_free(f);
            cy_free(t);
            return 2;
        }
        cy_free(f);
    }
    struct sb o = {0};
    sb_printf(&o, "# derived by nether %s from %s; do not edit (rerun with --set)\n", verb, src);
    cy_dump(t, &o);
    cy_free(t);
    int rc = cli_write_file_atomic(dst, o.s, o.n);
    sb_free(&o);
    if (rc) {
        fprintf(stderr, "nether %s: cannot write %s\n", verb, dst);
        return 2;
    }
    return 0;
}

int cr_refused(const char *verb, const char *fid, const struct sv *extra)
{
    char err[512];
    struct cy *f = cy_load(fid, err, sizeof err);
    if (!f) {
        fprintf(stderr, "nether %s: the fidelity file: %s\n", verb, err);
        return 2;
    }
    const char *ref = cy_get(f, "refuse", "");
    int bad = 0;
    for (int i = 0; extra && i < extra->n; ++i) {
        const char *a = extra->v[i];
        for (const char *p = ref; *p;) {
            size_t n = strcspn(p, ",");
            if (n && strlen(a) == n && !strncmp(a, p, n)) {
                fprintf(stderr, "nether %s: %s is not exact fidelity (%s refuse; --allow-approx to run it anyway)\n", verb,
                        a, fid);
                bad = 1;
            }
            p += n;
            if (*p == ',') ++p;
        }
    }
    cy_free(f);
    return bad ? 2 : 0;
}

int cr_section_flags(const char *verb, const struct cy *y, const char *sec, struct sv *out, const char *const *skip)
{
    const struct cy *s = cy_node(y, sec);
    for (int i = 0; s && i < s->nk; ++i) {
        const struct cy *k = s->kid[i];
        if (!k->key || !k->val) continue;
        int sk = 0;
        for (const char *const *p = skip; p && *p; ++p) sk |= !strcmp(*p, k->key);
        if (sk) continue;
        if (!strcmp(k->key, "flags")) {
            char err[256];
            if (cli_split_words(k->val, out, err, sizeof err)) {
                fprintf(stderr, "nether %s: %s.flags: %s\n", verb, sec, err);
                return 2;
            }
            continue;
        }
        char f[96];
        snprintf(f, sizeof f, "--%s", k->key);
        for (char *p = f + 2; *p; ++p)
            if (*p == '_') *p = '-';
        sv_push(out, f);
        sv_push(out, k->val);
    }
    return 0;
}
