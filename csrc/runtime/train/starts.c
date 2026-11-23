/* ntstarts (lane/starts): the library of starts at every stage. A start is a snapshot the Java client made (so every
 * episode played from it replays on the Java client from the same state): a checkpoint (a vanilla Save and Quit) at
 * the tick a good episode reached a stage, then the oracle's Snapshot of that checkpoint's join. The library is a
 * directory, an entry a subdirectory:
 *   LIB/ID/start/        the snapshot (the trainer's start: neth_rl_start_load), with its tape.jsonl (the join rows)
 *   LIB/ID/cp/           the checkpoint it was made from (its save; FROM= for a replay of an episode from the start;
 *                        a packed library keeps the save as cp/save.tar.zst: harvest.sh -l LIB --unpack ID first)
 *   LIB/ID/stage         lane/killlib: the stage by name, where the start's own rows do not carry it (the late ones)
 *   LIB/ID/prov          one line: where it came from (harvested NREC:EPISODE@TICK, human RUN/SESSION, golden NAME)
 *   LIB/ID/ref           instead of start/ and cp/: a stored snapshot by path (the golden chain's, the human's)
 *   LIB/index.tsv        id seed stage milestones dimension items provenance bytes path (ntstarts index)
 * csrc/runtime/train/harvest.sh drives the Java runs (make -C csrc/runtime starts-harvest); this tool is its C part:
 *
 *   ntstarts stages ROWS [--quiet N]   the oracle's rows of an episode (its tape.jsonl): a line each time the episode
 *                                      reaches a new chain entry (stages.h): the tick to save at (the first row at or
 *                                      after it with no screen open and the player on the ground, within N ticks,
 *                                      default 200, else the tick itself), the stage, the mask, the dimension, items
 *   ntstarts cut SCRIPT T OUT CPDIR    the script's acts through row T's (its first T lines: the Snapshot line, then
 *                                      an act a tick from row 2) and a save into CPDIR, to OUT
 *   ntstarts entry STARTDIR            a start's seed, stage, milestones, dimension and items, from its tape's first
 *                                      row with the server player
 *   ntstarts index LIB                 LIB/index.tsv from every entry (exit 1 when an entry is unreadable)
 *   ntstarts quiet ROWS T [--within N] [--script SCRIPT]   lane/armed: the tick to save a state reached at row T at
 *                                      (the first quiet row at or after T within N, default 200; else T + N, quiet 0);
 *                                      with the script, quiet also needs the client's screen closed as its acts show
 *                                      it (script_screen), for train/earn.sh
 *   ntstarts kit STARTDIR | kit TAPE T lane/armed: what the player holds (armour by piece and durability, tools and
 *                                      their uses left, ingots, food points, the stage), one line
 *
 * Exit 0, 1 a bad input, 2 usage. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../../engine/tape.h"
#include "stages.h"

#define MAXINV 64

/* a row's server player: dimension, inventory (id and count summed by id), quiet (no window, on the ground) */
struct prow { int64_t t; int dim, quiet, n; int ids[MAXINV], counts[MAXINV]; };

static int read_prow(char *line, struct prow *p)
{
    struct jval *row = json_parse(line);
    if (!row) return -1;
    const struct jval *sp = json_get(row, "sp");
    int64_t v = 0;
    if (!sp || !json_int(json_get(row, "t"), &p->t))
    {
        json_free(row);
        return 0;
    }
    p->dim = json_int(json_get(sp, "dim"), &v) ? (int)v : 0;
    const struct jval *win = json_get(sp, "win"), *cp = json_get(row, "cp");
    int64_t og = 0;
    p->quiet = (!win || win->kind == J_NULL) && cp && json_int(json_get(cp, "og"), &og) && og == 1;
    p->n = 0;
    const struct jval *inv = json_get(sp, "inv");
    for (int s = 0; inv && s < json_len(inv); ++s)
    {
        const struct jval *e = json_at(inv, s);
        int64_t id = -1, c = 0;
        if (!e || !json_int(json_at(e, 1), &id) || !json_int(json_at(e, 2), &c) || id < 0) continue;
        int k = 0;
        while (k < p->n && p->ids[k] != id) ++k;
        if (k == p->n && p->n < MAXINV) p->ids[p->n] = (int)id, p->counts[p->n++] = 0;
        if (k < p->n) p->counts[k] += (int)c;
    }
    json_free(row);
    return 1;
}

