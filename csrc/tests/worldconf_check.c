/* engine/worldconf.h, config.yaml's C reader, against the oracle's:
 *   1. config.yaml lists every registry key, in the registry's order, at
 *      its default (so the registry here and WorldConf.KEYS, which the
 *      oracle checks config.yaml against on every run, name the same keys);
 *   2. config.yaml and every configs/NAME.yaml read, and the start each
 *      names (pipeline.start, a recording the oracle made from that file)
 *      matches it: its manifest's world and its tape header's options;
 *   3. what the grammar refuses (tabs, unknown keys and sections, values
 *      outside a kind, a key twice, an entry before a section, other
 *      indents, quotes, flow and lists), in both forms (a run file's sim and
 *      policy parts: policy held to the grammar only), --set's bare world
 *      keys and sim.section.key, and sim.actions' refusal of anything but
 *      its defaults (wconf_unimplemented).
 *   worldconf_check [ROOT]   (make worldconf-check; make test runs it) */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/tape.h"
#include "../engine/worldconf.h"

static int bad;

static void expect(int ok, const char *what)
{
    if (!ok)
    {
        printf("FAIL %s\n", what);
        ++bad;
    }
}

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *t = malloc((size_t)len + 1);
    size_t got = fread(t, 1, (size_t)len, f);
    fclose(f);
    t[got] = 0;
    return t;
}

static char *first_line(const char *path)
{
    char *t = slurp(path);
    if (t && strchr(t, '\n')) *strchr(t, '\n') = 0;
    return t;
}

/* 1: config.yaml's entries are the registry, in order, at the defaults */
static void registry(const char *path)
{
    char *t = slurp(path);
    if (!t) { expect(0, "config.yaml reads"); return; }
    char section[64] = "";
    int k = 0;
    for (char *line = strtok(t, "\n"); line; line = strtok(NULL, "\n"))
    {
        char name[64], val[WCONF_VAL];
        if (line[0] >= 'a' && line[0] <= 'z' && sscanf(line, "%63[a-z0-9_]:", section) == 1) continue;
        if (line[0] != ' ' || line[1] != ' ' || line[2] < 'a' || line[2] > 'z') continue;
        if (sscanf(line + 2, "%63[a-z0-9_]: %159s", name, val) != 2) continue;
        char key[160], what[400];
        snprintf(key, sizeof key, "%s.%s", section, name);
        snprintf(what, sizeof what, "config.yaml's entry %d is %s = %s, the registry's %s = %s", k, key, val,
                 k < WC_NKEYS ? WCONF_KEYS[k].key : "(none)", k < WC_NKEYS ? WCONF_KEYS[k].def : "");
        expect(k < WC_NKEYS && !strcmp(key, WCONF_KEYS[k].key) && !strcmp(val, WCONF_KEYS[k].def), what);
        ++k;
    }
    char what[128];
    snprintf(what, sizeof what, "config.yaml lists %d keys, the registry %d", k, WC_NKEYS);
    expect(k == WC_NKEYS, what);
    free(t);
}

