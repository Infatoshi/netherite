/* Gate: native explosions against the oracle's ExplosionProbe dump
 * (oracle/harness/netherite/oracle/ExplosionProbe.java). The probe loads a raw
 * region, scatters blocks of every resistance class (shapes.bin), then runs
 * 3000 explosions from Random(opseed): per case the chooser draws (position,
 * size, flaming, smoking, the case's item and XP entities and their motion
 * overrides), world.rand is seeded to opseed + case_step * case, and the body
 * of WorldServer.newExplosion runs. The record per case is the affected
 * positions in the HashSet order, every block write, the drops the explosion
 * spawned, the case entities' state after, the region hash and the RNG states
 * (the world Random and the explosion's private rng after, Det's OTHER math
 * and seeder streams after).
 *
 * The replay rebuilds the same scene from the dump alone: the shapes, the Det
 * state from start.txt, then per case the chooser draws (replayed from opseed
 * and checked against the record), the entities, one expl_run, and every
 * recorded field compared. A failure names the case and the field.
 *
 * Stats (blocks destroyed, drops, damaged entities, fires) are printed so the
 * report can carry them.
 */
#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "../engine/blocks.h"
#include "../engine/det.h"
#include "../engine/entity.h"
#include "../engine/explosion.h"
#include "../engine/item_entity.h"
#include "../engine/trace.h"
#include "../engine/world.h"

static int failures = 0;

/* stats, printed once at the end */
static long long stat_destroyed, stat_fires, stat_drops, stat_damaged, stat_affected;

/* ------------------------------------------------------------- byte reads */
static void *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");

    if (!f)
    {
        perror(path);
        exit(2);
    }

    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    unsigned char *buf = malloc((size_t)n + 1);

    if (fread(buf, 1, (size_t)n, f) != (size_t)n)
    {
        fprintf(stderr, "short read in %s\n", path);
        exit(2);
    }

    fclose(f);
    buf[n] = 0;
    *len = (size_t)n;
    return buf;
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

static uint32_t float_bits(float f)
{
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}

/* a raw-bits long recorded by Double.doubleToRawLongBits, widened back */
static double bit_double(long long bits)
{
    uint64_t b = (uint64_t)bits;
    double d;
    memcpy(&d, &b, sizeof d);
    return d;
}

/* ------------------------------------------------------------ mini JSON */
/* The case lines are Gson output of known shape: numbers are decimal longs,
 * the only string (writes) carries no escapes. Enough of a tree to walk. */
enum { JV_NUM, JV_STR, JV_ARR, JV_OBJ };

typedef struct jv jv;
struct jv {
    int type;
    long long num;
    char *str;
    jv **kids;
    char **keys;
    int n;
};

static void jv_free(jv *v)
{
    if (!v) return;

    for (int i = 0; i < v->n; ++i)
    {
        if (v->keys) free(v->keys[i]);
        jv_free(v->kids[i]);
    }

    free(v->keys);
    free(v->kids);
    free(v->str);
    free(v);
}

typedef struct {
    const char *p;
} jscan;

static jv *jv_value(jscan *s);

static void jv_ws(jscan *s)
{
    while (*s->p == ' ' || *s->p == '\t' || *s->p == '\n' || *s->p == '\r') ++s->p;
}

static jv *jv_new(int type)
{
    jv *v = calloc(1, sizeof *v);
    v->type = type;
    return v;
}

static void jv_push(jv *v, char *key, jv *kid)
{
    v->kids = realloc(v->kids, (size_t)(v->n + 1) * sizeof *v->kids);
    v->kids[v->n] = kid;

    if (key)
    {
        v->keys = realloc(v->keys, (size_t)(v->n + 1) * sizeof *v->keys);
        v->keys[v->n] = key;
    }

    ++v->n;
}

