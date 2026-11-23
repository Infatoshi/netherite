/* worldgen_built_check: the built stage (built.cu, worldgen_fetch_built) against
 * the C engine's constructor and sky map, chunk by chunk, for every chunk of
 * the given oracle chunk dumps (out/java/chunks/<name>: the manifest's
 * region, overworld seeds). Each batch is generated on the device; C makes
 * the chunk from the device's raw arrays as the provider does
 * (chunkgen_construct: chunk_construct, the biome copy, generate_skylight_map)
 * and genahead_chunk_built makes it from the built records (what the env
 * pool's device generation serves), and the two chunks are compared field by
 * field: the band mask, every band's ids, high ids, metadata, sky and block
 * light (their values, and whether each array is the band's own or a
 * constant), the random-tick counts, the height and precipitation maps,
 * heightMapMinimum, the biome array and the flags. The raw arrays themselves
 * are worldgen_check's (check.c): equal to provide_chunk's there.
 *
 *   worldgen_built_check [--batch N] [--negative] DUMP_DIR...
 * --negative perturbs the stage's sky fill (WORLDGEN_NEG_BUILT) and passes
 * only when the comparison fails at the sky light. */
#define _POSIX_C_SOURCE 200809L
#include "worldgen.h"
#include "../../engine/chunkgen.h"
#include "../../engine/genahead.h"
#include "../../engine/world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct req { int64_t seed; int cx, cz; };

static struct req *reqs;
static size_t nreq, capreq;

static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "out of memory (%zu bytes)\n", n); exit(2); }
    return p;
}

static void add_req(int64_t seed, int cx, int cz)
{
    if (nreq == capreq)
    {
        capreq = capreq ? capreq * 2 : 4096;
        reqs = realloc(reqs, sizeof *reqs * capreq);
        if (!reqs) { perror("realloc"); exit(2); }
    }
    reqs[nreq++] = (struct req){seed, cx, cz};
}

static long long num(const char *json, const char *key, int *found)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (found) *found = p != NULL;
    if (!p)
    {
        if (found) return 0;
        fprintf(stderr, "manifest: no %s\n", key);
        exit(2);
    }
    return strtoll(p + strlen(pat), 0, 10);
}

static size_t add_region(const char *dir)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/manifest.json", dir);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    static char m[1 << 20];
    size_t n = fread(m, 1, sizeof m - 1, f);
    m[n] = 0;
    fclose(f);
    int has_step;
    int64_t seed = num(m, "seed", NULL);
    int x0 = (int)num(m, "x0", NULL), z0 = (int)num(m, "z0", NULL), x1 = (int)num(m, "x1", NULL), z1 = (int)num(m, "z1", NULL);
    int step = (int)num(m, "step", &has_step);
    if (!has_step) step = 1;
    size_t before = nreq;
    for (int cx = x0; cx <= x1; cx += step)
        for (int cz = z0; cz <= z1; cz += step) add_req(seed, cx, cz);
    return nreq - before;
}

static int req_cmp(const void *a, const void *b)
{
    const struct req *x = a, *y = b;
    if (x->seed != y->seed) return x->seed < y->seed ? -1 : 1;
    if (x->cx != y->cx) return x->cx < y->cx ? -1 : 1;
    return (x->cz > y->cz) - (x->cz < y->cz);
}

/* what differs, by part: the first difference of each named */
enum { P_MASK, P_IDS, P_METAS, P_SKY, P_BLOCK, P_TICKING, P_HEIGHT, P_OTHER, P_N };
static const char *part_name[P_N] = {"mask", "ids", "metas", "sky", "blocklight", "ticking", "height", "other"};
static long bad[P_N], chunks_bad;
static char first[P_N][256];

static void diff(int p, const struct req *r, const char *what, int a, int b)
{
    if (bad[p]++ == 0)
        snprintf(first[p], sizeof first[p], "seed %lld chunk (%d,%d): %s C %d built %d", (long long)r->seed, r->cx, r->cz, what,
                 a, b);
}

