/* Gate: native chest loot against the oracle's loot probe files.
 *
 *   test_loot DIR
 *
 * DIR holds manifest.json (seed, draw count, and one case per table: its name,
 * the structure's call expression, its count expression, the inventory's slot
 * count, the seed its book stack was drawn from and that stack as canonical
 * ItemStack NBT) and draws.jsonl (one line per draw: the case, the draw index,
 * the Random seed, and the non-empty slots the oracle ended up with).
 *
 * For every line this port rebuilds the draw the same way: seed a
 * java.util.Random with the line's seed, take the line's table, draw the count
 * with the structure's own expression (draws.jsonl already has the count
 * applied, so the port draws it too, in the same position), and run
 * generateChestContents into an inventory of the manifest's slot count. The
 * result is compared slot by slot as canonical ItemStack NBT, so an enchanted
 * book's StoredEnchantments are checked as well.
 *
 * Every case's book stack is also rebuilt from its bookSeed and compared with
 * the manifest's recorded stack: that is the enchantment path
 * (addRandomEnchantment -> buildEnchantmentList) on its own.
 */
#include "../engine/loot.h"
#include "../engine/nbtjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "test_loot: out of memory\n"); exit(1); }
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

/* The span of a string value, or 0. */
static int str_field(const char *line, const char *key, const char **val, size_t *len)
{
    char pat[128];
    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = strstr(line, pat);
    if (!p) return 0;
    p += strlen(pat);
    const char *s = p;
    while (*p && *p != '"') ++p;
    *val = s;
    *len = (size_t)(p - s);
    return 1;
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

/* The count kind of a case, from the manifest's count expression. */
static int count_kind(const char *expr)
{
    if (!strcmp(expr, "8")) return LOOT_COUNT_8;
    if (!strcmp(expr, "10")) return LOOT_COUNT_10;
    if (!strcmp(expr, "2")) return LOOT_COUNT_2;
    if (!strcmp(expr, "3 + rand.nextInt(4)")) return LOOT_COUNT_3_4;
    if (!strcmp(expr, "2 + rand.nextInt(2)")) return LOOT_COUNT_2_2;
    if (!strcmp(expr, "1 + rand.nextInt(4)")) return LOOT_COUNT_1_4;
    if (!strcmp(expr, "2 + rand.nextInt(5)")) return LOOT_COUNT_2_5;
    if (!strcmp(expr, "3 + rand.nextInt(6)")) return LOOT_COUNT_3_6;
    if (!strcmp(expr, "2 + rand.nextInt(4)")) return LOOT_COUNT_2_4;
    return -1;
}

/* One case out of the manifest. */
struct case_def {
    char *name;
    int slots, kind;
    long book_seed;
    char *book_nbt;      /* the structure's appended stack, or NULL */
};

/* Every case in the manifest's "cases" array. */
static int parse_cases(const char *manifest, struct case_def **out)
{
    const char *arr;
    size_t alen;
    if (!obj_field(manifest, "cases", &arr, &alen)) return 0;
    const char *end = arr + alen;
    int n = 0, cap = 8;
    struct case_def *v = xmalloc(sizeof *v * cap);

    const char *p = arr;
    while (p < end && (p = strstr(p, "{\"name\":\"")) != NULL)
    {
        /* the object's end: brace matching from here */
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
        memset(&v[n], 0, sizeof v[n]);

        const char *val;
        size_t len;
        if (str_field(obj, "name", &val, &len)) v[n].name = dup_span(val, len);
        const char *cexpr;
        size_t clen;
        if (str_field(obj, "count", &cexpr, &clen))
        {
            char *expr = dup_span(cexpr, clen);
            v[n].kind = count_kind(expr);
            free(expr);
        }
        else v[n].kind = -1;
        v[n].slots = (int)num_field(obj, "slots", 27);
        v[n].book_seed = num_field(obj, "bookSeed", 0);
        if (obj_field(obj, "book", &val, &len) && *val == '{') v[n].book_nbt = dup_span(val, len);

        ++n;
        free(obj);
        p = q;
    }
    *out = v;
    return n;
}

static const struct case_def *find_case(const struct case_def *v, int n, const char *name)
{
    for (int i = 0; i < n; ++i)
        if (!strcmp(v[i].name, name)) return &v[i];
    return NULL;
}

/* The canonical stack of one slot, or NULL when the slot is empty. */
static char *slot_nbt(const struct loot_stack *s)
{
    if (s->item <= 0) return NULL;
    nbt *root = nbt_new_compound();
    nbt_put(root, "id", nbt_new_short(s->item));
    nbt_put(root, "Count", nbt_new_byte(s->count));
    nbt_put(root, "Damage", nbt_new_short(s->damage));
    itag_put(root, s->tag);
    char *out = nbt_render(root);
    nbt_free(root);
    return out;
}

/* 1 when two canonical NBT strings differ, printing the first key path. */
static int nbt_str_diff(const char *want, const char *have, char *why, size_t whyn)
{
    nbt *a = nbt_parse(want);
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
        printf("test_loot: usage: test_loot DIR\n");
        return 0;
    }

    char path[4096];
    snprintf(path, sizeof path, "%s/manifest.json", argv[1]);
    char *manifest = read_whole(path);
    if (!manifest) { printf("test_loot: no %s\n", path); return 1; }
    snprintf(path, sizeof path, "%s/draws.jsonl", argv[1]);
    char *lines = read_whole(path);
    if (!lines) { printf("test_loot: no %s\n", path); return 1; }

    struct case_def *cases = NULL;
    int ncases = parse_cases(manifest, &cases);
    if (!ncases) { printf("test_loot: %s lists no cases\n", argv[1]); return 2; }

    int bad = 0, book_ok = 0, checked = 0;

    /* every case's book stack, rebuilt from its bookSeed. The manifest records the
     * whole stack (ItemStack.writeToNBT); this port builds the tag compound, so
     * the manifest's tag is what gets compared. */
    for (int i = 0; i < ncases; ++i)
    {
        const struct loot_table *t = loot_table_by_name(cases[i].name);
        if (!t) continue;
        const struct loot_entry *last = &t->entries[t->n - 1];
        if (last->book_enchantability < 0) continue;

        jrand r;
        jr_seed(&r, cases[i].book_seed);
        struct loot_book book;
        loot_book_build(&r, last, &book);

        /* the manifest's stack tag */
        nbt *stack = cases[i].book_nbt ? nbt_parse(cases[i].book_nbt) : NULL;
        const nbt *want_tag = stack ? nbt_get(stack, "tag") : NULL;
        char *want = want_tag ? nbt_render(want_tag) : NULL;

        char why[1024];
        if (!book.tag || !want || nbt_str_diff(want, itag_text(book.tag), why, sizeof why))
        {
            printf("FIRST DIFF %s book seed %ld: want %s got %s\n", cases[i].name, cases[i].book_seed,
                   want ? want : "(none)", book.tag ? itag_text(book.tag) : "(none)");
            ++bad;
        }
        else ++book_ok;

        free(want);
        if (stack) nbt_free(stack);
        loot_book_free(&book);
    }

    /* every draw line, in file order */
    const char *line = lines;
    while (line && *line)
    {
        const char *nl = strchr(line, '\n');
        size_t linelen = nl ? (size_t)(nl - line) : strlen(line);
        char *one = dup_span(line, linelen);

        const char *val;
        size_t len;
        char name[64] = {0};
        if (str_field(one, "case", &val, &len) && len < sizeof name)
        {
            memcpy(name, val, len);
            name[len] = 0;
        }
        long draw_seed = num_field(one, "seed", 0);
        long draw = num_field(one, "draw", -1);

        const struct case_def *c = find_case(cases, ncases, name);
        const struct loot_table *t = c ? loot_table_by_name(name) : NULL;
        if (!c || !t || c->kind < 0)
        {
            printf("FIRST DIFF %s draw %ld: the port has no table or count for it\n", name, draw);
            ++bad;
            free(one);
            line = nl ? nl + 1 : NULL;
            continue;
        }

        jrand r;
        jr_seed(&r, draw_seed);

        /* the structure's book stack for this case, if it appends one, is a
         * fixed stack: the chest RNG does not move for it */
        struct loot_book book;
        memset(&book, 0, sizeof book);
        if (c->book_nbt)
        {
            jrand br;
            jr_seed(&br, c->book_seed);
            loot_book_build(&br, &t->entries[t->n - 1], &book);
        }

        int slots = c->slots > 0 ? c->slots : 27;
        struct loot_stack *got = xmalloc(sizeof *got * (size_t)slots);
        memset(got, 0, sizeof *got * (size_t)slots);

        int count = loot_count(&r, (enum loot_count_kind)c->kind);
        loot_generate_contents(&r, t, c->book_nbt ? &book : NULL, got, slots, count);

        /* compare every slot the oracle recorded */
        int line_bad = 0;
        const char *sp = one;
        while ((sp = strstr(sp, "\"slot\":")) != NULL)
        {
            long slot = strtol(sp + strlen("\"slot\":"), NULL, 10);
            const char *iv;
            size_t il;
            if (!obj_field(sp, "item", &iv, &il)) break;
            char *want = dup_span(iv, il);
            char *have = (slot >= 0 && slot < slots) ? slot_nbt(&got[slot]) : NULL;
            char why[1024];
            if (!have || nbt_str_diff(want, have, why, sizeof why))
            {
                printf("FIRST DIFF %s draw %ld slot %ld: want %s got %s\n",
                       name, draw, slot, want, have ? have : "(empty)");
                if (have) printf("    %s\n", why);
                line_bad = 1;
            }
            free(want);
            free(have);
            ++sp;
        }

        /* and no slot the oracle left empty */
        for (int i = 0; i < slots && !line_bad; ++i)
        {
            if (got[i].item <= 0) continue;
            char pat[64];
            snprintf(pat, sizeof pat, "\"slot\":%d,", i);
            if (!strstr(one, pat))
            {
                char *have = slot_nbt(&got[i]);
                printf("FIRST DIFF %s draw %ld slot %d: want (empty) got %s\n", name, draw, i, have);
                free(have);
                line_bad = 1;
            }
        }

        if (line_bad) ++bad;
        else ++checked;

        loot_free(got, slots);
        loot_book_free(&book);
        free(got);
        free(one);
        line = nl ? nl + 1 : NULL;
    }

    for (int i = 0; i < ncases; ++i) { free(cases[i].name); free(cases[i].book_nbt); }
    free(cases);
    free(lines);
    free(manifest);
    /* the book sweep: every stack in books.jsonl, which is the enchantment path
     * (addRandomEnchantment -> buildEnchantmentList) at volume, not just the one
     * stack per table the chest draws carry */
    int book_lines = 0;
    snprintf(path, sizeof path, "%s/books.jsonl", argv[1]);
    char *blines = read_whole(path);
    if (blines)
    {
        const char *bl = blines;
        while (bl && *bl)
        {
            const char *nl = strchr(bl, '\n');
            size_t blen = nl ? (size_t)(nl - bl) : strlen(bl);
            char *one = dup_span(bl, blen);

            const char *val;
            size_t len;
            char name[64] = {0};
            if (str_field(one, "case", &val, &len) && len < sizeof name)
            {
                memcpy(name, val, len);
                name[len] = 0;
            }
            long seed = num_field(one, "seed", 0);
            const struct loot_table *t = loot_table_by_name(name);

            if (!t || t->entries[t->n - 1].book_enchantability < 0)
            {
                printf("FIRST DIFF book %s seed %ld: the port has no book row for it\n", name, seed);
                ++bad;
                free(one);
                bl = nl ? nl + 1 : NULL;
                continue;
            }

            jrand r;
            jr_seed(&r, seed);
            struct loot_book book;
            loot_book_build(&r, &t->entries[t->n - 1], &book);

            char *want_tag = NULL;
            const char *iv;
            size_t il;
            if (obj_field(one, "item", &iv, &il))
            {
                char *want_stack = dup_span(iv, il);
                nbt *st = nbt_parse(want_stack);
                const nbt *wt = st ? nbt_get(st, "tag") : NULL;
                if (wt) want_tag = nbt_render(wt);
                if (st) nbt_free(st);
                free(want_stack);
            }

            char why[1024];
            if (!want_tag || !book.tag || nbt_str_diff(want_tag, itag_text(book.tag), why, sizeof why))
            {
                printf("FIRST DIFF book %s seed %ld: want %s got %s\n", name, seed,
                       want_tag ? want_tag : "(none)", book.tag ? itag_text(book.tag) : "(none)");
                if (want_tag && book.tag) printf("    %s\n", why);
                ++bad;
            }
            else ++book_lines;

            free(want_tag);
            loot_book_free(&book);
            free(one);
            bl = nl ? nl + 1 : NULL;
        }
        free(blines);
    }

    printf("%s: %d draws, %d + %d book stacks, %s\n", argv[1], checked, book_ok, book_lines, bad ? "FAIL" : "ok");
    return bad ? 1 : 0;
}