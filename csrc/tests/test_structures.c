/* Gate: native structure starts against the oracle's structure probe files.
 *
 *   test_structures DIR
 *
 * DIR holds manifest.json (seed, region and dimension) and starts.jsonl (one
 * start per line, sorted by type then chunk x then z). For every type this port
 * implements, the native walk recomputes the starts of DIR's region and seed,
 * renders each as canonical NBT, and compares line by line with the file's
 * lines of that type. A type with no native implementation yet is reported and
 * skipped, and so is a type the manifest's dimension does not have (the
 * manifest's "dim" is the overworld when it is absent). On a mismatch the type,
 * the start chunk and the first differing key path print with both values.
 *
 * A structure start depends only on the seed and the chunk, so this reads no
 * binary stage dump: it is the same kind of gate as test_worldgen, run per
 * directory under out/java/structures.
 */
#include "../engine/structure.h"
#include "../engine/nbtjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_span(const char *s, size_t n)
{
    char *d = malloc(n + 1);
    if (!d) abort();
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

/* The whole file, NUL-terminated, or NULL. */
static char *read_whole(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 1 << 16, n = 0;
    char *b = malloc(cap);
    if (!b) abort();
    for (;;)
    {
        if (n + 4097 > cap)
        {
            cap *= 2;
            b = realloc(b, cap);
            if (!b) abort();
        }
        size_t got = fread(b + n, 1, 4096, f);
        n += got;
        if (got < 4096) break;
    }
    b[n] = 0;
    fclose(f);
    return b;
}

/* The value of a top-level integer key in the manifest, e.g. "seed". */
static long manifest_int(const char *json, const char *key)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    return strtol(p + strlen(pat), NULL, 10);
}

/* The generators each dimension's terrain provider has: the overworld's four,
 * or the Nether's one bridge generator. A probe file holds the types of its own
 * dimension only; a type that belongs to another one is not compared. */
static const char *const overworld_types[] = {"Mineshaft", "Stronghold", "Temple", "Village"};
static const char *const nether_types[] = {"Fortress"};

static int type_in_dimension(long dim, const char *type)
{
    const char *const *names = dim == -1 ? nether_types : overworld_types;
    int n = dim == -1 ? (int)(sizeof nether_types / sizeof nether_types[0])
                      : (int)(sizeof overworld_types / sizeof overworld_types[0]);
    for (int i = 0; i < n; ++i)
        if (!strcmp(names[i], type)) return 1;
    return 0;
}

struct wanted { char *type; int n; };

/* The type names the file holds, taken from the keys of the manifest's "counts"
 * object, in the order the file lists them. */
static int collect_types(const char *json, struct wanted **out)
{
    const char *p = strstr(json, "\"counts\":{");
    if (!p) return 0;
    p += strlen("\"counts\":{");
    /* End of the counts object, so a key after it (the manifest's "stronghold"
     * list) is never mistaken for a type. */
    const char *stop = strchr(p, '}');
    if (!stop) return 0;
    int cap = 8, n = 0;
    struct wanted *w = malloc((size_t)cap * sizeof *w);
    if (!w) abort();
    while (p < stop)
    {
        const char *end;
        const char *colon;
        if (*p != '"') break;
        end = strchr(p + 1, '"');
        if (!end || end > stop) break;
        if (n == cap)
        {
            cap *= 2;
            w = realloc(w, (size_t)cap * sizeof *w);
            if (!w) abort();
        }
        w[n].type = dup_span(p + 1, (size_t)(end - p - 1));
        w[n].n = 0;
        ++n;
        colon = strchr(end, ':');
        if (!colon || colon > stop) break;
        p = strchr(colon, ',');
        if (!p || p > stop) break;
        ++p;
    }
    *out = w;
    return n;
}

/* One field of a starts.jsonl line. cx and cz are numbers, type a string, nbt
 * the object that follows it: this returns a span for each, and the number for
 * cx and cz. 1 when the key is present. */
static int json_field(const char *line, const char *key, const char **val, size_t *len, long *num)
{
    char pat[32];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(line, pat);
    if (!p) return 0;
    p += strlen(pat);
    if (p[0] == '"')
    {
        const char *end = strchr(p + 1, '"');
        if (!end) return 0;
        *val = p + 1;
        *len = (size_t)(end - p - 1);
        return 1;
    }
    if (p[0] == '{')
    {
        /* Canonical NBT has no braces or quotes inside strings except the NBT
         * text keys and values, so track string state while matching braces. */
        int depth = 0, in_str = 0;
        const char *q = p;
        for (; *q; ++q)
        {
            if (in_str)
            {
                if (*q == '\\') ++q;
                else if (*q == '"') in_str = 0;
                continue;
            }
            if (*q == '"') in_str = 1;
            else if (*q == '{') ++depth;
            else if (*q == '}' && --depth == 0) { ++q; break; }
        }
        *val = p;
        *len = (size_t)(q - p);
        return 1;
    }
    *num = strtol(p, NULL, 10);
    return 1;
}

/* One probe line: its type, the chunk it begins in, and the start's NBT, which
 * canonical NBT already is, so nbt_parse reads it directly. */