/* 1 when the two chunks differ anywhere */
static int compare(const struct req *r, const struct chunk *a, const struct chunk *b)
{
    long before = 0;
    for (int p = 0; p < P_N; ++p) before += bad[p];

    if (a->mask != b->mask) diff(P_MASK, r, "band mask", a->mask, b->mask);
    for (int s = 0; s < 16; ++s)
    {
        const struct chunk_sec *x = chunk_sec_at(a, s), *y = chunk_sec_at(b, s);
        char what[64];

        if (a->sections_ticking[s] != b->sections_ticking[s])
        {
            snprintf(what, sizeof what, "band %d random-tick count", s);
            diff(P_TICKING, r, what, a->sections_ticking[s], b->sections_ticking[s]);
        }
        if ((x == NULL) != (y == NULL))
        {
            snprintf(what, sizeof what, "band %d storage", s);
            diff(P_MASK, r, what, x != NULL, y != NULL);
            continue;
        }
        if (x == NULL) continue;
        for (int i = 0; i < SEC_CELLS; ++i)
            if (chunk_sec_id(x, i) != chunk_sec_id(y, i))
            {
                snprintf(what, sizeof what, "band %d cell %d id", s, i);
                diff(P_IDS, r, what, chunk_sec_id(x, i), chunk_sec_id(y, i));
                break;
            }
        if ((x->ids_hi == NULL) != (y->ids_hi == NULL))
        {
            snprintf(what, sizeof what, "band %d high ids", s);
            diff(P_IDS, r, what, x->ids_hi != NULL, y->ids_hi != NULL);
        }
        static const int part[NIB_N] = {P_METAS, P_SKY, P_BLOCK};
        for (int k = 0; k < NIB_N; ++k)
        {
            const uint8_t *u = chunk_sec_nib(x, k), *v = chunk_sec_nib(y, k);
            if ((x->nib_own >> k & 1) != (y->nib_own >> k & 1))
            {
                snprintf(what, sizeof what, "band %d array held as its own", s);
                diff(part[k], r, what, x->nib_own >> k & 1, y->nib_own >> k & 1);
            }
            for (int i = 0; i < SEC_CELLS; ++i)
                if (nibble_get(u, i) != nibble_get(v, i))
                {
                    snprintf(what, sizeof what, "band %d cell (%d,%d,%d)", s, i >> 8, s << 4 | (i & 15), i >> 4 & 15);
                    diff(part[k], r, what, nibble_get(u, i), nibble_get(v, i));
                    break;
                }
        }
    }
    for (int i = 0; i < 256; ++i)
    {
        if (a->height[i] != b->height[i]) { diff(P_HEIGHT, r, "height map entry", a->height[i], b->height[i]); break; }
        if (a->precip[i] != b->precip[i]) { diff(P_OTHER, r, "precipitation map entry", a->precip[i], b->precip[i]); break; }
        if (a->biome[i] != b->biome[i]) { diff(P_OTHER, r, "biome entry", a->biome[i], b->biome[i]); break; }
    }
    if (a->height_min != b->height_min) diff(P_HEIGHT, r, "heightMapMinimum", a->height_min, b->height_min);
    if (a->queued_light_checks != b->queued_light_checks)
        diff(P_OTHER, r, "queuedLightChecks", a->queued_light_checks, b->queued_light_checks);
    if (a->cx != b->cx || a->cz != b->cz) diff(P_OTHER, r, "position", a->cx, b->cx);
    if (a->terrain_populated != b->terrain_populated || a->light_populated != b->light_populated ||
        a->populated != b->populated || a->no_sky != b->no_sky)
        diff(P_OTHER, r, "flags", a->terrain_populated | a->light_populated << 1 | a->populated << 2 | a->no_sky << 3,
             b->terrain_populated | b->light_populated << 1 | b->populated << 2 | b->no_sky << 3);

    long after = 0;
    for (int p = 0; p < P_N; ++p) after += bad[p];
    return after != before;
}

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

#define CK(x) do { if ((x) != 0) { fprintf(stderr, "FAIL: %s\n", #x); return 2; } } while (0)

