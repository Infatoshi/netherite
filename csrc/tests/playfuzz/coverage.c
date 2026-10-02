/* What the fuzzer's speedrun actions did in a round, from the rows Java checked.
 *
 *   out/native/playfuzz_coverage ROUND.out... > coverage.tsv   (from the repository root)
 *
 * ROUND.out is run.sh's output (one CASE line each). For every PASS case the
 * script's actions (out/playfuzz/scripts/pf-START-SEED.acts.tsv, playfuzz_gen
 * --actions: each action and the tick the model did it) are read beside the
 * Java rows of the session's check (out/playfuzz/cov/pf-START-SEED.jsonl, the
 * rows run.sh keeps of a PASS, else out/human/pf-START-SEED/play/...check/
 * tape.jsonl). Per action, one line:
 *
 *   action  sessions  rows_after  effect_sessions  effect_rows_after  effect
 *
 * sessions: PASS sessions whose script did it; rows_after: the checked rows
 * after its first attempt, summed over them. effect_*: the same for the
 * sessions whose rows show it took effect (the rule in the last column: the
 * item's count in the server player's inventory fell, the dimension changed,
 * a merchant window opened, the emeralds changed); '-' where the rows cannot
 * show it (a ride, a knot) and 'off' where config.yaml switches the item off
 * (boats, minecarts, fishing: using them does nothing). The C port of
 * coverage.py (lane/cport): the same table. */
#define _GNU_SOURCE
#include "../../engine/tape.h"
#include "../tool.h"

enum { NONE = -2147483647 - 1 }; /* a dimension the row does not carry (None) */

struct slot {
    long long id, count, meta;
    int has_meta;
};

struct row {
    long long t;
    int dim;
    struct slot *inv;
    int ninv;
    char *win; /* the window's kind, or NULL */
};

/* the effect each action's rows show */
enum { R_NONE, R_ITEM, R_DRINK, R_SPLASH, R_NETHER, R_END, R_TRADE_OPEN, R_TRADE };
static const struct rule {
    const char *action, *text;
    int kind;
    int item;
} RULES[] = {
    {"pearl", "item count fell", R_ITEM, 368}, {"eye", "item count fell", R_ITEM, 381},
    {"end_portal_eye", "item count fell", R_ITEM, 381}, {"potion_drink", "item count fell", R_DRINK, 0},
    {"potion_throw", "item count fell", R_SPLASH, 0}, {"tnt", "item count fell", R_ITEM, 46},
    {"enchanting_table", "item count fell", R_ITEM, 116}, {"anvil", "item count fell", R_ITEM, 145},
    {"brewing_stand", "item count fell", R_ITEM, 379}, {"pig_saddle", "item count fell", R_ITEM, 329},
    {"lead", "item count fell", R_ITEM, 420}, {"nether_portal", "dimension 0 <-> -1", R_NETHER, 0},
    {"end_portal", "dimension became 1", R_END, 0}, {"trade_open", "merchant window open", R_TRADE_OPEN, 0},
    {"trade", "emeralds changed", R_TRADE, 0}, {"boat", "off", R_NONE, 0}, {"minecart", "off", R_NONE, 0},
    {"fishing_rod", "off", R_NONE, 0}, {"nether_portal_light", "-", R_NONE, 0}, {"pig_ride", "-", R_NONE, 0},
    {"lead_fence", "-", R_NONE, 0},
};
#define NRULES ((int)(sizeof RULES / sizeof *RULES))

static const struct rule *rule_of(const char *a)
{
    for (int i = 0; i < NRULES; ++i)
        if (!strcmp(RULES[i].action, a)) return &RULES[i];
    return NULL;
}

static int pred(const struct rule *r, const struct slot *s)
{
    if (r->kind == R_ITEM) return s->id == r->item;
    if (r->kind == R_TRADE) return s->id == 388;
    if (r->kind == R_DRINK || r->kind == R_SPLASH) {
        if (s->id != 373) return 0;
        if (!s->has_meta) die("playfuzz_coverage: a potion slot without its meta");
        return ((s->meta & 16384) != 0) == (r->kind == R_SPLASH);
    }
    return 0;
}

static long long item_count(const struct row *w, const struct rule *r)
{
    long long n = 0;
    for (int i = 0; i < w->ninv; ++i)
        if (pred(r, &w->inv[i])) n += w->inv[i].count;
    return n;
}

static int is_merchant(const char *w) { return w && !strcmp(w, "ContainerMerchant"); }

