/* Gate: native inventory clicking against the oracle's probe files.
 *
 *   test_containers DIR
 *
 * DIR holds manifest.json and one gzipped JSONL file per container kind
 * (oracle/harness/netherite/oracle/ContainersProbe.java). Every kind's file is
 * replayed from a fresh container of that kind over the same inventories:
 *
 *   fill   sets the player's 40, the chest, the furnace, the grid and the
 *          cursor, then every slot is compared (so the native slot layout is
 *          checked before any click is)
 *   c      one Container.slotClick: the slot view, the cursor, the returned
 *          stack and every dropped stack are compared
 *   close  Container.onContainerClosed plus the container's own close: the same
 *          comparisons
 *
 * The first differing field is named, with the row and the click index; the
 * first rows print in full. The manifest's per-slot layout strings ("chest[12]
 * limit 64 Slot") are compared with the layout the native container built, so a
 * wrong slot order or index fails immediately rather than at the first click
 * that happens to notice.
 *
 * The JSON reader is the one test_crafting.c uses, trimmed to this file's rows.
 */
#include "../engine/container.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* A fill row's longest array: the 54-slot chest. */
#define MAX_FILL 54

/* Stop a kind after this many bad rows: after the first divergence every later
 * row reports that the state differs, which is noise. */
#define STOP_AFTER 20

static int bad, shown;
static long rows_read, clicks_read, drops_seen;

/* ---- a JSON reader for one line -------------------------------------- */

