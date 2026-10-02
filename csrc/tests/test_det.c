/* Gate: the native determinism layer against the oracle's DetProbe reference
 * (oracle/harness/netherite/oracle/DetProbe.java, recorded with
 * `make -C oracle run CLASS=DetProbe`).
 *
 * The reference drives the live java Det through its own entry points with the
 * calling thread bound to a role, so every value here was produced by Det.role()
 * selecting a role and then the real code running. This test replays the same
 * records through det.c:
 *
 *   start.txt   the state right after Det.reset(world seed), as the seeds for
 *               the steps: the four seeders, the four Math streams, the four
 *               entity-ID counters, and every registered split Random (the
 *               vanilla statics Det.splitRandom already replaced, with names).
 *               The native state is reset into it, and separately loaded into
 *               it from the recorded 48-bit stream states (det_load +
 *               det_split_add), so both snapshot paths are checked.
 *   steps.txt.gz one line per step: the op, the role, its arguments, its values
 *               and the three digests after it (seederState, mathState,
 *               splitState for that role). Every value is compared bit for bit.
 *   end.txt     the state after the last step, including which split streams each
 *               role has drawn from (splitState only folds those).
 *   reset-*.txt Det.reset(seed) for 1, 2 and 42: the counters and every split
 *               stream reseeded by name.
 *   math.txt    StrictMath.log and StrictMath.sqrt for 4,106 inputs. C's log is
 *               not fdlibm's (it differs by an ulp on about 3% of inputs), so
 *               det_log has to match these or nextGaussian diverges.
 *   dispatch.txt the five Det.role() thread bindings, which is what det_set_role
 *               has to reproduce at the engine's tick boundaries.
 *
 * The test also checks that the role-agnostic entry points (the *_cur forms the
 * engine calls) read det_state.role, by running them beside the explicit-role
 * ones on two copies of the same state.
 *
 * Byte patterns only: values are compared as raw 64-bit patterns, never as
 * decimals, and NaN counts as equal to NaN.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "../engine/det.h"

#define MANIFEST_MAX (1 << 20)
#define DET_PATH_MAX 1200
#define DET_LINE_MAX 1024
#define MAX_TOKENS 40
#define MAX_SPLITS 512
#define MAX_ERRORS 20

/* The probe's split-name table, in the order its index refers to. Duplicating it
 * here is the check: a name that reaches the native side mangled changes the
 * hash it is seeded and folded by. */
static const char *SPLIT_NAMES[] = {
    "./detprobe/Splits.java:a", "./detprobe/Splits.java:b", "./detprobe/Splits.java:c", "./detprobe/Splits.java:d",
    "./detprobe/Splits.java:e", "./detprobe/Splits.java:f", "./detprobe/Splits.java:g", "./detprobe/Splits.java:h",
};
#define NSPLIT_NAMES ((int)(sizeof SPLIT_NAMES / sizeof SPLIT_NAMES[0]))

/* The engine's tick boundaries, and the roles Det.role() reports for them. */
static const int DISPATCH_ROLES[] = {DET_CLIENT, DET_RENDER, DET_SERVER, DET_SERVER, DET_OTHER};
static const char *DISPATCH_NAMES[] = {"client", "client-render", "server", "server-render", "other"};
#define NDISPATCH ((int)(sizeof DISPATCH_ROLES / sizeof DISPATCH_ROLES[0]))

static const char *g_dir;
static long g_bad;

static void fail(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    if (g_bad < MAX_ERRORS)
    {
        fputs("FAIL ", stdout);
        vprintf(fmt, ap);
        fputc('\n', stdout);
    }
    va_end(ap);
    ++g_bad;
}

static uint64_t dbits(double d)
{
    uint64_t b;

    memcpy(&b, &d, sizeof b);
    return b;
}

static double from_bits(uint64_t b)
{
    double d;

    memcpy(&d, &b, sizeof d);
    return d;
}

static uint32_t fbits(float f)
{
    uint32_t b;

    memcpy(&b, &f, sizeof b);
    return b;
}