static void put_items(FILE *o, const struct prow *p)
{
    if (!p->n) fputs("-", o);
    for (int k = 0; k < p->n; ++k) fprintf(o, "%s%d:%d", k ? "," : "", p->ids[k], p->counts[k]);
}

static void put_ms(FILE *o, uint32_t ms)
{
    int any = 0;
    for (int i = 0; i < NETH_VEC_MILESTONES; ++i)
        if (ms >> i & 1) fprintf(o, "%s%s", any++ ? "+" : "", STAGE_MS_NAME[i]);
    if (!any) fputs("-", o);
}

static const char *dim_name(int d) { return d == -1 ? "nether" : d == 1 ? "end" : "overworld"; }

/* ntstarts stages: each new chain entry the episode reaches, held to the first quiet row within quiet ticks */
static int cmd_stages(const char *path, int quiet, const char *events)
{
    FILE *f = fopen(path, "r");
    if (!f) return fprintf(stderr, "ntstarts: %s: %s\n", path, strerror(errno)), 1;
    char *line = NULL;
    size_t cap = 0;
    ssize_t l;
    uint32_t reached = 0;           /* chain entries reached so far (bit j) */
    int pending = -1;               /* the entry waiting for a quiet row */
    int64_t pend_t = 0;
    struct prow p, first = {0};
    int64_t event_t[32] = {0};
    char event_name[32][48] = {{0}};
    int nevent = 0;
    if (events)
    {
        FILE *ev = fopen(events, "r");
        if (!ev) return fprintf(stderr, "ntstarts: %s: %s\n", events, strerror(errno)), fclose(f), 1;
        long long et;
        while (nevent < 32 && fscanf(ev, "%lld\t%47s", &et, event_name[nevent]) == 2)
        {
            event_t[nevent] = et;
            if (stage_by_name(event_name[nevent]) < STAGE_BASE_N)
                return fprintf(stderr, "ntstarts: unknown late stage %s\n", event_name[nevent]), fclose(ev), fclose(f), 1;
            ++nevent;
        }
        fclose(ev);
    }
    int rows = 0;
    while ((l = getline(&line, &cap, f)) > 0)
    {
        if (strncmp(line, "{\"t\":", 5)) continue;
        if (read_prow(strdup(line), &p) <= 0) continue;
        ++rows;
        uint32_t ms = stage_ms_of(p.ids, p.counts, p.n);
        if (pending >= 0 && (p.quiet || p.t - pend_t >= quiet))
        {
            printf("%lld\t%s\t%x\t%s\t", (long long)p.t, STAGE_CHAIN[pending].name, stage_ms_of(p.ids, p.counts, p.n),
                   dim_name(p.dim));
            put_items(stdout, &p);
            printf("\t%lld\n", (long long)pend_t);
            pending = -1;
        }
        if (rows == 1) first = p, reached |= 1u;
        for (int j = 1; j < STAGE_BASE_N; ++j)
            if (!(reached >> j & 1) && stage_has(j, ms, p.dim, -1))
            {
                reached |= 1u << j;
                /* the deepest new entry of the row: a row reaching two at once saves once */
                if (pending < 0 || j > pending) pending = j, pend_t = p.t;
            }
        for (int j = 0; j < nevent; ++j)
            if (event_name[j][0] && p.t >= event_t[j] &&
                (p.quiet || p.t - event_t[j] >= quiet ||
                 (!strcmp(event_name[j], "portal_lit") || !strcmp(event_name[j], "dragon_dead"))))
            {
                printf("%lld\t%s\t%x\t%s\t", (long long)p.t, event_name[j], ms, dim_name(p.dim));
                put_items(stdout, &p);
                printf("\t%lld\n", (long long)event_t[j]);
                event_name[j][0] = 0;
            }
    }
    free(line);
    fclose(f);
    if (!rows) return fprintf(stderr, "ntstarts: %s: no rows with a server player\n", path), 1;
    (void)first;
    return 0;
}

