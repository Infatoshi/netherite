/* Gate: native block drops against the oracle's DropsProbe dump.
 *
 *   test_drops DIR
 *
 * DIR holds manifest.json (seed, drop location, math_seed) and lines.jsonl,
 * one line per case: a block placed at its location, the world Random seeded,
 * Block.dropBlockAsItemWithChance called, and every entity it spawned recorded
 * in full (item and damage, canonical stack NBT, xp value, position, motion,
 * yaw). The native side replays every case in file order and compares entity
 * by entity.
 *
 * Two streams have to line up. The world Random is seeded to the case's opseed
 * inside drops_break, so each case stands alone. Math.random is one stream for
 * the whole sweep: before each case the state is saved, and after the
 * comparison the stream is put back and advanced by four nextDouble per
 * recorded entity, so one bad case cannot desynchronise the rest.
 */
#define _POSIX_C_SOURCE 200809L

#include "../engine/drops.h"
#include "../engine/nbtjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static int64_t dbits(double d)
{
    int64_t b;
    memcpy(&b, &d, 8);
    return b;
}

static int64_t fbits(float f)
{
    int32_t b;
    memcpy(&b, &f, 4);
    return b;
}

/* The whole file, NUL-terminated, or NULL. */
static char *read_whole(const char *path)
{
    FILE *f = fopen(path, "rb");

    if (!f) return 0;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long n = ftell(f);
    rewind(f);
    char *s = malloc((size_t)n + 1);

    if (!s) { fclose(f); return 0; }
    if (fread(s, 1, (size_t)n, f) != (size_t)n) { free(s); fclose(f); return 0; }
    s[n] = 0;
    fclose(f);
    return s;
}

/* Within [a,b), the value token of "key": . *val and *len describe it and the
 * return is past it, or NULL when the key is absent. Strings lose their
 * quotes; objects and arrays keep their braces. */
static const char *scan_value(const char *a, const char *b, const char *key,
                              const char **val, size_t *len)
{
    char pat[64];
    size_t kn = strlen(key);
    size_t pn;

    if (kn + 4 > sizeof pat) return 0;

    snprintf(pat, sizeof pat, "\"%s\":", key);
    pn = strlen(pat);

    for (const char *p = a; p + pn <= b; ++p)
    {
        const char *q;

        if (memcmp(p, pat, pn) != 0) continue;

        q = p + pn;

        if (q < b && *q == '"')
        {
            const char *s = ++q;

            while (q < b && *q != '"') ++q;
            if (q >= b) return 0;
            *val = s;
            *len = (size_t)(q - s);
            return q + 1;
        }

        if (q < b && (*q == '{' || *q == '['))
        {
            char open = *q, close = open == '{' ? '}' : ']';
            const char *s = q;
            int depth = 0, instr = 0;

            while (q < b)
            {
                if (instr) { if (*q == '"') instr = 0; }
                else if (*q == '"') instr = 1;
                else if (*q == open) ++depth;
                else if (*q == close)
                {
                    if (--depth == 0) { ++q; break; }
                }

                ++q;
            }

            *val = s;
            *len = (size_t)(q - s);
            return q;
        }

        {
            const char *s = q;

            while (q < b && *q != ',' && *q != '}' && *q != ']') ++q;
            *val = s;
            *len = (size_t)(q - s);
            return q;
        }
    }

    return 0;
}

static long long lnum(const char *v)
{
    return strtoll(v, 0, 10);
}

static int eq_span(const char *v, size_t n, const char *s)
{
    return strlen(s) == n && memcmp(v, s, n) == 0;
}

struct mismatches {
    int shown, total, lines;
};

static void bad(struct mismatches *m, int casei, const char *what, long long want,
                long long got, const char *line)
{
    ++m->total;

    if (m->shown < 20)
    {
        ++m->shown;
        printf("case %d %s: want %lld got %lld\n", casei, what, want, got);
        printf("  line: %.200s\n", line);
    }
}

/* One recorded entity: kind, the stack, the xp value, where it is and how it
 * moves. */
struct rec_ent {
    int kind;             /* 0 item, 1 xp */
    long long item, damage, count, xp;
    long long x, y, z, mx, my, mz, yaw;
    char nbt[256];
};