/* The raw 64-bit pattern of the value a token carries. Every value token is
 * "0x" plus 16 hex digits, so a double, a 48-bit state, an int and a float's
 * bits all parse the same way. */
static uint64_t tok_bits(const char *tok)
{
    return (uint64_t)strtoull(tok, NULL, 0);
}

static void cmp_bits(const char *what, long long i, const char *tok, uint64_t got)
{
    uint64_t want = tok_bits(tok);

    if (want != got) fail("%s at step %lld: want %016llx got %016llx", what, i, (unsigned long long)want, (unsigned long long)got);
}

static void cmp_double(const char *what, long long i, const char *tok, double got)
{
    uint64_t want = tok_bits(tok), have = dbits(got);
    double w = from_bits(want);

    if (w != w)
    {
        if (got == got) fail("%s at step %lld: want NaN got %016llx", what, i, (unsigned long long)have);
        return;
    }

    if (want != have) fail("%s at step %lld: want %016llx got %016llx", what, i, (unsigned long long)want, (unsigned long long)have);
}

static char *read_file(const char *path, long *len)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL)
    {
        perror(path);
        exit(2);
    }

    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *buf = malloc((size_t)n + 1);

    if (buf == NULL || fread(buf, 1, (size_t)n, f) != (size_t)n)
    {
        fprintf(stderr, "short read in %s\n", path);
        exit(2);
    }

    fclose(f);
    buf[n] = 0;
    if (len) *len = n;
    return buf;
}

static void data_path(char *out, size_t n, const char *name)
{
    snprintf(out, n, "%s/%s", g_dir, name);
}

static long long manifest_int(const char *json, const char *key)
{
    char pat[64];

    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(json, pat);

    if (p == NULL)
    {
        fprintf(stderr, "manifest: no %s\n", key);
        exit(2);
    }

    return strtoll(p + strlen(pat), NULL, 10);
}

static void manifest_str(const char *json, const char *key, char *out, size_t n)
{
    char pat[64];

    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = strstr(json, pat);

    if (p == NULL)
    {
        fprintf(stderr, "manifest: no %s\n", key);
        exit(2);
    }

    p += strlen(pat);
    const char *e = strchr(p, '"');

    if (e == NULL)
    {
        fprintf(stderr, "manifest: %s is not a string\n", key);
        exit(2);
    }

    size_t len = (size_t)(e - p);

    if (len >= n) len = n - 1;
    memcpy(out, p, len);
    out[len] = 0;
}

/* One recorded state: the counters, the three digests per role and the split
 * list in creation order. */
struct splitref {
    char name[DET_NAME_MAX];
    uint64_t state[DET_ROLES];
    uint8_t used[DET_ROLES];
};

struct ref {
    int64_t reset_seed, world_seed;
    int32_t next_id[DET_ROLES];
    uint64_t seeder[DET_ROLES], math[DET_ROLES], split[DET_ROLES];
    struct splitref splits[MAX_SPLITS];
    int nsplits;
};

static void read_ref(struct ref *r, const char *name)
{
    char path[DET_PATH_MAX], line[DET_LINE_MAX];

    data_path(path, sizeof path, name);
    FILE *f = fopen(path, "r");

    if (f == NULL)
    {
        perror(path);
        exit(2);
    }

    memset(r, 0, sizeof *r);
    while (fgets(line, sizeof line, f))
    {
        long long v;

        if (sscanf(line, "resetSeed %lld", &v) == 1)
        {
            r->reset_seed = v;
            continue;
        }

        if (sscanf(line, "worldSeed %lld", &v) == 1)
        {
            r->world_seed = v;
            continue;
        }
        if (sscanf(line, "nextId %d %d %d %d", &r->next_id[0], &r->next_id[1], &r->next_id[2], &r->next_id[3]) == 4)
            continue;
        {
            int role;
            unsigned long long a, b, c;

            if (sscanf(line, "digest %d %llx %llx %llx", &role, &a, &b, &c) == 4)
            {
                if (role < 0 || role >= DET_ROLES) fail("%s: digest role %d", name, role);
                else
                {
                    r->seeder[role] = a;
                    r->math[role] = b;
                    r->split[role] = c;
                }
                continue;
            }
        }
        {
            struct splitref *sp = r->nsplits < MAX_SPLITS ? &r->splits[r->nsplits] : NULL;
            unsigned long long s0, s1, s2, s3;
            int u0, u1, u2, u3;
            char nm[DET_NAME_MAX];

            if (sscanf(line, "split %127s %llx %llx %llx %llx %d %d %d %d", nm, &s0, &s1, &s2, &s3, &u0, &u1, &u2, &u3) == 9)
            {
                if (sp == NULL)
                {
                    fail("%s: more than %d splits", name, MAX_SPLITS);
                    continue;
                }
                snprintf(sp->name, sizeof sp->name, "%s", nm);
                sp->state[0] = s0;
                sp->state[1] = s1;
                sp->state[2] = s2;
                sp->state[3] = s3;
                sp->used[0] = (uint8_t)u0;
                sp->used[1] = (uint8_t)u1;
                sp->used[2] = (uint8_t)u2;
                sp->used[3] = (uint8_t)u3;
                ++r->nsplits;
                continue;
            }
        }

        if (strlen(line) > 1) fail("%s: cannot parse \"%s\"", name, line);
    }

    fclose(f);
}

