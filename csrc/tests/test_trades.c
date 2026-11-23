/* Gate: native villager trade generation against the oracle's TradesProbe.
 *
 *   test_trades DIR
 *
 * DIR holds manifest.json (the run seed, the case count, the unlock tick's n
 * values, both static price tables as id/min/max rows, per-profession stats)
 * and lines.jsonl (one line per case and unlock tick: the profession, the case
 * index, the case seed, the tick index and its n, the villager Random's 48-bit
 * state after the tick, the shuffle stream's state before it, and the recipe
 * list's NBT as canonical JSON).
 *
 * Every case is one villager driven through the unlock path: a first
 * addDefaultEquipmentAndRecipies(1), then two unlock ticks. An unlock tick is
 * updateAITick's block when needsInitilization fires: every disabled recipe
 * gains maxUses += nextInt(6) + nextInt(6) + 2 (only when the list holds more
 * than one recipe), then addDefaultEquipmentAndRecipies(n) into the existing
 * list. The probe disables every recipe before the second and third tick so
 * the boost draws happen, and runs n = 1, 3, 7 so the trim bound is exercised
 * past what a real unlock (always n = 1) reaches.
 *
 * This port replays each case from its seed the same way and compares, per
 * tick, the Random state and the list as canonical NBT, so the price tables,
 * every draw, the shuffle, the enchanted book and tool tags, and the
 * addToListWithCheck replacement rule are all checked.
 */
#include "../engine/jrand.h"
#include "../engine/nbtjson.h"
#include "../engine/trades.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "test_trades: out of memory\n"); exit(1); }
    return p;
}