/* 2: a config and its start */
static void config(const char *path)
{
    struct wconf c;
    char err[512], what[1200];
    int rc = wconf_load(&c, path, err, sizeof err);
    snprintf(what, sizeof what, "%s reads: %s", path, err);
    expect(rc == 0, what);
    if (rc) return;
    char start[1024], mf[1100], tp[1100];
    wconf_start_path(&c, start, sizeof start);
    snprintf(mf, sizeof mf, "%s/manifest.json", start);
    snprintf(tp, sizeof tp, "%s/tape.jsonl", start);
    char *m = slurp(mf), *h = first_line(tp);
    if (!m || !h)
    {
        /* a tree without the recordings (the Mac's): nothing to match */
        printf("skip %s: no %s\n", path, start);
        free(m);
        free(h);
        return;
    }
    struct jval *mj = json_parse(m), *hj = json_parse(h);
    err[0] = 0;
    int ok = mj && hj && wconf_match_start(&c, json_get(mj, "world"), json_get(hj, "options"), 1, err, sizeof err);
    snprintf(what, sizeof what, "%s is its start %s: %s", path, start, err);
    expect(ok, what);
    /* and one key off is refused */
    struct wconf d = c;
    wconf_set(&d, wconf_difficulty(&c) == 0 ? "difficulty=normal" : "difficulty=peaceful", err, sizeof err);
    expect(mj && hj && !wconf_match_start(&d, json_get(mj, "world"), json_get(hj, "options"), 1, err, sizeof err),
           "a start at another difficulty is refused");
    d = c;
    wconf_set(&d, "villages=off", err, sizeof err);
    expect(mj && hj && !wconf_match_start(&d, json_get(mj, "world"), json_get(hj, "options"), 1, err, sizeof err),
           "a start with another switch is refused");
    json_free(mj);
    json_free(hj);
    printf("ok %s: start %s\n", path, start);
}

static int parses(const char *text)
{
    struct wconf c;
    char err[512];
    wconf_defaults(&c);
    return wconf_parse(&c, text, "t", err, sizeof err) == 0;
}