/* One native state against a recorded one: the counters, the digests for all
 * four roles and the split list. exact is false for the reset references, which
 * were recorded before the steps created their own splits: those compare the
 * recorded splits as the prefix of the list they are. */
static void cmp_ref(const char *what, const struct ref *r, const det_state *s, int exact)
{
    if (s->world_seed != r->world_seed) fail("%s: worldSeed want %lld got %lld", what, (long long)r->world_seed, (long long)s->world_seed);

    for (int role = 0; role < DET_ROLES; ++role)
    {
        if (s->next_id[role] != r->next_id[role])
            fail("%s: nextId role %d want %d got %d", what, role, r->next_id[role], s->next_id[role]);
        if (det_seeder_state(s, role) != r->seeder[role])
            fail("%s: seeder role %d want %016llx got %016llx", what, role, (unsigned long long)r->seeder[role],
                 (unsigned long long)det_seeder_state(s, role));
        if (det_math_state(s, role) != r->math[role])
            fail("%s: math role %d want %016llx got %016llx", what, role, (unsigned long long)r->math[role],
                 (unsigned long long)det_math_state(s, role));
        if (det_split_state(s, role) != r->split[role])
            fail("%s: splitState role %d want %016llx got %016llx", what, role, (unsigned long long)r->split[role],
                 (unsigned long long)det_split_state(s, role));
    }

    const det_split *sp = s->splits;
    int i = 0;

    for (; sp; sp = sp->next, ++i)
    {
        if (i >= r->nsplits)
        {
            if (exact) fail("%s: native has a split %s the reference does not", what, sp->name);
            break;
        }

        if (strcmp(sp->name, r->splits[i].name) != 0)
        {
            fail("%s: split %d want %s got %s", what, i, r->splits[i].name, sp->name);
            continue;
        }

        for (int role = 0; role < DET_ROLES; ++role)
        {
            if (det_rng_state(&sp->d[role]) != r->splits[i].state[role])
                fail("%s: %s state role %d want %016llx got %016llx", what, sp->name, role,
                     (unsigned long long)r->splits[i].state[role], (unsigned long long)det_rng_state(&sp->d[role]));
            if (sp->used[role] != r->splits[i].used[role])
                fail("%s: %s used role %d want %d got %d", what, sp->name, role, r->splits[i].used[role], sp->used[role]);
        }
    }

    if (exact && i < r->nsplits) fail("%s: native has %d splits, the reference %d", what, i, r->nsplits);
}

static det_split *split_at(det_state *s, int idx)
{
    det_split *sp = s->splits;

    for (int i = 0; sp && i < idx; ++i) sp = sp->next;
    return sp;
}

static int tokenize(char *line, char **tok, int max)
{
    int n = 0;

    for (char *p = strtok(line, " \t\r\n"); p; p = strtok(NULL, " \t\r\n"))
    {
        if (n < max) tok[n] = p;
        ++n;
    }

    return n;
}