static int cmd_cut(const char *script, long T, const char *out, const char *cpdir)
{
    FILE *f = fopen(script, "r"), *o = fopen(out, "w");
    if (!f || !o) return fprintf(stderr, "ntstarts: %s: %s\n", f ? out : script, strerror(errno)), 1;
    char *line = NULL;
    size_t cap = 0;
    long n = 0;
    while (n < T && getline(&line, &cap, f) > 0) fputs(line, o), ++n;
    free(line);
    fclose(f);
    if (n < T) return fprintf(stderr, "ntstarts: %s has %ld lines, row %ld needs %ld\n", script, n, T, T), fclose(o), 1;
    fprintf(o, "{\"cmd\":\"save\",\"name\":\"lib\",\"out\":\"%s\"}\n", cpdir);
    return fclose(o) ? 1 : 0;
}

/* a start's tape: its header's seed and its first row with the server player */
static int start_read(const char *dir, long long *seed, struct prow *p)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/tape.jsonl", dir);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char *line = NULL;
    size_t cap = 0;
    int got = 0;
    *seed = -1;
    while (!got && getline(&line, &cap, f) > 0)
    {
        if (!strncmp(line, "{\"tape\"", 7))
        {
            struct jval *h = json_parse(strdup(line));
            int64_t s;
            if (h && json_int(json_get(h, "seed"), &s)) *seed = s;
            json_free(h);
        }
        else if (!strncmp(line, "{\"t\":", 5))
            got = read_prow(strdup(line), p) > 0;
    }
    free(line);
    fclose(f);
    return got ? 0 : -1;
}

/* lane/armed: ntstarts quiet ROWS T [--within N]: the tick to save at for a state reached at row T of the oracle's rows
 * (an attempt's arrival): the first row at or after T with no screen open and the player on the ground, within N ticks
 * (default 200, harvest's rule); else the row at T + N. One line: the row's t, quiet 1 or 0, the dimension, the items */
/* the client's screen after each of a script's first lines, as far as the acts show it (the server's row has no
 * client screen: its "win" is null under the player's own inventory): an inventory press opens it, any gui op runs
 * only under a screen, a gui close closes it (a press under a screen is ignored, Act.java). A container opened by a use
 * shows only at its first gui op: the checkpoint's manifest (quiescence.guiClosed) is the check. open[k] after line k */
static unsigned char *script_screen(const char *script, long *nlines)
{
    FILE *f = fopen(script, "r");
    if (!f) return NULL;
    char *line = NULL;
    size_t cap = 0;
    long n = 0, capn = 1 << 16;
    unsigned char *open = calloc((size_t)capn, 1);
    int o = 0;
    while (open && getline(&line, &cap, f) > 0)
    {
        if (n + 1 >= capn) open = realloc(open, (size_t)(capn *= 2));
        if (!open) break;
        const char *g = strstr(line, "\"gui\":[[");
        if (g) o = !strstr(g, "[\"close\"");
        else if (!o && strstr(line, "\"press\":[") && strstr(line, "\"inventory\"")) o = 1;
        open[++n] = (unsigned char)o;
    }
    free(line);
    fclose(f);
    *nlines = n;
    return open;
}

static int cmd_quiet(const char *path, long long T, int within, const char *script)
{
    FILE *f = fopen(path, "r");
    if (!f) return fprintf(stderr, "ntstarts: %s: %s\n", path, strerror(errno)), 1;
    long nl = 0;
    unsigned char *open = script ? script_screen(script, &nl) : NULL;
    if (script && !open) return fprintf(stderr, "ntstarts: %s: %s\n", script, strerror(errno)), fclose(f), 1;
    char *line = NULL;
    size_t cap = 0;
    struct prow p;
    int done = 0;
    while (!done && getline(&line, &cap, f) > 0)
    {
        if (strncmp(line, "{\"t\":", 5) || read_prow(strdup(line), &p) <= 0 || p.t < T) continue;
        /* a cut at row t keeps the script's first t lines */
        if (open && (p.t > nl || open[p.t])) p.quiet = 0;
        if (p.quiet || p.t - T >= within)
        {
            printf("%lld\t%d\t%s\t", (long long)p.t, p.quiet, dim_name(p.dim));
            put_items(stdout, &p);
            printf("\n");
            done = 1;
        }
    }
    free(line);
    free(open);
    fclose(f);
    return done ? 0 : (fprintf(stderr, "ntstarts quiet: %s: no row at or past %lld within %d ticks\n", path, T, within), 1);
}

