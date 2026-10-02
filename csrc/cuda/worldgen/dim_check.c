/* worldgen_dim_check: the CUDA Nether and End generator (dim_*.cu) against the C
 * engine's providers, byte for byte, for every chunk of the given oracle
 * dimension dumps (out/java/dims/<name>: the manifest's dim and region),
 * seed-world dimension spawn areas (out/java/seedworld/<name> with a dim:
 * the chunks the C seed-world build loads there) and, with --platform, the
 * 17x17 chunks around the End's obsidian spawn platform (100, 48, 0) for
 * every End seed listed.
 *
 * Each batch runs the GPU stages one at a time and compares each stage with
 * the C function that computes it, per dimension, so a difference names its
 * dimension and stage:
 *   field     nether_field / end_field (425 or 297 doubles, bits)
 *   terrain   nether_terrain / end_terrain raw ids
 *   surface   nether_surface (provider rand reseeded) / end_surface raw ids
 *   caves     nether_caves raw ids (the Nether only)
 *   final     world.c's load_dim_chunk through world_generate_chunk: the
 *             chunk's ids, metas (all 0) and biome array
 *   generate  the same batch through worldgen_dim_generate (no stage copies)
 *
 *   worldgen_dim_check [--batch N] [--no-stages] [--negative=STAGE] [--platform]
 *               [--store DIR KEY] DIM_DUMP_DIR... [--spawn SEEDWORLD_DIR]...
 * --store keeps C's side in DIR under KEY (ref.h): a later run with the
 * same KEY and chunks runs C only where the device differs.
 * --negative=field|terrain|surface|caves|construct perturbs one GPU constant
 * and passes only when, in each dimension it touches, the first failing stage
 * is the perturbed one, and the other dimension does not fail at all. */
#define _POSIX_C_SOURCE 200809L
#include "worldgen.h"
#include "ref.h"
#include "../../engine/end.h"
#include "../../engine/nether.h"
#include "../../engine/populate_nether.h"
#include "../../engine/seedworld.h"
#include "../../engine/world.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct req { int64_t seed; int dim, cx, cz; };

static struct req *reqs;
static size_t nreq, capreq;

static void add_req(int64_t seed, int dim, int cx, int cz)
{
    if (nreq == capreq)
    {
        capreq = capreq ? capreq * 2 : 4096;
        reqs = realloc(reqs, sizeof *reqs * capreq);
        if (!reqs) { perror("realloc"); exit(2); }
    }
    reqs[nreq++] = (struct req){seed, dim, cx, cz};
}

static int req_cmp(const void *a, const void *b)
{
    const struct req *x = a, *y = b;
    if (x->seed != y->seed) return x->seed < y->seed ? -1 : 1;
    if (x->dim != y->dim) return x->dim < y->dim ? -1 : 1;
    if (x->cx != y->cx) return x->cx < y->cx ? -1 : 1;
    return (x->cz > y->cz) - (x->cz < y->cz);
}

static char *read_manifest(const char *dir)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/manifest.json", dir);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    static char json[1 << 20];
    size_t n = fread(json, 1, sizeof json - 1, f);
    json[n] = 0;
    fclose(f);
    return json;
}

static int has_key(const char *json, const char *key)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    return strstr(json, pat) != NULL;
}

static long long num(const char *json, const char *key)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) { fprintf(stderr, "manifest: no %s\n", key); exit(2); }
    return strtoll(p + strlen(pat), 0, 10);
}

/* the manifest's dim: 0 when absent or null */
static int manifest_dim(const char *m)
{
    const char *p = strstr(m, "\"dim\":");
    if (!p || !strncmp(p + 6, "null", 4)) return 0;
    return (int)strtol(p + 6, 0, 10);
}