static jv *jv_value(jscan *s)
{
    jv_ws(s);

    if (*s->p == '"')
    {
        ++s->p;
        const char *start = s->p;

        while (*s->p && *s->p != '"') ++s->p;

        jv *v = jv_new(JV_STR);
        v->str = strndup(start, (size_t)(s->p - start));
        if (*s->p) ++s->p;
        return v;
    }

    if (*s->p == '[')
    {
        ++s->p;
        jv *v = jv_new(JV_ARR);
        jv_ws(s);

        if (*s->p == ']')
        {
            ++s->p;
            return v;
        }

        for (;;)
        {
            jv_push(v, NULL, jv_value(s));
            jv_ws(s);

            if (*s->p == ',')
            {
                ++s->p;
                continue;
            }

            if (*s->p == ']')
            {
                ++s->p;
                return v;
            }

            fprintf(stderr, "json: bad array\n");
            exit(2);
        }
    }

    if (*s->p == '{')
    {
        ++s->p;
        jv *v = jv_new(JV_OBJ);
        jv_ws(s);

        if (*s->p == '}')
        {
            ++s->p;
            return v;
        }

        for (;;)
        {
            jv_ws(s);

            if (*s->p != '"')
            {
                fprintf(stderr, "json: bad object key\n");
                exit(2);
            }

            jv *k = jv_value(s);
            jv_ws(s);

            if (*s->p != ':')
            {
                fprintf(stderr, "json: bad object colon\n");
                exit(2);
            }

            ++s->p;
            jv_push(v, k->str, jv_value(s));
            free(k);
            jv_ws(s);

            if (*s->p == ',')
            {
                ++s->p;
                continue;
            }

            if (*s->p == '}')
            {
                ++s->p;
                return v;
            }

            fprintf(stderr, "json: bad object\n");
            exit(2);
        }
    }

    /* a decimal long */
    jv *v = jv_new(JV_NUM);
    v->num = strtoll(s->p, (char **)&s->p, 10);
    return v;
}

static jv *jv_parse(const char *line)
{
    jscan s = { line };
    return jv_value(&s);
}

static jv *jv_get(const jv *obj, const char *key)
{
    for (int i = 0; i < obj->n; ++i)
    {
        if (strcmp(obj->keys[i], key) == 0) return obj->kids[i];
    }

    return NULL;
}

static long long jv_num(const jv *obj, const char *key)
{
    jv *v = jv_get(obj, key);

    if (!v || v->type != JV_NUM)
    {
        fprintf(stderr, "case line: no number %s\n", key);
        exit(2);
    }

    return v->num;
}

/* ------------------------------------------------------ the write record */
/* The Rows listener: every setBlock and metadata write the case makes, in
 * order, five ints per write (x y z id meta, a meta write carrying id -1). */
static int *writes;
static int writes_n, writes_cap;

static void wr_reset(void)
{
    writes_n = 0;
}

static void on_block(void *ctx, int x, int y, int z, int id, int meta)
{
    (void)ctx;

    /* a metadata write, which the oracle's Rows records as id -1 (world.c
     * hands the listener 0xffff for it) */
    if (id == 0xffff) id = -1;

    if (writes_n + 5 > writes_cap)
    {
        writes_cap = writes_cap ? writes_cap * 2 : 4096;
        writes = realloc(writes, (size_t)writes_cap * sizeof *writes);
    }

    writes[writes_n++] = x;
    writes[writes_n++] = y;
    writes[writes_n++] = z;
    writes[writes_n++] = id;
    writes[writes_n++] = meta;
}

/* ------------------------------------------------------------ helpers */
static void cmp_case_dbl(int ci, const char *what, const char *field, double want, double got)
{
    if (want == want && want == got) return;

    printf("FAIL case %d: %s %s: the oracle recorded %a, the native replay has %a\n",
           ci, what, field, want, got);
    ++failures;
}

static void cmp_case_int(int ci, const char *what, const char *field, long long want, long long got)
{
    if (want == got) return;

    printf("FAIL case %d: %s %s: the oracle recorded %lld, the native replay has %lld\n",
           ci, what, field, want, got);
    ++failures;
}

/* The region hash, the probe's hashRegion: FNV-1a over the ids for every cell
 * y in ylo..yhi, x-major then z, then the metas in the same order. */
static long long region_hash(struct world *w, int bx0, int bz0, int width, int ylo, int yhi)
{
    int64_t h = (int64_t)0xcbf29ce484222325ULL;
    const int64_t prime = (int64_t)0x100000001b3ULL;

    for (int x = 0; x < width; ++x)
    {
        for (int z = 0; z < width; ++z)
        {
            for (int y = ylo; y <= yhi; ++y)
            {
                h = (h ^ (int64_t)(int32_t)(world_get_block(w, bx0 + x, y, bz0 + z) & 4095)) * prime;
            }
        }
    }

    for (int x = 0; x < width; ++x)
    {
        for (int z = 0; z < width; ++z)
        {
            for (int y = ylo; y <= yhi; ++y)
            {
                h = (h ^ (int64_t)(int32_t)world_get_meta(w, bx0 + x, y, bz0 + z)) * prime;
            }
        }
    }

    return (long long)h;
}