static int op_nargs(int op)
{
    static const int nargs[] = {1, 0, 0, 0, 2, 3, 2};

    return op >= 0 && op < (int)(sizeof nargs / sizeof nargs[0]) ? nargs[op] : -1;
}

static int op_nvals(int op)
{
    static const int nvals[] = {8, 1, 2, 1, 4, 1, 1};

    return op >= 0 && op < (int)(sizeof nvals / sizeof nvals[0]) ? nvals[op] : -1;
}

/* Replays one step line against the native state, comparing every value and the
 * three digests the oracle recorded after it. */
static void step(det_state *s, char *line, long long expect)
{
    char *tok[MAX_TOKENS];
    int n = tokenize(line, tok, MAX_TOKENS);

    if (n < 4)
    {
        fail("step %lld: %d tokens", expect, n);
        return;
    }

    long long i = strtoll(tok[0], NULL, 10);
    int op = (int)strtol(tok[1], NULL, 10);
    int role = (int)strtol(tok[2], NULL, 10);

    if (i != expect) fail("step index want %lld got %lld", expect, i);

    if (role < 0 || role >= DET_ROLES)
    {
        fail("step %lld: role %d", i, role);
        return;
    }

    int nargs = op_nargs(op), nvals = op_nvals(op);

    if (nargs < 0)
    {
        fail("step %lld: op %d", i, op);
        return;
    }

    if (n != 3 + nargs + nvals + 3)
    {
        fail("step %lld: op %d has %d tokens, expected %d", i, op, n, 3 + nargs + nvals + 3);
        return;
    }

    const char **args = (const char **)&tok[3];
    const char **vals = (const char **)&tok[3 + nargs];

    switch (op)
    {
        case 0: /* newRandom */
        {
            int n_max = (int)strtol(args[0], NULL, 10);
            det_rng nr = det_new_random_role(s, role);

            cmp_bits("newRandom state", i, vals[0], det_rng_state(&nr));
            cmp_bits("newRandom nextInt", i, vals[1], (uint64_t)(int64_t)det_rng_int(&nr));
            cmp_bits("newRandom nextInt(n)", i, vals[2], (uint64_t)(int64_t)det_rng_int_n(&nr, n_max));
            cmp_bits("newRandom nextLong", i, vals[3], (uint64_t)det_rng_long(&nr));
            cmp_double("newRandom nextGaussian", i, vals[4], det_rng_gaussian(&nr));
            cmp_double("newRandom nextDouble", i, vals[5], det_rng_double(&nr));
            cmp_bits("newRandom nextFloat", i, vals[6], (uint64_t)(int64_t)(int32_t)fbits(det_rng_float(&nr)));
            cmp_bits("newRandom nextBoolean", i, vals[7], (uint64_t)det_rng_bool(&nr));
            break;
        }
        case 1: /* mathRandom */
            cmp_double("mathRandom", i, vals[0], det_math_random_role(s, role));
            break;
        case 2: /* uuid */
        {
            int64_t msb, lsb;

            det_uuid_role(s, role, &msb, &lsb);
            cmp_bits("uuid most", i, vals[0], (uint64_t)msb);
            cmp_bits("uuid least", i, vals[1], (uint64_t)lsb);
            break;
        }
        case 3: /* nextEntityId */
            cmp_bits("nextEntityId", i, vals[0], (uint64_t)(int64_t)det_next_entity_id_role(s, role));
            break;
        case 4: /* splitRandom */
        {
            int name_idx = (int)strtol(args[0], NULL, 10), idx = (int)strtol(args[1], NULL, 10);
            det_split *sp;

            if (name_idx < 0 || name_idx >= NSPLIT_NAMES)
            {
                fail("step %lld: split name index %d", i, name_idx);
                return;
            }

            sp = det_split_random(s, SPLIT_NAMES[name_idx]);

            if (sp == NULL)
            {
                fail("step %lld: det_split_random failed", i);
                return;
            }

            if (det_split_index(s, sp) != idx) fail("step %lld: split index want %d got %d", i, idx, det_split_index(s, sp));

            for (int r = 0; r < DET_ROLES; ++r) cmp_bits("splitRandom state", i, vals[r], det_rng_state(&sp->d[r]));
            break;
        }
        case 5: /* split draw */
        {
            int idx = (int)strtol(args[0], NULL, 10), kind = (int)strtol(args[1], NULL, 10);
            int k = (int)strtol(args[2], NULL, 10);
            det_split *sp = split_at(s, idx);

            if (sp == NULL)
            {
                fail("step %lld: no split %d", i, idx);
                return;
            }

            switch (kind)
            {
                case 0: cmp_bits("split next(bits)", i, vals[0], (uint64_t)(int64_t)det_split_next_role(s, sp, role, k)); break;
                case 1: cmp_bits("split nextInt", i, vals[0], (uint64_t)(int64_t)det_split_int_role(s, sp, role)); break;
                case 2:
                    cmp_bits("split nextInt(n)", i, vals[0], (uint64_t)(int64_t)det_split_int_n_role(s, sp, role, k));
                    break;
                case 3: cmp_bits("split nextLong", i, vals[0], (uint64_t)det_split_long_role(s, sp, role)); break;
                case 4: cmp_double("split nextDouble", i, vals[0], det_split_double_role(s, sp, role)); break;
                case 5: cmp_bits("split nextFloat", i, vals[0], (uint64_t)(int64_t)(int32_t)fbits(det_split_float_role(s, sp, role))); break;
                case 6: cmp_bits("split nextBoolean", i, vals[0], (uint64_t)det_split_bool_role(s, sp, role)); break;
                default: cmp_double("split nextGaussian", i, vals[0], det_split_gaussian_role(s, sp, role)); break;
            }
            break;
        }
        default: /* split setSeed */
        {
            int idx = (int)strtol(args[0], NULL, 10);
            int64_t seed = (int64_t)strtoll(args[1], NULL, 10);
            det_split *sp = split_at(s, idx);

            if (sp == NULL)
            {
                fail("step %lld: no split %d", i, idx);
                return;
            }

            det_split_set_seed_role(s, sp, role, seed);
            cmp_bits("split setSeed state", i, vals[0], det_rng_state(&sp->d[role]));
            break;
        }
    }

    cmp_bits("seederState", i, tok[n - 3], det_seeder_state(s, role));
    cmp_bits("mathState", i, tok[n - 2], det_math_state(s, role));
    cmp_bits("splitState", i, tok[n - 1], det_split_state(s, role));
}