static int line_start(const char *line, char *type, size_t typen, int *cx, int *cz,
                      const char **nbt, size_t *nbtn)
{
    const char *v;
    size_t n;
    long num;
    if (!json_field(line, "type", &v, &n, &num)) return 0;
    if (n >= typen) return 0;
    memcpy(type, v, n);
    type[n] = 0;
    if (!json_field(line, "cx", &v, &n, &num)) return 0;
    *cx = (int)num;
    if (!json_field(line, "cz", &v, &n, &num)) return 0;
    *cz = (int)num;
    if (!json_field(line, "nbt", &v, &n, &num)) return 0;
    *nbt = v;
    *nbtn = n;
    return 1;
}

int main(int argc, char **argv)
{
    char path[4096];
    char *manifest, *starts;
    struct wanted *want = NULL;
    int nwant, bad = 0, i;

    if (argc < 2)
    {
        fprintf(stderr, "usage: test_structures DIR\n");
        return 2;
    }

    snprintf(path, sizeof path, "%s/manifest.json", argv[1]);
    manifest = read_whole(path);
    if (!manifest)
    {
        fprintf(stderr, "test_structures: cannot read %s\n", path);
        return 2;
    }

    long seed = manifest_int(manifest, "seed");
    long dim = manifest_int(manifest, "dim");   /* 0 when the file has none */
    int x0 = (int)manifest_int(manifest, "x0");
    int z0 = (int)manifest_int(manifest, "z0");
    int x1 = (int)manifest_int(manifest, "x1");
    int z1 = (int)manifest_int(manifest, "z1");

    nwant = collect_types(manifest, &want);

    snprintf(path, sizeof path, "%s/starts.jsonl", argv[1]);
    starts = read_whole(path);
    if (!starts)
    {
        fprintf(stderr, "test_structures: cannot read %s\n", path);
        return 2;
    }

    /* How many lines of the file each type has. */
    for (i = 0; i < nwant; ++i)
    {
        const char *line = starts;
        while (line && *line)
        {
            char type[64];
            int cx, cz;
            const char *nbt;
            size_t nbtn;
            if (line_start(line, type, sizeof type, &cx, &cz, &nbt, &nbtn) && !strcmp(type, want[i].type))
                ++want[i].n;
            line = strchr(line, '\n');
            if (line) ++line;
        }
    }

    for (i = 0; i < nwant; ++i)
    {
        const struct structure_type *t = structure_type_by_name(want[i].type);
        struct start_list got;
        const char *line;
        int rank = 0;
        int nbad_before;

        if (!type_in_dimension(dim, want[i].type))
        {
            if (want[i].n) printf("%s: not a type of dimension %ld, %d lines skipped\n", want[i].type, dim, want[i].n);
            continue;
        }

        if (!t)
        {
            /* A type the region happens not to contain needs no report. */
            if (want[i].n) printf("%s: not implemented, %d lines skipped\n", want[i].type, want[i].n);
            continue;
        }

        memset(&got, 0, sizeof got);
        structure_walk(t, (int64_t)seed, x0, z0, x1, z1, &got);
        nbad_before = bad;

        /* The file is sorted by type then cx then cz, which is the order the
         * walk returns, so walk the file's lines of this type in step with the
         * port's starts. */
        line = starts;
        while (line && *line)
        {
            char type[64];
            int cx, cz;
            const char *nbt_text;
            size_t nbtn;
            nbt *want_nbt, *have_nbt;
            char *nbt_json;
            struct start *g;

            if (!line_start(line, type, sizeof type, &cx, &cz, &nbt_text, &nbtn))
            {
                fprintf(stderr, "test_structures: bad line in %s\n", path);
                return 2;
            }
            if (strcmp(type, want[i].type))
            {
                line = strchr(line, '\n');
                if (line) ++line;
                continue;
            }

            /* The span is inside the whole-file buffer, so copy it: nbt_parse
             * wants a NUL-terminated string. */
            nbt_json = dup_span(nbt_text, nbtn);
            want_nbt = nbt_parse(nbt_json);
            free(nbt_json);
            if (!want_nbt)
            {
                fprintf(stderr, "test_structures: %s(%d,%d): cannot parse the file's NBT\n", type, cx, cz);
                return 2;
            }

            if (rank >= got.n)
            {
                printf("FIRST DIFF %s (%d,%d): want a start, the port has none\n", type, cx, cz);
                ++bad;
            }
            else
            {
                g = got.v[rank];
                if (g->chunk_x != cx || g->chunk_z != cz)
                {
                    printf("FIRST DIFF %s (%d,%d): the port's start is at (%d,%d)\n",
                           type, cx, cz, g->chunk_x, g->chunk_z);
                    ++bad;
                }
                else
                {
                    char why[1024];
                    have_nbt = start_nbt(g);
                    if (nbt_diff(want_nbt, have_nbt, why, sizeof why))
                    {
                        printf("FIRST DIFF %s (%d,%d): %s\n", type, cx, cz, why);
                        ++bad;
                    }
                    nbt_free(have_nbt);
                }
            }
            nbt_free(want_nbt);
            ++rank;

            line = strchr(line, '\n');
            if (line) ++line;
        }

        if (rank < got.n)
        {
            printf("FIRST DIFF %s: the port has %d starts, the file has %d\n", t->name, got.n, rank);
            ++bad;
        }
        printf("%s: %d lines, %s\n", t->name, rank, bad == nbad_before ? "ok" : "FAIL");
        start_list_free(&got);
    }

    for (i = 0; i < nwant; ++i) free(want[i].type);
    free(want);
    free(manifest);
    free(starts);
    return bad ? 1 : 0;
}