static size_t add_region(const char *dir)
{
    const char *m = read_manifest(dir);
    int dim = manifest_dim(m);
    if (dim != -1 && dim != 1)
    {
        printf("dump %s: skipped (not the Nether or the End)\n", dir);
        return 0;
    }
    int64_t seed = num(m, "seed");
    int x0 = (int)num(m, "x0"), z0 = (int)num(m, "z0"), x1 = (int)num(m, "x1"), z1 = (int)num(m, "z1");
    int step = has_key(m, "step") ? (int)num(m, "step") : 1;
    size_t before = nreq;
    for (int cx = x0; cx <= x1; cx += step)
        for (int cz = z0; cz <= z1; cz += step) add_req(seed, dim, cx, cz);
    return nreq - before;
}

static const char *store_dir, *store_key;

static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "out of memory (%zu bytes)\n", n); exit(2); }
    return p;
}

/* The chunks the seed-world build loads in the dimension, as test_seedworld
 * builds it: the overworld's spawn area first, its Det streams handed over,
 * then the dimension's 625 around its spawn point and what population loads. */
static size_t add_spawn(const char *dir)
{
    const char *m = read_manifest(dir);
    int dim = manifest_dim(m);
    if (dim != -1 && dim != 1)
    {
        printf("spawn %s: skipped (not the Nether or the End)\n", dir);
        return 0;
    }
    int64_t seed = num(m, "seed");
    uint64_t *keys;
    size_t n = gr_spawn_load(store_dir, store_key, seed, dim, &keys);
    if (n == 0)
    {
        struct seedworld *ow = calloc(1, sizeof *ow), *sw = calloc(1, sizeof *sw);
        if (!ow || !sw) { perror("calloc"); exit(2); }
        seedworld_init(ow, seed);
        seedworld_build(ow);
        seedworld_init_dim(sw, seed, dim);
        seedworld_move_det(sw, ow);
        seedworld_free(ow);
        seedworld_build_dim(sw);
        const struct world *w = &sw->p.world;
        n = w->lon;
        keys = xmalloc(sizeof *keys * (n ? n : 1));
        for (size_t i = 0; i < n; ++i) keys[i] = (uint64_t)w->load_order[i];
        seedworld_free(sw);
        if (dim == -1) populate_hell_free();
        free(ow);
        free(sw);
        gr_spawn_save(store_dir, store_key, seed, dim, keys, n);
    }
    for (size_t i = 0; i < n; ++i)
        add_req(seed, dim, (int32_t)(uint32_t)(keys[i] & 0xffffffffu), (int32_t)(uint32_t)(keys[i] >> 32));
    free(keys);
    printf("spawn seed %lld dim %d: %zu chunks\n", (long long)seed, dim, n);
    return n;
}

enum { S_FIELD, S_TERRAIN, S_SURFACE, S_CAVES, S_FINAL, S_GENERATE, S_N };
static const char *stage_name[S_N] = {"field", "terrain", "surface", "caves", "final", "generate"};
/* per dimension: 0 the Nether, 1 the End */
static long bad[2][S_N], checked[2][S_N];
static char first[2][S_N][300];
static const char *dim_name[2] = {"nether", "end"};

static void diff(int s, const struct req *r, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void diff(int s, const struct req *r, const char *fmt, ...)
{
    int d = r->dim == 1;
    if (!bad[d][s]++)
    {
        int o = snprintf(first[d][s], sizeof first[d][s], "seed %lld chunk (%d,%d): ", (long long)r->seed, r->cx, r->cz);
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(first[d][s] + o, sizeof first[d][s] - (size_t)o, fmt, ap);
        va_end(ap);
    }
}

static void check_raw(int s, const struct req *r, const uint8_t *gpu, const uint16_t *c)
{
    ++checked[r->dim == 1][s];
    for (int i = 0; i < WORLDGEN_DIM_RAW; ++i)
        if (gpu[i] != c[i])
        {
            diff(s, r, "block (%d,%d,%d) want %d got %d", i >> 11, i & 127, (i >> 7) & 15, c[i], gpu[i]);
            return;
        }
}

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}


#define CK(x) do { if ((x) != 0) { fprintf(stderr, "FAIL: %s\n", #x); return 2; } } while (0)