/* The probe's takeOut: dead, out of the chunk's y-section list, out of the
 * loaded list, so the next case starts from the scatter only. */
static void ie_take_out(ie_world *iew, ie_ent *en)
{
    if (en->added_to_chunk)
    {
        for (int i = 0; i < iew->nchunks; ++i)
        {
            ie_chunk *c = &iew->chunks[i];

            if (!c->used || c->cx != en->chunk_x || c->cz != en->chunk_z) continue;

            for (int s = 0; s < IE_SECTIONS; ++s) sec_remove_first(&c->sec[s], ie_ent_index(en));
        }
    }

    en->is_dead = 1;

    for (int i = 0; i < iew->n; ++i)
    {
        if (ie_ent_at(iew->slot[i]) == en)
        {
            memmove(&iew->slot[i], &iew->slot[i + 1], (size_t)(iew->n - i - 1) * sizeof *iew->slot);
            --iew->n;
            return;
        }
    }
}

/* The Det snapshot format's counters, per-role states and split streams. */
struct splitref {
    char name[DET_NAME_MAX];
    uint64_t state[DET_ROLES];
    uint8_t used[DET_ROLES];
};

struct ref {
    int64_t reset_seed, world_seed;
    int32_t next_id[DET_ROLES];
    uint64_t seeder[DET_ROLES], math[DET_ROLES], split[DET_ROLES];
    struct splitref splits[64];
    int nsplits;
};

static void read_ref(struct ref *r, const char *dir, const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "r");

    if (!f)
    {
        perror(path);
        exit(2);
    }

    memset(r, 0, sizeof *r);
    char line[2048];

    while (fgets(line, sizeof line, f))
    {
        long long v;
        int role;
        unsigned long long a, b, c;

        if (sscanf(line, "resetSeed %lld", &v) == 1) { r->reset_seed = v; continue; }
        if (sscanf(line, "worldSeed %lld", &v) == 1) { r->world_seed = v; continue; }
        if (sscanf(line, "nextId %d %d %d %d", &r->next_id[0], &r->next_id[1], &r->next_id[2], &r->next_id[3]) == 4) continue;
        if (sscanf(line, "digest %d %llx %llx %llx", &role, &a, &b, &c) == 4)
        {
            r->seeder[role] = a;
            r->math[role] = b;
            r->split[role] = c;
            continue;
        }

        char nm[DET_NAME_MAX];
        unsigned long long s0, s1, s2, s3;
        int u0, u1, u2, u3;

        if (sscanf(line, "split %127s %llx %llx %llx %llx %d %d %d %d", nm, &s0, &s1, &s2, &s3, &u0, &u1, &u2, &u3) == 9)
        {
            struct splitref *sp = &r->splits[r->nsplits++];
            snprintf(sp->name, sizeof sp->name, "%s", nm);
            sp->state[0] = s0; sp->state[1] = s1; sp->state[2] = s2; sp->state[3] = s3;
            sp->used[0] = (uint8_t)u0; sp->used[1] = (uint8_t)u1;
            sp->used[2] = (uint8_t)u2; sp->used[3] = (uint8_t)u3;
        }
    }

    fclose(f);
}

/* The probe's fixed tables (documented in the manifest). */
static const int ITEM_IDS[] = {264, 265, 266, 337, 262, 331};
#define N_ITEM_IDS 6

/* One case's entities, kept for the after pass and the cleanup. */
struct case_ent {
    ie_ent *en;
};