/* lane/armed: ntstarts kit PATH [T]: what the player holds, from a start's first row with the server player (PATH a
 * start directory) or row T of an oracle tape (PATH a tape.jsonl): armour by piece (boots, leggings, chestplate,
 * helmet: the item and its durability left of its most), the points worn (1.7.10's), each pickaxe and sword with its
 * uses left, ingots, food points (chain.c food_points' table), the stage and milestones. One line. */
static int kit_max(int id)
{
    static const int16_t M[][2] = {{270, 59}, {274, 131}, {257, 250}, {278, 1561}, {285, 32}, {268, 59}, {272, 131},
                                   {267, 250}, {276, 1561}, {283, 32}, {298, 55}, {299, 80}, {300, 75}, {301, 65},
                                   {302, 165}, {303, 240}, {304, 225}, {305, 195}, {306, 165}, {307, 240}, {308, 225},
                                   {309, 195}, {310, 363}, {311, 528}, {312, 495}, {313, 429}, {314, 77}, {315, 112},
                                   {316, 105}, {317, 91}};
    for (size_t k = 0; k < sizeof M / sizeof *M; ++k)
        if (M[k][0] == id) return M[k][1];
    return 0;
}

static int kit_points(int id)
{
    /* ItemArmor's damageReduceAmount: helmet, chestplate, leggings, boots by material */
    static const int8_t P[5][4] = {{1, 3, 2, 1}, {2, 5, 4, 1}, {2, 6, 5, 2}, {3, 8, 6, 3}, {2, 5, 3, 1}};
    if (id < 298 || id > 317) return 0;
    return P[(id - 298) / 4][(id - 298) % 4];
}

static int cmd_kit(const char *path, long long T)
{
    char tp[4096];
    struct stat st;
    int isdir = !stat(path, &st) && S_ISDIR(st.st_mode);
    snprintf(tp, sizeof tp, isdir ? "%s/tape.jsonl" : "%s", path);
    FILE *f = fopen(tp, "r");
    if (!f) return fprintf(stderr, "ntstarts: %s: %s\n", tp, strerror(errno)), 1;
    char *line = NULL;
    size_t cap = 0;
    int got = 0;
    while (!got && getline(&line, &cap, f) > 0)
    {
        if (strncmp(line, "{\"t\":", 5)) continue;
        struct jval *row = json_parse(strdup(line));
        const struct jval *sp = row ? json_get(row, "sp") : NULL;
        int64_t t = 0;
        if (!sp || !json_int(json_get(row, "t"), &t) || (!isdir && t < T))
        {
            json_free(row);
            continue;
        }
        got = 1;
        struct prow p;
        read_prow(strdup(line), &p);
        static const char *PIECE[4] = {"boots", "legs", "chest", "helm"};
        int arm[4] = {-1, -1, -1, -1}, armd[4] = {0}, pts = 0;
        char tools[1024] = "";
        size_t tl = 0;
        const struct jval *inv = json_get(sp, "inv");
        for (int s = 0; inv && s < json_len(inv); ++s)
        {
            const struct jval *e = json_at(inv, s);
            int64_t sl = -1, id = -1, c = 0, d = 0;
            if (!e || !json_int(json_at(e, 0), &sl) || !json_int(json_at(e, 1), &id) || !json_int(json_at(e, 2), &c)) continue;
            json_int(json_at(e, 3), &d);
            if (sl >= 36 && sl < 40) arm[sl - 36] = (int)id, armd[sl - 36] = (int)d, pts += kit_points((int)id);
            else if (kit_max((int)id) && !kit_points((int)id) && tl < sizeof tools - 32)
                tl += (size_t)snprintf(tools + tl, sizeof tools - tl, "%s%lld:%d/%d", tl ? "," : "", (long long)id,
                                       kit_max((int)id) - (int)d, kit_max((int)id));
        }
        static const int16_t H[][2] = {{320, 8}, {364, 8}, {400, 8}, {366, 6}, {282, 6}, {393, 6}, {297, 5}, {350, 5},
                                       {260, 4}, {391, 4}, {322, 4}, {363, 3}, {319, 3}, {365, 2}, {349, 2}, {360, 2},
                                       {357, 2}, {392, 1}};
        int fpts = 0, ingot = 0;
        for (int k = 0; k < p.n; ++k)
        {
            for (size_t h = 0; h < sizeof H / sizeof *H; ++h)
                if (p.ids[k] == H[h][0]) fpts += H[h][1] * p.counts[k];
            if (p.ids[k] == 265) ingot = p.counts[k];
        }
        uint32_t ms = stage_ms_of(p.ids, p.counts, p.n);
        printf("t %lld armour %d:", (long long)t, pts);
        for (int k = 0; k < 4; ++k)
            if (arm[k] < 0) printf(" %s -", PIECE[k]);
            else printf(" %s %d %d/%d", PIECE[k], arm[k], kit_max(arm[k]) - armd[k], kit_max(arm[k]));
        printf("; tools %s; ingots %d; food points %d; stage %s; milestones ", tl ? tools : "-", ingot, fpts,
               STAGE_CHAIN[stage_of(ms, p.dim)].name);
        put_ms(stdout, ms);
        printf("; %s\n", dim_name(p.dim));
        json_free(row);
    }
    free(line);
    fclose(f);
    return got ? 0 : (fprintf(stderr, "ntstarts kit: %s: no row with the server player%s\n", tp, isdir ? "" : " at T"), 1);
}