int main(int argc, char **argv)
{
    int batch = 1024, negative = 0, nregions = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--batch") && i + 1 < argc) batch = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--negative")) negative = 1;
        else if (argv[i][0] == '-') { fprintf(stderr, "usage: worldgen_built_check [--batch N] [--negative] DUMP_DIR...\n"); return 2; }
        else { add_region(argv[i]); ++nregions; }
    }
    if (batch < 1) batch = 1;
    if (nreq == 0) { fprintf(stderr, "FAIL: no chunks to generate\n"); return 2; }
    qsort(reqs, nreq, sizeof *reqs, req_cmp);
    size_t u = 0;
    for (size_t i = 0; i < nreq; ++i)
        if (u == 0 || req_cmp(&reqs[u - 1], &reqs[i]) != 0) reqs[u++] = reqs[i];
    nreq = u;

    int64_t seeds[64];
    int nseeds = 0;
    for (size_t i = 0; i < nreq; ++i)
        if (nseeds == 0 || seeds[nseeds - 1] != reqs[i].seed)
        {
            if (nseeds == 64) { fprintf(stderr, "FAIL: more than 64 seeds\n"); return 2; }
            seeds[nseeds++] = reqs[i].seed;
        }
    printf("%zu chunks from %d regions, %d seeds, batch %d%s\n", nreq, nregions, nseeds, batch,
           negative ? ", negative control (the sky fill)" : "");

    struct worldgen *g = worldgen_create(batch, nseeds, negative ? WORLDGEN_NEG_BUILT : WORLDGEN_NEG_NONE);
    if (!g) return 2;
    CK(worldgen_set_seeds(g, seeds, nseeds));

    size_t B = (size_t)batch, C = WORLDGEN_CELLS, CB = (size_t)16 * WORLDGEN_BAND_BYTES;
    struct worldgen_req *greq = xmalloc(sizeof *greq * B);
    uint8_t *ids = xmalloc(C * B), *metas = xmalloc(C * B), *gen100 = xmalloc(100 * B), *full = xmalloc(256 * B);
    uint8_t *bands = xmalloc(CB * B);
    struct worldgen_built *hdr = xmalloc(sizeof *hdr * B);
    uint16_t *ids16 = xmalloc(sizeof *ids16 * C);
    struct chunk *a = chunk_new(), *b = chunk_new();
    double dev_s = 0, c_s = 0, built_s = 0;
    long bands_total = 0;

    for (size_t start = 0; start < nreq; start += B)
    {
        int n = (int)(nreq - start < B ? nreq - start : B);
        for (int k = 0; k < n; ++k)
        {
            const struct req *r = &reqs[start + (size_t)k];
            int s = 0;
            while (seeds[s] != r->seed) ++s;
            greq[k] = (struct worldgen_req){s, r->cx, r->cz};
        }
        double t0 = now();
        int top = -1;
        CK(worldgen_submit(g, greq, n));
        CK(worldgen_generate(g));
        CK(worldgen_fetch_built(g, hdr, bands, &top));
        dev_s += now() - t0;
        CK(worldgen_fetch_ids(g, ids));
        CK(worldgen_fetch_metas(g, metas));
        CK(worldgen_fetch_biomes(g, gen100, full));

        for (int k = 0; k < n; ++k)
        {
            const struct req *r = &reqs[start + (size_t)k];
            int bi[256];
            if (top < 0 || (hdr[k].mask != 0 && 31 - __builtin_clz(hdr[k].mask) > top))
            {
                fprintf(stderr, "FAIL: the batch's top band %d is below chunk (%d,%d)'s mask %x\n", top, r->cx, r->cz, hdr[k].mask);
                return 1;
            }
            double c0 = now();
            for (size_t i = 0; i < C; ++i) ids16[i] = ids[C * (size_t)k + i];
            for (int i = 0; i < 256; ++i) bi[i] = full[256 * (size_t)k + (size_t)i];
            chunkgen_construct(a, r->cx, r->cz, ids16, metas + C * (size_t)k, bi);
            double c1 = now();
            genahead_chunk_built(b, r->cx, r->cz, (const struct ga_built *)(const void *)&hdr[k], bands + CB * (size_t)k,
                                 full + 256 * (size_t)k);
            double c2 = now();
            c_s += c1 - c0;
            built_s += c2 - c1;
            bands_total += __builtin_popcount(hdr[k].mask);
            chunks_bad += compare(r, a, b);
        }
    }

    unsigned errs = worldgen_errors(g);
    int fail = errs != 0 || chunks_bad != 0, first_bad = -1;
    for (int p = 0; p < P_N; ++p)
    {
        printf("%-10s %s: %ld differences%s%s\n", part_name[p], bad[p] ? "FAIL" : "PASS", bad[p], bad[p] ? "; first " : "",
               bad[p] ? first[p] : "");
        if (bad[p] && first_bad < 0) first_bad = p;
    }
    printf("device errors: 0x%x; %ld chunks differ; %.1f bands a chunk; device generate and built %.3f s; host per chunk: "
           "C construct and sky map %.1f us, from the built records %.1f us\n",
           errs, chunks_bad, (double)bands_total / (double)nreq, dev_s, c_s * 1e6 / (double)nreq, built_s * 1e6 / (double)nreq);
    chunk_free(a);
    chunk_free(b);
    worldgen_free(g);
    if (negative)
    {
        int ok = first_bad == P_SKY && bad[P_IDS] == 0 && bad[P_METAS] == 0 && bad[P_MASK] == 0 && errs == 0;
        printf("NEGATIVE built: first failing part %s: %s\n", first_bad >= 0 ? part_name[first_bad] : "none",
               ok ? "CAUGHT" : "NOT CAUGHT");
        return ok ? 0 : 1;
    }
    printf("%s %zu chunks\n", fail ? "FAIL" : "PASS", nreq);
    return fail;
}