/* the first row at or after the action's tick where its effect shows, or -1 */
static long long effect_tick(const struct rule *r, long long t0, const struct row *rows, int n)
{
    int last = -1;
    for (int i = 0; i < n; ++i)
        if (rows[i].t < t0) last = i;
    if (last < 0) return -1;
    const struct row *b = &rows[last];
    if (r->kind == R_ITEM || r->kind == R_DRINK || r->kind == R_SPLASH) {
        long long n0 = item_count(b, r);
        for (int i = 0; i < n; ++i)
            if (rows[i].t >= t0 && item_count(&rows[i], r) < n0) return rows[i].t;
        return -1;
    }
    if (r->kind == R_NETHER) {
        for (int i = 0; i < n; ++i) {
            int d = rows[i].dim, d0 = b->dim;
            if (rows[i].t >= t0 && d != d0 && ((d == 0 && d0 == -1) || (d == -1 && d0 == 0))) return rows[i].t;
        }
        return -1;
    }
    if (r->kind == R_END) {
        for (int i = 0; i < n; ++i)
            if (rows[i].t >= t0 && rows[i].dim == 1 && b->dim != 1) return rows[i].t;
        return -1;
    }
    if (r->kind == R_TRADE_OPEN) {
        for (int i = 0; i < n; ++i)
            if (rows[i].t >= t0 && is_merchant(rows[i].win)) return rows[i].t;
        return -1;
    }
    if (r->kind == R_TRADE) {
        long long n0 = item_count(b, r);
        for (int i = 0; i < n; ++i)
            if (rows[i].t >= t0 && !is_merchant(rows[i].win) && item_count(&rows[i], r) != n0) return rows[i].t;
        return -1;
    }
    return -1;
}

struct tally {
    long long sessions, rows_after, eff_sessions, eff_rows;
};

static struct smap tallies; /* action -> struct tally *, in first-seen order */

static struct tally *tally(const char *a)
{
    struct tally *t = smap_get(&tallies, a);
    if (!t) {
        t = xcalloc(1, sizeof *t);
        smap_put(&tallies, a, t);
    }
    return t;
}

static int read_rows(FILE *fh, struct row **out)
{
    int n = 0, cap = 0;
    struct row *rows = NULL;
    char *line = NULL;
    size_t lcap = 0;
    ssize_t len;
    while ((len = getline(&line, &lcap, fh)) >= 0) {
        struct jval *r = json_parse(xstrndup(line, (size_t)len));
        if (!r) die("playfuzz_coverage: a row that is not JSON");
        const struct jval *sp = json_get(r, "sp"), *tv = json_get(r, "t");
        if (!sp || sp->kind == J_NULL || !tv || tv->dbl < 2) {
            json_free(r);
            continue;
        }
        if (n == cap) cap = cap ? cap * 2 : 1024, rows = xrealloc(rows, (size_t)cap * sizeof *rows);
        struct row *w = &rows[n++];
        memset(w, 0, sizeof *w);
        w->t = tv->num;
        const struct jval *d = json_get(sp, "dim");
        w->dim = d && d->kind == J_NUM ? (int)d->num : NONE;
        const struct jval *inv = json_get(sp, "inv");
        if (inv && inv->kind == J_ARR && inv->nitems) {
            w->inv = xcalloc((size_t)inv->nitems, sizeof *w->inv);
            for (int i = 0; i < inv->nitems; ++i) {
                const struct jval *s = inv->items[i];
                struct slot *o = &w->inv[w->ninv++];
                o->id = json_len(s) > 1 ? json_at(s, 1)->num : 0;
                o->count = json_len(s) > 2 ? json_at(s, 2)->num : 0;
                o->has_meta = json_len(s) > 3;
                o->meta = o->has_meta ? json_at(s, 3)->num : 0;
            }
        }
        const struct jval *win = json_get(sp, "win");
        if (win && win->kind == J_OBJ && win->nfields) {
            const struct jval *k = json_get(win, "kind");
            w->win = k && k->kind == J_STR ? xstrdup(k->str) : NULL;
        }
        json_free(r);
    }
    free(line);
    *out = rows;
    return n;
}