int main(int argc, char **argv)
{
    int batch = 1024, stages = 1, negative = WORLDGEN_DIM_NEG_NONE, platform = 0;
    size_t from_regions = 0, from_spawn = 0, from_platform = 0;
    int nregions = 0, nspawn = 0, nspawns = 0;
    const char **spawns = xmalloc(sizeof *spawns * (size_t)argc);
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--batch") && i + 1 < argc) batch = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--no-stages")) stages = 0;
        else if (!strcmp(argv[i], "--platform")) platform = 1;
        else if (!strcmp(argv[i], "--spawn") && i + 1 < argc) spawns[nspawns++] = argv[++i];
        else if (!strcmp(argv[i], "--store") && i + 2 < argc) { store_dir = argv[i + 1]; store_key = argv[i + 2]; i += 2; }
        else if (!strncmp(argv[i], "--negative=", 11))
        {
            const char *s = argv[i] + 11;
            negative = !strcmp(s, "field") ? WORLDGEN_DIM_NEG_FIELD : !strcmp(s, "terrain") ? WORLDGEN_DIM_NEG_TERRAIN
                     : !strcmp(s, "surface") ? WORLDGEN_DIM_NEG_SURFACE : !strcmp(s, "caves") ? WORLDGEN_DIM_NEG_CAVES
                     : !strcmp(s, "construct") ? WORLDGEN_DIM_NEG_CONSTRUCT : -1;
            if (negative < 0) { fprintf(stderr, "unknown negative %s\n", s); return 2; }
        }
        else if (argv[i][0] == '-')
        {
            fprintf(stderr, "usage: worldgen_dim_check [--batch N] [--no-stages] [--negative=STAGE] [--platform] [--store DIR KEY] DIM_DUMP_DIR... [--spawn DIR]...\n");
            return 2;
        }
        else
        {
            size_t n = add_region(argv[i]);
            from_regions += n;
            nregions += n != 0;
        }
    }
    /* (after the dumps, as when --spawn was read in place: the store's
     * options may follow it) */
    for (int i = 0; i < nspawns; ++i)
    {
        size_t n = add_spawn(spawns[i]);
        from_spawn += n;
        nspawn += n != 0;
    }
    free(spawns);
    if (platform)
    {
        /* the End seeds listed so far, each once */
        size_t listed = nreq;
        for (size_t i = 0; i < listed; ++i)
        {
            if (reqs[i].dim != 1) continue;
            int seen = 0;
            for (size_t j = 0; j < i && !seen; ++j) seen = reqs[j].dim == 1 && reqs[j].seed == reqs[i].seed;
            if (seen) continue;
            int64_t seed = reqs[i].seed;
            for (int cx = (100 >> 4) - 8; cx <= (100 >> 4) + 8; ++cx)
                for (int cz = -8; cz <= 8; ++cz) add_req(seed, 1, cx, cz);
            from_platform += 17 * 17;
        }
    }
    if (batch < 1) batch = 1;
    if (nreq == 0) { fprintf(stderr, "FAIL: no chunks to generate\n"); return 2; }
    size_t listed = nreq;
    qsort(reqs, nreq, sizeof *reqs, req_cmp);
    size_t u = 0;
    for (size_t i = 0; i < nreq; ++i)
        if (u == 0 || req_cmp(&reqs[u - 1], &reqs[i]) != 0) reqs[u++] = reqs[i];
    nreq = u;
    size_t per_dim[2] = {0, 0};
    for (size_t i = 0; i < nreq; ++i) ++per_dim[reqs[i].dim == 1];

    int64_t seeds[64];
    int nseeds = 0;
    for (size_t i = 0; i < nreq; ++i)
    {
        int s = 0;
        while (s < nseeds && seeds[s] != reqs[i].seed) ++s;
        if (s < nseeds) continue;
        if (nseeds == 64) { fprintf(stderr, "FAIL: more than 64 seeds\n"); return 2; }
        seeds[nseeds++] = reqs[i].seed;
    }
    printf("%zu chunks (%zu Nether, %zu End; %zu listed: %zu from %d dumps, %zu from %d spawn areas, %zu around the "
           "End platform), %d seeds, batch %d%s\n",
           nreq, per_dim[0], per_dim[1], listed, from_regions, nregions, from_spawn, nspawn, from_platform, nseeds,
           batch, stages ? ", staged" : "");

    /* C's stage hashes: field, terrain, surface, caves (staged runs), final */
    enum { GR_NST = 5 };
    struct gr_store ref;
    gr_open(&ref, store_dir, store_key, stages ? "dim-staged" : "dim-final", reqs, sizeof *reqs, nreq, GR_NST);

    struct worldgen_dim *g = worldgen_dim_create(batch, nseeds, negative);
    if (!g) return 2;
    CK(worldgen_dim_set_seeds(g, seeds, nseeds));

    /* the C side per seed: the two providers for the stages, and a world per
     * dimension whose world_generate_chunk is the finished chunk */
    struct nether **nth = xmalloc(sizeof *nth * (size_t)nseeds);
    struct end **end = xmalloc(sizeof *end * (size_t)nseeds);
    struct world **wn = xmalloc(sizeof *wn * (size_t)nseeds), **we = xmalloc(sizeof *we * (size_t)nseeds);
    for (int i = 0; i < nseeds; ++i)
    {
        nth[i] = xmalloc(sizeof **nth);
        nether_init(nth[i], seeds[i]);
        end[i] = xmalloc(sizeof **end);
        end_init(end[i], seeds[i]);
        wn[i] = xmalloc(sizeof **wn);
        world_init(wn[i], seeds[i]);
        wn[i]->dim = -1;
        we[i] = xmalloc(sizeof **we);
        world_init(we[i], seeds[i]);
        we[i]->dim = 1;
    }

    size_t B = (size_t)batch, C = WORLDGEN_CELLS, R = WORLDGEN_DIM_RAW;
    struct worldgen_dim_req *greq = xmalloc(sizeof *greq * B);
    uint8_t *hids = xmalloc(C * B), *hbio = xmalloc(256 * B);
    uint8_t *pids = xmalloc(C * B), *pbio = xmalloc(256 * B);
    double *hfield = NULL;
    uint8_t *hter = NULL, *hsur = NULL, *hcav = NULL;
    uint64_t *hrand = NULL;   /* the provider rand after the surface pass */
    if (stages)
    {
        hfield = xmalloc(sizeof(double) * 425 * B);
        hter = xmalloc(R * B);
        hsur = xmalloc(R * B);
        hrand = xmalloc(sizeof *hrand * B);
        hcav = xmalloc(R * B);
    }
    static uint16_t blocks[WORLDGEN_DIM_RAW], cids[WORLDGEN_CELLS];
    static uint8_t cmetas[WORLDGEN_CELLS], zero_metas[WORLDGEN_CELLS];
    double gpu_s = 0.0, cpu_s = 0.0;
    int batches = 0;

    for (size_t start = 0; start < nreq; start += B)
    {
        size_t n = nreq - start < B ? nreq - start : B;
        const struct req *Q = reqs + start;
        for (size_t k = 0; k < n; ++k)
        {
            int si = 0;
            while (seeds[si] != Q[k].seed) ++si;
            greq[k] = (struct worldgen_dim_req){si, Q[k].dim, Q[k].cx, Q[k].cz};
        }
        CK(worldgen_dim_submit(g, greq, (int)n));
        if (stages)
        {
            CK(worldgen_dim_run(g, WORLDGEN_DIM_DENSITY));
            CK(worldgen_dim_fetch_field(g, hfield));
            CK(worldgen_dim_fetch_raw(g, hter));
            CK(worldgen_dim_run(g, WORLDGEN_DIM_SURFACE));
            CK(worldgen_dim_fetch_raw(g, hsur));
            CK(worldgen_dim_fetch_rand(g, hrand));
            CK(worldgen_dim_run(g, WORLDGEN_DIM_CAVES));
            CK(worldgen_dim_fetch_raw(g, hcav));
            CK(worldgen_dim_run(g, WORLDGEN_DIM_CONSTRUCT));
            CK(worldgen_dim_fetch_chunk(g, hids, hbio));
        }

        /* the product path: every stage back to back, then one copy */
        double t0 = now();
        CK(worldgen_dim_generate(g));
        gpu_s += now() - t0;
        CK(worldgen_dim_fetch_chunk(g, pids, pbio));
        ++batches;

        for (size_t k = 0; k < n; ++k)
        {
            const struct req *r = &Q[k];
            int s = greq[k].seed_index, d = r->dim == 1;
            const uint8_t *fin_ids = (stages ? hids : pids) + C * k, *fin_bio = (stages ? hbio : pbio) + 256 * k;
            uint64_t dh[GR_NST] = {0}, ch_h[GR_NST] = {0};
            if (stages)
            {
                dh[0] = gr_f64(0, hfield + 425 * k, d ? 297 : 425);
                dh[1] = gr_u8(0, hter + R * k, R);
                /* the Nether's provider rand after the pass too */
                dh[2] = gr_u8(gr_u8(0, hsur + R * k, R), (const uint8_t *)&hrand[k], d ? 0 : 8);
                if (!d) dh[3] = gr_u8(0, hcav + R * k, R);
            }
            dh[4] = gr_u8(gr_u8(gr_u8(0, fin_ids, C), zero_metas, C), fin_bio, 256);
            if (gr_match(&ref, start + k, dh))
            {
                /* every stage is C's stored result: no C for this chunk */
                if (stages)
                {
                    ++checked[d][S_FIELD];
                    ++checked[d][S_TERRAIN];
                    ++checked[d][S_SURFACE];
                    if (!d) ++checked[d][S_CAVES];
                }
                ++checked[d][S_FINAL];
                goto product;
            }
            if (stages)
            {
                double fld[425];
                int nf = d ? 297 : 425;
                if (d) end_field(end[s], r->cx * 2, r->cz * 2, fld);
                else nether_field(nth[s], r->cx * 4, r->cz * 4, fld);
                ch_h[0] = gr_f64(0, fld, (size_t)nf);
                ++checked[d][S_FIELD];
                for (int i = 0; i < nf; ++i)
                    if (memcmp(&hfield[425 * k + i], &fld[i], sizeof(double)) != 0)
                    {
                        diff(S_FIELD, r, "sample %d want %a got %a", i, fld[i], hfield[425 * k + i]);
                        break;
                    }
                if (d) end_terrain(end[s], r->cx, r->cz, blocks);
                else nether_terrain(nth[s], r->cx, r->cz, blocks);
                ch_h[1] = gr_u16(0, blocks, R);
                check_raw(S_TERRAIN, r, hter + R * k, blocks);
                if (d) end_surface(r->cx, r->cz, blocks);
                else
                {
                    jr_seed(&nth[s]->rand, (int64_t)((uint64_t)(int64_t)r->cx * 341873128712ULL +
                                                     (uint64_t)(int64_t)r->cz * 132897987541ULL));
                    nether_surface(nth[s], r->cx, r->cz, blocks);
                }
                ch_h[2] = gr_u8(gr_u16(0, blocks, R), (const uint8_t *)&nth[s]->rand.seed, d ? 0 : 8);
                check_raw(S_SURFACE, r, hsur + R * k, blocks);
                if (!d && hrand[k] != nth[s]->rand.seed)
                    diff(S_SURFACE, r, "provider rand want %llx got %llx", (unsigned long long)nth[s]->rand.seed,
                         (unsigned long long)hrand[k]);
                if (!d)
                {
                    nether_caves(seeds[s], r->cx, r->cz, blocks);
                    ch_h[3] = gr_u16(0, blocks, R);
                    check_raw(S_CAVES, r, hcav + R * k, blocks);
                }
            }

            struct world *w = d ? we[s] : wn[s];
            double c0 = now();
            struct chunk *ch = world_generate_chunk(w, r->cx, r->cz);
            cpu_s += now() - c0;
            ++checked[d][S_FINAL];
            for (int i = 0; i < WORLDGEN_CELLS; ++i)
                if (fin_ids[i] != chunk_cell_id(ch, i) || chunk_cell_meta(ch, i) != 0)
                {
                    diff(S_FINAL, r, "cell (%d,%d,%d) want %d:%d got %d:0", i >> 12, i & 255, (i >> 8) & 15,
                         chunk_cell_id(ch, i), chunk_cell_meta(ch, i), fin_ids[i]);
                    break;
                }
            for (int i = 0; i < 256; ++i)
                if (fin_bio[i] != ch->biome[i])
                {
                    diff(S_FINAL, r, "biome (%d,%d) want %d got %d", i & 15, i >> 4, ch->biome[i], fin_bio[i]);
                    break;
                }
            for (int i = 0; i < WORLDGEN_CELLS; ++i)
            {
                cids[i] = (uint16_t)chunk_cell_id(ch, i);
                cmetas[i] = (uint8_t)chunk_cell_meta(ch, i);
            }
            ch_h[4] = gr_u8(gr_u8(gr_u16(0, cids, C), cmetas, C), ch->biome, 256);
            gr_made(&ref, start + k, ch_h);
            world_unload_chunk(w, r->cx, r->cz);

        product:

            if (stages)
            {
                ++checked[d][S_GENERATE];
                if (memcmp(pids + C * k, hids + C * k, C) || memcmp(pbio + 256 * k, hbio + 256 * k, 256))
                    diff(S_GENERATE, r, "worldgen_dim_generate differs from the staged run");
            }
        }
    }

    gr_close(&ref);
    unsigned errs = worldgen_dim_errors(g);
    int fail = errs != 0, first_bad[2] = {-1, -1};
    for (int d = 0; d < 2; ++d)
        for (int s = 0; s < S_N; ++s)
        {
            if (!checked[d][s])
            {
                printf("%-6s %-9s skipped\n", dim_name[d], stage_name[s]);
                continue;
            }
            printf("%-6s %-9s %s: %ld/%ld differ%s%s\n", dim_name[d], stage_name[s], bad[d][s] ? "FAIL" : "PASS",
                   bad[d][s], checked[d][s], bad[d][s] ? "; first " : "", bad[d][s] ? first[d][s] : "");
            if (bad[d][s] && first_bad[d] < 0) first_bad[d] = s;
            fail |= bad[d][s] != 0;
        }
    printf("device errors: 0x%x\n", errs);
    printf("timing (a shared GPU host GPU, not representative): worldgen_dim_generate %.3f s over %d batches, "
           "%.0f chunks/s; C world_generate_chunk one core %.3f s, %.0f chunks/s\n",
           gpu_s, batches, (double)nreq / gpu_s, cpu_s, (double)nreq / cpu_s);
    worldgen_dim_free(g);
    for (int i = 0; i < nseeds; ++i)
    {
        world_free(wn[i]);
        world_free(we[i]);
    }

    if (negative != WORLDGEN_DIM_NEG_NONE)
    {
        /* the stage each negative must be caught at, per dimension (-1: that
         * dimension is untouched and must pass) */
        static const int expect[][2] = {{-1, -1}, {S_FIELD, S_FIELD}, {S_TERRAIN, S_TERRAIN}, {S_SURFACE, -1},
                                        {S_CAVES, -1}, {S_FINAL, S_FINAL}};
        static const char *neg_name[] = {"none", "field", "terrain", "surface", "caves", "construct"};
        int ok = errs == 0;
        for (int d = 0; d < 2; ++d)
        {
            int want = expect[negative][d];
            if (!per_dim[d]) continue;
            printf("NEGATIVE %s %s: first failing stage %s (want %s)\n", neg_name[negative], dim_name[d],
                   first_bad[d] >= 0 ? stage_name[first_bad[d]] : "none", want >= 0 ? stage_name[want] : "none");
            ok &= first_bad[d] == want;
        }
        printf("NEGATIVE %s: %s\n", neg_name[negative], ok ? "CAUGHT" : "NOT CAUGHT");
        return ok ? 0 : 1;
    }
    printf("%s %zu chunks\n", fail ? "FAIL" : "PASS", nreq);
    return fail;
}