int main(int argc, char **argv)
{
    /* --cases N: only the first N cases, without the end-of-run compare (make smoke) */
    int limit = -1;
    if (argc > 3 && !strcmp(argv[1], "--cases"))
    {
        limit = atoi(argv[2]);
        argv += 2;
        argc -= 2;
    }

    if (argc != 2 && argc != 3)
    {
        fprintf(stderr, "usage: test_explosions [--cases N] EXPLOSIONS_DIR [TRACE_PATH]\n");
        return 2;
    }

    const char *dir = argv[1];
    const char *trace_path = argc == 3 ? argv[2] : NULL;
    char path[1200];

    snprintf(path, sizeof path, "%s/manifest.json", dir);
    size_t mlen;
    char *manifest;
    {
        FILE *f = fopen(path, "rb");

        if (!f)
        {
            printf("skip %s: no manifest\n", dir);
            return 0;
        }

        fseek(f, 0, SEEK_END);
        mlen = (size_t)ftell(f);
        rewind(f);
        manifest = malloc(mlen + 1);

        if (fread(manifest, 1, mlen, f) != mlen) { fprintf(stderr, "short read\n"); return 2; }

        manifest[mlen] = 0;
        fclose(f);
    }

    if (!strstr(manifest, "\"area\":\"explosions\""))
    {
        printf("skip %s: not an explosions dump\n", dir);
        return 0;
    }

    int64_t seed = manifest_int(manifest, "seed");
    int cx = (int)manifest_int(manifest, "cx");
    int cz = (int)manifest_int(manifest, "cz");
    int radius = (int)manifest_int(manifest, "radius");
    int cases = (int)manifest_int(manifest, "cases");
    int cut = limit >= 0 && limit < cases;
    if (cut) cases = limit;
    int ylo = (int)manifest_int(manifest, "ylo");
    int yhi = (int)manifest_int(manifest, "yhi");
    int nshapes = (int)manifest_int(manifest, "nshapes");
    long long opseed = manifest_int(manifest, "opseed");

    /* the world, the loaded chunks in the oracle's cx-major order */
    const char *lp = strstr(manifest, "\"loaded\":[");
    if (!lp) { fprintf(stderr, "%s: no loaded list\n", dir); return 2; }
    lp += strlen("\"loaded\":[");

    struct world w;
    world_init(&w, seed);

    while (*lp == '[')
    {
        int lx = (int)strtol(lp + 1, (char **)&lp, 10);
        int lz = (int)strtol(lp + 1, (char **)&lp, 10);
        world_load_chunk(&w, lx, lz);

        if (*lp == ']') ++lp;
        if (*lp == ',') ++lp;
    }

    int width = (2 * radius + 1) * 16;
    int bx0 = (cx - radius) * 16, bz0 = (cz - radius) * 16;

    /* the shapes, placed the way the probe placed them (the scatter Random is
     * a stream of its own; the record carries every placement, torch skips
     * included) */
    snprintf(path, sizeof path, "%s/shapes.bin", dir);
    size_t slen;
    unsigned char *shapes = read_file(path, &slen);

    if (slen % 16 != 0)
    {
        fprintf(stderr, "%s: shapes.bin is %zu bytes\n", dir, slen);
        return 2;
    }

    if (slen / 16 != (size_t)nshapes)
    {
        printf("FAIL %s: manifest says %d shapes, shapes.bin has %zu\n", dir, nshapes, slen / 16);
        ++failures;
    }

    for (size_t i = 0; i < slen / 16; ++i)
    {
        const unsigned char *s = shapes + i * 16;
        int x = (int)((uint32_t)s[0] | (uint32_t)s[1] << 8 | (uint32_t)s[2] << 16 | (uint32_t)s[3] << 24);
        int y = (int)((uint32_t)s[4] | (uint32_t)s[5] << 8 | (uint32_t)s[6] << 16 | (uint32_t)s[7] << 24);
        int z = (int)((uint32_t)s[8] | (uint32_t)s[9] << 8 | (uint32_t)s[10] << 16 | (uint32_t)s[11] << 24);
        int id = s[12] | s[13] << 8;
        int meta = s[14];
        world_set_block(&w, x, y, z, id, meta, 2);
    }

    free(shapes);

    /* the Det state at the start of the case loop */
    struct ref start;
    read_ref(&start, dir, "start.txt");

    det_state det;
    det_init(&det);
    det_load(&det, start.world_seed, start.seeder, start.math, start.next_id);

    for (int i = 0; i < start.nsplits; ++i)
    {
        det_split_add(&det, start.splits[i].name, start.splits[i].state, start.splits[i].used);
    }

    ie_world iew;
    ie_init(&iew, &w, &det);

    w.on_block = on_block;

    /* the case loop */
    trace_open(trace_path);

    snprintf(path, sizeof path, "%s/cases.txt.gz", dir);
    gzFile cf = gzopen(path, "rb");
    if (!cf)
    {
        perror(path);
        exit(2);
    }

    jrand chooser;
    jr_seed(&chooser, opseed);

    jrand world_rand;
    struct case_ent case_ents[3];

    char *line = NULL;
    size_t line_cap = 0;

    for (int ci = 0; ci < cases; ++ci)
    {
        /* one line, whatever its length */
        size_t len = 0;
        int c;

        while ((c = gzgetc(cf)) != EOF && c != '\n')
        {
            if (len + 2 > line_cap)
            {
                line_cap = line_cap ? line_cap * 2 : (1 << 16);
                line = realloc(line, line_cap);
            }

            line[len++] = (char)c;
        }

        if (c == EOF && len == 0)
        {
            printf("FAIL %s: cases file ended at case %d\n", dir, ci);
            ++failures;
            break;
        }

        if (len + 1 > line_cap)
        {
            line_cap += 2;
            line = realloc(line, line_cap);
        }

        line[len] = 0;
        jv *j = jv_parse(line);

        /* the chooser draws, replayed and checked against the record */
        int px = bx0 + 8 + jr_int_n(&chooser, width - 16);
        int pz = bz0 + 8 + jr_int_n(&chooser, width - 16);
        int py = ylo + 2 + jr_int_n(&chooser, yhi - ylo - 4);
        static const float SIZES[] = {0.5F, 1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 8.0F};
        float size = SIZES[jr_int_n(&chooser, 8)];
        int flaming = jr_int_n(&chooser, 4) == 0;
        int smoking = jr_int_n(&chooser, 8) != 0;
        int nents = jr_int_n(&chooser, 4);

        int rx = (int)jv_num(j, "x"), ry = (int)jv_num(j, "y"), rz = (int)jv_num(j, "z");

        if (px != rx || py != ry || pz != rz)
        {
            printf("FAIL case %d: chooser position: the draw stream gives (%d,%d,%d), the record has (%d,%d,%d)\n",
                   ci, px, py, pz, rx, ry, rz);
            ++failures;
            jv_free(j);
            break;
        }

        if ((long long)float_bits(size) != jv_num(j, "size"))
        {
            printf("FAIL case %d: chooser size: the draw stream gives raw %08x, the record has %lld\n",
                   ci, float_bits(size), jv_num(j, "size"));
            ++failures;
            jv_free(j);
            break;
        }

        if (flaming != (int)jv_num(j, "flaming") || smoking != (int)jv_num(j, "smoking") || nents != (int)jv_num(j, "nents"))
        {
            printf("FAIL case %d: chooser flags: the draw stream gives %d %d %d, the record has %lld %lld %lld\n",
                   ci, flaming, smoking, nents, jv_num(j, "flaming"), jv_num(j, "smoking"), jv_num(j, "nents"));
            ++failures;
            jv_free(j);
            break;
        }

        jv *meta = jv_get(j, "entmeta");

        if (meta->n != nents)
        {
            printf("FAIL case %d: entmeta length %d vs nents %d\n", ci, meta->n, nents);
            ++failures;
        }

        /* the case's entities, spawned the way the probe spawned them */
        jr_seed(&world_rand, jv_num(j, "seed"));
        wr_reset();

        for (int i = 0; i < nents; ++i)
        {
            int orb = jr_int_n(&chooser, 3) == 2;
            int ex = px + jr_int_n(&chooser, 11) - 5;
            int ez = pz + jr_int_n(&chooser, 11) - 5;
            int ey = py + jr_int_n(&chooser, 7) - 3;
            int eitem = ITEM_IDS[jr_int_n(&chooser, N_ITEM_IDS)];
            int edamage = jr_int_n(&chooser, 16);
            int ecount = jr_int_n(&chooser, 4) + 1;
            int expv = jr_int_n(&chooser, 64) + 1;
            double emx = (jr_double(&chooser) - 0.5) * 0.2;
            double emy = jr_double(&chooser) * 0.1;
            double emz = (jr_double(&chooser) - 0.5) * 0.2;

            /* the record's entmeta row: kind, item, damage, count, xp */
            jv *row = meta->kids[i];

            if ((orb ? 2 : 1) != row->kids[0]->num
                || (!orb && (eitem != row->kids[1]->num || edamage != row->kids[2]->num || ecount != row->kids[3]->num))
                || (orb && expv != row->kids[4]->num))
            {
                printf("FAIL case %d: chooser entity %d: the draw stream gives kind %d item %d damage %d count %d xp %d,"
                       " the record has kind %lld item %lld damage %lld count %lld xp %lld\n",
                       ci, i, orb ? 2 : 1, eitem, edamage, ecount, expv,
                       row->kids[0]->num, row->kids[1]->num, row->kids[2]->num, row->kids[3]->num, row->kids[4]->num);
                ++failures;
            }

            ie_ent *en = orb
                ? ie_spawn_orb(&iew, (double)ex + 0.5, (double)ey, (double)ez + 0.5, expv)
                : ie_spawn_item(&iew, (double)ex + 0.5, (double)ey, (double)ez + 0.5, eitem, edamage, ecount);

            if (!en)
            {
                printf("FAIL %s case %d: entity pool full\n", dir, ci);
                return 2;
            }

            en->e.motion_x = emx;
            en->e.motion_y = emy;
            en->e.motion_z = emz;
            ie_added_to_world(&iew, en);
            case_ents[i].en = en;
        }

        int n_spawned_before = iew.n;

        /* ------------------------------------------------------- the run */
        struct expl_result res;
        expl_run(&w, &det, DET_OTHER, &world_rand, &iew, NULL,
                 (double)rx, (double)ry, (double)rz, size, flaming, smoking, &res);

        /* --------------------------------------------------- the compares */
        cmp_case_int(ci, "rng", "rng_after", (long long)res.rng_state, jv_num(j, "rng_after"));
        cmp_case_int(ci, "rng", "world_mid", jv_num(j, "world_mid"), (long long)res.wr_mid);
        cmp_case_int(ci, "rng", "world_after", jv_num(j, "world_after"), (long long)world_rand.seed);
        cmp_case_int(ci, "rng", "det_math", jv_num(j, "det_math"), (long long)det.math[DET_OTHER].r.seed);
        cmp_case_int(ci, "rng", "det_seed", jv_num(j, "det_seed"), (long long)det.seeder[DET_OTHER].r.seed);

        /* the affected positions, in the HashSet order doExplosionB walks */
        jv *affected = jv_get(j, "affected");
        stat_affected += affected->n;
        cmp_case_int(ci, "affected", "count", affected->n, res.n_affected);

        int ncmp = affected->n < res.n_affected ? affected->n : res.n_affected;

        for (int i = 0; i < ncmp; ++i)
        {
            jv *a = affected->kids[i];
            long long wx = a->kids[0]->num, wy = a->kids[1]->num, wz = a->kids[2]->num;

            if (wx != res.affected[i * 3] || wy != res.affected[i * 3 + 1] || wz != res.affected[i * 3 + 2])
            {
                printf("FAIL case %d: affected %d: the oracle recorded (%lld,%lld,%lld), the native replay has (%d,%d,%d)\n",
                       ci, i, wx, wy, wz, res.affected[i * 3], res.affected[i * 3 + 1], res.affected[i * 3 + 2]);
                ++failures;
                break;
            }
        }

        /* the write stream */
        jv *writesv = jv_get(j, "writes");
        long long want_writesn = jv_num(j, "writesn");
        cmp_case_int(ci, "writes", "count", want_writesn, writes_n / 5);

        size_t got_cap = (size_t)writes_n * 14 + 2;
        char *got = malloc(got_cap);
        got[0] = 0;
        size_t off = 0;

        for (int i = 0; i + 4 < writes_n; i += 5)
        {
            off += (size_t)snprintf(got + off, got_cap - off, "%s%d %d %d %d %d", off ? " " : "",
                                    writes[i], writes[i + 1], writes[i + 2], writes[i + 3], writes[i + 4]);
        }

        if (strcmp(got, writesv->str ? writesv->str : "") != 0)
        {
            /* the first differing write, named */
            int nwant = (int)want_writesn;
            int *want = malloc((size_t)(nwant > 0 ? nwant : 1) * 5 * sizeof *want);
            const char *pp = writesv->str ? writesv->str : "";

            for (int i = 0; i < nwant; ++i)
            {
                for (int k = 0; k < 5; ++k) want[i * 5 + k] = (int)strtol(pp, (char **)&pp, 10);
            }

            int first = -1;

            for (int i = 0; i < nwant * 5 && i < writes_n; ++i)
            {
                if (want[i] != writes[i]) { first = i; break; }
            }

            if (first < 0) first = nwant * 5 <= writes_n ? nwant * 5 : writes_n;

            printf("FAIL case %d: writes: first difference at write %d token %d: the oracle recorded %d,"
                   " the native replay has %d\n",
                   ci, first / 5, first % 5, first / 5 < nwant ? want[first] : -999999,
                   first < writes_n ? writes[first] : -999999);
            ++failures;
            free(want);
        }

        free(got);

        /* the destroyed and fire lists, derived from the write stream */
        jv *destroyed = jv_get(j, "destroyed");
        jv *fires = jv_get(j, "fires");
        int ndestroyed = 0, nfires = 0;

        for (int i = 0; i + 4 < writes_n; i += 5)
        {
            if (writes[i + 3] == 0) ++ndestroyed;
            else if (writes[i + 3] == 51) ++nfires;
        }

        cmp_case_int(ci, "destroyed", "count", destroyed->n, ndestroyed);
        cmp_case_int(ci, "fires", "count", fires->n, nfires);
        stat_destroyed += ndestroyed;
        stat_fires += nfires;

        for (int i = 0; i < destroyed->n && i < ndestroyed; ++i)
        {
            jv *a = destroyed->kids[i];
            int wx = (int)a->kids[0]->num, wy = (int)a->kids[1]->num, wz = (int)a->kids[2]->num;
            int goti = -1;

            for (int k = 0; k + 4 < writes_n; k += 5)
            {
                if (writes[k + 3] == 0 && writes[k] == wx && writes[k + 1] == wy && writes[k + 2] == wz) { goti = k / 5; break; }
            }

            if (goti < 0)
            {
                printf("FAIL case %d: destroyed %d: the oracle destroyed (%d,%d,%d), the native write stream never did\n",
                       ci, i, wx, wy, wz);
                ++failures;
                break;
            }
        }

        for (int i = 0; i < fires->n && i < nfires; ++i)
        {
            jv *a = fires->kids[i];
            int wx = (int)a->kids[0]->num, wy = (int)a->kids[1]->num, wz = (int)a->kids[2]->num;
            int goti = -1;

            for (int k = 0; k + 4 < writes_n; k += 5)
            {
                if (writes[k + 3] == 51 && writes[k] == wx && writes[k + 1] == wy && writes[k + 2] == wz) { goti = k / 5; break; }
            }

            if (goti < 0)
            {
                printf("FAIL case %d: fire %d: the oracle placed fire at (%d,%d,%d), the native write stream never did\n",
                       ci, i, wx, wy, wz);
                ++failures;
                break;
            }
        }

        /* the entities the explosion spawned, in spawn order */
        jv *spawned = jv_get(j, "spawned");
        int nspawned = iew.n - n_spawned_before;
        cmp_case_int(ci, "spawned", "count", spawned->n, nspawned);
        stat_drops += nspawned;

        for (int i = 0; i < spawned->n && i < nspawned; ++i)
        {
            jv *sv = spawned->kids[i];
            ie_ent *en = ie_ent_at(iew.slot[n_spawned_before + i]);
            char what[48];
            snprintf(what, sizeof what, "spawned %d", i);

            /* kind, id, then the kind's stack fields, then the doubles. Once
             * the two sides disagree on the kind the field layouts diverge,
             * so compare the kind and the id only. */
            int okind = (int)sv->kids[0]->num;

            if (okind != en->kind)
            {
                cmp_case_int(ci, what, "kind", okind, en->kind);
                cmp_case_int(ci, what, "entity_id", sv->kids[1]->num, en->entity_id);
                continue;
            }

            cmp_case_int(ci, what, "kind", okind, en->kind);
            cmp_case_int(ci, what, "entity_id", sv->kids[1]->num, en->entity_id);

            if (okind == IE_ITEM)
            {
                /* item, damage, count, delay, hover, x, y, z, mx, my, mz */
                cmp_case_int(ci, what, "item", sv->kids[2]->num, en->stack_item);
                cmp_case_int(ci, what, "damage", sv->kids[3]->num, en->stack_damage);
                cmp_case_int(ci, what, "count", sv->kids[4]->num, en->stack_count);
                cmp_case_int(ci, what, "delay", sv->kids[5]->num, en->delay);
                cmp_case_int(ci, what, "hover", sv->kids[6]->num, (long long)(int32_t)float_bits(en->hover_start));

                cmp_case_dbl(ci, what, "x", bit_double(sv->kids[7]->num), en->e.pos_x);
                cmp_case_dbl(ci, what, "y", bit_double(sv->kids[8]->num), en->e.pos_y);
                cmp_case_dbl(ci, what, "z", bit_double(sv->kids[9]->num), en->e.pos_z);
                cmp_case_dbl(ci, what, "mx", bit_double(sv->kids[10]->num), en->e.motion_x);
                cmp_case_dbl(ci, what, "my", bit_double(sv->kids[11]->num), en->e.motion_y);
                cmp_case_dbl(ci, what, "mz", bit_double(sv->kids[12]->num), en->e.motion_z);
            }
            else
            {
                /* xp, x, y, z, mx, my, mz */
                cmp_case_int(ci, what, "xp", sv->kids[2]->num, en->xp_value);

                cmp_case_dbl(ci, what, "x", bit_double(sv->kids[3]->num), en->e.pos_x);
                cmp_case_dbl(ci, what, "y", bit_double(sv->kids[4]->num), en->e.pos_y);
                cmp_case_dbl(ci, what, "z", bit_double(sv->kids[5]->num), en->e.pos_z);
                cmp_case_dbl(ci, what, "mx", bit_double(sv->kids[6]->num), en->e.motion_x);
                cmp_case_dbl(ci, what, "my", bit_double(sv->kids[7]->num), en->e.motion_y);
                cmp_case_dbl(ci, what, "mz", bit_double(sv->kids[8]->num), en->e.motion_z);
            }
        }

        /* the case entities' state after: id, health, dead, age, then doubles */
        jv *after = jv_get(j, "after");

        for (int i = 0; i < nents && i < after->n; ++i)
        {
            jv *av = after->kids[i];
            ie_ent *en = case_ents[i].en;
            char what[48];
            snprintf(what, sizeof what, "after %d", i);

            cmp_case_int(ci, what, "health", av->kids[1]->num, en->health);
            cmp_case_int(ci, what, "dead", av->kids[2]->num, en->is_dead);
            cmp_case_int(ci, what, "age", av->kids[3]->num, en->age);

            if (en->health < 5 || en->is_dead) ++stat_damaged;

            cmp_case_dbl(ci, what, "x", bit_double(av->kids[4]->num), en->e.pos_x);
            cmp_case_dbl(ci, what, "y", bit_double(av->kids[5]->num), en->e.pos_y);
            cmp_case_dbl(ci, what, "z", bit_double(av->kids[6]->num), en->e.pos_z);
            cmp_case_dbl(ci, what, "mx", bit_double(av->kids[7]->num), en->e.motion_x);
            cmp_case_dbl(ci, what, "my", bit_double(av->kids[8]->num), en->e.motion_y);
            cmp_case_dbl(ci, what, "mz", bit_double(av->kids[9]->num), en->e.motion_z);
        }

        /* the region hash */
        cmp_case_int(ci, "hash", "hash", jv_num(j, "hash"),
                     region_hash(&w, bx0, bz0, width, ylo, yhi));

        /* everything the case added, out of the world again: the explosion's
         * spawns first (they sit above n_spawned_before, an index the case
         * entities' own removal would shift), then the case's entities */
        while (iew.n > n_spawned_before) ie_take_out(&iew, ie_ent_at(iew.slot[iew.n - 1]));

        for (int i = 0; i < nents; ++i) ie_take_out(&iew, case_ents[i].en);

        expl_result_free(&res);
        jv_free(j);
    }

    /* the Det state at the end, the snapshot format again */
    struct ref end;
    read_ref(&end, dir, "end.txt");

    for (int role = 0; role < DET_ROLES && !cut; ++role)
    {
        if ((uint64_t)det.seeder[role].r.seed != end.seeder[role]
            || (uint64_t)det.math[role].r.seed != end.math[role])
        {
            printf("FAIL %s: det digest role %d: the oracle recorded seeder %016llx math %016llx,"
                   " the native replay has seeder %016llx math %016llx\n",
                   dir, role, (unsigned long long)end.seeder[role], (unsigned long long)end.math[role],
                   (unsigned long long)(uint64_t)det.seeder[role].r.seed,
                   (unsigned long long)(uint64_t)det.math[role].r.seed);
            ++failures;
        }

        if (det.next_id[role] != end.next_id[role])
        {
            printf("FAIL %s: det nextId role %d: the oracle recorded %d, the native replay has %d\n",
                   dir, role, end.next_id[role], det.next_id[role]);
            ++failures;
        }
    }

    free(line);
    free(writes);
    free(manifest);
    trace_close();
    ie_free(&iew);
    world_free(&w);
    det_free(&det);
    gzclose(cf);

    printf("%s: %d cases%s, %lld affected, %lld destroyed, %lld drops, %lld entities damaged, %lld fires, %d failures\n",
           dir, cases, cut ? " (--cases)" : "", stat_affected, stat_destroyed, stat_drops, stat_damaged, stat_fires, failures);
    return failures != 0;
}
