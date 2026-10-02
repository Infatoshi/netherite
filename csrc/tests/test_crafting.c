/* Gate: native crafting and smelting against the oracle's probe files.
 *
 *   test_crafting DIR
 *
 * DIR holds manifest.json plus three JSONL files (oracle/harness/netherite/oracle/
 * CraftingProbe.java):
 *
 *   cases.jsonl    20000 random grids from the probe's plans, each with the
 *                  result CraftingManager.findMatchingRecipe gave, or the
 *                  exception it threw.
 *   special.jsonl  hand-built grids for the recipe classes and the repair path
 *                  the random plans reach rarely (every dyeable armor case, book
 *                  cloning with and without a tag, map cloning and extending,
 *                  the three fireworks shapes, a mirrored recipe over a tagged
 *                  stack).
 *   smelting.jsonl every smelting input the probe asked about, with
 *                  getSmeltingResult and the raw bits of getSmeltingExperience.
 *
 * Every row is replayed through craft_find_matching (with a null world, as the
 * probe calls it) and smelt_result / smelt_experience. A mismatch prints the row
 * number, its name, and the first differing field; the first ten print in full.
 * The dye damage to fleece index table is also checked against the formula the
 * native code uses, so a generator that drops an entry fails here.
 */
#include "../engine/crafting.h"
#include "../engine/smelting.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int bad, shown;
static long cases_n, special_n, smelt_n;