static char *dup_span(const char *s, size_t n)
{
    char *d = xmalloc(n + 1);
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

static char *read_whole(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = xmalloc((size_t)n + 1);
    size_t got = fread(b, 1, (size_t)n, f);
    b[got] = 0;
    fclose(f);
    return b;
}

/* The span of an object or array value, braces balanced, or 0. */
static int obj_field(const char *line, const char *key, const char **val, size_t *len)
{
    char pat[128];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(line, pat);
    if (!p) return 0;
    p += strlen(pat);
    if (*p != '{' && *p != '[') return 0;
    char open = *p, close = open == '{' ? '}' : ']';
    int depth = 0;
    const char *s = p;
    while (*p)
    {
        if (*p == open) ++depth;
        else if (*p == close) { if (--depth == 0) { ++p; break; } }
        ++p;
    }
    *val = s;
    *len = (size_t)(p - s);
    return 1;
}

static long num_field(const char *line, const char *key, long missing)
{
    char pat[128];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(line, pat);
    if (!p) return missing;
    return strtol(p + strlen(pat), NULL, 10);
}

/* The manifest's steps array into out, or 0. */
static int parse_steps(const char *manifest, int *out, int max)
{
    const char *val;
    size_t len;
    if (!obj_field(manifest, "steps", &val, &len)) return 0;
    int n = 0;
    const char *p = val, *end = val + len;
    while (p < end && n < max)
    {
        while (p < end && (*p < '0' || *p > '9')) ++p;
        if (p >= end) break;
        out[n++] = (int)strtol(p, (char **)&p, 10);
    }
    return n;
}

/* One row of a manifest table: item id, min, max. */
struct table_row { long item, min, max; };

static int parse_table(const char *manifest, const char *name, struct table_row **out)
{
    const char *tables, *arr;
    size_t tl, al;
    if (!obj_field(manifest, "tables", &tables, &tl)) return 0;
    char *tobj = dup_span(tables, tl);
    int n = 0;
    if (obj_field(tobj, name, &arr, &al))
    {
        const char *p = arr, *end = arr + al;
        int cap = 16;
        struct table_row *v = xmalloc(sizeof *v * cap);
        while ((p = strstr(p, "{\"item\":")) != NULL && p < end)
        {
            const char *q = p;
            int depth = 0;
            while (q < end)
            {
                if (*q == '{') ++depth;
                else if (*q == '}') { if (--depth == 0) { ++q; break; } }
                ++q;
            }
            char *obj = dup_span(p, (size_t)(q - p));
            if (n == cap) { cap *= 2; v = realloc(v, sizeof *v * cap); }
            v[n].item = num_field(obj, "item", -1);
            v[n].min = num_field(obj, "min", -1);
            v[n].max = num_field(obj, "max", -1);
            ++n;
            free(obj);
            p = q;
        }
        *out = v;
    }
    free(tobj);
    return n;
}

/* The native tables, for the manifest cross-check. */
static const struct trades_price_row *NATIVE_TABLES[2];
static const char *NATIVE_TABLE_NAMES[2] = {"villagerStockList", "blacksmithSellingList"};
static int N_NATIVE_ROWS[2];

/* 1 when two canonical NBT strings differ, printing the first key path. */
static int nbt_str_diff(const char *want, const char *have, char *why, size_t whyn)
{
    nbt *a = want ? nbt_parse(want) : NULL;
    nbt *b = have ? nbt_parse(have) : NULL;
    int d = (!a || !b) ? 1 : nbt_diff(a, b, why, whyn);
    if (a) nbt_free(a);
    if (b) nbt_free(b);
    return d;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("test_trades: usage: test_trades DIR\n");
        return 0;
    }

    NATIVE_TABLES[0] = trades_villager_stock(&N_NATIVE_ROWS[0]);
    NATIVE_TABLES[1] = trades_blacksmith_stock(&N_NATIVE_ROWS[1]);

    char path[4096];
    snprintf(path, sizeof path, "%s/manifest.json", argv[1]);
    char *manifest = read_whole(path);
    if (!manifest) { printf("test_trades: no %s\n", path); return 1; }
    snprintf(path, sizeof path, "%s/lines.jsonl", argv[1]);
    char *lines = read_whole(path);
    if (!lines) { printf("test_trades: no %s\n", path); return 1; }

    int steps[8];
    int nsteps = parse_steps(manifest, steps, 8);
    long cases = num_field(manifest, "cases", -1);
    if (!nsteps || cases < 0)
    {
        printf("test_trades: %s has no steps or cases\n", argv[1]);
        return 2;
    }

    /* the manifest's price tables must be the port's */
    int bad = 0, checked = 0;

    for (int t = 0; t < 2; ++t)
    {
        struct table_row *rows = NULL;
        int n = parse_table(manifest, NATIVE_TABLE_NAMES[t], &rows);
        if (n != N_NATIVE_ROWS[t])
        {
            printf("FIRST DIFF table %s: want %d rows got %d\n", NATIVE_TABLE_NAMES[t], n, N_NATIVE_ROWS[t]);
            ++bad;
        }
        for (int i = 0; i < n && t < 2; ++i)
        {
            const struct trades_price_row *tr = NATIVE_TABLES[t];
            int found = -1;
            for (int k = 0; k < N_NATIVE_ROWS[t]; ++k)
                if (tr[k].item == rows[i].item) { found = k; break; }
            if (found < 0 || tr[found].min != rows[i].min || tr[found].max != rows[i].max)
            {
                printf("FIRST DIFF table %s row item %ld: want [%ld,%ld] got [%d,%d]\n",
                       NATIVE_TABLE_NAMES[t], rows[i].item, rows[i].min, rows[i].max,
                       found >= 0 ? tr[found].min : -999, found >= 0 ? tr[found].max : -999);
                ++bad;
            }
        }
        free(rows);
    }

    /* every line, in file order, replayed case by case */
    long cur_prof = -1, cur_case = -1, cur_step = 0;
    jrand r, shuf;
    struct trade_list list;
    memset(&list, 0, sizeof list);

    const char *line = lines;
    while (line && *line)
    {
        const char *nl = strchr(line, '\n');
        size_t linelen = nl ? (size_t)(nl - line) : strlen(line);
        char *one = dup_span(line, linelen);

        long prof = num_field(one, "prof", -1);
        long ci = num_field(one, "case", -1);
        long step = num_field(one, "step", -1);
        long seed = num_field(one, "seed", 0);
        long state = num_field(one, "state", -1);
        long sh = num_field(one, "sh", -1);
        const char *nv;
        size_t nl2;
        int have_nbt = obj_field(one, "nbt", &nv, &nl2);
        char *want = have_nbt ? dup_span(nv, nl2) : NULL;

        if (prof < 0 || prof > 4 || step < 0 || step >= nsteps)
        {
            printf("FIRST DIFF line: prof %ld step %ld out of range\n", prof, step);
            ++bad;
            free(want);
            free(one);
            line = nl ? nl + 1 : NULL;
            continue;
        }

        if (prof != cur_prof || ci != cur_case)
        {
            cur_prof = prof;
            cur_case = ci;
            cur_step = 0;
            jr_seed(&r, seed);
            trades_free(&list);
        }
        else if (step != cur_step)
        {
            printf("FIRST DIFF prof %ld case %ld: lines out of order (step %ld after %ld)\n",
                   prof, ci, step, cur_step);
            ++bad;
            free(want);
            free(one);
            line = nl ? nl + 1 : NULL;
            continue;
        }

        /* the tick this line records: first the boost of disabled recipes,
         * then the recipe call. The shuffle Random's 48-bit state is loaded
         * directly (det state semantics, not setSeed). */
        shuf.seed = (uint64_t)sh;

        if (cur_step > 0)
        {
            for (int k = 0; k < list.n; ++k) trades_disable(&list.r[k]);
            trades_unlock_boost(&r, &list);
        }
        long count = num_field(one, "count", steps[step]);
        trades_add_default(&r, &shuf, (int)prof, (int)count, &list);
        ++cur_step;

        char why[1024];
        int line_bad = 0;

        if ((long)r.seed != state)
        {
            printf("FIRST DIFF prof %ld case %ld step %ld: Random state want %ld got %lu\n",
                   prof, ci, step, state, (unsigned long)r.seed);
            line_bad = 1;
        }

        char *have = trades_render(&list);
        if (!line_bad && nbt_str_diff(want, have, why, sizeof why))
        {
            printf("FIRST DIFF prof %ld case %ld step %ld: %s\n    want %s\n    got %s\n",
                   prof, ci, step, why, want ? want : "(none)", have);
            line_bad = 1;
        }

        if (line_bad) ++bad;
        else ++checked;

        free(want);
        free(have);
        free(one);
        line = nl ? nl + 1 : NULL;
    }

    trades_free(&list);
    free(lines);
    free(manifest);

    printf("test_trades %s: %d lines ok, %d bad\n", argv[1], checked, bad);
    return bad ? 1 : 0;
}