static int parse_ent(const char *a, const char *b, struct rec_ent *e)
{
    const char *v;
    size_t n;

    memset(e, 0, sizeof *e);

    if (!scan_value(a, b, "kind", &v, &n)) return 0;
    e->kind = eq_span(v, n, "xp") ? 1 : 0;

    if (e->kind == 0)
    {
        if (!scan_value(a, b, "id", &v, &n)) return 0;
        e->item = lnum(v);
        if (!scan_value(a, b, "damage", &v, &n)) return 0;
        e->damage = lnum(v);
        if (!scan_value(a, b, "count", &v, &n)) return 0;
        e->count = lnum(v);

        if (!scan_value(a, b, "nbt", &v, &n)) return 0;
        if (n >= sizeof e->nbt) return 0;
        memcpy(e->nbt, v, n);
        e->nbt[n] = 0;
    }
    else
    {
        if (!scan_value(a, b, "xp", &v, &n)) return 0;
        e->xp = lnum(v);
    }

    if (!scan_value(a, b, "x", &v, &n)) return 0;
    e->x = lnum(v);
    if (!scan_value(a, b, "y", &v, &n)) return 0;
    e->y = lnum(v);
    if (!scan_value(a, b, "z", &v, &n)) return 0;
    e->z = lnum(v);
    if (!scan_value(a, b, "mx", &v, &n)) return 0;
    e->mx = lnum(v);
    if (!scan_value(a, b, "my", &v, &n)) return 0;
    e->my = lnum(v);
    if (!scan_value(a, b, "mz", &v, &n)) return 0;
    e->mz = lnum(v);
    if (!scan_value(a, b, "yaw", &v, &n)) return 0;
    e->yaw = lnum(v);

    return 1;
}

static void bad_text(struct mismatches *m, int casei, const char *what,
                     const char *want, const char *got, const char *line)
{
    ++m->total;

    if (m->shown < 20)
    {
        ++m->shown;
        printf("case %d %s: want %s got %s\n", casei, what, want, got);
        printf("  line: %.200s\n", line);
    }
}

/* The canonical stack NBT the probe wrote: Short id, Byte Count, Short Damage,
 * no tag. The recorded text is parsed and rendered again, so it has to be the
 * canonical form as well as the right stack. */
static int check_nbt(struct mismatches *m, int casei, const struct rec_ent *r,
                     const struct drop_ent *e, const char *line)
{
    char want[128];
    nbt *t = nbt_parse(r->nbt);

    if (!t)
    {
        bad_text(m, casei, "nbt parses", "canonical", r->nbt, line);
        return 0;
    }

    snprintf(want, sizeof want, "{\"Count\":\"b:%d\",\"Damage\":\"s:%d\",\"id\":\"s:%d\"}",
             e->count, e->damage, e->item);

    char *got = nbt_render(t);

    if (!got || strcmp(got, want) != 0)
    {
        bad_text(m, casei, "nbt", want, got ? got : "(none)", line);
        free(got);
        nbt_free(t);
        return 0;
    }

    free(got);
    nbt_free(t);
    return 1;
}

/* One case: the native break against the line's entities. */
static void one_case(int casei, const char *line, int x, int y, int z, jrand *math,
                     struct mismatches *m)
{
    const char *v, *end = line + strlen(line);
    size_t n;
    struct drop_case cs;
    struct drop_ent nat[DROPS_MAX_ENTS];
    int n_rec = 0, n_nat, i;

    memset(&cs, 0, sizeof cs);
    scan_value(line, end, "block", &v, &n);
    cs.block = (int)lnum(v);
    scan_value(line, end, "placed_meta", &v, &n);
    cs.meta = (int)lnum(v);
    scan_value(line, end, "fortune", &v, &n);
    cs.fortune = (int)lnum(v);
    scan_value(line, end, "opseed", &v, &n);
    cs.opseed = lnum(v);
    cs.x = x;
    cs.y = y;
    cs.z = z;
    cs.math = math;

    /* The entities, as object spans inside "ents":[...]. */
    const char *ents, *ents_end;
    size_t en;

    if (!scan_value(line, end, "ents", &ents, &en))
    {
        bad(m, casei, "ents present", 1, 0, line);
        return;
    }

    ents_end = ents + en;

    struct rec_ent rec[DROPS_MAX_ENTS];
    {
        const char *p = ents + 1;

        while (p < ents_end && n_rec < DROPS_MAX_ENTS)
        {
            const char *s = memchr(p, '{', (size_t)(ents_end - p));

            if (!s) break;

            const char *q = s;
            int depth = 0, instr = 0;

            while (q < ents_end)
            {
                if (instr) { if (*q == '"') instr = 0; }
                else if (*q == '"') instr = 1;
                else if (*q == '{') ++depth;
                else if (*q == '}')
                {
                    if (--depth == 0) { ++q; break; }
                }

                ++q;
            }

            if (!parse_ent(s, q, &rec[n_rec]))
            {
                bad(m, casei, "entity parses", 1, 0, line);
                return;
            }

            ++n_rec;
            p = q;
        }
    }