static char *dup_span(const char *s, size_t n)
{
    char *d = malloc(n + 1);
    if (d == NULL) abort();
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

/* The whole file, NUL-terminated, or NULL. */
static char *read_whole(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    size_t cap = 1 << 16, n = 0;
    char *b = malloc(cap);
    if (b == NULL) abort();
    for (;;)
    {
        if (n + 4097 > cap)
        {
            cap *= 2;
            b = realloc(b, cap);
            if (b == NULL) abort();
        }
        size_t got = fread(b + n, 1, 4096, f);
        n += got;
        if (got < 4096) break;
    }
    b[n] = 0;
    fclose(f);
    return b;
}

/* ---- a JSON reader for one line -------------------------------------- */

/* The end of the JSON value starting at p, or NULL. */
static const char *span_end(const char *p)
{
    if (*p == '"')
    {
        for (++p; *p != 0; ++p)
        {
            if (*p == '\\' && p[1] != 0) { ++p; continue; }
            if (*p == '"') return p + 1;
        }
        return NULL;
    }

    if (*p == '{' || *p == '[')
    {
        int depth = 0;

        for (; *p != 0; ++p)
        {
            if (*p == '"')
            {
                const char *q = span_end(p);

                if (q == NULL) return NULL;
                p = q - 1;
                continue;
            }

            if (*p == '{' || *p == '[')
            {
                ++depth;
            }
            else if (*p == '}' || *p == ']')
            {
                if (--depth == 0) return p + 1;
            }
        }

        return NULL;
    }

    while (*p != 0 && *p != ',' && *p != '}' && *p != ']') ++p;
    return p;
}

/* The value of key in the object [obj, stop), or NULL; the value spans to *end. */
static const char *member(const char *obj, const char *stop, const char *key, const char **end)
{
    const char *p = obj + 1;

    while (p < stop && *p != '}')
    {
        if (*p != '"') return NULL;

        const char *kend = span_end(p);

        if (kend == NULL || kend[0] != ':' || kend + 1 > stop) return NULL;

        size_t klen = (size_t)(kend - p - 2);
        const char *vend = span_end(kend + 1);

        if (vend == NULL || vend > stop) return NULL;

        if (strlen(key) == klen && memcmp(p + 1, key, klen) == 0)
        {
            *end = vend;
            return kend + 1;
        }

        p = vend;
        if (*p == ',') ++p;
    }

    return NULL;
}

static long number(const char *obj, const char *stop, const char *key, int *ok)
{
    const char *v, *e;

    v = member(obj, stop, key, &e);
    *ok = v != NULL;
    return v != NULL ? strtol(v, NULL, 10) : 0;
}

/* One ItemStack as the files print it: {"item":id,"count":n,"damage":d,"tag":...}
 * or null. The tag is canonical NBT, so nbt_parse reads the span directly. */
static int read_stack(const char *obj, const char *stop, struct craft_stack *out)
{
    out->item = -1;
    out->count = 0;
    out->damage = 0;
    out->tag = 0;

    if (strncmp(obj, "null", 4) == 0) return 1;

    int ok;
    out->item = (int)number(obj, stop, "item", &ok);
    if (!ok) return 0;
    out->count = (int)number(obj, stop, "count", &ok);
    if (!ok) return 0;
    out->damage = (int)number(obj, stop, "damage", &ok);
    if (!ok) return 0;

    const char *v, *e;

    v = member(obj, stop, "tag", &e);
    if (v == NULL) return 0;

    if (strncmp(v, "null", 4) != 0)
    {
        char *text = dup_span(v, (size_t)(e - v));
        nbt *tree = nbt_parse(text);
        free(text);
        if (tree == NULL) return 0;
        out->tag = itag_from_tree(tree);
        nbt_free(tree);
    }

    return 1;
}

/* One recorded row. */
struct row {
    long n;
    char name[160];
    int side;
    struct craft_grid grid;
    enum craft_status expect;  /* none, stack or throw */
    struct craft_stack out;
    char threw[80];
};

/* Parses a cases.jsonl or special.jsonl line, or 0 when it is malformed. */
static int read_row(const char *line, const char *stop, struct row *r)
{
    int ok;

    memset(r, 0, sizeof *r);
    r->n = number(line, stop, "n", &ok);
    if (!ok) return 0;
    r->side = (int)number(line, stop, "side", &ok);
    if (!ok) return 0;

    const char *v, *e;

    v = member(line, stop, "name", &e);
    if (v != NULL && *v == '"')
    {
        size_t n = (size_t)(e - v - 2);
        if (n >= sizeof r->name) n = sizeof r->name - 1;
        memcpy(r->name, v + 1, n);
        r->name[n] = 0;
    }

    v = member(line, stop, "grid", &e);
    if (v == NULL || *v != '[') return 0;

    craft_grid_clear(&r->grid, r->side);

    const char *p = v + 1;
    int i = 0;

    while (p < e && *p != ']')
    {
        const char *elend = span_end(p);

        if (elend == NULL || elend > e || i >= CRAFT_MAX_SLOTS) return 0;
        if (!read_stack(p, elend, &r->grid.slot[i])) return 0;

        ++i;
        p = elend;
        if (*p == ',') ++p;
    }

    if (i != r->side * r->side) return 0;

    v = member(line, stop, "throw", &e);

    if (v != NULL)
    {
        size_t n = (size_t)(e - v - 2);
        if (n >= sizeof r->threw) n = sizeof r->threw - 1;
        memcpy(r->threw, v + 1, n);
        r->threw[n] = 0;
        r->expect = CRAFT_THROW;
        return 1;
    }

    v = member(line, stop, "out", &e);
    if (v == NULL) return 0;

    if (strncmp(v, "null", 4) == 0)
    {
        r->expect = CRAFT_NONE;
        return 1;
    }

    if (!read_stack(v, e, &r->out)) return 0;
    r->expect = CRAFT_STACK;
    return 1;
}

static void free_row(struct row *r)
{
    for (int i = 0; i < CRAFT_MAX_SLOTS; ++i) craft_stack_free(&r->grid.slot[i]);
    craft_stack_free(&r->out);
}

/* Where the row and what the port made of it differ, or NULL. */
static const char *compare(const struct row *r, const struct craft_result *got, char *buf, size_t cap)
{
    if (r->expect != got->status)
    {
        snprintf(buf, cap, "status: want %s got %s",
            r->expect == CRAFT_NONE ? "none" : r->expect == CRAFT_STACK ? "stack" : "throw",
            got->status == CRAFT_NONE ? "none" : got->status == CRAFT_STACK ? "stack" : "throw");
        return buf;
    }

    if (got->status == CRAFT_THROW)
    {
        if (strcmp(r->threw, got->threw) != 0)
        {
            snprintf(buf, cap, "exception: want %s got %s", r->threw, got->threw);
            return buf;
        }

        return NULL;
    }

    if (got->status == CRAFT_NONE) return NULL;

    if (r->out.item != got->out.item)
    {
        snprintf(buf, cap, "item: want %d got %d", r->out.item, got->out.item);
        return buf;
    }

    if (r->out.count != got->out.count)
    {
        snprintf(buf, cap, "count: want %d got %d", r->out.count, got->out.count);
        return buf;
    }

    if (r->out.damage != got->out.damage)
    {
        snprintf(buf, cap, "damage: want %d got %d", r->out.damage, got->out.damage);
        return buf;
    }

    char *want = dup_span(r->out.tag ? itag_text(r->out.tag) : "", r->out.tag ? strlen(itag_text(r->out.tag)) : 0);
    char *have = dup_span(got->out.tag ? itag_text(got->out.tag) : "", got->out.tag ? strlen(itag_text(got->out.tag)) : 0);

    if (strcmp(want, have) != 0)
    {
        snprintf(buf, cap, "tag: want %s got %s", want, have);
        free(want);
        free(have);
        return buf;
    }

    free(want);
    free(have);
    return NULL;
}

static void fail(const char *file, const struct row *r, const struct craft_result *got, const char *why)
{
    ++bad;

    if (shown++ < 10)
    {
        printf("FAIL %s row %ld %s: %s\n", file, r->n, r->name[0] != 0 ? r->name : "", why);
        printf("  side %d grid:", r->side);

        for (int i = 0; i < r->side * r->side; ++i)
        {
            const struct craft_stack *s = &r->grid.slot[i];

            if (craft_slot_empty(s)) printf(" -");
            else printf(" %d/%d/%d", s->item, s->count, s->damage);
        }

        printf("\n");
    }
}

/* One JSONL file of cases: every line replayed, mismatches counted. */
static int check_rows(const char *dir, const char *file, long *out_n)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", dir, file);

    char *text = read_whole(path);
    if (text == NULL)
    {
        fprintf(stderr, "test_crafting: cannot read %s\n", path);
        return 0;
    }

    long n = 0;
    char *p = text;

    while (*p != 0)
    {
        char *nl = strchr(p, '\n');
        if (nl != NULL) *nl = 0;

        struct row r;

        if (!read_row(p, p + strlen(p), &r))
        {
            printf("FAIL %s line %ld: not a case row\n", file, n + 1);
            ++bad;
        }
        else
        {
            struct craft_result res;

            craft_find_matching(&r.grid, NULL, &res);

            char why[4096];
            const char *diff = compare(&r, &res, why, sizeof why);

            if (diff != NULL) fail(file, &r, &res, diff);

            craft_result_free(&res);
            free_row(&r);
        }

        ++n;
        p = nl != NULL ? nl + 1 : p + strlen(p);
    }

    free(text);
    if (out_n != NULL) *out_n = n;
    return 1;
}