static int replay_steps(det_state *s, long long steps)
{
    char path[DET_PATH_MAX];

    data_path(path, sizeof path, "steps.txt.gz");
    gzFile g = gzopen(path, "rb");

    if (g == NULL)
    {
        fprintf(stderr, "cannot open %s\n", path);
        exit(2);
    }

    char line[DET_LINE_MAX];
    long long i = 0;

    while (gzgets(g, line, sizeof line) != NULL)
    {
        if (line[0] == '\n' || line[0] == 0) continue;
        step(s, line, i);
        ++i;
    }

    gzclose(g);

    if (i != steps) fail("steps: the manifest says %lld, the file has %lld", steps, i);
    return i;
}

/* StrictMath.log and StrictMath.sqrt, bit for bit, incluing the NaN shapes. */
static void check_math(void)
{
    long before = g_bad;
    char path[DET_PATH_MAX], a[32], b[32], c[32];

    data_path(path, sizeof path, "math.txt");
    FILE *f = fopen(path, "r");

    if (f == NULL)
    {
        perror(path);
        exit(2);
    }

    long n = 0;

    while (fscanf(f, "%31s %31s %31s", a, b, c) == 3)
    {
        double x = from_bits(tok_bits(a)), want_log = from_bits(tok_bits(b)), want_sqrt = from_bits(tok_bits(c));
        double got_log = det_log(x), got_sqrt = det_sqrt(x);

        if (want_log == want_log && dbits(got_log) != dbits(want_log))
            fail("log(%016llx): want %016llx got %016llx", (unsigned long long)tok_bits(a),
                 (unsigned long long)dbits(want_log), (unsigned long long)dbits(got_log));
        if (want_log != want_log && got_log == got_log) fail("log(%016llx): want NaN", (unsigned long long)tok_bits(a));
        if (want_sqrt == want_sqrt && dbits(got_sqrt) != dbits(want_sqrt))
            fail("sqrt(%016llx): want %016llx got %016llx", (unsigned long long)tok_bits(a),
                 (unsigned long long)dbits(want_sqrt), (unsigned long long)dbits(got_sqrt));
        if (want_sqrt != want_sqrt && got_sqrt == got_sqrt) fail("sqrt(%016llx): want NaN", (unsigned long long)tok_bits(a));
        ++n;
    }

    fclose(f);
    printf("math     %s: %ld inputs\n", g_bad == before ? "PASS" : "FAIL", n);

    if (n == 0) fail("math.txt is empty");
}