    jrand saved = *math;
    n_nat = drops_break(&cs, nat, DROPS_MAX_ENTS);

    if (n_nat < 0)
    {
        bad(m, casei, "drop path", 0, 1, line);
        return;
    }

    if (n_nat != n_rec)
    {
        bad(m, casei, "entity count", n_rec, n_nat, line);
    }

    for (i = 0; i < n_rec && i < n_nat; ++i)
    {
        const struct rec_ent *r = &rec[i];
        const struct drop_ent *e = &nat[i];

        if (r->kind != e->kind)
        {
            bad(m, casei, "kind", r->kind, e->kind, line);
            continue;
        }

        if (e->kind == DROP_XP)
        {
            if (r->xp != e->xp) bad(m, casei, "xp", r->xp, e->xp, line);
        }
        else
        {
            if (r->item == 0xfffe)
            {
                bad(m, casei, "item id", -1, e->item, line);
            }
            else if (r->item != e->item)
            {
                bad(m, casei, "item", r->item, e->item, line);
            }

            if (r->damage != e->damage) bad(m, casei, "damage", r->damage, e->damage, line);
            if (r->count != e->count) bad(m, casei, "count", r->count, e->count, line);
            check_nbt(m, casei, r, e, line);
        }

        if (r->x != dbits(e->x)) bad(m, casei, "x", r->x, dbits(e->x), line);
        if (r->y != dbits(e->y)) bad(m, casei, "y", r->y, dbits(e->y), line);
        if (r->z != dbits(e->z)) bad(m, casei, "z", r->z, dbits(e->z), line);
        if (r->mx != dbits(e->mx)) bad(m, casei, "mx", r->mx, dbits(e->mx), line);
        if (r->my != dbits(e->my)) bad(m, casei, "my", r->my, dbits(e->my), line);
        if (r->mz != dbits(e->mz)) bad(m, casei, "mz", r->mz, dbits(e->mz), line);
        if (r->yaw != fbits(e->yaw)) bad(m, casei, "yaw", r->yaw, fbits(e->yaw), line);
    }

    /* Whatever happened, leave the shared stream where the record says: four
     * nextDouble per recorded entity past the case's start. */
    *math = saved;

    for (i = 0; i < n_rec; ++i)
    {
        jr_double(math);
        jr_double(math);
        jr_double(math);
        jr_double(math);
    }

    ++m->lines;
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: test_drops DIR\n");
        return 2;
    }

    char path[1200];
    snprintf(path, sizeof path, "%s/manifest.json", argv[1]);
    char *json = read_whole(path);

    if (!json) { perror(path); return 2; }

    const char *v;
    size_t n;
    int x = 0, y = 0, z = 0;
    long long math_seed = 0;
    const char *jend = json + strlen(json);

    if (!scan_value(json, jend, "x", &v, &n))
    {
        fprintf(stderr, "%s: no drop location\n", path);
        return 2;
    }

    x = (int)lnum(v);

    if (!scan_value(json, jend, "y", &v, &n))
    {
        fprintf(stderr, "%s: no drop location\n", path);
        return 2;
    }

    y = (int)lnum(v);

    if (!scan_value(json, jend, "z", &v, &n))
    {
        fprintf(stderr, "%s: no drop location\n", path);
        return 2;
    }

    z = (int)lnum(v);

    if (!scan_value(json, jend, "math_seed", &v, &n))
    {
        fprintf(stderr, "%s: no math_seed\n", path);
        return 2;
    }

    math_seed = lnum(v);
    free(json);

    snprintf(path, sizeof path, "%s/lines.jsonl", argv[1]);
    FILE *f = fopen(path, "rb");

    if (!f) { perror(path); return 2; }

    jrand math;
    jr_seed(&math, math_seed);

    struct mismatches m = { 0, 0, 0 };
    char *line = 0;
    size_t cap = 0;
    ssize_t got;
    int casei = 0;

    while ((got = getline(&line, &cap, f)) > 0)
    {
        if (line[got - 1] == '\n') line[got - 1] = 0;
        if (line[0] == 0) continue;

        one_case(casei, line, x, y, z, &math, &m);
        ++casei;
    }

    free(line);
    fclose(f);

    printf("%s: %d cases, %d mismatches\n", argv[1], m.lines, m.total);

    return m.total != 0;
}