/* One smelting row: getSmeltingResult and the raw bits of
 * getSmeltingExperience. */
static void check_smelting(const char *dir)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/smelting.jsonl", dir);

    char *text = read_whole(path);
    if (text == NULL)
    {
        fprintf(stderr, "test_crafting: cannot read %s\n", path);
        return;
    }

    long n = 0;
    char *p = text;

    while (*p != 0)
    {
        char *nl = strchr(p, '\n');
        if (nl != NULL) *nl = 0;

        const char *stop = p + strlen(p);
        const char *v, *e;
        int ok;
        struct craft_stack in, want;

        memset(&in, 0, sizeof in);
        memset(&want, 0, sizeof want);
        in.item = -1;
        want.item = -1;

        v = member(p, stop, "in", &e);

        if (v == NULL || !read_stack(v, e, &in))
        {
            printf("FAIL smelting.jsonl row %ld: bad input\n", n);
            ++bad;
        }
        else
        {
            v = member(p, stop, "out", &e);
            int want_none = v != NULL && strncmp(v, "null", 4) == 0;

            if (v == NULL || (!want_none && !read_stack(v, e, &want)))
            {
                printf("FAIL smelting.jsonl row %ld: bad result\n", n);
                ++bad;
            }
            else
            {
                struct craft_stack got;
                int found = smelt_result(in.item, in.damage, &got);

                long bits = number(p, stop, "xp", &ok);
                unsigned want_bits = (unsigned)bits;

                if (want_none != !found)
                {
                    printf("FAIL smelting row %ld item %d damage %d: want %s got %s\n", n,
                        in.item, in.damage, want_none ? "nothing" : "a result", found ? "a result" : "nothing");
                    ++bad;
                }
                else if (found && (got.item != want.item || got.count != want.count || got.damage != want.damage))
                {
                    printf("FAIL smelting row %ld item %d damage %d: want %d/%d/%d got %d/%d/%d\n", n,
                        in.item, in.damage, want.item, want.count, want.damage, got.item, got.count, got.damage);
                    ++bad;
                }

                if (!want_none) craft_stack_free(&want);

                float xp = smelt_experience(in.item, in.damage);
                unsigned got_bits;
                memcpy(&got_bits, &xp, sizeof got_bits);

                if (got_bits != want_bits)
                {
                    printf("FAIL smelting row %ld item %d damage %d: xp want %08x got %08x\n", n,
                        in.item, in.damage, want_bits, got_bits);
                    ++bad;
                }
            }

            craft_stack_free(&in);
        }

        ++n;
        p = nl != NULL ? nl + 1 : p + strlen(p);
    }

    free(text);
    smelt_n = n;
}