static int cmd_entry(const char *dir)
{
    long long seed;
    struct prow p;
    if (start_read(dir, &seed, &p)) return fprintf(stderr, "ntstarts: %s: no tape row with the server player\n", dir), 1;
    uint32_t ms = stage_ms_of(p.ids, p.counts, p.n);
    printf("%lld\t%s\t", seed, STAGE_CHAIN[stage_of(ms, p.dim)].name);
    put_ms(stdout, ms);
    printf("\t%s\t", dim_name(p.dim));
    put_items(stdout, &p);
    printf("\n");
    return 0;
}

static long long du(const char *path)
{
    struct stat st;
    if (lstat(path, &st)) return 0;
    if (!S_ISDIR(st.st_mode)) return (long long)st.st_blocks * 512;
    long long t = (long long)st.st_blocks * 512;
    DIR *d = opendir(path);
    if (!d) return t;
    struct dirent *e;
    while ((e = readdir(d)))
    {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char c[4096];
        snprintf(c, sizeof c, "%s/%s", path, e->d_name);
        t += du(c);
    }
    closedir(d);
    return t;
}

static int name_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void first_line(const char *path, char *out, size_t n)
{
    out[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(out, (int)n, f))
    {
        size_t k = strlen(out);
        while (k && (out[k - 1] == '\n' || out[k - 1] == '\r')) out[--k] = 0;
    }
    fclose(f);
}