static char *dup_span(const char *s, size_t n)
{
    char *d = malloc(n + 1);
    if (d == NULL) abort();
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

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

static int member_str(const char *obj, const char *stop, const char *key, char *buf, size_t cap)
{
    const char *v, *e;

    v = member(obj, stop, key, &e);

    if (v == NULL || *v != '"') return 0;

    size_t n = (size_t)(e - v - 2);
    if (n >= cap) n = cap - 1;
    memcpy(buf, v + 1, n);
    buf[n] = 0;
    return 1;
}

static long number(const char *obj, const char *stop, const char *key, int *ok)
{
    const char *v, *e;

    v = member(obj, stop, key, &e);
    *ok = v != NULL;
    return v != NULL ? strtol(v, NULL, 10) : 0;
}

/* One stack as the files print it: null, [item,count,damage], or
 * [item,count,damage,<canonical NBT>]. */
static int read_stack(const char *obj, const char *stop, struct craft_stack *out)
{
    out->item = -1;
    out->count = 0;
    out->damage = 0;
    out->tag = 0;

    if (strncmp(obj, "null", 4) == 0) return 1;
    if (*obj != '[') return 0;

    const char *p = obj + 1;
    long v[3];
    int n = 0;

    while (p < stop && *p != ']')
    {
        const char *e = span_end(p);

        if (e == NULL || e > stop) return 0;

        if (n < 3)
        {
            v[n++] = strtol(p, NULL, 10);
        }
        else
        {
            char *text = dup_span(p, (size_t)(e - p));
            nbt *tree = nbt_parse(text);
            free(text);
            if (tree == NULL) return 0;
            out->tag = itag_from_tree(tree);
            nbt_free(tree);
        }

        p = e;
        if (*p == ',') ++p;
    }

    if (n != 3) return 0;

    out->item = (int)v[0];
    out->count = (int)v[1];
    out->damage = (int)v[2];
    return 1;
}

/* An array of at most max stacks under key. */
static int read_stacks(const char *line, const char *stop, const char *key, struct craft_stack *out, int *count,
                       int max, char *why, size_t cap)
{
    const char *v, *e;

    *count = 0;
    v = member(line, stop, key, &e);

    if (v == NULL || *v != '[')
    {
        snprintf(why, cap, "%s is missing or not an array", key);
        return 0;
    }

    const char *p = v + 1;
    int n = 0;

    while (p < e && *p != ']')
    {
        const char *elend = span_end(p);

        if (elend == NULL || elend > e || n >= max)
        {
            snprintf(why, cap, "%s has more than %d entries or is malformed", key, max);
            return 0;
        }

        if (!read_stack(p, elend, &out[n]))
        {
            snprintf(why, cap, "%s[%d] is not a stack", key, n);
            return 0;
        }

        ++n;
        p = elend;
        if (*p == ',') ++p;
    }

    *count = n;
    return 1;
}

static void free_stacks(struct craft_stack *a, int n)
{
    for (int i = 0; i < n; ++i) craft_stack_free(&a[i]);
}

/* Two stacks, either of which may be absent (NULL or item -1). */
static int stack_eq(const struct craft_stack *a, const struct craft_stack *b)
{
    int ea = a == NULL || a->item < 0;
    int eb = b == NULL || b->item < 0;

    if (ea || eb) return ea && eb;

    if (a->item != b->item || a->count != b->count || a->damage != b->damage) return 0;

    return a->tag == b->tag;
}

static void stack_text(char *buf, size_t cap, const struct craft_stack *s)
{
    if (s == NULL || s->item < 0)
    {
        snprintf(buf, cap, "null");
        return;
    }

    const char *tag = itag_text(s->tag);
    snprintf(buf, cap, "%d/%d/%d%s%s", s->item, s->count, s->damage, tag != NULL ? " " : "", tag != NULL ? tag : "");
}

/* ---- the file -------------------------------------------------------- */

static char *read_gz(const char *path)
{
    gzFile g = gzopen(path, "rb");

    if (g == NULL) return NULL;

    size_t cap = 1 << 16, n = 0;
    char *b = malloc(cap);
    if (b == NULL) abort();

    for (;;)
    {
        if (n + (1 << 16) + 1 > cap)
        {
            cap *= 2;
            b = realloc(b, cap);
            if (b == NULL) abort();
        }

        int got = gzread(g, b + n, 1 << 16);
        if (got <= 0) break;
        n += (size_t)got;
    }

    gzclose(g);
    b[n] = 0;
    return b;
}

/* ---- one kind -------------------------------------------------------- */

struct kind_info {
    char name[32];
    char file[64];
    int slots, clicks, chest_size, grid_size;
};

static const char *inv_name(enum inv_kind k)
{
    switch (k)
    {
        case INV_RESULT: return "result";
        case INV_CRAFT: return "grid";
        case INV_CHEST: return "chest";
        case INV_FURNACE: return "furnace";
        default: return "player";
    }
}

static const char *slot_class_name(enum slot_kind k)
{
    switch (k)
    {
        case SLOT_CRAFT_OUT: return "SlotCrafting";
        case SLOT_FURNACE_OUT: return "SlotFurnace";
        default: return "Slot";
    }
}

/* The row's slots array against the container's slot list. */
static int cmp_slots(struct container *c, const char *line, const char *stop, char *why, size_t cap)
{
    struct craft_stack want[MAX_FILL];
    int n = 0;

    /* a container has at most 90 slots; the chest is the biggest fill array */
    static struct craft_stack big[CONTAINER_MAX_SLOTS];
    struct craft_stack *use = n > 0 ? want : big;

    if (!read_stacks(line, stop, "slots", use, &n, CONTAINER_MAX_SLOTS, why, cap)) return 0;

    if (n != c->nslots)
    {
        snprintf(why, cap, "slots: the row has %d, the container has %d", n, c->nslots);
        free_stacks(use, n);
        return 0;
    }

    for (int i = 0; i < n; ++i)
    {
        const struct craft_stack *got = container_slot(c, i);

        if (!stack_eq(&use[i], got))
        {
            char a[160], b[160];
            stack_text(a, sizeof a, &use[i]);
            stack_text(b, sizeof b, got);
            snprintf(why, cap, "slots[%d]: want %s got %s", i, a, b);
            free_stacks(use, n);
            return 0;
        }
    }

    free_stacks(use, n);
    return 1;
}

/* The row's drops array against what the last click or close dropped. */
static int cmp_drops(struct container *c, const char *line, const char *stop, char *why, size_t cap)
{
    struct craft_stack want[MAX_FILL];
    int n = 0;

    if (!read_stacks(line, stop, "drops", want, &n, MAX_FILL, why, cap)) return 0;

    if (n != c->ndrops)
    {
        snprintf(why, cap, "drops: the row dropped %d, the port dropped %d", n, c->ndrops);
        free_stacks(want, n);
        return 0;
    }

    for (int i = 0; i < n; ++i)
    {
        if (!stack_eq(&want[i], &c->drops[i]))
        {
            char a[160], b[160];
            stack_text(a, sizeof a, &want[i]);
            stack_text(b, sizeof b, &c->drops[i]);
            snprintf(why, cap, "drops[%d]: want %s got %s", i, a, b);
            free_stacks(want, n);
            return 0;
        }
    }

    free_stacks(want, n);
    return 1;
}

/* The row's cursor against the container's. */
static int cmp_cursor(struct container *c, const char *line, const char *stop, char *why, size_t cap)
{
    struct craft_stack want;
    const char *v, *e;

    v = member(line, stop, "cursor", &e);

    if (v == NULL || !read_stack(v, e, &want))
    {
        snprintf(why, cap, "cursor is missing or not a stack");
        return 0;
    }

    int ok = stack_eq(&want, &c->cursor);

    if (!ok)
    {
        char a[160], b[160];
        stack_text(a, sizeof a, &want);
        stack_text(b, sizeof b, &c->cursor);
        snprintf(why, cap, "cursor: want %s got %s", a, b);
    }

    craft_stack_free(&want);
    return ok;
}

/* The manifest's layout strings against the container the port built. */
static void check_layout(const struct container *c, const char *line, const char *stop, char *why, size_t cap)
{
    const char *v, *e;

    v = member(line, stop, "layout", &e);

    if (v == NULL || *v != '[')
    {
        snprintf(why, cap, "the manifest has no layout");
        return;
    }

    const char *p = v + 1;
    int i = 0;

    while (p < e && *p != ']')
    {
        const char *elend = span_end(p);

        if (elend == NULL || elend > e || i >= c->nslots)
        {
            snprintf(why, cap, "the layout has more slots than the container");
            return;
        }

        char want[128];

        snprintf(want, sizeof want, "%s[%d] limit %d %s", inv_name(c->slots[i].inv->kind), c->slots[i].index,
                 c->slots[i].kind == SLOT_ARMOR ? 1 : c->slots[i].inv->limit, slot_class_name(c->slots[i].kind));

        size_t n = (size_t)(elend - p - 2);

        if (n != strlen(want) || memcmp(p + 1, want, n) != 0)
        {
            snprintf(why, cap, "layout[%d]: want '%s' got '%.*s'", i, want, (int)n, p + 1);
            return;
        }

        ++i;
        p = elend;
        if (*p == ',') ++p;
    }

    if (i != c->nslots) snprintf(why, cap, "the layout has %d slots, the container has %d", i, c->nslots);
}

/* The row's ret against what the click returned. */
static int cmp_ret(const char *line, const char *stop, int has_ret, const struct craft_stack *ret, char *why, size_t cap)
{
    const char *v, *e;

    v = member(line, stop, "ret", &e);

    if (v == NULL)
    {
        snprintf(why, cap, "the row has no ret");
        return 0;
    }

    struct craft_stack want;

    if (!read_stack(v, e, &want))
    {
        snprintf(why, cap, "ret is not a stack");
        return 0;
    }

    int ok = has_ret ? stack_eq(&want, ret) : (want.item < 0);

    if (!ok)
    {
        char a[160], b[160];
        stack_text(a, sizeof a, &want);
        stack_text(b, sizeof b, has_ret ? ret : NULL);
        snprintf(why, cap, "return: want %s got %s", a, b);
    }

    craft_stack_free(&want);
    return ok;
}

static void check_kind(const char *dir, const struct kind_info *k, const char *kind_json, const char *kind_stop)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", dir, k->file);

    char *text = read_gz(path);

    if (text == NULL)
    {
        fprintf(stderr, "test_containers: cannot read %s\n", path);
        ++bad;
        return;
    }

    enum container_kind kind = strcmp(k->name, "player") == 0 ? CONTAINER_PLAYER
        : strcmp(k->name, "workbench") == 0 ? CONTAINER_WORKBENCH
        : strcmp(k->name, "furnace") == 0 ? CONTAINER_FURNACE
        : CONTAINER_CHEST;

    struct container c;
    container_init(&c, kind, k->chest_size, 0);

    char why[512];
    why[0] = 0;
    long kind_bad = 0, kind_clicks = 0, kind_drops = 0;

    if (c.nslots != k->slots)
    {
        printf("FAIL %s: the manifest says %d slots, the port built %d\n", k->file, k->slots, c.nslots);
        ++bad;
        ++kind_bad;
    }
    else
    {
        check_layout(&c, kind_json, kind_stop, why, sizeof why);

        if (why[0] != 0)
        {
            printf("FAIL %s: %s\n", k->file, why);
            ++bad;
            ++kind_bad;
        }
    }

    long line = 0;
    char *p = text;

    while (*p != 0 && kind_bad < STOP_AFTER)
    {
        char *nl = strchr(p, '\n');
        if (nl != NULL) *nl = 0;

        const char *stop = p + strlen(p);
        char op[16];

        ++line;
        why[0] = 0;

        if (!member_str(p, stop, "op", op, sizeof op))
        {
            snprintf(why, sizeof why, "the row has no op");
        }
        else if (strcmp(op, "fill") == 0)
        {
            struct craft_stack a[MAX_FILL];
            int n = 0;

            if (!read_stacks(p, stop, "player", a, &n, 40, why, sizeof why) || n != 40)
            {
                if (why[0] == 0) snprintf(why, sizeof why, "player has %d slots", n);
            }
            else
            {
                for (int i = 0; i < n; ++i) container_set_player(&c, i, &a[i]);
            }

            if (why[0] == 0 && k->chest_size > 0)
            {
                if (!read_stacks(p, stop, "chest", a, &n, MAX_FILL, why, sizeof why) || n != k->chest_size)
                {
                    if (why[0] == 0) snprintf(why, sizeof why, "chest has %d slots", n);
                }
                else
                {
                    for (int i = 0; i < n; ++i) container_set_chest(&c, i, &a[i]);
                }
            }

            if (why[0] == 0 && strcmp(k->name, "furnace") == 0)
            {
                if (!read_stacks(p, stop, "furnace", a, &n, 3, why, sizeof why) || n != 3)
                {
                    if (why[0] == 0) snprintf(why, sizeof why, "furnace has %d slots", n);
                }
                else
                {
                    for (int i = 0; i < n; ++i) container_set_furnace(&c, i, &a[i]);
                }
            }

            if (why[0] == 0 && k->grid_size > 0)
            {
                if (!read_stacks(p, stop, "grid", a, &n, 9, why, sizeof why) || n != k->grid_size)
                {
                    if (why[0] == 0) snprintf(why, sizeof why, "grid has %d slots", n);
                }
                else
                {
                    for (int i = 0; i < n; ++i) container_set_grid(&c, i, &a[i]);
                }
            }

            if (why[0] == 0)
            {
                struct craft_stack s;
                const char *v, *e;

                v = member(p, stop, "cursor", &e);

                if (v == NULL || !read_stack(v, e, &s))
                {
                    snprintf(why, sizeof why, "cursor is not a stack");
                }
                else
                {
                    container_set_cursor(&c, &s);
                }
            }

            if (why[0] == 0) cmp_slots(&c, p, stop, why, sizeof why);
        }
        else if (strcmp(op, "c") == 0)
        {
            int ok;
            int slot = (int)number(p, stop, "slot", &ok);
            int button = (int)number(p, stop, "button", &ok);
            int mode = (int)number(p, stop, "mode", &ok);

            struct craft_stack ret;
            ret.item = -1;
            ret.count = 0;
            ret.damage = 0;
            ret.tag = 0;

            int has_ret = container_slot_click(&c, slot, button, mode, &ret);

            ++kind_clicks;
            kind_drops += c.ndrops;

            if (why[0] == 0) cmp_ret(p, stop, has_ret, &ret, why, sizeof why);
            if (why[0] == 0) cmp_cursor(&c, p, stop, why, sizeof why);
            if (why[0] == 0) cmp_slots(&c, p, stop, why, sizeof why);
            if (why[0] == 0) cmp_drops(&c, p, stop, why, sizeof why);

            craft_stack_free(&ret);
        }
        else if (strcmp(op, "close") == 0)
        {
            container_close(&c);
            kind_drops += c.ndrops;

            if (why[0] == 0) cmp_cursor(&c, p, stop, why, sizeof why);
            if (why[0] == 0) cmp_slots(&c, p, stop, why, sizeof why);
            if (why[0] == 0) cmp_drops(&c, p, stop, why, sizeof why);
        }
        else
        {
            snprintf(why, sizeof why, "unknown op %s", op);
        }

        if (why[0] != 0)
        {
            ++bad;
            ++kind_bad;

            if (shown++ < 12)
            {
                printf("FAIL %s line %ld (%s): %s\n", k->file, line, op, why);
                printf("  %s\n", p);
            }
        }

        ++rows_read;
        p = nl != NULL ? nl + 1 : p + strlen(p);
    }

    clicks_read += kind_clicks;
    drops_seen += kind_drops;

    printf("%s %s: %ld clicks, %ld drops, %d slots, %ld rows bad\n", kind_bad == 0 ? "ok  " : "FAIL", k->name,
           kind_clicks, kind_drops, c.nslots, kind_bad);

    container_free(&c);
    free(text);
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("containers: no probe directory given, nothing to check\n");
        return 0;
    }

    char path[4096];
    snprintf(path, sizeof path, "%s/manifest.json", argv[1]);

    char *manifest = read_gz(path);

    if (manifest == NULL)
    {
        fprintf(stderr, "test_containers: cannot read %s\n", path);
        return 2;
    }

    char *p = strchr(manifest, '\n');
    if (p != NULL) *p = 0;

    const char *stop = manifest + strlen(manifest);
    const char *v, *e;

    v = member(manifest, stop, "kinds", &e);

    if (v == NULL || *v != '[')
    {
        fprintf(stderr, "test_containers: %s has no kinds array\n", path);
        return 2;
    }

    int kinds = 0;
    const char *q = v + 1;

    while (q < e && *q != ']')
    {
        const char *kend = span_end(q);

        if (kend == NULL || kend > e) break;

        struct kind_info k;
        memset(&k, 0, sizeof k);
        int ok;

        if (!member_str(q, kend, "kind", k.name, sizeof k.name)
            || !member_str(q, kend, "file", k.file, sizeof k.file))
        {
            fprintf(stderr, "test_containers: a kind entry has no kind or file\n");
            return 2;
        }

        k.slots = (int)number(q, kend, "slots", &ok);
        k.clicks = (int)number(q, kend, "clicks", &ok);
        k.chest_size = (int)number(q, kend, "chestSize", &ok);
        k.grid_size = (int)number(q, kend, "gridSize", &ok);

        check_kind(argv[1], &k, q, kend);
        ++kinds;

        q = kend;
        if (*q == ',') ++q;
    }

    free(manifest);

    printf("containers %s: %d kinds, %ld rows, %ld clicks, %ld drops, %d mismatches\n", argv[1], kinds, rows_read,
           clicks_read, drops_seen, bad);

    return bad == 0 ? 0 : 1;
}