/* The generated dye table against the formula the native code uses. */
static void check_dye_table(void)
{
    for (int i = 0; i < 16; ++i)
    {
        if (CRAFT_DYE_TO_FLEECE[i] != (~i & 15))
        {
            printf("FAIL dye table: CRAFT_DYE_TO_FLEECE[%d] is %d, ~i & 15 is %d\n",
                i, CRAFT_DYE_TO_FLEECE[i], ~i & 15);
            ++bad;
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: test_crafting DIR\n");
        return 2;
    }

    char path[4096];
    snprintf(path, sizeof path, "%s/manifest.json", argv[1]);

    char *manifest = read_whole(path);
    if (manifest == NULL)
    {
        fprintf(stderr, "test_crafting: cannot read %s\n", path);
        return 2;
    }

    int ok;
    long want_cases = number(manifest, manifest + strlen(manifest), "cases", &ok);
    long want_special = number(manifest, manifest + strlen(manifest), "specialRows", &ok);
    long want_smelt = number(manifest, manifest + strlen(manifest), "smeltingRows", &ok);

    check_dye_table();
    if (!check_rows(argv[1], "cases.jsonl", &cases_n)) return 2;
    if (!check_rows(argv[1], "special.jsonl", &special_n)) return 2;
    check_smelting(argv[1]);

    if (cases_n != want_cases || special_n != want_special || smelt_n != want_smelt)
    {
        printf("FAIL counts: manifest says %ld/%ld/%ld, files hold %ld/%ld/%ld\n",
            want_cases, want_special, want_smelt, cases_n, special_n, smelt_n);
        ++bad;
    }

    printf("%s: %ld cases, %ld special, %ld smelting, %d mismatches\n",
        argv[1], cases_n, special_n, smelt_n, bad);
    return bad != 0;
}