/* Det.role()'s thread bindings, which the engine reproduces with det_set_role. */
static void check_dispatch(void)
{
    char path[DET_PATH_MAX], line[DET_LINE_MAX];

    data_path(path, sizeof path, "dispatch.txt");
    FILE *f = fopen(path, "r");

    if (f == NULL)
    {
        perror(path);
        exit(2);
    }

    int n = 0;

    while (fgets(line, sizeof line, f))
    {
        int role;
        char what[64];

        if (sscanf(line, "%d %63s", &role, what) != 2) continue;

        if (n >= NDISPATCH)
        {
            fail("dispatch: more lines than %d", NDISPATCH);
            break;
        }

        if (role != DISPATCH_ROLES[n]) fail("dispatch %s: want role %d got %d", DISPATCH_NAMES[n], DISPATCH_ROLES[n], role);

        if (strcmp(what, DISPATCH_NAMES[n]) != 0) fail("dispatch %d: want %s got %s", n, DISPATCH_NAMES[n], what);
        ++n;
    }

    fclose(f);

    if (n != NDISPATCH) fail("dispatch: %d lines, expected %d", n, NDISPATCH);
}

/* The *_cur entry points the engine calls have to read det_state.role: run them
 * beside the explicit-role ones on two copies of one reset state and compare
 * every draw. */