static int cmd_index(const char *lib)
{
    DIR *d = opendir(lib);
    if (!d) return fprintf(stderr, "ntstarts: %s: %s\n", lib, strerror(errno)), 1;
    char **names = NULL;
    int n = 0, capn = 0;
    struct dirent *e;
    while ((e = readdir(d)))
    {
        if (e->d_name[0] == '.') continue;
        char p[4096];
        struct stat st;
        snprintf(p, sizeof p, "%s/%s", lib, e->d_name);
        if (stat(p, &st) || !S_ISDIR(st.st_mode)) continue;
        if (n == capn) names = realloc(names, (size_t)(capn = capn ? 2 * capn : 64) * sizeof *names);
        names[n++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof *names, name_cmp);
    char ip[4096];
    snprintf(ip, sizeof ip, "%s/index.tsv", lib);
    FILE *o = fopen(ip, "w");
    if (!o) return fprintf(stderr, "ntstarts: %s: %s\n", ip, strerror(errno)), 1;
    fprintf(o, "id\tseed\tstage\tmilestones\tdimension\titems\tprovenance\tbytes\tpath\n");
    int bad = 0, rows = 0;
    long long total = 0;
    int per[STAGE_N] = {0};
    for (int i = 0; i < n; ++i)
    {
        char ent[2048], start[4096], ref[4096], prov[1024];
        snprintf(ent, sizeof ent, "%.1000s/%.1000s", lib, names[i]);
        snprintf(ref, sizeof ref, "%s/ref", ent);
        first_line(ref, start, sizeof start);
        if (!start[0]) snprintf(start, sizeof start, "%s/start", ent);
        snprintf(ref, sizeof ref, "%s/prov", ent);
        first_line(ref, prov, sizeof prov);
        long long seed;
        struct prow p;
        if (start_read(start, &seed, &p))
        {
            fprintf(stderr, "ntstarts index: %s: no start (%s: no tape row with the server player)\n", ent, start);
            bad = 1;
            continue;
        }
        /* a dev start (its manifest's dev true: a setup the product refuses, rlgui.h NETH_RL_DEV) is no start to train on */
        char mp[4200], man[65536];
        snprintf(mp, sizeof mp, "%s/manifest.json", start);
        FILE *mf = fopen(mp, "r");
        size_t ml = mf ? fread(man, 1, sizeof man - 1, mf) : 0;
        if (mf) fclose(mf);
        man[ml] = 0;
        if (strstr(man, "\"dev\":true"))
        {
            fprintf(stderr, "ntstarts index: %s: a dev start (skipped: the product refuses it)\n", ent);
            continue;
        }
        uint32_t ms = stage_ms_of(p.ids, p.counts, p.n);
        int st = stage_of(ms, p.dim);
        char stage_path[4096], saved_stage[80];
        snprintf(stage_path, sizeof stage_path, "%s/stage", ent);
        first_line(stage_path, saved_stage, sizeof saved_stage);
        if (saved_stage[0])
        {
            int named = stage_by_name(saved_stage);
            if (named < 0) { fprintf(stderr, "ntstarts index: %s: unknown stage %s\n", ent, saved_stage); bad = 1; continue; }
            st = named;
        }
        long long b = du(ent);
        total += b;
        ++per[st];
        fprintf(o, "%s\t%lld\t%s\t", names[i], seed, STAGE_CHAIN[st].name);
        put_ms(o, ms);
        fprintf(o, "\t%s\t", dim_name(p.dim));
        put_items(o, &p);
        fprintf(o, "\t%s\t%lld\t%s\n", prov[0] ? prov : "-", b, start);
        ++rows;
    }
    fclose(o);
    printf("%s: %d starts, %lld bytes (%.1f MB a start);", ip, rows, total, rows ? total / 1e6 / rows : 0.0);
    for (int j = 0; j < STAGE_N; ++j)
        if (per[j]) printf(" %s %d", STAGE_CHAIN[j].name, per[j]);
    printf("\n");
    for (int i = 0; i < n; ++i) free(names[i]);
    free(names);
    return bad;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "stages"))
    {
        int quiet = 200;
        const char *events = NULL;
        for (int i = 3; i < argc; ++i)
        {
            if (!strcmp(argv[i], "--quiet") && i + 1 < argc) quiet = atoi(argv[++i]);
            else if (!strcmp(argv[i], "--events") && i + 1 < argc) events = argv[++i];
            else return fprintf(stderr, "ntstarts stages: %s?\n", argv[i]), 2;
        }
        return cmd_stages(argv[2], quiet, events);
    }
    if (argc == 6 && !strcmp(argv[1], "cut")) return cmd_cut(argv[2], atol(argv[3]), argv[4], argv[5]);
    if (argc == 3 && !strcmp(argv[1], "entry")) return cmd_entry(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "index")) return cmd_index(argv[2]);
    if (argc >= 4 && !strcmp(argv[1], "quiet"))
    {
        int within = 200;
        const char *script = NULL;
        for (int i = 4; i < argc; ++i)
        {
            if (!strcmp(argv[i], "--within") && i + 1 < argc) within = atoi(argv[++i]);
            else if (!strcmp(argv[i], "--script") && i + 1 < argc) script = argv[++i];
            else return fprintf(stderr, "ntstarts quiet: %s?\n", argv[i]), 2;
        }
        return cmd_quiet(argv[2], atoll(argv[3]), within, script);
    }
    if ((argc == 3 || argc == 4) && !strcmp(argv[1], "kit")) return cmd_kit(argv[2], argc == 4 ? atoll(argv[3]) : 0);
    fprintf(stderr, "usage: ntstarts stages ROWS [--quiet N] [--events FILE] | cut SCRIPT T OUT CPDIR | entry STARTDIR | index LIB"
                    " | quiet ROWS T [--within N] | kit STARTDIR | kit TAPE T\n");
    return 2;
}