int main(int argc, char **argv)
{
    long long cases = 0;
    tally("kit");
    for (int ai = 1; ai < argc; ++ai) {
        FILE *rf = fopen(argv[ai], "r");
        if (!rf) die("playfuzz_coverage: cannot read %s", argv[ai]);
        char *line = NULL;
        size_t lcap = 0;
        ssize_t len;
        while ((len = getline(&line, &lcap, rf)) >= 0) {
            if (len && line[len - 1] == '\n') line[--len] = 0;
            char *f[4] = {0};
            int nf = 0;
            for (char *p = line; nf < 4;) {
                f[nf++] = p;
                char *tab = strchr(p, '\t');
                if (!tab) break;
                *tab = 0;
                p = tab + 1;
            }
            if (nf < 4 || strcmp(f[1], "PASS") || strchr(f[0], '@')) continue;
            char *colon = strchr(f[0], ':');
            if (!colon || strchr(colon + 1, ':')) die("playfuzz_coverage: a case that is not START:SEED: %s", f[0]);
            *colon = 0;
            char *run = xasprintf("pf-%s-%s", f[0], colon + 1);
            char *ap = xasprintf("out/playfuzz/scripts/%s.acts.tsv", run);
            char *acts = read_file(ap, NULL);
            free(ap);
            if (!acts) { free(run); continue; }
            ++cases;
            struct sv as = {0}, ts = {0};
            for (char *l = acts; *l;) {
                char *e = strchr(l, '\n');
                if (e) *e = 0;
                if (*l) {
                    char *tab = strchr(l, '\t');
                    if (!tab || strchr(tab + 1, '\t')) die("playfuzz_coverage: an action line that is not ACTION\\tTICK");
                    *tab = 0;
                    sv_push(&as, l);
                    sv_push(&ts, tab + 1);
                }
                if (!e) break;
                l = e + 1;
            }
            if (!as.n) { free(run); continue; }
            char *tape = xasprintf("out/playfuzz/cov/%s.jsonl", run);
            FILE *fh = fopen(tape, "r");
            if (!fh) {
                free(tape);
                tape = xasprintf("out/human/%s/play/%s-s1-1.check/tape.jsonl", run, run);
                fh = fopen(tape, "r");
                if (!fh) { free(tape); free(run); continue; }
                int c;
                while ((c = fgetc(fh)) != EOF && c != '\n') {}
            }
            struct row *rows;
            int n = read_rows(fh, &rows);
            fclose(fh);
            free(tape);
            free(run);
            if (!n) continue;
            struct smap first = {0};
            for (int i = 0; i < as.n; ++i)
                if (!smap_has(&first, as.v[i])) smap_put(&first, as.v[i], (void *)(intptr_t)strtoll(ts.v[i], NULL, 10));
            for (int i = 0; i < first.n; ++i) {
                const char *a = first.keys[i];
                long long t0 = (long long)(intptr_t)first.vals[i];
                struct tally *tl = tally(a);
                tl->sessions += 1;
                for (int k = 0; k < n; ++k) tl->rows_after += rows[k].t > t0;
                const struct rule *r = rule_of(a);
                if (r && strcmp(r->text, "-") && strcmp(r->text, "off")) {
                    long long te = effect_tick(r, t0, rows, n);
                    if (te >= 0) {
                        tl->eff_sessions += 1;
                        for (int k = 0; k < n; ++k) tl->eff_rows += rows[k].t >= te;
                    }
                }
            }
        }
        free(line);
        fclose(rf);
    }
    printf("action\tsessions\trows_after\teffect_sessions\teffect_rows_after\teffect\n");
    struct sv stage = {0}, rest = {0}, order = {0};
    for (int i = 0; i < tallies.n; ++i) {
        const char *a = tallies.keys[i];
        if (starts_with(a, "stage_")) sv_push(&stage, (char *)a);
        else if (strcmp(a, "kit")) sv_push(&rest, (char *)a);
    }
    sv_sort(&stage);
    sv_sort(&rest);
    sv_push(&order, "kit");
    for (int i = 0; i < stage.n; ++i) sv_push(&order, stage.v[i]);
    for (int i = 0; i < rest.n; ++i) sv_push(&order, rest.v[i]);
    for (int i = 0; i < order.n; ++i) {
        const char *a = order.v[i];
        const struct rule *r = rule_of(a);
        const char *rule = r ? r->text : "-";
        struct tally *tl = tally(a);
        int counted = strcmp(rule, "-") && strcmp(rule, "off");
        char es[32], er[32];
        if (counted) snprintf(es, sizeof es, "%lld", tl->eff_sessions), snprintf(er, sizeof er, "%lld", tl->eff_rows);
        else snprintf(es, sizeof es, "%s", rule), snprintf(er, sizeof er, "%s", rule);
        printf("%s\t%lld\t%lld\t%s\t%s\t%s\n", a, tl->sessions, tl->rows_after, es, er, r ? rule : "-");
    }
    printf("# %lld PASS generated sessions read\n", cases);
    return 0;
}