static void check_current_role(void)
{
    det_state a, b;

    det_init(&a);
    det_init(&b);
    det_reset(&a, 99);
    det_reset(&b, 99);

    for (int role = 0; role < DET_ROLES; ++role)
    {
        det_set_role(&a, role);
        det_set_role(&b, role);

        if (det_role(&a) != role) fail("det_role role %d", role);

        det_rng na = det_new_random(&a), nb = det_new_random_role(&b, role);

        if (det_rng_state(&na) != det_rng_state(&nb)) fail("newRandom cur role %d", role);
        if (det_rng_int(&na) != det_rng_int(&nb)) fail("newRandom draw cur role %d", role);
        if (det_rng_gaussian(&na) != det_rng_gaussian(&nb)) fail("newRandom gaussian cur role %d", role);

        if (det_math_random(&a) != det_math_random_role(&b, role)) fail("mathRandom cur role %d", role);

        int64_t am, al, bm, bl;

        det_uuid(&a, &am, &al);
        det_uuid_role(&b, role, &bm, &bl);
        if (am != bm || al != bl) fail("uuid cur role %d", role);
        if (det_next_entity_id(&a) != det_next_entity_id_role(&b, role)) fail("nextEntityId cur role %d", role);

        det_split *pa = det_split_random(&a, "detprobe/cur:split");
        det_split *pb = det_split_random(&b, "detprobe/cur:split");

        if (pa == NULL || pb == NULL)
        {
            fail("cur role %d: det_split_random", role);
            continue;
        }

        if (det_split_next(&a, pa, 17) != det_split_next_role(&b, pb, role, 17)) fail("split next cur role %d", role);
        if (det_split_long(&a, pa) != det_split_long_role(&b, pb, role)) fail("split nextLong cur role %d", role);
        if (det_split_gaussian(&a, pa) != det_split_gaussian_role(&b, pb, role)) fail("split gaussian cur role %d", role);
        if (det_split_state(&a, role) != det_split_state(&b, role)) fail("splitState cur role %d", role);

        det_split_set_seed(&a, pa, role * 7 + 1);
        det_split_set_seed_role(&b, pb, role, role * 7 + 1);
        if (det_split_state(&a, role) != det_split_state(&b, role)) fail("split setSeed cur role %d", role);
        if (det_split_float(&a, pa) != det_split_float_role(&b, pb, role)) fail("split nextFloat cur role %d", role);
        if (det_split_int_n(&a, pa, 1000) != det_split_int_n_role(&b, pb, role, 1000)) fail("split nextInt(n) cur role %d", role);
        if (det_split_bool(&a, pa) != det_split_bool_role(&b, pb, role)) fail("split nextBoolean cur role %d", role);
        if (det_split_double(&a, pa) != det_split_double_role(&b, pb, role)) fail("split nextDouble cur role %d", role);
        if (det_split_int(&a, pa) != det_split_int_role(&b, pb, role)) fail("split nextInt cur role %d", role);
    }

    det_free(&a);
    det_free(&b);
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: test_det DIR\n");
        return 2;
    }

    g_dir = argv[1];

    char mpath[DET_PATH_MAX];

    data_path(mpath, sizeof mpath, "manifest.json");
    char *manifest = read_file(mpath, NULL);
    long long seed = manifest_int(manifest, "seed");
    long long steps = manifest_int(manifest, "steps");

    struct ref start;

    read_ref(&start, "start.txt");

    det_state s;

    det_init(&s);
    for (int i = 0; i < start.nsplits; ++i)
        if (det_split_random(&s, start.splits[i].name) == NULL) fail("start: cannot register %s", start.splits[i].name);
    det_reset(&s, start.world_seed);

    long before = g_bad;

    cmp_ref("start", &start, &s, 1);
    {
        /* The same state again, loaded from the recorded 48-bit stream states
         * instead of reset: the snapshot path. */
        det_state t;

        det_init(&t);
        det_load(&t, start.world_seed, start.seeder, start.math, start.next_id);
        for (int i = 0; i < start.nsplits; ++i)
            det_split_add(&t, start.splits[i].name, start.splits[i].state, start.splits[i].used);
        cmp_ref("load", &start, &t, 1);
        det_free(&t);
    }

    /* det_split_find, the lookup a vanilla call site uses by name, has to find
     * the stream the list registered. */
    for (int i = 0; i < start.nsplits; ++i)
    {
        det_split *sp = det_split_find(&s, start.splits[i].name);

        if (sp == NULL || det_split_index(&s, sp) != i) fail("find: %s is not split %d", start.splits[i].name, i);
    }

    if (det_split_find(&s, "./no/such/Split.java:rand") != NULL) fail("find: an unregistered name found a split");

    printf("start    %s\n", g_bad == before ? "PASS" : "FAIL");

    before = g_bad;
    long long replayed = replay_steps(&s, steps);
    printf("steps    %s: %lld steps\n", g_bad == before ? "PASS" : "FAIL", replayed);

    struct ref end;

    read_ref(&end, "end.txt");
    before = g_bad;
    cmp_ref("end", &end, &s, 1);
    printf("end      %s\n", g_bad == before ? "PASS" : "FAIL");

    char files[256], *name;
    manifest_str(manifest, "resetFiles", files, sizeof files);

    before = g_bad;
    int nresets = 0;

    for (name = strtok(files, " "); name; name = strtok(NULL, " "))
    {
        struct ref r;

        read_ref(&r, name);
        det_reset(&s, r.world_seed);
        cmp_ref(name, &r, &s, 0);
        ++nresets;
    }

    printf("reset    %s: %d seeds\n", g_bad == before ? "PASS" : "FAIL", nresets);

    before = g_bad;
    check_dispatch();
    printf("dispatch %s\n", g_bad == before ? "PASS" : "FAIL");

    check_math();

    before = g_bad;
    check_current_role();
    printf("cur      %s\n", g_bad == before ? "PASS" : "FAIL");

    det_free(&s);
    free(manifest);

    printf("%s det seed %lld (%s)\n", g_bad ? "FAIL" : "PASS", seed, g_dir);
    return g_bad != 0;
}