static void grammar(void)
{
    static const char *const good[] = {
        "world:\n  seed: 7\n",
        "# c\n\nworld:   # c\n  seed: -7    # c\n  difficulty: hard\n\n  # c\nclient:\n  observation: 64x96\n",
        "world:\r\n  seed: 7\r\n",
        "pipeline:\n  start: /abs/dir/x-1.2\n  kind: a_1\n",
        "",
        "pipeline:\n  cards: 0,1,12\n  worlds: 64\n",
        /* a run file: parts, sections two in, entries four in; policy held to the grammar only */
        "sim:\n  world:\n    seed: 7\npolicy:\n  grpo:\n    lr: 1e-05   # c\n  placement:\n    cards: 2,3\n",
        "# c\npolicy:\n  anything:\n    goes_here: a/b.c\n\nsim:   # c\n  client:\n    observation: 64x96\n",
        "sim:\n",
        "policy:\n  model:\n    ckpt: x.pt\n",
    };
    static const char *const refused[] = {
        "world:\n\tseed: 7\n",              /* tab */
        "world:\n  seed:\t7\n",
        "world:\n  sed: 7\n",               /* unknown key */
        "wrld:\n  seed: 7\n",               /* unknown section */
        "  seed: 7\n",                      /* entry before a section */
        "world:\n   seed: 7\n",             /* three spaces */
        "world:\n seed: 7\n",
        "world:\n    seed: 7\n",
        "world:\n  seed: 7\n  seed: 8\n",   /* a key twice */
        "world:\n  seed: 7\nworld:\n  villages: on\n",
        "world:\n  seed: \"7\"\n",           /* quotes */
        "world:\n  seed: '7'\n",
        "world:\n  seed: x\n",              /* kinds */
        "world:\n  seed: 12345678901234567890\n",
        "world:\n  villages: true\n",
        "world:\n  villages: yes\n",
        "world:\n  difficulty: Peaceful\n",
        "world:\n  render_distance: 1\n",
        "world:\n  render_distance: 17\n",
        "client:\n  observation: 128\n",
        "client:\n  observation: 1x128\n",
        "engine:\n  device_mesh: 3\n",
        "pipeline:\n  kind: Head\n",
        "world:\n  seed: 7#c\n",           /* a comment needs a space before it */
        "world:#c\n  seed: 7\n",
        "world:\n  seed:7\n",               /* ": " */
        "world:\n  seed: 7 8\n",
        "world: {seed: 7}\n",               /* flow, lists, nesting */
        "world:\n  seed: [7]\n",
        "world:\n  - seed: 7\n",
        "world:\n  seed:\n    x: 7\n",
        "world:\n  seed: &a 7\n",
        "World:\n  seed: 7\n",
        "pipeline:\n  cards: 0,\n",       /* cards */
        "pipeline:\n  cards: 100\n",
        "pipeline:\n  cards: ,1\n",
        "pipeline:\n  worlds: 0\n",
        "actions:\n  look_ticks: 0\n",
        "world:\n  seed: 7,8\n",          /* a comma is a value character, not a long's */
        "sim:\n  world:\n  seed: 7\n",   /* run files: an entry two in */
        "sim:\n  world:\n     seed: 7\n",
        "sim:\n  world:\n   seed: 7\n",
        "sim:\nworld:\n  seed: 7\n",     /* a section at column 0 in a run file */
        "world:\n  seed: 7\nsim:\n  world:\n    seed: 7\n",   /* a part after sections */
        "sim:\n  world:\n    seed: 7\nsim:\n  client:\n    hud: on\n",   /* a part twice */
        "sim:\n  world:\n    seed: 7\n  world:\n    villages: on\n",
        "sim:\n  world:\n    seed: 7\n    seed: 8\n",
        "sim:\n  wrld:\n    seed: 7\n",
        "sim:\n  world:\n    sed: 7\n",
        "sim:\n  world:\n    seed: x\n",
        "sim:\n    seed: 7\n",           /* an entry before a section */
        "policy:\n  grpo:\n    lr: 1e-05\n    lr: 2\n",   /* policy: the grammar holds */
        "policy:\n  grpo:\n    lr: \"1\"\n",
        "policy:\n  grpo:\n    lr:1\n",
        "policy:\n  grpo:\n    lr: [1]\n",
        "policy:\n  grpo:\n  lr: 1\n",
        "policy:\n  Grpo:\n    lr: 1\n",
        "policy:\n  grpo:\n    lr: 1\n  grpo:\n    clip: 1\n",
        "train:\n  grpo:\n    lr: 1\n",   /* only sim and policy */
        "sim:\n  world:\n    seed: 7\ntrain:\n  x:\n    y: 1\n",
    };
    for (size_t i = 0; i < sizeof good / sizeof *good; ++i)
    {
        char what[256];
        snprintf(what, sizeof what, "reads: %.200s", good[i]);
        expect(parses(good[i]), what);
    }
    for (size_t i = 0; i < sizeof refused / sizeof *refused; ++i)
    {
        char what[256];
        snprintf(what, sizeof what, "refused: %.200s", refused[i]);
        expect(!parses(refused[i]), what);
    }
    struct wconf c;
    char err[512];
    wconf_defaults(&c);
    expect(wconf_set(&c, "seed=42", err, sizeof err) == 0 && !strcmp(wconf_get(&c, WC_SEED), "42"), "--set seed=42");
    expect(wconf_set(&c, "world.difficulty=peaceful", err, sizeof err) == 0 && wconf_difficulty(&c) == 0, "--set world.difficulty");
    expect(wconf_set(&c, "client.observation=64x64", err, sizeof err) == 0, "--set client.observation");
    int w, h;
    wconf_size(&c, &w, &h);
    expect(w == 64 && h == 64, "observation 64x64");
    expect(wconf_set(&c, "observation=64x64", err, sizeof err) != 0, "--set observation (a bare key is a world key)");
    expect(wconf_set(&c, "seed", err, sizeof err) != 0, "--set without =");
    expect(wconf_set(&c, "sim.world.seed=43", err, sizeof err) == 0 && !strcmp(wconf_get(&c, WC_SEED), "43"), "--set sim.world.seed");
    expect(wconf_set(&c, "sim.client.hud=on", err, sizeof err) == 0 && wconf_on(&c, WC_HUD), "--set sim.client.hud");
    expect(wconf_set(&c, "policy.grpo.lr=1", err, sizeof err) != 0, "--set policy.* (the trainer's)");
    expect(wconf_set(&c, "pipeline.cards=0,1", err, sizeof err) == 0 && !strcmp(wconf_get(&c, WC_CARDS), "0,1"), "--set pipeline.cards");
    /* a run file reads sim's values and skips policy's */
    wconf_defaults(&c);
    expect(wconf_parse(&c, "sim:\n  world:\n    seed: 9\n  pipeline:\n    worlds: 64\npolicy:\n  world:\n    seed: 5\n", "t", err, sizeof err) == 0
               && !strcmp(wconf_get(&c, WC_SEED), "9") && wconf_int(&c, WC_WORLDS) == 64,
           "a run file's sim values, policy's skipped");
    /* sim.actions: every key at any value is implemented (lane/guiact's
     * click_interval .. recipes, lane/moveact's hold_attack .. look_ticks) */
    wconf_defaults(&c);
    expect(wconf_unimplemented(&c, err, sizeof err) == 0, "the default actions are implemented");
    for (int k = WC_CLICK_INTERVAL; k <= WC_RECIPES; ++k)
    {
        struct wconf d = c;
        char kv[200], what[300];
        snprintf(kv, sizeof kv, "%s=%s", WCONF_KEYS[k].key, !strcmp(WCONF_KEYS[k].kind, "onoff") ? "on" : "3");
        snprintf(what, sizeof what, "%s taken (lane/guiact)", kv);
        expect(wconf_set(&d, kv, err, sizeof err) == 0 && wconf_unimplemented(&d, err, sizeof err) == 0, what);
    }
    for (int k = WC_HOLD_ATTACK; k <= WC_LOOK_TICKS; ++k)
    {
        struct wconf d = c;
        char kv[200], what[300];
        snprintf(kv, sizeof kv, "%s=%s", WCONF_KEYS[k].key, !strcmp(WCONF_KEYS[k].kind, "onoff") ? "on" : "2");
        int impl = k >= WC_HOLD_ATTACK && k <= WC_LOOK_TICKS;   /* lane/moveact's */
        snprintf(what, sizeof what, "%s %s", kv, impl ? "taken (implemented)" : "refused as not implemented yet");
        expect(wconf_set(&d, kv, err, sizeof err) == 0 &&
                   (impl ? wconf_unimplemented(&d, err, sizeof err) == 0
                         : wconf_unimplemented(&d, err, sizeof err) != 0 && strstr(err, "not implemented yet")), what);
    }
    /* named configs resolve their start against the tree, not configs/ */
    char p[256];
    snprintf(c.path, sizeof c.path, "/r/configs/x.yaml");
    snprintf(c.val[WC_START], WCONF_VAL, "out/s");
    wconf_start_path(&c, p, sizeof p);
    expect(!strcmp(p, "/r/out/s"), "configs/x.yaml's start under the tree");
    snprintf(c.path, sizeof c.path, "/r/config.yaml");
    wconf_start_path(&c, p, sizeof p);
    expect(!strcmp(p, "/r/out/s"), "config.yaml's start under the tree");
}

int main(int argc, char **argv)
{
    const char *root = argc > 1 ? argv[1] : "..";
    char path[1024];
    grammar();
    snprintf(path, sizeof path, "%s/config.yaml", root);
    registry(path);
    config(path);
    snprintf(path, sizeof path, "%s/configs", root);
    DIR *d = opendir(path);
    int named = 0;
    for (struct dirent *e; d && (e = readdir(d));)
    {
        size_t l = strlen(e->d_name);
        if (l < 6 || strcmp(e->d_name + l - 5, ".yaml")) continue;
        snprintf(path, sizeof path, "%s/configs/%s", root, e->d_name);
        config(path);
        ++named;
    }
    if (d) closedir(d);
    printf("worldconf_check: %s (%d named configs)\n", bad ? "FAIL" : "ok", named);
    return bad ? 1 : 0;
}
